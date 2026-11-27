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

add_executable(chexa-pc src/pc/main.c ${PLATFORM_SOURCES} ${GAME_SOURCES})
target_include_directories(chexa-pc PRIVATE src/platform src/game)
target_compile_options(chexa-pc PRIVATE -Wall -Wextra -Wno-unused-parameter)

if(CMAKE_SIZEOF_VOID_P EQUAL 4)
  target_compile_definitions(chexa-pc PRIVATE PCPORT_M32=1)
endif()

if(TARGET SDL3::SDL3-static)
  target_link_libraries(chexa-pc PRIVATE SDL3::SDL3-static)
else()
  target_link_libraries(chexa-pc PRIVATE SDL3::SDL3)
endif()

# SDL loads libX11 and friends at runtime with dlopen. With the user-local sysroot those
# are 32-bit libraries under .deps, so bake their directory into the binary. DT_RPATH (not
# RUNPATH) also covers their own dependencies, such as libxcb.
if(PCPORT_I386_LIBDIR)
  set_target_properties(chexa-pc PROPERTIES BUILD_RPATH "${PCPORT_I386_LIBDIR}")
  target_link_options(chexa-pc PRIVATE "-Wl,--disable-new-dtags")
endif()
