#include "ZwiftClickProtocol.h"

#include <algorithm>

namespace watt::zwift {

QUuid serviceUuid() { return QUuid(QStringLiteral("{00000001-19ca-4651-86e5-fa29dcdd09d1}")); }
QUuid asyncUuid() { return QUuid(QStringLiteral("{00000002-19ca-4651-86e5-fa29dcdd09d1}")); }
QUuid syncRxUuid() { return QUuid(QStringLiteral("{00000003-19ca-4651-86e5-fa29dcdd09d1}")); }
QUuid syncTxUuid() { return QUuid(QStringLiteral("{00000004-19ca-4651-86e5-fa29dcdd09d1}")); }
QUuid unknown6Uuid() { return QUuid(QStringLiteral("{00000006-19ca-4651-86e5-fa29dcdd09d1}")); }

QString deviceTypeText(int type)
{
    switch (type) {
    case TypeHub: return QStringLiteral("Zwift Hub");
    case TypePlayRight: return QStringLiteral("Zwift Play (right)");
    case TypePlayLeft: return QStringLiteral("Zwift Play (left)");
    case TypeRideRight: return QStringLiteral("Zwift Ride (right)");
    case TypeRideLeft: return QStringLiteral("Zwift Ride (left)");
    case TypeClick: return QStringLiteral("Zwift Click");
    case TypeClickV2A: return QStringLiteral("Zwift Click v2");
    case TypeClickV2B: return QStringLiteral("Zwift Click v2");
    case TypeController0E: return QStringLiteral("Zwift controller (0x0e)");
    default: break;
    }
    return QStringLiteral("Zwift device 0x%1").arg(type, 2, 16, QLatin1Char('0'));
}

bool isControllerType(int type)
{
    // Whitelist of the known Zwift controller types. Other values (the Hub
    // trainer, future trainers or accessories) are never taken as a
    // controller from the type byte alone; the caller falls back to the
    // name / the 0xFC82 service.
    switch (type) {
    case TypePlayRight:
    case TypePlayLeft:
    case TypeRideRight:
    case TypeRideLeft:
    case TypeClick:
    case TypeClickV2A:
    case TypeClickV2B:
    case TypeController0E:
        return true;
    default:
        return false;
    }
}

bool isV2ControllerType(int type)
{
    switch (type) {
    case TypeRideRight:
    case TypeRideLeft:
    case TypeClickV2A:
    case TypeClickV2B:
    case TypeController0E:
        return true;
    default:
        return false;
    }
}

bool nameLooksLikeController(const QString &name)
{
    const QString n = name.trimmed();
    return n.startsWith(QLatin1String("Zwift Click"), Qt::CaseInsensitive)
           || n.startsWith(QLatin1String("Zwift Play"), Qt::CaseInsensitive)
           || n.startsWith(QLatin1String("Zwift Ride"), Qt::CaseInsensitive)
           || n.startsWith(QLatin1String("Zwift SF2"), Qt::CaseInsensitive);
}

QByteArray rideOn()
{
    return QByteArrayLiteral("RideOn");
}

QByteArray unlockAck()
{
    return QByteArray::fromHex("ff0400");
}

SyncResponse parseSyncResponse(const QByteArray &data)
{
    if (data.isEmpty())
        return SyncResponse::None;
    if (data.startsWith(rideOn())) {
        // "RideOn" + 01 03 (Click) / 01 04 (Play) + 64-byte public key
        if (data.size() > 8 && quint8(data.at(6)) == 0x01)
            return SyncResponse::PublicKey;
        return SyncResponse::RideOn;
    }
    if (quint8(data.at(0)) == MsgVendor)
        return SyncResponse::Challenge;
    return SyncResponse::Other;
}

QString syncResponseText(SyncResponse r)
{
    switch (r) {
    case SyncResponse::None: return QStringLiteral("none");
    case SyncResponse::RideOn: return QStringLiteral("RideOn (unencrypted)");
    case SyncResponse::PublicKey: return QStringLiteral("public key (encryption requested)");
    case SyncResponse::Challenge: return QStringLiteral("challenge (locked)");
    case SyncResponse::Other: return QStringLiteral("other");
    }
    return QString();
}

bool readVarint(const QByteArray &data, int &pos, quint64 &out)
{
    out = 0;
    for (int shift = 0; shift < 64; shift += 7) {
        if (pos >= data.size())
            return false;
        const quint8 b = quint8(data.at(pos++));
        out |= quint64(b & 0x7F) << shift;
        if (!(b & 0x80))
            return true;
    }
    return false;
}

std::optional<QVector<PbField>> decodeFields(const QByteArray &data)
{
    QVector<PbField> out;
    int pos = 0;
    while (pos < data.size()) {
        quint64 key = 0;
        if (!readVarint(data, pos, key))
            return std::nullopt;
        PbField f;
        f.number = int(key >> 3);
        f.wireType = int(key & 0x07);
        if (f.number <= 0)
            return std::nullopt;
        switch (f.wireType) {
        case 0:
            if (!readVarint(data, pos, f.value))
                return std::nullopt;
            break;
        case 1:
            if (pos + 8 > data.size())
                return std::nullopt;
            for (int i = 0; i < 8; ++i)
                f.value |= quint64(quint8(data.at(pos + i))) << (8 * i);
            pos += 8;
            break;
        case 2: {
            quint64 len = 0;
            if (!readVarint(data, pos, len) || len > quint64(data.size() - pos))
                return std::nullopt;
            f.bytes = data.mid(pos, int(len));
            pos += int(len);
            break;
        }
        case 5:
            if (pos + 4 > data.size())
                return std::nullopt;
            for (int i = 0; i < 4; ++i)
                f.value |= quint64(quint8(data.at(pos + i))) << (8 * i);
            pos += 4;
            break;
        default:
            return std::nullopt;   // groups (3/4) are not used
        }
        out.append(f);
    }
    return out;
}

std::optional<quint64> varintField(const QVector<PbField> &fields, int number)
{
    for (const PbField &f : fields) {
        if (f.number == number && f.wireType == 0)
            return f.value;
    }
    return std::nullopt;
}

ShiftButtons shiftButtonsFromRideMask(quint32 mask)
{
    const quint32 pressed = ~mask;
    ShiftButtons b;
    b.plus = pressed & (RideShiftUpRight | RideShiftDownRight);
    b.minus = pressed & (RideShiftUpLeft | RideShiftDownLeft | RideB);
    return b;
}

Message parseAsync(const QByteArray &data)
{
    Message m;
    if (data.isEmpty())
        return m;
    m.type = quint8(data.at(0));
    const auto fields = decodeFields(data.mid(1));
    switch (m.type) {
    case MsgClickKeys: {
        if (!fields)
            return m;
        // PlayButtonStatus: ON = 0 (pressed), OFF = 1; a missing field is "released"
        const auto plus = varintField(*fields, 1);
        const auto minus = varintField(*fields, 2);
        if (!plus && !minus)
            return m;
        m.kind = Message::Kind::Buttons;
        m.buttons.plus = plus && *plus == 0;
        m.buttons.minus = minus && *minus == 0;
        return m;
    }
    case MsgRideKeys: {
        if (!fields)
            return m;
        const auto map = varintField(*fields, 1);
        if (!map)
            return m;
        m.kind = Message::Kind::Buttons;
        m.rideMask = quint32(*map);
        m.buttons = shiftButtonsFromRideMask(m.rideMask);
        return m;
    }
    case MsgBattery: {
        if (fields) {
            // BatteryStatus: field 1 is the type, field 2 the level. Without
            // a level field the message is only a periodic "idle" frame.
            const auto level = varintField(*fields, 2);
            if (level && *level <= 100) {
                m.kind = Message::Kind::Battery;
                m.battery = int(*level);
                return m;
            }
        }
        m.kind = Message::Kind::Idle;
        return m;
    }
    case MsgEmpty:
        m.kind = Message::Kind::Idle;
        return m;
    default:
        m.kind = Message::Kind::Other;
        return m;
    }
}

// --- ButtonTracker -------------------------------------------------------------

QVector<int> ButtonTracker::update(const ShiftButtons &state, qint64 nowMs)
{
    QVector<int> events;
    const bool wasPlus = m_state.plus && !m_state.minus;
    const bool wasMinus = m_state.minus && !m_state.plus;
    const bool isPlus = state.plus && !state.minus;
    const bool isMinus = state.minus && !state.plus;
    if (isPlus && !wasPlus && !m_state.plus)
        events.append(1);
    if (isMinus && !wasMinus && !m_state.minus)
        events.append(-1);
    m_state = state;
    if (isPlus || isMinus) {
        if (!events.isEmpty() || m_nextRepeat < 0)
            m_nextRepeat = nowMs + kRepeatDelayMs;
    } else {
        m_nextRepeat = -1;
    }
    return events;
}

QVector<int> ButtonTracker::poll(qint64 nowMs)
{
    QVector<int> events;
    if (m_nextRepeat < 0 || !held())
        return events;
    const int dir = m_state.plus ? 1 : -1;
    if (nowMs >= m_nextRepeat) {
        // one step per poll: a stalled timer must not fire a burst of shifts
        events.append(dir);
        m_nextRepeat = std::max(m_nextRepeat + kRepeatIntervalMs, nowMs + kRepeatIntervalMs / 2);
    }
    return events;
}

void ButtonTracker::reset()
{
    m_state = ShiftButtons();
    m_nextRepeat = -1;
}

} // namespace watt::zwift
