set(tokens ${CMAKE_CURRENT_BINARY_DIR}/Tokens.qml)
set(source ${design_dir}/tokens.json)
set(css ${design_dir}/tokens.css)
set(generator ${PROJECT_SOURCE_DIR}/../tools/desktop_tokens.py)
file(GLOB consumers CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.qml"
    "${PROJECT_SOURCE_DIR}/src/app/*.qml" "${PROJECT_SOURCE_DIR}/tools/catalog/*.qml")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${source} ${design_dir}/tokens.template.css ${css} ${generator} ${consumers})
execute_process(COMMAND ${Python3_EXECUTABLE} ${generator} ${source} --check-css ${css} --qml ${tokens} --check-uses ${consumers} COMMAND_ERROR_IS_FATAL ANY)
set_source_files_properties(${tokens} PROPERTIES QT_QML_SINGLETON_TYPE TRUE QT_RESOURCE_ALIAS Tokens.qml)

qt_add_library(bacuri-vms-client-ui STATIC)
qt_add_qml_module(bacuri-vms-client-ui
    URI Bacuri.Vms.Client.Ui
    VERSION 1.0
    OUTPUT_DIRECTORY ${qml_dir}/Ui
    QML_FILES ${tokens} KitButton.qml KitChoice.qml KitField.qml KitFilterPanel.qml KitLabel.qml KitPanel.qml KitSegmentedControl.qml StatusChip.qml StatePane.qml
    NO_IMPORT_SCAN
)
target_link_libraries(bacuri-vms-client-ui PUBLIC Qt6::QuickControls2 bacuri-vms-client-settingsplugin)
set(assets brand/mark.svg assets/inter-latin.woff2 assets/inter-cyrillic.woff2 assets/Inter-LICENSE.txt icons/LICENSE.txt)
foreach(icon archive expand move scan-line file-down map-pin clipboard-list archive-x bell-ring cctv circle-x hard-drive layers link-2 list-filter log-in log-out monitor moon rotate-cw search settings-2 sun unplug)
    list(APPEND assets icons/${icon}.svg)
endforeach()
list(TRANSFORM assets PREPEND "${design_dir}/")
qt_add_resources(bacuri-vms-client-ui design PREFIX /design BASE ${design_dir} FILES ${assets})
qt_add_resources(bacuri-vms-client-ui desktop_icons PREFIX /design BASE ${CMAKE_CURRENT_SOURCE_DIR}
    FILES icons/chevron-down.svg icons/ellipsis.svg icons/arrow-left.svg icons/new-window.svg)
