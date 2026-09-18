# gpui — C++ port of gpui-kit

A C++ port of [longbridge/gpui-kit](https://github.com/longbridge/gpui-kit)
targeting **Windows, Linux, macOS, iOS, Android and the browser**
(wasm/emscripten). The goal
is to port as much of the Rust as this tree can hold: every module of
`crates/base` and `crates/ui`, the story gallery, the showcase, the shell and
the examples. Assume a thing is in scope until a rule below says otherwise; the
answer to "should we port X?" is yes unless X needs a dependency or a runtime
we have ruled out.

The Rust sources are the **specification**, not something to compile: a
gitignored clone at `.work/gpui-component/`, installed at the pinned SHA by
`bun cmd/build.ts` or `bun cmd/run.ts`.

**Upstream pins** live in the pin block at the top of [`cmd/run.ts`](cmd/run.ts)
(`gpuiComponent`, `zedGpui`, and the five crates we port: `taffy`, `markdown`,
`html5ever`, `wry`, `autocorrect`). `bun cmd/run.ts -versions` prints and
syncs them.
Ingesting a later checkin: [`port-upstream.md`](port-upstream.md).

**Fidelity is the bar.** When a widget's look or numbers are in question, read
the Rust file under `.work/gpui-component/` at the pinned SHA and copy the
constants — heights, gaps, colors, column widths. Do not invent a different
design system. Where this tree must differ, say so in a comment where it
differs and add a bullet to [`port-status.md`](port-status.md).

Other maps: [`port-status.md`](port-status.md) (known gaps and deliberate
deviations), [`port-map.md`](port-map.md) (the Base/UI module ledger and
`cmd/audit-port.ts`).

## Hard rules

1. **No STL data structures.** Allowed: C headers, and the C++ headers
   SumatraPDF already uses (`cstdint`, `cstring`, `new`, `algorithm` for
   `std::min`/`std::max`, `utility`), plus `<coroutine>`. Do not introduce
   `std::string`, `std::vector`, `std::unique_ptr`, `std::optional`,
   `std::function`, `std::unordered_map`.

   `<coroutine>` is compiler support, not a library: `std::coroutine_handle`
   and `std::suspend_never` are how the language spells `co_await`, the
   compiler accepts no substitute, and the header brings no container,
   allocator or exception. It is for `src/sys/task.h` and what builds on it,
   not an opening for the rest of the standard library.
2. **Use SumatraPDF base types.** `Str`, `Vec<T>`, `Arena`, `StrBuilder`,
   `fmt()`, sized integer types, `Func0`/`Func1`. Source of truth:
   `C:\Users\kjk\src\sumatrapdf\src\base`; a curated copy is `src/base.h` /
   `src/base.cpp` in `namespace base`. Everything else in `src/` is
   `namespace gpui` (themed widgets in `gpui::component`) and takes base in
   with a using-directive, so gpui code writes `Str` unqualified. Examples
   `#include "gpui.h"` and `using namespace gpui;`.

   `src/taffy`, `src/markdown`, `src/html5ever`, `src/wry` and
   `src/autocorrect` are ports of crates that have never heard of gpui: they
   are written against `base.h` and nothing else, include no gpui header and
   name no gpui symbol.
   `cmd/update-dist.ts` fails the build if that stops being true. Anything one
   of them needs from the tree belongs in `base`, or it does not belong to them.
3. **Six targets, no third-party C++ libraries.** Windows: MSVC `cl.exe`,
   static CRT (`/MT`, `/MTd`) — no redistributable DLLs — plus WinHTTP. Linux:
   g++/clang++ with system X11, cairo and Pango via `pkg-config`, and libcurl
   the same way when installed (the one soft dependency; without it the tree
   builds and only loses remote images). macOS: clang++ with Cocoa, Core
   Graphics, Core Text, IOKit, NSURLSession. iOS: the Xcode iPhoneOS SDK and a
   UIKit host. Android: the pinned NDK in `cmd/android-install-deps.ps1`, API
   24 or newer, and an app-owned native host. Mobile builds are static
   libraries: the application owns lifecycle and embeds the GPUI surface. No
   CMake, Gradle, vcpkg, or C++ package manager, no `ext/`. What Rust gets
   from a crate, this tree writes itself or
   ports. **QuickJS-NG is the sole vendored-source exception**: pinned in
   `cmd/run.ts`, checked out only under `.work/quickjs-ng`, reduced by
   `bun cmd/update-quickjs.ts` to the tracked `src/quickjs/quickjs.h` +
   `quickjs.c`, compiled as C11. Ordinary builds never fetch it.
   `quickjs-libc.c` is excluded — Shell owns its sandboxed module loader,
   scheduler and capability-gated system APIs.
4. **POD-friendly C++.** Structs with explicit ownership. `Vec<T>` is
   memcpy/POD only. Heap strings are `Str` owned by `StrDup`/`StrFree` or an
   `Arena`. Frame UI trees allocate from a per-frame `Arena` and are discarded,
   not destructed as a graph of C++ objects.
5. **No exceptions, no RTTI.** COM uses HRESULT checks.
6. **When unsure about a widget's look or numbers, read the Rust file.**
7. **Portable by default.** `GPUI_OS_WINDOWS` / `_LINUX` / `_MAC` / `_IOS` /
   `_ANDROID` / `_WASM` are for the handful of places where a single
   expression differs. Anything
   larger gets a portable signature in a shared header and one implementation
   per platform. **Never call an OS API from a shared file.**

## Non-goals

Standing exclusions. Everything else in gpui-kit is in scope, and a
module being large or unglamorous is not a reason to skip it. A thing not
ported for a reason *other* than these belongs in `port-status.md`.

- **A general-purpose C++ async runtime.** Coroutines themselves are in:
  `src/sys/task.h` is one `Task`, one awaitable over the executor, and a
  registry that checks the owner on the single resume path. What stays out is
  everything around it — combinators, `select`, cancellation tokens, an
  awaitable per operation, a `Task<T>` carrying a value. `src/sys/executor.h`
  is GPUI's foreground/background pair as callbacks: a queue the main thread
  drains, a pool that fills it, integer handles where Rust has a
  destructor-cancels handle. An owner here is an entity with a generation
  rather than a C++ scope, so `TaskCancel` is the drop and `TaskGuard` the weak
  handle Rust upgrades on resume.
- **Zed's scene graph as a whole.** Two halves are here. `src/gpui/scene.h` is
  the collection half and is on: a frame's drawing as a flat array of
  primitives, each with its content mask and layer, hashed against last frame.
  `src/gpui/paintgpu.h` is the renderer half — one instance buffer a frame, SDF
  rounded rects and borders, a glyph atlas, stencil-and-cover paths — compiled
  only under `WIN_BACKEND_D3D11`/`_D3D12`/`_ALL`. What is missing is in
  `port-status.md`; both headers say what they are worth.
- **No refcounted entities and no `Task<T>` that cancels by being dropped.** We
  do have `App`/`Window`/`Entity`/`Ctx`, actions, a keymap, `EventEmitter`,
  window subscriptions and an executor.
- **STL containers** (rule 1).
- **A general network.** `src/sys/http.h` sends one http(s) request with the
  OS's own client (WinHTTP, NSURLSession, libcurl) and reads the answer, with
  method, request headers and body — enough that `crates/shell`'s `fetch` is
  ported whole. No session, cookie jar, keep-alive, socket, WebSocket or TLS of
  ours; each wants a bigger reason than tidiness. **`src/shell/fetch.h` is the
  security-carrying part**: what may be sent to which host on which path is the
  capability policy's decision, a redirect is authorized before it is followed,
  HTTPS never continues onto plaintext, a non-GET never replays its body across
  origins, and neither `Authorization` nor any caller-supplied header follows a
  redirect off its origin.
- **Anything needing a third-party C++ library** (rule 3): tree-sitter and
  syntect, an LSP client, resvg, ropey. Where Rust reaches for one
  and the feature is worth having, write the small version this tree needs or
  port the crate the way `src/taffy`, `src/markdown`, `src/html5ever`,
  `src/wry` and `src/autocorrect` are ported. `port-upstream.md` lists which
  is which.
  `src/wry/` is the worked example of the second route: WebView2's COM bindings
  and Microsoft's loader are declared and written out in `wry_win.cpp` rather
  than vendored.

## The layers

```
examples/*.cpp        a view entity with static Render(self, cx)
src/ui/               themed crates/ui façade (component::Button, …)
src/base/             crates/base unstyled primitives; TextView in base/text.h
src/gpui/             App, Window, entity store, El tree, hit-test, timers,
                      frame arena; layout via src/taffy, paint via paint.h,
                      the OS window via platform.h
src/taffy/            the taffy crate, ported — every El box goes through it
src/markdown/         the markdown crate, ported — every TextView parses with it
src/sys/              metrics, executor, coroutine registry, http, fetch table
src/base.h            Str, Vec, Arena, Geom, Color
```

## Portability

`src/base.h` defines the six `GPUI_OS_*` macros from compiler predefines;
exactly one is 1. Seams:

| Seam                               | Shared header          | Windows           | Linux                  | macOS             | iOS                  | Android              | wasm                  |
| ---------------------------------- | ---------------------- | ----------------- | ---------------------- | ----------------- | -------------------- | -------------------- | --------------------- |
| memory, paths, strings, self usage | `src/base.h` (`Plat*`) | `base_win.cpp`    | `base_linux.cpp`       | `base_mac.cpp`    | host adapter + POSIX | host adapter + POSIX | `base_wasm.cpp`       |
| 2D drawing and shaped text         | `src/gpui/paint.h`     | `paint_win.cpp`   | `paint_linux.cpp`      | `paint_mac.cpp`   | host adapter         | host adapter         | `paint_wasm.cpp`      |
| the OS window and its event loop   | `src/gpui/platform.h`  | `window_win.cpp`  | `window_linux.cpp`     | `window_mac.cpp`  | UIKit host           | Android host         | `window_wasm.cpp`     |
| system metrics                     | `src/sys/sysinfo.h`    | `sysinfo_win.cpp` | `sysinfo_linux.cpp`    | `sysinfo_mac.cpp` | host adapter         | host adapter         | `sysinfo_wasm.cpp`    |
| one HTTP request                   | `src/sys/http.h`       | `http_win.cpp`    | `http_linux.cpp`       | `http_mac.cpp`    | host adapter         | host adapter         | `http_wasm.cpp`       |
| a webview in the window            | `src/wry/wry.h`        | `wry_win.cpp`     | `wry_linux.cpp` (stub) | `wry_mac.cpp`     | host adapter         | host adapter         | `wry_wasm.cpp` (stub) |

`_posix.cpp` is the shared suffix for Linux, macOS, iOS, Android **and** wasm,
since
emscripten's libc answers for strings, directories, threads and the clock. What
it cannot answer is mmap with a reserve/commit split, so that half is
`_mem_posix.cpp`; every hosted POSIX target takes that half.

### Mobile

`bun cmd/mobile-build.ts -android` cross-compiles `libgpui.a` for arm64-v8a
at Android API 24 using the pinned NDK. On Windows, install or verify it with
`powershell -ExecutionPolicy Bypass -File cmd/android-install-deps.ps1`; the
script checks the vendor archive checksum and installs beside Android Studio's
side-by-side NDKs. `ANDROID_NDK_HOME`, `ANDROID_NDK_ROOT`, `ANDROID_HOME`, and
`ANDROID_SDK_ROOT` are honored.

`bun cmd/mobile-build.ts -ios` cross-compiles an arm64 iOS 15 `libgpui.a`
using `xcrun --sdk iphoneos`; it therefore runs only on a Mac with Xcode. The
mobile application supplies the native view, lifecycle and platform adapters.
The compile target is deliberately independent of Gradle and Xcode project
files so any host can consume the archive.

`src/gpui/window_common.cpp` holds everything a window does that is not the OS
window — frame drawing, input dispatch, the app lifecycle — and every platform
file calls into it.

An example never names an OS API. It implements `int GpuiMain(int argc, char**
argv)`; the runtime provides `wWinMain`/`main`. Key codes are the `Key*`
constants in `gpui.h` (Win32 `VK_*` values, which X11 maps keysyms onto).

### Three Windows backends

Define exactly one of `WIN_BACKEND_DIRECT2D`, `WIN_BACKEND_D3D11`,
`WIN_BACKEND_D3D12`; with none defined, Direct2D wins for its mature driver
path, WARP fallback and ClearType. `WIN_BACKEND_ALL` compiles all three and
keeps the process-start `__paint=d2d|d3d11|d3d12` selector; fixed builds ignore
an unavailable choice. Build flag: `--win-backend=d2d|d3d11|d3d12|all`.

`paint_win.cpp` is Direct2D on a D3D11 device over a flip-model swap chain —
the default, and what screenshots use. `paintgpu_win.cpp` is GPUI's own shape:
shape and content mask evaluated in the pixel shader, path fills through
stencil-and-cover. CPU batching, shaders, atlas and path machinery are shared;
only native resource and command submission differ. The D3D12 half owns its
queue, triple-buffered allocators, persistent upload heaps and descriptor
tables — no D3D11On12. `__msaa=1|2|4|8` sets the sample count.
`WinPaintOptionsGet()` is the typed accessor for all of it; Windows fills it
while stripping the reserved arguments before `GpuiMain`.

All three share everything device-independent — the DirectWrite factory and
formats, the `IDWriteTextLayout` behind `TextLayout*` (so shaping, measurement,
hit-testing and range rects are one implementation), WIC decode — and Direct2D
and the D3D11 half also share the device. Read `src/gpui/paintgpu.h` before
touching either: it has the measured numbers, why it is not the default, and
the remaining dash gap. `bun cmd/gpu-parity.ts` compares all three backends and
stress-tests resize plus injected device recovery on both custom ones.

The four SM5 entry points are `src/gpui/paintgpu_win.hlsl` and are **not**
compiled at startup. `bun cmd/update-win-shaders.ts` runs FXC with `/O3 /WX`
and rewrites the checked-in `paintgpu_shaders_win.cpp` (basE95 DXBC in raw
strings, decoded once into BSS) only when the output changes. `cmd/build.ts`
checks the recorded HLSL SHA-256 and rejects stale bytecode; ordinary builds
need no FXC and never load D3DCompiler.

### The scene

`src/gpui/scene.h` sits between the element tree and `paint.h` on Windows: the
tree's drawing is collected as a flat array and replayed through the same
`paint.h` entry points, so all three backends draw it and none can tell. It is
on at the `skip` level; `__scene=off|replay|cache|skip|damage` turns it down.
**`__scene=off` is the first thing to try if a frame comes out stale** — it is
the only thing here that can decide not to draw. Read the header before
touching it.

`GPUI_FRAME_BENCH=<n>` draws n frames back to back and prints each cost split
into build / layout / paint. It is the right tool for any frame-time question;
without the variable it is inert.

### The browser

`bun cmd/build.ts -wasm <example>` builds a page; `bun cmd/run.ts -wasm
<example>` builds, serves and opens it — a wasm module must come off a server.
Emscripten is found through `$EMCC`, `$EMSDK`, `PATH` or a sibling `.emsdk`, and
only when the target is wasm; the browser half draws through Canvas2D and takes
no library. `web/shell.html` puts a canvas called `gpui-canvas` at the top left
of the viewport and does nothing else, which is what lets `window_wasm.cpp`
read a `clientX` as a window coordinate. Assets preload into MEMFS at
`/assets`, so `gpui/assets.cpp` walks them with the same `fopen`.

The whole platform layer is `EM_JS` over one `globalThis.__gpui` object handed
to C++ as integer ids. **An `EM_JS` body is stringified by the preprocessor**,
so it can hold no empty `''`, no regex literal and no backslash outside a
string literal — use `""` and build a `RegExp` from a string.

Where a page is not a desktop: one window; `AppRun` never returns
(`emscripten_set_main_loop` unwinds the stack, so nothing after it runs); no
threads (`ExecSpawn` queues on the main thread, `ExecHasThreads()` tells —
but rule 1 still holds, write the job as if it were elsewhere); no blocking
`HttpGet`, but `HttpSendAsync` uses browser `fetch()` for remote images and
the shell (subject to CORS, and an opaque manual redirect is refused because
its target cannot be capability-checked); `ImageDecode` answers before
decoding and both loads repaint (SVG never goes through it — `src/gpui/svg.h`
turns SVG into draw ops); the clipboard is a DOM-`paste`-kept mirror and the
paste chord is driven by that event; `sysinfo` reports the tab; an arena
reserves 4 MB, not 64 (`PlatArenaReserveSize()` is the seam).

## App, Window, Entity, Ctx

The runtime mirrors GPUI's shape. Read this before touching `src/gpui`, adding
an example, or writing a widget that owns state.

| GPUI (Rust) | Here |
| --- | --- |
| `App` | `App` — factories, shared fonts, window list, entity store |
| `Window` | `Window` — native handle, render target, frame arena, hover/focus, root view |
| `Entity<T>` | `Entity<T>` — a POD generational handle; `App` owns the state |
| `Context<T>` | `Ctx` — `{app, win, a, self}`; GPUI only splits it for the borrow checker |
| `impl Render for T` | `static El* T::Render(T* self, Ctx* cx)` |
| `cx.new(...)` | `EntityNew<T>(app)` |
| `cx.listener(...)` | `Listen(cx, &T::OnThing)` / `ListenTo(entity, &T::OnThing)` |
| `cx.listener(move \|…\| … ix …)` | `Listen(cx, &T::OnThing, ix)` — the captured value |
| `cx.notify()` | `Notify(cx)` |
| `cx.observe(&e, ..)` | `Observe(cx, e, &T::OnChanged)`; `ObserveTo` where `SubscribeTo` would be |
| `cx.emit` / `cx.subscribe` | `EntityEmit` / `Subscribe(cx, emitter, &T::OnEvent)`, `EntityUnsubscribe` |
| `impl EventEmitter<E> for T` | `template <> struct EventEmitter<T, E> {};` |
| `Drop for T` | `~T()`, run when the entity is dropped |
| `window.use_keyed_state` | `KeyedState<T>(cx, key)` |
| `cx.spawn(...)` with no await | `WindowPost(win, Listener)` |
| `cx.background_spawn(work)` | `ExecSpawn(work, done)` — `done` is posted to the main thread |
| `cx.background_spawn(w).await` | `co_await BackgroundSpawn(w)` inside a `Task` (`sys/task.h`) |
| `Task<T>` dropped | `ExecCancel(id)`, `TaskCancel(handle)`, or the entity going stale |
| `Timer::after(d).await` | `WindowSetTimeout(win, ms, Listener)` |

A view is a plain struct with state, a static `Render`, and static handlers:

```cpp
struct Example {
    int clicks = 0;

    static void OnGo(Example* self, Ctx* cx, const ClickEvent*) {
        self->clicks++;
        Notify(cx);
    }

    static El* Render(Example* self, Ctx* cx) {
        return Div(cx->a)->Child(
            component::Button::New(cx, StrL("go"))
                ->Label(StrL("Let's Go!"))
                ->OnClick(Listen(cx, &Example::OnGo))
                ->IntoEl());
    }
};

int GpuiMain(int argc, char** argv) {
    App* app = AppNew();
    ThemeSet(app, ThemeMode::Light);
    return AppRunView(StrL("Example"), 800, 600,
                      EntityNew<Example>(app).id, app, WinOpts{});
}
```

`AppNew` → `WindowOpenView` → `AppRun` → `AppFree` is the whole lifecycle;
`AppRunView` is the one-window shorthand. There is no hook table.

An entity/event pair must have an `EventEmitter<T, E>` specialization before
it can be passed to `EntityEmit`, `Subscribe` or `SubscribeTo`. The constraint
checks both sides: subscribing an `Entity<T>` with a handler for the wrong
event type, or emitting an undeclared event from it, does not compile. A state
may specialize `EventEmitter` more than once. Subscriber storage carries the
event type key as well as the entity id, so those event channels remain
separate at runtime. Keep the state entity's self handle typed as `Entity<T>`;
dropping it to `EntityId` before an emit would also drop the check.

Rules:

1. **State lives in an entity**, never in a `static` or a `void*` payload. One
   view type per screen or page; two independent states are two entities.
2. **`Ctx*` is the first parameter** of anything that builds elements. The
   frame arena is `cx->a`; do not pass `Arena*` around.
3. **Elements carry their own listener**: `->OnClick(Listen(cx, &T::Handler))`.
   Dispatch resolves the entity and drops the event if the handle went stale,
   so a listener can safely outlive its view. There is no click-id switch
   anywhere; do not add one.
4. **Bind the value instead of decoding an id.** What a Rust closure captures,
   `Listen` takes as an `intptr_t`: `Listen(cx, &T::OnTab, ix)`. A component
   that produces the value itself fills it with `ListenerArg` and its caller
   writes `Listen(cx, &T::OnDay)`.
5. `El::Click(id)` is **identity only** — GPUI's `ElementId`: hit-testing,
   hover, focus, Tab traps. It does not dispatch. `WindowOnUnhandledClick`
   fires for a click no element handled, which is the outside click that
   dismisses an overlay. A widget wanting the press rather than the click takes
   `El::OnMouseDown`/`OnMouseUp`/`OnDragMove`, where the element that took the
   press keeps the moves until the button comes back up. An id only has to be
   unique among its siblings; rows are named by their place.
6. **Window-level input is a subscription bound to a view**, one per event type
   the way `window.on_mouse_event::<T>` is: `WindowOnKey`, `WindowOnMouseDown`
   / `Up` / `Move` / `Exit`, `WindowOnScrollWheel`, `WindowSetInterval` /
   `WindowSetTimeout`. The handler takes the matching GPUI event; the platform
   window builds a `PlatformInput` for `WindowDispatchInput` (Rust's
   `Window::dispatch_event`). Any number of timers can be armed, each returning
   a handle for `WindowCancelTimer`; one whose view goes stale is dropped.
   **A worker never touches an entity, a window or the frame arena** — off-clock
   work goes `ExecSpawn` → `WindowPost` back to a main-thread listener.
7. **A blinking caret is state, not a function of the clock:** `BlinkStart` /
   `Stop` / `Pause` / `Visible`, a port of `blink_cursor.rs`. Sampling
   `TimeNow()` at paint time makes the caret invisible whenever nothing
   repaints during the lit half. One cursor per field, as an entity;
   `InputFocus` / `InputBlur` start and stop it, and the runtime does the same
   handoff for whichever `InputState` a view points `win->input` at.
   `AppRequestAnim` is for real animation, never for a caret.
8. **`Notify(cx)`** marks the entity and schedules a repaint of the windows it
   is on — GPUI's `App::notify`. Observers run; a window is invalidated only
   when the notifying entity is one of the views it rendered last frame
   (`Window::rendered`, GPUI's `dirty_views`). An entity nothing has rendered
   yet falls back to the window the notify came from, and to every window when
   it came from none.
9. **Entity handles are generational, not refcounted.** `Entity<T>::Get`
   returns null once the slot is recycled; check it.
10. **Geometry is `Point`/`Size`/`Bounds`/`Edges`, all DIP floats**, named the
    way Rust names them. Rust's `Point<T>` is generic over a *unit* and
    everything above `paint.h` here is DIPs, so the parameter is gone. Use them
    for what is produced, returned or passed whole; code writing one component
    at a time keeps flat fields. A unit that is not DIPs gets a named struct
    (`WinSize`), never a template.

`src/gpui/keymap.*` is `Keystroke::parse`, the binding table, key contexts with
`key=value` pairs, `KeyBindingContextPredicate` (`"Editor && mode == full"`,
`"Workspace > Editor"`, `!`, `||`, parens) and multi-stroke bindings.
`WindowDispatchKeyAction` resolves a chord against the contexts stacked over
the focused element, then walks handlers out from it. Every component declares
its context and calls its `init` the way Rust's modules do — no application
wires a component's keys by hand. What a component's state cannot hold waits in
a keyed entity beside it. `InputState` is the one keyboard translated rather
than bound: the window offers it a chord first, which makes a focused field's
editing the innermost context.

## Layout

`src/taffy/` is a C++ port of [taffy](https://github.com/DioxusLabs/taffy) at
the version `Cargo.lock` resolves for `gpui` — the crate Zed's GPUI lays out
with, so it defines what gpui-kit's layout *means*. Flexbox, CSS Grid,
block layout and floats, plus ports of the crate's unit tests
(`tests/TaffyTests.cpp`). It is `namespace taffy`, not `gpui`: `Style`,
`Overflow`, `Position` and `Display` exist in both and mean different things.

`LayoutEl` in `src/gpui/gpui.cpp` is the seam: each frame it walks the `El`
tree, translates every `gpui::Style` into a `taffy::Style`, runs
`ComputeLayoutWithMeasure`, and writes the boxes back. Text, icons, images and
progress bars are measured leaves (Rust's `request_measured_layout`). What
taffy does not model — `fixed`, `anchorBelow`/`anchorAbove`/`anchorCenterX`,
the `relative(f)` half of an inset, scroll offsets — is applied around it; the
comment above `LayoutEl` says how.

**The taffy tree is kept between frames, not rebuilt.** A window owns a
`LayoutCache`; each frame the element tree is reconciled against it *by
position* — the nth child of the nth child is the same node — and a node is
told something only when it has something to hear: its `taffy::Style` is not
the one it carries (`operator==` on `taffy::Style` is Rust's `PartialEq`
derive, ported), or it is a measured leaf whose text, font, weight, line
height, wrap or image moved. A `PerformLayout` that hits the cache does not
walk the subtree at all, so a hover that only recolours a box costs no layout.
`__layout_reuse=off` (or `GPUI_LAYOUT_REUSE=off`) rebuilds every frame and is
the first thing to try if a frame comes out laid out stale; `GPUI_FRAME_BENCH`
prints what each frame had to tell taffy (`nodes=1466 made=0 dropped=0
restyled=0 remeasured=0` is a page that did not change).

**Do not add a special case to `LayoutEl` for something CSS already has a rule
for** — make the style translation say the right thing instead. When a layout
question comes up the answer is in `src/taffy/`, and behind that in the crate.

`gpui::Style` is meant to say everything gpui's `Styled` trait says. The one
that bit: `crates/ui` and `crates/story` never call `.flex_grow()`, so every
grow in the Rust is `.flex_1()` — grow 1, **shrink 1, basis 0** — and
`El::Grow()` left the basis at `auto`, which sizes an item by its content
instead of by the line. **`El::Flex1()` is the faithful one**; `Grow(f)` is for
a factor that is not 1.

## Markdown

`src/markdown/` is a C++ port of
[markdown-rs](https://github.com/wooorm/markdown-rs) at the version
`crates/ui/Cargo.toml` asks for — CommonMark and GFM, tables, footnotes, task
lists. It is `namespace markdown`, not `gpui`: `CharKind`, `Name`, `Link`,
`Point` and `Node` exist in both.

`MdParse` in `src/base/text.cpp` is the seam: it calls
`markdown::ToMdast(a, source, ParseOptions::Gfm())` and folds the mdast into
the `MdNode` tree the renderer walks, which is what
`crates/base/src/text/format/markdown.rs` does. Raw HTML goes through
`src/base/text_format.cpp` into the same tree through `src/html5ever`.
**Do not add a special case to `MdParse` for something CommonMark has a rule
for.**

Rich text belongs to gpui-base, not the themed layer: `src/base/text.h` owns
the parser, renderer, `TextViewState` and selection, and reads no theme — every
colour comes from `TextViewStyle`, and highlighting is the opt-in
`CodeBlockHighlighter` callback. `src/ui/text.h` is the façade: it re-exports
those names under `component::`, derives a `TextViewStyle` from the component
`Theme`, and installs it plus the `ui/syntax.h` highlighter as
`TextViewDefaults` whenever the theme is set.

`src/markdown-mini/` is the size-oriented alternative selected by
`-markdown=mini`. It shares `markdown.h` and `mdast.*` but is **not** an
upstream crate — keep its smaller contract (`src/markdown-mini/readme.md`).

`src/html5ever-mini/` is the corresponding size-oriented HTML parser selected
by `-html=mini`. It implements `src/html5ever/html5ever.h`; keep its
smaller contract (`src/html5ever-mini/readme.md`).

## Code style

```cpp
#include "base.h"

struct MetricPoint {
    float cpu = 0;
    float memory = 0;
};

void FormatBytes(uint64_t bytes, StrBuilder& out);
```

- Include the base header first.
- `TempStr s = fmt("%.1f%%", cpu);` — `fmt()` returns a temp-arena string; do
  not `free` it.
- Functions returning `TempStr` are named with a `Temp` suffix; `fmt()` is the
  established formatting-shorthand exception.
- Own a heap `Str` only if it must survive a frame: `StrDup` / `StrFree`.
- `Vec<T>` for arrays of POD, not for `Str` graphs — hold a `char name[kMax]`
  or an arena `Str` in the element.
- Read a `Str`, `Vec`, or `ArenaVec` length with `len(s)` / `len(v)`, not
  `.len`. The field is what constructors and growth/reset write; call sites
  use the free function.
- `logf("...")` for debug prints.
- Prefer `int32_t` indexes; `int` where an existing base API uses it
  (`len(v)`).
- **Strings are UTF-8 `Str` everywhere**, including our own API. Convert with
  `ToCWstrTemp(s)` only where an OS call needs UTF-16, at the call itself. Do
  not widen a signature to `wchar_t*` to save a conversion.
- COM: pair every successful `Create` with `Release`. No `CComPtr`.

## Build, run, test

```
bun cmd/build.ts                         # no example name prints the list
bun cmd/build.ts -rel showcase           # the example is the last argument
bun cmd/build.ts -dbg -all
bun cmd/build.ts -rel -asan system_monitor
bun cmd/build.ts -wasm system_monitor
bun cmd/mobile-build.ts -android -rel
bun cmd/mobile-build.ts -ios -rel
bun cmd/mac-build.ts -ios -rel          # run the iOS compile on the remote Mac
bun cmd/build.ts -clang -rel showcase    # Windows: clang-cl, into out/rel_clang/
bun cmd/build.ts -rel --win-backend=d3d12 story
bun cmd/build-no-amalgam.ts -rel         # one object per source: header build check
bun cmd/build-no-amalgam.ts -clang -rel
bun cmd/clang-tidy.ts                    # over all src/**/*.cpp
bun cmd/test.ts                          # build tests/ and run it (-dbg, -rel)
bun cmd/bench.ts                         # release, 10 samples
bun cmd/bench.ts -small -large -n=3 grid/deep
bun cmd/run.ts -dbg hello_world
bun cmd/run.ts -wasm story
bun cmd/run.ts -rel --win-backend=all story -- __paint=d3d12 __msaa=4 __scene=damage
```

`cmd/build.ts` is the whole build for every platform: MSVC on Windows,
g++/clang++ on Linux, clang++ on macOS, emscripten with `-wasm` from any of
them. `cmd/run.ts` imports it rather than spawning it, so a flag means the same
thing and lands in the same `out/`. On Windows `cl.exe` is used off PATH when
the MSVC environment is set; otherwise Visual Studio is found with `vswhere`
(or by scanning the default roots, newest first) and `vcvars64.bat` is run once
for the environment it exports, so a plain shell builds. Neither script
downloads what the build does not need. Output dirs: `out/rel`, `out/dbg`,
`out/rel_asan`, `out/rel_clang`, and `out/linux/`, `out/mac/` per host, so one
checkout can build for several platforms without clobbering. Add `-clean` for a
clean rebuild of that dir.

`cmd/run.ts` adds: `-debugger` (WinDbg then cdb, gdb then lldb, lldb — force
one with `-windbg`/`-cdb`/`-gdb`/`-lldb`; a named debugger that is not
installed is an error carrying the install command, never a quiet fallback);
`-compare` (cargo-build and launch the Rust twin beside ours); `-versions`;
`-no-build`; `-no-open` / `-port N` for wasm. It does not accept `-all`.

Runtime flags every example understands, stripped from argv before the example
parses it: `-gpui-window=X,Y,W,H`, `__layout_reuse`, and on Windows `__paint`,
`__msaa`, `__scene`, plus the test-only `__gpu_reset_every=N` recovery seam.
`GPUI_LAYOUT_DUMP=lay.txt` dumps every frame's laid-out tree;
`GPUI_TODAY=YYYY-MM-DD` pins `DateToday()` so a calendar screenshot is
reproducible.

Linux prerequisites install with `bash cmd/ubuntu-install-deps.sh`
(`--build-only` for just a compile). From a Windows checkout,
`bun cmd/wsl-run.ts -rel system_monitor` builds and runs the Linux binary in
WSL, with the window through WSLg. macOS needs only the Xcode command line
tools; `bun cmd/mac-build.ts -rel -all` compiles on a remote Mac over ssh by
snapshotting the tree to a scratch branch — it compiles only, since a Cocoa
window needs a login session.

CI (`.github/workflows/build.yml`) runs `bun cmd/build.ts -rel -all` then
`bun cmd/test.ts -rel` on windows/ubuntu/macos-latest, and compiles the source
tree without amalgamation on all three. The Windows lane installs the pinned
NDK and cross-compiles Android arm64; the macOS lane cross-compiles iOS arm64.
Separate Windows and Linux lanes use
clang-cl/clang++, compile the mini markdown configuration, and run the full
parser's tests; the Windows lane also compiles all paint backends. A wasm lane
builds every supported example and runs the tests under node. CI sets
`GPUI_NO_RUST_TREE=1`, so the Rust clone is skipped.

**Warnings are errors** — `/W4 /WX`, `-Wall -Wextra -Werror`. Fix the warning;
do not add a suppression. The only ones left are the same amalgam artifact
under two names (`/wd4505`, `-Wno-unused-function`) plus `/wd4996`, each with a
comment saying why.

**After changing `.cpp`/`.h`/`.ts` files, run `bun cmd/format.ts` on those
paths** (clang-format on C++ in `src/`, `examples/`, `tests/`, `bench/` and
`gpui_shell/`, Chromium-based, 80 columns; Prettier on TypeScript, printWidth
120, lf). `-ts` / `-cpp` runs one. Pass the changed paths — with no arguments
it reformats the whole tree, which buries a small change under unrelated churn.
Never format `.work/` or `out/`. The vendored QuickJS pair under
`src/quickjs/` and the generated shader/icon tables are skipped.

### Tests and benchmarks

`tests/` holds ports of the pure-logic tests in the Rust tree, one file per
Rust module, each naming the module it came from. The framework is
`utassert(cond)` and a counter — `tests/Test.h` is all of it; a test is a plain
function `tests.cpp` calls, nothing registers itself. Only tests that pin code
we ported belong here: most of upstream's are `#[gpui::test]` and need
`TestAppContext`, which has no counterpart. **When a test needs a seam to reach
the logic, add the seam rather than the harness** (`FrameSamplerIngest` is the
drain half of `FrameSamplerTick`, split out so the rolling window can be driven
without a window).

`bench/` ports taffy's own benchmarks — large flexbox trees, wide and deep
grids, tree construction — plus `MarkdownBench.cpp`, which is ours (markdown-rs
carries none) and splits a parse into its tokenize and to_mdast halves. The
harness is criterion's `iter_batched` without criterion: setup is untimed, the
row reports median and minimum. **Read a number only from a release build**, and
**if you touch `src/taffy/` or `src/markdown/`, run them** — layout is the one
thing here with an asymptotic complexity, and a wrong one hides: an insertion
sort over a grid's item list was fine on every hand-written grid in the story
gallery and took 94 seconds on a 316×316 one.

### Memory leaks (Deleaker, Windows)

CRT/COM growth Task Manager shows while a window is open is often freed on
`AppFree`, so `--export-xml-report-on-exit` is not the test for "grows while
scrolling" — periodic snapshots are. DeleakerConsole is
`C:\Program Files (x86)\Deleaker\DeleakerConsole.exe`, and **`--run` must be
last**: everything after it belongs to the launched process, our `__paint` /
`__layout_reuse` flags included.

`bun cmd/deleaker-scroll.ts` does the whole thing for `editor.exe` and wheels
the document down, up and down again (needs PDBs; `bun cmd/build.ts -rel
editor` is enough). The `.dsnapshot` is SQLite (`Snapshot`,
`AllocationGroup`, `StackTrace`, `StackEntry`). **Compare two live snapshots,
not the exit one**; groups whose hit count rises between them with a stack in
`src/` are the leak. The first snapshot of a process is slow (symbol
download), later ones are seconds.

## The distributed amalgam

`cmd/update-dist.ts` amalgamates `src/` into `gpui.h` + `gpui.cpp`, copies the
separately compiled `quickjs/quickjs.h` + `quickjs.c`, and amalgamates each
ported crate into a standalone pair under `extras/` — one header + one source,
the header inlining `base.h` behind a `GPUI_BASE_H_` guard shared with
`gpui.h`'s copy, so a pair header and `gpui.h` can meet in one translation unit
in either order. All of it is the same on every platform.

- **`extras/autocorrect/` links beside `gpui.cpp`** (taking base from it), and
  `cmd/build.ts` compiles it only into the targets that use it (the editor
  example, the tests). `extras/taffy/`, `extras/markdown/`,
  `extras/markdown-mini/`, `extras/html5ever/`, `extras/html5ever-mini/` and
  `extras/wry/` are *inside* `gpui.cpp`; their
  pairs exist for using one library without gpui, each carries the base
  implementation, and **must never link beside `gpui.cpp`**.
- Implementation-private headers (markdown's tokenizer, taffy's compute
  internals, autocorrect's `internal.h`) are inlined behind
  `#if GPUI_INCLUDE_PRIVATE_API`, which `gpui.h` defaults to 0. `gpui.cpp` and
  the few tests that reach internals define it to 1 before their first include.
  The split is computed from the include graph: a crate header is public
  exactly when a header outside the crate's directory transitively includes it.
- **`.work/` is what every build compiles** — `cmd/build.ts`, `cmd/test.ts` and
  CI all go through it. There it is the sources concatenated and nothing else,
  comments and all, so a line is the line its `#line 1 "src/..."` marker says.
- The published copy is a repo of its own,
  [gpui-kit-cpp-dist](https://github.com/kjk/gpui-kit-cpp-dist), cloned to
  `.work/gpui-kit-cpp-dist` and refreshed **only by running `bun
  cmd/update-dist.ts` by hand**: it syncs the clone, writes the pairs, builds
  every example against it (`GPUI_AMALGAM_DIR`, objects in `out/*_dist`),
  rewrites the readme with the commit it came from, then commits and pushes.
  **Never regenerate a published copy as part of a build, a test run or a
  commit**; `buildDist()` takes a required `outDir` so an automatic caller has
  to say `.work` out loud. A dirty or non-`main` checkout is removed and cloned
  again rather than repaired — the script commits whatever `git status` reports,
  so a stray file would be published.
- In the published copy the text is read as a document: comments come out,
  blank runs collapse, and `#include` lines are lifted to the top of `gpui.cpp`
  and de-duplicated — portable ones first, then one guarded block per platform,
  which must stay below the portable code because `<X11/Xlib.h>` defines `None`
  and `Window`. Each platform-suffixed file sits inside its own
  `#if GPUI_OS_*`, so platform SDK headers never reach another target's
  translation unit. Apple platform implementations compile as Objective-C++.
- The snapshot is a checkout, not six files: beside the pairs go every example,
  `gpui_shell/`, `assets/`, `web/shell.html`, `build.ts`, `run.ts`, and
  `winapi.ts` + `mac-window-place.m` because `run.ts -compare` reaches for them
  by name — so `bun run.ts story` works in a fresh clone. Its `.gitignore` is
  written from here too. `readme-dist.md` **in this repo** is the published
  readme (`<checkin-sha1>` filled in on the way over); edit it here, never
  there.
- **`build.ts` and `run.ts` are copied verbatim, not forked**, and discover
  which tree they are in at startup: `isDist` is `existsSync(scriptDir +
  "/gpui.h")`, and the repo root, `amalgamDir()`, the `out/` layout, the target
  list and the usage text all hang off it. Two rules follow.
  `cmd/update-dist.ts` must be imported **dynamically** — it does not exist
  over there, and a static import fails at load, which is why `ensureAmalgam`
  and `build()` are async. And anything either script reaches for by path must
  be resolved against `scriptDir`, not `<root>/cmd/`. Keep both runnable in
  both trees; `bun cmd/update-dist.ts` builds every example against the
  snapshot, which is the check that catches it.

## Layout of this tree

```
AGENTS.md              this file
port-status.md         known gaps and deliberate deviations
port-upstream.md       how to ingest a later upstream checkin
port-map.md            the Base/UI module ledger (cmd/audit-port.ts)
readme-dist.md         the published snapshot's readme; edit here, never there

cmd/build.ts           the whole build, every platform
cmd/run.ts             build then run; also holds the upstream pins
cmd/build-no-amalgam.ts  one object per source — the header build check
cmd/test.ts            build tests/ and run it
cmd/bench.ts           build bench/ and run it
cmd/format.ts          clang-format + prettier (pass the changed paths)
cmd/clang-tidy.ts      clang-tidy over src/**/*.cpp
cmd/audit-port.ts      the Base/UI declaration/export/test audit; CI runs it
cmd/update-dist.ts     amalgamate src/** into gpui.h + gpui.cpp
cmd/update-win-shaders.ts  FXC over paintgpu_win.hlsl -> checked-in DXBC
cmd/update-quickjs.ts  regenerate src/quickjs/ from the pinned QuickJS-NG
cmd/update-shell-types.ts  regenerate the shell's TypeScript declarations
cmd/gen-theme-data.ts / gen-locale-data.ts  upstream JSON -> generated tables
cmd/wsl-run.ts         run cmd/run.ts inside WSL from a Windows checkout
cmd/mac-build.ts       compile on a remote Mac over ssh
cmd/ubuntu-install-deps.sh  apt + bun + rustup for Linux
cmd/shot.ts            screenshot one example; -click=X,Y, -hover, -half=left|right
cmd/imgdiff.ts         compare two shots or two dirs, exit 1 on a difference;
                       -skip=32 ignores the title bar, -bbox names the widget,
                       -tol sets what counts as more than antialiasing
cmd/compare-story.ts / compare-showcase.ts / compare-ui.ts
                       screenshot the Rust app and this one side by side
cmd/parity.ts          drive representative Rust/C++ interaction cases, check
                       visual budgets and write out/parity/{results.json,results.md}
cmd/vec-log.ts         run with the debug Vec/ArenaVec instrument on and analyze
                       the log — answer "should this capacity be bigger?" with it
cmd/deleaker-scroll.ts DeleakerConsole against editor.exe (Windows)
cmd/svg-to-bytecode.ts assets/icons -> src/gpui/asset_icons.cpp
cmd/crlf-to-lf.ts      normalize line endings after a scripted edit

src/base.h/.cpp        vendored SumatraPDF subset; base_{win,linux,mac,wasm,posix}.cpp
src/gpui/              App, Window, Entity, Ctx, El, theme, layout, paint, assets,
                       SVG, keymap, scene; paint.h / platform.h and their per-OS
                       files; window_common.cpp; drawops + svg + asset_icons
src/sys/               metrics, executor, task.h's coroutine and registry, http
src/base/              crates/base unstyled primitives; text.h owns TextView
src/ui/                themed crates/ui façade (component::*)
src/fps/               the crates/fps performance HUD
src/taffy/             the taffy crate, ported (readme.md)
src/markdown/          the markdown crate, ported (readme.md)
src/markdown-mini/     the smaller alternative, ours (readme.md)
src/html5ever/         the html5ever crate, ported (readme.md)
src/html5ever-mini/    the smaller alternative, ours (readme.md)
src/wry/               the wry webview crate, ported (readme.md)
src/autocorrect/       the autocorrect CJK linter crate, ported (readme.md)
src/webview/           crates/webview: the view that gives a wry webview a box
src/shell/             crates/shell: sandboxed JS apps; fetch.h is the policy
src/quickjs/           the reduced QuickJS-NG, generated, compiled as C11

examples/              AppLog.cpp (log hooks) + every example, showcase/, story/
gpui_shell/            the shell host binary
tests/ bench/          utassert ports; taffy's benchmarks and markdown's
assets/                Lucide icons and per-example assets
web/shell.html         the wasm page
```

## How to port a module

Port `crates/base` primitives into `src/base/`, one Rust module at a time,
keeping the type name (`Button`, `Checkbox`, `Accordion`, …). These primitives
own interaction — click, focus, open/checked state — and **not** paint: the
caller applies `.Bg()`, `.Border()`, `.H()`, `.Child()`, matching how Rust's
`Button::new(id).bg(...).child(...)` works.

```cpp
Button::New(cx, StrL("primary-button"), ClickSave)
    ->PadX(12)
    ->H(28)
    ->Bg(Rgb(0x17, 0x17, 0x17))
    ->Child(TextEl(cx->a, StrL("Save changes")));
```

Do not inline a styled `Div` tree when a primitive exists. When a primitive
needs a GPUI capability we do not have, add the smallest piece in `src/gpui`
first, then the widget.

If `src/base.h` is missing an API you need, copy the corresponding bits from
`C:\Users\kjk\src\sumatrapdf\src\base`. Do not copy CrashHandler, GdiPlusUtil,
Http, Zip or other app-level Sumatra files — the HTTP client this tree has is
`src/sys/http.h`, written against the OS's own library.
