// Gradient simulation on the device side: the virtual trainer ("sim:trainer")
// resists like the road, DeviceManager forwards SensorHub simulation requests
// and reports whether the trainer can simulate.
#include "Database.h"
#include "DeviceManager.h"
#include "SensorHub.h"
#include "SimulatedDevices.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>

using namespace watt;

namespace {
// average power over n simulated seconds
double ride(SimulatedTrainer &t, int n, QSignalSpy &power)
{
    power.clear();
    for (int i = 0; i < n; ++i)
        t.step();
    double sum = 0;
    for (const auto &args : power)
        sum += args.at(0).toInt();
    return power.isEmpty() ? 0.0 : sum / power.size();
}
} // namespace

class TestTrainerSim : public QObject
{
    Q_OBJECT
private slots:
    void brakeFollowsTheRoad()
    {
        SimulatedTrainer t;
        t.setNoise(false);
        t.setSimulation(0.0, 0.0, SensorHub::kDefaultCrr, SensorHub::kDefaultCw);
        QVERIFY(t.simulating());
        QCOMPARE(t.simulationSupport(), 1);
        QVERIFY(t.capabilities().contains(QStringLiteral("Gradient Simulation")));
        // P = (Crr·m·g + ½·Cw·v²)·v on the flat
        const double v = 8.0;
        const double expected = (0.004 * SimulatedTrainer::kSimMassKg * 9.80665 + 0.5 * 0.51 * v * v) * v;
        QVERIFY(std::abs(t.brakePower(v) - expected) < 0.01);
        t.setSimulation(5.0, 0.0, 0.004, 0.51);
        QVERIFY(t.brakePower(v) > expected + 250);   // + m·g·sinθ·v ≈ 325 W
        t.setSimulation(-8.0, 0.0, 0.004, 0.51);
        QCOMPARE(t.brakePower(v), 0.0);               // gravity wins: no negative brake power
        // headwind adds drag
        t.setSimulation(0.0, 3.0, 0.004, 0.51);
        QVERIFY(t.brakePower(v) > expected);
    }

    void riderShiftsOnClimbs()
    {
        SimulatedTrainer t;
        t.setNoise(false);
        t.setFreeRidePower(160);
        QSignalSpy power(&t, &TrainerDevice::powerMeasured);
        QSignalSpy speed(&t, &TrainerDevice::speedMeasured);
        t.setSimulation(0.0, 0.0, 0.004, 0.51);
        ride(t, 20, power);
        const double flat = ride(t, 20, power);
        const int flatGear = t.gearIndex();
        const double flatSpeed = t.wheelSpeedMps();
        QVERIFY2(flat > 120 && flat < 200, qPrintable(QString::number(flat)));
        QVERIFY(std::abs(speed.last().at(0).toDouble() - flatSpeed * 3.6) < 1e-6);
        // a 6 % climb: the pedals get heavy at once ...
        t.setSimulation(6.0, 0.0, 0.004, 0.51);
        power.clear();
        t.step();
        QVERIFY2(power.last().at(0).toInt() > flat * 1.8, qPrintable(QString::number(power.last().at(0).toInt())));
        // ... then the rider shifts down and settles at a higher effort, slower
        ride(t, 20, power);
        const double climb = ride(t, 20, power);
        QVERIFY(t.gearIndex() < flatGear);
        QVERIFY(t.wheelSpeedMps() < flatSpeed * 0.7);
        QVERIFY2(climb > flat + 25 && climb < flat * 2.2, qPrintable(QStringLiteral("%1 %2").arg(flat).arg(climb)));
        // downhill: easy, the rider shifts up
        t.setSimulation(-4.0, 0.0, 0.004, 0.51);
        const double down = ride(t, 30, power);
        QVERIFY(down < flat);
        QVERIFY(t.gearIndex() > flatGear);
        // ERG overrides the simulation
        t.setTargetPower(220);
        ride(t, 10, power);
        QVERIFY(!t.simulating());
        QVERIFY(std::abs(t.currentPower() - 220) < 2);
    }

    void noSimulationSupport()
    {
        SimulatedTrainer t;
        QSignalSpy support(&t, &TrainerDevice::simulationSupportChanged);
        t.setSimulationSupported(false);
        QCOMPARE(support.count(), 1);
        QCOMPARE(t.simulationSupport(), 0);
        QVERIFY(!t.capabilities().contains(QStringLiteral("Gradient Simulation")));
        t.setSimulation(5.0, 0.0, 0.004, 0.51);
        QVERIFY(!t.simulating());   // ignored, like "Op Code not supported"
    }

    void hubSimulationRequests()
    {
        SensorHub hub;
        QSignalSpy sim(&hub, &SensorHub::simulationRequested);
        QSignalSpy erg(&hub, &SensorHub::targetPowerRequested);
        hub.requestTargetPower(200);
        QVERIFY(hub.ergActive());
        hub.requestSimulation(3.5);
        QCOMPARE(sim.count(), 1);
        QCOMPARE(sim.last().at(0).toDouble(), 3.5);
        QCOMPARE(sim.last().at(1).toDouble(), 0.0);
        QCOMPARE(sim.last().at(2).toDouble(), 0.004);
        QCOMPARE(sim.last().at(3).toDouble(), 0.51);
        QVERIFY(hub.simulationActive());
        QVERIFY(!hub.ergActive());
        QCOMPARE(hub.requestedTarget(), 0);
        QCOMPARE(hub.requestedGrade(), 3.5);
        // re-applied after a reconnect
        hub.setTrainerConnected(true);
        QCOMPARE(sim.count(), 2);
        hub.setTrainerConnected(false);
        hub.requestFreeRide();
        QVERIFY(!hub.simulationActive());
        hub.setTrainerConnected(true);
        QCOMPARE(sim.count(), 2);
        QCOMPARE(erg.count(), 1);
        QSignalSpy support(&hub, &SensorHub::trainerSimulationSupportChanged);
        QCOMPARE(hub.trainerSimulationSupport(), -1);
        hub.setTrainerSimulationSupport(0);
        QVERIFY(hub.trainerSimulationUnsupported());
        QCOMPARE(support.count(), 1);
        hub.setTrainerSimulationSupport(0);
        QCOMPARE(support.count(), 1);
    }

    void deviceManagerForwardsSimulation()
    {
        QTemporaryDir dir;
        Database db;
        QVERIFY(db.open(dir.filePath(QStringLiteral("t.db"))));
        SensorHub hub;
        DeviceManager mgr(&db, &hub);
        mgr.setSimulatorEnabled(false);
        mgr.setSimulatorEnabled(true);   // adds the virtual devices without Bluetooth
        mgr.connectDevice(QStringLiteral("sim:trainer"));
        QTRY_VERIFY_WITH_TIMEOUT(hub.trainerConnected(), 3000);
        auto *trainer = mgr.findChild<SimulatedTrainer *>();
        QVERIFY(trainer);
        QCOMPARE(hub.trainerSimulationSupport(), 1);
        hub.requestSimulation(4.25);
        QVERIFY(trainer->simulating());
        QCOMPARE(trainer->simulationGrade(), 4.25);
        hub.requestTargetPower(180);
        QVERIFY(!trainer->simulating());
        hub.requestSimulation(-2.0);
        QVERIFY(trainer->simulating());
        hub.requestFreeRide();
        QVERIFY(!trainer->simulating());
        // a trainer that cannot simulate is reported to the hub
        trainer->setSimulationSupported(false);
        QCOMPARE(hub.trainerSimulationSupport(), 0);
        mgr.disconnectDevice(QStringLiteral("sim:trainer"));
    }
};

QTEST_GUILESS_MAIN(TestTrainerSim)
#include "tst_trainersim.moc"
