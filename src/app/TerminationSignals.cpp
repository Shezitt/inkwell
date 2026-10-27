// SPDX-License-Identifier: MIT
#include "TerminationSignals.h"

#ifdef Q_OS_UNIX
#include <QSocketNotifier>
#include <QtDebug>

#include <array>
#include <csignal>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace inkwell {

#ifdef Q_OS_UNIX

namespace {

constexpr std::array kSignals = {SIGTERM, SIGINT, SIGHUP};

/// [0] is written by the signal handler, [1] is read by the event loop.
int g_sockets[2] = {-1, -1};

void onSignal(int signalNumber)
{
    const char byte = char(signalNumber);
    [[maybe_unused]] const auto written = ::write(g_sockets[0], &byte, 1);
}

} // namespace

TerminationSignals::TerminationSignals(QObject *parent)
    : QObject(parent)
{
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, g_sockets) != 0) {
        qWarning("cannot create signal socket; termination signals will not save");
        return;
    }
    for (const int fd : g_sockets)
        ::fcntl(fd, F_SETFD, FD_CLOEXEC);
    // Never block inside the handler, even if the socket buffer is full.
    ::fcntl(g_sockets[0], F_SETFL, ::fcntl(g_sockets[0], F_GETFL) | O_NONBLOCK);

    m_notifier = new QSocketNotifier(g_sockets[1], QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, [this] {
        char byte = 0;
        if (::read(g_sockets[1], &byte, 1) == 1)
            emit received(int(byte));
    });

    struct sigaction action = {};
    action.sa_handler = onSignal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    for (const int signalNumber : kSignals)
        ::sigaction(signalNumber, &action, nullptr);
}

TerminationSignals::~TerminationSignals()
{
    if (!m_notifier)
        return;
    for (const int signalNumber : kSignals)
        std::signal(signalNumber, SIG_DFL);
    for (int &fd : g_sockets) {
        ::close(fd);
        fd = -1;
    }
}

#else

TerminationSignals::TerminationSignals(QObject *parent)
    : QObject(parent)
{ }

TerminationSignals::~TerminationSignals() = default;

#endif

} // namespace inkwell
