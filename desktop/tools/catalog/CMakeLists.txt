qt_add_library(bacuri-vms-client-catalog-ui STATIC)
qt_add_qml_module(bacuri-vms-client-catalog-ui
    URI Bacuri.Vms.Client.Catalog
    VERSION 1.0
    OUTPUT_DIRECTORY ${qml_dir}/Catalog
    DEPENDENCIES Bacuri.Vms.Client.App Bacuri.Vms.Client.Ui
    SOURCES catalog_workspace.hpp catalog_workspace.cpp
    QML_FILES Catalog.qml ComponentPreview.qml
    NO_IMPORT_SCAN
)
target_link_libraries(bacuri-vms-client-catalog-ui PUBLIC bacuri-vms-client-app bacuri-vms-client-appplugin)
qt_add_executable(bacuri-vms-client-catalog ${PROJECT_SOURCE_DIR}/src/app/main.cpp)
set_target_properties(bacuri-vms-client-catalog PROPERTIES QT_QML_MODULE_NO_IMPORT_SCAN TRUE RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
target_compile_definitions(bacuri-vms-client-catalog PRIVATE BACURI_COMPONENT_CATALOG)
target_link_libraries(bacuri-vms-client-catalog PRIVATE bacuri-vms-client-app bacuri-vms-client-catalog-uiplugin)

target_include_directories(bacuri-vms-client-catalog-ui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
