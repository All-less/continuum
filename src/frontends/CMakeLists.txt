cmake_minimum_required(VERSION 3.2 FATAL_ERROR)

##################################
# Data REST frontend executable

add_executable(data_frontend src/data_frontend_main.cpp)

target_link_libraries(data_frontend continuum boost httpserver cxxopts rapidjson)

cotire(data_frontend)

##################################
# Tests for frontends

add_executable(frontendtests EXCLUDE_FROM_ALL
    src/frontend_tests.cpp
    src/data_frontend_tests.cpp
    )
target_link_libraries(frontendtests continuum boost httpserver gtest gmock_main rapidjson cxxopts)
cotire(frontendtests)
add_dependencies(unittests frontendtests)
