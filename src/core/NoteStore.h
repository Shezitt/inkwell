// SPDX-License-Identifier: MIT
#pragma once

#include "Types.h"

#include <QList>
#include <QString>

#include <optional>

namespace inkwell {

class Database;

/// Reads and writes folders and pages through one Database connection.
///
/// NoteStore is cheap to construct and holds no state besides the connection,
/// so each thread can wrap its own Database in its own NoteStore.
/// The database must already be migrated to the latest schema.
class NoteStore
{
public:
    explicit NoteStore(Database &db);

    /// All folders, ordered by parent and then position.
    [[nodiscard]] QList<Folder> folders() const;
    /// All page metadata (without bodies), ordered by folder and then position.
    [[nodiscard]] QList<Page> pages() const;

    /// Creates a folder as the last child of @p parentId (kNoFolder for top level).
    std::optional<Folder> createFolder(FolderId parentId, const QString &name);
    bool renameFolder(FolderId id, const QString &name);
    /// Deletes the folder together with all its subfolders and pages.
    bool deleteFolder(FolderId id);
    /// Moves a folder under @p parentId (kNoFolder for top level), to @p index
    /// among that parent's subfolders (clamped). Fails if @p parentId is the
    /// folder itself or inside it.
    bool moveFolder(FolderId id, FolderId parentId, int index);

    /// Creates an empty page as the last page of @p folderId.
    std::optional<Page> createPage(FolderId folderId, const QString &title);
    bool renamePage(PageId id, const QString &title);
    bool deletePage(PageId id);
    /// Moves a page into @p folderId, to @p index among that folder's pages
    /// (clamped). Also reorders a page within its own folder.
    bool movePage(PageId id, FolderId folderId, int index);

    [[nodiscard]] bool pageExists(PageId id) const;
    [[nodiscard]] std::optional<QString> pageBody(PageId id) const;
    /// Stores @p body and bumps the page's updated_at. Fails for unknown pages.
    bool setPageBody(PageId id, const QString &body);

    [[nodiscard]] QString lastError() const { return m_lastError; }

private:
    bool rename(const QString &table, const QString &column, qint64 id, const QString &value);
    bool remove(const QString &table, qint64 id);
    bool place(
        const QString &table, const QString &parentColumn, qint64 id, qint64 parentId, int index);
    bool fail(const QString &message) const;

    Database &m_db;
    mutable QString m_lastError;
};

} // namespace inkwell
