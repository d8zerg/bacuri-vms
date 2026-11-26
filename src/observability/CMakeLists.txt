add_library(bacuri-vms-metrics STATIC metrics.cpp)
target_include_directories(bacuri-vms-metrics PUBLIC "${PROJECT_SOURCE_DIR}/src")

if(BUILD_TESTING)
    add_executable(bacuri-vms-metrics-test "${PROJECT_SOURCE_DIR}/tests/observability/metrics.cpp")
    target_link_libraries(bacuri-vms-metrics-test PRIVATE bacuri-vms-metrics GTest::gtest_main)
    gtest_discover_tests(bacuri-vms-metrics-test DISCOVERY_MODE PRE_TEST)
endif()
