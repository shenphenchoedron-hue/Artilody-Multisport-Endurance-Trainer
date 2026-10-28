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
// References (compared byte by byte; our own code):
//   * Incyclist, MIT: https://github.com/incyclist/devices
//     protos/zwift_hub.proto and src/ble/zwift/play/sensor.ts
//     (pair() "RideOn" 02 01; initHubService() 41 08 05; setSimulationData()
//     / setGearRatio() each followed by HubRequest 512; CWa 5100, Crr 400,
//     incline 0 sent as 1); src/ble/fm/adapter.ts: on the hub path only FTMS
//     RequestControl, never an FTMS 0x11 / 0x05.
//   * qdomyos-zwift: src/devices/ftmsbike/ftmsbike.cpp zwiftPlayInit() (a
//     replay of a Zwift capture), src/android/src/ZwiftHubBike.java; with the
//     hub in use no FTMS control-point write at all; "gear commands don't
//     work until an inclination command has been sent first"; every gear is
//     followed by HubRequest 520 ("gearApply").
//   * Makinolo, "Zwift Trainer protocol" (2024-10-20, Kickr Core / Victory):
//     https://www.makinolo.com/blog/2024/10/20/zwift-trainer-protocol/
//
// Zwift's own sequence (qdomyos-zwift zwiftPlayInit, captured from Zwift):
//   RideOn 02 01; 41 08 05; 04 2A 04 10 C0 BB 01 (gear 2.40 = gear 12);
//   00 08 00; 41 08 05; 00 08 88 04 (request 520);
//   04 2A 0A 10 C0 BB 01 20 BF 06 28 B4 42 (gear 2.40, bike 8.31, rider 85 kg);
//   04 22 0B 08 00 10 DA 02 18 EC 27 20 90 03 (wind 0, +1.73 %, CWa 5100, Crr 400);
//   04 2A 04 10 C0 BB 01; 00 08 88 04;
//   04 22 03 10 A9 01 (-0.85 %); 04 2A 04 10 C0 BB 01; 00 08 88 04;
//   04 22 02 10 00 (0 %, sent as 0)
// i.e. after EVERY Simulation the gear ratio again, then request 520; no FTMS.
// ZwiftHubLink sends the same: Simulation -> Physical{gear} -> request 520.
//
// Session: subscribe 0002 + 0004; "RideOn" 02 01 to 0003; the trainer
// answers "RideOn" on 0004; 41 08 05 starts the stream: 03 <HubRidingData>
// on 0002 (about 1/s). Commands: 04 <HubCommand>, requests: 00 <HubRequest>.
//
// Message prefixes (first byte), inbound: 03 riding data, 2A trainer
// response, 3C device data envelope (settings echo / device info), 19
// keep-alive/idle, 15 pairing/empty. Outbound: 04 command, 00 request,
// 41 start stream.
//
// Protobuf (proto2, Incyclist protos/zwift_hub.proto = Makinolo = qdomyos):
//   HubCommand  { 3 PowerTarget uint32; 4 SimulationParam; 5 PhysicalParam }
//   SimulationParam { 1 Windx100 sint32 (m/s); 2 InclineX100 sint32 (zigzag);
//                 3 CWax10000 uint32 (Zwift: 5100); 4 Crrx100000 uint32 (Zwift: 400) }
//   PhysicalParam { 2 GearRatioX10000 uint32; 4 BikeWeightx100 uint32;
//                 5 RiderWeightx100 uint32 }
//   HubRequest  { 1 DataId uint32 }  (0 device info, 512 settings, 520 gear ratio)
//   HubRidingData { 1 Power W; 2 Cadence rpm; 3 SpeedX100 (m/s x 100: Makinolo
//                 190 W / 80 rpm / 829 = 29.8 km/h); 4 "HR" (the Victory sends
//                 the power here); 5, 6 unknown }
//   DeviceDataEnvelope { 1 message_type; 2 payload }; types 512..525 carry
//     DeviceSettings { 1 SubContent { ... } }. Incyclist's names (5 Windx100,
//     6 InclineX100, 8 GearRatiox10000, 11 BikeWeightx100, 12 RiderWeightx100,
//     13 Crrx100000) only partly fit the Victory (log of v1.3.3, sent rider
//     68 kg, bike 9 kg, CWa 0.37, Crr 0.004):
//       6  sint32, WHOLE percent truncated toward 0 (sent -2.93 % -> -2,
//          +1.89 % -> +1, -1.35 % -> -1): the sign proves the zigzag incline
//          is read correctly (an int32 reader would have stored +5.85 %)
//       8  gear ratio x 10000 (= the last Physical gear)
//       9  CWa x 10000 (= the sent CWa)
//       11 6800 = the RIDER weight x 100 (not the bike)
//       13 Crr x 100000
//       1, 2, 3, 4, 7, 10, 14, 15: unknown (e.g. 4 = 992, 7 = 30000,
//          10 = 6684, 14 = 5000), logged raw
//   TrainerResponse { 1 Unknown; 2 { 4 Text bytes } }
//
// Byte examples (tests/tst_zwifthub.cpp):
//   Physical{gear 1.0, bike 10 kg, rider 75 kg} = 04 2A 09 10 90 4E 20 E8 07 28 CC 3A
//   Simulation{wind 0, incline -4 %, CWa 0.51, Crr 0.004}
//                                            = 04 22 0B 08 00 10 9F 06 18 EC 27 20 90 03
#include "ZwiftClickProtocol.h"

#include <QByteArray>
#include <QString>
#include <QPair>
#include <QUuid>
#include <QVector>
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

constexpr quint32 kDeviceInfoDataId = 0;
constexpr quint32 kSettingsDataId = 512;
constexpr quint32 kGearRatioDataId = 520;   // Zwift sends it after every gear
constexpr int kDefaultCdaX10000 = 5100;     // Zwift's constant CWa 0.51
constexpr int kDefaultCrrX100000 = 400;     // Zwift's constant Crr 0.004
constexpr double kZwiftCwa = kDefaultCdaX10000 / 10000.0;
constexpr double kZwiftCrr = kDefaultCrrX100000 / 100000.0;

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
    std::optional<double> cw;               // CWa (Zwift 0.51)
    std::optional<double> crr;              // Zwift 0.004
};
struct Physical
{
    std::optional<double> gearRatio;        // chainring / cog equivalent
    std::optional<double> bikeKg;
    std::optional<double> riderKg;
};

int inclineX100(double percent);   // rounded (0 stays 0, as Zwift sends it)
QByteArray encodeSimulation(const Simulation &s);   // 04 22 <len> ... (only the set fields)
QByteArray encodePhysical(const Physical &p);       // 04 2A <len> ...
QByteArray encodePowerTarget(int watts);            // 04 18 <varint>
QByteArray encodeRequest(quint32 dataId);           // 00 08 <varint>

// --- decoding -----------------------------------------------------------------------
struct RidingData
{
    int power = 0;                   // 1
    int cadence = 0;                 // 2
    double speedMps = 0;             // 3: SpeedX100 = m/s x 100
    int field4 = 0;                  // 4: proto "HR" (Victory: = the power)
    std::optional<quint64> field5;   // unknown
    std::optional<quint64> field6;   // unknown
    double speedKmh() const { return speedMps * 3.6; }
};
// Settings echo (DeviceSettings.SubContent), see the field table above.
struct Settings
{
    int messageType = 0;
    std::optional<double> windMps;          // 5 (sint32, x100)
    std::optional<int> inclineWholePercent; // 6 (sint32, whole %, truncated)
    std::optional<double> gearRatio;        // 8 (x10000)
    std::optional<double> cwa;              // 9 (x10000)
    std::optional<double> weight11Kg;       // 11 (x100; the rider on the Victory)
    std::optional<double> weight12Kg;       // 12 (x100; proto: RiderWeightx100)
    std::optional<double> crr;              // 13 (x100000)
    QVector<QPair<int, quint64>> other;     // further varint fields (number, raw)
};
// The incline the Victory echoes in field 6 for a sent incline.
int echoedIncline(double sentPercent);
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
// speed 8.29 m/s (29.84 km/h), f4 0, ..." or "command Physical: gear ratio 1.0000, bike 10.00 kg, rider
// 75.00 kg"; works for both directions (prefix byte included).
QString describe(const QByteArray &message, bool outbound);

} // namespace watt::zhub
