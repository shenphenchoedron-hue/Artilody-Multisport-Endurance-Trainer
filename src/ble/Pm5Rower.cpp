#include "Pm5Rower.h"

#include "BleLink.h"
#include "Pm5Protocol.h"

#include <QLowEnergyCharacteristic>

namespace watt {

namespace {
QBluetoothUuid pmUuid(quint16 id) { return QBluetoothUuid(pm5::uuid(id)); }

QString deviceId(const QBluetoothDeviceInfo &info)
{
    // macOS/iOS do not expose MAC addresses, only a per-host UUID.
    return info.address().isNull() ? info.deviceUuid().toString() : info.address().toString();
}
} // namespace

Pm5Rower::Pm5Rower(const QBluetoothDeviceInfo &info, const QBluetoothAddress &localAdapter, QObject *parent)
    : RowerDevice(parent)
    , m_id(deviceId(info))
    , m_name(info.name().isEmpty() ? QStringLiteral("Concept2 PM5") : info.name())
    , m_link(new BleLink(info, pmUuid(pm5::kRowingService), localAdapter, this))
{
    setRssi(info.rssi());
    m_dataWatchdog.setSingleShot(true);
    m_dataWatchdog.setInterval(4000);
    connect(&m_dataWatchdog, &QTimer::timeout, this, [this] {
        if (!m_gotData && !m_multiplexed && canNotify(pmUuid(pm5::kMultiplexed))) {
            emit logMessage(QStringLiteral("No rowing data from %1 yet – enabling multiplexed channel").arg(m_name), true);
            enableMultiplexed();
        }
    });

    connect(m_link, &BleLink::stateChanged, this, [this](LinkState s) {
        if (s != LinkState::Connected) {
            m_multiplexed = false;
            m_gotData = false;
            m_dataWatchdog.stop();
        }
        setState(s);
    });
    connect(m_link, &BleLink::serviceReady, this, &Pm5Rower::onServiceReady);
    connect(m_link, &BleLink::characteristicChanged, this, &Pm5Rower::onNotification);
    connect(m_link, &BleLink::characteristicRead, this, &Pm5Rower::onNotification);
    connect(m_link, &BleLink::rssiChanged, this, [this](int r) { setRssi(r); });
    connect(m_link, &BleLink::logMessage, this, &Pm5Rower::logMessage);
}

void Pm5Rower::connectDevice()
{
    m_link->setAutoReconnect(m_autoReconnect);
    m_link->connectToDevice();
}

void Pm5Rower::disconnectDevice()
{
    m_dataWatchdog.stop();
    m_link->disconnectFromDevice();
}

void Pm5Rower::setAutoReconnect(bool on)
{
    RowerDevice::setAutoReconnect(on);
    m_link->setAutoReconnect(on);
}

bool Pm5Rower::canNotify(const QBluetoothUuid &uuid) const
{
    if (!m_link->service())
        return false;
    const QLowEnergyCharacteristic ch = m_link->service()->characteristic(uuid);
    return ch.isValid() && (ch.properties() & QLowEnergyCharacteristic::Notify);
}

bool Pm5Rower::subscribe(quint16 shortId)
{
    const QBluetoothUuid u = pmUuid(shortId);
    return canNotify(u) && m_link->enableNotifications(u);
}

void Pm5Rower::enableMultiplexed()
{
    if (m_multiplexed)
        return;
    m_multiplexed = subscribe(pm5::kMultiplexed);
}

void Pm5Rower::onServiceReady()
{
    m_gotData = false;
    m_multiplexed = false;

    // 250 ms updates for general/additional status (default would be 500 ms).
    if (!m_link->write(pmUuid(pm5::kSampleRate), pm5::sampleRateCommand(pm5::SampleRate::Ms250)))
        emit logMessage(QStringLiteral("%1: could not set sample rate, using default").arg(m_name), false);

    const bool general = subscribe(pm5::kGeneralStatus);
    const bool status1 = subscribe(pm5::kAdditionalStatus1);
    subscribe(pm5::kAdditionalStatus2);   // optional (average power, calories)
    const bool stroke = subscribe(pm5::kAdditionalStrokeData);

    if (general && status1 && stroke) {
        emit logMessage(QStringLiteral("Rower connected (%1)").arg(m_name), true);
        m_dataWatchdog.start();
    } else if (canNotify(pmUuid(pm5::kMultiplexed))) {
        // Remaining ids are delivered over 0x0080 as long as their own
        // notification is not enabled.
        enableMultiplexed();
        emit logMessage(QStringLiteral("Rower connected (%1, multiplexed data)").arg(m_name), true);
    } else {
        emit logMessage(QStringLiteral("%1 does not offer rowing data notifications").arg(m_name), false);
    }
}

void Pm5Rower::onNotification(const QBluetoothUuid &uuid, const QByteArray &value)
{
    const auto id = pm5::shortId(uuid);
    if (!id)
        return;

    std::optional<pm5::GeneralStatus> general;
    std::optional<pm5::AdditionalStatus1> status1;
    std::optional<pm5::AdditionalStatus2> status2;
    std::optional<pm5::AdditionalStrokeData> stroke;

    switch (*id) {
    case pm5::kGeneralStatus: general = pm5::parseGeneralStatus(value); break;
    case pm5::kAdditionalStatus1: status1 = pm5::parseAdditionalStatus1(value); break;
    case pm5::kAdditionalStatus2: status2 = pm5::parseAdditionalStatus2(value); break;
    case pm5::kAdditionalStrokeData: stroke = pm5::parseAdditionalStrokeData(value); break;
    case pm5::kMultiplexed:
        if (const auto p = pm5::parseMultiplexed(value)) {
            general = p->generalStatus;
            status1 = p->additionalStatus1;
            status2 = p->additionalStatus2;
            stroke = p->additionalStrokeData;
        }
        break;
    default:
        return;
    }

    // Elapsed time is taken from general status only: the other packets carry
    // slightly different timestamps and would make the value jitter.
    if (general) {
        applyElapsed(general->elapsedSeconds);
        applyDistance(general->distanceMeters);
        applyDragFactor(general->dragFactor);
        applyWorkoutState(general->workoutState, general->rowingActive());
    }
    if (status1) {
        applySpeed(status1->speedMps);
        applyStrokeRate(status1->strokesPerMinute);
        applyHeartRate(status1->heartRateBpm);
        applyPace(status1->currentPace500Seconds);
        applyAveragePace(status1->averagePace500Seconds);
        if (status1->averagePowerW)
            applyAverageWatts(*status1->averagePowerW);
    }
    if (status2) {
        applyCalories(status2->totalCalories);
        if (status2->averagePowerW)
            applyAverageWatts(*status2->averagePowerW);
    }
    if (stroke) {
        applyStroke(stroke->strokeCount, stroke->strokePowerW);
    }
    if (general || status1 || status2 || stroke) {
        m_gotData = true;
        commitMeasurement();
    }
}

} // namespace watt
