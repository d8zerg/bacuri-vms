add_library(bacuri-vms-onvif STATIC
    onvif/adapter.cpp
    onvif/discovery.cpp
    onvif/events.cpp
    onvif/imaging.cpp
    onvif/profiles.cpp
    onvif/ptz.cpp
    onvif/recording.cpp)
target_link_libraries(bacuri-vms-onvif PUBLIC bacuri-vms-control-api
    PRIVATE LibXml2::LibXml2 PkgConfig::SODIUM bacuri-vms-rpc)

add_library(bacuri-vms-inventory-operations STATIC operations.cpp)
target_link_libraries(bacuri-vms-inventory-operations PUBLIC bacuri-vms-control-storage)

add_library(bacuri-vms-inventory STATIC
    audio.cpp
    batches.cpp
    camera_events.cpp
    configuration.cpp
    delivery.cpp
    registry.cpp
    telemetry.cpp)
target_link_libraries(bacuri-vms-inventory PUBLIC bacuri-vms-access bacuri-vms-inventory-operations
    PRIVATE bacuri-vms-events bacuri-vms-media-auth bacuri-vms-onvif bacuri-vms-recording-catalog)

target_link_libraries(bacuri-vms-inventory PRIVATE bacuri-vms-configuration-snapshot)

add_core_module_checks(bacuri-vms-inventory
    TESTS device.cpp
    ARCHIVES bacuri-vms-inventory-operations bacuri-vms-onvif)
