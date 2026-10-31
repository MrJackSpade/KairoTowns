cmake_minimum_required(VERSION 3.22.1)
project(kairotowns LANGUAGES C CXX)

get_filename_component(KAIROTOWNS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../.." ABSOLUTE)
include("${KAIROTOWNS_ROOT}/shared/cmake/KairoMachine.cmake")

# The Tsugaru FM Towns core, driven one frame at a time (core/).
add_subdirectory("${KAIROTOWNS_ROOT}/core" core)

kairo_machine_library(kairotowns towns_core.cpp)
# libkairo_chd.so, through which the library hashes CDs kept as CHDs (KairoTowns#97).
kairo_chd_jni_library()
target_link_libraries(kairotowns PRIVATE kairotowns_core)
