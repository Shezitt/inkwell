// SPDX-License-Identifier: MIT
#pragma once

#include "core/Types.h"

#include <QDialog>
#include <QList>

class QLabel;
class QPushButton;
class QStackedWidget;
class QTreeWidget;

namespace inkwell {

class NoteStore;
class PageTreeModel;

/// Lists what is in the trash and restores or permanently deletes it.
class TrashDialog : public QDialog
{
    Q_OBJECT

public:
    TrashDialog(NoteStore &store, PageTreeModel *model, QWidget *parent = nullptr);

    [[nodiscard]] QTreeWidget *items() const { return m_items; }
    [[nodiscard]] QPushButton *restoreButton() const { return m_restore; }
    [[nodiscard]] QPushButton *deleteButton() const { return m_delete; }
    [[nodiscard]] QPushButton *emptyButton() const { return m_empty; }

public slots:
    /// Puts the selected items back where they were.
    void restoreSelected();
    /// Permanently deletes the selected items, after asking.
    void deleteSelected();
    /// Permanently deletes everything in the trash, after asking.
    void emptyTrash();

private:
    void refresh();
    void updateButtons();
    [[nodiscard]] QList<TrashItem> selectedItems() const;
    bool confirm(const QString &question, const QString &action);

    NoteStore &m_store;
    PageTreeModel *m_model;
    QList<TrashItem> m_trash;
    QStackedWidget *m_stack;
    QTreeWidget *m_items;
    QLabel *m_emptyLabel;
    QPushButton *m_restore;
    QPushButton *m_delete;
    QPushButton *m_empty;
};

} // namespace inkwell
