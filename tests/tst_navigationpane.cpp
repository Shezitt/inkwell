// SPDX-License-Identifier: MIT
#include "core/Database.h"
#include "core/NoteStore.h"
#include "core/Schema.h"
#include "ui/NavigationPane.h"
#include "ui/PageTreeModel.h"

#include <QAction>
#include <QSignalSpy>
#include <QTest>
#include <QTreeView>

#include <memory>

using namespace Qt::StringLiterals;
using namespace inkwell;

class TestNavigationPane : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<Database> m_db;
    std::unique_ptr<NoteStore> m_store;
    std::unique_ptr<PageTreeModel> m_model;
    std::unique_ptr<NavigationPane> m_pane;

    QStringList pagesIn(FolderId folder)
    {
        QStringList titles;
        for (const Page &page : m_store->pages()) {
            if (page.folderId == folder)
                titles.append(page.title);
        }
        return titles;
    }

    QAction *action(const QString &text)
    {
        for (QAction *candidate : m_pane->view()->actions()) {
            if (candidate->text().remove(u'&') == text)
                return candidate;
        }
        return nullptr;
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
        m_pane.reset();
        m_model.reset();
        m_store.reset();
        m_db.reset();
    }

    void movesSelectionWithKeyboard()
    {
        const auto folder = m_store->createFolder(kNoFolder, u"F"_s);
        m_store->createPage(folder->id, u"a"_s);
        m_store->createPage(folder->id, u"b"_s);
        const auto c = m_store->createPage(folder->id, u"c"_s);
        m_model = std::make_unique<PageTreeModel>(*m_store);
        m_pane = std::make_unique<NavigationPane>(m_model.get());
        m_pane->show();
        QVERIFY(QTest::qWaitForWindowActive(m_pane.get()));

        QTreeView *view = m_pane->view();
        view->setFocus();
        view->setCurrentIndex(m_model->pageIndex(c->id));
        QAction *up = action(u"Move Up"_s);
        QAction *down = action(u"Move Down"_s);
        QVERIFY(up && down);
        QVERIFY(up->isEnabled());
        QVERIFY(!down->isEnabled()); // already last

        QTest::keyClick(view, Qt::Key_Up, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(pagesIn(folder->id), (QStringList {u"a"_s, u"c"_s, u"b"_s}));
        QTest::keyClick(view, Qt::Key_Up, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(pagesIn(folder->id), (QStringList {u"c"_s, u"a"_s, u"b"_s}));

        // The moved page stays selected, and the actions follow its new place.
        QCOMPARE(PageTreeModel::id(view->currentIndex()), c->id);
        QVERIFY(!up->isEnabled());
        QVERIFY(down->isEnabled());

        QTest::keyClick(view, Qt::Key_Down, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(pagesIn(folder->id), (QStringList {u"a"_s, u"c"_s, u"b"_s}));
    }

    void movesSelectionToTrashWithDeleteKey()
    {
        const auto folder = m_store->createFolder(kNoFolder, u"F"_s);
        const auto page = m_store->createPage(folder->id, u"Doomed"_s);
        m_model = std::make_unique<PageTreeModel>(*m_store);
        m_pane = std::make_unique<NavigationPane>(m_model.get());
        m_pane->show();
        QVERIFY(QTest::qWaitForWindowActive(m_pane.get()));
        QTreeView *view = m_pane->view();
        view->setFocus();
        view->setCurrentIndex(m_model->pageIndex(page->id));

        // No confirmation: it can be restored from the trash. A dialog would
        // block here and fail the test.
        QSignalSpy trashed(m_pane.get(), &NavigationPane::movedToTrash);
        QTest::keyClick(view, Qt::Key_Delete);
        QCOMPARE(trashed.size(), 1);
        QCOMPARE(trashed.constFirst().at(0).toString(), u"Doomed"_s);
        QVERIFY(!m_model->pageIndex(page->id).isValid());
        QCOMPARE(m_store->trash().size(), 1);
    }

    void keepsFoldersOpenWhenTheTreeIsRebuilt()
    {
        const auto open = m_store->createFolder(kNoFolder, u"Open"_s);
        const auto closed = m_store->createFolder(kNoFolder, u"Closed"_s);
        const auto inner = m_store->createFolder(open->id, u"Inner"_s);
        m_store->createPage(inner->id, u"p"_s);
        m_store->createPage(closed->id, u"q"_s);
        m_model = std::make_unique<PageTreeModel>(*m_store);
        m_pane = std::make_unique<NavigationPane>(m_model.get());
        m_pane->setExpandedFolders({open->id, inner->id});

        m_model->reload();
        QCOMPARE(m_pane->expandedFolders(), (QList<FolderId> {open->id, inner->id}));
    }

    void enablesDragAndDrop()
    {
        m_model = std::make_unique<PageTreeModel>(*m_store);
        m_pane = std::make_unique<NavigationPane>(m_model.get());
        QCOMPARE(m_pane->view()->dragDropMode(), QAbstractItemView::InternalMove);
        QCOMPARE(m_pane->view()->defaultDropAction(), Qt::MoveAction);
    }
};

QTEST_MAIN(TestNavigationPane)
#include "tst_navigationpane.moc"
