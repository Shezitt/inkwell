// SPDX-License-Identifier: MIT
#pragma once

#include <QSqlDatabase>
#include <QString>

namespace inkwell {

/// Owns one SQLite connection configured for durability.
///
/// The connection runs in WAL mode with `synchronous=FULL`: once a transaction
/// commits, it has been fsynced and survives a crash or a power loss.
///
/// SQLite connections are bound to the thread that opened them, so every
/// thread that touches the database owns its own Database instance.
class Database
{
public:
    Database();
    ~Database();

    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;

    /// Opens, and creates if needed, the database at @p path. Missing parent
    /// directories are created. ":memory:" opens a private in-memory database.
    bool open(const QString &path);
    void close();
    [[nodiscard]] bool isOpen() const;

    [[nodiscard]] QSqlDatabase &connection() { return m_db; }
    [[nodiscard]] QString path() const { return m_path; }
    [[nodiscard]] QString lastError() const { return m_lastError; }

    /// Runs a single SQL statement without results. Records the error on failure.
    bool exec(const QString &sql);

    /// Default location of the notes database in the user's data directory.
    [[nodiscard]] static QString defaultPath();

private:
    friend class Transaction;
    void setError(const QString &context, const QString &message);

    QSqlDatabase m_db;
    QString m_connectionName;
    QString m_path;
    QString m_lastError;
    int m_transactionDepth = 0;
};

/// RAII write transaction. Rolls back on destruction unless committed.
///
/// The outermost transaction uses `BEGIN IMMEDIATE` so the write lock is taken
/// up front. That way a concurrent writer makes us wait (bounded by the busy
/// timeout) instead of failing half-way through the transaction.
///
/// Transactions nest: inner ones become SAVEPOINTs, so a helper that needs
/// atomicity can open a Transaction whether or not its caller already did.
class Transaction
{
public:
    explicit Transaction(Database &db);
    ~Transaction();

    Transaction(const Transaction &) = delete;
    Transaction &operator=(const Transaction &) = delete;

    [[nodiscard]] bool isActive() const { return m_active; }
    bool commit();

private:
    void rollback();

    Database &m_db;
    int m_depth = 0;
    bool m_active = false;
};

} // namespace inkwell
