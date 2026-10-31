#pragma once

#include "Profile.h"
#include "Ride.h"
#include "RlvRoute.h"
#include "RouteProgress.h"
#include "RunWorkout.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QMetaObject>
#include <QObject>
#include <QPair>
#include <QPointer>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>
#include <cmath>
#include <functional>
#include <optional>

namespace watt {

class Database;
class ProfileManager;
class RunLibrary;
class SensorHub;
class TreadmillDevice;

// Treadmill Run: running on a treadmill through the 3D world of the GPX Ride.
// Exposed to QML as "Running" (Artilody.Backend).
//
// The treadmill is only READ (TreadmillDevice: belt speed + incline): the
// runner sets the belt, the app never controls it. The distance on the route
// is the belt speed integrated over time; the 3D view (Route3DView) follows
// it like the GPX Ride.
//
// Route: the built-in standard route (runroutes::standardRoute(), a 10 km
// loop) while GPX is off, or a loaded .gpx track while GPX is on
// (setGpxEnabled() / selectGpx()). A loop is run lap after lap; on a point
// to point route the run finishes at its end - unless a workout is still
// running, then the route starts over.
//
// Workouts (RunWorkout, from RunLibrary) prescribe speed (% of the AnT speed
// or km/h) and a suggested incline; the page shows target vs. actual. The AnT
// test is a workout: at the end of its test block the threshold is evaluated
// (running::evaluateAntTest) and offered (antTestCompleted / antResult); the
// runner accepts it (acceptAntResult()) and it becomes the new AnT.
//
// States: "ready" (not started), "running", "paused", "finished". A run only
// starts with start() (button / Space) - never by the belt moving.
class RunSession : public QObject, public RouteProgressSource
{
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool active READ active NOTIFY stateChanged)
    Q_PROPERTY(bool running READ running NOTIFY stateChanged)
    Q_PROPERTY(bool paused READ paused NOTIFY stateChanged)
    Q_PROPERTY(bool inProgress READ inProgress NOTIFY stateChanged)
    Q_PROPERTY(bool treadmillConnected READ treadmillConnected NOTIFY treadmillChanged)
    Q_PROPERTY(QString treadmillName READ treadmillName NOTIFY treadmillChanged)
    Q_PROPERTY(bool simulatedTreadmill READ simulatedTreadmill NOTIFY treadmillChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

    // route
    Q_PROPERTY(bool gpxEnabled READ gpxEnabled WRITE setGpxEnabled NOTIFY routeChanged)
    Q_PROPERTY(QString gpxPath READ gpxPath NOTIFY routeChanged)        // last loaded GPX ("" = none)
    Q_PROPERTY(bool hasGpx READ hasGpx NOTIFY routeChanged)             // a GPX track is loaded
    Q_PROPERTY(bool hasRoute READ hasRoute NOTIFY routeChanged)
    Q_PROPERTY(QString routeName READ routeName NOTIFY routeChanged)
    Q_PROPERTY(QString routePath READ routePath NOTIFY routeChanged)
    Q_PROPERTY(bool routeIsLoop READ routeIsLoop NOTIFY routeChanged)
    Q_PROPERTY(double routeLengthM READ routeLengthM NOTIFY routeChanged)
    Q_PROPERTY(double routeAscentM READ routeAscentM NOTIFY routeChanged)
    Q_PROPERTY(QVariantList profile READ profile NOTIFY routeChanged)   // [{d, ele}]
    Q_PROPERTY(double minElevationM READ minElevationM NOTIFY routeChanged)
    Q_PROPERTY(double maxElevationM READ maxElevationM NOTIFY routeChanged)
    Q_PROPERTY(double distanceMeters READ distanceMeters NOTIFY positionChanged)   // position on the route
    Q_PROPERTY(double startDistanceM READ startDistanceM CONSTANT)
    Q_PROPERTY(double elevationM READ elevationM NOTIFY positionChanged)
    Q_PROPERTY(double routeGradePercent READ routeGradePercent NOTIFY positionChanged)
    Q_PROPERTY(int lap READ lap NOTIFY positionChanged)                 // 1-based

    // live / totals
    Q_PROPERTY(double speedKmh READ speedKmh NOTIFY metricsChanged)     // belt speed
    Q_PROPERTY(QString pace READ pace NOTIFY metricsChanged)            // "5:00" min/km
    Q_PROPERTY(double inclinePercent READ inclinePercent NOTIFY metricsChanged)
    Q_PROPERTY(bool inclineKnown READ inclineKnown NOTIFY metricsChanged)
    Q_PROPERTY(int heartRate READ heartRate NOTIFY metricsChanged)
    Q_PROPERTY(int cadence READ cadence NOTIFY metricsChanged)          // estimated steps / min
    Q_PROPERTY(double elapsedSeconds READ elapsedSeconds NOTIFY metricsChanged)
    Q_PROPERTY(double runDistanceM READ runDistanceM NOTIFY metricsChanged)
    Q_PROPERTY(double avgSpeedKmh READ avgSpeedKmh NOTIFY metricsChanged)
    Q_PROPERTY(QString avgPace READ avgPace NOTIFY metricsChanged)
    Q_PROPERTY(int avgHeartRate READ avgHeartRate NOTIFY metricsChanged)
    Q_PROPERTY(int maxHeartRate READ maxHeartRate NOTIFY metricsChanged)
    Q_PROPERTY(double calories READ calories NOTIFY metricsChanged)
    Q_PROPERTY(double climbedM READ climbedM NOTIFY metricsChanged)     // from the treadmill incline
    Q_PROPERTY(double rtss READ rtss NOTIFY metricsChanged)
    Q_PROPERTY(int speedZone READ speedZone NOTIFY metricsChanged)      // 0..5 by % AnT
    Q_PROPERTY(double antSpeedKmh READ antSpeedKmh NOTIFY setupChanged)
    Q_PROPERTY(QVariantList speedSeries READ speedSeries NOTIFY seriesChanged)       // km/h per seriesStep s
    Q_PROPERTY(QVariantList heartRateSeries READ heartRateSeries NOTIFY seriesChanged)
    Q_PROPERTY(QVariantList targetSeries READ targetSeries NOTIFY seriesChanged)
    Q_PROPERTY(int seriesStep READ seriesStep NOTIFY seriesChanged)

    // workout
    Q_PROPERTY(bool hasWorkout READ hasWorkout NOTIFY setupChanged)
    Q_PROPERTY(qint64 workoutId READ workoutId NOTIFY setupChanged)
    Q_PROPERTY(QString workoutName READ workoutName NOTIFY setupChanged)
    Q_PROPERTY(bool antTest READ antTest NOTIFY setupChanged)           // the workout is the AnT test
    Q_PROPERTY(QVariantList workoutSegments READ workoutSegments NOTIFY setupChanged)
    Q_PROPERTY(int workoutTotalSec READ workoutTotalSec NOTIFY setupChanged)
    Q_PROPERTY(int workoutRemainingSec READ workoutRemainingSec NOTIFY metricsChanged)
    Q_PROPERTY(bool workoutFinished READ workoutFinished NOTIFY intervalChanged)
    Q_PROPERTY(int currentStepIndex READ currentStepIndex NOTIFY intervalChanged)
    Q_PROPERTY(QString currentStepName READ currentStepName NOTIFY intervalChanged)
    Q_PROPERTY(int stepRemainingSec READ stepRemainingSec NOTIFY metricsChanged)
    Q_PROPERTY(double targetSpeedKmh READ targetSpeedKmh NOTIFY metricsChanged)
    Q_PROPERTY(QString targetPace READ targetPace NOTIFY metricsChanged)
    Q_PROPERTY(int targetPercent READ targetPercent NOTIFY metricsChanged)      // % of AnT
    Q_PROPERTY(double targetInclinePercent READ targetInclinePercent NOTIFY metricsChanged)
    Q_PROPERTY(QString nextStepText READ nextStepText NOTIFY intervalChanged)
    Q_PROPERTY(int compliance READ compliance NOTIFY metricsChanged)            // % of target seconds within ±3 %
    Q_PROPERTY(bool antTestBlockActive READ antTestBlockActive NOTIFY intervalChanged)

    // results
    Q_PROPERTY(bool completed READ completed NOTIFY resultChanged)
    Q_PROPERTY(qint64 lastRideId READ lastRideId NOTIFY resultChanged)
    Q_PROPERTY(QVariantMap antResult READ antResult NOTIFY antResultChanged)   // {} = none

public:
    enum class State { Ready, Running, Paused, Finished };
    static constexpr int kSeriesMaxPoints = 600;
    static constexpr double kComplianceFraction = 0.03;

    RunSession(Database *db, ProfileManager *profiles, SensorHub *hub, RunLibrary *library, QObject *parent = nullptr);
    ~RunSession() override;

    void setTreadmill(TreadmillDevice *treadmill);
    TreadmillDevice *treadmill() const;
    // Only one session at a time (SessionGuard wires these).
    void setStartBlocker(const QString &key, std::function<bool()> blocker, const QString &reason = {});
    void setAbandonCanceller(const QString &key, std::function<void()> cancel);

    // Tests: drive the clock manually (disables the internal timer).
    void setManualClock(bool manual);
    void advanceClock(int ms);
    void setMinimumSaveSeconds(int s) { m_minSaveSec = s; }

    QString state() const;
    State stateValue() const { return m_state; }
    bool active() const { return m_state == State::Running || m_state == State::Paused; }
    bool running() const override { return m_state == State::Running; }
    bool paused() const { return m_state == State::Paused; }
    bool hasRecording() const { return m_recordedSec > 0; }
    bool inProgress() const { return running() || (paused() && hasRecording()); }
    bool abandonable() const { return paused() && !hasRecording(); }
    void abandon() { if (abandonable()) finish(false); }
    bool treadmillConnected() const;
    QString treadmillName() const;
    bool simulatedTreadmill() const;
    QString lastError() const { return m_lastError; }

    bool gpxEnabled() const { return m_gpxEnabled; }
    void setGpxEnabled(bool on);
    QString gpxPath() const { return m_gpxRoute.isValid() ? m_gpxRoute.sourcePath() : QString(); }
    bool hasGpx() const { return m_gpxRoute.isValid(); }
    bool hasRoute() const override { return route().isValid(); }
    const RlvRoute &route() const override { return m_gpxEnabled && m_gpxRoute.isValid() ? m_gpxRoute : m_standard; }
    QString routeName() const;
    QString routePath() const override;
    bool routeIsLoop() const { return m_loop; }
    double routeLengthM() const { return route().totalDistance(); }
    double routeAscentM() const { return route().ascent(); }
    QVariantList profile() const { return m_profile; }
    double minElevationM() const { return route().minElevation(); }
    double maxElevationM() const { return route().maxElevation(); }
    double distanceMeters() const override { return m_position; }
    double startDistanceM() const override { return 0.0; }
    double elevationM() const { return route().elevationAtDistance(m_position); }
    double routeGradePercent() const;
    int lap() const { return m_lap; }

    double speedKmh() const override;
    QString pace() const;
    double inclinePercent() const;
    bool inclineKnown() const;
    int heartRate() const;
    int cadence() const override;
    bool runnerFigure() const override { return true; }
    double walkRunKmh() const override;
    double elapsedSeconds() const { return m_activeMs / 1000.0; }
    double runDistanceM() const { return m_runDistance; }
    double avgSpeedKmh() const;
    QString avgPace() const;
    int avgHeartRate() const { return m_hrSec > 0 ? int(std::lround(double(m_hrSum) / m_hrSec)) : 0; }
    int maxHeartRate() const { return m_maxHr; }
    double calories() const { return m_kcal; }
    double climbedM() const { return m_climb; }
    double rtss() const;
    int speedZone() const;
    double antSpeedKmh() const;
    QVariantList speedSeries() const;
    QVariantList heartRateSeries() const;
    QVariantList targetSeries() const;
    int seriesStep() const;

    bool hasWorkout() const { return m_workout.has_value(); }
    qint64 workoutId() const { return m_workout ? m_workout->id : -1; }
    QString workoutName() const { return m_workout ? m_workout->name : QString(); }
    bool antTest() const { return m_workout && m_workout->isAntTest(); }
    QVariantList workoutSegments() const;
    int workoutTotalSec() const { return m_workout ? m_workout->totalDurationSec() : 0; }
    int workoutRemainingSec() const;
    bool workoutFinished() const;
    int currentStepIndex() const;
    QString currentStepName() const;
    int stepRemainingSec() const;
    double targetSpeedKmh() const;
    QString targetPace() const;
    int targetPercent() const;
    double targetInclinePercent() const;
    QString nextStepText() const;
    int compliance() const { return m_targetSec > 0 ? int(std::lround(100.0 * m_inRangeSec / m_targetSec)) : 0; }
    bool antTestBlockActive() const;

    bool completed() const { return m_completed; }
    qint64 lastRideId() const { return m_lastRideId; }
    QVariantMap antResult() const { return m_antResult; }
    const QVector<Sample> &samples() const { return m_samples; }

    Q_INVOKABLE bool useStandardRoute() { setGpxEnabled(false); return true; }
    // Loads a .gpx file (path or file URL) and turns GPX on.
    Q_INVOKABLE bool selectGpx(const QString &pathOrUrl);
    // Attaches a running workout (RunLibrary id); only before the start.
    Q_INVOKABLE bool attachWorkout(qint64 id);
    Q_INVOKABLE void detachWorkout();
    // The AnT test workout.
    Q_INVOKABLE bool setupAntTest();
    Q_INVOKABLE bool start();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void resume();
    Q_INVOKABLE void togglePause();
    Q_INVOKABLE qint64 finish(bool save = true);   // ride id or -1
    Q_INVOKABLE void discard() { finish(false); }
    Q_INVOKABLE void reset();                      // back to "ready" (route + workout kept)
    // The evaluated AnT test: save it as the new AnT (true) or drop it.
    Q_INVOKABLE bool acceptAntResult();
    Q_INVOKABLE void dismissAntResult();

signals:
    void stateChanged();
    void treadmillChanged();
    void routeChanged();
    void positionChanged();
    void metricsChanged();
    void seriesChanged();
    void setupChanged();
    void intervalChanged();
    void resultChanged();
    void antResultChanged();
    void lastErrorChanged();
    void started();
    void finished();
    void rideSaved(qint64 id);
    void startRefused(const QString &reason);
    // The AnT test block is over: result = antResult (valid, speed, heartRate,
    // incline, previousSpeed, message, ...).
    void antTestCompleted(const QVariantMap &result);

private:
    void setState(State s);
    void setError(const QString &e);
    void rebuildRouteData();
    void clearRecording();
    void onTimer();
    void process(int ms);
    void recordSecond();
    void checkStep();
    void evaluateAntTestBlock();
    void suggestToTreadmill();
    qint64 saveRun();
    double workoutSecond() const { return m_activeMs / 1000.0; }

    Database *m_db;
    ProfileManager *m_profiles;
    SensorHub *m_hub;
    RunLibrary *m_library;
    QPointer<TreadmillDevice> m_treadmill;
    QList<QMetaObject::Connection> m_treadmillConnections;
    QHash<QString, QPair<std::function<bool()>, QString>> m_startBlockers;
    QHash<QString, std::function<void()>> m_abandonCancellers;

    State m_state = State::Ready;
    bool m_manualClock = false;
    bool m_inProcess = false;   // process() runs (finish() then skips its catch-up)
    int m_minSaveSec = 30;
    QTimer m_timer;
    QElapsedTimer m_clock;
    qint64 m_activeMs = 0;
    QString m_lastError;

    // route
    RlvRoute m_standard;
    RlvRoute m_gpxRoute;
    bool m_gpxEnabled = false;
    bool m_loop = true;
    QVariantList m_profile;
    double m_position = 0;
    int m_lap = 1;

    // workout
    std::optional<RunWorkout> m_workout;
    int m_lastStep = -2;
    int m_targetSec = 0, m_inRangeSec = 0;
    bool m_antEvaluated = false;
    QVariantMap m_antResult;
    qint64 m_acceptedTestId = -1;
    double m_lastSuggestSpeed = -2, m_lastSuggestIncline = -100;

    // recording
    QDateTime m_startedAt;
    Profile m_runProfile;
    double m_antAtStart = 0;
    QVector<Sample> m_samples;
    QVector<double> m_secSpeed, m_secIncline, m_secTarget;
    QVector<int> m_secHr;
    int m_recordedSec = 0;
    double m_runDistance = 0;
    double m_kcal = 0, m_climb = 0;
    qint64 m_hrSum = 0;
    int m_hrSec = 0, m_maxHr = 0;
    // accumulation of the current second
    double m_accSpeed = 0, m_accIncline = 0, m_accMs = 0;
    bool m_completed = false;
    qint64 m_lastRideId = -1;
};

} // namespace watt
