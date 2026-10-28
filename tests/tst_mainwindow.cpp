// SPDX-License-Identifier: MIT
#include "TestNotebook.h"
#include "core/Session.h"
#include "ui/MainWindow.h"

#include <QTest>

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
