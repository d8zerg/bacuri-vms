add_library(bacuri-vms-domain STATIC
    archive.cpp
    resources.cpp
    time.cpp
)
target_include_directories(bacuri-vms-domain PUBLIC "${PROJECT_SOURCE_DIR}/src")

if(BUILD_TESTING)
    add_executable(bacuri-vms-domain-test "${PROJECT_SOURCE_DIR}/tests/domain/model.cpp")
    target_link_libraries(bacuri-vms-domain-test PRIVATE bacuri-vms-domain GTest::gmock_main)
    gtest_discover_tests(bacuri-vms-domain-test DISCOVERY_MODE PRE_TEST)
endif()
