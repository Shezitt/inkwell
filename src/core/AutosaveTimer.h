// SPDX-License-Identifier: MIT
#pragma once

#include <QObject>
#include <QTimer>

namespace inkwell {

/// Decides when to autosave a document that is being edited.
///
/// Fires once the user pauses typing for idleDelay(), and at least every
/// maxDelay() while they keep typing, so that a crash can never cost more
/// than a couple of seconds of work, even in the middle of a long burst.
class AutosaveTimer : public QObject
{
    Q_OBJECT

public:
    static constexpr int kDefaultIdleDelayMs = 400;
    static constexpr int kDefaultMaxDelayMs = 2000;

    explicit AutosaveTimer(QObject *parent = nullptr);

    void setIdleDelay(int ms);
    void setMaxDelay(int ms);
    [[nodiscard]] int idleDelay() const { return m_idle.interval(); }
    [[nodiscard]] int maxDelay() const { return m_max.interval(); }

    /// True while a change is waiting for timeout().
    [[nodiscard]] bool isPending() const { return m_idle.isActive(); }

public slots:
    /// Records a change. Cheap enough to call on every keystroke.
    void notifyChanged();
    /// Forgets the pending change, for example after saving explicitly.
    void cancel();

signals:
    void timeout();

private:
    void fire();

    QTimer m_idle;
    QTimer m_max;
};

} // namespace inkwell
