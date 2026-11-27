# Graphics: libgu, the GE and the display

How the port draws. The game links Sony's libgu statically and talks to the hardware
through the `sceGe_user` and `sceDisplay` imports. The port replaces all three layers
with hand-written C:

| Layer | Game | Port |
|---|---|---|
| CRender / CGraphics | `FUN_08b205c0` (GetInstance), `FUN_08b209dc` (ctor), `FUN_08b1fe70` (display set-up), `FUN_08b20678` (packet), `FUN_08b20770` (render state), `FUN_08b20624` (kick), `FUN_08b200ec` (frame end) | `src/game/render.c` |
| libgu / libgum | statically linked, `0x08b75608` (`sceGuInit`) .. `0x08b7a5f0` | `src/platform/gu.c`, same calls and argument order |
| GE (display lists, rasterizer) | hardware | `src/platform/ge.c`: interpreter and software renderer |
| display | `sceDisplay*` | `src/platform/display.c`, shown by `sdl_platform.c` |

The game's own libgu can't be reused even as a model for address handling: it masks
every pointer to a 28-bit PSP address. The port's lists carry host pointers instead:
an address command's 24 bits hold the low bits and the preceding `BASE` command holds
the high byte (8 bits instead of the PSP's 4). Values below 2 MiB, or in
0x04000000-0x041FFFFF, mean video memory. This only works in the 32-bit build.

## Frames

Per main-loop iteration (`FUN_088064b0` → `FUN_08b200ec`):

1. `sceGuStart(3, packet[frame])`, the reset of the render state, the clear
   (`CClear` at +0x4F8), every object on the render list, `sceGuFinish`.
2. `sceGuSync(0, 0)`.
3. Wait for vertical blanks until `interval` (1) have passed since the last frame.
4. `sceGuSwapBuffers`: the frame drawn last time is shown.
5. A 64-byte direct list sets the draw buffer and calls this frame's packet.

Frame buffers: two 512-wide 5551 buffers at VRAM offsets 0 and 0x44000, depth at
0x88000, and the game's "fast heap" above 0xCC000. A drawn frame appears one swap
after it is built, so the first frame with drawing in it is the third
`sceDisplaySetFrameBuf` call (the first is `sceGuDisplay(1)`).

## sceGu calls in the first frames

`chexa-pc --disc DIR --startup --gu-trace 3` logs them. Start-up, once:

```
sceGuInit()
sceGuStart(0, packet0, 0x80000)
sceGuDrawBuffer(1, 0x0, 512)                 5551
sceGuDispBuffer(480, 272, 0x44000, 512)
sceGuDepthBuffer(0x88000, 512)
sceGuOffset(1808, 1912)                      2048 - 240, 2048 - 136
sceGuFinish(); sceGuSync(0, 0)
sceGuStart(1, packet0/1, 0x80000); sceGuFinish()   x2, empty call lists
sceGumSetMatrixStack(m, 4, 4, 8, 0)
sceGumMatrixMode(2/1/0); sceGumLoadIdentity()
sceGuDisplay(1)
```

Each frame:

```
sceGuStart(3, packet[frame], 0x80000)
sceGuDisable: FACE_NORMAL_REVERSE, FRAGMENT_2X, COLOR_TEST, LINE_SMOOTH, COLOR_LOGIC_OP, STENCIL_TEST
sceGuEnable:  PATCH_FACE, DEPTH_TEST, ALPHA_TEST, BLEND, DITHER, CLIP_PLANES, CULL_FACE, PATCH_CULL_FACE
sceGuShadeModel(1); sceGuFrontFace(1); sceGuSetDither(standard 4x4)
sceGuDepthOffset(0); sceGuDepthRange(0xFFFF, 0); sceGuColorMaterial(3)
sceGuAlphaFunc(6, 0, 0xFF); sceGuDepthFunc(7); sceGuStencilFunc(7, 0, 0xFF)
sceGuColorFunc(2, 0xFFFFFF, 0xFFFFFF); sceGuBlendFunc(0, 2, 3, 0, 0)
sceGuStencilOp(0, 0, 0); sceGuLogicalOp(15)
sceGuDisable: SCISSOR_TEST, FOG, LIGHTING, LIGHT0-3, TEXTURE_2D
sceGuTexMode(1, 0, 0, 0); sceGuTexFilter(7, 1); sceGuTexFunc(0, 1); sceGuTexWrap(0, 0)
sceGuFog(FLT_MIN, FLT_MAX, 0xFFFFFFFF); sceGuLightMode(0)
sceGuAmbient(-1); sceGuAmbientColor(-1); sceGuMaterial(1/2/4, -1); sceGuSpecular(1)
sceGuSpriteSize(16, 16, 32, 32)              0x08b767ec: stored sprite size, emits nothing
sceGuColor(-1); sceGuPatchDivision(1, 1)
sceGumMatrixMode(2/1/0); sceGumLoadIdentity(); sceGumUpdateMatrix()
sceGuClearColor(0); sceGuClearDepth(0); sceGuClearStencil(0); sceGuClear(0x16)
  ... the render list's objects draw here (none yet in the port) ...
sceGuFinish(); sceGuSync(0, 0)
sceGuSwapBuffers()
sceGuStart(0, kick, 0x40); sceGuCallList(packet[frame]); sceGuFinish()
```

The first screen sets a black clear colour, and `CRender` drops the colour bit from
the clear when the colour is 0. `sceGuClear(0x16)` therefore clears only depth and
stencil (GE `CMODE 0x601`, one 16-vertex fast-clear sprite list). The screen stays
black because video memory starts zeroed.

`--ge-trace` logs every GE command executed, and `--screenshot [FILE]` saves the
first displayed frame the GE drew into (default `orig/first_frame.png`).

## Renderer coverage

Done: through-mode and transformed vertices (world/view/projection, viewport,
near-plane clipping), points, lines, triangles, strips, fans, sprites, flat and
Gouraud shading, perspective-correct texturing, 565/5551/4444/8888 and CLUT4/8/16/32
textures, swizzling, nearest/bilinear filtering, repeat/clamp, texture functions,
alpha/stencil/depth tests, stencil ops, blending, pixel masks, clear mode, block
transfers, signal/finish callbacks.

Not yet (each logs once when met): lighting, fog, skinning and morphing, Bezier and
spline patches, DXT textures, mipmaps beyond level 0, color test, logic ops,
dithering, texture projection and environment mapping.

`chexa-gu-test` draws through the port's libgu and checks pixels.

## Performance

The port runs at the PSP's 60 Hz: the game thread builds a frame, waits for the
vertical blank (`interval` 1, emulated by `sceDisplayWaitVblankStart`), then the GE
draws the list into VRAM, and the main thread shows the displayed buffer with vsync.
The game's time step is fixed per frame, so a slow frame runs late rather than skipping
ahead, and screenshots at a given frame are the same in any build.

`--perf` logs, every 120 frames, the game thread's frame rate, average and slowest
frame and the frames that missed their vblank (`late`), the work in a frame (`busy`,
with the slowest) split into the screens' update (`logic`, with the chip interpreter
inside it as `chips`), building the GU list (`build`) and the GE (`commands`: list
parsing and vertex transform; `raster`: drawing pixels), then the vblank wait,
primitives and pixels per frame. When a window holds a frame over 14 ms it also logs
that frame. The main thread logs presents per second and the time to convert and
upload the picture (`present`) apart from waiting for the swap. Starting a battle logs
its load time (`[perf] battle loaded in ...`).

Measured on a Ryzen 7 7800X3D under WSL2 (WSLg window, SDL's software renderer), the
title screen and a 3-on-3 battle (the test save's Team 01 against Olshguy):

| | Debug build (before) | RelWithDebInfo + SSE2 | + faster rasterizer |
|---|---|---|---|
| Title: frame rate | 7.5 fps | 20.0 fps | 60 fps |
| Title: raster per frame | 127 ms | 41 ms | 6.5 ms |
| Battle: frame rate | 8.5 fps | | 60 fps, no late frames |
| Battle: raster per frame | 107-112 ms | | 9.7 ms (busy 10.5 ms) |
| Battle: logic + chips | 0.03-0.10 ms | | 0.02 ms |
| Battle: GE commands | 1.5 ms | | 0.65 ms |
| Present (main thread) | 0.65 ms | | 0.23 ms |
| Battle load | 200 ms | | 190-200 ms (field and models 130 ms) |

Almost all the time was the rasterizer. What changed:

- **Build flags.** The presets now build `RelWithDebInfo` (`-O2 -g`) instead of an
  unoptimized Debug build (`m32-debug` keeps one), and the 32-bit targets use
  `-msse2 -mfpmath=sse`: i386 defaults to x87 floating point, slower, and its excess
  precision makes results depend on register allocation (9 pixels of one battle frame
  differ between the two by at most 9 in a channel).
- **Per-primitive state.** `target()` decodes the registers once per primitive into a
  `Target` (scissor within the buffer, tests, blend mode, texture layout and CLUT), so
  `plot()` no longer reads and decodes ~15 registers per pixel. The CLUT is converted to
  RGBA once per load and format, not per texel.
- **SIMD.** Bilinear filtering weights the four texels in SSE float lanes; the usual
  blend (source alpha, one minus source alpha) and the modulate texture function divide
  by 255 in 16-bit lanes with `(x + 1 + (x >> 8)) >> 8`, exact for the sums they make
  (at most 65025).
- **Less visiting.** Triangles estimate each row's span from the edge equations and
  test only around it (growing it while pixels are covered) instead of the whole
  bounding box; z, texture coordinates and colour are interpolated only when used.
  Sprites stop at the scissor; clear-mode sprites fill rows directly. Lines step only
  where they are on screen (found by binary search on the same expression), which
  removed 30-70 ms frames in battles where a line ended far off screen.
- **Presenting.** The screen texture is copied without blending (the PSP's display
  ignores the frame buffer's alpha, which holds the stencil).

Every change keeps the same float operations in the same order, so the pictures are
identical: screenshots of ten scenes (title, Battle Mode menu, team screen, four battle
frames, OKE Configuration, map, sky) match the unchanged rasterizer's pixel for pixel
(`tools/png_diff.py A.png B.png`). The software renderer now has about 6 ms of headroom
in a battle, so the GPU path through SDL3 wasn't needed.
