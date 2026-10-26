// RlvSession: rider position, video sync (rate / seek / hold), resistance
// with difficulty, power mode, auto-finish + saved ride, mutual exclusion,
// shared gearing with the free ride.
#include "Database.h"
#include "FreeRideSession.h"
#include "ProfileManager.h"
#include "RlvLibrary.h"
#include "RlvSession.h"
#include "SensorHub.h"
#include "TrainingSession.h"

#include <QFile>
#include <QSignalSpy>
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
        freeRide = std::make_unique<FreeRideSession>(&db, profiles.get(), hub.get());
        freeRide->setManualClock(true);
        freeRide->setTrainingSession(training.get());
        library = std::make_unique<RlvLibrary>(&db);
        library->setLibraryFolder(dir.path());
        rlv = std::make_unique<RlvSession>(&db, profiles.get(), hub.get(), library.get());
        rlv->setManualClock(true);
        rlv->setMinimumSaveSeconds(5);
        rlv->setFreeRideSession(freeRide.get());
        rlv->setTrainingSession(training.get());
        rlv->setStartBlocker(QStringLiteral("freeRide"), [this] { return freeRide->active(); });
        freeRide->setStartBlocker(QStringLiteral("rlv"), [this] { return rlv->active() || rlv->waiting(); });
        return true;
    }
    bool addRoute(const QString &name, double lengthM, double speed, const std::function<double(double)> &grade,
                  const QByteArray &infos = {})
    {
        if (!QDir(dir.path()).mkpath(name))
            return false;
        if (!writeRoute(dir.filePath(name), name, lengthM, speed, grade, infos))
            return false;
        library->rescan();
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
        RlvSession s(&db, &profiles, &hub);
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
        QCOMPARE(s.mode(), QStringLiteral("gears"));
        QCOMPARE(s.gearLabel(), QStringLiteral("50×21"));   // own gearing (profile setup)
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
        RlvSession again(&f.db, f.profiles.get(), f.hub.get(), f.library.get());
        QVERIFY(again.hasRoute());
        QCOMPARE(again.routeName(), QStringLiteral("route"));
    }

    void rideAdvancesAndVideoFollows()
    {
        Fixture f;
        QVERIFY(f.init());
        QVERIFY(f.addRoute(QStringLiteral("flat"), 3000, 10, flat));
        QVERIFY(f.selectRoute(QStringLiteral("flat")));
        QSignalSpy targets(f.hub.get(), &SensorHub::targetPowerRequested);
        f.pedal(90, 150);
        QVERIFY(f.rlv->start());
        QVERIFY(f.rlv->running());
        const double v = f.freeRide->gearing().virtualSpeed(90);   // shared gearing, ≈ 7.5 m/s
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
        QVERIFY(std::abs(f.rlv->targetWatts() - expected) <= 2);
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
        // 1 s behind -> slightly faster
        f.rlv->reportVideoPosition(f.rlv->videoPosition() - 1000);
        f.ride_for(250);
        QVERIFY(f.rlv->playbackRate() > base);
        QVERIFY(f.rlv->playbackRate() <= base * 1.1 + 0.05);
        // stale reports are ignored
        f.ride_for(4000);
        QCOMPARE(f.rlv->videoDriftSec(), 0.0);
        QCOMPARE(f.rlv->playbackRate(), base);

        // very slow rider (below the minimum rate): the video waits instead of running ahead
        QVERIFY(f.rlv->shiftDown());
        while (f.rlv->shiftDown()) { }
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
        const int full = f.rlv->targetWatts();
        const double v = f.freeRide->gearing().virtualSpeed(80);
        QVERIFY(std::abs(full - road::powerForSpeed(f.rlv->roadModel(f.rlv->effectiveGradePercent()), v)) <= 3);
        f.rlv->setDifficulty(50);
        QVERIFY(std::abs(f.rlv->effectiveGradePercent() - 3.0) < 0.1);
        const int half = f.rlv->targetWatts();   // applied at once
        f.rlv->setDifficulty(0);
        const int none = f.rlv->targetWatts();
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

    void powerModeAndNoCadence()
    {
        Fixture f;
        QVERIFY(f.init());
        QVERIFY(f.addRoute(QStringLiteral("hill"), 4000, 5, [](double) { return 4.0; }));
        QVERIFY(f.selectRoute(QStringLiteral("hill")));
        f.rlv->setMode(QStringLiteral("power"));
        QCOMPARE(f.db.setting(QStringLiteral("rlv/mode")).toString(), QStringLiteral("power"));
        f.pedal(85, 250);
        QVERIFY(f.rlv->start());
        QCOMPARE(f.hub->requestedTarget(), 0);   // trainer free ride
        f.ride_for(30000);
        const double expected = f.rlv->solveSpeedForPower(250, f.rlv->effectiveGradePercent());
        QVERIFY(expected > 3 && expected < 9);
        QVERIFY(std::abs(f.rlv->speedKmh() / 3.6 - expected) < 0.05);
        QVERIFY(f.rlv->ascentDoneM() > 5);
        QCOMPARE(f.hub->requestedTarget(), 0);
        // Zwift Click in power mode: difficulty ±10 %
        f.rlv->controllerShift(-1);
        QCOMPARE(f.rlv->difficultyPercent(), 90);
        f.rlv->controllerShift(1);
        QCOMPARE(f.rlv->difficultyPercent(), 100);
        f.rlv->finish(false);
        f.rlv->reset();

        // gears mode, trainer without cadence: speed from power, trainer released
        f.rlv->setMode(QStringLiteral("gears"));
        f.pedal(0, 200);
        QVERIFY(f.rlv->start());
        f.ride_for(30000);
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
        QVERIFY(f.addRoute(QStringLiteral("flat"), 3000, 10, flat));
        QVERIFY(f.selectRoute(QStringLiteral("flat")));
        QSignalSpy gear(f.rlv.get(), &RlvSession::gearChanged);
        QSignalSpy shifted(f.rlv.get(), &RlvSession::shifted);
        const QString before = f.rlv->gearLabel();
        QCOMPARE(before, f.freeRide->gearLabel());
        QVERIFY(f.rlv->shiftUp());
        QVERIFY(f.rlv->gearLabel() != before);
        QCOMPARE(f.rlv->gearLabel(), f.freeRide->gearLabel());
        QVERIFY(gear.count() >= 1);
        QCOMPARE(shifted.count(), 1);
        // a shift in the free ride (setup page) is the same gear
        QVERIFY(f.freeRide->shiftDown());
        QCOMPARE(f.rlv->gearLabel(), before);
        // controller in gears mode shifts and re-applies the target at once
        f.pedal(90, 200);
        QVERIFY(f.rlv->start());
        f.ride_for(2000);
        const int t0 = f.rlv->targetWatts();
        f.rlv->controllerShift(1);
        QVERIFY(f.rlv->targetWatts() > t0);
        QCOMPARE(f.hub->requestedTarget(), f.rlv->targetWatts());
        f.rlv->controllerShift(-1);
        f.rlv->controllerShift(-1);
        QVERIFY(f.rlv->targetWatts() < t0);
        QCOMPARE(f.rlv->gear().value(QStringLiteral("label")).toString(), f.rlv->gearLabel());
        f.rlv->finish(false);
    }
};

QTEST_GUILESS_MAIN(TestRlvSession)
#include "tst_rlvsession.moc"
