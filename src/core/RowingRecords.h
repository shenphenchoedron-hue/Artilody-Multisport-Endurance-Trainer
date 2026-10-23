#pragma once

#include "PlanTypes.h"

#include <QHash>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <optional>

namespace watt {

class Database;
class ProfileManager;

// Personal rowing bests per distance of the active profile, used for ghost
// races. Exposed to QML as "RowingRecords".
//
// A trace is a list of time-stamped distances [t (s), d (m)], sampled about
// once per second, starting at [0, 0]. In QML a trace is a list of 2-element
// lists: [[0,0],[1,4.2],...]. Stored as compact JSON text (0.1 s / 0.1 m).
class RowingRecords : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QList<int> standardDistances READ standardDistances CONSTANT)
    Q_PROPERTY(int revision READ revision NOTIFY recordsChanged)
public:
    RowingRecords(Database *db, ProfileManager *profiles, QObject *parent = nullptr);

    static QList<int> standardDistances() { return { 500, 1000, 2000, 5000, 6000, 10000 }; }
    int revision() const { return m_revision; }

    // { distanceM, timeSec, pace500Sec, trace, rideId, createdAt, recordId } or {} when none.
    Q_INVOKABLE QVariantMap bestFor(int distanceM) const;
    // Saves the piece when it beats the stored best (or none exists). Returns true for a new record.
    // rideId: saved ride of the piece; unknown ids are stored as "no ride".
    // profileId: rider of the piece (< 0 = active profile). Implausible times
    // (faster than 1:00 /500 m, see plausibleTime()) are never saved.
    Q_INVOKABLE bool saveIfBest(int distanceM, double timeSec, const QVariantList &trace, qint64 rideId = -1);
    bool saveIfBest(int distanceM, double timeSec, const QVector<TracePoint> &trace, qint64 rideId = -1,
                    qint64 profileId = -1);
    // One map per standard distance (hasRecord false when not rowed yet) plus
    // every custom distance that has a record, ordered by distance:
    // { distanceM, hasRecord, timeSec, pace500Sec, rideId, createdAt, standard }
    Q_INVOKABLE QVariantList allBests() const;
    // Ghost position (m) of the best piece over distanceM after t seconds (0 when no record).
    Q_INVOKABLE double ghostDistance(int distanceM, double t) const;

    // Best piece of profileId (< 0 = active profile).
    std::optional<RowingRecord> best(int distanceM, qint64 profileId = -1) const;

    // False for a pace faster than 1:00 /500 m (0.12 s per metre), which no
    // human rows: such a time comes from bad monitor data.
    static bool plausibleTime(int distanceM, double timeSec)
    {
        return distanceM > 0 && timeSec >= distanceM * 0.12;
    }

    // Linear interpolation of a trace (sorted by t). Before the first sample
    // it interpolates from [0, 0]; after the last sample it returns the final
    // distance (the ghost has finished). Empty trace -> 0.
    static double ghostDistanceAt(const QVector<TracePoint> &trace, double t);
    // Seconds per 500 m.
    static double pace500(int distanceM, double timeSec);
    static QVector<TracePoint> traceFromVariant(const QVariantList &list);
    static QVariantList traceToVariant(const QVector<TracePoint> &trace);

signals:
    void recordsChanged();
    void newRecord(int distanceM, double timeSec, double previousTimeSec);   // previousTimeSec 0 = first record

private:
    Database *m_db;
    ProfileManager *m_profiles;
    int m_revision = 0;
    mutable QHash<int, QVector<TracePoint>> m_traceCache;   // ghost traces of the active profile
};

} // namespace watt
