cmake_minimum_required(VERSION 3.25)
project(bacuri-vms VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BACURI_VMS_BUILD_RTSP_FIXTURE "Build the test RTSP camera" OFF)
include(CTest)

add_executable(bacuri-vms-core src/core/main.cpp)
target_compile_definitions(bacuri-vms-core PRIVATE BACURI_VMS_VERSION="${PROJECT_VERSION}")
target_compile_options(bacuri-vms-core PRIVATE -Wall -Wextra -Wpedantic)

if(BUILD_TESTING)
    add_test(NAME core-version COMMAND bacuri-vms-core --version)
endif()

if(BACURI_VMS_BUILD_RTSP_FIXTURE)
    add_subdirectory(tests/fixtures)
endif()
