add_library(bacuri-vms-event-journal STATIC operations.cpp)
target_link_libraries(bacuri-vms-event-journal PUBLIC bacuri-vms-control-storage PRIVATE bacuri-vms-inventory-operations)

add_library(bacuri-vms-events STATIC
    alert_work.cpp
    alerts.cpp
    observations.cpp
    video_rules.cpp)
target_link_libraries(bacuri-vms-events PUBLIC bacuri-vms-control-api bacuri-vms-event-journal bacuri-vms-rpc
    PRIVATE bacuri-vms-access bacuri-vms-analysis bacuri-vms-inventory-operations bacuri-vms-recording-catalog bacuri-vms-volume)

add_core_module_checks(bacuri-vms-events
    TESTS alert_workflow.cpp
    ARCHIVES bacuri-vms-event-journal)

if(BUILD_TESTING)
    add_dependencies(check-events bacuri-vms-observation-source)
endif()
