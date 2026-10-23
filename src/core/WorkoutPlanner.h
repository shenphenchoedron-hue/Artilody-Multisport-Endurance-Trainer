#pragma once

#include "PlanTypes.h"

#include <QDate>
#include <QObject>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

namespace watt {

class Database;
class ProfileManager;

// Training plan / calendar of the active profile. Exposed to QML as "Planner".
//
// Dates may be passed as "yyyy-MM-dd" strings, QDate or JS Date values (the
// local calendar day is used). Entries are returned as maps:
//   { id, date ("yyyy-MM-dd"), workoutId, name, sport, category, durationSec,
//     completed, rideId (-1 = none), position, notes, isPast, isToday }
// Entries of deleted workouts are removed with the workout and never returned.
class WorkoutPlanner : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY planChanged)   // bumps on every change (for QML bindings)
    // Plan entry started from the calendar (-1 = none), see setActiveEntry().
    Q_PROPERTY(qint64 activeEntryId READ activeEntryId NOTIFY activeEntryChanged)
public:
    WorkoutPlanner(Database *db, ProfileManager *profiles, QObject *parent = nullptr);

    int revision() const { return m_revision; }
    qint64 activeEntryId() const { return m_activeEntryId; }

    // Call right before Training.loadWorkout()/Rowing.setupPlan() when a plan
    // entry is started from the calendar (any day: past, today or future).
    // When a completed ride of that entry's workout is saved, exactly this
    // entry is ticked off (whatever its date) and the active entry is cleared.
    // Rides of other workouts never complete it. Unknown ids clear it.
    Q_INVOKABLE void setActiveEntry(qint64 entryId);
    Q_INVOKABLE void clearActiveEntry();

    // Completion hook for a saved ride of a *completed* workout (not for
    // aborted ones): ticks off the active entry when it belongs to workoutId,
    // otherwise the first open entry of workoutId on date. profileId is the
    // rider of the ride (< 0 = active profile). Returns the entry id or -1.
    qint64 completeForRide(qint64 workoutId, const QDate &date, qint64 rideId, qint64 profileId = -1);

    Q_INVOKABLE qint64 addWorkout(const QVariant &date, qint64 workoutId, const QString &notes = QString());   // id or -1
    Q_INVOKABLE bool remove(qint64 id);
    Q_INVOKABLE bool move(qint64 id, const QVariant &newDate);
    Q_INVOKABLE bool setCompleted(qint64 id, bool completed);
    Q_INVOKABLE bool setNotes(qint64 id, const QString &notes);
    Q_INVOKABLE QVariantMap entry(qint64 id) const;   // empty map when not found

    Q_INVOKABLE QVariantList entriesForMonth(int year, int month) const;   // month 1..12
    Q_INVOKABLE QVariantList entriesForDate(const QVariant &date) const;
    // Entries of the visible range of a month calendar (e.g. 6 weeks starting Monday).
    Q_INVOKABLE QVariantList entriesBetween(const QVariant &from, const QVariant &to) const;
    // The next n open (not completed) entries from today on.
    Q_INVOKABLE QVariantList upcoming(int n = 5) const;
    // Marks the first open entry of workoutId on date (default: today) completed.
    // Returns the entry id or -1 when nothing was planned.
    // profileId: rider of the ride (< 0 = active profile).
    Q_INVOKABLE qint64 markCompletedForWorkout(qint64 workoutId, const QVariant &date = QVariant(), qint64 rideId = -1,
                                               qint64 profileId = -1);

    // Routing helper for "start planned workout": the sport of a plan entry
    // ("cycling" or "rowing"). Accepts an entry map (as returned above, uses
    // its "sport" field) or an entry id. Unknown entries -> "cycling".
    //   if (Planner.isRowing(entry)) Rowing.setupPlan(entry.workoutId)
    //   else Training.loadWorkout(entry.workoutId)
    Q_INVOKABLE QString sportFor(const QVariant &entryOrId) const;
    Q_INVOKABLE bool isRowing(const QVariant &entryOrId) const;

    static QDate toDate(const QVariant &value);
    QVariantMap toMap(const PlannedWorkout &e) const;

public slots:
    // Re-emits planChanged (e.g. after workouts were deleted or renamed).
    void refresh();

signals:
    void planChanged();
    void activeEntryChanged();

private:
    QVariantList toList(const QVector<PlannedWorkout> &entries) const;
    Database *m_db;
    ProfileManager *m_profiles;
    int m_revision = 0;
    qint64 m_activeEntryId = -1;
};

} // namespace watt
