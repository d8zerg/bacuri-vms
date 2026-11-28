set_source_files_properties(StatusText.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_add_library(bacuri-vms-client-app STATIC)
qt_add_qml_module(bacuri-vms-client-app
    URI Bacuri.Vms.Client.App
    VERSION 1.0
    OUTPUT_DIRECTORY ${qml_dir}/App
    DEPENDENCIES QtQuick QtQml.Models Bacuri.Vms.Client.Ui
    SOURCES
        feature_types.hpp
        window_commands.cpp window_commands.hpp command_actions.cpp shell_commands.cpp view_commands.cpp
        palette_index.cpp palette_index.hpp point_model.cpp point_model.hpp navigation.cpp navigation.hpp
        workspace.cpp workspace.hpp window_group.cpp window_group.hpp
    QML_FILES CommandPanel.qml CameraMenu.qml Shell.qml AppearancePage.qml ShortcutSettings.qml LanguageSwitch.qml PointFilters.qml PointStatus.qml StatusText.qml PointRow.qml
    NO_IMPORT_SCAN
)
target_include_directories(bacuri-vms-client-app PUBLIC .. PRIVATE .)
target_link_libraries(bacuri-vms-client-app PUBLIC Qt6::WebSockets Qt6::Quick Qt6::Concurrent bacuri-vms-client-media bacuri-vms-client-site
    bacuri-vms-client-uiplugin bacuri-vms-client-settings bacuri-vms-client-settingsplugin
    bacuri-vms-client-live bacuri-vms-client-archive bacuri-vms-client-export)
set(messages ${CMAKE_CURRENT_BINARY_DIR}/messages.json)
set(root ${PROJECT_SOURCE_DIR}/..)
add_custom_command(OUTPUT ${messages}
    COMMAND ${Python3_EXECUTABLE} ${root}/tools/errors.py json
        ${CMAKE_CURRENT_BINARY_DIR}/retry.json ${CMAKE_CURRENT_BINARY_DIR}/codes.json ${messages}
    DEPENDS ${root}/tools/errors.py ${root}/contracts/v1/errors.yaml
        ${root}/web/client/src/i18n/en.ts ${root}/web/client/src/i18n/ru.ts VERBATIM)
set_source_files_properties(${messages} PROPERTIES GENERATED TRUE QT_RESOURCE_ALIAS messages.json)
qt_add_resources(bacuri-vms-client-app site_messages PREFIX /site FILES ${messages})
