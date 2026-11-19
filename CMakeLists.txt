cmake_minimum_required(VERSION 3.25)
project(bacuri-vms VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BACURI_VMS_BUILD_RTSP_FIXTURE "Build the test RTSP camera" OFF)
include(CTest)

add_library(bacuri-vms-domain STATIC
    src/domain/archive.cpp
    src/domain/resources.cpp
    src/domain/time.cpp
)
target_include_directories(bacuri-vms-domain PUBLIC src)
target_compile_options(bacuri-vms-domain PRIVATE -Wall -Wextra -Wpedantic)

add_executable(bacuri-vms-core src/core/main.cpp)
target_link_libraries(bacuri-vms-core PRIVATE bacuri-vms-domain)
target_compile_definitions(bacuri-vms-core PRIVATE BACURI_VMS_VERSION="${PROJECT_VERSION}")
target_compile_options(bacuri-vms-core PRIVATE -Wall -Wextra -Wpedantic)

if(BUILD_TESTING)
    find_package(GTest CONFIG REQUIRED)
    include(GoogleTest)
    add_test(NAME core-version COMMAND bacuri-vms-core --version)
    add_executable(bacuri-vms-domain-test tests/domain/model.cpp)
    target_link_libraries(bacuri-vms-domain-test PRIVATE bacuri-vms-domain GTest::gmock_main)
    target_compile_options(bacuri-vms-domain-test PRIVATE -Wall -Wextra -Wpedantic)
    gtest_discover_tests(bacuri-vms-domain-test DISCOVERY_MODE PRE_TEST)
endif()

if(BACURI_VMS_BUILD_RTSP_FIXTURE)
    add_subdirectory(tests/fixtures)
endif()
