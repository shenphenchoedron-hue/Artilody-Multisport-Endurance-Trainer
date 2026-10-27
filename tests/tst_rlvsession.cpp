// RlvSession: rider position, video sync (rate / seek / hold), resistance
// modes (gradient simulation with fallback, virtual gears, ERG) with
// difficulty, speed from power with inertia, auto-finish + saved ride,
// mutual exclusion, shared gearing with the free ride, mode persistence.
#include "Database.h"
#include "FreeRideSession.h"
#include "VirtualGears.h"
#include "ProfileManager.h"
#include "RlvLibrary.h"
#include "RlvSession.h"
#include "SensorHub.h"
#include "TrainingSession.h"

#include <QFile>
#include <QSignalSpy>
#include <QThread>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>
#include <memory>

using namespace watt;

namespace {

const double kMPerDegLat = RlvRoute::haversine(0, 0, 1, 0);

// Straight route north, 1 point per second at `speed` m/s; grade(d) in %.
bool writeRoute(const QString &dir, const QString &name, double lengthM, double speed,
                const std::function<double(double)> &grade, const QByteArray &infos = {})
{
    QByteArray gpx = "<?xml version=\"1.0\"?>\n<gpx version=\"1.1\" xmlns=\"http://www.topografix.com/GPX/1/1\">"
                     "<trk><trkseg>\n";
    const QDateTime t0 = QDateTime::fromString(QStringLiteral("1999-12-31T23:00:00Z"), Qt::ISODate);
    double ele = 300, d = 0;
    for (int i = 0; d <= lengthM + 1e-6; ++i, d += speed) {
        gpx += QStringLiteral("<trkpt lat=\"%1\" lon=\"6.0\"><ele>%2</ele><time>%3</time></trkpt>\n")
                   .arg(45.0 + d / kMPerDegLat, 0, 'f', 8)
                   .arg(ele, 0, 'f', 3)
                   .arg(t0.addSecs(i).toString(Qt::ISODate))
                   .toUtf8();
        ele += grade(d + speed / 2) / 100.0 * speed;
    }
    gpx += "</trkseg></trk></gpx>\n";
    QFile g(dir + QLatin1Char('/') + name + QStringLiteral(".gpx"));
    QFile v(dir + QLatin1Char('/') + name + QStringLiteral(".mp4"));
    if (!g.open(QIODevice::WriteOnly) || !v.open(QIODevice::WriteOnly))
        return false;
    g.write(gpx);
    v.write("not a real video");
    if (!infos.isEmpty()) {
        QFile x(dir + QLatin1Char('/') + name + QStringLiteral(".xml"));
        if (!x.open(QIODevice::WriteOnly))
            return false;
        x.write("<gpx-import><name>" + name.toUtf8() + "</name><title>Test Title</title><country>DK</country>"
                "<video-file-path>" + name.toUtf8() + ".mp4</video-file-path><gpx-file-path>" + name.toUtf8()
                + ".gpx</gpx-file-path><framerate>30</framerate><start-frame>1</start-frame><informations>"
                + infos + "</informations></gpx-import>");
    }
    return true;
}

struct Fixture
{
    QTemporaryDir dir;
    Database db;
    std::unique_ptr<ProfileManager> profiles;
    std::unique_ptr<SensorHub> hub;
    std::unique_ptr<TrainingSession> training;
    std::unique_ptr<VirtualGears> gears;
    std::unique_ptr<FreeRideSession> freeRide;
    std::unique_ptr<RlvLibrary> library;
    std::unique_ptr<RlvSession> rlv;

    bool init()
    {
        if (!dir.isValid() || !db.open(QStringLiteral(":memory:")))
            return false;
        db.seedDefaults();
        profiles = std::make_unique<ProfileManager>(&db);
        profiles->load();
        hub = std::make_unique<SensorHub>();
        hub->setStaleTimeoutMs(600000);
        hub->setTrainerConnected(true);
        training = std::make_unique<TrainingSession>(&db, profiles.get(), hub.get());
        training->setManualClock(true);
        gears = std::make_unique<VirtualGears>(&db, profiles.get());
        gears->setManualSave(true);
        freeRide = std::make_unique<FreeRideSession>(&db, profiles.get(), hub.get(), gears.get());
        freeRide->setManualClock(true);
        freeRide->setTrainingSession(training.get());
        library = std::make_unique<RlvLibrary>(&db);
        library->setLibraryFolder(dir.path());
        rlv = std::make_unique<RlvSession>(&db, profiles.get(), hub.get(), gears.get(), library.get());
        rlv->setManualClock(true);
        rlv->setMinimumSaveSeconds(5);
        rlv->setFreeRideSession(freeRide.get());
        rlv->setTrainingSession(training.get());
        rlv->setStartBlocker(QStringLiteral("freeRide"), [this] { return freeRide->active(); });
        freeRide->setStartBlocker(QStringLiteral("rlv"), [this] { return rlv->active() || rlv->waiting(); });
        gears->setSessions(freeRide.get(), rlv.get());
        return true;
    }
    // simulation with virtual gears on a trainer without gradient
    // simulation: path b, ERG
    void useGears()
    {
        rlv->setMode(QStringLiteral("sim"));
        hub->setTrainerSimulationSupport(0);
        gears->setEnabled(true);
    }
    bool addRoute(const QString &name, double lengthM, double speed, const std::function<double(double)> &grade,
                  const QByteArray &infos = {})
    {
        if (!QDir(dir.path()).mkpath(name))
            return false;
        if (!writeRoute(dir.filePath(name), name, lengthM, speed, grade, infos))
            return false;
        library->rescanNow();
        return true;
    }
    bool selectRoute(const QString &name)
    {
        return rlv->selectPath(dir.filePath(name));
    }
    void pedal(int cadence, int watts)
    {
        hub->updateCadence(cadence);
        hub->updatePower(watts);
    }
    void ride_for(int ms)
    {
        for (int t = 0; t < ms && (rlv->running() || rlv->waiting()); t += 250)
            rlv->advanceClock(250);
    }
};

double flat(double) { return 0.0; }

} // namespace

class TestRlvSession : public QObject
{
    Q_OBJECT
private slots:
    void idleIsSafeWithoutRoute()
    {
        Database db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        db.seedDefaults();
        ProfileManager profiles(&db);
        profiles.load();
        SensorHub hub;
        VirtualGears gears(&db, &profiles);
        RlvSession s(&db, &profiles, &hub, &gears);
        QSignalSpy refused(&s, &RlvSession::startRefused);
        QCOMPARE(s.state(), QStringLiteral("idle"));
        QVERIFY(!s.hasRoute());
        QCOMPARE(s.routeIndex(), -1);
        QVERIFY(s.videoUrl().isEmpty());
        QVERIFY(s.profile().isEmpty());
        QVERIFY(s.informations().isEmpty());
        QCOMPARE(s.totalDistanceM(), 0.0);
        QCOMPARE(s.progress(), 0.0);
        QCOMPARE(s.gradePercent(), 0.0);
        QCOMPARE(s.videoPosition(), qint64(0));
        QVERIFY(!s.videoShouldPlay());
        QCOMPARE(s.speedKmh(), 0.0);
        QCOMPARE(s.difficultyPercent(), 100);
        QCOMPARE(s.mode(), QStringLiteral("sim"));   // classic RLV by default
        QVERIFY(!s.simulationFallback());
        QVERIFY(!s.virtualGears());
        QCOMPARE(s.gearing().label(), QStringLiteral("50×21"));   // the shared gears (profile setup)
        QCOMPARE(s.resistancePath(), QStringLiteral("simulation"));
        QVERIFY(!s.start());
        QCOMPARE(refused.count(), 1);
        s.startWhenPedaling();
        QCOMPARE(s.state(), QStringLiteral("idle"));
        QVERIFY(!s.select(0));
        QVERIFY(!s.selectPath(QStringLiteral("/no/such/route")));
        QVERIFY(!s.lastError().isEmpty());
        QVERIFY(!s.seekToDistance(100));
        QCOMPARE(s.finish(true), qint64(-1));
        s.pause();
        s.resume();
        s.reportVideoPosition(1000);
        s.advanceClock(250);
        QCOMPARE(s.state(), QStringLiteral("idle"));
        QCOMPARE(s.formatDistance(850), QStringLiteral("850 m"));
        QCOMPARE(s.formatDistance(1250), QStringLiteral("1.25 km"));
    }

    void selectRoute()
    {
        Fixture f;
        QVERIFY(f.init());
        QVERIFY(f.addRoute(QStringLiteral("route"), 3000, 10, flat,
                           "<information distance=\"100\" en=\"Hello\\nWorld\" de=\"Hallo\"/>"));
        QCOMPARE(f.library->count(), 1);
        QSignalSpy seeks(f.rlv.get(), &RlvSession::seekRequested);
        QVERIFY(f.rlv->select(0));
        QCOMPARE(seeks.count(), 1);
        QCOMPARE(seeks.at(0).at(0).toLongLong(), qint64(0));
        QVERIFY(f.rlv->hasRoute());
        QCOMPARE(f.rlv->routeIndex(), 0);
        QCOMPARE(f.rlv->routeName(), QStringLiteral("route"));
        QCOMPARE(f.rlv->routeTitle(), QStringLiteral("Test Title"));
        QCOMPARE(f.rlv->routeCountry(), QStringLiteral("DK"));
        QVERIFY(f.rlv->videoUrl().isLocalFile());
        QVERIFY(f.rlv->videoUrl().toLocalFile().endsWith(QStringLiteral("route.mp4")));
        QVERIFY(std::abs(f.rlv->totalDistanceM() - 3000) < 1);
        QCOMPARE(f.rlv->routeDurationSec(), 301);
        QCOMPARE(f.rlv->profile().size(), RlvSession::kProfileSamples);
        QCOMPARE(f.rlv->informations().size(), 1);
        QCOMPARE(f.rlv->informations().at(0).toMap().value(QStringLiteral("text")).toString(),
                 QStringLiteral("Hello\nWorld"));
        QCOMPARE(f.rlv->currentInformation(), QString());
        QVERIFY(f.rlv->seekToDistance(120));
        QCOMPARE(f.rlv->currentInformation(), QStringLiteral("Hello\nWorld"));
        QCOMPARE(seeks.count(), 2);
        QCOMPARE(seeks.at(1).at(0).toLongLong(), qint64(12000));
        // remembered for the next start
        RlvSession again(&f.db, f.profiles.get(), f.hub.get(), f.gears.get(), f.library.get());
        QVERIFY(again.hasRoute());
        QCOMPARE(again.routeName(), QStringLiteral("route"));
    }

    void rideAdvancesAndVideoFollows()
    {
        Fixture f;
        QVERIFY(f.init());
        f.useGears();
        QVERIFY(f.addRoute(QStringLiteral("flat"), 3000, 10, flat));
        QVERIFY(f.selectRoute(QStringLiteral("flat")));
        QSignalSpy targets(f.hub.get(), &SensorHub::targetPowerRequested);
        f.pedal(90, 150);
        QVERIFY(f.rlv->start());
        QVERIFY(f.rlv->running());
        const double v = f.gears->gearing().virtualSpeed(90);   // shared gearing, ≈ 7.5 m/s
        f.ride_for(10000);
        QCOMPARE(f.rlv->elapsedSeconds(), 10);
        QVERIFY(std::abs(f.rlv->distanceMeters() - v * 10) < 0.5);
        QVERIFY(std::abs(f.rlv->speedKmh() - v * 3.6) < 0.01);
        QVERIFY(std::abs(f.rlv->videoPosition() - f.rlv->distanceMeters() / 10.0 * 1000) < 60);
        QVERIFY(f.rlv->videoShouldPlay());
        // rate = rider / filmed speed, quantised to 0.05
        QCOMPARE(f.rlv->playbackRate(), std::round(v / 10.0 / 0.05) * 0.05);
        QVERIFY(std::abs(f.rlv->progress() - f.rlv->distanceMeters() / f.rlv->totalDistanceM()) < 1e-9);
        QVERIFY(std::abs(f.rlv->remainingMeters() - (f.rlv->totalDistanceM() - f.rlv->distanceMeters())) < 1e-9);
        // ERG target = road power for the virtual speed on a flat road
        const int expected = int(std::lround(road::powerForSpeed(f.rlv->roadModel(0.0), v)));
        QVERIFY(std::abs(f.rlv->sentTargetWatts() - expected) <= 2);
        QVERIFY(targets.count() >= 1);
        QCOMPARE(f.rlv->samples().size(), 10);
        QVERIFY(std::abs(f.rlv->samples().at(5).speedMps - v) < 0.01);
        QVERIFY(std::abs(f.rlv->avgSpeedKmh() - v * 3.6) < 0.1);
        QCOMPARE(f.rlv->avgWatts(), 150);
        // faster filmed parts play faster: double cadence ~ double rate
        f.pedal(110, 250);
        f.ride_for(3000);
        QVERIFY(f.rlv->playbackRate() > 0.85);
        f.rlv->finish(false);
        QCOMPARE(f.hub->requestedTarget(), 0);
        QVERIFY(!f.rlv->videoShouldPlay());
    }

    void videoPausesWhenRiderStops()
    {
        Fixture f;
        QVERIFY(f.init());
        f.useGears();
        QVERIFY(f.addRoute(QStringLiteral("flat"), 5000, 10, flat));
        QVERIFY(f.selectRoute(QStringLiteral("flat")));
        f.pedal(90, 150);
        QVERIFY(f.rlv->start());
        f.ride_for(5000);
        QVERIFY(f.rlv->videoShouldPlay());
        // stop pedaling: the virtual bike rolls out, the video stops below 2 km/h
        f.pedal(0, 0);
        f.ride_for(1000);
        QVERIFY(f.rlv->coasting());
        QCOMPARE(f.hub->requestedTarget(), 0);   // coasting releases the trainer
        QVERIFY(f.rlv->speedKmh() > 20);         // still rolling
        QVERIFY(f.rlv->videoShouldPlay());
        f.ride_for(150000);
        QCOMPARE(f.rlv->speedKmh(), 0.0);
        QVERIFY(!f.rlv->videoShouldPlay());
        const double d = f.rlv->distanceMeters();
        f.ride_for(5000);
        QCOMPARE(f.rlv->distanceMeters(), d);
        // pause: video off, trainer released, clock stopped
        f.pedal(90, 150);
        f.ride_for(2000);
        QVERIFY(f.rlv->videoShouldPlay());
        const int elapsed = f.rlv->elapsedSeconds();
        f.rlv->pause();
        QVERIFY(f.rlv->paused());
        QVERIFY(!f.rlv->videoShouldPlay());
        QCOMPARE(f.hub->requestedTarget(), 0);
        f.rlv->advanceClock(5000);
        QCOMPARE(f.rlv->elapsedSeconds(), elapsed);
        f.rlv->resume();
        QVERIFY(f.hub->requestedTarget() > 0);
        f.ride_for(1000);
        QVERIFY(f.rlv->videoShouldPlay());
        f.rlv->finish(false);
    }

    void driftCorrection()
    {
        Fixture f;
        QVERIFY(f.init());
        f.useGears();
        QVERIFY(f.addRoute(QStringLiteral("flat"), 5000, 10, flat));
        QVERIFY(f.selectRoute(QStringLiteral("flat")));
        QSignalSpy seeks(f.rlv.get(), &RlvSession::seekRequested);
        f.pedal(90, 150);
        QVERIFY(f.rlv->start());
        QCOMPARE(seeks.count(), 1);   // start seeks to the start position
        f.ride_for(5000);
        const double base = f.rlv->playbackRate();
        // video 3 s behind -> hard seek to the target
        f.rlv->reportVideoPosition(f.rlv->videoPosition() - 3000);
        f.ride_for(250);
        QCOMPARE(seeks.count(), 2);
        QVERIFY(std::abs(seeks.last().at(0).toLongLong() - f.rlv->videoPosition()) < 300);
        // reports right after a seek are ignored (player still settling)
        f.rlv->reportVideoPosition(f.rlv->videoPosition() - 5000);
        f.ride_for(250);
        QCOMPARE(seeks.count(), 2);
        f.ride_for(2000);
        // 0.5 s ahead -> slightly slower, no seek
        f.rlv->reportVideoPosition(f.rlv->videoPosition() + 500);
        f.ride_for(250);
        QCOMPARE(seeks.count(), 2);
        QVERIFY(f.rlv->videoDriftSec() > 0.3);
        QVERIFY(f.rlv->playbackRate() < base);
        QVERIFY(f.rlv->playbackRate() >= base * 0.9 - 0.05);
        // 1 s behind -> slightly faster (a small rate change waits for
        // kRateMinIntervalMs after the last one: fewer flushing seeks)
        const double slower = f.rlv->playbackRate();
        f.rlv->reportVideoPosition(f.rlv->videoPosition() - 1000);
        f.ride_for(250);
        QCOMPARE(f.rlv->playbackRate(), slower);
        f.ride_for(RlvSession::kRateMinIntervalMs);
        QVERIFY(f.rlv->playbackRate() > base);
        QVERIFY(f.rlv->playbackRate() <= base * 1.1 + 0.05);
        // stale reports are ignored
        f.ride_for(4000);
        QCOMPARE(f.rlv->videoDriftSec(), 0.0);
        QCOMPARE(f.rlv->playbackRate(), base);

        // very slow rider (below the minimum rate): the video waits instead of running ahead
        QVERIFY(f.gears->shiftDown());
        while (f.gears->shiftDown()) { }
        f.pedal(25, 80);
        f.ride_for(500);
        QVERIFY(f.rlv->speedKmh() >= RlvSession::kMinPlaySpeedKmh);
        QCOMPARE(f.rlv->playbackRate(), RlvSession::kMinRate);
        QVERIFY(f.rlv->videoShouldPlay());
        f.rlv->reportVideoPosition(f.rlv->videoPosition() + 1000);
        f.ride_for(250);
        QVERIFY(!f.rlv->videoShouldPlay());   // hold
        const int seeksBefore = int(seeks.count());
        f.rlv->reportVideoPosition(f.rlv->videoPosition() - 50);   // caught up
        f.ride_for(250);
        QVERIFY(f.rlv->videoShouldPlay());
        QCOMPARE(int(seeks.count()), seeksBefore);
        f.rlv->finish(false);
    }

    void difficultyScalesGrade()
    {
        Fixture f;
        QVERIFY(f.init());
        f.useGears();
        QVERIFY(f.addRoute(QStringLiteral("hill"), 4000, 5, [](double d) { return d < 2000 ? 6.0 : -5.0; }));
        QVERIFY(f.selectRoute(QStringLiteral("hill")));
        QVERIFY(f.rlv->seekToDistance(1000));
        QCOMPARE(f.rlv->startDistanceM(), 1000.0);
        QVERIFY(std::abs(f.rlv->gradePercent() - 6.0) < 0.1);
        QVERIFY(std::abs(f.rlv->effectiveGradePercent() - 6.0) < 0.1);
        f.pedal(80, 200);
        QVERIFY(f.rlv->start());
        QVERIFY(!f.rlv->seekToDistance(0));   // not while riding
        QVERIFY(std::abs(f.rlv->distanceMeters() - 1000) < 1e-9);
        f.ride_for(2000);
        const int full = f.rlv->sentTargetWatts();
        const double v = f.gears->gearing().virtualSpeed(80);
        QVERIFY(std::abs(full - road::powerForSpeed(f.rlv->roadModel(f.rlv->effectiveGradePercent()), v)) <= 3);
        f.rlv->setDifficulty(50);
        QVERIFY(std::abs(f.rlv->effectiveGradePercent() - 3.0) < 0.1);
        const int half = f.rlv->sentTargetWatts();   // applied at once
        f.rlv->setDifficulty(0);
        const int none = f.rlv->sentTargetWatts();
        QVERIFY2(full > half && half > none, qPrintable(QStringLiteral("%1 %2 %3").arg(full).arg(half).arg(none)));
        QVERIFY(std::abs(none - road::powerForSpeed(f.rlv->roadModel(0.0), v)) <= 3);
        f.rlv->setDifficulty(250);
        QCOMPARE(f.rlv->difficultyPercent(), 100);
        f.rlv->setDifficulty(50);
        QCOMPARE(f.db.setting(QStringLiteral("rlv/difficulty")).toInt(), 50);
        // descents are scaled too
        f.rlv->finish(false);
        f.rlv->reset();
        QVERIFY(f.rlv->seekToDistance(3000));
        QVERIFY(std::abs(f.rlv->gradePercent() + 5.0) < 0.1);
        QVERIFY(std::abs(f.rlv->effectiveGradePercent() + 2.5) < 0.1);
        // coasting downhill speeds up
        f.pedal(0, 0);
        QVERIFY(f.rlv->start());
        f.ride_for(60000);
        QVERIFY2(f.rlv->speedKmh() > 20, qPrintable(QString::number(f.rlv->speedKmh())));
        QVERIFY(f.rlv->ascentDoneM() < 0.5);
        f.rlv->finish(false);
    }

    void simulationSendsGradeRateLimited()
    {
        Fixture f;
        QVERIFY(f.init());
        // 0 % for 500 m, a steady ramp 0 -> 8 % over 1500 m, then 8 %
        QVERIFY(f.addRoute(QStringLiteral("ramp"), 5000, 5, [](double d) {
            return d < 500 ? 0.0 : d < 2000 ? (d - 500) / 1500.0 * 8.0 : 8.0;
        }));
        QVERIFY(f.selectRoute(QStringLiteral("ramp")));
        QCOMPARE(f.rlv->mode(), QStringLiteral("sim"));
        QSignalSpy sims(f.hub.get(), &SensorHub::simulationRequested);
        QSignalSpy ergs(f.hub.get(), &SensorHub::targetPowerRequested);
        QVERIFY(f.rlv->seekToDistance(400));
        f.pedal(85, 300);
        QVERIFY(f.rlv->start());
        QCOMPARE(sims.count(), 1);   // at once on start
        QVERIFY(std::abs(sims.last().at(0).toDouble() - f.rlv->simulationGradeToSend()) < 1e-9);
        QCOMPARE(sims.last().at(1).toDouble(), 0.0);
        QCOMPARE(sims.last().at(2).toDouble(), 0.004);
        // Cw = ρ·CdA of the rider (auto CdA from the profile weight), not Zwift's 0.51
        QCOMPARE(sims.last().at(3).toDouble(), road::trainerModelFor(f.rlv->roadModel(0)).cwKgPerM);
        QCOMPARE(sims.last().at(3).toDouble(), std::round(1.225 * road::autoCda(f.profiles->weight()) * 100) / 100);
        QVERIFY(f.hub->simulationActive());
        QCOMPARE(f.hub->requestedTarget(), 0);
        QVERIFY(f.rlv->simulationActive());
        QCOMPARE(f.rlv->gearTargetWatts(), 0);

        // ride up the ramp: every send >= 1 s apart, a new grade differs by
        // >= 0.1 % (or is the 3 s refresh), and matches the position
        qint64 now = 0, lastAt = 0;
        double lastGrade = sims.last().at(0).toDouble();
        int sends = 0, refreshes = 0;
        while (f.rlv->distanceMeters() < 2300 && now < 600000) {
            const int before = int(sims.count());
            f.rlv->advanceClock(250);
            now += 250;
            if (sims.count() == before)
                continue;
            QCOMPARE(int(sims.count()), before + 1);
            const double g = sims.last().at(0).toDouble();
            QVERIFY2(now - lastAt >= RlvSession::kSimMinIntervalMs, qPrintable(QString::number(now - lastAt)));
            QVERIFY2(now - lastAt <= RlvSession::kSimRefreshMs, qPrintable(QString::number(now - lastAt)));
            if (std::abs(g - lastGrade) < RlvSession::kSimMinGradeChange - 1e-9) {
                QCOMPARE(now - lastAt, qint64(RlvSession::kSimRefreshMs));
                ++refreshes;
            }
            QVERIFY(std::abs(g - f.rlv->simulationGradeToSend()) < 1e-9);
            QVERIFY(std::abs(g - std::round(f.rlv->effectiveGradePercent() * 100) / 100) < 0.011);
            QCOMPARE(f.rlv->simulationGradePercent(), g);
            lastAt = now;
            lastGrade = g;
            ++sends;
        }
        QVERIFY(sends >= 10);
        QVERIFY(lastGrade > 7.5);
        QVERIFY(refreshes >= 1);   // the flat start and the 8 % top only refresh
        QCOMPARE(ergs.count(), 0);   // never ERG in simulation mode
        QVERIFY(f.rlv->ascentDoneM() > 20);

        // difficulty: sent at once, grade × difficulty
        const int before = int(sims.count());
        f.rlv->setDifficulty(50);
        QCOMPARE(int(sims.count()), before + 1);
        QVERIFY(std::abs(sims.last().at(0).toDouble() - 4.0) < 0.05);
        // Zwift Click in simulation mode: shifts gears, never the difficulty
        const int gearBefore = f.gears->gearIndex();
        f.rlv->controllerShift(1);
        QCOMPARE(f.rlv->difficultyPercent(), 50);
        QCOMPARE(f.gears->gearIndex(), gearBefore + 1);
        f.rlv->controllerShift(-1);
        QCOMPARE(f.rlv->difficultyPercent(), 50);
        QCOMPARE(f.gears->gearIndex(), gearBefore);
        // keys PgUp / PgDn: difficulty ±10 %
        f.rlv->adjustDifficulty(10);
        QCOMPARE(f.rlv->difficultyPercent(), 60);
        QVERIFY(std::abs(sims.last().at(0).toDouble() - 4.8) < 0.05);
        f.rlv->adjustDifficulty(-10);
        // pause releases the trainer, resume sends the grade again
        f.rlv->pause();
        QVERIFY(!f.hub->simulationActive());
        QVERIFY(!f.rlv->simulationActive());
        const int paused = int(sims.count());
        f.rlv->resume();
        QCOMPARE(int(sims.count()), paused + 1);
        f.rlv->finish(false);
        QVERIFY(!f.hub->simulationActive());

        // steep descents are limited to kMinSimGrade
        QVERIFY(f.addRoute(QStringLiteral("down"), 3000, 5, [](double) { return -14.0; }));
        QVERIFY(f.selectRoute(QStringLiteral("down")));
        f.rlv->setDifficulty(100);
        QVERIFY(f.rlv->start());
        QCOMPARE(sims.last().at(0).toDouble(), RlvSession::kMinSimGrade);
        f.rlv->finish(false);
    }

    void speedFromPowerWithInertia()
    {
        Fixture f;
        QVERIFY(f.init());
        QVERIFY(f.addRoute(QStringLiteral("flat"), 20000, 10, flat));
        QVERIFY(f.selectRoute(QStringLiteral("flat")));
        f.pedal(85, 250);
        QVERIFY(f.rlv->start());
        // accelerates smoothly from 0: no jumps, always faster
        double prev = 0;
        for (int i = 0; i < 40; ++i) {
            f.rlv->advanceClock(250);
            const double v = f.rlv->speedMps();
            QVERIFY2(v > prev, qPrintable(QStringLiteral("%1 %2").arg(prev).arg(v)));
            QVERIFY2(v - prev < 1.0, qPrintable(QStringLiteral("%1 %2").arg(prev).arg(v)));
            prev = v;
        }
        const double steady250 = f.rlv->solveSpeedForPower(250, 0);
        QVERIFY(prev < steady250 * 0.9);   // still accelerating after 10 s
        f.ride_for(120000);
        QVERIFY2(std::abs(f.rlv->speedMps() - steady250) < 0.05,
                 qPrintable(QStringLiteral("%1 %2").arg(f.rlv->speedMps()).arg(steady250)));
        // less power: slows down smoothly
        f.pedal(85, 120);
        prev = f.rlv->speedMps();
        for (int i = 0; i < 40; ++i) {
            f.rlv->advanceClock(250);
            const double v = f.rlv->speedMps();
            QVERIFY(v < prev);
            QVERIFY(prev - v < 0.5);
            prev = v;
        }
        f.ride_for(150000);
        QVERIFY(std::abs(f.rlv->speedMps() - f.rlv->solveSpeedForPower(120, 0)) < 0.05);
        // stop pedaling: rolls out and stops on the flat
        f.pedal(0, 0);
        f.ride_for(2000);
        QVERIFY(f.rlv->speedKmh() > 15);
        f.ride_for(200000);
        QCOMPARE(f.rlv->speedMps(), 0.0);
        f.rlv->finish(false);

        // on a climb the same power is slower; steady state = road physics
        QVERIFY(f.addRoute(QStringLiteral("climb"), 8000, 5, [](double) { return 5.0; }));
        QVERIFY(f.selectRoute(QStringLiteral("climb")));
        f.pedal(85, 250);
        QVERIFY(f.rlv->start());
        f.ride_for(90000);
        const double steadyClimb = f.rlv->solveSpeedForPower(250, f.rlv->effectiveGradePercent());
        QVERIFY(steadyClimb < steady250 * 0.6);
        QVERIFY2(std::abs(f.rlv->speedMps() - steadyClimb) < 0.05,
                 qPrintable(QStringLiteral("%1 %2").arg(f.rlv->speedMps()).arg(steadyClimb)));
        f.rlv->finish(false);
    }

    void simulationFallbackToVirtualGears()
    {
        Fixture f;
        QVERIFY(f.init());
        QVERIFY(f.addRoute(QStringLiteral("hill"), 4000, 5, [](double) { return 4.0; }));
        QVERIFY(f.selectRoute(QStringLiteral("hill")));
        QSignalSpy sims(f.hub.get(), &SensorHub::simulationRequested);
        QSignalSpy modes(f.rlv.get(), &RlvSession::modeChanged);
        // unknown support (-1): try the simulation
        f.pedal(85, 220);
        QVERIFY(f.rlv->start());
        QCOMPARE(sims.count(), 1);
        QVERIFY(!f.rlv->simulationFallback());
        f.ride_for(2000);
        // the trainer rejects 0x11 / lacks the feature bit: virtual gears via ERG
        f.hub->setTrainerSimulationSupport(0);
        QVERIFY(modes.count() >= 1);
        QCOMPARE(f.rlv->mode(), QStringLiteral("sim"));   // the choice is kept
        QVERIFY(f.rlv->simulationFallback());
        QVERIFY(f.rlv->virtualGears());
        QVERIFY(!f.hub->simulationActive());
        QVERIFY(f.hub->requestedTarget() > 0);
        QCOMPARE(f.hub->requestedTarget(), f.rlv->sentTargetWatts());
        QVERIFY(!f.rlv->simulationActive());
        const int simCount = int(sims.count());
        f.ride_for(5000);
        QCOMPARE(int(sims.count()), simCount);
        // gear speed and gear target like the gears mode
        const double v = f.gears->gearing().virtualSpeed(85);
        QVERIFY(std::abs(f.rlv->speedMps() - v) < 0.01);
        QVERIFY(std::abs(f.rlv->sentTargetWatts()
                         - road::powerForSpeed(f.rlv->roadModel(f.rlv->effectiveGradePercent()), v)) <= 3);
        // Zwift Click shifts in the fallback
        const QString gear = f.gears->gearLabel();
        f.rlv->controllerShift(1);
        QVERIFY(f.gears->gearLabel() != gear);
        QCOMPARE(f.rlv->difficultyPercent(), 100);
        // trainer without cadence: released, speed from power
        f.pedal(0, 200);
        f.ride_for(120000);
        QCOMPARE(f.hub->requestedTarget(), 0);
        QVERIFY(std::abs(f.rlv->speedMps() - f.rlv->solveSpeedForPower(200, 4.0)) < 0.05);
        // a trainer that can simulate again (other trainer connected)
        f.pedal(85, 220);
        f.hub->setTrainerSimulationSupport(1);
        QVERIFY(!f.rlv->simulationFallback());
        QVERIFY(f.hub->simulationActive());
        QCOMPARE(f.hub->requestedTarget(), 0);
        f.rlv->finish(false);

        // known unsupported before the start: never sends 0x11
        f.hub->setTrainerSimulationSupport(0);
        f.rlv->reset();
        sims.clear();
        QVERIFY(f.rlv->start());
        QCOMPARE(sims.count(), 0);
        QVERIFY(f.hub->requestedTarget() > 0);
        f.rlv->finish(false);
    }

    void ergTargetOnThePage()
    {
        Fixture f;
        QVERIFY(f.init());
        QVERIFY(f.addRoute(QStringLiteral("hill"), 6000, 5, [](double d) { return d < 3000 ? 6.0 : -3.0; }));
        QVERIFY(f.selectRoute(QStringLiteral("hill")));
        // never set: 60 % FTP, rounded to 5 W
        const int ftp = f.profiles->current().ftpFor(QStringLiteral("cycling"));
        const int expected = int(std::lround(ftp * 0.6 / 5.0)) * 5;
        QCOMPARE(f.rlv->targetWatts(), expected);
        QSignalSpy ergs(f.hub.get(), &SensorHub::targetPowerRequested);
        QSignalSpy sims(f.hub.get(), &SensorHub::simulationRequested);
        f.rlv->setMode(QStringLiteral("erg"));
        QCOMPARE(f.rlv->mode(), QStringLiteral("erg"));
        QCOMPARE(f.rlv->targetWatts(), expected);   // not riding: keeps the target
        QCOMPARE(ergs.count(), 0);
        f.rlv->setTargetWatts(200);
        QCOMPARE(f.db.setting(QStringLiteral("rlv/ergWatts")).toInt(), 200);
        f.pedal(85, 200);
        QVERIFY(f.rlv->start());
        QCOMPARE(f.hub->requestedTarget(), 200);
        QCOMPARE(f.rlv->sentTargetWatts(), 200);
        QCOMPARE(sims.count(), 0);
        QVERIFY(!f.rlv->virtualGears());
        // ± on the page, clamped
        f.rlv->adjustTargetWatts(25);
        QCOMPARE(f.hub->requestedTarget(), 225);
        f.rlv->adjustTargetWatts(-5);
        QCOMPARE(f.hub->requestedTarget(), 220);
        f.rlv->setTargetWatts(3);
        QCOMPARE(f.rlv->targetWatts(), RlvSession::kMinErgWatts);
        f.rlv->setTargetWatts(5000);
        QCOMPARE(f.rlv->targetWatts(), RlvSession::kMaxWatts);
        QCOMPARE(f.hub->requestedTarget(), RlvSession::kMaxWatts);
        f.rlv->setTargetWatts(200);
        // Zwift Click ±10 W
        QSignalSpy shifted(f.rlv.get(), &RlvSession::shifted);
        f.rlv->controllerShift(1);
        QCOMPARE(f.rlv->targetWatts(), 210);
        QCOMPARE(f.hub->requestedTarget(), 210);
        f.rlv->controllerShift(-1);
        f.rlv->controllerShift(-1);
        QCOMPARE(f.rlv->targetWatts(), 190);
        QCOMPARE(shifted.count(), 3);
        QCOMPARE(f.rlv->difficultyPercent(), 100);
        f.rlv->setTargetWatts(200);
        // the difficulty does not flatten the route in ERG
        f.rlv->setDifficulty(50);
        QVERIFY(std::abs(f.rlv->effectiveGradePercent() - 6.0) < 0.1);
        // the trainer holds the watts, the climb makes the speed slower
        f.ride_for(90000);
        QVERIFY(f.rlv->distanceMeters() < 3000);
        const double climbSpeed = f.rlv->speedMps();
        QVERIFY(std::abs(climbSpeed - f.rlv->solveSpeedForPower(200, 6.0)) < 0.05);
        QCOMPARE(f.hub->requestedTarget(), 200);   // unchanged by the grade
        QVERIFY(f.rlv->sentTargetWatts() == 200);
        // switching to ERG while riding starts from the current 3 s power
        f.rlv->setMode(QStringLiteral("sim"));
        QVERIFY(f.hub->simulationActive());
        f.hub->setPowerSmoothing(false);   // 3 s average = this power
        f.pedal(85, 237);
        QCOMPARE(f.hub->displayPower(), 237);
        f.rlv->setMode(QStringLiteral("erg"));
        QCOMPARE(f.rlv->targetWatts(), 235);
        QCOMPARE(f.hub->requestedTarget(), 235);
        QVERIFY(!f.hub->simulationActive());
        f.rlv->setTargetWatts(200);
        f.pedal(85, 200);
        f.ride_for(10000);
        // saved as an ERG ride with the target in the samples
        const qint64 id = f.rlv->finish(true);
        QVERIFY(id >= 0);
        const auto ride = f.db.ride(id);
        QVERIFY(ride.has_value());
        QVERIFY(ride->ergMode);
        QCOMPARE(f.db.rideSamples(id).last().targetPower, 200);
        QCOMPARE(f.hub->requestedTarget(), 0);
    }

    // ERG pressed on the Video Ride page puts the trainer in ERG at once
    // (idle / armed); Simulation or leaving the page releases it; paused:
    // applied on resume. The Free Ride page's hold and this one never fight.
    void ergSelectorEngagesTrainerWhenIdle()
    {
        Fixture f;
        QVERIFY(f.init());
        QVERIFY(f.addRoute(QStringLiteral("flat"), 6000, 5, [](double) { return 0.0; }));
        QVERIFY(f.selectRoute(QStringLiteral("flat")));
        f.rlv->setTargetWatts(190);
        f.gears->setActivePage(QStringLiteral("video"));
        QCOMPARE(f.hub->requestedTarget(), 0);
        f.rlv->setProperty("mode", QStringLiteral("erg"));   // = onActivated: Rlv.mode = v
        QCOMPARE(f.rlv->state(), QStringLiteral("idle"));
        QVERIFY(f.rlv->ergHolding());
        QCOMPARE(f.hub->requestedTarget(), 190);
        QCOMPARE(f.rlv->sentTargetWatts(), 190);
        f.rlv->controllerShift(1);   // Zwift Click +10 W
        QCOMPARE(f.hub->requestedTarget(), 200);
        f.rlv->setProperty("mode", QStringLiteral("sim"));
        QVERIFY(!f.rlv->ergHolding());
        QCOMPARE(f.hub->requestedTarget(), 0);
        f.rlv->setMode(QStringLiteral("erg"));
        QCOMPARE(f.hub->requestedTarget(), 200);

        // page switches: Free Ride (ERG 200 too) <-> Video Ride, either order
        f.freeRide->setTargetWatts(200);
        f.freeRide->setMode(QStringLiteral("erg"));
        f.gears->setActivePage(QStringLiteral("freeride"));
        QVERIFY(!f.rlv->ergHolding());
        QVERIFY(f.freeRide->ergHolding());
        QCOMPARE(f.hub->requestedTarget(), 200);   // not released by the video page
        f.gears->setActivePage(QStringLiteral("video"));
        QVERIFY(f.rlv->ergHolding());
        QVERIFY(!f.freeRide->ergHolding());
        QCOMPARE(f.hub->requestedTarget(), 200);
        f.freeRide->setMode(QStringLiteral("ride"));
        QCOMPARE(f.hub->requestedTarget(), 200);   // not the free ride's trainer any more
        f.gears->setActivePage(QStringLiteral("history"));
        QCOMPARE(f.hub->requestedTarget(), 0);
        f.gears->setActivePage(QStringLiteral("video"));
        QCOMPARE(f.hub->requestedTarget(), 200);

        // armed -> held, pedaling starts the ride in ERG
        f.rlv->startWhenPedaling();
        QCOMPARE(f.rlv->state(), QStringLiteral("ready"));
        QCOMPARE(f.hub->requestedTarget(), 200);
        f.pedal(85, 195);
        QCOMPARE(f.rlv->state(), QStringLiteral("running"));
        QVERIFY(!f.rlv->ergHolding());
        QCOMPARE(f.hub->requestedTarget(), 200);
        f.ride_for(3000);
        // running: Simulation -> ERG
        f.rlv->setMode(QStringLiteral("sim"));
        QVERIFY(f.hub->simulationActive());
        f.rlv->setMode(QStringLiteral("erg"));
        QVERIFY(!f.hub->simulationActive());
        QVERIFY(f.hub->requestedTarget() > 0);
        // paused: released, ERG pressed while paused comes with resume
        f.rlv->setMode(QStringLiteral("sim"));
        f.rlv->pause();
        QCOMPARE(f.hub->requestedTarget(), 0);
        QVERIFY(!f.hub->simulationActive());
        f.rlv->setMode(QStringLiteral("erg"));
        QCOMPARE(f.hub->requestedTarget(), 0);
        f.rlv->resume();
        QCOMPARE(f.hub->requestedTarget(), f.rlv->targetWatts());
        f.rlv->finish(false);
        QCOMPARE(f.hub->requestedTarget(), 0);
        QVERIFY(!f.rlv->ergHolding());
    }

    void modePersistenceAndMigration()
    {
        Fixture f;
        QVERIFY(f.init());
        // old "power" mode -> classic simulation, setting rewritten
        f.db.setSetting(QStringLiteral("rlv/mode"), QStringLiteral("power"));
        {
            RlvSession s(&f.db, f.profiles.get(), f.hub.get(), f.gears.get());
            QCOMPARE(s.mode(), QStringLiteral("sim"));
            QCOMPARE(f.db.setting(QStringLiteral("rlv/mode")).toString(), QStringLiteral("sim"));
            s.setMode(QStringLiteral("power"));   // still accepted, as sim
            QCOMPARE(s.mode(), QStringLiteral("sim"));
            s.setMode(QStringLiteral("bogus"));
            QCOMPARE(s.mode(), QStringLiteral("sim"));
            s.setMode(QStringLiteral("erg"));
            s.setTargetWatts(185);
        }
        {
            RlvSession s(&f.db, f.profiles.get(), f.hub.get(), f.gears.get());
            QCOMPARE(s.mode(), QStringLiteral("erg"));
            QCOMPARE(s.targetWatts(), 185);
            s.setMode(QStringLiteral("gears"));   // the old virtual gears mode: simulation
            QCOMPARE(s.mode(), QStringLiteral("sim"));
        }
        // stored "gears" (older versions) -> "sim", rewritten
        f.db.setSetting(QStringLiteral("rlv/mode"), QStringLiteral("gears"));
        {
            RlvSession s(&f.db, f.profiles.get(), f.hub.get(), f.gears.get());
            QCOMPARE(s.mode(), QStringLiteral("sim"));
            QCOMPARE(f.db.setting(QStringLiteral("rlv/mode")).toString(), QStringLiteral("sim"));
            QVERIFY(!s.virtualGears());   // virtual gears come from the Devices toggle / Click
            f.gears->setEnabled(true);
            QVERIFY(s.virtualGears());
            f.gears->setEnabled(false);
        }
        f.db.setSetting(QStringLiteral("rlv/mode"), QStringLiteral("nonsense"));
        RlvSession s(&f.db, f.profiles.get(), f.hub.get(), f.gears.get());
        QCOMPARE(s.mode(), QStringLiteral("sim"));
        QCOMPARE(RlvSession::parseMode(QStringLiteral("power")), std::optional<RlvSession::Mode>(RlvSession::Mode::Sim));
        QCOMPARE(RlvSession::parseMode(QStringLiteral("gears")), std::optional<RlvSession::Mode>(RlvSession::Mode::Sim));
        QVERIFY(!RlvSession::parseMode(QStringLiteral("x")));
        QCOMPARE(RlvSession::modeName(RlvSession::Mode::Erg), QStringLiteral("erg"));
    }

    void gearsWithoutCadence()
    {
        Fixture f;
        QVERIFY(f.init());
        QVERIFY(f.addRoute(QStringLiteral("hill"), 4000, 5, [](double) { return 4.0; }));
        QVERIFY(f.selectRoute(QStringLiteral("hill")));
        // gears mode, trainer without cadence: speed from power, trainer released
        f.useGears();
        QCOMPARE(f.rlv->mode(), QStringLiteral("sim"));
        QCOMPARE(f.db.setting(QStringLiteral("gears/enabled")).toBool(), true);
        f.pedal(0, 200);
        QVERIFY(f.rlv->start());
        f.ride_for(120000);
        QVERIFY(std::abs(f.rlv->speedKmh() / 3.6 - f.rlv->solveSpeedForPower(200, 4.0)) < 0.05);
        QCOMPARE(f.hub->requestedTarget(), 0);
        f.rlv->finish(false);
        QCOMPARE(f.rlv->solveSpeedForPower(0, 0), 0.0);
        QVERIFY(f.rlv->solveSpeedForPower(200, 0) > f.rlv->solveSpeedForPower(200, 5));
    }

    void finishesAtRouteEndAndSaves()
    {
        Fixture f;
        QVERIFY(f.init());
        f.useGears();
        QVERIFY(f.addRoute(QStringLiteral("short"), 400, 8, [](double d) { return d < 200 ? 3.0 : 0.0; }));
        QVERIFY(f.selectRoute(QStringLiteral("short")));
        QSignalSpy saved(f.rlv.get(), &RlvSession::rideSaved);
        QSignalSpy finished(f.rlv.get(), &RlvSession::finished);
        f.pedal(95, 220);
        f.rlv->startWhenPedaling();   // already pedaling -> starts right away
        QVERIFY(f.rlv->running());
        f.ride_for(200000);
        QCOMPARE(f.rlv->state(), QStringLiteral("finished"));
        QVERIFY(f.rlv->completed());
        QCOMPARE(finished.count(), 1);
        QCOMPARE(saved.count(), 1);
        QVERIFY(std::abs(f.rlv->distanceMeters() - f.rlv->totalDistanceM()) < 1e-6);
        QCOMPARE(f.rlv->progress(), 1.0);
        QCOMPARE(f.rlv->remainingMeters(), 0.0);
        QVERIFY(f.rlv->ascentDoneM() > 4.0);
        const qint64 id = f.rlv->lastRideId();
        QVERIFY(id >= 0);
        const auto ride = f.db.ride(id);
        QVERIFY(ride.has_value());
        QCOMPARE(ride->workoutName, QStringLiteral("RLV: short"));
        QCOMPARE(ride->sport, QStringLiteral("cycling"));
        QVERIFY(std::abs(ride->distanceM - f.rlv->totalDistanceM()) < 1.0);
        QVERIFY(!ride->ergMode);
        const QVector<Sample> samples = f.db.rideSamples(id);
        QCOMPARE(samples.size(), f.rlv->samples().size());
        QVERIFY(samples.at(3).speedMps > 5);
        QCOMPARE(samples.at(3).power, 220);
        QCOMPARE(f.hub->requestedTarget(), 0);
        // stop() after the end is a no-op; reset returns to the start
        QCOMPARE(f.rlv->stop(), qint64(-1));
        f.rlv->reset();
        QCOMPARE(f.rlv->state(), QStringLiteral("idle"));
        QCOMPARE(f.rlv->distanceMeters(), 0.0);
        QVERIFY(!f.rlv->completed());
        // a new start after finishing rides again from the start distance
        QVERIFY(f.rlv->start());
        f.ride_for(2000);
        QVERIFY(f.rlv->distanceMeters() > 10 && f.rlv->distanceMeters() < 30);
        QCOMPARE(f.rlv->finish(true), qint64(-1));   // too short to save (5 s minimum)
    }

    void mutualExclusion()
    {
        Fixture f;
        QVERIFY(f.init());
        QVERIFY(f.addRoute(QStringLiteral("flat"), 3000, 10, flat));
        QVERIFY(f.selectRoute(QStringLiteral("flat")));
        QSignalSpy refused(f.rlv.get(), &RlvSession::startRefused);
        QSignalSpy trainingRefused(f.training.get(), &TrainingSession::startRefused);
        QVERIFY(f.rlv->start());
        f.training->start();
        QVERIFY(!f.training->active());
        QCOMPARE(trainingRefused.count(), 1);
        QVERIFY(!f.freeRide->start());
        // no other route while riding
        QVERIFY(!f.rlv->selectPath(f.dir.filePath(QStringLiteral("flat"))));
        f.rlv->finish(false);

        f.training->start();
        QVERIFY(f.training->active());
        QVERIFY(!f.rlv->start());
        QCOMPARE(refused.count(), 1);
        f.training->discard();

        QVERIFY(f.freeRide->start());
        QVERIFY(!f.rlv->start());
        QCOMPARE(refused.count(), 2);
        f.freeRide->finish(false);

        // armed sessions are cancelled when the video ride starts
        f.training->startWhenPedaling();
        QVERIFY(f.training->waiting());
        QVERIFY(f.rlv->start());
        QVERIFY(!f.training->waiting());
        f.rlv->finish(false);
        f.freeRide->reset();
        f.freeRide->startWhenPedaling();
        QVERIFY(f.freeRide->waiting());
        QVERIFY(f.rlv->start());
        QVERIFY(!f.freeRide->waiting());
        f.rlv->finish(false);

        // the guard goes with the session
        f.rlv.reset();
        f.training->start();
        QVERIFY(f.training->active());
        f.training->discard();
    }

    void sharedGearingAndController()
    {
        Fixture f;
        QVERIFY(f.init());
        f.useGears();
        QVERIFY(f.addRoute(QStringLiteral("flat"), 3000, 10, flat));
        QVERIFY(f.selectRoute(QStringLiteral("flat")));
        QSignalSpy gear(f.gears.get(), &VirtualGears::gearChanged);
        QSignalSpy shifted(f.gears.get(), &VirtualGears::shifted);
        const QString before = f.gears->gearLabel();
        QVERIFY(f.gears->shiftUp());
        QVERIFY(f.gears->gearLabel() != before);
        QCOMPARE(f.rlv->gearing().label(), f.freeRide->gearing().label());   // one gearing
        QVERIFY(gear.count() >= 1);
        QCOMPARE(shifted.count(), 1);
        QVERIFY(f.gears->shiftDown());
        QCOMPARE(f.rlv->gearing().label(), before);
        // controller shifts and re-applies the target at once
        f.pedal(90, 200);
        QVERIFY(f.rlv->start());
        f.ride_for(2000);
        const int t0 = f.rlv->sentTargetWatts();
        f.rlv->controllerShift(1);
        QVERIFY(f.rlv->sentTargetWatts() > t0);
        QCOMPARE(f.hub->requestedTarget(), f.rlv->sentTargetWatts());
        f.rlv->controllerShift(-1);
        f.rlv->controllerShift(-1);
        QVERIFY(f.rlv->sentTargetWatts() < t0);
        QCOMPARE(f.rlv->difficultyPercent(), 100);
        f.rlv->finish(false);
    }

    // VirtualGears::controllerShift: the single Zwift Click routing
    void clickRouting()
    {
        Fixture f;
        QVERIFY(f.init());
        QVERIFY(f.addRoute(QStringLiteral("hill"), 4000, 5, [](double) { return 5.0; }));
        QVERIFY(f.selectRoute(QStringLiteral("hill")));
        f.rlv->setDifficulty(70);
        f.freeRide->setGrade(3.0);
        QVERIFY(!f.gears->enabled());
        QSignalSpy notices(f.gears.get(), &VirtualGears::notice);
        QSignalSpy rlvErg(f.rlv.get(), &RlvSession::shifted);
        QSignalSpy freeErg(f.freeRide.get(), &FreeRideSession::shifted);

        // no ride, e.g. Devices page: shifts the gear, and turns virtual gears on once
        f.gears->setActivePage(QStringLiteral("devices"));
        int gear = f.gears->gearIndex();
        f.gears->controllerShift(1);
        QVERIFY(f.gears->enabled());
        QCOMPARE(notices.count(), 1);
        QCOMPARE(notices.first().first().toString(), QStringLiteral("Zwift Click detected – using virtual gears"));
        QCOMPARE(f.gears->gearIndex(), ++gear);
        f.gears->controllerShift(-1);
        QCOMPARE(f.gears->gearIndex(), --gear);
        QCOMPARE(notices.count(), 1);   // already on

        // Video Ride page, simulation: gears; ERG: ±10 W - never the difficulty
        f.gears->setActivePage(QStringLiteral("video"));
        f.gears->controllerShift(1);
        QCOMPARE(f.gears->gearIndex(), ++gear);
        f.rlv->setMode(QStringLiteral("erg"));
        const int rlvTarget = f.rlv->targetWatts();
        f.gears->controllerShift(1);
        QCOMPARE(f.rlv->targetWatts(), rlvTarget + 10);
        QCOMPARE(f.gears->gearIndex(), gear);
        QCOMPARE(rlvErg.count(), 1);
        f.rlv->setMode(QStringLiteral("sim"));

        // Free Ride page: ride -> gears, ERG -> ±10 W - never the grade
        f.gears->setActivePage(QStringLiteral("freeride"));
        f.gears->controllerShift(-1);
        QCOMPARE(f.gears->gearIndex(), --gear);
        f.freeRide->setMode(QStringLiteral("erg"));
        const int freeTarget = f.freeRide->targetWatts();
        f.gears->controllerShift(-1);
        QCOMPARE(f.freeRide->targetWatts(), freeTarget - 10);
        QCOMPARE(freeErg.count(), 1);
        QCOMPARE(f.gears->gearIndex(), gear);
        f.freeRide->setMode(QStringLiteral("ride"));

        // a running video ride gets the buttons wherever the user is
        f.gears->setActivePage(QStringLiteral("history"));
        f.pedal(85, 200);
        QVERIFY(f.rlv->start());
        f.gears->controllerShift(1);
        QCOMPARE(f.gears->gearIndex(), ++gear);
        f.rlv->finish(false);
        // a running free ride too
        QVERIFY(f.freeRide->start());
        f.gears->controllerShift(1);
        QCOMPARE(f.gears->gearIndex(), ++gear);
        f.freeRide->finish(false);

        QCOMPARE(f.rlv->difficultyPercent(), 70);
        QCOMPARE(f.freeRide->gradePercent(), 3.0);
        // a press while off turns them on again (with the notice)
        f.gears->setEnabled(false);
        f.gears->controllerShift(1);
        QVERIFY(f.gears->enabled());
        QCOMPARE(notices.count(), 2);
    }

    // Saved on the Devices page -> both rides at once; reloaded when the
    // Click is recognised and after a restart.
    void gearSetupSharedAndReloaded()
    {
        Fixture f;
        QVERIFY(f.init());
        QSignalSpy freeTargets(f.freeRide.get(), &FreeRideSession::targetChanged);
        QSignalSpy rlvTargets(f.rlv.get(), &RlvSession::targetChanged);
        QVERIFY(f.gears->setGearSetup({ 40 }, { 11, 13, 15, 17, 19, 21, 24, 28, 32, 36, 42 }, 2136, false));
        QVERIFY(f.gears->setBikePhysics(10.0, 0.35, 0.005));
        QVERIFY(f.gears->setReferenceGear(40, 19));
        QVERIFY(!f.gears->setReferenceGear(50, 17));   // not on this bike
        QCOMPARE(f.rlv->gearing().setup().chainrings, QVector<int>({ 40 }));
        QCOMPARE(f.freeRide->gearing().setup().circumferenceMm, 2136);
        QCOMPARE(f.rlv->roadModel(0).massKg, f.freeRide->roadModel(0).massKg);
        QCOMPARE(f.rlv->roadModel(0).cda, 0.35);
        QVERIFY(freeTargets.count() >= 1);
        QVERIFY(rlvTargets.count() >= 1);
        QCOMPARE(f.gears->referenceLabel(), QStringLiteral("40×19"));
        QCOMPARE(f.gears->summary(), QStringLiteral("1× 40 · 11–42 11-speed · 700×28c · 11 gears"));
        QVERIFY(f.gears->setGear(0, 4));   // 40×19
        QCOMPARE(f.gears->gearFactor(), 1.0);
        f.gears->flushSettings();

        // the Click is recognised: the saved setup is (re)loaded, virtual gears on
        f.db.setGearSetupJson(f.profiles->profileId(), [&] {
            GearSetup s = f.gears->setup();
            s.rearIndex = 5;   // saved meanwhile (e.g. by another instance)
            return s.toJsonText();
        }());
        QVERIFY(!f.gears->enabled());
        f.gears->setClickConnected(true);
        QVERIFY(f.gears->enabled());
        QVERIFY(f.gears->clickConnected());
        QCOMPARE(f.gears->gearLabel(), QStringLiteral("40×21"));
        QCOMPARE(f.rlv->gearing().label(), QStringLiteral("40×21"));
        // the user turns them off while the Click stays connected: respected
        f.gears->setEnabled(false);
        f.gears->setClickConnected(true);
        QVERIFY(!f.gears->enabled());
        f.gears->setClickConnected(false);
        QVERIFY(!f.gears->enabled());
        f.gears->setEnabled(true);

        // restart: setup, gear and the toggle come back
        f.gears->flushSettings();
        VirtualGears again(&f.db, f.profiles.get());
        QVERIFY(again.enabled());
        QCOMPARE(again.gearLabel(), QStringLiteral("40×21"));
        QCOMPARE(again.referenceLabel(), QStringLiteral("40×19"));
        QCOMPARE(again.setup().bikeWeightKg, 10.0);
    }

    // "Start when pedaling" only while the Video Ride page is open; a workout
    // start cancels an armed video ride instead of being refused.
    void armedRideDisarmsAndYields()
    {
        Fixture f;
        QVERIFY(f.init());
        QVERIFY(f.addRoute(QStringLiteral("flat"), 3000, 10, flat));
        QVERIFY(f.selectRoute(QStringLiteral("flat")));
        f.gears->setActivePage(QStringLiteral("video"));
        f.rlv->startWhenPedaling();
        QVERIFY(f.rlv->waiting());
        f.gears->setActivePage(QStringLiteral("dashboard"));
        QCOMPARE(f.rlv->state(), QStringLiteral("idle"));

        f.gears->setActivePage(QStringLiteral("video"));
        f.rlv->startWhenPedaling();
        QSignalSpy refused(f.training.get(), &TrainingSession::startRefused);
        f.training->start();
        QCOMPARE(refused.count(), 0);
        QVERIFY(f.training->active());
        QCOMPARE(f.rlv->state(), QStringLiteral("idle"));
        f.training->discard();
    }

    // Discarding right before the route end must not save the ride through
    // the last time slice reaching the end.
    void discardNearTheEndDoesNotSave()
    {
        Fixture f;
        QVERIFY(f.init());
        QVERIFY(f.addRoute(QStringLiteral("flat"), 3000, 10, flat));
        QVERIFY(f.selectRoute(QStringLiteral("flat")));
        QVERIFY(f.rlv->seekToDistance(f.rlv->totalDistanceM() - 150));
        QSignalSpy saved(f.rlv.get(), &RlvSession::rideSaved);
        f.pedal(90, 250);
        QVERIFY(f.rlv->start());
        while (f.rlv->running() && f.rlv->remainingMeters() > 1.5)
            f.rlv->advanceClock(100);
        QVERIFY(f.rlv->running());
        QVERIFY(f.rlv->samples().size() >= 5);   // would be saved
        f.rlv->setManualClock(false);   // finish() takes the last slice from the wall clock
        QThread::msleep(600);
        QCOMPARE(f.rlv->finish(false), qint64(-1));
        QCOMPARE(saved.count(), 0);
        QVERIFY(!f.rlv->completed());
        QCOMPARE(f.rlv->lastRideId(), qint64(-1));
        QCOMPARE(f.db.rides(f.profiles->profileId()).size(), 0);
    }

    void longClockGapIsAPauseAndLastRideIdNotifies()
    {
        Fixture f;
        QVERIFY(f.init());
        QVERIFY(f.addRoute(QStringLiteral("flat"), 3000, 10, flat));
        QVERIFY(f.selectRoute(QStringLiteral("flat")));
        QSignalSpy idChanged(f.rlv.get(), &RlvSession::lastRideIdChanged);
        f.pedal(90, 250);
        QVERIFY(f.rlv->start());
        QCOMPARE(idChanged.count(), 1);
        f.ride_for(6000);
        const double d = f.rlv->distanceMeters();
        const int t = f.rlv->elapsedSeconds();
        f.rlv->advanceWallClock(30 * 60 * 1000);   // the computer slept
        QCOMPARE(f.rlv->distanceMeters(), d);
        QCOMPARE(f.rlv->elapsedSeconds(), t);
        f.rlv->advanceWallClock(250);
        QVERIFY(f.rlv->distanceMeters() > d);
        QVERIFY(f.rlv->stop() > 0);
        QCOMPARE(idChanged.count(), 2);
        f.rlv->reset();
        QCOMPARE(idChanged.count(), 3);
        QCOMPARE(f.rlv->lastRideId(), qint64(-1));
        QVERIFY(f.rlv->start());
        f.rlv->discard();
        QCOMPARE(idChanged.count(), 5);
    }

    // Rate changes are limited (each one makes the player flush): small
    // steps at most every kRateMinIntervalMs, big jumps at once.
    void playbackRateHysteresis()
    {
        Fixture f;
        QVERIFY(f.init());
        QVERIFY(f.addRoute(QStringLiteral("flat"), 20000, 10, flat));
        QVERIFY(f.selectRoute(QStringLiteral("flat")));
        f.pedal(90, 200);
        QVERIFY(f.rlv->start());
        f.ride_for(30000);   // steady
        QSignalSpy video(f.rlv.get(), &RlvSession::videoChanged);
        int changes = 0;
        double last = f.rlv->playbackRate();
        // power wobbling a little: the rate may move, but rarely
        for (int i = 0; i < 40; ++i) {
            f.pedal(90, i % 2 ? 190 : 215);
            f.rlv->advanceClock(250);
            if (!qFuzzyCompare(last, f.rlv->playbackRate())) {
                ++changes;
                last = f.rlv->playbackRate();
            }
        }
        QVERIFY2(changes <= 40 * 250 / RlvSession::kRateMinIntervalMs + 1, qPrintable(QString::number(changes)));
        f.rlv->finish(false);
    }
};

QTEST_GUILESS_MAIN(TestRlvSession)
#include "tst_rlvsession.moc"
