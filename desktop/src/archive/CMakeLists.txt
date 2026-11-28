add_library(bacuri-vms-client-archive STATIC archive_commands.cpp intervals.cpp)
target_link_libraries(bacuri-vms-client-archive PUBLIC bacuri-vms-client-commands)
