// SPDX-License-Identifier: MIT
#pragma once

#include <QString>
#include <QtGlobal>

namespace inkwell {

/// Row ids from SQLite. Valid ids are always positive; 0 means "none".
using FolderId = qint64;
using PageId = qint64;

constexpr FolderId kNoFolder = 0;

struct Folder
{
    FolderId id = kNoFolder;
    FolderId parentId = kNoFolder; ///< kNoFolder for top-level folders
    QString name;
    int position = 0; ///< order among siblings

    friend bool operator==(const Folder &, const Folder &) = default;
};

/// Page metadata. The body is loaded separately and only on demand.
struct Page
{
    PageId id = 0;
    FolderId folderId = kNoFolder;
    QString title;
    int position = 0; ///< order within the folder
    qint64 updatedAt = 0; ///< milliseconds since the Unix epoch

    friend bool operator==(const Page &, const Page &) = default;
};

/// A page found by NoteStore::search().
struct SearchHit
{
    /// Markers around the matched words in a snippet.
    static constexpr QChar kMatchStart = u'\x02';
    static constexpr QChar kMatchEnd = u'\x03';

    PageId id = 0;
    QString title;
    /// One line of the body around the best match, with the matched words
    /// between kMatchStart and kMatchEnd. Empty if only the title matched.
    QString snippet;
};

} // namespace inkwell
