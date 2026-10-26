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
#include "ZwiftClick.h"
#include "ZwiftClickProtocol.h"

#include <QBluetoothLocalDevice>
#include <QClipboard>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QRegularExpression>
#include <QSysInfo>
#include <QTextStream>
#include <QTime>
#include <QVariantMap>

#include <algorithm>

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
#include <QPermissions>
#endif

// Visible in a terminal / journalctl; silence with QT_LOGGING_RULES="artilody.ble.info=false".
Q_LOGGING_CATEGORY(lcBle, "artilody.ble")

namespace watt {

namespace {
const QString kSimTrainer = QStringLiteral("sim:trainer");
const QString kSimHr = QStringLiteral("sim:hr");
const QString kSimRower = QStringLiteral("sim:rower");
const QString kKindTrainer = QStringLiteral("trainer");
const QString kKindHeartRate = QStringLiteral("heartRate");
const QString kKindRower = QStringLiteral("rower");
const QString kKindOther = QStringLiteral("other");
const QString kKindController = QStringLiteral("controller");

// Zwift Click / Play / Ride: manufacturer data 0x094A with a controller type
// byte (not the Hub trainer), the newer 0xFC82 service, or the name.
bool isZwiftController(const QBluetoothDeviceInfo &info, const QString &name)
{
    const QByteArray mfr = info.manufacturerData(zwift::kManufacturerId);
    if (!mfr.isEmpty())
        return zwift::isControllerType(quint8(mfr.at(0)));
    if (zwift::nameLooksLikeController(name))
        return true;
    return info.serviceUuids().contains(QBluetoothUuid(quint16(zwift::kServiceUuid16)));
}

bool nameLooksLikeConcept2(const QString &n)
{
    // PM5 advertises as "PM5 430012345", newer firmware may append " Row",
    // " Ski" or " Bike". Accept any case and an explicit "Concept2" name.
    const QString t = n.trimmed();
    return t.startsWith(QLatin1String("PM5"), Qt::CaseInsensitive)
           || t.contains(QLatin1String("Concept2"), Qt::CaseInsensitive)
           || t.contains(QLatin1String("Concept 2"), Qt::CaseInsensitive);
}

bool hasConcept2Uuid(const QBluetoothDeviceInfo &info)
{
    // Any CE06xxxx-43E5-11E4-916C-0800200C9A66 service: 0x0000 (discovery,
    // the one the PM5 advertises), 0x0010 (info), 0x0020 (control), 0x0030 (rowing).
    const auto uuids = info.serviceUuids();
    for (const QBluetoothUuid &u : uuids) {
        if (pm5::shortId(QUuid(u)).has_value())
            return true;
    }
    return false;
}

// FTMS advertising service data (Flags + Fitness Machine Type), if any.
std::optional<ftms::ServiceData> ftmsServiceData(const QBluetoothDeviceInfo &info)
{
    const QByteArray raw = info.serviceData(QBluetoothUuid(ftms::kServiceUuid));
    if (raw.isEmpty())
        return std::nullopt;
    return ftms::parseServiceData(raw);
}

QString ftmsServiceDataText(const QBluetoothDeviceInfo &info)
{
    const QByteArray raw = info.serviceData(QBluetoothUuid(ftms::kServiceUuid));
    if (raw.isEmpty())
        return QString();
    const auto d = ftms::parseServiceData(raw);
    if (!d)
        return QStringLiteral("malformed (%1)").arg(QString::fromLatin1(raw.toHex(' ')));
    return QStringLiteral("machine %1, type 0x%2 (%3) -> %4  [raw %5]")
        .arg(d->available ? QStringLiteral("available") : QStringLiteral("not available"))
        .arg(d->machineType, 4, 16, QLatin1Char('0'))
        .arg(ftms::machineTypeText(d->machineType),
             d->rowerOnly() ? QStringLiteral("rower") : d->indoorBike() ? QStringLiteral("indoor bike") : QStringLiteral("other"),
             QString::fromLatin1(raw.toHex(' ')));
}

QString uuidText(const QBluetoothUuid &u)
{
    bool ok = false;
    const quint16 s16 = u.toUInt16(&ok);
    if (ok)
        return QStringLiteral("0x%1").arg(s16, 4, 16, QLatin1Char('0'));
    return u.toString(QUuid::WithoutBraces).toUpper();
}

QString coreConfigText(QBluetoothDeviceInfo::CoreConfigurations c)
{
    if (c == QBluetoothDeviceInfo::UnknownCoreConfiguration)
        return QStringLiteral("unknown");
    QStringList parts;
    if (c & QBluetoothDeviceInfo::LowEnergyCoreConfiguration)
        parts << QStringLiteral("LE");
    if (c & QBluetoothDeviceInfo::BaseRateCoreConfiguration)
        parts << QStringLiteral("BR/EDR");
    return parts.join(QLatin1Char('+'));
}

QString hostModeText(QBluetoothLocalDevice::HostMode m)
{
    switch (m) {
    case QBluetoothLocalDevice::HostPoweredOff: return QStringLiteral("powered off");
    case QBluetoothLocalDevice::HostConnectable: return QStringLiteral("on (connectable)");
    case QBluetoothLocalDevice::HostDiscoverable: return QStringLiteral("on (discoverable)");
    case QBluetoothLocalDevice::HostDiscoverableLimitedInquiry: return QStringLiteral("on (limited discoverable)");
    }
    return QStringLiteral("unknown");
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

    // One "Scan" press scans for this long. The agent itself runs without a
    // timeout (see doStartScan()), this timer ends the window.
    m_scanWindow.setSingleShot(true);
    m_scanWindow.setInterval(kScanWindowMs);
    connect(&m_scanWindow, &QTimer::timeout, this, [this] {
        if (m_agent && m_agent->isActive())
            m_agent->stop();   // -> canceled() -> onScanWindowEnded()
    });
}

DeviceManager::~DeviceManager()
{
    if (m_trainer)
        m_trainer->disconnectDevice();
    if (m_hr)
        m_hr->disconnectDevice();
    if (m_rower)
        m_rower->disconnectDevice();
    for (const auto &c : std::as_const(m_controllerDevs)) {
        if (c)
            c->disconnectDevice();
    }
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
    m_knownControllers = m_db->setting(QStringLiteral("ble/knownControllers")).toString().split(QLatin1Char(';'), Qt::SkipEmptyParts);
    m_showAll = m_db->setting(QStringLiteral("ble/showAll"), false).toBool();
    const QStringList rowers = m_db->setting(QStringLiteral("ble/rowerIds")).toString().split(QLatin1Char(';'), Qt::SkipEmptyParts);
    m_forcedRowers = QSet<QString>(rowers.cbegin(), rowers.cend());

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
                               || (!m_knownRower.isEmpty() && m_knownRower != kSimRower)
                               || !m_knownControllers.isEmpty();
        if (needsScan && bluetoothAvailable()) {
            m_autoRescans = 4;   // keep looking for a known device for ~5 min
            QTimer::singleShot(500, this, &DeviceManager::startScan);
        }
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
    connect(m_local, &QBluetoothLocalDevice::hostModeStateChanged, this, [this](QBluetoothLocalDevice::HostMode m) {
        addLog(bluetoothAvailable() ? QStringLiteral("Bluetooth ready") : QStringLiteral("Bluetooth turned off"),
               bluetoothAvailable());
        addDiag(QStringLiteral("Adapter host mode: %1").arg(hostModeText(m)));
        if (bluetoothAvailable() && m_scanAfterPowerOn) {
            m_scanAfterPowerOn = false;
            setScanError(QString());
            doStartScan();
        }
        emit adapterChanged();
    });
    if (bluetoothAvailable()) {
        addLog(QStringLiteral("Bluetooth ready"), true);
    } else {
        addLog(QStringLiteral("No Bluetooth adapter available – simulator only"), false);
    }
    if (m_local->isValid())
        addDiag(QStringLiteral("Adapter %1 (%2), host mode: %3")
                    .arg(m_local->name(), m_local->address().toString(), hostModeText(m_local->hostMode())));
    else
        addDiag(QStringLiteral("No valid Bluetooth adapter (QBluetoothLocalDevice invalid)"));
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

void DeviceManager::doStartScan(bool clearLists)
{
    addSimulatedEntries();
    m_userStop = false;
    if (!m_local || !m_local->isValid()) {
        m_scanStatus = QStringLiteral("Bluetooth unavailable – simulator only");
        setScanError(QStringLiteral("No Bluetooth adapter found. Check that Bluetooth is enabled "
                                    "(Raspberry Pi: `bluetoothctl show`, `systemctl status bluetooth`)."));
        addLog(m_scanStatus, false);
        emit scanningChanged();
        return;
    }
    if (m_local->hostMode() == QBluetoothLocalDevice::HostPoweredOff) {
        // Try to switch the adapter on; the scan starts from hostModeStateChanged.
        m_scanAfterPowerOn = true;
        m_scanStatus = QStringLiteral("Bluetooth is off");
        setScanError(QStringLiteral("Bluetooth is turned off – trying to switch it on. If nothing happens, turn it on "
                                    "in the system settings (Raspberry Pi: `bluetoothctl power on`, "
                                    "`sudo rfkill unblock bluetooth`)."));
        addLog(QStringLiteral("Bluetooth is off – requesting power on"), false);
        m_local->powerOn();
        emit scanningChanged();
        return;
    }
    if (!m_agent) {
        const auto hosts = QBluetoothLocalDevice::allDevices();
        if (m_selectedAdapter > 0 && m_selectedAdapter <= hosts.size())
            m_agent = new QBluetoothDeviceDiscoveryAgent(hosts.at(m_selectedAdapter - 1).address(), this);
        else
            m_agent = new QBluetoothDeviceDiscoveryAgent(this);
        // No agent timeout: with Qt's BlueZ backend (Qt <= 6.5) a device whose
        // name arrives after its first report (scan response) is only ever
        // re-reported when the timeout is 0 – with a timeout the update is
        // dropped as an "almost duplicate" (qbluetoothdevicediscoveryagent_bluez.cpp,
        // _q_PropertiesChanged). m_scanWindow ends the scan instead.
        m_agent->setLowEnergyDiscoveryTimeout(0);
        connect(m_agent, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered, this,
                [this](const QBluetoothDeviceInfo &info) { handleDeviceInfo(info, false); });
        connect(m_agent, &QBluetoothDeviceDiscoveryAgent::deviceUpdated, this,
                [this](const QBluetoothDeviceInfo &info, QBluetoothDeviceInfo::Fields) { handleDeviceInfo(info, true); });
        connect(m_agent, &QBluetoothDeviceDiscoveryAgent::finished, this,
                [this] { onScanWindowEnded(QStringLiteral("finished")); });
        connect(m_agent, &QBluetoothDeviceDiscoveryAgent::canceled, this,
                [this] { onScanWindowEnded(QStringLiteral("stopped")); });
        connect(m_agent, &QBluetoothDeviceDiscoveryAgent::errorOccurred, this,
                [this](QBluetoothDeviceDiscoveryAgent::Error e) {
                    m_scanWindow.stop();
                    const QString text = scanErrorText(int(e), m_agent->errorString());
                    m_scanStatus = QStringLiteral("Scan failed");
                    setScanError(text);
                    addLog(text, false);
                    addDiag(QStringLiteral("Discovery error %1: %2").arg(int(e)).arg(m_agent->errorString()));
                    qCWarning(lcBle) << "Discovery error" << e << m_agent->errorString();
                    // An I/O error is also what BlueZ reports when another
                    // program (e.g. bluetoothctl) interrupted the discovery.
                    if (e == QBluetoothDeviceDiscoveryAgent::InputOutputError && m_ioRetries < 2 && !m_userStop) {
                        ++m_ioRetries;
                        QTimer::singleShot(3000, this, [this] {
                            if (!scanning())
                                doStartScan(false);
                        });
                    }
                    emit scanningChanged();
                });
    }
    if (m_agent->isActive()) {
        m_userStop = true;   // restart, not a finished window
        m_agent->stop();
        m_userStop = false;
    }
    if (clearLists) {
        m_trainers.removeDisconnected();
        m_hrSensors.removeDisconnected();
        m_rowers.removeDisconnected();
        m_others.removeDisconnected();
        m_controllers.removeDisconnected();
    }
    addSimulatedEntries();
    setScanError(QString());
    m_scanStatus = QStringLiteral("Scanning for nearby devices…");
    addLog(QStringLiteral("Scanning for devices…"), true);
    addDiag(QStringLiteral("Scan started (LowEnergyMethod, %1 s window)").arg(kScanWindowMs / 1000));
    qCInfo(lcBle) << "Scan started on adapter" << (m_local ? m_local->address().toString() : QString());
    m_scanStartedAt = QDateTime::currentDateTime();
    m_agent->start(QBluetoothDeviceDiscoveryAgent::LowEnergyMethod);
    if (m_agent->isActive())
        m_scanWindow.start();
    emit scanningChanged();
}

void DeviceManager::ensureScanning()
{
    // BlueZ forgets unpaired devices ~30 s after it last saw them; Qt then
    // fails to connect with UnknownRemoteDeviceError. Scanning keeps the
    // device object alive (and re-creates it) while a connection is tried.
    if (!scanning() && bluetoothAvailable())
        doStartScan(false);
}

void DeviceManager::onScanWindowEnded(const QString &how)
{
    if (m_agent && m_agent->isActive()) {   // late signal of a restarted scan
        emit scanningChanged();
        return;
    }
    m_scanWindow.stop();
    m_lastScan = QDateTime::currentDateTime();
    if (m_userStop) {
        emit scanningChanged();
        return;
    }
    int real = 0;
    for (const DiagEntry &d : std::as_const(m_diag)) {
        if (d.lastSeen >= m_scanStartedAt)
            ++real;
    }
    m_ioRetries = 0;
    m_scanStatus = QStringLiteral("Scan %1 – %2 Bluetooth device(s) seen").arg(how).arg(real);
    addDiag(QStringLiteral("Scan %1, %2 device(s) seen").arg(how).arg(real));
    qCInfo(lcBle) << "Scan" << how << "-" << real << "device(s) seen";

    auto missing = [](const QString &known, const QString &sim, const auto &dev) {
        return !known.isEmpty() && known != sim && !(dev && dev->state() != LinkState::Disconnected);
    };
    bool knownMissing = missing(m_knownTrainer, kSimTrainer, m_trainer) || missing(m_knownHr, kSimHr, m_hr)
                        || missing(m_knownRower, kSimRower, m_rower);
    for (const QString &id : std::as_const(m_knownControllers)) {
        const ControllerDevice *c = controllerFor(id);
        if (!c || c->state() == LinkState::Disconnected)
            knownMissing = true;
    }
    if (m_autoConnect && knownMissing && m_autoRescans > 0) {
        --m_autoRescans;
        QTimer::singleShot(1000, this, [this] {
            if (!scanning())
                doStartScan(false);
        });
    }
    emit scanningChanged();
}

void DeviceManager::stopScan()
{
    m_scanWindow.stop();
    m_autoRescans = 0;
    m_scanAfterPowerOn = false;
    if (m_agent && m_agent->isActive()) {
        m_userStop = true;
        m_agent->stop();
        m_lastScan = QDateTime::currentDateTime();
        m_scanStatus = QStringLiteral("Scan stopped");
    }
    emit scanningChanged();
}

void DeviceManager::setScanError(const QString &text)
{
    if (m_scanError == text)
        return;
    m_scanError = text;
    emit scanningChanged();
}

void DeviceManager::clearScanError()
{
    setScanError(QString());
}

QString DeviceManager::scanErrorText(int error, const QString &qtText)
{
    using A = QBluetoothDeviceDiscoveryAgent;
    switch (A::Error(error)) {
    case A::NoError:
        return QString();
    case A::PoweredOffError:
        return QStringLiteral("Bluetooth is turned off. Turn it on and scan again "
                              "(Raspberry Pi: `bluetoothctl power on`; if it is blocked: `sudo rfkill unblock bluetooth`).");
    case A::InputOutputError:
        return QStringLiteral("Bluetooth adapter error (%1). Another program may have interrupted the scan, or the "
                              "bluetooth service is not running (Raspberry Pi: `sudo systemctl restart bluetooth`).")
            .arg(qtText.isEmpty() ? QStringLiteral("I/O error") : qtText);
    case A::InvalidBluetoothAdapterError:
        return QStringLiteral("The selected Bluetooth adapter was not found – choose another one under Connection Preferences.");
    case A::UnsupportedPlatformError:
        return QStringLiteral("Bluetooth scanning is not supported on this system.");
    case A::UnsupportedDiscoveryMethod:
        return QStringLiteral("This Bluetooth adapter does not support Bluetooth Low Energy scanning "
                              "(a Bluetooth 4.0+ adapter and BlueZ 5 are required).");
    case A::LocationServiceTurnedOffError:
        return QStringLiteral("Location services are turned off – they are required for Bluetooth scanning on this system.");
#if QT_VERSION >= QT_VERSION_CHECK(6, 4, 0)
    case A::MissingPermissionsError:
        return QStringLiteral("Bluetooth permission missing – allow Bluetooth for Artilody Watts Trainer in the system settings.");
#endif
    default:
        break;
    }
    return QStringLiteral("Bluetooth scan error: %1").arg(qtText.isEmpty() ? QString::number(error) : qtText);
}

QString DeviceManager::idFor(const QBluetoothDeviceInfo &info)
{
    return info.address().isNull() ? info.deviceUuid().toString() : info.address().toString();
}

QString DeviceManager::effectiveName(const QBluetoothDeviceInfo &info)
{
    // BlueZ reports Device1.Alias, which is the address with dashes until the
    // real name (usually in the scan response) has been received.
    static const QRegularExpression addressLike(QStringLiteral("^([0-9A-Fa-f]{2}[-:]){5}[0-9A-Fa-f]{2}$"));
    const QString n = info.name().trimmed();
    if (n.isEmpty() || addressLike.match(n).hasMatch())
        return QString();
    return n;
}

DeviceManager::Kind DeviceManager::classifyName(const QString &name)
{
    if (!nameLooksLikeConcept2(name))
        return Kind::Unknown;
    // A SkiErg/BikeErg PM5 ("PM5 430012345 Ski") is no rower.
    return pm5::machineKindFromName(name) == pm5::MachineKind::Rower ? Kind::Rower : Kind::Unknown;
}

DeviceManager::Kind DeviceManager::classify(const QBluetoothDeviceInfo &info)
{
    const QString name = effectiveName(info);
    // Rowers first: newer PM5 firmware may also advertise FTMS (rower data),
    // which must not be treated as an indoor-bike trainer. Any Concept2
    // CE06xxxx service or a "PM5"/"Concept2" name identifies a PM. No core
    // configuration check here: BlueZ derives it from the Class property and
    // cached device objects may report it oddly.
    if (hasConcept2Uuid(info) || nameLooksLikeConcept2(name)) {
        // SkiErg/BikeErg monitors are not offered (neither as rower nor as
        // FTMS trainer); they are listed under "Other devices".
        if (pm5::machineKindFromName(name) != pm5::MachineKind::Rower)
            return Kind::Unknown;
        return Kind::Rower;
    }
    // FTMS advertising service data tells the machine type (FTMS v1.0 §3.1):
    // current PM5 firmware advertises FTMS 0x1826 with the Rower bit, and its
    // name may arrive late (or never) on BlueZ.
    if (const auto sd = ftmsServiceData(info)) {
        if (sd->rowerOnly())
            return Kind::Rower;
        if (sd->machineType != 0 && !sd->indoorBike())
            return Kind::Unknown;   // treadmill, cross trainer, ... – not supported
    }
    if (!(info.coreConfigurations() & QBluetoothDeviceInfo::LowEnergyCoreConfiguration))
        return Kind::Unknown;
    const auto uuids = info.serviceUuids();
    if (uuids.contains(QBluetoothUuid(ftms::kServiceUuid)) || !info.serviceData(QBluetoothUuid(ftms::kServiceUuid)).isEmpty())
        return Kind::Trainer;
    if (uuids.contains(QBluetoothUuid(hrs::kServiceUuid)))
        return Kind::HeartRate;
    // Before the name hints: "Zwift Click" would match the trainer hint "ZWIFT".
    if (isZwiftController(info, name))
        return Kind::Controller;
    if (nameLooksLikeTrainer(name))
        return Kind::Trainer;
    if (nameLooksLikeHr(name))
        return Kind::HeartRate;
    return Kind::Unknown;
}

QString DeviceManager::kindName(Kind k)
{
    switch (k) {
    case Kind::Trainer: return QStringLiteral("Trainer");
    case Kind::HeartRate: return QStringLiteral("HeartRate");
    case Kind::Rower: return QStringLiteral("Rower");
    case Kind::Controller: return QStringLiteral("Controller");
    case Kind::Unknown: break;
    }
    return QStringLiteral("Unknown");
}

void DeviceManager::handleDeviceInfo(const QBluetoothDeviceInfo &incoming, bool updated)
{
    const QString id = idFor(incoming);
    if (id.isEmpty())
        return;

    // Merge with what we already know: later reports may lack the name
    // (BlueZ alias placeholder) or carry fewer service UUIDs.
    QBluetoothDeviceInfo info = incoming;
    const auto old = m_infos.constFind(id);
    if (old != m_infos.constEnd()) {
        if (effectiveName(info).isEmpty() && !effectiveName(*old).isEmpty())
            info.setName(old->name());
        QList<QBluetoothUuid> uuids = info.serviceUuids();
        for (const QBluetoothUuid &u : old->serviceUuids()) {
            if (!uuids.contains(u))
                uuids.append(u);
        }
        info.setServiceUuids(uuids);
        if (info.rssi() == 0 && old->rssi() != 0)
            info.setRssi(old->rssi());
        const auto oldMfr = old->manufacturerData();
        for (auto it = oldMfr.cbegin(); it != oldMfr.cend(); ++it) {
            if (!info.manufacturerData().contains(it.key()))
                info.setManufacturerData(it.key(), it.value());
        }
        info.setCoreConfigurations(info.coreConfigurations() | old->coreConfigurations());
        for (const QBluetoothUuid &sid : old->serviceIds()) {
            if (info.serviceData(sid).isEmpty())
                info.setServiceData(sid, old->serviceData(sid));
        }
    }
    m_infos.insert(id, info);

    const Kind kind = m_forcedRowers.contains(id) ? Kind::Rower : classify(info);
    const auto prevIt = m_kinds.constFind(id);
    const bool isNew = prevIt == m_kinds.constEnd();
    const Kind prev = isNew ? Kind::Unknown : *prevIt;
    m_kinds.insert(id, kind);
    recordDiag(id, info, kind);

    const QString name = effectiveName(info);
    if (isNew || prev != kind) {
        QStringList uuids;
        for (const QBluetoothUuid &u : info.serviceUuids())
            uuids << uuidText(u);
        qCInfo(lcBle).noquote() << (isNew ? "Discovered" : "Reclassified") << id << "name:" << (name.isEmpty() ? QStringLiteral("<none>") : name)
                                << "rssi:" << info.rssi() << "uuids:" << uuids.join(QLatin1Char(','))
                                << "core:" << coreConfigText(info.coreConfigurations())
                                << "->" << kindName(kind) << (isNew ? "" : QStringLiteral("(was %1)").arg(kindName(prev)))
                                << (updated ? "[update]" : "");
        if (!isNew)
            addDiag(QStringLiteral("%1 reclassified %2 -> %3").arg(name.isEmpty() ? id : name, kindName(prev), kindName(kind)));
    }

    // A device belongs to exactly one list: its classification may change
    // between advertisements (e.g. name or services arrive later). The
    // connected/connecting device stays where it is.
    const bool activeTrainer = m_trainer && m_trainer->id() == id;
    const bool activeHr = m_hr && m_hr->id() == id;
    const bool activeRower = m_rower && m_rower->id() == id;
    const bool activeController = controllerFor(id) != nullptr;
    if (kind != Kind::Controller && m_controllers.contains(id) && !activeController)
        m_controllers.remove(id);
    if (kind != Kind::Trainer && m_trainers.contains(id) && !activeTrainer)
        m_trainers.remove(id);
    if (kind != Kind::HeartRate && m_hrSensors.contains(id) && !activeHr)
        m_hrSensors.remove(id);
    if (kind != Kind::Rower && m_rowers.contains(id) && !activeRower)
        m_rowers.remove(id);
    if (kind != Kind::Unknown && m_others.contains(id))
        m_others.remove(id);

    DeviceEntry e;
    e.id = id;
    e.name = name.isEmpty() ? (info.name().trimmed().isEmpty() ? id : info.name().trimmed()) : name;
    e.rssi = info.rssi();
    if (kind == Kind::Unknown) {
        // Fallback list ("Show all Bluetooth devices"); LE devices only –
        // BlueZ also reports cached classic devices such as headphones.
        if (!(info.coreConfigurations() & QBluetoothDeviceInfo::LowEnergyCoreConfiguration)
            && info.coreConfigurations() != QBluetoothDeviceInfo::UnknownCoreConfiguration)
            return;
        if (activeTrainer || activeHr || activeRower || activeController)
            return;
        e.kind = kKindOther;
        e.protocol = info.serviceUuids().isEmpty()
                         ? QStringLiteral("Bluetooth LE · %1").arg(id)
                         : QStringLiteral("Bluetooth LE · %1 service(s) · %2").arg(info.serviceUuids().size()).arg(id);
        m_others.upsert(e);
        return;
    }
    if (kind == Kind::Trainer) {
        e.protocol = QStringLiteral("Bluetooth FTMS");
        e.kind = kKindTrainer;
        e.known = id == m_knownTrainer;
        m_trainers.upsert(e);
    } else if (kind == Kind::Rower) {
        const bool concept2 = hasConcept2Uuid(info) || classifyName(name) == Kind::Rower;
        const auto sd = ftmsServiceData(info);
        if (activeRower && m_rower->state() == LinkState::Connected)
            e.protocol = m_rower->protocol();
        else if (concept2)
            e.protocol = QStringLiteral("Concept2 PM5 Bluetooth");
        else if (sd && sd->rowerOnly())
            e.protocol = QStringLiteral("Bluetooth FTMS rower");
        else
            e.protocol = QStringLiteral("Rower (detected after connecting)");
        e.kind = kKindRower;
        e.known = id == m_knownRower;
        m_rowers.upsert(e);
    } else if (kind == Kind::Controller) {
        const QByteArray mfr = info.manufacturerData(zwift::kManufacturerId);
        if (activeController && controllerFor(id)->state() == LinkState::Connected)
            e.protocol = controllerFor(id)->protocol();
        else if (!mfr.isEmpty())
            e.protocol = QStringLiteral("%1 · Bluetooth LE").arg(zwift::deviceTypeText(quint8(mfr.at(0))));
        else
            e.protocol = QStringLiteral("Zwift controller · Bluetooth LE");
        e.kind = kKindController;
        e.known = m_knownControllers.contains(id);
        m_controllers.upsert(e);
    } else {
        e.protocol = QStringLiteral("Bluetooth Heart Rate Service (HRS)");
        e.kind = kKindHeartRate;
        e.known = id == m_knownHr;
        m_hrSensors.upsert(e);
    }
    if (isNew || prev != kind)
        addLog(QStringLiteral("Found %1").arg(e.name), true);

    if (m_autoConnect && e.known) {
        // In single-sensor mode never replace another device that is already
        // connected or connecting (avoids flip-flopping between two known devices).
        if (!m_allowMultiple) {
            const bool otherBusy = (m_trainer && m_trainer->id() != id && m_trainer->state() != LinkState::Disconnected)
                                   || (m_hr && m_hr->id() != id && m_hr->state() != LinkState::Disconnected)
                                   || (m_rower && m_rower->id() != id && m_rower->state() != LinkState::Disconnected);
            bool controllerBusy = false;
            for (const auto &c : std::as_const(m_controllerDevs))
                controllerBusy = controllerBusy || (c && c->id() != id && c->state() != LinkState::Disconnected);
            if (otherBusy || controllerBusy)
                return;
        }
        const bool trainerFree = kind == Kind::Trainer && !activeTrainer;
        const bool hrFree = kind == Kind::HeartRate && !activeHr;
        const bool rowerFree = kind == Kind::Rower && !activeRower;
        const bool controllerFree = kind == Kind::Controller && !activeController
                                    && m_controllerDevs.size() < kMaxControllers;
        if (trainerFree || hrFree || rowerFree || controllerFree)
            connectDevice(id);
    }
}

void DeviceManager::recordDiag(const QString &id, const QBluetoothDeviceInfo &info, Kind kind)
{
    const QDateTime now = QDateTime::currentDateTime();
    if (!m_diag.contains(id) && m_diag.size() >= 300)
        return;   // enough for a report
    DiagEntry &d = m_diag[id];
    if (d.reports == 0)
        d.firstSeen = now;
    d.id = id;
    d.lastSeen = now;
    ++d.reports;
    d.name = info.name();
    d.address = info.address().isNull() ? info.deviceUuid().toString() : info.address().toString();
    d.rssi = info.rssi();
    d.uuids.clear();
    for (const QBluetoothUuid &u : info.serviceUuids())
        d.uuids << uuidText(u);
    d.manufacturerIds.clear();
    for (quint16 k : info.manufacturerIds()) {
        const QByteArray data = info.manufacturerData(k);
        d.manufacturerIds << (data.isEmpty() ? QStringLiteral("0x%1").arg(k, 4, 16, QLatin1Char('0'))
                                             : QStringLiteral("0x%1=%2").arg(k, 4, 16, QLatin1Char('0'))
                                                   .arg(QString::fromLatin1(data.left(16).toHex(' '))));
    }
    d.coreConfig = coreConfigText(info.coreConfigurations());
    d.ftmsServiceData = ftmsServiceDataText(info);
    d.serviceDataIds.clear();
    for (const QBluetoothUuid &u : info.serviceIds())
        d.serviceDataIds << QStringLiteral("%1=%2").arg(uuidText(u), QString::fromLatin1(info.serviceData(u).toHex(' ')));
    d.kind = kind;
}

void DeviceManager::addDiag(const QString &text)
{
    m_diagEvents.append(QStringLiteral("%1  %2").arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")), text));
    while (m_diagEvents.size() > 300)
        m_diagEvents.removeFirst();
}

QString DeviceManager::diagnosticsReport() const
{
    QString r;
    QTextStream out(&r);
    out << "Artilody Watts Trainer – Bluetooth diagnostics\n";
    out << "Generated: " << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n";
    out << "App: " << QCoreApplication::applicationVersion() << "  Qt: " << qVersion() << " (built with " << QT_VERSION_STR << ")\n";
    out << "OS: " << QSysInfo::prettyProductName() << "  kernel " << QSysInfo::kernelVersion() << "  " << QSysInfo::currentCpuArchitecture() << "\n";
    out << "\n[Adapter]\n";
    if (m_local && m_local->isValid()) {
        out << "Selected: " << m_local->name() << "  " << m_local->address().toString() << "  host mode: " << hostModeText(m_local->hostMode()) << "\n";
    } else {
        out << "Selected: none / invalid\n";
    }
    if (m_local) {   // adapter not queried yet (e.g. unit tests): do not touch the OS
        const auto hosts = QBluetoothLocalDevice::allDevices();
        out << "Adapters found: " << hosts.size() << "\n";
        for (const QBluetoothHostInfo &h : hosts)
            out << "  - " << h.name() << "  " << h.address().toString() << "\n";
    }
    out << "Supported discovery: "
        << ((QBluetoothDeviceDiscoveryAgent::supportedDiscoveryMethods() & QBluetoothDeviceDiscoveryAgent::LowEnergyMethod) ? "LE " : "")
        << ((QBluetoothDeviceDiscoveryAgent::supportedDiscoveryMethods() & QBluetoothDeviceDiscoveryAgent::ClassicMethod) ? "Classic" : "") << "\n";
    out << "\n[Scan]\n";
    out << "Scanning: " << (scanning() ? "yes" : "no") << "  last scan: " << lastScanText() << "  status: " << m_scanStatus << "\n";
    if (!m_scanError.isEmpty())
        out << "Problem: " << m_scanError << "\n";
    if (m_agent && m_agent->error() != QBluetoothDeviceDiscoveryAgent::NoError)
        out << "Agent error: " << int(m_agent->error()) << " " << m_agent->errorString() << "\n";
    out << "Known rower: " << (m_knownRower.isEmpty() ? QStringLiteral("-") : m_knownRower)
        << "  known trainer: " << (m_knownTrainer.isEmpty() ? QStringLiteral("-") : m_knownTrainer)
        << "  known HR: " << (m_knownHr.isEmpty() ? QStringLiteral("-") : m_knownHr)
        << "  known controllers: " << (m_knownControllers.isEmpty() ? QStringLiteral("-") : m_knownControllers.join(QLatin1Char(','))) << "\n";
    out << "Auto-connect: " << (m_autoConnect ? "on" : "off") << "  multiple sensors: " << (m_allowMultiple ? "on" : "off")
        << "  virtual devices: " << (m_simulatorEnabled ? "on" : "off") << "\n";
    if (m_rower)
        out << "Active rower: " << m_rower->name() << " " << m_rower->id() << " state " << int(m_rower->state()) << "\n";
    out << "\n[Controllers]\n";
    if (m_controllerDevs.isEmpty())
        out << "No controller selected\n";
    for (const auto &c : std::as_const(m_controllerDevs)) {
        if (!c)
            continue;
        out << c->name() << " " << c->id() << "  state " << int(c->state()) << "  " << c->protocol()
            << "  battery " << (c->batteryLevel() >= 0 ? QStringLiteral("%1 %").arg(c->batteryLevel()) : QStringLiteral("?"));
        if (auto *z = qobject_cast<ZwiftClick *>(c.data()))
            out << "  type " << (z->deviceType() >= 0 ? zwift::deviceTypeText(z->deviceType()) : QStringLiteral("?"))
                << "  handshake " << (z->handshakeDone() ? "ok" : "pending") << (z->isV2() ? "  v2" : "");
        out << "\n";
    }
    out << "\n[Rower data]\n";
    if (m_rower) {
        out << m_rower->dataDiagnostics() << "\n";
    } else {
        out << "No rower selected\n";
    }

    out << "\n[Devices seen: " << m_diag.size() << "]\n";
    QList<DiagEntry> list = m_diag.values();
    std::sort(list.begin(), list.end(), [](const DiagEntry &a, const DiagEntry &b) {
        if ((a.kind == Kind::Rower) != (b.kind == Kind::Rower))
            return a.kind == Kind::Rower;
        return a.rssi > b.rssi;
    });
    for (const DiagEntry &d : list) {
        out << "- " << (d.name.isEmpty() ? QStringLiteral("<no name>") : d.name) << "  [" << d.address << "]\n";
        out << "    class: " << kindName(d.kind) << (m_forcedRowers.contains(d.id) ? " (remembered rower)" : "")
            << "  rssi: " << d.rssi << " dBm  core: " << d.coreConfig << "  reports: " << d.reports
            << "  seen: " << d.firstSeen.toString(QStringLiteral("HH:mm:ss")) << "-" << d.lastSeen.toString(QStringLiteral("HH:mm:ss")) << "\n";
        out << "    services: " << (d.uuids.isEmpty() ? QStringLiteral("-") : d.uuids.join(QStringLiteral(", "))) << "\n";
        if (!d.manufacturerIds.isEmpty())
            out << "    manufacturer data: " << d.manufacturerIds.join(QStringLiteral(", ")) << "\n";
        if (!d.serviceDataIds.isEmpty())
            out << "    service data: " << d.serviceDataIds.join(QStringLiteral(", ")) << "\n";
        if (!d.ftmsServiceData.isEmpty())
            out << "    FTMS advertisement: " << d.ftmsServiceData << "\n";
        const auto g = m_gatt.constFind(d.id);
        if (g != m_gatt.constEnd()) {
            out << "    GATT (" << g->first.toString(QStringLiteral("HH:mm:ss")) << "):\n";
            for (const QString &line : g->second.split(QLatin1Char('\n')))
                out << "      " << line << "\n";
        }
    }
    // GATT results of devices that are not in the scan list (e.g. cleared)
    for (auto it = m_gatt.cbegin(); it != m_gatt.cend(); ++it) {
        if (m_diag.contains(it.key()))
            continue;
        out << "- GATT of " << it.key() << " (" << it->first.toString(QStringLiteral("HH:mm:ss")) << "):\n";
        for (const QString &line : it->second.split(QLatin1Char('\n')))
            out << "      " << line << "\n";
    }
    out << "\n[Events]\n";
    for (const QString &e : m_diagEvents)
        out << e << "\n";
    out << "\n[Connection log]\n";
    for (int i = m_log.size() - 1; i >= 0; --i) {
        const QVariantMap e = m_log.at(i).toMap();
        out << e.value(QStringLiteral("time")).toString() << "  " << (e.value(QStringLiteral("ok")).toBool() ? "" : "! ")
            << e.value(QStringLiteral("text")).toString() << "\n";
    }
    return r;
}

void DeviceManager::copyToClipboard(const QString &text) const
{
    if (auto *app = qobject_cast<QGuiApplication *>(QCoreApplication::instance()))
        app->clipboard()->setText(text);
}

void DeviceManager::setShowAllDevices(bool on)
{
    if (m_showAll == on)
        return;
    m_showAll = on;
    m_db->setSetting(QStringLiteral("ble/showAll"), on);
    emit preferencesChanged();
}

void DeviceManager::connectAsRower(const QString &id)
{
    if (!m_infos.contains(id)) {
        setScanError(QStringLiteral("This device is no longer available – scan again."));
        return;
    }
    QString name = id;
    if (const DeviceEntry *e = m_others.find(id))
        name = e->name;
    else if (const DeviceEntry *t = m_trainers.find(id))
        name = t->name;
    addLog(QStringLiteral("Connecting %1 as rower (manual)").arg(name), true);
    addDiag(QStringLiteral("Connect as rower: %1").arg(id));
    moveToRowers(id);
    connectDevice(id);
}

void DeviceManager::moveToRowers(const QString &id)
{
    // A trainer connection to the same device must go first.
    if (m_trainer && m_trainer->id() == id)
        releaseTrainer();
    if (m_knownTrainer == id) {
        m_knownTrainer.clear();
        m_db->setSetting(QStringLiteral("ble/knownTrainer"), QString());
    }
    rememberRower(id, true);
    m_others.remove(id);
    m_trainers.remove(id);
    if (m_infos.contains(id))
        handleDeviceInfo(m_infos.value(id), true);   // re-lists it under rowers
    emit devicesChanged();
}

void DeviceManager::rememberRower(const QString &id, bool on)
{
    const bool changed = on ? !m_forcedRowers.contains(id) : m_forcedRowers.remove(id);
    if (on)
        m_forcedRowers.insert(id);
    if (!changed)
        return;
    QStringList list(m_forcedRowers.cbegin(), m_forcedRowers.cend());
    list.sort();
    m_db->setSetting(QStringLiteral("ble/rowerIds"), list.join(QLatin1Char(';')));
}

void DeviceManager::onTrainerIsRower(const QString &id)
{
    if (!m_trainer || m_trainer->id() != id)
        return;
    addLog(QStringLiteral("%1 is a rowing machine – moved to Rowers").arg(m_trainer->name()), true);
    addDiag(QStringLiteral("%1: trainer connection found rowing services, switching to rower").arg(id));
    qCInfo(lcBle) << "Trainer" << id << "exposes rowing services - switching to the rower driver";
    moveToRowers(id);
    // Give the peripheral a moment after the trainer link was closed.
    QTimer::singleShot(1500, this, [this, id] {
        if (!(m_rower && m_rower->id() == id))
            connectDevice(id);
    });
}

void DeviceManager::recordGatt(const QString &id, const QString &summary)
{
    m_gatt[id] = qMakePair(QDateTime::currentDateTime(), summary);
    addDiag(QStringLiteral("GATT %1: %2").arg(id, summary.section(QLatin1Char('\n'), 0, 1).replace(QLatin1Char('\n'), QStringLiteral(" | "))));
    qCInfo(lcBle).noquote() << "GATT" << id << summary;
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
        releaseControllers(id);
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
    else if (controllerFor(id))
        kind = kKindController;
    else if (m_controllers.contains(id))
        kind = kKindController;
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
            auto *pm = new Pm5Rower(m_infos.value(id), adapter, this);
            connect(pm, &Pm5Rower::rowingServiceMissing, this, [this, id, name = pm->name()] {
                rememberRower(id, false);
                setScanError(QStringLiteral("%1 offers neither the Concept2 rowing service (CE060030) nor FTMS rower "
                                            "data. It is not a rowing monitor, or the PM5 is busy with another app.").arg(name));
                addDiag(QStringLiteral("%1: no rowing service (0x0030 / FTMS 0x2AD1) found").arg(id));
            });
            connect(pm, &Pm5Rower::protocolChanged, this, [this, pm] {
                if (const DeviceEntry *e = m_rowers.find(pm->id())) {
                    DeviceEntry copy = *e;
                    copy.protocol = pm->protocol();
                    m_rowers.upsert(copy);
                }
                emit devicesChanged();
            });
            r = pm;
        }
        if (!r)
            return;
        attachRower(r);
        m_knownRower = id;
        m_db->setSetting(QStringLiteral("ble/knownRower"), id);
        r->connectDevice();
    } else if (kind == kKindController) {
        if (ControllerDevice *c = controllerFor(id)) {
            if (c->state() == LinkState::Disconnected)
                c->connectDevice();
            return;
        }
        if (m_controllerDevs.size() >= kMaxControllers) {
            // make room: drop a controller that is not connected, else refuse
            QString drop;
            for (const auto &c : std::as_const(m_controllerDevs)) {
                if (c && c->state() != LinkState::Connected) {
                    drop = c->id();
                    break;
                }
            }
            if (drop.isEmpty()) {
                addLog(QStringLiteral("Already %1 controllers connected – disconnect one first").arg(kMaxControllers), false);
                return;
            }
            releaseController(drop);
        }
        if (!m_infos.contains(id))
            return;
        auto *c = new ZwiftClick(m_infos.value(id), adapter, this);
        attachController(c);
        if (!m_knownControllers.contains(id)) {
            m_knownControllers.append(id);
            saveKnownControllers();
        }
        c->connectDevice();
    }
    // Real BLE device: keep BlueZ's device object alive while connecting.
    if (!id.startsWith(QLatin1String("sim:")) && !kind.isEmpty())
        ensureScanning();
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
    } else if (controllerFor(id)) {
        releaseController(id);
        m_knownControllers.removeAll(id);
        saveKnownControllers();
    }
    emit devicesChanged();
}

// --- controllers -------------------------------------------------------------

ControllerDevice *DeviceManager::controllerFor(const QString &id) const
{
    for (const auto &c : m_controllerDevs) {
        if (c && c->id() == id)
            return c.data();
    }
    return nullptr;
}

QList<ControllerDevice *> DeviceManager::controllerDevices() const
{
    QList<ControllerDevice *> out;
    for (const auto &c : m_controllerDevs) {
        if (c)
            out << c.data();
    }
    return out;
}

ControllerDevice *DeviceManager::primaryController() const
{
    ControllerDevice *fallback = nullptr;
    for (const auto &c : m_controllerDevs) {
        if (!c)
            continue;
        if (c->state() == LinkState::Connected)
            return c.data();
        if (!fallback)
            fallback = c.data();
    }
    return fallback;
}

void DeviceManager::attachController(ControllerDevice *c)
{
    m_controllerDevs.append(c);
    c->setAutoReconnect(m_autoReconnect);
    m_controllers.setState(c->id(), int(LinkState::Connecting));
    connect(c, &ControllerDevice::shiftRequested, this, [this](int dir) {
        emit controllerShift(dir);
        if (dir > 0)
            emit controllerShiftUp();
        else if (dir < 0)
            emit controllerShiftDown();
    });
    connect(c, &ControllerDevice::buttonsChanged, this, &DeviceManager::controllerButtonsChanged);
    connect(c, &ControllerDevice::batteryChanged, this, &DeviceManager::devicesChanged);
    connect(c, &ControllerDevice::logMessage, this, &DeviceManager::addLog);
    connect(c, &ControllerDevice::gattReport, this, [this, id = c->id()](const QString &g) { recordGatt(id, g); });
    connect(c, &ControllerDevice::rssiChanged, this, [this, c](int rssi) { m_controllers.setRssi(c->id(), rssi); });
    connect(c, &ControllerDevice::stateChanged, this, [this, c](LinkState s) {
        m_controllers.setState(c->id(), int(s));
        if (s == LinkState::Connected) {
            if (const DeviceEntry *e = m_controllers.find(c->id())) {
                DeviceEntry copy = *e;
                copy.protocol = c->protocol();
                m_controllers.upsert(copy);
            }
        }
        emit devicesChanged();
    });
    emit devicesChanged();
}

void DeviceManager::releaseController(const QString &id)
{
    for (int i = 0; i < m_controllerDevs.size(); ++i) {
        ControllerDevice *c = m_controllerDevs.at(i);
        if (c && c->id() != id)
            continue;
        m_controllerDevs.removeAt(i);
        if (c) {
            m_controllers.setState(c->id(), int(LinkState::Disconnected));
            c->disconnect(this);
            c->disconnectDevice();
            c->deleteLater();
        }
        emit controllerButtonsChanged(false, false);
        emit devicesChanged();
        return;
    }
}

void DeviceManager::releaseControllers(const QString &exceptId)
{
    const QList<ControllerDevice *> list = controllerDevices();
    for (ControllerDevice *c : list) {
        if (c->id() != exceptId)
            releaseController(c->id());
    }
}

void DeviceManager::saveKnownControllers()
{
    m_db->setSetting(QStringLiteral("ble/knownControllers"), m_knownControllers.join(QLatin1Char(';')));
}

QString DeviceManager::controllerId() const
{
    const ControllerDevice *c = primaryController();
    return c ? c->id() : QString();
}

QString DeviceManager::controllerName() const
{
    QStringList names;
    for (const auto &c : m_controllerDevs) {
        if (c && c->state() == LinkState::Connected)
            names << c->name();
    }
    if (!names.isEmpty())
        return names.join(QStringLiteral(" + "));
    const ControllerDevice *c = primaryController();
    return c ? c->name() : QString();
}

QString DeviceManager::controllerProtocol() const
{
    const ControllerDevice *c = primaryController();
    return c ? c->protocol() : QString();
}

int DeviceManager::controllerState() const
{
    const ControllerDevice *c = primaryController();
    return c ? int(c->state()) : 0;
}

bool DeviceManager::controllerConnected() const
{
    return controllerCount() > 0;
}

int DeviceManager::controllerCount() const
{
    int n = 0;
    for (const auto &c : m_controllerDevs) {
        if (c && c->state() == LinkState::Connected)
            ++n;
    }
    return n;
}

int DeviceManager::controllerBattery() const
{
    int level = -1;
    for (const auto &c : m_controllerDevs) {
        if (c && c->state() == LinkState::Connected && c->batteryLevel() >= 0)
            level = level < 0 ? c->batteryLevel() : std::min(level, c->batteryLevel());
    }
    return level;
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
    connect(t, &TrainerDevice::gattReport, this, [this, id = t->id()](const QString &g) { recordGatt(id, g); });
    connect(t, &TrainerDevice::rowerDetected, this, [this, id = t->id()] {
        // Not inside the trainer's own signal: it is deleted on the switch.
        QTimer::singleShot(0, this, [this, id] { onTrainerIsRower(id); });
    });
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
    connect(h, &HeartRateDevice::gattReport, this, [this, id = h->id()](const QString &g) { recordGatt(id, g); });
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
    connect(r, &RowerDevice::gattReport, this, [this, id = r->id()](const QString &g) { recordGatt(id, g); });
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
    return n + controllerCount();
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
    if (controllerConnected())
        parts << QStringLiteral("Controller");
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
    for (const auto &c : std::as_const(m_controllerDevs)) {
        if (c)
            c->setAutoReconnect(on);
    }
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
