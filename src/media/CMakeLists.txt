add_library(bacuri-vms-media-auth STATIC lease.cpp)
target_link_libraries(bacuri-vms-media-auth PUBLIC bacuri-vms-platform bacuri-vms-rpc bacuri-vms-metrics)
add_library(bacuri-video-transform STATIC clip.cpp privacy.cpp)
target_link_libraries(bacuri-video-transform PUBLIC PkgConfig::GST bacuri-vms-evidence)

add_library(bacuri-acceleration-settings STATIC acceleration/settings.cpp)
target_link_libraries(bacuri-acceleration-settings PUBLIC nlohmann_json::nlohmann_json)
target_include_directories(bacuri-acceleration-settings PUBLIC "${PROJECT_SOURCE_DIR}/src")

add_library(bacuri-node-probe STATIC acceleration/probe.cpp acceleration/health.cpp acceleration/scheduler.cpp acceleration/process.cpp
    acceleration/inventory.cpp acceleration/video.cpp acceleration/inference.cpp)
target_compile_definitions(bacuri-node-probe PRIVATE BACURI_VA_DRIVER_DIRECTORY="/usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}/dri")
target_link_libraries(bacuri-node-probe PUBLIC bacuri-acceleration-settings bacuri-video-transform bacuri-vms-analysis ${CMAKE_DL_LIBS})

if(BUILD_TESTING)
    add_executable(bacuri-scheduler-worker EXCLUDE_FROM_ALL "${PROJECT_SOURCE_DIR}/tests/acceleration/worker.cpp")
    target_link_libraries(bacuri-scheduler-worker PRIVATE bacuri-vms-media-auth PkgConfig::GST nlohmann_json::nlohmann_json)
    add_executable(bacuri-node-probe-test "${PROJECT_SOURCE_DIR}/tests/acceleration/probe.cpp"
        "${PROJECT_SOURCE_DIR}/tests/acceleration/settings.cpp" "${PROJECT_SOURCE_DIR}/tests/acceleration/scheduler.cpp")
    target_link_libraries(bacuri-node-probe-test PRIVATE bacuri-node-probe bacuri-acceleration-settings GTest::gtest_main)
    gtest_discover_tests(bacuri-node-probe-test DISCOVERY_MODE PRE_TEST)
endif()
