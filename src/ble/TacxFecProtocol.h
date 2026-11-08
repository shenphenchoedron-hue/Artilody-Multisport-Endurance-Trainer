#pragma once

// Tacx "FE-C over BLE": ANT+ FE-C messages through Tacx's own GATT service on
// Tacx trainers (next to FTMS on current firmware). The app controls the
// trainer with FTMS; this service is only used for the NEO Road Feel: the
// NEO's motor vibrates like a road surface (gravel, cobbles ...), scaled by
// the trainer with the wheel speed.
//
// Message (ANT broadcast data, channel 5):
//   A4 09 4F 05 <8 data bytes> <checksum = XOR of all bytes before it>
// NEO-specific page 252 (0xFC), "NEO modes" (Tacx BleTrainerControl sample,
// pycycling tacx_trainer_control):
//   FC 00 <isokinetic 0/1> <isokinetic speed 0.05 m/s> 00 <surface> <intensity> 00
//   intensity 0..100 % (255 = trainer default); Tacx notes that 50 % already
//   feels intense.
#include <QBluetoothUuid>
#include <QByteArray>

namespace watt::tacxfec {

inline QBluetoothUuid serviceUuid() { return QBluetoothUuid(QStringLiteral("6e40fec1-b5a3-f393-e0a9-e50e24dcca9e")); }
inline QBluetoothUuid notifyUuid() { return QBluetoothUuid(QStringLiteral("6e40fec2-b5a3-f393-e0a9-e50e24dcca9e")); }
inline QBluetoothUuid writeUuid() { return QBluetoothUuid(QStringLiteral("6e40fec3-b5a3-f393-e0a9-e50e24dcca9e")); }

// NEO Road Feel surfaces (page 252, byte 5)
enum RoadSurface : quint8 {
    SurfaceOff = 0,
    ConcretePlates = 1,
    CattleGrid = 2,
    CobblestonesHard = 3,
    CobblestonesSoft = 4,
    BrickRoad = 5,
    OffRoad = 6,
    Gravel = 7,
    Ice = 8,
    WoodenBoards = 9,
};

// Road feel intensity for fine gravel (percent): noticeable, not harsh.
constexpr int kGravelIntensity = 30;

// One complete FE-C message (13 bytes) for page 252: road feel `surface` at
// `intensityPercent` (clamped 0..100), isokinetic mode off.
QByteArray encodeRoadFeel(quint8 surface, int intensityPercent);
quint8 checksum(const QByteArray &bytes);   // XOR of all bytes
QString describe(const QByteArray &message);   // for the diagnostic log

} // namespace watt::tacxfec
