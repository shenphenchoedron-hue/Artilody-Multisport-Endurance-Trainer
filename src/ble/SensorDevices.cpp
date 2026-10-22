#include "SensorDevices.h"

namespace watt {

void SensorDevice::setState(LinkState s)
{
    if (m_state == s)
        return;
    m_state = s;
    emit stateChanged(s);
}

void SensorDevice::setRssi(int rssi)
{
    if (m_rssi == rssi)
        return;
    m_rssi = rssi;
    emit rssiChanged(rssi);
}

} // namespace watt
