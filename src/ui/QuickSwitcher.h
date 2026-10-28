// SPDX-License-Identifier: MIT
#pragma once

#include "PageTreeModel.h"
#include "core/Types.h"

#include <QFrame>
#include <QList>

class QLabel;
class QLineEdit;
class QListView;
class QStandardItemModel;

namespace inkwell {

/// The Ctrl+P popup: type part of a page's name, pick a match, open it.
///
/// Matching is fuzzy (see fuzzyMatch()) and runs over the page names already
/// in memory, so the results update on every keystroke without touching the
/// database.
class QuickSwitcher : public QFrame
{
    Q_OBJECT

public:
    explicit QuickSwitcher(PageTreeModel *pages, QWidget *parent = nullptr);

    /// Shows the popup near the top of @p window, with an empty query.
    void popup(QWidget *window);

    [[nodiscard]] QLineEdit *input() const { return m_input; }
    [[nodiscard]] QListView *results() const { return m_results; }

signals:
    void pageChosen(inkwell::PageId id);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void updateResults();
    void choose(const QModelIndex &index);

    PageTreeModel *m_pages;
    QLineEdit *m_input;
    QListView *m_results;
    QLabel *m_noResults;
    QStandardItemModel *m_model;
    QList<PageTreeModel::PageEntry> m_entries;
};

} // namespace inkwell
