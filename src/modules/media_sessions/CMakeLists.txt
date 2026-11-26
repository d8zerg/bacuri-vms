add_library(bacuri-vms-media-session-store STATIC operations.cpp)
target_link_libraries(bacuri-vms-media-session-store PUBLIC bacuri-vms-control-storage)

add_library(bacuri-vms-media-sessions STATIC
    archive_search.cpp
    open_session.cpp
    session_delivery.cpp
    layouts.cpp
    media_authority.cpp
    playback.cpp)
target_link_libraries(bacuri-vms-media-sessions PUBLIC bacuri-vms-manifest bacuri-vms-access bacuri-vms-media-session-store bacuri-vms-rpc
    PRIVATE bacuri-vms-event-journal bacuri-vms-inventory-operations bacuri-vms-media-auth bacuri-vms-recording-catalog)

target_link_libraries(bacuri-vms-media-sessions PRIVATE bacuri-vms-configuration-snapshot)

add_core_module_checks(bacuri-vms-media-sessions
    TESTS layouts.cpp
    ARCHIVES bacuri-vms-media-session-store)

if(BUILD_TESTING)
    target_link_libraries(bacuri-vms-media-sessions-test PRIVATE GTest::gmock)
endif()
