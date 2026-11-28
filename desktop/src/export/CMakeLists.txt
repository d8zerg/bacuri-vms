add_library(bacuri-vms-client-export STATIC export_commands.cpp export_jobs.cpp)
target_link_libraries(bacuri-vms-client-export PUBLIC bacuri-vms-client-commands)
qt_extract_metatypes(bacuri-vms-client-export)
