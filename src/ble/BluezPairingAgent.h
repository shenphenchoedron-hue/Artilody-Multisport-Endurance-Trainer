#pragma once

// Linux only (built when CMake finds BlueZ's platform, see src/ble/CMakeLists.txt).
//
// A BlueZ pairing agent (org.bluez.Agent1 on the system D-Bus) so Bluetooth
// devices that require a PIN / passkey can be paired from inside the app.
//
// Why: some FTMS machines only give their data over an encrypted link and
// pair with Passkey Entry - the KETTLER Track treadmills (S2 ... S10, Alpha
// Run) show a 6-digit PIN on the console in APP mode that has to be typed on
// the connecting device. On a phone the OS asks for it; on a Raspberry Pi
// nothing does (no agent, or the desktop's agent popup is hidden behind the
// full-screen app), so the pairing silently fails and the device never sends
// data. While the app runs this agent is BlueZ's default agent: both a
// pairing the kernel starts by itself (the GATT client raising the security
// level after "Insufficient Authentication", or the device's Security
// Request) and an explicit pair() reach it, and RequestPasskey /
// RequestPinCode become a PIN dialog in the app (DeviceManager ->
// PairingPinDialog.qml). Capability "KeyboardOnly": the device shows the PIN,
// the user types it here.
//
// Security: being the default agent also makes the adapter accept incoming
// pairing, so everything that does not need the user's PIN (Just Works
// authorization, numeric comparison, service authorization) is only accepted
// for devices the app itself connects to (allowDevice()); every other device
// is rejected. A PIN request always needs the user to type the number.
//
// Every call to bluetoothd is asynchronous (no blocking D-Bus call on the GUI
// thread). BlueZ waits for the agent's answers with a delayed D-Bus reply;
// the kernel's SMP timeout (30 s) bounds how long the user has.

#include <QDBusContext>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QObject>
#include <QSet>
#include <QString>

#include <functional>

namespace watt {

class BluezPairingAgent : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.bluez.Agent1")

public:
    enum class RequestKind { None, Passkey, PinCode };

    explicit BluezPairingAgent(QObject *parent = nullptr);
    ~BluezPairingAgent() override;

    // Exports the agent and registers it with org.bluez.AgentManager1 as the
    // default agent (asynchronous; registeredChanged() / logMessage() report
    // the outcome). false when the system bus is not available.
    bool start();
    bool isRegistered() const { return m_registered; }

    // A device the app connects to: Just Works / numeric comparison / service
    // authorization requests from it are accepted. Others are rejected.
    void allowDevice(const QString &address);
    bool isAllowed(const QString &address) const;

    // The open request (one at a time).
    RequestKind pendingKind() const { return m_kind; }
    QString pendingAddress() const { return m_address; }   // "AA:BB:CC:DD:EE:FF"
    // Answers the open request: digits only, a passkey must be 0..999999.
    // false when there is no request or the value is invalid (request stays open).
    bool submit(const QString &value);
    // Rejects the open request (the user cancelled).
    void reject();

    // Pairs with a device unless it is paired already: looks up its object
    // path, reads Device1.Paired and calls Device1.Pair (all asynchronous).
    // pairFinished() reports the result; alreadyPaired = it was paired
    // before (nothing was done).
    void pairIfNeeded(const QString &address);
    // The device refuses every connection: forget it in BlueZ (keys, cached
    // services), wait until it advertises again and pair it fresh with
    // Device1.Pair (the PIN dialog opens). repairFinished() gives the result
    // with BlueZ's real error text (Qt only says "Unknown Error").
    void repair(const QString &address);
    // Diagnostics: BlueZ's Device1 properties (Paired, Bonded, AddressType, ...) to the log.
    void logDeviceState(const QString &address);

    // "/org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF" -> "AA:BB:CC:DD:EE:FF" ("" when no device path)
    static QString addressFromPath(const QString &path);
    // A passkey as typed: 1..6 digits (spaces ignored) -> value, else -1.
    static int parsePasskey(const QString &text);

signals:
    // a request arrived, was answered, cancelled by BlueZ or timed out
    void requestChanged();
    void registeredChanged();
    void pairFinished(const QString &address, bool ok, bool alreadyPaired, const QString &error);
    void logMessage(const QString &text, bool ok);
    void repairFinished(const QString &address, bool ok, const QString &error);
    // BlueZ's reason for a disconnection of a device the app uses (BlueZ >= 5.73)
    void deviceDisconnected(const QString &address, const QString &reason, const QString &message);

public slots:
    // --- org.bluez.Agent1 (called by bluetoothd) -------------------------
    void Release();
    QString RequestPinCode(const QDBusObjectPath &device);
    void DisplayPinCode(const QDBusObjectPath &device, const QString &pincode);
    uint RequestPasskey(const QDBusObjectPath &device);
    void DisplayPasskey(const QDBusObjectPath &device, uint passkey, ushort entered);
    void RequestConfirmation(const QDBusObjectPath &device, uint passkey);
    void RequestAuthorization(const QDBusObjectPath &device);
    void AuthorizeService(const QDBusObjectPath &device, const QString &uuid);
    void Cancel();

private slots:
    // (private: not exported on D-Bus as an Agent1 method)
    void onBluezDisconnected(const QDBusMessage &msg);

private:
    void waitForDeviceAndPair(const QString &address);
    void beginRequest(RequestKind kind, const QDBusObjectPath &device);
    void clearRequest();
    // rejects the current D-Bus call unless the device is allowed; true = accepted
    bool acceptIfAllowed(const QDBusObjectPath &device, const QString &what);
    void requestDefaultAgent();
    // async lookup of the device's object path ("" when BlueZ does not know it)
    void findDevicePath(const QString &address, std::function<void(const QString &)> done);
    void readPairedAndPair(const QString &address, const QString &path);
    void callPair(const QString &address, const QString &path);

    bool m_registered = false;
    RequestKind m_kind = RequestKind::None;
    QString m_address;
    QDBusMessage m_pending;   // the delayed reply for the open request
    QSet<QString> m_allowed;  // upper-case addresses the app connects to
    QString m_repairAddress;  // repair() in progress
    int m_repairTries = 0;
    int m_pairRetries = 0;    // Pair retried after "Page Timeout"
};

} // namespace watt
