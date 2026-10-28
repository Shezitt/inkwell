// SPDX-License-Identifier: MIT
#include "MainWindow.h"

#include "NavigationPane.h"
#include "PageTreeModel.h"

#include <QApplication>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QSplitter>

using namespace Qt::StringLiterals;

namespace inkwell {

MainWindow::MainWindow(NoteStore &store, QWidget *parent)
    : QMainWindow(parent)
    , m_model(new PageTreeModel(store, this))
    , m_navigation(new NavigationPane(m_model, this))
    , m_splitter(new QSplitter(Qt::Horizontal, this))
{
    setWindowTitle(QApplication::applicationName());

    auto *placeholder = new QLabel(tr("Select a page or create a new one."), this);
    placeholder->setAlignment(Qt::AlignCenter);
    placeholder->setEnabled(false);

    m_splitter->addWidget(m_navigation);
    m_splitter->addWidget(placeholder);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({260, 840});
    m_splitter->setChildrenCollapsible(false);
    setCentralWidget(m_splitter);

    createActions();
    resize(1100, 720);
}

MainWindow::~MainWindow() = default;

void MainWindow::createActions()
{
    QMenu *file = menuBar()->addMenu(tr("&File"));
    file->addAction(m_navigation->newPageAction());
    file->addAction(m_navigation->newFolderAction());
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

    QMenu *help = menuBar()->addMenu(tr("&Help"));
    QAction *about = help->addAction(tr("&About Inkwell"));
    about->setMenuRole(QAction::AboutRole);
    connect(about, &QAction::triggered, this, &MainWindow::showAbout);
    QAction *aboutQt = help->addAction(tr("About &Qt"));
    aboutQt->setMenuRole(QAction::AboutQtRole);
    connect(aboutQt, &QAction::triggered, qApp, &QApplication::aboutQt);
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
