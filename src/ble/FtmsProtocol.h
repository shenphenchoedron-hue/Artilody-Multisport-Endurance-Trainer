#pragma once

#include <QByteArray>
#include <QString>
#include <optional>

// Bluetooth SIG Fitness Machine Service (FTMS, 0x1826) – the protocol used by
// smart trainers (Tacx, Wahoo, Saris, Elite, Zwift Hub, ...).
namespace watt::ftms {

constexpr quint16 kServiceUuid = 0x1826;
constexpr quint16 kFeatureUuid = 0x2ACC;               // read
constexpr quint16 kRowerDataUuid = 0x2AD1;             // notify
constexpr quint16 kIndoorBikeDataUuid = 0x2AD2;        // notify
constexpr quint16 kTrainingStatusUuid = 0x2AD3;        // read / notify (optional)
constexpr quint16 kSupportedInclinationRangeUuid = 0x2AD5;   // read (optional)
constexpr quint16 kSupportedResistanceRangeUuid = 0x2AD6;    // read (optional)
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

// --- Advertising (FTMS v1.0 §3.1) -------------------------------------------
// Service Data AD type for UUID 0x1826: Flags (UInt8, bit 0 = Fitness Machine
// Available) followed by Fitness Machine Type (UInt16 LE).
enum MachineTypeBit : quint16 {
    MachineTreadmill = 1 << 0,
    MachineCrossTrainer = 1 << 1,
    MachineStepClimber = 1 << 2,
    MachineStairClimber = 1 << 3,
    MachineRower = 1 << 4,
    MachineIndoorBike = 1 << 5,
};

struct ServiceData
{
    bool available = false;
    quint16 machineType = 0;
    bool rower() const { return machineType & MachineRower; }
    bool indoorBike() const { return machineType & MachineIndoorBike; }
    // A rower and not (also) an indoor bike: classify as rowing machine.
    bool rowerOnly() const { return rower() && !indoorBike(); }
};
// `data` is the service data value as returned by
// QBluetoothDeviceInfo::serviceData(0x1826), i.e. without the UUID. A copy that
// still starts with the UUID (26 18) is accepted too. Returns nullopt when the
// value is too short.
std::optional<ServiceData> parseServiceData(const QByteArray &data);
// "Rower, Indoor Bike" / "none"
QString machineTypeText(quint16 machineType);

// --- Rower Data (FTMS v1.0 §4.8) ---------------------------------------------
struct RowerData
{
    bool moreData = false;                       // flags bit 0: further packets follow
    std::optional<double> strokeRateSpm;         // 0.5 /min (present when bit 0 == 0)
    std::optional<int> strokeCount;              // present when bit 0 == 0
    std::optional<double> averageStrokeRateSpm;  // 0.5 /min
    std::optional<int> totalDistanceM;
    std::optional<int> pace500S;                 // s per 500 m
    std::optional<int> averagePace500S;
    std::optional<int> powerW;
    std::optional<int> averagePowerW;
    std::optional<int> resistanceLevel;
    std::optional<int> totalEnergyKcal;
    std::optional<int> energyPerHourKcal;
    std::optional<int> energyPerMinuteKcal;
    std::optional<int> heartRateBpm;
    std::optional<double> metabolicEquivalent;   // 0.1
    std::optional<int> elapsedTimeS;
    std::optional<int> remainingTimeS;

    // Copies every field present in `other` (later packet wins).
    void merge(const RowerData &other);
    // True when `other` carries a field this record already has.
    bool overlaps(const RowerData &other) const;
    bool isEmpty() const;
};
// Parses one Rower Data notification. Returns nullopt for malformed data.
std::optional<RowerData> parseRowerData(const QByteArray &data);

// Joins Rower Data split over several notifications ("More Data"): packets
// with bit 0 set are buffered until the packet with bit 0 cleared (stroke rate
// and count) completes the record. If a buffered record would be overwritten
// (a device that never clears bit 0), the buffered record is released first.
class RowerDataAssembler
{
public:
    // Returns a complete record, or nullopt while waiting for more packets /
    // for malformed input.
    std::optional<RowerData> feed(const QByteArray &packet);
    void reset() { m_pending.reset(); }

private:
    std::optional<RowerData> m_pending;
};

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

// Guards against an endless "request control -> command -> Control Not
// Permitted (0x05) -> request control" loop: a trainer may grant control and
// still refuse the command. Counts consecutive 0x05 answers for the same
// command op code; the first kMaxImmediate refusals ask for control again at
// once, later ones only after a delay (BleTrainer: 3 s retry timer). A
// success of that op code (or reset(), e.g. after a reconnect) clears it.
class ControlRefusalGuard
{
public:
    static constexpr int kMaxImmediate = 2;
    enum class Action { RequestNow, RetryLater };
    Action onNotPermitted(quint8 opCode);
    void onSuccess(quint8 opCode);
    void reset();
    int count() const { return m_count; }
    quint8 opCode() const { return m_op; }

private:
    quint8 m_op = 0;
    int m_count = 0;
};

// Control point commands
QByteArray requestControl();
QByteArray reset();
QByteArray startOrResume();
QByteArray stopOrPause(bool pause);
QByteArray setTargetPower(int watts);
QByteArray setTargetResistance(double level);          // unit 0.1
// Non-finite values are replaced (wind/grade 0, Crr 0.004, Cw 0.39 kg/m) and
// finite ones clamped to the field ranges before rounding.
QByteArray setIndoorBikeSimulation(double windSpeedMs, double gradePercent, double crr, double cwKgPerM);

// --- Readable text for the diagnostic log (DiagLog) ------------------------
// "Set Indoor Bike Simulation" / "Op 0x2a"
QString opCodeName(quint8 opCode);
// A Control Point write, e.g. "0x11 Set Indoor Bike Simulation: wind 0.000 m/s,
// grade -2.20 %, Crr 0.0040, Cw 0.39 kg/m" or "0x05 Set Target Power: 150 W".
QString describeControlPoint(const QByteArray &command);
// A Control Point indication, e.g. "Response to 0x11 Set Indoor Bike
// Simulation: Success" (result code name + raw code).
QString describeControlPointResponse(const QByteArray &data);
// Fitness Machine Status (0x2ADA), e.g. "Indoor Bike Simulation Parameters
// Changed: wind 0.000 m/s, grade 1.50 %, Crr 0.0040, Cw 0.39 kg/m" or
// "Control Permission Lost".
QString describeMachineStatus(const QByteArray &data);
// Training Status (0x2AD3), e.g. "Speed Outside of Control Region - Low".
QString describeTrainingStatus(const QByteArray &data);
// Fitness Machine Feature (0x2ACC): every set bit by name.
QString describeFeatures(const QByteArray &data);
// Supported Inclination (0x2AD5) / Resistance Level (0x2AD6) / Power (0x2AD8)
// Range, e.g. "power 0..2000 W, step 1 W".
QString describeRange(quint16 uuid, const QByteArray &data);
// Indoor Bike Data, the fields present, e.g. "speed 33.20 km/h, cadence 90.0 rpm,
// power 185 W, resistance 12".
QString describeIndoorBikeData(const IndoorBikeData &d);

} // namespace watt::ftms
