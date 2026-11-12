#include "LiveRide.h"

#include "DiagLog.h"
#include "ServerConnection.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include <algorithm>
#include <cmath>

namespace watt {

LiveRide::LiveRide(const QString &sport, const QString &stationId, ServerConnection *connection, QObject *parent)
    : QObject(parent)
    , m_sport(sport)
    , m_station(stationId)
{
    m_timer.setInterval(kIntervalMs);
    connect(&m_timer, &QTimer::timeout, this, &LiveRide::askNow);
    if (connection) {
        connect(connection, &ServerConnection::changed, this, [this, connection] {
            setServer(connection->connected() ? connection->baseUrl() : QString());
        });
    }
}

LiveRide::~LiveRide()
{
    if (m_reply) {   // (its finished() is ignored: this object is going)
        QNetworkReply *r = m_reply;
        m_reply = nullptr;
        r->abort();
    }
}

void LiveRide::setServer(const QString &baseUrl)
{
    if (baseUrl == m_url)
        return;
    m_url = baseUrl;
    if (m_url.isEmpty()) {
        m_timer.stop();
        clear();
    } else if (!m_timer.isActive()) {
        m_timer.start();
    }
}

void LiveRide::clear()
{
    m_wasRiding = false;
    if (!m_riders.isEmpty()) {
        m_riders.clear();
        emit changed();
    }
}

// metres the other is ahead (+) or behind (-), along the way the rider goes
// (a there-and-back loop: out then back; a closed loop: the short way round)
double LiveRide::gap(const Me &me, double distance, bool returning)
{
    const double L = me.routeLength;
    const auto along = [&](double d, bool back) { return back && L > 0 ? 2 * L - d : d; };
    double g = along(distance, returning) - along(me.distanceM, me.returning);
    if (me.loop && L > 0) {
        const double cycle = (returning || me.returning) ? 2 * L : L;
        g = std::remainder(g, cycle);
    }
    return g;
}

QVariantList LiveRide::riders() const
{
    QVariantList out;
    for (const Rider &r : m_riders)
        out.append(QVariantMap {
            { QStringLiteral("name"), r.name },
            { QStringLiteral("userUuid"), r.userUuid },
            { QStringLiteral("distanceM"), r.distanceM },
            { QStringLiteral("returning"), r.returning },
            { QStringLiteral("speedKmh"), r.speedKmh },
            { QStringLiteral("watts"), r.watts },
            { QStringLiteral("gapM"), r.gapM },
        });
    return out;
}

void LiveRide::askNow()
{
    if (m_sinceAnswer.isValid() && m_sinceAnswer.elapsed() > kStaleMs)
        clear();   // (the server stopped answering: they are not shown standing still)
    if (m_url.isEmpty() || !m_source || m_reply)
        return;
    const Me me = m_source();
    // no route: nothing to ask - unless this rider was shown (then it is removed once)
    if ((!me.route || me.routeKey.isEmpty()) && !m_wasRiding) {
        clear();
        return;
    }
    QJsonObject body {
        { QStringLiteral("station"), m_station },
        { QStringLiteral("sport"), m_sport },
        { QStringLiteral("routeKey"), me.routeKey.isEmpty() ? QStringLiteral("-") : me.routeKey },
        { QStringLiteral("riding"), me.route && me.riding },
    };
    if (me.route && me.riding) {
        body.insert(QStringLiteral("userUuid"), me.userUuid);
        body.insert(QStringLiteral("name"), me.name);
        body.insert(QStringLiteral("distanceM"), std::round(me.distanceM * 10) / 10);
        body.insert(QStringLiteral("returning"), me.returning);
        body.insert(QStringLiteral("speedKmh"), std::round(me.speedKmh * 10) / 10);
        body.insert(QStringLiteral("watts"), me.watts);
    }
    m_wasRiding = me.route && me.riding;
    QNetworkRequest req(QUrl(m_url + QStringLiteral("api/live.php")));
    req.setTransferTimeout(kTimeoutMs);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArray("application/json"));
    req.setRawHeader("X-Artilody-Station", m_station.toLatin1());   // (the server refuses POSTs without it)
    QNetworkReply *reply = m_net.post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    m_reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, me] { onReply(reply, me); });
}

void LiveRide::onReply(QNetworkReply *reply, const Me &me)
{
    reply->deleteLater();
    if (reply != m_reply)
        return;
    m_reply = nullptr;
    if (reply->error() != QNetworkReply::NoError)
        return;   // (tried again in a second; cleared when it goes on failing)
    m_sinceAnswer.restart();
    if (!me.route) {
        clear();
        return;
    }
    QVector<Rider> riders;
    for (const QJsonValue &v : QJsonDocument::fromJson(reply->readAll()).object().value(QStringLiteral("riders")).toArray()) {
        const QJsonObject o = v.toObject();
        Rider r;
        r.station = o.value(QStringLiteral("station")).toString();
        r.userUuid = o.value(QStringLiteral("userUuid")).toString();
        r.name = o.value(QStringLiteral("name")).toString().remove(QLatin1Char('<')).remove(QLatin1Char('>')).simplified().left(40);
        if (r.name.isEmpty())
            r.name = QStringLiteral("Rider");
        r.returning = o.value(QStringLiteral("returning")).toBool();
        r.speedKmh = std::clamp(o.value(QStringLiteral("speedKmh")).toDouble(), 0.0, 200.0);
        r.watts = o.value(QStringLiteral("watts")).toInt();
        // where it is now: its report moved on by its speed for the report's age
        const double age = std::clamp(o.value(QStringLiteral("ageMs")).toDouble(), 0.0, double(kStaleMs)) / 1000.0;
        double d = o.value(QStringLiteral("distanceM")).toDouble() + (r.returning ? -1 : 1) * r.speedKmh / 3.6 * age;
        if (me.routeLength > 0)
            d = me.loop ? d : std::clamp(d, 0.0, me.routeLength);
        r.distanceM = d;
        r.gapM = gap(me, r.distanceM, r.returning);
        riders.append(r);
    }
    std::sort(riders.begin(), riders.end(), [](const Rider &a, const Rider &b) { return std::abs(a.gapM) < std::abs(b.gapM); });
    if (riders.size() > kMaxRiders)
        riders.resize(kMaxRiders);
    const bool joined = riders.size() != m_riders.size();
    if (joined)
        DiagLog::write("LIVE", QStringLiteral("%1: %2 other rider(s) on the route").arg(m_sport).arg(riders.size()));
    m_riders = riders;
    emit changed();
}

} // namespace watt
