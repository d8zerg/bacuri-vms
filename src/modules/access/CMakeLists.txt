add_library(bacuri-vms-access STATIC
    admin.cpp
    configuration.cpp
    oidc.cpp
    operations.cpp
    session.cpp
    webauthn.cpp)
target_link_libraries(bacuri-vms-access PUBLIC bacuri-vms-control-api
    PRIVATE bacuri-vms-inventory-operations PkgConfig::FIDO PkgConfig::CURL OpenSSL::Crypto protobuf::libprotobuf)

target_link_libraries(bacuri-vms-access PRIVATE bacuri-vms-configuration-snapshot)

add_core_module_checks(bacuri-vms-access
    TESTS access.cpp)

if(BUILD_TESTING)
    # The access library alone must not resolve a lookup owned by maps.
    add_executable(core-undeclared-module-link EXCLUDE_FROM_ALL "${PROJECT_SOURCE_DIR}/tests/modules/access/access_map_link.cpp")
    target_link_libraries(core-undeclared-module-link PRIVATE bacuri-vms-access)
    set(link_check "${PROJECT_SOURCE_DIR}/tests/architecture/expect_link_failure.py")
    set_target_properties(core-undeclared-module-link PROPERTIES
        CXX_LINKER_LAUNCHER "${Python3_EXECUTABLE};${link_check}"
        LINK_DEPENDS "${link_check}"
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/module-checks" SUFFIX ".checked")
    add_dependencies(bacuri-vms-access-test core-undeclared-module-link)
endif()
