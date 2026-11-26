add_library(bacuri-vms-recording-plan STATIC policy.cpp plan.cpp)
target_link_libraries(bacuri-vms-recording-plan PUBLIC nlohmann_json::nlohmann_json bacuri-vms-sqlite PkgConfig::GIO)

if(BUILD_TESTING)
    add_executable(bacuri-vms-recording-plan-test "${PROJECT_SOURCE_DIR}/tests/recording/plan.cpp")
    target_link_libraries(bacuri-vms-recording-plan-test PRIVATE bacuri-vms-recording-plan GTest::gtest_main)
    gtest_discover_tests(bacuri-vms-recording-plan-test DISCOVERY_MODE PRE_TEST)
    add_module_quality_check(bacuri-vms-recording-plan-test)
endif()
