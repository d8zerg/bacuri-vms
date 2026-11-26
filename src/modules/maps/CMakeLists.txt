add_library(bacuri-vms-map-placements STATIC operations.cpp)
target_link_libraries(bacuri-vms-map-placements PUBLIC bacuri-vms-control-storage)

add_library(bacuri-vms-maps STATIC
    configuration.cpp
    georeference.cpp
    import.cpp
    orientation.cpp
    packages.cpp
    scene.cpp
    transfers.cpp)
target_link_libraries(bacuri-vms-maps PUBLIC bacuri-vms-access bacuri-vms-map-placements
    PRIVATE bacuri-vms-inventory-operations LibArchive::LibArchive PkgConfig::CURL PNG::PNG SQLite::SQLite3)

target_link_libraries(bacuri-vms-maps PRIVATE bacuri-vms-configuration-snapshot)

add_core_module_checks(bacuri-vms-maps
    TESTS georeference.cpp
    ARCHIVES bacuri-vms-map-placements)
