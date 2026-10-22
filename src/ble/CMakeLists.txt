# Pure protocol code (FTMS/HRS parsing + encoding). Only depends on QtCore so
# it can be unit-tested without Bluetooth hardware.
qt_add_library(watt_bleprotocol STATIC
    FtmsProtocol.h FtmsProtocol.cpp
    HrsProtocol.h HrsProtocol.cpp
)
target_include_directories(watt_bleprotocol PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(watt_bleprotocol PUBLIC Qt6::Core)

qt_add_library(watt_ble STATIC
    SensorDevices.h SensorDevices.cpp
    BleLink.h BleLink.cpp
    BleTrainer.h BleTrainer.cpp
    BleHeartRateSensor.h BleHeartRateSensor.cpp
    SimulatedDevices.h SimulatedDevices.cpp
    DeviceListModel.h DeviceListModel.cpp
    DeviceManager.h DeviceManager.cpp
)
target_include_directories(watt_ble PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(watt_ble PUBLIC watt_bleprotocol watt_core Qt6::Bluetooth)
