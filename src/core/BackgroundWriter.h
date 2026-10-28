// SPDX-License-Identifier: MIT
#pragma once

#include "Types.h"

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QWaitCondition>

class QThread;

namespace inkwell {

class Database;
class NoteStore;

/// Persists page bodies on a dedicated thread with its own SQLite connection.
///
/// The UI thread hands over a snapshot of the text with savePage() and goes
/// straight back to handling input. All disk I/O, including the fsync that
/// makes a save durable, happens on the writer thread.
///
/// Saves are coalesced: if a page is saved several times before the writer
/// gets to it, only the latest text is written. Everything waiting when the
/// writer wakes up goes into a single transaction, so one fsync covers it all.
///
/// Thread-safe. Signals are emitted from the writer thread, so receivers in
/// other threads get them through queued connections.
class BackgroundWriter : public QObject
{
    Q_OBJECT

public:
    explicit BackgroundWriter(QObject *parent = nullptr);
    /// Flushes everything still queued, then stops the thread.
    ~BackgroundWriter() override;

    /// Opens a connection to the (already migrated) database at @p path and
    /// starts the writer thread. Returns false if the database cannot be opened.
    bool start(const QString &path);
    /// Writes everything still queued, then stops the thread.
    void stop();
    [[nodiscard]] bool isRunning() const;

    /// Queues @p body to be written to page @p id and returns a ticket that
    /// pageSaved() reports once the write is durable. Tickets increase
    /// monotonically. Never blocks on I/O.
    quint64 savePage(PageId id, const QString &body);

    /// Blocks until everything queued before this call is durable, or until
    /// @p timeoutMs elapses (-1 waits forever). Returns true on success.
    bool flush(int timeoutMs = -1);

    [[nodiscard]] QString lastError() const;

signals:
    /// The save with @p ticket, and all earlier saves of the page, are on disk.
    void pageSaved(inkwell::PageId id, quint64 ticket);
    /// A write failed. It is retried automatically.
    void saveFailed(inkwell::PageId id, const QString &error);

private:
    struct PendingSave
    {
        QString body;
        quint64 ticket = 0;
    };
    using Batch = QHash<PageId, PendingSave>;
    enum class State { Stopped, Starting, Running, FailedToStart };

    void run(const QString &path);
    void processQueue(Database &db, NoteStore &store);
    bool writeBatch(Database &db, NoteStore &store, Batch &batch, QString *error);

    mutable QMutex m_mutex;
    QWaitCondition m_workAvailable; ///< wakes the writer thread
    QWaitCondition m_progress; ///< wakes threads waiting in start() and flush()
    Batch m_pending;
    quint64 m_lastTicket = 0; ///< last ticket handed out
    quint64 m_durableTicket = 0; ///< every save up to this ticket is on disk
    State m_state = State::Stopped;
    bool m_stopRequested = false;
    QString m_lastError;
    QThread *m_thread = nullptr;
};

} // namespace inkwell
