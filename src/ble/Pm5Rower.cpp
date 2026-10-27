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

    m_status1Watchdog.setSingleShot(true);
    m_status1Watchdog.setInterval(5000);
    connect(&m_status1Watchdog, &QTimer::timeout, this, &Pm5Rower::onStatus1Watchdog);
    connect(m_link, &BleLink::notificationResult, this, &Pm5Rower::onNotificationResult);

    connect(m_link, &BleLink::stateChanged, this, [this](LinkState s) {
        if (s != LinkState::Connected) {
            m_multiplexed = false;
            m_gotData = false;
            m_machineWarned = false;
            m_dataWatchdog.stop();
            m_status1Watchdog.stop();
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
    m_status1Watchdog.stop();
    m_link->disconnectFromDevice();
}

bool Pm5Rower::reconnectNow()
{
    m_link->setAutoReconnect(m_autoReconnect);
    return m_link->reconnectNow();
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

QString Pm5Rower::channelKey(quint16 shortId)
{
    return QStringLiteral("0x%1").arg(shortId, 4, 16, QLatin1Char('0')).toUpper().replace(QLatin1String("0X"), QLatin1String("0x"));
}

void Pm5Rower::setSubscription(quint16 shortId, const QString &text)
{
    m_channels[channelKey(shortId)].subscription = text;
}

bool Pm5Rower::subscribe(quint16 shortId)
{
    const QBluetoothUuid u = pmUuid(shortId);
    const bool ok = canNotify(u) && m_link->enableNotifications(u);
    setSubscription(shortId, ok ? QStringLiteral("requested") : QStringLiteral("not offered"));
    return ok;
}

void Pm5Rower::enableMultiplexed()
{
    if (m_multiplexed)
        return;
    m_multiplexed = subscribe(pm5::kMultiplexed);
}

void Pm5Rower::onNotificationResult(const QBluetoothUuid &uuid, bool enabled, bool ok)
{
    quint16 id = 0;
    if (uuid == kFtmsRowerData) {
        id = ftms::kRowerDataUuid;
    } else if (const auto s = pm5::shortId(uuid)) {
        id = *s;
    } else {
        return;
    }
    setSubscription(id, !ok ? QStringLiteral("FAILED") : enabled ? QStringLiteral("enabled") : QStringLiteral("disabled"));
    if (ok || m_source != Source::Concept2)
        return;
    if (id == pm5::kAdditionalStatus1)
        emit logMessage(QStringLiteral("%1: stroke rate channel 0x0032 could not be enabled – using the multiplexed channel").arg(m_name), false);
    if (id != pm5::kMultiplexed)
        enableMultiplexed();   // the PM5 multiplexes every id whose own notification is off
    else
        m_multiplexed = false;
}

void Pm5Rower::onStatus1Watchdog()
{
    if (m_source != Source::Concept2 || state() != LinkState::Connected || m_status1Seen)
        return;
    if (!m_gotData) {   // nothing at all yet: the data watchdog handles that first
        m_status1Watchdog.start();
        return;
    }
    ++m_status1Stage;
    if (m_status1Stage == 1) {
        emit logMessage(QStringLiteral("No stroke rate data (0x0032) from %1 – enabling its notifications again").arg(m_name), false);
        subscribe(pm5::kAdditionalStatus1);
        if (canNotify(pmUuid(pm5::kMultiplexed)))
            enableMultiplexed();
        m_status1Watchdog.start();
    } else if (m_status1Stage == 2 && canNotify(pmUuid(pm5::kMultiplexed))) {
        emit logMessage(QStringLiteral("Still no 0x0032 from %1 – moving it to the multiplexed channel 0x0080").arg(m_name), false);
        if (m_link->disableNotifications(pmUuid(pm5::kAdditionalStatus1)))
            setSubscription(pm5::kAdditionalStatus1, QStringLiteral("disabling (moved to 0x0080)"));
        enableMultiplexed();
        m_status1Watchdog.start();
    } else {
        emit logMessage(QStringLiteral("%1 sends no stroke rate – computing it from the stroke timing").arg(m_name), false);
    }
}

Pm5Rower::Channel &Pm5Rower::notePacket(const QString &key, const QByteArray &value, bool decoded)
{
    Channel &c = m_channels[key];
    ++c.packets;
    if (!decoded)
        ++c.undecoded;
    c.last = value;
    c.lastMs = nowMs();
    return c;
}

QString Pm5Rower::dataSourceText() const
{
    if (m_source == Source::Ftms)
        return QStringLiteral("FTMS Rower Data (0x2AD1)");
    if (m_source != Source::Concept2)
        return QStringLiteral("not connected");
    QStringList direct, mux;
    for (auto it = m_channels.cbegin(); it != m_channels.cend(); ++it) {
        if (it->packets == 0)
            continue;
        if (it.key().startsWith(QLatin1String("0x0080/")))
            mux << it.key().mid(7);
        else if (it.key() != QLatin1String("0x0080"))
            direct << it.key().right(2).prepend(QLatin1String("0x"));
    }
    QString text = QStringLiteral("Concept2 rowing service");
    QStringList parts;
    if (!direct.isEmpty())
        parts << QStringLiteral("direct %1").arg(direct.join(QLatin1Char(' ')));
    if (!mux.isEmpty())
        parts << QStringLiteral("0x0080 multiplex %1").arg(mux.join(QLatin1Char(' ')));
    if (!parts.isEmpty())
        text += QStringLiteral(" (%1)").arg(parts.join(QStringLiteral(", ")));
    return text;
}

QString Pm5Rower::dataDiagnostics() const
{
    const qint64 now = nowMs();
    QStringList l;
    l << QStringLiteral("Data source: %1").arg(dataSourceText());
    l << QStringLiteral("Protocol: %1  multiplexed channel: %2").arg(protocol(), m_multiplexed ? QStringLiteral("enabled") : QStringLiteral("off"));
    if (m_channels.isEmpty())
        l << QStringLiteral("Channels: none subscribed yet");
    for (auto it = m_channels.cbegin(); it != m_channels.cend(); ++it) {
        const Channel &c = it.value();
        QString line = QStringLiteral("  %1  sub: %2  packets: %3").arg(it.key(), -12)
                           .arg(c.subscription.isEmpty() ? QStringLiteral("-") : c.subscription, -10)
                           .arg(c.packets);
        if (c.undecoded > 0)
            line += QStringLiteral(" (%1 undecoded)").arg(c.undecoded);
        if (c.lastMs >= 0)
            line += QStringLiteral("  last %1 s ago (%2 B): %3")
                        .arg((now - c.lastMs) / 1000.0, 0, 'f', 1)
                        .arg(c.last.size())
                        .arg(QString::fromLatin1(c.last.toHex(' ')));
        l << line;
    }
    if (m_source == Source::Concept2)
        l << QStringLiteral("Additional Status 1 (stroke rate) via: %1").arg(m_status1Via.isEmpty() ? QStringLiteral("none received") : m_status1Via);
    l << QStringLiteral("Last decoded stroke rate: %1")
             .arg(m_lastDecodedSpm < 0 ? QStringLiteral("none")
                                       : QStringLiteral("%1 SPM (%2 s ago)").arg(m_lastDecodedSpm).arg((now - m_lastDecodedSpmMs) / 1000.0, 0, 'f', 1));
    l << RowerDevice::dataDiagnostics();
    return l.join(QLatin1Char('\n'));
}

void Pm5Rower::onServiceReady()
{
    m_gotData = false;
    m_multiplexed = false;
    m_ftmsAssembler.reset();
    m_channels.clear();
    m_status1Seen = false;
    m_status1Stage = 0;
    m_status1Via.clear();
    m_lastDecodedSpm = -1;
    m_lastDecodedSpmMs = -1;

    if (m_link->activeService() == kFtmsService) {
        m_source = Source::Ftms;
        const QLowEnergyCharacteristic ch = m_link->service() ? m_link->service()->characteristic(kFtmsRowerData)
                                                              : QLowEnergyCharacteristic();
        const bool notify = ch.isValid() && (ch.properties() & QLowEnergyCharacteristic::Notify);
        const bool requested = notify && m_link->enableNotifications(kFtmsRowerData);
        m_channels[channelKey(ftms::kRowerDataUuid)].subscription = requested ? QStringLiteral("requested") : QStringLiteral("not offered");
        if (!requested) {
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

    m_status1Watchdog.start();
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
        if (m_source == Source::Unknown)
            m_source = Source::Ftms;
        const auto parsed = ftms::parseRowerData(value);
        notePacket(channelKey(ftms::kRowerDataUuid), value, parsed.has_value());
        if (parsed && parsed->strokeRateSpm) {
            m_lastDecodedSpm = int(std::lround(*parsed->strokeRateSpm));
            m_lastDecodedSpmMs = nowMs();
        }
        if (const auto d = m_ftmsAssembler.feed(value))
            applyFtmsRowerData(*d);
        return;
    }
    const auto id = pm5::shortId(uuid);
    if (!id)
        return;
    if (m_source == Source::Unknown)
        m_source = Source::Concept2;

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
        notePacket(channelKey(*id), value, false);
        return;
    }
    const bool decoded = general || status1 || status2 || stroke;
    notePacket(channelKey(*id), value, decoded);
    if (*id == pm5::kMultiplexed && !value.isEmpty())
        notePacket(QStringLiteral("0x0080/0x%1").arg(quint8(value.at(0)), 2, 16, QLatin1Char('0')), value, decoded);
    if (status1) {
        m_status1Seen = true;
        m_status1Watchdog.stop();
        m_status1Via = channelKey(*id);
        m_lastDecodedSpm = status1->strokesPerMinute;
        m_lastDecodedSpmMs = nowMs();
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
