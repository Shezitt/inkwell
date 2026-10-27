// SPDX-License-Identifier: MIT
#include "core/Database.h"

#include <QFileInfo>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using inkwell::Database;
using inkwell::Transaction;

namespace {

QVariant scalar(Database &db, const QString &sql)
{
    QSqlQuery query(db.connection());
    if (!query.exec(sql) || !query.next())
        return {};
    return query.value(0);
}

} // namespace

class TestDatabase : public QObject
{
    Q_OBJECT

private slots:
    void opensWithDurableSettings()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Database db;
        QVERIFY2(db.open(dir.filePath(u"notes.sqlite3"_s)), qPrintable(db.lastError()));

        QCOMPARE(scalar(db, u"PRAGMA journal_mode"_s).toString(), u"wal"_s);
        QCOMPARE(scalar(db, u"PRAGMA synchronous"_s).toInt(), 2); // FULL
        QCOMPARE(scalar(db, u"PRAGMA foreign_keys"_s).toInt(), 1);
        QVERIFY(scalar(db, u"PRAGMA busy_timeout"_s).toInt() > 0);
    }

    void createsMissingParentDirectories()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"a/b/c/notes.sqlite3"_s);
        Database db;
        QVERIFY2(db.open(path), qPrintable(db.lastError()));
        QVERIFY(QFileInfo::exists(path));
    }

    void supportsInMemoryDatabases()
    {
        Database db;
        QVERIFY2(db.open(u":memory:"_s), qPrintable(db.lastError()));
        QVERIFY(db.isOpen());
    }

    void closesAndReopens()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"notes.sqlite3"_s);
        Database db;
        QVERIFY(db.open(path));
        QVERIFY(db.exec(u"CREATE TABLE t (x INTEGER)"_s));
        db.close();
        QVERIFY(!db.isOpen());

        QVERIFY(db.open(path));
        QVERIFY(db.exec(u"INSERT INTO t VALUES (1)"_s));
    }

    void reportsErrors()
    {
        Database db;
        QVERIFY(db.open(u":memory:"_s));
        QVERIFY(!db.exec(u"THIS IS NOT SQL"_s));
        QVERIFY(!db.lastError().isEmpty());
    }

    void transactionCommits()
    {
        Database db;
        QVERIFY(db.open(u":memory:"_s));
        QVERIFY(db.exec(u"CREATE TABLE t (x INTEGER)"_s));
        {
            Transaction tx(db);
            QVERIFY(tx.isActive());
            QVERIFY(db.exec(u"INSERT INTO t VALUES (1)"_s));
            QVERIFY(tx.commit());
        }
        QCOMPARE(scalar(db, u"SELECT COUNT(*) FROM t"_s).toInt(), 1);
    }

    void transactionRollsBackUnlessCommitted()
    {
        Database db;
        QVERIFY(db.open(u":memory:"_s));
        QVERIFY(db.exec(u"CREATE TABLE t (x INTEGER)"_s));
        {
            Transaction tx(db);
            QVERIFY(db.exec(u"INSERT INTO t VALUES (1)"_s));
        }
        QCOMPARE(scalar(db, u"SELECT COUNT(*) FROM t"_s).toInt(), 0);
    }

    void nestedTransactionsUseSavepoints()
    {
        Database db;
        QVERIFY(db.open(u":memory:"_s));
        QVERIFY(db.exec(u"CREATE TABLE t (x INTEGER)"_s));
        {
            Transaction outer(db);
            QVERIFY(db.exec(u"INSERT INTO t VALUES (1)"_s));
            {
                Transaction inner(db);
                QVERIFY(inner.isActive());
                QVERIFY(db.exec(u"INSERT INTO t VALUES (2)"_s));
                // Rolled back on scope exit.
            }
            {
                Transaction inner(db);
                QVERIFY(db.exec(u"INSERT INTO t VALUES (3)"_s));
                QVERIFY(inner.commit());
            }
            QVERIFY(outer.commit());
        }
        QCOMPARE(scalar(db, u"SELECT group_concat(x) FROM t"_s).toString(), u"1,3"_s);
    }

    void outerRollbackDiscardsCommittedInnerTransactions()
    {
        Database db;
        QVERIFY(db.open(u":memory:"_s));
        QVERIFY(db.exec(u"CREATE TABLE t (x INTEGER)"_s));
        {
            Transaction outer(db);
            Transaction inner(db);
            QVERIFY(db.exec(u"INSERT INTO t VALUES (1)"_s));
            QVERIFY(inner.commit());
        }
        QCOMPARE(scalar(db, u"SELECT COUNT(*) FROM t"_s).toInt(), 0);

        // The depth counter must be back to zero: a new transaction is a real BEGIN.
        Transaction tx(db);
        QVERIFY(db.exec(u"INSERT INTO t VALUES (1)"_s));
        QVERIFY(tx.commit());
        QCOMPARE(scalar(db, u"SELECT COUNT(*) FROM t"_s).toInt(), 1);
    }

    void twoConnectionsShareTheFile()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"notes.sqlite3"_s);
        Database writer;
        Database reader;
        QVERIFY(writer.open(path));
        QVERIFY(reader.open(path));
        QVERIFY(writer.exec(u"CREATE TABLE t (x INTEGER)"_s));
        QVERIFY(writer.exec(u"INSERT INTO t VALUES (42)"_s));
        QCOMPARE(scalar(reader, u"SELECT x FROM t"_s).toInt(), 42);
    }
};

QTEST_GUILESS_MAIN(TestDatabase)
#include "tst_database.moc"
