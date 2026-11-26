add_library(bacuri-vms-investigations STATIC cases.cpp)
target_link_libraries(bacuri-vms-investigations PUBLIC bacuri-vms-control-api
    PRIVATE bacuri-vms-recordings bacuri-vms-event-journal bacuri-vms-access bacuri-vms-configuration-snapshot)

add_core_module_checks(bacuri-vms-investigations TESTS bookmarks.cpp)
