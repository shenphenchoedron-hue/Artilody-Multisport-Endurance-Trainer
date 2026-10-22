#pragma once

#include <QObject>
#include <QStringList>

namespace watt {

enum class LinkState { Disconnected = 0, Connecting = 1, Connected = 2 };

// Common interface for every connectable sensor (BLE or simulated).
class SensorDevice : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;

    virtual QString id() const = 0;
    virtual QString name() const = 0;
    virtual QString protocol() const = 0;   // e.g. "Bluetooth FTMS"
    virtual bool isSimulated() const { return false; }
    virtual void connectDevice() = 0;
    virtual void disconnectDevice() = 0;

    LinkState state() const { return m_state; }
    int rssi() const { return m_rssi; }
    virtual void setAutoReconnect(bool on) { m_autoReconnect = on; }
    bool autoReconnect() const { return m_autoReconnect; }

signals:
    void stateChanged(watt::LinkState state);
    void rssiChanged(int rssi);
    void logMessage(const QString &text, bool ok);

protected:
    void setState(LinkState s);
    void setRssi(int rssi);

    bool m_autoReconnect = true;

private:
    LinkState m_state = LinkState::Disconnected;
    int m_rssi = 0;
};

// Smart trainer: measures power/cadence and can be controlled (ERG).
class TrainerDevice : public SensorDevice
{
    Q_OBJECT
public:
    using SensorDevice::SensorDevice;

    virtual void setTargetPower(int watts) = 0;
    virtual void setFreeRide() = 0;          // release ERG, flat road simulation
    virtual QStringList capabilities() const = 0;
    virtual bool supportsControl() const = 0;

signals:
    void powerMeasured(int watts);
    void cadenceMeasured(double rpm);
    void speedMeasured(double kmh);
    void heartRateMeasured(int bpm);         // some trainers forward HR
    void capabilitiesChanged();
};

class HeartRateDevice : public SensorDevice
{
    Q_OBJECT
public:
    using SensorDevice::SensorDevice;

signals:
    void heartRateMeasured(int bpm);
};

} // namespace watt

Q_DECLARE_METATYPE(watt::LinkState)
