add_library(bacuri-vms-control-api STATIC api.cpp)
target_link_libraries(bacuri-vms-control-api PUBLIC bacuri-vms-control-storage bacuri-vms-platform Boost::headers)
