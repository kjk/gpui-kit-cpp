# Port status

What is deliberately different from the Rust, and what is still missing. Keep
it terse: one bullet per gap, the reason, and the file that owns it. When you
decide not to port something, add the bullet here instead of leaving the next
session to rediscover it. This is not a changelog — do not log what was done.

Everything in `crates/base`, `crates/component`, `crates/story`,
`crates/base/examples/showcase`, `crates/fps`, `crates/webview`,
`crates/shell` and `examples/` is ported and builds on Windows, Linux, macOS
and wasm. The portable library also cross-compiles for iOS and Android; their
application-owned native window/paint adapters remain integration work. The
work left is depth, not breadth.

## Upstream revision

Processed through `6b8581a1e5458eace91beb842376f833beaef2ff`
(2026-09-19, Version 0.6.4). Workspace crates move 0.6.2 to 0.6.4. The
current update target is `6b8581a1e5458eace91beb842376f833beaef2ff`.

## Known gaps vs Rust

- **Upstream package names.** `crates/component` remains `src/ui/` here;
  `gpui.h` and `AppNew`/`ThemeSet` provide the Kit facade and initialization.
  Rust procedural macros and Cargo publishing have no C++ runtime counterpart.
  The GPUI reference is `gpui-pre` 0.3.5 (Zed `d89e9c2124b2`); the five ported
  dependency versions are unchanged.

- **Shell stays on the portable QuickJS-NG interpreter.** Upstream Rust moved
  to the platform-specific quickjs-jit runtime in `88a1bdc8`; the C++ shell
  keeps the repository's sole vendored-source exception and identical host API
  on every target, including wasm (`src/quickjs`, `src/shell/runtime.cpp`).

- **Language configuration is callback-based.** Configured bracket pairs,
  closer skipping, pair deletion and smart Enter indentation are ported.
  Rust regexes are dependency-free function pointers here, and generated-pair
  history is not retained (`src/base/input_editor.cpp`).

- **Reduced motion is sampled at startup.** The platform preference updates
  all motion, but the Linux portal's live-change subscription and a separate
  application override are not exposed (`src/base/lib.cpp`).

- **Chart story still uses the previous fixture set.** Hover, pie lift, and
  the gallery's off-canvas sidebar are ported; the SaaS datasets, computed
  footers, legends and width-wise bar ramps from #3112 are not yet
  transcribed (`examples/story/chart.cpp`, `ChartFixtures.h`). Sankey node
  hit-testing and tooltip are not painted (`src/ui/chart.cpp`).

- **Textarea token wrap is flex-wrap, not display-map inline metrics.** A
  chip stays atomic; a text gap does not reflow character-by-character
  around it (`src/base/input.cpp`). Composer token icons map Image /
  Sparkles / AtSign onto File / Star / User
  (`examples/story/input_tokens.cpp`).

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
- **HTML parsing is complete-input, not browser-hosted.** `src/html5ever`
  builds its arena DOM from one UTF-8 `Str`; it has no incremental tendril
  feed or script-execution pause, and its named-reference table is the reader
  set rather than all generated spellings. `src/html5ever/readme.md` names the
  boundary; numeric references and the tree rules TextView consumes are on.
- **Process CPU %** is a Win32/procfs times delta, not `sysinfo`. First sample
  is 0; values are in the same ballpark, not bit-identical.
- **The scene graph is half of GPUI's.** `src/gpui/scene.h` collects and culls;
  there is no stacking context per element (layers are a field, not a tree),
  there is no offscreen mask cache, and only `paint_win.cpp` dispatches into
  it.
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
  `sysinfo` reports the tab. An opaque manual redirect is refused because the
  browser hides the target that the shell must capability-check. See the
  browser section of AGENTS.md.
- **No webview on Linux or wasm.** `src/wry/wry_linux.cpp` and `wry_wasm.cpp`
  are stubs; `src/wry/readme.md` says what a real one would take.

## Not ported, on purpose

Per-crate exclusion lists live with the crate: `src/taffy/readme.md`,
`src/markdown/readme.md`, `src/markdown-mini/readme.md`,
`src/html5ever/readme.md`, `src/html5ever-mini/readme.md`, `src/wry/readme.md`
and `src/autocorrect/readme.md`. `.claude/skills/update-port/crates.md` lists
the dependencies we replace rather than port.
