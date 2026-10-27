// SPDX-License-Identifier: MIT
//
// Runs the real application and checks that it shuts down gracefully, saving
// everything, when it receives a termination signal.

#include "core/Database.h"
#include "core/Session.h"

#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

#include <csignal>

using namespace Qt::StringLiterals;
using namespace inkwell;

namespace {

void startApp(QProcess &app, const QString &databasePath)
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(u"QT_QPA_PLATFORM"_s, u"offscreen"_s);
    app.setProcessEnvironment(env);
    app.start(QStringLiteral(INKWELL_APP_PATH), {u"--database"_s, databasePath});
}

/// Whether the process has a handler installed for SIGTERM, read from /proc.
bool catchesSigterm(qint64 pid)
{
    QFile status(u"/proc/%1/status"_s.arg(pid));
    if (!status.open(QIODevice::ReadOnly))
        return false;
    for (const QByteArray &line : status.readAll().split('\n')) {
        if (line.startsWith("SigCgt:"))
            return line.mid(7).trimmed().toULongLong(nullptr, 16) & (1ULL << (SIGTERM - 1));
    }
    return false;
}

} // namespace

class TestShutdown : public QObject
{
    Q_OBJECT

private slots:
    void savesAndExitsCleanlyOnSigterm()
    {
#ifndef Q_OS_UNIX
        QSKIP("termination signals are Unix-only");
#else
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(u"notes.sqlite3"_s);

        QProcess app;
        startApp(app, path);
        QVERIFY2(app.waitForStarted(), qPrintable(app.errorString()));

        // The lock is taken right after the signal handler is installed, so from
        // here on SIGTERM must lead to a clean, saving shutdown, even though the
        // window may not be up yet.
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(path + u".lock"_s), 10'000);
        app.terminate(); // SIGTERM
        QVERIFY(app.waitForFinished(10'000));
        QCOMPARE(app.exitStatus(), QProcess::NormalExit);
        QCOMPARE(app.exitCode(), 0);

        // The final save wrote the session, well before its 1 s debounce.
        Database db;
        QVERIFY(db.open(path));
        QVERIFY(!session::load(db).windowGeometry.isEmpty());
#endif
    }

    void exitsOnSigtermWhileReportingNotebookInUse()
    {
#ifndef Q_OS_LINUX
        QSKIP("needs /proc to tell when the signal handler is installed");
#else
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(u"notes.sqlite3"_s);
        QLockFile otherInstance(path + u".lock"_s);
        QVERIFY(otherInstance.tryLock());

        QProcess app;
        startApp(app, path);
        QVERIFY2(app.waitForStarted(), qPrintable(app.errorString()));

        // The app tells the user the notebook is in use and waits in a dialog.
        // SIGTERM must end it there too, not only once the window is up.
        QTRY_VERIFY_WITH_TIMEOUT(catchesSigterm(app.processId()), 10'000);
        app.terminate();
        QVERIFY(app.waitForFinished(10'000));
        QCOMPARE(app.exitStatus(), QProcess::NormalExit);
        QCOMPARE(app.exitCode(), 0);
#endif
    }
};

QTEST_GUILESS_MAIN(TestShutdown)
#include "tst_shutdown.moc"
