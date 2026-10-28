// SPDX-License-Identifier: MIT
#pragma once

#include "PageTreeModel.h"
#include "SearchPopup.h"
#include "core/Types.h"

#include <QList>

namespace inkwell {

/// The Ctrl+P popup: type part of a page's name, pick a match, open it.
///
/// Matching is fuzzy (see fuzzyMatch()) and runs over the page names already
/// in memory, so the results update on every keystroke without touching the
/// database.
class QuickSwitcher : public SearchPopup
{
    Q_OBJECT

public:
    explicit QuickSwitcher(PageTreeModel *pages, QWidget *parent = nullptr);

signals:
    void pageChosen(inkwell::PageId id);

protected:
    void prepare() override;
    void updateResults() override;
    void activate(const QModelIndex &index) override;

private:
    PageTreeModel *m_pages;
    QList<PageTreeModel::PageEntry> m_entries;
};

} // namespace inkwell
