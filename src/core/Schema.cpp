// SPDX-License-Identifier: MIT
#include "Schema.h"

#include "Database.h"
#include "Log.h"

#include <QList>
#include <QSqlQuery>
#include <QString>

using namespace Qt::StringLiterals;

namespace inkwell::schema {

namespace {

struct Migration
{
    int version;
    QList<QString> statements;
};

// Append new migrations at the end. Never edit one that has been released.
const QList<Migration> &migrations()
{
    static const QList<Migration> list = {
        {1,
            {
                // Folders form a tree; a NULL parent means a top-level folder.
                u"CREATE TABLE folders ("
                "  id         INTEGER PRIMARY KEY,"
                "  parent_id  INTEGER REFERENCES folders(id) ON DELETE CASCADE,"
                "  name       TEXT    NOT NULL,"
                "  position   INTEGER NOT NULL,"
                "  created_at INTEGER NOT NULL,"
                "  updated_at INTEGER NOT NULL)"_s,
                u"CREATE INDEX folders_by_parent ON folders(parent_id, position)"_s,

                // Every page lives in exactly one folder.
                u"CREATE TABLE pages ("
                "  id         INTEGER PRIMARY KEY,"
                "  folder_id  INTEGER NOT NULL REFERENCES folders(id) ON DELETE CASCADE,"
                "  title      TEXT    NOT NULL,"
                "  position   INTEGER NOT NULL,"
                "  created_at INTEGER NOT NULL,"
                "  updated_at INTEGER NOT NULL)"_s,
                u"CREATE INDEX pages_by_folder ON pages(folder_id, position)"_s,

                // Bodies live in their own table so that loading the navigation
                // tree never reads (potentially large) page contents.
                u"CREATE TABLE page_content ("
                "  page_id INTEGER PRIMARY KEY REFERENCES pages(id) ON DELETE CASCADE,"
                "  body    TEXT NOT NULL DEFAULT '')"_s,
            }},
        {2,
            {
                // Open tabs, in order. Deleting a page closes its tab.
                u"CREATE TABLE session_tabs ("
                "  position INTEGER PRIMARY KEY,"
                "  page_id  INTEGER NOT NULL REFERENCES pages(id) ON DELETE CASCADE,"
                "  cursor   INTEGER NOT NULL DEFAULT 0,"
                "  scroll   INTEGER NOT NULL DEFAULT 0)"_s,
                // Everything else about the last session (window layout, ...).
                u"CREATE TABLE session_values ("
                "  key   TEXT PRIMARY KEY,"
                "  value BLOB)"_s,
            }},
        {3,
            {
                // Full-text index of page titles and bodies; rowid is the page id.
                // unicode61 with remove_diacritics folds case and accents, so
                // "cache" finds "Caché". The prefix indexes make search-as-you-type
                // ("proc*") fast.
                u"CREATE VIRTUAL TABLE page_search USING fts5("
                "  title, body,"
                "  tokenize = 'unicode61 remove_diacritics 2',"
                "  prefix = '2 3')"_s,
                u"INSERT INTO page_search (rowid, title, body)"
                "  SELECT p.id, p.title, c.body FROM pages p JOIN page_content c ON c.page_id = p.id"_s,

                // Triggers keep the index in step with every write, in the same
                // transaction, so it can never drift from the notes, not even after
                // a crash. They also fire for pages deleted along with their folder.
                u"CREATE TRIGGER page_search_add AFTER INSERT ON page_content BEGIN"
                "  INSERT INTO page_search (rowid, title, body)"
                "    SELECT id, title, new.body FROM pages WHERE id = new.page_id;"
                " END"_s,
                u"CREATE TRIGGER page_search_body AFTER UPDATE OF body ON page_content BEGIN"
                "  UPDATE page_search SET body = new.body WHERE rowid = new.page_id;"
                " END"_s,
                u"CREATE TRIGGER page_search_title AFTER UPDATE OF title ON pages BEGIN"
                "  UPDATE page_search SET title = new.title WHERE rowid = new.id;"
                " END"_s,
                u"CREATE TRIGGER page_search_remove AFTER DELETE ON pages BEGIN"
                "  DELETE FROM page_search WHERE rowid = old.id;"
                " END"_s,
            }},
    };
    return list;
}

} // namespace

int latestVersion()
{
    return migrations().constLast().version;
}

int version(Database &db)
{
    QSqlQuery query(db.connection());
    if (!query.exec(u"PRAGMA user_version"_s) || !query.next())
        return -1;
    return query.value(0).toInt();
}

bool migrate(Database &db)
{
    const int current = version(db);
    if (current < 0)
        return false;
    if (current > latestVersion()) {
        qCWarning(lcStorage) << "database schema" << current << "is newer than supported"
                             << latestVersion() << "- refusing to open";
        return false;
    }

    for (const Migration &migration : migrations()) {
        if (migration.version <= current)
            continue;

        Transaction tx(db);
        if (!tx.isActive())
            return false;
        for (const QString &sql : migration.statements) {
            if (!db.exec(sql))
                return false;
        }
        if (!db.exec(u"PRAGMA user_version = %1"_s.arg(migration.version)) || !tx.commit())
            return false;
        qCInfo(lcStorage) << "migrated database to schema" << migration.version;
    }
    return true;
}

} // namespace inkwell::schema
