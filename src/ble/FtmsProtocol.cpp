#include "FtmsProtocol.h"

#include <QStringList>

#include <algorithm>
#include <cmath>

namespace watt::ftms {

namespace {

// Little-endian reader with bounds checking.
class Reader
{
public:
    explicit Reader(const QByteArray &d) : m_d(d) {}
    bool ok() const { return m_ok; }
    bool atEnd() const { return m_pos >= m_d.size(); }

    quint32 u(int bytes)
    {
        if (m_pos + bytes > m_d.size()) {
            m_ok = false;
            return 0;
        }
        quint32 v = 0;
        for (int i = 0; i < bytes; ++i)
            v |= quint32(quint8(m_d.at(m_pos + i))) << (8 * i);
        m_pos += bytes;
        return v;
    }
    quint8 u8() { return quint8(u(1)); }
    quint16 u16() { return quint16(u(2)); }
    quint32 u24() { return u(3); }
    quint32 u32() { return u(4); }
    qint16 s16() { return qint16(u16()); }

private:
    const QByteArray &m_d;
    int m_pos = 0;
    bool m_ok = true;
};

void putU16(QByteArray &b, quint16 v)
{
    b.append(char(v & 0xFF));
    b.append(char((v >> 8) & 0xFF));
}

QByteArray op(OpCode code)
{
    return QByteArray(1, char(quint8(code)));
}

} // namespace

std::optional<IndoorBikeData> parseIndoorBikeData(const QByteArray &data)
{
    Reader r(data);
    const quint16 flags = r.u16();
    if (!r.ok())
        return std::nullopt;

    IndoorBikeData d;
    // Bit 0 "More Data": when 0 the instantaneous speed field IS present.
    if (!(flags & (1 << 0)))
        d.speedKmh = r.u16() / 100.0;
    if (flags & (1 << 1))
        d.averageSpeedKmh = r.u16() / 100.0;
    if (flags & (1 << 2))
        d.cadenceRpm = r.u16() / 2.0;
    if (flags & (1 << 3))
        d.averageCadenceRpm = r.u16() / 2.0;
    if (flags & (1 << 4))
        d.totalDistanceM = int(r.u24());
    if (flags & (1 << 5))
        d.resistanceLevel = r.s16();
    if (flags & (1 << 6))
        d.powerW = r.s16();
    if (flags & (1 << 7))
        d.averagePowerW = r.s16();
    if (flags & (1 << 8)) {
        d.totalEnergyKcal = r.u16();
        r.u16(); // energy per hour
        r.u8();  // energy per minute
    }
    if (flags & (1 << 9))
        d.heartRateBpm = r.u8();
    if (flags & (1 << 10))
        r.u8(); // metabolic equivalent
    if (flags & (1 << 11))
        d.elapsedTimeS = r.u16();
    if (flags & (1 << 12))
        d.remainingTimeS = r.u16();

    if (!r.ok())
        return std::nullopt;
    return d;
}

std::optional<ServiceData> parseServiceData(const QByteArray &raw)
{
    QByteArray data = raw;
    // Some stacks hand over the AD payload including the 16-bit UUID.
    if (data.size() >= 4 && quint8(data.at(0)) == 0x26 && quint8(data.at(1)) == 0x18)
        data = data.mid(2);
    if (data.size() < 2)
        return std::nullopt;
    ServiceData d;
    d.available = quint8(data.at(0)) & 0x01;
    if (data.size() == 2) {
        // Lenient: Jetblack Victory / Zwift Hub send Flags + a 1-byte type
        // ("01 20" = available, Indoor Bike) instead of the UInt16 type.
        d.machineType = quint8(data.at(1));
        d.shortForm = true;
    } else {
        d.machineType = quint16(quint8(data.at(1)) | (quint8(data.at(2)) << 8));
    }
    return d;
}

QString machineTypeText(quint16 t)
{
    static const char *names[] = { "Treadmill", "Cross Trainer", "Step Climber", "Stair Climber", "Rower", "Indoor Bike" };
    QStringList parts;
    for (int i = 0; i < 6; ++i) {
        if (t & (1 << i))
            parts << QString::fromLatin1(names[i]);
    }
    if (t & ~quint16(0x3F))
        parts << QStringLiteral("RFU bits 0x%1").arg(t & ~quint16(0x3F), 4, 16, QLatin1Char('0'));
    return parts.isEmpty() ? QStringLiteral("none") : parts.join(QStringLiteral(", "));
}

std::optional<RowerData> parseRowerData(const QByteArray &data)
{
    Reader r(data);
    const quint16 flags = r.u16();
    if (!r.ok())
        return std::nullopt;

    RowerData d;
    d.moreData = flags & (1 << 0);
    // Bit 0 "More Data" has inverted semantics: stroke rate and stroke count
    // are present when it is 0.
    if (!d.moreData) {
        d.strokeRateSpm = r.u8() / 2.0;
        d.strokeCount = r.u16();
    }
    if (flags & (1 << 1))
        d.averageStrokeRateSpm = r.u8() / 2.0;
    if (flags & (1 << 2))
        d.totalDistanceM = int(r.u24());
    if (flags & (1 << 3))
        d.pace500S = r.u16();
    if (flags & (1 << 4))
        d.averagePace500S = r.u16();
    if (flags & (1 << 5))
        d.powerW = r.s16();
    if (flags & (1 << 6))
        d.averagePowerW = r.s16();
    if (flags & (1 << 7))
        d.resistanceLevel = r.s16();
    if (flags & (1 << 8)) {
        d.totalEnergyKcal = r.u16();
        d.energyPerHourKcal = r.u16();
        d.energyPerMinuteKcal = r.u8();
    }
    if (flags & (1 << 9))
        d.heartRateBpm = r.u8();
    if (flags & (1 << 10))
        d.metabolicEquivalent = r.u8() / 10.0;
    if (flags & (1 << 11))
        d.elapsedTimeS = r.u16();
    if (flags & (1 << 12))
        d.remainingTimeS = r.u16();

    if (!r.ok())
        return std::nullopt;
    return d;
}

namespace {
template<typename F>
void forEachField(RowerData &a, const RowerData &b, F f)
{
    f(a.strokeRateSpm, b.strokeRateSpm);
    f(a.strokeCount, b.strokeCount);
    f(a.averageStrokeRateSpm, b.averageStrokeRateSpm);
    f(a.totalDistanceM, b.totalDistanceM);
    f(a.pace500S, b.pace500S);
    f(a.averagePace500S, b.averagePace500S);
    f(a.powerW, b.powerW);
    f(a.averagePowerW, b.averagePowerW);
    f(a.resistanceLevel, b.resistanceLevel);
    f(a.totalEnergyKcal, b.totalEnergyKcal);
    f(a.energyPerHourKcal, b.energyPerHourKcal);
    f(a.energyPerMinuteKcal, b.energyPerMinuteKcal);
    f(a.heartRateBpm, b.heartRateBpm);
    f(a.metabolicEquivalent, b.metabolicEquivalent);
    f(a.elapsedTimeS, b.elapsedTimeS);
    f(a.remainingTimeS, b.remainingTimeS);
}
} // namespace

void RowerData::merge(const RowerData &other)
{
    forEachField(*this, other, [](auto &mine, const auto &theirs) {
        if (theirs)
            mine = theirs;
    });
    moreData = other.moreData;
}

bool RowerData::overlaps(const RowerData &other) const
{
    bool hit = false;
    RowerData copy = *this;
    forEachField(copy, other, [&hit](auto &mine, const auto &theirs) {
        if (mine && theirs)
            hit = true;
    });
    return hit;
}

bool RowerData::isEmpty() const
{
    return !strokeRateSpm && !strokeCount && !averageStrokeRateSpm && !totalDistanceM
           && !pace500S && !averagePace500S && !powerW && !averagePowerW && !resistanceLevel && !totalEnergyKcal
           && !energyPerHourKcal && !energyPerMinuteKcal && !heartRateBpm && !metabolicEquivalent && !elapsedTimeS
           && !remainingTimeS;
}

std::optional<RowerData> RowerDataAssembler::feed(const QByteArray &packet)
{
    const auto d = parseRowerData(packet);
    if (!d)
        return std::nullopt;
    if (d->moreData) {
        if (m_pending && m_pending->overlaps(*d)) {
            // A new record started without the previous one being completed.
            RowerData done = *m_pending;
            m_pending = *d;
            return done;
        }
        if (!m_pending)
            m_pending = RowerData();
        m_pending->merge(*d);
        return std::nullopt;
    }
    RowerData done = m_pending ? *m_pending : RowerData();
    m_pending.reset();
    done.merge(*d);
    return done;
}

std::optional<Features> parseFeatures(const QByteArray &data)
{
    Reader r(data);
    const quint32 machine = r.u32();
    const quint32 target = r.u32();
    if (!r.ok())
        return std::nullopt;
    Features f;
    f.cadence = machine & (1u << 1);
    f.heartRate = machine & (1u << 10);
    f.powerMeasurement = machine & (1u << 14);
    f.resistanceTarget = target & (1u << 2);
    f.powerTarget = target & (1u << 3);
    f.simulation = target & (1u << 13);
    return f;
}

std::optional<PowerRange> parsePowerRange(const QByteArray &data)
{
    Reader r(data);
    PowerRange p;
    p.minimum = r.s16();
    p.maximum = r.s16();
    p.increment = r.u16();
    if (!r.ok())
        return std::nullopt;
    return p;
}

double InclinationRange::clamp(double gradePercent) const
{
    if (!(maximumPercent >= minimumPercent))
        return gradePercent;
    return std::clamp(gradePercent, minimumPercent, maximumPercent);
}

std::optional<InclinationRange> parseInclinationRange(const QByteArray &data)
{
    Reader r(data);
    InclinationRange range;
    range.minimumPercent = r.s16() / 10.0;
    range.maximumPercent = r.s16() / 10.0;
    range.incrementPercent = r.u16() / 10.0;
    if (!r.ok() || range.maximumPercent < range.minimumPercent)
        return std::nullopt;
    return range;
}

QString commandNoun(quint8 opCode)
{
    switch (OpCode(opCode)) {
    case OpCode::RequestControl: return QStringLiteral("control request");
    case OpCode::Reset: return QStringLiteral("reset command");
    case OpCode::SetTargetResistance: return QStringLiteral("resistance command");
    case OpCode::SetTargetPower: return QStringLiteral("target power command");
    case OpCode::StartOrResume: return QStringLiteral("start command");
    case OpCode::StopOrPause: return QStringLiteral("stop command");
    case OpCode::SetIndoorBikeSimulation: return QStringLiteral("gradient command");
    default: break;
    }
    return QStringLiteral("command 0x%1").arg(opCode, 2, 16, QLatin1Char('0'));
}

QString rejectionText(quint8 opCode, quint8 result)
{
    if (opCode == quint8(OpCode::RequestControl))
        return QStringLiteral("Trainer refused control (%1)").arg(resultCodeText(result));
    return QStringLiteral("Trainer rejected the %1 (%2)").arg(commandNoun(opCode), resultCodeText(result));
}

QString timeoutText(quint8 opCode, int timeoutMs)
{
    return QStringLiteral("Trainer did not answer the %1 (no response within %2 s)")
        .arg(commandNoun(opCode))
        .arg(timeoutMs / 1000.0, 0, 'g', 2);
}

std::optional<ControlPointResponse> parseControlPointResponse(const QByteArray &data)
{
    if (data.size() < 3 || quint8(data.at(0)) != quint8(OpCode::ResponseCode))
        return std::nullopt;
    return ControlPointResponse { quint8(data.at(1)), quint8(data.at(2)) };
}

QString resultCodeText(quint8 result)
{
    switch (ResultCode(result)) {
    case ResultCode::Success: return QStringLiteral("Success");
    case ResultCode::NotSupported: return QStringLiteral("Not supported");
    case ResultCode::InvalidParameter: return QStringLiteral("Invalid parameter");
    case ResultCode::OperationFailed: return QStringLiteral("Operation failed");
    case ResultCode::ControlNotPermitted: return QStringLiteral("Control not permitted");
    }
    return QStringLiteral("Unknown (%1)").arg(result);
}

ControlRefusalGuard::Action ControlRefusalGuard::onNotPermitted(quint8 opCode)
{
    if (m_count > 0 && m_op == opCode) {
        ++m_count;
    } else {
        m_op = opCode;
        m_count = 1;
    }
    return m_count <= kMaxImmediate ? Action::RequestNow : Action::RetryLater;
}

void ControlRefusalGuard::onSuccess(quint8 opCode)
{
    if (opCode == m_op)
        reset();
}

void ControlRefusalGuard::reset()
{
    m_op = 0;
    m_count = 0;
}

namespace {
// value * scale rounded and clamped to [lo, hi]; `fallback` (already scaled)
// for NaN / infinity. Clamping before rounding keeps lround() defined.
long scaledField(double value, double scale, long lo, long hi, long fallback)
{
    if (!std::isfinite(value))
        return fallback;
    return std::lround(std::clamp(value * scale, double(lo), double(hi)));
}
} // namespace

QByteArray requestControl() { return op(OpCode::RequestControl); }
QByteArray reset() { return op(OpCode::Reset); }
QByteArray startOrResume() { return op(OpCode::StartOrResume); }

QByteArray stopOrPause(bool pause)
{
    QByteArray b = op(OpCode::StopOrPause);
    b.append(char(pause ? 0x02 : 0x01));
    return b;
}

QByteArray setTargetPower(int watts)
{
    QByteArray b = op(OpCode::SetTargetPower);
    putU16(b, quint16(qint16(std::clamp(watts, 0, 4000))));
    return b;
}

QByteArray setTargetResistance(double level)
{
    QByteArray b = op(OpCode::SetTargetResistance);
    b.append(char(quint8(scaledField(level, 10, 0, 255, 0))));
    return b;
}

QByteArray setIndoorBikeSimulation(double windSpeedMs, double gradePercent, double crr, double cw)
{
    QByteArray b = op(OpCode::SetIndoorBikeSimulation);
    putU16(b, quint16(qint16(scaledField(windSpeedMs, 1000, -32768, 32767, 0))));
    putU16(b, quint16(qint16(scaledField(gradePercent, 100, -32768, 32767, 0))));
    b.append(char(quint8(scaledField(crr, 10000, 0, 255, 40))));    // 0.004
    b.append(char(quint8(scaledField(cw, 100, 0, 255, 39))));       // 0.39 kg/m
    return b;
}

// --- readable text --------------------------------------------------------------

namespace {
QString num(double v, int decimals) { return QString::number(v, 'f', decimals); }

// The simulation parameter block of 0x11 (control point) / 0x12 (status).
QString simulationParams(Reader &r)
{
    const double wind = r.s16() / 1000.0;
    const double grade = r.s16() / 100.0;
    const double crr = r.u8() / 10000.0;
    const double cw = r.u8() / 100.0;
    if (!r.ok())
        return QStringLiteral("(truncated)");
    return QStringLiteral("wind %1 m/s, grade %2 %, Crr %3, Cw %4 kg/m")
        .arg(num(wind, 3), num(grade, 2), num(crr, 4), num(cw, 2));
}

QString withOp(quint8 op)
{
    return QStringLiteral("0x%1 %2").arg(op, 2, 16, QLatin1Char('0')).arg(opCodeName(op));
}

QString trailing(const Reader &r, const QByteArray &data, int consumed)
{
    Q_UNUSED(r)
    if (data.size() <= consumed)
        return QString();
    return QStringLiteral(" (+%1)").arg(QString::fromLatin1(data.mid(consumed).toHex(' ')));
}
} // namespace

QString opCodeName(quint8 opCode)
{
    switch (opCode) {
    case 0x00: return QStringLiteral("Request Control");
    case 0x01: return QStringLiteral("Reset");
    case 0x02: return QStringLiteral("Set Target Speed");
    case 0x03: return QStringLiteral("Set Target Inclination");
    case 0x04: return QStringLiteral("Set Target Resistance Level");
    case 0x05: return QStringLiteral("Set Target Power");
    case 0x06: return QStringLiteral("Set Target Heart Rate");
    case 0x07: return QStringLiteral("Start or Resume");
    case 0x08: return QStringLiteral("Stop or Pause");
    case 0x09: return QStringLiteral("Set Targeted Expended Energy");
    case 0x0A: return QStringLiteral("Set Targeted Number of Steps");
    case 0x0B: return QStringLiteral("Set Targeted Number of Strides");
    case 0x0C: return QStringLiteral("Set Targeted Distance");
    case 0x0D: return QStringLiteral("Set Targeted Training Time");
    case 0x0E: return QStringLiteral("Set Targeted Time in Two HR Zones");
    case 0x0F: return QStringLiteral("Set Targeted Time in Three HR Zones");
    case 0x10: return QStringLiteral("Set Targeted Time in Five HR Zones");
    case 0x11: return QStringLiteral("Set Indoor Bike Simulation");
    case 0x12: return QStringLiteral("Set Wheel Circumference");
    case 0x13: return QStringLiteral("Spin Down Control");
    case 0x14: return QStringLiteral("Set Targeted Cadence");
    case 0x80: return QStringLiteral("Response Code");
    }
    return QStringLiteral("Op 0x%1").arg(opCode, 2, 16, QLatin1Char('0'));
}

QString describeControlPoint(const QByteArray &data)
{
    if (data.isEmpty())
        return QStringLiteral("(empty)");
    Reader r(data);
    const quint8 op = r.u8();
    QString params;
    int used = 1;
    switch (op) {
    case 0x02: params = QStringLiteral("%1 km/h").arg(num(r.u16() / 100.0, 2)); used = 3; break;
    case 0x03: params = QStringLiteral("%1 %").arg(num(r.s16() / 10.0, 1)); used = 3; break;
    case 0x04: params = QStringLiteral("level %1").arg(num(r.u8() / 10.0, 1)); used = 2; break;
    case 0x05: params = QStringLiteral("%1 W").arg(r.s16()); used = 3; break;
    case 0x06: params = QStringLiteral("%1 bpm").arg(r.u8()); used = 2; break;
    case 0x08: {
        const quint8 p = r.u8();
        params = p == 1 ? QStringLiteral("stop") : p == 2 ? QStringLiteral("pause") : QStringLiteral("param %1").arg(p);
        used = 2;
        break;
    }
    case 0x11: params = simulationParams(r); used = 7; break;
    case 0x12: params = QStringLiteral("%1 mm").arg(num(r.u16() / 10.0, 1)); used = 3; break;
    case 0x13: params = r.u8() == 1 ? QStringLiteral("start") : QStringLiteral("ignore"); used = 2; break;
    case 0x14: params = QStringLiteral("%1 rpm").arg(num(r.u16() / 2.0, 1)); used = 3; break;
    default: break;
    }
    if (!r.ok())
        return withOp(op) + QStringLiteral(": (truncated)");
    QString s = withOp(op);
    if (!params.isEmpty())
        s += QStringLiteral(": ") + params;
    return s + trailing(r, data, used);
}

QString describeControlPointResponse(const QByteArray &data)
{
    const auto resp = parseControlPointResponse(data);
    if (!resp)
        return QStringLiteral("(not a response: %1)").arg(QString::fromLatin1(data.toHex(' ')));
    QString s = QStringLiteral("Response to %1: %2 (0x%3)")
                    .arg(withOp(resp->requestOpCode), resultCodeText(resp->result))
                    .arg(resp->result, 2, 16, QLatin1Char('0'));
    if (data.size() > 3)
        s += QStringLiteral(" params %1").arg(QString::fromLatin1(data.mid(3).toHex(' ')));
    return s;
}

QString describeMachineStatus(const QByteArray &data)
{
    if (data.isEmpty())
        return QStringLiteral("(empty)");
    Reader r(data);
    const quint8 op = r.u8();
    QString s;
    int used = 1;
    switch (op) {
    case 0x01: s = QStringLiteral("Reset"); break;
    case 0x02: {
        const quint8 p = r.u8();
        s = QStringLiteral("Stopped or Paused by the User (%1)")
                .arg(p == 1 ? QStringLiteral("stop") : p == 2 ? QStringLiteral("pause") : QString::number(p));
        used = 2;
        break;
    }
    case 0x03: s = QStringLiteral("Stopped by Safety Key"); break;
    case 0x04: s = QStringLiteral("Started or Resumed by the User"); break;
    case 0x05: s = QStringLiteral("Target Speed Changed: %1 km/h").arg(num(r.u16() / 100.0, 2)); used = 3; break;
    case 0x06: s = QStringLiteral("Target Incline Changed: %1 %").arg(num(r.s16() / 10.0, 1)); used = 3; break;
    case 0x07: s = QStringLiteral("Target Resistance Level Changed: %1").arg(num(r.u8() / 10.0, 1)); used = 2; break;
    case 0x08: s = QStringLiteral("Target Power Changed: %1 W").arg(r.s16()); used = 3; break;
    case 0x09: s = QStringLiteral("Target Heart Rate Changed: %1 bpm").arg(r.u8()); used = 2; break;
    case 0x12: s = QStringLiteral("Indoor Bike Simulation Parameters Changed: ") + simulationParams(r); used = 7; break;
    case 0x13: s = QStringLiteral("Wheel Circumference Changed: %1 mm").arg(num(r.u16() / 10.0, 1)); used = 3; break;
    case 0x14: {
        const quint8 p = r.u8();
        static const char *names[] = { "?", "Spin Down Requested", "Success", "Error", "Stop Pedaling" };
        s = QStringLiteral("Spin Down Status: %1").arg(QString::fromLatin1(p <= 4 ? names[p] : "?"));
        used = 2;
        break;
    }
    case 0x15: s = QStringLiteral("Targeted Cadence Changed: %1 rpm").arg(num(r.u16() / 2.0, 1)); used = 3; break;
    case 0xFF: s = QStringLiteral("Control Permission Lost"); break;
    default:
        s = op >= 0x0A && op <= 0x11 ? QStringLiteral("Training target changed (0x%1)").arg(op, 2, 16, QLatin1Char('0'))
                                     : QStringLiteral("Status 0x%1").arg(op, 2, 16, QLatin1Char('0'));
        used = 1;
        break;
    }
    if (!r.ok())
        return s + QStringLiteral(" (truncated)");
    return s + trailing(r, data, used);
}

QString describeTrainingStatus(const QByteArray &data)
{
    if (data.size() < 2)
        return QStringLiteral("(too short: %1)").arg(QString::fromLatin1(data.toHex(' ')));
    const quint8 flags = quint8(data.at(0));
    const quint8 st = quint8(data.at(1));
    static const char *names[] = {
        "Other", "Idle", "Warming Up", "Low Intensity Interval", "High Intensity Interval", "Recovery Interval",
        "Isometric", "Heart Rate Control", "Fitness Test", "Speed Outside of Control Region - Low",
        "Speed Outside of Control Region - High", "Cool Down", "Watt Control", "Manual Mode (Quick Start)",
        "Pre-Workout", "Post-Workout",
    };
    QString s = st < sizeof(names) / sizeof(names[0]) ? QString::fromLatin1(names[st])
                                                       : QStringLiteral("Status 0x%1").arg(st, 2, 16, QLatin1Char('0'));
    if ((flags & 0x01) && data.size() > 2)
        s += QStringLiteral(" \"%1\"").arg(QString::fromUtf8(data.mid(2)));
    return s;
}

QString describeFeatures(const QByteArray &data)
{
    Reader r(data);
    const quint32 machine = r.u32();
    const quint32 target = r.u32();
    if (!r.ok())
        return QStringLiteral("(unreadable: %1)").arg(QString::fromLatin1(data.toHex(' ')));
    static const char *machineNames[] = {
        "Average Speed", "Cadence", "Total Distance", "Inclination", "Elevation Gain", "Pace", "Step Count",
        "Resistance Level", "Stride Count", "Expended Energy", "Heart Rate", "Metabolic Equivalent",
        "Elapsed Time", "Remaining Time", "Power Measurement", "Force on Belt and Power Output", "User Data Retention",
    };
    static const char *targetNames[] = {
        "Speed Target", "Inclination Target", "Resistance Target", "Power Target", "Heart Rate Target",
        "Targeted Expended Energy", "Targeted Step Number", "Targeted Stride Number", "Targeted Distance",
        "Targeted Training Time", "Targeted Time in Two HR Zones", "Targeted Time in Three HR Zones",
        "Targeted Time in Five HR Zones", "Indoor Bike Simulation", "Wheel Circumference", "Spin Down Control",
        "Targeted Cadence",
    };
    auto names = [](quint32 bits, const char *const *table, int count) {
        QStringList out;
        for (int i = 0; i < 32; ++i) {
            if (!(bits & (1u << i)))
                continue;
            out << (i < count ? QString::fromLatin1(table[i]) : QStringLiteral("bit %1").arg(i));
        }
        return out.isEmpty() ? QStringLiteral("none") : out.join(QStringLiteral(", "));
    };
    return QStringLiteral("machine [%1] (0x%2); targets [%3] (0x%4)")
        .arg(names(machine, machineNames, 17))
        .arg(machine, 8, 16, QLatin1Char('0'))
        .arg(names(target, targetNames, 17))
        .arg(target, 8, 16, QLatin1Char('0'));
}

QString describeRange(quint16 uuid, const QByteArray &data)
{
    Reader r(data);
    const qint16 lo = r.s16();
    const qint16 hi = r.s16();
    const quint16 inc = r.u16();
    if (!r.ok())
        return QStringLiteral("(unreadable: %1)").arg(QString::fromLatin1(data.toHex(' ')));
    switch (uuid) {
    case kSupportedInclinationRangeUuid:
        return QStringLiteral("inclination %1..%2 %, step %3 %").arg(num(lo / 10.0, 1), num(hi / 10.0, 1), num(inc / 10.0, 1));
    case kSupportedResistanceRangeUuid:
        return QStringLiteral("resistance level %1..%2, step %3").arg(num(lo / 10.0, 1), num(hi / 10.0, 1), num(inc / 10.0, 1));
    case kSupportedPowerRangeUuid:
        return QStringLiteral("power %1..%2 W, step %3 W").arg(lo).arg(hi).arg(inc);
    default:
        return QStringLiteral("%1..%2 step %3").arg(lo).arg(hi).arg(inc);
    }
}

QString describeIndoorBikeData(const IndoorBikeData &d)
{
    QStringList p;
    if (d.speedKmh)
        p << QStringLiteral("speed %1 km/h").arg(num(*d.speedKmh, 2));
    if (d.cadenceRpm)
        p << QStringLiteral("cadence %1 rpm").arg(num(*d.cadenceRpm, 1));
    if (d.powerW)
        p << QStringLiteral("power %1 W").arg(*d.powerW);
    if (d.resistanceLevel)
        p << QStringLiteral("resistance %1").arg(*d.resistanceLevel);
    if (d.heartRateBpm)
        p << QStringLiteral("HR %1").arg(*d.heartRateBpm);
    if (d.averageSpeedKmh)
        p << QStringLiteral("avg speed %1").arg(num(*d.averageSpeedKmh, 2));
    if (d.averagePowerW)
        p << QStringLiteral("avg power %1").arg(*d.averagePowerW);
    if (d.totalDistanceM)
        p << QStringLiteral("distance %1 m").arg(*d.totalDistanceM);
    if (d.elapsedTimeS)
        p << QStringLiteral("elapsed %1 s").arg(*d.elapsedTimeS);
    return p.isEmpty() ? QStringLiteral("(no fields)") : p.join(QStringLiteral(", "));
}

} // namespace watt::ftms
