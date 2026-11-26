add_library(bacuri-vms-archive-recovery STATIC archive_recovery.cpp)
target_link_libraries(bacuri-vms-archive-recovery PUBLIC bacuri-vms-control-storage
    PRIVATE bacuri-vms-volume bacuri-vms-media-session-store)

add_library(bacuri-vms-recording-catalog STATIC
    operations.cpp
    policies.cpp
    recorder.cpp
    status.cpp)
target_include_directories(bacuri-vms-recording-catalog PRIVATE "${generated_dir}")
target_link_libraries(bacuri-vms-recording-catalog PUBLIC bacuri-vms-manifest bacuri-vms-control-api
    PRIVATE bacuri-vms-recording-plan bacuri-vms-access bacuri-vms-event-journal bacuri-vms-inventory-operations bacuri-vms-media-auth bacuri-vms-volume)

add_library(bacuri-vms-recordings STATIC
    archive_catalog.cpp
    configuration.cpp
    holds.cpp
    search.cpp)
target_include_directories(bacuri-vms-recordings PRIVATE "${generated_dir}")
target_link_libraries(bacuri-vms-recordings PUBLIC bacuri-vms-recording-catalog bacuri-vms-rpc bacuri-vms-manifest
    PRIVATE bacuri-vms-access bacuri-vms-analysis bacuri-vms-archive-recovery bacuri-vms-events bacuri-vms-map-placements)

target_link_libraries(bacuri-vms-recordings PRIVATE bacuri-vms-configuration-snapshot)

add_core_module_checks(bacuri-vms-recordings
    TESTS recording_status.cpp
    ARCHIVES bacuri-vms-recording-catalog bacuri-vms-archive-recovery)
