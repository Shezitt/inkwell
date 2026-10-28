// SPDX-License-Identifier: MIT
#include "core/Database.h"
#include "core/NoteStore.h"
#include "core/Schema.h"

#include <QRegularExpression>
#include <QTest>

#include <memory>

using namespace Qt::StringLiterals;
using namespace inkwell;

namespace {

void expectWarning()
{
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u".*"_s));
}

/// Titles of the pages in @p folder, in stored order.
QStringList pageTitles(const NoteStore &store, FolderId folder)
{
    QStringList titles;
    for (const Page &page : store.pages()) {
        if (page.folderId == folder)
            titles.append(page.title);
    }
    return titles;
}

/// Names of the subfolders of @p parent, in stored order.
QStringList folderNames(const NoteStore &store, FolderId parent)
{
    QStringList names;
    for (const Folder &folder : store.folders()) {
        if (folder.parentId == parent)
            names.append(folder.name);
    }
    return names;
}

} // namespace

class TestNoteStore : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<Database> m_db;
    std::unique_ptr<NoteStore> m_store;

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

    void startsEmpty()
    {
        QVERIFY(m_store->folders().isEmpty());
        QVERIFY(m_store->pages().isEmpty());
    }

    void createsNestedFolders()
    {
        const auto work = m_store->createFolder(kNoFolder, u"Work"_s);
        const auto home = m_store->createFolder(kNoFolder, u"Home"_s);
        QVERIFY(work && home);
        const auto projects = m_store->createFolder(work->id, u"Projects"_s);
        QVERIFY(projects);

        QCOMPARE(work->position, 0);
        QCOMPARE(home->position, 1);
        QCOMPARE(projects->position, 0);
        QCOMPARE(projects->parentId, work->id);

        const QList<Folder> all = m_store->folders();
        QCOMPARE(all.size(), 3);
        QVERIFY(all.contains(*work));
        QVERIFY(all.contains(*home));
        QVERIFY(all.contains(*projects));
    }

    void cleansNames()
    {
        const auto folder = m_store->createFolder(kNoFolder, u"  Daily\n  notes  "_s);
        QVERIFY(folder);
        QCOMPARE(folder->name, u"Daily notes"_s);
    }

    void rejectsEmptyNames()
    {
        expectWarning();
        QVERIFY(!m_store->createFolder(kNoFolder, u"   "_s));
        const auto folder = m_store->createFolder(kNoFolder, u"Notes"_s);
        expectWarning();
        QVERIFY(!m_store->createPage(folder->id, u""_s));
        expectWarning();
        QVERIFY(!m_store->renameFolder(folder->id, u"\t"_s));
    }

    void rejectsUnknownParents()
    {
        expectWarning();
        QVERIFY(!m_store->createFolder(1234, u"Orphan"_s));
        expectWarning();
        QVERIFY(!m_store->createPage(1234, u"Orphan"_s));
        QVERIFY(m_store->folders().isEmpty());
        QVERIFY(m_store->pages().isEmpty());
    }

    void renamesFoldersAndPages()
    {
        const auto folder = m_store->createFolder(kNoFolder, u"Old"_s);
        const auto page = m_store->createPage(folder->id, u"Draft"_s);
        QVERIFY(m_store->renameFolder(folder->id, u"New"_s));
        QVERIFY(m_store->renamePage(page->id, u"Final"_s));
        QCOMPARE(m_store->folders().constFirst().name, u"New"_s);
        QCOMPARE(m_store->pages().constFirst().title, u"Final"_s);

        expectWarning();
        QVERIFY(!m_store->renamePage(999, u"Nope"_s));
    }

    void createsPagesInOrder()
    {
        const auto a = m_store->createFolder(kNoFolder, u"A"_s);
        const auto b = m_store->createFolder(kNoFolder, u"B"_s);
        const auto a1 = m_store->createPage(a->id, u"a1"_s);
        const auto a2 = m_store->createPage(a->id, u"a2"_s);
        const auto b1 = m_store->createPage(b->id, u"b1"_s);
        QVERIFY(a1 && a2 && b1);
        QCOMPARE(a1->position, 0);
        QCOMPARE(a2->position, 1);
        QCOMPARE(b1->position, 0);
        QVERIFY(a1->updatedAt > 0);

        const QList<Page> pages = m_store->pages();
        QCOMPARE(pages.size(), 3);
        QCOMPARE(pages.at(0).title, u"a1"_s);
        QCOMPARE(pages.at(1).title, u"a2"_s);
    }

    void newPagesHaveEmptyBodies()
    {
        const auto folder = m_store->createFolder(kNoFolder, u"Notes"_s);
        const auto page = m_store->createPage(folder->id, u"Empty"_s);
        QCOMPARE(m_store->pageBody(page->id), std::optional<QString>(QString()));
    }

    void roundTripsBodies()
    {
        const auto folder = m_store->createFolder(kNoFolder, u"Notes"_s);
        const auto page = m_store->createPage(folder->id, u"Unicode"_s);

        const QString body = u"Hello, wörld! 你好 🌍\n\tindented\n"_s.repeated(10'000);
        QVERIFY(m_store->setPageBody(page->id, body));
        QCOMPARE(m_store->pageBody(page->id).value(), body);
    }

    void settingBodyBumpsUpdatedAt()
    {
        const auto folder = m_store->createFolder(kNoFolder, u"Notes"_s);
        const auto page = m_store->createPage(folder->id, u"Page"_s);
        QTest::qWait(5);
        QVERIFY(m_store->setPageBody(page->id, u"changed"_s));
        QVERIFY(m_store->pages().constFirst().updatedAt > page->updatedAt);
    }

    void bodyOperationsFailForUnknownPages()
    {
        expectWarning();
        QVERIFY(!m_store->pageBody(42));
        expectWarning();
        QVERIFY(!m_store->setPageBody(42, u"lost"_s));
    }

    void setPageBodyJoinsCallerTransaction()
    {
        const auto folder = m_store->createFolder(kNoFolder, u"Notes"_s);
        const auto page = m_store->createPage(folder->id, u"Page"_s);
        {
            Transaction tx(*m_db);
            QVERIFY(m_store->setPageBody(page->id, u"uncommitted"_s));
            // tx rolls back here.
        }
        QCOMPARE(m_store->pageBody(page->id).value(), QString());
    }

    void deletesPages()
    {
        const auto folder = m_store->createFolder(kNoFolder, u"Notes"_s);
        const auto page = m_store->createPage(folder->id, u"Page"_s);
        QVERIFY(m_store->pageExists(page->id));
        QVERIFY(m_store->deletePage(page->id));
        QVERIFY(!m_store->pageExists(page->id));
        QVERIFY(m_store->pages().isEmpty());
        expectWarning();
        QVERIFY(!m_store->pageBody(page->id));
    }

    void deletingFolderRemovesSubtree()
    {
        const auto root = m_store->createFolder(kNoFolder, u"Root"_s);
        const auto keep = m_store->createFolder(kNoFolder, u"Keep"_s);
        const auto child = m_store->createFolder(root->id, u"Child"_s);
        const auto grandchild = m_store->createFolder(child->id, u"Grandchild"_s);
        m_store->createPage(root->id, u"r"_s);
        m_store->createPage(grandchild->id, u"g"_s);
        const auto kept = m_store->createPage(keep->id, u"k"_s);

        QVERIFY(m_store->deleteFolder(root->id));

        QCOMPARE(m_store->folders(), QList<Folder> {*keep});
        QCOMPARE(m_store->pages(), QList<Page> {*kept});
    }

    void reordersPagesWithinFolder()
    {
        const auto folder = m_store->createFolder(kNoFolder, u"F"_s);
        m_store->createPage(folder->id, u"a"_s);
        m_store->createPage(folder->id, u"b"_s);
        const auto c = m_store->createPage(folder->id, u"c"_s);

        QVERIFY(m_store->movePage(c->id, folder->id, 0));
        QCOMPARE(pageTitles(*m_store, folder->id), (QStringList {u"c"_s, u"a"_s, u"b"_s}));
        QVERIFY(m_store->movePage(c->id, folder->id, 1));
        QCOMPARE(pageTitles(*m_store, folder->id), (QStringList {u"a"_s, u"c"_s, u"b"_s}));

        // Positions stay dense, so new pages still go to the end.
        QList<int> positions;
        for (const Page &page : m_store->pages())
            positions.append(page.position);
        QCOMPARE(positions, (QList<int> {0, 1, 2}));
        QCOMPARE(m_store->createPage(folder->id, u"d"_s)->position, 3);
    }

    void movesPagesBetweenFolders()
    {
        const auto from = m_store->createFolder(kNoFolder, u"From"_s);
        const auto to = m_store->createFolder(kNoFolder, u"To"_s);
        const auto moved = m_store->createPage(from->id, u"moved"_s);
        m_store->createPage(from->id, u"stays"_s);
        m_store->createPage(to->id, u"x"_s);
        m_store->createPage(to->id, u"y"_s);
        m_store->setPageBody(moved->id, u"body"_s);

        QVERIFY(m_store->movePage(moved->id, to->id, 1));
        QCOMPARE(pageTitles(*m_store, from->id), QStringList {u"stays"_s});
        QCOMPARE(pageTitles(*m_store, to->id), (QStringList {u"x"_s, u"moved"_s, u"y"_s}));
        QCOMPARE(m_store->pageBody(moved->id), u"body"_s);

        // Out-of-range indexes are clamped.
        QVERIFY(m_store->movePage(moved->id, to->id, 99));
        QCOMPARE(pageTitles(*m_store, to->id), (QStringList {u"x"_s, u"y"_s, u"moved"_s}));
        QVERIFY(m_store->movePage(moved->id, to->id, -5));
        QCOMPARE(pageTitles(*m_store, to->id), (QStringList {u"moved"_s, u"x"_s, u"y"_s}));
    }

    void movesFolders()
    {
        const auto a = m_store->createFolder(kNoFolder, u"A"_s);
        m_store->createFolder(kNoFolder, u"B"_s);
        const auto c = m_store->createFolder(kNoFolder, u"C"_s);
        const auto page = m_store->createPage(c->id, u"inside"_s);

        QVERIFY(m_store->moveFolder(c->id, kNoFolder, 0));
        QCOMPARE(folderNames(*m_store, kNoFolder), (QStringList {u"C"_s, u"A"_s, u"B"_s}));

        // Nesting takes the folder's whole content along.
        QVERIFY(m_store->moveFolder(c->id, a->id, 0));
        QCOMPARE(folderNames(*m_store, kNoFolder), (QStringList {u"A"_s, u"B"_s}));
        QCOMPARE(folderNames(*m_store, a->id), QStringList {u"C"_s});
        QCOMPARE(pageTitles(*m_store, c->id), QStringList {u"inside"_s});
        QVERIFY(m_store->pageExists(page->id));

        // And back out to the top level.
        QVERIFY(m_store->moveFolder(c->id, kNoFolder, 1));
        QCOMPARE(folderNames(*m_store, kNoFolder), (QStringList {u"A"_s, u"C"_s, u"B"_s}));
        QVERIFY(folderNames(*m_store, a->id).isEmpty());
    }

    void refusesToMoveFolderIntoItself()
    {
        const auto root = m_store->createFolder(kNoFolder, u"Root"_s);
        const auto child = m_store->createFolder(root->id, u"Child"_s);
        const auto grandchild = m_store->createFolder(child->id, u"Grandchild"_s);
        const QList<Folder> before = m_store->folders();

        expectWarning();
        QVERIFY(!m_store->moveFolder(root->id, root->id, 0));
        expectWarning();
        QVERIFY(!m_store->moveFolder(root->id, grandchild->id, 0));
        QVERIFY(!m_store->lastError().isEmpty());
        QCOMPARE(m_store->folders(), before);
    }

    void moveFailsForUnknownTargets()
    {
        const auto folder = m_store->createFolder(kNoFolder, u"F"_s);
        const auto page = m_store->createPage(folder->id, u"p"_s);
        const QList<Page> pagesBefore = m_store->pages();
        const QList<Folder> foldersBefore = m_store->folders();

        expectWarning();
        QVERIFY(!m_store->movePage(page->id, 4242, 0));
        expectWarning();
        QVERIFY(!m_store->movePage(page->id, kNoFolder, 0)); // pages need a folder
        expectWarning();
        QVERIFY(!m_store->movePage(4242, folder->id, 0));
        expectWarning();
        QVERIFY(!m_store->moveFolder(folder->id, 4242, 0));
        expectWarning();
        QVERIFY(!m_store->moveFolder(4242, kNoFolder, 0));

        QCOMPARE(m_store->pages(), pagesBefore);
        QCOMPARE(m_store->folders(), foldersBefore);
    }
};

QTEST_GUILESS_MAIN(TestNoteStore)
#include "tst_notestore.moc"
