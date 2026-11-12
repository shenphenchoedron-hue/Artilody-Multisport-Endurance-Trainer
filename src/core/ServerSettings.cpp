#include "ServerSettings.h"

#include "Database.h"

#include <QUrl>

namespace watt {

namespace {
const QString kEnabledKey = QStringLiteral("server/enabled");
const QString kAddressKey = QStringLiteral("server/address");
}

ServerSettings::ServerSettings(Database *db, QObject *parent)
    : QObject(parent)
    , m_db(db)
{
    if (m_db) {
        m_enabled = m_db->setting(kEnabledKey, false).toBool();
        m_address = m_db->setting(kAddressKey).toString().trimmed();
        m_stationId = m_db->stationId();
    }
}

void ServerSettings::setEnabled(bool on)
{
    if (on == m_enabled)
        return;
    m_enabled = on;
    if (m_db)
        m_db->setSetting(kEnabledKey, on);
    emit changed();
}

void ServerSettings::setAddress(const QString &address)
{
    const QString a = address.trimmed();
    if (a == m_address)
        return;
    m_address = a;
    if (m_db)
        m_db->setSetting(kAddressKey, a);
    emit changed();
}

QString ServerSettings::normalizedUrl(const QString &address)
{
    QString a = address.trimmed();
    if (a.isEmpty() || a.contains(QLatin1Char(' ')))
        return QString();
    if (!a.contains(QLatin1String("://")))
        a.prepend(QLatin1String("http://"));
    const QUrl u(a, QUrl::StrictMode);
    const QString scheme = u.scheme().toLower();
    if (!u.isValid() || (scheme != QLatin1String("http") && scheme != QLatin1String("https")) || u.host().isEmpty()
        || !u.userInfo().isEmpty() || u.hasQuery() || u.hasFragment())
        return QString();
    QUrl out;
    out.setScheme(scheme);
    out.setHost(u.host().toLower());
    out.setPort(u.port());
    QString path = u.path();
    if (!path.endsWith(QLatin1Char('/')))
        path += QLatin1Char('/');
    out.setPath(path);
    return out.toString();
}

} // namespace watt
