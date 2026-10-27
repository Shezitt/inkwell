// SPDX-License-Identifier: MIT
#include "MainWindow.h"

#include "EditorArea.h"
#include "NavigationPane.h"
#include "PageTreeModel.h"
#include "core/BackgroundWriter.h"

#include <QApplication>
#include <QCloseEvent>
#include <QMenuBar>
#include <QMessageBox>
#include <QSplitter>

using namespace Qt::StringLiterals;

namespace inkwell {

namespace {

/// How long closing the window may wait for queued saves to reach the disk.
constexpr int kCloseFlushTimeoutMs = 5000;

} // namespace

MainWindow::MainWindow(NoteStore &store, BackgroundWriter &writer, QWidget *parent)
    : QMainWindow(parent)
    , m_writer(writer)
    , m_model(new PageTreeModel(store, this))
    , m_navigation(new NavigationPane(m_model, this))
    , m_editors(new EditorArea(store, writer, this))
    , m_splitter(new QSplitter(Qt::Horizontal, this))
{
    m_splitter->addWidget(m_navigation);
    m_splitter->addWidget(m_editors);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({260, 840});
    m_splitter->setChildrenCollapsible(false);
    setCentralWidget(m_splitter);

    connect(m_navigation, &NavigationPane::pageActivated, this, &MainWindow::openPage);
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

    createActions();
    updateWindowTitle();
    resize(1100, 720);
}

MainWindow::~MainWindow() = default;

void MainWindow::closeEvent(QCloseEvent *event)
{
    m_editors->saveAll();
    if (m_writer.flush(kCloseFlushTimeoutMs)) {
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

void MainWindow::createActions()
{
    QMenu *file = menuBar()->addMenu(tr("&File"));
    file->addAction(m_navigation->newPageAction());
    file->addAction(m_navigation->newFolderAction());
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
    QAction *sidebar = view->addAction(tr("Show &Sidebar"));
    sidebar->setCheckable(true);
    sidebar->setChecked(true);
    sidebar->setShortcut(Qt::CTRL | Qt::Key_B);
    connect(sidebar, &QAction::toggled, m_navigation, &QWidget::setVisible);
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
