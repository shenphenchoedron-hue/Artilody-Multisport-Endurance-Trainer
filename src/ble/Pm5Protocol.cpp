#include "Pm5Protocol.h"

#include <cmath>

namespace watt::pm5 {

namespace {
// Base UUID CE06XXXX-43E5-11E4-916C-0800200C9A66
constexpr uint kBaseData1 = 0xCE060000u;
constexpr ushort kData2 = 0x43E5;
constexpr ushort kData3 = 0x11E4;
constexpr uchar kData4[8] = { 0x91, 0x6C, 0x08, 0x00, 0x20, 0x0C, 0x9A, 0x66 };

quint32 u8(const QByteArray &d, int i) { return quint8(d.at(i)); }
quint32 u16(const QByteArray &d, int i) { return u8(d, i) | (u8(d, i + 1) << 8); }
quint32 u24(const QByteArray &d, int i) { return u16(d, i) | (u8(d, i + 2) << 16); }
} // namespace

QUuid uuid(quint16 id)
{
    return QUuid(kBaseData1 | id, kData2, kData3, kData4[0], kData4[1], kData4[2], kData4[3], kData4[4], kData4[5],
                 kData4[6], kData4[7]);
}

std::optional<quint16> shortId(const QUuid &u)
{
    if ((u.data1 & 0xFFFF0000u) != kBaseData1 || u.data2 != kData2 || u.data3 != kData3)
        return std::nullopt;
    for (int i = 0; i < 8; ++i) {
        if (u.data4[i] != kData4[i])
            return std::nullopt;
    }
    return quint16(u.data1 & 0xFFFFu);
}

QByteArray sampleRateCommand(SampleRate rate)
{
    return QByteArray(1, char(quint8(rate)));
}

SampleRate sampleRateForInterval(int intervalMs)
{
    if (intervalMs <= 100)
        return SampleRate::Ms100;
    if (intervalMs <= 250)
        return SampleRate::Ms250;
    if (intervalMs <= 500)
        return SampleRate::Ms500;
    return SampleRate::Ms1000;
}

MachineKind machineKind(int t)
{
    if (t == 225 || (t >= 128 && t < 192))
        return MachineKind::SkiErg;
    if (t == 226 || (t >= 192 && t < 224))
        return MachineKind::BikeErg;
    return MachineKind::Rower;
}

MachineKind machineKindFromName(const QString &name)
{
    const QString n = name.trimmed();
    if (n.endsWith(QLatin1String(" Ski"), Qt::CaseInsensitive) || n.endsWith(QLatin1String("SkiErg"), Qt::CaseInsensitive))
        return MachineKind::SkiErg;
    if (n.endsWith(QLatin1String(" Bike"), Qt::CaseInsensitive) || n.endsWith(QLatin1String("BikeErg"), Qt::CaseInsensitive))
        return MachineKind::BikeErg;
    return MachineKind::Rower;
}

bool isWorkingState(int s)
{
    switch (WorkoutState(s)) {
    case WorkoutState::WorkoutRow:
    case WorkoutState::IntervalWorkTime:
    case WorkoutState::IntervalWorkDistance:
    case WorkoutState::IntervalWorkTimeToRest:
    case WorkoutState::IntervalWorkDistanceToRest:
        return true;
    default:
        return false;
    }
}

int expectedLength(quint16 id, Layout layout)
{
    const bool mux = layout == Layout::Multiplexed;
    switch (id) {
    case kGeneralStatus: return 19;
    case kAdditionalStatus1: return mux ? 19 : 17;
    case kAdditionalStatus2: return mux ? 18 : 20;
    case kStrokeData: return mux ? 18 : 20;
    case kAdditionalStrokeData: return mux ? 17 : 15;
    default: return 0;
    }
}

std::optional<GeneralStatus> parseGeneralStatus(const QByteArray &d)
{
    if (d.size() < 19)
        return std::nullopt;
    GeneralStatus s;
    s.elapsedSeconds = u24(d, 0) * 0.01;
    s.distanceMeters = u24(d, 3) * 0.1;
    s.workoutType = int(u8(d, 6));
    s.intervalType = int(u8(d, 7));
    s.workoutState = int(u8(d, 8));
    s.rowingState = int(u8(d, 9));
    s.strokeState = int(u8(d, 10));
    s.totalWorkDistance = int(u24(d, 11));
    s.workoutDuration = int(u24(d, 14));
    s.durationType = int(u8(d, 17));
    s.dragFactor = int(u8(d, 18));
    return s;
}

std::optional<AdditionalStatus1> parseAdditionalStatus1(const QByteArray &d, Layout layout)
{
    // The 17 common bytes (up to the rest time) are required in both
    // layouts. A multiplexed packet is documented with 19 data bytes, but a
    // shorter one must not lose the stroke rate/heart rate/pace: average power
    // and machine type are read only when present.
    if (d.size() < expectedLength(kAdditionalStatus1, Layout::Direct))
        return std::nullopt;
    AdditionalStatus1 s;
    s.elapsedSeconds = u24(d, 0) * 0.01;
    s.speedMps = u16(d, 3) * 0.001;
    s.strokesPerMinute = int(u8(d, 5));
    const int hr = int(u8(d, 6));
    s.heartRateValid = hr != 255 && hr != 0;
    s.heartRateBpm = s.heartRateValid ? hr : 0;
    s.currentPace500Seconds = u16(d, 7) * 0.01;
    s.averagePace500Seconds = u16(d, 9) * 0.01;
    // Rest distance: UInt16 in metres per the interface definition (rev. 1.30,
    // "Rest Distance Lo/Hi, 1 m LSB"). Some third-party code scales it by 0.1;
    // the PDF value is kept.
    s.restDistanceMeters = int(u16(d, 11));
    s.restTimeSeconds = u24(d, 13) * 0.01;
    // Average power is part of the multiplexed packet. Current firmware also
    // sends the direct 0x0032 with 19 bytes in that layout (average power at
    // 16-17, machine type at 18), so the length decides. A direct packet of
    // exactly 17 bytes has the machine type at 16; one of 18 bytes is
    // ambiguous (byte 16 may be the average power low byte), so it carries
    // no machine type (the default, rower, stays).
    if (d.size() >= 19) {
        s.averagePowerW = int(u16(d, 16));
        s.ergMachineType = int(u8(d, 18));
    } else if (layout == Layout::Multiplexed && d.size() >= 18) {
        s.averagePowerW = int(u16(d, 16));   // machine type cut off
    } else if (layout == Layout::Direct && d.size() == 17) {
        s.ergMachineType = int(u8(d, 16));
    }
    return s;
}

std::optional<AdditionalStatus2> parseAdditionalStatus2(const QByteArray &d, Layout layout)
{
    // Direct: 20 bytes with average power at 4-5. A direct packet of 18/19
    // bytes is taken as the shorter layout without average power (as
    // multiplexed); calories etc. then start at byte 4.
    if (d.size() < expectedLength(kAdditionalStatus2, Layout::Multiplexed))
        return std::nullopt;
    AdditionalStatus2 s;
    s.elapsedSeconds = u24(d, 0) * 0.01;
    s.intervalCount = int(u8(d, 3));
    int p = 4;
    if (layout == Layout::Direct && d.size() >= expectedLength(kAdditionalStatus2, Layout::Direct)) {
        s.averagePowerW = int(u16(d, p));
        p += 2;
    }
    s.totalCalories = int(u16(d, p));
    s.splitAveragePace500Seconds = u16(d, p + 2) * 0.01;
    s.splitAveragePowerW = int(u16(d, p + 4));
    s.splitAverageCalories = int(u16(d, p + 6));
    s.lastSplitTimeSeconds = u24(d, p + 8) * 0.1;
    s.lastSplitDistanceMeters = int(u24(d, p + 11));
    return s;
}

std::optional<AdditionalStrokeData> parseAdditionalStrokeData(const QByteArray &d, Layout layout)
{
    Q_UNUSED(layout)
    // The 15 common bytes are required in both layouts; work per stroke
    // (15-16, 0.1 J) is read whenever present (multiplexed, newer firmware
    // also in the direct characteristic).
    if (d.size() < 15)
        return std::nullopt;
    AdditionalStrokeData s;
    s.elapsedSeconds = u24(d, 0) * 0.01;
    s.strokePowerW = int(u16(d, 3));
    s.strokeCaloriesPerHour = int(u16(d, 5));
    s.strokeCount = int(u16(d, 7));
    s.projectedWorkTimeSeconds = int(u24(d, 9));
    s.projectedWorkDistanceMeters = int(u24(d, 12));
    if (d.size() >= 17)
        s.workPerStrokeJoules = u16(d, 15) * 0.1;
    return s;
}

std::optional<MultiplexedPacket> parseMultiplexed(const QByteArray &data)
{
    if (data.isEmpty())
        return std::nullopt;
    MultiplexedPacket p;
    p.id = quint8(data.at(0));
    p.payload = data.mid(1);
    switch (p.id) {
    case kGeneralStatus:
        p.generalStatus = parseGeneralStatus(p.payload);
        break;
    case kAdditionalStatus1:
        p.additionalStatus1 = parseAdditionalStatus1(p.payload, Layout::Multiplexed);
        break;
    case kAdditionalStatus2:
        p.additionalStatus2 = parseAdditionalStatus2(p.payload, Layout::Multiplexed);
        break;
    case kAdditionalStrokeData:
        p.additionalStrokeData = parseAdditionalStrokeData(p.payload, Layout::Multiplexed);
        break;
    default:
        return p;   // known-but-unhandled or unknown id: report it, decode nothing
    }
    if (!p.decoded())
        return std::nullopt;   // supported id with a truncated payload
    return p;
}

double wattsFromPace500(double pace500Seconds)
{
    if (pace500Seconds <= 0)
        return 0;
    const double perMeter = pace500Seconds / 500.0;
    return 2.80 / (perMeter * perMeter * perMeter);
}

double pace500FromWatts(double watts)
{
    if (watts <= 0)
        return 0;
    return 500.0 * std::cbrt(2.80 / watts);
}

} // namespace watt::pm5
