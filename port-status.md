# Port status

What is deliberately different from the Rust, and what is still missing. Keep
it terse: one bullet per gap, the reason, and the file that owns it. When you
decide not to port something, add the bullet here instead of leaving the next
session to rediscover it. This is not a changelog — do not log what was done.

The shared story gallery pages in `crates/story` are ported. Everything in
`crates/base`, `crates/component`,
`crates/base/examples/showcase`, `crates/fps`, `crates/webview`,
`crates/shell` and `examples/` is ported and builds on Windows, Linux, macOS
and wasm. The portable library also cross-compiles for iOS and Android; their
application-owned native window/paint adapters remain integration work. The
work left is mostly depth.

## Upstream revision

Processed through `201b55a431fb1b82a6047e908de63913db3d4354` (2026-09-30,
chore: upgrade notify to 8.2 and harden theme watching (#3320)). Upstream
moves the theme watcher to notify 8.2, coalescing reloads and watching
non-recursively, which is what `ThemeRegistryWatchDir` does here. The current
update target is `201b55a431fb1b82a6047e908de63913db3d4354`.

## Known gaps vs Rust

- **Upstream package names.** `crates/component` remains `src/ui/` here;
  `gpui.h` and `AppNew`/`ThemeSet` provide the Kit facade and initialization.
  Rust procedural macros and Cargo publishing have no C++ runtime counterpart.
  The GPUI reference is `gpui-pre` 0.3.7 (Zed `1a28cff4b409`); the five ported
  dependency versions are unchanged.
- **Base Root keeps its plugins on the window.** Rust's `Root` entity owns
  each plugin's entity and observes it; here `RootPlugin` is a function table
  whose per-window state lives in the window's keyed state, so a view that is
  its own window root (the shell's `ShellRoot`) renders the same surface
  through `RootSurface`. Root's Tab / shift-Tab / copy actions are the
  runtime's for every window (`FocusNext`, `WindowSelectionCopy`); the
  surface carries no `id("root")`, which would re-key every element's state;
  WindowState's `prepare` sets no rem size and its tooltip overlay is the
  window's own; and WindowExt's layers still open in a
  window with no Root (Rust panics) (`src/base/root.cpp`, `src/ui/root.cpp`).
- **Theme hot reload is desktop-only and keeps what it replaced.** Rust's
  `watch_dir` watches on every non-wasm target; `src/sys/dir_watch.h` has
  no iOS or Android backend, so there the folder is read once. A reload
  re-parses into the registry's arena and never frees the documents it
  replaced — installed palettes and callers' theme names point into them —
  so each reload costs the folder's size until the App goes
  (`src/ui/theme.cpp`).
- **A dock's own edge is an in-flow strip, not a hugging resize handle.**
  Rust's docks resize from a `resize_handle(..).inside(edge)` with the
  indicator appearance; here the edge is the four-DIP strip beside the dock's
  body with a hover fill, so it shows no indicator and `HandleEdge` is used
  only by standalone handles (`src/ui/dock.cpp` SkinDock,
  `src/base/dock_area.cpp` DockBindResizeStrip). Splits inside a dock do use
  the indicator.
- **Editor range decorations paint from the rows, not from one prepaint.**
  Rust projects each decoration through the shaped lines in prepaint and
  paints one path per decoration; the editor's rows are separate flex
  elements here, so the corners are measured at paint from where each row's
  run landed (`ElTextRangeRects`) and every row paints its own slice of the
  paths after its active-line wash and before its text. The collection
  methods do not notify the editor as Rust's do; the owning view re-renders.
  element.rs's four window-driven geometry tests (scrolled viewport, wrap
  boundaries and newline cells, CRLF, folds) are not ported: this suite lays
  out no editor window (`src/base/input.cpp` RangeDecorationCorners).
- **A toolbar's items are the tab stops inside its box.** Rust constrains
  roving Left/Right focus to the toolbar's subtree through its focus handle;
  a handle here knows containment only through a focus trap, which would
  also keep Tab inside, so the toolbar records its laid-out bounds and roves
  among the tab stops whose centre lies within them (`src/base/toolbar.cpp`).

- **Shell stays on the portable QuickJS-NG interpreter.** Upstream Rust moved
  to the platform-specific quickjs-jit runtime in `88a1bdc8`; the C++ shell
  keeps the repository's sole vendored-source exception and identical host API
  on every target, including wasm (`src/quickjs`, `src/shell/runtime.cpp`).

- **Questionnaire choices carry no accessible description.** Rust sets
  `aria_description` from a choice's description; the accessibility node
  here has no field for it, so the description is visible text only
  (`src/base/questionnaire.cpp`).
- **A key down's held and IME flags come from the platform, not from a
  keystroke.** A key event here has no `key_char`, so
  `Keystroke::is_ime_in_progress` is the platform saying an input method
  has the key — Win32 `VK_PROCESSKEY`, the browser's keyCode 229, marked
  text on macOS, a printable X11 key that looked up no character — rather
  than "a printable key with no character", which on Windows also covers
  Enter and Tab. X11 marks the press after a dropped auto-repeat release as
  held, where Zed's X11 client drops the same release but reports every
  press as fresh (`KeyDownFlags`, `src/gpui/platform.h`).
- **TextView range highlights land with the render.** Rust parses in the
  background and rebuilds `RenderedText` when a parse lands; the parse here
  is synchronous inside `TextView::IntoEl`, so `RenderedText()` and the
  validation `SetRangeHighlights` does describe the last rendered text until
  the view renders again. `RenderedText::text` is borrowed from the view's
  index (Rust's snapshot holds the parsed document), so read a fresh
  snapshot's text and only compare old ones. Every parse is a full one, so
  `remap` always compares every leaf rather than Rust's `tail_only` fast
  path, and the Rust-only `an_append_after_a_full_update…` case collapses
  into the append test. The washes go through the selection painter
  (`PaintTextRange`), so inline.rs's `glyph_boxes`/`range_boxes` geometry
  tests have no C++ counterpart (`src/base/text.cpp`).
- **`reveal_range` reads back last frame's paint.** Rust's `Inline` asks the
  enclosing `gpui::list` to autoscroll during prepaint and checks the line
  against the content mask. Here the view marks the text the range starts in
  (`El::RangeOut`, a whole block through `BoundsOut`), and the next frame
  reads where it was painted: a scrollable view scrolls its own offset the
  least that shows it, and anything else calls `OnReveal`. There is no
  request_autoscroll, so an application list around a fit-content view does
  not follow by itself — hand it the line through `OnReveal`, as the
  markdown example does for its preview panel. Visibility for a fit-content
  view is the window cut down to the scroll boxes last frame painted around
  the view, which is what this runtime can read back of the clip.
- **`selected_source_range` reads the window's painted runs.** Rust walks
  each inline state's selection; here the selection is the window's, so the
  view maps the runs it painted, which takes an inline image in whenever the
  selection covers its place in the document order rather than by Rust's
  run-boundary rule (`MdSelectedSourceRange` keeps Rust's rule for the parsed
  tree). `select_all` is the selection `SelectAll` made, for as long as the
  window still holds it. Under `-markdown=mini` the parser keeps no
  positions, so the answer is always None (`src/base/text.cpp`).
- **A series chart paints its hover tooltip.** Line, area, bar and
  candlestick charts are one custom-painted element, so their tooltip is
  drawn in the paint pass (`PaintChartSeriesTooltip` in `src/gpui/gpui.cpp`)
  with the rows `TooltipContent::apply` would build, not built as a
  `plot::Tooltip`. `tooltip_content`, which hands back an element, has no
  place to go there and is not ported; the story's "Revenue vs Last Year"
  card keeps the default rows. The tooltip closures receive the datum's
  index rather than the datum. The radar paints no hover at all — no dots,
  no tooltip — so it has none of the tooltip builders, and the stacked bar
  story is four overlaid BarCharts rather than a custom `Plot`, so it has no
  `plain_row` total.
- **Base plot values are float and gradient strokes are solid.** The scales
  take `float` domains, so Rust's `PlotValue` bound (f32, f64, Decimal) has
  no counterpart, and a range is a pointer and count read as its first two
  entries. `PlotAxis` and `Grid` lines and a line's dots take a
  `Background`, but the runtime draws lines and ellipses with one color, so
  a gradient paints its first stop. There is no `PlotElement` or `Plot`
  trait: charts track hover with `TrackHover` and their appear with
  `TrackAppear` under their own id scope (`src/base/plot.cpp`), and
  `Plot::interactive` / `appear_generation` are `PlotInteractive` /
  `AppearGeneration` on each chart.
- **The styled Popover takes its surface from the caller.** It has no
  `appearance`, `popover_style().p_3()` or child list; `Content` is the whole
  styled surface. So `arrow` fills with that surface's background and outlines
  with its border, or with the ring `PopoverSurface` draws, instead of reading
  `appearance` (`src/ui/popover.cpp`).
- **`crates/component-shell` registrations are not ported.** The C++ shell
  materializes the base components; the styled Carousel, Chart, Toolbar,
  Questionnaire and TimeField registrations and `examples/js_story` have no
  counterpart (`src/shell/runtime.cpp`).
- **A script TextView's images outlive the view.** They load under the
  script's network grant as upstream's do, but the per-view owner is window
  keyed state and the decoded pixels sit in the app's encoded-image cache, so
  both go with the window rather than with the view; and the time limit is
  the transport's per request rather than one 30-second deadline over every
  redirect (`src/shell/materialize.cpp`).
- **A focus handle is one tab stop however many elements track it.** An
  input's field and its editor rows all track the state's handle, where
  upstream's frame has a handle of its own; Tab traversal counts a handle
  once, at its last element (`FocusNext`, `src/gpui/gpui.cpp`).
- **FocusLine::Inside carries no colour.** Rust's `Inside(Hsla)` takes the
  line's colour from its caller, which a filled button passes as its normal
  foreground at `FOCUS_LINE_OPACITY` (0.6). `Style` has no room for a colour
  beside the two bits `El::FocusLineStyle` records, so the runtime always
  draws the focused element's own foreground at 0.6 — the same colour for a
  focused button, but no other colour can be asked for (`FocusLine`,
  `src/gpui/gpui.h`; the focus line in `src/gpui/gpui.cpp`).
- **Font features are one flag, and the browser ignores it.** GPUI's
  `FontFeatures` is any list of OpenType (tag, value) pairs; here it is
  `FontFeatures::TabularFigures` or none — `tnum`, the one gpui-kit names —
  carried as a bit of the text weight word. Canvas2D has no
  `font-variant-numeric`, so in the browser the TimeField's digits stay
  proportional (`kFontTabularNums`, `src/gpui/paint.h`).

- **Textarea tokens still use flex wrapping instead of display-map inline
  metrics.** Text gaps can break at UTF-8 characters around atomic chips, but
  shaping, selection geometry and hit testing do not yet share Rust's fragment
  map (`src/base/input.cpp`).
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
- **No webview on Linux or wasm.** `src/wry/wry_linux.cpp` and `wry_wasm.cpp`
  are stubs; `src/wry/readme.md` says what a real one would take.

## Not ported, on purpose

Per-crate exclusion lists live with the crate: `src/taffy/readme.md`,
`src/markdown/readme.md`, `src/markdown-mini/readme.md`,
`src/html5ever/readme.md`, `src/html5ever-mini/readme.md`, `src/wry/readme.md`
and `src/autocorrect/readme.md`. `.claude/skills/update-port/crates.md` lists
the dependencies we replace rather than port.
