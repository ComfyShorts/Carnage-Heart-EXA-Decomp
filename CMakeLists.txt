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
# Optimized by default: the GE is a software rasterizer, and a Debug build (no -O)
# can't hold 60 frames a second (docs/graphics.md, "Performance"). RelWithDebInfo
# keeps the symbols; configure with -DCMAKE_BUILD_TYPE=Debug (or the m32-debug preset)
# to step through the code.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  message(STATUS "Debug build: unoptimized, expect well under 60 frames a second")
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
    # i386 code defaults to x87 floating point, where every float-to-int conversion
    # reloads the FPU control word and results depend on register spills. Every
    # x86-64 CPU, which the 32-bit build targets, has SSE2.
    target_compile_options(${target} PRIVATE -msse2 -mfpmath=sse)
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

# libavcodec (FFmpeg, LGPL-2.1-or-later) decodes the ATRAC3plus music, linked dynamically
# from the system (for the 32-bit build: tools/setup_pc_deps.sh --libavcodec). Without
# it the port builds and runs with sound effects only.
option(CHEXA_LIBAVCODEC "Use the system libavcodec for ATRAC3plus (music)" ON)
if(CHEXA_LIBAVCODEC)
  find_path(AVCODEC_INCLUDE_DIR libavcodec/avcodec.h)
  find_library(AVCODEC_LIBRARY avcodec)
  find_library(AVUTIL_LIBRARY avutil)
endif()
if(CHEXA_LIBAVCODEC AND AVCODEC_INCLUDE_DIR AND AVCODEC_LIBRARY AND AVUTIL_LIBRARY)
  message(STATUS "ATRAC3plus: libavcodec ${AVCODEC_LIBRARY}")
  target_include_directories(chexa_core SYSTEM BEFORE PRIVATE ${AVCODEC_INCLUDE_DIR})
  target_compile_definitions(chexa_core PRIVATE CHEXA_HAVE_LIBAVCODEC=1)
  target_link_libraries(chexa_core PUBLIC ${AVCODEC_LIBRARY} ${AVUTIL_LIBRARY})
else()
  message(STATUS "ATRAC3plus: no libavcodec, the music won't play")
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

add_executable(chexa-savedata-test tests/savedata_test.c)
chexa_target_setup(chexa-savedata-test)
target_link_libraries(chexa-savedata-test PRIVATE chexa_core)

add_executable(chexa-oke-parts-test tests/oke_parts_test.c)
chexa_target_setup(chexa-oke-parts-test)
target_link_libraries(chexa-oke-parts-test PRIVATE chexa_core)

add_executable(chexa-sound-test tests/sound_test.c)
chexa_target_setup(chexa-sound-test)
target_link_libraries(chexa-sound-test PRIVATE chexa_core)

add_executable(chexa-movie-test tests/movie_test.c)
chexa_target_setup(chexa-movie-test)
target_link_libraries(chexa-movie-test PRIVATE chexa_core)

add_executable(chexa-gu-test tests/gu_test.c)
chexa_target_setup(chexa-gu-test)
target_link_libraries(chexa-gu-test PRIVATE chexa_core)

# Tests that need the game's files are skipped (exit 77) when they aren't found.
# Point them at your dump with CHEXA_DISC=/path/to/disc-root.
enable_testing()
add_test(NAME io_disc COMMAND chexa-io-test)
add_test(NAME gu_render COMMAND chexa-gu-test)
add_test(NAME savedata_roundtrip COMMAND chexa-savedata-test ${CMAKE_CURRENT_BINARY_DIR}/test-saves/unit)
add_test(NAME pgchip_one_record COMMAND chexa-pgchip-test --ticks 20 -q)
add_test(NAME pgchip_all_records COMMAND chexa-pgchip-test --all --ticks 600)
add_test(NAME battle_presets COMMAND chexa-battle-test --all
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
add_test(NAME battle_teams COMMAND chexa-battle-test --teams
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
add_test(NAME oke_parts COMMAND chexa-oke-parts-test
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
add_test(NAME sound_effects COMMAND chexa-sound-test
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
add_test(NAME movie_decode COMMAND chexa-movie-test)
add_test(NAME music_decode COMMAND chexa-sound-test --music
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
# The game's start-up path into the main loop: 30 frames drawn, then the HOME-menu exit.
set(CHEXA_TEST_DISC "$ENV{CHEXA_DISC}")
if(NOT CHEXA_TEST_DISC)
  set(CHEXA_TEST_DISC /mnt/c/psp/chexa)
endif()
add_test(NAME startup_path COMMAND chexa-pc --disc ${CHEXA_TEST_DISC} --startup --skip-movie --frames 30
         --saves ${CMAKE_CURRENT_BINARY_DIR}/test-saves/startup)
# The opening movie plays until circle stops it, as on the PSP.
add_test(NAME movie_skip COMMAND chexa-pc --disc ${CHEXA_TEST_DISC} --startup --press 40 circle --frames 80
         --saves ${CMAKE_CURRENT_BINARY_DIR}/test-saves/movie)
set_tests_properties(movie_skip PROPERTIES ENVIRONMENT "SDL_VIDEO_DRIVER=offscreen;SDL_AUDIO_DRIVER=dummy"
                     PASS_REGULAR_EXPRESSION "movie/che_op.pmf skipped after [0-9]+ pictures")
set_tests_properties(startup_path PROPERTIES ENVIRONMENT "SDL_VIDEO_DRIVER=offscreen;SDL_AUDIO_DRIVER=dummy"
                     PASS_REGULAR_EXPRESSION "the game called sceKernelExitGame")
set_tests_properties(io_disc pgchip_one_record pgchip_all_records battle_presets battle_teams oke_parts sound_effects music_decode movie_decode startup_path movie_skip PROPERTIES SKIP_RETURN_CODE 77)

# SDL loads libX11 and friends at runtime with dlopen. With the user-local sysroot those
# are 32-bit libraries under .deps, so bake their directory into the binary. DT_RPATH (not
# RUNPATH) also covers their own dependencies, such as libxcb.
if(PCPORT_I386_LIBDIR)
  # libpulse.so.0 finds its private libpulsecommon through its own RUNPATH, a system
  # path; audio.c loads it from here first (see psp_audio_preload_pulse).
  target_compile_definitions(chexa_core PRIVATE PCPORT_I386_LIBDIR="${PCPORT_I386_LIBDIR}")
  foreach(exe chexa-pc chexa-pgchip-test chexa-io-test chexa-gu-test chexa-battle-test chexa-savedata-test chexa-sound-test chexa-movie-test)
    set_target_properties(${exe} PROPERTIES BUILD_RPATH "${PCPORT_I386_LIBDIR}")
    target_link_options(${exe} PRIVATE "-Wl,--disable-new-dtags")
  endforeach()
endif()
