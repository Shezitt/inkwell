// SPDX-License-Identifier: MIT
#include "BackgroundWriter.h"

#include "Database.h"
#include "Log.h"
#include "NoteStore.h"

#include <QDeadlineTimer>
#include <QThread>

#include <utility>

using namespace Qt::StringLiterals;

namespace inkwell {

namespace {

/// How long to wait before retrying after a failed write (disk full, locked, …).
constexpr int kRetryDelayMs = 1000;
/// When stopping, give up after this many consecutive failed attempts rather
/// than hang the application on exit.
constexpr int kMaxAttemptsWhileStopping = 3;

} // namespace

BackgroundWriter::BackgroundWriter(QObject *parent)
    : QObject(parent)
{ }

BackgroundWriter::~BackgroundWriter()
{
    stop();
}

bool BackgroundWriter::start(const QString &path)
{
    Q_ASSERT_X(!m_thread, "BackgroundWriter::start", "already started");

    {
        QMutexLocker lock(&m_mutex);
        m_state = State::Starting;
        m_stopRequested = false;
    }

    m_thread = QThread::create([this, path] { run(path); });
    m_thread->setObjectName(u"inkwell-writer"_s);
    m_thread->start();

    QMutexLocker lock(&m_mutex);
    while (m_state == State::Starting)
        m_progress.wait(&m_mutex);
    if (m_state == State::Running)
        return true;

    lock.unlock();
    m_thread->wait();
    delete std::exchange(m_thread, nullptr);
    return false;
}

void BackgroundWriter::stop()
{
    if (!m_thread)
        return;
    {
        QMutexLocker lock(&m_mutex);
        m_stopRequested = true;
        m_workAvailable.wakeAll();
    }
    m_thread->wait();
    delete std::exchange(m_thread, nullptr);
}

bool BackgroundWriter::isRunning() const
{
    QMutexLocker lock(&m_mutex);
    return m_state == State::Running;
}

quint64 BackgroundWriter::savePage(PageId id, const QString &body)
{
    QMutexLocker lock(&m_mutex);
    const quint64 ticket = ++m_lastTicket;
    m_pending.insert(id, {body, ticket});
    m_workAvailable.wakeOne();
    return ticket;
}

bool BackgroundWriter::flush(int timeoutMs)
{
    QMutexLocker lock(&m_mutex);
    const quint64 target = m_lastTicket;
    const QDeadlineTimer deadline(timeoutMs);
    while (m_durableTicket < target) {
        if (m_state != State::Running)
            return false;
        if (!m_progress.wait(&m_mutex, deadline))
            return m_durableTicket >= target;
    }
    return true;
}

QString BackgroundWriter::lastError() const
{
    QMutexLocker lock(&m_mutex);
    return m_lastError;
}

void BackgroundWriter::run(const QString &path)
{
    Database db;
    const bool opened = db.open(path);
    {
        QMutexLocker lock(&m_mutex);
        m_state = opened ? State::Running : State::FailedToStart;
        if (!opened)
            m_lastError = db.lastError();
        m_progress.wakeAll();
    }
    if (!opened)
        return;

    NoteStore store(db);
    processQueue(db, store);

    QMutexLocker lock(&m_mutex);
    m_state = State::Stopped;
    m_progress.wakeAll();
}

void BackgroundWriter::processQueue(Database &db, NoteStore &store)
{
    int attemptsWhileStopping = 0;
    QMutexLocker lock(&m_mutex);
    for (;;) {
        while (m_pending.isEmpty() && !m_stopRequested)
            m_workAvailable.wait(&m_mutex);
        if (m_pending.isEmpty())
            return; // stop requested and nothing left to write

        // Every ticket handed out so far is either in this batch or was
        // superseded by a newer save of the same page that is.
        Batch batch = std::exchange(m_pending, {});
        const quint64 batchTicket = m_lastTicket;

        lock.unlock();
        QString error;
        const bool ok = writeBatch(db, store, batch, &error);
        lock.relock();

        if (ok) {
            m_durableTicket = batchTicket;
            m_progress.wakeAll();
            lock.unlock();
            for (auto it = batch.cbegin(); it != batch.cend(); ++it)
                emit pageSaved(it.key(), it.value().ticket);
            lock.relock();
            attemptsWhileStopping = 0;
            continue;
        }

        // Put back whatever has not been superseded meanwhile and retry later.
        m_lastError = error;
        for (auto it = batch.cbegin(); it != batch.cend(); ++it) {
            if (!m_pending.contains(it.key()))
                m_pending.insert(it.key(), it.value());
        }
        lock.unlock();
        for (auto it = batch.cbegin(); it != batch.cend(); ++it)
            emit saveFailed(it.key(), error);
        lock.relock();

        if (m_stopRequested && ++attemptsWhileStopping >= kMaxAttemptsWhileStopping) {
            qCCritical(lcStorage) << "giving up on" << m_pending.size()
                                  << "unsaved page(s) while stopping:" << error;
            return;
        }
        m_workAvailable.wait(&m_mutex, kRetryDelayMs);
    }
}

bool BackgroundWriter::writeBatch(Database &db, NoteStore &store, Batch &batch, QString *error)
{
    Transaction tx(db);
    if (!tx.isActive()) {
        *error = db.lastError();
        return false;
    }

    for (auto it = batch.begin(); it != batch.end();) {
        if (store.setPageBody(it.key(), it.value().body)) {
            ++it;
        } else if (!store.pageExists(it.key())) {
            // Deleted while the save was queued; nothing left to save.
            it = batch.erase(it);
        } else {
            *error = store.lastError();
            return false;
        }
    }

    if (!tx.commit()) {
        *error = db.lastError();
        return false;
    }
    qCDebug(lcStorage) << "wrote" << batch.size() << "page(s)";
    return true;
}

} // namespace inkwell
