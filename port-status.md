# Port status

What is deliberately different from the Rust, and what is still missing. Keep
it terse: one bullet per gap, the reason, and the file that owns it. When you
decide not to port something, add the bullet here instead of leaving the next
session to rediscover it. This is not a changelog — do not log what was done.

The shared story gallery pages in `crates/story` are ported. Everything in
`crates/base`, `crates/component`,
`crates/base/examples/showcase`, `crates/fps`, `crates/webview`,
`crates/shell`, `crates/component-shell` and `examples/` is ported and builds on
Windows, Linux, macOS
and wasm. The portable library also cross-compiles for iOS and Android; their
application-owned native window/paint adapters remain integration work. The
work left is mostly depth.

## Upstream revision

Processed through `87d10ae5e1299d1be18670a37570c5402f952e34` (2026-10-05,
kit: Update GPUI to gpui-pre 0.3.8 and prepare v0.7.1 (#3370)). The workspace
packages are 0.7.1. The editor wrapper and inline flow pass
`IndentAdjustment::SameIndent`, which is the continuation indent already
applied here, and the shell scroll regression reads laid-out bounds rather
than a release-disabled debug selector. The current update target is
`288767cc730ca4977852a7860f52a8465a61876f`.

## Known gaps vs Rust

- **Upstream package names.** `crates/component` remains `src/ui/` here;
  `gpui.h` and `AppNew`/`ThemeSet` provide the Kit facade and initialization.
  Rust procedural macros and Cargo publishing have no C++ runtime counterpart.
  The GPUI reference is `gpui-pre` 0.3.8 (Zed `279fe070bb38`); the five ported
  dependency versions are unchanged.
- **Base Root's Tab and copy actions are the runtime's.** Base Root's Tab /
  shift-Tab / copy actions are the runtime's for every window (`FocusNext`,
  `WindowSelectionCopy`), and WindowState's tooltip overlay is the window's
  own (`src/base/root.cpp`, `src/ui/root.cpp`).
- **Editor range decorations do not notify their owning view.** Decorations
  are measured and painted at the editor column's current visual rows, but
  collection methods require the owning view to re-render
  (`src/base/input.cpp` PaintEditorUnderlay).

- **Shell stays on the portable QuickJS-NG interpreter.** Upstream Rust moved
  to the platform-specific quickjs-jit runtime in `88a1bdc8`; the C++ shell
  keeps the repository's sole vendored-source exception and identical host API
  on every target, including wasm (`src/quickjs`, `src/shell/runtime.cpp`).

- **A key down's held and IME flags come from the platform, not from a
  keystroke.** A key event here has no `key_char`, so
  `Keystroke::is_ime_in_progress` is the platform saying an input method
  has the key — Win32 `VK_PROCESSKEY`, the browser's keyCode 229, marked
  text on macOS, a printable X11 key that looked up no character — rather
  than "a printable key with no character", which on Windows also covers
  Enter and Tab. X11 marks the press after a dropped auto-repeat release as
  held, where Zed's X11 client drops the same release but reports every
  press as fresh (`KeyDownFlags`, `src/gpui/platform.h`).
- **TextView parser callback payloads are caller-owned.** Parser tables and
  renderer names are copied out of frame storage into state and parse-job
  arenas; callback payloads must outlive outstanding jobs and be safe to read
  on the executor. The legacy `MdPlugin` hook takes a `Ctx` and remains a
  render-time block replacement; Markdown parser plugins run on the background
  executor for appends and large replacements. Updates arriving during a parse
  are taken up when it lands rather than bounded by
  `MAX_COALESCED_UPDATES_PER_PARSE`; a parse cannot fail, so there is no
  `parsed_error`, and streamed fades start on the first frame that shows them.
  `RenderedText` reads the view's index rather than retaining the parsed
  document: read and convert through a fresh snapshot, only compare old ones
  (`src/base/text.cpp`).
- **Measured soft wrap probes cached character widths first.** Line-break
  opportunities come from the full `unicode-linebreak` 0.1.5 port (Unicode
  15.0), matching Rust. `MeasuredWrapBoundaries` starts each row's search
  from the cached character widths rather than from one grapheme, changing
  how many prefixes are shaped, not which one is chosen. Grapheme filtering
  still uses this tree's UAX #29 subset (`src/base/input_editor.cpp`).
- **A token's hover exit is reconciled as the field builds, not at
  prepaint.** Rust's `prepaint_tokens` compares the retained hover against
  the token elements it has just placed. The rows here are elements built
  before layout, so a field reconciles when it starts building its rows and
  reads the ones the last build placed: an exit for a token that was
  removed, replaced, masked or disabled arrives in the same frame, one for
  a token scrolled out of the built rows a frame later. Re-entry under a
  still pointer works by giving the chip a new id (`hoverEpoch`) rather than
  by resetting GPUI's retained element state (`src/base/input.cpp`,
  `src/base/input_tokens.cpp`).
- **Speech's platform halves are written against the OS, and two are
  untested on hardware.** Rust captures through cpal and reaches WinRT and
  the Speech framework through crates; `src/sys/audio_input.h` is WASAPI, an
  AudioQueue and ALSA directly, and `src/sys/speech_recognizer.h` is
  `Windows.Media.SpeechRecognition` through the SDK's ABI headers (combase
  loaded by name) and `SFSpeechRecognizer`. Both are always compiled in,
  where Rust puts them behind its `speech` feature; ALSA is a soft
  dependency (`GPUI_HAVE_ALSA`), and without it Linux has no microphone.
  The macOS microphone asks an AudioQueue for 48 kHz mono rather than
  taking the device's own format. A recognizer's results are posted to the
  main thread with `ExecPost` where Rust awaits them in a task, and a
  sink's deferred call and the stop timeout ride a window (`WindowPost`, a
  window timer) where Rust uses `cx.defer` and the executor's timer. The
  wasm build has neither half, so the Speech story feeds its demo
  recognizer a generated tone there and on a Linux build without ALSA.
  The WASAPI capture was run end to end; the Windows recognizer, the macOS
  halves and ALSA capture compile and pass their unit tests but have not
  been run against a speech pack, a granted microphone or a sound card.
- **`examples/speech` asks the tree for what Rust asks cpal and Cargo.** The
  input device's name comes from `Microphone::DeviceNameTemp()`
  (`SysAudioInputDeviceName`), which Rust's `Microphone` does not have: the
  Rust example calls cpal itself, and an example here names no audio API. On
  ALSA that name is `default`. The three extra icons are files under
  `assets/speech/icons` rather than embedded, the usage descriptions are
  `examples/speech_Info.plist`, linked into the macOS binary by
  `cmd/build.ts` where Rust has a `build.rs`, and the demo recognizer is
  `examples/speech_demo.h` so `tests/SpeechDemoTests.cpp` can reach it. The
  window has no minimum size (Rust: 760 × 520) because no window here has
  one. `speech --check` prints to stdout, which a Windows GUI build only has
  when its output is piped or redirected. The example was run on Windows
  with no input device and no speech pack, so a whole dictation — demo or
  system — has not been seen end to end.
- **A TextView's scroll layouts are flags, not `overflow` on a refinement.**
  Rust opts a table into horizontal scrolling with `overflow.x: Scroll` on
  `style.table` and a code block into vertical scrolling with `overflow.y:
Scroll` on `style.code_block`. A refinement here names no overflow field,
  so they are `TextView::TableScroll()` and `TextView::CodeBlockScroll()`;
  the max height still comes from the `code_block` refinement.
- **TextView reveal scrolling settles on the following layout.** The root
  checks the current frame's line against the active content mask and invokes
  `OnReveal` during that paint. Its own scroll offset, or an enclosing
  VirtualList's scroll handle, is updated then and laid out on the next frame;
  Rust can scroll and prepaint the line again within the same frame
  (`src/base/text.cpp`, `src/base/virtual_list.cpp`).
- **TextView selection mapping uses painted runs.** The window owns selection
  rather than each parsed inline node. Source mapping includes images when a
  selected run reaches their boundary, and Select All follows committed
  appends until the selection moves. Under `-markdown=mini` the parser keeps
  no positions, so source mapping is unavailable (`src/base/text.cpp`).
- **The window's selection of painted text needs no layer.** A participant
  registers only while a `TextSelectionLayer` is rendering, as Rust's
  `WindowSelectionState::existing` asks, but the selection of the runs the
  frame paints is the window's own and works in a window with no layer,
  which Rust's would not. Participants register as their view builds rather
  than at prepaint, so a registration made before the layer renders on a
  window's first frame is dropped. A run under no `text_selection_scope`
  takes its focus trap as its scope, which is what confines a dialog's text:
  the dialogs name no scope of their own (`src/base/text_selection.cpp`,
  `ElSelectionScope` in `src/gpui/gpui.cpp`).
- **A chart's tooltip datum is what `Data(..)` gave it.** The charts hold
  numbers rather than mapping a `data: Vec<T>`, so the datum the tooltip
  closures receive is an item of what `Data(..)` gave the chart, or the
  chart's own number for the point (`ChartTooltipContent::Datum`,
  `src/gpui/gpui.h`).
- **Base plot values are float.** The scales take `float` domains, so
  Rust's `PlotValue` bound (f32, f64, Decimal) has no counterpart, and a
  range is a pointer and count read as its first two entries. There is no
  `PlotElement` or `Plot` trait: charts track hover with `TrackHover` and
  their appear with `TrackAppear` under their own id scope
  (`src/base/plot.cpp`), and `Plot::interactive` / `appear_generation` are
  `PlotInteractive` / `AppearGeneration` on each chart. A gradient stroke
  on a line or area runs over the box of the points the path passes
  through, where GPUI's spans the tessellated path's bounds; a natural
  curve's overshoot between two points is outside it.
- **The shell window host follows root.rs.**
  `ShellRootOf` is `window.root::<ShellRoot>()`, the root owns its own tooltip layer
  (`Window::rootTooltip`), a background press blurs the focused field, and
  an application mounts with the default policy. A focusable element taking
  a press marks it `pressTookFocus`, which `WindowDefaultPrevented` reports
  as GPUI's focus handler preventing the default does, but which still
  lets the field under it place its caret.
- **Registered typed parts can render twice.** A typed part is rendered
  standalone and again by its parent. A retained InputState hands a change
  to at most 16 components rendering it in one frame (`src/component_shell/`).
- **Shell scrollbars use the viewport's integrated bar.** Scroll and
  Scrollbar share current-frame axis and visibility settings regardless of
  sibling order, but `viewport_from_layout` uses the Scroll viewport's box
  rather than a separately laid-out overlay (`src/component_shell/scroll/`).
- **Registered-component counts are held to INT_MAX, which reads the same.**
  The registry reads a usize as 64 bits and checks it as Rust does, and the
  numbers a script can see are 64-bit: a Badge's count and max,
  Pagination's pages, a chart's values (f64 for its tooltip and labels) and
  a listener's argument. The other usizes reach components that count in
  `int` and are clamped to INT_MAX, which behaves as Rust's value does for
  any input a window can show: a selected index (tabs, toggle groups,
  steppers, a settings page) or a span past the last item selects or spans
  the same; a tick margin past the data labels the same points; and a count
  that is rendered (Rating stars, OTP groups, calendar months, radar grid
  levels, textarea rows) hangs at either size, as Rust's would. A
  MessageScrollerState or OtpState length past INT_MAX is refused, since
  this tree's int-indexed Vec cannot hold it; Rust would abort allocating
  it (`src/component_shell/compound/common.cpp`).
- **Component-shell gaps against the Rust components.** MenuItem/Menu
  `disabled` and the retained forms' `disabled()` are inert (upstream
  records them as common behaviors and drops the op — ported as-is).
- **A start or middle ellipsis measures off the shaped run.** It measures
  characters off the shaped run where Rust sums each one's advance, which
  differs only by kerning (`src/shell/style.cpp`,
  `TruncateTextStartOrMiddle`).
- **Font features are one flag, and the browser ignores it.** GPUI's
  `FontFeatures` is any list of OpenType (tag, value) pairs; here it is
  `FontFeatures::TabularFigures` or none — `tnum`, the one gpui-kit names —
  carried as a bit of the text weight word. Canvas2D has no
  `font-variant-numeric`, so in the browser the TimeField's digits stay
  proportional (`kFontTabularNums`, `src/gpui/paint.h`).

- **The highlighter state has no factory.** The themed layer installs the
  implementation itself. A multi-edit change past 64 edits or 64 MB of
  reconstructed per-edit text falls back to a whole-document update
  (`src/base/input.cpp`, `src/ui/highlighter.cpp`).
- **No language server.** Every seam in `input/editor/lsp` is ported —
  completion, resolve, ghost text, hover, code actions, document colours,
  semantic tokens, go-to-definition — but there is no JSON-RPC, no child
  process and no `lsp_types` (hard rule 3). A provider is a function pointer
  an application fills.
- **Syntax colouring is a scanner, not a parser.** `src/ui/syntax.cpp` in
  tree-sitter's place: comments, strings, numbers, keywords, type names, and
  what position alone settles. Nothing that needs a tree (rename, semantic
  scope) can be asked of it. Folding is brace-pair scanning, which is what
  upstream's own showcase highlighter does.
- **The Base showcase's editor is plain text with a gutter.** Upstream's
  page installs its own syntect highlighter (`syntect_highlighter.rs`,
  a third-party crate) for Rust, colours its captures through
  `ShowcaseHighlightStyles`, and turns on folding; here the page draws the
  same text with line numbers, whitespace markers and the showcase editor
  style only (`examples/showcase/editor.cpp`).
- **Process CPU %** is a Win32/procfs times delta, not `sysinfo`. First sample
  is 0; values are in the same ballpark, not bit-identical.
- **The scene graph is still smaller than GPUI's.** `src/gpui/scene.h`
  collects, orders and culls through per-element stacking contexts. It caches
  offscreen coverage bitmaps for solid fills and round-cap strokes up to 160
  device pixels per side; gradients, other strokes and larger paths keep the
  retained geometry path. Every paint backend records.
- **A repaint rebuilds the whole element tree.** `Notify` picks the right
  windows (see AGENTS.md), but an `El` is arena-allocated per frame, so hover,
  focus and animation are resolved while the tree is built. Layout _is_ kept
  across frames.
- **Dialogs, sheets and notifications draw inside their window** — which is
  where Rust draws them too; they are `Root` layers, not windows. Real second
  windows do exist (`StoryOpenWindow`).
- **Text selection is character-accurate through the platform hit-test.**
  A `Scrollbar` pairs by element id; the shared-slot `SharedScroll` enum and
  its scroll-area-versus-list collision warning have no counterpart.
- **Icons fall back.** `assets/icons/*.svg` are Lucide's own files; where the
  folder is missing, `DrawIcon`'s stroke sketches cover every `IconName`.
- **wasm is not a desktop** — one window, `AppRun` never returns, no threads,
  no blocking `HttpGet`, browser-async HTTP subject to CORS, async image
  decode, clipboard mirror, no semantic DOM projection for the canvas, and
  `sysinfo` reports the tab. The story page accepts `?story=slug&dark=1` for
  Rust's embedded story shape; keyboard and paste stay scoped to its canvas,
  which can sit anywhere on a host page. The host changes appearance live with
  `Module.set_theme(dark)`. An opaque manual redirect is refused because the
  browser hides the target that the shell must capability-check.
  See the browser section of AGENTS.md.
- **macOS webview drag positions account for the child view.** The pinned
  Wry backend flips a window-relative point using the view's height; this
  port first converts it into the child view, then normalizes the top-left
  origin (`src/wry/wry_mac.cpp` emitDrag).
- **No webview on wasm.** `src/wry/wry_wasm.cpp` is a stub. The Linux one,
  `wry_linux.cpp` over WebKitGTK 4.1 (a soft dependency), is compiled and
  linked but not yet seen running in a window; without
  `libwebkit2gtk-4.1-dev` it is a stub too. `src/wry/readme.md` has both.

## Not ported, on purpose

Per-crate exclusion lists live with the crate: `src/taffy/readme.md`,
`src/markdown/readme.md`, `src/markdown-mini/readme.md`,
`src/html5ever/readme.md`, `src/html5ever-mini/readme.md`, `src/wry/readme.md`
and `src/autocorrect/readme.md`. `.claude/skills/update-port/crates.md` lists
the dependencies we replace rather than port.
