#include "ZwiftHubProtocol.h"

#include <QStringList>
#include <cmath>

namespace watt::zhub {

namespace {
QByteArray withPrefix(quint8 prefix, const QByteArray &body)
{
    QByteArray out;
    out.append(char(prefix));
    out.append(body);
    return out;
}
void appendTag(QByteArray &out, int field, int wireType)
{
    appendVarint(out, quint64((field << 3) | wireType));
}
void appendUInt(QByteArray &out, int field, quint64 v)
{
    appendTag(out, field, 0);
    appendVarint(out, v);
}
void appendSInt(QByteArray &out, int field, qint64 v)
{
    appendTag(out, field, 0);
    appendVarint(out, zigzag(v));
}
void appendMessage(QByteArray &out, int field, const QByteArray &msg)
{
    appendTag(out, field, 2);
    appendVarint(out, quint64(msg.size()));
    out.append(msg);
}
quint64 nonNegative(double v)
{
    return std::isfinite(v) && v > 0 ? quint64(std::llround(v)) : 0;
}
QString num(double v, int decimals) { return QString::number(v, 'f', decimals); }
} // namespace

bool isZwiftService(const QUuid &service)
{
    // 0xFC82 on the Bluetooth base UUID 0000xxxx-0000-1000-8000-00805F9B34FB
    static const QUuid fc82(quint32(zwift::kServiceUuid16), 0x0000, 0x1000, 0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB);
    return service == zwift::serviceUuid() || service == fc82;
}

void appendVarint(QByteArray &out, quint64 v)
{
    do {
        quint8 b = v & 0x7F;
        v >>= 7;
        if (v)
            b |= 0x80;
        out.append(char(b));
    } while (v);
}

quint64 zigzag(qint64 v) { return (quint64(v) << 1) ^ quint64(v >> 63); }
qint64 unzigzag(quint64 v) { return qint64(v >> 1) ^ -qint64(v & 1); }

QByteArray handshake()
{
    QByteArray out("RideOn");
    out.append(char(0x02));
    out.append(char(0x01));
    return out;
}

QByteArray startStream()
{
    return QByteArray::fromHex("410805");
}

int inclineX100(double percent)
{
    if (!std::isfinite(percent))
        return 1;
    const long v = std::lround(percent * 100.0);
    return v == 0 ? 1 : int(v);
}

QByteArray encodeSimulation(const Simulation &s)
{
    QByteArray sim;
    if (s.windMps)
        appendSInt(sim, 1, std::isfinite(*s.windMps) ? std::llround(*s.windMps * 100.0) : 0);
    if (s.inclinePercent)
        appendSInt(sim, 2, inclineX100(*s.inclinePercent));
    if (s.cw)
        appendUInt(sim, 3, nonNegative(*s.cw * 10000.0));
    if (s.crr)
        appendUInt(sim, 4, nonNegative(*s.crr * 100000.0));
    QByteArray cmd;
    appendMessage(cmd, 4, sim);
    return withPrefix(OutCommand, cmd);
}

QByteArray encodePhysical(const Physical &p)
{
    QByteArray phys;
    if (p.gearRatio)
        appendUInt(phys, 2, nonNegative(*p.gearRatio * 10000.0));
    if (p.bikeKg)
        appendUInt(phys, 4, nonNegative(*p.bikeKg * 100.0));
    if (p.riderKg)
        appendUInt(phys, 5, nonNegative(*p.riderKg * 100.0));
    QByteArray cmd;
    appendMessage(cmd, 5, phys);
    return withPrefix(OutCommand, cmd);
}

QByteArray encodePowerTarget(int watts)
{
    QByteArray cmd;
    appendUInt(cmd, 3, quint64(std::max(0, watts)));
    return withPrefix(OutCommand, cmd);
}

QByteArray encodeRequest(quint32 dataId)
{
    QByteArray req;
    appendUInt(req, 1, dataId);
    return withPrefix(OutRequest, req);
}

std::optional<RidingData> decodeRidingData(const QByteArray &payload)
{
    const auto fields = zwift::decodeFields(payload);
    if (!fields)
        return std::nullopt;
    RidingData d;
    d.power = int(zwift::varintField(*fields, 1).value_or(0));
    d.cadence = int(zwift::varintField(*fields, 2).value_or(0));
    d.speedKmh = zwift::varintField(*fields, 3).value_or(0) / 100.0;
    d.heartRate = int(zwift::varintField(*fields, 4).value_or(0));
    return d;
}

std::optional<Settings> decodeDeviceData(const QByteArray &payload)
{
    const auto env = zwift::decodeFields(payload);
    if (!env)
        return std::nullopt;
    Settings s;
    s.messageType = int(zwift::varintField(*env, 1).value_or(0));
    if (s.messageType < 512 || s.messageType > 525)
        return std::nullopt;
    QByteArray inner;
    for (const zwift::PbField &f : *env)
        if (f.number == 2 && f.wireType == 2)
            inner = f.bytes;
    const auto settings = zwift::decodeFields(inner);
    if (!settings)
        return std::nullopt;
    QByteArray sub;
    for (const zwift::PbField &f : *settings)
        if (f.number == 1 && f.wireType == 2)
            sub = f.bytes;
    const auto c = zwift::decodeFields(sub);
    if (!c)
        return std::nullopt;
    if (auto v = zwift::varintField(*c, 5))
        s.windMps = unzigzag(*v) / 100.0;
    if (auto v = zwift::varintField(*c, 6))
        s.inclinePercent = unzigzag(*v) / 100.0;
    if (auto v = zwift::varintField(*c, 8))
        s.gearRatio = *v / 10000.0;
    if (auto v = zwift::varintField(*c, 11))
        s.bikeKg = *v / 100.0;
    if (auto v = zwift::varintField(*c, 12))
        s.riderKg = *v / 100.0;
    if (auto v = zwift::varintField(*c, 13))
        s.crr = *v / 100000.0;
    return s;
}

std::optional<QString> decodeTrainerResponse(const QByteArray &payload)
{
    const auto fields = zwift::decodeFields(payload);
    if (!fields)
        return std::nullopt;
    for (const zwift::PbField &f : *fields) {
        if (f.number != 2 || f.wireType != 2)
            continue;
        const auto content = zwift::decodeFields(f.bytes);
        if (!content)
            return std::nullopt;
        for (const zwift::PbField &t : *content)
            if (t.number == 4 && t.wireType == 2)
                return QString::fromLatin1(t.bytes);
    }
    return QString();
}

std::optional<Command> decodeCommand(const QByteArray &payload)
{
    const auto fields = zwift::decodeFields(payload);
    if (!fields)
        return std::nullopt;
    Command c;
    for (const zwift::PbField &f : *fields) {
        if (f.number == 3 && f.wireType == 0) {
            c.powerTarget = int(f.value);
        } else if (f.number == 4 && f.wireType == 2) {
            const auto s = zwift::decodeFields(f.bytes);
            if (!s)
                return std::nullopt;
            Simulation sim;
            if (auto v = zwift::varintField(*s, 1))
                sim.windMps = unzigzag(*v) / 100.0;
            if (auto v = zwift::varintField(*s, 2))
                sim.inclinePercent = unzigzag(*v) / 100.0;
            if (auto v = zwift::varintField(*s, 3))
                sim.cw = *v / 10000.0;
            if (auto v = zwift::varintField(*s, 4))
                sim.crr = *v / 100000.0;
            c.simulation = sim;
        } else if (f.number == 5 && f.wireType == 2) {
            const auto p = zwift::decodeFields(f.bytes);
            if (!p)
                return std::nullopt;
            Physical phys;
            if (auto v = zwift::varintField(*p, 2))
                phys.gearRatio = *v / 10000.0;
            if (auto v = zwift::varintField(*p, 4))
                phys.bikeKg = *v / 100.0;
            if (auto v = zwift::varintField(*p, 5))
                phys.riderKg = *v / 100.0;
            c.physical = phys;
        }
    }
    return c;
}

std::optional<quint32> decodeRequest(const QByteArray &payload)
{
    const auto fields = zwift::decodeFields(payload);
    if (!fields)
        return std::nullopt;
    return quint32(zwift::varintField(*fields, 1).value_or(0));
}

QByteArray encodeRidingData(const RidingData &d)
{
    QByteArray body;
    appendUInt(body, 1, quint64(std::max(0, d.power)));
    appendUInt(body, 2, quint64(std::max(0, d.cadence)));
    appendUInt(body, 3, nonNegative(d.speedKmh * 100.0));
    appendUInt(body, 4, quint64(std::max(0, d.heartRate)));
    return withPrefix(InRidingData, body);
}

QByteArray encodeSettingsEcho(const Settings &s)
{
    QByteArray c;
    if (s.windMps)
        appendSInt(c, 5, std::llround(*s.windMps * 100.0));
    if (s.inclinePercent)
        appendSInt(c, 6, std::llround(*s.inclinePercent * 100.0));
    if (s.gearRatio)
        appendUInt(c, 8, nonNegative(*s.gearRatio * 10000.0));
    if (s.bikeKg)
        appendUInt(c, 11, nonNegative(*s.bikeKg * 100.0));
    if (s.riderKg)
        appendUInt(c, 12, nonNegative(*s.riderKg * 100.0));
    if (s.crr)
        appendUInt(c, 13, nonNegative(*s.crr * 100000.0));
    QByteArray settings;
    appendMessage(settings, 1, c);
    QByteArray env;
    appendUInt(env, 1, quint64(s.messageType > 0 ? s.messageType : int(kSettingsDataId)));
    appendMessage(env, 2, settings);
    return withPrefix(InDeviceData, env);
}

bool isRideOn(const QByteArray &value)
{
    return value.startsWith("RideOn");
}

QString describe(const QByteArray &message, bool outbound)
{
    if (message.isEmpty())
        return QStringLiteral("(empty)");
    if (isRideOn(message))
        return outbound ? QStringLiteral("handshake RideOn %1").arg(QString::fromLatin1(message.mid(6).toHex(' ')))
                        : QStringLiteral("handshake answer RideOn");
    const quint8 prefix = quint8(message.at(0));
    const QByteArray body = message.mid(1);
    if (outbound) {
        switch (prefix) {
        case OutStartStream: return QStringLiteral("start hub data stream");
        case OutRequest: {
            const auto f = zwift::decodeFields(body);
            return QStringLiteral("request dataId %1").arg(f ? zwift::varintField(*f, 1).value_or(0) : 0);
        }
        case OutCommand: {
            const auto f = zwift::decodeFields(body);
            if (!f)
                return QStringLiteral("command (malformed)");
            QStringList parts;
            for (const zwift::PbField &field : *f) {
                if (field.number == 3 && field.wireType == 0) {
                    parts << QStringLiteral("PowerTarget %1 W").arg(field.value);
                } else if (field.number == 4 && field.wireType == 2) {
                    const auto s = zwift::decodeFields(field.bytes);
                    if (!s)
                        continue;
                    QStringList ss;
                    if (auto v = zwift::varintField(*s, 2))
                        ss << QStringLiteral("incline %1 %").arg(num(unzigzag(*v) / 100.0, 2));
                    if (auto v = zwift::varintField(*s, 1))
                        ss << QStringLiteral("wind %1 m/s").arg(num(unzigzag(*v) / 100.0, 2));
                    if (auto v = zwift::varintField(*s, 3))
                        ss << QStringLiteral("CWa %1 kg/m").arg(num(*v / 10000.0, 4));
                    if (auto v = zwift::varintField(*s, 4))
                        ss << QStringLiteral("Crr %1").arg(num(*v / 100000.0, 5));
                    parts << QStringLiteral("Simulation: ") + (ss.isEmpty() ? QStringLiteral("(no fields)") : ss.join(QStringLiteral(", ")));
                } else if (field.number == 5 && field.wireType == 2) {
                    const auto p = zwift::decodeFields(field.bytes);
                    if (!p)
                        continue;
                    QStringList ps;
                    if (auto v = zwift::varintField(*p, 2))
                        ps << QStringLiteral("gear ratio %1").arg(num(*v / 10000.0, 4));
                    if (auto v = zwift::varintField(*p, 4))
                        ps << QStringLiteral("bike %1 kg").arg(num(*v / 100.0, 2));
                    if (auto v = zwift::varintField(*p, 5))
                        ps << QStringLiteral("rider %1 kg").arg(num(*v / 100.0, 2));
                    parts << QStringLiteral("Physical: ") + ps.join(QStringLiteral(", "));
                }
            }
            return QStringLiteral("command ") + parts.join(QStringLiteral("; "));
        }
        default: return QStringLiteral("message 0x%1").arg(prefix, 2, 16, QLatin1Char('0'));
        }
    }
    switch (prefix) {
    case InRidingData: {
        const auto d = decodeRidingData(body);
        if (!d)
            return QStringLiteral("riding data (truncated)");
        return QStringLiteral("riding data: %1 W, %2 rpm, %3 km/h, HR %4").arg(d->power).arg(d->cadence)
            .arg(num(d->speedKmh, 2)).arg(d->heartRate);
    }
    case InDeviceData: {
        const auto s = decodeDeviceData(body);
        if (!s)
            return QStringLiteral("device data (not a settings echo)");
        QStringList p;
        if (s->inclinePercent) p << QStringLiteral("incline %1 %").arg(num(*s->inclinePercent, 2));
        if (s->gearRatio) p << QStringLiteral("gear ratio %1").arg(num(*s->gearRatio, 4));
        if (s->riderKg) p << QStringLiteral("rider %1 kg").arg(num(*s->riderKg, 2));
        if (s->bikeKg) p << QStringLiteral("bike %1 kg").arg(num(*s->bikeKg, 2));
        if (s->crr) p << QStringLiteral("Crr %1").arg(num(*s->crr, 5));
        if (s->windMps) p << QStringLiteral("wind %1 m/s").arg(num(*s->windMps, 2));
        return QStringLiteral("settings echo (type %1): %2").arg(s->messageType).arg(p.join(QStringLiteral(", ")));
    }
    case InTrainerResponse: {
        const auto t = decodeTrainerResponse(body);
        return QStringLiteral("trainer response%1").arg(t && !t->isEmpty() ? QStringLiteral(": ") + *t : QString());
    }
    case InKeepAlive: return QStringLiteral("keep-alive");
    case InPairing: return QStringLiteral("pairing / empty");
    default: return QStringLiteral("message 0x%1").arg(prefix, 2, 16, QLatin1Char('0'));
    }
}

} // namespace watt::zhub
