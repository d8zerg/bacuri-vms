add_library(bacuri-vms-manifest STATIC manifest.cpp)
target_link_libraries(bacuri-vms-manifest PUBLIC bacuri-vms-identifiers nlohmann_json::nlohmann_json PRIVATE PkgConfig::SODIUM)
add_library(bacuri-vms-mp4 STATIC mp4.cpp)
target_link_libraries(bacuri-vms-mp4 PUBLIC bacuri-vms-manifest PRIVATE PkgConfig::SODIUM Boost::headers)
# Export and case verification with libsodium and JSON only.
add_library(bacuri-vms-verify STATIC verify.cpp)
target_link_libraries(bacuri-vms-verify PUBLIC bacuri-vms-manifest PRIVATE bacuri-vms-mp4 PkgConfig::SODIUM)
add_library(bacuri-vms-analysis STATIC analysis.cpp)
target_link_libraries(bacuri-vms-analysis PUBLIC bacuri-vms-manifest PRIVATE PkgConfig::GST)
add_library(bacuri-vms-volume STATIC volume.cpp replica.cpp recording_policy.cpp)
target_include_directories(bacuri-vms-volume PRIVATE "${generated_dir}")
target_link_libraries(bacuri-vms-volume PUBLIC bacuri-vms-manifest bacuri-vms-mp4 bacuri-vms-platform bacuri-vms-metrics bacuri-vms-sqlite PkgConfig::GIO
    PRIVATE Boost::headers)
add_library(bacuri-vms-evidence STATIC evidence.cpp)
target_link_libraries(bacuri-vms-evidence PUBLIC bacuri-vms-verify bacuri-vms-volume bacuri-vms-platform)
add_executable(bacuri-vms-verify-export verify_export.cpp)
target_link_libraries(bacuri-vms-verify-export PRIVATE bacuri-vms-verify PkgConfig::SODIUM)
add_library(bacuri-vms-fmp4 STATIC fmp4.cpp)
target_include_directories(bacuri-vms-fmp4 PUBLIC "${PROJECT_SOURCE_DIR}/src")
add_library(bacuri-vms-archive STATIC writer.cpp delivery.cpp)
target_include_directories(bacuri-vms-archive PRIVATE "${generated_dir}")
target_link_libraries(bacuri-vms-archive PUBLIC bacuri-vms-analysis bacuri-vms-volume bacuri-vms-ingest bacuri-vms-sqlite
    PRIVATE bacuri-vms-fmp4 bacuri-vms-rpc PkgConfig::GST Boost::headers)

if(BUILD_TESTING)
    add_executable(bacuri-vms-archive-test "${PROJECT_SOURCE_DIR}/tests/archive/volume.cpp" "${PROJECT_SOURCE_DIR}/tests/archive/recording_policy.cpp"
        "${PROJECT_SOURCE_DIR}/tests/archive/writer.cpp" "${PROJECT_SOURCE_DIR}/tests/archive/fmp4.cpp")
    target_link_libraries(bacuri-vms-archive-test PRIVATE bacuri-vms-archive bacuri-vms-fmp4 PkgConfig::GST GTest::gtest_main)
    if(CMAKE_CXX_FLAGS MATCHES "-fsanitize=thread")
        set(codec_test "RecordingPolicy.ArchiveDecodesNumberedSamplesAcrossTheRepeatedHour:ArchiveWriter.*:FragmentedMp4.*")
        gtest_discover_tests(bacuri-vms-archive-test DISCOVERY_MODE PRE_TEST TEST_FILTER "-${codec_test}")
        # The native media pipeline transfers buffer ownership inside distribution libraries built without TSan.
        gtest_discover_tests(bacuri-vms-archive-test DISCOVERY_MODE PRE_TEST TEST_FILTER "${codec_test}"
            PROPERTIES ENVIRONMENT_MODIFICATION "TSAN_OPTIONS=string_append::ignore_noninstrumented_modules=1")
    else()
        gtest_discover_tests(bacuri-vms-archive-test DISCOVERY_MODE PRE_TEST)
    endif()
endif()
