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

Processed through `9b4989399aa2cebc2c0e70ebd09afd4032ec94e5` (2026-09-21,
list: Restore the outline on the right-clicked item (#3155)). ListItem
outlines a right-clicked item in `selection` again, keeps the selected fill
under it, and drops the hover background while an item is active; the Tree row
follows. The current update target is
`9c369db6f9b0f3754fdf5d2e4027acb1f68b1146`.

## Known gaps vs Rust

- **Upstream package names.** `crates/component` remains `src/ui/` here;
  `gpui.h` and `AppNew`/`ThemeSet` provide the Kit facade and initialization.
  Rust procedural macros and Cargo publishing have no C++ runtime counterpart.
  The GPUI reference is `gpui-pre` 0.3.6 (Zed `bcf6582ce350`); the five ported
  dependency versions are unchanged.
- **Linux centres a new window on the whole X display.** GPUI's
  `Bounds::centered` uses the display's visible bounds; Windows and macOS read
  the work area, the X11 path does not read `_NET_WORKAREA`
  (`src/gpui/window_linux.cpp`).

- **Shell stays on the portable QuickJS-NG interpreter.** Upstream Rust moved
  to the platform-specific quickjs-jit runtime in `88a1bdc8`; the C++ shell
  keeps the repository's sole vendored-source exception and identical host API
  on every target, including wasm (`src/quickjs`, `src/shell/runtime.cpp`).

- **Questionnaire keys are always fresh presses.** A key down here carries no
  held/repeat flag, no `prefer_character_input` and no IME composition state,
  so `handle_key_down`'s guards on them have nothing to read, and
  `aria_description` has no field in the accessibility node
  (`src/base/questionnaire.cpp`).
- **A styled Select does not close when focus leaves it.** Rust's
  `SelectState::on_blur` closes the menu (and emits `DismissEvent`); here only
  Escape, an outside click, the trigger and a confirm close it, so the blur leg
  of `select_emits_one_dismiss_event_for_each_open_to_closed_transition` is
  not ported (`src/ui/select.cpp`).
- **TextView's stream fade runs per top-level block.** Rust fades rendered
  byte ranges inside a leaf and staggers them word by word (character by
  character for CJK); an El has opacity only per subtree, so the block holding
  the new text fades as one unit and `stream_fade_stagger` / `StaggerStepMs`
  shape nothing on screen (`src/base/text.cpp`).
- **No text alignment on an element.** `text_center()` / `text_right()` have
  no counterpart, so a wrapped centered or trailing Marker label keeps its
  lines at the leading edge; a single run is placed by the flex box instead
  (`src/ui/marker.cpp`).
- **The styled Popover takes its surface from the caller.** It has no
  `appearance`, `popover_style().p_3()` or child list; `Content` is the whole
  styled surface. So `arrow` fills with that surface's background and outlines
  with its border, or with the ring `PopoverSurface` draws, instead of reading
  `appearance` (`src/ui/popover.cpp`).
- **`crates/component-shell` registrations are not ported.** The C++ shell
  materializes the base components; the styled Carousel, Chart and
  Questionnaire registrations and `examples/js_story` have no counterpart
  (`src/shell/runtime.cpp`).

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
