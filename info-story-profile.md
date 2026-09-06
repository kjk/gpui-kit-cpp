# Story sampling profile — 2026-09-06

The Chart page repeatedly created shaped text for labels, including the Sankey
charts below the viewport. Every new layout also received a new scene resource
generation, so an otherwise unchanged page failed the scene comparison and
repainted. Plot and Sankey labels now use the existing per-window text cache.
The scene retains recorded text layouts until it releases the recording: a
custom painter can release its own reference before replay starts.

Sankey labels now follow the pinned Rust `chart/sankey_chart.rs` behavior of
calling `truncate_text_to_width` before positioning the label. Overlong labels
end in an ellipsis instead of being clipped. No widget dimensions or colors
were changed.

## Method

- Baseline: `01b4ca1fd20655c46c4f4bf9a1066640b100dd21`; original executable and
  matching PDB preserved in `out/story-profile/story.{exe,pdb}`.
- Windows, Intel Core Ultra 7 265U, MSVC release `/O2`, default Direct2D backend
  and `__scene=skip`, window argument `-gpui-window=40,40,1100,750`.
- `../winperf/out/rel64/winperf.exe`, xperf ETW sampling at 1,000 Hz, with
  stack walks and local PDB symbolication. Each recording launches Story with
  `GPUI_FRAME_BENCH=1200` and ends when Story exits.
- Pages: Introduction (rich Markdown), Button, Table, Chart, and Editor.
  The preliminary `text` captures duplicate Introduction because an unknown
  Story slug falls back to that page; they are excluded.
- ETW samples include startup and shutdown. Frame timings discard the first
  30 frames. These are repeated unchanged-frame workloads, not scroll latency
  or idle CPU measurements. The scene is still collected and compared on
  frames whose replay is skipped.
- Final timing comparisons alternate the original and changed executable,
  three runs each, 600 measured frames per run, without ETW or compilation
  running alongside them. Private memory is process private bytes reported
  at the end of each run, not a live-allocation or leak report.
- The desktop was in use during the experiment; scheduling, hover state and
  presentation affected timings. Use the paired results and the eliminated
  layout creation work together, rather than comparing isolated ETW timings
  on the unchanged control pages.

## Sampling findings

The original Chart recording had 13,324 samples, of which 3,059 (22.96%)
included `gpui::TextLayoutNew`. The changed recording had 3,402 samples,
of which 14 (0.41%) included it. Both requested 1,200 measured frames.
The scene skipped 1 of 1,230 frames before, versus 1,227 of 1,230 afterward.
Reported private bytes in those captures were 101,801,984 and 58,687,488.
This is a reduction in observed process memory, not evidence of a fixed leak.

Introduction, Button, Table and Editor had no single comparable hotspot. Their
application samples were spread across element construction, layout
reconciliation and style comparison, paint-tree traversal, scene path hashing
and accessibility hashing. Those paths are unchanged by this optimization.

## Alternating runs without the profiler

Each timing is the median of three run medians, in milliseconds. Private
memory is the median of the three end-of-run readings, in MiB.

| Page | Before ms | After ms | Before private MiB | After private MiB |
| --- | ---: | ---: | ---: | ---: |
| Introduction | 2.651 | 2.766 | 63.1 | 83.7 |
| Button | 2.375 | 0.700 | 79.2 | 79.0 |
| Table | 0.745 | 0.435 | 48.4 | 48.3 |
| Chart | 10.103 | 0.981 | 95.8 | 55.9 |
| Editor | 0.459 | 0.501 | 52.5 | 52.4 |

Chart's three before medians were 10.726 / 9.234 / 10.103 ms; after medians
were 1.005 / 0.940 / 0.981 ms: approximately 90% less steady-frame time and
42% less observed private memory. Its frame arena remained 776,736 bytes.

The control pages do not establish a general speed or memory improvement.
Introduction's changed-binary private readings ranged from 61.1 to 84.1 MiB;
its README loads remote images whose completion was not pinned. A follow-up
4,000-frame run allowed both binaries to reach the same 1,045-primitive
scene and measured **83.76 MiB before / 83.70 MiB after**. The initially
higher after median was not reproduced once both runs reached that state.
Those longer-run medians were 3.888 / 4.544 ms; all three timing phases rose,
including the unchanged build and layout code, so they do not isolate the
cost of text retention. Button and Table also varied substantially between
recordings and runs. The readings are retained in `paired.json` and
`settled-introduction-*.log`.

An actual scrolling pass also improved: 30 wheel notches down and 30 back up,
35 ms apart, using `GPUI_INTERACTION_BENCH=1` and excluding startup frames.
Three alternating runs per executable produced median-of-medians draw times
of **9.912 ms before / 5.066 ms after** (49% less). The individual run medians
were 8.711 / 9.912 / 9.917 ms before and 5.066 / 4.542 / 5.270 ms after.
Unlike the unchanged-frame workload, scrolling still presents changed frames;
its final private memory was approximately 70.0 MiB before and 71.1 MiB after.
`scroll.mjs`, `scroll.json` and `scroll-*.log` retain the driver and readings.

## Validation

- `bun cmd/build.ts -rel story` succeeded with warnings treated as errors.
- `bun cmd/test.ts -rel`: 23,678 checks passed. New checks cover recorded text
  surviving its caller and another scene's release, stable plot/Sankey labels
  preserving frame identity, and color/alignment changes invalidating it.
- Compared the old and new binaries with `__scene=off` at three scroll
  positions on all five pages, plus the bottom Sankey section. Fifteen of
  sixteen captures were pixel-identical below the title bar. The remaining
  1,091 differing pixels were confined to the two overlong Sankey labels
  that now truncate with an ellipsis.
- Also compared the final Chart with scene recording on and off at four
  scroll positions. These were not pixel-identical: 0.01–0.60% of pixels
  differed, with 2–7 pixels per capture exceeding the image tool's default
  channel-sum threshold of 90. The captures use PrintWindow to exclude
  overlapping desktop windows; they check drawing, not stale presentation.

## Reproduction and artifacts

Build with `bun cmd/build.ts -rel story`. In an elevated PowerShell shell:

```powershell
$env:GPUI_FRAME_BENCH = '1200'
$env:_NT_SYMBOL_PATH = 'D:\src\gpui-kit-cpp\out\rel;C:\symbols'
& ..\winperf\out\rel64\winperf.exe record -i 1000 `
  -o out\story-profile\chart.etl --keep-etl `
  -write-agent out\story-profile\chart.txt -- `
  D:\src\gpui-kit-cpp\out\rel\story.exe chart '-gpui-window=40,40,1100,750'
Remove-Item Env:\GPUI_FRAME_BENCH
```

The local `out/story-profile/` directory contains the original and final ETLs,
Firefox Profiler JSON files, symbolicated `before-*.txt` / `final-*.txt`
reports, frame logs and PNG captures. `paired.mjs` runs the alternating timing
comparison and writes `paired.json`; `capture.mjs` captures pages and scroll
positions. These diagnostic artifacts are gitignored.
