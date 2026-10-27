#pragma once

// Zwift "hub" trainer protocol (pure, no Bluetooth): the protocol Zwift uses to
// drive trainers with built-in virtual shifting (Zwift Hub, Wahoo Kickr Core
// Zwift One, Jetblack Victory, ...). The trainer does the physics itself: the
// app sends the road (incline, CdA, Crr, wind) and the virtual gear ratio plus
// the weights; the trainer computes the brake force.
//
// Characteristics (service 0xFC82 or 00000001-19CA-4651-86E5-FA29DCDD09D1,
// shared with the Zwift Click / Play / Ride controllers):
//   00000002-... notify      "async": riding data, keep-alive, settings echo
//   00000003-... write (no response) "sync rx": commands
//   00000004-... indicate    "sync tx": handshake answer, command responses
//
// Session (unencrypted trainer variant), verified against Incyclist's
// ZwiftPlaySensor (incyclist/devices src/ble/zwift/play/sensor.ts: pair() with
// isFM -> "RideOn" 02 01; initHubService() -> 41 08 05, then waits up to 2 s
// for riding data; setSimulationData()/setGearRatio() send only the fields
// that differ from the last settings echo, each followed by HubRequest 512)
// and its protos/zwift_hub.proto:
//   1. subscribe 0002 + 0004
//   2. write "RideOn" 02 01 to 0003           (handshake, trainer variant)
//   3. trainer answers "RideOn" on 0004
//   4. write 41 08 05 to 0003                  (start the hub data stream)
//   5. trainer streams 03 <HubRidingData> on 0002 (about 1/s)
//   commands: 04 <HubCommand>, requests: 00 <HubRequest>
//
// Message prefixes (first byte), inbound: 03 riding data, 2A trainer
// response, 3C device data envelope (settings echo / device info), 19
// keep-alive/idle, 15 pairing/empty. Outbound: 04 command, 00 request,
// 41 start stream.
//
// Protobuf (proto2, protos/zwift_hub.proto):
//   HubCommand  { 3 PowerTarget uint32; 4 SimulationParam; 5 PhysicalParam }
//   SimulationParam { 1 Windx100 sint32 (m/s); 2 InclineX100 sint32 (0 sent
//                 as 1, like Incyclist); 3 CWax10000 uint32 (Zwift: 5100 =
//                 0.51 kg/m); 4 Crrx100000 uint32 (Zwift: 400) }
//   PhysicalParam { 2 GearRatioX10000; 4 BikeWeightx100; 5 RiderWeightx100 }
//   HubRequest  { 1 DataId uint32 }  (512: settings echo)
//   HubRidingData { 1 Power W; 2 Cadence rpm; 3 SpeedX100 km/h; 4 HR; 5, 6 ? }
//   DeviceDataEnvelope { 1 message_type; 2 payload }; types 512..525 carry
//     DeviceSettings { 1 SubContent { 5 Windx100 sint; 6 InclineX100 sint;
//     8 GearRatiox10000; 11 BikeWeightx100; 12 RiderWeightx100; 13 Crrx100000 } }
//   TrainerResponse { 1 Unknown; 2 { 4 Text bytes } }
//
// Byte examples (tests/tst_zwifthub.cpp):
//   Physical{gear 1.0, bike 10 kg, rider 75 kg} = 04 2A 09 10 90 4E 20 E8 07 28 CC 3A
//   Simulation{wind 0, incline -4 %, CWa 0.51, Crr 0.004}
//                                            = 04 22 0B 08 00 10 9F 06 18 EC 27 20 90 03
#include "ZwiftClickProtocol.h"

#include <QByteArray>
#include <QString>
#include <QUuid>
#include <optional>

namespace watt::zhub {

enum Prefix : quint8 {
    OutRequest = 0x00,
    OutCommand = 0x04,
    OutStartStream = 0x41,
    InRidingData = 0x03,
    InPairing = 0x15,
    InKeepAlive = 0x19,
    InTrainerResponse = 0x2A,
    InDeviceData = 0x3C,
};

constexpr quint32 kSettingsDataId = 512;
constexpr int kDefaultCdaX10000 = 5100;     // Zwift's constant CdA 0.51 m²
constexpr int kDefaultCrrX100000 = 400;     // Zwift's constant Crr 0.004

// The trainer exposes the Zwift service with the three characteristics.
bool isZwiftService(const QUuid &service);

// --- encoding -----------------------------------------------------------------------
void appendVarint(QByteArray &out, quint64 v);
quint64 zigzag(qint64 v);
qint64 unzigzag(quint64 v);

QByteArray handshake();        // "RideOn" 02 01
QByteArray startStream();      // 41 08 05

// Fields left empty are not sent (the trainer keeps its value).
struct Simulation
{
    std::optional<double> windMps;          // head wind > 0
    std::optional<double> inclinePercent;   // the grade the trainer simulates
    std::optional<double> cw;               // CWa = rho * CdA, kg/m (Zwift 0.51)
    std::optional<double> crr;              // Zwift 0.004
};
struct Physical
{
    std::optional<double> gearRatio;        // chainring / cog equivalent
    std::optional<double> bikeKg;
    std::optional<double> riderKg;
};

int inclineX100(double percent);   // rounded, 0 -> 1 (as Zwift / Incyclist)
QByteArray encodeSimulation(const Simulation &s);   // 04 22 <len> ... (only the set fields)
QByteArray encodePhysical(const Physical &p);       // 04 2A <len> ...
QByteArray encodePowerTarget(int watts);            // 04 18 <varint>
QByteArray encodeRequest(quint32 dataId);           // 00 08 <varint>

// --- decoding -----------------------------------------------------------------------
struct RidingData
{
    int power = 0;
    int cadence = 0;
    double speedKmh = 0;
    int heartRate = 0;
};
struct Settings
{
    int messageType = 0;
    std::optional<double> windMps;
    std::optional<double> inclinePercent;
    std::optional<double> gearRatio;
    std::optional<double> bikeKg;
    std::optional<double> riderKg;
    std::optional<double> crr;
};
// The payloads WITHOUT the prefix byte; nullopt on malformed / truncated data.
std::optional<RidingData> decodeRidingData(const QByteArray &payload);
std::optional<Settings> decodeDeviceData(const QByteArray &payload);   // settings echo only
std::optional<QString> decodeTrainerResponse(const QByteArray &payload);
bool isRideOn(const QByteArray &value);   // handshake answer ("RideOn" ...)

// The trainer side (virtual trainer WATT_SIM_TRAINER_ZWIFT=1, tests): decode
// an outbound command, encode what a trainer sends.
struct Command
{
    std::optional<int> powerTarget;
    std::optional<Simulation> simulation;   // fields not sent stay empty
    std::optional<Physical> physical;
};
std::optional<Command> decodeCommand(const QByteArray &payload);   // without the 04
std::optional<quint32> decodeRequest(const QByteArray &payload);   // without the 00
QByteArray encodeRidingData(const RidingData &d);                  // 03 ...
QByteArray encodeSettingsEcho(const Settings &s);                  // 3C ...

// One line for the diagnostic log, e.g. "riding data: 190 W, 80 rpm,
// 8.29 km/h" or "command Physical: gear ratio 1.0000, bike 10.00 kg, rider
// 75.00 kg"; works for both directions (prefix byte included).
QString describe(const QByteArray &message, bool outbound);

} // namespace watt::zhub
