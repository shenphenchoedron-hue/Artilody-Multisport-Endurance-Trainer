qt_add_library(watt_media STATIC
    TrackListModel.h TrackListModel.cpp
    MusicPlayer.h MusicPlayer.cpp
)
target_include_directories(watt_media PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(watt_media PUBLIC watt_core Qt6::Multimedia Qt6::Gui)
