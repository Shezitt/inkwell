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
