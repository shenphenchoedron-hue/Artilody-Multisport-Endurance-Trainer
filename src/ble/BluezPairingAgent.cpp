#include "BluezPairingAgent.h"

#include "DiagLog.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QRegularExpression>

namespace watt {

namespace {
const QString kService = QStringLiteral("org.bluez");
const QString kAgentPath = QStringLiteral("/app/artilody/PairingAgent");
const QString kAgentManager = QStringLiteral("org.bluez.AgentManager1");
const QString kDeviceIface = QStringLiteral("org.bluez.Device1");
const QString kProps = QStringLiteral("org.freedesktop.DBus.Properties");
const QString kRejected = QStringLiteral("org.bluez.Error.Rejected");
const QString kCanceled = QStringLiteral("org.bluez.Error.Canceled");

QDBusMessage agentManagerCall(const QString &method)
{
    QDBusMessage m = QDBusMessage::createMethodCall(kService, QStringLiteral("/org/bluez"), kAgentManager, method);
    m << QVariant::fromValue(QDBusObjectPath(kAgentPath));
    return m;
}
} // namespace

BluezPairingAgent::BluezPairingAgent(QObject *parent)
    : QObject(parent)
{
}

BluezPairingAgent::~BluezPairingAgent()
{
    QDBusConnection bus = QDBusConnection::systemBus();
    if (m_kind != RequestKind::None)
        bus.send(m_pending.createErrorReply(kCanceled, QStringLiteral("application closed")));
    if (m_registered)
        bus.send(agentManagerCall(QStringLiteral("UnregisterAgent")));   // no need to wait
    bus.unregisterObject(kAgentPath);
}

bool BluezPairingAgent::start()
{
    QDBusConnection bus = QDBusConnection::systemBus();
    if (!bus.isConnected()) {
        emit logMessage(QStringLiteral("Bluetooth pairing agent: no system D-Bus – devices that need a PIN must be "
                                       "paired with bluetoothctl"), false);
        return false;
    }
    if (!bus.objectRegisteredAt(kAgentPath) && !bus.registerObject(kAgentPath, this, QDBusConnection::ExportAllSlots)) {
        emit logMessage(QStringLiteral("Bluetooth pairing agent: could not register %1 (%2)")
                            .arg(kAgentPath, bus.lastError().message()), false);
        return false;
    }
    QDBusMessage reg = agentManagerCall(QStringLiteral("RegisterAgent"));
    reg << QStringLiteral("KeyboardOnly");
    auto *w = new QDBusPendingCallWatcher(bus.asyncCall(reg), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, w] {
        w->deleteLater();
        const QDBusPendingReply<> r = *w;
        if (r.isError() && r.error().name() != QLatin1String("org.bluez.Error.AlreadyExists")) {
            emit logMessage(QStringLiteral("Bluetooth pairing agent: BlueZ refused it (%1) – devices that need a PIN "
                                           "must be paired with bluetoothctl").arg(r.error().message()), false);
            return;
        }
        m_registered = true;
        emit registeredChanged();
        requestDefaultAgent();
    });
    return true;
}

void BluezPairingAgent::requestDefaultAgent()
{
    auto *w = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(agentManagerCall(QStringLiteral("RequestDefaultAgent"))), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [w] {
        w->deleteLater();
        const QDBusPendingReply<> r = *w;
        if (r.isError())
            DiagLog::write("PAIR", QStringLiteral("agent registered, not default: %1").arg(r.error().message()));
        else
            DiagLog::write("PAIR", QStringLiteral("agent registered as default agent (KeyboardOnly) at %1").arg(kAgentPath));
    });
}

void BluezPairingAgent::allowDevice(const QString &address)
{
    if (!address.isEmpty())
        m_allowed.insert(address.toUpper());
}

bool BluezPairingAgent::isAllowed(const QString &address) const
{
    return !address.isEmpty() && m_allowed.contains(address.toUpper());
}

QString BluezPairingAgent::addressFromPath(const QString &path)
{
    static const QRegularExpression re(QStringLiteral("dev_([0-9A-Fa-f]{2}(?:_[0-9A-Fa-f]{2}){5})"));
    const auto m = re.match(path);
    if (!m.hasMatch())
        return QString();
    return m.captured(1).replace(QLatin1Char('_'), QLatin1Char(':')).toUpper();
}

int BluezPairingAgent::parsePasskey(const QString &text)
{
    QString t = text;
    t.remove(QLatin1Char(' '));
    if (t.isEmpty() || t.size() > 6)
        return -1;
    for (const QChar c : std::as_const(t))
        if (!c.isDigit() || c.unicode() > 0x7F)
            return -1;
    return t.toInt();
}

void BluezPairingAgent::beginRequest(RequestKind kind, const QDBusObjectPath &device)
{
    // a new request replaces an unanswered one (BlueZ sends one at a time)
    if (m_kind != RequestKind::None)
        connection().send(m_pending.createErrorReply(kCanceled, QStringLiteral("replaced by a new request")));
    setDelayedReply(true);
    m_pending = message();
    m_kind = kind;
    m_address = addressFromPath(device.path());
    DiagLog::write("PAIR", QStringLiteral("%1 requested for %2").arg(kind == RequestKind::Passkey ? QStringLiteral("passkey")
                                                                                                  : QStringLiteral("PIN code"),
                                                                      m_address));
    emit requestChanged();
}

void BluezPairingAgent::clearRequest()
{
    m_kind = RequestKind::None;
    m_address.clear();
    m_pending = QDBusMessage();
    emit requestChanged();
}

bool BluezPairingAgent::acceptIfAllowed(const QDBusObjectPath &device, const QString &what)
{
    const QString address = addressFromPath(device.path());
    if (isAllowed(address)) {
        DiagLog::write("PAIR", QStringLiteral("%1 for %2 accepted (device used by the app)").arg(what, address));
        return true;
    }
    DiagLog::write("PAIR", QStringLiteral("%1 for %2 rejected (not a device the app connects to)").arg(what, address));
    sendErrorReply(kRejected, QStringLiteral("not a device used by this application"));
    return false;
}

bool BluezPairingAgent::submit(const QString &value)
{
    if (m_kind == RequestKind::None)
        return false;
    QDBusMessage reply;
    if (m_kind == RequestKind::Passkey) {
        const int passkey = parsePasskey(value);
        if (passkey < 0)
            return false;
        reply = m_pending.createReply(QVariant::fromValue(uint(passkey)));
    } else {
        QString pin = value;
        pin.remove(QLatin1Char(' '));
        if (pin.isEmpty() || pin.size() > 16)
            return false;
        reply = m_pending.createReply(pin);
    }
    QDBusConnection::systemBus().send(reply);
    DiagLog::write("PAIR", QStringLiteral("PIN entered for %1").arg(m_address));
    clearRequest();
    return true;
}

void BluezPairingAgent::reject()
{
    if (m_kind == RequestKind::None)
        return;
    QDBusConnection::systemBus().send(m_pending.createErrorReply(kRejected, QStringLiteral("cancelled by the user")));
    DiagLog::write("PAIR", QStringLiteral("PIN entry cancelled for %1").arg(m_address));
    clearRequest();
}

// --- org.bluez.Agent1 -------------------------------------------------------

void BluezPairingAgent::Release()
{
    DiagLog::write("PAIR", QStringLiteral("agent released by BlueZ"));
    m_registered = false;
    emit registeredChanged();
    if (m_kind != RequestKind::None) {
        m_pending = QDBusMessage();
        clearRequest();
    }
}

QString BluezPairingAgent::RequestPinCode(const QDBusObjectPath &device)
{
    beginRequest(RequestKind::PinCode, device);
    return QString();   // delayed reply
}

void BluezPairingAgent::DisplayPinCode(const QDBusObjectPath &device, const QString &pincode)
{
    emit logMessage(QStringLiteral("Enter PIN %1 on the device %2").arg(pincode, addressFromPath(device.path())), true);
}

uint BluezPairingAgent::RequestPasskey(const QDBusObjectPath &device)
{
    beginRequest(RequestKind::Passkey, device);
    return 0;   // delayed reply
}

void BluezPairingAgent::DisplayPasskey(const QDBusObjectPath &device, uint passkey, ushort entered)
{
    Q_UNUSED(entered)
    emit logMessage(QStringLiteral("Enter %1 on the device %2")
                        .arg(passkey, 6, 10, QLatin1Char('0'))
                        .arg(addressFromPath(device.path())),
                    true);
}

void BluezPairingAgent::RequestConfirmation(const QDBusObjectPath &device, uint passkey)
{
    // KeyboardOnly normally never gets a numeric comparison; only a device the
    // app connects to itself may be confirmed without the user.
    acceptIfAllowed(device, QStringLiteral("confirmation %1").arg(passkey, 6, 10, QLatin1Char('0')));
}

void BluezPairingAgent::RequestAuthorization(const QDBusObjectPath &device)
{
    // Just Works pairing: incoming ones from other devices are refused
    acceptIfAllowed(device, QStringLiteral("authorization"));
}

void BluezPairingAgent::AuthorizeService(const QDBusObjectPath &device, const QString &uuid)
{
    // profile connections (HID, audio, ...) of devices not used by the app are refused
    acceptIfAllowed(device, QStringLiteral("service %1").arg(uuid));
}

void BluezPairingAgent::Cancel()
{
    if (m_kind == RequestKind::None)
        return;
    DiagLog::write("PAIR", QStringLiteral("request for %1 cancelled by BlueZ (timeout or the device gave up)").arg(m_address));
    emit logMessage(QStringLiteral("Pairing with %1 was cancelled (the PIN was not entered in time)").arg(m_address), false);
    m_pending = QDBusMessage();   // BlueZ no longer waits for the reply
    clearRequest();
}

// --- explicit pairing (all asynchronous) ------------------------------------

void BluezPairingAgent::pairIfNeeded(const QString &address)
{
    allowDevice(address);
    // GetManagedObjects returns a{oa{sa{sv}}}; only the paths are needed:
    // the object whose path ends with dev_AA_BB_... (any adapter).
    const QDBusMessage call = QDBusMessage::createMethodCall(kService, QStringLiteral("/"),
                                                             QStringLiteral("org.freedesktop.DBus.ObjectManager"),
                                                             QStringLiteral("GetManagedObjects"));
    auto *w = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(call), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, w, address] {
        w->deleteLater();
        const QString suffix = QStringLiteral("/dev_") + address.toUpper().replace(QLatin1Char(':'), QLatin1Char('_'));
        QString path;
        const QDBusMessage r = w->reply();
        if (r.type() == QDBusMessage::ReplyMessage && !r.arguments().isEmpty()) {
            const QDBusArgument arg = r.arguments().constFirst().value<QDBusArgument>();
            arg.beginMap();
            while (!arg.atEnd() && path.isEmpty()) {
                arg.beginMapEntry();
                QDBusObjectPath p;
                arg >> p;
                arg.endMapEntry();   // skips the interfaces
                if (p.path().endsWith(suffix))
                    path = p.path();
            }
        }
        if (path.isEmpty()) {
            DiagLog::write("PAIR", QStringLiteral("%1: no BlueZ device object").arg(address));
            emit pairFinished(address, false, false, QStringLiteral("device not known to BlueZ – scan again"));
            return;
        }
        readPairedAndPair(address, path);
    });
}

void BluezPairingAgent::readPairedAndPair(const QString &address, const QString &path)
{
    QDBusMessage get = QDBusMessage::createMethodCall(kService, path, kProps, QStringLiteral("Get"));
    get << kDeviceIface << QStringLiteral("Paired");
    auto *w = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(get), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, w, address, path] {
        w->deleteLater();
        const QDBusPendingReply<QDBusVariant> r = *w;
        if (!r.isError() && r.value().variant().toBool()) {
            DiagLog::write("PAIR", QStringLiteral("%1 is paired already").arg(address));
            emit pairFinished(address, true, true, QString());
            return;
        }
        callPair(address, path);
    });
}

void BluezPairingAgent::callPair(const QString &address, const QString &path)
{
    const QDBusMessage msg = QDBusMessage::createMethodCall(kService, path, kDeviceIface, QStringLiteral("Pair"));
    DiagLog::write("PAIR", QStringLiteral("Device1.Pair %1").arg(path));
    // the user needs time to read and type the PIN
    auto *w = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(msg, 60000), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, w, address, path] {
        w->deleteLater();
        const QDBusPendingReply<> r = *w;
        if (!r.isError() || r.error().name() == QLatin1String("org.bluez.Error.AlreadyExists")) {
            DiagLog::write("PAIR", QStringLiteral("%1 paired").arg(address));
            // trusted: BlueZ lets it reconnect without asking again (no need to wait)
            QDBusMessage set = QDBusMessage::createMethodCall(kService, path, kProps, QStringLiteral("Set"));
            set << kDeviceIface << QStringLiteral("Trusted") << QVariant::fromValue(QDBusVariant(true));
            QDBusConnection::systemBus().send(set);
            emit pairFinished(address, true, false, QString());
        } else {
            DiagLog::write("PAIR", QStringLiteral("%1 pairing failed: %2 %3").arg(address, r.error().name(), r.error().message()));
            emit pairFinished(address, false, false, r.error().message());
        }
    });
}

} // namespace watt
