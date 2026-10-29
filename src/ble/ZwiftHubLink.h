#pragma once

// Driver for the Zwift hub protocol on the CONNECTED trainer's own GATT link
// (ZwiftHubProtocol for the bytes). Virtual shifting with a Zwift Click on a
// trainer with the Zwift trainer service: the trainer gets the road (incline,
// CWa, Crr) and the virtual gear ratio + weights and does the physics itself.
//
// States: Absent (no Zwift service with 0002/0003/0004 on this trainer) ->
// Available (detect()) -> start(): Handshake (subscribe 0002 + 0004, "RideOn"
// 02 01, wait kHandshakeTimeoutMs for "RideOn" on 0004, one retry; without an
// answer the stream is tried anyway) -> Stream (41 08 05, wait
// kStreamTimeoutMs for riding data 0x03) -> Active. A trainer that answers
// the stream start with other messages (Zwift Hub: 2A / FE) but no riding
// data becomes Active "waiting for riding data" (gear and road are sent); it
// fails only when FTMS shows it moving for kNoRidingWhileMovingMs without
// riding data. Failed on a timeout without any answer: the owner falls back
// to FTMS gradient simulation (never ERG).
// Keep-alive: while Active (with riding data seen) the trainer must send
// something on 0002 (riding data / 0x19 idle) within kSilenceMs; after a
// silence the stream is requested again (41 08 05), after a second one the
// link fails. While waiting for riding data, a silence only re-requests.
//
// While Active, update() sends (only the fields that changed) in Zwift's
// order (see ZwiftHubProtocol.h, "Zwift's own sequence"):
//   Physical{gear ratio, rider kg, bike kg} at once on a gear / weight change;
//   Simulation{incline} when the incline moved >= 0.1 % (at most every
//     kMinSimIntervalMs), CWa / Crr / wind when they changed, and the incline
//     again every kRefreshMs anyway (timer: keeps the trainer fed);
//   after EVERY Simulation: Physical{gear ratio} again;
//   after every gear: HubRequest{520} (gear ratio, as Zwift);
//   HubRequest{512} (settings echo, logged and compared with what was sent)
//     after a change, at most every kEchoIntervalMs.
// No FTMS resistance command while the hub path is in use (BleTrainer).
// Every write and notification goes to the diagnostic log (hex + decoded).
#include "GattLink.h"
#include "SensorHub.h"
#include "ZwiftHubProtocol.h"

#include <QBluetoothUuid>
#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <functional>
#include <optional>

namespace watt {

class ZwiftHubLink : public QObject
{
    Q_OBJECT
public:
    enum class State { Absent, Available, Handshake, Stream, Active, Failed };
    Q_ENUM(State)

    static constexpr int kHandshakeTimeoutMs = 3000;
    static constexpr int kHandshakeAttempts = 2;
    static constexpr int kStreamTimeoutMs = 4000;
    static constexpr int kSilenceMs = 8000;
    static constexpr int kStreamRetries = 1;          // after a silence
    static constexpr int kRefreshMs = 2500;
    static constexpr int kMinSimIntervalMs = 1000;
    static constexpr int kEchoIntervalMs = 2000;
    static constexpr double kMinInclineChange = 0.1;   // percent
    // A trainer that answered the handshake but sends no riding data yet (the
    // Zwift Hub answers 41 08 05 with 2A / FE messages while the flywheel
    // stands still): Active "waiting for riding data". It fails only when FTMS
    // shows the trainer moving for kNoRidingWhileMovingMs without riding data.
    static constexpr double kMovingKmh = 3.0;
    static constexpr int kNoRidingWhileMovingMs = 8000;

    explicit ZwiftHubLink(GattLink *link, QObject *parent = nullptr);

    // Tests: shorter timeouts and a fake clock (ms).
    void setTimingForTesting(int handshakeMs, int streamMs, int silenceMs, int refreshMs);
    void setClockForTesting(std::function<qint64()> clock) { m_clock = std::move(clock); }

    // The link's services were inspected: Available when the trainer has the
    // Zwift service (0xFC82 or 00000001-19CA-...) with 0002/0003/0004.
    void detect();
    static bool linkHasHub(const GattLink *link);
    void start();                 // handshake (Available / Failed / Active: restart)
    void stop();                  // stop sending (back to Available)
    void reset();                 // link lost: Absent

    // Notification from the link: true when it was a hub characteristic.
    bool handleNotification(const QBluetoothUuid &uuid, const QByteArray &value);
    void update(const HubSimulation &p, bool force = false);
    void tick();                  // refresh timer (public for tests)
    // FTMS Indoor Bike Data: the trainer moves (speed / cadence)
    void noteFtmsMotion(bool moving);
    // Active, but no riding data (0x03) received yet
    bool awaitingRidingData() const { return m_state == State::Active && !m_ridingSeen; }

    State state() const { return m_state; }
    bool active() const { return m_state == State::Active; }
    bool available() const { return m_state != State::Absent; }
    // SensorHub::ZwiftHubState
    int hubState() const;
    QString failureReason() const { return m_failure; }
    QString stateText() const;
    std::optional<zhub::RidingData> lastRidingData() const { return m_riding; }
    std::optional<zhub::Settings> lastSettings() const { return m_settings; }
    bool hasParams() const { return m_haveParams; }
    HubSimulation params() const { return m_params; }

    static QBluetoothUuid asyncUuid();
    static QBluetoothUuid syncRxUuid();
    static QBluetoothUuid syncTxUuid();

signals:
    void stateChanged();
    void failed(const QString &reason);
    void ridingData(int power, int cadence, double speedKmh);

private:
    void setState(State s);
    void fail(const QString &reason);
    void sendHandshake();
    bool writeMessage(const QByteArray &message, bool log = true);
    // waiting for riding data: log the re-requests (and the answers) only
    // for the first few and then every 10th (they repeat every kSilenceMs)
    bool awaitLogDue() const { return m_awaitRequests <= 3 || m_awaitRequests % 10 == 0; }
    void onMessage(const QByteArray &value, bool fromTx);
    void becomeActive();
    qint64 now() const { return m_clock ? m_clock() : m_since.elapsed(); }

    GattLink *m_link;
    State m_state = State::Absent;
    QString m_failure;
    int m_attempt = 0;
    bool m_rideOnSeen = false;
    QTimer m_timeout;             // handshake / stream
    QTimer m_silence;             // keep-alive watchdog while active
    QTimer m_refresh;
    int m_handshakeMs = kHandshakeTimeoutMs;
    int m_streamMs = kStreamTimeoutMs;
    QElapsedTimer m_since;
    std::function<qint64()> m_clock;

    bool m_haveParams = false;
    HubSimulation m_params;
    std::optional<HubSimulation> m_sentSim;   // incline/wind/cda/crr fields used
    std::optional<HubSimulation> m_sentPhys;  // ratio/weights fields used
    qint64 m_lastSimMs = -1;
    qint64 m_lastEchoMs = -1;
    bool m_echoWanted = false;
    std::optional<zhub::RidingData> m_riding;
    std::optional<zhub::Settings> m_settings;
    QByteArray m_pending;         // fragment of a split notification
    int m_ridingLogCount = 0;
    int m_streamRetries = 0;
    bool m_trainerAnswered = false;   // any 0002 message after the stream start
    bool m_ridingSeen = false;        // riding data (0x03) in this session
    qint64 m_movingSinceMs = -1;      // FTMS motion while awaiting riding data
    int m_awaitRequests = 0;          // stream re-requests while awaiting riding data
};

} // namespace watt
