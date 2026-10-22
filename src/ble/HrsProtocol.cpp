#include "HrsProtocol.h"

namespace watt::hrs {

std::optional<Measurement> parseMeasurement(const QByteArray &data)
{
    if (data.size() < 2)
        return std::nullopt;
    auto byte = [&](int i) { return quint8(data.at(i)); };

    const quint8 flags = byte(0);
    int pos = 1;
    Measurement m;
    if (flags & 0x01) {
        if (data.size() < pos + 2)
            return std::nullopt;
        m.bpm = byte(pos) | (byte(pos + 1) << 8);
        pos += 2;
    } else {
        m.bpm = byte(pos);
        pos += 1;
    }
    m.contactSupported = flags & 0x04;
    m.contactDetected = (flags & 0x06) == 0x06;
    if (flags & 0x08) {
        if (data.size() < pos + 2)
            return std::nullopt;
        m.energyExpendedKj = byte(pos) | (byte(pos + 1) << 8);
        pos += 2;
    }
    if (flags & 0x10) {
        while (pos + 1 < data.size()) {
            const int rr = byte(pos) | (byte(pos + 1) << 8);
            m.rrIntervalsMs.append(rr * 1000.0 / 1024.0);
            pos += 2;
        }
    }
    return m;
}

} // namespace watt::hrs
