# Obsolete Windows backends and rendering options

Removed 2026-11-08. The tree once compiled three Windows renderers and carried
a ladder of runtime rendering variants beside them. This file records what
each option was, how the candidates measured against each other, why one
combination was kept, and what it would take to bring any of the others back.
The last commit that still contains all of them is `45e5d5fa` (the merge of
`dedupe-src`); `git show 45e5d5fa:src/gpui/paintgpu_win.cpp` and friends is
where to start.

## What was kept

| Option | Kept as | Why |
| --- | --- | --- |
| Windows renderer | Direct2D on a D3D11 device, flip-model swap chain (`paint_win.cpp`) | Same whole-frame time as the GPU renderers once the scene is on, 1.5–3× less memory, the only one with high-DPI, transparent windows and scroll blits, and the only fixed configuration that still linked |
| Scene | Always on, with `__scene=off` as the one diagnostic switch | Every level below the top one existed to measure the level above it; `off` stays because the scene is the only thing in the tree that can decide not to draw, so it is what a stale-frame bisect needs |
| Damage redraw | A platform fact, `SceneDamageSupported()` (`scene_win.cpp` true, `scene_posix.cpp` false) | It was never a user choice: Windows' flip chain is the one back buffer checked to hold the last frame |
| Scroll layers | Always on; `GPUI_SCROLL_VERIFY=1` keeps checking blits against a full replay | Measured neutral-to-positive and `__scene=off` already bypasses it |
| `__layout_reuse=off\|on` | Kept, unchanged | Cross-platform, forty lines, and the test suite's correctness oracle for the layout cache (`tests/LayoutReuseTests.cpp` compares reuse against rebuild) |

## What was removed

| Option | Form | Replaced by |
| --- | --- | --- |
| `WIN_BACKEND_DIRECT2D` / `WIN_BACKEND_D3D11` / `WIN_BACKEND_D3D12` / `WIN_BACKEND_ALL` | compile-time defines in `paint.h`; `--win-backend=d2d\|d3d11\|d3d12\|all` in `build.ts`, `run.ts`, `test.ts`, `bench.ts`, `build-no-amalgam.ts`; `-DWIN_BACKEND_ALL` in `clang-tidy.ts`; a CI lane | nothing; Direct2D is the one renderer |
| `__paint=d2d\|d3d11\|d3d12` | runtime selector, `WinPaintOptions::backend` | nothing |
| `__msaa=1\|2\|4\|8` | GPU renderer sample count, `WinPaintOptions::msaa` | nothing (Direct2D antialiases analytically) |
| `__gpu_reset_every=N` | test seam that tore down the GPU renderer's device-local state every N frames, driven by `cmd/gpu-parity.ts` | nothing |
| `__scene=replay\|cache\|skip\|damage` | five cumulative levels (`SceneLevel`, `WinSceneMode`, `SceneLevelOn()`) | `__scene=off\|on`; the top level is the only one |
| `__scroll_layers=on\|off`, `GPUI_SCROLL_LAYERS=0` | scroll-blit toggle | always on |
| `GPUI_PATH_CACHE_TRANSLATION=off` | profiling switch that keyed the path cache on absolute coordinates | always translation-independent |
| `src/gpui/paintgpu.h`, `paintgpu_win.cpp` (3,643 lines), `paintgpu_win.hlsl`, `paintgpu_shaders_win.cpp` | the GPU renderer, its four SM5 shaders and their checked-in DXBC | deleted |
| `cmd/update-win-shaders.ts`, `cmd/gpu-parity.ts`, the HLSL SHA-256 check in `build.ts` | shader regeneration and the three-backend pixel parity / device-recovery suite | deleted |
| `WinPaintOptions`, `WinPaintOptionsGet`, `WinPaintOptionsTakeArg`, `WindowTakePaintArg`, `FrameBenchLogGpu`, `PaintShared*`, `PaintImagePixels`, `PaintTextLayoutNative`, `PaintFlipChainDesc`, `gpuw::*` | the seam between `paint_win.cpp` and the GPU renderer | deleted; `FlipChainDesc` is a static helper in `paint_win.cpp` |

## The options as they were

Compile time: exactly one of `WIN_BACKEND_DIRECT2D`, `WIN_BACKEND_D3D11`,
`WIN_BACKEND_D3D12`, or `WIN_BACKEND_ALL` (all three plus the runtime
selector). Direct2D was the default. The D3D11 half borrowed the Direct2D
file's D3D11 device and DXGI factory; the D3D12 half owned its device, queue,
triple-buffered command allocators, upload heaps and descriptor tables (no
D3D11On12). Both shared one CPU front end: instance batching, path fans
(stencil-and-cover), a glyph atlas fed by DirectWrite's ClearType masks with
gamma and enhanced contrast applied in the shader, image upload. Everything
device-independent (DirectWrite factory and formats, `IDWriteTextLayout`, WIC
decode) was shared with Direct2D.

Run time, consumed by `GpuiTakeRuntimeArgs` before `GpuiMain`:

- `__paint=d2d|d3d11|d3d12` in an `ALL` build.
- `__msaa=1|2|4|8` (default 4): quads and glyphs had analytic coverage, so
  this only smoothed tessellated paths and expanded strokes.
- `__scene=off|replay|cache|skip|damage`, each level including the ones
  before: `off` drew the tree straight to the backend; `replay` collected,
  culled by content mask, sorted by stacking context and replayed; `cache`
  kept path geometry realizations and small offscreen coverage masks across
  frames; `skip` hashed the frame and neither drew nor presented an identical
  one; `damage` redrew only the union of changed primitive bounds, snapped out
  to device pixels, carrying prior damage across the three-buffer chain.
  Default `damage` on Windows, `skip` elsewhere.
- `__scroll_layers=on|off`: when the only change inside a scroll viewport was
  the offset, blit the overlap and redraw the strip that entered.
- `__gpu_reset_every=N`: the device-removal recovery path on a timer.
- `GPUI_PATH_CACHE_TRANSLATION=off`: absolute-coordinate path cache keys.

## Measurements

One machine (Intel Core Ultra 7 265U, Windows 11, MSVC release `/O2`), one
sitting, release builds of the examples built with `--win-backend=all`. Three
harnesses:

- **`GPUI_FRAME_BENCH=600`**: redraws one frame 600 times after 30 warm-up
  frames, window `-gpui-window=40,40,1100,750`. With the scene on, 97% of
  those frames are identical, so `damage` numbers there are the cost of an
  idle window and `off` numbers are the cost of a full paint. `fps_monitor`
  changes every primitive every frame and is the counterweight.
- **`gpui_perf --auto`**: nine driven scenarios (spinner, three scrolls, a
  table refresh, a list scroll, and a workspace's quotes/scroll/hover), 240
  frames each, real timers and input. This is the number that matters.
- **`cmd/bench-scene.ts -n=8`**: seven interactions (two scrolls, an icon
  scroll, hover, caret, popup, chart tick) through Win32 input against the
  story and table examples, visible surface read back and compared.

### Whole frame, `GPUI_FRAME_BENCH`, median ms (build + layout + paint)

| example | scene | Direct2D | D3D11 | D3D12 |
| --- | --- | ---: | ---: | ---: |
| story | off | 3.58 | 2.31 | 2.25 |
| story | damage | 1.50 | 1.53 | 1.49 |
| showcase | off | 0.27 | 0.40 | 0.23 |
| showcase | damage | 0.07 | 0.07 | 0.07 |
| system_monitor | off | 0.55 | 0.43 | 0.34 |
| system_monitor | damage | 0.05 | 0.04 | 0.05 |
| fps_monitor | off | 0.90 | 0.58 | 0.32 |
| fps_monitor | damage | 1.23 | 0.48 | 0.66 |

Paint phase alone, story: off 2.31 / 1.11 / 1.07 ms, damage 0.43 / 0.39 /
0.38 ms. Build and layout are the same code everywhere (story: ~0.2 + ~1.0
ms), and with the scene on they are the frame.

### Private bytes at the end of the same runs, MB

| example | Direct2D | D3D11 | D3D12 |
| --- | ---: | ---: | ---: |
| story (damage) | 123 | 170 | 235 |
| story (off) | 184 | 330 | 298 |
| showcase | 63 | 97 | 172 |
| system_monitor | 65 | 103 | 172 |
| fps_monitor | 74 | 107 | 180 |

D3D12's floor is ~110 MB above Direct2D's on the lightest scene (its heaps
and descriptor tables); D3D11's grows with the scene.

### `gpui_perf --auto`, scene on (damage), median draw ms / mean paint ms / MB

| scenario | Direct2D | D3D11 | D3D12 |
| --- | --- | --- | --- |
| Spinner | 2.23 / 0.77 / 68 | 1.88 / 0.51 / 107 | 1.21 / 0.36 / 182 |
| ScrollSidebar | 1.98 / 1.59 / 68 | 1.20 / 0.46 / 107 | 1.88 / 0.88 / 182 |
| ScrollPage | 1.44 / 0.74 / 72 | 1.11 / 0.43 / 107 | 2.02 / 1.06 / 182 |
| ScrollTable | 2.56 / 1.48 / 80 | 2.06 / 0.69 / 139 | 2.27 / 1.07 / 190 |
| RefreshTable | 3.00 / 1.88 / 78 | 2.00 / 0.52 / 135 | 1.71 / 0.71 / 186 |
| ScrollList | 1.58 / 0.86 / 78 | 0.89 / 0.38 / 112 | 2.23 / 1.12 / 185 |
| WorkspaceQuotes | 2.43 / 1.02 / 86 | 2.21 / 0.73 / 169 | 2.36 / 0.85 / 193 |
| WorkspaceScroll | 5.38 / 3.78 / 95 | 2.56 / 1.03 / 209 | 2.81 / 1.22 / 198 |
| WorkspaceHover | 4.28 / 2.44 / 92 | 2.50 / 1.00 / 205 | 2.66 / 1.12 / 195 |

With the scene off, Direct2D's WorkspaceScroll is 9.95 ms and D3D11's 2.48;
the scene is what brings Direct2D within range (it culls the primitives the
backend would have clipped and blits pure scrolls). The GPU renderers' paint
is 1.5–3× cheaper on a heavily scrolling workspace, and every Direct2D median
is under 6 ms of a 16.7 ms budget.

### `cmd/bench-scene.ts`, paint median ms, Direct2D vs D3D11

| interaction | D2D off | D2D damage | D3D11 off | D3D11 damage |
| --- | ---: | ---: | ---: | ---: |
| scroll (table) | 1.45 | 1.16 | 0.40 | 0.28 |
| introduction-scroll | 3.38 | 1.66 | 1.05 | 1.13 |
| icon-scroll | 1.37 | 3.73 | 1.13 | 2.74 |
| hover | 0.65 | 0.22 | 0.16 | 0.02 |
| caret | 0.71 | 0.53 | 0.40 | 0.27 |
| popup | 0.23 | 0.39 | 0.21 | 0.08 |
| chart-tick | 0.98 | 1.26 | 0.34 | 0.31 |

Off/skip/damage captures were pixel-identical on both renderers for every
deterministic interaction. `icon-scroll` is the one where the scene costs:
32 cached icon paths that move every frame are rebuilt as masks faster than
they are diffed.

### The scene ladder, story, Direct2D, `GPUI_FRAME_BENCH` (whole / paint ms)

| off | replay | cache | skip | damage |
| --- | --- | --- | --- | --- |
| 3.42 / 2.26 | 2.08 / 0.89 | 1.88 / 0.74 | 1.51 / 0.41 | 1.55 / 0.42 |

Each step pays. There was no reason to run at an intermediate level except
to measure the next one, which is why the levels went.

### Other single factors

- **MSAA** (D3D11, scene off): story 2.20 ms at ×1, 2.09 at ×8;
  fps_monitor 0.27 at ×1, 0.37 at ×8 (+48 MB private). Within noise on real
  scenes, ×8 visible only on a path-heavy one.
- **Layout reuse off**: story whole frame 5.41 ms against 1.55 (layout 4.84
  vs 0.97). Not an option anyone should take; kept as a test oracle only.
- **Scroll layers off** (`GPUI_SCROLL_LAYERS=0`, Direct2D, damage):
  introduction-scroll paint median 1.44 vs 1.66 on, table scroll 1.27 vs
  1.16 — noise at n=8 either way. The layer stays because the mechanism is
  what makes a large page's scroll cheap and the verify hook still checks it.
- **Executable size**, showcase release: Direct2D only 2,380,288 bytes,
  all three 2,677,248 (+297 KB of shaders, atlas and submission code).
- **Link rot**: `--win-backend=d3d11` and `--win-backend=d3d12` fixed builds
  failed to link (2 and 1 unresolved externals) at the time of removal. Only
  `d2d` and `all` had been exercised by anyone.

## Why Direct2D

1. Whole-frame time with the scene on is indistinguishable across the three
   on idle and lightly changing frames, which is what the consumer apps
   (SumatraPDF ng, Sumatra File Manager) spend their time in. The GPU
   renderers win 1.5–3× on paint when everything changes, and even there
   Direct2D stays far inside the budget; build and layout are the costs left.
2. The GPU renderers used 1.5–3× more private memory, D3D12 a flat ~110 MB
   more on the smallest scene.
3. Features: the GPU renderers placed geometry in device pixels, so a window
   stayed at 96 dpi on a scaled display; they blended onto an opaque target,
   so `WinOpts::transparent` was Direct2D only; `CanvasScrollRect` (the scroll
   blit) returned false on them; text was not pixel-identical with Direct2D
   (0.3–0.9% of pixels over tolerance on the parity captures).
4. Code: 3,643 + 181 + 147 lines of renderer, a shader toolchain that needed
   FXC and a checked-in bytecode table with a hash check in the build, a
   283-line parity suite, and a seam of a dozen `void*` helpers through the
   Direct2D file. D3D12 also raised the floor to Windows 10.
5. Direct2D falls back to WARP without a GPU and runs back to Windows 7 with
   the platform update.

## Bringing one back

The GPU renderer is self-contained behind `paint.h`'s entry points, so it
returns as a unit:

1. Restore `src/gpui/paintgpu.h`, `paintgpu_win.cpp`, `paintgpu_win.hlsl`,
   `paintgpu_shaders_win.cpp`, `cmd/update-win-shaders.ts` and
   `cmd/gpu-parity.ts` from `45e5d5fa`.
2. In `paint.h`, bring back the `WIN_BACKEND_*` macro block, `WinPaintBackend`,
   `WinPaintMsaa` and `WinPaintOptions` (the scene level has moved to
   `scene.cpp` as `__scene=off|on` and can stay there).
3. In `paint_win.cpp`, bring back the `PaintGpuOn()` dispatch at the top of
   each entry point, the `PaintShared*` / `PaintImagePixels` /
   `PaintTextLayoutNative` / `PaintFlipChainDesc` helpers, the
   `WinPaintOptionsTakeArg` parser, and the `#if WIN_BACKEND_*` guards around
   the Direct2D factory and device creation. Note that the shared swap-chain
   description is now the static `FlipChainDesc`.
4. In `window_win.cpp`, `DrawDpi` returned 96 under `PaintGpuOn()` (the GPU
   renderers' one real gap worth fixing on the way back), `FrameBenchLogGpu`
   logged `gpuw::LastFrameStats()`, and `WindowTakePaintArg` fed argv to the
   parser; `window_common.cpp` declared both and `GpuiTakeRuntimeArgs` called
   the latter first. The non-Windows stubs lived in `window_posix.cpp`, which
   was deleted when it became empty.
5. `build.ts`: `WindowsBackend` type, `--win-backend` flag, `winLibs()` per
   backend (`d3d12.lib` for D3D12, `dcomp.lib` only with Direct2D), the
   `/DWIN_BACKEND_*` define, `ensureWinShadersCurrent` (SHA-256 of the HLSL
   against the `source-sha256:` marker in the generated file). `format.ts`
   skipped the generated bytecode file; `clang-tidy.ts` passed
   `-DWIN_BACKEND_ALL=1`; the CI lane `windows-clang-mini-all-backends` built
   `--win-backend=all -all`.
6. Tests: `RuntimeArgsTests.cpp` checked `__paint`, `__msaa` and
   `__gpu_reset_every` parsing; `SceneTests.cpp` had
   `D3d12ImageDescriptorsAreReusable` and `GpuImagesEvictAtFinalRelease`
   (`gpuw::RenderImageCacheCountForTest`), and `paint_backend.h` declared
   `RenderImageDropNative` on Windows so the final image release could drop
   the GPU textures.
7. `cmd/bench-scene.ts` took `-paint=d3d11|d3d12` and compared `skip` against
   `damage` captures by hash; it now compares `off` against `damage` through
   `cmd/imgdiff.ts` with a tolerance, since an uncached path fill and a cached
   realization differ on a few curve-edge pixels.

The scene levels, if ever wanted again for measurement, were three
comparisons in `scene.cpp`: `SceneLevelOn() < kSceneCache` short-circuited
`PathFor` to a fresh `BuildPath` and made `MaskFor` return null;
`>= kSceneDamage` gated the bag diff and damage ring in `FrameEnd` (now
`SceneDamageSupported()`); `>= kSceneSkip` gated `skip = identical`. Windows
read the level from `WinPaintOptions`, every other platform from
`SceneLevelFallback` in `scene.cpp`. The scroll-layer toggle was
`ScrollLayersTakeArg` plus a `GPUI_SCROLL_LAYERS` read in `ScrollLayersOn`,
and the path-cache profiling switch was `TranslationIndependentPathCache`,
which chose between `PathRec::cacheHash` with the path's origin and
`PathRec::hash` with none.
