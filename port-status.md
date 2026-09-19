# Port status

What is deliberately different from the Rust, and what is still missing. Keep
it terse: one bullet per gap, the reason, and the file that owns it. When you
decide not to port something, add the bullet here instead of leaving the next
session to rediscover it. This is not a changelog — do not log what was done.

The shared story gallery pages in `crates/story` are ported except for
`ShellStory`, noted below. Everything in `crates/base`, `crates/component`,
`crates/base/examples/showcase`, `crates/fps`, `crates/webview`,
`crates/shell` and `examples/` is ported and builds on Windows, Linux, macOS
and wasm. The portable library also cross-compiles for iOS and Android; their
application-owned native window/paint adapters remain integration work. The
work left is mostly depth.

## Upstream revision

Processed through `6b8581a1e5458eace91beb842376f833beaef2ff`
(2026-09-19, Version 0.6.4). Workspace crates move 0.6.2 to 0.6.4. The
current update target is `6b8581a1e5458eace91beb842376f833beaef2ff`.

## Known gaps vs Rust

- **The story gallery omits `ShellStory`.** The pinned Rust gallery has 74
  components including Shell; the C++ gallery has 74 because it also exposes
  `SearchableList`, which Rust does not list separately. The quote board's
  Rust and scripted halves still need a story page (`examples/story/story.cpp`,
  `crates/story/src/stories/shell_story.rs` upstream).
- **The story's Go menu uses the sidebar search and installed-theme submenu.**
  Rust opens command and theme palette dialogs. The menu labels now match,
  but the dialog interaction and the `Ctrl+K` theme shortcut are pending
  (`examples/story/story.cpp`).
- **The story chart gallery still differs in its card grid and fixtures.**
  At the comparison window width, Rust shows four narrow pie cards in its
  second row while the port shows two wider cards; the trend percentage also
  differs (`examples/story/chart.cpp`, `examples/story/ChartFixtures.h`).
- **The story introduction renders extra README HTML in the port.** Both
  galleries read the same pinned README, but the header image and badges are
  visible only in the C++ text view (`examples/story/welcome.cpp`,
  `src/base/text.cpp`).
- **The MessageScroller story seeds row heights from text measurements.**
  Rust measures rendered rows through its virtual list; the C++ story uses
  fixed bubble padding around `MeasureText` until row measurement feeds back
  into the list state (`examples/story/message_scroller.cpp`).

- **Upstream package names.** `crates/component` remains `src/ui/` here;
  `gpui.h` and `AppNew`/`ThemeSet` provide the Kit facade and initialization.
  Rust procedural macros and Cargo publishing have no C++ runtime counterpart.
  The GPUI reference is `gpui-pre` 0.3.5 (Zed `d89e9c2124b2`); the five ported
  dependency versions are unchanged.

- **Shell stays on the portable QuickJS-NG interpreter.** Upstream Rust moved
  to the platform-specific quickjs-jit runtime in `88a1bdc8`; the C++ shell
  keeps the repository's sole vendored-source exception and identical host API
  on every target, including wasm (`src/quickjs`, `src/shell/runtime.cpp`).

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
