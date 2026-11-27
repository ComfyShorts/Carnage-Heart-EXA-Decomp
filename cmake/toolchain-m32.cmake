# 32-bit x86 Linux build (pointers and longs are 4 bytes, as on the PSP).
#
# Works with a system multilib install (gcc-multilib) or with the user-local sysroot that
# tools/setup_pc_deps.sh unpacks into .deps/sysroot (no root needed). The sysroot only
# adds the 32-bit crt files, libgcc and stubs-32.h on top of the host's headers.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR i686)

set(PCPORT_SYSROOT "${CMAKE_CURRENT_LIST_DIR}/../.deps/sysroot" CACHE PATH
    "User-local 32-bit sysroot from tools/setup_pc_deps.sh")
get_filename_component(PCPORT_SYSROOT "${PCPORT_SYSROOT}" ABSOLUTE)

set(_m32 "-m32")
set(_m32_link "-m32")
if(EXISTS "${PCPORT_SYSROOT}/usr/lib32/crt1.o")
  file(GLOB _gccdir LIST_DIRECTORIES true "${PCPORT_SYSROOT}/usr/lib/gcc/x86_64-linux-gnu/*/32")
  list(GET _gccdir 0 _gccdir)
  string(APPEND _m32
    " -idirafter /usr/include/x86_64-linux-gnu"
    " -idirafter ${PCPORT_SYSROOT}/usr/include/x86_64-linux-gnu")
  string(APPEND _m32_link
    " -B${PCPORT_SYSROOT}/usr/lib32/ -B${_gccdir}/"
    " -L${PCPORT_SYSROOT}/usr/lib32 -L${_gccdir}"
    " -L${PCPORT_SYSROOT}/usr/lib/i386-linux-gnu"
    " -Wl,-rpath-link,${PCPORT_SYSROOT}/usr/lib/i386-linux-gnu")
  # 32-bit runtime libraries unpacked from Ubuntu's i386 packages (libX11 and friends).
  set(PCPORT_I386_LIBDIR "${PCPORT_SYSROOT}/usr/lib/i386-linux-gnu")
endif()

set(CMAKE_C_FLAGS_INIT "${_m32}")
set(CMAKE_CXX_FLAGS_INIT "${_m32}")
set(CMAKE_ASM_FLAGS_INIT "${_m32}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${_m32_link}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_m32_link}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${_m32_link}")

set(CMAKE_LIBRARY_ARCHITECTURE i386-linux-gnu)
set(CMAKE_FIND_ROOT_PATH "${PCPORT_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
