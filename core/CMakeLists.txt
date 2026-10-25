# The FM Towns machine (Tsugaru, third_party/tsugaru) driven one guest frame at a time
# (towns_machine.h), for the Android app and for host tests.
cmake_minimum_required(VERSION 3.22.1)
project(kairotowns_core LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_filename_component(TSUGARU "${CMAKE_CURRENT_LIST_DIR}/../third_party/tsugaru/src" ABSOLUTE)

# Tsugaru's own build (src/CMakeLists.txt) sets these for every target.
add_compile_definitions(YS_LITTLE_ENDIAN YS_TWOS_COMPLEMENT)
add_compile_options(-Wno-unused-variable -Wno-deprecated-declarations -Wno-undefined-inline)
# Tsugaru is written for x86, where plain char is signed; ARM compilers make it unsigned.
add_compile_options(-fsigned-char)
# The emulator runs in real time only optimized, so the core is optimized in debug builds too;
# without this a debug APK compiles every Tsugaru library at -O0.
add_compile_options(-O3)
# Scheduled for the Cortex-A55 (KairoTowns#12): 2-3% less time a frame on the RG DS than generic
# scheduling, with the instruction set unchanged (ARMv8.0), so it runs on every arm64 device. -O2
# was slower, -mcpu=cortex-a55 (ARMv8.2) no faster than this.
if(ANDROID)
    add_compile_options(-mtune=cortex-a55)
endif()
# The device build of the core uses a profile of the device playing games (KairoTowns#9,
# core/pgo/README.md). The profile describes this exact code as the device compiles it, so a
# training build (KAIROTOWNS_PGO_GENERATE), an experiment on the core or another compiler turns it
# off with -DKAIRO_PGO=OFF. Set before Tsugaru's libraries are added, so they all use it.
if(ANDROID)
    set(KAIROTOWNS_PGO_DEFAULT ON)
else()
    set(KAIROTOWNS_PGO_DEFAULT OFF)
endif()
option(KAIRO_PGO "Build the core with its recorded profile (core/pgo)" ${KAIROTOWNS_PGO_DEFAULT})
option(KAIROTOWNS_PGO_GENERATE "Build the core to record a profile (tools/perf/pgo-train.ps1)" OFF)
if(KAIRO_PGO)
    if(KAIROTOWNS_PGO_GENERATE)
        message(FATAL_ERROR "The recorded profile is for the ordinary core: add -DKAIRO_PGO=OFF to a KAIROTOWNS_PGO_GENERATE build")
    endif()
    include("${CMAKE_CURRENT_LIST_DIR}/../shared/cmake/KairoPgo.cmake")
    kairo_use_profile("${CMAKE_CURRENT_LIST_DIR}/pgo/arm64.proftext" "${CMAKE_CURRENT_LIST_DIR}/pgo/profile.json"
        "${CMAKE_CURRENT_LIST_DIR}/..")
endif()
if(KAIROTOWNS_PGO_GENERATE)
    add_compile_options(-fprofile-generate)
    add_link_options(-fprofile-generate)
endif()
# On Windows Tsugaru marks unreachable code with MSVC's __assume, which clang accepts as an
# extension; the host test builds there with llvm-mingw.
if(WIN32)
    add_compile_options(-fms-extensions)
endif()

# Some Tsugaru files use std::pow and rand without including <cmath> and <cstdlib>, which
# libc++ does not include for them.
add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:SHELL:-include cmath>" "$<$<COMPILE_LANGUAGE:CXX>:SHELL:-include cstdlib>")

# Tsugaru links pthread on Unix; Android's threads are in its C library, with no library to link.
if(ANDROID)
    add_library(pthread INTERFACE)
endif()

# Only the emulator libraries: Tsugaru's executables, tests and desktop window and sound
# connections are left out, and EXCLUDE_FROM_ALL builds just what the core links.
foreach(dir
        osdependent/gamepad osdependent/midi osdependent/osinit vgmrecorder cpputil lineparser miscutil cheapmath vmbase device cpu inout ramrom
        ym2612 rf5c68 i8251 i8251_to_socket i8253 diskdrive customstick towns discimg diskimg
        externals/d77 externals/yspng externals/yssocket/src main_cui/command main_cui/argv)
    add_subdirectory("${TSUGARU}/${dir}" "tsugaru/${dir}" EXCLUDE_FROM_ALL)
endforeach()

# Tsugaru's file and sound helpers, chosen here for both the host and Android: its own build picks
# stubs for a Windows compiler other than MSVC. Sound output is the core's, so the silent
# backend serves only the WAV data helpers Tsugaru's sound device uses.
set(FILESYS "${TSUGARU}/osdependent/filesys")
add_library(filesys STATIC "${FILESYS}/filesys_common.cpp"
            $<IF:$<BOOL:${WIN32}>,${FILESYS}/filesys_win.cpp,${FILESYS}/filesys_unix.cpp>)
target_include_directories(filesys PUBLIC "${FILESYS}")
target_link_libraries(filesys PUBLIC cpputil miscutil)
set(YSSOUND "${TSUGARU}/externals/yssimplesound/src")
add_library(yssimplesound STATIC "${YSSOUND}/yssimplesound.cpp" "${YSSOUND}/nownd/yssimplesound_nownd.cpp")
target_include_directories(yssimplesound PUBLIC "${YSSOUND}")

add_library(kairotowns_core STATIC towns_machine.cpp)
target_include_directories(kairotowns_core PUBLIC "${CMAKE_CURRENT_LIST_DIR}")
target_link_libraries(kairotowns_core PUBLIC towns townsrender outside_world yssimplesound)

# The measurement tool for the device too, as a standalone executable run through adb
# (tools/perf/device-profile.ps1).
option(KAIROTOWNS_TOOLS "Build game_profile on Android" OFF)
if(ANDROID AND KAIROTOWNS_TOOLS)
    add_executable(game_profile test/game_profile.cpp)
    target_link_libraries(game_profile PRIVATE kairotowns_core)
    target_link_options(game_profile PRIVATE -static-libstdc++)
endif()

if(NOT ANDROID)
    add_executable(boot_test test/boot_test.cpp)
    target_link_libraries(boot_test PRIVATE kairotowns_core)
    add_executable(memory_check_test test/memory_check_test.cpp)
    target_link_libraries(memory_check_test PRIVATE kairotowns_core)
    add_executable(frame_dump test/frame_dump.cpp)
    target_link_libraries(frame_dump PRIVATE kairotowns_core)
    add_executable(game_profile test/game_profile.cpp)
    target_link_libraries(game_profile PRIVATE kairotowns_core)
    if(WIN32)
        target_link_libraries(game_profile PRIVATE winmm)
    endif()
    if(WIN32)
        target_link_options(boot_test PRIVATE -static)
        target_link_options(memory_check_test PRIVATE -static)
        target_link_options(frame_dump PRIVATE -static)
        target_link_options(game_profile PRIVATE -static)
    endif()
endif()
