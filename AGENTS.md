# gpui — C++ port of gpui-kit

Port [longbridge/gpui-kit](https://github.com/longbridge/gpui-kit) to C++ for
**Windows, Linux, macOS, iOS, Android and wasm/emscripten**. All of
`crates/base`, `crates/ui`, story, showcase, shell and examples are in scope
unless excluded below. Size or difficulty alone is not a reason to skip a port.

**Rust is the specification**, not something to compile into this project.
`bun cmd/build.ts` / `bun cmd/run.ts` installs the gitignored pinned clone at
`.work/gpui-component/`. Copy upstream widget constants (heights, gaps, colors,
column widths); do not invent a design system. Comment deliberate differences
where they occur and record them in [port-status.md](port-status.md).

Pins are at the top of [cmd/run.ts](cmd/run.ts): `gpuiComponent`, `zedGpui`,
`taffy`, `markdown`, `html5ever`, `wry`, `autocorrect`, `unicodeLinebreak`.
`bun cmd/run.ts -versions` prints/syncs them; `bun cmd/upstream-pending.ts`
lists pending checkins. To ingest updates, use
[.claude/skills/update-port/SKILL.md](.claude/skills/update-port/SKILL.md),
including its [crates.md](.claude/skills/update-port/crates.md).
[port-map.md](port-map.md) is the Base/UI ledger (`cmd/audit-port.ts`).

## Hard rules

1. **No STL data structures.** Allowed: C headers, `cstdint`, `cstring`, `new`,
   `algorithm` for `std::min`/`std::max`, `utility`, and `<coroutine>`. No
   `std::string`, `vector`, `unique_ptr`, `optional`, `function`, `unordered_map`.
   `<coroutine>` is compiler support (`std::coroutine_handle`,
   `std::suspend_never`) for `src/sys/task.h` and its users, not permission to
   use the rest of the standard library.
2. **Use SumatraPDF base types:** `Str`, `Vec<T>`, `Arena`, `StrBuilder`,
   `fmt()`, sized integers, `Func0`/`Func1`. Source of truth:
   `C:\Users\kjk\src\sumatrapdf\src\base`; curated copy: `src/base.h/.cpp`,
   `namespace base`. GPUI code uses `namespace gpui` (`gpui::component` for
   themed widgets) and a using-directive for unqualified base names. Examples
   include `gpui.h` and use `using namespace gpui;`.
   **Crate ports** (`src/{taffy,markdown,html5ever,wry,autocorrect,unicode-linebreak}`)
   depend on `base.h` only, include no gpui header and name no gpui symbol.
   `cmd/update-dist.ts` enforces this. Shared needs belong in base.
3. **Six targets, no third-party C++ libraries.** No CMake, Gradle, vcpkg,
   C++ package manager or `ext/`. Write or port what Rust gets from crates.
   - Windows: MSVC `cl.exe`, static CRT (`/MT`, `/MTd`), no redistributable
     DLLs. WinHTTP, WASAPI and WinRT speech use SDK/COM headers, no new libraries
     for the latter two.
   - Linux: g++/clang++, system X11, cairo, Pango, gdk-pixbuf via `pkg-config`.
     Soft dependencies: libcurl (remote images), `webkit2gtk-4.1` (webview),
     `alsa` (microphone), `xcursor` (image cursors), `xi` (touch and pen).
     Build without them; unavailable features stay empty.
   - macOS: clang++, Cocoa, Core Graphics, ImageIO, Core Text, IOKit,
     CoreServices/FSEvents, NSURLSession, AudioToolbox, AVFoundation, Speech.
   - iOS: Xcode iPhoneOS SDK, UIKit host. Android: pinned NDK from
     `cmd/android-install-deps.ts`, API 24+. Both build static libraries;
     the application owns lifecycle and embeds the surface.
   **QuickJS-NG is the sole vendored-source exception:** pinned in `cmd/run.ts`,
   checkout only in `.work/quickjs-ng`, reduced by `bun cmd/update-quickjs.ts`
   to tracked `src/quickjs/quickjs.h` + `quickjs.c`, compiled as C11. Ordinary
   builds never fetch it. Exclude `quickjs-libc.c`: Shell owns the sandboxed
   loader, scheduler and capability-gated system APIs. The generator strips
   comments; the local `JS_MakeError` fix for messages over 256 bytes lives in
   `src/quickjs/js_make_error.patch`. After regeneration run
   `git apply src/quickjs/js_make_error.patch`; update the patch if upstream
   moved the function.
4. **POD-friendly C++, explicit ownership.** `Vec<T>` is memcpy/POD only.
   Heap strings use `StrDup`/`StrFree` or an arena. UI trees live in a per-frame
   `Arena` and are discarded, not destructed as object graphs.
5. **No exceptions or RTTI.** Check COM HRESULTs.
6. **Read the pinned Rust when widget look or numbers are uncertain.**
7. **No OS switching in `src/**/*.cpp`.** No `#if`/`#elif` on `GPUI_OS_*`,
   including self-wrapping platform files. The suffix selects the platform:

   | Suffix | Targets |
   | --- | --- |
   | `_win.cpp`, `_linux.cpp`, `_mac.cpp`, `_ios.cpp`, `_android.cpp`, `_wasm.cpp` | Named target only |
   | `_posix.cpp` | Linux, macOS, iOS, Android **and wasm** |
   | `_mem_posix.cpp` | Hosted POSIX: Linux, macOS, iOS, Android |
   | `_inotify.cpp` | Linux and Android |

   `_posix.cpp` must compile for wasm: no `fork`, `openat` or Apple-only APIs.
   `filesystem_posix.cpp` and `process_posix.cpp` are explicit exceptions kept
   off wasm; their browser implementations are `*_wasm.cpp`.
   Split OS-dependent bodies by suffix. **Never call an OS API from a shared
   file.** Shared bodies live once under a matching suffix or feature macro.
   Small facts (separator, caret width, secondary modifier) belong in
   `PlatFacts` (`base.h`), read by one `Plat*` function in `base.cpp`;
   `base_<os>.cpp` defines `kPlatFacts` with only differing fields. No copied
   function bodies or OS ternaries in shared source.

   `cmd/update-dist.ts` adds OS guards to suffixed sources and rejects switches
   left in source. Headers may use OS guards for differing declarations/SDK
   includes. `base.h` defines exactly one of the six `GPUI_OS_*` macros as 1.

   **Write unsupported stubs once.** Module headers derive feature macros
   (`GPUI_WRY_UNSUPPORTED`, `GPUI_AUDIO_INPUT_UNSUPPORTED`,
   `GPUI_SPEECH_RECOGNIZER_UNSUPPORTED`, `GPUI_NOTIFY_UNSUPPORTED`) from OS and
   `GPUI_HAVE_*`; shared stubs go in `<module>_unsupported.cpp` under that
   macro. Shared one-line no-ops/constants are header `inline`s under the same
   kind of guard (`PlatHasMenu`, `PlatReduceMotionKnown`, `wry::EventLoopPrepare`).
   Do not add iOS/Android to a macro unless they already had the symbol; their
   host adapters supply missing facilities.

## Non-goals

Other omissions must be recorded in `port-status.md`.

- **General async runtime:** `sys/task.h` has one `Task`, one executor awaitable
  and an owner-checking resume registry. No combinators, `select`, cancellation
  tokens, per-operation awaitables or value-carrying `Task<T>`. `sys/executor.h`
  provides foreground/background callbacks, main-thread queue, worker pool and
  integer cancellation handles. `TaskCancel` is drop; `TaskGuard` checks the
  generational owner. No destructor-cancels task or refcounted entities.
- **Zed's whole scene graph:** the collection half (`gpui/scene.h`) is on.
  Its own renderer was implemented as D3D11/D3D12 backends and removed; see
  [obsolete-windows-backends.md](obsolete-windows-backends.md) for measurements
  and revival requirements, `port-status.md` for gaps.
- **General networking:** `sys/http.h` sends one HTTP(S) request with method,
  headers and body using WinHTTP/NSURLSession/libcurl. No session, cookie jar,
  keep-alive, sockets, WebSockets or own TLS. **`shell/fetch.h` owns policy:**
  authorize host/path and every redirect before following; never downgrade
  HTTPS to plaintext; never replay a non-GET body across origins; never forward
  `Authorization` or any caller-supplied header off-origin.
- **STL containers and third-party C++ dependencies:** tree-sitter, syntect,
  LSP client, resvg, ropey. Write the small needed implementation or port the
  crate; see the update-port `crates.md`. `wry_win.cpp` declares WebView2 COM
  bindings and implements Microsoft's loader instead of vendoring them.

## Layers and platform seams

| Path | Role |
| --- | --- |
| `examples/` | View entities with static `Render(self, cx)`; story/showcase |
| `src/ui/` | Themed crates/ui facade (`component::*`) |
| `src/base/` | Unstyled crates/base primitives; `text.h` owns TextView |
| `src/gpui/` | App, Window, entities, El tree, input, timers, layout, paint, SVG/assets, keymap, scene |
| `src/base.h/.cpp` | SumatraPDF subset: Str, Vec, Arena, geometry, Color |
| `src/sys/` | Metrics, executor, coroutine registry, HTTP, microphone, speech |
| `src/taffy/`, `src/markdown/`, `src/html5ever/` | Independent crate ports |
| `src/markdown-mini/`, `src/html5ever-mini/` | Smaller local alternatives |
| `src/wry/`, `src/autocorrect/`, `src/unicode-linebreak/` | Independent crate ports |
| `src/webview/` | crates/webview: gives a wry webview a box |
| `src/shell/`, `src/component_shell/` | Sandboxed JS apps; component catalog (`component_registry.h`) |
| `src/quickjs/` | Generated C11 QuickJS-NG and local patch |
| `src/fps/` | crates/fps performance HUD |
| `gpui_shell/`, `tests/`, `bench/`, `assets/`, `web/shell.html` | Host, tests, benchmarks, assets, wasm page |

Platform implementations use the suffixes above. Main headers/seams:
`base.h` (`Plat*`), `gpui/paint.h` (`paint_*`), `gpui/platform.h` (`window_*`),
`sys/sysinfo.h`, `sys/http.h`, `sys/dir_watch.h`, `wry/wry.h`,
`sys/audio_input.h`, `sys/speech_recognizer.h`. Mobile hosts provide drawing,
window, metrics, HTTP, webview, microphone and speech adapters. Directory
watching uses inotify on Linux/Android, native Windows/macOS/iOS files and a
wasm stub. Webview/microphone have wasm stubs; speech has Linux/wasm stubs.

`gpui/window_common.cpp` owns shared frame drawing, input dispatch and app
lifecycle; platform windows call it. Examples never name an OS API: implement
`int GpuiMain(int argc, char** argv)`; runtime supplies `wWinMain`/`main`. Use
`Key*` constants in `gpui.h` (Win32 values, translated from X11 keysyms).

### Mobile

`bun cmd/mobile-build.ts -android` builds arm64-v8a `libgpui.a` at API 24.
On Windows install/verify the pinned, checksum-checked NDK with
`bun cmd/android-install-deps.ts`.
It installs beside Android Studio NDKs; honors `ANDROID_NDK_HOME`,
`ANDROID_NDK_ROOT`, `ANDROID_HOME`, `ANDROID_SDK_ROOT`.
`bun cmd/mobile-build.ts -ios` builds arm64 iOS 15 using
`xcrun --sdk iphoneos` on a Mac with Xcode. Hosts supply views/lifecycle/adapters;
archives are independent of Gradle/Xcode project files.

### Drawing, scene and layout diagnostics

Windows has one renderer: `paint_win.cpp`, Direct2D on D3D11 with flip-model
swap chain, WARP fallback, DirectWrite ClearType and WIC decoding.
`gpui/scene.h` collects flat primitives with masks/layers, hashes against the
previous frame, then replays through `paint.h`. Unchanged frames skip paint;
Windows supports partial damage (`SceneDamageSupported`). **Try `__scene=off`
first for stale drawing; read the header before changing it.**

`GPUI_FRAME_BENCH=<n>` draws n frames and reports build/layout/paint costs and
layout-cache counts. Use it for frame-time questions. For stale layout try
`__layout_reuse=off` / `GPUI_LAYOUT_REUSE=off` (see Layout below).

### Browser

`bun cmd/build.ts -wasm <example>` builds; `bun cmd/run.ts -wasm <example>`
builds, serves and opens it. Wasm must be served. Emscripten discovery:
`$EMCC`, `$EMSDK`, PATH, sibling `.emsdk`, only for wasm builds. Drawing uses
Canvas2D without a library. `web/shell.html` fills the viewport with
`gpui-canvas`; embedding pages may reposition it and pointer coordinates are
translated through its bounding box. Assets preload at MEMFS `/assets` and
are read with `fopen`. `Module.set_theme(true|false)` switches the story theme.

`EM_JS` bodies are preprocessor-stringified: **no empty `''`, regex literals
or backslashes outside string literals**. Use `""` and `new RegExp` from a
string. JS platform state lives in `globalThis.__gpui`, referenced by integer IDs.

Browser constraints: one window; `AppRun` never returns (main-loop unwinding),
so nothing after it runs. No threads: `ExecSpawn` queues on the main thread;
check `ExecHasThreads()`, but write workers as if elsewhere. No blocking
`HttpGet`; async HTTP uses browser fetch/CORS. Refuse opaque manual redirects
whose targets cannot be authorized. `ImageDecode` returns before decode;
loads repaint. SVG uses `gpui/svg.h` draw ops. Clipboard is a DOM-paste-kept
mirror; paste events drive the chord. sysinfo reports the tab. Arenas reserve
4 MB rather than 64 via `PlatArenaReserveSize()`.

## App, Window, Entity, Ctx

Read before touching `src/gpui`, adding examples or stateful widgets.
`App` owns factories/fonts/windows/entity state; `Window` owns native handle,
render target, frame arena, hover/focus and root view. `Entity<T>` is a POD
**generational handle**, not refcounted. `Ctx` is `{app, win, a, self}`.

| Rust operation | C++ |
| --- | --- |
| `impl Render for T` | `static El* T::Render(T* self, Ctx* cx)` |
| `cx.new(...)` | `EntityNew<T>(app)` |
| `cx.listener(...)` | `Listen(cx, &T::Handler)` / `ListenTo(entity, &T::Handler)` |
| Listener captures `ix` | `Listen(cx, &T::Handler, ix)` |
| `cx.notify()` | `Notify(cx)` |
| `cx.observe(...)` | `Observe(cx, e, &T::OnChanged)` / `ObserveTo` |
| Emit / subscribe | `EntityEmit`, `Subscribe` / `SubscribeTo`, `EntityUnsubscribe` |
| `impl EventEmitter<E> for T` | `template <> struct EventEmitter<T, E> {};` |
| Drop state | `~T()` when entity is dropped |
| `window.use_keyed_state` | `KeyedState<T>(cx, key)` |
| Spawn without await | `WindowPost(win, Listener)` |
| Background work | `ExecSpawn(work, done)`; done posts to main thread |
| Await background work | `co_await BackgroundSpawn(w)` in `Task` (`sys/task.h`) |
| Cancel task | `ExecCancel(id)`, `TaskCancel(handle)`, or stale owner |
| Timer | `WindowSetTimeout(win, ms, Listener)` |

A view is a plain struct with state, static Render and static handlers.
Create an app with `AppNew`, then `WindowOpenView`, `AppRun`, `AppFree`;
`AppRunView` is the single-window shorthand. No hook table. Typical button:
`component::Button::New(cx, StrL("go"))->Label(StrL("Go"))
->OnClick(Listen(cx, &Example::OnGo))->IntoEl()`; handler signature:
`static void OnGo(Example* self, Ctx* cx, const ClickEvent*)`.
Call `Notify(cx)` after changing state.

- **State lives in an entity**, never a static or `void*` payload. One view
  type per page/screen; independent states are separate entities.
- **`Ctx*` is the first argument of element-building helpers.** Use `cx->a`;
  do not pass `Arena*` around. Static view Render uses the signature above.
- Elements own listeners; dispatch resolves the handle and drops stale events.
  No click-ID dispatch switches. Bind captured values as `intptr_t` arguments
  to `Listen`; components that produce values use `ListenerArg`.
- `El::Click(id)` is **identity only** for hit-testing, hover, focus, Tab traps.
  IDs need only be unique among siblings; rows use their position.
  `WindowOnUnhandledClick` handles outside clicks. For press/drag use
  `OnMouseDown`/`OnMouseUp`/`OnDragMove`; the pressed element retains moves
  until release.
- Window input is view-bound subscription, one per event type:
  `WindowOnKey`, `WindowOnMouseDown`/`Up`/`Move`/`Exit`, `WindowOnScrollWheel`.
  Platform code builds `PlatformInput` for `WindowDispatchInput`. Timers use
  `WindowSetInterval`/`WindowSetTimeout`; any number, with handles for
  `WindowCancelTimer`; stale views drop callbacks.
  **Workers never touch entities, windows or frame arenas.** Use `ExecSpawn`
  then a main-thread `WindowPost` listener.
- **Caret blinking is entity state:** `BlinkStart`/`Stop`/`Pause`/`Visible`,
  one per field; focus/blur starts/stops it and runtime hands it off for
  `win->input`. Never sample `TimeNow()` at paint or use `AppRequestAnim`
  for a caret; that is for real animation.
- `Notify(cx)` marks the entity, runs observers and invalidates windows that
  rendered it last frame (`Window::rendered` / Rust `dirty_views`). An entity
  not yet rendered falls back to the originating window, or all windows when
  there was none.
- `Entity<T>::Get` returns null after recycling; **check it**. Specialize
  `EventEmitter<T,E>` before typed emit/subscribe. Multiple event types per
  entity are allowed and runtime channels stay separate. Keep self handles
  typed `Entity<T>`; reducing to `EntityId` loses compile-time event checks.
- Geometry is `Point`/`Size`/`Bounds`/`Edges`, DIP floats. Use these when passing
  geometry whole, flat fields when writing components individually. Other
  units get named structs (`WinSize`), not templates.

`gpui/keymap.*` implements keystroke parsing, bindings, multi-stroke chords,
contexts (`key=value`) and predicates (`Editor && mode == full`,
`Workspace > Editor`, `!`, `||`, parentheses). `WindowDispatchKeyAction`
resolves the focused context stack and walks handlers outward. Components
own their context/init/key bindings; applications do not wire them manually.
Extra state lives in a keyed entity. The focused `InputState` gets chords
first as the innermost editing context.

## Layout

`src/taffy/` ports the Cargo.lock version of taffy used by GPUI: flexbox,
CSS Grid, blocks and floats, plus `tests/TaffyTests.cpp`. It is `namespace taffy`; do not confuse its Style/Overflow/Position/Display with GPUI's.
`LayoutEl` (`src/gpui/gpui.cpp`) translates `gpui::Style` to `taffy::Style`,
calls `ComputeLayoutWithMeasure`, writes boxes back. Text/icons/images/progress
bars are measured leaves. Fixed positioning, anchors, relative insets and
scroll offsets are applied around taffy; read the comment above `LayoutEl`.
**Do not special-case CSS rules in `LayoutEl`; fix style translation or taffy.**

The window's `LayoutCache` persists the taffy tree, reconciling **by position**
each frame. Update only changed styles (`operator==` ports Rust PartialEq)
or measured content (text/font/weight/line height/wrap/image). A cached
`PerformLayout` skips the subtree, so color-only hover costs no layout.
Changed measured leaves replay past measurement questions and stay clean if
all answers match (`held=` in bench output). Virtual rows use separate
`LayoutRowCache`s keyed by list/index, retained for 240 unclaimed frames.
`__layout_reuse=off` / `GPUI_LAYOUT_REUSE=off` disables all reuse; try it for
stale layout. `GPUI_FRAME_BENCH` reports made/dropped/restyled/remeasured counts.

Rust UI/story uses `.flex_1()`: grow 1, **shrink 1, basis 0**.
**Use `El::Flex1()`**; `Grow()` leaves basis auto and sizes by content.
`Grow(f)` is for factors other than 1.

## Markdown

`src/markdown/` ports markdown-rs (version in `crates/ui/Cargo.toml`), CommonMark
and GFM, tables, footnotes, task lists. It is `namespace markdown`, with names
that overlap GPUI (CharKind, Name, Link, Point, Node).
`MdParse` (`src/base/text.cpp`) calls
`markdown::ToMdast(a, source, ParseOptions::Gfm())`, then folds mdast into MdNode,
matching `crates/base/src/text/format/markdown.rs`. Raw HTML goes through
`base/text_format.cpp` and html5ever into the same tree. **Do not special-case
CommonMark rules in `MdParse`.**

`base/text.h` owns rich text parsing/rendering, `TextViewState`, selection;
it reads no theme. Colors come from `TextViewStyle`; highlighting is opt-in
`CodeBlockHighlighter`. `ui/text.h` re-exports under `component::`, derives the
style from Theme and installs it plus `ui/syntax.h` as `TextViewDefaults` on
theme changes. `-markdown=mini` selects the local `markdown-mini` (shared
markdown.h/mdast); `-html=mini` selects `html5ever-mini` (same html5ever.h API).
These are not upstream crates; preserve their smaller contracts in their
respective `readme.md` files.

## Code style and porting

- Include the base header first. Use structs with explicit ownership.
- `TempStr s = fmt("%.1f%%", cpu);` uses the temp arena; never free it.
  Functions returning `TempStr` use a `Temp` suffix; `fmt()` is the exception.
- Own heap `Str` only across frames (`StrDup`/`StrFree`). `Vec<T>` holds POD,
  not string graphs: use `char name[kMax]` or arena-backed Str in elements.
- Read Str/Vec/ArenaVec lengths with **`len(s)` / `len(v)`, not `.len`**.
  Fields are for constructors and growth/reset implementations.
- `logf` for debug prints; prefer `int32_t` indexes, `int` for existing base
  APIs such as `len(v)`.
- **UTF-8 Str throughout our APIs.** Convert using `ToCWstrTemp(s)` only at
  OS calls needing UTF-16; do not widen signatures to `wchar_t*`. Open,
  remove and rename files by path with `PlatFileOpen`/`PlatFileRemove`/
  `PlatFileRename`, not `fopen`/`remove`/`rename` (ANSI on Windows).
- Pair successful COM Create with Release; no `CComPtr`.
- Port Base primitives one Rust module at a time into `src/base/`, preserving
  type names. Primitives own interaction (click/focus/open/checked), not paint;
  callers style via `.Bg()`, `.Border()`, `.H()`, `.Child()`.
  **Use existing primitives instead of styled Div trees.** Add the smallest
  missing GPUI capability before the widget that needs it.
- Missing base APIs come from `C:\Users\kjk\src\sumatrapdf\src\base`.
  Do not copy app-level CrashHandler, GdiPlusUtil, Http, Zip, etc.; our HTTP
  seam is `src/sys/http.h` against system libraries.

## Build, run, test

```sh
bun cmd/build.ts                         # list targets
bun cmd/build.ts -rel showcase           # example last
bun cmd/build.ts -dbg -all
bun cmd/build.ts -rel -asan system_monitor
bun cmd/build.ts -clang -rel showcase    # clang-cl on Windows
bun cmd/build.ts -wasm system_monitor
bun cmd/build-no-amalgam.ts -rel         # one object/source; header check
bun cmd/build-no-amalgam.ts -clang -rel
bun cmd/mobile-build.ts -android -rel
bun cmd/mobile-build.ts -ios -rel
bun cmd/mac-build.ts -ios -rel           # remote Mac
bun cmd/test.ts                          # accepts -dbg / -rel
bun cmd/bench.ts                         # release, 10 samples
bun cmd/bench.ts -small -large -n=3 grid/deep
bun cmd/clang-tidy.ts
bun cmd/run.ts -dbg hello_world
bun cmd/run.ts -wasm story
bun cmd/run.ts -rel story -- __scene=off
```

`cmd/build.ts` is the whole build on every platform; run.ts imports it so flags
and output dirs agree. Windows finds cl.exe on PATH or discovers VS using
vswhere/default roots (newest first), then imports vcvars64.bat's environment.
Neither script downloads unneeded dependencies. Outputs: `out/rel`, `out/dbg`,
`out/rel_asan`, `out/rel_clang`, host-specific `out/linux/`, `out/mac/`.
`-clean` rebuilds that output dir.

run.ts adds `-debugger` (WinDbg/cdb on Windows, gdb/lldb on Linux, lldb on Mac),
explicit `-windbg`/`-cdb`/`-gdb`/`-lldb` (missing named debugger is an error with
install command, never a silent fallback), `-compare` (build/launch Rust twin),
`-versions`, `-no-build`, wasm `-no-open` / `-port N`. No `-all` on run.ts.
Runtime strips `-gpui-window=X,Y,W,H`, `__layout_reuse`, `__scene` before
example argument parsing. `GPUI_LAYOUT_DUMP=lay.txt` dumps each laid-out frame;
`GPUI_TODAY=YYYY-MM-DD` makes calendar screenshots reproducible.

Linux setup: `bash cmd/ubuntu-install-deps.sh` (`--build-only` skips runtime
setup). Windows can use `bun cmd/wsl-run.ts -rel system_monitor` via WSLg.
macOS needs Xcode command line tools; `bun cmd/mac-build.ts -rel -all` snapshots
to a scratch branch and compiles via ssh on a remote Mac (no GUI without a
login session).

CI builds all examples, tests and non-amalgamated sources on Windows/Linux/Mac;
Windows cross-compiles Android, Mac cross-compiles iOS. Separate clang lanes
build every example with `-markdown=mini -html=mini` and run the tests with
both the mini and the full parsers; a test of what the mini contract omits is
gated on `TestMarkdownIsFull()`. Wasm builds supported examples and
runs tests under node. `GPUI_NO_RUST_TREE=1` skips cloning Rust in CI.

**Warnings are errors:** `/W4 /WX`, `-Wall -Wextra -Werror`. Fix them, never
add suppressions. Existing documented exceptions are amalgam unused-functions
(`/wd4505`, `-Wno-unused-function`) and `/wd4996`.
**After changing .cpp/.h/.ts, run `bun cmd/format.ts <changed paths>`.**
No paths reformats the whole tree: avoid unrelated churn. `-ts`/`-cpp` limits
the language. C++ uses Chromium-based clang-format, 80 columns; TS uses
Prettier, 120 columns, LF. Never format `.work/` or `out/`; generated tables
and vendored QuickJS are skipped.

### Tests and benchmarks

Tests port Rust modules, one file per module with provenance. Use plain test
functions called by `tests.cpp`, `utassert` and the counter in `tests/Test.h`;
no self-registration. Only tests pinning ported code belong here. **For pure
logic add a seam instead of opening a window** (e.g. `FrameSamplerIngest`).
Window-driven Rust `#[gpui::test]` ports use **`src/gpui/test_app.h`**:
`TestAppNew`, `TestWindowOpen`, `TestDraw`, `TestSimulateKeystrokes`/`Input`/`Click`,
`TestDispatchAction`, `TestRunUntilParked`, `TestAdvanceClock`, clipboard pair.
This is real App/Window render/layout/paint through `WindowDrawFrameHeadless`,
without OS window/paint target, with simulated clock and in-memory clipboard;
it runs on all targets including wasm.

`bench/` ports taffy's flex/grid/construction benchmarks; our MarkdownBench
splits tokenize/to_mdast. Setup is untimed, rows report median/minimum.
**Use release numbers; run benchmarks after changes to `src/taffy/` or
`src/markdown/`.** Small examples conceal asymptotic regressions.

### Memory leaks (Windows)

Use `bun cmd/deleaker-scroll.ts` for editor.exe scroll down/up/down, with PDBs
(`bun cmd/build.ts -rel editor`). DeleakerConsole lives at
`C:\Program Files (x86)\Deleaker\DeleakerConsole.exe`; **`--run` must be last**
because later arguments belong to the launched process. Compare **two live
snapshots**, not `--export-xml-report-on-exit`: AppFree may free growing memory.
`.dsnapshot` is SQLite (Snapshot, AllocationGroup, StackTrace, StackEntry).
Rising group hit counts with a stack in src/ identify leaks. First snapshot
is slow due to symbol downloads.

## The distributed amalgam

`cmd/update-dist.ts` makes `gpui.h` + `gpui.cpp`, copies separately compiled
QuickJS, and makes standalone header/source pairs per crate under `extras/`.
Pair headers inline base.h behind `GPUI_BASE_H_`, shared with gpui.h, allowing
either include order. **`extras/autocorrect/` links beside gpui.cpp**, taking
base from it, only in targets that use it (editor/tests). Taffy, markdown,
markdown-mini, html5ever, html5ever-mini, wry and unicode-linebreak are already
inside gpui.cpp; their standalone pairs carry base implementation and **must
never link beside gpui.cpp**.

Private headers are behind `GPUI_INCLUDE_PRIVATE_API` (default 0); gpui.cpp
and internal tests define it to 1 before the first include. Include graph
classification: a crate header is public iff an outside header transitively
includes it.

**All builds/tests/CI compile `.work/` amalgams**, concatenated with comments
and `#line 1 "src/..."` markers. The published copy is a separate repo,
[gpui-kit-cpp-dist](https://github.com/kjk/gpui-kit-cpp-dist), cloned into
`.work/gpui-kit-cpp-dist`. **Only manually running `bun cmd/update-dist.ts`
refreshes publication:** sync clone, write pairs, build all examples against
it (`GPUI_AMALGAM_DIR`, `out/*_dist`), fill readme SHA, commit and push.
**Never regenerate published output during a build, test or commit.**
`buildDist()` requires an explicit outDir; automatic callers must use `.work`.
The publication script replaces dirty/non-main clones rather than repairing
them and commits every reported change, so stray files would be published.

Published text strips comments, collapses blanks and deduplicates/lifts
includes: portable first, guarded platform blocks below portable code
(X11 defines None/Window). The generator adds OS guards; source files do not.
SDK headers stay within their target; Apple implementations are Objective-C++.

The snapshot also contains examples, gpui_shell, assets, web/shell.html,
build.ts, run.ts, winapi.ts, mac-window-place.m and generated .gitignore so
`bun run.ts story` works in a fresh clone. **Edit `readme-dist.md` here**, never
the published readme; `<checkin-sha1>` is filled on export.

**build.ts/run.ts are copied verbatim.** Detect dist via
`existsSync(scriptDir + "/gpui.h")`; root, amalgamDir, outputs, targets and
usage follow that. Import update-dist.ts **dynamically**, since it does not
exist in dist (`ensureAmalgam`/`build` are async). Resolve script dependencies
against **scriptDir**, not `<root>/cmd/`. Keep both scripts runnable in both
trees; the manual publication command checks all examples against the snapshot.

## Additional tools

- `cmd/audit-port.ts`: Base/UI declaration/export/test ledger; CI runs it.
- `cmd/update-shell-types.ts`: shell TypeScript declarations.
- `cmd/gen-theme-data.ts`, `cmd/gen-locale-data.ts`: upstream JSON to tables.
- `cmd/shot.ts`: screenshots (`-click=X,Y`, `-hover`, `-half=left|right`).
- `cmd/imgdiff.ts`: compare shots/dirs, exit 1 on differences; `-skip=32` for
  title bars, `-bbox` for widget bounds, `-tol` for antialiasing tolerance.
- `cmd/compare-story.ts`, `compare-showcase.ts`, `compare-ui.ts`: Rust/C++ shots.
- `cmd/story-parity.ts`: gallery order, titles, section titles, visible text.
- `cmd/parity.ts`: Rust/C++ interaction/visual checks, `out/parity/results.{json,md}`.
- `cmd/vec-log.ts`: instrument Vec/ArenaVec and analyze capacity needs.
- `cmd/svg-to-bytecode.ts`: assets/icons to gpui/asset_icons.cpp.
- `cmd/crlf-to-lf.ts`: normalize line endings after scripted edits.
- `cmd/gen-compile-commands.ts`: compile_commands.json for clangd.
