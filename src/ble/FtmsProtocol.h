#pragma once

#include <QByteArray>
#include <QString>
#include <optional>

// Bluetooth SIG Fitness Machine Service (FTMS, 0x1826) – the protocol used by
// smart trainers (Tacx, Wahoo, Saris, Elite, Zwift Hub, ...).
namespace watt::ftms {

constexpr quint16 kServiceUuid = 0x1826;
constexpr quint16 kFeatureUuid = 0x2ACC;               // read
constexpr quint16 kIndoorBikeDataUuid = 0x2AD2;        // notify
constexpr quint16 kSupportedPowerRangeUuid = 0x2AD8;   // read
constexpr quint16 kControlPointUuid = 0x2AD9;          // write + indicate
constexpr quint16 kMachineStatusUuid = 0x2ADA;         // notify

enum class OpCode : quint8 {
    RequestControl = 0x00,
    Reset = 0x01,
    SetTargetResistance = 0x04,
    SetTargetPower = 0x05,
    StartOrResume = 0x07,
    StopOrPause = 0x08,
    SetIndoorBikeSimulation = 0x11,
    ResponseCode = 0x80,
};

enum class ResultCode : quint8 {
    Success = 0x01,
    NotSupported = 0x02,
    InvalidParameter = 0x03,
    OperationFailed = 0x04,
    ControlNotPermitted = 0x05,
};

struct IndoorBikeData
{
    std::optional<double> speedKmh;
    std::optional<double> averageSpeedKmh;
    std::optional<double> cadenceRpm;
    std::optional<double> averageCadenceRpm;
    std::optional<int> totalDistanceM;
    std::optional<int> resistanceLevel;
    std::optional<int> powerW;
    std::optional<int> averagePowerW;
    std::optional<int> totalEnergyKcal;
    std::optional<int> heartRateBpm;
    std::optional<int> elapsedTimeS;
    std::optional<int> remainingTimeS;
};

// Parses an Indoor Bike Data notification. Returns nullopt for malformed data.
std::optional<IndoorBikeData> parseIndoorBikeData(const QByteArray &data);

struct Features
{
    bool cadence = false;
    bool heartRate = false;
    bool powerMeasurement = false;
    bool resistanceTarget = false;
    bool powerTarget = false;
    bool simulation = false;
};
std::optional<Features> parseFeatures(const QByteArray &data);

struct PowerRange
{
    int minimum = 0;
    int maximum = 0;
    int increment = 1;
};
std::optional<PowerRange> parsePowerRange(const QByteArray &data);

struct ControlPointResponse
{
    quint8 requestOpCode = 0;
    quint8 result = 0;
    bool success() const { return result == quint8(ResultCode::Success); }
};
std::optional<ControlPointResponse> parseControlPointResponse(const QByteArray &data);
QString resultCodeText(quint8 result);

// Control point commands
QByteArray requestControl();
QByteArray reset();
QByteArray startOrResume();
QByteArray stopOrPause(bool pause);
QByteArray setTargetPower(int watts);
QByteArray setTargetResistance(double level);          // unit 0.1
QByteArray setIndoorBikeSimulation(double windSpeedMs, double gradePercent, double crr, double cwKgPerM);

} // namespace watt::ftms
