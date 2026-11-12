#pragma once

#include "RouteRival.h"

#include <QVariantList>

namespace watt {

class Database;

// The ghost of the GPX Ride and the Treadmill Run: race an earlier ride on
// the same route - one of your own or (another profile's, with a server from
// any station) another rider's. Owned by the session (RlvSession, Kind::Gpx:
// sport "gpx"; RunSession: sport "run") and exposed to QML as its "ghost".
// (The pacer - a rider at a set W/kg - is separate: RoutePacer. One rival at a
// time: RlvSession turns the pacer off when a ghost is chosen, and back.)
//
// Recording: while the session runs it calls advance(t, covered) - the
// session's riding time (s, pauses excluded) and the metres covered since the
// start (all laps). One point per second is kept; when the ride is saved,
// saveRecording() stores it as a RouteTrace linked to the ride (deleted with
// it). A discarded ride's recording is dropped.
//
// Racing: the traces of the active profile on the same route (sprint::
// routeKey), ridden from the same start (within 5 m) in the same way (loop or
// not), are the choices: "off", "best", "latest" or one ride ("id:<trace id>").
// off / best / latest is kept in the settings ("<sport>/ghost"); a chosen ride
// only until the route changes. "best": the fastest completed ride (a loop:
// the highest average speed). The other profiles' rides on the route (with a
// server: from every station) follow one's own as choices too; best and
// latest are always one's own.
class RouteGhost : public RouteRival
{
    Q_OBJECT
    Q_PROPERTY(QString choice READ choice WRITE setChoice NOTIFY choicesChanged)
    // [{ value, label }] for a combo box: Off, Best, Latest, then every ride (newest first)
    Q_PROPERTY(QVariantList choices READ choices NOTIFY choicesChanged)
    Q_PROPERTY(int choiceIndex READ choiceIndex NOTIFY choicesChanged)   // in choices
    Q_PROPERTY(int rideCount READ rideCount NOTIFY choicesChanged)        // earlier rides to race (anyone's)

public:
    struct Context
    {
        qint64 profileId = -1;
        QString routeKey;       // "" = no route: no ghost
        QString routeName;
        double startM = 0;
        bool loop = false;
    };

    static constexpr double kMinTraceSec = 30.0;   // shorter recordings are never saved

    RouteGhost(Database *db, const QString &sport, QObject *parent = nullptr);

    // The route / start / loop / profile: the choices again; the ghost too
    // unless a ride is being recorded.
    void setContext(const Context &context);
    // Reloads the choices (a ride was saved) and keeps the loaded ghost.
    void refreshChoices();

    QString choice() const { return m_choice; }
    void setChoice(const QString &choice);
    QVariantList choices() const { return m_choices; }
    int choiceIndex() const;
    int rideCount() const { return m_candidates.size() + m_others.size(); }

    QString ghostLabel() const override;

    // The ride: begin() at the start (the choice is resolved again: "best"
    // may be a newer ride now), advance() while it runs, then
    // saveRecording() / discardRecording(); reset() back to the start.
    void begin();
    // Saves the recording linked to rideId (>= 0, at least kMinTraceSec).
    // Returns the trace id or -1.
    qint64 saveRecording(qint64 rideId, bool completed);
    void discardRecording();
    void reset();

signals:
    void choicesChanged();

protected:
    void advanced() override;   // records one point per second

private:
    void loadCandidates();
    void rebuildChoices();
    void resolve();                    // m_choice -> the trace
    qint64 bestId() const;
    const RouteTrace *findTrace(qint64 id) const;   // one's own or another's
    QString settingKey() const { return m_sport + QStringLiteral("/ghost"); }
    QString describe(const RouteTrace &t) const;

    Database *m_db;
    QString m_sport;
    Context m_context;
    QString m_choice = QStringLiteral("best");
    QVector<RouteTrace> m_candidates;   // newest first, without traces
    QVector<RouteTrace> m_others;       // the other profiles' (riderName set), newest first
    QVariantList m_choices;
    QString m_traceKind;                // "Best" / "Latest" / another rider's name / "" (a chosen ride of one's own)

    bool m_recording = false;
    QVector<TracePoint> m_recorded;
};

} // namespace watt
