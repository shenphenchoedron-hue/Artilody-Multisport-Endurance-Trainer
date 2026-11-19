#pragma once

#include <QByteArray>
#include <QString>
#include <optional>

// Bluetooth SIG Running Speed and Cadence service (RSC, 0x1814) - foot pods
// (Stryd, Zwift RunPod, ...) and treadmills that send their belt speed like a
// foot pod instead of FTMS, e.g. the KETTLER Track S2 ... S10 (they offer RSC
// and Heart Rate, no Fitness Machine service; Zwift reads their speed this way).
namespace watt::rsc {

constexpr quint16 kServiceUuid = 0x1814;
constexpr quint16 kMeasurementUuid = 0x2A53;   // notify
constexpr quint16 kFeatureUuid = 0x2A54;       // read

// RSC Measurement (RSCS v1.0 §3.2): Flags (UInt8: bit 0 stride length present,
// bit 1 total distance present, bit 2 running), Instantaneous Speed (UInt16,
// 1/256 m/s), Instantaneous Cadence (UInt8, 1/min), [Stride Length (UInt16,
// 1/100 m)], [Total Distance (UInt32, 1/10 m)].
struct Measurement
{
    double speedKmh = 0;
    int cadence = 0;                          // steps per minute
    bool running = false;                     // the device's walking / running status
    std::optional<double> strideLengthM;
    std::optional<double> totalDistanceM;
};

// One RSC Measurement notification; nullopt when it is too short for its flags.
std::optional<Measurement> parseMeasurement(const QByteArray &data);
// "speed 10.50 km/h, cadence 160 /min, distance 1234.5 m (running)"
QString describeMeasurement(const Measurement &m);

} // namespace watt::rsc
