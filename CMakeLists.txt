cmake_minimum_required(VERSION 3.20)
project(agora-desktop VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(PkgConfig REQUIRED)

# Qt6 (with Qt5 fallback)
find_package(Qt6 COMPONENTS Widgets QUIET)
if(NOT Qt6_FOUND)
    find_package(Qt5 5.15 REQUIRED COMPONENTS Widgets)
    set(QT_LIBS Qt5::Widgets)
    set(QT_INCLUDE_DIRS "")
else()
    set(QT_LIBS Qt6::Widgets)
    set(QT_INCLUDE_DIRS "")
endif()

# SQLite3
find_package(SQLite3 REQUIRED)

# libcurl
pkg_check_modules(CURL REQUIRED libcurl)

# PipeWire (optional)
pkg_check_modules(PIPEWIRE libpipewire-0.3)
if(PIPEWIRE_FOUND)
    add_definitions(-DHAVE_PIPEWIRE)
endif()

# ALSA (optional)
pkg_check_modules(ALSA alsa)
if(ALSA_FOUND)
    add_definitions(-DHAVE_ALSA)
endif()

# PortAudio (optional)
pkg_check_modules(PORTAUDIO portaudio-2.0)
if(PORTAUDIO_FOUND)
    add_definitions(-DHAVE_PORTAUDIO)
endif()

# OpenSSL
find_package(OpenSSL REQUIRED)

# nlohmann_json - bundled
set(NLOHMANN_JSON_DIR "${CMAKE_SOURCE_DIR}/src/thirdparty/json")

set(SOURCES
    src/main.cpp
    src/db/database.cpp
    src/models/message.cpp
    src/models/conversation.cpp
    src/api/http_client.cpp
    src/api/provider.cpp
    src/audio/recorder.cpp
    src/utils/config.cpp
    src/utils/import_export.cpp
    src/ui/main_window_qt.cpp
    src/ui/theme.cpp
)

set(HEADERS
    src/ui/main_window_qt.hpp
    src/db/database.hpp
    src/models/message.hpp
    src/models/conversation.hpp
    src/api/http_client.hpp
    src/api/provider.hpp
    src/audio/recorder.hpp
    src/utils/config.hpp
)

add_executable(agora ${SOURCES} ${HEADERS})

target_include_directories(agora PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${NLOHMANN_JSON_DIR}
    ${CURL_INCLUDE_DIRS}
    ${PIPEWIRE_INCLUDE_DIRS}
    ${ALSA_INCLUDE_DIRS}
    ${PORTAUDIO_INCLUDE_DIRS}
    ${SQLite3_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
)

target_link_libraries(agora PRIVATE
    ${QT_LIBS}
    SQLite::SQLite3
    ${CURL_LIBRARIES}
    ${PIPEWIRE_LIBRARIES}
    ${ALSA_LIBRARIES}
    ${PORTAUDIO_LIBRARIES}
    OpenSSL::Crypto
)

target_compile_options(agora PRIVATE -Wall -Wextra)
install(TARGETS agora RUNTIME DESTINATION bin)
