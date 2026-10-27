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
    if (data.size() >= 5 && quint8(data.at(0)) == 0x26 && quint8(data.at(1)) == 0x18)
        data = data.mid(2);
    if (data.size() < 3)
        return std::nullopt;
    ServiceData d;
    d.available = quint8(data.at(0)) & 0x01;
    d.machineType = quint16(quint8(data.at(1)) | (quint8(data.at(2)) << 8));
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

} // namespace watt::ftms
