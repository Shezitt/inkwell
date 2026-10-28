// SPDX-License-Identifier: MIT
#pragma once

#include <QObject>

class QSocketNotifier;

namespace inkwell {

/// Turns SIGTERM, SIGINT and SIGHUP into a Qt signal delivered on the event loop.
///
/// By default these signals kill the process on the spot, skipping the final
/// save. The handler only writes one byte to a socket (all that is safe to do
/// inside a signal handler); a QSocketNotifier picks it up on the main thread.
///
/// Signals that arrive before anything is connected are queued in the socket
/// and delivered once the event loop runs. Does nothing on non-Unix systems.
class TerminationSignals : public QObject
{
    Q_OBJECT

public:
    explicit TerminationSignals(QObject *parent = nullptr);
    ~TerminationSignals() override;

signals:
    void received(int signalNumber);

private:
    QSocketNotifier *m_notifier = nullptr;
};

} // namespace inkwell
