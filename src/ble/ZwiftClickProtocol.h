#pragma once

// Zwift Click (v1 and v2) Bluetooth LE protocol: advertising data, handshake
// and button messages. Pure code (QtCore only), unit-tested in tst_zwiftclick.
//
// Sources (protocol facts only; independent C++ implementation):
//  * jat255/zwift_click_handling (MIT License, (c) 2024 Joshua Taillon),
//    https://github.com/jat255/zwift_click_handling – Python reference for the
//    Click v1: service/characteristic UUIDs, unencrypted "RideOn" handshake,
//    message types 0x37 (buttons) / 0x19 (battery) / 0x15 (idle), and the
//    pressed = 0 / released = 1 button encoding.
//  * Makinolo, "Connecting to Zwift Play controllers" (2023) and "Zwift Ride
//    protocol" (2024, edit 01/2025), https://www.makinolo.com/blog/ –
//    manufacturer data layout (0x094A + device type), unencrypted handshake
//    (controller answers "RideOn" on the indication characteristic), message
//    0x23 with a 32-bit active-low button bitmap, and the 16-bit service
//    UUID 0xFC82 of newer firmware.
//  * ajchellew/zwiftplay and OpenBikeControl/bikecontrol (formerly
//    jonasbark/swiftcontrol) – proto definitions (ClickKeyPadStatus,
//    RideKeyPadStatus), Click v2 uses the Ride (0x23) messages; and
//    cagnulein/qdomyos-zwift notes on the Click v2 RideOn keep-alive.
//
// Not supported: the encrypted mode (ECDH + AES-CCM) used by Zwift Play and
// required by some Click firmware – it would need a crypto library. The Click
// v1 accepts the plain "RideOn" handshake; the Click v2 left pod only sends
// buttons after Zwift "unlocked" it (~24 h), its right pod always works.
#include <QByteArray>
#include <QString>
#include <QUuid>
#include <QVector>
#include <optional>

namespace watt::zwift {

constexpr quint16 kManufacturerId = 0x094A;   // Zwift, Inc. (2378)
constexpr quint16 kServiceUuid16 = 0xFC82;    // newer firmware (Ride 01/2025, Click v2)

QUuid serviceUuid();      // 00000001-19ca-4651-86e5-fa29dcdd09d1
QUuid asyncUuid();        // 00000002-... notify: button/battery messages
QUuid syncRxUuid();       // 00000003-... write: commands (handshake)
QUuid syncTxUuid();       // 00000004-... indicate: command responses
QUuid unknown6Uuid();     // 00000006-... notify (unused)

// Device type: first byte of the manufacturer specific data (after the
// company id), followed by the last two bytes of the BLE address.
enum DeviceType : quint8 {
    TypeUnknown = 0x00,
    TypeHub = 0x01,         // Zwift Hub trainer (FTMS) – not a controller
    TypePlayRight = 0x02,
    TypePlayLeft = 0x03,
    TypeRideRight = 0x07,
    TypeRideLeft = 0x08,
    TypeClick = 0x09,
};

QString deviceTypeText(int type);
// true for every Zwift controller type (not the Hub trainer)
bool isControllerType(int type);
// "Zwift Click", "Zwift Play", "Zwift Ride", "Zwift SF2" (any case)
bool nameLooksLikeController(const QString &name);

// --- handshake ------------------------------------------------------------------

QByteArray rideOn();                  // "RideOn" (52 69 64 65 4F 6E)
// Keep-alive / unlock acknowledgement sent to Click v2 pods (qdomyos-zwift,
// bikecontrol): devices already unlocked by Zwift stay unlocked.
QByteArray unlockAck();               // FF 04 00

enum class SyncResponse {
    None,                 // empty / unrelated
    RideOn,               // plain "RideOn" echo: unencrypted mode active
    PublicKey,            // "RideOn" + 01 03/04 + key: device wants encryption
    Challenge,            // 0xFF ...: Click v2 left pod not unlocked (locked)
    Other,
};
SyncResponse parseSyncResponse(const QByteArray &data);
QString syncResponseText(SyncResponse r);

// --- protobuf (minimal) ------------------------------------------------------------

struct PbField
{
    int number = 0;
    int wireType = 0;       // 0 varint, 1 fixed64, 2 length-delimited, 5 fixed32
    quint64 value = 0;      // varint / fixed value
    QByteArray bytes;       // length-delimited payload
};
bool readVarint(const QByteArray &data, int &pos, quint64 &out);
// Decodes one message level; nullopt on malformed input.
std::optional<QVector<PbField>> decodeFields(const QByteArray &data);
std::optional<quint64> varintField(const QVector<PbField> &fields, int number);

// --- async messages ------------------------------------------------------------------

enum MessageType : quint8 {
    MsgPlayKeys = 0x07,       // Zwift Play (encrypted in practice)
    MsgEmpty = 0x15,          // idle
    MsgBattery = 0x19,        // battery level (field 2), also periodic idle
    MsgRideKeys = 0x23,       // Zwift Ride / Click v2: active-low bitmap (field 1)
    MsgClickKeys = 0x37,      // Zwift Click v1: Button_Plus (1), Button_Minus (2); 0 = pressed
    MsgDeviceInfo = 0x3C,
    MsgVendor = 0xFF,
};

// RideKeyPadStatus.ButtonMap bits (a 0 bit = pressed)
enum RideButton : quint32 {
    RideLeft = 0x00001,
    RideUp = 0x00002,
    RideRight = 0x00004,
    RideDown = 0x00008,
    RideA = 0x00010,
    RideB = 0x00020,
    RideY = 0x00040,
    RideZ = 0x00100,
    RideShiftUpLeft = 0x00200,
    RideShiftDownLeft = 0x00400,
    RidePowerUpLeft = 0x00800,
    RideOnOffLeft = 0x01000,
    RideShiftUpRight = 0x02000,
    RideShiftDownRight = 0x04000,
    RidePowerUpRight = 0x10000,
    RideOnOffRight = 0x20000,
};

// Pressed state of the two virtual shift buttons.
struct ShiftButtons
{
    bool plus = false;      // shift up (harder)
    bool minus = false;     // shift down (easier)
    bool operator==(const ShiftButtons &o) const { return plus == o.plus && minus == o.minus; }
};

// Ride/Click v2 bitmap (pressed bits = 0) -> shift buttons:
//   plus  = right-hand shifter (ShiftUpRight / ShiftDownRight)
//   minus = left-hand shifter (ShiftUpLeft / ShiftDownLeft) or B
// (Zwift: right controller shifts harder, left easier. B is the "minus"
// fallback of the Click v2 right pod, as in bikecontrol's right-only mode.)
ShiftButtons shiftButtonsFromRideMask(quint32 mask);

struct Message
{
    enum class Kind { Invalid, Buttons, Battery, Idle, Other };
    Kind kind = Kind::Invalid;
    quint8 type = 0;
    ShiftButtons buttons;       // Kind::Buttons
    quint32 rideMask = 0;       // raw bitmap of 0x23 messages
    int battery = -1;           // Kind::Battery, percent
};
// One notification of the async characteristic (unencrypted).
Message parseAsync(const QByteArray &data);

// Turns pressed states into shift events: one event per press edge, then
// auto-repeat while a button is held (kRepeatDelayMs, then every
// kRepeatIntervalMs). Both buttons at once cancel each other.
class ButtonTracker
{
public:
    static constexpr int kRepeatDelayMs = 500;
    static constexpr int kRepeatIntervalMs = 250;

    // +1 (plus) / -1 (minus) events caused by this state change
    QVector<int> update(const ShiftButtons &state, qint64 nowMs);
    // auto-repeat events due at nowMs
    QVector<int> poll(qint64 nowMs);
    bool held() const { return m_state.plus != m_state.minus; }
    void reset();

private:
    ShiftButtons m_state;
    qint64 m_nextRepeat = -1;
};

} // namespace watt::zwift
