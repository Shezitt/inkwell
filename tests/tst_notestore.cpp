// SPDX-License-Identifier: MIT
#include "core/Database.h"
#include "core/NoteStore.h"
#include "core/Schema.h"

#include <QRegularExpression>
#include <QSqlQuery>
#include <QTest>

#include <memory>
#include <tuple>

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

    void searchesTitlesAndBodies()
    {
        const auto folder = m_store->createFolder(kNoFolder, u"F"_s);
        const auto inBody = m_store->createPage(folder->id, u"Monday"_s);
        const auto inTitle = m_store->createPage(folder->id, u"Robot trials"_s);
        m_store->createPage(folder->id, u"Unrelated"_s);
        QVERIFY(m_store->setPageBody(inBody->id, u"The robot dropped the marker."_s));

        const QList<SearchHit> hits = m_store->search(u"robot"_s);
        QCOMPARE(hits.size(), 2);
        // A match in the title outranks one in the body.
        QCOMPARE(hits.at(0).id, inTitle->id);
        QCOMPARE(hits.at(0).title, u"Robot trials"_s);
        QVERIFY(hits.at(0).snippet.isEmpty());
        QCOMPARE(hits.at(1).id, inBody->id);
        QCOMPARE(hits.at(1).snippet, u"The \x02robot\x03 dropped the marker."_s);
    }

    void searchIgnoresCaseAndAccentsAndMatchesPrefixes()
    {
        const auto folder = m_store->createFolder(kNoFolder, u"F"_s);
        const auto page = m_store->createPage(folder->id, u"Tema 5"_s);
        QVERIFY(m_store->setPageBody(page->id, u"Cómo funciona la Caché del procesador"_s));

        QCOMPARE(m_store->search(u"cache"_s).size(), 1);
        QCOMPARE(m_store->search(u"COMO"_s).size(), 1);
        QCOMPARE(m_store->search(u"proc"_s).size(), 1); // still typing "procesador"
        QCOMPARE(m_store->search(u"caché procesador"_s).size(), 1);
        // Only the last word is a prefix; earlier ones are complete words.
        QVERIFY(m_store->search(u"proc cache"_s).isEmpty());
    }

    void searchRequiresEveryWord()
    {
        const auto folder = m_store->createFolder(kNoFolder, u"F"_s);
        const auto both = m_store->createPage(folder->id, u"a"_s);
        const auto one = m_store->createPage(folder->id, u"b"_s);
        QVERIFY(m_store->setPageBody(both->id, u"gripper opens wide"_s));
        QVERIFY(m_store->setPageBody(one->id, u"gripper closes"_s));

        const QList<SearchHit> hits = m_store->search(u"  wide   gripper "_s);
        QCOMPARE(hits.size(), 1);
        QCOMPARE(hits.constFirst().id, both->id);
    }

    void searchTakesAnyTextLiterally()
    {
        const auto folder = m_store->createFolder(kNoFolder, u"F"_s);
        const auto page = m_store->createPage(folder->id, u"Query syntax"_s);
        QVERIFY(m_store->setPageBody(page->id, u"say \"hello\" OR not, title: yes"_s));

        // None of these may reach FTS5 as syntax, which would fail with a warning.
        QTest::failOnWarning(QRegularExpression(u".*"_s));
        for (const QString &text :
            {u"\""_s, u"\"hello"_s, u"*"_s, u"OR"_s, u"hello OR"_s, u"NEAR(hello"_s, u"title:"_s,
                u"title: yes"_s, u"-"_s, u"^hello"_s, u"(("_s, u"hello)"_s, u"   "_s, u""_s}) {
            std::ignore = m_store->search(text);
        }
        QCOMPARE(m_store->search(u"\"hello\""_s).size(), 1);
        QCOMPARE(m_store->search(u"OR not"_s).size(), 1);
        QVERIFY(m_store->search(u"-"_s).isEmpty());
    }

    void searchHonoursLimitAndSeesNewText()
    {
        const auto folder = m_store->createFolder(kNoFolder, u"F"_s);
        for (int i = 0; i < 5; ++i) {
            const auto page = m_store->createPage(folder->id, u"Page %1"_s.arg(i));
            QVERIFY(m_store->setPageBody(page->id, u"common word"_s));
        }
        QCOMPARE(m_store->search(u"common"_s, 3).size(), 3);
        QCOMPARE(m_store->search(u"common"_s).size(), 5);

        const auto page = m_store->createPage(folder->id, u"Fresh"_s);
        QVERIFY(m_store->search(u"zebra"_s).isEmpty());
        QVERIFY(m_store->setPageBody(page->id, u"a zebra"_s));
        QCOMPARE(m_store->search(u"zebra"_s).size(), 1);
        QVERIFY(m_store->renamePage(page->id, u"Stale"_s));
        QCOMPARE(m_store->search(u"zebra"_s).constFirst().title, u"Stale"_s);
        QVERIFY(m_store->deletePage(page->id));
        QVERIFY(m_store->search(u"zebra"_s).isEmpty());
    }

    void trashedPagesDisappearButKeepTheirText()
    {
        const auto work = m_store->createFolder(kNoFolder, u"Work"_s);
        const auto projects = m_store->createFolder(work->id, u"Projects"_s);
        const auto page = m_store->createPage(projects->id, u"Plan"_s);
        const auto kept = m_store->createPage(projects->id, u"Kept"_s);
        QVERIFY(m_store->setPageBody(page->id, u"secret roadmap"_s));

        QVERIFY(m_store->trashPage(page->id));
        QCOMPARE(m_store->pages(), QList<Page> {*kept});
        QVERIFY(m_store->search(u"roadmap"_s).isEmpty());
        QCOMPARE(m_store->pageBody(page->id), u"secret roadmap"_s);
        QVERIFY(m_store->pageExists(page->id));
        // Edits still waiting to be written when it was trashed are kept.
        QVERIFY(m_store->setPageBody(page->id, u"secret roadmap v2"_s));

        const QList<TrashItem> trash = m_store->trash();
        QCOMPARE(trash.size(), 1);
        QCOMPARE(trash.constFirst().kind, TrashItem::Kind::Page);
        QCOMPARE(trash.constFirst().id, page->id);
        QCOMPARE(trash.constFirst().name, u"Plan"_s);
        QCOMPARE(trash.constFirst().location, u"Work / Projects"_s);
        QVERIFY(trash.constFirst().trashedAt > 0);

        expectWarning();
        QVERIFY(!m_store->trashPage(page->id)); // already in the trash
    }

    void trashingAFolderHidesEverythingInIt()
    {
        const auto work = m_store->createFolder(kNoFolder, u"Work"_s);
        const auto home = m_store->createFolder(kNoFolder, u"Home"_s);
        const auto sub = m_store->createFolder(work->id, u"Sub"_s);
        m_store->createPage(work->id, u"top"_s);
        const auto deep = m_store->createPage(sub->id, u"deep"_s);
        m_store->createPage(home->id, u"home page"_s);
        QVERIFY(m_store->setPageBody(deep->id, u"needle"_s));

        QVERIFY(m_store->trashFolder(work->id));
        QCOMPARE(m_store->folders(), QList<Folder> {*home});
        QCOMPARE(m_store->pages().size(), 1);
        QVERIFY(m_store->search(u"needle"_s).isEmpty());

        // Only the folder is listed, with the pages that come back with it.
        const QList<TrashItem> trash = m_store->trash();
        QCOMPARE(trash.size(), 1);
        QCOMPARE(trash.constFirst().kind, TrashItem::Kind::Folder);
        QCOMPARE(trash.constFirst().name, u"Work"_s);
        QVERIFY(trash.constFirst().location.isEmpty());
        QCOMPARE(trash.constFirst().pageCount, 2);

        QVERIFY(m_store->restore(TrashItem::Kind::Folder, work->id));
        QCOMPARE(m_store->folders().size(), 3);
        QCOMPARE(m_store->pages().size(), 3);
        QCOMPARE(m_store->search(u"needle"_s).constFirst().id, deep->id);
        QVERIFY(m_store->trash().isEmpty());
    }

    void restoresAtTheEndOfTheFolder()
    {
        const auto folder = m_store->createFolder(kNoFolder, u"F"_s);
        const auto a = m_store->createPage(folder->id, u"a"_s);
        m_store->createPage(folder->id, u"b"_s);
        m_store->createPage(folder->id, u"c"_s);

        QVERIFY(m_store->trashPage(a->id));
        QCOMPARE(pageTitles(*m_store, folder->id), (QStringList {u"b"_s, u"c"_s}));
        QVERIFY(m_store->restore(TrashItem::Kind::Page, a->id));
        QCOMPARE(pageTitles(*m_store, folder->id), (QStringList {u"b"_s, u"c"_s, u"a"_s}));

        expectWarning();
        QVERIFY(!m_store->restore(TrashItem::Kind::Page, a->id)); // not in the trash
        expectWarning();
        QVERIFY(!m_store->restore(TrashItem::Kind::Folder, 4242));
    }

    void restoringAPageBringsBackItsTrashedFolders()
    {
        const auto work = m_store->createFolder(kNoFolder, u"Work"_s);
        const auto sub = m_store->createFolder(work->id, u"Sub"_s);
        const auto page = m_store->createPage(sub->id, u"page"_s);
        const auto other = m_store->createPage(sub->id, u"other"_s);
        QVERIFY(m_store->trashPage(other->id));
        QVERIFY(m_store->trashPage(page->id));
        QVERIFY(m_store->trashFolder(work->id));
        QCOMPARE(m_store->trash().size(), 3);

        QVERIFY(m_store->restore(TrashItem::Kind::Page, page->id));
        QCOMPARE(folderNames(*m_store, kNoFolder), QStringList {u"Work"_s});
        QCOMPARE(folderNames(*m_store, work->id), QStringList {u"Sub"_s});
        // The page that was trashed on its own stays in the trash.
        QCOMPARE(pageTitles(*m_store, sub->id), QStringList {u"page"_s});
        QCOMPARE(m_store->trash().size(), 1);
        QCOMPARE(m_store->trash().constFirst().id, other->id);
    }

    void movesIgnoreTrashedSiblings()
    {
        // Positions are where items appear; trashed ones do not take a place.
        const auto folder = m_store->createFolder(kNoFolder, u"F"_s);
        const auto a = m_store->createPage(folder->id, u"a"_s);
        const auto b = m_store->createPage(folder->id, u"b"_s);
        m_store->createPage(folder->id, u"c"_s);
        m_store->createPage(folder->id, u"d"_s);
        QVERIFY(m_store->trashPage(b->id));

        QVERIFY(m_store->movePage(a->id, folder->id, 1));
        QCOMPARE(pageTitles(*m_store, folder->id), (QStringList {u"c"_s, u"a"_s, u"d"_s}));
    }

    void emptyingTheTrashDeletesForGood()
    {
        const auto keep = m_store->createFolder(kNoFolder, u"Keep"_s);
        const auto gone = m_store->createFolder(kNoFolder, u"Gone"_s);
        const auto inside = m_store->createPage(gone->id, u"inside"_s);
        const auto trashed = m_store->createPage(keep->id, u"trashed"_s);
        const auto kept = m_store->createPage(keep->id, u"kept"_s);
        QVERIFY(m_store->setPageBody(inside->id, u"word"_s));
        QVERIFY(m_store->trashFolder(gone->id));
        QVERIFY(m_store->trashPage(trashed->id));

        QVERIFY(m_store->emptyTrash());
        QVERIFY(m_store->trash().isEmpty());
        QVERIFY(!m_store->pageExists(inside->id));
        QVERIFY(!m_store->pageExists(trashed->id));
        QCOMPARE(m_store->folders(), QList<Folder> {*keep});
        QCOMPARE(m_store->pages(), QList<Page> {*kept});
        QSqlQuery query(m_db->connection());
        QVERIFY(query.exec(u"SELECT COUNT(*) FROM page_search"_s) && query.next());
        QCOMPARE(query.value(0).toInt(), 1); // the index lets go of them too
    }
};

QTEST_GUILESS_MAIN(TestNoteStore)
#include "tst_notestore.moc"
