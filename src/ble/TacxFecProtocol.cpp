#include "TacxFecProtocol.h"

#include <algorithm>

namespace watt::tacxfec {

quint8 checksum(const QByteArray &bytes)
{
    quint8 x = 0;
    for (char c : bytes)
        x ^= quint8(c);
    return x;
}

QByteArray encodeRoadFeel(quint8 surface, int intensityPercent)
{
    QByteArray m;
    m.reserve(13);
    m.append(char(0xA4));   // sync
    m.append(char(0x09));   // length (channel + 8 data bytes)
    m.append(char(0x4F));   // broadcast data
    m.append(char(0x05));   // channel
    m.append(char(0xFC));   // page 252: NEO modes
    m.append(char(0x00));
    m.append(char(0x00));   // isokinetic mode off
    m.append(char(0x00));   // isokinetic speed (unused)
    m.append(char(0x00));
    m.append(char(surface));
    m.append(char(std::clamp(intensityPercent, 0, 100)));
    m.append(char(0x00));
    m.append(char(checksum(m)));
    return m;
}

QString describe(const QByteArray &m)
{
    if (m.size() == 13 && quint8(m.at(4)) == 0xFC)
        return QStringLiteral("NEO road feel: surface %1, intensity %2 % [%3]")
            .arg(quint8(m.at(9)))
            .arg(quint8(m.at(10)))
            .arg(QString::fromLatin1(m.toHex(' ')));
    return QString::fromLatin1(m.toHex(' '));
}

} // namespace watt::tacxfec
