add_library(bacuri-vms-configuration-recovery STATIC configuration_backup.cpp)
target_link_libraries(bacuri-vms-configuration-recovery PUBLIC bacuri-vms-control-api
    PRIVATE bacuri-vms-investigations bacuri-vms-access bacuri-vms-inventory bacuri-vms-maps bacuri-vms-recordings bacuri-vms-media-sessions bacuri-vms-configuration-snapshot)

add_core_module_checks(bacuri-vms-configuration-recovery TESTS snapshot.cpp)
