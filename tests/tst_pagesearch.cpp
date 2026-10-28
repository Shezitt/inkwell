// SPDX-License-Identifier: MIT
#include "core/Database.h"
#include "core/NoteStore.h"
#include "core/Schema.h"
#include "ui/PageSearch.h"
#include "ui/PageTreeModel.h"

#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QSignalSpy>
#include <QTest>

#include <memory>

using namespace Qt::StringLiterals;
using namespace inkwell;

class TestPageSearch : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<Database> m_db;
    std::unique_ptr<NoteStore> m_store;
    std::unique_ptr<PageTreeModel> m_model;
    std::unique_ptr<QWidget> m_window;
    std::unique_ptr<PageSearch> m_search;
    PageId m_trials = 0;
    PageId m_monday = 0;

    QAbstractItemModel *results() const { return m_search->results()->model(); }
    QString titleAt(int row) const { return results()->index(row, 0).data().toString(); }

private slots:
    void init()
    {
        m_db = std::make_unique<Database>();
        QVERIFY(m_db->open(u":memory:"_s));
        QVERIFY(schema::migrate(*m_db));
        m_store = std::make_unique<NoteStore>(*m_db);
        const auto work = m_store->createFolder(kNoFolder, u"Work"_s);
        const auto robots = m_store->createFolder(work->id, u"Robots"_s);
        m_trials = m_store->createPage(robots->id, u"Robot trials"_s)->id;
        m_monday = m_store->createPage(work->id, u"Monday"_s)->id;
        QVERIFY(m_store->setPageBody(m_monday, u"Talked about the robot gripper."_s));
        m_model = std::make_unique<PageTreeModel>(*m_store);

        m_window = std::make_unique<QWidget>();
        m_window->resize(800, 600);
        m_window->show();
        QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));
        m_search = std::make_unique<PageSearch>(*m_store, m_model.get(), m_window.get());
        m_search->popup(m_window.get());
        QVERIFY(QTest::qWaitForWindowExposed(m_search.get()));
    }

    void cleanup()
    {
        m_search.reset();
        m_window.reset();
        m_model.reset();
        m_store.reset();
        m_db.reset();
    }

    void startsEmpty()
    {
        QVERIFY(m_search->input()->hasFocus());
        QCOMPARE(results()->rowCount(), 0);
        QVERIFY(!m_search->findChild<QLabel *>()->isVisible()); // nothing searched yet
    }

    void findsPagesAsYouType()
    {
        QTest::keyClicks(m_search->input(), u"rob"_s);
        QCOMPARE(results()->rowCount(), 2);
        QCOMPARE(titleAt(0), u"Robot trials"_s); // title matches rank first
        QCOMPARE(titleAt(1), u"Monday"_s);
        QCOMPARE(results()->index(0, 0).data(Qt::ToolTipRole).toString(), u"Work / Robots"_s);

        QTest::keyClicks(m_search->input(), u"ot grip"_s);
        QCOMPARE(results()->rowCount(), 1);
        QCOMPARE(titleAt(0), u"Monday"_s);
    }

    void saysWhenNothingMatches()
    {
        QTest::keyClicks(m_search->input(), u"zebra"_s);
        QCOMPARE(results()->rowCount(), 0);
        QVERIFY(m_search->findChild<QLabel *>()->isVisible());
    }

    void choosesWithTheWordsSearchedFor()
    {
        QSignalSpy chosen(m_search.get(), &PageSearch::pageChosen);
        QTest::keyClicks(m_search->input(), u"robot  gripper"_s);
        QTest::keyClick(m_search->input(), Qt::Key_Return);
        QCOMPARE(chosen.size(), 1);
        QCOMPARE(chosen.constFirst().at(0).value<PageId>(), m_monday);
        QCOMPARE(
            chosen.constFirst().at(1).toStringList(), (QStringList {u"robot"_s, u"gripper"_s}));
        QVERIFY(!m_search->isVisible());
    }
};

QTEST_MAIN(TestPageSearch)
#include "tst_pagesearch.moc"
