#include "RowingSession.h"

#include "Database.h"
#include "ProfileManager.h"
#include "RideAnalysis.h"
#include "RowingRecords.h"
#include "SensorDevices.h"
#include "SensorHub.h"
#include "TrainingSession.h"
#include "WorkoutPlanner.h"

#include <QVariantMap>
#include <algorithm>
#include <cmath>

namespace watt {

namespace {
constexpr int kMaxSeriesPoints = 600;
constexpr int kPaceWindowSec = 5;
constexpr int kTickMs = 250;
} // namespace

RowingSession::RowingSession(Database *db, ProfileManager *profiles, SensorHub *hub, RowingRecords *records,
                             WorkoutPlanner *planner, QObject *parent)
    : QObject(parent)
    , m_db(db)
    , m_profiles(profiles)
    , m_hub(hub)
    , m_records(records)
    , m_planner(planner)
{
    m_timer.setInterval(kTickMs);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &RowingSession::onTimer);

    if (m_hub)
        connect(m_hub, &SensorHub::heartRateChanged, this, &RowingSession::metricsChanged);

    // Rider / FTP / records changes refresh what a not yet started session shows.
    connect(m_profiles, &ProfileManager::profileChanged, this, [this] {
        if (active() || m_state == State::Finished)
            return;
        if (m_mode == Mode::Plan)
            reloadPlan();
        else if (m_mode == Mode::Ghost)
            reloadGhost();
        emit setupChanged();
        emit intervalChanged();
        emit metricsChanged();
    });
    if (m_records) {
        connect(m_records, &RowingRecords::recordsChanged, this, [this] {
            if (m_mode == Mode::Ghost && !active() && m_state != State::Finished) {
                reloadGhost();
                emit setupChanged();
            }
        });
    }
}

RowingSession::~RowingSession()
{
    releaseTarget();
    for (const auto &c : std::as_const(m_rowerConnections))
        disconnect(c);
    if (m_training)
        m_training->setStartBlocker(QStringLiteral("rowing"), {});
}

// --- wiring -----------------------------------------------------------------

void RowingSession::setRower(RowerDevice *rower)
{
    if (m_rower == rower)
        return;
    for (const auto &c : std::as_const(m_rowerConnections))
        disconnect(c);
    m_rowerConnections.clear();
    if (m_rower && m_lastSentTarget > 0)
        m_rower->requestTargetPower(0);
    m_rower = rower;
    if (rower) {
        m_rowerConnections << connect(rower, &RowerDevice::measurementChanged, this, &RowingSession::onMeasurement)
                           << connect(rower, &RowerDevice::strokeMeasured, this, &RowingSession::onStroke)
                           << connect(rower, &RowerDevice::rowingActiveChanged, this, &RowingSession::onRowingActive)
                           << connect(rower, &RowerDevice::workoutReset, this, &RowingSession::onWorkoutReset)
                           << connect(rower, &RowerDevice::connectedChanged, this, &RowingSession::rowerConnectedChanged)
                           << connect(rower, &SensorDevice::stateChanged, this, &RowingSession::rowerConnectedChanged)
                           << connect(rower, &QObject::destroyed, this, [this] {
                                  emit rowerConnectedChanged();
                                  emit metricsChanged();
                              });
    }
    // A different monitor has different counters: continue seamlessly. A
    // brand-new device object has not reported anything yet (all zeros), so
    // the rebase waits for its first measurement.
    if (active())
        rebaseOrDefer();
    else
        captureReady();
    if (running())
        applyTarget(true);
    emit rowerConnectedChanged();
    emit metricsChanged();
}

void RowingSession::setTrainingSession(TrainingSession *training)
{
    if (m_training)
        m_training->setStartBlocker(QStringLiteral("rowing"), {});
    m_training = training;
    if (training)
        training->setStartBlocker(QStringLiteral("rowing"), [this] { return active(); });
}

void RowingSession::setStartBlocker(const QString &key, std::function<bool()> blocker, const QString &reason)
{
    if (blocker)
        m_startBlockers.insert(key, qMakePair(std::move(blocker),
                                              reason.isEmpty() ? QStringLiteral("Another session is running.") : reason));
    else
        m_startBlockers.remove(key);
}

RowerDevice *RowingSession::rower() const
{
    return m_rower.data();
}

bool RowingSession::rowerConnected() const
{
    return m_rower && m_rower->connected();
}

QString RowingSession::rowerName() const
{
    return m_rower ? m_rower->name() : QString();
}

QList<int> RowingSession::standardDistances()
{
    return RowingRecords::standardDistances();
}

void RowingSession::setAutoStart(bool on)
{
    if (m_autoStart == on)
        return;
    m_autoStart = on;
    emit autoStartChanged();
}

// --- strings ----------------------------------------------------------------

QString RowingSession::mode() const
{
    switch (m_mode) {
    case Mode::Solo: return QStringLiteral("solo");
    case Mode::Plan: return QStringLiteral("plan");
    case Mode::Ghost: return QStringLiteral("ghost");
    case Mode::None: break;
    }
    return QStringLiteral("none");
}

QString RowingSession::state() const
{
    switch (m_state) {
    case State::Ready: return QStringLiteral("ready");
    case State::Running: return QStringLiteral("running");
    case State::Paused: return QStringLiteral("paused");
    case State::Finished: return QStringLiteral("finished");
    case State::Idle: break;
    }
    return QStringLiteral("idle");
}

QString RowingSession::workoutName() const
{
    switch (m_mode) {
    case Mode::Plan: return m_engine.workout().name;
    case Mode::Solo:
        return m_targetDistance > 0 ? QStringLiteral("Row %1 m").arg(m_targetDistance) : QStringLiteral("Free Row");
    case Mode::Ghost: return QStringLiteral("Ghost Race %1 m").arg(m_targetDistance);
    case Mode::None: break;
    }
    return QString();
}

// --- live values --------------------------------------------------------------

double RowingSession::nowSec() const
{
    qint64 ms = m_activeMs;
    if (running() && !m_manualClock && m_clock.isValid())
        ms += m_clock.elapsed();
    return ms / 1000.0;
}

double RowingSession::elapsedSeconds() const
{
    if (m_state == State::Finished)
        return m_finishTime;
    if (active())
        return nowSec();
    return 0.0;
}

double RowingSession::progress() const
{
    if (m_mode == Mode::Plan)
        return m_engine.progress();
    if (m_targetDistance <= 0)
        return 0.0;
    return std::clamp(m_distance / m_targetDistance, 0.0, 1.0);
}

double RowingSession::remainingMeters() const
{
    if (m_mode == Mode::Plan || m_targetDistance <= 0)
        return 0.0;
    return std::max(0.0, m_targetDistance - m_distance);
}

int RowingSession::watts() const { return m_rower ? m_rower->watts() : 0; }
int RowingSession::spm() const { return m_rower ? m_rower->strokesPerMinute() : 0; }

int RowingSession::heartRate() const
{
    // Same priority as DeviceManager: a dedicated heart rate sensor wins over
    // a strap paired to the rowing monitor.
    if (m_hub && m_hub->heartRateConnected() && m_hub->heartRate() > 0)
        return m_hub->heartRate();
    if (m_rower && m_rower->heartRateBpm() > 0)
        return m_rower->heartRateBpm();
    return m_hub ? m_hub->heartRate() : 0;
}

double RowingSession::pace500() const
{
    if (!m_rower)
        return 0.0;
    if (m_rower->pace500Seconds() > 0)
        return m_rower->pace500Seconds();
    const int w = m_rower->watts();
    // Concept2: watts = 2.80 / (s per m)^3
    return w > 0 ? 500.0 * std::cbrt(2.80 / w) : 0.0;
}

double RowingSession::avgPace500() const
{
    return m_distance >= 1.0 ? 500.0 * elapsedSeconds() / m_distance : 0.0;
}

int RowingSession::avgWatts() const
{
    return m_recordedSec > 0 ? int(std::lround(m_powerSum / m_recordedSec)) : 0;
}

int RowingSession::avgSpm() const
{
    return m_spmSec > 0 ? int(std::lround(double(m_spmSum) / m_spmSec)) : 0;
}

int RowingSession::avgHeartRate() const
{
    return m_hrSec > 0 ? int(std::lround(double(m_hrSum) / m_hrSec)) : 0;
}

int RowingSession::seriesStep() const
{
    return std::max(1, int(std::ceil(m_powerData.size() / double(kMaxSeriesPoints))));
}

QVariantList RowingSession::powerSeries() const
{
    QVariantList out;
    const int step = seriesStep();
    for (int i = 0; i < m_powerData.size(); i += step) {
        int sum = 0, cnt = 0;
        for (int j = i; j < std::min<int>(m_powerData.size(), i + step); ++j, ++cnt)
            sum += m_powerData.at(j);
        out.append(cnt > 0 ? int(std::lround(double(sum) / cnt)) : 0);
    }
    return out;
}

QVariantList RowingSession::heartRateSeries() const
{
    QVariantList out;
    const int step = seriesStep();
    for (int i = 0; i < m_samples.size(); i += step) {
        int sum = 0, cnt = 0;
        for (int j = i; j < std::min<int>(m_samples.size(), i + step); ++j) {
            if (m_samples.at(j).heartRate > 0) {
                sum += m_samples.at(j).heartRate;
                ++cnt;
            }
        }
        out.append(cnt > 0 ? int(std::lround(double(sum) / cnt)) : 0);
    }
    return out;
}

QVariantList RowingSession::paceSeries() const
{
    QVariantList out;
    const int step = seriesStep();
    for (int i = 0; i < m_distanceData.size(); i += step) {
        // pace over a trailing window of at least kPaceWindowSec seconds: the
        // monitor reports distance in steps, single seconds are too noisy
        const int end = std::min<int>(m_distanceData.size(), i + step);
        const int begin = std::max(0, end - std::max(step, kPaceWindowSec));
        const double d0 = begin > 0 ? m_distanceData.at(begin - 1) : 0.0;
        const double dd = m_distanceData.at(end - 1) - d0;
        out.append(dd > 0.5 ? 500.0 * (end - begin) / dd : 0.0);
    }
    return out;
}

QVariantList RowingSession::distanceSeries() const
{
    QVariantList out;
    const int step = seriesStep();
    for (int i = 0; i < m_distanceData.size(); i += step)
        out.append(m_distanceData.at(std::min<int>(m_distanceData.size(), i + step) - 1));
    return out;
}

int RowingSession::rowingFtp() const
{
    const Profile &p = active() || m_state == State::Finished ? m_rideProfile : m_profiles->current();
    return p.ftpFor(sport::rowing());
}

int RowingSession::currentIntervalIndex() const
{
    return m_mode == Mode::Plan ? m_engine.currentIndex() : -1;
}

QString RowingSession::currentIntervalName() const
{
    const int idx = currentIntervalIndex();
    if (idx < 0 || idx >= m_engine.workout().intervals.size())
        return QString();
    return m_engine.workout().intervals.at(idx).name;
}

int RowingSession::targetWatts() const
{
    return m_mode == Mode::Plan && m_state != State::Finished ? m_engine.targetWatts() : 0;
}

int RowingSession::nextTargetWatts() const
{
    return m_mode == Mode::Plan && m_state != State::Finished ? m_engine.nextTargetWatts() : 0;
}

int RowingSession::intervalRemainingSec() const
{
    return m_mode == Mode::Plan && m_state != State::Finished ? m_engine.intervalRemainingSec() : 0;
}

int RowingSession::intervalAvgWatts() const
{
    return m_intervalSec > 0 ? int(std::lround(m_intervalPowerSum / m_intervalSec)) : 0;
}

int RowingSession::compliance() const
{
    return m_targetSec > 0 ? int(std::lround(100.0 * m_inRangeSec / m_targetSec)) : 0;
}

double RowingSession::recordPace500() const
{
    return m_hasGhost && m_targetDistance > 0 ? RowingRecords::pace500(m_targetDistance, m_recordTime) : 0.0;
}

double RowingSession::ghostDistanceM() const
{
    if (!m_hasGhost || m_state == State::Idle || m_state == State::Ready)
        return 0.0;
    return std::min<double>(m_targetDistance, RowingRecords::ghostDistanceAt(m_ghostTrace, elapsedSeconds()));
}

double RowingSession::ghostGapMeters() const
{
    if (!m_hasGhost || m_state == State::Idle || m_state == State::Ready)
        return 0.0;
    return m_distance - ghostDistanceM();
}

double RowingSession::ghostGapSeconds() const
{
    if (!m_hasGhost || m_state == State::Idle || m_state == State::Ready || m_distance <= 0)
        return 0.0;
    if (m_distance >= m_targetDistance)
        return m_recordTime - elapsedSeconds();
    return traceTimeAt(m_ghostTrace, m_distance) - elapsedSeconds();
}

double RowingSession::traceTimeAt(const QVector<TracePoint> &trace, double d)
{
    if (trace.isEmpty() || d <= 0)
        return 0.0;
    TracePoint prev;
    for (const TracePoint &p : trace) {
        if (p.d >= d) {
            if (p.d - prev.d <= 0)
                return p.t;
            return prev.t + (d - prev.d) / (p.d - prev.d) * (p.t - prev.t);
        }
        prev = p;
    }
    return trace.constLast().t;
}

// --- setup ------------------------------------------------------------------

RowingSession::Raw RowingSession::raw() const
{
    Raw r;
    if (m_rower) {
        r.distance = m_rower->distanceMeters();
        r.strokes = m_rower->strokeCount();
        r.calories = m_rower->calories();
    }
    return r;
}

void RowingSession::captureReady()
{
    m_ready = raw();
    // Only values the monitor actually reported are a usable baseline.
    m_readyValid = m_rower && m_rower->hasData();
}

void RowingSession::rebase()
{
    const Raw r = raw();
    m_offset.distance = r.distance - m_distance;
    m_offset.strokes = r.strokes - m_strokes;
    m_offset.calories = r.calories - m_calories;
    m_rebasePending = false;
}

void RowingSession::rebaseOrDefer()
{
    if (m_rower && m_rower->hasData())
        rebase();
    else
        m_rebasePending = true;   // done by the first measurement of the device
}

void RowingSession::setState(State s)
{
    if (m_state == s)
        return;
    m_state = s;
    emit stateChanged();
}

void RowingSession::clearRecording()
{
    m_samples.clear();
    m_powerData.clear();
    m_distanceData.clear();
    m_trace.clear();
    m_recordedSec = 0;
    m_powerSum = 0;
    m_maxWatts = 0;
    m_spmSum = m_hrSum = 0;
    m_spmSec = m_hrSec = 0;
    m_activeMs = 0;
    m_distance = 0;
    m_strokes = 0;
    m_calories = 0;
    m_prevT = m_prevD = 0;
    m_rebasePending = false;
    m_pausedDuringPiece = false;
    m_targetSec = m_inRangeSec = 0;
    m_intervalPowerSum = 0;
    m_intervalSec = 0;
    m_lastIndex = -2;
}

void RowingSession::clearResults()
{
    m_completed = false;
    m_newRecord = false;
    m_previousRecord = 0;
    m_finishTime = 0;
    m_lastRideId = -1;
}

void RowingSession::prepare(Mode mode)
{
    m_timer.stop();
    releaseTarget();
    m_mode = mode;
    clearRecording();
    clearResults();
    m_targetDistance = 0;
    m_planWorkoutId = -1;
    m_intervals.clear();
    m_hasGhost = false;
    m_recordTime = 0;
    m_ghostTrace.clear();
    m_engine.loadFreeRide();
    captureReady();
    m_state = mode == Mode::None ? State::Idle : State::Ready;
}

static void emitAll(RowingSession *s)
{
    emit s->setupChanged();
    emit s->stateChanged();
    emit s->intervalChanged();
    emit s->metricsChanged();
    emit s->seriesChanged();
    emit s->resultChanged();
}

QVariantList RowingSession::rowingWorkouts() const
{
    QVector<Workout> list;
    for (const Workout &w : m_db->workouts()) {
        if (w.isRowing() && !w.intervals.isEmpty() && w.totalDurationSec() > 0)
            list.append(w);
    }
    std::sort(list.begin(), list.end(), [](const Workout &a, const Workout &b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    const int ftp = m_profiles->current().ftpFor(sport::rowing());
    QVariantList out;
    for (const Workout &w : list) {
        QVariantMap m;
        m.insert(QStringLiteral("workoutId"), w.id);
        m.insert(QStringLiteral("name"), w.name);
        m.insert(QStringLiteral("totalSec"), w.totalDurationSec());
        m.insert(QStringLiteral("summary"), w.summary(ftp));
        out.append(m);
    }
    return out;
}

bool RowingSession::setupSolo(int distanceM)
{
    if (active() || distanceM < 0)
        return false;
    prepare(Mode::Solo);
    m_targetDistance = distanceM;
    emitAll(this);
    return true;
}

bool RowingSession::setupGhost(int distanceM)
{
    if (active() || distanceM <= 0)
        return false;
    prepare(Mode::Ghost);
    m_targetDistance = distanceM;
    reloadGhost();
    emitAll(this);
    return true;
}

bool RowingSession::setupPlan(qint64 workoutId)
{
    if (active())
        return false;
    const auto w = m_db->workout(workoutId);
    if (!w || !w->isRowing() || w->intervals.isEmpty() || w->totalDurationSec() <= 0)
        return false;
    prepare(Mode::Plan);
    m_planWorkoutId = workoutId;
    reloadPlan();
    emitAll(this);
    return true;
}

void RowingSession::reloadGhost()
{
    m_hasGhost = false;
    m_recordTime = 0;
    m_ghostTrace.clear();
    if (!m_records || m_targetDistance <= 0)
        return;
    const auto rec = m_records->best(m_targetDistance);
    if (!rec || rec->timeSec <= 0)
        return;
    m_recordTime = rec->timeSec;
    m_ghostTrace = rec->trace;
    if (m_ghostTrace.isEmpty())   // record without trace: even pace
        m_ghostTrace = { { 0, 0 }, { rec->timeSec, double(m_targetDistance) } };
    m_hasGhost = true;
}

void RowingSession::reloadPlan()
{
    auto w = m_db->workout(m_planWorkoutId);
    if (!w)
        return;
    const Profile &p = m_profiles->current();
    m_engine.load(*w, p.ftpFor(w->sport));
    m_lastIndex = -2;
    m_intervals.clear();
    int start = 0;
    for (const Interval &i : std::as_const(w->intervals)) {
        QVariantMap m;
        m.insert(QStringLiteral("name"), i.name);
        m.insert(QStringLiteral("startSec"), start);
        m.insert(QStringLiteral("durationSec"), i.durationSec);
        m.insert(QStringLiteral("watts"), watt::targetWatts(i, 0.0, w->sport, p.ftp, p.rowingFtp));
        m.insert(QStringLiteral("endWatts"), watt::targetWatts(i, double(i.durationSec), w->sport, p.ftp, p.rowingFtp));
        m.insert(QStringLiteral("percent"), int(std::lround(i.averagePercent() * 100.0)));
        m.insert(QStringLiteral("ramp"), i.isRamp());
        m_intervals.append(m);
        start += i.durationSec;
    }
}

// --- control ------------------------------------------------------------------

bool RowingSession::start()
{
    if (active())
        return false;
    if (m_training && m_training->active()) {
        emit startRefused(QStringLiteral("A cycling session is running. Stop it before rowing."));
        return false;
    }
    for (auto it = m_startBlockers.cbegin(); it != m_startBlockers.cend(); ++it) {
        if (it->first && it->first()) {
            emit startRefused(it->second);
            return false;
        }
    }
    if (m_mode == Mode::Plan && !m_db->workout(m_planWorkoutId)) {
        // Checked before reset(): the results of a finished row stay visible.
        emit startRefused(QStringLiteral("This rowing workout no longer exists. Choose another workout."));
        return false;
    }
    if (m_state == State::Finished)
        reset();
    if (m_mode == Mode::None)
        setupSolo(0);
    if (m_mode == Mode::Plan && m_engine.isFreeRide()) {
        emit startRefused(QStringLiteral("This rowing workout cannot be started."));
        return false;
    }
    if (m_training && m_training->waiting())
        m_training->stop();   // only armed: nothing ridden yet

    m_rideProfile = m_profiles->current();
    clearRecording();
    clearResults();
    // Values from the last update before the start; for a start triggered by
    // the first stroke this keeps the distance of that stroke. Without such a
    // reported baseline (e.g. the very first packet of the monitor starts the
    // row) the current values are used, and without any data yet the
    // baseline is taken from the first measurement.
    if (m_rower && m_readyValid) {
        m_offset = m_ready;
    } else if (m_rower && m_rower->hasData()) {
        m_offset = raw();
    } else {
        m_offset = Raw {};
        m_rebasePending = true;
    }
    m_trace.append({ 0, 0 });
    m_startedAt = QDateTime::currentDateTime();
    if (m_mode == Mode::Plan) {
        m_engine.setFtp(m_rideProfile.ftpFor(sport::rowing()));
        m_engine.reset();
        m_engine.start();
        m_lastIndex = m_engine.currentIndex();
        if (m_engine.workout().id >= 0)
            m_db->incrementWorkoutUsage(m_engine.workout().id);
    }
    m_clock.start();
    if (!m_manualClock)
        m_timer.start();
    m_state = State::Running;
    applyTarget(true);
    emit stateChanged();
    emit started();
    emit intervalChanged();
    emit metricsChanged();
    emit seriesChanged();
    emit resultChanged();
    return true;
}

void RowingSession::pause()
{
    if (!running())
        return;
    if (!m_manualClock)
        process(int(m_clock.restart()));
    if (!running())
        return;   // finished during the last slice
    m_timer.stop();
    releaseTarget();
    if (m_mode == Mode::Plan)
        m_engine.pause();
    m_pausedDuringPiece = true;
    setState(State::Paused);
    emit metricsChanged();
}

void RowingSession::resume()
{
    if (!paused())
        return;
    // Metres rowed while paused do not count.
    if (!m_rebasePending)
        rebaseOrDefer();
    m_prevT = m_activeMs / 1000.0;
    m_prevD = m_distance;
    if (m_mode == Mode::Plan)
        m_engine.resume();
    m_clock.restart();
    if (!m_manualClock)
        m_timer.start();
    setState(State::Running);
    applyTarget(true);
    emit metricsChanged();
}

void RowingSession::togglePause()
{
    if (running())
        pause();
    else if (paused())
        resume();
    else
        start();
}

qint64 RowingSession::finish(bool save)
{
    if (!active())
        return m_state == State::Finished ? m_lastRideId : -1;
    if (running() && !m_manualClock)
        process(int(m_clock.restart()));
    if (!active())
        return m_lastRideId;   // completed (and saved) during the last slice
    return finishInternal(save, false, m_activeMs / 1000.0);
}

void RowingSession::reset()
{
    const Mode mode = m_mode;
    const int target = m_targetDistance;
    const qint64 planId = m_planWorkoutId;
    prepare(mode);
    m_targetDistance = target;
    m_planWorkoutId = planId;
    if (mode == Mode::Plan)
        reloadPlan();
    else if (mode == Mode::Ghost)
        reloadGhost();
    emitAll(this);
}

void RowingSession::clear()
{
    prepare(Mode::None);
    emitAll(this);
}

// --- clock / recording ----------------------------------------------------------

void RowingSession::advanceClock(int ms)
{
    process(ms);
}

void RowingSession::onTimer()
{
    process(int(m_clock.restart()));
}

void RowingSession::process(int ms)
{
    if (m_hub)
        m_hub->expireStale();
    if (!running() || ms <= 0)
        return;
    m_activeMs += ms;
    recordUpTo(int(m_activeMs / 1000));
    if (m_mode == Mode::Plan) {
        m_engine.advance(ms);
        const int idx = m_engine.currentIndex();
        if (idx != m_lastIndex) {
            m_lastIndex = idx;
            m_intervalPowerSum = 0;
            m_intervalSec = 0;
            emit intervalChanged();
        }
        if (m_engine.state() == WorkoutEngine::State::Finished) {
            finishInternal(true, true, m_engine.totalSec());
            return;
        }
        applyTarget();
    }
    emit metricsChanged();
}

void RowingSession::recordUpTo(int second)
{
    bool added = false;
    while (m_recordedSec < second) {
        Sample s;
        s.t = m_recordedSec;
        s.power = watts();
        s.cadence = spm();
        s.heartRate = heartRate();
        s.targetPower = m_mode == Mode::Plan ? m_engine.targetWatts() : 0;
        m_samples.append(s);
        m_powerData.append(s.power);
        m_distanceData.append(m_distance);
        m_powerSum += s.power;
        m_maxWatts = std::max(m_maxWatts, s.power);
        if (s.cadence > 0) {
            m_spmSum += s.cadence;
            ++m_spmSec;
        }
        if (s.heartRate > 0) {
            m_hrSum += s.heartRate;
            ++m_hrSec;
        }
        if (m_mode == Mode::Plan && s.targetPower > 0) {
            ++m_targetSec;
            if (std::abs(s.power - s.targetPower) <= 0.1 * s.targetPower)
                ++m_inRangeSec;
            m_intervalPowerSum += s.power;
            ++m_intervalSec;
        }
        ++m_recordedSec;
        added = true;
    }
    if (added)
        emit seriesChanged();
}

// --- rower events -------------------------------------------------------------

void RowingSession::onMeasurement()
{
    if (!m_rower)
        return;
    if (!active()) {
        captureReady();
        emit metricsChanged();
        return;
    }
    if (paused()) {
        emit metricsChanged();   // rebased on resume
        return;
    }
    if (m_rebasePending)
        rebase();   // first values of a new monitor / of the row: continue from here
    const Raw r = raw();
    // Each counter is bridged on its own: after a reset on the monitor the
    // stroke count and calories may restart later than the distance.
    double d = r.distance - m_offset.distance;
    if (d + 1.0 < m_distance) {
        m_offset.distance = r.distance - m_distance;
        d = m_distance;
    }
    if (r.strokes - m_offset.strokes < m_strokes)
        m_offset.strokes = r.strokes - m_strokes;
    m_strokes = r.strokes - m_offset.strokes;
    if (r.calories - m_offset.calories < m_calories)
        m_offset.calories = r.calories - m_calories;
    m_calories = r.calories - m_offset.calories;
    const double now = nowSec();
    // After a gap without movement the boat stood still until (at most) a
    // second before this update: interpolate from there, not over the gap.
    if (d > m_prevD)
        m_prevT = std::max(m_prevT, now - 1.0);
    if (m_mode != Mode::Plan && m_targetDistance > 0 && d >= m_targetDistance) {
        // Finish time interpolated between the last two updates.
        double t = now;
        if (d > m_prevD && now > m_prevT && m_prevD < m_targetDistance)
            t = m_prevT + (m_targetDistance - m_prevD) / (d - m_prevD) * (now - m_prevT);
        t = std::clamp(t, m_prevT, now);
        extendTrace(t, m_targetDistance);
        // Seconds up to the finish still see the distance before it (never
        // more than the target).
        recordUpTo(int(std::floor(t)));
        completeDistance(t);
        return;
    }
    // Only updates that move the boat are interpolation points: packets
    // without new distance data (e.g. stroke notifications) would otherwise
    // pin the last known distance to a later time.
    if (d > m_prevD) {
        extendTrace(now, d);
        m_distance = std::max(m_distance, d);
        m_prevT = now;
        m_prevD = m_distance;
    }
    emit metricsChanged();
}

void RowingSession::extendTrace(double now, double d)
{
    // One point per whole second, interpolated between the last update
    // (m_prevT, m_prevD) and this one.
    const double last = m_trace.isEmpty() ? 0.0 : m_trace.constLast().t;
    for (int s = int(std::floor(last)) + 1; s <= int(std::floor(now)); ++s) {
        double ds = d;
        if (now > m_prevT && s > m_prevT)
            ds = m_prevD + (d - m_prevD) * (s - m_prevT) / (now - m_prevT);
        else if (s <= m_prevT)
            ds = m_prevD;
        if (!m_trace.isEmpty())
            ds = std::max(ds, m_trace.constLast().d);
        m_trace.append({ double(s), ds });
    }
}

void RowingSession::completeDistance(double finishTime)
{
    m_distance = m_targetDistance;
    if (!m_trace.isEmpty() && m_trace.constLast().t >= finishTime)
        m_trace.last() = { m_trace.constLast().t, double(m_targetDistance) };
    else
        m_trace.append({ finishTime, double(m_targetDistance) });
    finishInternal(true, true, finishTime);
}

void RowingSession::onStroke()
{
    maybeAutoStart();
}

void RowingSession::onRowingActive(bool on)
{
    if (on)
        maybeAutoStart();
}

void RowingSession::maybeAutoStart()
{
    if (m_autoStart && m_state == State::Ready)
        start();
}

void RowingSession::onWorkoutReset()
{
    // New workout on the monitor: counters restart from 0. Counters that
    // restart later are bridged in onMeasurement().
    if (active()) {
        if (!m_rebasePending)
            rebaseOrDefer();
    } else {
        captureReady();
    }
}

// --- targets --------------------------------------------------------------------

void RowingSession::applyTarget(bool force)
{
    if (m_mode != Mode::Plan || !running())
        return;
    const int target = m_engine.targetWatts();
    if (force || target != m_lastSentTarget) {
        if (m_rower)
            m_rower->requestTargetPower(target);
        m_lastSentTarget = target;
    }
}

void RowingSession::releaseTarget()
{
    if (m_lastSentTarget > 0 && m_rower)
        m_rower->requestTargetPower(0);
    m_lastSentTarget = -1;
}

// --- finish -------------------------------------------------------------------

qint64 RowingSession::finishInternal(bool save, bool completed, double finishTime)
{
    m_timer.stop();
    releaseTarget();
    if (m_mode == Mode::Plan)
        m_engine.finish();
    m_activeMs = qint64(std::llround(finishTime * 1000.0));
    m_finishTime = finishTime;
    m_completed = completed;
    m_state = State::Finished;

    const qint64 rideId = save ? saveRide() : -1;

    if (completed && m_mode != Mode::Plan && m_targetDistance > 0 && m_records) {
        const qint64 profileId = m_rideProfile.id;
        const auto prev = m_records->best(m_targetDistance, profileId);
        m_previousRecord = prev ? prev->timeSec : 0.0;
        // No personal best from a piece with a pause in it, or with an
        // impossible pace (faster than 1:00 /500 m: bad monitor data).
        if (!m_pausedDuringPiece && RowingRecords::plausibleTime(m_targetDistance, finishTime)) {
            m_newRecord = m_records->saveIfBest(m_targetDistance, finishTime, m_trace, rideId, profileId);
            if (m_newRecord)
                emit newRecordSet(m_targetDistance, finishTime, m_previousRecord);
        }
    }
    // Only a completed plan workout ticks off its calendar entry.
    if (m_mode == Mode::Plan && completed && rideId >= 0 && m_planner)
        m_planner->completeForRide(m_engine.workout().id, m_startedAt.date(), rideId, m_rideProfile.id);

    emit resultChanged();
    emit stateChanged();
    emit intervalChanged();
    emit metricsChanged();
    emit finished();
    return rideId;
}

qint64 RowingSession::saveRide()
{
    if (m_samples.size() < m_minSaveSec)
        return -1;
    const Profile &p = m_rideProfile;
    RideRecord r;
    r.profileId = p.id;
    r.workoutId = m_mode == Mode::Plan ? m_engine.workout().id : -1;
    r.workoutName = workoutName();
    r.startedAt = m_startedAt;
    r.blocks = m_mode == Mode::Plan ? int(m_engine.workout().intervals.size()) : 0;
    r.ergMode = false;
    r.sport = sport::rowing();
    r.ftp = p.ftpFor(r.sport);
    analysis::applyStats(r, analysis::analyze(m_samples, r.ftp, p.zones));
    r.distanceM = m_distance;
    m_lastRideId = m_db->saveRide(r, m_samples);
    if (m_lastRideId >= 0)
        emit rideSaved(m_lastRideId);
    return m_lastRideId;
}

} // namespace watt
