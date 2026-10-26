#pragma once

#include "Profile.h"
#include "Ride.h"
#include "WorkoutEngine.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <functional>

namespace watt {

class Database;
class ProfileManager;
class SensorHub;

// Runs a training session: drives the WorkoutEngine with a timer, sends ERG
// targets to the trainer via the SensorHub, records 1 Hz samples and stores
// the finished ride. Exposed to QML as "Training".
class TrainingSession : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool active READ active NOTIFY stateChanged)
    Q_PROPERTY(bool running READ running NOTIFY stateChanged)
    Q_PROPERTY(bool paused READ paused NOTIFY stateChanged)
    // Armed via startWhenPedaling(): workout loaded, clock stopped until the rider pedals.
    Q_PROPERTY(bool waiting READ waiting NOTIFY stateChanged)
    Q_PROPERTY(bool armedOrActive READ armedOrActive NOTIFY stateChanged)
    Q_PROPERTY(bool ergMode READ ergMode WRITE setErgMode NOTIFY ergModeChanged)
    Q_PROPERTY(bool freeRide READ freeRide NOTIFY workoutChanged)

    Q_PROPERTY(qint64 workoutId READ workoutId NOTIFY workoutChanged)
    Q_PROPERTY(QString workoutName READ workoutName NOTIFY workoutChanged)
    Q_PROPERTY(QString category READ category NOTIFY workoutChanged)
    Q_PROPERTY(QString focus READ focus NOTIFY workoutChanged)
    Q_PROPERTY(QString summary READ summary NOTIFY workoutChanged)
    Q_PROPERTY(QString description READ description NOTIFY workoutChanged)
    Q_PROPERTY(QVariantList timeline READ timeline NOTIFY workoutChanged)
    Q_PROPERTY(QVariantList segments READ segments NOTIFY workoutChanged)
    Q_PROPERTY(int blockCount READ blockCount NOTIFY workoutChanged)

    Q_PROPERTY(int elapsedSec READ elapsedSec NOTIFY tick)
    Q_PROPERTY(int totalSec READ totalSec NOTIFY workoutChanged)
    Q_PROPERTY(int remainingSec READ remainingSec NOTIFY tick)
    Q_PROPERTY(double progress READ progress NOTIFY tick)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY intervalChanged)
    Q_PROPERTY(QString currentIntervalName READ currentIntervalName NOTIFY intervalChanged)
    Q_PROPERTY(int intervalRemainingSec READ intervalRemainingSec NOTIFY tick)
    Q_PROPERTY(int targetPower READ targetPower NOTIFY targetChanged)
    Q_PROPERTY(int nextTargetPower READ nextTargetPower NOTIFY intervalChanged)
    Q_PROPERTY(int targetCadence READ targetCadence NOTIFY intervalChanged)
    Q_PROPERTY(int intensityOffset READ intensityOffset NOTIFY targetChanged)

    Q_PROPERTY(int power READ power NOTIFY metricsChanged)
    Q_PROPERTY(int cadence READ cadence NOTIFY metricsChanged)
    Q_PROPERTY(int heartRate READ heartRate NOTIFY metricsChanged)
    Q_PROPERTY(int heartRateZone READ heartRateZone NOTIFY metricsChanged)
    Q_PROPERTY(int avgPower READ avgPower NOTIFY tick)
    Q_PROPERTY(double kilojoules READ kilojoules NOTIFY tick)

    // Down-sampled (<= kMaxSeriesPoints values, seriesStep seconds each) for the UI
    Q_PROPERTY(QVariantList powerSeries READ powerSeries NOTIFY seriesChanged)
    Q_PROPERTY(QVariantList heartRateSeries READ heartRateSeries NOTIFY seriesChanged)
    Q_PROPERTY(int seriesStep READ seriesStep NOTIFY seriesChanged)
    Q_PROPERTY(qint64 lastRideId READ lastRideId NOTIFY rideSaved)

public:
    enum State { Idle = 0, Running = 1, Paused = 2, Finished = 3 };
    Q_ENUM(State)

    TrainingSession(Database *db, ProfileManager *profiles, SensorHub *hub, QObject *parent = nullptr);

    int state() const { return int(m_engine.state()); }
    bool active() const { return running() || paused(); }
    bool running() const { return m_engine.state() == WorkoutEngine::State::Running; }
    bool paused() const { return m_engine.state() == WorkoutEngine::State::Paused; }
    bool waiting() const { return m_waiting; }
    bool armedOrActive() const { return m_waiting || active(); }

    // Pedaling detection thresholds for startWhenPedaling().
    static constexpr int kStartCadenceRpm = 20;   // cadence that counts as pedaling
    static constexpr int kStartPowerWatts = 30;   // fallback for trainers without cadence
    bool ergMode() const { return m_ergMode; }
    void setErgMode(bool erg);
    bool freeRide() const { return m_engine.isFreeRide(); }

    qint64 workoutId() const { return m_engine.workout().id; }
    QString workoutName() const { return m_engine.workout().name; }
    QString category() const { return m_engine.workout().category; }
    QString focus() const { return m_engine.workout().focus; }
    QString summary() const;
    QString description() const { return m_engine.workout().description; }
    QVariantList timeline() const;
    QVariantList segments() const;
    int blockCount() const { return m_engine.workout().intervals.size(); }

    int elapsedSec() const { return m_engine.elapsedSec(); }
    int totalSec() const { return m_engine.totalSec(); }
    int remainingSec() const { return m_engine.remainingSec(); }
    double progress() const { return m_engine.progress(); }
    int currentIndex() const { return m_engine.currentIndex(); }
    QString currentIntervalName() const;
    int intervalRemainingSec() const { return m_engine.intervalRemainingSec(); }
    int targetPower() const { return m_engine.targetWatts(); }
    int nextTargetPower() const { return m_engine.nextTargetWatts(); }
    int targetCadence() const { return m_engine.targetCadence(); }
    int intensityOffset() const { return m_engine.intensityOffset(); }

    int power() const;
    int cadence() const;
    int heartRate() const;
    int heartRateZone() const;
    int avgPower() const;
    double kilojoules() const { return m_energyJ / 1000.0; }

    static constexpr int kMaxSeriesPoints = 720;
    QVariantList powerSeries() const;
    QVariantList heartRateSeries() const;
    int seriesStep() const;
    qint64 lastRideId() const { return m_lastRideId; }
    // True when the last finished ride ran the workout to its end (also when
    // the remaining intervals were skipped); false after stop()/discard().
    bool lastWorkoutCompleted() const { return m_lastCompleted; }
    const QVector<Sample> &samples() const { return m_samples; }
    const WorkoutEngine &engine() const { return m_engine; }

    // Minimum ride length that will be saved (seconds).
    void setMinimumSaveSeconds(int s) { m_minSaveSec = s; }
    // Drive the clock manually (unit tests). Disables the internal timer.
    void setManualClock(bool manual) { m_manualClock = manual; }
    void advanceClock(int ms);
    // Optional guards: while one returns true, start()/startWhenPedaling() are
    // refused (e.g. a rowing or free ride session is running; only one
    // session at a time). Each owner uses its own key; an empty function
    // removes the guard. The key-less overload is the "default" guard.
    void setStartBlocker(std::function<bool()> blocker) { setStartBlocker(QStringLiteral("default"), std::move(blocker)); }
    void setStartBlocker(const QString &key, std::function<bool()> blocker);
    bool startBlocked() const;

    // Indoor cycling only: rowing workouts are refused (false + rowingWorkoutRefused());
    // they run in RowingSession.
    Q_INVOKABLE bool loadWorkout(qint64 id);
    bool loadWorkout(const Workout &workout);
    Q_INVOKABLE void loadFreeRide();
    Q_INVOKABLE void start();               // starts the clock immediately
    Q_INVOKABLE void startWhenPedaling();   // arms; start() runs once the rider pedals
    Q_INVOKABLE void pause();
    Q_INVOKABLE void resume();
    Q_INVOKABLE void togglePause();
    Q_INVOKABLE qint64 stop();      // finishes and saves; returns ride id or -1
    Q_INVOKABLE void discard();     // stops without saving
    Q_INVOKABLE void skipInterval();
    Q_INVOKABLE void adjustIntensity(int deltaPercent);

signals:
    void stateChanged();
    void ergModeChanged();
    void workoutChanged();
    void intervalChanged();
    void targetChanged();
    void metricsChanged();
    void tick();
    void seriesChanged();
    void rideSaved(qint64 id);
    // Emitted right after rideSaved() when the saved ride completed its
    // workout (not stopped early). Plan entries are only ticked off by this.
    void workoutCompleted(qint64 rideId, qint64 workoutId);
    void finished();
    // start()/startWhenPedaling() was refused because another session (rowing) is active.
    void startRefused();
    // loadWorkout() was given a rowing workout (use Rowing.setupPlan()).
    void rowingWorkoutRefused(qint64 workoutId, const QString &name);
    // Emitted after finished() when a ride of an FTP test workout (category
    // "FTP Tests") was saved and gave a plausible estimate. The profile is not
    // changed; the UI asks the rider whether to apply it.
    void ftpTestCompleted(qint64 rideId, int estimatedFtp, int previousFtp, int best1minPower);

private:
    void onTimer();
    void checkPedaling();
    void cancelWaiting();
    void process(int ms);
    void recordUpTo(int second);
    void applyTarget(bool force = false);
    void clearRecording();
    qint64 saveRide();
    qint64 finishAndSave(bool completed);
    void checkFtpTest(qint64 rideId);
    QVariantList downsample(const QVector<int> &data, bool ignoreZero) const;

    Database *m_db;
    ProfileManager *m_profiles;
    SensorHub *m_hub;
    WorkoutEngine m_engine;
    QTimer m_timer;
    QElapsedTimer m_clock;
    bool m_manualClock = false;
    bool m_waiting = false;
    bool m_ergMode = true;
    int m_lastSentTarget = -1;
    int m_recordedSec = 0;
    int m_lastIndex = -2;
    int m_minSaveSec = 30;
    double m_energyJ = 0;
    double m_powerSum = 0;
    QDateTime m_startedAt;
    QVector<Sample> m_samples;
    QVector<int> m_powerData;
    QVector<int> m_hrData;
    qint64 m_rideMs = 0;        // ridden time (excludes pauses and skipped intervals)
    Profile m_rideProfile;      // rider snapshot taken at start()
    qint64 m_lastRideId = -1;
    bool m_lastCompleted = false;
    QHash<QString, std::function<bool()>> m_startBlockers;
};

} // namespace watt
