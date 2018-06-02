# Define library. Only source files here!
cmake_minimum_required(VERSION 3.2 FATAL_ERROR)
project(libcontinuum VERSION 0.1.0 LANGUAGES CXX)


add_library(continuum
    src/data_processor.cpp
    src/datatypes.cpp
    src/metrics.cpp
    src/retrain_policies.cpp
    src/persistent_state.cpp
    src/rpc_backend_service.cpp
    src/selection_policies.cpp
    src/timers.cpp
    src/redis.cpp
    src/logging.cpp
    src/json_util.cpp
    src/exceptions.cpp)


# Define headers for this library. PUBLIC headers are used for
# compiling the library, and will be added to consumers' build
# paths.
target_include_directories(continuum PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
    PRIVATE src)


target_link_libraries(continuum boost folly zmqcpp redox pthread rapidjson spdlog base64)

# This makes the project importable from the build directory
export(TARGETS continuum FILE ContinuumConfig.cmake)


# config_test resets the value of the config so it should always
# be last in the list
add_executable(libcontinuumtests EXCLUDE_FROM_ALL
    test/test_main.cpp
    test/metrics_test.cpp
    test/timers_test.cpp
    test/data_serialization_tests.cpp
    test/persistent_state_test.cpp
    test/redis_test.cpp
    test/json_util_test.cpp
    test/logging_test.cpp
    test/config_test.cpp
    test/base64_test.cpp)

target_link_libraries(libcontinuumtests continuum gtest gmock_main cxxopts)
add_dependencies(unittests libcontinuumtests)

