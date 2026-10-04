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

Processed through `4c7f1350331562436df868c55ac33bebc4c6406c` (2026-10-04,
editor: Measure the completion prefix from the typed text and drop stale
trigger offsets (#3363)). Upstream measures a completion's prefix from the
typed text and drops a stale trigger offset; a completion request here already
reads its query from the document at the caret, and hiding a menu dismisses at
once, so a newer response is never the one dismissed. The current update target
is `4c7f1350331562436df868c55ac33bebc4c6406c`.

## Known gaps vs Rust

- **Upstream package names.** `crates/component` remains `src/ui/` here;
  `gpui.h` and `AppNew`/`ThemeSet` provide the Kit facade and initialization.
  Rust procedural macros and Cargo publishing have no C++ runtime counterpart.
  The GPUI reference is `gpui-pre` 0.3.7 (Zed `1a28cff4b409`); the five ported
  dependency versions are unchanged.
- **Base Root's Tab and copy actions are the runtime's.** Base Root's Tab /
  shift-Tab / copy actions are the runtime's for every window (`FocusNext`,
  `WindowSelectionCopy`), and WindowState's tooltip overlay is the window's
  own (`src/base/root.cpp`, `src/ui/root.cpp`).
- **The editor's wrap map is a flat list, not a sum tree.** An edit re-wraps
  only the lines it touched, as Rust's TextWrapper does, but the rows sit in
  one array, so the lines after the edit have their offsets and row counts
  shifted one by one rather than through tree summaries: integer work,
  nothing measured, about 1 ms per keystroke on a 100k-line document. The
  visual rows are elements built before layout, at the column the last frame
  laid out; a column that comes out another width is built again at
  prepaint, so the frame that resizes it already shows the new wrap, but
  that frame wraps the document twice. Range decorations are measured when
  the editor's column paints, from where each visual row's run landed, and
  painted once from there under every row; the collection methods do not
  notify the editor as Rust's do, the owning view re-renders
  (`src/base/input.cpp` WrapMapCatchUp, RewrapEditorColumn,
  PaintEditorUnderlay).

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
- **A TextView's parser plugins are the view's, so it parses as it
  renders.** Parsing follows state.rs: a small replacement parses at once,
  a larger one and an append parse on the background executor while the
  last document stays up, and an append parses its last block again and
  keeps the rest (`tail_only`). But markdown extensions are set on the
  `TextView` each frame rather than on the state, and a plugin takes a
  `Ctx`, so a view with a parser plugin parses on the UI thread when it
  renders, and a state parses with the parser flags its view last rendered
  with. Updates that arrive during a parse are taken up when it lands
  rather than merged by `MAX_COALESCED_UPDATES_PER_PARSE`; a parse cannot
  fail, so there is no `parsed_error` (state.rs
  `set_text_extending_after_a_parse_error_parses_it_again` is not ported);
  and a streamed fade starts on the first frame that shows it.
  `RenderedText`'s text, source and `RangeForSource` read the view's index
  (Rust's snapshot holds the parsed document), so read and convert through
  a fresh snapshot and only compare old ones (`src/base/text.cpp`).
- **Soft wrap takes its line-break opportunities from a UAX #14 subset,
  not the `unicode-linebreak` crate.** `LineBreakOpportunities` decides the
  rules an input's text meets — spaces, Latin words and numbers, CJK
  ideographs and kana, opening and closing punctuation, quotes, hyphens,
  glue, combining marks — from the rules themselves; Thai and Lao, the
  Hangul jamo classes, regional-indicator pairs and the numeric prefix and
  postfix classes fall under the default, where a break is allowed. It
  stands beside the UAX #29 subset the token edges already use.
  `MeasuredWrapBoundaries` also starts each row's search from the cached
  character widths rather than from one grapheme, which changes how many
  prefixes are shaped and not which one is chosen (`src/base/input_editor.cpp`).
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
- **Speech input has no microphone and no system recognizer.** Rust's
  `speech` feature captures through cpal (`Microphone`) and recognizes with
  `SFSpeechRecognizer` on macOS and `Windows.Media.SpeechRecognition` on
  Windows (`SystemRecognizer`). Each wants a platform seam this tree does
  not have — WASAPI, Core Audio and ALSA capture, the Speech framework, a
  WinRT activation — and ALSA would be a new Linux dependency, so neither
  is ported: `SpeechState` has no default input and its system fallback
  finds nothing, exactly as upstream behaves without the feature. An
  application fills `SpeechRecognizer` and `AudioInput` itself;
  `SpeechAudioConverter` is the microphone's resampler, ported for it. The
  Speech story feeds its demo recognizer a generated tone on every platform
  (upstream does only on the web), its System section is always the
  disabled button, and `examples/speech` — a bench for the two missing
  seams — is not ported. A sink's deferred call and the stop timeout ride
  a window (`WindowPost`, a window timer) where Rust uses `cx.defer` and
  the executor's timer (`src/ui/speech.cpp`).
- **A TextView follows the text color named on it, not an ancestor's.**
  Rust's view reads `window.text_style().color`, which a `Bubble` or any
  other container has pushed by the time the view prepaints. The tree here
  is built child first and inherits its text color at layout, so with
  `TextViewDefaults::WithInheritTextColor` the color a view adapts to is its
  own (`TextView::Refine(style, StyleFieldColor)`); a container that fills
  its surface has to hand its text color to the view it holds.
- **A TextView's scroll layouts are flags, not `overflow` on a refinement.**
  Rust opts a table into horizontal scrolling with `overflow.x: Scroll` on
  `style.table` and a code block into vertical scrolling with `overflow.y:
  Scroll` on `style.code_block`. A refinement here names no overflow field,
  so they are `TextView::TableScroll()` and `TextView::CodeBlockScroll()`;
  the max height still comes from the `code_block` refinement.
- **`reveal_range` reads back last frame's paint.** Rust's `Inline` asks the
  enclosing `gpui::list` to autoscroll during prepaint and checks the line
  against the content mask. Here the view marks the text the range starts in
  (`El::RangeOut`, a whole block through `BoundsOut`), and the next frame
  reads where it was painted: a scrollable view scrolls its own offset the
  least that shows it, and anything else asks for it through
  `WindowRequestAutoscroll`, which a `VirtualList` with a scroll handle takes
  after binding its rows, and calls `OnReveal`. The request is that last
  frame's box, so the list reads it against where its content was then.
  Visibility for a fit-content view is the window cut down to the scroll
  boxes last frame painted around the view, which is what this runtime can
  read back of the clip. The handler runs while the view is built, so a
  container following a fit-content view through `OnReveal` reads its offset
  after building it.
- **`selected_source_range` reads the window's painted runs.** Rust walks
  each inline state's selection; here the selection is the window's, so the
  view maps the runs it painted, which takes an inline image in whenever the
  selection covers its place in the document order rather than by Rust's
  run-boundary rule (`MdSelectedSourceRange` keeps Rust's rule for the parsed
  tree). `select_all` is the selection `SelectAll` made, for as long as the
  window still holds it. Under `-markdown=mini` the parser keeps no
  positions, so the answer is always None (`src/base/text.cpp`).
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
- **A shell template refuses a registered component.** The registered
  component's payload lives in the arena that recorded it, so a template
  cannot keep it past that description (`src/shell/component_registry.h`).
  The window host is otherwise root.rs's: `ShellRootOf` is
  `window.root::<ShellRoot>()`, the root owns its own tooltip layer
  (`Window::rootTooltip`), a background press blurs the focused field, and
  an application mounts with the default policy. A focusable element taking
  a press marks it `pressTookFocus`, which `WindowDefaultPrevented` reports
  as GPUI's focus handler preventing the default does, but which still
  lets the field under it place its caret.
- **Registered components render within one frame's description.** A typed
  part is rendered standalone and again by its parent; deferred slots,
  delegate rows and window-effect surfaces (Dialog, Sheet, ...) are rebuilt
  from the latest render rather than a leased snapshot, so an open surface
  shows the current render's content and callbacks. A retained InputState
  hands a change to at most 16 components rendering it in one frame
  (`src/component_shell/`).
- **A shell Scrollbar placed after its viewport lags a frame.**
  Scroll/Scrollbar has no overlay bar reading a shared handle: the viewport
  paints its own bar from the ScrollbarHandle entity, and a Scrollbar
  placed after its viewport takes effect a frame later
  (`src/component_shell/scroll/`).
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

- **The highlighter is driven once a frame.** Rust drives `update` from each
  change and `update_batch` from each multi-edit change; here the text
  funnels log every edit with the bytes it removed, and the themed layer
  hands the log over once a frame (`InputDriveHighlighter`), so two
  keystrokes inside one frame arrive as one batch of two where Rust makes
  two calls. Past 64 edits, or 64 MB of rebuilt per-edit text, the log is
  one whole-document update. The state installs no highlighter factory: the
  themed layer installs the implementation itself (`src/base/input.cpp`,
  `src/ui/highlighter.cpp`).
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
