// SPDX-License-Identifier: MIT
#pragma once

#include "core/Types.h"

#include <QAbstractItemModel>
#include <QHash>
#include <QIcon>
#include <QList>

#include <memory>
#include <vector>

namespace inkwell {

class NoteStore;

/// Tree of folders and pages for the navigation pane.
///
/// The whole tree (names only, never page bodies) is loaded once and kept in
/// memory, so views never wait on the database while scrolling or expanding.
/// Edits go through the NoteStore first; the in-memory tree is updated only
/// when the store reports success.
///
/// Within a folder, subfolders come first, then pages, each in their stored order.
class PageTreeModel : public QAbstractItemModel
{
    Q_OBJECT

public:
    enum class Kind { Folder, Page };
    Q_ENUM(Kind)
    enum Role { KindRole = Qt::UserRole + 1, IdRole };

    explicit PageTreeModel(NoteStore &store, QObject *parent = nullptr);
    ~PageTreeModel() override;

    /// Rebuilds the tree from the store.
    void reload();

    QModelIndex index(int row, int column, const QModelIndex &parent = {}) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

    [[nodiscard]] QModelIndex folderIndex(FolderId id) const;
    [[nodiscard]] QModelIndex pageIndex(PageId id) const;
    [[nodiscard]] static Kind kind(const QModelIndex &index);
    [[nodiscard]] static qint64 id(const QModelIndex &index);
    [[nodiscard]] bool isFolder(const QModelIndex &index) const;
    [[nodiscard]] bool isPage(const QModelIndex &index) const;
    /// The folder itself, or the folder containing a page. kNoFolder if invalid.
    [[nodiscard]] FolderId containingFolder(const QModelIndex &index) const;
    /// The first top-level folder, or kNoFolder if there are none.
    [[nodiscard]] FolderId firstFolder() const;
    [[nodiscard]] QString pageTitle(PageId id) const;
    /// Ids of all pages in the subtree rooted at @p index (inclusive).
    [[nodiscard]] QList<PageId> pagesUnder(const QModelIndex &index) const;

    /// Creates a folder (kNoFolder parent for top level) and returns its index.
    QModelIndex createFolder(FolderId parentId, const QString &name);
    /// Creates a page at the end of @p folderId and returns its index.
    QModelIndex createPage(FolderId folderId, const QString &title);
    bool rename(const QModelIndex &index, const QString &name);
    /// Deletes a page, or a folder with everything inside it.
    bool remove(const QModelIndex &index);

signals:
    void pageRenamed(inkwell::PageId id, const QString &title);
    /// Emitted after pages were deleted, directly or with their folder.
    void pagesRemoved(const QList<inkwell::PageId> &ids);
    void errorOccurred(const QString &message);

private:
    struct Node;

    [[nodiscard]] Node *nodeFor(const QModelIndex &index) const;
    [[nodiscard]] QModelIndex indexFor(const Node *node) const;
    void collectPages(const Node *node, QList<PageId> &out) const;
    void forget(const Node *node);

    NoteStore &m_store;
    std::unique_ptr<Node> m_root;
    QHash<FolderId, Node *> m_folders;
    QHash<PageId, Node *> m_pages;
    QIcon m_folderIcon;
    QIcon m_pageIcon;
};

} // namespace inkwell
