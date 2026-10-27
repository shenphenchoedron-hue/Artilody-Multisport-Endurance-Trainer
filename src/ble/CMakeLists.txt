# Pure protocol code (FTMS/HRS/Concept2 PM5 parsing + encoding). Only depends on QtCore so
# it can be unit-tested without Bluetooth hardware.
qt_add_library(watt_bleprotocol STATIC
    FtmsProtocol.h FtmsProtocol.cpp
    HrsProtocol.h HrsProtocol.cpp
    Pm5Protocol.h Pm5Protocol.cpp
    ZwiftClickProtocol.h ZwiftClickProtocol.cpp
)
target_include_directories(watt_bleprotocol PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(watt_bleprotocol PUBLIC Qt6::Core)

qt_add_library(watt_ble STATIC
    SensorDevices.h SensorDevices.cpp
    GattLink.h
    BleLink.h BleLink.cpp
    BleTrainer.h BleTrainer.cpp
    GradientTest.h GradientTest.cpp
    BleHeartRateSensor.h BleHeartRateSensor.cpp
    Pm5Rower.h Pm5Rower.cpp
    ZwiftClick.h ZwiftClick.cpp
    SimulatedDevices.h SimulatedDevices.cpp
    DeviceListModel.h DeviceListModel.cpp
    DeviceManager.h DeviceManager.cpp
)
target_include_directories(watt_ble PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(watt_ble PUBLIC watt_bleprotocol watt_core Qt6::Bluetooth Qt6::Gui)
