// SPDX-License-Identifier: MIT
#include "TestNotebook.h"
#include "core/Session.h"
#include "ui/EditorArea.h"
#include "ui/EditorView.h"
#include "ui/MainWindow.h"
#include "ui/NavigationPane.h"
#include "ui/PageSearch.h"
#include "ui/PageTreeModel.h"
#include "ui/QuickSwitcher.h"

#include <QApplication>
#include <QLineEdit>
#include <QListView>
#include <QStatusBar>
#include <QTest>
#include <QTreeView>

using namespace Qt::StringLiterals;
using namespace inkwell;

class TestMainWindow : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<TestNotebook> m_notebook;

private slots:
    void init()
    {
        m_notebook = std::make_unique<TestNotebook>();
        QVERIFY(m_notebook->open());
    }

    void cleanup() { m_notebook.reset(); }

    void restoresTabsFromSession()
    {
        const PageId a = m_notebook->newPage(u"alpha"_s);
        const PageId b = m_notebook->newPage(u"beta"_s);

        SessionState state;
        state.tabs = {{a, 3, 0}, {b, 1, 0}};
        state.currentTab = 1;
        state.expandedFolders = {m_notebook->folder};
        state.sidebarVisible = false;

        MainWindow window(*m_notebook->store, *m_notebook->writer);
        window.restoreSession(state);

        const SessionState captured = window.captureSession();
        QCOMPARE(captured.tabs, state.tabs);
        QCOMPARE(captured.currentTab, 1);
        QVERIFY(!captured.sidebarVisible);
        QVERIFY(!captured.windowGeometry.isEmpty());
    }

    void showsExistingFoldersAndPagesOnStartup()
    {
        // Everything already exists when the window is built, as on every
        // start after the first. Each row must take up space in the sidebar.
        const PageId page = m_notebook->newPage();
        MainWindow window(*m_notebook->store, *m_notebook->writer);
        SessionState state;
        state.expandedFolders = {m_notebook->folder};
        window.restoreSession(state);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        const QTreeView *view = window.findChild<NavigationPane *>()->view();
        const QModelIndex folder = view->model()->index(0, 0);
        const QModelIndex pageRow = view->model()->index(0, 0, folder);
        QCOMPARE(pageRow.data(PageTreeModel::IdRole).toLongLong(), page);
        QVERIFY(view->visualRect(folder).height() > 0);
        QVERIFY(view->visualRect(pageRow).top() >= view->visualRect(folder).bottom());
    }

    void opensPagesFromTheQuickSwitcher()
    {
        m_notebook->newPage(u"first"_s);
        const auto target = m_notebook->store->createPage(m_notebook->folder, u"Roadmap"_s);
        MainWindow window(*m_notebook->store, *m_notebook->writer);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));

        QTest::keyClick(&window, Qt::Key_P, Qt::ControlModifier);
        auto *switcher = window.findChild<QuickSwitcher *>();
        QVERIFY(switcher->isVisible());
        QTest::keyClicks(switcher->input(), u"road"_s);
        QTest::keyClick(switcher->input(), Qt::Key_Return);

        QVERIFY(!switcher->isVisible());
        QCOMPARE(window.captureSession().tabs.size(), 1);
        QCOMPARE(window.captureSession().tabs.constFirst().page, target->id);
        QVERIFY(window.windowTitle().startsWith(u"Roadmap"_s));
        // Ready to type into the page.
        QTRY_VERIFY(qobject_cast<EditorView *>(QApplication::focusWidget()));
    }

    void closesCleanlyWithUnsavedEdits()
    {
        // As when saving fails and the user closes anyway: the editors still
        // hand over their text while the window is being destroyed.
        const PageId page = m_notebook->newPage();
        SessionState state;
        state.tabs = {{page, 0, 0}};
        state.currentTab = 0;
        auto window = std::make_unique<MainWindow>(*m_notebook->store, *m_notebook->writer);
        window->restoreSession(state);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window.get()));
        QTest::keyClicks(window->findChild<EditorArea *>()->editor(page), u"unsaved"_s);

        window.reset();
        QVERIFY(m_notebook->writer->flush(5000));
        QCOMPARE(m_notebook->storedBody(page), u"unsaved"_s);
    }

    void searchesTextJustTypedAndJumpsToIt()
    {
        m_notebook->newPage(u"nothing here"_s);
        const PageId page = m_notebook->newPage(u"first line\n"_s);
        SessionState state;
        state.tabs = {{page, 0, 0}};
        state.currentTab = 0;
        MainWindow window(*m_notebook->store, *m_notebook->writer);
        window.restoreSession(state);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));

        // Typed a moment ago into an editor without focus, so neither the
        // autosave timer nor losing focus has saved it yet.
        window.findChild<NavigationPane *>()->view()->setFocus();
        EditorView *editor = window.findChild<EditorArea *>()->editor(page);
        editor->moveCursor(QTextCursor::End);
        QTest::keyClicks(editor, u"the zebra crossed"_s);
        QVERIFY(editor->hasUnsavedChanges());

        QTest::keyClick(&window, Qt::Key_F, Qt::ControlModifier | Qt::ShiftModifier);
        auto *search = window.findChild<PageSearch *>();
        QVERIFY(search->isVisible());
        QTest::keyClicks(search->input(), u"zebra"_s);
        QCOMPARE(search->results()->model()->rowCount(), 1);
        QTest::keyClick(search->input(), Qt::Key_Return);

        QCOMPARE(window.captureSession().tabs.constFirst().page, page);
        QCOMPARE(editor->textCursor().selectedText(), u"zebra"_s);
        QTRY_COMPARE(QApplication::focusWidget(), editor);
    }

    void trashingAnOpenPageKeepsItsUnsavedText()
    {
        const PageId page = m_notebook->newPage(u"saved"_s);
        SessionState state;
        state.tabs = {{page, 0, 0}};
        state.currentTab = 0;
        MainWindow window(*m_notebook->store, *m_notebook->writer);
        window.restoreSession(state);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        EditorView *editor = window.findChild<EditorArea *>()->editor(page);
        editor->moveCursor(QTextCursor::End);
        QTest::keyClicks(editor, u" and unsaved"_s);

        auto *navigation = window.findChild<NavigationPane *>();
        navigation->revealPage(page);
        navigation->trashSelected();

        QVERIFY(window.captureSession().tabs.isEmpty());
        QVERIFY(window.statusBar()->currentMessage().contains(u"Trash"_s));
        QVERIFY(m_notebook->writer->flush(5000));
        QCOMPARE(m_notebook->storedBody(page), u"saved and unsaved"_s);
    }

    void skipsTabsOfMissingPages()
    {
        const PageId kept = m_notebook->newPage();
        SessionState state;
        state.tabs = {{4242, 0, 0}, {kept, 0, 0}};
        state.currentTab = 0;

        MainWindow window(*m_notebook->store, *m_notebook->writer);
        window.restoreSession(state);
        QCOMPARE(window.captureSession().tabs, (QList<TabState> {{kept, 0, 0}}));
    }

    void persistsSessionWithoutClosing()
    {
        // No clean shutdown: the session must reach the disk on its own, so a
        // crash still reopens the same tabs.
        const PageId page = m_notebook->newPage(u"text"_s);
        SessionState state;
        state.tabs = {{page, 2, 0}};
        state.currentTab = 0;

        MainWindow window(*m_notebook->store, *m_notebook->writer);
        window.restoreSession(state);

        QTRY_COMPARE_WITH_TIMEOUT(session::load(m_notebook->db).tabs, state.tabs, 5000);
    }
};

QTEST_MAIN(TestMainWindow)
#include "tst_mainwindow.moc"
