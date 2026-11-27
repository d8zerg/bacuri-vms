add_executable(bacuri-vms-recorder main.cpp sources.cpp detector.cpp)
target_link_libraries(bacuri-vms-recorder PRIVATE bacuri-vms-archive bacuri-video-transform PkgConfig::GST)
target_link_libraries(bacuri-vms-recorder PRIVATE bacuri-vms-ingest nlohmann_json::nlohmann_json)
target_link_libraries(bacuri-vms-recorder PRIVATE bacuri-vms-platform bacuri-vms-rpc bacuri-vms-sqlite Boost::headers)
