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
0x08b767ec(16, 16, 32, 32)                   stores four values, emits nothing; unknown
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
