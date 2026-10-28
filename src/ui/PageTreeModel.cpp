// SPDX-License-Identifier: MIT
#include "PageTreeModel.h"

#include "core/NoteStore.h"

#include <QApplication>
#include <QDataStream>
#include <QIODevice>
#include <QMimeData>
#include <QStyle>

#include <algorithm>
#include <functional>
#include <limits>

using namespace Qt::StringLiterals;

namespace inkwell {

namespace {

const QString kItemMimeType = u"application/x-inkwell-item"_s;

} // namespace

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
        return index.isValid() ? Qt::NoItemFlags : Qt::ItemIsDropEnabled; // top level
    Qt::ItemFlags result
        = Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable | Qt::ItemIsDragEnabled;
    if (node->kind == Kind::Page)
        result |= Qt::ItemNeverHasChildren;
    else
        result |= Qt::ItemIsDropEnabled;
    return result;
}

QStringList PageTreeModel::mimeTypes() const
{
    return {kItemMimeType};
}

QMimeData *PageTreeModel::mimeData(const QModelIndexList &indexes) const
{
    // The view allows a single selection, so one item is dragged at a time.
    const Node *node = indexes.isEmpty() ? nullptr : nodeFor(indexes.constFirst());
    if (!node)
        return nullptr;
    QByteArray encoded;
    QDataStream stream(&encoded, QIODevice::WriteOnly);
    stream << int(node->kind) << node->id;
    auto *data = new QMimeData;
    data->setData(kItemMimeType, encoded);
    return data;
}

bool PageTreeModel::canDropMimeData(const QMimeData *data, Qt::DropAction action, int /*row*/,
    int /*column*/, const QModelIndex &parent) const
{
    return action == Qt::MoveAction && dropTarget(draggedNode(data), parent);
}

bool PageTreeModel::dropMimeData(const QMimeData *data, Qt::DropAction action, int row,
    int /*column*/, const QModelIndex &parent)
{
    // The view calls removeRows() on the dragged rows after a successful move.
    // This model keeps the default, which does nothing: the move below is
    // already complete. Do not override removeRows().
    const Node *node = draggedNode(data);
    const Node *target = dropTarget(node, parent);
    if (action != Qt::MoveAction || !target)
        return false;

    // row -1 means dropped onto the folder itself: append. Otherwise the item
    // goes before `row`, counted among all children and before the move.
    int position = std::numeric_limits<int>::max();
    if (row >= 0) {
        position = node->kind == Kind::Folder ? std::min(row, target->folderCount)
                                              : std::max(row - target->folderCount, 0);
        if (node->parent == target && node->row() < row)
            --position; // its own row is freed above the drop point
    }
    return move(indexFor(node), target == m_root.get() ? kNoFolder : target->id, position);
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

QList<PageTreeModel::PageEntry> PageTreeModel::pageEntries() const
{
    QList<PageEntry> result;
    result.reserve(m_pages.size());
    const std::function<void(const Node *, const QString &)> visit = [&](const Node *folder,
                                                                         const QString &path) {
        for (const auto &child : folder->children) {
            if (child->kind == Kind::Page) {
                result.append({child->id, child->name, path});
            } else {
                visit(child.get(), path.isEmpty() ? child->name : path + u" / "_s + child->name);
            }
        }
    };
    visit(m_root.get(), {});
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

bool PageTreeModel::move(const QModelIndex &index, FolderId folderId, int position)
{
    Node *node = nodeFor(index);
    Node *target = folderId == kNoFolder ? m_root.get() : m_folders.value(folderId);
    if (!node || !target)
        return false;
    const bool isFolder = node->kind == Kind::Folder;
    if (!isFolder && target == m_root.get())
        return false; // every page lives in a folder
    for (const Node *ancestor = target; ancestor; ancestor = ancestor->parent) {
        if (ancestor == node)
            return false; // a folder cannot go inside itself
    }

    // Folders occupy the first rows of their parent and pages the rest, so the
    // final row is the position within the item's own block.
    Node *source = node->parent;
    const int sourceRow = node->row();
    const int blockStart = isFolder ? 0 : target->folderCount;
    int blockSize
        = isFolder ? target->folderCount : int(target->children.size()) - target->folderCount;
    if (source == target)
        --blockSize; // the item leaves its block before it is put back
    position = std::clamp(position, 0, blockSize);
    const int targetRow = blockStart + position;
    if (source == target && targetRow == sourceRow)
        return true;

    const bool ok = isFolder ? m_store.moveFolder(node->id, folderId, position)
                             : m_store.movePage(node->id, folderId, position);
    if (!ok) {
        emit errorOccurred(m_store.lastError());
        return false;
    }

    // beginMoveRows() counts the destination row before the item is taken out.
    const int destinationRow
        = source == target && targetRow > sourceRow ? targetRow + 1 : targetRow;
    if (!beginMoveRows(indexFor(source), sourceRow, sourceRow, indexFor(target), destinationRow)) {
        reload(); // cannot happen for the moves allowed above; stay in sync regardless
        return true;
    }
    std::unique_ptr<Node> owned = std::move(source->children[size_t(sourceRow)]);
    source->children.erase(source->children.begin() + sourceRow);
    target->children.insert(target->children.begin() + targetRow, std::move(owned));
    if (isFolder) {
        --source->folderCount;
        ++target->folderCount;
    }
    node->parent = target;
    endMoveRows();
    return true;
}

bool PageTreeModel::canShift(const QModelIndex &index, int delta) const
{
    const Node *node = nodeFor(index);
    if (!node || delta == 0)
        return false;
    const auto [position, count] = positionInBlock(node);
    return position + delta >= 0 && position + delta < count;
}

bool PageTreeModel::shift(const QModelIndex &index, int delta)
{
    if (!canShift(index, delta))
        return false;
    const Node *node = nodeFor(index);
    const FolderId folder = node->parent == m_root.get() ? kNoFolder : node->parent->id;
    return move(index, folder, positionInBlock(node).first + delta);
}

std::pair<int, int> PageTreeModel::positionInBlock(const Node *node)
{
    const Node *parent = node->parent;
    if (node->kind == Kind::Folder)
        return {node->row(), parent->folderCount};
    return {node->row() - parent->folderCount, int(parent->children.size()) - parent->folderCount};
}

PageTreeModel::Node *PageTreeModel::draggedNode(const QMimeData *data) const
{
    if (!data || !data->hasFormat(kItemMimeType))
        return nullptr;
    QDataStream stream(data->data(kItemMimeType));
    int kind = -1;
    qint64 id = 0;
    stream >> kind >> id;
    if (stream.status() != QDataStream::Ok)
        return nullptr;
    if (kind == int(Kind::Folder))
        return m_folders.value(id);
    return kind == int(Kind::Page) ? m_pages.value(id) : nullptr;
}

PageTreeModel::Node *PageTreeModel::dropTarget(const Node *node, const QModelIndex &parent) const
{
    if (!node)
        return nullptr;
    Node *target = parent.isValid() ? nodeFor(parent) : m_root.get();
    if (!target || target->kind != Kind::Folder)
        return nullptr;
    if (node->kind == Kind::Page)
        return target == m_root.get() ? nullptr : target;
    for (const Node *ancestor = target; ancestor; ancestor = ancestor->parent) {
        if (ancestor == node)
            return nullptr;
    }
    return target;
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
