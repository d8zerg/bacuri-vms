add_library(bacuri-vms-client-live STATIC live_commands.cpp live_preview.cpp ptz.cpp snapshot.cpp)
target_link_libraries(bacuri-vms-client-live
    PUBLIC bacuri-vms-client-commands bacuri-vms-client-media
    PRIVATE Qt6::Concurrent)
qt_extract_metatypes(bacuri-vms-client-live)
