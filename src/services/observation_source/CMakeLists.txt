add_executable(bacuri-vms-observation-source main.cpp)
target_link_libraries(bacuri-vms-observation-source PRIVATE bacuri-vms-rpc bacuri-vms-platform PkgConfig::GIO)
