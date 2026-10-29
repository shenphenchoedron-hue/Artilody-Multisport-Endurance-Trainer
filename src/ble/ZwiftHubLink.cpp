#include "ZwiftHubLink.h"

#include "DiagLog.h"

#include <cmath>
#include <limits>

namespace watt {

namespace {
QString stateName(ZwiftHubLink::State s)
{
    switch (s) {
    case ZwiftHubLink::State::Absent: return QStringLiteral("absent");
    case ZwiftHubLink::State::Available: return QStringLiteral("available");
    case ZwiftHubLink::State::Handshake: return QStringLiteral("handshake");
    case ZwiftHubLink::State::Stream: return QStringLiteral("starting data stream");
    case ZwiftHubLink::State::Active: return QStringLiteral("active");
    case ZwiftHubLink::State::Failed: return QStringLiteral("failed");
    }
    return QString();
}
} // namespace

QBluetoothUuid ZwiftHubLink::asyncUuid() { return QBluetoothUuid(zwift::asyncUuid()); }
QBluetoothUuid ZwiftHubLink::syncRxUuid() { return QBluetoothUuid(zwift::syncRxUuid()); }
QBluetoothUuid ZwiftHubLink::syncTxUuid() { return QBluetoothUuid(zwift::syncTxUuid()); }

ZwiftHubLink::ZwiftHubLink(GattLink *link, QObject *parent)
    : QObject(parent)
    , m_link(link)
{
    m_since.start();
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        if (m_state == State::Handshake) {
            if (m_attempt < kHandshakeAttempts) {
                DiagLog::write("ZHUB", QStringLiteral("no RideOn answer within %1 ms – retrying the handshake").arg(m_handshakeMs));
                sendHandshake();
                return;
            }
            // lenient: some trainers may skip the answer - try the stream anyway
            DiagLog::write("ZHUB", QStringLiteral("no RideOn answer after %1 attempts – trying to start the data stream anyway")
                                       .arg(kHandshakeAttempts));
            setState(State::Stream);
            writeMessage(zhub::startStream());
            m_timeout.start(m_streamMs);
        } else if (m_state == State::Stream) {
            if (m_trainerAnswered) {
                DiagLog::write("ZHUB", QStringLiteral("no riding data within %1 ms, but the trainer answered the stream start – "
                                                      "active, waiting for riding data (sent once the flywheel turns)")
                                           .arg(m_streamMs));
                becomeActive();
                return;
            }
            fail(m_rideOnSeen ? QStringLiteral("no riding data from the trainer after the handshake")
                              : QStringLiteral("no answer to the handshake"));
        }
    });
    m_silence.setSingleShot(true);
    m_silence.setInterval(kSilenceMs);
    connect(&m_silence, &QTimer::timeout, this, [this] {
        if (m_state != State::Active)
            return;
        if (!m_ridingSeen) {
            // still waiting for the first riding data: ask again, never fail here
            ++m_awaitRequests;
            if (awaitLogDue())
                DiagLog::write("ZHUB", QStringLiteral("still no riding data (%1. request) – requesting the data stream again")
                                           .arg(m_awaitRequests));
            writeMessage(zhub::startStream(), awaitLogDue());
            m_silence.start();
            return;
        }
        if (m_streamRetries < kStreamRetries) {
            ++m_streamRetries;
            DiagLog::write("ZHUB", QStringLiteral("keep-alive: nothing on 0002 for %1 ms – requesting the data stream again")
                                       .arg(m_silence.interval()));
            writeMessage(zhub::startStream());
            m_silence.start();
            return;
        }
        fail(QStringLiteral("the trainer stopped sending Zwift data"));
    });
    m_refresh.setInterval(500);
    connect(&m_refresh, &QTimer::timeout, this, &ZwiftHubLink::tick);
}

void ZwiftHubLink::setTimingForTesting(int handshakeMs, int streamMs, int silenceMs, int refreshMs)
{
    m_handshakeMs = handshakeMs;
    m_streamMs = streamMs;
    m_silence.setInterval(silenceMs);
    m_refresh.setInterval(refreshMs);
}

bool ZwiftHubLink::linkHasHub(const GattLink *link)
{
    if (!link)
        return false;
    bool service = false;
    for (const QBluetoothUuid &u : link->discoveredServices())
        if (zhub::isZwiftService(u))
            service = true;
    return service && link->hasCharacteristic(asyncUuid()) && link->hasCharacteristic(syncRxUuid())
           && link->hasCharacteristic(syncTxUuid());
}

void ZwiftHubLink::detect()
{
    const bool has = linkHasHub(m_link);
    if (has && m_state == State::Absent) {
        DiagLog::write("ZHUB", QStringLiteral("connected trainer exposes the Zwift service with 0002/0003/0004 – "
                                              "Zwift protocol available (used while a Zwift Click is connected)"));
        setState(State::Available);
    } else if (!has && m_state == State::Absent) {
        bool service = false;
        for (const QBluetoothUuid &u : m_link->discoveredServices())
            service = service || zhub::isZwiftService(u);
        DiagLog::write("ZHUB", service ? QStringLiteral("connected trainer has a Zwift service but not the 0002/0003/0004 "
                                                        "characteristics – Zwift protocol not available")
                                       : QStringLiteral("connected trainer has no Zwift service – Zwift protocol not available"));
    }
}

void ZwiftHubLink::start()
{
    if (m_state == State::Absent)
        return;
    m_failure.clear();
    m_attempt = 0;
    m_streamRetries = 0;
    m_lastSimMs = -1;
    m_lastEchoMs = -1;
    m_rideOnSeen = false;
    m_trainerAnswered = false;
    m_ridingSeen = false;
    m_movingSinceMs = -1;
    m_awaitRequests = 0;
    m_sentSim.reset();
    m_sentPhys.reset();
    m_pending.clear();
    DiagLog::write("ZHUB", QStringLiteral("handshake: subscribing 0002 (notify) + 0004 (indicate)"));
    const bool a = m_link->enableNotifications(asyncUuid());
    const bool t = m_link->enableNotifications(syncTxUuid(), true);
    if (!a || !t) {
        fail(QStringLiteral("could not subscribe to the Zwift characteristics"));
        return;
    }
    setState(State::Handshake);
    sendHandshake();
}

void ZwiftHubLink::sendHandshake()
{
    ++m_attempt;
    DiagLog::write("ZHUB", QStringLiteral("handshake attempt %1/%2").arg(m_attempt).arg(kHandshakeAttempts));
    if (!writeMessage(zhub::handshake())) {
        fail(QStringLiteral("could not write to the Zwift characteristic 0003"));
        return;
    }
    m_timeout.start(m_handshakeMs);
}

void ZwiftHubLink::stop()
{
    m_timeout.stop();
    m_silence.stop();
    m_refresh.stop();
    if (m_state != State::Absent && m_state != State::Available) {
        DiagLog::write("ZHUB", QStringLiteral("Zwift protocol stopped (FTMS only from here)"));
        setState(State::Available);
    }
}

void ZwiftHubLink::reset()
{
    m_timeout.stop();
    m_silence.stop();
    m_refresh.stop();
    m_failure.clear();
    m_sentSim.reset();
    m_sentPhys.reset();
    m_riding.reset();
    m_settings.reset();
    m_pending.clear();
    setState(State::Absent);
}

void ZwiftHubLink::setState(State s)
{
    if (m_state == s)
        return;
    m_state = s;
    DiagLog::write("ZHUB", QStringLiteral("state: %1").arg(stateName(s)));
    emit stateChanged();
}

void ZwiftHubLink::fail(const QString &reason)
{
    m_timeout.stop();
    m_silence.stop();
    m_refresh.stop();
    m_failure = reason;
    DiagLog::write("ZHUB", QStringLiteral("FAILED: %1 – falling back to FTMS gradient simulation").arg(reason));
    setState(State::Failed);
    emit failed(reason);
}

int ZwiftHubLink::hubState() const
{
    switch (m_state) {
    case State::Absent: return SensorHub::HubNone;
    case State::Available: return SensorHub::HubAvailable;
    case State::Handshake:
    case State::Stream: return SensorHub::HubStarting;
    case State::Active: return SensorHub::HubActive;
    case State::Failed: return SensorHub::HubFailed;
    }
    return SensorHub::HubNone;
}

QString ZwiftHubLink::stateText() const
{
    if (m_state == State::Failed)
        return QStringLiteral("failed: %1").arg(m_failure);
    return stateName(m_state);
}

bool ZwiftHubLink::writeMessage(const QByteArray &message, bool log)
{
    const bool ok = m_link->write(syncRxUuid(), message, false);
    if (log || !ok)
        DiagLog::write("ZHUB", QStringLiteral("write 0003 [%1] = %2%3").arg(DiagLog::hex(message), zhub::describe(message, true),
                                                                      ok ? QString() : QStringLiteral(" (NOT queued)")));
    return ok;
}

bool ZwiftHubLink::handleNotification(const QBluetoothUuid &uuid, const QByteArray &value)
{
    const bool async = uuid == asyncUuid();
    const bool tx = uuid == syncTxUuid();
    if (!async && !tx)
        return false;
    if (m_state == State::Absent)
        return true;
    onMessage(value, tx);
    return true;
}

void ZwiftHubLink::onMessage(const QByteArray &raw, bool fromTx)
{
    if (raw.isEmpty())
        return;
    if (zhub::isRideOn(raw)) {
        DiagLog::write("ZHUB", QStringLiteral("notify %1 [%2] = %3").arg(fromTx ? QStringLiteral("0004") : QStringLiteral("0002"),
                                                                        DiagLog::hex(raw), zhub::describe(raw, false)));
        if (m_state == State::Handshake) {
            m_rideOnSeen = true;
            setState(State::Stream);
            writeMessage(zhub::startStream());
            m_timeout.start(m_streamMs);
        }
        return;
    }
    // a message may be split over two notifications: retry with the fragment
    QByteArray value = raw;
    const quint8 prefix = quint8(value.at(0));
    const bool known = prefix == zhub::InRidingData || prefix == zhub::InDeviceData || prefix == zhub::InTrainerResponse
                       || prefix == zhub::InKeepAlive || prefix == zhub::InPairing;
    if (!known && !m_pending.isEmpty()) {
        value = m_pending + raw;
        m_pending.clear();
    }
    const quint8 type = quint8(value.at(0));
    const QByteArray body = value.mid(1);
    if ((type == zhub::InRidingData || type == zhub::InDeviceData) && !zwift::decodeFields(body)) {
        m_pending = value;   // truncated: wait for the rest
        return;
    }
    m_pending.clear();
    const bool riding = type == zhub::InRidingData;
    // riding data comes about once per second: log the first ones, then every
    // 10th; the answers to the re-requests while waiting for riding data too
    const bool logIt = riding ? (m_ridingLogCount < 5 || m_ridingLogCount % 10 == 0)
                              : (!awaitingRidingData() || awaitLogDue());
    if (logIt)
        DiagLog::write("ZHUB", QStringLiteral("notify %1 [%2] = %3").arg(fromTx ? QStringLiteral("0004") : QStringLiteral("0002"),
                                                                        DiagLog::hex(value), zhub::describe(value, false)));
    if (riding)
        ++m_ridingLogCount;
    if (!fromTx && m_state == State::Stream && m_rideOnSeen)
        m_trainerAnswered = true;
    if (riding && !m_ridingSeen) {
        m_ridingSeen = true;
        m_movingSinceMs = -1;
        if (m_state == State::Active)
            DiagLog::write("ZHUB", QStringLiteral("first riding data – the Zwift data stream runs"));
    }
    if (!fromTx && (m_state == State::Active)) {
        m_silence.start();
        m_streamRetries = 0;
    }
    switch (type) {
    case zhub::InRidingData:
        if (auto d = zhub::decodeRidingData(body)) {
            m_riding = d;
            emit ridingData(d->power, d->cadence, d->speedKmh());
        }
        if (m_state == State::Stream || m_state == State::Handshake)
            becomeActive();
        break;
    case zhub::InDeviceData:
        if (auto s = zhub::decodeDeviceData(body)) {
            m_settings = s;
            // the trainer's settings vs. what was sent (diagnostics)
            if (m_sentPhys && s->gearRatio && std::abs(*s->gearRatio - m_sentPhys->gearRatio) > 0.0001)
                DiagLog::write("ZHUB", QStringLiteral("settings echo: gear ratio %1 differs from the sent %2")
                                           .arg(*s->gearRatio, 0, 'f', 4).arg(m_sentPhys->gearRatio, 0, 'f', 4));
            if (m_sentSim && s->inclineWholePercent
                && std::abs(*s->inclineWholePercent - zhub::echoedIncline(m_sentSim->inclinePercent)) > 1)
                DiagLog::write("ZHUB", QStringLiteral("settings echo: incline %1 % (whole percent) differs from the sent %2 %")
                                           .arg(*s->inclineWholePercent)
                                           .arg(m_sentSim->inclinePercent, 0, 'f', 2));
            if (m_sentPhys && s->weight11Kg && m_sentPhys->riderKg > 0 && std::abs(*s->weight11Kg - m_sentPhys->riderKg) > 0.01)
                DiagLog::write("ZHUB", QStringLiteral("settings echo: weight (f11) %1 kg differs from the sent rider %2 kg")
                                           .arg(*s->weight11Kg, 0, 'f', 2)
                                           .arg(m_sentPhys->riderKg, 0, 'f', 2));
        }
        break;
    default:
        break;
    }
}

void ZwiftHubLink::becomeActive()
{
    m_timeout.stop();
    if (!m_rideOnSeen)
        DiagLog::write("ZHUB", QStringLiteral("riding data without a RideOn answer – continuing"));
    setState(State::Active);
    m_silence.start();
    m_refresh.start();
    if (!m_ridingSeen) {
        // Zwift's own start: device info request and the stream once more
        writeMessage(zhub::encodeRequest(zhub::kDeviceInfoDataId));
        writeMessage(zhub::startStream());
    }
    if (m_haveParams)
        update(m_params, true);
}

void ZwiftHubLink::noteFtmsMotion(bool moving)
{
    if (!awaitingRidingData()) {
        m_movingSinceMs = -1;
        return;
    }
    if (!moving) {
        m_movingSinceMs = -1;
        return;
    }
    const qint64 t = now();
    if (m_movingSinceMs < 0) {
        m_movingSinceMs = t;
        return;
    }
    if (t - m_movingSinceMs >= kNoRidingWhileMovingMs)
        fail(QStringLiteral("the trainer moves (FTMS) but sends no Zwift riding data"));
}

void ZwiftHubLink::update(const HubSimulation &p, bool force)
{
    m_params = p;
    m_haveParams = true;
    if (m_state != State::Active)
        return;
    const qint64 t = now();
    bool gearSent = false;
    // 1. gear / weights: at once, only the fields that changed
    {
        zhub::Physical phys;
        if (force || !m_sentPhys || std::abs(m_sentPhys->gearRatio - p.gearRatio) >= 0.00005)
            if (p.gearRatio > 0)
                phys.gearRatio = p.gearRatio;
        if (force || !m_sentPhys || std::abs(m_sentPhys->bikeKg - p.bikeKg) >= 0.005)
            if (p.bikeKg > 0)
                phys.bikeKg = p.bikeKg;
        if (force || !m_sentPhys || std::abs(m_sentPhys->riderKg - p.riderKg) >= 0.005)
            if (p.riderKg > 0)
                phys.riderKg = p.riderKg;
        if (phys.gearRatio || phys.bikeKg || phys.riderKg) {
            if (writeMessage(zhub::encodePhysical(phys))) {
                HubSimulation sent = m_sentPhys.value_or(p);
                if (phys.gearRatio) sent.gearRatio = p.gearRatio;
                if (phys.bikeKg) sent.bikeKg = p.bikeKg;
                if (phys.riderKg) sent.riderKg = p.riderKg;
                m_sentPhys = sent;
                m_echoWanted = true;
                gearSent = gearSent || bool(phys.gearRatio);
            }
        }
    }
    // 2. road: incline step >= 0.1 % (rate limited) or the refresh; CWa / Crr /
    // wind only when they changed
    bool simSent = false;
    {
        const qint64 since = m_lastSimMs < 0 ? std::numeric_limits<qint64>::max() : t - m_lastSimMs;
        const bool first = !m_sentSim || force;
        const bool inclineMoved = m_sentSim && std::abs(zhub::inclineX100(m_sentSim->inclinePercent)
                                                        - zhub::inclineX100(p.inclinePercent)) >= kMinInclineChange * 100 - 1e-9;
        zhub::Simulation sim;
        if (first || (inclineMoved && since >= kMinSimIntervalMs) || since >= kRefreshMs)
            sim.inclinePercent = p.inclinePercent;
        if (first || std::abs(m_sentSim->cw - p.cw) >= 0.00005)
            sim.cw = p.cw;
        if (first || std::abs(m_sentSim->crr - p.crr) >= 0.000005)
            sim.crr = p.crr;
        if (first || std::abs(m_sentSim->windMps - p.windMps) >= 0.005)
            sim.windMps = p.windMps;
        if (sim.inclinePercent || sim.cw || sim.crr || sim.windMps) {
            if (writeMessage(zhub::encodeSimulation(sim))) {
                simSent = true;
                HubSimulation sent = m_sentSim.value_or(p);
                if (sim.inclinePercent) {
                    sent.inclinePercent = p.inclinePercent;
                    m_lastSimMs = t;
                    if (inclineMoved || first)
                        m_echoWanted = true;
                }
                if (sim.cw) sent.cw = p.cw;
                if (sim.crr) sent.crr = p.crr;
                if (sim.windMps) sent.windMps = p.windMps;
                m_sentSim = sent;
            }
        }
    }
    // 3. after EVERY Simulation the gear ratio again, as Zwift does (its
    // sequence: Simulation -> Physical{gear} -> request 520). A gear sent
    // only before the first incline is not applied by these trainers
    // (qdomyos-zwift: "gear commands don't work until an inclination command
    // has been sent first") - v1.3.3 sent the start gear exactly once, BEFORE
    // the first Simulation, so the trainer rode without the virtual gear
    // until the first shift.
    if (simSent && p.gearRatio > 0) {
        zhub::Physical gear;
        gear.gearRatio = p.gearRatio;
        if (writeMessage(zhub::encodePhysical(gear))) {
            HubSimulation sent = m_sentPhys.value_or(p);
            sent.gearRatio = p.gearRatio;
            m_sentPhys = sent;
            gearSent = true;
        }
    }
    // 4. Zwift asks for the gear ratio (dataId 520) after every gear it sends
    if (gearSent)
        writeMessage(zhub::encodeRequest(zhub::kGearRatioDataId));
    // 5. settings echo (logged): after a change, at most every kEchoIntervalMs
    if (m_echoWanted && (m_lastEchoMs < 0 || t - m_lastEchoMs >= kEchoIntervalMs)) {
        if (writeMessage(zhub::encodeRequest(zhub::kSettingsDataId))) {
            m_lastEchoMs = t;
            m_echoWanted = false;
        }
    }
}

void ZwiftHubLink::tick()
{
    if (m_state == State::Active && m_haveParams)
        update(m_params, false);
}

} // namespace watt
