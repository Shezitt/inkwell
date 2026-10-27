// SPDX-License-Identifier: MIT

#include "TerminationSignals.h"
#include "core/BackgroundWriter.h"
#include "core/Database.h"
#include "core/NoteStore.h"
#include "core/Schema.h"
#include "core/Session.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QLockFile>
#include <QMessageBox>
#include <QPointer>
#include <QSessionManager>

using namespace Qt::StringLiterals;

namespace {

void fatal(const QString &message)
{
    QMessageBox::critical(nullptr, QApplication::applicationName(), message);
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(u"Inkwell"_s);
    QApplication::setOrganizationName(u"Inkwell"_s);
    QApplication::setApplicationVersion(QStringLiteral(INKWELL_VERSION));
    QApplication::setDesktopFileName(u"io.github.shezitt.Inkwell"_s);

    // Set once the window is up. Until then there is nothing to save.
    QPointer<inkwell::MainWindow> mainWindow;

    // Installed first so that a SIGTERM during startup is not lost either.
    inkwell::TerminationSignals termination;
    // kill, Ctrl+C in a terminal, or the system shutting down: save, then quit.
    // exit() also ends the event loop of a startup dialog, where quit() would
    // do nothing.
    QObject::connect(&termination, &inkwell::TerminationSignals::received, &app, [&mainWindow] {
        if (mainWindow)
            mainWindow->saveEverything(5000);
        QApplication::exit();
    });

    QCommandLineParser parser;
    parser.setApplicationDescription(QApplication::translate("main", "A fast, native notes app."));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption databaseOption(u"database"_s,
        QApplication::translate("main", "Use the notes database at <path>."),
        QApplication::translate("main", "path"), inkwell::Database::defaultPath());
    parser.addOption(databaseOption);
    parser.process(app);
    const QString databasePath = parser.value(databaseOption);

    // Two windows editing the same notebook would overwrite each other's
    // changes. QLockFile detects locks left behind by a crashed instance (the
    // owning process no longer exists) and takes them over.
    QDir().mkpath(QFileInfo(databasePath).absolutePath());
    QLockFile lock(databasePath + u".lock"_s);
    lock.setStaleLockTime(0); // only the PID check decides whether a lock is stale
    if (!lock.tryLock(100)) {
        if (lock.error() == QLockFile::LockFailedError) {
            QMessageBox::information(nullptr, QApplication::applicationName(),
                QApplication::translate("main", "Inkwell is already running with this notebook."));
            return 0;
        }
        fatal(QApplication::translate("main", "Could not lock the notes database at\n%1")
                .arg(databasePath));
        return 1;
    }

    inkwell::Database db;
    if (!db.open(databasePath)) {
        fatal(QApplication::translate("main", "Could not open the notes database at\n%1\n\n%2")
                .arg(databasePath, db.lastError()));
        return 1;
    }
    if (!inkwell::schema::migrate(db)) {
        fatal(QApplication::translate("main",
            "The notes database at\n%1\ncould not be upgraded. It may have been created by a "
            "newer version of Inkwell.\n\n%2")
                .arg(databasePath, db.lastError()));
        return 1;
    }
    inkwell::NoteStore store(db);

    // Declared before the window so it outlives it and can write whatever the
    // editors queue while they are torn down.
    inkwell::BackgroundWriter writer;
    if (!writer.start(databasePath)) {
        fatal(QApplication::translate("main", "Could not start saving to\n%1\n\n%2")
                .arg(databasePath, writer.lastError()));
        return 1;
    }

    inkwell::MainWindow window(store, writer);
    window.restoreSession(inkwell::session::load(db));
    window.show();
    mainWindow = &window;

    // Logging out: the session manager may end the process right after this.
    QObject::connect(
        &app, &QGuiApplication::commitDataRequest, &window,
        [&window](QSessionManager &) { window.saveEverything(5000); }, Qt::DirectConnection);
    return QApplication::exec();
}
