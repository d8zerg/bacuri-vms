qt_add_library(bacuri-vms-client-settings STATIC)
qt_add_qml_module(bacuri-vms-client-settings
    URI Bacuri.Vms.Client.Settings
    VERSION 1.0
    OUTPUT_DIRECTORY ${qml_dir}/Settings
    SOURCES preferences.cpp preferences.hpp
    NO_IMPORT_SCAN
)
target_include_directories(bacuri-vms-client-settings PUBLIC .. PRIVATE .)
target_link_libraries(bacuri-vms-client-settings PUBLIC Qt6::Qml Qt6::Gui Qt6::DBus)
find_program(LRELEASE lrelease HINTS /usr/lib/qt6/bin REQUIRED)
foreach(language en ru)
    set(qm ${CMAKE_CURRENT_BINARY_DIR}/${language}.qm)
    add_custom_command(OUTPUT ${qm}
        COMMAND ${LRELEASE} ${PROJECT_SOURCE_DIR}/i18n/${language}.ts -qm ${qm}
        DEPENDS ${PROJECT_SOURCE_DIR}/i18n/${language}.ts VERBATIM)
    set_source_files_properties(${qm} PROPERTIES GENERATED TRUE QT_RESOURCE_ALIAS ${language}.qm)
    list(APPEND catalogs ${qm})
endforeach()
qt_add_resources(bacuri-vms-client-settings translations PREFIX /i18n FILES ${catalogs})
