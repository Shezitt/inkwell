// SPDX-License-Identifier: MIT
#include "AutosaveTimer.h"

namespace inkwell {

AutosaveTimer::AutosaveTimer(QObject *parent)
    : QObject(parent)
{
    m_idle.setSingleShot(true);
    m_max.setSingleShot(true);
    m_idle.setInterval(kDefaultIdleDelayMs);
    m_max.setInterval(kDefaultMaxDelayMs);
    connect(&m_idle, &QTimer::timeout, this, &AutosaveTimer::fire);
    connect(&m_max, &QTimer::timeout, this, &AutosaveTimer::fire);
}

void AutosaveTimer::setIdleDelay(int ms)
{
    m_idle.setInterval(ms);
}

void AutosaveTimer::setMaxDelay(int ms)
{
    m_max.setInterval(ms);
}

void AutosaveTimer::notifyChanged()
{
    m_idle.start(); // restarts the pause countdown
    if (!m_max.isActive())
        m_max.start(); // but the first unsaved change bounds the wait
}

void AutosaveTimer::cancel()
{
    m_idle.stop();
    m_max.stop();
}

void AutosaveTimer::fire()
{
    cancel();
    emit timeout();
}

} // namespace inkwell
