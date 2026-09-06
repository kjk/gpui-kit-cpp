# Editor sampling profile

Profiled the standalone `examples/editor.cpp`, starting from
`d43fab345eca5eb3ae396eb60cdf649e543cfd96`, on Windows with MSVC `/O2`,
Direct2D, the default scene `skip` mode, and a 1200 × 750 window. The document
is the bundled `assets/editor/test.rs` (27,306 bytes), with the repository as
the working directory. The saved baseline executable and matching PDB are
in `out/editor-profile/`.

## Changes supported by the traces

- Colored text previously drew the entire shaped line once for every syntax
  color, clipping each draw to that token. Scrolling spent substantial CPU
  time in Direct2D and the Intel graphics driver. Direct2D now sets temporary
  drawing effects and draws the line once. Effects are cleared before the
  cached layout is reused; no brush or render target is retained by it.
  Other backends use the existing portable range-clip implementation.
- The scene records one colored text primitive per line, retaining its layout
  and copying its text and color ranges until replay. Color and range changes
  participate in the frame hash. Payload storage is reset each frame.
- The editor's semantic decoration buffers now use their actual upper bound
  instead of allocating 4,096 spans unconditionally. On the initial fixture
  this removes 81,920 bytes from each frame's arena use: 840,824 → 758,904 bytes.
  The scene's new payload arena is separate from that frame-arena counter.
- Typing sampled the sample's `SemanticTokensFor` marker scanner heavily.
  Its five marker strings now carry their lengths, and a first-byte check
  rejects nonmatches before calling the string comparison. Match ordering
  and the delta-encoded token output remain the same.

## Sampling

Used `../winperf/out/rel64/winperf.exe`, which records xperf kernel sampling
at 1,000 Hz with stacks. ETL, Firefox profile JSON, symbolicated agent reports,
frame logs, and screenshots are retained under the gitignored
`out/editor-profile/`. Symbols came from the local matching PDBs and
`C:/symbols`.

The scroll workload sends 50 wheel notches down and 50 up, repeated three
times, with 25 ms between notches. Startup is excluded from frame statistics;
ETW includes startup and shutdown. Before/after scroll captures contain
319/318 measured frames and 6,941/3,210 samples with stacks. Inclusive
`TextLayoutDraw` samples fall from 1,425 to 704. Direct2D self samples fall
from 1,183 to 378, and Intel `igd10umt64xe.dll` samples from 2,309 to 415.
These captures identify the reduced work; they are not the timing benchmark.

The corrected typing captures (`focused-before-type` and
`focused-final-type`) contain 513/493 measured frames and 3,926/2,183 samples
with stacks. Inclusive marker-scanner samples fall from 699 (17.8%) to 43
(2.0%). Remaining typing hotspots include syntax lexing and AutoCorrect;
those were not changed.

## Repeated release measurements

Three runs per executable and workload, alternating before/after order.
The table reports the median of the three run medians, and median private
bytes at the end of each workload, converted to MiB. These runs used no
profiler. Draw time includes building, layout, and painting; it does not
include all input-handler work or measure keyboard-to-display latency.

| Workload | Before draw, ms | After draw, ms | Before private MiB | After private MiB |
| --- | ---: | ---: | ---: | ---: |
| Unchanged, 2,000 frames | 0.190 | 0.172 | 54.4 | 48.3 |
| Scroll down/up, three cycles | 6.992 | 6.139 | 58.7 | 51.3 |
| Type and erase, ten cycles | 11.875 | 4.436 | 58.2 | 51.5 |

The typing workload first clicks the editor at (318,45), then inserts
` // profiling text` and sends 18 backspaces, ten times, with 25 ms between
operations. The click establishes the keyboard focus context. Preliminary
typing captures without that click did not exercise backspace correctly and
are excluded from this table (`before-type.*` and `paired-*-type-*`). The
corrected runs are `focused-*-type-*`, summarized in `type.json`.

The desktop was active, so presentation, scheduling, caret, and hover events
vary between runs. Scroll medians were 6.179/6.992/11.299 ms before and
6.139/5.327/7.519 ms after. Typing medians were 15.185/8.703/11.875 ms before
and 4.871/4.436/2.005 ms after. Treat the percentages as results of these
workloads, not a prediction for every document. Unchanged-frame medians were
more stable: 0.193/0.188/0.190 ms before and 0.172/0.170/0.173 ms after.

Private memory consistently decreased in these runs. It includes native
text and graphics allocations, not just the C++ buffers. This measures a
smaller working allocation footprint, not a proven leak fix. The 80 KiB
frame-arena saving is directly attributable to the span-buffer change.

## Validation

- `bun cmd/build.ts -rel editor` passed.
- `bun cmd/test.ts -rel`: **23,697 checks passed**. New tests cover recorded
  color ownership, color/range invalidation, replay, and clearing drawing
  effects before reusing a layout after render-target recreation.
- `bun cmd/build-no-amalgam.ts -rel --win-backend=all` passed, compiling and
  linking all three Windows backends from separate translation units.
- Compared initial, scrolled, and soft-wrap screenshots. Baseline versus
  revised rendering changed 374/110/110 pixels (at most 0.04%), with no pixel
  exceeding the image tool's 90-channel-sum threshold. These are small
  antialiasing differences from native color-run drawing. Revised scene
  `skip` versus `off` was pixel-identical in all three captures.
- Changed C++ paths were formatted; `git diff --check` passed. Linux, macOS,
  and wasm retain their existing drawing fallback and were not run here.

## Reproduction

Build with `bun cmd/build.ts -rel editor`. Preserve the executable and PDB
together before editing. To capture unchanged frames from PowerShell:

```powershell
$env:GPUI_FRAME_BENCH = '2000'
$env:_NT_SYMBOL_PATH = 'D:\src\gpui-kit-cpp\out\rel;C:\symbols'
& ..\winperf\out\rel64\winperf.exe record -i 1000 `
  -o out/editor-profile/idle.etl --keep-etl `
  -write-agent out/editor-profile/idle.txt -- `
  D:\src\gpui-kit-cpp\out\rel\editor.exe '-gpui-window=40,40,1200,750'
```

For interactions use `GPUI_FRAME_BENCH=0` and `GPUI_INTERACTION_BENCH=1`.
The local `drive.mjs` accepts a capture label, absolute executable path,
`yes|no` for profiling, and `scroll|type`. `paired.mjs`, `type-paired.mjs`,
and `frame.mjs` automate alternating before/after runs. Each application must
finish before launching another because all write `out/gpui.log`.
