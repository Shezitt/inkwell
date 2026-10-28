// SPDX-License-Identifier: MIT
#include "Database.h"

#include "Log.h"

#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>

#include <atomic>

using namespace Qt::StringLiterals;

namespace inkwell {

namespace {

constexpr int kBusyTimeoutMs = 5000;

QString nextConnectionName()
{
    static std::atomic<quint64> counter {0};
    return u"inkwell-%1"_s.arg(counter.fetch_add(1, std::memory_order_relaxed));
}

} // namespace

Database::Database()
    : m_connectionName(nextConnectionName())
{ }

Database::~Database()
{
    close();
}

bool Database::open(const QString &path)
{
    close();
    m_lastError.clear();
    m_path = path;

    const bool inMemory = path == u":memory:"_s;
    if (!inMemory) {
        const QString dir = QFileInfo(path).absolutePath();
        if (!QDir().mkpath(dir)) {
            setError(u"open"_s, u"cannot create directory %1"_s.arg(dir));
            return false;
        }
    }

    m_db = QSqlDatabase::addDatabase(u"QSQLITE"_s, m_connectionName);
    m_db.setDatabaseName(path);
    m_db.setConnectOptions(u"QSQLITE_BUSY_TIMEOUT=%1"_s.arg(kBusyTimeoutMs));
    if (!m_db.open()) {
        setError(u"open"_s, m_db.lastError().text());
        close();
        return false;
    }

    QSqlQuery journal(m_db);
    if (!journal.exec(u"PRAGMA journal_mode=WAL"_s) || !journal.next()) {
        setError(u"journal_mode"_s, journal.lastError().text());
        close();
        return false;
    }
    const QString mode = journal.value(0).toString();
    if (mode != u"wal"_s && !(inMemory && mode == u"memory"_s)) {
        setError(u"journal_mode"_s, u"WAL not supported, got '%1'"_s.arg(mode));
        close();
        return false;
    }
    journal.finish();

    // FULL makes every commit durable across power loss, not just app crashes.
    // Frequent commits (autosave) belong on a background thread so the fsync
    // cost is never felt while typing.
    const bool configured = exec(u"PRAGMA synchronous=FULL"_s) && exec(u"PRAGMA foreign_keys=ON"_s)
        && exec(u"PRAGMA temp_store=MEMORY"_s);
    if (!configured) {
        close();
        return false;
    }

    qCDebug(lcStorage) << "opened" << path << "as" << m_connectionName;
    return true;
}

void Database::close()
{
    if (!QSqlDatabase::contains(m_connectionName))
        return;
    m_db.close();
    m_db = QSqlDatabase();
    m_transactionDepth = 0;
    QSqlDatabase::removeDatabase(m_connectionName);
}

bool Database::isOpen() const
{
    return m_db.isOpen();
}

bool Database::exec(const QString &sql)
{
    QSqlQuery query(m_db);
    if (!query.exec(sql)) {
        setError(sql, query.lastError().text());
        return false;
    }
    return true;
}

QString Database::defaultPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + u"/inkwell.sqlite3"_s;
}

void Database::setError(const QString &context, const QString &message)
{
    m_lastError = context + u": "_s + message;
    qCWarning(lcStorage).noquote() << m_lastError;
}

Transaction::Transaction(Database &db)
    : m_db(db)
    , m_depth(db.m_transactionDepth)
{
    m_active = m_db.exec(m_depth == 0 ? u"BEGIN IMMEDIATE"_s : u"SAVEPOINT sp%1"_s.arg(m_depth));
    if (m_active)
        ++m_db.m_transactionDepth;
}

Transaction::~Transaction()
{
    if (m_active)
        rollback();
}

bool Transaction::commit()
{
    if (!m_active)
        return false;
    if (!m_db.exec(m_depth == 0 ? u"COMMIT"_s : u"RELEASE sp%1"_s.arg(m_depth))) {
        rollback();
        return false;
    }
    m_active = false;
    --m_db.m_transactionDepth;
    return true;
}

void Transaction::rollback()
{
    m_active = false;
    --m_db.m_transactionDepth;
    if (m_depth == 0) {
        m_db.exec(u"ROLLBACK"_s);
    } else {
        m_db.exec(u"ROLLBACK TO sp%1"_s.arg(m_depth));
        m_db.exec(u"RELEASE sp%1"_s.arg(m_depth));
    }
}

} // namespace inkwell
