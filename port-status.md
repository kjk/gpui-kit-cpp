# Port status

What is deliberately different from the Rust, and what is still missing. Keep
it terse: one bullet per gap, the reason, and the file that owns it. When you
decide not to port something, add the bullet here instead of leaving the next
session to rediscover it. This is not a changelog — do not log what was done.

Everything in `crates/base`, `crates/component`, `crates/story`,
`crates/base/examples/showcase`, `crates/fps`, `crates/webview`,
`crates/shell` and `examples/` is ported and builds on Windows, Linux, macOS
and wasm. The work left is depth, not breadth.

## Upstream revision

Processed through `1f7f8c6549b8a3f605702f3dd07b549244f6b43f`
(2026-09-04, fps: Sustain frames by default, and stop grading the rate (#2944)). FPS HUDs sustain frames by default, with explicit continuous(false) observing application frames. The headline rate uses the foreground color; frame-cost rows retain budget grading.
The current update target is `cbdf5baa26a5c20ae5c1d7481bffdd1d0d2abd3d`.

## Known gaps vs Rust

- **Upstream package names.** `crates/component` remains `src/ui/` here;
  `gpui.h` and `AppNew`/`ThemeSet` provide the Kit facade and initialization.
  Rust procedural macros and Cargo publishing have no C++ runtime counterpart.
  The GPUI reference is `gpui-pre` 0.3.2 (Zed `801c087a`); the five ported
  dependency versions are unchanged.

- **Dock tree persistence integration.** `PaneTree::ToState` implements the
  persisted tree format, including a bare Tiles center. The older live
  `DockState` still uses its separate `DockDump`/`DockLoad` path;
  `PaneTree::FromState` and live Tiles-center reconciliation remain missing
  (`src/base/dock_state.cpp`, `src/base/dock_layout.cpp`).

- **Image loading uses bounded process caches.** `src/gpui/image.cpp` keeps
  32 resources and 16 encoded `Image` values rather than Rust's configurable
  App/entity caches. Decoded
  `RenderImage` data uses explicit main-thread retain/release rather than
  `Arc`; recorded scenes retain their images through replay. Desktop local
  reads and decoding are synchronous and use platform decoders. Windows WIC,
  macOS AppKit and the browser retain and schedule animated GIF/WebP frames;
  Linux remains PNG-only because cairo is its only image decoder.

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
  focus and animation are resolved while the tree is built. Layout *is* kept
  across frames.
- **Dialogs, sheets and notifications draw inside their window** — which is
  where Rust draws them too; they are `Root` layers, not windows. Real second
  windows do exist (`StoryOpenWindow`).
- **Text selection and virtual lists are approximations.** Selection is
  character-accurate through the platform hit-test; the virtual list
  virtualizes with a spacer rather than GPUI's `v_virtual_list`.
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
and `src/autocorrect/readme.md`. `port-upstream.md` lists the dependencies we
replace rather than port.
