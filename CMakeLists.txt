cmake_minimum_required(VERSION 3.25)
project(bacuri-vms LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# Static archives must have an acyclic link graph.
set_property(GLOBAL PROPERTY GLOBAL_DEPENDS_NO_CYCLES ON)

add_compile_options(
    -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
    "$<$<CONFIG:Release>:-ffile-prefix-map=${CMAKE_SOURCE_DIR}=.>"
    "$<$<CONFIG:Release>:-ffile-prefix-map=${CMAKE_BINARY_DIR}=./build>"
)

option(BACURI_VMS_BUILD_RTSP_FIXTURE "Build the test RTSP camera" OFF)
include(CTest)

find_package(PkgConfig REQUIRED)
find_package(Protobuf REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(PNG REQUIRED)
find_package(LibArchive REQUIRED)
pkg_check_modules(CURL REQUIRED IMPORTED_TARGET libcurl)
find_package(PostgreSQL REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
pkg_check_modules(GRPC REQUIRED IMPORTED_TARGET grpc++)
find_package(Boost 1.83 CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(LibXml2 REQUIRED)
pkg_check_modules(FIDO REQUIRED IMPORTED_TARGET libfido2)
pkg_check_modules(SODIUM REQUIRED IMPORTED_TARGET libsodium)
pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0)
find_program(GRPC_CPP_PLUGIN grpc_cpp_plugin REQUIRED)

set(proto_dir "${CMAKE_CURRENT_SOURCE_DIR}/contracts/v1")
set(generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${generated_dir}")
pkg_check_modules(GST REQUIRED IMPORTED_TARGET gstreamer-app-1.0>=1.22 gstreamer-rtp-1.0 gstreamer-rtsp-1.0 gstreamer-video-1.0)
pkg_check_modules(WEBRTC REQUIRED IMPORTED_TARGET gstreamer-webrtc-1.0>=1.22 gstreamer-sdp-1.0)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_custom_target(bacuri-product-version
    COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/tools/product_version.py" generate "${generated_dir}"
    BYPRODUCTS "${generated_dir}/product_version.hpp" "${generated_dir}/product-version.json"
    VERBATIM
)
function(archive_sql variable path)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${path}")
    file(READ "${path}" contents)
    set("${variable}" "${contents}" PARENT_SCOPE)
endfunction()
archive_sql(archive_initial storage/sqlite/migrations/0001_initial.sql)
archive_sql(archive_upgrade storage/sqlite/migrations/0002_archive.sql)
archive_sql(archive_coverage storage/sqlite/migrations/0003_coverage.sql)
archive_sql(archive_gap_history storage/sqlite/migrations/0004_gap_history.sql)
archive_sql(archive_retention storage/sqlite/migrations/0005_retention.sql)
archive_sql(archive_holds storage/sqlite/migrations/0007_archive_holds.sql)
archive_sql(archive_policies storage/sqlite/migrations/0006_recording_policies.sql)
archive_sql(archive_verification storage/sqlite/migrations/0008_block_verification.sql)
archive_sql(archive_keyframe_gaps storage/sqlite/migrations/0009_keyframe_gaps.sql)
archive_sql(archive_insert storage/postgresql/insert-block.sql)
archive_sql(archive_read storage/postgresql/read-block-manifest.sql)
archive_sql(archive_search storage/postgresql/search-archive.sql)
configure_file(src/archive/sql.hpp.in "${generated_dir}/archive_sql.hpp" @ONLY)
# The error catalog: ErrorCode and the properties of each code for every service.
set(error_catalog_dir "${CMAKE_CURRENT_BINARY_DIR}/error-catalog")
set(error_catalog_header "${error_catalog_dir}/errors/catalog.hpp")
add_custom_command(OUTPUT "${error_catalog_header}"
    COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/tools/errors.py" cpp "${error_catalog_header}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/contracts/v1/errors.yaml" "${CMAKE_CURRENT_SOURCE_DIR}/tools/errors.py"
    VERBATIM)
add_custom_target(bacuri-error-catalog DEPENDS "${error_catalog_header}")
add_library(bacuri-vms-errors INTERFACE)
target_include_directories(bacuri-vms-errors INTERFACE "${error_catalog_dir}")
add_dependencies(bacuri-vms-errors bacuri-error-catalog)
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
# Generated Protobuf and gRPC code is not ours to change, so its warnings are not reported.
target_include_directories(bacuri-vms-rpc SYSTEM PUBLIC "${generated_dir}")
target_compile_options(bacuri-vms-rpc PRIVATE -w)
target_link_libraries(bacuri-vms-rpc PUBLIC protobuf::libprotobuf PkgConfig::GRPC)

if(BUILD_TESTING)
    find_package(GTest CONFIG REQUIRED)
    include(GoogleTest)
endif()

# Keep the library and executable paths used by packaging and site scenarios.
block()
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
    include(cmake/CoreModule.cmake)
    add_subdirectory(src/domain)
    add_subdirectory(src/platform)
    add_subdirectory(src/observability)
    add_subdirectory(src/storage/sqlite)
    add_subdirectory(src/storage/postgresql)
    add_subdirectory(src/recording)
    add_subdirectory(src/ingest)
    add_subdirectory(src/archive)
    add_subdirectory(src/media)
    add_subdirectory(src/application/control_api)
    add_subdirectory(src/services/control)
    add_subdirectory(src/services/recorder)
    add_subdirectory(src/services/media)
    add_subdirectory(src/services/observation_source)
    add_subdirectory(src/modules/access)
    add_subdirectory(src/modules/events)
    add_subdirectory(src/modules/inventory)
    add_subdirectory(src/modules/maps)
    add_subdirectory(src/modules/media_sessions)
    add_subdirectory(src/modules/recordings)
    add_subdirectory(src/modules/investigations)
    add_subdirectory(src/application/export)
    add_subdirectory(src/application/recovery)
    add_subdirectory(src/admin)
endblock()

foreach(target IN ITEMS bacuri-maintain bacuri-vms bacuri-vms-core)
    add_dependencies(${target} bacuri-product-version)
    target_include_directories(${target} PRIVATE "${generated_dir}")
endforeach()

if(BUILD_TESTING)
    add_test(NAME coverage-thresholds COMMAND Python3::Interpreter -B "${PROJECT_SOURCE_DIR}/tests/architecture/coverage.py")
    add_test(NAME core-module-boundaries COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tests/architecture/core_boundaries.py)
endif()

if(BACURI_VMS_BUILD_RTSP_FIXTURE)
    add_subdirectory(tests/fixtures)
endif()
