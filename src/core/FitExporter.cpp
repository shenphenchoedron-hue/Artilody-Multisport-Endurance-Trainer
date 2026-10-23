#include "FitExporter.h"

#include <QSaveFile>
#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <vector>

namespace watt::fit {

namespace {

// FIT base types (base type byte as written into definition messages).
enum BaseType : quint8 {
    Enum = 0x00,
    UInt8 = 0x02,
    UInt16 = 0x84,
    UInt32 = 0x86,
    UInt32z = 0x8C,
};

int baseTypeSize(quint8 t)
{
    switch (t) {
    case UInt16: return 2;
    case UInt32:
    case UInt32z: return 4;
    default: return 1;
    }
}

// Global message numbers.
enum Mesg : quint16 {
    MesgFileId = 0,
    MesgSession = 18,
    MesgLap = 19,
    MesgRecord = 20,
    MesgEvent = 21,
    MesgDeviceInfo = 23,
    MesgActivity = 34,
};

// Common field numbers.
constexpr quint8 kFieldTimestamp = 253;
constexpr quint8 kFieldMessageIndex = 254;

// Enum values.
constexpr quint64 kFileTypeActivity = 4;
constexpr quint64 kManufacturerDevelopment = 255;
constexpr quint64 kProduct = 1;
constexpr quint64 kSoftwareVersion = 100;   // 1.00 (scale 100)
constexpr quint64 kSportCycling = 2;
constexpr quint64 kSubSportIndoorCycling = 6;
constexpr quint64 kEventTimer = 0;
constexpr quint64 kEventSession = 8;
constexpr quint64 kEventLap = 9;
constexpr quint64 kEventActivity = 26;
constexpr quint64 kEventTypeStart = 0;
constexpr quint64 kEventTypeStop = 1;
constexpr quint64 kEventTypeStopAll = 4;
constexpr quint64 kActivityManual = 0;

constexpr quint16 kProfileVersion = 2140;   // 21.40
constexpr quint8 kProtocolVersion = 0x20;   // 2.0 (no developer fields used)

quint64 invalidFor(quint8 t)
{
    switch (t) {
    case UInt16: return 0xFFFF;
    case UInt32: return 0xFFFFFFFF;
    case UInt32z: return 0;
    default: return 0xFF;
    }
}

struct Field
{
    quint8 num;
    quint8 type;
    quint64 value;
};

// Clamps v into the valid range of an unsigned type with `bits` bits, keeping
// the all-ones value reserved as "invalid".
quint64 clampValid(double v, int bits)
{
    const double maxValid = double((quint64(1) << bits) - 2);
    if (!(v > 0))   // also catches NaN
        return 0;
    return quint64(std::llround(std::min(v, maxValid)));
}

quint64 u8(double v) { return clampValid(v, 8); }
quint64 u16(double v) { return clampValid(v, 16); }
quint64 u32(double v) { return clampValid(v, 32); }
// Optional uint8 value where 0 means "no data" (heart rate).
quint64 u8OrInvalid(double v) { return v >= 0.5 ? u8(v) : 0xFF; }
quint64 u16OrInvalid(double v) { return v >= 0.5 ? u16(v) : 0xFFFF; }

class Encoder
{
public:
    // Writes a data message; emits a definition message first whenever the
    // field layout of the local message type changes.
    void write(quint8 local, quint16 global, std::initializer_list<Field> fields)
    {
        Layout layout { global, {} };
        for (const Field &f : fields)
            layout.fields.push_back({ f.num, f.type });
        if (local >= m_layouts.size())
            m_layouts.resize(local + 1);
        if (!m_layouts[local].valid || !(m_layouts[local].layout == layout)) {
            writeDefinition(local, layout);
            m_layouts[local] = { true, layout };
        }
        m_data.append(char(local & 0x0F));
        for (const Field &f : fields)
            put(f.value, baseTypeSize(f.type));
    }

    QByteArray finish() const
    {
        QByteArray out;
        out.reserve(14 + m_data.size() + 2);
        out.append(char(14));
        out.append(char(kProtocolVersion));
        appendLE(out, kProfileVersion, 2);
        appendLE(out, quint64(m_data.size()), 4);
        out.append(".FIT", 4);
        appendLE(out, crc16(out), 2);
        out.append(m_data);
        appendLE(out, crc16(out), 2);
        return out;
    }

private:
    struct FieldDef
    {
        quint8 num, type;
        bool operator==(const FieldDef &o) const { return num == o.num && type == o.type; }
    };
    struct Layout
    {
        quint16 global = 0;
        std::vector<FieldDef> fields;
        bool operator==(const Layout &o) const { return global == o.global && fields == o.fields; }
    };
    struct Slot
    {
        bool valid = false;
        Layout layout;
    };

    static void appendLE(QByteArray &b, quint64 v, int bytes)
    {
        for (int i = 0; i < bytes; ++i)
            b.append(char((v >> (8 * i)) & 0xFF));
    }
    void put(quint64 v, int bytes) { appendLE(m_data, v, bytes); }

    void writeDefinition(quint8 local, const Layout &l)
    {
        m_data.append(char(0x40 | (local & 0x0F)));
        m_data.append(char(0));   // reserved
        m_data.append(char(0));   // architecture: little endian
        put(l.global, 2);
        m_data.append(char(l.fields.size()));
        for (const FieldDef &f : l.fields) {
            m_data.append(char(f.num));
            m_data.append(char(baseTypeSize(f.type)));
            m_data.append(char(f.type));
        }
    }

    QByteArray m_data;
    std::vector<Slot> m_layouts;
};

// Local message types (one per kind of message we write).
enum Local : quint8 { LFileId, LDeviceInfo, LEvent, LRecord, LLap, LSession, LActivity };

quint64 fitTime(qint64 unixSecs)
{
    return quint64(std::max<qint64>(0, unixSecs - kFitEpochOffset));
}

} // namespace

quint16 crc16(const QByteArray &data, quint16 crc)
{
    static const quint16 table[16] = { 0x0000, 0xCC01, 0xD801, 0x1400, 0xF001, 0x3C00, 0x2800, 0xE401,
                                       0xA001, 0x6C00, 0x7800, 0xB401, 0x5000, 0x9C01, 0x8801, 0x4400 };
    for (const char c : data) {
        const quint8 byte = quint8(c);
        quint16 tmp = table[crc & 0xF];
        crc = (crc >> 4) & 0x0FFF;
        crc = crc ^ tmp ^ table[byte & 0xF];
        tmp = table[crc & 0xF];
        crc = (crc >> 4) & 0x0FFF;
        crc = crc ^ tmp ^ table[(byte >> 4) & 0xF];
    }
    return crc;
}

QByteArray encodeActivity(const RideRecord &ride, const QVector<Sample> &samples)
{
    const QDateTime started = ride.startedAt.isValid() ? ride.startedAt : QDateTime::currentDateTimeUtc();
    const qint64 startUnix = started.toSecsSinceEpoch();
    const int lastT = samples.isEmpty() ? -1 : samples.constLast().t;
    const qint64 elapsedSec = std::max<qint64>({ qint64(ride.durationSec), qint64(lastT) + 1, 0 });
    const quint64 start = fitTime(startUnix);
    const quint64 end = fitTime(startUnix + elapsedSec);
    const quint64 elapsedMs = u32(double(elapsedSec) * 1000.0);
    const quint64 serial = ride.id > 0 ? quint64(ride.id) & 0xFFFFFFFF : 1;
    const quint64 workJ = u32(ride.kilojoules * 1000.0);
    const quint64 calories = u16(ride.kilojoules);   // ~1 kcal per kJ of mechanical work on a bike

    Encoder e;
    e.write(LFileId, MesgFileId,
            { { 0, Enum, kFileTypeActivity },
              { 1, UInt16, kManufacturerDevelopment },
              { 2, UInt16, kProduct },
              { 3, UInt32z, serial ? serial : 1 },
              { 4, UInt32, start } });
    e.write(LDeviceInfo, MesgDeviceInfo,
            { { kFieldTimestamp, UInt32, start },
              { 0, UInt8, 0 },   // device_index: creator
              { 2, UInt16, kManufacturerDevelopment },
              { 3, UInt32z, serial ? serial : 1 },
              { 4, UInt16, kProduct },
              { 5, UInt16, kSoftwareVersion } });
    e.write(LEvent, MesgEvent,
            { { kFieldTimestamp, UInt32, start },
              { 0, Enum, kEventTimer },
              { 1, Enum, kEventTypeStart },
              { 4, UInt8, 0 } });

    for (const Sample &s : samples) {
        const qint64 t = std::max(0, s.t);
        e.write(LRecord, MesgRecord,
                { { kFieldTimestamp, UInt32, fitTime(startUnix + t) },
                  { 7, UInt16, u16(s.power) },
                  { 3, UInt8, u8OrInvalid(s.heartRate) },
                  { 4, UInt8, s.cadence >= 0 ? u8(s.cadence) : invalidFor(UInt8) } });
    }

    e.write(LEvent, MesgEvent,
            { { kFieldTimestamp, UInt32, end },
              { 0, Enum, kEventTimer },
              { 1, Enum, kEventTypeStopAll },
              { 4, UInt8, 0 } });

    const quint64 avgPower = u16(ride.avgPower);
    const quint64 maxPower = u16(ride.maxPower);
    const quint64 avgHr = u8OrInvalid(ride.avgHeartRate);
    const quint64 maxHr = u8OrInvalid(ride.maxHeartRate);
    const quint64 avgCad = u8(ride.avgCadence);
    const quint64 np = u16OrInvalid(ride.normalizedPower);

    e.write(LLap, MesgLap,
            { { kFieldMessageIndex, UInt16, 0 },
              { kFieldTimestamp, UInt32, end },
              { 0, Enum, kEventLap },
              { 1, Enum, kEventTypeStop },
              { 2, UInt32, start },
              { 7, UInt32, elapsedMs },
              { 8, UInt32, elapsedMs },
              { 11, UInt16, calories },
              { 15, UInt8, avgHr },
              { 16, UInt8, maxHr },
              { 17, UInt8, avgCad },
              { 19, UInt16, avgPower },
              { 20, UInt16, maxPower },
              { 25, Enum, kSportCycling },
              { 33, UInt16, np },
              { 39, Enum, kSubSportIndoorCycling },
              { 41, UInt32, workJ } });

    e.write(LSession, MesgSession,
            { { kFieldMessageIndex, UInt16, 0 },
              { kFieldTimestamp, UInt32, end },
              { 0, Enum, kEventSession },
              { 1, Enum, kEventTypeStop },
              { 2, UInt32, start },
              { 5, Enum, kSportCycling },
              { 6, Enum, kSubSportIndoorCycling },
              { 7, UInt32, elapsedMs },
              { 8, UInt32, elapsedMs },
              { 11, UInt16, calories },
              { 16, UInt8, avgHr },
              { 17, UInt8, maxHr },
              { 18, UInt8, avgCad },
              { 20, UInt16, avgPower },
              { 21, UInt16, maxPower },
              { 25, UInt16, 0 },   // first_lap_index
              { 26, UInt16, 1 },   // num_laps
              { 34, UInt16, np },
              { 35, UInt16, u16OrInvalid(ride.tss * 10.0) },
              { 36, UInt16, u16OrInvalid(ride.intensityFactor * 1000.0) },
              { 45, UInt16, ride.ftp > 0 ? u16(ride.ftp) : invalidFor(UInt16) },
              { 48, UInt32, workJ } });

    const qint64 localOffset = started.offsetFromUtc();
    e.write(LActivity, MesgActivity,
            { { kFieldTimestamp, UInt32, end },
              { 0, UInt32, elapsedMs },
              { 1, UInt16, 1 },   // num_sessions
              { 2, Enum, kActivityManual },
              { 3, Enum, kEventActivity },
              { 4, Enum, kEventTypeStop },
              { 5, UInt32, fitTime(startUnix + elapsedSec + localOffset) } });

    return e.finish();
}

bool writeFile(const QString &path, const RideRecord &ride, const QVector<Sample> &samples, QString *error)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    const QByteArray data = encodeActivity(ride, samples);
    if (f.write(data) != data.size() || !f.commit()) {
        if (error)
            *error = f.errorString();
        return false;
    }
    return true;
}

} // namespace watt::fit
