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

find_package(PkgConfig REQUIRED)
find_package(Protobuf REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(PostgreSQL REQUIRED)
find_package(nlohmann_json REQUIRED)
pkg_check_modules(GRPC REQUIRED IMPORTED_TARGET grpc++)
find_package(Boost 1.83 CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
pkg_check_modules(SODIUM REQUIRED IMPORTED_TARGET libsodium)
pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0)
find_program(GRPC_CPP_PLUGIN grpc_cpp_plugin REQUIRED)

set(proto_dir "${CMAKE_CURRENT_SOURCE_DIR}/contracts/v1")
set(generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${generated_dir}")
add_custom_command(
    OUTPUT "${generated_dir}/control.pb.cc" "${generated_dir}/control.pb.h"
           "${generated_dir}/control.grpc.pb.cc" "${generated_dir}/control.grpc.pb.h"
    COMMAND protobuf::protoc -I "${proto_dir}" --cpp_out=${generated_dir}
        --grpc_out=${generated_dir} --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN}
        "${proto_dir}/control.proto"
    DEPENDS "${proto_dir}/control.proto"
    VERBATIM
)
add_library(bacuri-vms-rpc STATIC "${generated_dir}/control.pb.cc" "${generated_dir}/control.grpc.pb.cc")
target_include_directories(bacuri-vms-rpc PUBLIC "${generated_dir}")
target_link_libraries(bacuri-vms-rpc PUBLIC protobuf::libprotobuf PkgConfig::GRPC)

add_library(bacuri-vms-control-support STATIC src/control/support.cpp)
target_link_libraries(bacuri-vms-control-support PUBLIC bacuri-vms-domain PkgConfig::SODIUM PkgConfig::GIO)
add_executable(bacuri-vms-recorder src/recorder/main.cpp)
target_link_libraries(bacuri-vms-recorder PRIVATE bacuri-vms-control-support bacuri-vms-rpc SQLite::SQLite3 Boost::headers)
add_executable(bacuri-vms-core src/core/main.cpp src/control/database.cpp src/control/registry.cpp
    src/control/session.cpp src/control/server.cpp src/control/https.cpp src/control/delivery.cpp)
target_link_libraries(bacuri-vms-core PRIVATE bacuri-vms-control-support bacuri-vms-rpc
    PostgreSQL::PostgreSQL Boost::headers OpenSSL::SSL PkgConfig::GIO nlohmann_json::nlohmann_json)
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
    add_executable(bacuri-vms-control-test tests/control/support.cpp)
    target_link_libraries(bacuri-vms-control-test PRIVATE bacuri-vms-control-support GTest::gtest_main)
    gtest_discover_tests(bacuri-vms-control-test DISCOVERY_MODE PRE_TEST)
endif()

if(BACURI_VMS_BUILD_RTSP_FIXTURE)
    add_subdirectory(tests/fixtures)
endif()
