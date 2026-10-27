// SPDX-License-Identifier: MIT
#include "core/AutosaveTimer.h"

#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTest>

using inkwell::AutosaveTimer;

class TestAutosaveTimer : public QObject
{
    Q_OBJECT

private slots:
    void hasSensibleDefaults()
    {
        AutosaveTimer timer;
        QCOMPARE(timer.idleDelay(), AutosaveTimer::kDefaultIdleDelayMs);
        QCOMPARE(timer.maxDelay(), AutosaveTimer::kDefaultMaxDelayMs);
        QVERIFY(timer.idleDelay() < timer.maxDelay());
        QVERIFY(!timer.isPending());
    }

    void firesOnceAfterIdle()
    {
        AutosaveTimer timer;
        timer.setIdleDelay(20);
        timer.setMaxDelay(10'000);
        QSignalSpy spy(&timer, &AutosaveTimer::timeout);

        timer.notifyChanged();
        timer.notifyChanged();
        QVERIFY(timer.isPending());
        QVERIFY(spy.wait(1000));
        QCOMPARE(spy.size(), 1);
        QVERIFY(!timer.isPending());

        QTest::qWait(60);
        QCOMPARE(spy.size(), 1); // nothing more without new changes
    }

    void keepsWaitingWhileChangesContinue()
    {
        AutosaveTimer timer;
        timer.setIdleDelay(300); // generous, so a slow CI machine cannot fire it early
        timer.setMaxDelay(10'000);
        QSignalSpy spy(&timer, &AutosaveTimer::timeout);

        for (int i = 0; i < 5; ++i) {
            timer.notifyChanged();
            QTest::qWait(20);
        }
        QCOMPARE(spy.size(), 0);
        QVERIFY(spy.wait(1000));
    }

    void firesAtMaxDelayDuringContinuousChanges()
    {
        AutosaveTimer timer;
        timer.setIdleDelay(100);
        timer.setMaxDelay(150);
        QSignalSpy spy(&timer, &AutosaveTimer::timeout);

        QElapsedTimer elapsed;
        elapsed.start();
        // Keep "typing" faster than the idle delay; only the max delay can fire.
        while (spy.isEmpty() && elapsed.elapsed() < 5000) {
            timer.notifyChanged();
            QTest::qWait(10);
        }
        QCOMPARE(spy.size(), 1);
        QVERIFY(elapsed.elapsed() < 1000);
    }

    void cancelDiscardsPendingChange()
    {
        AutosaveTimer timer;
        timer.setIdleDelay(20);
        QSignalSpy spy(&timer, &AutosaveTimer::timeout);
        timer.notifyChanged();
        timer.cancel();
        QVERIFY(!timer.isPending());
        QTest::qWait(60);
        QCOMPARE(spy.size(), 0);
    }
};

QTEST_GUILESS_MAIN(TestAutosaveTimer)
#include "tst_autosavetimer.moc"
