# Scene graph: scope, value and next steps

> **Status (Nov 2026): historical in parts.** The measurements below were
> taken when Windows also had custom D3D11/D3D12 renderers and the scene had
> `skip` and `damage` levels. Since then:
>
> - The D3D11/D3D12 renderers are removed; Direct2D is the only Windows
>   renderer. See [obsolete-windows-backends.md](obsolete-windows-backends.md).
> - The scene is only on or off (`__scene=off`). There is no `skip`/`damage`
>   choice; Windows uses partial damage whenever `SceneDamageSupported`.
> - Relative path-cache keys are always on. `GPUI_PATH_CACHE_TRANSLATION` and
>   `cmd/bench-scene.ts -absolute-paths` no longer exist.
>
> `src/gpui/scene.h` is the current description. The comparison with Zed's
> scene graph and the reasons not to port it still stand.

This note compares the scene support in this tree with Zed GPUI at the pinned
`zedGpui` revision (`f66ed399cdde86092af8af3dc7b418abf45f37f8`). The short
answer is that porting Zed's scene graph as a whole is not currently worth the
cost. The existing recorder has already captured the useful result for the UI
this tree draws: visibility culling, persistent Direct2D path geometry and
whole-frame comparison.

The decision should be revisited if this tree starts drawing a Zed-sized editor
with many independently dirty view entities, gains custom GPU renderers outside
Windows, or needs renderer primitives such as native surfaces and transformed
atlas sprites.

## What is here

`src/gpui/scene.cpp` records the portable `Paint.h` calls into a flat primitive
array. Each primitive carries its content mask and numeric layer. At the end of
the frame the recorder sorts the primitives, hashes and compares the frame,
culls empty or out-of-damage primitives, and replays through `Paint.h`. Paths
are retained as verbs and points, with a Direct2D geometry cache keyed by their
hash.

Windows uses the recorder for both Direct2D and the custom D3D11/D3D12 renderer.
Linux, macOS and wasm still issue `Paint.h` calls directly. The custom Windows
renderer already builds one instance buffer and batches the frame, independently
of whether the scene recorder is enabled.

Scene state is owned by each window's paint context. The previous frame, skip
decision, damage history and path cache therefore describe only that window's
surface. Path-building calls retain one non-owning pointer to the active
recorder because the public calls after `PathNew` carry `Path*` but no
`PaintCtx*`; painting is single-threaded and the pointer owns no state.

## What upstream has in addition

Upstream `scene.rs` has separate arrays for shadows, quads, paths, underlines,
monochrome sprites, subpixel sprites, polychrome sprites and native surfaces.
It records `PaintOperation`s, uses a `BoundsTree` to assign draw order to
overlapping paint layers, sorts each primitive stream, and merges the streams
into renderer batches.

More importantly, upstream scene replay is part of view retention rather than
an isolated drawing optimization. A cached view replays a range of scene
operations together with its:

- hitboxes and mouse listeners;
- input handlers and cursor requests;
- tab stops;
- shaped text layouts;
- deferred draws;
- element state and accessed-entity dependencies.

This tree rebuilds its frame-arena `El` tree, hit testing, focus and
accessibility state on every invalidated frame. Its persistent layout cache
avoids repeating unchanged Taffy work, but it has no equivalent of upstream's
cached prepaint and paint ranges. Copying `scene.rs` and `bounds_tree.rs` would
therefore port the data structures without providing upstream's retained-view
behavior.

## Cost of a full port

A faithful end-to-end port would require:

1. Typed primitive buffers, paint operations, a layer stack, `BoundsTree`,
   batch iteration, and their unit tests.
2. Per-window previous and next scenes, explicit retained resource ownership,
   resize and device-loss handling.
3. Cached view state and replay ranges spanning prepaint, paint, listeners,
   hitboxes, tab stops, text layout and entity dependency tracking.
4. Stable cross-frame handles for text, images and atlas entries. Frame-arena
   pointers cannot be retained.
5. Renderer support for shadows, quads, paths, underlines, the sprite classes
   and surfaces. A faithful GPU path would also need its shader and atlas
   machinery on every platform on which parity is promised.
6. Ordering, replay, multi-window, resource-lifetime and device-loss tests,
   real interaction benchmarks, and cross-platform visual comparisons.

For one experienced maintainer, the likely order of work is 12--20 engineer
weeks: 1--2 for the core scene, 2--3 for window/resource ownership, 3--5 for
retained view integration, 4--8 for renderer/platform parity, and 2--4 for
validation and tuning. Some phases overlap. A collection-only port that keeps
replaying through `Paint.h` is closer to 3--5 weeks, but supplies little that
the current recorder does not already do.

## Measured value

The release measurements in `src/gpui/scene.h` show:

- On the story gallery, Direct2D paint falls from 1.46 ms with the scene off to
  0.86 ms with collection and replay, then to 0.76 ms with path caching.
- The main win is culling: 799 of 1,071 recorded story primitives are already
  clipped out. Reordering provides no measurable benefit.
- Identical-frame skipping makes repeated-frame benchmarks much faster, but an
  idle application normally requests no frame in the first place.
- On the continuously changing FPS monitor, Direct2D rises from 0.82 ms with
  the scene off to 0.91 ms with replay and 1.00 ms with damage. Collection and
  hashing are overhead when reuse is unavailable.
- With the scene disabled, the custom D3D11 renderer already takes story paint
  from Direct2D's 1.625 ms to 0.582 ms. The renderer, not a fuller scene model,
  owns that batching gain.

Retained-view replay could reduce build and prepaint work that these paint-only
numbers do not measure. Its opportunity is limited by this tree's deliberately
coarse view structure: components normally build `El`s inside one screen or
page entity. If that entity becomes dirty, there are few independently clean
subviews to replay; if nothing is dirty, the window already does not redraw.

## Decision

Keep "Zed's scene graph as a whole" as a non-goal. Do not replace the current
recorder with upstream's typed scene until a concrete feature or whole-frame
profile requires it.

The worthwhile near-term work is narrower:

1. Enable the recorder on Linux, macOS and wasm only after measuring its cost
   on those backends.
2. Port `BoundsTree` and typed batches only when an ordering problem or a new
   renderer needs them.

Per-window ownership and stable resource generations were the first
implementations following this analysis. The remaining items do not commit the
project to the full retained-scene architecture.

## Interaction measurements

`bun cmd/bench-scene.ts -n=12` now drives the release Direct2D build with real
Win32 wheel, pointer and button input, plus the caret and chart timers. It
captures the already-presented client surface rather than using `PrintWindow`,
which would request another draw and conceal a stale frame. Startup frames are
excluded before each interaction starts. This run was made on 3 September 2026
on the Windows development machine used for the measurements above:

| interaction | skip median / p95 | damage median / p95 | median change | mean redraw area |
| --- | ---: | ---: | ---: | ---: |
| table scroll | 1.920 / 2.844 ms | 1.434 / 2.994 ms | -25% | 62.7% |
| button hover movement | 0.266 / 0.341 ms | 0.163 / 0.397 ms | not material | 0% |
| blinking caret | 0.686 / 1.277 ms | 0.789 / 1.890 ms | +15% | 20.8% |
| popup open/close | 0.235 / 0.867 ms | 0.264 / 1.150 ms | +12% | 13.2% |
| chart tick | 1.290 / 1.633 ms | 1.183 / 1.710 ms | -8% | 90.1% |

The hover moves caused invalidated frames but no primitive changes in this
showcase page, so both scene modes skipped every present; their sub-millisecond
difference is noise, not a damage win. Popup also produced an unchanged frame
after each changed frame, which `skip` and `damage` both rejected.

The final popup and hover captures were pixel-identical between `skip` and
`damage`. The scroll captures had no visible stale region: 0.27% of pixels had
some channel difference and only 18 pixels (less than 0.004%) exceeded the
image comparator's antialiasing tolerance. The changed pixels were spread over
text and scrollbars, consistent with partial ClearType redraw rather than old
content left on screen.

Damage therefore remains an opt-in mode, while `skip` remains the default. It
earns that limited place through the 25% scrolling median improvement, not as a
general default: caret and popup regress, chart's small median improvement does
not improve its p95, and scrolling's p95 is also slightly worse. A future
renderer or workload should rerun the command before relying on damage mode.

### Translated path cache

The interaction trace also reports per-frame path hits, misses and backend
construction time. An icon-heavy scroll through the Button story crossed the
threshold for making cache keys translation-independent: with absolute keys,
30 wheel steps produced 272 misses and path construction took 0.203 ms per
frame in `skip` mode. Relative keys reduced that to 20 first-shape misses and
0.053 ms per frame. Median paint fell from 1.600 to 1.291 ms and median total
draw from 2.268 to 2.013 ms in the same-build A/B comparison.

The implementation retains absolute path coordinates in primitive hashes and
damage bounds, but hashes and builds cached backend geometry relative to the
path's first point. Replay supplies the absolute translation separately for
fills, gradients and strokes. `GPUI_PATH_CACHE_TRANSLATION=off` restores the
old absolute keys for profiling; `cmd/bench-scene.ts -absolute-paths` drives
that comparison.

Direct2D, D3D11 and D3D12 all completed the icon-scroll interaction with the
translated cache. The `skip` and `damage` captures were pixel-identical in the
20-step Direct2D validation and in both GPU validations. Compared with the
uncached Direct2D surface, the only differences remained the already-recorded
geometry-realization edge pixels. A same-mode Direct2D comparison between
relative and absolute cache keys differed at one pixel by less than the image
comparison tolerance.
