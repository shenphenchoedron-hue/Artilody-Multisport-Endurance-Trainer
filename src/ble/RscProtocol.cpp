#include "RscProtocol.h"

#include <QStringList>

namespace watt::rsc {

namespace {
quint32 le(const QByteArray &d, int pos, int bytes)
{
    quint32 v = 0;
    for (int i = 0; i < bytes; ++i)
        v |= quint32(quint8(d.at(pos + i))) << (8 * i);
    return v;
}
} // namespace

std::optional<Measurement> parseMeasurement(const QByteArray &data)
{
    if (data.size() < 4)
        return std::nullopt;
    const quint8 flags = quint8(data.at(0));
    const bool hasStride = flags & 0x01;
    const bool hasDistance = flags & 0x02;
    const int needed = 4 + (hasStride ? 2 : 0) + (hasDistance ? 4 : 0);
    if (data.size() < needed)
        return std::nullopt;
    Measurement m;
    m.running = flags & 0x04;
    m.speedKmh = le(data, 1, 2) / 256.0 * 3.6;
    m.cadence = quint8(data.at(3));
    int pos = 4;
    if (hasStride) {
        m.strideLengthM = le(data, pos, 2) / 100.0;
        pos += 2;
    }
    if (hasDistance)
        m.totalDistanceM = le(data, pos, 4) / 10.0;
    return m;
}

QString describeMeasurement(const Measurement &m)
{
    QStringList parts;
    parts << QStringLiteral("speed %1 km/h").arg(m.speedKmh, 0, 'f', 2);
    parts << QStringLiteral("cadence %1 /min").arg(m.cadence);
    if (m.strideLengthM)
        parts << QStringLiteral("stride %1 m").arg(*m.strideLengthM, 0, 'f', 2);
    if (m.totalDistanceM)
        parts << QStringLiteral("distance %1 m").arg(*m.totalDistanceM, 0, 'f', 1);
    return parts.join(QStringLiteral(", ")) + (m.running ? QStringLiteral(" (running)") : QStringLiteral(" (walking)"));
}

} // namespace watt::rsc
