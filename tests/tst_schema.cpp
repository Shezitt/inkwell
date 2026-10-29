// SPDX-License-Identifier: MIT
#include "core/Database.h"
#include "core/Schema.h"

#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace inkwell;

namespace {

QStringList tables(Database &db)
{
    QStringList names;
    QSqlQuery query(db.connection());
    query.exec(u"SELECT name FROM sqlite_master WHERE type = 'table' ORDER BY name"_s);
    while (query.next())
        names << query.value(0).toString();
    return names;
}

/// Turns a migrated database back into a version 3 one, without the trash.
bool downgradeToVersion3(Database &db)
{
    return db.exec(u"ALTER TABLE folders DROP COLUMN trashed_at"_s)
        && db.exec(u"ALTER TABLE pages DROP COLUMN trashed_at"_s)
        && db.exec(u"PRAGMA user_version = 3"_s);
}

/// Turns a migrated database back into a version 2 one, without the search index.
bool downgradeToVersion2(Database &db)
{
    if (!downgradeToVersion3(db))
        return false;
    for (const QString &trigger : {u"page_search_add"_s, u"page_search_body"_s,
             u"page_search_title"_s, u"page_search_remove"_s}) {
        if (!db.exec(u"DROP TRIGGER %1"_s.arg(trigger)))
            return false;
    }
    return db.exec(u"DROP TABLE page_search"_s) && db.exec(u"PRAGMA user_version = 2"_s);
}

/// Ids of the pages whose title or body matches the FTS5 @p query.
QList<qint64> search(Database &db, const QString &query)
{
    QList<qint64> ids;
    QSqlQuery sql(db.connection());
    sql.prepare(u"SELECT rowid FROM page_search WHERE page_search MATCH ? ORDER BY rowid"_s);
    sql.addBindValue(query);
    if (!sql.exec())
        return {-1};
    while (sql.next())
        ids.append(sql.value(0).toLongLong());
    return ids;
}

} // namespace

class TestSchema : public QObject
{
    Q_OBJECT

private slots:
    void migratesFreshDatabaseToLatest()
    {
        Database db;
        QVERIFY(db.open(u":memory:"_s));
        QCOMPARE(schema::version(db), 0);

        QVERIFY(schema::migrate(db));
        QCOMPARE(schema::version(db), schema::latestVersion());

        const QStringList names = tables(db);
        QVERIFY(names.contains(u"folders"_s));
        QVERIFY(names.contains(u"pages"_s));
        QVERIFY(names.contains(u"page_content"_s));
        QVERIFY(names.contains(u"session_tabs"_s));
        QVERIFY(names.contains(u"session_values"_s));
        QVERIFY(names.contains(u"page_search"_s));
    }

    void upgradesOlderDatabasesKeepingNotes()
    {
        // Recreate a version 1 database: same tables, minus the later ones.
        Database db;
        QVERIFY(db.open(u":memory:"_s));
        QVERIFY(schema::migrate(db));
        QVERIFY(downgradeToVersion2(db));
        QVERIFY(db.exec(u"DROP TABLE session_tabs"_s));
        QVERIFY(db.exec(u"DROP TABLE session_values"_s));
        QVERIFY(db.exec(u"PRAGMA user_version = 1"_s));
        QVERIFY(db.exec(
            u"INSERT INTO folders (id, parent_id, name, position, created_at, updated_at) VALUES (1, NULL, 'kept', 0, 0, 0)"_s));

        QVERIFY(schema::migrate(db));
        QCOMPARE(schema::version(db), schema::latestVersion());
        QVERIFY(tables(db).contains(u"session_tabs"_s));
        QSqlQuery query(db.connection());
        QVERIFY(query.exec(u"SELECT name FROM folders"_s) && query.next());
        QCOMPARE(query.value(0).toString(), u"kept"_s);
    }

    void indexesExistingPagesWhenUpgrading()
    {
        Database db;
        QVERIFY(db.open(u":memory:"_s));
        QVERIFY(schema::migrate(db));
        QVERIFY(downgradeToVersion2(db));
        QVERIFY(db.exec(
            u"INSERT INTO folders (id, parent_id, name, position, created_at, updated_at) VALUES (1, NULL, 'f', 0, 0, 0)"_s));
        QVERIFY(db.exec(
            u"INSERT INTO pages (id, folder_id, title, position, created_at, updated_at) VALUES (1, 1, 'Robot trials', 0, 0, 0)"_s));
        QVERIFY(db.exec(u"INSERT INTO page_content VALUES (1, 'the gripper opened')"_s));
        QVERIFY(db.exec(
            u"INSERT INTO pages (id, folder_id, title, position, created_at, updated_at) VALUES (2, 1, 'Groceries', 0, 0, 0)"_s));
        QVERIFY(db.exec(u"INSERT INTO page_content VALUES (2, 'milk')"_s));

        QVERIFY(schema::migrate(db));
        QCOMPARE(search(db, u"gripper"_s), QList<qint64> {1});
        QCOMPARE(search(db, u"title:groceries"_s), QList<qint64> {2});
    }

    void searchIndexFollowsEveryChange()
    {
        Database db;
        QVERIFY(db.open(u":memory:"_s));
        QVERIFY(schema::migrate(db));
        QVERIFY(db.exec(
            u"INSERT INTO folders (id, parent_id, name, position, created_at, updated_at) VALUES (1, NULL, 'root', 0, 0, 0)"_s));
        QVERIFY(db.exec(
            u"INSERT INTO folders (id, parent_id, name, position, created_at, updated_at) VALUES (2, 1, 'child', 0, 0, 0)"_s));
        QVERIFY(db.exec(
            u"INSERT INTO pages (id, folder_id, title, position, created_at, updated_at) VALUES (1, 2, 'Tema 5', 0, 0, 0)"_s));
        QVERIFY(db.exec(u"INSERT INTO page_content VALUES (1, 'La caché del procesador')"_s));
        QVERIFY(db.exec(
            u"INSERT INTO pages (id, folder_id, title, position, created_at, updated_at) VALUES (2, 1, 'Other', 0, 0, 0)"_s));
        QVERIFY(db.exec(u"INSERT INTO page_content VALUES (2, '')"_s));

        // New pages, ignoring case and accents, and word prefixes.
        QCOMPARE(search(db, u"CACHE"_s), QList<qint64> {1});
        QCOMPARE(search(db, u"proc*"_s), QList<qint64> {1});
        QCOMPARE(search(db, u"tema"_s), QList<qint64> {1});

        QVERIFY(db.exec(u"UPDATE page_content SET body = 'memoria virtual' WHERE page_id = 1"_s));
        QVERIFY(search(db, u"cache"_s).isEmpty());
        QCOMPARE(search(db, u"virtual"_s), QList<qint64> {1});

        QVERIFY(db.exec(u"UPDATE pages SET title = 'Tema 6' WHERE id = 1"_s));
        QCOMPARE(search(db, u"title:6"_s), QList<qint64> {1});
        QVERIFY(search(db, u"title:5"_s).isEmpty());

        QVERIFY(db.exec(u"DELETE FROM pages WHERE id = 2"_s));
        QVERIFY(search(db, u"title:other"_s).isEmpty());
        // Pages deleted with their folder leave the index too.
        QVERIFY(db.exec(u"DELETE FROM folders WHERE id = 1"_s));
        QVERIFY(search(db, u"virtual"_s).isEmpty());
        QSqlQuery query(db.connection());
        QVERIFY(query.exec(u"SELECT COUNT(*) FROM page_search"_s) && query.next());
        QCOMPARE(query.value(0).toInt(), 0);
    }

    void addsTheTrashKeepingNotes()
    {
        Database db;
        QVERIFY(db.open(u":memory:"_s));
        QVERIFY(schema::migrate(db));
        QVERIFY(downgradeToVersion3(db));
        QVERIFY(db.exec(
            u"INSERT INTO folders (id, parent_id, name, position, created_at, updated_at) VALUES (1, NULL, 'f', 0, 0, 0)"_s));
        QVERIFY(db.exec(
            u"INSERT INTO pages (id, folder_id, title, position, created_at, updated_at) VALUES (1, 1, 'kept', 0, 0, 0)"_s));
        QVERIFY(db.exec(u"INSERT INTO page_content VALUES (1, 'text')"_s));

        QVERIFY(schema::migrate(db));
        QSqlQuery query(db.connection());
        QVERIFY(query.exec(u"SELECT title, trashed_at IS NULL FROM pages"_s) && query.next());
        QCOMPARE(query.value(0).toString(), u"kept"_s);
        QVERIFY(query.value(1).toBool()); // existing notes are not in the trash
        QCOMPARE(search(db, u"text"_s), QList<qint64> {1});
    }

    void migrationIsIdempotent()
    {
        Database db;
        QVERIFY(db.open(u":memory:"_s));
        QVERIFY(schema::migrate(db));
        QVERIFY(schema::migrate(db));
        QCOMPARE(schema::version(db), schema::latestVersion());
    }

    void persistsAcrossReopen()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"notes.sqlite3"_s);
        {
            Database db;
            QVERIFY(db.open(path));
            QVERIFY(schema::migrate(db));
        }
        Database db;
        QVERIFY(db.open(path));
        QCOMPARE(schema::version(db), schema::latestVersion());
    }

    void refusesDatabasesFromNewerVersions()
    {
        Database db;
        QVERIFY(db.open(u":memory:"_s));
        const int future = schema::latestVersion() + 1;
        QVERIFY(db.exec(u"PRAGMA user_version = %1"_s.arg(future)));

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"newer than supported"_s));
        QVERIFY(!schema::migrate(db));
        QCOMPARE(schema::version(db), future);
    }

    void cascadesFolderDeletion()
    {
        Database db;
        QVERIFY(db.open(u":memory:"_s));
        QVERIFY(schema::migrate(db));
        QVERIFY(db.exec(
            u"INSERT INTO folders (id, parent_id, name, position, created_at, updated_at) VALUES (1, NULL, 'root', 0, 0, 0)"_s));
        QVERIFY(db.exec(
            u"INSERT INTO folders (id, parent_id, name, position, created_at, updated_at) VALUES (2, 1, 'child', 0, 0, 0)"_s));
        QVERIFY(db.exec(
            u"INSERT INTO pages (id, folder_id, title, position, created_at, updated_at) VALUES (1, 2, 'page', 0, 0, 0)"_s));
        QVERIFY(db.exec(u"INSERT INTO page_content VALUES (1, 'hello')"_s));

        QVERIFY(db.exec(u"DELETE FROM folders WHERE id = 1"_s));

        QSqlQuery query(db.connection());
        QVERIFY(query.exec(u"SELECT (SELECT COUNT(*) FROM folders) + (SELECT COUNT(*) FROM pages)"
                           " + (SELECT COUNT(*) FROM page_content)"_s));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toInt(), 0);
    }

    void rejectsPagesWithoutFolder()
    {
        Database db;
        QVERIFY(db.open(u":memory:"_s));
        QVERIFY(schema::migrate(db));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"FOREIGN KEY"_s));
        QVERIFY(!db.exec(
            u"INSERT INTO pages (id, folder_id, title, position, created_at, updated_at) VALUES (1, 99, 'orphan', 0, 0, 0)"_s));
    }
};

QTEST_GUILESS_MAIN(TestSchema)
#include "tst_schema.moc"
