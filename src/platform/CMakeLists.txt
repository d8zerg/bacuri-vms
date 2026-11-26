# Identifiers, settings, secrets and camera network policy shared by all services.
# Identifiers have no system dependencies, so the export verifier can use them.
add_library(bacuri-vms-identifiers STATIC identifiers.cpp)
target_link_libraries(bacuri-vms-identifiers PUBLIC bacuri-vms-domain)
add_library(bacuri-vms-platform STATIC settings.cpp secrets.cpp
    camera_networks.cpp)
target_link_libraries(bacuri-vms-platform PUBLIC bacuri-vms-errors bacuri-vms-identifiers PkgConfig::SODIUM PkgConfig::GIO)

if(BUILD_TESTING)
    add_executable(bacuri-vms-platform-test
        "${PROJECT_SOURCE_DIR}/tests/platform/support.cpp" "${PROJECT_SOURCE_DIR}/tests/platform/rpc_status.cpp")
    target_link_libraries(bacuri-vms-platform-test PRIVATE bacuri-vms-platform PkgConfig::GRPC GTest::gtest_main)
    gtest_discover_tests(bacuri-vms-platform-test DISCOVERY_MODE PRE_TEST)
endif()
