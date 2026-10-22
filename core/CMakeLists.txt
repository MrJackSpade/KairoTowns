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

if(NOT ANDROID)
    add_executable(boot_test test/boot_test.cpp)
    target_link_libraries(boot_test PRIVATE kairotowns_core)
    if(WIN32)
        target_link_options(boot_test PRIVATE -static)
    endif()
endif()
