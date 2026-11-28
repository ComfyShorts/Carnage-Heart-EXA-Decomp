#!/usr/bin/env bash
# Native Windows build of the PC port, cross-compiled from Linux / WSL with MinGW-w64
# (docs/windows.md):
#
#   1. SDL3's official MinGW development package (headers, import library, SDL3.dll)
#      into .deps/sdl3-mingw;
#   2. a minimal FFmpeg (LGPL, shared: avcodec and avutil DLLs with only the H.264 and
#      ATRAC3plus decoders the opening movie and the music use) built from source into
#      .deps/ffmpeg-mingw-i686;
#   3. chexa-pc.exe and the tests with the "windows" CMake preset, into build/win32;
#   4. dist/chexa-pc-windows/: the exe, the DLLs it needs, run.bat, the licences.
#
#   tools/build_windows.sh [--deps-only] [--no-package] [--jobs N]
#
# The build is 32-bit (i686), like the m32 Linux build: the port needs 4-byte pointers
# (see cmake/toolchain-mingw.cmake). It runs on 64-bit Windows.
#
# Needs (apt): mingw-w64 (and nasm for FFmpeg's assembly; without it FFmpeg is built
# with --disable-x86asm), plus curl, xz, cmake, ninja, make. Downloads are checked
# against the SHA-256 sums below. Nothing is installed outside the repository; .deps/,
# build/ and dist/ are gitignored.
set -euo pipefail

ARCH=i686
PRESET=windows
BUILD=build/win32
DEPS_ONLY=0
PACKAGE=1
JOBS=$(nproc 2>/dev/null || echo 4)
while [ $# -gt 0 ]; do
  case "$1" in
    --deps-only) DEPS_ONLY=1 ;;
    --no-package) PACKAGE=0 ;;
    --jobs) JOBS=$2; shift ;;
    -h|--help) sed -n '2,23p' "$0"; exit 0 ;;
    *) echo "usage: $0 [--deps-only] [--no-package] [--jobs N]" >&2; exit 2 ;;
  esac
  shift
done

ROOT=$(cd "$(dirname "$0")/.." && pwd)
DEPS=$ROOT/.deps
DL=$DEPS/downloads
TRIPLE=$ARCH-w64-mingw32
cd "$ROOT"

SDL_VER=3.4.2
SDL_TAR=SDL3-devel-$SDL_VER-mingw.tar.gz
SDL_URL=https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VER/$SDL_TAR
SDL_SHA256=7b8f0c5453ea68e7afc6bacb79c44b8492aa312fce712fb3be5a7b3ada393cb2
FFMPEG_VER=8.1.3
FFMPEG_TAR=ffmpeg-$FFMPEG_VER.tar.xz
FFMPEG_URL=https://ffmpeg.org/releases/$FFMPEG_TAR
# Checked against the release signature (FFmpeg release signing key FCF986EA15E6E293A5644F10B4322F04D67658D8).
FFMPEG_SHA256=7138d28c96d9d3e3af4ee3d8cad72741f8ffb40da90c1112235dea3ecd3178a3

need() {
  command -v "$1" >/dev/null || { echo "missing: $1 ($2)" >&2; exit 1; }
}
need "$TRIPLE-gcc" "sudo apt install mingw-w64"
need curl "sudo apt install curl"
need cmake "sudo apt install cmake"
need ninja "sudo apt install ninja-build"
need make "sudo apt install make"
need xz "sudo apt install xz-utils"

fetch() { # URL FILE SHA256
  mkdir -p "$DL"
  if [ ! -f "$DL/$2" ]; then
    echo "== downloading $1"
    curl -fSL --retry 3 -o "$DL/$2.part" "$1"
    mv "$DL/$2.part" "$DL/$2"
  fi
  echo "$3  $DL/$2" | sha256sum -c --quiet - || { echo "checksum mismatch: $DL/$2 (delete it to download again)" >&2; exit 1; }
}

# ---- 1. SDL3 ----
SDL_DIR=$DEPS/sdl3-mingw/SDL3-$SDL_VER
if [ ! -f "$SDL_DIR/$TRIPLE/bin/SDL3.dll" ]; then
  fetch "$SDL_URL" "$SDL_TAR" "$SDL_SHA256"
  echo "== unpacking SDL3 $SDL_VER (MinGW)"
  rm -rf "$SDL_DIR"
  mkdir -p "$DEPS/sdl3-mingw"
  tar xzf "$DL/$SDL_TAR" -C "$DEPS/sdl3-mingw"
fi
echo "== SDL3: $SDL_DIR/$TRIPLE"

# ---- 2. FFmpeg (avcodec + avutil, two decoders) ----
FF_PREFIX=$DEPS/ffmpeg-mingw-$ARCH
if ! ls "$FF_PREFIX"/bin/avcodec-*.dll >/dev/null 2>&1; then
  fetch "$FFMPEG_URL" "$FFMPEG_TAR" "$FFMPEG_SHA256"
  SRC=$DEPS/src/ffmpeg-$FFMPEG_VER-$ARCH
  rm -rf "$SRC"
  mkdir -p "$DEPS/src"
  echo "== unpacking FFmpeg $FFMPEG_VER"
  tar xJf "$DL/$FFMPEG_TAR" -C "$DEPS/src"
  mv "$DEPS/src/ffmpeg-$FFMPEG_VER" "$SRC"
  ASM=()
  command -v nasm >/dev/null || ASM=(--disable-x86asm)
  echo "== building FFmpeg $FFMPEG_VER for $TRIPLE (LGPL, shared, H.264 + ATRAC3plus decoders)"
  (cd "$SRC" && ./configure \
      --prefix="$FF_PREFIX" --target-os=mingw32 --arch="$ARCH" --cross-prefix="$TRIPLE-" --enable-cross-compile \
      --enable-shared --disable-static --disable-programs --disable-doc --disable-autodetect \
      --disable-everything --enable-decoder=h264 --enable-decoder=atrac3p \
      --disable-avdevice --disable-avformat --disable-avfilter --disable-swscale --disable-swresample \
      --disable-network --enable-w32threads --extra-ldflags=-static-libgcc "${ASM[@]}" >/dev/null &&
    make -j"$JOBS" >/dev/null && make install >/dev/null)
  rm -f "$FF_PREFIX"/bin/*.lib # MSVC import libraries: MinGW links the .dll.a ones
  cp "$SRC/COPYING.LGPLv2.1" "$FF_PREFIX/COPYING.LGPLv2.1"
fi
echo "== FFmpeg: $(ls "$FF_PREFIX"/bin/*.dll | xargs -n1 basename | tr '\n' ' ')"

[ "$DEPS_ONLY" = 1 ] && exit 0

# ---- 3. the port ----
echo "== configuring and building $BUILD (preset $PRESET)"
cmake --preset "$PRESET" >/dev/null
cmake --build --preset "$PRESET" -j "$JOBS"

[ "$PACKAGE" = 1 ] || exit 0

# ---- 4. dist/ ----
DIST=$ROOT/dist/chexa-pc-windows
echo "== packaging $DIST"
rm -rf "$DIST"
mkdir -p "$DIST/licenses"
cp "$BUILD/chexa-pc.exe" "$DIST/"
cp "$SDL_DIR/$TRIPLE/bin/SDL3.dll" "$DIST/"
cp "$FF_PREFIX"/bin/avcodec-*.dll "$FF_PREFIX"/bin/avutil-*.dll "$DIST/"
# Anything else the exe or the DLLs import that isn't part of Windows.
missing=$("$TRIPLE-objdump" -p "$DIST"/*.exe "$DIST"/*.dll | sed -n 's/^\s*DLL Name: //p' | sort -u |
  grep -viE '^(kernel32|user32|gdi32|winmm|imm32|ole32|oleaut32|version|setupapi|shell32|advapi32|cfgmgr32|hid|bcrypt|msvcrt|ucrtbase|api-ms-win-.*|ws2_32|dinput8|dxgi|d3d11|d3d12|xinput1_4|uuid|winhttp|shlwapi|dwmapi|SDL3|avcodec-.*|avutil-.*)\.dll$' || true)
if [ -n "$missing" ]; then
  echo "warning: the package may also need: $missing" >&2
fi
cp "$SDL_DIR/$TRIPLE/share/licenses/SDL3/LICENSE.txt" "$DIST/licenses/SDL3-LICENSE.txt"
cp "$FF_PREFIX/COPYING.LGPLv2.1" "$DIST/licenses/FFmpeg-COPYING.LGPLv2.1.txt"
cat > "$DIST/licenses/README.txt" <<EOF
SDL3.dll: SDL $SDL_VER (zlib licence, SDL3-LICENSE.txt), the official MinGW build from
$SDL_URL

avcodec / avutil DLLs: FFmpeg $FFMPEG_VER (LGPL 2.1 or later, FFmpeg-COPYING.LGPLv2.1.txt),
built unmodified from $FFMPEG_URL with:
  --enable-shared --disable-everything --enable-decoder=h264 --enable-decoder=atrac3p
  (see tools/build_windows.sh in the chexa-pc source for the full configure line).
The DLLs can be replaced with other builds of the same FFmpeg major version.
EOF
printf '@echo off\r\nrem Runs the PC port on the game files in C:\\psp\\chexa. Edit the path if yours differ.\r\ncd /d "%%~dp0"\r\nchexa-pc.exe --disc C:\\psp\\chexa --startup %%*\r\nif errorlevel 1 pause\r\n' > "$DIST/run.bat"
printf 'Carnage Heart EXA PC port (Windows build)\r\n\r\nrun.bat starts the game from C:\\psp\\chexa (the folder holding PSP_GAME).\r\nSaves and controls.ini go to %%APPDATA%%\\chexa-pc.\r\nchexa-pc.exe --controls lists the controls, --list-gamepads the controllers SDL sees.\r\nThe game files and the decrypted EBOOT are not included.\r\n' > "$DIST/README.txt"
(cd "$DIST" && ls -l)
echo "== done: $DIST"
