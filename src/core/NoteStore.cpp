// SPDX-License-Identifier: MIT
#include "NoteStore.h"

#include "Database.h"
#include "Log.h"

#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>

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

/// Names and titles are single-line; collapse whitespace and newlines.
QString cleanName(const QString &name)
{
    return name.simplified();
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
    if (!query.exec(u"SELECT id, COALESCE(parent_id, 0), name, position FROM folders"
                    " ORDER BY parent_id, position, id"_s)) {
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
    if (!query.exec(u"SELECT id, folder_id, title, position, updated_at FROM pages"
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

bool NoteStore::remove(const QString &table, qint64 id)
{
    QSqlQuery query(m_db.connection());
    query.prepare(u"DELETE FROM %1 WHERE id = ?"_s.arg(table));
    query.addBindValue(id);
    if (!query.exec())
        return fail(query.lastError().text());
    return query.numRowsAffected() == 1 || fail(u"no row %1 in %2"_s.arg(id).arg(table));
}

bool NoteStore::fail(const QString &message) const
{
    m_lastError = message;
    qCWarning(lcStorage).noquote() << message;
    return false;
}

} // namespace inkwell
