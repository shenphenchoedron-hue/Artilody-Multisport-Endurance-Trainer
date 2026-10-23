#include "WorkoutPlanner.h"

#include "Database.h"
#include "ProfileManager.h"

#include <QDateTime>

namespace watt {

WorkoutPlanner::WorkoutPlanner(Database *db, ProfileManager *profiles, QObject *parent)
    : QObject(parent)
    , m_db(db)
    , m_profiles(profiles)
{
    connect(m_profiles, &ProfileManager::profileChanged, this, &WorkoutPlanner::refresh);
}

void WorkoutPlanner::setActiveEntry(qint64 entryId)
{
    const auto e = entryId >= 0 ? m_db->plannedWorkout(entryId) : std::nullopt;
    const qint64 id = e && e->profileId == m_profiles->profileId() ? entryId : -1;
    if (m_activeEntryId == id)
        return;
    m_activeEntryId = id;
    emit activeEntryChanged();
}

void WorkoutPlanner::clearActiveEntry()
{
    if (m_activeEntryId < 0)
        return;
    m_activeEntryId = -1;
    emit activeEntryChanged();
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
    m.insert(QStringLiteral("id"), e.id);
    m.insert(QStringLiteral("date"), e.date.toString(Qt::ISODate));
    m.insert(QStringLiteral("workoutId"), e.workoutId);
    m.insert(QStringLiteral("name"), e.workoutName);
    m.insert(QStringLiteral("sport"), e.sport);
    m.insert(QStringLiteral("category"), e.category);
    m.insert(QStringLiteral("durationSec"), e.durationSec);
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

QString WorkoutPlanner::sportFor(const QVariant &entryOrId) const
{
    QVariantMap m;
    if (entryOrId.typeId() == QMetaType::QVariantMap || entryOrId.typeId() == QMetaType::QVariantHash)
        m = entryOrId.toMap();
    else if (entryOrId.canConvert<qint64>() && entryOrId.toLongLong() > 0)
        m = entry(entryOrId.toLongLong());
    else
        m = entryOrId.toMap();
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
