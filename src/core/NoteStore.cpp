// SPDX-License-Identifier: MIT
#include "NoteStore.h"

#include "Database.h"
#include "Log.h"

#include <QDateTime>
#include <QHash>
#include <QSqlError>
#include <QSqlQuery>

#include <algorithm>
#include <limits>
#include <utility>

using namespace Qt::StringLiterals;

namespace inkwell {

namespace {

qint64 now()
{
    return QDateTime::currentMSecsSinceEpoch();
}

QVariant nullableId(qint64 id)
{
    return id == 0 ? QVariant(QMetaType::fromType<qint64>()) : QVariant(id);
}

/// A WITH clause defining `live`: the folders not in the trash, directly or
/// through one of their parents.
const QString kLiveFolders
    = u"WITH RECURSIVE live(id) AS ("
      " SELECT id FROM folders WHERE parent_id IS NULL AND trashed_at IS NULL"
      " UNION ALL"
      " SELECT f.id FROM folders f JOIN live ON f.parent_id = live.id"
      " WHERE f.trashed_at IS NULL) "_s;

const QString &tableFor(TrashItem::Kind kind)
{
    static const QString folders = u"folders"_s;
    static const QString pages = u"pages"_s;
    return kind == TrashItem::Kind::Folder ? folders : pages;
}

const QString &parentColumnFor(TrashItem::Kind kind)
{
    static const QString folder = u"parent_id"_s;
    static const QString page = u"folder_id"_s;
    return kind == TrashItem::Kind::Folder ? folder : page;
}

/// Names and titles are single-line; collapse whitespace and newlines.
QString cleanName(const QString &name)
{
    return name.simplified();
}

/// Turns what the user typed into an FTS5 query that matches pages containing
/// every word, the last one also as a prefix. Each word is quoted, so
/// operators, column filters and stray quotes are taken literally.
QString ftsQuery(const QString &text)
{
    QStringList terms;
    for (const QString &word : text.split(u' ', Qt::SkipEmptyParts)) {
        // Words without letters or digits produce no tokens; FTS5 cannot match them.
        if (std::none_of(word.cbegin(), word.cend(), [](QChar c) { return c.isLetterOrNumber(); }))
            continue;
        QString quoted = word;
        quoted.replace(u'"', u"\"\""_s);
        terms.append(u'"' + quoted + u'"');
    }
    if (!terms.isEmpty())
        terms.last().append(u'*');
    return terms.join(u' ');
}

} // namespace

NoteStore::NoteStore(Database &db)
    : m_db(db)
{ }

QList<Folder> NoteStore::folders() const
{
    QList<Folder> result;
    QSqlQuery query(m_db.connection());
    query.setForwardOnly(true);
    if (!query.exec(kLiveFolders
            + u"SELECT id, COALESCE(parent_id, 0), name, position FROM folders"
              " WHERE id IN live ORDER BY parent_id, position, id"_s)) {
        fail(query.lastError().text());
        return result;
    }
    while (query.next()) {
        result.append({query.value(0).toLongLong(), query.value(1).toLongLong(),
            query.value(2).toString(), query.value(3).toInt()});
    }
    return result;
}

QList<Page> NoteStore::pages() const
{
    QList<Page> result;
    QSqlQuery query(m_db.connection());
    query.setForwardOnly(true);
    if (!query.exec(kLiveFolders
            + u"SELECT id, folder_id, title, position, updated_at FROM pages"
              " WHERE trashed_at IS NULL AND folder_id IN live"
              " ORDER BY folder_id, position, id"_s)) {
        fail(query.lastError().text());
        return result;
    }
    while (query.next()) {
        result.append({query.value(0).toLongLong(), query.value(1).toLongLong(),
            query.value(2).toString(), query.value(3).toInt(), query.value(4).toLongLong()});
    }
    return result;
}

std::optional<Folder> NoteStore::createFolder(FolderId parentId, const QString &name)
{
    Folder folder;
    folder.parentId = parentId;
    folder.name = cleanName(name);
    if (folder.name.isEmpty()) {
        fail(u"folder name must not be empty"_s);
        return std::nullopt;
    }

    Transaction tx(m_db);
    if (!tx.isActive()) {
        fail(m_db.lastError());
        return std::nullopt;
    }

    QSqlQuery query(m_db.connection());
    query.prepare(u"SELECT COALESCE(MAX(position) + 1, 0) FROM folders WHERE parent_id IS ?"_s);
    query.addBindValue(nullableId(parentId));
    if (!query.exec() || !query.next()) {
        fail(query.lastError().text());
        return std::nullopt;
    }
    folder.position = query.value(0).toInt();
    query.finish();

    const qint64 timestamp = now();
    query.prepare(u"INSERT INTO folders (parent_id, name, position, created_at, updated_at)"
                  " VALUES (?, ?, ?, ?, ?)"_s);
    query.addBindValue(nullableId(parentId));
    query.addBindValue(folder.name);
    query.addBindValue(folder.position);
    query.addBindValue(timestamp);
    query.addBindValue(timestamp);
    if (!query.exec()) {
        fail(query.lastError().text());
        return std::nullopt;
    }
    folder.id = query.lastInsertId().toLongLong();

    if (!tx.commit()) {
        fail(m_db.lastError());
        return std::nullopt;
    }
    return folder;
}

bool NoteStore::renameFolder(FolderId id, const QString &name)
{
    return rename(u"folders"_s, u"name"_s, id, name);
}

bool NoteStore::deleteFolder(FolderId id)
{
    return remove(u"folders"_s, id);
}

bool NoteStore::moveFolder(FolderId id, FolderId parentId, int index)
{
    Transaction tx(m_db);
    if (!tx.isActive())
        return fail(m_db.lastError());

    if (parentId != kNoFolder) {
        // Walk up from the new parent; meeting the folder would create a cycle.
        QSqlQuery query(m_db.connection());
        query.prepare(u"WITH RECURSIVE ancestors(id) AS ("
                      " SELECT ? UNION ALL"
                      " SELECT f.parent_id FROM folders f JOIN ancestors a ON f.id = a.id"
                      " WHERE f.parent_id IS NOT NULL)"
                      " SELECT 1 FROM ancestors WHERE id = ?"_s);
        query.addBindValue(parentId);
        query.addBindValue(id);
        if (!query.exec())
            return fail(query.lastError().text());
        if (query.next())
            return fail(u"cannot move a folder into itself"_s);
    }

    if (!place(u"folders"_s, u"parent_id"_s, id, parentId, index))
        return false;
    return tx.commit() || fail(m_db.lastError());
}

std::optional<Page> NoteStore::createPage(FolderId folderId, const QString &title)
{
    Page page;
    page.folderId = folderId;
    page.title = cleanName(title);
    if (page.title.isEmpty()) {
        fail(u"page title must not be empty"_s);
        return std::nullopt;
    }

    Transaction tx(m_db);
    if (!tx.isActive()) {
        fail(m_db.lastError());
        return std::nullopt;
    }

    QSqlQuery query(m_db.connection());
    query.prepare(u"SELECT COALESCE(MAX(position) + 1, 0) FROM pages WHERE folder_id = ?"_s);
    query.addBindValue(folderId);
    if (!query.exec() || !query.next()) {
        fail(query.lastError().text());
        return std::nullopt;
    }
    page.position = query.value(0).toInt();
    query.finish();

    page.updatedAt = now();
    query.prepare(u"INSERT INTO pages (folder_id, title, position, created_at, updated_at)"
                  " VALUES (?, ?, ?, ?, ?)"_s);
    query.addBindValue(folderId);
    query.addBindValue(page.title);
    query.addBindValue(page.position);
    query.addBindValue(page.updatedAt);
    query.addBindValue(page.updatedAt);
    if (!query.exec()) {
        fail(query.lastError().text());
        return std::nullopt;
    }
    page.id = query.lastInsertId().toLongLong();

    query.prepare(u"INSERT INTO page_content (page_id) VALUES (?)"_s);
    query.addBindValue(page.id);
    if (!query.exec()) {
        fail(query.lastError().text());
        return std::nullopt;
    }

    if (!tx.commit()) {
        fail(m_db.lastError());
        return std::nullopt;
    }
    return page;
}

bool NoteStore::renamePage(PageId id, const QString &title)
{
    return rename(u"pages"_s, u"title"_s, id, title);
}

bool NoteStore::deletePage(PageId id)
{
    return remove(u"pages"_s, id);
}

bool NoteStore::movePage(PageId id, FolderId folderId, int index)
{
    Transaction tx(m_db);
    if (!tx.isActive())
        return fail(m_db.lastError());
    if (!place(u"pages"_s, u"folder_id"_s, id, folderId, index))
        return false;
    return tx.commit() || fail(m_db.lastError());
}

bool NoteStore::trashFolder(FolderId id)
{
    return moveToTrash(u"folders"_s, id);
}

bool NoteStore::trashPage(PageId id)
{
    return moveToTrash(u"pages"_s, id);
}

QList<TrashItem> NoteStore::trash() const
{
    // Every folder, trashed or not, to describe where trashed items were.
    QHash<FolderId, std::pair<FolderId, QString>> folders;
    QSqlQuery query(m_db.connection());
    query.setForwardOnly(true);
    if (!query.exec(u"SELECT id, COALESCE(parent_id, 0), name FROM folders"_s)) {
        fail(query.lastError().text());
        return {};
    }
    while (query.next()) {
        folders.insert(
            query.value(0).toLongLong(), {query.value(1).toLongLong(), query.value(2).toString()});
    }
    const auto location = [&folders](FolderId folder) {
        QStringList names;
        for (int depth = 0; folder != kNoFolder && depth < 1000; ++depth) {
            const auto it = folders.constFind(folder);
            if (it == folders.cend())
                break;
            names.prepend(it->second);
            folder = it->first;
        }
        return names.join(u" / "_s);
    };

    QList<TrashItem> items;
    if (!query.exec(
            u"SELECT 0, id, name, COALESCE(parent_id, 0), trashed_at FROM folders"
            " WHERE trashed_at IS NOT NULL"
            " UNION ALL"
            " SELECT 1, id, title, folder_id, trashed_at FROM pages WHERE trashed_at IS NOT NULL"
            " ORDER BY 5 DESC, 2 DESC"_s)) {
        fail(query.lastError().text());
        return {};
    }
    while (query.next()) {
        TrashItem item;
        item.kind = query.value(0).toInt() == 0 ? TrashItem::Kind::Folder : TrashItem::Kind::Page;
        item.id = query.value(1).toLongLong();
        item.name = query.value(2).toString();
        item.location = location(query.value(3).toLongLong());
        item.trashedAt = query.value(4).toLongLong();
        items.append(item);
    }

    // Pages that come back with a folder: everything inside it that was not
    // trashed on its own, nor inside a subfolder that was.
    QSqlQuery count(m_db.connection());
    count.prepare(u"WITH RECURSIVE inside(id) AS ("
                  " SELECT ? UNION ALL"
                  " SELECT f.id FROM folders f JOIN inside ON f.parent_id = inside.id"
                  " WHERE f.trashed_at IS NULL)"
                  " SELECT COUNT(*) FROM pages WHERE folder_id IN inside AND trashed_at IS NULL"_s);
    for (TrashItem &item : items) {
        if (item.kind != TrashItem::Kind::Folder)
            continue;
        count.addBindValue(item.id);
        if (count.exec() && count.next())
            item.pageCount = count.value(0).toInt();
    }
    return items;
}

bool NoteStore::restore(TrashItem::Kind kind, qint64 id)
{
    Transaction tx(m_db);
    if (!tx.isActive())
        return fail(m_db.lastError());

    const QString &table = tableFor(kind);
    QSqlQuery query(m_db.connection());
    query.prepare(u"SELECT COALESCE(%1, 0), trashed_at IS NOT NULL FROM %2 WHERE id = ?"_s.arg(
        parentColumnFor(kind), table));
    query.addBindValue(id);
    if (!query.exec())
        return fail(query.lastError().text());
    if (!query.next())
        return fail(u"no row %1 in %2"_s.arg(id).arg(table));
    const FolderId parent = query.value(0).toLongLong();
    bool inTrash = query.value(1).toBool();

    // The trashed folders it is in, from the top down, come back first.
    QList<std::pair<FolderId, FolderId>> trashedAncestors; // folder, its parent
    query.prepare(
        u"SELECT COALESCE(parent_id, 0), trashed_at IS NOT NULL FROM folders WHERE id = ?"_s);
    for (FolderId folder = parent; folder != kNoFolder;) {
        query.addBindValue(folder);
        if (!query.exec() || !query.next())
            return fail(u"no folder %1"_s.arg(folder));
        const FolderId grandparent = query.value(0).toLongLong();
        if (query.value(1).toBool())
            trashedAncestors.prepend({folder, grandparent});
        folder = grandparent;
    }
    if (!inTrash && trashedAncestors.isEmpty())
        return fail(u"%1 %2 is not in the trash"_s.arg(table).arg(id));

    const auto putBack = [this, &query](const QString &itemTable, const QString &parentColumn,
                             qint64 item, FolderId itemParent) {
        query.prepare(u"UPDATE %1 SET trashed_at = NULL WHERE id = ?"_s.arg(itemTable));
        query.addBindValue(item);
        return (query.exec() || fail(query.lastError().text()))
            && place(itemTable, parentColumn, item, itemParent, std::numeric_limits<int>::max());
    };
    for (const auto &[folder, folderParent] : std::as_const(trashedAncestors)) {
        if (!putBack(u"folders"_s, u"parent_id"_s, folder, folderParent))
            return false;
    }
    if (inTrash && !putBack(table, parentColumnFor(kind), id, parent))
        return false;
    return tx.commit() || fail(m_db.lastError());
}

bool NoteStore::emptyTrash()
{
    Transaction tx(m_db);
    if (!tx.isActive())
        return fail(m_db.lastError());
    // Deleting a folder also deletes everything inside it, through the foreign keys.
    for (const QString &table : {u"folders"_s, u"pages"_s}) {
        if (!m_db.exec(u"DELETE FROM %1 WHERE trashed_at IS NOT NULL"_s.arg(table)))
            return fail(m_db.lastError());
    }
    return tx.commit() || fail(m_db.lastError());
}

bool NoteStore::pageExists(PageId id) const
{
    QSqlQuery query(m_db.connection());
    query.setForwardOnly(true);
    query.prepare(u"SELECT 1 FROM pages WHERE id = ?"_s);
    query.addBindValue(id);
    return query.exec() && query.next();
}

std::optional<QString> NoteStore::pageBody(PageId id) const
{
    QSqlQuery query(m_db.connection());
    query.setForwardOnly(true);
    query.prepare(u"SELECT body FROM page_content WHERE page_id = ?"_s);
    query.addBindValue(id);
    if (!query.exec()) {
        fail(query.lastError().text());
        return std::nullopt;
    }
    if (!query.next()) {
        fail(u"no page with id %1"_s.arg(id));
        return std::nullopt;
    }
    return query.value(0).toString();
}

bool NoteStore::setPageBody(PageId id, const QString &body)
{
    Transaction tx(m_db);
    if (!tx.isActive())
        return fail(m_db.lastError());

    QSqlQuery query(m_db.connection());
    query.prepare(u"UPDATE page_content SET body = ? WHERE page_id = ?"_s);
    query.addBindValue(body);
    query.addBindValue(id);
    if (!query.exec())
        return fail(query.lastError().text());
    if (query.numRowsAffected() != 1)
        return fail(u"no page with id %1"_s.arg(id));

    query.prepare(u"UPDATE pages SET updated_at = ? WHERE id = ?"_s);
    query.addBindValue(now());
    query.addBindValue(id);
    if (!query.exec())
        return fail(query.lastError().text());

    return tx.commit() || fail(m_db.lastError());
}

QList<SearchHit> NoteStore::search(const QString &text, int limit) const
{
    const QString query = ftsQuery(text.simplified());
    if (query.isEmpty())
        return {};

    // bm25() is lower for better matches; weigh a word in the title like ten in the body.
    QSqlQuery sql(m_db.connection());
    sql.setForwardOnly(true);
    sql.prepare(kLiveFolders
        + u"SELECT rowid, title, snippet(page_search, 1, ?, ?, '…', 12)"
          " FROM page_search WHERE page_search MATCH ?"
          " AND rowid IN (SELECT id FROM pages WHERE trashed_at IS NULL AND folder_id IN live)"
          " ORDER BY bm25(page_search, 10.0, 1.0) LIMIT ?"_s);
    sql.addBindValue(QString(SearchHit::kMatchStart));
    sql.addBindValue(QString(SearchHit::kMatchEnd));
    sql.addBindValue(query);
    sql.addBindValue(limit);
    if (!sql.exec()) {
        fail(sql.lastError().text());
        return {};
    }

    QList<SearchHit> hits;
    while (sql.next()) {
        QString snippet = sql.value(2).toString().simplified();
        if (!snippet.contains(SearchHit::kMatchStart))
            snippet.clear(); // only the title matched; the body start says nothing
        hits.append({sql.value(0).toLongLong(), sql.value(1).toString(), snippet});
    }
    return hits;
}

bool NoteStore::rename(const QString &table, const QString &column, qint64 id, const QString &value)
{
    const QString cleaned = cleanName(value);
    if (cleaned.isEmpty())
        return fail(u"name must not be empty"_s);

    QSqlQuery query(m_db.connection());
    query.prepare(u"UPDATE %1 SET %2 = ?, updated_at = ? WHERE id = ?"_s.arg(table, column));
    query.addBindValue(cleaned);
    query.addBindValue(now());
    query.addBindValue(id);
    if (!query.exec())
        return fail(query.lastError().text());
    return query.numRowsAffected() == 1 || fail(u"no row %1 in %2"_s.arg(id).arg(table));
}

bool NoteStore::moveToTrash(const QString &table, qint64 id)
{
    QSqlQuery query(m_db.connection());
    query.prepare(u"UPDATE %1 SET trashed_at = ? WHERE id = ? AND trashed_at IS NULL"_s.arg(table));
    query.addBindValue(now());
    query.addBindValue(id);
    if (!query.exec())
        return fail(query.lastError().text());
    return query.numRowsAffected() == 1
        || fail(u"no row %1 in %2 outside the trash"_s.arg(id).arg(table));
}

bool NoteStore::remove(const QString &table, qint64 id)
{
    QSqlQuery query(m_db.connection());
    query.prepare(u"DELETE FROM %1 WHERE id = ?"_s.arg(table));
    query.addBindValue(id);
    if (!query.exec())
        return fail(query.lastError().text());
    return query.numRowsAffected() == 1 || fail(u"no row %1 in %2"_s.arg(id).arg(table));
}

/// Puts row @p id under @p parentId at @p index among its new siblings and
/// renumbers them, so positions stay dense. Must run inside a transaction.
bool NoteStore::place(
    const QString &table, const QString &parentColumn, qint64 id, qint64 parentId, int index)
{
    QSqlQuery query(m_db.connection());
    query.prepare(u"SELECT 1 FROM %1 WHERE id = ?"_s.arg(table));
    query.addBindValue(id);
    if (!query.exec())
        return fail(query.lastError().text());
    if (!query.next())
        return fail(u"no row %1 in %2"_s.arg(id).arg(table));

    QList<qint64> order;
    // Only siblings outside the trash count: positions are where items appear.
    query.prepare(u"SELECT id FROM %1 WHERE %2 IS ? AND id != ? AND trashed_at IS NULL"
                  " ORDER BY position, id"_s.arg(table, parentColumn));
    query.addBindValue(nullableId(parentId));
    query.addBindValue(id);
    if (!query.exec())
        return fail(query.lastError().text());
    while (query.next())
        order.append(query.value(0).toLongLong());
    order.insert(std::clamp(qsizetype(index), qsizetype(0), order.size()), id);

    query.prepare(u"UPDATE %1 SET %2 = ?, position = ? WHERE id = ?"_s.arg(table, parentColumn));
    for (qsizetype position = 0; position < order.size(); ++position) {
        query.addBindValue(nullableId(parentId));
        query.addBindValue(int(position));
        query.addBindValue(order.at(position));
        // A parent that does not exist fails here, on the foreign key.
        if (!query.exec())
            return fail(query.lastError().text());
    }
    return true;
}

bool NoteStore::fail(const QString &message) const
{
    m_lastError = message;
    qCWarning(lcStorage).noquote() << message;
    return false;
}

} // namespace inkwell
