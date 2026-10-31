#include "RunSession.h"

#include "Database.h"
#include "DiagLog.h"
#include "ProfileManager.h"
#include "RlvLibrary.h"
#include "RunLibrary.h"
#include "RunRoutes.h"
#include "SensorDevices.h"
#include "SensorHub.h"
#include "Workout.h"

#include <QFileInfo>
#include <algorithm>
#include <cmath>

namespace watt {

namespace {
constexpr int kTickMs = 250;
constexpr int kProfileSamples = 500;
const QString kSettingGpx = QStringLiteral("run/gpxPath");
const QString kSettingGpxOn = QStringLiteral("run/gpxEnabled");
} // namespace

RunSession::RunSession(Database *db, ProfileManager *profiles, SensorHub *hub, RunLibrary *library, QObject *parent)
    : QObject(parent)
    , m_db(db)
    , m_profiles(profiles)
    , m_hub(hub)
    , m_library(library)
    , m_standard(runroutes::standardRoute())
{
    m_timer.setInterval(kTickMs);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &RunSession::onTimer);
    if (m_hub)
        connect(m_hub, &SensorHub::heartRateChanged, this, [this] {
            if (!running())
                emit metricsChanged();
        });
    if (m_library) {
        connect(m_library, &RunLibrary::antChanged, this, [this] {
            emit setupChanged();
            emit metricsChanged();
        });
        connect(m_library, &RunLibrary::workoutsChanged, this, [this] {
            // an edited workout that is attached (not started): take the new version
            if (m_workout && !active()) {
                if (const auto w = m_library->workout(m_workout->id))
                    m_workout = *w;
                else
                    m_workout.reset();
                emit setupChanged();
                emit intervalChanged();
            }
        });
    }
    // the last GPX route (it is loaded again; GPX on only if it loads)
    const QString last = m_db->setting(kSettingGpx).toString();
    if (!last.isEmpty() && QFileInfo::exists(last)) {
        RlvRoute r;
        QString error;
        if (r.load(last, &error))
            m_gpxRoute = std::move(r);
    }
    m_gpxEnabled = m_gpxRoute.isValid() && m_db->setting(kSettingGpxOn, false).toBool();
    rebuildRouteData();
}

RunSession::~RunSession()
{
    for (const auto &c : std::as_const(m_treadmillConnections))
        disconnect(c);
}

// --- devices / guards ----------------------------------------------------------

void RunSession::setTreadmill(TreadmillDevice *t)
{
    for (const auto &c : std::as_const(m_treadmillConnections))
        disconnect(c);
    m_treadmillConnections.clear();
    m_treadmill = t;
    m_lastSuggestSpeed = -2;
    if (t) {
        m_treadmillConnections << connect(t, &TreadmillDevice::stateChanged, this, &RunSession::treadmillChanged);
        m_treadmillConnections << connect(t, &TreadmillDevice::measurementChanged, this, [this] {
            if (!running())
                emit metricsChanged();   // live values before the start
        });
        m_treadmillConnections << connect(t, &QObject::destroyed, this, [this] { emit treadmillChanged(); });
    }
    suggestToTreadmill();
    emit treadmillChanged();
    emit metricsChanged();
}

TreadmillDevice *RunSession::treadmill() const
{
    return m_treadmill.data();
}

bool RunSession::treadmillConnected() const
{
    return m_treadmill && m_treadmill->state() == LinkState::Connected;
}

QString RunSession::treadmillName() const
{
    return m_treadmill ? m_treadmill->name() : QString();
}

bool RunSession::simulatedTreadmill() const
{
    return m_treadmill && m_treadmill->isSimulated();
}

void RunSession::setStartBlocker(const QString &key, std::function<bool()> blocker, const QString &reason)
{
    if (blocker)
        m_startBlockers.insert(key, qMakePair(std::move(blocker), reason));
    else
        m_startBlockers.remove(key);
}

void RunSession::setAbandonCanceller(const QString &key, std::function<void()> cancel)
{
    if (cancel)
        m_abandonCancellers.insert(key, std::move(cancel));
    else
        m_abandonCancellers.remove(key);
}

void RunSession::setManualClock(bool manual)
{
    m_manualClock = manual;
    if (manual)
        m_timer.stop();
}

void RunSession::advanceClock(int ms)
{
    // in steps of at most one tick, like the real timer
    while (ms > 0) {
        const int step = std::min(ms, kTickMs);
        process(step);
        ms -= step;
    }
}

void RunSession::setError(const QString &e)
{
    if (m_lastError == e)
        return;
    m_lastError = e;
    emit lastErrorChanged();
}

QString RunSession::state() const
{
    switch (m_state) {
    case State::Ready: return QStringLiteral("ready");
    case State::Running: return QStringLiteral("running");
    case State::Paused: return QStringLiteral("paused");
    case State::Finished: return QStringLiteral("finished");
    }
    return QStringLiteral("ready");
}

void RunSession::setState(State s)
{
    if (m_state == s)
        return;
    m_state = s;
    if (s == State::Running && !m_manualClock) {
        m_clock.restart();
        m_timer.start();
    } else if (s != State::Running) {
        m_timer.stop();
    }
    emit stateChanged();
}

// --- route ------------------------------------------------------------------------

void RunSession::rebuildRouteData()
{
    const RlvRoute &r = route();
    m_loop = runroutes::isLoop(r);
    m_profile.clear();
    for (const QPointF &p : r.profile(kProfileSamples))
        m_profile.append(QVariantMap { { QStringLiteral("d"), std::round(p.x() * 10.0) / 10.0 },
                                       { QStringLiteral("ele"), std::round(p.y() * 10.0) / 10.0 } });
    m_position = std::clamp(m_position, 0.0, r.totalDistance());
    emit routeChanged();
    emit positionChanged();
}

QString RunSession::routeName() const
{
    if (m_gpxEnabled && m_gpxRoute.isValid())
        return m_gpxRoute.title().isEmpty() ? QFileInfo(m_gpxRoute.sourcePath()).completeBaseName() : m_gpxRoute.title();
    return runroutes::standardName();
}

QString RunSession::routePath() const
{
    return m_gpxEnabled && m_gpxRoute.isValid() ? m_gpxRoute.sourcePath() : runroutes::standardPath();
}

void RunSession::setGpxEnabled(bool on)
{
    if (active()) {
        setError(QStringLiteral("Finish the run before changing the route"));
        emit routeChanged();   // (the switch in the page snaps back)
        return;
    }
    if (on && !m_gpxRoute.isValid()) {
        setError(QStringLiteral("Load a GPX file first"));
        emit routeChanged();
        return;
    }
    if (on == m_gpxEnabled)
        return;
    m_gpxEnabled = on;
    m_db->setSetting(kSettingGpxOn, on);
    if (m_state == State::Finished)
        reset();
    m_position = 0;
    m_lap = 1;
    setError(QString());
    rebuildRouteData();
}

bool RunSession::selectGpx(const QString &pathOrUrl)
{
    if (active()) {
        setError(QStringLiteral("Finish the run before choosing another route"));
        return false;
    }
    const QString path = RlvLibrary::toLocalPath(pathOrUrl);
    if (QFileInfo(path).suffix().compare(QLatin1String("gpx"), Qt::CaseInsensitive) != 0) {
        setError(QStringLiteral("Choose a .gpx file"));
        return false;
    }
    RlvRoute r;
    QString error;
    if (!r.load(path, &error) || !r.isValid()) {
        setError(error.isEmpty() ? QStringLiteral("The GPX file has no usable track") : error);
        return false;
    }
    m_gpxRoute = std::move(r);
    m_db->setSetting(kSettingGpx, m_gpxRoute.sourcePath());
    m_gpxEnabled = true;
    m_db->setSetting(kSettingGpxOn, true);
    if (m_state == State::Finished)
        reset();
    m_position = 0;
    m_lap = 1;
    setError(QString());
    rebuildRouteData();
    return true;
}

double RunSession::routeGradePercent() const
{
    return hasRoute() ? route().gradeAheadAtDistance(m_position) : 0.0;
}

// --- live values ------------------------------------------------------------------

double RunSession::speedKmh() const
{
    return treadmillConnected() ? m_treadmill->speedKmh() : 0.0;
}

QString RunSession::pace() const
{
    return running::paceText(speedKmh());
}

double RunSession::inclinePercent() const
{
    return treadmillConnected() && m_treadmill->inclineKnown() ? m_treadmill->inclinePercent() : 0.0;
}

bool RunSession::inclineKnown() const
{
    return treadmillConnected() && m_treadmill->inclineKnown();
}

int RunSession::heartRate() const
{
    return m_hub ? m_hub->heartRate() : 0;
}

int RunSession::cadence() const
{
    return running::estimatedCadence(speedKmh());
}

double RunSession::avgSpeedKmh() const
{
    return m_activeMs > 0 ? m_runDistance / (m_activeMs / 1000.0) * 3.6 : 0.0;
}

QString RunSession::avgPace() const
{
    return running::paceText(avgSpeedKmh());
}

double RunSession::antSpeedKmh() const
{
    if (active() || m_state == State::Finished)
        return m_antAtStart > 0 ? m_antAtStart : (m_library ? m_library->antSpeedKmh() : running::kDefaultAntKmh);
    return m_library ? m_library->antSpeedKmh() : running::kDefaultAntKmh;
}

int RunSession::speedZone() const
{
    const double ant = antSpeedKmh();
    return ant > 0 ? runzones::zoneForFraction(speedKmh() / ant) : 0;
}

double RunSession::rtss() const
{
    const int n = int(m_secSpeed.size());
    const double ant = antSpeedKmh();
    if (n == 0 || ant <= 0)
        return 0.0;
    // normalized graded speed: 30 s rolling mean, 4th-power mean (like NP)
    double window = 0, sum4 = 0;
    int count = 0;
    for (int i = 0; i < n; ++i) {
        window += running::gradeAdjustedKmh(m_secSpeed.at(i), m_secIncline.at(i));
        if (i >= 30)
            window -= running::gradeAdjustedKmh(m_secSpeed.at(i - 30), m_secIncline.at(i - 30));
        const double mean = window / std::min(i + 1, 30);
        sum4 += std::pow(mean, 4.0);
        ++count;
    }
    const double ngs = std::pow(sum4 / count, 0.25);
    const double ifactor = ngs / ant;
    return n / 3600.0 * ifactor * ifactor * 100.0;
}

int RunSession::seriesStep() const
{
    const int n = int(m_secSpeed.size());
    return std::max(5, int(std::ceil(n / double(kSeriesMaxPoints) / 5.0)) * 5);
}

namespace {
template<typename T>
QVariantList bucketMeans(const QVector<T> &values, int step, bool skipZero)
{
    QVariantList out;
    for (int i = 0; i < values.size(); i += step) {
        double sum = 0;
        int n = 0;
        for (int k = i; k < std::min(int(values.size()), i + step); ++k) {
            if (skipZero && values.at(k) <= 0)
                continue;
            sum += values.at(k);
            ++n;
        }
        out.append(n > 0 ? std::round(sum / n * 10.0) / 10.0 : 0.0);
    }
    return out;
}
} // namespace

QVariantList RunSession::speedSeries() const
{
    return bucketMeans(m_secSpeed, seriesStep(), false);
}

QVariantList RunSession::heartRateSeries() const
{
    return bucketMeans(m_secHr, seriesStep(), true);
}

QVariantList RunSession::targetSeries() const
{
    return bucketMeans(m_secTarget, seriesStep(), false);
}

// --- workout ----------------------------------------------------------------------

QVariantList RunSession::workoutSegments() const
{
    return m_workout ? m_workout->segments(antSpeedKmh()) : QVariantList();
}

int RunSession::workoutRemainingSec() const
{
    if (!m_workout)
        return 0;
    return std::max(0, m_workout->totalDurationSec() - int(workoutSecond()));
}

bool RunSession::workoutFinished() const
{
    return m_workout && workoutSecond() >= m_workout->totalDurationSec();
}

int RunSession::currentStepIndex() const
{
    return m_workout ? m_workout->stepIndexAt(workoutSecond()) : -1;
}

QString RunSession::currentStepName() const
{
    const int i = currentStepIndex();
    return i >= 0 ? m_workout->steps.at(i).name : QString();
}

int RunSession::stepRemainingSec() const
{
    const int i = currentStepIndex();
    if (i < 0)
        return 0;
    const double end = m_workout->stepStartSec(i) + m_workout->steps.at(i).durationSec;
    return std::max(0, int(std::ceil(end - workoutSecond())));
}

double RunSession::targetSpeedKmh() const
{
    return m_workout ? m_workout->targetSpeedAt(workoutSecond(), antSpeedKmh()) : 0.0;
}

QString RunSession::targetPace() const
{
    return running::paceText(targetSpeedKmh());
}

int RunSession::targetPercent() const
{
    const double ant = antSpeedKmh();
    return ant > 0 ? int(std::lround(targetSpeedKmh() / ant * 100.0)) : 0;
}

double RunSession::targetInclinePercent() const
{
    return m_workout ? m_workout->inclineAt(workoutSecond()) : 0.0;
}

QString RunSession::nextStepText() const
{
    if (!m_workout)
        return QString();
    const int i = currentStepIndex();
    const int next = i < 0 ? (workoutSecond() <= 0 ? 0 : -1) : i + 1;
    if (next < 0 || next >= m_workout->steps.size())
        return i >= 0 ? QStringLiteral("Last step") : QString();
    const RunStep &s = m_workout->steps.at(next);
    return QStringLiteral("Next: %1 · %2 km/h · %3 %").arg(s.name).arg(s.speedAt(0, antSpeedKmh()), 0, 'f', 1)
        .arg(s.inclinePercent, 0, 'f', 1);
}

bool RunSession::antTestBlockActive() const
{
    return antTest() && currentStepIndex() == m_workout->antTestStepIndex();
}

bool RunSession::attachWorkout(qint64 id)
{
    if (active()) {
        setError(QStringLiteral("Finish the run before choosing another workout"));
        return false;
    }
    if (id < 0) {
        detachWorkout();
        return true;
    }
    const auto w = m_library ? m_library->workout(id) : std::nullopt;
    if (!w || w->steps.isEmpty() || w->totalDurationSec() <= 0) {
        setError(QStringLiteral("This workout cannot be run"));
        return false;
    }
    if (m_state == State::Finished)
        reset();
    m_workout = *w;
    m_lastStep = -2;
    m_antEvaluated = false;
    setError(QString());
    suggestToTreadmill();
    emit setupChanged();
    emit intervalChanged();
    emit metricsChanged();
    return true;
}

void RunSession::detachWorkout()
{
    if (active() || !m_workout)
        return;
    m_workout.reset();
    suggestToTreadmill();
    emit setupChanged();
    emit intervalChanged();
    emit metricsChanged();
}

bool RunSession::setupAntTest()
{
    if (!m_library || m_library->antTestWorkoutId() < 0) {
        setError(QStringLiteral("The AnT test workout is missing"));
        return false;
    }
    return attachWorkout(m_library->antTestWorkoutId());
}

void RunSession::suggestToTreadmill()
{
    if (!m_treadmill)
        return;
    // only the virtual treadmill follows this; a real treadmill is never controlled
    const bool live = m_workout && !workoutFinished();
    const double v = live ? targetSpeedKmh() : -1.0;
    const double inc = live ? targetInclinePercent() : -1.0;
    if (std::abs(v - m_lastSuggestSpeed) < 0.01 && std::abs(inc - m_lastSuggestIncline) < 0.01)
        return;
    m_lastSuggestSpeed = v;
    m_lastSuggestIncline = inc;
    m_treadmill->suggest(v, inc);
}

// --- run ----------------------------------------------------------------------------

void RunSession::clearRecording()
{
    m_activeMs = 0;
    m_samples.clear();
    m_secSpeed.clear();
    m_secIncline.clear();
    m_secTarget.clear();
    m_secHr.clear();
    m_recordedSec = 0;
    m_runDistance = 0;
    m_kcal = 0;
    m_climb = 0;
    m_hrSum = 0;
    m_hrSec = 0;
    m_maxHr = 0;
    m_accSpeed = m_accIncline = m_accMs = 0;
    m_targetSec = m_inRangeSec = 0;
    m_position = 0;
    m_lap = 1;
    m_lastStep = -2;
    m_antEvaluated = false;
    m_acceptedTestId = -1;
    m_completed = false;
}

bool RunSession::start()
{
    if (active())
        return false;
    if (!hasRoute()) {
        setError(QStringLiteral("No route"));
        emit startRefused(m_lastError);
        return false;
    }
    if (!treadmillConnected()) {
        setError(QStringLiteral("No treadmill connected"));
        emit startRefused(m_lastError);
        return false;
    }
    for (auto it = m_startBlockers.cbegin(); it != m_startBlockers.cend(); ++it) {
        if (it.value().first && it.value().first()) {
            const QString reason = it.value().second.isEmpty()
                ? QStringLiteral("Another session is in progress – finish it first") : it.value().second;
            setError(reason);
            emit startRefused(reason);
            return false;
        }
    }
    for (auto it = m_abandonCancellers.cbegin(); it != m_abandonCancellers.cend(); ++it) {
        if (it.value())
            it.value()();
    }
    if (m_state == State::Finished)
        reset();
    clearRecording();
    if (m_antResult.size()) {
        m_antResult.clear();
        emit antResultChanged();
    }
    m_startedAt = QDateTime::currentDateTime();
    m_runProfile = m_profiles->current();
    m_antAtStart = m_library ? m_library->antSpeedKmh() : running::kDefaultAntKmh;
    m_lastRideId = -1;
    setError(QString());
    setState(State::Running);
    DiagLog::write("RUN", QStringLiteral("==== treadmill run started: route \"%1\"%2, workout \"%3\", AnT %4 km/h")
                              .arg(routeName(), m_gpxEnabled ? QStringLiteral(" (GPX)") : QString(), workoutName())
                              .arg(m_antAtStart, 0, 'f', 1));
    checkStep();
    suggestToTreadmill();
    emit started();
    emit resultChanged();
    emit setupChanged();
    emit metricsChanged();
    emit positionChanged();
    emit seriesChanged();
    return true;
}

void RunSession::pause()
{
    if (m_state != State::Running)
        return;
    if (!m_manualClock)
        process(int(m_clock.restart()));
    setState(State::Paused);
}

void RunSession::resume()
{
    if (m_state != State::Paused)
        return;
    setState(State::Running);
}

void RunSession::togglePause()
{
    if (m_state == State::Running)
        pause();
    else if (m_state == State::Paused)
        resume();
    else
        start();
}

void RunSession::onTimer()
{
    if (m_state != State::Running)
        return;
    process(int(m_clock.restart()));
}

void RunSession::process(int ms)
{
    if (m_state != State::Running || ms <= 0 || m_inProcess)
        return;
    m_inProcess = true;
    struct Guard { bool &f; ~Guard() { f = false; } } guard { m_inProcess };
    ms = std::min(ms, 2000);   // a stalled event loop does not jump the run
    const double v = speedKmh();
    const double inc = inclinePercent();
    const double dt = ms / 1000.0;
    const double d = v / 3.6 * dt;
    m_activeMs += ms;
    m_runDistance += d;
    m_kcal += running::kcalPerSecond(v, inc, m_runProfile.weightKg > 0 ? m_runProfile.weightKg : 75.0) * dt;
    m_climb += d * std::max(0.0, inc) / 100.0;
    m_accSpeed += v * ms;
    m_accIncline += inc * ms;
    m_accMs += ms;

    // position on the route (laps)
    bool routeEnded = false;
    const double len = route().totalDistance();
    if (len > 0) {
        m_position += d;
        const bool workoutLive = m_workout && !workoutFinished();
        while (m_position >= len) {
            if (m_loop || workoutLive) {
                m_position -= len;
                ++m_lap;
            } else {
                m_position = len;
                routeEnded = true;
                break;
            }
        }
    }
    while (int(m_activeMs / 1000) > m_recordedSec)
        recordSecond();
    checkStep();
    suggestToTreadmill();
    emit metricsChanged();
    emit positionChanged();

    if (routeEnded) {
        DiagLog::write("RUN", QStringLiteral("route end reached after %1 m").arg(m_runDistance, 0, 'f', 0));
        m_completed = true;
        finish(true);
    } else if (m_workout && workoutFinished()) {
        DiagLog::write("RUN", QStringLiteral("workout \"%1\" complete").arg(m_workout->name));
        m_completed = true;
        finish(true);
    }
}

void RunSession::recordSecond()
{
    const double v = m_accMs > 0 ? m_accSpeed / m_accMs : speedKmh();
    const double inc = m_accMs > 0 ? m_accIncline / m_accMs : inclinePercent();
    m_accSpeed = m_accIncline = m_accMs = 0;
    const int t = m_recordedSec;
    const int hr = heartRate();
    const double target = m_workout ? m_workout->targetSpeedAt(t + 0.5, antSpeedKmh()) : 0.0;
    m_secSpeed.append(v);
    m_secIncline.append(inc);
    m_secTarget.append(target);
    m_secHr.append(hr);
    Sample s;
    s.t = t;
    s.heartRate = hr;
    s.cadence = running::estimatedCadence(v);
    s.speedMps = v / 3.6;
    m_samples.append(s);
    if (hr > 0) {
        m_hrSum += hr;
        ++m_hrSec;
        m_maxHr = std::max(m_maxHr, hr);
    }
    if (target > 0) {
        ++m_targetSec;
        if (std::abs(v - target) <= std::max(0.15, target * kComplianceFraction))
            ++m_inRangeSec;
    }
    ++m_recordedSec;
    if (m_recordedSec % 5 == 0)
        emit seriesChanged();
}

void RunSession::checkStep()
{
    if (!m_workout)
        return;
    const int i = currentStepIndex();
    if (i != m_lastStep) {
        const int prev = m_lastStep;
        m_lastStep = i;
        emit intervalChanged();
        if (prev >= 0)
            DiagLog::write("RUN", QStringLiteral("step %1/%2 \"%3\" target %4 km/h incline %5 %")
                                      .arg(i + 1).arg(m_workout->steps.size()).arg(currentStepName())
                                      .arg(targetSpeedKmh(), 0, 'f', 1).arg(targetInclinePercent(), 0, 'f', 1));
    }
    if (antTest() && !m_antEvaluated) {
        const int t = m_workout->antTestStepIndex();
        if (t >= 0 && workoutSecond() >= m_workout->stepStartSec(t) + m_workout->steps.at(t).durationSec
            && m_recordedSec >= m_workout->stepStartSec(t) + m_workout->steps.at(t).durationSec)
            evaluateAntTestBlock();
    }
}

void RunSession::evaluateAntTestBlock()
{
    m_antEvaluated = true;
    const int t = m_workout->antTestStepIndex();
    const int from = m_workout->stepStartSec(t);
    const int to = from + m_workout->steps.at(t).durationSec;
    const running::AntTestResult r = running::evaluateAntTest(m_secSpeed, m_secIncline, m_secHr, from, to);
    const double previous = m_antAtStart;
    QVariantMap m;
    m.insert(QStringLiteral("valid"), r.valid);
    m.insert(QStringLiteral("speed"), std::round(r.speedKmh * 10.0) / 10.0);
    m.insert(QStringLiteral("pace"), running::paceText(r.speedKmh));
    m.insert(QStringLiteral("heartRate"), r.heartRate);
    m.insert(QStringLiteral("incline"), std::round(r.inclinePercent * 10.0) / 10.0);
    m.insert(QStringLiteral("previousSpeed"), previous);
    m.insert(QStringLiteral("previousPace"), running::paceText(previous));
    m.insert(QStringLiteral("previousHeartRate"), m_library ? m_library->antHeartRate() : 0);
    m.insert(QStringLiteral("diff"), std::round((r.speedKmh - previous) * 10.0) / 10.0);
    m.insert(QStringLiteral("message"), r.message);
    m.insert(QStringLiteral("accepted"), false);
    m_antResult = m;
    DiagLog::write("RUN", QStringLiteral("AnT test evaluated: %1 – %2 km/h, HR %3 (%4)")
                              .arg(r.valid ? QStringLiteral("valid") : QStringLiteral("not valid"))
                              .arg(r.speedKmh, 0, 'f', 2).arg(r.heartRate).arg(r.message));
    emit antResultChanged();
    emit antTestCompleted(m);
}

bool RunSession::acceptAntResult()
{
    if (m_antResult.isEmpty() || !m_antResult.value(QStringLiteral("valid")).toBool() || !m_library)
        return false;
    if (m_antResult.value(QStringLiteral("accepted")).toBool())
        return true;
    const qint64 id = m_library->saveTestResult(m_antResult.value(QStringLiteral("speed")).toDouble(),
                                                m_antResult.value(QStringLiteral("heartRate")).toInt(),
                                                m_antResult.value(QStringLiteral("incline")).toDouble(), m_lastRideId);
    if (id < 0)
        return false;
    m_acceptedTestId = id;
    m_antResult.insert(QStringLiteral("accepted"), true);
    emit antResultChanged();
    return true;
}

void RunSession::dismissAntResult()
{
    if (m_antResult.isEmpty())
        return;
    m_antResult.clear();
    emit antResultChanged();
}

qint64 RunSession::finish(bool save)
{
    if (!active())
        return -1;
    if (m_state == State::Running && !m_manualClock && !m_inProcess)
        process(int(m_clock.restart()));
    if (!active())   // process() may have finished it (route / workout end)
        return m_lastRideId;
    m_timer.stop();
    qint64 id = -1;
    if (save && m_recordedSec >= m_minSaveSec)
        id = saveRun();
    m_lastRideId = id;
    // an accepted AnT test made during this run points to it
    if (id >= 0 && m_acceptedTestId >= 0) {
        for (const AntTest &t : m_db->antTests(m_runProfile.id, 5)) {
            if (t.id == m_acceptedTestId) {
                AntTest linked = t;
                linked.rideId = id;
                m_db->saveAntTest(linked);
                break;
            }
        }
    }
    setState(State::Finished);
    DiagLog::write("RUN", QStringLiteral("==== treadmill run %1: %2 m in %3 s%4")
                              .arg(save ? QStringLiteral("finished") : QStringLiteral("discarded"))
                              .arg(m_runDistance, 0, 'f', 0).arg(m_recordedSec)
                              .arg(id >= 0 ? QStringLiteral(" (ride %1)").arg(id) : QString()));
    if (m_treadmill)
        m_treadmill->suggest(-1, -1);
    m_lastSuggestSpeed = -2;
    emit resultChanged();
    emit finished();
    if (id >= 0)
        emit rideSaved(id);
    return id;
}

void RunSession::reset()
{
    if (active())
        return;
    clearRecording();
    m_lastRideId = -1;
    setState(State::Ready);
    suggestToTreadmill();
    emit resultChanged();
    emit metricsChanged();
    emit positionChanged();
    emit seriesChanged();
    emit intervalChanged();
}

qint64 RunSession::saveRun()
{
    RideRecord r;
    r.profileId = m_runProfile.id;
    r.workoutId = m_workout ? m_workout->id : -1;
    r.workoutName = m_workout ? m_workout->name : QStringLiteral("Treadmill Run · %1").arg(routeName());
    r.startedAt = m_startedAt;
    r.durationSec = m_recordedSec;
    r.blocks = m_workout ? int(m_workout->steps.size()) : 0;
    r.ergMode = false;
    r.ftp = m_runProfile.ftp;
    double hrSum = 0;
    int hrN = 0, cadSum = 0, cadN = 0;
    const double ant = antSpeedKmh();
    for (int i = 0; i < m_secSpeed.size(); ++i) {
        const int z = ant > 0 ? runzones::zoneForFraction(m_secSpeed.at(i) / ant) : 0;
        if (m_secSpeed.at(i) > 1.0)
            r.zoneSeconds[size_t(std::clamp(z, 0, kZoneCount - 1))] += 1;
        if (m_secHr.at(i) > 0) {
            hrSum += m_secHr.at(i);
            ++hrN;
        }
        const int c = running::estimatedCadence(m_secSpeed.at(i));
        if (c > 0) {
            cadSum += c;
            ++cadN;
        }
    }
    r.avgHeartRate = hrN > 0 ? hrSum / hrN : 0.0;
    r.maxHeartRate = m_maxHr;
    r.avgCadence = cadN > 0 ? double(cadSum) / cadN : 0.0;
    r.kilojoules = m_kcal;   // (kcal, like the ~1 kcal per kJ convention of the rides)
    r.tss = rtss();
    r.intensityFactor = m_recordedSec > 0 ? std::sqrt(r.tss / 100.0 / (m_recordedSec / 3600.0)) : 0.0;
    r.sport = sport::running();
    r.distanceM = m_runDistance;
    const qint64 id = m_db->saveRide(r, m_samples);
    if (id < 0)
        setError(QStringLiteral("Could not save the run: %1").arg(m_db->lastError()));
    return id;
}

double RunSession::walkRunKmh() const
{
    return m_library ? m_library->walkRunKmh() : RunLibrary::kDefaultWalkRunKmh;
}

} // namespace watt
