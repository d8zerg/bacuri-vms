add_library(bacuri-vms-ingest STATIC frame_clock.cpp frame_queue.cpp
    ingest.cpp replay.cpp rtsp_session.cpp audio.cpp)
target_link_libraries(bacuri-vms-ingest PUBLIC bacuri-vms-domain bacuri-vms-platform PRIVATE nlohmann_json::nlohmann_json PkgConfig::GST PkgConfig::GIO)

if(BUILD_TESTING)
    add_executable(bacuri-vms-frame-queue-test "${PROJECT_SOURCE_DIR}/tests/ingest/frame_queue.cpp")
    target_link_libraries(bacuri-vms-frame-queue-test PRIVATE bacuri-vms-ingest GTest::gmock_main)
    gtest_discover_tests(bacuri-vms-frame-queue-test DISCOVERY_MODE PRE_TEST)
    add_executable(bacuri-vms-rtsp-test "${PROJECT_SOURCE_DIR}/tests/ingest/rtsp.cpp")
    target_link_libraries(bacuri-vms-rtsp-test PRIVATE bacuri-vms-ingest GTest::gtest_main)
endif()
