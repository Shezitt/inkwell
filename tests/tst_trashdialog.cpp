// SPDX-License-Identifier: MIT
#include "core/Database.h"
#include "core/NoteStore.h"
#include "core/Schema.h"
#include "ui/PageTreeModel.h"
#include "ui/TrashDialog.h"

#include <QApplication>
#include <QMessageBox>
#include <QPushButton>
#include <QTest>
#include <QTimer>
#include <QTreeWidget>

#include <memory>

using namespace Qt::StringLiterals;
using namespace inkwell;

namespace {

/// Answers the next confirmation with @p button once it is shown.
void answerConfirmation(QMessageBox::StandardButton button)
{
    QTimer::singleShot(0, qApp, [button] {
        auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        QVERIFY2(box, "no confirmation was shown");
        box->button(button)->click();
    });
}

} // namespace

class TestTrashDialog : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<Database> m_db;
    std::unique_ptr<NoteStore> m_store;
    std::unique_ptr<PageTreeModel> m_model;
    FolderId m_work = kNoFolder;
    PageId m_plan = 0;

    QStringList rows(const TrashDialog &dialog, int column = 0) const
    {
        QStringList texts;
        for (int i = 0; i < dialog.items()->topLevelItemCount(); ++i)
            texts.append(dialog.items()->topLevelItem(i)->text(column));
        return texts;
    }

    void select(const TrashDialog &dialog, int row)
    {
        dialog.items()->clearSelection();
        dialog.items()->topLevelItem(row)->setSelected(true);
    }

private slots:
    void init()
    {
        m_db = std::make_unique<Database>();
        QVERIFY(m_db->open(u":memory:"_s));
        QVERIFY(schema::migrate(*m_db));
        m_store = std::make_unique<NoteStore>(*m_db);
        const auto notes = m_store->createFolder(kNoFolder, u"Notes"_s);
        m_work = m_store->createFolder(kNoFolder, u"Work"_s)->id;
        m_store->createPage(m_work, u"a"_s);
        m_store->createPage(m_work, u"b"_s);
        m_plan = m_store->createPage(notes->id, u"Plan"_s)->id;
        m_model = std::make_unique<PageTreeModel>(*m_store);
        QVERIFY(m_model->moveToTrash(m_model->folderIndex(m_work)));
        QVERIFY(m_model->moveToTrash(m_model->pageIndex(m_plan)));
    }

    void cleanup()
    {
        m_model.reset();
        m_store.reset();
        m_db.reset();
    }

    void listsWhatIsInTheTrash()
    {
        TrashDialog dialog(*m_store, m_model.get());
        // Most recently trashed first.
        QCOMPARE(rows(dialog), (QStringList {u"Plan"_s, u"Work (2 pages)"_s}));
        QCOMPARE(rows(dialog, 1), (QStringList {u"Notes"_s, u"Top level"_s}));
        QVERIFY(!rows(dialog, 2).constFirst().isEmpty());

        QVERIFY(!dialog.restoreButton()->isEnabled()); // nothing selected
        QVERIFY(!dialog.deleteButton()->isEnabled());
        QVERIFY(dialog.emptyButton()->isEnabled());
        select(dialog, 0);
        QVERIFY(dialog.restoreButton()->isEnabled());
        QVERIFY(dialog.deleteButton()->isEnabled());
    }

    void restoresSelectedItems()
    {
        TrashDialog dialog(*m_store, m_model.get());
        dialog.items()->selectAll();
        dialog.restoreSelected();

        QVERIFY(m_model->pageIndex(m_plan).isValid());
        QVERIFY(m_model->folderIndex(m_work).isValid());
        QCOMPARE(m_model->rowCount(m_model->folderIndex(m_work)), 2);
        QCOMPARE(dialog.items()->topLevelItemCount(), 0);
        QVERIFY(!dialog.emptyButton()->isEnabled());
    }

    void deletesPermanentlyOnlyWhenConfirmed()
    {
        TrashDialog dialog(*m_store, m_model.get());
        select(dialog, 0);

        answerConfirmation(QMessageBox::Cancel);
        dialog.deleteSelected();
        QCOMPARE(dialog.items()->topLevelItemCount(), 2);
        QVERIFY(m_store->pageExists(m_plan));

        select(dialog, 0);
        answerConfirmation(QMessageBox::Yes);
        dialog.deleteSelected();
        QCOMPARE(rows(dialog), QStringList {u"Work (2 pages)"_s});
        QVERIFY(!m_store->pageExists(m_plan));
    }

    void emptiesTheTrash()
    {
        TrashDialog dialog(*m_store, m_model.get());
        answerConfirmation(QMessageBox::Yes);
        dialog.emptyTrash();
        QCOMPARE(dialog.items()->topLevelItemCount(), 0);
        QVERIFY(m_store->trash().isEmpty());
        QVERIFY(m_store->pages().isEmpty());
        QCOMPARE(m_store->folders().size(), 1); // Notes, which was never trashed
    }
};

QTEST_MAIN(TestTrashDialog)
#include "tst_trashdialog.moc"
