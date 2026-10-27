#include "BleLink.h"

#include <QLowEnergyDescriptor>
#include <QtGlobal>

namespace watt {

BleLink::BleLink(const QBluetoothDeviceInfo &info, const QBluetoothUuid &serviceUuid,
                 const QBluetoothAddress &localAdapter, QObject *parent)
    : GattLink(parent)
    , m_info(info)
    , m_serviceUuid(serviceUuid)
    , m_localAdapter(localAdapter)
{
    m_reconnectTimer.setSingleShot(true);
    connect(&m_reconnectTimer, &QTimer::timeout, this, &BleLink::connectToDevice);

    m_settleTimer.setSingleShot(true);
    connect(&m_settleTimer, &QTimer::timeout, this, [this] {
        if (!m_service || m_service->state() != QLowEnergyService::RemoteServiceDiscovered)
            return;
        setState(LinkState::Connected);
        emit serviceReady();
    });

    // Detail discovery that never finishes (seen with BlueZ when the
    // peripheral is busy): drop the link and let the reconnect logic retry.
    m_detailsTimeout.setSingleShot(true);
    m_detailsTimeout.setInterval(20000);
    connect(&m_detailsTimeout, &QTimer::timeout, this, [this] {
        if (m_state != LinkState::Connecting || !m_controller)
            return;
        emit logMessage(QStringLiteral("%1: service discovery timed out").arg(displayName()), false);
        m_controller->disconnectFromDevice();
    });

    // start delay, then the timeout of the service being inspected
    m_inspectTimer.setSingleShot(true);
    connect(&m_inspectTimer, &QTimer::timeout, this, [this] {
        if (m_inspecting)
            finishInspection(QStringLiteral("    (detail discovery timed out)"));
        else
            inspectNextService();
    });

    m_rssiTimer.setInterval(5000);
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    connect(&m_rssiTimer, &QTimer::timeout, this, [this] {
        if (m_controller && m_controller->state() != QLowEnergyController::UnconnectedState)
            m_controller->readRssi();
    });
#endif
}

BleLink::~BleLink()
{
    m_userDisconnect = true;
    cleanup();
}

QString BleLink::displayName() const
{
    const QString n = m_info.name().trimmed();
    return n.isEmpty() ? m_info.address().toString() : n;
}

void BleLink::setState(LinkState s)
{
    if (m_state == s)
        return;
    m_state = s;
    if (s == LinkState::Connected) {
        m_reconnectAttempts = 0;
        m_rssiTimer.start();
        if (m_inspectOthers && !m_othersDone && m_inspectQueue.isEmpty() && !m_inspecting) {
            m_otherLines.clear();
            for (const QBluetoothUuid &u : std::as_const(m_discovered))
                if (u != m_activeUuid)
                    m_inspectQueue << u;
            m_inspectTimer.start(kInspectDelayMs);
        }
    } else {
        m_rssiTimer.stop();
    }
    emit stateChanged(s);
}

void BleLink::cleanup()
{
    m_settleTimer.stop();
    m_detailsTimeout.stop();
    m_pendingCccd.clear();
    m_inspectTimer.stop();
    m_inspectQueue.clear();
    m_othersDone = false;
    if (m_inspecting) {
        m_inspecting->disconnect(this);
        m_inspecting->deleteLater();
    }
    m_inspecting = nullptr;
    for (const QPointer<QLowEnergyService> &o : std::as_const(m_others)) {
        if (o) {
            o->disconnect(this);
            o->deleteLater();
        }
    }
    m_others.clear();
    if (m_service) {
        m_service->disconnect(this);
        m_service->deleteLater();
    }
    m_service = nullptr;
    if (m_controller) {
        m_controller->disconnect(this);
        m_controller->disconnectFromDevice();
        m_controller->deleteLater();
    }
    m_controller = nullptr;
}

void BleLink::connectToDevice()
{
    if (m_state != LinkState::Disconnected)
        return;
    m_userDisconnect = false;
    m_reconnectTimer.stop();
    m_lastAttempt.start();
    cleanup();
    setState(LinkState::Connecting);
    emit logMessage(QStringLiteral("Connecting to %1…").arg(displayName()), true);

    m_controller = m_localAdapter.isNull() ? QLowEnergyController::createCentral(m_info, this)
                                           : QLowEnergyController::createCentral(m_info, m_localAdapter, this);
    QLowEnergyController *c = m_controller;
    c->setRemoteAddressType(m_addressType);

    connect(c, &QLowEnergyController::connected, this, [this] {
        m_everConnected = true;
        emit logMessage(QStringLiteral("Connected to %1, discovering services").arg(displayName()), true);
        m_detailsTimeout.start();
        m_controller->discoverServices();
    });
    connect(c, &QLowEnergyController::discoveryFinished, this, &BleLink::onDiscoveryFinished);
    connect(c, &QLowEnergyController::disconnected, this, [this] {
        emit logMessage(QStringLiteral("%1 disconnected").arg(displayName()), false);
        setState(LinkState::Disconnected);
        scheduleReconnect();
    });
    connect(c, &QLowEnergyController::errorOccurred, this, [this](QLowEnergyController::Error e) {
        emit logMessage(QStringLiteral("Bluetooth error (%1): %2").arg(int(e)).arg(m_controller ? m_controller->errorString() : QString()), false);
        if (e == QLowEnergyController::UnknownRemoteDeviceError)
            emit logMessage(QStringLiteral("%1 is not known to the Bluetooth stack any more – it must be advertising "
                                           "(PM5: Connect screen) while scanning").arg(displayName()), false);
        // Kernel ATT backend only: an unanswered connection may mean the
        // peripheral uses the other address type, so alternate it.
        if (e == QLowEnergyController::ConnectionError && !m_everConnected)
            m_addressType = m_addressType == QLowEnergyController::PublicAddress ? QLowEnergyController::RandomAddress
                                                                                 : QLowEnergyController::PublicAddress;
        if (m_state == LinkState::Connecting) {
            setState(LinkState::Disconnected);
            scheduleReconnect();
        }
    });
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    connect(c, &QLowEnergyController::rssiRead, this, [this](qint16 rssi) { emit rssiChanged(rssi); });
#endif

    c->connectToDevice();
}

void BleLink::onDiscoveryFinished()
{
    if (!m_controller)
        return;
    m_discovered = m_controller->services();
    m_activeUuid = QBluetoothUuid();
    QList<QBluetoothUuid> wanted { m_serviceUuid };
    wanted += m_fallbacks;
    for (const QBluetoothUuid &u : std::as_const(wanted)) {
        if (m_discovered.contains(u)) {
            m_activeUuid = u;
            break;
        }
    }
    if (m_activeUuid.isNull()) {
        m_detailsTimeout.stop();
        emit gattDiscovered(gattSummary());
        emit logMessage(QStringLiteral("%1 does not offer the required service (%2)")
                            .arg(displayName(), uuidText(m_serviceUuid)), false);
        m_userDisconnect = true;   // do not loop on an incompatible device
        emit serviceMissing();
        if (m_controller)
            m_controller->disconnectFromDevice();
        return;
    }
    if (m_activeUuid != m_serviceUuid)
        emit logMessage(QStringLiteral("%1: %2 not found, using %3").arg(displayName(), uuidText(m_serviceUuid), uuidText(m_activeUuid)), true);
    m_service = m_controller->createServiceObject(m_activeUuid, this);
    if (!m_service) {
        emit logMessage(QStringLiteral("Could not create service object"), false);
        m_controller->disconnectFromDevice();
        return;
    }
    connect(m_service, &QLowEnergyService::stateChanged, this, &BleLink::onServiceState);
    wireService(m_service, true);
    m_service->discoverDetails();
}

void BleLink::wireService(QLowEnergyService *svc, bool primary)
{
    connect(svc, &QLowEnergyService::characteristicChanged, this,
            [this](const QLowEnergyCharacteristic &ch, const QByteArray &v) { emit characteristicChanged(ch.uuid(), v); });
    connect(svc, &QLowEnergyService::characteristicRead, this,
            [this](const QLowEnergyCharacteristic &ch, const QByteArray &v) { emit characteristicRead(ch.uuid(), v); });
    connect(svc, &QLowEnergyService::descriptorWritten, this,
            [this, svc](const QLowEnergyDescriptor &d, const QByteArray &value) {
                if (d.uuid() != QBluetoothUuid(QBluetoothUuid::DescriptorType::ClientCharacteristicConfiguration))
                    return;
                const auto chars = svc->characteristics();
                for (const QLowEnergyCharacteristic &ch : chars) {
                    if (ch.descriptor(QBluetoothUuid::DescriptorType::ClientCharacteristicConfiguration) != d)
                        continue;
                    for (int i = 0; i < m_pendingCccd.size(); ++i) {
                        if (m_pendingCccd.at(i).uuid == ch.uuid()) {
                            m_pendingCccd.removeAt(i);
                            break;
                        }
                    }
                    const bool enabled = !value.isEmpty() && (quint8(value.at(0)) & 0x03);
                    emit notificationResult(ch.uuid(), enabled, true);
                    return;
                }
            });
    connect(svc, &QLowEnergyService::errorOccurred, this, [this, svc, primary](QLowEnergyService::ServiceError e) {
        // only the primary (FTMS) service's write errors concern the control point
        if (e == QLowEnergyService::CharacteristicWriteError && primary)
            emit writeFailed();
        if (e == QLowEnergyService::DescriptorWriteError && !m_pendingCccd.isEmpty()) {
            // Requests are processed in order: the oldest open CCCD write failed.
            const PendingCccd p = m_pendingCccd.takeFirst();
            emit logMessage(QStringLiteral("%1: could not %2 notifications for %3").arg(displayName(),
                                p.enable ? QStringLiteral("enable") : QStringLiteral("disable"), uuidText(p.uuid)), false);
            emit notificationResult(p.uuid, false, false);
        }
        emit logMessage(QStringLiteral("GATT error %1 (service %2)").arg(int(e)).arg(uuidText(svc->serviceUuid())), false);
    });
}

void BleLink::inspectNextService()
{
    if (!m_controller || m_state != LinkState::Connected)
        return;
    if (m_inspectQueue.isEmpty()) {
        m_inspectTimer.stop();
        m_othersDone = true;
        emit gattDiscovered(gattSummary());
        emit otherServicesDiscovered();
        return;
    }
    const QBluetoothUuid uuid = m_inspectQueue.takeFirst();
    QLowEnergyService *svc = m_controller->createServiceObject(uuid, this);
    if (!svc) {
        m_otherLines << QStringLiteral("service %1%2: could not create the service object")
                            .arg(uuidText(uuid), serviceName(uuid).isEmpty() ? QString() : QStringLiteral(" ") + serviceName(uuid));
        QTimer::singleShot(0, this, &BleLink::inspectNextService);
        return;
    }
    m_inspecting = svc;
    connect(svc, &QLowEnergyService::stateChanged, this, [this, svc](QLowEnergyService::ServiceState st) {
        if (svc != m_inspecting)
            return;
        if (st == QLowEnergyService::RemoteServiceDiscovered) {
            m_others.append(svc);
            finishInspection(characteristicLines(svc, QStringLiteral("    ")));
        } else if (st == QLowEnergyService::InvalidService) {
            finishInspection(QStringLiteral("    (service became invalid)"));
        }
    });
    wireService(svc, false);
    m_inspectTimer.start(kInspectTimeoutMs);
    svc->discoverDetails(QLowEnergyService::SkipValueDiscovery);
}

void BleLink::finishInspection(const QString &lines)
{
    QLowEnergyService *svc = m_inspecting;
    m_inspecting = nullptr;
    m_inspectTimer.stop();
    if (svc) {
        const QString name = serviceName(svc->serviceUuid());
        m_otherLines << QStringLiteral("service %1%2").arg(uuidText(svc->serviceUuid()),
                                                            name.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(name));
        if (!lines.isEmpty())
            m_otherLines << lines;
        if (!m_others.contains(svc)) {
            svc->disconnect(this);
            svc->deleteLater();
        }
    }
    QTimer::singleShot(0, this, &BleLink::inspectNextService);
}

QString BleLink::propertiesText(QLowEnergyCharacteristic::PropertyTypes p)
{
    QStringList props;
    if (p & QLowEnergyCharacteristic::Read) props << QStringLiteral("read");
    if (p & QLowEnergyCharacteristic::Write) props << QStringLiteral("write");
    if (p & QLowEnergyCharacteristic::WriteNoResponse) props << QStringLiteral("write-no-resp");
    if (p & QLowEnergyCharacteristic::Notify) props << QStringLiteral("notify");
    if (p & QLowEnergyCharacteristic::Indicate) props << QStringLiteral("indicate");
    return props.join(QLatin1Char(' '));
}

QString BleLink::characteristicLines(const QLowEnergyService *svc, const QString &indent) const
{
    QStringList lines;
    const auto chars = svc->characteristics();
    for (const QLowEnergyCharacteristic &ch : chars) {
        const bool cccd = ch.descriptor(QBluetoothUuid::DescriptorType::ClientCharacteristicConfiguration).isValid();
        lines << QStringLiteral("%1char %2 [%3]%4").arg(indent, uuidText(ch.uuid()), propertiesText(ch.properties()),
                                                        cccd ? QStringLiteral(" cccd") : QString());
    }
    if (lines.isEmpty())
        lines << indent + QStringLiteral("(no characteristics)");
    return lines.join(QLatin1Char('\n'));
}

QString BleLink::serviceName(const QBluetoothUuid &uuid)
{
    bool ok = false;
    const quint16 s16 = uuid.toUInt16(&ok);
    if (ok) {
        switch (s16) {
        case 0x1800: return QStringLiteral("Generic Access");
        case 0x1801: return QStringLiteral("Generic Attribute");
        case 0x180A: return QStringLiteral("Device Information");
        case 0x180D: return QStringLiteral("Heart Rate");
        case 0x180F: return QStringLiteral("Battery");
        case 0x1816: return QStringLiteral("Cycling Speed and Cadence");
        case 0x1818: return QStringLiteral("Cycling Power");
        case 0x1826: return QStringLiteral("Fitness Machine (FTMS)");
        case 0xFC82: return QStringLiteral("Zwift (16-bit Zwift service)");
        case 0xFE59: return QStringLiteral("Nordic DFU");
        default: break;
        }
        return QString();
    }
    const QString t = uuid.toString(QUuid::WithoutBraces).toLower();
    if (t == QLatin1String("00000001-19ca-4651-86e5-fa29dcdd09d1"))
        return QStringLiteral("Zwift (custom service)");
    if (t.startsWith(QLatin1String("a026ee")))
        return QStringLiteral("Wahoo vendor service");
    return QStringLiteral("vendor service");
}

void BleLink::onServiceState(QLowEnergyService::ServiceState s)
{
    if (s != QLowEnergyService::RemoteServiceDiscovered)
        return;
    m_detailsTimeout.stop();
    emit gattDiscovered(gattSummary());
    // Subscribe only once every characteristic/descriptor is known, after an
    // optional settle delay.
    if (m_settleMs > 0) {
        m_settleTimer.start(m_settleMs);
    } else {
        setState(LinkState::Connected);
        emit serviceReady();
    }
}

QString BleLink::uuidText(const QBluetoothUuid &u)
{
    bool ok = false;
    const quint16 s16 = u.toUInt16(&ok);
    if (ok)
        return QStringLiteral("0x%1").arg(s16, 4, 16, QLatin1Char('0'));
    return u.toString(QUuid::WithoutBraces).toUpper();
}

QString BleLink::gattSummary() const
{
    QStringList lines;
    QStringList services;
    for (const QBluetoothUuid &u : m_discovered)
        services << uuidText(u);
    lines << QStringLiteral("services: %1").arg(services.isEmpty() ? QStringLiteral("-") : services.join(QStringLiteral(", ")));
    if (m_service) {
        lines << QStringLiteral("using %1 (%2)")
                     .arg(uuidText(m_service->serviceUuid()),
                          m_service->state() == QLowEnergyService::RemoteServiceDiscovered ? QStringLiteral("details discovered")
                                                                                            : QStringLiteral("details pending"));
        lines << characteristicLines(m_service, QStringLiteral("  "));
    } else if (!m_discovered.isEmpty()) {
        lines << QStringLiteral("required service %1 not found").arg(uuidText(m_serviceUuid));
    }
    if (!m_otherLines.isEmpty()) {
        lines << QStringLiteral("other services (details, values not read):");
        lines += m_otherLines;
    } else if (m_inspectOthers && m_service && !m_othersDone) {
        lines << QStringLiteral("other services: being inspected…");
    }
    return lines.join(QLatin1Char('\n'));
}

void BleLink::disconnectFromDevice()
{
    m_userDisconnect = true;
    m_reconnectTimer.stop();
    cleanup();
    setState(LinkState::Disconnected);
}

bool BleLink::reconnectNow()
{
    if (m_state != LinkState::Disconnected || m_userDisconnect || !m_autoReconnect)
        return false;
    if (m_lastAttempt.isValid() && m_lastAttempt.elapsed() < kMinReconnectGapMs)
        return false;
    m_reconnectAttempts = 0;
    emit logMessage(QStringLiteral("%1 is advertising again – reconnecting").arg(displayName()), true);
    connectToDevice();
    return true;
}

void BleLink::scheduleReconnect()
{
    if (m_userDisconnect || !m_autoReconnect)
        return;
    ++m_reconnectAttempts;
    const int delay = qMin(30000, 2000 * m_reconnectAttempts);
    emit logMessage(QStringLiteral("Reconnecting to %1 in %2 s").arg(displayName()).arg(delay / 1000), true);
    // The controller must be recreated from the event loop, not inside its own signal.
    QTimer::singleShot(0, this, [this] { cleanup(); });
    m_reconnectTimer.start(delay);
}

QLowEnergyService *BleLink::serviceWith(const QBluetoothUuid &characteristic) const
{
    // the primary (FTMS) service first, then the other services inspected for
    // diagnostics (Zwift 0xFC82 / 00000001-19CA-...: a second service object on
    // the same controller; the FTMS service is not touched)
    if (m_service && m_service->characteristic(characteristic).isValid())
        return m_service;
    for (const QPointer<QLowEnergyService> &o : m_others)
        if (o && o->characteristic(characteristic).isValid())
            return o;
    return nullptr;
}

bool BleLink::hasCharacteristic(const QBluetoothUuid &uuid) const
{
    return serviceWith(uuid) != nullptr;
}

bool BleLink::enableNotifications(const QBluetoothUuid &characteristic, bool indicate)
{
    QLowEnergyService *svc = serviceWith(characteristic);
    if (!svc)
        return false;
    const QLowEnergyCharacteristic ch = svc->characteristic(characteristic);
    const QLowEnergyDescriptor cccd = ch.descriptor(QBluetoothUuid::DescriptorType::ClientCharacteristicConfiguration);
    if (!cccd.isValid())
        return false;
    m_pendingCccd.append({ characteristic, true });
    svc->writeDescriptor(cccd, indicate ? QLowEnergyCharacteristic::CCCDEnableIndication
                                        : QLowEnergyCharacteristic::CCCDEnableNotification);
    return true;
}

bool BleLink::disableNotifications(const QBluetoothUuid &characteristic)
{
    QLowEnergyService *svc = serviceWith(characteristic);
    if (!svc)
        return false;
    const QLowEnergyCharacteristic ch = svc->characteristic(characteristic);
    const QLowEnergyDescriptor cccd = ch.descriptor(QBluetoothUuid::DescriptorType::ClientCharacteristicConfiguration);
    if (!cccd.isValid())
        return false;
    m_pendingCccd.append({ characteristic, false });
    svc->writeDescriptor(cccd, QLowEnergyCharacteristic::CCCDDisable);
    return true;
}

bool BleLink::write(const QBluetoothUuid &characteristic, const QByteArray &data, bool withResponse)
{
    if (m_state != LinkState::Connected)
        return false;
    QLowEnergyService *svc = serviceWith(characteristic);
    if (!svc)
        return false;
    const QLowEnergyCharacteristic ch = svc->characteristic(characteristic);
    // Use a write type the characteristic actually supports.
    const auto props = ch.properties();
    if (withResponse && !(props & QLowEnergyCharacteristic::Write) && (props & QLowEnergyCharacteristic::WriteNoResponse))
        withResponse = false;
    else if (!withResponse && !(props & QLowEnergyCharacteristic::WriteNoResponse) && (props & QLowEnergyCharacteristic::Write))
        withResponse = true;
    svc->writeCharacteristic(ch, data, withResponse ? QLowEnergyService::WriteWithResponse
                                                    : QLowEnergyService::WriteWithoutResponse);
    return true;
}

bool BleLink::read(const QBluetoothUuid &characteristic)
{
    QLowEnergyService *svc = serviceWith(characteristic);
    if (!svc)
        return false;
    svc->readCharacteristic(svc->characteristic(characteristic));
    return true;
}

} // namespace watt
