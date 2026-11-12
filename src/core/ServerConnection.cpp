#include "ServerConnection.h"

#include "DiagLog.h"
#include "ServerSettings.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSysInfo>
#include <QUrl>

namespace watt {

ServerConnection::ServerConnection(ServerSettings *settings, const QString &appVersion, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_appVersion(appVersion)
{
    m_timer.setInterval(kIntervalMs);
    connect(&m_timer, &QTimer::timeout, this, &ServerConnection::checkNow);
    if (m_settings)
        connect(m_settings, &ServerSettings::changed, this, &ServerConnection::onSettingsChanged);
    onSettingsChanged();
}

ServerConnection::~ServerConnection()
{
    abortPending();
}

void ServerConnection::setIntervalMs(int ms)
{
    m_timer.setInterval(std::max(10, ms));
}

void ServerConnection::abortPending()
{
    if (m_reply) {
        QNetworkReply *r = m_reply;
        m_reply = nullptr;   // (its finished() is ignored)
        r->abort();
        r->deleteLater();
    }
}

void ServerConnection::onSettingsChanged()
{
    const QString url = m_settings && m_settings->active() ? m_settings->url() : QString();
    if (url == m_url)
        return;
    m_url = url;
    abortPending();
    m_serverName.clear();
    m_serverVersion.clear();
    m_databaseOk = false;
    m_lastContact = {};
    m_serverId.clear();
    m_usersRev = -1;
    m_mediaRev = -1;
    m_ridesRev = -1;
    if (m_url.isEmpty()) {
        m_timer.stop();
        setState(QStringLiteral("off"));
        return;
    }
    setState(QStringLiteral("checking"));
    m_timer.start();
    checkNow();
}

void ServerConnection::checkNow()
{
    if (m_url.isEmpty() || m_reply)
        return;
    QNetworkRequest req(QUrl(m_url + QStringLiteral("api/ping.php")));
    req.setTransferTimeout(kTimeoutMs);
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    req.setRawHeader("Accept", "application/json");
    req.setRawHeader("User-Agent", QStringLiteral("Artilody/%1").arg(m_appVersion).toUtf8());
    if (m_settings)
        req.setRawHeader("X-Artilody-Station", m_settings->stationId().toLatin1());
    req.setRawHeader("X-Artilody-Name", QSysInfo::machineHostName().left(100).toUtf8());
    req.setRawHeader("X-Artilody-Version", m_appVersion.left(40).toUtf8());
    QNetworkReply *reply = m_net.get(req);
    m_reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] { onFinished(reply); });
}

void ServerConnection::onFinished(QNetworkReply *reply)
{
    reply->deleteLater();
    if (reply != m_reply)
        return;   // (aborted: the setting changed)
    m_reply = nullptr;

    if (reply->error() != QNetworkReply::NoError) {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status >= 500)   // the server is there, but has a fault (e.g. a PHP error)
            setState(QStringLiteral("unreachable"), QStringLiteral("HTTP %1 – the server has a problem").arg(status));
        else if (status > 0)   // something answered: a web server, but no Artilody API there
            setState(QStringLiteral("invalid"), QStringLiteral("HTTP %1 – not an Artilody server").arg(status));
        else
            setState(QStringLiteral("unreachable"), reply->error() == QNetworkReply::OperationCanceledError
                                                        ? QStringLiteral("no answer within %1 s").arg(kTimeoutMs / 1000)
                                                        : reply->errorString());
        return;
    }
    const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
    if (o.value(QStringLiteral("app")).toString() != QLatin1String("artilody-server")
        || o.value(QStringLiteral("api")).toInt() < 1) {
        setState(QStringLiteral("invalid"), QStringLiteral("it answers, but is not an Artilody server"));
        return;
    }
    // (shown as plain text; cut so a long one cannot fill the dialog)
    m_serverName = o.value(QStringLiteral("name")).toString().simplified().left(80);
    m_serverVersion = o.value(QStringLiteral("version")).toString().simplified().left(20);
    m_databaseOk = o.value(QStringLiteral("db")).toBool();
    m_lastContact = QDateTime::currentDateTime();
    m_serverId = o.value(QStringLiteral("serverId")).toString().left(64);
    m_usersRev = o.value(QStringLiteral("usersRev")).toInteger(-1);
    m_mediaRev = o.value(QStringLiteral("mediaRev")).toInteger(-1);
    m_ridesRev = o.value(QStringLiteral("ridesRev")).toInteger(-1);
    const int api = o.value(QStringLiteral("api")).toInt();
    // the database answers, but this station was not recorded: the request
    // and the server disagree (headers, lengths) - the next steps need it
    const bool recorded = o.value(QStringLiteral("station")).toBool();
    setState(QStringLiteral("connected"),
             !m_databaseOk ? QStringLiteral("the server's database does not answer")
             : !recorded   ? QStringLiteral("the server did not record this station (see its error log)")
             : api > kApi  ? QStringLiteral("the server is newer than this app – update the app")
                           : QString());
    if (m_databaseOk && !m_serverId.isEmpty())
        emit answered();
}

void ServerConnection::setState(const QString &state, const QString &message)
{
    if (state != m_state)
        DiagLog::write("SERVER", QStringLiteral("%1 %2%3").arg(state, m_url,
                                                             message.isEmpty() ? QString() : QStringLiteral(": ") + message));
    // (the server name, version, db and time may change with the same state)
    m_state = state;
    m_message = message;
    emit changed();
}

} // namespace watt
