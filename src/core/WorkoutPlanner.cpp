#include "WorkoutPlanner.h"

#include "Database.h"
#include "FreeRideSession.h"
#include "ProfileManager.h"
#include "RlvLibrary.h"
#include "RlvRoute.h"
#include "RlvSession.h"
#include "TrainingSession.h"

#include <QDateTime>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cmath>

namespace watt {

namespace {
QVariantMap optionsFromText(const QString &text)
{
    if (text.trimmed().isEmpty())
        return {};
    return QJsonDocument::fromJson(text.toUtf8()).object().toVariantMap();
}

QString optionsToText(const QVariantMap &m)
{
    return m.isEmpty() ? QString()
                       : QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(m)).toJson(QJsonDocument::Compact));
}
} // namespace

WorkoutPlanner::WorkoutPlanner(Database *db, ProfileManager *profiles, QObject *parent)
    : QObject(parent)
    , m_db(db)
    , m_profiles(profiles)
{
    connect(m_profiles, &ProfileManager::profileChanged, this, &WorkoutPlanner::refresh);
}

void WorkoutPlanner::setSessions(TrainingSession *training, FreeRideSession *freeRide, RlvSession *rlv,
                                 RlvLibrary *videos)
{
    m_training = training;
    m_freeRide = freeRide;
    m_rlv = rlv;
    if (m_videos)
        disconnect(m_videos, nullptr, this, nullptr);
    m_videos = videos;
    if (m_videos)   // route names / distances of Video Ride entries
        connect(m_videos, &RlvLibrary::scanned, this, &WorkoutPlanner::refresh);
}

void WorkoutPlanner::setError(const QString &e)
{
    if (m_lastError == e)
        return;
    m_lastError = e;
    emit lastErrorChanged();
}

void WorkoutPlanner::setActiveEntry(qint64 entryId)
{
    const auto e = entryId >= 0 ? m_db->plannedWorkout(entryId) : std::nullopt;
    const qint64 id = e && e->profileId == m_profiles->profileId() ? entryId : -1;
    if (m_activeEntryId == id)
        return;
    const qint64 previous = m_activeEntryId;
    m_activeEntryId = id;
    activeEntryReset(previous);
    emit activeEntryChanged();
}

void WorkoutPlanner::clearActiveEntry()
{
    if (m_activeEntryId < 0)
        return;
    const qint64 previous = m_activeEntryId;
    m_activeEntryId = -1;
    activeEntryReset(previous);
    emit activeEntryChanged();
}

void WorkoutPlanner::activeEntryReset(qint64)
{
    // The Free Ride's planned duration belongs to its (active) plan entry.
    if (!m_freeRide || m_freeRide->active() || m_freeRide->waiting())
        return;
    const auto e = m_activeEntryId >= 0 ? m_db->plannedWorkout(m_activeEntryId) : std::nullopt;
    if (!e || e->kind != plankind::freeRide())
        m_freeRide->setPlannedDurationSec(0);
}

bool WorkoutPlanner::freeRideQualifies(int riddenSec, int plannedSec)
{
    if (plannedSec > 0)
        return riddenSec >= int(std::lround(plannedSec * kFreeRideDoneShare));
    return riddenSec >= kMinFreeRideSec;
}

bool WorkoutPlanner::videoRideQualifies(bool routeCompleted, double distanceShare)
{
    return routeCompleted || (std::isfinite(distanceShare) && distanceShare >= kVideoRideDoneShare - 1e-9);
}

qint64 WorkoutPlanner::completeFreeRide(qint64 rideId, int riddenSec, qint64 profileId)
{
    if (m_activeEntryId < 0)
        return -1;
    const auto e = m_db->plannedWorkout(m_activeEntryId);
    if (!e) {
        clearActiveEntry();   // deleted meanwhile
        return -1;
    }
    const qint64 profile = profileId >= 0 ? profileId : m_profiles->profileId();
    if (e->kind != plankind::freeRide() || e->profileId != profile || !freeRideQualifies(riddenSec, e->plannedDurationSec))
        return -1;   // not this entry's ride (or too short): it stays open and active
    const qint64 id = m_activeEntryId;
    const bool ok = m_db->setPlannedCompleted(id, true, rideId);
    clearActiveEntry();
    if (!ok)
        return -1;
    refresh();
    return id;
}

qint64 WorkoutPlanner::completeVideoRide(qint64 rideId, const QString &routePath, bool routeCompleted,
                                         double distanceShare, qint64 profileId)
{
    if (m_activeEntryId < 0)
        return -1;
    const auto e = m_db->plannedWorkout(m_activeEntryId);
    if (!e) {
        clearActiveEntry();
        return -1;
    }
    const qint64 profile = profileId >= 0 ? profileId : m_profiles->profileId();
    if (e->kind != plankind::video() || e->profileId != profile || !samePath(e->routePath, routePath)
        || !videoRideQualifies(routeCompleted, distanceShare))
        return -1;
    const qint64 id = m_activeEntryId;
    const bool ok = m_db->setPlannedCompleted(id, true, rideId);
    clearActiveEntry();
    if (!ok)
        return -1;
    refresh();
    return id;
}

bool WorkoutPlanner::samePath(const QString &a, const QString &b)
{
    const QString la = RlvLibrary::toLocalPath(a);
    const QString lb = RlvLibrary::toLocalPath(b);
    if (la.isEmpty() || lb.isEmpty())
        return false;
    if (la == lb)
        return true;
    // a route folder and a route file inside it
    const QFileInfo fa(la), fb(lb);
    return (fa.isDir() && fb.absolutePath() == la) || (fb.isDir() && fa.absolutePath() == lb);
}

WorkoutPlanner::RouteInfo WorkoutPlanner::lookupRoute(const QString &path, bool allowLoad) const
{
    RouteInfo r;
    const QString local = RlvLibrary::toLocalPath(path);
    if (local.isEmpty())
        return r;
    if (m_videos) {
        const int row = m_videos->indexOfPath(local);
        if (row >= 0 && row < m_videos->entries().size() && m_videos->entries().at(row).valid) {
            const RlvLibrary::Entry &e = m_videos->entries().at(row);
            r.found = r.inLibrary = true;
            r.path = e.path;
            r.name = e.title.isEmpty() ? e.name : e.title;
            r.distanceM = e.distanceM;
            r.ascentM = e.ascentM;
            r.durationSec = e.durationSec;
            return r;
        }
    }
    if (allowLoad) {
        RlvRoute route;
        if (route.load(local) && route.isValid()) {
            r.found = true;
            r.path = route.sourcePath();
            r.name = route.title();
            r.distanceM = route.totalDistance();
            r.ascentM = route.ascent();
            r.durationSec = std::max(0, int(std::lround(route.videoDuration() - route.videoOffset())));
        }
    }
    return r;
}

bool WorkoutPlanner::normalizeOptions(const QString &kind, const QVariantMap &in, QVariantMap *out)
{
    QVariantMap m;
    const QString mode = in.value(QStringLiteral("mode")).toString().trimmed().toLower();
    const int ergWatts = in.value(QStringLiteral("ergWatts"), 0).toInt();
    if (kind == plankind::freeRide()) {
        const auto parsed = FreeRideSession::parseMode(mode.isEmpty() ? QStringLiteral("ride") : mode);
        if (!parsed) {
            setError(QStringLiteral("Unknown free ride mode \"%1\"").arg(mode));
            return false;
        }
        m.insert(QStringLiteral("mode"), FreeRideSession::modeName(*parsed));
        m.insert(QStringLiteral("ergWatts"),
                 ergWatts > 0 ? std::clamp(ergWatts, int(FreeRideSession::kMinErgWatts), int(FreeRideSession::kMaxWatts))
                              : 0);
    } else if (kind == plankind::video()) {
        const auto parsed = RlvSession::parseMode(mode.isEmpty() ? QStringLiteral("sim") : mode);
        if (!parsed) {
            setError(QStringLiteral("Unknown video ride mode \"%1\"").arg(mode));
            return false;
        }
        m.insert(QStringLiteral("mode"), RlvSession::modeName(*parsed));
        m.insert(QStringLiteral("difficulty"),
                 std::clamp(in.value(QStringLiteral("difficulty"), int(RlvSession::kDefaultDifficulty)).toInt(), 0, 100));
        m.insert(QStringLiteral("ergWatts"),
                 ergWatts > 0 ? std::clamp(ergWatts, int(RlvSession::kMinErgWatts), int(RlvSession::kMaxWatts)) : 0);
        const QVariant wv = in.value(QStringLiteral("workoutId"), -1);
        const qint64 workoutId = wv.isNull() ? -1 : wv.toLongLong();
        if (workoutId >= 0) {
            const auto w = m_db->workout(workoutId);
            if (!w) {
                setError(QStringLiteral("Workout not found"));
                return false;
            }
            if (w->isRowing() || w->intervals.isEmpty() || w->totalDurationSec() <= 0) {
                setError(QStringLiteral("\"%1\" cannot be ridden in a video ride").arg(w->name));
                return false;
            }
        }
        m.insert(QStringLiteral("workoutId"), workoutId >= 0 ? workoutId : qint64(-1));
        m.insert(QStringLiteral("workoutErg"), in.value(QStringLiteral("workoutErg"), true).toBool());
    }
    *out = m;
    return true;
}

qint64 WorkoutPlanner::addFreeRide(const QVariant &date, int durationMin, const QVariantMap &options,
                                   const QString &notes)
{
    const QDate d = toDate(date);
    if (!d.isValid() || m_profiles->profileId() < 0) {
        setError(QStringLiteral("Invalid date"));
        return -1;
    }
    QVariantMap opts;
    if (!normalizeOptions(plankind::freeRide(), options, &opts))
        return -1;
    PlannedWorkout e;
    e.profileId = m_profiles->profileId();
    e.date = d;
    e.kind = plankind::freeRide();
    e.plannedDurationSec = std::clamp(durationMin, 0, int(kMaxFreeRideMin)) * 60;
    e.options = optionsToText(opts);
    e.notes = notes;
    const qint64 id = m_db->addPlannedEntry(e);
    if (id < 0) {
        setError(m_db->lastError());
        return -1;
    }
    setError(QString());
    refresh();
    return id;
}

qint64 WorkoutPlanner::addVideoRide(const QVariant &date, const QString &routePath, const QVariantMap &options,
                                    const QString &notes)
{
    const QDate d = toDate(date);
    if (!d.isValid() || m_profiles->profileId() < 0) {
        setError(QStringLiteral("Invalid date"));
        return -1;
    }
    const RouteInfo route = lookupRoute(routePath, true);
    if (!route.found) {
        setError(QStringLiteral("Route not found: %1").arg(routePath));
        return -1;
    }
    QVariantMap opts;
    if (!normalizeOptions(plankind::video(), options, &opts))
        return -1;
    PlannedWorkout e;
    e.profileId = m_profiles->profileId();
    e.date = d;
    e.kind = plankind::video();
    e.routePath = route.path;
    e.routeName = route.name;
    e.options = optionsToText(opts);
    e.notes = notes;
    const qint64 id = m_db->addPlannedEntry(e);
    if (id < 0) {
        setError(m_db->lastError());
        return -1;
    }
    setError(QString());
    refresh();
    return id;
}

bool WorkoutPlanner::updateEntry(qint64 id, const QVariantMap &options, int durationMin)
{
    const auto e = m_db->plannedWorkout(id);
    if (!e || e->profileId != m_profiles->profileId()) {
        setError(QStringLiteral("Plan entry not found"));
        return false;
    }
    if (e->kind == plankind::workout()) {
        setError(QStringLiteral("Workout entries have no options"));
        return false;
    }
    QVariantMap merged = optionsFromText(e->options);
    for (auto it = options.cbegin(); it != options.cend(); ++it)
        merged.insert(it.key(), it.value());
    QVariantMap opts;
    if (!normalizeOptions(e->kind, merged, &opts))
        return false;
    int planned = e->plannedDurationSec;
    if (e->kind == plankind::freeRide() && durationMin >= 0)
        planned = std::clamp(durationMin, 0, int(kMaxFreeRideMin)) * 60;
    if (!m_db->updatePlannedDetails(id, planned, optionsToText(opts))) {
        setError(m_db->lastError());
        return false;
    }
    setError(QString());
    refresh();
    return true;
}

qint64 WorkoutPlanner::completeForRide(qint64 workoutId, const QDate &date, qint64 rideId, qint64 profileId)
{
    if (workoutId < 0)
        return -1;
    const qint64 profile = profileId >= 0 ? profileId : m_profiles->profileId();
    if (m_activeEntryId >= 0) {
        const auto e = m_db->plannedWorkout(m_activeEntryId);
        if (!e) {
            clearActiveEntry();   // deleted meanwhile
        } else if (e->workoutId == workoutId && e->profileId == profile) {
            const qint64 id = m_activeEntryId;
            const bool ok = m_db->setPlannedCompleted(id, true, rideId);
            clearActiveEntry();
            if (ok) {
                refresh();
                return id;
            }
        }
        // else: a different workout was ridden; the active entry stays open.
    }
    const QDate d = date.isValid() ? date : QDate::currentDate();
    const qint64 id = m_db->completePlannedWorkout(profile, workoutId, d, rideId);
    if (id >= 0)
        refresh();
    return id;
}

void WorkoutPlanner::refresh()
{
    ++m_revision;
    emit planChanged();
}

QDate WorkoutPlanner::toDate(const QVariant &value)
{
    if (!value.isValid() || value.isNull())
        return {};
    switch (value.typeId()) {
    case QMetaType::QDate:
        return value.toDate();
    case QMetaType::QDateTime:
        // JS Date values arrive as QDateTime; use the local calendar day.
        return value.toDateTime().toLocalTime().date();
    default:
        break;
    }
    const QString text = value.toString().trimmed();
    // A full date-time (e.g. a JS Date serialized as "2026-03-14T23:30:00.000Z")
    // names a moment: its local calendar day may differ from the first 10
    // characters, so parse it as a date-time first.
    if (text.size() > 10) {
        QDateTime dt = QDateTime::fromString(text, Qt::ISODateWithMs);
        if (!dt.isValid())
            dt = QDateTime::fromString(text, Qt::ISODate);
        if (dt.isValid())
            return dt.toLocalTime().date();
    }
    return QDate::fromString(text.left(10), Qt::ISODate);
}

QVariantMap WorkoutPlanner::toMap(const PlannedWorkout &e) const
{
    const QDate today = QDate::currentDate();
    QVariantMap m;
    const QVariantMap opts = optionsFromText(e.options);
    qint64 workoutId = e.workoutId;
    QString name = e.workoutName;
    QString workoutName = e.kind == plankind::workout() ? e.workoutName : QString();
    QString sportName = e.sport;
    QString category = e.category;
    int durationSec = e.durationSec;
    RouteInfo route;
    if (e.kind == plankind::freeRide()) {
        workoutId = -1;
        sportName = sport::cycling();
        category = QStringLiteral("Free Ride");
        durationSec = e.plannedDurationSec;
        name = e.plannedDurationSec > 0
                   ? QStringLiteral("Free Ride · %1 min").arg(int(std::lround(e.plannedDurationSec / 60.0)))
                   : QStringLiteral("Free Ride");
    } else if (e.kind == plankind::video()) {
        route = lookupRoute(e.routePath, false);   // library only: no file parsing per refresh
        const QString routeName = route.inLibrary && !route.name.isEmpty()
                                      ? route.name
                                      : (e.routeName.isEmpty() ? QFileInfo(e.routePath).completeBaseName() : e.routeName);
        sportName = sport::cycling();
        category = QStringLiteral("Video Ride");
        workoutId = -1;
        durationSec = route.durationSec;
        const QVariant wv = opts.value(QStringLiteral("workoutId"), -1);
        const qint64 attached = wv.isNull() ? -1 : wv.toLongLong();
        if (attached >= 0) {
            if (const auto w = m_db->workout(attached)) {
                workoutId = attached;
                workoutName = w->name;
                durationSec = w->totalDurationSec();
            }
        }
        if (workoutId >= 0)
            name = QStringLiteral("Video: %1 + %2").arg(routeName, workoutName);
        else if (route.distanceM > 0)
            name = QStringLiteral("Video: %1 · %2 km").arg(routeName, QString::number(route.distanceM / 1000.0, 'f', 1));
        else
            name = QStringLiteral("Video: %1").arg(routeName);
        m.insert(QStringLiteral("routeName"), routeName);
    }
    m.insert(QStringLiteral("id"), e.id);
    m.insert(QStringLiteral("date"), e.date.toString(Qt::ISODate));
    m.insert(QStringLiteral("kind"), e.kind);
    m.insert(QStringLiteral("workoutId"), workoutId);
    m.insert(QStringLiteral("workoutName"), workoutName);
    m.insert(QStringLiteral("name"), name);
    m.insert(QStringLiteral("sport"), sportName);
    m.insert(QStringLiteral("category"), category);
    m.insert(QStringLiteral("durationSec"), durationSec);
    m.insert(QStringLiteral("plannedDurationSec"), e.plannedDurationSec);
    m.insert(QStringLiteral("options"), opts);
    m.insert(QStringLiteral("routePath"), e.routePath);
    if (!m.contains(QStringLiteral("routeName")))
        m.insert(QStringLiteral("routeName"), e.routeName);
    m.insert(QStringLiteral("routeDistanceM"), route.distanceM);
    m.insert(QStringLiteral("routeAscentM"), int(std::lround(route.ascentM)));
    m.insert(QStringLiteral("routeAvailable"), route.inLibrary);
    m.insert(QStringLiteral("completed"), e.completed);
    m.insert(QStringLiteral("rideId"), e.rideId);
    m.insert(QStringLiteral("position"), e.position);
    m.insert(QStringLiteral("notes"), e.notes);
    m.insert(QStringLiteral("isPast"), e.date < today);
    m.insert(QStringLiteral("isToday"), e.date == today);
    return m;
}

QVariantList WorkoutPlanner::toList(const QVector<PlannedWorkout> &entries) const
{
    QVariantList list;
    list.reserve(entries.size());
    for (const PlannedWorkout &e : entries)
        list.append(toMap(e));
    return list;
}

qint64 WorkoutPlanner::addWorkout(const QVariant &date, qint64 workoutId, const QString &notes)
{
    const QDate d = toDate(date);
    if (!d.isValid() || m_profiles->profileId() < 0)
        return -1;
    const qint64 id = m_db->addPlannedWorkout(m_profiles->profileId(), d, workoutId, notes);
    if (id >= 0)
        refresh();
    return id;
}

bool WorkoutPlanner::remove(qint64 id)
{
    const auto e = m_db->plannedWorkout(id);
    if (!e || e->profileId != m_profiles->profileId())
        return false;
    const bool ok = m_db->removePlannedWorkout(id);
    if (ok)
        refresh();
    return ok;
}

bool WorkoutPlanner::move(qint64 id, const QVariant &newDate)
{
    const QDate d = toDate(newDate);
    const auto e = m_db->plannedWorkout(id);
    if (!d.isValid() || !e || e->profileId != m_profiles->profileId())
        return false;
    if (e->date == d)
        return true;
    const bool ok = m_db->movePlannedWorkout(id, d);
    if (ok)
        refresh();
    return ok;
}

bool WorkoutPlanner::setCompleted(qint64 id, bool completed)
{
    const auto e = m_db->plannedWorkout(id);
    if (!e || e->profileId != m_profiles->profileId())
        return false;
    if (e->completed == completed)
        return true;
    const bool ok = m_db->setPlannedCompleted(id, completed, completed ? e->rideId : -1);
    if (ok)
        refresh();
    return ok;
}

bool WorkoutPlanner::setNotes(qint64 id, const QString &notes)
{
    const auto e = m_db->plannedWorkout(id);
    if (!e || e->profileId != m_profiles->profileId())
        return false;
    const bool ok = m_db->setPlannedNotes(id, notes);
    if (ok)
        refresh();
    return ok;
}

QVariantMap WorkoutPlanner::entry(qint64 id) const
{
    const auto e = m_db->plannedWorkout(id);
    if (!e || e->profileId != m_profiles->profileId())
        return {};
    return toMap(*e);
}

QVariantList WorkoutPlanner::entriesForMonth(int year, int month) const
{
    const QDate first(year, month, 1);
    if (!first.isValid())
        return {};
    return toList(m_db->plannedWorkouts(m_profiles->profileId(), first, first.addMonths(1).addDays(-1)));
}

QVariantList WorkoutPlanner::entriesForDate(const QVariant &date) const
{
    const QDate d = toDate(date);
    if (!d.isValid())
        return {};
    return toList(m_db->plannedWorkouts(m_profiles->profileId(), d, d));
}

QVariantList WorkoutPlanner::entriesBetween(const QVariant &from, const QVariant &to) const
{
    const QDate a = toDate(from);
    const QDate b = toDate(to);
    if (!a.isValid() || !b.isValid() || b < a)
        return {};
    return toList(m_db->plannedWorkouts(m_profiles->profileId(), a, b));
}

QVariantList WorkoutPlanner::upcoming(int n) const
{
    if (n <= 0)
        return {};
    return toList(m_db->plannedWorkouts(m_profiles->profileId(), QDate::currentDate(), QDate(), true, n));
}

QVariantMap WorkoutPlanner::entryMapFor(const QVariant &entryOrId) const
{
    if (entryOrId.typeId() == QMetaType::QVariantMap || entryOrId.typeId() == QMetaType::QVariantHash)
        return entryOrId.toMap();
    if (entryOrId.canConvert<qint64>() && entryOrId.toLongLong() > 0)
        return entry(entryOrId.toLongLong());
    return entryOrId.toMap();
}

QString WorkoutPlanner::kindFor(const QVariant &entryOrId) const
{
    const QVariantMap m = entryMapFor(entryOrId);
    QString k = m.value(QStringLiteral("kind")).toString();
    if (k.isEmpty() && m.contains(QStringLiteral("id"))) {
        if (const auto e = m_db->plannedWorkout(m.value(QStringLiteral("id")).toLongLong()))
            k = e->kind;
    }
    return plankind::isValid(k) ? k : plankind::workout();
}

QString WorkoutPlanner::startEntry(const QVariant &entryOrId)
{
    setError(QString());
    const QVariantMap m = entryMapFor(entryOrId);
    const qint64 id = m.value(QStringLiteral("id"), -1).toLongLong();
    const auto e = id > 0 ? m_db->plannedWorkout(id) : std::nullopt;
    if (!e || e->profileId != m_profiles->profileId()) {
        setError(QStringLiteral("Plan entry not found"));
        return {};
    }
    const QVariantMap opts = optionsFromText(e->options);
    if (e->kind == plankind::freeRide()) {
        if (!m_freeRide) {
            setError(QStringLiteral("Free rides are not available"));
            return {};
        }
        if (m_freeRide->active() || m_freeRide->waiting()) {
            setError(QStringLiteral("A free ride is in progress – finish it first"));
            return {};
        }
        m_freeRide->setMode(opts.value(QStringLiteral("mode"), QStringLiteral("ride")).toString());
        if (const int w = opts.value(QStringLiteral("ergWatts"), 0).toInt(); w > 0)
            m_freeRide->setTargetWatts(w);
        m_freeRide->setPlannedDurationSec(e->plannedDurationSec);
        setActiveEntry(id);
        return QStringLiteral("freeride");
    }
    if (e->kind == plankind::video())
        return startVideo(*e, opts);

    // workout
    const auto w = m_db->workout(e->workoutId);
    if (!w) {
        setError(QStringLiteral("Workout not found"));
        return {};
    }
    if (w->isRowing()) {
        if (!m_rowingStarter) {
            setError(QStringLiteral("Rowing is not available"));
            return {};
        }
        if (!m_rowingStarter(w->id)) {
            setError(QStringLiteral("Could not load \"%1\" as a rowing plan").arg(w->name));
            return {};
        }
        setActiveEntry(id);
        return QStringLiteral("rowing");
    }
    if (!m_training) {
        setError(QStringLiteral("Workouts are not available"));
        return {};
    }
    if (m_training->active()) {
        setError(QStringLiteral("A workout is in progress – finish it first"));
        return {};
    }
    if (!m_training->loadWorkout(w->id)) {
        setError(QStringLiteral("Could not load \"%1\"").arg(w->name));
        return {};
    }
    setActiveEntry(id);
    return QStringLiteral("dashboard");
}

QString WorkoutPlanner::startVideo(const PlannedWorkout &e, const QVariantMap &opts)
{
    if (!m_rlv) {
        setError(QStringLiteral("Video rides are not available"));
        return {};
    }
    if (m_rlv->active() || m_rlv->waiting()) {
        setError(QStringLiteral("A video ride is in progress – finish it first"));
        return {};
    }
    const RouteInfo route = lookupRoute(e.routePath, false);
    const QString path = route.inLibrary ? route.path : RlvLibrary::toLocalPath(e.routePath);
    const QString displayName = route.inLibrary ? route.name : (e.routeName.isEmpty() ? e.routePath : e.routeName);
    if (path.isEmpty() || (!route.inLibrary && !QFileInfo::exists(path))) {
        setError(QStringLiteral("Route not found: %1").arg(displayName));
        return {};
    }
    if (!m_rlv->selectPath(path)) {
        setError(QStringLiteral("Route not found: %1").arg(displayName));
        return {};
    }
    m_rlv->setMode(opts.value(QStringLiteral("mode"), QStringLiteral("sim")).toString());
    m_rlv->setDifficulty(opts.value(QStringLiteral("difficulty"), int(RlvSession::kDefaultDifficulty)).toInt());
    if (const int w = opts.value(QStringLiteral("ergWatts"), 0).toInt(); w > 0)
        m_rlv->setTargetWatts(w);
    QString warning;
    const QVariant wv = opts.value(QStringLiteral("workoutId"), -1);
    const qint64 workoutId = wv.isNull() ? -1 : wv.toLongLong();
    if (workoutId >= 0) {
        if (m_rlv->attachWorkout(workoutId)) {
            m_rlv->setWorkoutErg(opts.value(QStringLiteral("workoutErg"), true).toBool());
        } else {
            m_rlv->detachWorkout();
            warning = QStringLiteral("Workout not found – riding the route only");
        }
    } else {
        m_rlv->detachWorkout();
    }
    setActiveEntry(e.id);
    setError(warning);
    return QStringLiteral("video");
}

QString WorkoutPlanner::sportFor(const QVariant &entryOrId) const
{
    const QVariantMap m = entryMapFor(entryOrId);
    QString s = m.value(QStringLiteral("sport")).toString();
    if (s.isEmpty() && m.contains(QStringLiteral("workoutId"))) {
        if (const auto w = m_db->workout(m.value(QStringLiteral("workoutId")).toLongLong()))
            s = w->sport;
    }
    return sport::normalize(s);
}

bool WorkoutPlanner::isRowing(const QVariant &entryOrId) const
{
    return sportFor(entryOrId) == sport::rowing();
}

qint64 WorkoutPlanner::markCompletedForWorkout(qint64 workoutId, const QVariant &date, qint64 rideId, qint64 profileId)
{
    QDate d = toDate(date);
    if (!d.isValid())
        d = QDate::currentDate();
    const qint64 id = m_db->completePlannedWorkout(profileId >= 0 ? profileId : m_profiles->profileId(), workoutId, d,
                                                   rideId);
    if (id >= 0)
        refresh();
    return id;
}

} // namespace watt
