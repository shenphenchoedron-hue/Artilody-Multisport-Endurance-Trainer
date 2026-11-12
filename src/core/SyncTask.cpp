#include "SyncTask.h"

#include "Database.h"
#include "DiagLog.h"
#include "ServerConnection.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace watt {

SyncTask::SyncTask(Database *db, const QString &what, const QString &settingsPrefix, int timeoutMs, QObject *parent)
    : QObject(parent)
    , m_db(db)
    , m_what(what)
    , m_idKey(settingsPrefix + QStringLiteral("ServerId"))
    , m_revKey(settingsPrefix + QStringLiteral("Rev"))
    , m_timeoutMs(timeoutMs)
{
    m_soon.setSingleShot(true);
    connect(&m_soon, &QTimer::timeout, this, [this] {
        if (serverThere())
            start();
    });
}

SyncTask::~SyncTask()
{
    if (m_reply)
        m_reply->abort();
}

void SyncTask::follow(ServerConnection *connection, RevOf revOf)
{
    if (!connection)
        return;
    connect(connection, &ServerConnection::answered, this, [this, connection, revOf] {
        serverAnswered(connection->baseUrl(), connection->serverId(), revOf(connection));
    });
    connect(connection, &ServerConnection::changed, this, [this, connection] {
        if (!connection->connected())
            serverGone();
    });
}

void SyncTask::refreshWaiting()
{
    const int n = countWaiting();
    if (n != m_waiting) {
        m_waiting = n;
        emit changed();
    }
}

void SyncTask::setState(const QString &state, const QString &message)
{
    if (state == m_state && message == m_message)
        return;
    if (state == QLatin1String("error") && message != m_message)
        DiagLog::write("SYNC", QStringLiteral("%1: %2").arg(m_what, message));
    m_state = state;
    m_message = message;
    emit changed();
}

qint64 SyncTask::storedRev() const
{
    return m_db->setting(m_revKey, 0).toLongLong();
}

void SyncTask::storeRev(qint64 rev)
{
    // (a fetch from the start was asked for during this round: that wins)
    m_db->setSetting(m_revKey, m_restartFetch ? 0 : rev);
}

void SyncTask::restartFetch()
{
    m_db->setSetting(m_revKey, 0);
    if (m_running) {
        m_restartFetch = true;   // (the round under way must not store its rev over it)
        m_again = true;
    } else if (serverThere()) {
        soon(2000);
    }
}

void SyncTask::soon(int ms)
{
    if (!serverThere())
        return;
    m_soon.setInterval(ms);
    m_soon.start();
}

void SyncTask::serverGone()
{
    m_url.clear();
    m_soon.stop();
    if (m_reply) {   // (the round ends quietly: finish() from the aborted request)
        QNetworkReply *r = m_reply;
        m_reply = nullptr;
        r->abort();
        r->deleteLater();
    }
    if (m_running)
        finish();
    else
        setState(QStringLiteral("off"));
}

void SyncTask::serverAnswered(const QString &baseUrl, const QString &serverId, qint64 rev)
{
    if (baseUrl.isEmpty() || serverId.isEmpty())
        return;
    m_url = baseUrl;
    m_serverId = serverId;
    m_serverRev = rev;
    if (m_state == QLatin1String("off"))
        setState(QStringLiteral("idle"));
    // something to send, another server, or something new there
    const bool newServer = m_db->setting(m_idKey).toString() != serverId;
    const bool news = rev < 0 || rev != m_db->setting(m_revKey, -1).toLongLong();
    if (newServer || news || countWaiting() > 0 || m_again)
        start();
}

void SyncTask::start()
{
    if (m_running) {
        m_again = true;
        return;
    }
    if (busy() || !serverThere()) {
        m_again = true;   // (after the ride: the next answer)
        return;
    }
    m_running = true;
    m_again = false;
    m_restartFetch = false;
    setState(QStringLiteral("syncing"));
    if (m_db->setting(m_idKey).toString() != m_serverId) {
        // another server (new / reinstalled): it gets everything recorded here
        DiagLog::write("SYNC", QStringLiteral("%1: server %2 - sending all").arg(m_what, m_serverId));
        resendAll();
        m_db->setSetting(m_revKey, 0);
        m_db->setSetting(m_idKey, m_serverId);
    }
    round();
}

QString SyncTask::errorOf(QNetworkReply *reply)
{
    const QString why = QJsonDocument::fromJson(reply->readAll()).object().value(QStringLiteral("error")).toString();
    return QStringLiteral("%1: %2").arg(reply->url().path(), why.isEmpty() ? reply->errorString() : why);
}

void SyncTask::request(const QString &method, const QString &path, const QByteArray &body, const QByteArray &type, Done done)
{
    if (!serverThere()) {   // (gone meanwhile: the round ends quietly)
        finish();
        return;
    }
    QNetworkRequest req(QUrl(m_url + path));
    req.setTransferTimeout(m_timeoutMs);
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    // (every request names the station: the server refuses POSTs without it - no browser page can send them)
    req.setRawHeader("X-Artilody-Station", m_db->stationId().toLatin1());
    if (!type.isEmpty())
        req.setHeader(QNetworkRequest::ContentTypeHeader, type);
    QNetworkReply *reply = method == QLatin1String("POST") ? m_net.post(req, body) : m_net.get(req);
    m_reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, done] {
        reply->deleteLater();
        if (reply != m_reply)
            return;
        m_reply = nullptr;
        if (reply->error() != QNetworkReply::NoError) {
            finish(errorOf(reply));
            return;
        }
        done(reply);
    });
}

void SyncTask::finish(const QString &error)
{
    if (!m_running)
        return;
    m_running = false;
    roundEnded();
    refreshWaiting();
    if (!serverThere())
        setState(QStringLiteral("off"));
    else if (error.isEmpty())
        setState(QStringLiteral("idle"));
    else
        setState(QStringLiteral("error"), error);
    if (error.isEmpty())
        emit synced();
    if (m_again && serverThere())
        QTimer::singleShot(0, this, &SyncTask::start);
}

} // namespace watt
