cmake_minimum_required(VERSION 3.25)
project(bacuri-vms-client LANGUAGES CXX)

# Bacuri VMS Client: the desktop workstation of Bacuri VMS. Each module is a library with its own
# directory under src and depends only on the modules listed for it. See ADR-023.

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion)

include(CTest)
find_package(Qt6 6.8 REQUIRED COMPONENTS Gui Network OpenGL Qml Quick)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED IMPORTED_TARGET gstreamer-1.0>=1.26 gstreamer-video-1.0)
find_package(QMapLibre 3.0 REQUIRED COMPONENTS Core)
qt_standard_project_setup(REQUIRES 6.8)

# Diagnostics: measurement of frame times and frame paths, without Qt or GStreamer.
add_library(bacuri-vms-client-diagnostics STATIC src/diagnostics/frame_stats.cpp)
target_include_directories(bacuri-vms-client-diagnostics PUBLIC src)

# Site: sign-in through the system browser, the desktop session and the gateway for resources of the site.
add_library(bacuri-vms-client-site STATIC src/site/help.cpp src/site/pkce.cpp src/site/sign_in.cpp src/site/site_proxy.cpp)
set_target_properties(bacuri-vms-client-site PROPERTIES AUTOMOC ON)
target_include_directories(bacuri-vms-client-site PUBLIC src)
target_link_libraries(bacuri-vms-client-site PUBLIC Qt6::Core Qt6::Gui Qt6::Network)

# Media: video streams from the decoder into Qt Quick on a common timeline, the choice of the hardware
# decoder, the software mode of a station without a GPU and the fallback to software decoding.
add_library(bacuri-vms-client-media STATIC
    src/media/graphics_probe.cpp src/media/stream_group.cpp src/media/video_stream.cpp)
set_target_properties(bacuri-vms-client-media PROPERTIES AUTOMOC ON)
target_include_directories(bacuri-vms-client-media PUBLIC src)
target_link_libraries(bacuri-vms-client-media
    PUBLIC Qt6::Core Qt6::Gui Qt6::Quick PkgConfig::GST
    PRIVATE bacuri-vms-client-diagnostics)

# Map: the MapLibre map as a Qt Quick item, published as the QML module Bacuri.Vms.Client.Map.
qt_add_library(bacuri-vms-client-map STATIC)
qt_add_qml_module(bacuri-vms-client-map
    URI Bacuri.Vms.Client.Map
    VERSION 1.0
    SOURCES src/map/map_view.cpp src/map/map_view.hpp
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/qml/Bacuri/Vms/Client/Map
    NO_IMPORT_SCAN
)
# The QML type registration includes the headers of the module by their file names.
target_include_directories(bacuri-vms-client-map PUBLIC src PRIVATE src/map)
target_link_libraries(bacuri-vms-client-map PUBLIC Qt6::OpenGL Qt6::Quick QMapLibre::Core)

# Video and map bench: measures the media and map paths on a station.
qt_add_executable(bacuri-vms-client-bench tools/bench/main.cpp)
qt_add_qml_module(bacuri-vms-client-bench
    URI Bacuri.Vms.Client.Bench
    VERSION 1.0
    QML_FILES tools/bench/Bench.qml
    NO_RESOURCE_TARGET_PATH
    NO_IMPORT_SCAN
)
target_link_libraries(bacuri-vms-client-bench PRIVATE
    bacuri-vms-client-diagnostics bacuri-vms-client-site bacuri-vms-client-media bacuri-vms-client-mapplugin
    Qt6::Gui Qt6::Qml Qt6::Quick)

# Sign-in check: signs in through the browser, reads the session and signs out.
qt_add_executable(bacuri-vms-client-signin tools/signin/main.cpp)
target_link_libraries(bacuri-vms-client-signin PRIVATE bacuri-vms-client-site)

if(BUILD_TESTING)
    find_package(GTest REQUIRED)
    add_executable(bacuri-vms-client-tests tests/frame_stats_test.cpp tests/sign_in_test.cpp)
    target_link_libraries(bacuri-vms-client-tests PRIVATE
        bacuri-vms-client-diagnostics bacuri-vms-client-site GTest::gtest_main)
    include(GoogleTest)
    gtest_discover_tests(bacuri-vms-client-tests)
endif()
