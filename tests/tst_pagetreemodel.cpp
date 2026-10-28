// SPDX-License-Identifier: MIT
#include "core/Database.h"
#include "core/NoteStore.h"
#include "core/Schema.h"
#include "ui/PageTreeModel.h"

#include <QAbstractItemModelTester>
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
