add_library(bacuri-vms-control-storage STATIC database.cpp migrations.cpp)
target_include_directories(bacuri-vms-control-storage PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(bacuri-vms-control-storage PUBLIC PostgreSQL::PostgreSQL nlohmann_json::nlohmann_json)

add_library(bacuri-vms-configuration-snapshot STATIC configuration_snapshot.cpp)
target_link_libraries(bacuri-vms-configuration-snapshot PUBLIC bacuri-vms-control-storage)

if(BUILD_TESTING)
    # The site scenarios provide the PostgreSQL connection for the pool tests.
    add_executable(bacuri-vms-database-test "${PROJECT_SOURCE_DIR}/tests/storage/postgresql/database.cpp")
    target_link_libraries(bacuri-vms-database-test PRIVATE bacuri-vms-control-storage GTest::gtest_main)
    gtest_discover_tests(bacuri-vms-database-test DISCOVERY_MODE PRE_TEST)
endif()
