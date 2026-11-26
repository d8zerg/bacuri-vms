# Link every object of each owner against its declared dependencies, including in release builds.
if(NOT BUILD_TESTING)
    file(GENERATE OUTPUT "${generated_dir}/module_link.cpp" CONTENT "int main() {}\n")
    add_library(bacuri-module-link-main OBJECT "${generated_dir}/module_link.cpp")
endif()

function(add_core_module_checks library)
    cmake_parse_arguments(module "" "" "TESTS;ARCHIVES" ${ARGN})
    get_filename_component(owner "${CMAKE_CURRENT_SOURCE_DIR}" NAME)
    file(RELATIVE_PATH directory "${PROJECT_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}")
    if(BUILD_TESTING)
        set(target "${library}-test")
        list(TRANSFORM module_TESTS PREPEND "${PROJECT_SOURCE_DIR}/tests/${directory}/")
        add_executable(${target} ${module_TESTS})
        target_link_libraries(${target} PRIVATE GTest::gtest_main)
        gtest_discover_tests(${target} DISCOVERY_MODE PRE_TEST PROPERTIES LABELS core-modules)

        add_custom_target(check-${owner}
            COMMAND sh tools/check-cpp format "src/${directory}" "tests/${directory}"
            COMMAND "${Python3_EXECUTABLE}" tests/architecture/core_boundaries.py
            COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${CMAKE_CURRENT_BINARY_DIR}" --output-on-failure --no-tests=error
            COMMAND sh tools/check-cpp lint "${PROJECT_BINARY_DIR}" "/(src|tests)/${directory}/"
            DEPENDS ${target}
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
            USES_TERMINAL VERBATIM)
    else()
        string(REPLACE "bacuri-vms-" "bacuri-link-" target "${library}")
        add_executable(${target} EXCLUDE_FROM_ALL $<TARGET_OBJECTS:bacuri-module-link-main>)
        set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/module-checks")
    endif()

    target_link_libraries(${target} PRIVATE ${library})
    foreach(archive IN ITEMS ${library} ${module_ARCHIVES})
        set_property(TARGET ${target} PROPERTY LINK_LIBRARY_OVERRIDE_${archive} WHOLE_ARCHIVE)
    endforeach()
    add_dependencies(bacuri-vms-core ${target})
endfunction()
