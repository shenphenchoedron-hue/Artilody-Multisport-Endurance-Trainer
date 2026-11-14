#pragma once

#include <QElapsedTimer>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>
#include <functional>

class QNetworkReply;

namespace watt {

class ServerConnection;

// Riding together (docs/server-plan.md, step 6): the other stations' riders on
// the same route, live - cyclists and runners alike (a route is its GPX
// track: the same file in GPX Ride and in Treadmill Run). One per session that moves along a GPX route - the
// GPX Ride (sport "gpx") and the Treadmill Run ("run"). While the server is
// there and a route is chosen it asks api/live.php about once a second,
// telling where this rider is (while riding: the others see it) and getting
// the others on the same route (sport and route key) - each where it really
// is on the route. The session draws them in 3D (RouteProgressSource::
// liveRiders()) and QML lists them (the session's "live").
//
// riders: [{ name, distanceM, returning, speedKmh, watts, gapM (+ ahead of
//            you), lapsAhead, userUuid, jersey ("#rrggbb" / ""), runner }] - nearest first
class LiveRide : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList riders READ riders NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY changed)
    // the session's page is open (QML): this rider is shown to the others as
    // soon as a route is chosen there - waiting at its start - not only riding
    Q_PROPERTY(bool present READ present WRITE setPresent NOTIFY presentChanged)

public:
    static constexpr int kMaxRiders = 6;          // drawn at most (the nearest)
    static constexpr int kIntervalMs = 1000;
    static constexpr int kTimeoutMs = 3000;
    static constexpr int kStaleMs = 6000;         // no answer for this long: the others are gone

    // What this station says about itself (the session, each ask).
    struct Me
    {
        bool route = false;          // a route is chosen (else: nothing asked)
        QString routeKey;
        bool riding = false;         // on the route: a ride runs / is paused, or its page is open (shown to the others)
        double distanceM = 0;        // place on the route
        bool returning = false;
        double speedKmh = 0;
        int watts = 0;
        QString userUuid, name;
        double routeLength = 0;      // (the gap on a loop: the short way round)
        bool loop = false;
        QVariantMap look;            // the rider's 3D figure and colours (ProfileManager::avatar3d / runner3d)
        int lap = 1;                 // the lap being ridden (a loop; out and back: each way is a lap)
        bool roundTrip = true;       // a loop: round and round (else out and back)
    };
    struct Rider
    {
        QString station, userUuid, name;
        double distanceM = 0;        // where it is now (extrapolated from its last report)
        bool returning = false;
        double speedKmh = 0;
        int watts = 0;
        double gapM = 0;             // ahead of this rider (+) / behind (-), the short way round on a loop
        int lapsAhead = 0;           // a loop: whole laps ahead (+) / behind (-) of this rider
        QVariantMap look;            // its 3D figure and colours (drawn so)
        bool runner = false;         // on a treadmill ("run") - else on a bike ("gpx"): drawn so
    };

    LiveRide(const QString &sport, const QString &stationId, ServerConnection *connection, QObject *parent = nullptr);
    ~LiveRide() override;

    void setSource(std::function<Me()> source) { m_source = std::move(source); }

    QVariantList riders() const;
    int count() const { return int(m_riders.size()); }
    const QVector<Rider> &list() const { return m_riders; }   // nearest first, at most kMaxRiders

    void setServer(const QString &baseUrl);   // "" = none
    bool present() const { return m_present; }
    void setPresent(bool on);
    void askNow();                            // (the timer; tests)
    bool asking() const { return !m_reply.isNull(); }   // a question under way (tests)

signals:
    void changed();
    void presentChanged();

private:
    void onReply(QNetworkReply *reply, const Me &me);
    void clear();
    // ahead (+) / behind (-): the short way round, and whole laps (a loop)
    static void gap(const Me &me, double distance, bool returning, int lap, double *shortWay, int *laps);

    QString m_sport, m_station;
    std::function<Me()> m_source;
    QNetworkAccessManager m_net;
    QPointer<QNetworkReply> m_reply;
    QTimer m_timer;
    QString m_url;
    QVector<Rider> m_riders;
    QElapsedTimer m_sinceAnswer;
    bool m_wasRiding = false;
    bool m_present = false;
};

} // namespace watt
