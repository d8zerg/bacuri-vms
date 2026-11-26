cmake_minimum_required(VERSION 3.25)
project(bacuri-vms LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(
    -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
    "$<$<CONFIG:Release>:-ffile-prefix-map=${CMAKE_SOURCE_DIR}=.>"
    "$<$<CONFIG:Release>:-ffile-prefix-map=${CMAKE_BINARY_DIR}=./build>"
)

option(BACURI_VMS_BUILD_RTSP_FIXTURE "Build the test RTSP camera" OFF)
include(CTest)

add_library(bacuri-vms-metrics STATIC src/observability/metrics.cpp)
target_include_directories(bacuri-vms-metrics PUBLIC src)

add_library(bacuri-vms-domain STATIC
    src/domain/archive.cpp
    src/domain/resources.cpp
    src/domain/time.cpp
)
target_include_directories(bacuri-vms-domain PUBLIC src)

find_package(PkgConfig REQUIRED)
find_package(Protobuf REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(PNG REQUIRED)
find_package(LibArchive REQUIRED)
pkg_check_modules(MAP_CURL REQUIRED IMPORTED_TARGET libcurl)
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

# Identifiers, settings, secrets and camera network policy shared by all services.
# Identifiers have no system dependencies, so the export verifier can use them.
add_library(bacuri-vms-identifiers STATIC src/platform/identifiers.cpp)
target_link_libraries(bacuri-vms-identifiers PUBLIC bacuri-vms-domain)
add_library(bacuri-vms-platform STATIC src/platform/settings.cpp src/platform/secrets.cpp
    src/platform/camera_networks.cpp)
target_link_libraries(bacuri-vms-platform PUBLIC bacuri-vms-identifiers PkgConfig::SODIUM PkgConfig::GIO)
pkg_check_modules(GST REQUIRED IMPORTED_TARGET gstreamer-app-1.0>=1.22 gstreamer-rtp-1.0 gstreamer-rtsp-1.0 gstreamer-video-1.0)
add_library(bacuri-vms-ingest STATIC src/recorder/frame_clock.cpp src/recorder/frame_queue.cpp
    src/recorder/ingest.cpp src/recorder/replay.cpp src/recorder/rtsp_session.cpp src/recorder/audio.cpp)
target_link_libraries(bacuri-vms-ingest PUBLIC bacuri-vms-domain bacuri-vms-platform PRIVATE nlohmann_json::nlohmann_json PkgConfig::GST PkgConfig::GIO)
add_library(bacuri-vms-manifest STATIC src/archive/manifest.cpp)
target_link_libraries(bacuri-vms-manifest PUBLIC bacuri-vms-identifiers nlohmann_json::nlohmann_json PRIVATE PkgConfig::SODIUM)
add_library(bacuri-vms-mp4 STATIC src/archive/mp4.cpp)
target_link_libraries(bacuri-vms-mp4 PUBLIC bacuri-vms-manifest PRIVATE PkgConfig::SODIUM Boost::headers)
# Export and case verification with libsodium and JSON only.
add_library(bacuri-vms-verify STATIC src/archive/verify.cpp)
target_link_libraries(bacuri-vms-verify PUBLIC bacuri-vms-manifest PRIVATE bacuri-vms-mp4 PkgConfig::SODIUM)
add_library(bacuri-vms-analysis STATIC src/archive/analysis.cpp)
target_link_libraries(bacuri-vms-analysis PUBLIC bacuri-vms-manifest PRIVATE PkgConfig::GST)
add_library(bacuri-vms-volume STATIC src/archive/volume.cpp src/archive/replica.cpp src/archive/recording_policy.cpp)
target_include_directories(bacuri-vms-volume PRIVATE "${generated_dir}")
target_link_libraries(bacuri-vms-volume PUBLIC bacuri-vms-manifest bacuri-vms-mp4 bacuri-vms-platform bacuri-vms-metrics SQLite::SQLite3 PkgConfig::GIO
    PRIVATE Boost::headers)
add_library(bacuri-vms-evidence STATIC src/archive/evidence.cpp)
target_link_libraries(bacuri-vms-evidence PUBLIC bacuri-vms-verify bacuri-vms-volume bacuri-vms-platform)
add_executable(bacuri-vms-verify-export src/archive/verify_export.cpp)
target_link_libraries(bacuri-vms-verify-export PRIVATE bacuri-vms-verify PkgConfig::SODIUM)
add_library(bacuri-vms-fmp4 STATIC src/archive/fmp4.cpp)
target_include_directories(bacuri-vms-fmp4 PUBLIC src)
add_library(bacuri-vms-archive STATIC src/archive/writer.cpp src/archive/delivery.cpp)
target_include_directories(bacuri-vms-archive PRIVATE "${generated_dir}")
target_link_libraries(bacuri-vms-archive PUBLIC bacuri-vms-analysis bacuri-vms-volume bacuri-vms-ingest SQLite::SQLite3
    PRIVATE bacuri-vms-fmp4 bacuri-vms-rpc PkgConfig::GST Boost::headers)
add_executable(bacuri-vms-recorder src/recorder/main.cpp src/recorder/sources.cpp src/recorder/detector.cpp)
target_link_libraries(bacuri-vms-recorder PRIVATE bacuri-vms-archive PkgConfig::GST)
target_link_libraries(bacuri-vms-recorder PRIVATE bacuri-vms-ingest nlohmann_json::nlohmann_json)
target_link_libraries(bacuri-vms-recorder PRIVATE bacuri-vms-platform bacuri-vms-rpc SQLite::SQLite3 Boost::headers)
add_library(bacuri-vms-media-auth STATIC src/media/lease.cpp)
target_link_libraries(bacuri-vms-media-auth PUBLIC bacuri-vms-platform bacuri-vms-rpc bacuri-vms-metrics)
pkg_check_modules(WEBRTC REQUIRED IMPORTED_TARGET gstreamer-webrtc-1.0>=1.22 gstreamer-sdp-1.0)
add_library(bacuri-video-transform STATIC src/archive/clip.cpp src/media/privacy.cpp)
target_link_libraries(bacuri-video-transform PUBLIC PkgConfig::GST bacuri-vms-evidence)
add_executable(bacuri-vms-media src/media/main.cpp src/media/live.cpp src/media/webrtc.cpp)
target_compile_definitions(bacuri-vms-media PRIVATE GST_USE_UNSTABLE_API)
target_link_libraries(bacuri-vms-media PRIVATE bacuri-video-transform PkgConfig::WEBRTC PkgConfig::GST)
target_link_libraries(bacuri-vms-media PRIVATE bacuri-vms-media-auth bacuri-vms-archive Boost::headers)
add_executable(bacuri-vms-observation-source src/control/observation_source.cpp)
target_link_libraries(bacuri-vms-observation-source PRIVATE bacuri-vms-rpc bacuri-vms-platform)
add_library(bacuri-maintenance-common STATIC src/core/maintenance_process.cpp src/core/maintenance_bundle.cpp)
add_executable(bacuri-install src/core/install.cpp)
target_link_libraries(bacuri-install PRIVATE bacuri-maintenance-common)
# The sanitizer runtimes need a dynamic loader. The offline installer is linked statically for distribution.
if(NOT CMAKE_CXX_FLAGS MATCHES "-fsanitize=")
    target_link_options(bacuri-install PRIVATE -static)
endif()
add_executable(bacuri-maintain src/core/maintenance.cpp src/core/maintenance_crypto.cpp src/core/maintenance_diagnostics.cpp
    src/control/database.cpp src/control/archive_recovery.cpp src/control/media_sessions/operations.cpp)
target_link_libraries(bacuri-maintain PRIVATE bacuri-maintenance-common bacuri-vms-volume PostgreSQL::PostgreSQL)
add_executable(bacuri-vms src/core/site_admin.cpp)
target_link_libraries(bacuri-vms PRIVATE bacuri-vms-platform OpenSSL::Crypto)
add_library(bacuri-vms-recording-status STATIC src/control/recordings/status.cpp)
target_include_directories(bacuri-vms-recording-status PUBLIC src)
add_executable(bacuri-vms-core src/core/main.cpp
    src/control/recordings/recorder.cpp
    src/control/access/admin.cpp src/control/access/oidc.cpp src/control/access/operations.cpp src/control/inventory/operations.cpp src/control/events/operations.cpp src/control/events/video_rules.cpp
    src/control/recordings/operations.cpp src/control/recordings/policies.cpp src/control/recordings/investigations.cpp src/control/recordings/search.cpp src/control/media_sessions/operations.cpp
    src/control/inventory/configuration.cpp src/control/access/configuration.cpp src/control/maps/configuration.cpp src/control/recordings/configuration.cpp
    src/control/configuration_snapshot.cpp src/control/configuration_backup.cpp src/control/archive_recovery.cpp src/control/database.cpp src/control/inventory/batches.cpp src/control/registry.cpp src/control/inventory/audio.cpp src/control/onvif.cpp src/control/camera_events.cpp src/control/inventory/telemetry.cpp src/control/maps/orientation.cpp src/control/maps/georeference.cpp
    src/control/observations.cpp src/control/alert_work.cpp src/control/alerts.cpp src/control/alert_socket.cpp
    src/control/export.cpp src/control/media_authority.cpp src/control/playback.cpp src/control/media_sessions/layouts.cpp
    src/control/webauthn.cpp src/control/session.cpp src/control/server.cpp src/control/routes.cpp src/control/https.cpp src/control/maps.cpp src/control/map_import.cpp src/control/maps/packages.cpp src/control/maps/transfers.cpp src/control/delivery.cpp src/control/archive_catalog.cpp)
target_link_libraries(bacuri-vms-core PRIVATE bacuri-vms-analysis bacuri-video-transform LibArchive::LibArchive PkgConfig::MAP_CURL PkgConfig::FIDO bacuri-vms-evidence bacuri-vms-media-auth PNG::PNG PkgConfig::GST)
target_link_libraries(bacuri-vms-core PRIVATE bacuri-vms-platform bacuri-vms-rpc bacuri-vms-recording-status
    bacuri-vms-metrics
    LibXml2::LibXml2 PostgreSQL::PostgreSQL Boost::headers OpenSSL::SSL PkgConfig::GIO nlohmann_json::nlohmann_json)
foreach(target IN ITEMS bacuri-maintain bacuri-vms bacuri-vms-core)
    add_dependencies(${target} bacuri-product-version)
    target_include_directories(${target} PRIVATE "${generated_dir}")
endforeach()

if(BUILD_TESTING)
    find_package(GTest CONFIG REQUIRED)
    include(GoogleTest)
    add_test(NAME core-module-boundaries COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tests/architecture/core_boundaries.py)
    add_executable(bacuri-vms-archive-test tests/archive/volume.cpp tests/archive/recording_policy.cpp
        tests/archive/writer.cpp tests/archive/fmp4.cpp)
    target_link_libraries(bacuri-vms-archive-test PRIVATE bacuri-vms-archive bacuri-vms-fmp4 PkgConfig::GST GTest::gtest_main)
    if(CMAKE_CXX_FLAGS MATCHES "-fsanitize=thread")
        set(codec_test "RecordingPolicy.ArchiveDecodesNumberedSamplesAcrossTheRepeatedHour:ArchiveWriter.*:FragmentedMp4.*")
        gtest_discover_tests(bacuri-vms-archive-test DISCOVERY_MODE PRE_TEST TEST_FILTER "-${codec_test}")
        # The native media pipeline transfers buffer ownership inside distribution libraries built without TSan.
        gtest_discover_tests(bacuri-vms-archive-test DISCOVERY_MODE PRE_TEST TEST_FILTER "${codec_test}"
            PROPERTIES ENVIRONMENT_MODIFICATION "TSAN_OPTIONS=string_append::ignore_noninstrumented_modules=1")
    else()
        gtest_discover_tests(bacuri-vms-archive-test DISCOVERY_MODE PRE_TEST)
    endif()
    add_executable(bacuri-vms-frame-queue-test tests/ingest/frame_queue.cpp)
    target_link_libraries(bacuri-vms-frame-queue-test PRIVATE bacuri-vms-ingest GTest::gmock_main)
    gtest_discover_tests(bacuri-vms-frame-queue-test DISCOVERY_MODE PRE_TEST)
    add_executable(bacuri-vms-rtsp-test tests/ingest/rtsp.cpp)
    target_link_libraries(bacuri-vms-rtsp-test PRIVATE bacuri-vms-ingest GTest::gtest_main)
    add_test(NAME core-version COMMAND bacuri-vms-core --version)
    add_executable(bacuri-vms-domain-test tests/domain/model.cpp)
    target_link_libraries(bacuri-vms-domain-test PRIVATE bacuri-vms-domain GTest::gmock_main)
    gtest_discover_tests(bacuri-vms-domain-test DISCOVERY_MODE PRE_TEST)
    add_executable(bacuri-vms-control-test tests/control/support.cpp tests/control/alert_workflow.cpp
        tests/control/recording_status.cpp tests/control/routes.cpp src/control/routes.cpp)
    target_link_libraries(bacuri-vms-control-test PRIVATE bacuri-vms-platform bacuri-vms-recording-status
        GTest::gtest_main)
    gtest_discover_tests(bacuri-vms-control-test DISCOVERY_MODE PRE_TEST)
    add_executable(bacuri-vms-metrics-test tests/observability/metrics.cpp)
    target_link_libraries(bacuri-vms-metrics-test PRIVATE bacuri-vms-metrics GTest::gtest_main)
    gtest_discover_tests(bacuri-vms-metrics-test DISCOVERY_MODE PRE_TEST)
    # Skipped without PostgreSQL. The control scenarios run it against their isolated database.
    add_executable(bacuri-vms-database-test tests/control/database.cpp src/control/database.cpp)
    target_include_directories(bacuri-vms-database-test PRIVATE src)
    target_link_libraries(bacuri-vms-database-test PRIVATE PostgreSQL::PostgreSQL GTest::gtest_main)
    gtest_discover_tests(bacuri-vms-database-test DISCOVERY_MODE PRE_TEST)
endif()

if(BACURI_VMS_BUILD_RTSP_FIXTURE)
    add_subdirectory(tests/fixtures)
endif()
