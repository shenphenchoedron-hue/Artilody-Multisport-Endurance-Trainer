#include "Pm5Rower.h"

#include "BleLink.h"
#include "Pm5Protocol.h"

#include <QLowEnergyCharacteristic>

#include <cmath>

namespace watt {

namespace {
QBluetoothUuid pmUuid(quint16 id) { return QBluetoothUuid(pm5::uuid(id)); }
const QBluetoothUuid kFtmsService { quint16(ftms::kServiceUuid) };
const QBluetoothUuid kFtmsRowerData { quint16(ftms::kRowerDataUuid) };

QString deviceId(const QBluetoothDeviceInfo &info)
{
    // macOS/iOS do not expose MAC addresses, only a per-host UUID.
    return info.address().isNull() ? info.deviceUuid().toString() : info.address().toString();
}
} // namespace

Pm5Rower::Pm5Rower(const QBluetoothDeviceInfo &info, const QBluetoothAddress &localAdapter, QObject *parent)
    : RowerDevice(parent)
    , m_id(deviceId(info))
    , m_name(info.name().trimmed().isEmpty() ? QStringLiteral("Concept2 PM5") : info.name().trimmed())
    , m_link(new BleLink(info, pmUuid(pm5::kRowingService), localAdapter, this))
{
    setRssi(info.rssi());
    // Current PM5 firmware advertises (and serves) the standard FTMS service;
    // its rowing data is used when the Concept2 rowing service is missing.
    // Any other FTMS rowing machine works the same way.
    m_link->setFallbackServices({ kFtmsService });
    // Like Concept2's own Web Bluetooth sample: give the PM5 ~1 s after
    // discovery before enabling notifications (BlueZ drops early CCCD writes).
    m_link->setSettleDelayMs(1000);
    m_dataWatchdog.setSingleShot(true);
    m_dataWatchdog.setInterval(4000);
    connect(&m_dataWatchdog, &QTimer::timeout, this, [this] {
        if (m_source == Source::Concept2 && !m_gotData && !m_multiplexed && canNotify(pmUuid(pm5::kMultiplexed))) {
            emit logMessage(QStringLiteral("No rowing data from %1 yet – enabling multiplexed channel").arg(m_name), true);
            enableMultiplexed();
        }
    });

    connect(m_link, &BleLink::stateChanged, this, [this](LinkState s) {
        if (s != LinkState::Connected) {
            m_multiplexed = false;
            m_gotData = false;
            m_machineWarned = false;
            m_dataWatchdog.stop();
            m_ftmsAssembler.reset();
        }
        setState(s);
    });
    connect(m_link, &BleLink::serviceReady, this, &Pm5Rower::onServiceReady);
    connect(m_link, &BleLink::characteristicChanged, this, &Pm5Rower::processNotification);
    connect(m_link, &BleLink::characteristicRead, this, &Pm5Rower::processNotification);
    connect(m_link, &BleLink::gattDiscovered, this, &Pm5Rower::gattReport);
    connect(m_link, &BleLink::rssiChanged, this, [this](int r) { setRssi(r); });
    connect(m_link, &BleLink::logMessage, this, &Pm5Rower::logMessage);
    connect(m_link, &BleLink::serviceMissing, this, [this] {
        emit logMessage(QStringLiteral("%1 has neither the Concept2 rowing service nor FTMS – not a rowing monitor").arg(m_name), false);
        emit rowingServiceMissing();
    });
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

QString Pm5Rower::protocol() const
{
    return m_source == Source::Ftms ? QStringLiteral("Bluetooth FTMS rower") : QStringLiteral("Concept2 PM5 Bluetooth");
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
    m_ftmsAssembler.reset();

    if (m_link->activeService() == kFtmsService) {
        m_source = Source::Ftms;
        const QLowEnergyCharacteristic ch = m_link->service() ? m_link->service()->characteristic(kFtmsRowerData)
                                                              : QLowEnergyCharacteristic();
        const bool notify = ch.isValid() && (ch.properties() & QLowEnergyCharacteristic::Notify);
        if (!notify || !m_link->enableNotifications(kFtmsRowerData)) {
            emit logMessage(QStringLiteral("%1 offers FTMS but no Rower Data (0x2AD1) – not a rowing machine").arg(m_name), false);
            emit rowingServiceMissing();
            m_link->disconnectFromDevice();
            return;
        }
        emit logMessage(QStringLiteral("Rower connected (%1, FTMS rower data)").arg(m_name), true);
        emit protocolChanged();
        return;
    }
    m_source = Source::Concept2;
    emit protocolChanged();

    // 250 ms updates for general/additional status (default would be 500 ms).
    if (!m_link->write(pmUuid(pm5::kSampleRate), pm5::sampleRateCommand(pm5::SampleRate::Ms250)))
        emit logMessage(QStringLiteral("%1: could not set sample rate, using default").arg(m_name), false);

    const bool general = subscribe(pm5::kGeneralStatus);
    const bool status1 = subscribe(pm5::kAdditionalStatus1);
    const bool status2 = subscribe(pm5::kAdditionalStatus2);   // optional (average power, calories)
    const bool stroke = subscribe(pm5::kAdditionalStrokeData);

    // Every id whose own notification is not enabled (e.g. a failed
    // subscription) is delivered over 0x0080, so a partial failure is covered.
    if (canNotify(pmUuid(pm5::kMultiplexed)))
        enableMultiplexed();

    if (general && status1 && status2 && stroke) {
        emit logMessage(QStringLiteral("Rower connected (%1)").arg(m_name), true);
        m_dataWatchdog.start();
    } else if (m_multiplexed) {
        emit logMessage(QStringLiteral("Rower connected (%1, multiplexed data)").arg(m_name), true);
    } else if (general || status1 || stroke) {
        emit logMessage(QStringLiteral("Rower connected (%1, partial data)").arg(m_name), true);
        m_dataWatchdog.start();
    } else {
        emit logMessage(QStringLiteral("%1 does not offer rowing data notifications").arg(m_name), false);
    }
}

void Pm5Rower::processNotification(const QBluetoothUuid &uuid, const QByteArray &value)
{
    if (uuid == kFtmsRowerData) {
        if (const auto d = m_ftmsAssembler.feed(value))
            applyFtmsRowerData(*d);
        return;
    }
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
        // State first: whether a jump backwards is a new workout depends on it.
        applyWorkoutState(general->workoutState, general->rowingActive());
        applyTotalWorkDistance(general->totalWorkDistance);
        applyElapsed(general->elapsedSeconds);
        applyDistance(general->distanceMeters);
        applyDragFactor(general->dragFactor);
    }
    if (status1) {
        if (!m_machineWarned && pm5::machineKind(status1->ergMachineType) != pm5::MachineKind::Rower) {
            m_machineWarned = true;
            emit logMessage(QStringLiteral("%1 is a %2, not a rowing machine – rowing values may be wrong")
                                .arg(m_name, pm5::machineKind(status1->ergMachineType) == pm5::MachineKind::SkiErg
                                                 ? QStringLiteral("SkiErg") : QStringLiteral("BikeErg")),
                            false);
        }
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

void Pm5Rower::applyFtmsRowerData(const ftms::RowerData &d)
{
    if (d.isEmpty())
        return;
    if (d.elapsedTimeS)
        applyElapsed(*d.elapsedTimeS);
    if (d.totalDistanceM)
        applyDistance(*d.totalDistanceM);
    if (d.strokeRateSpm)
        applyStrokeRate(int(std::lround(*d.strokeRateSpm)));
    if (d.pace500S) {
        applyPace(*d.pace500S);
        applySpeed(*d.pace500S > 0 ? 500.0 / *d.pace500S : 0.0);
    }
    if (d.averagePace500S)
        applyAveragePace(*d.averagePace500S);
    if (d.averagePowerW)
        applyAverageWatts(*d.averagePowerW);
    if (d.totalEnergyKcal)
        applyCalories(*d.totalEnergyKcal);
    if (d.heartRateBpm)
        applyHeartRate(*d.heartRateBpm);

    // FTMS has no workout state: rowing is active while strokes/power come in.
    if (d.strokeRateSpm || d.powerW) {
        const bool active = (d.strokeRateSpm && *d.strokeRateSpm > 0) || (d.powerW && *d.powerW > 0);
        applyWorkoutState(int(active ? pm5::WorkoutState::WorkoutRow : pm5::WorkoutState::WaitToBegin), active);
    }
    if (d.strokeCount) {
        int watts = 0;
        if (d.powerW)
            watts = std::max(0, *d.powerW);
        else if (d.pace500S)
            watts = int(std::lround(pm5::wattsFromPace500(*d.pace500S)));
        applyStroke(*d.strokeCount, watts);
    }
    m_gotData = true;
    commitMeasurement();
}

} // namespace watt
