#include "FtmsProtocol.h"

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
    b.append(char(quint8(std::clamp(int(std::lround(level * 10)), 0, 255))));
    return b;
}

QByteArray setIndoorBikeSimulation(double windSpeedMs, double gradePercent, double crr, double cw)
{
    QByteArray b = op(OpCode::SetIndoorBikeSimulation);
    putU16(b, quint16(qint16(std::clamp(std::lround(windSpeedMs * 1000), -32768L, 32767L))));
    putU16(b, quint16(qint16(std::clamp(std::lround(gradePercent * 100), -32768L, 32767L))));
    b.append(char(quint8(std::clamp(std::lround(crr * 10000), 0L, 255L))));
    b.append(char(quint8(std::clamp(std::lround(cw * 100), 0L, 255L))));
    return b;
}

} // namespace watt::ftms
