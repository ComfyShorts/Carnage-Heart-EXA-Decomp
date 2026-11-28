# Windows build (PC port)

`chexa-pc.exe` is a native Windows program, cross-compiled from Linux or WSL with
MinGW-w64. It runs by double-click or from PowerShell, without WSL. The SDL3 and FFmpeg
DLLs it needs ship in the same folder.

## Building

Install the cross-compiler once. Everything else goes into `.deps/` in the repository,
without root:

```sh
sudo apt install mingw-w64 nasm     # nasm: FFmpeg's assembly (optional, faster decoding)
sudo apt install wine64             # optional: to try the exe from Linux
tools/build_windows.sh              # deps, build, package
```

`tools/build_windows.sh` does four things, skipping steps already done:

1. **SDL3:** downloads SDL 3.4.2's official MinGW development package (headers, import
   library, `SDL3.dll`) into `.deps/sdl3-mingw/`.
2. **FFmpeg:** downloads FFmpeg 8.1.3's source and builds `avcodec-62.dll` and
   `avutil-60.dll` into `.deps/ffmpeg-mingw-i686/`. The build is LGPL and shared, with
   only the two decoders the port uses: H.264 for the opening movie and ATRAC3plus for
   the movie's sound and the music. This takes under a minute.
3. **The port:** configures and builds with the `windows` CMake preset
   (`cmake/toolchain-mingw.cmake`) into `build/win32/`. This builds `chexa-pc.exe` and
   the test programs.
4. **The package:** fills `dist/chexa-pc-windows/` with:
   - `chexa-pc.exe`, `SDL3.dll`, `avcodec-62.dll` and `avutil-60.dll`;
   - `run.bat` and a short `README.txt`;
   - `licenses/` (SDL's zlib licence, FFmpeg's LGPL, and how the DLLs were built).

Both downloads are checked against SHA-256 sums in the script. The FFmpeg tarball's sum
was taken after checking its release signature (FFmpeg release key
`FCF9 86EA 15E6 E293 A564 4F10 B432 2F04 D676 58D8`).

Options:
- `--deps-only`: steps 1 and 2 only.
- `--no-package`: skips step 4.
- `--jobs N`: build jobs.

You can also configure by hand after `--deps-only`: `cmake --preset windows && cmake
--build --preset windows`. `.deps/`, `build/` and `dist/` are gitignored.

### Why 32-bit

The exe is 32-bit (i686), like the Linux `m32` build, and runs on 64-bit Windows. The
port needs 4-byte pointers for two reasons:
- the decompiled structs keep the PSP's ILP32 layout;
- the GE's display lists carry host addresses in 32 bits (`src/platform/ge.c`).

A 64-bit build compiles, but its exe and heap sit above 4 GB, so the renderer would
follow truncated pointers. The exe is linked large-address-aware, so it can use up to
4 GB.

Only Windows' own DLLs are needed besides the three that ship. libgcc is linked
statically, and MinGW's win32 thread model needs no `libwinpthread`. The packaging step
checks the exe's and DLLs' imports and warns if anything else turns up.

## Running

Copy `dist/chexa-pc-windows/` anywhere on the Windows side, for example
`C:\Games\chexa-pc`. Put the game's files in `C:\psp\chexa`: the folder holding
`PSP_GAME`, as for the Linux build.

- **Double-click `run.bat`.** It runs `chexa-pc.exe --disc C:\psp\chexa --startup` and
  passes on any extra arguments. If the program exits with an error, the window stays
  open so you can read it. Edit the path in `run.bat` if your files are elsewhere.
- **From PowerShell:**

  ```powershell
  cd C:\Games\chexa-pc
  .\chexa-pc.exe --disc C:\psp\chexa --startup
  .\chexa-pc.exe --controls        # the key and gamepad bindings
  .\chexa-pc.exe --list-gamepads   # the controllers SDL sees
  ```

The exe is a console program, so its log appears in the console window, or in
PowerShell.

### Paths and files

- **`--disc`** takes Windows paths: `C:\psp\chexa`, `C:\psp\chexa\`, `C:/psp/chexa`, a
  relative path, or the `PSP_GAME\USRDIR` folder itself. Lookups are case-insensitive.
- **Saves** go to `%APPDATA%\chexa-pc\saves` (for example
  `C:\Users\you\AppData\Roaming\chexa-pc\saves`), one folder per save, unless you pass
  `--saves DIR`. The folder is created on the first run. On Linux the default is still
  `saves/` in the current folder.
- **`controls.ini`** is `%APPDATA%\chexa-pc\controls.ini` (`chexa-pc --write-controls`
  writes one to edit; see `docs/controls.md`). `CHEXA_CONTROLS` can name another file.
- **The decrypted EBOOT** gives the menus their text (see `docs/screens.md`). Without
  `--elf FILE`, the port looks in three places, in order:
  1. `orig\EBOOT.elf` under the current folder;
  2. `EBOOT.elf` next to `chexa-pc.exe`;
  3. `EBOOT.elf` in the `--disc` folder.

  Without it the game runs, but its menus have no text. Neither the game files nor the
  EBOOT are in the package.

## Controllers

The controller code is the same as on Linux: SDL3's gamepad API, buttons bound by
position (east is Circle, south is Cross), and hotplug. SDL 3.4.2 on Windows has these
joystick backends on by default:
- XInput;
- HIDAPI, with drivers for Xbox, PlayStation, Switch and 8BitDo pads;
- DirectInput.

RawInput and Windows.Gaming.Input are off by default. The port sets no hints, so those
defaults stand.

An **8BitDo Pro 3** is covered in each mode of its switch:
- **X:** an XInput pad, read as an Xbox controller.
- **D:** USB id `2dc8:6009`, handled by SDL's 8BitDo HIDAPI driver
  (`USB_PRODUCT_8BITDO_PRO_3` in SDL's source).
- **S:** a Switch Pro controller, through SDL's Switch HIDAPI driver.

Since the bindings go by position, Circle (which confirms in this game) is the right face
button in every mode, whatever its label. Rebind it in `controls.ini` if you want
otherwise.

`chexa-pc --list-gamepads` prints, for every controller SDL sees:
- its name and USB id;
- its path, which shows the backend: `XInput#0` for XInput, a `\\?\HID#...` path for
  HIDAPI;
- whether SDL has a gamepad mapping for it (without one the port ignores it);
- the face buttons' labels and the PSP buttons each one presses:

```text
SDL 3.4.2
1. 8BitDo Pro 3
   USB id 2dc8:6009, path \\?\HID#VID_2DC8&PID_6009...
   gamepad (type ...); face buttons as labelled on it and what they press here:
    south (B) = cross  east (A) = circle  west (Y) = square  north (X) = triangle
```

That output is an illustration of the format. The real pad wasn't available here (see
below).

## What was and wasn't tested

Everything below was run under **Wine 10.0** in WSL (WSLg display, PulseAudio sound), not
on real Windows.

**Tested, and working:**

- **The test programs:** all 11 of the Windows test programs pass under Wine with the
  game files:
  - `gu`, `controls` (including SDL virtual-gamepad hotplug), `io`;
  - `savedata`, which saved into a Windows path;
  - `pgchip`, `battle --all`, `oke-parts`;
  - `scene-files`, `story`, `sound`, `movie`.
- **`run.bat`:** started the game from `C:\psp\chexa` (a symlink to the files inside the
  Wine prefix). The checks:
  - the opening movie played through with sound, all 1184 pictures, none dropped;
  - the title screen drew correctly, with menu text when `EBOOT.elf` sat next to the exe;
  - the save start-up check created and loaded its data in
    `C:\users\...\AppData\Roaming\chexa-pc\saves`.
- **A story step:** `--story-step 3 --battle-result win --auto-scenes` ran its scene and
  battle through to the save prompt.
- **`--disc` spellings:** `C:\psp\chexa`, `C:\psp\chexa\`, `C:/psp/chexa` and
  `c:\PSP\Chexa\PSP_GAME\USRDIR` all mounted. A missing folder failed cleanly.
- **The options:** `--controls` showed `%APPDATA%\chexa-pc\controls.ini` as the config
  file. `--list-gamepads` listed a virtual pad with its bindings, and said "no
  controllers" without one. The ctest `list_gamepads` checks the same on Linux.
- **The Linux build:** all 19 ctest tests still pass after the portability changes.

**Not tested:**

- **Real Windows.** Wine is close, but not the same.
- **A real controller,** including the 8BitDo Pro 3. Wine in WSL can't see USB or
  Bluetooth devices. The XInput, HIDAPI and Switch paths above come from SDL's source
  and defaults, not from a run.
- **Windows Defender SmartScreen.** The exe isn't signed, so Windows may warn on the
  first run ("More info", then "Run anyway").
- **High-DPI scaling and fullscreen** on Windows.
- **Running the test programs through ctest on Windows.** They were run by hand, since
  ctest's registered commands are Linux paths. `chip_edit_roundtrip` isn't built for
  Windows: it drives `chexa-pc` through a POSIX shell.

## Portability changes

These are the Windows-specific parts of the code:

- **`src/platform/host_compat.h`** wraps the host calls that differ between POSIX and
  Windows:
  - `mkdir`'s mode, and binary-mode `open` (`O_BINARY` instead of `O_CLOEXEC`);
  - `localtime_r` and `gmtime_r`, and the PSP clock's real time;
  - the time zone offset (`tm_gmtoff`), `realpath`, and thread CPU time for `--perf`;
  - `\` as a path separator.
- **`files.c` and `savedata.c`** accept drive letters and either separator.
- **The VRAM window.** On 32-bit Windows the stack and heap sit low in memory (below 2 MB,
  and a big heap block can cross 0x04000000), where the GE took addresses for video
  memory. Under Wine this crashed the title screen whenever the heap landed there. Now
  only `0x04000000-0x041FFFFF` means VRAM. Frame and depth buffers use their VRAM offsets
  directly, and on Windows that window is reserved at start-up so no host memory is
  there. Linux runs never used the other forms: a run logging them over the title,
  movie, a story battle, model and map views found none.
- **`controls_config_path`** reads the environment through `SDL_getenv`, which sees
  variables SDL sets on Windows.
- **The savedata test** empties its folder with SDL's file calls instead of `rm -rf`.
