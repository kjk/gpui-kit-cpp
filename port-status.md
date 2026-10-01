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

Processed through `912f8a9b70c24aa79de696f26ab0c7ef065ed134` (2026-10-01,
docs: Document macOS font-kit requirement (#3339)). Upstream documents the
macOS font-kit requirement on the website only. The current update target is
`912f8a9b70c24aa79de696f26ab0c7ef065ed134`.

## Known gaps vs Rust

- **Upstream package names.** `crates/component` remains `src/ui/` here;
  `gpui.h` and `AppNew`/`ThemeSet` provide the Kit facade and initialization.
  Rust procedural macros and Cargo publishing have no C++ runtime counterpart.
  The GPUI reference is `gpui-pre` 0.3.7 (Zed `1a28cff4b409`); the five ported
  dependency versions are unchanged.
- **Base Root keeps its plugins on the window.** Rust's `Root` entity owns
  each plugin's entity and observes it; here `RootPlugin` is a function table
  whose per-window state lives in the window's keyed state, so a view that
  renders the surface itself through `RootSurface` finds the same
  instances. Root's Tab / shift-Tab / copy actions are the
  runtime's for every window (`FocusNext`, `WindowSelectionCopy`); the
  surface carries no `id("root")`, which would re-key every element's state;
  WindowState's `prepare` sets no rem size and its tooltip overlay is the
  window's own; and WindowExt's layers still open in a
  window with no Root (Rust panics) (`src/base/root.cpp`, `src/ui/root.cpp`).
- **Editor range decorations paint from the rows, not from one prepaint.**
  Rust projects each decoration through the shaped lines in prepaint and
  paints one path per decoration; the editor's rows are separate flex
  elements here, so the corners are measured at paint from where each row's
  run landed (`ElTextRangeRects`) and every row paints its own slice of the
  paths after its active-line wash and before its text. The collection
  methods do not notify the editor as Rust's do; the owning view re-renders.
  The geometry tests read the last frame's rows back through
  `InputLastRangeCorners`; the wrap-boundary one is not ported, since it
  needs the wrap indent of the next bullet (`src/base/input.cpp`
  RangeDecorationCorners).
- **Soft-wrapped editor lines are not indented.** Rust's default
  `WrappingIndent::Same` keeps a wrapped line's leading whitespace for its
  continuation rows: GPUI's `LineWrapper` wraps them at the width less
  that indent, and `LineLayout::wrap_indent` shifts them by it. Here each
  logical line is one text run wrapped by the platform's own layout, so a
  continuation row starts at the left edge. A hanging indent inside the
  run is a Pango (`pango_layout_set_indent`) and Core Text (head indent)
  paragraph property, but DirectWrite has none, so Windows would need a
  run made of two layouts, or the editor its own visual rows
  (`src/base/input.cpp`).

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
  the view, which is what this runtime can read back of the clip. The
  handler runs while the view is built, so a container following a
  fit-content view through `OnReveal` reads its offset after building it.
  text/state.rs's `an_enclosing_list_scrolls_to_a_line_of_a_fit_content_view`
  is not ported, and neither are the background-parse tests: a parse here
  happens with the render.
- **`selected_source_range` reads the window's painted runs.** Rust walks
  each inline state's selection; here the selection is the window's, so the
  view maps the runs it painted, which takes an inline image in whenever the
  selection covers its place in the document order rather than by Rust's
  run-boundary rule (`MdSelectedSourceRange` keeps Rust's rule for the parsed
  tree). `select_all` is the selection `SelectAll` made, for as long as the
  window still holds it. Under `-markdown=mini` the parser keeps no
  positions, so the answer is always None (`src/base/text.cpp`).
- **The window owns its text selection; the layer element is a marker.**
  `TextSelectionLayer` creates and gates nothing: `WindowSelectionOf` makes
  the state on first use, the frame sweeps registrations itself, and a
  registered participant's local selection answers queries whether or not a
  layer rendered. A scope is an element's trap id, not a stack, and a run's
  selection range cannot detect a layout shaped from different text, since
  paint.h's `TextLayout` carries no length (`src/base/text_selection.cpp`).
  Five text_selection.rs tests that turn on those are not ported.
- **A series chart paints its default hover tooltip.** Line, area, bar
  and candlestick charts are one custom-painted element, so the title and
  rows `TooltipContent::apply` would build are drawn in the paint pass
  (`PaintChartSeriesTooltip` in `src/gpui/gpui.cpp`) rather than built as
  a `plot::Tooltip`; `tooltip_content`, the radar's whole tooltip and a
  custom plot's (the story's stacked bars) are built and laid out over the
  plot while it paints (`plot::PlotOverlayAttach`, `src/ui/plot.h`). The
  tooltip closures receive the datum's index rather than the datum.
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
- **The shell's window host differs from root.rs in four places.**
  `src/shell` includes no `ui/` header (a test enforces it), ShellRoot is
  its own overlay host over gpui-base, the catalog's window opener roots a
  `gpui_shell` window at the component library's Root, and hosts mount
  through a single-mount `LoadedApplication`, all as in Rust. What still
  differs: `ShellRootOf` also finds the ShellRoot as the content of that
  Base Root, where Rust's `window.root::<ShellRoot>()` would not and a
  script's `open_dialog` in an opened component-shell window would have no
  host; the tooltip is the window's one overlay, so the shell's enter and
  switch transition dresses a component's tooltip too; a press on the
  background does not blur the focused field (`blur_on_background_press`
  needs GPUI's `default_prevented`); and `LoadApplication` takes the host's
  policy where Rust's mount always uses the default. A template still
  refuses a registered component, whose payload lives in the arena that
  recorded it (`src/shell/component_registry.h`).
- **Registered components render within one frame's description.** A typed
  part is rendered standalone and again by its parent; deferred slots,
  delegate rows and window-effect surfaces (Dialog, Sheet, ...) are rebuilt
  from the latest render rather than a leased snapshot, so an open surface
  shows the current render's content and callbacks. A retained InputState
  hands a change to at most 16 components rendering it in one frame
  (`src/component_shell/`).
- **Components that need a number before layout take last frame's.**
  Rust's uniform_list and list virtualize at prepaint from the bounds
  layout gave them; this tree's Tree (TreeList) and DataTable build their
  rows before layout, and List works out scroll_to_item and load_more
  before it. Those, native and registered, are `size_full()` as upstream
  and build with the height they were laid out at last frame
  (`UseLaidOutHeight`, `src/gpui/gpui.h`): the first frame uses the
  caller's height (a definite height in a script's style) or a default —
  320, or the window's height for a DataTable — and a change in the
  laid-out height costs one extra frame. VirtualList and MessageScroller
  bind their rows at prepaint, as upstream, and need no number. Settings
  picks its stacked layout from last frame's width the same way.
  Scroll/Scrollbar has no overlay bar reading a shared handle: the viewport
  paints its own bar from the ScrollbarHandle entity, and a Scrollbar
  placed after its viewport takes effect a frame later
  (`src/component_shell/scroll/`).
- **Registered-component numbers are ints.** The registry reads a usize as
  64 bits and checks it as Rust does, but the C++ components it hands the
  number to count in `int`, as this tree indexes everywhere: Badge's count
  and max, Rating, Pagination's pages, textarea rows, chart ticks and grid
  levels, the Carousel, Settings and typed-compound indices,
  description-list and table spans, OTP groups, calendar months and
  OtpState's length are clamped to INT_MAX, and a MessageScrollerState
  count past it is refused (Rust would allocate a row height for each).
  Only a Badge max or a page number past 2^31 would read differently; the
  rest count things no window lays out that many of.
  Chart rows are narrowed to f32. Id checks trim ASCII whitespace only.
- **Component-shell gaps against the Rust components.** MenuItem/Menu
  `disabled` and the retained forms' `disabled()` are inert (upstream
  records them as common behaviors and drops the op — ported as-is).
- **A script's `debug()` outlines without the hovered element's id.** In a
  debug build `debug()` / `debug_below()` outline the element (or every
  element painted under it) in red, as GPUI's Style::paint does, and a
  release build accepts them and changes nothing; GPUI's debug build also
  prints a hovered debug element's GlobalElementId over it, which an
  element here has no Debug form of. A start or middle ellipsis measures
  characters off the shaped run where Rust sums each one's advance, which
  differs only by kerning (`src/shell/style.cpp`,
  `TruncateTextStartOrMiddle`).
- **A focus handle is one tab stop however many elements track it.** An
  input's editor rows each track the state's handle, where upstream's state
  is one element, and so does a bare field bound to it (the code editor's,
  the shell's); Tab traversal counts a handle once, at its last element
  (`FocusNext`, `src/gpui/gpui.cpp`). The component Input's frame tracks a
  handle of its own, as upstream's does, but stays the element bound to the
  state, since its box is the field's geometry and accessibility node.
- **Font features are one flag, and the browser ignores it.** GPUI's
  `FontFeatures` is any list of OpenType (tag, value) pairs; here it is
  `FontFeatures::TabularFigures` or none — `tnum`, the one gpui-kit names —
  carried as a bit of the text weight word. Canvas2D has no
  `font-variant-numeric`, so in the browser the TimeField's digits stay
  proportional (`kFontTabularNums`, `src/gpui/paint.h`).

- **Textarea tokens still use flex wrapping instead of display-map inline
  metrics.** Text gaps can break at UTF-8 characters around atomic chips, but
  shaping, selection geometry and hit testing do not yet share Rust's fragment
  map (`src/base/input.cpp`). Nor do inline tokens have Rust's keyboard
  activation or geometry query: there is no `ActivateToken` action and no
  `range_to_bounds`, so a chip is activated only by a click on it (TokenChip).
  state.rs's `test_inline_token_wrap_and_size_refresh` and
  `test_inline_token_geometry_and_reentrant_activation` are not ported.
- **The input's touch handles and edit menu are not drawn.** touch.rs's
  touch selection is ported (`InputTouchSelection` and the edge-drag calls,
  `src/base/input.cpp`), but the styled layer draws handles and an edit menu
  only for the window's text selection (`src/ui/touch_selection.cpp`).
- **The input's right-click menu belongs to the themed field.** Rust's state
  carries `on_context_menu` and `handle_right_click_menu`, which defer the
  handler and skip a disabled field; here `BindInputContextMenu` in
  `src/ui/input.cpp` opens it, so state.rs's
  `context_menu_handler_is_deferred_and_respects_disabled` is not ported.
- **The highlighter seam has no batch update.** Rust's
  `InputHighlighter::update_batch` hands a multi-edit change over once, as
  each edit and the text after it, and the state installs highlighters
  through a factory. Here a second splice before the highlighter is asked
  collapses into one whole-document edit (`TextSplice`, `src/base/input.cpp`),
  so `test_replace_text_in_ranges_drives_the_highlighter_once` is not ported.
- **An editor's text decorations are kept beside the state.** Rust's
  `create_decorations_collection` puts the collection in the editor state's
  extras and every edit moves it; here a `DecorationCollections` is held next
  to the `InputState` and its owner calls `AdjustForEdit`
  (`src/base/input_editor.h`), so `test_editor_decorations_follow_typing` is
  not ported.
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
