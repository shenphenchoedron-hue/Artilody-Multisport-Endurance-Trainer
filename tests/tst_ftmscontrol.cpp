// FTMS control point hardening with a scripted fake trainer (FakeGattLink):
// rejections, timeouts, control lost / stop / reset, the actual trainer mode,
// a trainer without Supported Inclination Range / Training Status (Jetblack
// Victory) and the inclination range clamp.
#include "BleTrainer.h"
#include "FakeGattLink.h"
#include "FtmsProtocol.h"
#include "GradientTest.h"
#include "SensorHub.h"

#include <QSignalSpy>
#include <QTest>

using namespace watt;
using watt::test::FakeGattLink;

namespace {
QBluetoothUuid u16(quint16 v) { return QBluetoothUuid(v); }
const QBluetoothUuid kCp = u16(ftms::kControlPointUuid);
const QBluetoothUuid kStatus = u16(ftms::kMachineStatusUuid);
constexpr quint8 kOpControl = 0x00, kOpStart = 0x07, kOpSim = 0x11, kOpPower = 0x05;

QByteArray bytes(std::initializer_list<int> b)
{
    QByteArray out;
    for (int v : b)
        out.append(char(v));
    return out;
}

// cadence + power measurement; power target, resistance target, simulation
QByteArray featureBytes() { return bytes({ 0x02, 0x40, 0x00, 0x00, 0x0C, 0x20, 0x00, 0x00 }); }

// The Jetblack Victory's FTMS characteristics (Bluetooth Diagnostics report):
// no 0x2AD5 Supported Inclination Range, no 0x2AD3 Training Status.
QList<QBluetoothUuid> victoryChars()
{
    return { u16(0x2AD2), u16(0x2ACC), u16(0x2AD6), u16(0x2AD8), u16(0x2ADA), u16(0x2AD9) };
}

double gradeOf(const QByteArray &cmd)
{
    return qint16(quint16(quint8(cmd.at(3))) | (quint16(quint8(cmd.at(4))) << 8)) / 100.0;
}

struct Rig
{
    FakeGattLink *link;
    BleTrainer *trainer;
    // result code per op code (0x01 success); -1 = no answer (timeout)
    QHash<int, int> results;
};

Rig makeRig(QList<QBluetoothUuid> chars = victoryChars())
{
    Rig r;
    r.link = new FakeGattLink(chars, { u16(0x1826), u16(0x1818), u16(0xFC82) });
    r.link->readValues.insert(u16(ftms::kFeatureUuid), featureBytes());
    r.trainer = new BleTrainer(r.link, QStringLiteral("E7:45:CF:5F:0F:D9"), QStringLiteral("Victory 0FD9"));
    r.trainer->setTimingForTesting(100, 50, 100);
    return r;
}

void installResponder(Rig &r, QHash<int, int> *results)
{
    r.link->responder = [results](const QBluetoothUuid &c, const QByteArray &d) -> QVector<FakeGattLink::Notification> {
        if (c != kCp || d.isEmpty())
            return {};
        const int op = quint8(d.at(0));
        const int res = results->value(op, 0x01);
        if (res < 0)
            return {};   // no indication: the trainer stays silent
        return { { kCp, bytes({ 0x80, op, res }) } };
    };
}
} // namespace

class TestFtmsControl : public QObject
{
    Q_OBJECT

private slots:
    void victoryWithoutOptionalCharacteristics()
    {
        Rig r = makeRig();
        QHash<int, int> results;
        installResponder(r, &results);
        r.trainer->connectDevice();
        r.trainer->setSimulation(2.0, 0, 0.004, 0.39);
        QTRY_COMPARE(r.trainer->trainerMode(), QStringLiteral("simulation"));
        // nothing waited for / read from the missing characteristics
        QVERIFY(!r.link->reads.contains(u16(ftms::kSupportedInclinationRangeUuid)));
        QVERIFY(!r.link->subscribed.contains(u16(ftms::kTrainingStatusUuid)));
        QVERIFY(r.link->subscribed.contains(kCp));
        QVERIFY(!r.trainer->inclinationRangeKnown());
        QCOMPARE(r.trainer->inclinationNote(), QStringLiteral("Trainer doesn't report its gradient range"));
        QVERIFY(!r.trainer->controlProblem());
        QVERIFY(r.trainer->controlStatus().startsWith(QStringLiteral("Simulation – trainer accepted 2.00 %")));
        QCOMPARE(r.link->countOp(kCp, kOpControl), 1);
        QCOMPARE(r.link->countOp(kCp, kOpStart), 1);
        QCOMPARE(r.link->countOp(kCp, kOpSim), 1);
        // grades are sent unclamped (no range)
        r.trainer->setSimulation(-7.5, 0, 0.004, 0.39);
        QTRY_COMPARE(r.link->countOp(kCp, kOpSim), 2);
        QCOMPARE(gradeOf(r.link->writesTo(kCp).constLast()), -7.5);
        QTRY_COMPARE(r.trainer->acceptedGrade(), -7.5);
        delete r.trainer;
    }

    void rejectionIsShownRetriedAndCleared()
    {
        Rig r = makeRig();
        QHash<int, int> results;
        installResponder(r, &results);
        results.insert(kOpSim, 0x03);   // Invalid parameter
        QSignalSpy status(r.trainer, &TrainerDevice::controlStatusChanged);
        r.trainer->connectDevice();
        r.trainer->setSimulation(-3.0, 0, 0.004, 0.39);
        QTRY_VERIFY(r.trainer->controlProblem());
        QCOMPARE(r.trainer->controlStatus(), QStringLiteral("Trainer rejected the gradient command (Invalid parameter)"));
        // never claims simulation when the last 0x11 was not accepted
        QVERIFY(r.trainer->trainerMode() != QStringLiteral("simulation"));
        QVERIFY(status.count() > 0);
        // retried with backoff: 50 ms, 100 ms, 200 ms, then no more
        QTRY_COMPARE_WITH_TIMEOUT(r.link->countOp(kCp, kOpSim), 1 + BleTrainer::kMaxRetries, 3000);
        QTest::qWait(600);
        QCOMPARE(r.link->countOp(kCp, kOpSim), 1 + BleTrainer::kMaxRetries);
        QVERIFY(r.trainer->controlProblem());
        // the next command is accepted: the problem is gone
        results.insert(kOpSim, 0x01);
        r.trainer->setSimulation(-2.0, 0, 0.004, 0.39);
        QTRY_COMPARE(r.trainer->trainerMode(), QStringLiteral("simulation"));
        QVERIFY(!r.trainer->controlProblem());
        QCOMPARE(r.trainer->failureStreak(), 0);
        delete r.trainer;
    }

    void operationFailedRetrySucceeds()
    {
        Rig r = makeRig();
        QHash<int, int> results;
        installResponder(r, &results);
        int failuresLeft = 1;
        r.link->responder = [&](const QBluetoothUuid &c, const QByteArray &d) -> QVector<FakeGattLink::Notification> {
            if (c != kCp)
                return {};
            const int op = quint8(d.at(0));
            int res = 0x01;
            if (op == kOpSim && failuresLeft > 0) {
                --failuresLeft;
                res = 0x04;   // Operation failed
            }
            return { { kCp, bytes({ 0x80, op, res }) } };
        };
        r.trainer->connectDevice();
        r.trainer->setSimulation(1.0, 0, 0.004, 0.39);
        QTRY_VERIFY(r.trainer->controlProblem());
        QCOMPARE(r.trainer->controlStatus(), QStringLiteral("Trainer rejected the gradient command (Operation failed)"));
        // the retry re-sends the current command and it is accepted
        QTRY_COMPARE(r.trainer->trainerMode(), QStringLiteral("simulation"));
        QVERIFY(!r.trainer->controlProblem());
        QCOMPARE(r.link->countOp(kCp, kOpSim), 2);
        QCOMPARE(gradeOf(r.link->writesTo(kCp).constLast()), 1.0);
        delete r.trainer;
    }

    void timeoutIsShownAndRetried()
    {
        Rig r = makeRig();
        QHash<int, int> results;
        installResponder(r, &results);
        results.insert(kOpSim, -1);   // never answered
        r.trainer->connectDevice();
        r.trainer->setSimulation(4.0, 0, 0.004, 0.39);
        QTRY_VERIFY(r.trainer->controlProblem());
        QVERIFY(r.trainer->controlStatus().startsWith(QStringLiteral("Trainer did not answer the gradient command")));
        QVERIFY(r.trainer->trainerMode() != QStringLiteral("simulation"));
        QTRY_VERIFY_WITH_TIMEOUT(r.link->countOp(kCp, kOpSim) >= 2, 2000);
        results.insert(kOpSim, 0x01);
        QTRY_COMPARE_WITH_TIMEOUT(r.trainer->trainerMode(), QStringLiteral("simulation"), 3000);
        QVERIFY(!r.trainer->controlProblem());
        delete r.trainer;
    }

    void controlPermissionLostReacquires()
    {
        Rig r = makeRig();
        QHash<int, int> results;
        installResponder(r, &results);
        r.trainer->connectDevice();
        r.trainer->setSimulation(3.0, 0, 0.004, 0.39);
        QTRY_COMPARE(r.trainer->trainerMode(), QStringLiteral("simulation"));
        const int controls = r.link->countOp(kCp, kOpControl);
        const int sims = r.link->countOp(kCp, kOpSim);
        r.link->notify(kStatus, bytes({ 0xFF }));   // control permission lost
        QCOMPARE(r.trainer->trainerMode(), QStringLiteral("no control"));
        QTRY_COMPARE(r.link->countOp(kCp, kOpControl), controls + 1);
        QTRY_COMPARE(r.link->countOp(kCp, kOpSim), sims + 1);
        QCOMPARE(gradeOf(r.link->writesTo(kCp).constLast()), 3.0);
        QTRY_COMPARE(r.trainer->trainerMode(), QStringLiteral("simulation"));
        delete r.trainer;
    }

    void controlNotPermittedReacquires()
    {
        Rig r = makeRig();
        QHash<int, int> results;
        installResponder(r, &results);
        r.trainer->connectDevice();
        r.trainer->setSimulation(1.5, 0, 0.004, 0.39);
        QTRY_COMPARE(r.trainer->trainerMode(), QStringLiteral("simulation"));
        results.insert(kOpSim, 0x05);   // Control not permitted (once)
        r.trainer->setSimulation(2.5, 0, 0.004, 0.39);
        QTRY_VERIFY(r.trainer->controlProblem());
        QVERIFY(r.trainer->controlStatus().contains(QStringLiteral("Control not permitted"), Qt::CaseInsensitive)
                || r.trainer->controlStatus().contains(QStringLiteral("control"), Qt::CaseInsensitive));
        results.insert(kOpSim, 0x01);
        QTRY_COMPARE_WITH_TIMEOUT(r.trainer->trainerMode(), QStringLiteral("simulation"), 3000);
        QCOMPARE(gradeOf(r.link->writesTo(kCp).constLast()), 2.5);
        delete r.trainer;
    }

    void stoppedByUserRestarts()
    {
        Rig r = makeRig();
        QHash<int, int> results;
        installResponder(r, &results);
        r.trainer->connectDevice();
        r.trainer->setSimulation(-1.0, 0, 0.004, 0.39);
        QTRY_COMPARE(r.trainer->trainerMode(), QStringLiteral("simulation"));
        const int starts = r.link->countOp(kCp, kOpStart);
        const int sims = r.link->countOp(kCp, kOpSim);
        r.link->notify(kStatus, bytes({ 0x02, 0x01 }));   // stopped by the user
        QCOMPARE(r.trainer->trainerMode(), QStringLiteral("stopped"));
        QTRY_COMPARE(r.link->countOp(kCp, kOpStart), starts + 1);
        QTRY_COMPARE(r.link->countOp(kCp, kOpSim), sims + 1);
        QTRY_COMPARE(r.trainer->trainerMode(), QStringLiteral("simulation"));
        delete r.trainer;
    }

    void statusReportsOtherMode()
    {
        Rig r = makeRig();
        QHash<int, int> results;
        installResponder(r, &results);
        r.trainer->connectDevice();
        r.trainer->setSimulation(2.0, 0, 0.004, 0.39);
        QTRY_COMPARE(r.trainer->trainerMode(), QStringLiteral("simulation"));
        // simulation parameters changed (wind 0, grade -2.40 %, crr, cw)
        r.link->notify(kStatus, bytes({ 0x12, 0x00, 0x00, 0x10, 0xFF, 0x28, 0x33 }));
        QCOMPARE(r.trainer->acceptedGrade(), -2.4);
        // another app switched it to ERG: the trainer is not in simulation any more
        const int sims = r.link->countOp(kCp, kOpSim);
        r.link->notify(kStatus, bytes({ 0x08, 0xC8, 0x00 }));
        QCOMPARE(r.trainer->trainerMode(), QStringLiteral("erg"));
        QTRY_COMPARE(r.link->countOp(kCp, kOpSim), sims + 1);   // asked once more
        QTRY_COMPARE(r.trainer->trainerMode(), QStringLiteral("simulation"));
        QCOMPARE(r.link->countOp(kCp, kOpPower), 0);            // never ERG by itself
        delete r.trainer;
    }

    void inclinationRangeClampsAndNotes()
    {
        QList<QBluetoothUuid> chars = victoryChars();
        chars << u16(ftms::kSupportedInclinationRangeUuid);
        Rig r = makeRig(chars);
        // 0.0 … 15.0 %, step 0.5 %
        r.link->readValues.insert(u16(ftms::kSupportedInclinationRangeUuid), bytes({ 0x00, 0x00, 0x96, 0x00, 0x05, 0x00 }));
        QHash<int, int> results;
        installResponder(r, &results);
        r.trainer->connectDevice();
        QTRY_VERIFY(r.trainer->inclinationRangeKnown());
        QCOMPARE(r.trainer->minInclinationPercent(), 0.0);
        QCOMPARE(r.trainer->maxInclinationPercent(), 15.0);
        QVERIFY(r.trainer->inclinationNote().contains(QStringLiteral("doesn't simulate descents")));
        r.trainer->setSimulation(-4.0, 0, 0.004, 0.39);
        QTRY_COMPARE(r.trainer->trainerMode(), QStringLiteral("simulation"));
        QCOMPARE(gradeOf(r.link->writesTo(kCp).constLast()), 0.0);
        QVERIFY(r.trainer->controlStatus().contains(QStringLiteral("outside its range")));
        delete r.trainer;
    }

    // Gradient test: +2, 0, -2, -5 % via 0x11 only, one step per kStepSeconds,
    // measured after kSettleSeconds; a trainer that brakes the same below 0 %
    // is reported as clamping descents.
    void gradientTestRunsSimulationOnly()
    {
        Rig r = makeRig();
        QHash<int, int> results;
        installResponder(r, &results);
        SensorHub hub;
        r.trainer->connectDevice();
        r.trainer->setSimulation(0.5, 0, 0.004, 0.39);
        QTRY_COMPARE(r.trainer->trainerMode(), QStringLiteral("simulation"));
        GradientTest test(&hub);
        QString error;
        QVERIFY(test.start(r.trainer, 0.004, 0.39, &error));
        QVERIFY(test.running());
        QTRY_VERIFY(r.link->countOp(kCp, kOpSim) >= 2);
        QCOMPARE(gradeOf(r.link->writesTo(kCp).constLast()), 2.0);
        // a trainer that clamps descents: 200 W at +2 %, 150 W at 0 % and below
        const QVector<int> watts { 200, 150, 150, 148 };
        for (int step = 0; step < 4; ++step) {
            for (int sec = 0; sec < GradientTest::kStepSeconds; ++sec) {
                hub.updatePower(watts[step]);
                hub.updateCadence(90);
                hub.updateSpeed(30);
                test.tick();
                QCoreApplication::processEvents();
            }
            if (step < 3) {
                QTRY_COMPARE(gradeOf(r.link->writesTo(kCp).constLast()), GradientTest::grades().at(step + 1));
            }
        }
        QVERIFY(!test.running());
        QCOMPARE(test.steps().size(), 4);
        for (const GradientTest::Step &st : test.steps()) {
            QVERIFY(st.done);
            QCOMPARE(st.samples, GradientTest::kStepSeconds - GradientTest::kSettleSeconds);
            QVERIFY(st.accepted);
        }
        QVERIFY(test.verdict().startsWith(QStringLiteral("Power stops falling below 0 % → the trainer clamps descents")));
        QCOMPARE(r.link->countOp(kCp, kOpPower), 0);   // never ERG
        QCOMPARE(test.results().size(), 4);
    }

    void gradientTestVerdicts()
    {
        auto steps = [](std::initializer_list<double> power) {
            QVector<GradientTest::Step> out;
            int i = 0;
            for (double p : power) {
                GradientTest::Step s;
                s.grade = GradientTest::grades().at(i++);
                s.powerSum = p * 10;
                s.speedSum = 30 * 10;
                s.cadenceSum = 90 * 10;
                s.samples = 10;
                s.done = true;
                s.accepted = true;
                out << s;
            }
            return out;
        };
        QVERIFY(GradientTest::judge(steps({ 260, 180, 110, 40 })).startsWith(QStringLiteral("Power keeps falling on descents")));
        QVERIFY(GradientTest::judge(steps({ 260, 180, 180, 178 })).startsWith(QStringLiteral("Power stops falling below 0 %")));
        QVERIFY(GradientTest::judge(steps({ 260, 180, 150, 145 })).startsWith(QStringLiteral("Power falls only a little")));
        QVERIFY(GradientTest::judge({}).startsWith(QStringLiteral("Not enough data")));
        auto s = steps({ 260, 180, 110, 40 });
        s[2].accepted = false;
        QVERIFY(GradientTest::judge(s).contains(QStringLiteral("Not every gradient command was confirmed")));
    }

    void gradientTestStopAndNoTrainer()
    {
        SensorHub hub;
        GradientTest test(&hub);
        QString error;
        QVERIFY(!test.start(nullptr, 0.004, 0.39, &error));
        QCOMPARE(error, QStringLiteral("Connect a trainer first"));
        Rig r = makeRig();
        QHash<int, int> results;
        installResponder(r, &results);
        r.trainer->connectDevice();
        QSignalSpy finished(&test, &GradientTest::finished);
        QVERIFY(test.start(r.trainer, 0.004, 0.39, &error));
        test.stop();
        QVERIFY(!test.running());
        QCOMPARE(finished.count(), 1);
        QCOMPARE(test.status(), QStringLiteral("Stopped"));
    }

    void rejectionTexts()
    {
        QCOMPARE(ftms::rejectionText(0x11, 0x03), QStringLiteral("Trainer rejected the gradient command (Invalid parameter)"));
        QCOMPARE(ftms::rejectionText(0x05, 0x04), QStringLiteral("Trainer rejected the target power command (Operation failed)"));
        QVERIFY(ftms::rejectionText(0x00, 0x05).startsWith(QStringLiteral("Trainer refused control")));
        QCOMPARE(ftms::timeoutText(0x11, 2000), QStringLiteral("Trainer did not answer the gradient command (no response within 2 s)"));
    }
};

QTEST_GUILESS_MAIN(TestFtmsControl)
#include "tst_ftmscontrol.moc"
