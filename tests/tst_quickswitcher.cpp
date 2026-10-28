// SPDX-License-Identifier: MIT
#include "core/Database.h"
#include "core/NoteStore.h"
#include "core/Schema.h"
#include "ui/PageTreeModel.h"
#include "ui/QuickSwitcher.h"

#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QSignalSpy>
#include <QTest>

#include <memory>

using namespace Qt::StringLiterals;
using namespace inkwell;

class TestQuickSwitcher : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<Database> m_db;
    std::unique_ptr<NoteStore> m_store;
    std::unique_ptr<PageTreeModel> m_model;
    std::unique_ptr<QWidget> m_window;
    std::unique_ptr<QuickSwitcher> m_switcher;
    PageId m_feedback = 0;
    PageId m_offset = 0;
    PageId m_plan = 0;

    QStringList shownTitles() const
    {
        QStringList titles;
        const QAbstractItemModel *results = m_switcher->results()->model();
        for (int row = 0; row < results->rowCount(); ++row)
            titles.append(results->index(row, 0).data().toString());
        return titles;
    }

    QString currentTitle() const { return m_switcher->results()->currentIndex().data().toString(); }

private slots:
    void init()
    {
        m_db = std::make_unique<Database>();
        QVERIFY(m_db->open(u":memory:"_s));
        QVERIFY(schema::migrate(*m_db));
        m_store = std::make_unique<NoteStore>(*m_db);
        const auto work = m_store->createFolder(kNoFolder, u"Work"_s);
        const auto robots = m_store->createFolder(work->id, u"Robots"_s);
        m_offset = m_store->createPage(work->id, u"offset"_s)->id;
        m_feedback = m_store->createPage(robots->id, u"Feedback for Sav"_s)->id;
        m_plan = m_store->createPage(work->id, u"Plan"_s)->id;
        m_model = std::make_unique<PageTreeModel>(*m_store);

        m_window = std::make_unique<QWidget>();
        m_window->resize(800, 600);
        m_window->show();
        QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));
        m_switcher = std::make_unique<QuickSwitcher>(m_model.get(), m_window.get());
        m_switcher->popup(m_window.get());
        QVERIFY(QTest::qWaitForWindowExposed(m_switcher.get()));
    }

    void cleanup()
    {
        m_switcher.reset();
        m_window.reset();
        m_model.reset();
        m_store.reset();
        m_db.reset();
    }

    void listsAllPagesInTreeOrderAtFirst()
    {
        QVERIFY(m_switcher->input()->hasFocus());
        QCOMPARE(shownTitles(), (QStringList {u"Feedback for Sav"_s, u"offset"_s, u"Plan"_s}));
        QCOMPARE(currentTitle(), u"Feedback for Sav"_s);
    }

    void filtersAndRanksAsYouType()
    {
        QTest::keyClicks(m_switcher->input(), u"fs"_s);
        // Both match, but "Feedback for Sav" matches at word starts.
        QCOMPARE(shownTitles(), (QStringList {u"Feedback for Sav"_s, u"offset"_s}));

        QTest::keyClicks(m_switcher->input(), u"av"_s);
        QCOMPARE(shownTitles(), QStringList {u"Feedback for Sav"_s});
        const QModelIndex first = m_switcher->results()->model()->index(0, 0);
        QCOMPARE(first.data(Qt::ToolTipRole).toString(), u"Work / Robots"_s);
    }

    void showsWhenNothingMatches()
    {
        QTest::keyClicks(m_switcher->input(), u"zzz"_s);
        QVERIFY(shownTitles().isEmpty());
        QVERIFY(!m_switcher->results()->isVisible());
        QVERIFY(m_switcher->findChild<QLabel *>()->isVisible());

        // Enter with no results does nothing.
        QSignalSpy chosen(m_switcher.get(), &QuickSwitcher::pageChosen);
        QTest::keyClick(m_switcher->input(), Qt::Key_Return);
        QCOMPARE(chosen.size(), 0);
        QVERIFY(m_switcher->isVisible());
    }

    void choosesWithArrowsAndEnter()
    {
        QSignalSpy chosen(m_switcher.get(), &QuickSwitcher::pageChosen);
        QTest::keyClick(m_switcher->input(), Qt::Key_Down);
        QTest::keyClick(m_switcher->input(), Qt::Key_Down);
        QCOMPARE(currentTitle(), u"Plan"_s);
        QTest::keyClick(m_switcher->input(), Qt::Key_Up);
        QCOMPARE(currentTitle(), u"offset"_s);
        QVERIFY(m_switcher->input()->hasFocus()); // arrows never steal focus from the input

        QTest::keyClick(m_switcher->input(), Qt::Key_Return);
        QCOMPARE(chosen.size(), 1);
        QCOMPARE(chosen.constFirst().at(0).value<PageId>(), m_offset);
        QVERIFY(!m_switcher->isVisible());
    }

    void escapeClosesWithoutChoosing()
    {
        QSignalSpy chosen(m_switcher.get(), &QuickSwitcher::pageChosen);
        QTest::keyClicks(m_switcher->input(), u"plan"_s);
        QTest::keyClick(m_switcher->input(), Qt::Key_Escape);
        QVERIFY(!m_switcher->isVisible());
        QCOMPARE(chosen.size(), 0);

        // Reopening starts from scratch and sees pages created meanwhile.
        m_model->createPage(m_model->firstFolder(), u"Brand new"_s);
        m_switcher->popup(m_window.get());
        QVERIFY(m_switcher->input()->text().isEmpty());
        QVERIFY(shownTitles().contains(u"Brand new"_s));
    }

    void choosesWithMouse()
    {
        QSignalSpy chosen(m_switcher.get(), &QuickSwitcher::pageChosen);
        QListView *results = m_switcher->results();
        const QRect third = results->visualRect(results->model()->index(2, 0));
        QTest::mouseClick(results->viewport(), Qt::LeftButton, {}, third.center());
        QCOMPARE(chosen.size(), 1);
        QCOMPARE(chosen.constFirst().at(0).value<PageId>(), m_plan);
    }
};

QTEST_MAIN(TestQuickSwitcher)
#include "tst_quickswitcher.moc"
