#include "BleTreadmill.h"

#include "BleLink.h"
#include "DiagLog.h"

namespace watt {

namespace {
QBluetoothUuid uuid16(quint16 v) { return QBluetoothUuid(v); }
} // namespace

BleTreadmill::BleTreadmill(const QBluetoothDeviceInfo &info, const QBluetoothAddress &localAdapter, QObject *parent)
    : TreadmillDevice(parent)
    , m_id(info.address().isNull() ? info.deviceUuid().toString() : info.address().toString())
    , m_name(info.name().trimmed().isEmpty() ? QStringLiteral("Treadmill") : info.name().trimmed())
    , m_link(new BleLink(info, uuid16(ftms::kServiceUuid), localAdapter, this))
{
    setRssi(info.rssi());
    m_link->setAdvertiseHint(QStringLiteral("switch the treadmill on / wake its console"));
    connect(m_link, &BleLink::stateChanged, this, [this](LinkState s) { setState(s); });
    connect(m_link, &BleLink::rssiChanged, this, [this](int r) { setRssi(r); });
    connect(m_link, &BleLink::logMessage, this, &BleTreadmill::logMessage);
    connect(m_link, &BleLink::gattDiscovered, this, &BleTreadmill::gattReport);
    connect(m_link, &BleLink::serviceReady, this, &BleTreadmill::onServiceReady);
    connect(m_link, &BleLink::serviceMissing, this, &BleTreadmill::treadmillDataMissing);
    connect(m_link, &BleLink::characteristicChanged, this, &BleTreadmill::processNotification);
    connect(m_link, &BleLink::characteristicRead, this, [this](const QBluetoothUuid &uuid, const QByteArray &value) {
        if (uuid == uuid16(ftms::kFeatureUuid))
            DiagLog::write("TREAD", QStringLiteral("%1 features: %2").arg(m_name, ftms::describeFeatures(value)));
    });
}

void BleTreadmill::onServiceReady()
{
    if (!m_link->hasCharacteristic(uuid16(ftms::kTreadmillDataUuid))) {
        emit logMessage(QStringLiteral("%1 offers FTMS but no Treadmill Data (0x2ACD) – not a treadmill").arg(m_name), false);
        emit treadmillDataMissing();
        m_link->disconnectFromDevice();
        return;
    }
    if (m_link->hasCharacteristic(uuid16(ftms::kFeatureUuid)))
        m_link->read(uuid16(ftms::kFeatureUuid));
    // Only the data notification: the control point is never touched.
    if (m_link->enableNotifications(uuid16(ftms::kTreadmillDataUuid)))
        emit logMessage(QStringLiteral("Treadmill connected (%1) – reading speed and incline only").arg(m_name), true);
    else
        emit logMessage(QStringLiteral("Treadmill Data notifications not available on %1").arg(m_name), false);
    DiagLog::write("TREAD", QStringLiteral("%1 [%2]: FTMS treadmill ready (read-only, control point untouched)").arg(m_name, m_id));
}

void BleTreadmill::processNotification(const QBluetoothUuid &uuid, const QByteArray &value)
{
    if (uuid != uuid16(ftms::kTreadmillDataUuid))
        return;
    const auto d = ftms::parseTreadmillData(value);
    if (!d) {
        DiagLog::write("TREAD", QStringLiteral("malformed Treadmill Data [%1]").arg(DiagLog::hex(value)));
        return;
    }
    if (!m_logged) {
        m_logged = true;
        DiagLog::write("TREAD", QStringLiteral("%1 first data: %2 [%3]").arg(m_name, ftms::describeTreadmillData(*d),
                                                                             DiagLog::hex(value)));
    }
    applyMeasurement(d->speedKmh ? *d->speedKmh : -1.0, d->inclinePercent ? *d->inclinePercent : 0.0,
                     d->inclinePercent.has_value(), d->totalDistanceM ? double(*d->totalDistanceM) : -1.0,
                     d->heartRateBpm ? *d->heartRateBpm : 0);
}

void BleTreadmill::connectDevice()
{
    m_link->setAutoReconnect(m_autoReconnect);
    m_link->connectToDevice();
}

void BleTreadmill::disconnectDevice()
{
    m_link->disconnectFromDevice();
}

void BleTreadmill::setAutoReconnect(bool on)
{
    TreadmillDevice::setAutoReconnect(on);
    m_link->setAutoReconnect(on);
}

bool BleTreadmill::reconnectNow()
{
    m_link->setAutoReconnect(m_autoReconnect);
    return m_link->reconnectNow();
}

} // namespace watt
