qt_add_library(watt_core STATIC
    Interval.h Interval.cpp
    Workout.h Workout.cpp
    Zones.h Zones.cpp
    Profile.h Profile.cpp
    Ride.h
    Track.h
    Database.h Database.cpp
    RideAnalysis.h RideAnalysis.cpp
    CsvExporter.h CsvExporter.cpp
    FitExporter.h FitExporter.cpp
    WorkoutEngine.h WorkoutEngine.cpp
    PowerSmoother.h PowerSmoother.cpp
    SensorHub.h SensorHub.cpp
    TimeFormat.h TimeFormat.cpp
    ProfileManager.h ProfileManager.cpp
    WorkoutLibrary.h WorkoutLibrary.cpp
    WorkoutIO.h WorkoutIO.cpp
    BuilderBackend.h
    WorkoutBuilder.h WorkoutBuilder.cpp
    AppBuilderBackend.h AppBuilderBackend.cpp
    TrainingSession.h TrainingSession.cpp
    HistoryManager.h HistoryManager.cpp
    PlanTypes.h
    WorkoutPlanner.h WorkoutPlanner.cpp
    RowingRecords.h RowingRecords.cpp
    Gearing.h Gearing.cpp
    FreeRideSession.h FreeRideSession.cpp
)

target_include_directories(watt_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(watt_core PUBLIC Qt6::Core Qt6::Sql)

# Rowing session logic: needs the domain model above plus the RowerDevice
# interface from src/ble (watt_ble links watt_core, so it is a separate library).
qt_add_library(watt_rowing STATIC
    RowingSession.h RowingSession.cpp
)
target_include_directories(watt_rowing PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(watt_rowing PUBLIC watt_core watt_ble)
