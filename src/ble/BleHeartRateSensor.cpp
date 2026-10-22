#include "BleHeartRateSensor.h"

#include "BleLink.h"
#include "HrsProtocol.h"

namespace watt {

BleHeartRateSensor::BleHeartRateSensor(const QBluetoothDeviceInfo &info, const QBluetoothAddress &localAdapter,
                                       QObject *parent)
    : HeartRateDevice(parent)
    , m_id(info.address().isNull() ? info.deviceUuid().toString() : info.address().toString())
    , m_name(info.name().isEmpty() ? QStringLiteral("Heart Rate Sensor") : info.name())
    , m_link(new BleLink(info, QBluetoothUuid(hrs::kServiceUuid), localAdapter, this))
{
    setRssi(info.rssi());
    connect(m_link, &BleLink::stateChanged, this, [this](LinkState s) { setState(s); });
    connect(m_link, &BleLink::rssiChanged, this, [this](int r) { setRssi(r); });
    connect(m_link, &BleLink::logMessage, this, &BleHeartRateSensor::logMessage);
    connect(m_link, &BleLink::serviceReady, this, [this] {
        if (!m_link->enableNotifications(QBluetoothUuid(hrs::kMeasurementUuid)))
            emit logMessage(QStringLiteral("Heart rate measurement not available on %1").arg(m_name), false);
        else
            emit logMessage(QStringLiteral("Heart rate sensor connected (%1)").arg(m_name), true);
    });
    connect(m_link, &BleLink::characteristicChanged, this, [this](const QBluetoothUuid &uuid, const QByteArray &value) {
        if (uuid != QBluetoothUuid(hrs::kMeasurementUuid))
            return;
        const auto m = hrs::parseMeasurement(value);
        // Ignore readings while the strap reports that it has no skin contact.
        if (m && !(m->contactSupported && !m->contactDetected) && m->bpm > 0)
            emit heartRateMeasured(m->bpm);
    });
}

void BleHeartRateSensor::connectDevice()
{
    m_link->setAutoReconnect(m_autoReconnect);
    m_link->connectToDevice();
}

void BleHeartRateSensor::disconnectDevice()
{
    m_link->disconnectFromDevice();
}

void BleHeartRateSensor::setAutoReconnect(bool on)
{
    HeartRateDevice::setAutoReconnect(on);
    m_link->setAutoReconnect(on);
}

} // namespace watt
