// SPDX-License-Identifier: MIT
//
// Helper for tst_crashrecovery. Saves a page in a tight loop and prints the
// revision number of every save once BackgroundWriter reports it durable.
// The test kills this process at an arbitrary point and then checks that the
// database is intact and that every printed revision survived.
//
// Usage: crash_writer <database> <page id>

#include "core/BackgroundWriter.h"

#include <QCoreApplication>
#include <QStringList>

#include <cstdio>

using namespace Qt::StringLiterals;

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const QStringList args = QCoreApplication::arguments();
    if (args.size() != 3) {
        std::fprintf(stderr, "usage: crash_writer <database> <page id>\n");
        return 2;
    }
    const QString path = args.at(1);
    const inkwell::PageId page = args.at(2).toLongLong();

    inkwell::BackgroundWriter writer;
    if (!writer.start(path))
        return 1;

    // Large enough that a write spans many database pages, so a kill is likely
    // to land in the middle of one.
    const QString padding(64 * 1024, u'x');
    for (unsigned long long revision = 1;; ++revision) {
        writer.savePage(page, u"revision %1\n"_s.arg(revision) + padding);
        if (!writer.flush())
            return 1;
        std::printf("%llu\n", revision);
        std::fflush(stdout);
    }
}
