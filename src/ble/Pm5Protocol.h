#pragma once

#include <QByteArray>
#include <QString>
#include <QUuid>
#include <optional>

// Concept2 PM5 rowing monitor – proprietary Bluetooth LE protocol
// ("PM Bluetooth Smart Communication Interface Definition", rev. 1.30).
//
// All proprietary UUIDs are CE06XXXX-43E5-11E4-916C-0800200C9A66. Payloads are
// little-endian, unsigned. Every parser is pure/stateless and returns nullopt
// when the payload is too short to contain the documented fields (longer
// payloads are accepted so newer firmware with appended fields still parses).
//
// Two payload layouts exist:
//  * Direct  – the separate notify characteristics 0x0031..0x003A.
//  * Multiplexed – characteristic 0x0080: byte 0 is the data id (0x31, 0x32,
//    ...), followed by the data bytes. Some multiplexed packets differ from the
//    direct ones (see Layout below). A data id is only multiplexed while the
//    separate characteristic with the same id is NOT notifying.
namespace watt::pm5 {

// Short ids (insert into the base UUID with pm5::uuid()).
constexpr quint16 kDeviceInfoService = 0x0010;
constexpr quint16 kControlService = 0x0020;
constexpr quint16 kControlReceive = 0x0021;        // write CSAFE frame
constexpr quint16 kControlTransmit = 0x0022;       // CSAFE response
constexpr quint16 kRowingService = 0x0030;
constexpr quint16 kGeneralStatus = 0x0031;         // notify, 19 bytes
constexpr quint16 kAdditionalStatus1 = 0x0032;     // notify, 17 bytes
constexpr quint16 kAdditionalStatus2 = 0x0033;     // notify, 20 bytes
constexpr quint16 kSampleRate = 0x0034;            // read/write, 1 byte
constexpr quint16 kStrokeData = 0x0035;            // notify, 20 bytes
constexpr quint16 kAdditionalStrokeData = 0x0036;  // notify, 15 bytes
constexpr quint16 kSplitIntervalData = 0x0037;
constexpr quint16 kAdditionalSplitIntervalData = 0x0038;
constexpr quint16 kEndOfWorkoutSummary = 0x0039;
constexpr quint16 kAdditionalEndOfWorkoutSummary = 0x003A;
constexpr quint16 kMultiplexed = 0x0080;           // notify, up to 20 bytes

// CE06XXXX-43E5-11E4-916C-0800200C9A66
QUuid uuid(quint16 shortId);
// Returns the XXXX part if `u` is a Concept2 PM UUID, otherwise nullopt.
std::optional<quint16> shortId(const QUuid &u);

enum class Layout { Direct, Multiplexed };

// Characteristic 0x0034 value.
enum class SampleRate : quint8 { Ms1000 = 0, Ms500 = 1, Ms250 = 2, Ms100 = 3 };
QByteArray sampleRateCommand(SampleRate rate);
// Picks the closest supported rate that is not slower than `intervalMs`.
SampleRate sampleRateForInterval(int intervalMs);

// OBJ_WORKOUTSTATE_T
enum class WorkoutState : quint8 {
    WaitToBegin = 0,
    WorkoutRow = 1,
    CountdownPause = 2,
    IntervalRest = 3,
    IntervalWorkTime = 4,
    IntervalWorkDistance = 5,
    IntervalRestEndToWorkTime = 6,
    IntervalRestEndToWorkDistance = 7,
    IntervalWorkTimeToRest = 8,
    IntervalWorkDistanceToRest = 9,
    WorkoutEnd = 10,
    Terminate = 11,
    WorkoutLogged = 12,
    Rearm = 13,
};
// OBJ_ROWINGSTATE_T
enum class RowingState : quint8 { Inactive = 0, Active = 1 };

// True for workout states in which the athlete is actually working
// (rowing or in a work interval; not waiting, resting or finished).
bool isWorkingState(int workoutState);

// OBJ_ERGMACHINETYPE_T: rowers are the static/slides/dyno types (< 128) and
// the MultiErg row type (224). SkiErg (128..143, 225) and BikeErg (192..207,
// 226) report no rowing data in the sense of this app.
enum class MachineKind { Rower, SkiErg, BikeErg };
MachineKind machineKind(int ergMachineType);
// Advertised name hint: PM5 names end in "Row", "Ski" or "Bike"
// (e.g. "PM5 430012345 Ski"). Unknown suffix -> Rower.
MachineKind machineKindFromName(const QString &name);

// 0x0031 – identical in both layouts (19 bytes).
struct GeneralStatus
{
    double elapsedSeconds = 0;      // 0.01 s
    double distanceMeters = 0;      // 0.1 m
    int workoutType = 0;
    int intervalType = 0;
    int workoutState = 0;           // WorkoutState
    int rowingState = 0;            // RowingState
    int strokeState = 0;
    int totalWorkDistance = 0;      // m
    int workoutDuration = 0;        // raw; 0.01 s if duration type is time, else m/cal
    int durationType = 0;
    int dragFactor = 0;
    bool rowingActive() const { return rowingState == int(RowingState::Active); }
};

// 0x0032 – direct 17 bytes; multiplexed 19 bytes (average power inserted
// before erg machine type). A direct packet of >= 19 bytes (current firmware)
// is parsed in the 19-byte layout.
struct AdditionalStatus1
{
    double elapsedSeconds = 0;
    double speedMps = 0;            // 0.001 m/s
    int strokesPerMinute = 0;
    int heartRateBpm = 0;           // 0 when the PM reports 255 (invalid)
    bool heartRateValid = false;
    double currentPace500Seconds = 0;   // 0.01 s per 500 m
    double averagePace500Seconds = 0;
    int restDistanceMeters = 0;
    double restTimeSeconds = 0;     // 0.01 s
    int ergMachineType = 0;
    std::optional<int> averagePowerW;   // multiplexed layout / 19-byte direct packet
};

// 0x0033 – direct 20 bytes; multiplexed 18 bytes (no average power). A direct
// packet of 18/19 bytes is parsed in the 18-byte layout (no average power).
struct AdditionalStatus2
{
    double elapsedSeconds = 0;
    int intervalCount = 0;
    std::optional<int> averagePowerW;   // 20-byte direct layout only
    int totalCalories = 0;
    double splitAveragePace500Seconds = 0;
    int splitAveragePowerW = 0;
    int splitAverageCalories = 0;       // cal/hr
    double lastSplitTimeSeconds = 0;    // 0.1 s
    int lastSplitDistanceMeters = 0;
};

// 0x0036 – direct 15 bytes; multiplexed 17 bytes (work per stroke appended;
// read from any packet of >= 17 bytes).
struct AdditionalStrokeData
{
    double elapsedSeconds = 0;
    int strokePowerW = 0;
    int strokeCaloriesPerHour = 0;
    int strokeCount = 0;
    int projectedWorkTimeSeconds = 0;
    int projectedWorkDistanceMeters = 0;
    std::optional<double> workPerStrokeJoules;   // when >= 17 bytes (0.1 J)
};

int expectedLength(quint16 shortId, Layout layout);   // 0 = unknown id

std::optional<GeneralStatus> parseGeneralStatus(const QByteArray &data);
std::optional<AdditionalStatus1> parseAdditionalStatus1(const QByteArray &data, Layout layout = Layout::Direct);
std::optional<AdditionalStatus2> parseAdditionalStatus2(const QByteArray &data, Layout layout = Layout::Direct);
std::optional<AdditionalStrokeData> parseAdditionalStrokeData(const QByteArray &data, Layout layout = Layout::Direct);

// Decoded 0x0080 notification. `id` is always set when parsing succeeded;
// exactly one of the optionals is filled for supported ids (0x31, 0x32, 0x33,
// 0x36). Other ids (0x35, 0x37..0x3C) are returned with no payload decoded.
struct MultiplexedPacket
{
    quint8 id = 0;
    QByteArray payload;   // data bytes without the id byte
    std::optional<GeneralStatus> generalStatus;
    std::optional<AdditionalStatus1> additionalStatus1;
    std::optional<AdditionalStatus2> additionalStatus2;
    std::optional<AdditionalStrokeData> additionalStrokeData;
    bool decoded() const
    {
        return generalStatus || additionalStatus1 || additionalStatus2 || additionalStrokeData;
    }
};

// Returns nullopt for an empty packet, or when a supported id carries too few bytes.
std::optional<MultiplexedPacket> parseMultiplexed(const QByteArray &data);

// Concept2 pace/power relation: watts = 2.80 / (s per m)^3.
double wattsFromPace500(double pace500Seconds);
double pace500FromWatts(double watts);   // 0 for watts <= 0

} // namespace watt::pm5
