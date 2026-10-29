// SPDX-License-Identifier: MIT
#include "TrashDialog.h"

#include "PageTreeModel.h"
#include "core/NoteStore.h"

#include <QDateTime>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QStyle>
#include <QTreeWidget>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace inkwell {

namespace {

enum Column { NameColumn, LocationColumn, TrashedColumn };
/// Index into the dialog's list of trash items, stored on each row.
constexpr int kIndexRole = Qt::UserRole;

} // namespace

TrashDialog::TrashDialog(NoteStore &store, PageTreeModel *model, QWidget *parent)
    : QDialog(parent)
    , m_store(store)
    , m_model(model)
    , m_stack(new QStackedWidget(this))
    , m_items(new QTreeWidget(this))
    , m_emptyLabel(new QLabel(tr("The trash is empty."), this))
{
    setWindowTitle(tr("Trash"));
    resize(640, 400);

    m_items->setColumnCount(3);
    m_items->setHeaderLabels({tr("Name"), tr("Was in"), tr("Deleted")});
    m_items->setRootIsDecorated(false);
    m_items->setUniformRowHeights(true);
    m_items->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_items->header()->setStretchLastSection(false);
    m_items->header()->setSectionResizeMode(NameColumn, QHeaderView::Stretch);
    m_items->header()->setSectionResizeMode(LocationColumn, QHeaderView::Stretch);
    m_items->header()->setSectionResizeMode(TrashedColumn, QHeaderView::ResizeToContents);
    connect(m_items, &QTreeWidget::itemSelectionChanged, this, &TrashDialog::updateButtons);
    connect(m_items, &QTreeWidget::itemDoubleClicked, this, &TrashDialog::restoreSelected);

    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setEnabled(false); // drawn in the dimmed text colour
    m_stack->addWidget(m_items);
    m_stack->addWidget(m_emptyLabel);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    m_restore = buttons->addButton(tr("&Restore"), QDialogButtonBox::ActionRole);
    m_delete = buttons->addButton(tr("&Delete Permanently"), QDialogButtonBox::ActionRole);
    m_empty = buttons->addButton(tr("&Empty Trash"), QDialogButtonBox::DestructiveRole);
    connect(m_restore, &QPushButton::clicked, this, &TrashDialog::restoreSelected);
    connect(m_delete, &QPushButton::clicked, this, &TrashDialog::deleteSelected);
    connect(m_empty, &QPushButton::clicked, this, &TrashDialog::emptyTrash);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(
        tr("Deleted pages and folders stay here until you delete them permanently."), this));
    layout->addWidget(m_stack, 1);
    layout->addWidget(buttons);

    refresh();
}

void TrashDialog::restoreSelected()
{
    for (const TrashItem &item : selectedItems()) {
        if (!m_model->restoreFromTrash(item.kind, item.id))
            break; // the model reports the error
    }
    refresh();
}

void TrashDialog::deleteSelected()
{
    const QList<TrashItem> items = selectedItems();
    if (items.isEmpty())
        return;
    const QString question = items.size() == 1
        ? tr("Permanently delete “%1”?").arg(items.constFirst().name)
        : tr("Permanently delete the %1 selected items?").arg(items.size());
    if (!confirm(question, tr("Delete Permanently")))
        return;
    for (const TrashItem &item : items) {
        const bool ok = item.kind == TrashItem::Kind::Folder ? m_store.deleteFolder(item.id)
                                                             : m_store.deletePage(item.id);
        if (!ok) {
            QMessageBox::warning(this, tr("Trash"),
                tr("“%1” could not be deleted:\n%2").arg(item.name, m_store.lastError()));
            break;
        }
    }
    refresh();
}

void TrashDialog::emptyTrash()
{
    if (m_trash.isEmpty()
        || !confirm(tr("Permanently delete everything in the trash?"), tr("Empty Trash")))
        return;
    if (!m_store.emptyTrash()) {
        QMessageBox::warning(
            this, tr("Trash"), tr("The trash could not be emptied:\n%1").arg(m_store.lastError()));
    }
    refresh();
}

void TrashDialog::refresh()
{
    m_trash = m_store.trash();
    m_items->clear();
    const QIcon folderIcon = style()->standardIcon(QStyle::SP_DirIcon);
    const QIcon pageIcon = style()->standardIcon(QStyle::SP_FileIcon);
    const QLocale locale;
    for (qsizetype i = 0; i < m_trash.size(); ++i) {
        const TrashItem &item = m_trash.at(i);
        const bool folder = item.kind == TrashItem::Kind::Folder;
        auto *row = new QTreeWidgetItem(m_items);
        QString name = item.name;
        if (folder) {
            name = item.pageCount == 1 ? tr("%1 (1 page)").arg(item.name)
                                       : tr("%1 (%2 pages)").arg(item.name).arg(item.pageCount);
        }
        row->setText(NameColumn, name);
        row->setIcon(NameColumn,
            QIcon::fromTheme(
                folder ? u"folder"_s : u"text-x-generic"_s, folder ? folderIcon : pageIcon));
        row->setText(LocationColumn, item.location.isEmpty() ? tr("Top level") : item.location);
        row->setText(TrashedColumn,
            locale.toString(QDateTime::fromMSecsSinceEpoch(item.trashedAt), QLocale::ShortFormat));
        row->setData(NameColumn, kIndexRole, int(i));
    }
    m_stack->setCurrentWidget(m_trash.isEmpty() ? static_cast<QWidget *>(m_emptyLabel) : m_items);
    updateButtons();
}

void TrashDialog::updateButtons()
{
    const bool selection = !m_items->selectedItems().isEmpty();
    m_restore->setEnabled(selection);
    m_delete->setEnabled(selection);
    m_empty->setEnabled(!m_trash.isEmpty());
}

QList<TrashItem> TrashDialog::selectedItems() const
{
    QList<TrashItem> result;
    for (const QTreeWidgetItem *row : m_items->selectedItems())
        result.append(m_trash.at(row->data(NameColumn, kIndexRole).toInt()));
    return result;
}

bool TrashDialog::confirm(const QString &question, const QString &action)
{
    QMessageBox box(
        QMessageBox::Warning, tr("Trash"), question, QMessageBox::Cancel | QMessageBox::Yes, this);
    box.setInformativeText(tr("This cannot be undone."));
    box.button(QMessageBox::Yes)->setText(action);
    box.setDefaultButton(QMessageBox::Cancel);
    return box.exec() == QMessageBox::Yes;
}

} // namespace inkwell
