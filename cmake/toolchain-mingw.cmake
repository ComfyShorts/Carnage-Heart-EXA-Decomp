# Windows build cross-compiled with MinGW-w64 (docs/windows.md); tools/build_windows.sh
# puts SDL3 and FFmpeg into .deps/.
#
# 32-bit (i686) like the m32 Linux build: the decompiled structs keep the PSP's ILP32
# layout and the GE's display lists carry 32-bit host addresses (src/platform/ge.c), so
# a 64-bit exe would draw nothing.

set(CMAKE_SYSTEM_NAME Windows)
set(PCPORT_MINGW_ARCH i686)
set(CMAKE_SYSTEM_PROCESSOR ${PCPORT_MINGW_ARCH})
set(_triple ${PCPORT_MINGW_ARCH}-w64-mingw32)
set(CMAKE_C_COMPILER ${_triple}-gcc)
set(CMAKE_CXX_COMPILER ${_triple}-g++)
set(CMAKE_RC_COMPILER ${_triple}-windres)

get_filename_component(_deps "${CMAKE_CURRENT_LIST_DIR}/../.deps" ABSOLUTE)
file(GLOB _sdl LIST_DIRECTORIES true "${_deps}/sdl3-mingw/SDL3-*/${_triple}")
set(CMAKE_FIND_ROOT_PATH /usr/${_triple} ${_sdl} "${_deps}/ffmpeg-mingw-${PCPORT_MINGW_ARCH}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# No libgcc DLL to ship; the win32-thread GCC needs no winpthread DLL either.
# --large-address-aware: the 32-bit exe may use up to 4 GB on 64-bit Windows.
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static-libgcc -Wl,--large-address-aware")
