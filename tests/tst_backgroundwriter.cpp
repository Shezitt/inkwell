// SPDX-License-Identifier: MIT
#include "core/BackgroundWriter.h"
#include "core/Database.h"
#include "core/NoteStore.h"
#include "core/Schema.h"
#include "core/Session.h"

#include <QFile>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include <memory>
#include <vector>

using namespace Qt::StringLiterals;
using namespace inkwell;

class TestBackgroundWriter : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<Database> m_db;
    std::unique_ptr<NoteStore> m_store;
    FolderId m_folder = kNoFolder;

    QString dbPath() const { return m_dir->filePath(u"notes.sqlite3"_s); }

    PageId newPage(const QString &title = u"Page"_s)
    {
        const auto page = m_store->createPage(m_folder, title);
        return page ? page->id : 0;
    }

private slots:
    void init()
    {
        m_dir = std::make_unique<QTemporaryDir>();
        QVERIFY(m_dir->isValid());
        m_db = std::make_unique<Database>();
        QVERIFY(m_db->open(dbPath()));
        QVERIFY(schema::migrate(*m_db));
        m_store = std::make_unique<NoteStore>(*m_db);
        m_folder = m_store->createFolder(kNoFolder, u"Notes"_s)->id;
    }

    void cleanup()
    {
        m_store.reset();
        m_db.reset();
        m_dir.reset();
    }

    void failsToStartWhenDatabaseCannotBeOpened()
    {
        // A regular file where a directory is expected makes mkpath() fail.
        QFile blocker(m_dir->filePath(u"blocker"_s));
        QVERIFY(blocker.open(QIODevice::WriteOnly));
        blocker.close();

        BackgroundWriter writer;
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"cannot create directory"_s));
        QVERIFY(!writer.start(blocker.fileName() + u"/db.sqlite3"_s));
        QVERIFY(!writer.isRunning());
        QVERIFY(!writer.lastError().isEmpty());
    }

    void savesPageBodies()
    {
        const PageId page = newPage();
        BackgroundWriter writer;
        QVERIFY(writer.start(dbPath()));
        QVERIFY(writer.isRunning());

        writer.savePage(page, u"first draft"_s);
        QVERIFY(writer.flush(5000));
        QCOMPARE(m_store->pageBody(page).value(), u"first draft"_s);
    }

    void reportsDurableSavesWithTickets()
    {
        const PageId page = newPage();
        BackgroundWriter writer;
        QSignalSpy saved(&writer, &BackgroundWriter::pageSaved);
        QVERIFY(writer.start(dbPath()));

        const quint64 first = writer.savePage(page, u"a"_s);
        const quint64 second = writer.savePage(page, u"b"_s);
        QVERIFY(second > first);

        // Signals arrive through the event loop, after the write is durable.
        QTRY_VERIFY(!saved.isEmpty() && saved.constLast().at(1).toULongLong() == second);
        QCOMPARE(saved.constLast().at(0).value<PageId>(), page);
        QCOMPARE(m_store->pageBody(page).value(), u"b"_s);
    }

    void latestSaveWins()
    {
        const PageId page = newPage();
        BackgroundWriter writer;
        QVERIFY(writer.start(dbPath()));
        for (int i = 0; i < 500; ++i)
            writer.savePage(page, u"revision %1"_s.arg(i));
        QVERIFY(writer.flush(5000));
        QCOMPARE(m_store->pageBody(page).value(), u"revision 499"_s);
    }

    void savesOnlyTheLatestSession()
    {
        const PageId page = newPage();
        BackgroundWriter writer;
        QVERIFY(writer.start(dbPath()));

        SessionState older;
        older.tabs = {{page, 1, 0}};
        older.currentTab = 0;
        SessionState newer = older;
        newer.tabs.first().cursor = 99;
        newer.sidebarVisible = false;

        writer.saveSession(older);
        writer.savePage(page, u"body"_s);
        writer.saveSession(newer);
        QVERIFY(writer.flush(5000));

        QCOMPARE(session::load(*m_db), newer);
        QCOMPARE(m_store->pageBody(page).value(), u"body"_s);
    }

    void dropsSavesForDeletedPages()
    {
        const PageId deleted = newPage(u"Deleted"_s);
        const PageId kept = newPage(u"Kept"_s);
        QVERIFY(m_store->deletePage(deleted));

        BackgroundWriter writer;
        QSignalSpy failed(&writer, &BackgroundWriter::saveFailed);
        QVERIFY(writer.start(dbPath()));

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"no page with id"_s));
        writer.savePage(deleted, u"too late"_s);
        writer.savePage(kept, u"safe"_s);
        QVERIFY(writer.flush(5000));

        QCOMPARE(m_store->pageBody(kept).value(), u"safe"_s);
        QCoreApplication::processEvents();
        QVERIFY(failed.isEmpty());
    }

    void stopWritesEverythingQueued()
    {
        const PageId page = newPage();
        {
            BackgroundWriter writer;
            QVERIFY(writer.start(dbPath()));
            writer.savePage(page, u"written on shutdown"_s);
            writer.stop();
            QVERIFY(!writer.isRunning());
        }
        QCOMPARE(m_store->pageBody(page).value(), u"written on shutdown"_s);
    }

    void destructorStopsAndFlushes()
    {
        const PageId page = newPage();
        {
            BackgroundWriter writer;
            QVERIFY(writer.start(dbPath()));
            writer.savePage(page, u"saved by destructor"_s);
        }
        QCOMPARE(m_store->pageBody(page).value(), u"saved by destructor"_s);
    }

    void flushFailsWhenNotRunning()
    {
        BackgroundWriter writer;
        QVERIFY(writer.flush(0)); // nothing queued
        writer.savePage(newPage(), u"never written"_s);
        QVERIFY(!writer.flush(10));
    }

    void canRestartAfterStop()
    {
        const PageId page = newPage();
        BackgroundWriter writer;
        QVERIFY(writer.start(dbPath()));
        writer.stop();
        QVERIFY(writer.start(dbPath()));
        writer.savePage(page, u"after restart"_s);
        QVERIFY(writer.flush(5000));
        QCOMPARE(m_store->pageBody(page).value(), u"after restart"_s);
    }

    void acceptsSavesFromManyThreads()
    {
        constexpr int kThreads = 4;
        constexpr int kSavesPerThread = 250;

        std::vector<PageId> pages;
        for (int i = 0; i < kThreads; ++i)
            pages.push_back(newPage());

        BackgroundWriter writer;
        QVERIFY(writer.start(dbPath()));

        std::vector<std::unique_ptr<QThread>> threads;
        for (int t = 0; t < kThreads; ++t) {
            threads.emplace_back(QThread::create([&writer, page = pages[t]] {
                for (int i = 0; i < kSavesPerThread; ++i)
                    writer.savePage(page, u"page %1 rev %2"_s.arg(page).arg(i));
            }));
            threads.back()->start();
        }
        for (auto &thread : threads)
            QVERIFY(thread->wait(10'000));

        QVERIFY(writer.flush(10'000));
        for (const PageId page : pages)
            QCOMPARE(m_store->pageBody(page).value(),
                u"page %1 rev %2"_s.arg(page).arg(kSavesPerThread - 1));
    }
};

QTEST_GUILESS_MAIN(TestBackgroundWriter)
#include "tst_backgroundwriter.moc"
