cmake_minimum_required(VERSION 3.25)
project(bacuri-desktop LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion)

include(CTest)
find_package(Qt6 6.8 REQUIRED COMPONENTS Gui Qml Quick)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED IMPORTED_TARGET gstreamer-1.0>=1.26 gstreamer-video-1.0)
qt_standard_project_setup(REQUIRES 6.8)

# Measurement helpers without Qt or GStreamer, tested on their own.
add_library(bacuri-desktop-measure STATIC src/prototype/measure.cpp)
target_include_directories(bacuri-desktop-measure PUBLIC src)

qt_add_executable(bacuri-desktop-prototype
    src/prototype/main.cpp
    src/prototype/video_cell.cpp
)
qt_add_qml_module(bacuri-desktop-prototype
    URI Bacuri.Prototype
    VERSION 1.0
    QML_FILES src/prototype/Main.qml
    NO_RESOURCE_TARGET_PATH
    NO_IMPORT_SCAN
)
target_link_libraries(bacuri-desktop-prototype PRIVATE
    bacuri-desktop-measure Qt6::Gui Qt6::Qml Qt6::Quick PkgConfig::GST)

if(BUILD_TESTING)
    find_package(GTest REQUIRED)
    add_executable(bacuri-desktop-tests tests/measure_test.cpp)
    target_link_libraries(bacuri-desktop-tests PRIVATE bacuri-desktop-measure GTest::gtest_main)
    include(GoogleTest)
    gtest_discover_tests(bacuri-desktop-tests)
endif()
