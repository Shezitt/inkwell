// SPDX-License-Identifier: MIT
#pragma once

#include "core/BackgroundWriter.h"
#include "core/Database.h"
#include "core/NoteStore.h"
#include "core/Schema.h"

#include <QTemporaryDir>

#include <memory>

/// A migrated database in a temporary directory with a running writer and
/// one folder, for tests that exercise the full save path.
struct TestNotebook
{
    QTemporaryDir dir;
    inkwell::Database db;
    std::unique_ptr<inkwell::NoteStore> store;
    std::unique_ptr<inkwell::BackgroundWriter> writer;
    inkwell::FolderId folder = inkwell::kNoFolder;

    [[nodiscard]] QString path() const { return dir.filePath(QStringLiteral("notes.sqlite3")); }

    bool open()
    {
        if (!dir.isValid() || !db.open(path()) || !inkwell::schema::migrate(db))
            return false;
        store = std::make_unique<inkwell::NoteStore>(db);
        writer = std::make_unique<inkwell::BackgroundWriter>();
        const auto created = store->createFolder(inkwell::kNoFolder, QStringLiteral("Notes"));
        folder = created ? created->id : inkwell::kNoFolder;
        return created && writer->start(path());
    }

    inkwell::PageId newPage(const QString &body = {})
    {
        const auto page = store->createPage(folder, QStringLiteral("Page"));
        if (page && !body.isEmpty())
            store->setPageBody(page->id, body);
        return page ? page->id : 0;
    }

    QString storedBody(inkwell::PageId id)
    {
        return store->pageBody(id).value_or(QStringLiteral("<missing>"));
    }
};
