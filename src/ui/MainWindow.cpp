// SPDX-License-Identifier: MIT
#include "MainWindow.h"

#include "EditorArea.h"
#include "EditorView.h"
#include "NavigationPane.h"
#include "PageTreeModel.h"
#include "QuickSwitcher.h"
#include "core/BackgroundWriter.h"

#include <QApplication>
#include <QCloseEvent>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>

using namespace Qt::StringLiterals;

namespace inkwell {

namespace {

/// How long closing the window may wait for queued saves to reach the disk.
constexpr int kCloseFlushTimeoutMs = 5000;
/// Session changes (tabs, cursor, layout) are batched for this long.
constexpr int kSessionSaveDelayMs = 1000;

} // namespace

MainWindow::MainWindow(NoteStore &store, BackgroundWriter &writer, QWidget *parent)
    : QMainWindow(parent)
    , m_writer(writer)
    , m_model(new PageTreeModel(store, this))
    , m_navigation(new NavigationPane(m_model, this))
    , m_editors(new EditorArea(store, writer, this))
    , m_splitter(new QSplitter(Qt::Horizontal, this))
    , m_quickSwitcher(new QuickSwitcher(m_model, this))
    , m_saveStatus(new QLabel(this))
    , m_cursorStatus(new QLabel(this))
    , m_sessionTimer(new QTimer(this))
{
    m_splitter->addWidget(m_navigation);
    m_splitter->addWidget(m_editors);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({260, 840});
    m_splitter->setChildrenCollapsible(false);
    setCentralWidget(m_splitter);

    connect(m_navigation, &NavigationPane::pageActivated, this, &MainWindow::openPage);
    connect(m_quickSwitcher, &QuickSwitcher::pageChosen, this, [this](PageId id) {
        openPage(id);
        m_editors->focusCurrentEditor(); // so typing goes straight into the page
    });
    connect(
        m_navigation, &NavigationPane::newPageNamed, m_editors, &EditorArea::focusCurrentEditor);
    connect(m_model, &PageTreeModel::pageRenamed, m_editors, &EditorArea::renamePage);
    connect(m_model, &PageTreeModel::pageRenamed, this, &MainWindow::updateWindowTitle);
    connect(m_model, &PageTreeModel::pagesRemoved, m_editors, &EditorArea::discardPages);
    connect(m_editors, &EditorArea::currentPageChanged, this, [this](PageId id) {
        m_navigation->revealPage(id);
        updateWindowTitle();
    });
    connect(m_editors, &EditorArea::errorOccurred, this, &MainWindow::showError);

    m_cursorStatus->setContentsMargins(8, 0, 8, 0);
    m_saveStatus->setContentsMargins(8, 0, 8, 0);
    statusBar()->addPermanentWidget(m_cursorStatus);
    statusBar()->addPermanentWidget(m_saveStatus);
    connect(m_editors, &EditorArea::cursorPositionChanged, this, [this](int line, int column) {
        m_cursorStatus->setText(tr("Ln %1, Col %2").arg(line).arg(column));
    });
    connect(m_editors, &EditorArea::currentPageChanged, this,
        [this](PageId id) { m_cursorStatus->setVisible(id != 0); });
    connect(m_editors, &EditorArea::saveStateChanged, this, &MainWindow::updateSaveStatus);
    // Writer signals come from its thread and arrive here queued.
    connect(&m_writer, &BackgroundWriter::pageSaved, this, [this] {
        m_saveFailing = false;
        updateSaveStatus();
    });
    connect(&m_writer, &BackgroundWriter::saveFailed, this, [this] {
        m_saveFailing = true;
        updateSaveStatus();
    });

    // Keep the stored session current, so even a crash reopens the same tabs.
    m_sessionTimer->setSingleShot(true);
    m_sessionTimer->setInterval(kSessionSaveDelayMs);
    connect(
        m_sessionTimer, &QTimer::timeout, this, [this] { m_writer.saveSession(captureSession()); });
    connect(m_editors, &EditorArea::tabsChanged, this, &MainWindow::scheduleSessionSave);
    connect(m_editors, &EditorArea::currentPageChanged, this, &MainWindow::scheduleSessionSave);
    connect(m_editors, &EditorArea::viewStateChanged, this, &MainWindow::scheduleSessionSave);
    connect(
        m_navigation, &NavigationPane::expansionChanged, this, &MainWindow::scheduleSessionSave);
    connect(m_splitter, &QSplitter::splitterMoved, this, &MainWindow::scheduleSessionSave);

    createActions();
    updateWindowTitle();
    updateSaveStatus();
    m_cursorStatus->hide();
    resize(1100, 720);
}

MainWindow::~MainWindow()
{
    // The editors are destroyed with the child widgets, after this destructor,
    // and still hand over unsaved text then, which changes their save state.
    // By that time this object is no longer a MainWindow: stop listening first.
    disconnect(m_editors, nullptr, this, nullptr);
}

void MainWindow::restoreSession(const SessionState &state)
{
    if (!state.windowGeometry.isEmpty())
        restoreGeometry(state.windowGeometry);
    restoreState(state.windowState);
    m_splitter->restoreState(state.splitterState);
    m_sidebarAction->setChecked(state.sidebarVisible);
    m_navigation->setExpandedFolders(state.expandedFolders);

    for (const TabState &tab : state.tabs) {
        if (!m_model->pageIndex(tab.page).isValid())
            continue;
        if (m_editors->openPage(tab.page, m_model->pageTitle(tab.page)))
            m_editors->editor(tab.page)->restoreViewState(tab.cursor, tab.scroll);
    }
    if (state.currentTab >= 0)
        m_editors->setCurrentIndex(state.currentTab);
}

SessionState MainWindow::captureSession() const
{
    SessionState state;
    state.tabs = m_editors->tabStates();
    state.currentTab = m_editors->currentIndex();
    state.expandedFolders = m_navigation->expandedFolders();
    state.sidebarVisible = m_sidebarAction->isChecked();
    state.windowGeometry = saveGeometry();
    state.windowState = saveState();
    state.splitterState = m_splitter->saveState();
    return state;
}

bool MainWindow::saveEverything(int timeoutMs)
{
    m_editors->saveAll();
    m_sessionTimer->stop();
    m_writer.saveSession(captureSession());
    return m_writer.flush(timeoutMs);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (saveEverything(kCloseFlushTimeoutMs)) {
        event->accept();
        return;
    }

    const auto answer = QMessageBox::warning(this, tr("Unsaved changes"),
        tr("Some changes could not be written to disk yet:\n%1\n\nQuit anyway?")
            .arg(m_writer.lastError()),
        QMessageBox::Cancel | QMessageBox::Discard, QMessageBox::Cancel);
    if (answer == QMessageBox::Discard)
        event->accept();
    else
        event->ignore();
}

void MainWindow::moveEvent(QMoveEvent *event)
{
    QMainWindow::moveEvent(event);
    scheduleSessionSave();
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    scheduleSessionSave();
}

void MainWindow::createActions()
{
    QMenu *file = menuBar()->addMenu(tr("&File"));
    file->addAction(m_navigation->newPageAction());
    file->addAction(m_navigation->newFolderAction());
    file->addSeparator();

    QAction *goToPage = file->addAction(QIcon::fromTheme(u"go-jump"_s), tr("&Go to Page…"));
    goToPage->setShortcut(Qt::CTRL | Qt::Key_P);
    connect(goToPage, &QAction::triggered, this, [this] { m_quickSwitcher->popup(this); });
    file->addSeparator();

    // Everything is saved automatically; Ctrl+S just saves right away.
    QAction *save = file->addAction(QIcon::fromTheme(u"document-save"_s), tr("&Save Now"));
    save->setShortcut(QKeySequence::Save);
    connect(save, &QAction::triggered, m_editors, &EditorArea::saveAll);

    QAction *closeTab = file->addAction(QIcon::fromTheme(u"tab-close"_s), tr("&Close Tab"));
    closeTab->setShortcut(QKeySequence::Close);
    connect(closeTab, &QAction::triggered, m_editors, &EditorArea::closeCurrentTab);
    file->addSeparator();

    QAction *quit = file->addAction(QIcon::fromTheme(u"application-exit"_s), tr("&Quit"));
    quit->setShortcut(QKeySequence::Quit);
    quit->setMenuRole(QAction::QuitRole);
    connect(quit, &QAction::triggered, this, &QWidget::close);

    QMenu *view = menuBar()->addMenu(tr("&View"));
    m_sidebarAction = view->addAction(tr("Show &Sidebar"));
    m_sidebarAction->setCheckable(true);
    m_sidebarAction->setChecked(true);
    m_sidebarAction->setShortcut(Qt::CTRL | Qt::Key_B);
    connect(m_sidebarAction, &QAction::toggled, m_navigation, &QWidget::setVisible);
    connect(m_sidebarAction, &QAction::toggled, this, &MainWindow::scheduleSessionSave);
    view->addSeparator();

    QAction *next = view->addAction(tr("&Next Tab"));
    next->setShortcuts(
        {QKeySequence(Qt::CTRL | Qt::Key_Tab), QKeySequence(Qt::CTRL | Qt::Key_PageDown)});
    connect(next, &QAction::triggered, m_editors, &EditorArea::nextTab);
    QAction *previous = view->addAction(tr("&Previous Tab"));
    previous->setShortcuts({QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Tab),
        QKeySequence(Qt::CTRL | Qt::Key_PageUp)});
    connect(previous, &QAction::triggered, m_editors, &EditorArea::previousTab);

    QMenu *help = menuBar()->addMenu(tr("&Help"));
    QAction *about = help->addAction(tr("&About Inkwell"));
    about->setMenuRole(QAction::AboutRole);
    connect(about, &QAction::triggered, this, &MainWindow::showAbout);
    QAction *aboutQt = help->addAction(tr("About &Qt"));
    aboutQt->setMenuRole(QAction::AboutQtRole);
    connect(aboutQt, &QAction::triggered, qApp, &QApplication::aboutQt);
}

void MainWindow::openPage(PageId id)
{
    m_editors->openPage(id, m_model->pageTitle(id));
}

void MainWindow::updateWindowTitle()
{
    const PageId page = m_editors->currentPage();
    const QString app = QApplication::applicationName();
    setWindowTitle(page ? m_model->pageTitle(page) + u" — "_s + app : app);
}

void MainWindow::updateSaveStatus()
{
    if (m_saveFailing) {
        m_saveStatus->setText(tr("⚠ Saving failed, retrying…"));
        m_saveStatus->setToolTip(m_writer.lastError());
    } else if (m_editors->hasUnsavedChanges()) {
        m_saveStatus->setText(tr("Saving…"));
        m_saveStatus->setToolTip({});
    } else {
        m_saveStatus->setText(tr("All changes saved"));
        m_saveStatus->setToolTip({});
    }
}

void MainWindow::scheduleSessionSave()
{
    m_sessionTimer->start();
}

void MainWindow::showError(const QString &message)
{
    QMessageBox::warning(this, QApplication::applicationName(), message);
}

void MainWindow::showAbout()
{
    QMessageBox::about(this, tr("About Inkwell"),
        tr("<h3>Inkwell %1</h3>"
           "<p>A fast, native notes app that never loses what you wrote.</p>"
           "<p><a href=\"https://github.com/Shezitt/inkwell\">github.com/Shezitt/inkwell</a></p>"
           "<p>Released under the MIT License.</p>")
            .arg(QApplication::applicationVersion()));
}

} // namespace inkwell
