find_package(PkgConfig REQUIRED)
pkg_check_modules(GstRtsp REQUIRED IMPORTED_TARGET gstreamer-rtsp-server-1.0)

add_executable(bacuri-vms-test-rtsp-camera rtsp_camera.cpp)
target_link_libraries(bacuri-vms-test-rtsp-camera PRIVATE PkgConfig::GstRtsp)
target_compile_options(bacuri-vms-test-rtsp-camera PRIVATE -Wall -Wextra -Wpedantic)
