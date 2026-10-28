// SPDX-License-Identifier: MIT
#pragma once

namespace inkwell {

class Database;

/// Versioned database schema, tracked through SQLite's `user_version`.
///
/// Each migration runs in its own transaction, so a crash in the middle of an
/// upgrade leaves the database at the previous, consistent version.
namespace schema {

/// The schema version this build of Inkwell writes.
[[nodiscard]] int latestVersion();

/// The schema version stored in @p db, or -1 on error.
[[nodiscard]] int version(Database &db);

/// Brings @p db up to latestVersion(). Refuses to touch databases written by
/// a newer Inkwell, so a downgrade can never damage the user's notes.
bool migrate(Database &db);

} // namespace schema
} // namespace inkwell
