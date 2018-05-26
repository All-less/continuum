cmake_minimum_required(VERSION 3.2 FATAL_ERROR)
project("Continuum")

# Targets that we develop
add_subdirectory(libcontinuum)
add_subdirectory(frontends)
add_subdirectory(management)
add_subdirectory(libs)
