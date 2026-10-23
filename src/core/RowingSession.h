#pragma once

#include "PlanTypes.h"
#include "Profile.h"
#include "Ride.h"
#include "WorkoutEngine.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QList>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantList>
#include <QVector>

namespace watt {

class Database;
class ProfileManager;
class RowerDevice;
class RowingRecords;
class SensorHub;
class TrainingSession;
class WorkoutPlanner;

// Runs a rowing session on the active RowerDevice (Concept2 PM5 or simulated).
// Exposed to QML as "Rowing" (Artilody.Backend).
//
// Modes ("mode"):
//   "none"  - nothing set up (state "idle")
//   "solo"  - free row over a target distance (0 = open ended), setupSolo()
//   "plan"  - watt-plan rowing workout (sport "rowing") from the library,
//             targets = % of the rowing FTP, setupPlan()
//   "ghost" - race against the personal best over a distance, setupGhost()
// States ("state"): "idle", "ready" (set up, waits for the first stroke when
// autoStart is on), "running", "paused", "finished".
//
// Distances, strokes and calories are session relative: the monitor values at
// start are subtracted and a workout reset on the monitor (or a rower switch)
// is bridged, so the session distance never jumps.
//
// Lives in its own library (watt_rowing) because it needs both watt_core and
// the RowerDevice interface of watt_ble.
class RowingSession : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString mode READ mode NOTIFY setupChanged)
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool active READ active NOTIFY stateChanged)       // running or paused
    Q_PROPERTY(bool running READ running NOTIFY stateChanged)
    Q_PROPERTY(bool paused READ paused NOTIFY stateChanged)
    Q_PROPERTY(bool autoStart READ autoStart WRITE setAutoStart NOTIFY autoStartChanged)
    Q_PROPERTY(bool rowerConnected READ rowerConnected NOTIFY rowerConnectedChanged)
    Q_PROPERTY(QString rowerName READ rowerName NOTIFY rowerConnectedChanged)
    Q_PROPERTY(QList<int> standardDistances READ standardDistances CONSTANT)

    // live / session values
    Q_PROPERTY(double elapsedSeconds READ elapsedSeconds NOTIFY metricsChanged)
    Q_PROPERTY(double distanceMeters READ distanceMeters NOTIFY metricsChanged)
    Q_PROPERTY(int targetDistanceM READ targetDistanceM NOTIFY setupChanged)
    Q_PROPERTY(double progress READ progress NOTIFY metricsChanged)               // 0..1
    Q_PROPERTY(double remainingMeters READ remainingMeters NOTIFY metricsChanged)
    Q_PROPERTY(int watts READ watts NOTIFY metricsChanged)
    Q_PROPERTY(int avgWatts READ avgWatts NOTIFY metricsChanged)
    Q_PROPERTY(int maxWatts READ maxWatts NOTIFY metricsChanged)
    Q_PROPERTY(int spm READ spm NOTIFY metricsChanged)
    // Session averages over the recorded seconds with a value (0 = none yet).
    Q_PROPERTY(int avgSpm READ avgSpm NOTIFY metricsChanged)
    Q_PROPERTY(int heartRate READ heartRate NOTIFY metricsChanged)
    Q_PROPERTY(int avgHeartRate READ avgHeartRate NOTIFY metricsChanged)
    Q_PROPERTY(double pace500 READ pace500 NOTIFY metricsChanged)                  // s / 500 m
    Q_PROPERTY(double avgPace500 READ avgPace500 NOTIFY metricsChanged)
    Q_PROPERTY(int strokeCount READ strokeCount NOTIFY metricsChanged)
    Q_PROPERTY(int calories READ calories NOTIFY metricsChanged)
    Q_PROPERTY(QVariantList powerSeries READ powerSeries NOTIFY seriesChanged)    // mean W per seriesStep s
    Q_PROPERTY(int seriesStep READ seriesStep NOTIFY seriesChanged)
    // Same buckets as powerSeries: mean heart rate (0 = none), pace s/500 m
    // over the bucket (at least the trailing 5 s; 0 = no movement) and session
    // distance (m) at the bucket end.
    Q_PROPERTY(QVariantList heartRateSeries READ heartRateSeries NOTIFY seriesChanged)
    Q_PROPERTY(QVariantList paceSeries READ paceSeries NOTIFY seriesChanged)
    Q_PROPERTY(QVariantList distanceSeries READ distanceSeries NOTIFY seriesChanged)

    // plan mode
    Q_PROPERTY(qint64 workoutId READ workoutId NOTIFY setupChanged)
    Q_PROPERTY(QString workoutName READ workoutName NOTIFY setupChanged)
    Q_PROPERTY(int rowingFtp READ rowingFtp NOTIFY setupChanged)
    Q_PROPERTY(QVariantList intervals READ intervals NOTIFY setupChanged)
    Q_PROPERTY(int intervalCount READ intervalCount NOTIFY setupChanged)
    Q_PROPERTY(int totalDurationSec READ totalDurationSec NOTIFY setupChanged)
    Q_PROPERTY(int remainingSec READ remainingSec NOTIFY metricsChanged)
    Q_PROPERTY(int currentIntervalIndex READ currentIntervalIndex NOTIFY intervalChanged)
    Q_PROPERTY(QString currentIntervalName READ currentIntervalName NOTIFY intervalChanged)
    Q_PROPERTY(int targetWatts READ targetWatts NOTIFY metricsChanged)
    Q_PROPERTY(int nextTargetWatts READ nextTargetWatts NOTIFY intervalChanged)
    Q_PROPERTY(int intervalRemainingSec READ intervalRemainingSec NOTIFY metricsChanged)
    Q_PROPERTY(int intervalAvgWatts READ intervalAvgWatts NOTIFY metricsChanged)
    Q_PROPERTY(int compliance READ compliance NOTIFY metricsChanged)              // % of seconds within +-10 %

    // ghost mode
    Q_PROPERTY(bool hasGhost READ hasGhost NOTIFY setupChanged)
    Q_PROPERTY(double recordTimeSec READ recordTimeSec NOTIFY setupChanged)
    Q_PROPERTY(double recordPace500 READ recordPace500 NOTIFY setupChanged)
    Q_PROPERTY(double ghostDistanceM READ ghostDistanceM NOTIFY metricsChanged)
    Q_PROPERTY(double ghostGapMeters READ ghostGapMeters NOTIFY metricsChanged)   // > 0: ahead of the ghost
    Q_PROPERTY(double ghostGapSeconds READ ghostGapSeconds NOTIFY metricsChanged) // > 0: ahead of the ghost

    // results (valid in state "finished")
    Q_PROPERTY(bool completed READ completed NOTIFY resultChanged)                // full distance / workout done
    Q_PROPERTY(bool newRecord READ newRecord NOTIFY resultChanged)
    Q_PROPERTY(double previousRecordSec READ previousRecordSec NOTIFY resultChanged) // 0 = first record
    Q_PROPERTY(double finishTimeSec READ finishTimeSec NOTIFY resultChanged)
    Q_PROPERTY(qint64 lastRideId READ lastRideId NOTIFY resultChanged)

public:
    enum class Mode { None, Solo, Plan, Ghost };
    enum class State { Idle, Ready, Running, Paused, Finished };

    RowingSession(Database *db, ProfileManager *profiles, SensorHub *hub, RowingRecords *records,
                  WorkoutPlanner *planner, QObject *parent = nullptr);
    ~RowingSession() override;

    // The active rower (DeviceManager::rower(); may be null / replaced at any time).
    void setRower(RowerDevice *rower);
    RowerDevice *rower() const;
    // Only one session at a time: installs a start guard on the cycling
    // session; start() refuses while it is active (cancels it when only armed).
    void setTrainingSession(TrainingSession *training);

    // Tests: drive the clock manually (disables the internal timer).
    void setManualClock(bool manual) { m_manualClock = manual; }
    void advanceClock(int ms);
    void setMinimumSaveSeconds(int s) { m_minSaveSec = s; }

    QString mode() const;
    QString state() const;
    Mode modeValue() const { return m_mode; }
    State stateValue() const { return m_state; }
    bool active() const { return m_state == State::Running || m_state == State::Paused; }
    bool running() const { return m_state == State::Running; }
    bool paused() const { return m_state == State::Paused; }
    bool autoStart() const { return m_autoStart; }
    void setAutoStart(bool on);
    bool rowerConnected() const;
    QString rowerName() const;
    static QList<int> standardDistances();

    double elapsedSeconds() const;
    double distanceMeters() const { return m_distance; }
    int targetDistanceM() const { return m_targetDistance; }
    double progress() const;
    double remainingMeters() const;
    int watts() const;
    int avgWatts() const;
    int maxWatts() const { return m_maxWatts; }
    int spm() const;
    int avgSpm() const;
    int heartRate() const;
    int avgHeartRate() const;
    double pace500() const;
    double avgPace500() const;
    int strokeCount() const { return m_strokes; }
    int calories() const { return m_calories; }
    QVariantList powerSeries() const;
    int seriesStep() const;
    QVariantList heartRateSeries() const;
    QVariantList paceSeries() const;
    QVariantList distanceSeries() const;

    qint64 workoutId() const { return m_mode == Mode::Plan ? m_engine.workout().id : -1; }
    QString workoutName() const;
    int rowingFtp() const;
    QVariantList intervals() const { return m_intervals; }
    int intervalCount() const { return m_mode == Mode::Plan ? int(m_engine.workout().intervals.size()) : 0; }
    int totalDurationSec() const { return m_mode == Mode::Plan ? m_engine.totalSec() : 0; }
    int remainingSec() const { return m_mode == Mode::Plan ? m_engine.remainingSec() : 0; }
    int currentIntervalIndex() const;
    QString currentIntervalName() const;
    int targetWatts() const;
    int nextTargetWatts() const;
    int intervalRemainingSec() const;
    int intervalAvgWatts() const;
    int compliance() const;

    bool hasGhost() const { return m_hasGhost; }
    double recordTimeSec() const { return m_recordTime; }
    double recordPace500() const;
    double ghostDistanceM() const;
    double ghostGapMeters() const;
    double ghostGapSeconds() const;

    bool completed() const { return m_completed; }
    bool newRecord() const { return m_newRecord; }
    double previousRecordSec() const { return m_previousRecord; }
    double finishTimeSec() const { return m_finishTime; }
    qint64 lastRideId() const { return m_lastRideId; }
    const QVector<Sample> &samples() const { return m_samples; }
    const QVector<TracePoint> &trace() const { return m_trace; }

    // Time (s) at which the trace first reached distance d (linear
    // interpolation from [0,0]; beyond the end -> time of the last point).
    static double traceTimeAt(const QVector<TracePoint> &trace, double d);

    // Rowing workouts usable by setupPlan(): [{workoutId, name, totalSec, summary}],
    // sorted by name (independent of the shared Workouts filter).
    Q_INVOKABLE QVariantList rowingWorkouts() const;

    Q_INVOKABLE bool setupSolo(int distanceM);      // 0 = open-ended free row
    Q_INVOKABLE bool setupPlan(qint64 workoutId);   // rowing workouts only
    Q_INVOKABLE bool setupGhost(int distanceM);
    Q_INVOKABLE bool start();                       // "start now" (from idle: open-ended solo)
    Q_INVOKABLE void pause();
    Q_INVOKABLE void resume();
    Q_INVOKABLE void togglePause();
    Q_INVOKABLE qint64 stop() { return finish(true); }
    Q_INVOKABLE qint64 finish(bool save = true);    // ride id or -1
    Q_INVOKABLE void reset();                       // discard; back to "ready" (same setup) or "idle"
    Q_INVOKABLE void clear();                       // discard and forget the setup ("idle", mode "none")

signals:
    void setupChanged();
    void stateChanged();
    void autoStartChanged();
    void rowerConnectedChanged();
    void metricsChanged();
    void intervalChanged();
    void seriesChanged();
    void resultChanged();
    void started();
    void finished();
    void rideSaved(qint64 id);
    void newRecordSet(int distanceM, double timeSec, double previousTimeSec);
    void startRefused(const QString &reason);

private:
    struct Raw
    {
        double distance = 0;
        int strokes = 0;
        int calories = 0;
    };
    Raw raw() const;
    void captureReady();
    void rebase();
    void setState(State s);
    void prepare(Mode mode);
    void clearRecording();
    void clearResults();
    double nowSec() const;
    void onTimer();
    void process(int ms);
    void recordUpTo(int second);
    void onMeasurement();
    void onStroke();
    void onRowingActive(bool on);
    void onWorkoutReset();
    void maybeAutoStart();
    void extendTrace(double now, double d);
    void completeDistance(double finishTime);
    qint64 finishInternal(bool save, bool completed, double finishTime);
    qint64 saveRide();
    void applyTarget(bool force = false);
    void releaseTarget();
    void reloadGhost();
    void reloadPlan();

    Database *m_db;
    ProfileManager *m_profiles;
    SensorHub *m_hub;
    RowingRecords *m_records;
    WorkoutPlanner *m_planner;
    QPointer<TrainingSession> m_training;
    QPointer<RowerDevice> m_rower;
    QList<QMetaObject::Connection> m_rowerConnections;

    Mode m_mode = Mode::None;
    State m_state = State::Idle;
    bool m_autoStart = true;
    bool m_manualClock = false;
    int m_minSaveSec = 30;
    QTimer m_timer;
    QElapsedTimer m_clock;
    qint64 m_activeMs = 0;

    int m_targetDistance = 0;
    Raw m_ready;          // monitor values at the last update before start
    Raw m_offset;         // subtracted from the monitor values while active
    double m_distance = 0;
    int m_strokes = 0;
    int m_calories = 0;
    double m_prevT = 0, m_prevD = 0;

    // plan
    qint64 m_planWorkoutId = -1;
    WorkoutEngine m_engine;
    QVariantList m_intervals;
    int m_lastIndex = -2;
    int m_lastSentTarget = -1;
    int m_targetSec = 0, m_inRangeSec = 0;
    double m_intervalPowerSum = 0;
    int m_intervalSec = 0;

    // ghost
    bool m_hasGhost = false;
    double m_recordTime = 0;
    QVector<TracePoint> m_ghostTrace;

    // recording
    QDateTime m_startedAt;
    Profile m_rideProfile;
    QVector<Sample> m_samples;
    QVector<int> m_powerData;
    QVector<double> m_distanceData;   // session distance at each recorded second
    QVector<TracePoint> m_trace;
    int m_recordedSec = 0;
    double m_powerSum = 0;
    int m_maxWatts = 0;
    qint64 m_spmSum = 0, m_hrSum = 0;
    int m_spmSec = 0, m_hrSec = 0;

    // results
    bool m_completed = false;
    bool m_newRecord = false;
    double m_previousRecord = 0;
    double m_finishTime = 0;
    qint64 m_lastRideId = -1;
};

} // namespace watt
