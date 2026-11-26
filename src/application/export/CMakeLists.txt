add_library(bacuri-vms-export STATIC export.cpp package.cpp audio.cpp case_export.cpp)
target_link_libraries(bacuri-vms-export PUBLIC bacuri-vms-control-api
    PRIVATE bacuri-vms-investigations bacuri-vms-media-sessions bacuri-vms-events bacuri-vms-recordings bacuri-vms-evidence bacuri-video-transform)

add_core_module_checks(bacuri-vms-export TESTS export.cpp)
