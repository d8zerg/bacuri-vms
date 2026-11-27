add_library(bacuri-vms-routes STATIC transport/routes.cpp)
target_link_libraries(bacuri-vms-routes PUBLIC bacuri-vms-control-api)

add_executable(bacuri-vms-core
    main.cpp
    transport/alert_socket.cpp
    transport/https.cpp
    transport/node_probe.cpp
    transport/server.cpp)
target_link_libraries(bacuri-vms-core PRIVATE bacuri-vms-export bacuri-vms-configuration-recovery bacuri-vms-investigations
    bacuri-vms-access bacuri-vms-events bacuri-vms-inventory bacuri-vms-maps
    bacuri-vms-media-sessions bacuri-vms-recordings bacuri-vms-routes bacuri-video-transform bacuri-vms-media-auth
    bacuri-vms-metrics OpenSSL::SSL)

if(BUILD_TESTING)
    add_test(NAME core-version COMMAND bacuri-vms-core --version)
    add_executable(bacuri-vms-control-test "${PROJECT_SOURCE_DIR}/tests/services/control/routes.cpp")
    target_link_libraries(bacuri-vms-control-test PRIVATE bacuri-vms-routes GTest::gtest_main)
    gtest_discover_tests(bacuri-vms-control-test DISCOVERY_MODE PRE_TEST)
endif()
