// SPDX-License-Identifier: MIT
#include "core/Database.h"
#include "core/NoteStore.h"
#include "core/Schema.h"
#include "core/Session.h"

#include <QTemporaryDir>
#include <QTest>

#include <memory>

using namespace Qt::StringLiterals;
using namespace inkwell;

class TestSession : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<Database> m_db;
    std::unique_ptr<NoteStore> m_store;

    PageId newPage()
    {
        const auto folder = m_store->createFolder(kNoFolder, u"F"_s);
        return m_store->createPage(folder->id, u"P"_s)->id;
    }

private slots:
    void init()
    {
        m_db = std::make_unique<Database>();
        QVERIFY(m_db->open(u":memory:"_s));
        QVERIFY(schema::migrate(*m_db));
        m_store = std::make_unique<NoteStore>(*m_db);
    }

    void cleanup()
    {
        m_store.reset();
        m_db.reset();
    }

    void defaultsWhenNothingSaved()
    {
        const SessionState state = session::load(*m_db);
        QCOMPARE(state, SessionState());
        QVERIFY(state.sidebarVisible);
        QVERIFY(state.markdownHighlighting);
        QCOMPARE(state.currentTab, -1);
    }

    void roundTrips()
    {
        SessionState state;
        state.tabs = {{newPage(), 12, 3}, {newPage(), 0, 0}, {newPage(), 999, 42}};
        state.currentTab = 2;
        state.expandedFolders = {1, 3};
        state.sidebarVisible = false;
        state.markdownHighlighting = false;
        state.windowGeometry = QByteArray("\x01\x02\x00\x03", 4);
        state.windowState = "state";
        state.splitterState = "splitter";

        QVERIFY(session::save(*m_db, state));
        QCOMPARE(session::load(*m_db), state);
    }

    void saveReplacesPreviousSession()
    {
        SessionState first;
        first.tabs = {{newPage(), 1, 1}, {newPage(), 2, 2}};
        first.currentTab = 1;
        QVERIFY(session::save(*m_db, first));

        SessionState second;
        second.tabs = {{first.tabs.at(1).page, 5, 5}};
        second.currentTab = 0;
        QVERIFY(session::save(*m_db, second));
        QCOMPARE(session::load(*m_db), second);
    }

    void dropsTabsOfDeletedPages()
    {
        const PageId kept = newPage();
        const PageId deleted = newPage();
        SessionState state;
        state.tabs = {{kept, 0, 0}, {deleted, 0, 0}};
        state.currentTab = 1;
        QVERIFY(session::save(*m_db, state));

        QVERIFY(m_store->deletePage(deleted));
        const SessionState loaded = session::load(*m_db);
        QCOMPARE(loaded.tabs, (QList<TabState> {{kept, 0, 0}}));
        QCOMPARE(loaded.currentTab, 0); // clamped to the remaining tab
    }

    void skipsTabsOfPagesDeletedBeforeSaving()
    {
        const PageId kept = newPage();
        SessionState state;
        state.tabs = {{424242, 0, 0}, {kept, 7, 0}};
        state.currentTab = 1;
        QVERIFY(session::save(*m_db, state));
        QCOMPARE(session::load(*m_db).tabs, (QList<TabState> {{kept, 7, 0}}));
    }
};

QTEST_GUILESS_MAIN(TestSession)
#include "tst_session.moc"
