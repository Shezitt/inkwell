// SPDX-License-Identifier: MIT
#pragma once

#include "SearchPopup.h"
#include "core/Types.h"

#include <QHash>
#include <QStringList>

namespace inkwell {

class NoteStore;
class PageTreeModel;

/// The Ctrl+Shift+F popup: find pages by the words in their title or text.
///
/// Each result shows the page, its folder and a line of the text around the
/// match, with the matched words in bold. See NoteStore::search() for how
/// words are matched.
class PageSearch : public SearchPopup
{
    Q_OBJECT

public:
    PageSearch(NoteStore &store, PageTreeModel *pages, QWidget *parent = nullptr);

signals:
    /// @p words are the words searched for, to find the match in the page.
    void pageChosen(inkwell::PageId id, const QStringList &words);

protected:
    void prepare() override;
    void updateResults() override;
    void activate(const QModelIndex &index) override;

private:
    NoteStore &m_store;
    PageTreeModel *m_pages;
    QHash<PageId, QString> m_folderPaths;
};

} // namespace inkwell
