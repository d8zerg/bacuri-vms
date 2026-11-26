add_library(bacuri-vms-sqlite INTERFACE)
target_include_directories(bacuri-vms-sqlite INTERFACE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(bacuri-vms-sqlite INTERFACE SQLite::SQLite3)
