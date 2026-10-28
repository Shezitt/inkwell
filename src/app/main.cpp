// SPDX-License-Identifier: MIT

#include "core/BackgroundWriter.h"
#include "core/Database.h"
#include "core/NoteStore.h"
#include "core/Schema.h"
#include "core/Session.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QMessageBox>

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
    return QApplication::exec();
}
