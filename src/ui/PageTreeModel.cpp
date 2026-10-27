// SPDX-License-Identifier: MIT
#include "PageTreeModel.h"

#include "core/NoteStore.h"

#include <QApplication>
#include <QStyle>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace inkwell {

struct PageTreeModel::Node
{
    Kind kind = Kind::Folder;
    qint64 id = 0;
    QString name;
    Node *parent = nullptr;
    /// Subfolders first (the first `folderCount` entries), then pages.
    std::vector<std::unique_ptr<Node>> children;
    int folderCount = 0;

    [[nodiscard]] int row() const
    {
        if (!parent)
            return 0;
        const auto &siblings = parent->children;
        const auto it = std::find_if(siblings.cbegin(), siblings.cend(),
            [this](const std::unique_ptr<Node> &sibling) { return sibling.get() == this; });
        return int(it - siblings.cbegin());
    }
};

PageTreeModel::PageTreeModel(NoteStore &store, QObject *parent)
    : QAbstractItemModel(parent)
    , m_store(store)
    , m_root(std::make_unique<Node>())
{
    const QStyle *style = QApplication::style();
    m_folderIcon = QIcon::fromTheme(u"folder"_s, style->standardIcon(QStyle::SP_DirIcon));
    m_pageIcon = QIcon::fromTheme(u"text-x-generic"_s, style->standardIcon(QStyle::SP_FileIcon));
    reload();
}

PageTreeModel::~PageTreeModel() = default;

void PageTreeModel::reload()
{
    beginResetModel();
    m_root = std::make_unique<Node>();
    m_folders.clear();
    m_pages.clear();

    // Folders arrive sorted by (parent, position) but a parent may come after
    // its children, so create every node first and link them afterwards.
    const QList<Folder> folders = m_store.folders();
    std::vector<std::unique_ptr<Node>> pending;
    pending.reserve(size_t(folders.size()));
    for (const Folder &folder : folders) {
        auto node = std::make_unique<Node>();
        node->kind = Kind::Folder;
        node->id = folder.id;
        node->name = folder.name;
        m_folders.insert(folder.id, node.get());
        pending.push_back(std::move(node));
    }
    for (qsizetype i = 0; i < folders.size(); ++i) {
        Node *parent = m_folders.value(folders.at(i).parentId, m_root.get());
        auto &node = pending[size_t(i)];
        node->parent = parent;
        parent->children.push_back(std::move(node));
        ++parent->folderCount;
    }

    for (const Page &page : m_store.pages()) {
        Node *folder = m_folders.value(page.folderId);
        if (!folder)
            continue;
        auto node = std::make_unique<Node>();
        node->kind = Kind::Page;
        node->id = page.id;
        node->name = page.title;
        node->parent = folder;
        m_pages.insert(page.id, node.get());
        folder->children.push_back(std::move(node));
    }
    endResetModel();
}

QModelIndex PageTreeModel::index(int row, int column, const QModelIndex &parent) const
{
    const Node *parentNode = parent.isValid() ? nodeFor(parent) : m_root.get();
    if (column != 0 || !parentNode || row < 0 || size_t(row) >= parentNode->children.size())
        return {};
    return createIndex(row, column, parentNode->children[size_t(row)].get());
}

QModelIndex PageTreeModel::parent(const QModelIndex &child) const
{
    const Node *node = nodeFor(child);
    if (!node || node->parent == m_root.get())
        return {};
    return indexFor(node->parent);
}

int PageTreeModel::rowCount(const QModelIndex &parent) const
{
    if (parent.column() > 0)
        return 0;
    const Node *node = parent.isValid() ? nodeFor(parent) : m_root.get();
    return node ? int(node->children.size()) : 0;
}

int PageTreeModel::columnCount(const QModelIndex &) const
{
    return 1;
}

QVariant PageTreeModel::data(const QModelIndex &index, int role) const
{
    const Node *node = nodeFor(index);
    if (!node)
        return {};
    switch (role) {
    case Qt::DisplayRole:
    case Qt::EditRole:
        return node->name;
    case Qt::DecorationRole:
        return node->kind == Kind::Folder ? m_folderIcon : m_pageIcon;
    case KindRole:
        return QVariant::fromValue(node->kind);
    case IdRole:
        return node->id;
    default:
        return {};
    }
}

bool PageTreeModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (role != Qt::EditRole)
        return false;
    return rename(index, value.toString());
}

Qt::ItemFlags PageTreeModel::flags(const QModelIndex &index) const
{
    const Node *node = nodeFor(index);
    if (!node)
        return Qt::NoItemFlags;
    Qt::ItemFlags result = Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
    if (node->kind == Kind::Page)
        result |= Qt::ItemNeverHasChildren;
    return result;
}

QModelIndex PageTreeModel::folderIndex(FolderId id) const
{
    return indexFor(m_folders.value(id));
}

QModelIndex PageTreeModel::pageIndex(PageId id) const
{
    return indexFor(m_pages.value(id));
}

PageTreeModel::Kind PageTreeModel::kind(const QModelIndex &index)
{
    return index.data(KindRole).value<Kind>();
}

qint64 PageTreeModel::id(const QModelIndex &index)
{
    return index.data(IdRole).toLongLong();
}

bool PageTreeModel::isFolder(const QModelIndex &index) const
{
    const Node *node = nodeFor(index);
    return node && node->kind == Kind::Folder;
}

bool PageTreeModel::isPage(const QModelIndex &index) const
{
    const Node *node = nodeFor(index);
    return node && node->kind == Kind::Page;
}

FolderId PageTreeModel::containingFolder(const QModelIndex &index) const
{
    const Node *node = nodeFor(index);
    if (!node)
        return kNoFolder;
    return node->kind == Kind::Folder ? node->id : node->parent->id;
}

FolderId PageTreeModel::firstFolder() const
{
    return m_root->folderCount > 0 ? m_root->children.front()->id : kNoFolder;
}

QString PageTreeModel::pageTitle(PageId id) const
{
    const Node *node = m_pages.value(id);
    return node ? node->name : QString();
}

QList<PageId> PageTreeModel::pagesUnder(const QModelIndex &index) const
{
    QList<PageId> result;
    if (const Node *node = nodeFor(index))
        collectPages(node, result);
    return result;
}

QModelIndex PageTreeModel::createFolder(FolderId parentId, const QString &name)
{
    Node *parent = parentId == kNoFolder ? m_root.get() : m_folders.value(parentId);
    if (!parent)
        return {};
    const auto folder = m_store.createFolder(parentId, name);
    if (!folder) {
        emit errorOccurred(m_store.lastError());
        return {};
    }

    auto node = std::make_unique<Node>();
    node->kind = Kind::Folder;
    node->id = folder->id;
    node->name = folder->name;
    node->parent = parent;
    Node *raw = node.get();

    const int row = parent->folderCount;
    beginInsertRows(indexFor(parent), row, row);
    parent->children.insert(parent->children.begin() + row, std::move(node));
    ++parent->folderCount;
    m_folders.insert(raw->id, raw);
    endInsertRows();
    return indexFor(raw);
}

QModelIndex PageTreeModel::createPage(FolderId folderId, const QString &title)
{
    Node *parent = m_folders.value(folderId);
    if (!parent)
        return {};
    const auto page = m_store.createPage(folderId, title);
    if (!page) {
        emit errorOccurred(m_store.lastError());
        return {};
    }

    auto node = std::make_unique<Node>();
    node->kind = Kind::Page;
    node->id = page->id;
    node->name = page->title;
    node->parent = parent;
    Node *raw = node.get();

    const int row = int(parent->children.size());
    beginInsertRows(indexFor(parent), row, row);
    parent->children.push_back(std::move(node));
    m_pages.insert(raw->id, raw);
    endInsertRows();
    return indexFor(raw);
}

bool PageTreeModel::rename(const QModelIndex &index, const QString &name)
{
    Node *node = nodeFor(index);
    if (!node)
        return false;
    const QString cleaned = name.simplified();
    if (cleaned.isEmpty() || cleaned == node->name)
        return false;

    const bool ok = node->kind == Kind::Folder ? m_store.renameFolder(node->id, cleaned)
                                               : m_store.renamePage(node->id, cleaned);
    if (!ok) {
        emit errorOccurred(m_store.lastError());
        return false;
    }
    node->name = cleaned;
    emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole});
    if (node->kind == Kind::Page)
        emit pageRenamed(node->id, cleaned);
    return true;
}

bool PageTreeModel::remove(const QModelIndex &index)
{
    const Node *node = nodeFor(index);
    if (!node)
        return false;

    const bool ok = node->kind == Kind::Folder ? m_store.deleteFolder(node->id)
                                               : m_store.deletePage(node->id);
    if (!ok) {
        emit errorOccurred(m_store.lastError());
        return false;
    }

    QList<PageId> removedPages;
    collectPages(node, removedPages);

    Node *parent = node->parent;
    const int row = node->row();
    beginRemoveRows(indexFor(parent), row, row);
    forget(node);
    if (node->kind == Kind::Folder)
        --parent->folderCount;
    parent->children.erase(parent->children.begin() + row); // destroys node
    endRemoveRows();

    if (!removedPages.isEmpty())
        emit pagesRemoved(removedPages);
    return true;
}

PageTreeModel::Node *PageTreeModel::nodeFor(const QModelIndex &index) const
{
    if (!index.isValid() || index.model() != this)
        return nullptr;
    return static_cast<Node *>(index.internalPointer());
}

QModelIndex PageTreeModel::indexFor(const Node *node) const
{
    if (!node || node == m_root.get())
        return {};
    return createIndex(node->row(), 0, const_cast<Node *>(node));
}

void PageTreeModel::collectPages(const Node *node, QList<PageId> &out) const
{
    if (node->kind == Kind::Page) {
        out.append(node->id);
        return;
    }
    for (const auto &child : node->children)
        collectPages(child.get(), out);
}

void PageTreeModel::forget(const Node *node)
{
    if (node->kind == Kind::Page) {
        m_pages.remove(node->id);
        return;
    }
    m_folders.remove(node->id);
    for (const auto &child : node->children)
        forget(child.get());
}

} // namespace inkwell
