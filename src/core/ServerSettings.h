#pragma once

#include <QObject>
#include <QString>

namespace watt {

class Database;

// The network server setting (docs/server-plan.md, step 1): off by default -
// then the station works entirely on its own, as it always has. On, with the
// server's address, later versions sync users, media and rides with it in
// the background (local first: everything is saved here first).
//
// Kept in the settings: "server/enabled" (bool) and "server/address" (as
// typed: "artilody-server.local", "192.168.1.20:8080", "http://host/artilody").
// Exposed to QML as "Server".
class ServerSettings : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(QString address READ address WRITE setAddress NOTIFY changed)
    // the address as a base URL ("http://artilody-server.local/"); "" when it is not one
    Q_PROPERTY(QString url READ url NOTIFY changed)
    Q_PROPERTY(bool addressValid READ addressValid NOTIFY changed)
    // switched on with a usable address: what the sync of later steps asks
    Q_PROPERTY(bool active READ active NOTIFY changed)
    // this station's uuid (Database::stationId())
    Q_PROPERTY(QString stationId READ stationId CONSTANT)

public:
    explicit ServerSettings(Database *db, QObject *parent = nullptr);

    bool enabled() const { return m_enabled; }
    void setEnabled(bool on);
    QString address() const { return m_address; }
    void setAddress(const QString &address);   // trimmed
    QString url() const { return normalizedUrl(m_address); }
    bool addressValid() const { return !url().isEmpty(); }
    bool active() const { return m_enabled && addressValid(); }
    QString stationId() const { return m_stationId; }

    // "host", "host:port", "http(s)://host[:port][/path]" -> "scheme://host[:port]/path/";
    // "" when it is none (empty, another scheme, user info, query, spaces ...)
    static QString normalizedUrl(const QString &address);

signals:
    void changed();

private:
    Database *m_db;
    bool m_enabled = false;
    QString m_address;
    QString m_stationId;
};

} // namespace watt
