#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <functional>

class QNetworkReply;

namespace watt {

class Database;
class ServerConnection;

// What UserSync and RideSync share (docs/server-plan.md, steps 3 and 5): a
// round against the server whenever it answers - send what is waiting here,
// then fetch what changed there since the last look (its change number,
// kept in the settings with the server's id: another server - new or
// reinstalled - gets everything again). One round at a time; asked again
// while one runs, another follows. The server gone during a round: the round
// ends quietly ("off"), not as an error. Nothing at all while a ride runs
// (setBusyCheck): it waits for the next answer after the ride.
//
// state: "off" (no server), "idle", "syncing", "error" (message: why)
class SyncTask : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(int waiting READ waiting NOTIFY changed)   // changes here not sent yet
    Q_PROPERTY(QString message READ message NOTIFY changed)

public:
    using RevOf = std::function<qint64(const ServerConnection *)>;
    // what: "profiles" / "rides" (the log, the error texts); settingsPrefix:
    // the keys of the server's id and change number ("server/users": ...ServerId, ...Rev)
    SyncTask(Database *db, const QString &what, const QString &settingsPrefix, int timeoutMs, QObject *parent = nullptr);
    ~SyncTask() override;

    // Follows the connection: answered -> serverAnswered(revOf(connection)), lost -> serverGone().
    void follow(ServerConnection *connection, RevOf revOf);
    void setBusyCheck(std::function<bool()> busy) { m_busy = std::move(busy); }

    QString state() const { return m_state; }
    int waiting() const { return m_waiting; }
    QString message() const { return m_message; }

    void serverAnswered(const QString &baseUrl, const QString &serverId, qint64 rev);
    void serverGone();

signals:
    void changed();
    void synced();   // a round finished

protected:
    using Done = std::function<void(QNetworkReply *)>;
    // The round's steps (the subclass): begin a round (the server's id is
    // stored, rev reset on another one; resendAll() first then)...
    virtual void round() = 0;
    virtual void resendAll() = 0;      // another server: everything recorded here goes again
    virtual int countWaiting() const = 0;
    // ...ended (ok or not; before the state changes): what came in is applied
    virtual void roundEnded() {}

    // A request of the round under way; a failure ends the round (finish(error)).
    void request(const QString &method, const QString &path, const QByteArray &body, const QByteArray &type, Done done);
    void finish(const QString &error = {});
    void soon(int ms);                 // a round shortly (when the server is there)
    void refreshWaiting();
    bool busy() const { return m_busy && m_busy(); }
    bool running() const { return m_running; }
    bool serverThere() const { return !m_url.isEmpty(); }
    QString serverId() const { return m_serverId; }
    qint64 serverRev() const { return m_serverRev; }
    void setServerRev(qint64 rev) { m_serverRev = rev; }
    qint64 storedRev() const;          // the server's change number as fetched last
    void storeRev(qint64 rev);
    // Fetch again from the start (a new profile arrived): now, or - a round
    // running - once it has stored its own rev.
    void restartFetch();
    // the server's {"error": "..."} or the network's
    static QString errorOf(QNetworkReply *reply);

    Database *m_db;

private:
    void start();
    void setState(const QString &state, const QString &message = {});

    QString m_what, m_idKey, m_revKey;
    int m_timeoutMs;
    std::function<bool()> m_busy;
    QNetworkAccessManager m_net;
    QPointer<QNetworkReply> m_reply;
    QTimer m_soon;
    QString m_url, m_serverId;
    qint64 m_serverRev = -1;
    bool m_running = false, m_again = false, m_restartFetch = false;
    QString m_state = QStringLiteral("off"), m_message;
    int m_waiting = 0;
};

} // namespace watt
