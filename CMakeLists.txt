cmake_minimum_required(VERSION 3.20)
project(chexa_pc LANGUAGES C)

# Default to a 32-bit build so struct layouts match the PSP (ILP32). Configure with
#   cmake --preset m32   (or -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-m32.cmake)
# A 64-bit build (no toolchain file) also works for the platform layer, but decompiled
# game structs will only be checked against the 32-bit layout.

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug)
endif()

# SDL3: the 32-bit static build from tools/setup_pc_deps.sh, or a system SDL3 for 64-bit.
list(PREPEND CMAKE_PREFIX_PATH "${CMAKE_SOURCE_DIR}/.deps/sdl3-m32")
find_package(SDL3 3.2 REQUIRED CONFIG)

file(GLOB GAME_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/game/*.c")
file(GLOB PLATFORM_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/platform/*.c")

function(chexa_target_setup target)
  target_include_directories(${target} PRIVATE src/platform src/game)
  target_compile_options(${target} PRIVATE -Wall -Wextra -Wno-unused-parameter)
  if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    target_compile_definitions(${target} PRIVATE PCPORT_M32=1)
  endif()
  # 64-bit off_t/ino_t/time_t even in the 32-bit build: without them stat() fails with
  # EOVERFLOW on file systems with large inode numbers (e.g. WSL's /mnt/c).
  target_compile_definitions(${target} PRIVATE _FILE_OFFSET_BITS=64 _TIME_BITS=64)
endfunction()

# PSP shim + decompiled game code, shared by the game and the tests.
add_library(chexa_core STATIC ${PLATFORM_SOURCES} ${GAME_SOURCES})
chexa_target_setup(chexa_core)
if(TARGET SDL3::SDL3-static)
  target_link_libraries(chexa_core PUBLIC SDL3::SDL3-static)
else()
  target_link_libraries(chexa_core PUBLIC SDL3::SDL3)
endif()

add_executable(chexa-pc src/pc/main.c)
chexa_target_setup(chexa-pc)
target_link_libraries(chexa-pc PRIVATE chexa_core)

add_executable(chexa-pgchip-test tests/pgchip_test.c)
chexa_target_setup(chexa-pgchip-test)
target_link_libraries(chexa-pgchip-test PRIVATE chexa_core)
add_executable(chexa-battle-test tests/battle_test.c)
chexa_target_setup(chexa-battle-test)
target_link_libraries(chexa-battle-test PRIVATE chexa_core)

add_executable(chexa-io-test tests/io_test.c)
chexa_target_setup(chexa-io-test)
target_link_libraries(chexa-io-test PRIVATE chexa_core)

add_executable(chexa-gu-test tests/gu_test.c)
chexa_target_setup(chexa-gu-test)
target_link_libraries(chexa-gu-test PRIVATE chexa_core)

# Tests that need the game's files are skipped (exit 77) when they aren't found.
# Point them at your dump with CHEXA_DISC=/path/to/disc-root.
enable_testing()
add_test(NAME io_disc COMMAND chexa-io-test)
add_test(NAME gu_render COMMAND chexa-gu-test)
add_test(NAME pgchip_one_record COMMAND chexa-pgchip-test --ticks 20 -q)
add_test(NAME pgchip_all_records COMMAND chexa-pgchip-test --all --ticks 600)
add_test(NAME battle_presets COMMAND chexa-battle-test --all
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
# The game's start-up path into the main loop: 30 frames drawn, then the HOME-menu exit.
set(CHEXA_TEST_DISC "$ENV{CHEXA_DISC}")
if(NOT CHEXA_TEST_DISC)
  set(CHEXA_TEST_DISC /mnt/c/psp/chexa)
endif()
add_test(NAME startup_path COMMAND chexa-pc --disc ${CHEXA_TEST_DISC} --startup --frames 30)
set_tests_properties(startup_path PROPERTIES ENVIRONMENT "SDL_VIDEO_DRIVER=offscreen"
                     PASS_REGULAR_EXPRESSION "the game called sceKernelExitGame")
set_tests_properties(io_disc pgchip_one_record pgchip_all_records battle_presets startup_path PROPERTIES SKIP_RETURN_CODE 77)

# SDL loads libX11 and friends at runtime with dlopen. With the user-local sysroot those
# are 32-bit libraries under .deps, so bake their directory into the binary. DT_RPATH (not
# RUNPATH) also covers their own dependencies, such as libxcb.
if(PCPORT_I386_LIBDIR)
  foreach(exe chexa-pc chexa-pgchip-test chexa-io-test chexa-gu-test chexa-battle-test)
    set_target_properties(${exe} PROPERTIES BUILD_RPATH "${PCPORT_I386_LIBDIR}")
    target_link_options(${exe} PRIVATE "-Wl,--disable-new-dtags")
  endforeach()
endif()
