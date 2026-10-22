#pragma once

#include <QByteArray>
#include <QVector>
#include <optional>

// Bluetooth SIG Heart Rate Service (HRS, 0x180D).
namespace watt::hrs {

constexpr quint16 kServiceUuid = 0x180D;
constexpr quint16 kMeasurementUuid = 0x2A37;     // notify
constexpr quint16 kBodySensorLocationUuid = 0x2A38;

struct Measurement
{
    int bpm = 0;
    bool contactSupported = false;
    bool contactDetected = false;
    std::optional<int> energyExpendedKj;
    QVector<double> rrIntervalsMs;
};

std::optional<Measurement> parseMeasurement(const QByteArray &data);

} // namespace watt::hrs
