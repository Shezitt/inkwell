// SPDX-License-Identifier: MIT
#include "core/Database.h"
#include "core/NoteStore.h"
#include "core/Schema.h"
#include "ui/PageTreeModel.h"

#include <QAbstractItemModelTester>
#include <QMimeData>
#include <QSignalSpy>
#include <QTest>

#include <algorithm>
#include <memory>

using namespace Qt::StringLiterals;
using namespace inkwell;

class TestPageTreeModel : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<Database> m_db;
    std::unique_ptr<NoteStore> m_store;
    std::unique_ptr<PageTreeModel> m_model;
    std::unique_ptr<QAbstractItemModelTester> m_tester;

    void createModel()
    {
        m_tester.reset();
        m_model = std::make_unique<PageTreeModel>(*m_store);
        // Checks every structural change against the QAbstractItemModel contract.
        m_tester = std::make_unique<QAbstractItemModelTester>(
            m_model.get(), QAbstractItemModelTester::FailureReportingMode::QtTest);
    }

    QString nameAt(const QModelIndex &index) { return index.data().toString(); }

    /// Names of the children of @p parent, in row order.
    QStringList namesUnder(const QModelIndex &parent = {})
    {
        QStringList names;
        for (int row = 0; row < m_model->rowCount(parent); ++row)
            names.append(nameAt(m_model->index(row, 0, parent)));
        return names;
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
        m_tester.reset();
        m_model.reset();
        m_store.reset();
        m_db.reset();
    }

    void loadsExistingTree()
    {
        const auto work = m_store->createFolder(kNoFolder, u"Work"_s);
        const auto home = m_store->createFolder(kNoFolder, u"Home"_s);
        const auto sub = m_store->createFolder(work->id, u"Projects"_s);
        m_store->createPage(work->id, u"Todo"_s);
        m_store->createPage(sub->id, u"Inkwell"_s);
        createModel();

        QCOMPARE(m_model->rowCount(), 2);
        const QModelIndex workIndex = m_model->index(0, 0);
        QCOMPARE(nameAt(workIndex), u"Work"_s);
        QCOMPARE(nameAt(m_model->index(1, 0)), u"Home"_s);
        QVERIFY(m_model->isFolder(workIndex));
        QCOMPARE(PageTreeModel::id(workIndex), work->id);

        // Subfolders come before pages.
        QCOMPARE(m_model->rowCount(workIndex), 2);
        QCOMPARE(nameAt(m_model->index(0, 0, workIndex)), u"Projects"_s);
        const QModelIndex todo = m_model->index(1, 0, workIndex);
        QCOMPARE(nameAt(todo), u"Todo"_s);
        QVERIFY(m_model->isPage(todo));
        QCOMPARE(PageTreeModel::kind(todo), PageTreeModel::Kind::Page);
        QVERIFY(m_model->flags(todo).testFlag(Qt::ItemNeverHasChildren));
        QCOMPARE(m_model->parent(todo), workIndex);
        QCOMPARE(m_model->containingFolder(todo), work->id);
        QCOMPARE(m_model->firstFolder(), work->id);

        QCOMPARE(m_model->rowCount(m_model->folderIndex(home->id)), 0);
    }

    void loadsChildrenOfLaterParents()
    {
        // A child folder whose parent has a higher id, as after future "move" support.
        const auto a = m_store->createFolder(kNoFolder, u"A"_s);
        const auto b = m_store->createFolder(kNoFolder, u"B"_s);
        QVERIFY(
            m_db->exec(u"UPDATE folders SET parent_id = %1 WHERE id = %2"_s.arg(b->id).arg(a->id)));
        createModel();

        QCOMPARE(m_model->rowCount(), 1);
        const QModelIndex bIndex = m_model->index(0, 0);
        QCOMPARE(nameAt(bIndex), u"B"_s);
        QCOMPARE(nameAt(m_model->index(0, 0, bIndex)), u"A"_s);
    }

    void createsFoldersBeforePages()
    {
        createModel();
        const QModelIndex notes = m_model->createFolder(kNoFolder, u"Notes"_s);
        QVERIFY(notes.isValid());
        const FolderId notesId = PageTreeModel::id(notes);

        const QModelIndex page = m_model->createPage(notesId, u"Page"_s);
        const QModelIndex sub = m_model->createFolder(notesId, u"Sub"_s);
        QVERIFY(page.isValid() && sub.isValid());

        const QModelIndex parent = m_model->folderIndex(notesId);
        QCOMPARE(nameAt(m_model->index(0, 0, parent)), u"Sub"_s);
        QCOMPARE(nameAt(m_model->index(1, 0, parent)), u"Page"_s);

        // The store agrees with the model.
        QCOMPARE(m_store->folders().size(), 2);
        QCOMPARE(m_store->pages().size(), 1);
    }

    void renamesAndNotifies()
    {
        createModel();
        const FolderId folder = PageTreeModel::id(m_model->createFolder(kNoFolder, u"F"_s));
        const QModelIndex page = m_model->createPage(folder, u"Old"_s);
        const PageId pageId = PageTreeModel::id(page);

        QSignalSpy renamed(m_model.get(), &PageTreeModel::pageRenamed);
        QVERIFY(m_model->setData(page, u"  New  title "_s));
        QCOMPARE(nameAt(m_model->pageIndex(pageId)), u"New title"_s);
        QCOMPARE(m_model->pageTitle(pageId), u"New title"_s);
        QCOMPARE(renamed.size(), 1);
        QCOMPARE(renamed.constFirst().at(1).toString(), u"New title"_s);
        QCOMPARE(m_store->pages().constFirst().title, u"New title"_s);

        // Empty or unchanged names are ignored.
        QVERIFY(!m_model->setData(page, u"   "_s));
        QVERIFY(!m_model->setData(page, u"New title"_s));
        QCOMPARE(renamed.size(), 1);
    }

    void removesPage()
    {
        createModel();
        const FolderId folder = PageTreeModel::id(m_model->createFolder(kNoFolder, u"F"_s));
        const PageId page = PageTreeModel::id(m_model->createPage(folder, u"P"_s));

        QSignalSpy removed(m_model.get(), &PageTreeModel::pagesRemoved);
        QVERIFY(m_model->remove(m_model->pageIndex(page)));
        QVERIFY(!m_model->pageIndex(page).isValid());
        QCOMPARE(removed.size(), 1);
        QCOMPARE(removed.constFirst().at(0).value<QList<PageId>>(), QList<PageId> {page});
        QVERIFY(m_store->pages().isEmpty());
    }

    void removesFolderSubtree()
    {
        createModel();
        const FolderId root = PageTreeModel::id(m_model->createFolder(kNoFolder, u"Root"_s));
        const FolderId keep = PageTreeModel::id(m_model->createFolder(kNoFolder, u"Keep"_s));
        const FolderId child = PageTreeModel::id(m_model->createFolder(root, u"Child"_s));
        const PageId p1 = PageTreeModel::id(m_model->createPage(root, u"p1"_s));
        const PageId p2 = PageTreeModel::id(m_model->createPage(child, u"p2"_s));
        const PageId kept = PageTreeModel::id(m_model->createPage(keep, u"kept"_s));

        const QModelIndex rootIndex = m_model->folderIndex(root);
        QList<PageId> under = m_model->pagesUnder(rootIndex);
        std::sort(under.begin(), under.end());
        QCOMPARE(under, (QList<PageId> {p1, p2}));

        QSignalSpy removed(m_model.get(), &PageTreeModel::pagesRemoved);
        QVERIFY(m_model->remove(rootIndex));

        QCOMPARE(m_model->rowCount(), 1);
        QVERIFY(!m_model->folderIndex(child).isValid());
        QVERIFY(!m_model->pageIndex(p2).isValid());
        QVERIFY(m_model->pageIndex(kept).isValid());
        QCOMPARE(removed.size(), 1);
        QCOMPARE(m_model->firstFolder(), keep);

        // A new top-level folder lands after the remaining one.
        m_model->createFolder(kNoFolder, u"New"_s);
        QCOMPARE(nameAt(m_model->index(1, 0)), u"New"_s);
    }

    void movesPagesWithinFolder()
    {
        createModel();
        const FolderId folder = PageTreeModel::id(m_model->createFolder(kNoFolder, u"F"_s));
        m_model->createFolder(folder, u"Sub"_s);
        const PageId a = PageTreeModel::id(m_model->createPage(folder, u"a"_s));
        m_model->createPage(folder, u"b"_s);
        const PageId c = PageTreeModel::id(m_model->createPage(folder, u"c"_s));
        const QModelIndex parent = m_model->folderIndex(folder);

        QVERIFY(m_model->move(m_model->pageIndex(c), folder, 0));
        QCOMPARE(namesUnder(parent), (QStringList {u"Sub"_s, u"c"_s, u"a"_s, u"b"_s}));
        QVERIFY(m_model->move(m_model->pageIndex(a), folder, 2));
        QCOMPARE(namesUnder(parent), (QStringList {u"Sub"_s, u"c"_s, u"b"_s, u"a"_s}));

        // Moving to where it already is changes nothing.
        QSignalSpy moved(m_model.get(), &QAbstractItemModel::rowsMoved);
        QVERIFY(m_model->move(m_model->pageIndex(a), folder, 2));
        QCOMPARE(moved.size(), 0);

        // The order is stored.
        createModel();
        QCOMPARE(namesUnder(m_model->folderIndex(folder)),
            (QStringList {u"Sub"_s, u"c"_s, u"b"_s, u"a"_s}));
    }

    void movesPagesAndFoldersBetweenFolders()
    {
        createModel();
        const FolderId from = PageTreeModel::id(m_model->createFolder(kNoFolder, u"From"_s));
        const FolderId to = PageTreeModel::id(m_model->createFolder(kNoFolder, u"To"_s));
        m_model->createFolder(to, u"ToSub"_s);
        m_model->createPage(to, u"x"_s);
        const PageId page = PageTreeModel::id(m_model->createPage(from, u"page"_s));
        const FolderId sub = PageTreeModel::id(m_model->createFolder(from, u"Sub"_s));
        m_model->createPage(sub, u"deep"_s);

        // A page keeps coming after the subfolders, whatever position it asks for.
        const QPersistentModelIndex pageIndex = m_model->pageIndex(page);
        QVERIFY(m_model->move(pageIndex, to, 0));
        QCOMPARE(
            namesUnder(m_model->folderIndex(to)), (QStringList {u"ToSub"_s, u"page"_s, u"x"_s}));
        QCOMPARE(pageIndex, QPersistentModelIndex(m_model->pageIndex(page)));
        QCOMPARE(m_model->containingFolder(pageIndex), to);

        // A folder takes its content along, and goes among the subfolders.
        QVERIFY(m_model->move(m_model->folderIndex(sub), to, 5));
        QCOMPARE(namesUnder(m_model->folderIndex(to)),
            (QStringList {u"ToSub"_s, u"Sub"_s, u"page"_s, u"x"_s}));
        QCOMPARE(namesUnder(m_model->folderIndex(sub)), QStringList {u"deep"_s});
        QVERIFY(namesUnder(m_model->folderIndex(from)).isEmpty());

        // Up to the top level, first.
        QVERIFY(m_model->move(m_model->folderIndex(sub), kNoFolder, 0));
        QCOMPARE(namesUnder(), (QStringList {u"Sub"_s, u"From"_s, u"To"_s}));
        QCOMPARE(m_model->firstFolder(), sub);

        createModel();
        QCOMPARE(namesUnder(), (QStringList {u"Sub"_s, u"From"_s, u"To"_s}));
        QCOMPARE(
            namesUnder(m_model->folderIndex(to)), (QStringList {u"ToSub"_s, u"page"_s, u"x"_s}));
    }

    void refusesImpossibleMoves()
    {
        createModel();
        const FolderId root = PageTreeModel::id(m_model->createFolder(kNoFolder, u"Root"_s));
        const FolderId child = PageTreeModel::id(m_model->createFolder(root, u"Child"_s));
        const PageId page = PageTreeModel::id(m_model->createPage(root, u"page"_s));
        QSignalSpy moved(m_model.get(), &QAbstractItemModel::rowsMoved);
        QSignalSpy errors(m_model.get(), &PageTreeModel::errorOccurred);

        QVERIFY(!m_model->move(m_model->pageIndex(page), kNoFolder, 0));
        QVERIFY(!m_model->move(m_model->folderIndex(root), root, 0));
        QVERIFY(!m_model->move(m_model->folderIndex(root), child, 0));
        QVERIFY(!m_model->move(m_model->pageIndex(page), 4242, 0));
        QVERIFY(!m_model->move({}, root, 0));
        QCOMPARE(moved.size(), 0);
        QCOMPARE(errors.size(), 0); // all rejected before reaching the store
        QCOMPARE(namesUnder(m_model->folderIndex(root)), (QStringList {u"Child"_s, u"page"_s}));
    }

    void listsPagesWithFolderPaths()
    {
        const auto work = m_store->createFolder(kNoFolder, u"Work"_s);
        const auto projects = m_store->createFolder(work->id, u"Projects"_s);
        const auto home = m_store->createFolder(kNoFolder, u"Home"_s);
        const auto todo = m_store->createPage(work->id, u"Todo"_s);
        const auto plan = m_store->createPage(projects->id, u"Plan"_s);
        const auto list = m_store->createPage(home->id, u"List"_s);
        createModel();

        const QList<PageTreeModel::PageEntry> entries = m_model->pageEntries();
        QCOMPARE(entries.size(), 3);
        // Tree order: a folder's subfolders (and their pages) come before its own pages.
        QCOMPARE(entries.at(0).id, plan->id);
        QCOMPARE(entries.at(0).title, u"Plan"_s);
        QCOMPARE(entries.at(0).folderPath, u"Work / Projects"_s);
        QCOMPARE(entries.at(1).id, todo->id);
        QCOMPARE(entries.at(1).folderPath, u"Work"_s);
        QCOMPARE(entries.at(2).id, list->id);
        QCOMPARE(entries.at(2).folderPath, u"Home"_s);
    }

    void shiftsItemsWithinTheirFolder()
    {
        createModel();
        const FolderId a = PageTreeModel::id(m_model->createFolder(kNoFolder, u"A"_s));
        const FolderId b = PageTreeModel::id(m_model->createFolder(kNoFolder, u"B"_s));
        const FolderId sub = PageTreeModel::id(m_model->createFolder(a, u"Sub"_s));
        const PageId p1 = PageTreeModel::id(m_model->createPage(a, u"p1"_s));
        const PageId p2 = PageTreeModel::id(m_model->createPage(a, u"p2"_s));
        const QModelIndex aIndex = m_model->folderIndex(a);

        // The first page cannot move up past the subfolders, nor the last one down.
        QVERIFY(!m_model->canShift(m_model->pageIndex(p1), -1));
        QVERIFY(!m_model->canShift(m_model->pageIndex(p2), 1));
        QVERIFY(!m_model->canShift(m_model->folderIndex(sub), 1));
        QVERIFY(!m_model->canShift(m_model->pageIndex(p1), 0));
        QVERIFY(!m_model->canShift({}, 1));
        QVERIFY(!m_model->shift(m_model->pageIndex(p2), 1));

        QVERIFY(m_model->canShift(m_model->pageIndex(p2), -1));
        QVERIFY(m_model->shift(m_model->pageIndex(p2), -1));
        QCOMPARE(namesUnder(aIndex), (QStringList {u"Sub"_s, u"p2"_s, u"p1"_s}));

        QVERIFY(m_model->shift(m_model->folderIndex(b), -1));
        QCOMPARE(namesUnder(), (QStringList {u"B"_s, u"A"_s}));

        createModel();
        QCOMPARE(namesUnder(), (QStringList {u"B"_s, u"A"_s}));
        QCOMPARE(namesUnder(m_model->folderIndex(a)), (QStringList {u"Sub"_s, u"p2"_s, u"p1"_s}));
    }

    void dragsAndDropsItems()
    {
        createModel();
        const FolderId a = PageTreeModel::id(m_model->createFolder(kNoFolder, u"A"_s));
        const FolderId b = PageTreeModel::id(m_model->createFolder(kNoFolder, u"B"_s));
        const PageId p1 = PageTreeModel::id(m_model->createPage(a, u"p1"_s));
        m_model->createPage(a, u"p2"_s);
        m_model->createPage(a, u"p3"_s);
        const QModelIndex aIndex = m_model->folderIndex(a);
        const QModelIndex bIndex = m_model->folderIndex(b);

        QVERIFY(m_model->flags(m_model->pageIndex(p1)).testFlag(Qt::ItemIsDragEnabled));
        QVERIFY(!m_model->flags(m_model->pageIndex(p1)).testFlag(Qt::ItemIsDropEnabled));
        QVERIFY(m_model->flags(aIndex).testFlag(Qt::ItemIsDropEnabled));
        QVERIFY(m_model->flags({}).testFlag(Qt::ItemIsDropEnabled));

        auto drag = [this](const QModelIndex &index) {
            return std::unique_ptr<QMimeData>(m_model->mimeData({index}));
        };

        // Between p2 and p3: rows count from before the move.
        std::unique_ptr<QMimeData> data = drag(m_model->pageIndex(p1));
        QVERIFY(m_model->canDropMimeData(data.get(), Qt::MoveAction, 2, 0, aIndex));
        QVERIFY(m_model->dropMimeData(data.get(), Qt::MoveAction, 2, 0, aIndex));
        QCOMPARE(namesUnder(aIndex), (QStringList {u"p2"_s, u"p1"_s, u"p3"_s}));

        // Onto a folder: appended there.
        data = drag(m_model->pageIndex(p1));
        QVERIFY(m_model->dropMimeData(data.get(), Qt::MoveAction, -1, -1, bIndex));
        QCOMPARE(namesUnder(bIndex), QStringList {u"p1"_s});

        // A folder between top-level folders, and into another folder.
        data = drag(bIndex);
        QVERIFY(m_model->dropMimeData(data.get(), Qt::MoveAction, 0, 0, {}));
        QCOMPARE(namesUnder(), (QStringList {u"B"_s, u"A"_s}));
        data = drag(m_model->folderIndex(b));
        QVERIFY(m_model->dropMimeData(data.get(), Qt::MoveAction, -1, -1, m_model->folderIndex(a)));
        QCOMPARE(namesUnder(), QStringList {u"A"_s});
        QCOMPARE(namesUnder(m_model->folderIndex(a)), (QStringList {u"B"_s, u"p2"_s, u"p3"_s}));
    }

    void refusesImpossibleDrops()
    {
        createModel();
        const FolderId root = PageTreeModel::id(m_model->createFolder(kNoFolder, u"Root"_s));
        const FolderId child = PageTreeModel::id(m_model->createFolder(root, u"Child"_s));
        const PageId page = PageTreeModel::id(m_model->createPage(root, u"page"_s));
        const std::unique_ptr<QMimeData> pageData(m_model->mimeData({m_model->pageIndex(page)}));
        const std::unique_ptr<QMimeData> rootData(m_model->mimeData({m_model->folderIndex(root)}));
        const QModelIndex childIndex = m_model->folderIndex(child);

        QVERIFY(!m_model->canDropMimeData(pageData.get(), Qt::MoveAction, 0, 0, {}));
        QVERIFY(!m_model->canDropMimeData(rootData.get(), Qt::MoveAction, -1, -1, childIndex));
        QVERIFY(!m_model->canDropMimeData(
            rootData.get(), Qt::MoveAction, -1, -1, m_model->folderIndex(root)));
        QVERIFY(!m_model->canDropMimeData(
            pageData.get(), Qt::MoveAction, -1, -1, m_model->pageIndex(page)));
        QVERIFY(!m_model->canDropMimeData(pageData.get(), Qt::CopyAction, -1, -1, childIndex));
        QMimeData foreign;
        foreign.setText(u"page"_s);
        QVERIFY(!m_model->canDropMimeData(&foreign, Qt::MoveAction, -1, -1, childIndex));
        QVERIFY(!m_model->dropMimeData(rootData.get(), Qt::MoveAction, -1, -1, childIndex));
        QVERIFY(!m_model->dropMimeData(&foreign, Qt::MoveAction, -1, -1, childIndex));

        // A page deleted while it was being dragged cannot be dropped.
        QVERIFY(m_model->remove(m_model->pageIndex(page)));
        QVERIFY(!m_model->dropMimeData(pageData.get(), Qt::MoveAction, -1, -1, childIndex));
        QCOMPARE(namesUnder(m_model->folderIndex(root)), QStringList {u"Child"_s});
    }

    void reportsStoreErrors()
    {
        createModel();
        QSignalSpy errors(m_model.get(), &PageTreeModel::errorOccurred);
        QVERIFY(!m_model->createPage(12345, u"Nowhere"_s).isValid()); // unknown folder
        QCOMPARE(errors.size(), 0); // rejected before reaching the store

        const QModelIndex folder = m_model->createFolder(kNoFolder, u"F"_s);
        // Delete behind the model's back so the store call fails.
        QVERIFY(m_store->deleteFolder(PageTreeModel::id(folder)));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u".*"_s));
        QVERIFY(!m_model->rename(folder, u"Renamed"_s));
        QCOMPARE(errors.size(), 1);
    }
};

QTEST_MAIN(TestPageTreeModel)
#include "tst_pagetreemodel.moc"
