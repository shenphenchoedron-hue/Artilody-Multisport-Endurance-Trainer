#pragma once

#include <QDateTime>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QTimer>

class QNetworkReply;

namespace watt {

class ServerSettings;

// Is the server there? (docs/server-plan.md, step 2) While the Server setting
// is on with a valid address (ServerSettings::active()) the station asks the
// server's api/ping.php every kIntervalMs - the server is only switched on for
// training, so it may come and go at any time; the station never waits for it
// and keeps working on its own. Each ping tells the server this station's
// uuid, host name and app version. Exposed to QML as "ServerStatus".
//
// state: "off"          the setting is off (or no valid address)
//        "checking"     the first answer is awaited
//        "connected"    an Artilody server answered (message: a warning, if any)
//        "unreachable"  no answer (switched off, wrong address, no network) or
//                       an HTTP 5xx (the server has a fault)
//        "invalid"      something answered, but not an Artilody server
//
// The ping's contract (header names, the "app" / "api" fields) is described
// in server/public/api/ping.php.
class ServerConnection : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    Q_PROPERTY(QString serverName READ serverName NOTIFY changed)
    Q_PROPERTY(QString serverVersion READ serverVersion NOTIFY changed)
    Q_PROPERTY(bool databaseOk READ databaseOk NOTIFY changed)   // (connected) its database answers
    Q_PROPERTY(QString message READ message NOTIFY changed)      // why unreachable / invalid / a warning
    // the last answer of this server (invalid: none yet); shown while it is unreachable
    Q_PROPERTY(QDateTime lastContact READ lastContact NOTIFY changed)
    Q_PROPERTY(int intervalSeconds READ intervalSeconds CONSTANT)

public:
    // (the server's status page counts a station as "just now" for 3 intervals: index.php)
    static constexpr int kIntervalMs = 30000;
    static constexpr int kTimeoutMs = 5000;
    static constexpr int kApi = 1;   // the server API this app speaks

    ServerConnection(ServerSettings *settings, const QString &appVersion, QObject *parent = nullptr);
    ~ServerConnection() override;

    QString state() const { return m_state; }
    bool connected() const { return m_state == QLatin1String("connected"); }
    QString serverName() const { return m_serverName; }
    QString serverVersion() const { return m_serverVersion; }
    bool databaseOk() const { return m_databaseOk; }
    QString message() const { return m_message; }
    QDateTime lastContact() const { return m_lastContact; }
    static int intervalSeconds() { return kIntervalMs / 1000; }
    // From the last answer (with its database): the server's id and the change
    // numbers of its users / media / rides - UserSync / MediaSync / RideSync
    // fetch them when they differ.
    QString serverId() const { return m_serverId; }
    qint64 usersRev() const { return m_usersRev; }
    qint64 mediaRev() const { return m_mediaRev; }
    qint64 ridesRev() const { return m_ridesRev; }
    QString baseUrl() const { return m_url; }   // "" when off

    // Asks now (Settings > "Check now"); nothing while one is under way or off.
    Q_INVOKABLE void checkNow();
    void setIntervalMs(int ms);   // (tests)

signals:
    void changed();
    // an Artilody server answered (every interval while it is there)
    void answered();

private:
    void onSettingsChanged();
    void onFinished(QNetworkReply *reply);
    void setState(const QString &state, const QString &message = {});
    void abortPending();

    ServerSettings *m_settings;
    QString m_appVersion;
    QNetworkAccessManager m_net;
    QTimer m_timer;
    QPointer<QNetworkReply> m_reply;
    QString m_url;   // the URL being watched ("" when off)
    QString m_state = QStringLiteral("off");
    QString m_serverName, m_serverVersion, m_message;
    bool m_databaseOk = false;
    QDateTime m_lastContact;
    QString m_serverId;
    qint64 m_usersRev = -1;
    qint64 m_mediaRev = -1;
    qint64 m_ridesRev = -1;
};

} // namespace watt
