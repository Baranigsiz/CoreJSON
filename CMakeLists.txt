cmake_minimum_required(VERSION 3.14)
project(SenkoJSON VERSION 2.6.0 LANGUAGES CXX)

# ==============================================================================
# C++ Standard & Compiler Flags
# ==============================================================================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Option to build tests and examples
option(SENKO_BUILD_TESTS "Build SenkoJSON unit tests" ON)
option(SENKO_BUILD_BENCHMARKS "Build SenkoJSON benchmarks" ON)
option(SENKO_BUILD_EXAMPLES "Build SenkoJSON examples" ON)

# ==============================================================================
# Core Library Target (Header-Only Interface)
# ==============================================================================
add_library(senko INTERFACE)
add_library(senko::senko ALIAS senko)
add_library(SenkoJSON::SenkoJSON ALIAS senko)

target_include_directories(senko INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(senko INTERFACE cxx_std_17)

# ==============================================================================
# Main Application
# ==============================================================================
add_executable(senko_app src/main.cpp)
target_link_libraries(senko_app PRIVATE senko::senko)

# ==============================================================================
# Examples
# ==============================================================================
if(SENKO_BUILD_EXAMPLES)
    add_executable(example_01_basic_usage examples/01_basic_usage.cpp)
    target_link_libraries(example_01_basic_usage PRIVATE senko::senko)

    add_executable(example_02_struct_serialization examples/02_struct_serialization.cpp)
    target_link_libraries(example_02_struct_serialization PRIVATE senko::senko)

    add_executable(example_03_json_pointer examples/03_json_pointer.cpp)
    target_link_libraries(example_03_json_pointer PRIVATE senko::senko)

    add_executable(example_04_binary_msgpack_cbor examples/04_binary_msgpack_cbor.cpp)
    target_link_libraries(example_04_binary_msgpack_cbor PRIVATE senko::senko)

    add_executable(example_05_jsonpath_queries examples/05_jsonpath_queries.cpp)
    target_link_libraries(example_05_jsonpath_queries PRIVATE senko::senko)

    add_executable(example_06_json_patch_and_diff examples/06_json_patch_and_diff.cpp)
    target_link_libraries(example_06_json_patch_and_diff PRIVATE senko::senko)

    add_executable(example_07_iterators_and_file_io examples/07_iterators_and_file_io.cpp)
    target_link_libraries(example_07_iterators_and_file_io PRIVATE senko::senko)

    add_executable(example_08_json_merge_patch examples/08_json_merge_patch.cpp)
    target_link_libraries(example_08_json_merge_patch PRIVATE senko::senko)

    add_executable(example_09_streaming_sax_parser examples/09_streaming_sax_parser.cpp)
    target_link_libraries(example_09_streaming_sax_parser PRIVATE senko::senko)

    add_executable(example_10_jsonc_configuration examples/10_jsonc_configuration.cpp)
    target_link_libraries(example_10_jsonc_configuration PRIVATE senko::senko)

    add_executable(example_11_jsonl_streaming examples/11_jsonl_streaming.cpp)
    target_link_libraries(example_11_jsonl_streaming PRIVATE senko::senko)
endif()

# ==============================================================================
# Tests
# ==============================================================================
if(SENKO_BUILD_TESTS)
    enable_testing()

    add_executable(senko_tests
        tests/test_main.cpp
        tests/test_primitives.cpp
        tests/test_objects_arrays.cpp
        tests/test_unicode.cpp
        tests/test_errors.cpp
        tests/test_json_pointer.cpp
        tests/test_struct_binding.cpp
        tests/test_stl_containers.cpp
        tests/test_patch.cpp
        tests/test_binary.cpp
        tests/test_jsonpath.cpp
        tests/test_iterators.cpp
        tests/test_file_io.cpp
        tests/test_schema.cpp
        tests/test_sax.cpp
        tests/test_jsonc_jsonl.cpp
        tests/test_fuzz_robustness.cpp
    )
    target_link_libraries(senko_tests PRIVATE senko::senko)
    target_include_directories(senko_tests PRIVATE tests)

    add_test(NAME SenkoJSON_UnitTests COMMAND senko_tests)
endif()

# ==============================================================================
# Benchmarks
# ==============================================================================
if(SENKO_BUILD_BENCHMARKS)
    add_executable(senko_benchmarks benchmarks/benchmark_suite.cpp)
    target_link_libraries(senko_benchmarks PRIVATE senko::senko)

    add_executable(senko_comparative_proof benchmarks/comparative_proof.cpp)
    target_link_libraries(senko_comparative_proof PRIVATE senko::senko)
endif()

# ==============================================================================
# Installation & CMake Packaging
# ==============================================================================
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS senko
    EXPORT SenkoJSONTargets
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY include/senko DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY single_include/senko DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/single_include OPTIONAL)

install(EXPORT SenkoJSONTargets
    FILE SenkoJSONTargets.cmake
    NAMESPACE senko::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SenkoJSON
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/SenkoJSONConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/SenkoJSONConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SenkoJSON
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/SenkoJSONConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/SenkoJSONConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/SenkoJSONConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SenkoJSON
)