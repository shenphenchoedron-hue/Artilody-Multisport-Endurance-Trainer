#include "TrainingSession.h"

#include "Database.h"
#include "ProfileManager.h"
#include "RideAnalysis.h"
#include "SensorHub.h"
#include "SessionClock.h"
#include "TimeFormat.h"

#include <QVariantMap>
#include <algorithm>
#include <cmath>

namespace watt {

TrainingSession::TrainingSession(Database *db, ProfileManager *profiles, SensorHub *hub, QObject *parent)
    : QObject(parent)
    , m_db(db)
    , m_profiles(profiles)
    , m_hub(hub)
{
    m_timer.setInterval(250);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &TrainingSession::onTimer);

    connect(m_hub, &SensorHub::powerChanged, this, &TrainingSession::metricsChanged);
    connect(m_hub, &SensorHub::cadenceChanged, this, &TrainingSession::metricsChanged);
    connect(m_hub, &SensorHub::heartRateChanged, this, &TrainingSession::metricsChanged);

    connect(m_profiles, &ProfileManager::profileChanged, this, [this] {
        if (!active()) {
            m_engine.setFtp(m_profiles->current().ftpFor(m_engine.workout().sport));
            m_ergMode = m_profiles->defaultErg();
            emit ergModeChanged();
            emit workoutChanged();
            emit targetChanged();
            emit metricsChanged();
        }
    });

    m_ergMode = m_profiles->defaultErg();
    m_engine.loadFreeRide();
}

void TrainingSession::setStartBlocker(const QString &key, std::function<bool()> blocker)
{
    if (blocker)
        m_startBlockers.insert(key, std::move(blocker));
    else
        m_startBlockers.remove(key);
}

void TrainingSession::setAbandonCanceller(const QString &key, std::function<void()> cancel)
{
    if (cancel)
        m_abandonCancellers.insert(key, std::move(cancel));
    else
        m_abandonCancellers.remove(key);
}

void TrainingSession::abandonOthers()
{
    for (auto it = m_abandonCancellers.cbegin(); it != m_abandonCancellers.cend(); ++it) {
        if (it.value())
            it.value()();
    }
}

void TrainingSession::abandon()
{
    if (abandonable())
        discard();
}

bool TrainingSession::startBlocked() const
{
    for (auto it = m_startBlockers.cbegin(); it != m_startBlockers.cend(); ++it) {
        if (it.value() && it.value()())
            return true;
    }
    return false;
}

void TrainingSession::setErgMode(bool erg)
{
    if (m_ergMode == erg)
        return;
    m_ergMode = erg;
    emit ergModeChanged();
    if (active())
        applyTarget(true);
}

QString TrainingSession::summary() const
{
    const Workout &w = m_engine.workout();
    if (m_engine.isFreeRide())
        return QStringLiteral("Ride at your own pace • No targets");
    QStringList parts { w.summary(m_engine.ftp()) };
    if (!w.focus.isEmpty())
        parts << w.focus;
    parts << QStringLiteral("%1 min total").arg(int(std::lround(w.totalDurationSec() / 60.0)));
    return parts.join(QStringLiteral("  •  "));
}

QVariantList TrainingSession::timeline() const
{
    QVariantList list;
    const Workout &w = m_engine.workout();
    const ZoneSet &zones = m_profiles->current().zones;
    int start = 0;
    for (const Interval &i : w.intervals) {
        QVariantMap m;
        m.insert(QStringLiteral("name"), i.name);
        m.insert(QStringLiteral("start"), start);
        m.insert(QStringLiteral("duration"), i.durationSec);
        m.insert(QStringLiteral("durationText"), formatClock(i.durationSec));
        const int w0 = i.wattsAt(0, m_engine.ftp());
        const int w1 = i.wattsAt(i.durationSec, m_engine.ftp());
        m.insert(QStringLiteral("watts"), w0);
        m.insert(QStringLiteral("wattsText"), i.isRamp() ? QStringLiteral("%1–%2 W").arg(w0).arg(w1)
                                                          : QStringLiteral("%1 W").arg(w0));
        m.insert(QStringLiteral("zone"), zoneForInterval(i, i.averagePercent(), zones));
        list.append(m);
        start += i.durationSec;
    }
    return list;
}

QVariantList TrainingSession::segments() const
{
    return Workout::segmentsToVariant(m_engine.workout().segments(m_engine.ftp(), m_profiles->current().zones));
}

QString TrainingSession::currentIntervalName() const
{
    const int idx = m_engine.currentIndex();
    if (idx < 0)
        return m_engine.isFreeRide() ? QStringLiteral("Free Ride") : QString();
    return m_engine.workout().intervals.at(idx).name;
}

int TrainingSession::power() const { return m_hub->power(); }
int TrainingSession::cadence() const { return m_hub->cadence(); }
int TrainingSession::heartRate() const { return m_hub->heartRate(); }

int TrainingSession::heartRateZone() const
{
    return m_profiles->heartRateZoneFor(m_hub->heartRate());
}

int TrainingSession::avgPower() const
{
    return m_recordedSec > 0 ? int(std::lround(m_powerSum / m_recordedSec)) : 0;
}

bool TrainingSession::loadWorkout(qint64 id)
{
    if (active())
        return false;
    auto w = m_db->workout(id);
    if (!w)
        return false;
    return loadWorkout(*w);
}

bool TrainingSession::loadWorkout(const Workout &workout)
{
    if (active())
        return false;
    if (workout.isRowing()) {
        // The cycling session never shows a rowing workout (Rowing.setupPlan()).
        emit rowingWorkoutRefused(workout.id, workout.name);
        return false;
    }
    if (workout.isRunning()) {
        // running workouts are run on the Treadmill Run page (Running.attachWorkout())
        emit runningWorkoutRefused(workout.id, workout.name);
        return false;
    }
    m_engine.load(workout, m_profiles->current().ftpFor(workout.sport));
    clearRecording();
    m_lastIndex = -2;
    emit workoutChanged();
    emit stateChanged();
    emit intervalChanged();
    emit targetChanged();
    emit tick();
    return true;
}

void TrainingSession::loadFreeRide()
{
    if (active())
        return;
    m_engine.loadFreeRide();
    clearRecording();
    emit workoutChanged();
    emit stateChanged();
    emit intervalChanged();
    emit targetChanged();
    emit tick();
}

void TrainingSession::clearRecording()
{
    m_samples.clear();
    m_powerData.clear();
    m_hrData.clear();
    m_recordedSec = 0;
    m_rideMs = 0;
    m_energyJ = 0;
    m_powerSum = 0;
    m_lastSentTarget = -1;
    emit seriesChanged();
}

int TrainingSession::seriesStep() const
{
    return std::max(1, int(std::ceil(m_powerData.size() / double(kMaxSeriesPoints))));
}

QVariantList TrainingSession::downsample(const QVector<int> &data, bool ignoreZero) const
{
    // Bounded size for QML: at most kMaxSeriesPoints values, each the mean of
    // seriesStep() seconds. Keeps the per-second UI cost constant on long rides.
    QVariantList out;
    const int step = seriesStep();
    out.reserve(data.size() / step + 1);
    for (int i = 0; i < data.size(); i += step) {
        int sum = 0, cnt = 0;
        for (int j = i; j < std::min<int>(data.size(), i + step); ++j) {
            if (ignoreZero && data.at(j) <= 0)
                continue;
            sum += data.at(j);
            ++cnt;
        }
        out.append(cnt > 0 ? int(std::lround(double(sum) / cnt)) : 0);
    }
    return out;
}

QVariantList TrainingSession::powerSeries() const
{
    return downsample(m_powerData, false);
}

QVariantList TrainingSession::heartRateSeries() const
{
    return downsample(m_hrData, true);
}

void TrainingSession::start()
{
    if (active())
        return;
    if (startBlocked()) {
        emit startRefused();
        return;
    }
    abandonOthers();
    if (m_engine.state() == WorkoutEngine::State::Finished)
        m_engine.reset();   // a new ride: no intensity offset / position from the last one
    m_skipped = false;
    const Workout &w = m_engine.workout();
    // Snapshot the rider so profile changes during the ride cannot alter it.
    m_rideProfile = m_profiles->current();
    m_engine.setFtp(m_rideProfile.ftpFor(w.sport));
    clearRecording();
    m_lastIndex = -2;
    m_lastRideId = -1;
    m_lastCompleted = false;
    m_startedAt = QDateTime::currentDateTime();
    m_engine.start();
    if (w.id >= 0)
        m_db->incrementWorkoutUsage(w.id);
    m_clock.start();
    if (!m_manualClock)
        m_timer.start();
    applyTarget(true);
    emit stateChanged();
    emit intervalChanged();
    emit targetChanged();
}

void TrainingSession::pause()
{
    if (!running())
        return;
    if (!m_manualClock)
        process(sessionClockStep(m_clock.restart()));
    if (!running())
        return;   // the workout finished during the last time slice
    m_engine.pause();
    m_timer.stop();   // nothing to do while paused
    // Release ERG while stopped, otherwise the trainer keeps the resistance.
    m_hub->requestFreeRide();
    m_lastSentTarget = -1;
    emit stateChanged();
}

void TrainingSession::resume()
{
    if (!paused())
        return;
    m_engine.resume();
    m_clock.restart();
    if (!m_manualClock)
        m_timer.start();
    applyTarget(true);
    emit stateChanged();
}

void TrainingSession::togglePause()
{
    if (running())
        pause();
    else if (paused())
        resume();
    else
        start();
}

qint64 TrainingSession::stop()
{
    if (!active())
        return -1;
    if (running() && !m_manualClock)
        process(sessionClockStep(m_clock.restart()));
    if (!active())
        return m_lastRideId;   // process() just completed and saved the workout
    return finishAndSave(false);
}

qint64 TrainingSession::finishAndSave(bool completed)
{
    m_timer.stop();
    m_engine.finish();
    m_hub->requestFreeRide();
    m_lastSentTarget = -1;
    m_lastCompleted = completed;
    const qint64 id = saveRide();
    if (id >= 0 && completed)
        emit workoutCompleted(id, m_engine.workout().id);
    emit stateChanged();
    emit finished();
    if (id >= 0 && !m_skipped)
        checkFtpTest(id);   // stopped (exhausted) or completed - but not with skipped parts
    return id;
}

void TrainingSession::discard()
{
    if (!active())
        return;
    m_timer.stop();
    m_lastCompleted = false;
    m_engine.finish();
    m_hub->requestFreeRide();
    m_lastSentTarget = -1;
    emit stateChanged();
}

void TrainingSession::skipInterval()
{
    if (!active())
        return;
    // Only the workout position jumps; ridden time (and recorded samples) do not.
    m_engine.skipInterval();
    m_skipped = true;
    emit intervalChanged();
    emit targetChanged();
    emit tick();
    if (m_engine.state() == WorkoutEngine::State::Finished) {
        // skipped past the last interval: saved, but not a completed workout
        // (no plan entry ticked off, no FTP test result)
        finishAndSave(false);
        return;
    }
    applyTarget();
}

void TrainingSession::adjustIntensity(int delta)
{
    m_engine.setIntensityOffset(m_engine.intensityOffset() + delta);
    applyTarget();
    emit targetChanged();
}

void TrainingSession::advanceClock(int ms)
{
    process(ms);
}

void TrainingSession::advanceWallClock(qint64 elapsedMs)
{
    process(sessionClockStep(elapsedMs));
}

void TrainingSession::onTimer()
{
    process(sessionClockStep(m_clock.restart()));
}

void TrainingSession::process(int ms)
{
    m_hub->expireStale();
    if (!running() || ms <= 0)
        return;
    m_engine.advance(ms);
    m_rideMs += ms;
    recordUpTo(int(m_rideMs / 1000));

    const int idx = m_engine.currentIndex();
    if (idx != m_lastIndex) {
        m_lastIndex = idx;
        emit intervalChanged();
    }
    applyTarget();
    emit targetChanged();
    emit tick();

    if (m_engine.state() == WorkoutEngine::State::Finished)
        finishAndSave(true);   // workout completed: store it automatically
}

void TrainingSession::recordUpTo(int second)
{
    bool added = false;
    while (m_recordedSec < second) {
        Sample s;
        s.t = m_recordedSec;
        s.power = m_hub->power();
        s.cadence = m_hub->cadence();
        s.heartRate = m_hub->heartRate();
        // the target actually requested (includes the intensity adjustment)
        s.targetPower = m_engine.isFreeRide() ? 0 : m_engine.targetWatts();
        m_samples.append(s);
        m_powerData.append(s.power);
        m_hrData.append(s.heartRate);
        m_energyJ += s.power;
        m_powerSum += s.power;
        ++m_recordedSec;
        added = true;
    }
    if (added)
        emit seriesChanged();
}

void TrainingSession::applyTarget(bool force)
{
    if (!running())
        return;
    if (!m_ergMode || m_engine.isFreeRide()) {
        if (force || m_lastSentTarget != 0) {
            m_hub->requestFreeRide();
            m_lastSentTarget = 0;
        }
        return;
    }
    const int target = m_engine.targetWatts();
    if (force || target != m_lastSentTarget) {
        m_hub->requestTargetPower(target);
        m_lastSentTarget = target;
    }
}

void TrainingSession::checkFtpTest(qint64 rideId)
{
    const Workout::FtpTestKind kind = m_engine.workout().ftpTestKind();
    if (kind == Workout::FtpTestKind::None)
        return;   // not an FTP test, or one whose protocol is not known: no result
    // Only offer the result to the rider who did the test (the profile may
    // have been switched during the ride).
    if (m_profiles->profileId() != m_rideProfile.id)
        return;
    const analysis::FtpEstimate e = analysis::ftpTestEstimate(m_samples, kind);
    if (e.ftp <= 0)
        return;
    emit ftpTestCompleted(rideId, e.ftp, m_profiles->ftp(), e.bestPower, e.method);
}

qint64 TrainingSession::saveRide()
{
    if (m_samples.size() < m_minSaveSec)
        return -1;
    const Profile &p = m_rideProfile;
    RideRecord r;
    r.profileId = p.id;
    r.workoutId = m_engine.workout().id;
    r.workoutName = m_engine.workout().name;
    r.startedAt = m_startedAt;
    r.blocks = m_engine.workout().intervals.size();
    r.ergMode = m_ergMode;
    r.sport = sport::normalize(m_engine.workout().sport);
    r.ftp = p.ftpFor(r.sport);
    analysis::applyStats(r, analysis::analyze(m_samples, r.ftp, p.zones));
    m_lastRideId = m_db->saveRide(r, m_samples);
    if (m_lastRideId >= 0)
        emit rideSaved(m_lastRideId);
    return m_lastRideId;
}

} // namespace watt
