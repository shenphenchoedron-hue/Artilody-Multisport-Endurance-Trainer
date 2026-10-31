#include "RunLibrary.h"

#include "Database.h"
#include "ProfileManager.h"

#include <QLocale>

#include <algorithm>
#include <cmath>

namespace watt {

namespace {
const QString kManualNote = QStringLiteral("Set by hand");
const QString kTestNote = QStringLiteral("AnT test");
} // namespace

RunLibrary::RunLibrary(Database *db, ProfileManager *profiles, QObject *parent)
    : QObject(parent)
    , m_db(db)
    , m_profiles(profiles)
{
    m_weeks = std::clamp(m_db->setting(QStringLiteral("run/antTestWeeks"), 6).toInt(), 2, 26);
    connect(m_profiles, &ProfileManager::profileChanged, this, &RunLibrary::reload);
    reload();
}

void RunLibrary::reload()
{
    const qint64 pid = m_profiles->profileId();
    const double walkRun = std::clamp(
        m_db->setting(QStringLiteral("run/walkRunKmh/%1").arg(pid), kDefaultWalkRunKmh).toDouble(), kMinWalkRunKmh,
        kMaxWalkRunKmh);
    if (!qFuzzyCompare(walkRun, m_walkRun)) {
        m_walkRun = walkRun;
        emit walkRunChanged();
    }
    m_history = pid >= 0 ? m_db->antTests(pid) : QVector<AntTest>();
    m_hasAnt = !m_history.isEmpty();
    m_ant = m_hasAnt ? m_history.constFirst() : AntTest();
    if (!m_hasAnt)
        m_ant.speedKmh = running::kDefaultAntKmh;
    m_lastTest.reset();
    for (const AntTest &t : std::as_const(m_history)) {
        if (t.note != kManualNote) {
            m_lastTest = t;
            break;
        }
    }
    emit antChanged();
    reloadWorkouts();
}

void RunLibrary::reloadWorkouts()
{
    m_workouts.clear();
    QVector<RunWorkout> own;
    for (const Workout &w : m_db->workouts()) {
        if (!w.isRunning())
            continue;
        (w.builtIn ? m_workouts : own).append(RunWorkout::fromWorkout(w));
    }
    std::stable_sort(own.begin(), own.end(), [](const RunWorkout &a, const RunWorkout &b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    m_workouts += own;
    emit workoutsChanged();
}

void RunLibrary::antStored()
{
    reload();
    m_profiles->refreshAnt();   // running workouts elsewhere (Workouts, Builder) follow
}

qint64 RunLibrary::antTestWorkoutId() const
{
    for (const RunWorkout &w : m_workouts) {
        if (w.builtIn && w.isAntTest())
            return w.id;
    }
    for (const RunWorkout &w : m_workouts) {
        if (w.isAntTest())
            return w.id;
    }
    return -1;
}

void RunLibrary::setError(const QString &e)
{
    m_lastError = e;
    emit lastErrorChanged();
}

QString RunLibrary::antPace() const
{
    return running::paceText(m_ant.speedKmh);
}

QString RunLibrary::paceFor(double kmh) const
{
    return running::paceText(kmh);
}

int RunLibrary::zoneForSpeed(double kmh) const
{
    return m_ant.speedKmh > 0 ? runzones::zoneForFraction(kmh / m_ant.speedKmh) : 0;
}

int RunLibrary::daysSinceTest() const
{
    if (!m_lastTest || !m_lastTest->testedAt.isValid())
        return -1;
    return int(m_lastTest->testedAt.date().daysTo(today()));
}

void RunLibrary::setTestIntervalWeeks(int weeks)
{
    weeks = std::clamp(weeks, 2, 26);
    if (weeks == m_weeks)
        return;
    m_weeks = weeks;
    m_db->setSetting(QStringLiteral("run/antTestWeeks"), weeks);
    emit antChanged();
}

void RunLibrary::setWalkRunKmh(double kmh)
{
    kmh = std::round(std::clamp(kmh, kMinWalkRunKmh, kMaxWalkRunKmh) * 10.0) / 10.0;
    if (qFuzzyCompare(kmh, m_walkRun))
        return;
    m_walkRun = kmh;
    m_db->setSetting(QStringLiteral("run/walkRunKmh/%1").arg(m_profiles->profileId()), kmh);
    emit walkRunChanged();
}

QDate RunLibrary::nextTestDate() const
{
    if (!m_lastTest || !m_lastTest->testedAt.isValid())
        return today();
    return m_lastTest->testedAt.date().addDays(7 * m_weeks);
}

bool RunLibrary::testDue() const
{
    return nextTestDate() <= today();
}

QString RunLibrary::testStatusText() const
{
    if (!m_lastTest)
        return m_hasAnt ? QStringLiteral("AnT set by hand – run the AnT test to measure it")
                        : QStringLiteral("No AnT test yet – run the test to set your zones");
    const int days = daysSinceTest();
    const QString ago = days <= 0 ? QStringLiteral("today") : days == 1 ? QStringLiteral("yesterday")
                                                                         : QStringLiteral("%1 days ago").arg(days);
    if (testDue())
        return QStringLiteral("Last test %1 – time for a new AnT test").arg(ago);
    const int left = int(today().daysTo(nextTestDate()));
    return QStringLiteral("Last test %1 – next test in %2 day%3").arg(ago).arg(left).arg(left == 1 ? "" : "s");
}

QVariantList RunLibrary::antHistory() const
{
    QVariantList out;
    for (const AntTest &t : m_history) {
        QVariantMap m;
        m.insert(QStringLiteral("id"), t.id);
        m.insert(QStringLiteral("testedAt"), t.testedAt);
        m.insert(QStringLiteral("dateText"), QLocale().toString(t.testedAt.date(), QLocale::ShortFormat));
        m.insert(QStringLiteral("speed"), t.speedKmh);
        m.insert(QStringLiteral("pace"), running::paceText(t.speedKmh));
        m.insert(QStringLiteral("heartRate"), t.heartRate);
        m.insert(QStringLiteral("incline"), t.inclinePercent);
        m.insert(QStringLiteral("rideId"), t.rideId);
        m.insert(QStringLiteral("manual"), t.note == kManualNote);
        m.insert(QStringLiteral("note"), t.note);
        out.append(m);
    }
    return out;
}

QVariantList RunLibrary::zones() const
{
    return runzones::table(m_ant.speedKmh);
}

QVariantList RunLibrary::workouts() const
{
    QVariantList out;
    for (const RunWorkout &w : m_workouts)
        out.append(w.toVariant(m_ant.speedKmh));
    return out;
}

std::optional<RunWorkout> RunLibrary::workout(qint64 id) const
{
    for (const RunWorkout &w : m_workouts) {
        if (w.id == id)
            return w;
    }
    // not loaded yet (e.g. saved a moment ago): straight from the database
    if (const auto w = m_db->workout(id); w && w->isRunning())
        return RunWorkout::fromWorkout(*w);
    return std::nullopt;
}

QVariantMap RunLibrary::get(qint64 id) const
{
    const auto w = workout(id);
    return w ? w->toVariant(m_ant.speedKmh) : QVariantMap();
}

bool RunLibrary::setManualAnt(double speedKmh, int heartRate)
{
    if (!(speedKmh >= running::kMinAntKmh && speedKmh <= running::kMaxAntKmh)) {
        setError(QStringLiteral("The AnT speed must be between %1 and %2 km/h").arg(running::kMinAntKmh).arg(running::kMaxAntKmh));
        return false;
    }
    if (heartRate != 0 && (heartRate < 60 || heartRate > 230)) {
        setError(QStringLiteral("The AnT heart rate must be between 60 and 230 bpm (0 = unknown)"));
        return false;
    }
    AntTest t;
    t.profileId = m_profiles->profileId();
    t.testedAt = QDateTime::currentDateTime();
    t.speedKmh = std::round(speedKmh * 10.0) / 10.0;
    t.heartRate = heartRate;
    t.inclinePercent = 1.0;
    t.note = kManualNote;
    if (m_db->saveAntTest(t) < 0) {
        setError(m_db->lastError());
        return false;
    }
    antStored();
    return true;
}

qint64 RunLibrary::saveTestResult(double speedKmh, int heartRate, double inclinePercent, qint64 rideId)
{
    AntTest t;
    t.profileId = m_profiles->profileId();
    t.testedAt = QDateTime::currentDateTime();
    t.speedKmh = std::round(std::clamp(speedKmh, running::kMinAntKmh, running::kMaxAntKmh) * 10.0) / 10.0;
    t.heartRate = std::max(0, heartRate);
    t.inclinePercent = std::round(inclinePercent * 10.0) / 10.0;
    t.rideId = rideId;
    t.note = kTestNote;
    const qint64 id = m_db->saveAntTest(t);
    if (id < 0)
        setError(m_db->lastError());
    antStored();
    return id;
}

bool RunLibrary::deleteAntTest(qint64 id)
{
    if (!m_db->deleteAntTest(id)) {
        setError(QStringLiteral("Could not delete the entry"));
        return false;
    }
    antStored();
    return true;
}

} // namespace watt
