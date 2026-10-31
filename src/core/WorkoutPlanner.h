#pragma once

#include "PlanTypes.h"

#include <QDate>
#include <QObject>
#include <QPointer>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <functional>

namespace watt {

class Database;
class FreeRideSession;
class ProfileManager;
class RlvLibrary;
class RlvSession;
class TrainingSession;

// Training plan / calendar of the active profile. Exposed to QML as "Planner".
//
// Dates may be passed as "yyyy-MM-dd" strings, QDate or JS Date values (the
// local calendar day is used). Entries are returned as maps:
//   { id, date ("yyyy-MM-dd"), kind ("workout" | "freeride" | "rlv"),
//     workoutId, name, sport, category, durationSec, completed,
//     rideId (-1 = none), position, notes, isPast, isToday,
//     routePath, routeName, routeDistanceM, routeAscentM, routeAvailable,
//     plannedDurationSec, options (map), workoutName }
//   kind "workout":  as before (workoutId/name/sport/category/durationSec of the workout).
//   kind "freeride": workoutId -1, name "Free Ride · 60 min" ("Free Ride" without a
//                    duration), category "Free Ride", durationSec = plannedDurationSec,
//                    options { mode: "ride" | "erg", ergWatts (0 = the Free Ride's own target) }.
//   kind "rlv":      workoutId = the workout to ride during the video (-1 = none),
//                    name "Video: <route> · 12.0 km" or "Video: <route> + <workout>",
//                    category "Video Ride", durationSec = workout duration when one
//                    is attached, else the route's video duration (0 = unknown),
//                    route* from the video library (Videos) when it knows the route
//                    (routeAvailable), else the cached name and 0,
//                    options { mode: "sim" | "erg", difficulty 0..100, ergWatts
//                    (0 = Rlv's own target), workoutId, workoutErg (bool) }.
//   sport is "cycling" for Free Ride and Video Ride entries.
// Workout entries of deleted workouts are removed with the workout and never
// returned; a Video Ride entry whose attached workout was deleted keeps the
// route (workoutId -1).
//
// Starting an entry: startEntry() prepares the matching session (sessions
// injected with setSessions() / setRowingStarter(), main.cpp) and returns
// the page to show ("dashboard", "rowing", "freeride", "video"; "" + lastError
// on failure).
//
// Completion (the active entry, i.e. the one started from the calendar):
//   workout:  a completed ride of that workout (completeForRide()).
//   freeride: a saved Free Ride of >= 50 % of the planned duration, or of
//             >= kMinFreeRideSec (10 min) when no duration was planned
//             (completeFreeRide()).
//   rlv:      a saved Video Ride of the entry's route that reached the route
//             end (Rlv.completed) or rode >= 90 % of the route distance
//             (completeVideoRide()); an attached workout does not change this.
// Rides that do not qualify leave the entry open and active. Deleting the
// ride reopens the entry (Database::deleteRide()), for every kind.
class WorkoutPlanner : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY planChanged)   // bumps on every change (for QML bindings)
    // Plan entry started from the calendar (-1 = none), see setActiveEntry().
    Q_PROPERTY(qint64 activeEntryId READ activeEntryId NOTIFY activeEntryChanged)
    // Why the last add*/startEntry() call failed ("" after a success).
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
public:
    static constexpr int kDefaultFreeRideMin = 60;
    static constexpr int kMaxFreeRideMin = 24 * 60;
    static constexpr int kMinFreeRideSec = 10 * 60;            // no planned duration
    static constexpr double kFreeRideDoneShare = 0.5;          // of the planned duration
    static constexpr double kVideoRideDoneShare = 0.9;         // of the route distance

    WorkoutPlanner(Database *db, ProfileManager *profiles, QObject *parent = nullptr);

    // Sessions startEntry() prepares (any may be null: that kind cannot be
    // started) and the video library for route lookups.
    void setSessions(TrainingSession *training, FreeRideSession *freeRide, RlvSession *rlv, RlvLibrary *videos);
    // Rowing lives in another library: main.cpp passes Rowing.setupPlan().
    void setRowingStarter(std::function<bool(qint64 workoutId)> starter) { m_rowingStarter = std::move(starter); }
    // running workouts: attaches the workout to the treadmill run (page "treadmill")
    void setRunningStarter(std::function<bool(qint64 workoutId)> starter) { m_runningStarter = std::move(starter); }

    int revision() const { return m_revision; }
    qint64 activeEntryId() const { return m_activeEntryId; }
    QString lastError() const { return m_lastError; }

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

    // Completion hooks for saved Free Ride / Video Ride rides (main.cpp, see
    // the class comment). Only the active entry of that kind is completed
    // (and the active entry cleared). Return the entry id or -1.
    qint64 completeFreeRide(qint64 rideId, int riddenSec, qint64 profileId = -1);
    qint64 completeVideoRide(qint64 rideId, const QString &routePath, bool routeCompleted, double distanceShare,
                             qint64 profileId = -1);
    static bool freeRideQualifies(int riddenSec, int plannedSec);
    static bool videoRideQualifies(bool routeCompleted, double distanceShare);

    Q_INVOKABLE qint64 addWorkout(const QVariant &date, qint64 workoutId, const QString &notes = QString());   // id or -1
    // Free Ride entry: durationMin 0 = open; options { mode: "ride"|"erg", ergWatts }.
    Q_INVOKABLE qint64 addFreeRide(const QVariant &date, int durationMin = kDefaultFreeRideMin,
                                   const QVariantMap &options = QVariantMap(), const QString &notes = QString());
    // Video Ride entry: routePath = route folder, .xml or .gpx (file:// URLs ok);
    // options { mode: "sim"|"erg", difficulty, ergWatts, workoutId (-1), workoutErg (true) }.
    // The route must be in the video library or loadable; the workout (if
    // any) must be a cycling workout with intervals. id or -1 (lastError).
    Q_INVOKABLE qint64 addVideoRide(const QVariant &date, const QString &routePath,
                                    const QVariantMap &options = QVariantMap(), const QString &notes = QString());
    // Edit a Free Ride / Video Ride entry: options are merged into the stored
    // ones and validated like add*(); durationMin < 0 keeps the duration.
    Q_INVOKABLE bool updateEntry(qint64 id, const QVariantMap &options, int durationMin = -1);
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
    // "workout", "freeride" or "rlv" (entry map or id; unknown -> "workout").
    Q_INVOKABLE QString kindFor(const QVariant &entryOrId) const;

    // Prepares the session of an entry (map or id) and makes it the active
    // entry. Returns the page to show:
    //   workout (cycling) -> Training.loadWorkout()                 "dashboard"
    //   workout (rowing)  -> Rowing.setupPlan()                     "rowing"
    //   freeride          -> FreeRide mode / ERG watts / plannedDurationSec  "freeride"
    //   rlv               -> Rlv.selectPath(route), mode, difficulty, ERG watts,
    //                        attachWorkout()/detachWorkout(), workoutErg  "video"
    // "" when it cannot be prepared (lastError, e.g. "Route not found: <name>",
    // a session of that kind is running); the active entry is unchanged then.
    // A Video Ride whose attached workout is gone starts without it (lastError
    // says so, the page is still returned).
    Q_INVOKABLE QString startEntry(const QVariant &entryOrId);

    static QDate toDate(const QVariant &value);
    QVariantMap toMap(const PlannedWorkout &e) const;

public slots:
    // Re-emits planChanged (e.g. after workouts were deleted or renamed).
    void refresh();

signals:
    void planChanged();
    void activeEntryChanged();
    void lastErrorChanged();

private:
    QVariantList toList(const QVector<PlannedWorkout> &entries) const;
    QVariantMap entryMapFor(const QVariant &entryOrId) const;
    void setError(const QString &e);
    // validated options JSON for a kind ("" + lastError when invalid)
    bool normalizeOptions(const QString &kind, const QVariantMap &in, QVariantMap *out);
    struct RouteInfo
    {
        bool found = false;      // in the library, or loadable
        bool inLibrary = false;
        QString path;            // route file
        QString name;
        double distanceM = 0;
        double ascentM = 0;
        int durationSec = 0;
    };
    RouteInfo lookupRoute(const QString &path, bool allowLoad) const;
    static bool samePath(const QString &a, const QString &b);
    void activeEntryReset(qint64 previous);
    QString startVideo(const PlannedWorkout &e, const QVariantMap &opts);

    Database *m_db;
    ProfileManager *m_profiles;
    QPointer<TrainingSession> m_training;
    QPointer<FreeRideSession> m_freeRide;
    QPointer<RlvSession> m_rlv;
    QPointer<RlvLibrary> m_videos;
    std::function<bool(qint64)> m_rowingStarter;
    std::function<bool(qint64)> m_runningStarter;
    int m_revision = 0;
    qint64 m_activeEntryId = -1;
    QString m_lastError;
};

} // namespace watt
