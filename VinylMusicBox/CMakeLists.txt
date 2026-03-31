cmake_minimum_required(VERSION 3.16)
project(VinylMusicBox VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Qt Resources for QML
qt_add_resources(vinylmusicbox "qml"
    PREFIX "/"
    FILES
        qml/main.qml
)

find_package(Qt6 REQUIRED COMPONENTS Core QML Quick Network Multimedia Sql)
find_package(PkgConfig REQUIRED)
pkg_check_modules(BLUEZ bluez)
pkg_check_modules(PULSEAUDIO libpulse)

if(BLUEZ_FOUND)
    include_directories(${BLUEZ_INCLUDE_DIRS})
endif()
if(PULSEAUDIO_FOUND)
    include_directories(${PULSEAUDIO_INCLUDE_DIRS})
endif()

add_executable(vinylmusicbox
    src/main.cpp
    src/BluetoothManager.cpp
    src/LyricsFetcher.cpp
    src/LyricsCache.cpp
    include/BluetoothManager.h
    include/LyricsFetcher.h
    include/LyricsCache.h
)

target_link_libraries(vinylmusicbox PRIVATE
    Qt6::Core Qt6::QML Qt6::Quick Qt6::Network Qt6::Multimedia Qt6::Sql
    ${BLUEZ_LIBRARIES} ${PULSEAUDIO_LIBRARIES}
    curl sqlite3 pthread
)

install(TARGETS vinylmusicbox DESTINATION bin)
