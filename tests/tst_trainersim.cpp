// Gradient simulation on the device side: the virtual trainer ("sim:trainer")
// resists like the road, DeviceManager forwards SensorHub simulation requests
// and reports whether the trainer can simulate.
#include "Database.h"
#include "DeviceManager.h"
#include "FreeRideSession.h"
#include "Gearing.h"
#include "ProfileManager.h"
#include "SensorHub.h"
#include "SimulatedDevices.h"
#include "VirtualGears.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>
#include <limits>

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
        t.setSimulation(0.0, 0.0, 0.004, 0.51);
        QVERIFY(t.simulating());
        QCOMPARE(t.simulationSupport(), 1);
        QVERIFY(t.capabilities().contains(QStringLiteral("Gradient Simulation")));
        // P = (Crr·m·g + ½·Cw·v²)·v on the flat, m = its weight setting
        QCOMPARE(t.massKg(), SimulatedTrainer::kDefaultMassKg);
        const double v = 8.0;
        const double expected = (0.004 * t.massKg() * 9.80665 + 0.5 * 0.51 * v * v) * v;
        QVERIFY(std::abs(t.brakePower(v) - expected) < 0.01);
        // Cw as sent: the rider's ρ·CdA
        t.setSimulation(0.0, 0.0, 0.004, 0.39);
        QVERIFY(std::abs(t.brakePower(v) - (0.004 * t.massKg() * 9.80665 + 0.5 * 0.39 * v * v) * v) < 0.01);
        // a lighter rider + bike: less to lift
        t.setSimulation(5.0, 0.0, 0.004, 0.39);
        const double heavy = t.brakePower(v);
        t.setMassKg(69.0);
        QCOMPARE(t.massKg(), 69.0);
        QVERIFY(t.brakePower(v) < heavy - 50);
        t.setMassKg(5.0);   // nonsense is ignored
        QCOMPARE(t.massKg(), 69.0);
        t.setMassKg(SimulatedTrainer::kDefaultMassKg);
        t.setSimulation(0.0, 0.0, 0.004, 0.51);
        t.setSimulation(5.0, 0.0, 0.004, 0.51);
        QVERIFY(t.brakePower(v) > expected + 250);   // + m·g·sinθ·v ≈ 325 W
        t.setSimulation(-8.0, 0.0, 0.004, 0.51);
        QCOMPARE(t.brakePower(v), 0.0);               // gravity wins: no negative brake power
        // headwind adds drag
        t.setSimulation(0.0, 3.0, 0.004, 0.51);
        QVERIFY(t.brakePower(v) > expected);
    }

    // The sim reports crank power (brake/wheel power / η, the app's power
    // convention, Gearing.h); ERG holds the requested crank watts.
    void reportsCrankPower()
    {
        SimulatedTrainer t;
        t.setNoise(false);
        t.setSimulation(3.0, 0.0, 0.004, 0.39);
        const double v = 8.0;
        QVERIFY(std::abs(t.crankPower(v) - t.brakePower(v) / road::kDrivetrainEfficiency) < 1e-9);
        QVERIFY(t.crankPower(v) > t.brakePower(v));
        // virtual gears (fixed reference gear, no FTP limit): the shown power
        // settles at the crank power for the wheel speed
        t.setFixedGearRatio(50.0 / 17.0);
        QSignalSpy power(&t, &TrainerDevice::powerMeasured);
        for (int i = 0; i < 30; ++i)
            t.step();
        const double crank = t.crankPower(t.wheelSpeedMps());
        QVERIFY2(std::abs(power.last().at(0).toInt() - crank) <= 1.0,
                 qPrintable(QStringLiteral("%1 vs %2 W").arg(power.last().at(0).toInt()).arg(crank)));
        // ERG: the requested watts are crank watts, reported as such
        t.setTargetPower(230);
        for (int i = 0; i < 30; ++i)
            t.step();
        QCOMPARE(power.last().at(0).toInt(), 230);
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

    void nonFiniteSimulationIgnored()
    {
        SimulatedTrainer t;
        t.setNoise(false);
        t.setSimulation(2.0, 1.0, 0.005, 0.40);
        const double ref = t.brakePower(8.0);
        QVERIFY(std::isfinite(ref));
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double inf = std::numeric_limits<double>::infinity();
        // NaN Crr / Cw keep the last valid values; NaN wind / grade = 0
        t.setSimulation(2.0, 1.0, nan, inf);
        QVERIFY(std::abs(t.brakePower(8.0) - ref) < 1e-9);
        t.setSimulation(nan, nan, -1.0, nan);
        QCOMPARE(t.simulationGrade(), 0.0);
        QVERIFY(std::isfinite(t.brakePower(8.0)));
        QVERIFY(std::abs(t.brakePower(8.0) - t.brakePowerAt(8.0, 0.0)) < 1e-9);
        const double expected = (0.005 * t.massKg() * 9.80665 + 0.5 * 0.40 * 8.0 * 8.0) * 8.0;
        QVERIFY(std::abs(t.brakePower(8.0) - expected) < 1e-6);
        QSignalSpy power(&t, &TrainerDevice::powerMeasured);
        for (int i = 0; i < 5; ++i)
            t.step();
        for (const auto &args : power)
            QVERIFY(args.at(0).toInt() >= 0 && args.at(0).toInt() < 5000);
    }

    void riderGoalWithZeroFreeRidePower()
    {
        // base = free ride power + 15·sin(t/40) went negative: std::clamp(v,
        // 0.5·base, 1.8·base) with lo > hi is undefined behaviour
        SimulatedTrainer t;
        t.setNoise(false);
        t.setFreeRidePower(0);
        t.setSimulation(0.0, 0.0, 0.004, 0.39);
        for (int i = 0; i < 260; ++i) {   // sin(t/40) < 0 for t in 126..251
            t.step();
            const double goal = t.riderGoal();
            QVERIFY2(std::isfinite(goal) && goal >= 0.5 && goal <= 1.8 * 16.0,
                     qPrintable(QStringLiteral("t=%1 goal=%2").arg(i).arg(goal)));
        }
    }

    void stopErgTestRestoresWind()
    {
        QTemporaryDir dir;
        Database db;
        QVERIFY(db.open(dir.filePath(QStringLiteral("t.db"))));
        SensorHub hub;
        DeviceManager mgr(&db, &hub);
        mgr.setSimulatorEnabled(false);
        mgr.setSimulatorEnabled(true);
        mgr.connectDevice(QStringLiteral("sim:trainer"));
        QTRY_VERIFY_WITH_TIMEOUT(hub.trainerConnected(), 3000);
        auto *trainer = mgr.findChild<SimulatedTrainer *>();
        QVERIFY(trainer);
        hub.requestSimulation(3.0, 2.5, 0.004, 0.39);
        const double withWind = trainer->brakePower(8.0);
        mgr.startErgTest(150);
        QVERIFY(!trainer->simulating());
        mgr.stopErgTest();
        QVERIFY(trainer->simulating());
        QCOMPARE(trainer->simulationGrade(), 3.0);
        // the wind of the last request is back (it used to be reset to 0)
        QVERIFY2(std::abs(trainer->brakePower(8.0) - withWind) < 1e-9,
                 qPrintable(QStringLiteral("%1 vs %2").arg(trainer->brakePower(8.0)).arg(withWind)));
        mgr.disconnectDevice(QStringLiteral("sim:trainer"));
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
        QCOMPARE(sim.last().at(3).toDouble(), SensorHub::kDefaultCw);
        QCOMPARE(SensorHub::kDefaultCw, 0.39);   // ρ·CdA = 1.225 · 0.32, not Zwift's 0.51
        QCOMPARE(hub.simulationCw(), 0.39);
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
        // the virtual trainer simulates with the hub's rider + bike and the
        // rider's FTP - no hard-coded weight
        QCOMPARE(trainer->massKg(), hub.riderMassKg());
        hub.setRiderMassKg(69.0);
        QCOMPARE(trainer->massKg(), 69.0);
        hub.setRiderMassKg(99.0);
        QCOMPARE(trainer->massKg(), 99.0);
        mgr.setRiderPhysiology(60, 180, 210);
        QCOMPARE(trainer->riderFtp(), 210);
        hub.requestSimulation(4.25, 0.0, 0.004, 0.34);
        QCOMPARE(trainer->simulationGrade(), 4.25);
        QVERIFY(std::abs(trainer->brakePower(8.0) - trainer->brakePowerAt(8.0, 4.25)) < 1e-9);
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

    // End to end on the virtual trainer: ERG on the Free Ride page's mode
    // selector reaches the trainer through DeviceManager right away (idle),
    // Ride gives it back, a started ride keeps ERG.
    void freeRideErgSelectorReachesTheTrainer()
    {
        QTemporaryDir dir;
        Database db;
        QVERIFY(db.open(dir.filePath(QStringLiteral("t.db"))));
        db.seedDefaults();
        ProfileManager profiles(&db);
        profiles.load();
        SensorHub hub;
        DeviceManager mgr(&db, &hub);
        mgr.setSimulatorEnabled(false);
        mgr.setSimulatorEnabled(true);   // adds the virtual devices without Bluetooth
        VirtualGears gears(&db, &profiles);
        FreeRideSession ride(&db, &profiles, &hub, &gears);
        ride.setManualClock(true);
        gears.setSessions(&ride, nullptr);
        gears.setActivePage(QStringLiteral("freeride"));
        ride.setTargetWatts(220);
        ride.setMode(QStringLiteral("erg"));   // before the trainer connects
        QVERIFY(!ride.ergHolding());
        mgr.connectDevice(QStringLiteral("sim:trainer"));
        QTRY_VERIFY_WITH_TIMEOUT(hub.trainerConnected(), 3000);
        auto *trainer = mgr.findChild<SimulatedTrainer *>();
        QVERIFY(trainer);
        QVERIFY(ride.ergHolding());
        QVERIFY(trainer->ergActive());
        QCOMPARE(trainer->targetPower(), 220);
        ride.setMode(QStringLiteral("ride"));
        QVERIFY(!trainer->ergActive());
        ride.setProperty("mode", QStringLiteral("erg"));   // what the QML selector does
        QVERIFY(trainer->ergActive());
        QCOMPARE(trainer->targetPower(), 220);
        gears.controllerShift(-1);   // Zwift Click -10 W
        QCOMPARE(trainer->targetPower(), 210);
        QVERIFY(ride.start());
        QVERIFY(trainer->ergActive());
        QCOMPARE(trainer->targetPower(), 210);
        ride.setMode(QStringLiteral("ride"));
        QVERIFY(!trainer->ergActive());   // simulation (or gear watts) again
        ride.setMode(QStringLiteral("erg"));
        QVERIFY(trainer->ergActive());
        ride.finish(false);
        QVERIFY(!trainer->ergActive());
        mgr.disconnectDevice(QStringLiteral("sim:trainer"));
    }
};

QTEST_GUILESS_MAIN(TestTrainerSim)
#include "tst_trainersim.moc"
