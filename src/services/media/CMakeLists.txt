add_executable(bacuri-vms-media main.cpp live.cpp webrtc.cpp)
target_compile_definitions(bacuri-vms-media PRIVATE GST_USE_UNSTABLE_API)
target_link_libraries(bacuri-vms-media PRIVATE bacuri-node-probe PkgConfig::WEBRTC PkgConfig::GST)
target_link_libraries(bacuri-vms-media PRIVATE bacuri-vms-media-auth bacuri-vms-archive Boost::headers)
