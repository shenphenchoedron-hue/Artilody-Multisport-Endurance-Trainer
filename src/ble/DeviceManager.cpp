#include "DeviceManager.h"

#include "BleHeartRateSensor.h"
#include "BleTrainer.h"
#include "Database.h"
#include "FtmsProtocol.h"
#include "HrsProtocol.h"
#include "Pm5Protocol.h"
#include "Pm5Rower.h"
#include "SensorHub.h"
#include "SimulatedDevices.h"

#include <QBluetoothLocalDevice>
#include <QCoreApplication>
#include <QTime>
#include <QVariantMap>

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
#include <QPermissions>
#endif

namespace watt {

namespace {
const QString kSimTrainer = QStringLiteral("sim:trainer");
const QString kSimHr = QStringLiteral("sim:hr");
const QString kSimRower = QStringLiteral("sim:rower");
const QString kKindTrainer = QStringLiteral("trainer");
const QString kKindHeartRate = QStringLiteral("heartRate");
const QString kKindRower = QStringLiteral("rower");

bool nameLooksLikeRower(const QString &n)
{
    // PM5 advertises as e.g. "PM5 430012345 Row" / "PM5 430012345 Ski".
    return n.trimmed().startsWith(QStringLiteral("PM5"), Qt::CaseInsensitive);
}

bool nameLooksLikeTrainer(const QString &n)
{
    static const QStringList hints { QStringLiteral("KICKR"), QStringLiteral("NEO"), QStringLiteral("TACX"),
                                     QStringLiteral("SARIS"), QStringLiteral("H3"), QStringLiteral("ELITE"),
                                     QStringLiteral("DIRETO"), QStringLiteral("SUITO"), QStringLiteral("ZWIFT"),
                                     QStringLiteral("WAHOO"), QStringLiteral("FLUX"), QStringLiteral("JETBLACK"),
                                     QStringLiteral("THINKRIDER"), QStringLiteral("VAN RYSEL"), QStringLiteral("MAGENE") };
    const QString u = n.toUpper();
    for (const QString &h : hints) {
        if (u.contains(h))
            return true;
    }
    return false;
}

bool nameLooksLikeHr(const QString &n)
{
    static const QStringList hints { QStringLiteral("HRM"), QStringLiteral("POLAR"), QStringLiteral("H10"),
                                     QStringLiteral("H9"), QStringLiteral("TICKR"), QStringLiteral("HR "),
                                     QStringLiteral("HEART"), QStringLiteral("COOSPO"), QStringLiteral("VERITY") };
    const QString u = n.toUpper();
    for (const QString &h : hints) {
        if (u.contains(h))
            return true;
    }
    return false;
}
} // namespace

DeviceManager::DeviceManager(Database *db, SensorHub *hub, QObject *parent)
    : QObject(parent)
    , m_db(db)
    , m_hub(hub)
{
    qRegisterMetaType<watt::LinkState>();

    // Route ERG requests from the (cycling) training session to the trainer.
    // The rower only follows RowingSession (RowerDevice::requestTargetPower()).
    connect(m_hub, &SensorHub::targetPowerRequested, this, [this](int watts) {
        if (m_trainer && !m_ergTest)
            m_trainer->setTargetPower(watts);
    });
    connect(m_hub, &SensorHub::freeRideRequested, this, [this] {
        if (m_trainer && !m_ergTest)
            m_trainer->setFreeRide();
    });

    // keeps "Last scan: x min ago" fresh
    m_uiTimer.setInterval(30000);
    connect(&m_uiTimer, &QTimer::timeout, this, &DeviceManager::scanningChanged);
    m_uiTimer.start();
}

DeviceManager::~DeviceManager()
{
    if (m_trainer)
        m_trainer->disconnectDevice();
    if (m_hr)
        m_hr->disconnectDevice();
    if (m_rower)
        m_rower->disconnectDevice();
}

void DeviceManager::initialize()
{
    m_autoConnect = m_db->setting(QStringLiteral("ble/autoConnect"), true).toBool();
    m_autoReconnect = m_db->setting(QStringLiteral("ble/autoReconnect"), true).toBool();
    m_allowMultiple = m_db->setting(QStringLiteral("ble/allowMultiple"), true).toBool();
    m_simulatorEnabled = m_db->setting(QStringLiteral("ble/simulator"), true).toBool();
    m_selectedAdapter = m_db->setting(QStringLiteral("ble/adapter"), 0).toInt();
    m_knownTrainer = m_db->setting(QStringLiteral("ble/knownTrainer")).toString();
    m_knownHr = m_db->setting(QStringLiteral("ble/knownHr")).toString();
    m_knownRower = m_db->setting(QStringLiteral("ble/knownRower")).toString();

    addLog(QStringLiteral("Application started"), true);
    refreshAdapter();
    addSimulatedEntries();
    emit preferencesChanged();

    if (m_autoConnect) {
        if (m_knownTrainer == kSimTrainer && m_simulatorEnabled)
            connectDevice(kSimTrainer);
        if (m_knownHr == kSimHr && m_simulatorEnabled)
            connectDevice(kSimHr);
        if (m_knownRower == kSimRower && m_simulatorEnabled)
            connectDevice(kSimRower);
        const bool needsScan = (!m_knownTrainer.isEmpty() && m_knownTrainer != kSimTrainer)
                               || (!m_knownHr.isEmpty() && m_knownHr != kSimHr)
                               || (!m_knownRower.isEmpty() && m_knownRower != kSimRower);
        if (needsScan && bluetoothAvailable())
            QTimer::singleShot(500, this, &DeviceManager::startScan);
    }
}

void DeviceManager::setRiderPhysiology(int restHr, int maxHr, int ftp)
{
    m_restHr = restHr;
    m_maxHr = maxHr;
    m_ftp = ftp;
    if (auto *sim = qobject_cast<SimulatedHeartRate *>(m_hr.data()))
        sim->setPhysiology(restHr, maxHr, ftp);
}

// --- adapter ---------------------------------------------------------------

void DeviceManager::refreshAdapter()
{
    const auto hosts = QBluetoothLocalDevice::allDevices();
    QBluetoothAddress address;
    if (m_selectedAdapter > 0 && m_selectedAdapter <= hosts.size())
        address = hosts.at(m_selectedAdapter - 1).address();
    delete m_local;
    m_local = address.isNull() ? new QBluetoothLocalDevice(this) : new QBluetoothLocalDevice(address, this);
    connect(m_local, &QBluetoothLocalDevice::hostModeStateChanged, this, [this] {
        addLog(bluetoothAvailable() ? QStringLiteral("Bluetooth ready") : QStringLiteral("Bluetooth turned off"),
               bluetoothAvailable());
        emit adapterChanged();
    });
    if (bluetoothAvailable())
        addLog(QStringLiteral("Bluetooth ready"), true);
    else
        addLog(QStringLiteral("No Bluetooth adapter available – simulator only"), false);
    emit adapterChanged();
}

bool DeviceManager::bluetoothAvailable() const
{
    return m_local && m_local->isValid() && m_local->hostMode() != QBluetoothLocalDevice::HostPoweredOff;
}

QString DeviceManager::adapterStatus() const
{
    if (!m_local || !m_local->isValid())
        return QStringLiteral("Unavailable");
    if (m_local->hostMode() == QBluetoothLocalDevice::HostPoweredOff)
        return QStringLiteral("Off");
    return QStringLiteral("Ready");
}

QString DeviceManager::adapterName() const
{
    if (!m_local || !m_local->isValid())
        return QStringLiteral("No adapter found");
    const QString n = m_local->name();
    return n.isEmpty() ? QStringLiteral("Built-in Bluetooth") : n;
}

QVariantList DeviceManager::adapters() const
{
    QVariantList list { QStringLiteral("Built-in") };
    for (const QBluetoothHostInfo &h : QBluetoothLocalDevice::allDevices())
        list.append(h.name().isEmpty() ? h.address().toString() : h.name());
    return list;
}

void DeviceManager::setSelectedAdapter(int index)
{
    if (index == m_selectedAdapter)
        return;
    stopScan();
    delete m_agent;
    m_agent = nullptr;
    m_selectedAdapter = index;
    savePreferences();
    refreshAdapter();
}

// --- scanning --------------------------------------------------------------

bool DeviceManager::scanning() const
{
    return m_agent && m_agent->isActive();
}

QString DeviceManager::lastScanText() const
{
    if (!m_lastScan.isValid())
        return QStringLiteral("Never");
    const qint64 s = m_lastScan.secsTo(QDateTime::currentDateTime());
    if (s < 60)
        return QStringLiteral("Just now");
    if (s < 3600)
        return QStringLiteral("%1 min ago").arg(s / 60);
    return m_lastScan.toString(QStringLiteral("HH:mm"));
}

void DeviceManager::startScan()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
#if QT_CONFIG(permissions)
    QBluetoothPermission permission;
    permission.setCommunicationModes(QBluetoothPermission::Access);
    switch (qApp->checkPermission(permission)) {
    case Qt::PermissionStatus::Undetermined:
        qApp->requestPermission(permission, this, [this](const QPermission &p) {
            if (p.status() == Qt::PermissionStatus::Granted)
                doStartScan();
            else
                addLog(QStringLiteral("Bluetooth permission denied"), false);
        });
        return;
    case Qt::PermissionStatus::Denied:
        addLog(QStringLiteral("Bluetooth permission denied – enable it in system settings"), false);
        return;
    case Qt::PermissionStatus::Granted:
        break;
    }
#endif
#endif
    doStartScan();
}

void DeviceManager::doStartScan()
{
    addSimulatedEntries();
    if (!bluetoothAvailable()) {
        m_scanStatus = QStringLiteral("Bluetooth unavailable – simulator only");
        addLog(m_scanStatus, false);
        emit scanningChanged();
        return;
    }
    if (!m_agent) {
        const auto hosts = QBluetoothLocalDevice::allDevices();
        if (m_selectedAdapter > 0 && m_selectedAdapter <= hosts.size())
            m_agent = new QBluetoothDeviceDiscoveryAgent(hosts.at(m_selectedAdapter - 1).address(), this);
        else
            m_agent = new QBluetoothDeviceDiscoveryAgent(this);
        m_agent->setLowEnergyDiscoveryTimeout(12000);
        connect(m_agent, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered, this, &DeviceManager::onDiscovered);
        connect(m_agent, &QBluetoothDeviceDiscoveryAgent::deviceUpdated, this,
                [this](const QBluetoothDeviceInfo &info, QBluetoothDeviceInfo::Fields) { onDiscovered(info); });
        connect(m_agent, &QBluetoothDeviceDiscoveryAgent::finished, this, [this] {
            m_scanStatus = QStringLiteral("Scan finished");
            m_lastScan = QDateTime::currentDateTime();
            emit scanningChanged();
        });
        connect(m_agent, &QBluetoothDeviceDiscoveryAgent::canceled, this, &DeviceManager::scanningChanged);
        connect(m_agent, &QBluetoothDeviceDiscoveryAgent::errorOccurred, this,
                [this](QBluetoothDeviceDiscoveryAgent::Error) {
                    m_scanStatus = QStringLiteral("Scan error: %1").arg(m_agent->errorString());
                    addLog(m_scanStatus, false);
                    emit scanningChanged();
                });
    }
    if (m_agent->isActive())
        m_agent->stop();
    m_trainers.removeDisconnected();
    m_hrSensors.removeDisconnected();
    m_rowers.removeDisconnected();
    addSimulatedEntries();
    m_scanStatus = QStringLiteral("Scanning for nearby devices…");
    addLog(QStringLiteral("Scanning for devices…"), true);
    m_agent->start(QBluetoothDeviceDiscoveryAgent::LowEnergyMethod);
    emit scanningChanged();
}

void DeviceManager::stopScan()
{
    if (m_agent && m_agent->isActive())
        m_agent->stop();
    emit scanningChanged();
}

QString DeviceManager::idFor(const QBluetoothDeviceInfo &info)
{
    return info.address().isNull() ? info.deviceUuid().toString() : info.address().toString();
}

DeviceManager::Kind DeviceManager::classify(const QBluetoothDeviceInfo &info)
{
    if (!(info.coreConfigurations() & QBluetoothDeviceInfo::LowEnergyCoreConfiguration))
        return Kind::Unknown;
    const auto uuids = info.serviceUuids();
    // Rowers first: newer PM5 firmware may also advertise FTMS (rower data),
    // which must not be treated as an indoor-bike trainer. Any Concept2
    // CE06xxxx service (the PM5 may advertise only e.g. its device
    // information or control service) or a "PM5" name identifies a PM.
    bool concept2 = nameLooksLikeRower(info.name());
    for (const QBluetoothUuid &u : uuids)
        concept2 = concept2 || pm5::shortId(QUuid(u)).has_value();
    if (concept2) {
        // A SkiErg/BikeErg PM5 ("PM5 430012345 Ski") is no rower; it is not
        // offered at all (neither as rower nor as FTMS trainer).
        if (pm5::machineKindFromName(info.name()) != pm5::MachineKind::Rower)
            return Kind::Unknown;
        return Kind::Rower;
    }
    if (uuids.contains(QBluetoothUuid(ftms::kServiceUuid)))
        return Kind::Trainer;
    if (uuids.contains(QBluetoothUuid(hrs::kServiceUuid)))
        return Kind::HeartRate;
    if (nameLooksLikeTrainer(info.name()))
        return Kind::Trainer;
    if (nameLooksLikeHr(info.name()))
        return Kind::HeartRate;
    return Kind::Unknown;
}

void DeviceManager::onDiscovered(const QBluetoothDeviceInfo &info)
{
    const Kind kind = classify(info);
    if (kind == Kind::Unknown)
        return;
    const QString id = idFor(info);
    const bool isNew = !m_infos.contains(id);
    m_infos.insert(id, info);

    // A device belongs to exactly one list: its classification may change
    // between advertisements (e.g. name or services arrive later).
    if (kind != Kind::Trainer && m_trainers.contains(id) && !(m_trainer && m_trainer->id() == id))
        m_trainers.remove(id);
    if (kind != Kind::HeartRate && m_hrSensors.contains(id) && !(m_hr && m_hr->id() == id))
        m_hrSensors.remove(id);
    if (kind != Kind::Rower && m_rowers.contains(id) && !(m_rower && m_rower->id() == id))
        m_rowers.remove(id);

    DeviceEntry e;
    e.id = id;
    e.name = info.name().isEmpty() ? id : info.name();
    e.rssi = info.rssi();
    if (kind == Kind::Trainer) {
        e.protocol = QStringLiteral("Bluetooth FTMS");
        e.kind = kKindTrainer;
        e.known = id == m_knownTrainer;
        m_trainers.upsert(e);
    } else if (kind == Kind::Rower) {
        e.protocol = QStringLiteral("Concept2 PM5 Bluetooth");
        e.kind = kKindRower;
        e.known = id == m_knownRower;
        m_rowers.upsert(e);
    } else {
        e.protocol = QStringLiteral("Bluetooth Heart Rate Service (HRS)");
        e.kind = kKindHeartRate;
        e.known = id == m_knownHr;
        m_hrSensors.upsert(e);
    }
    if (isNew)
        addLog(QStringLiteral("Found %1").arg(e.name), true);

    if (m_autoConnect && e.known) {
        // In single-sensor mode never replace another device that is already
        // connected or connecting (avoids flip-flopping between two known devices).
        if (!m_allowMultiple) {
            const bool otherBusy = (m_trainer && m_trainer->id() != id && m_trainer->state() != LinkState::Disconnected)
                                   || (m_hr && m_hr->id() != id && m_hr->state() != LinkState::Disconnected)
                                   || (m_rower && m_rower->id() != id && m_rower->state() != LinkState::Disconnected);
            if (otherBusy)
                return;
        }
        const bool trainerFree = kind == Kind::Trainer && (!m_trainer || m_trainer->id() != id);
        const bool hrFree = kind == Kind::HeartRate && (!m_hr || m_hr->id() != id);
        const bool rowerFree = kind == Kind::Rower && (!m_rower || m_rower->id() != id);
        if (trainerFree || hrFree || rowerFree)
            connectDevice(id);
    }
}

void DeviceManager::addSimulatedEntries()
{
    if (!m_simulatorEnabled)
        return;
    DeviceEntry t;
    t.id = kSimTrainer;
    t.name = QStringLiteral("Virtual Trainer");
    t.protocol = QStringLiteral("Simulated FTMS");
    t.simulated = true;
    t.kind = kKindTrainer;
    t.rssi = -52;
    t.known = m_knownTrainer == kSimTrainer;
    m_trainers.upsert(t);

    DeviceEntry h;
    h.id = kSimHr;
    h.name = QStringLiteral("Virtual HR Sensor");
    h.protocol = QStringLiteral("Simulated HRS");
    h.simulated = true;
    h.kind = kKindHeartRate;
    h.rssi = -60;
    h.known = m_knownHr == kSimHr;
    m_hrSensors.upsert(h);

    DeviceEntry r;
    r.id = kSimRower;
    r.name = QStringLiteral("Virtual Rower");
    r.protocol = QStringLiteral("Simulated Concept2 PM5");
    r.simulated = true;
    r.kind = kKindRower;
    r.rssi = -55;
    r.known = m_knownRower == kSimRower;
    m_rowers.upsert(r);
}

// --- connections -----------------------------------------------------------

void DeviceManager::connectDevice(const QString &id)
{
    QBluetoothAddress adapter;
    const auto hosts = QBluetoothLocalDevice::allDevices();
    if (m_selectedAdapter > 0 && m_selectedAdapter <= hosts.size())
        adapter = hosts.at(m_selectedAdapter - 1).address();

    if (!m_allowMultiple) {
        // single-sensor mode: only one device at a time
        if (m_trainer && m_trainer->id() != id)
            releaseTrainer();
        if (m_hr && m_hr->id() != id)
            releaseHeartRate();
        if (m_rower && m_rower->id() != id)
            releaseRower();
    }

    // The driver follows the entry kind; an id is only in one list (see
    // onDiscovered), the connected kind wins if it ever is in more.
    QString kind;
    if (m_rower && m_rower->id() == id)
        kind = kKindRower;
    else if (m_trainer && m_trainer->id() == id)
        kind = kKindTrainer;
    else if (m_hr && m_hr->id() == id)
        kind = kKindHeartRate;
    else if (m_rowers.contains(id))
        kind = kKindRower;
    else if (m_trainers.contains(id))
        kind = kKindTrainer;
    else if (m_hrSensors.contains(id))
        kind = kKindHeartRate;

    if (kind == kKindTrainer) {
        if (m_trainer && m_trainer->id() == id) {
            if (m_trainer->state() == LinkState::Disconnected)
                m_trainer->connectDevice();
            return;
        }
        releaseTrainer();
        TrainerDevice *t = nullptr;
        if (id == kSimTrainer)
            t = new SimulatedTrainer(this);
        else if (m_infos.contains(id))
            t = new BleTrainer(m_infos.value(id), adapter, this);
        if (!t)
            return;
        attachTrainer(t);
        m_knownTrainer = id;
        m_db->setSetting(QStringLiteral("ble/knownTrainer"), id);
        t->connectDevice();
    } else if (kind == kKindHeartRate) {
        if (m_hr && m_hr->id() == id) {
            if (m_hr->state() == LinkState::Disconnected)
                m_hr->connectDevice();
            return;
        }
        releaseHeartRate();
        HeartRateDevice *h = nullptr;
        if (id == kSimHr) {
            auto *sim = new SimulatedHeartRate(this);
            sim->setPhysiology(m_restHr, m_maxHr, m_ftp);
            h = sim;
        } else if (m_infos.contains(id)) {
            h = new BleHeartRateSensor(m_infos.value(id), adapter, this);
        }
        if (!h)
            return;
        attachHeartRate(h);
        m_knownHr = id;
        m_db->setSetting(QStringLiteral("ble/knownHr"), id);
        h->connectDevice();
    } else if (kind == kKindRower) {
        if (m_rower && m_rower->id() == id) {
            if (m_rower->state() == LinkState::Disconnected)
                m_rower->connectDevice();
            return;
        }
        releaseRower();
        RowerDevice *r = nullptr;
        if (id == kSimRower) {
            r = new SimulatedRower(this);   // rows freely until RowingSession sends a target
        } else if (m_infos.contains(id)) {
            r = new Pm5Rower(m_infos.value(id), adapter, this);
        }
        if (!r)
            return;
        attachRower(r);
        m_knownRower = id;
        m_db->setSetting(QStringLiteral("ble/knownRower"), id);
        r->connectDevice();
    }
    emit devicesChanged();
}

void DeviceManager::disconnectDevice(const QString &id)
{
    if (m_trainer && m_trainer->id() == id) {
        releaseTrainer();
        m_knownTrainer.clear();
        m_db->setSetting(QStringLiteral("ble/knownTrainer"), QString());
    } else if (m_hr && m_hr->id() == id) {
        releaseHeartRate();
        m_knownHr.clear();
        m_db->setSetting(QStringLiteral("ble/knownHr"), QString());
    } else if (m_rower && m_rower->id() == id) {
        releaseRower();
        m_knownRower.clear();
        m_db->setSetting(QStringLiteral("ble/knownRower"), QString());
    }
    emit devicesChanged();
}

void DeviceManager::attachTrainer(TrainerDevice *t)
{
    m_trainer = t;
    t->setAutoReconnect(m_autoReconnect);
    m_trainers.setState(t->id(), int(LinkState::Connecting));
    connect(t, &TrainerDevice::powerMeasured, m_hub, &SensorHub::updatePower);
    connect(t, &TrainerDevice::cadenceMeasured, m_hub, &SensorHub::updateCadence);
    connect(t, &TrainerDevice::speedMeasured, m_hub, &SensorHub::updateSpeed);
    connect(t, &TrainerDevice::heartRateMeasured, this, [this](int bpm) {
        // Use the trainer's HR only when no dedicated HR sensor is connected.
        if (!m_hr || m_hr->state() != LinkState::Connected)
            m_hub->updateHeartRate(bpm);
    });
    connect(t, &TrainerDevice::powerMeasured, this, [this](int watts) {
        if (auto *sim = qobject_cast<SimulatedHeartRate *>(m_hr.data()))
            sim->setLoad(watts);
    });
    connect(t, &TrainerDevice::capabilitiesChanged, this, &DeviceManager::devicesChanged);
    connect(t, &TrainerDevice::rssiChanged, this, [this, t](int rssi) {
        m_trainers.setRssi(t->id(), rssi);
        emit devicesChanged();
    });
    connect(t, &TrainerDevice::logMessage, this, &DeviceManager::addLog);
    connect(t, &TrainerDevice::stateChanged, this, [this, t](LinkState s) {
        m_trainers.setState(t->id(), int(s));
        m_hub->setTrainerConnected(s == LinkState::Connected);
        if (s != LinkState::Connected && m_ergTest) {
            m_ergTest = false;
            emit ergTestChanged();
        }
        emit devicesChanged();
    });
}

void DeviceManager::attachHeartRate(HeartRateDevice *h)
{
    m_hr = h;
    h->setAutoReconnect(m_autoReconnect);
    m_hrSensors.setState(h->id(), int(LinkState::Connecting));
    connect(h, &HeartRateDevice::heartRateMeasured, m_hub, &SensorHub::updateHeartRate);
    connect(h, &HeartRateDevice::logMessage, this, &DeviceManager::addLog);
    connect(h, &HeartRateDevice::rssiChanged, this, [this, h](int rssi) { m_hrSensors.setRssi(h->id(), rssi); });
    connect(h, &HeartRateDevice::stateChanged, this, [this, h](LinkState s) {
        m_hrSensors.setState(h->id(), int(s));
        m_hub->setHeartRateConnected(s == LinkState::Connected);
        emit devicesChanged();
    });
}

void DeviceManager::attachRower(RowerDevice *r)
{
    m_rower = r;
    r->setAutoReconnect(m_autoReconnect);
    m_rowers.setState(r->id(), int(LinkState::Connecting));
    connect(r, &RowerDevice::measurementChanged, this, &DeviceManager::rowerDataChanged);
    connect(r, &RowerDevice::heartRateMeasured, this, [this](int bpm) {
        // Use the PM5's HR only when no dedicated HR sensor is connected.
        if (!m_hr || m_hr->state() != LinkState::Connected)
            m_hub->updateHeartRate(bpm);
    });
    connect(r, &RowerDevice::powerMeasured, this, [this](int watts) {
        // Let the virtual HR sensor respond to rowing when no trainer drives it.
        if (m_trainer && m_trainer->state() == LinkState::Connected)
            return;
        if (auto *sim = qobject_cast<SimulatedHeartRate *>(m_hr.data()))
            sim->setLoad(watts);
    });
    connect(r, &RowerDevice::rssiChanged, this, [this, r](int rssi) {
        m_rowers.setRssi(r->id(), rssi);
        emit devicesChanged();
    });
    connect(r, &RowerDevice::logMessage, this, &DeviceManager::addLog);
    connect(r, &RowerDevice::stateChanged, this, [this, r](LinkState s) {
        m_rowers.setState(r->id(), int(s));
        emit devicesChanged();
        emit rowerDataChanged();
    });
    emit rowerChanged(r);
    emit rowerDataChanged();
}

void DeviceManager::releaseRower()
{
    if (!m_rower)
        return;
    RowerDevice *r = m_rower;
    m_rower = nullptr;
    m_rowers.setState(r->id(), int(LinkState::Disconnected));
    r->disconnect(this);
    r->disconnectDevice();
    r->deleteLater();
    emit rowerChanged(nullptr);
    emit rowerDataChanged();
}

void DeviceManager::releaseTrainer()
{
    if (!m_trainer)
        return;
    TrainerDevice *t = m_trainer;
    m_trainer = nullptr;
    m_trainers.setState(t->id(), int(LinkState::Disconnected));
    t->disconnect(this);
    t->disconnectDevice();
    t->deleteLater();
    m_hub->setTrainerConnected(false);
    if (m_ergTest) {
        m_ergTest = false;
        emit ergTestChanged();
    }
}

void DeviceManager::releaseHeartRate()
{
    if (!m_hr)
        return;
    HeartRateDevice *h = m_hr;
    m_hr = nullptr;
    m_hrSensors.setState(h->id(), int(LinkState::Disconnected));
    h->disconnect(this);
    h->disconnectDevice();
    h->deleteLater();
    m_hub->setHeartRateConnected(false);
    m_hub->updateHeartRate(0);
}

// --- state accessors -------------------------------------------------------

int DeviceManager::connectedCount() const
{
    int n = 0;
    if (m_trainer && m_trainer->state() == LinkState::Connected)
        ++n;
    if (m_hr && m_hr->state() == LinkState::Connected)
        ++n;
    if (m_rower && m_rower->state() == LinkState::Connected)
        ++n;
    return n;
}

QString DeviceManager::connectedSummary() const
{
    const bool t = m_trainer && m_trainer->state() == LinkState::Connected;
    const bool h = m_hr && m_hr->state() == LinkState::Connected;
    const bool r = m_rower && m_rower->state() == LinkState::Connected;
    QStringList parts;
    if (t)
        parts << QStringLiteral("Trainer");
    if (r)
        parts << QStringLiteral("Rower");
    if (h)
        parts << QStringLiteral("Heart Rate Sensor");
    if (parts.isEmpty())
        return QStringLiteral("No sensors connected");
    return parts.join(QStringLiteral(" + "));
}

QString DeviceManager::trainerId() const { return m_trainer ? m_trainer->id() : QString(); }
QString DeviceManager::trainerName() const { return m_trainer ? m_trainer->name() : QString(); }
QString DeviceManager::trainerProtocol() const { return m_trainer ? m_trainer->protocol() : QString(); }
int DeviceManager::trainerState() const { return m_trainer ? int(m_trainer->state()) : 0; }
int DeviceManager::trainerRssi() const { return m_trainer ? m_trainer->rssi() : 0; }
QStringList DeviceManager::trainerCapabilities() const { return m_trainer ? m_trainer->capabilities() : QStringList(); }
bool DeviceManager::trainerSupportsControl() const
{
    return m_trainer && m_trainer->state() == LinkState::Connected && m_trainer->supportsControl();
}
QString DeviceManager::heartRateId() const { return m_hr ? m_hr->id() : QString(); }
QString DeviceManager::heartRateName() const { return m_hr ? m_hr->name() : QString(); }
QString DeviceManager::heartRateProtocol() const { return m_hr ? m_hr->protocol() : QString(); }
int DeviceManager::heartRateState() const { return m_hr ? int(m_hr->state()) : 0; }
QString DeviceManager::rowerId() const { return m_rower ? m_rower->id() : QString(); }
QString DeviceManager::rowerName() const { return m_rower ? m_rower->name() : QString(); }
QString DeviceManager::rowerProtocol() const { return m_rower ? m_rower->protocol() : QString(); }
int DeviceManager::rowerState() const { return m_rower ? int(m_rower->state()) : 0; }
int DeviceManager::rowerRssi() const { return m_rower ? m_rower->rssi() : 0; }
bool DeviceManager::rowerConnected() const { return m_rower && m_rower->state() == LinkState::Connected; }

QString DeviceManager::signalText(int rssi) const
{
    if (rssi == 0)
        return QStringLiteral("Signal unknown");
    QString quality;
    if (rssi >= -60)
        quality = QStringLiteral("Strong Signal");
    else if (rssi >= -75)
        quality = QStringLiteral("Good Signal");
    else
        quality = QStringLiteral("Weak Signal");
    return QStringLiteral("%1 (%2 dBm)").arg(quality).arg(rssi);
}

// --- ERG test --------------------------------------------------------------

void DeviceManager::startErgTest(int watts)
{
    if (!m_trainer || m_trainer->state() != LinkState::Connected) {
        addLog(QStringLiteral("ERG test: no trainer connected"), false);
        return;
    }
    m_trainer->setTargetPower(watts);
    addLog(QStringLiteral("ERG test: target %1 W").arg(watts), true);
    if (!m_ergTest) {
        m_ergTest = true;
        emit ergTestChanged();
    }
}

void DeviceManager::stopErgTest()
{
    if (!m_ergTest)
        return;
    m_ergTest = false;
    emit ergTestChanged();
    if (m_trainer) {
        if (m_hub->requestedTarget() > 0)
            m_trainer->setTargetPower(m_hub->requestedTarget());
        else
            m_trainer->setFreeRide();
    }
    addLog(QStringLiteral("ERG test stopped"), true);
}

// --- log + preferences -----------------------------------------------------

void DeviceManager::addLog(const QString &text, bool ok)
{
    QVariantMap e;
    e.insert(QStringLiteral("time"), QTime::currentTime().toString(QStringLiteral("HH:mm:ss")));
    e.insert(QStringLiteral("text"), text);
    e.insert(QStringLiteral("ok"), ok);
    m_log.prepend(e);
    while (m_log.size() > 100)
        m_log.removeLast();
    emit logChanged();
}

void DeviceManager::clearLog()
{
    m_log.clear();
    emit logChanged();
}

void DeviceManager::savePreferences()
{
    m_db->setSetting(QStringLiteral("ble/autoConnect"), m_autoConnect);
    m_db->setSetting(QStringLiteral("ble/autoReconnect"), m_autoReconnect);
    m_db->setSetting(QStringLiteral("ble/allowMultiple"), m_allowMultiple);
    m_db->setSetting(QStringLiteral("ble/simulator"), m_simulatorEnabled);
    m_db->setSetting(QStringLiteral("ble/adapter"), m_selectedAdapter);
    emit preferencesChanged();
}

void DeviceManager::setAutoConnect(bool on)
{
    if (m_autoConnect == on)
        return;
    m_autoConnect = on;
    savePreferences();
}

void DeviceManager::setAutoReconnect(bool on)
{
    if (m_autoReconnect == on)
        return;
    m_autoReconnect = on;
    if (m_trainer)
        m_trainer->setAutoReconnect(on);
    if (m_hr)
        m_hr->setAutoReconnect(on);
    if (m_rower)
        m_rower->setAutoReconnect(on);
    savePreferences();
}

void DeviceManager::setAllowMultiple(bool on)
{
    if (m_allowMultiple == on)
        return;
    m_allowMultiple = on;
    savePreferences();
}

void DeviceManager::setSimulatorEnabled(bool on)
{
    if (m_simulatorEnabled == on)
        return;
    m_simulatorEnabled = on;
    if (!on) {
        if (m_trainer && m_trainer->isSimulated())
            disconnectDevice(m_trainer->id());
        if (m_hr && m_hr->isSimulated())
            disconnectDevice(m_hr->id());
        if (m_rower && m_rower->isSimulated())
            disconnectDevice(m_rower->id());
        m_trainers.remove(kSimTrainer);
        m_hrSensors.remove(kSimHr);
        m_rowers.remove(kSimRower);
    }
    savePreferences();
    if (on)
        addSimulatedEntries();
}

} // namespace watt
