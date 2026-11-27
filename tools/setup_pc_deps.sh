#!/usr/bin/env bash
# Fetch and build what the 32-bit PC port needs, without root, into .deps/ (git-ignored).
#
#  1. Ubuntu's multilib dev packages (32-bit crt files, libgcc, stubs-32.h) and X11
#     headers, fetched with `apt-get download` and unpacked into .deps/sysroot.
#  2. 32-bit X11 runtime libraries from Ubuntu's i386 archive. apt uses a private
#     package list under .deps/apt-i386, so i386 doesn't need to be enabled system-wide.
#  3. SDL3 built from source as a static 32-bit library into .deps/sdl3-m32, with X11
#     (loaded at runtime), dummy audio and no Wayland/PulseAudio.
#
# Needs: apt-get, dpkg-deb, curl, cmake, ninja, gcc (all present on stock Ubuntu with
# build-essential, cmake and ninja-build). Tested on Ubuntu 26.04 under WSL2.
# With gcc-multilib and the i386 X11 packages installed system-wide, step 1-2 aren't needed.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
DEPS=$ROOT/.deps
SYSROOT=$DEPS/sysroot
SDL_VER=3.4.2
SDL_SHA256=ef39a2e3f9a8a78296c40da701967dd1b0d0d6e267e483863ce70f8a03b4050c

GCC_MAJOR=$(gcc -dumpversion | cut -d. -f1)
DEV_PKGS=(libc6-dev-i386 "lib32gcc-${GCC_MAJOR}-dev" libx11-dev x11proto-dev libxext-dev
          libxrandr-dev libxrender-dev libxi-dev libxfixes-dev libxcursor-dev)
I386_PKGS=(libx11-6 libxcb1 libxau6 libxdmcp6 libbsd0 libmd0 libxext6 libxrandr2
           libxrender1 libxi6 libxfixes3 libxcursor1)

mkdir -p "$DEPS/debs/amd64" "$DEPS/debs/i386" "$SYSROOT" "$DEPS/src" "$DEPS/build"

echo "== multilib dev packages"
(cd "$DEPS/debs/amd64" && apt-get download "${DEV_PKGS[@]}")

echo "== i386 runtime libraries (private apt index)"
APT_D=$DEPS/apt-i386
mkdir -p "$APT_D/lists/partial" "$APT_D/cache/archives/partial"
APT_OPTS=(-o "Dir::State::Lists=$APT_D/lists" -o "Dir::Cache=$APT_D/cache"
          -o APT::Architecture=i386 -o APT::Architectures::=i386
          -o Debug::NoLocking=1 -o "APT::Sandbox::User=$(id -un)")
apt-get "${APT_OPTS[@]}" update >/dev/null
(cd "$DEPS/debs/i386" && apt-get "${APT_OPTS[@]}" download "${I386_PKGS[@]}")

echo "== unpacking into $SYSROOT"
for deb in "$DEPS"/debs/*/*.deb; do dpkg-deb -x "$deb" "$SYSROOT"; done
# libc.so is a linker script with absolute paths; point the static part at the sysroot.
sed -i "s# /usr/lib32/libc_nonshared.a# $SYSROOT/usr/lib32/libc_nonshared.a#" "$SYSROOT/usr/lib32/libc.so"
# Runtime packages ship only libfoo.so.N; add the libfoo.so names the build looks for.
LIBDIR=$SYSROOT/usr/lib/i386-linux-gnu
for so in "$LIBDIR"/lib*.so.[0-9]; do ln -sf "$(basename "$so")" "${so%.so.*}.so"; done
# libc6-dev-i386's libm.so (and friends) link to libm.so.6 in the same directory, which
# the runtime package installs under the system's /usr/lib32 instead. Point dangling links
# there, or -lm silently falls back to the static libm.a, which doesn't link.
for so in "$SYSROOT"/usr/lib32/lib*.so; do
  if [ -L "$so" ] && [ ! -e "$so" ] && [ -e "/usr/lib32/$(readlink "$so")" ]; then
    ln -sf "/usr/lib32/$(readlink "$so")" "$so"
  fi
done

echo "== SDL3 $SDL_VER"
TARBALL=$DEPS/src/SDL3-$SDL_VER.tar.gz
if [ ! -f "$TARBALL" ]; then
  curl -fsSL -o "$TARBALL" "https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VER/SDL3-$SDL_VER.tar.gz"
fi
echo "$SDL_SHA256  $TARBALL" | sha256sum -c -
rm -rf "$DEPS/src/SDL3-$SDL_VER" "$DEPS/build/sdl3-m32"
tar -xzf "$TARBALL" -C "$DEPS/src"
cmake -S "$DEPS/src/SDL3-$SDL_VER" -B "$DEPS/build/sdl3-m32" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/toolchain-m32.cmake" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$DEPS/sdl3-m32" \
  -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF \
  -DSDL_X11=ON -DSDL_X11_SHARED=ON -DX11_INCLUDEDIR="$SYSROOT/usr/include" \
  -DSDL_X11_XSCRNSAVER=OFF -DSDL_X11_XTEST=OFF -DSDL_X11_XSHAPE=OFF -DSDL_X11_XSYNC=OFF -DSDL_X11_XDBE=OFF \
  -DSDL_WAYLAND=OFF -DSDL_KMSDRM=OFF -DSDL_VULKAN=OFF -DSDL_RENDER_VULKAN=OFF \
  -DSDL_PULSEAUDIO=OFF -DSDL_PIPEWIRE=OFF -DSDL_ALSA=OFF -DSDL_JACK=OFF -DSDL_SNDIO=OFF \
  -DSDL_LIBURING=OFF -DSDL_DBUS=OFF -DSDL_IBUS=OFF -DSDL_HIDAPI_LIBUSB=OFF >/dev/null
ninja -C "$DEPS/build/sdl3-m32" install >/dev/null

echo "== done. Build the port with: cmake --preset m32 && cmake --build --preset m32"
