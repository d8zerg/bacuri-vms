add_library(bacuri-vms-media-auth STATIC lease.cpp)
target_link_libraries(bacuri-vms-media-auth PUBLIC bacuri-vms-platform bacuri-vms-rpc bacuri-vms-metrics)
add_library(bacuri-video-transform STATIC clip.cpp privacy.cpp)
target_link_libraries(bacuri-video-transform PUBLIC PkgConfig::GST bacuri-vms-evidence)
