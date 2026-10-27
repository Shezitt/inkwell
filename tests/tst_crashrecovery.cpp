// SPDX-License-Identifier: MIT
//
// Kills a process while it is saving and verifies that nothing it reported as
// saved is lost and that the database is not corrupted.
//
// This covers application crashes and forced quits. Power loss cannot be
// simulated here; it is covered by SQLite's guarantees for synchronous=FULL.

#include "core/Database.h"
#include "core/NoteStore.h"
#include "core/Schema.h"

#include <QProcess>
#include <QRandomGenerator>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

using namespace Qt::StringLiterals;
using namespace inkwell;

namespace {

/// Returns the last complete line of @p output parsed as a number, or 0.
quint64 lastConfirmedRevision(const QByteArray &output)
{
    const qsizetype end = output.lastIndexOf('\n');
    if (end < 0)
        return 0;
    const qsizetype start = output.lastIndexOf('\n', end - 1) + 1;
    return output.mid(start, end - start).toULongLong();
}

} // namespace

class TestCrashRecovery : public QObject
{
    Q_OBJECT

private slots:
    void survivesBeingKilledWhileSaving_data()
    {
        QTest::addColumn<int>("confirmedSaves");
        QTest::newRow("immediately") << 1;
        QTest::newRow("after a few saves") << 5;
        QTest::newRow("after many saves") << 40;
        QTest::newRow("after very many saves") << 120;
    }

    void survivesBeingKilledWhileSaving()
    {
        QFETCH(int, confirmedSaves);

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(u"notes.sqlite3"_s);
        PageId page = 0;
        {
            Database db;
            QVERIFY(db.open(path));
            QVERIFY(schema::migrate(db));
            NoteStore store(db);
            const auto folder = store.createFolder(kNoFolder, u"Notes"_s);
            page = store.createPage(folder->id, u"Victim"_s)->id;
        }

        QProcess writer;
        writer.start(QStringLiteral(CRASH_WRITER_PATH), {path, QString::number(page)});
        QVERIFY2(writer.waitForStarted(), qPrintable(writer.errorString()));

        QByteArray output;
        while (output.count('\n') < confirmedSaves) {
            QVERIFY2(writer.waitForReadyRead(10'000), "writer stopped making progress");
            output += writer.readAllStandardOutput();
        }
        // Let the writer get a random distance into its next save, then pull the plug.
        QThread::usleep(QRandomGenerator::global()->bounded(3000));
        writer.kill();
        QVERIFY(writer.waitForFinished(10'000));
        output += writer.readAllStandardOutput();

        const quint64 confirmed = lastConfirmedRevision(output);
        QVERIFY(confirmed >= quint64(confirmedSaves));

        Database db;
        QVERIFY(db.open(path));
        QSqlQuery integrity(db.connection());
        QVERIFY(integrity.exec(u"PRAGMA integrity_check"_s) && integrity.next());
        QCOMPARE(integrity.value(0).toString(), u"ok"_s);

        const QString body = NoteStore(db).pageBody(page).value();
        const QString firstLine = body.section(u'\n', 0, 0);
        QVERIFY2(firstLine.startsWith(u"revision "_s), qPrintable(firstLine));
        const quint64 stored = firstLine.mid(9).toULongLong();
        QVERIFY2(stored >= confirmed,
            qPrintable(u"stored revision %1 < confirmed %2"_s.arg(stored).arg(confirmed)));
        QCOMPARE(body.size(), firstLine.size() + 1 + 64 * 1024); // no torn write
    }
};

QTEST_GUILESS_MAIN(TestCrashRecovery)
#include "tst_crashrecovery.moc"
