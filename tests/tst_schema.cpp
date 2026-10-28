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
    }

    void upgradesOlderDatabasesKeepingNotes()
    {
        // Recreate a version 1 database: same tables, minus the session ones.
        Database db;
        QVERIFY(db.open(u":memory:"_s));
        QVERIFY(schema::migrate(db));
        QVERIFY(db.exec(u"DROP TABLE session_tabs"_s));
        QVERIFY(db.exec(u"DROP TABLE session_values"_s));
        QVERIFY(db.exec(u"PRAGMA user_version = 1"_s));
        QVERIFY(db.exec(u"INSERT INTO folders VALUES (1, NULL, 'kept', 0, 0, 0)"_s));

        QVERIFY(schema::migrate(db));
        QCOMPARE(schema::version(db), schema::latestVersion());
        QVERIFY(tables(db).contains(u"session_tabs"_s));
        QSqlQuery query(db.connection());
        QVERIFY(query.exec(u"SELECT name FROM folders"_s) && query.next());
        QCOMPARE(query.value(0).toString(), u"kept"_s);
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
        QVERIFY(db.exec(u"INSERT INTO folders VALUES (1, NULL, 'root', 0, 0, 0)"_s));
        QVERIFY(db.exec(u"INSERT INTO folders VALUES (2, 1, 'child', 0, 0, 0)"_s));
        QVERIFY(db.exec(u"INSERT INTO pages VALUES (1, 2, 'page', 0, 0, 0)"_s));
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
        QVERIFY(!db.exec(u"INSERT INTO pages VALUES (1, 99, 'orphan', 0, 0, 0)"_s));
    }
};

QTEST_GUILESS_MAIN(TestSchema)
#include "tst_schema.moc"
