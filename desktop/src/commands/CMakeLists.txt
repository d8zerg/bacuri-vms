add_library(bacuri-vms-client-commands STATIC command_registry.cpp command_time.cpp window_context.cpp)
target_include_directories(bacuri-vms-client-commands PUBLIC ..)
target_link_libraries(bacuri-vms-client-commands PUBLIC Qt6::Gui bacuri-vms-client-site)
