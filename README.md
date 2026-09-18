# gpui for C++

A **C++** port of [longbridge/gpui-kit](https://github.com/longbridge/gpui-kit), a Rust UI kit built on [Zed GPUI](https://github.com/zed-industries/zed). Targets **Windows**, **Linux**, **macOS**, **iOS**, **Android**, and **the browser** (wasm).

Original project:

- Repository: https://github.com/longbridge/gpui-kit
- Docs: https://gpui-kit.com

This tree reimplements the component examples and a small runtime on top of the OS: Win32 + Direct2D + DirectWrite on Windows, X11 + cairo + Pango on Linux, Cocoa + Core Graphics + Core Text on macOS, an application-supplied native host on iOS and Android, and a `<canvas>` 2D context in the browser. Everything above the `paint.h` / `platform.h` seam is shared. It is not a binding to the Rust crates, and it does not use Blade or Zed’s renderer. Layout is the exception: `src/taffy/` is a C++ port of the taffy crate GPUI itself lays out with, at the version gpui-kit pins.

The API follows GPUI's shape: an `App` owns the entity store and the windows, a `Window` renders a view, and a view is a struct with state plus `static El* Render(T* self, Ctx* cx)`:

```cpp
struct Example {
    static void OnGo(Example*, Ctx*, const ClickEvent*) { log(StrL("Clicked!")); }

    static El* Render(Example*, Ctx* cx) {
        return Div(cx->a)->FlexCol()->SizeFull()->ItemsCenter()->JustifyCenter()
            ->Child(TextEl(cx->a, StrL("Hello, World!")))
            ->Child(ButtonEl(cx->a, 0, StrL("Let's Go!"), BtnKind::Primary)
                        ->OnClick(Listen(cx, &Example::OnGo)));
    }
};

int GpuiMain(int argc, char** argv) {
    App* app = AppNew();
    return AppRunView(StrL("Hello World"), 800, 600,
                      EntityNew<Example>(app).id, app, WinOpts{});
}
```

Entities are generational handles owned by `App`, not refcounted; `cx.listener` becomes `Listen(cx, &T::Handler)` and `cx.notify()` becomes `Notify(cx)`. See the _App, Window, Entity, Ctx_ section of [AGENTS.md](AGENTS.md).

The Rust sources used as the spec live in a gitignored clone at `.work/gpui-component/`. Exact checkins we are porting are in [`cmd/run.ts`](cmd/run.ts); `bun cmd/build.ts` installs that tree. Ingesting later upstream checkins: the `/update-port` skill ([.claude/skills/update-port/SKILL.md](.claude/skills/update-port/SKILL.md)).

## Build

`bun cmd/build.ts` and `bun cmd/run.ts` dispatch to the toolchain for the
machine they run on, so the same commands work on all three platforms:

```
bun cmd/build.ts -rel story
bun cmd/run.ts -rel -compare story
# compile every source file as a separate object and link a header-only example
bun cmd/build-no-amalgam.ts -rel
bun cmd/build-no-amalgam.ts -clang -rel   # Windows: clang-cl
# arm64 static libraries for externally hosted mobile applications
powershell -ExecutionPolicy Bypass -File cmd/android-install-deps.ps1
bun cmd/mobile-build.ts -android -rel
bun cmd/mobile-build.ts -ios -rel         # macOS + Xcode only
bun cmd/mac-build.ts -ios -rel            # from a Windows/Linux checkout
```

`bun cmd/build.ts` with no example name lists targets (`system_monitor`, `showcase`, `story`, …).

## Rust/C++ parity suite

On Windows, the parity suite builds the pinned Rust story application and this
port, drives identical native input through both, and compares screenshots at
each checkpoint:

```powershell
bun cmd/parity.ts
bun cmd/parity.ts -nobuild input-edit select-open
bun cmd/parity.ts -list
```

The default cases cover static composition, toggles, text input, overlays,
scrolling, pointer capture and retained tree state. Each checkpoint has a
checked-in percentage budget for ordinary renderer and text-antialiasing
differences; interactive checkpoints must also prove that both applications
changed. Captures plus machine-readable JSON and Markdown reports are written
to `out/parity/`. `-report` records results without failing on an exceeded
visual budget, for deliberately investigating a known difference.
`-nobuild` assumes both existing executables were built from the current pin;
run the default command after moving an upstream pin.

The Windows GPU suite compares the same showcase pages under Direct2D, D3D11
and D3D12, requires the two custom backends to be pixel-identical, then repeats
their captures after resize churn and deterministic device resets:

```powershell
bun cmd/gpu-parity.ts
bun cmd/gpu-parity.ts -nobuild introduction text-view
bun cmd/gpu-parity.ts -list
```

It builds `showcase` with `--win-backend=all` and writes PNGs plus JSON and
Markdown results under `out/gpu-parity/`. The recovery run uses the reserved
`__gpu_reset_every=N` diagnostic seam; zero, the default, is inert.

To lint every source translation unit under `src/` with LLVM's clang-tidy (using
the repository's `.clang-tidy` configuration):

```
bun cmd/clang-tidy.ts
bun cmd/clang-tidy.ts -checks=bugprone-*,performance-*
bun cmd/clang-tidy.ts --host       # only this platform's source files
```

On Windows, the renderer is selected at compile time. With no definition the
build contains only Direct2D, the compatibility default. The repository build
script accepts the same choice through `--win-backend`:

```powershell
bun cmd/build.ts -rel --win-backend=d3d11 story
bun cmd/build.ts -rel --win-backend=d3d12 story
bun cmd/run.ts -rel --win-backend=all story -- __paint=d3d12 __msaa=4 __scene=damage
```

Library users can instead define exactly one of `WIN_BACKEND_DIRECT2D`,
`WIN_BACKEND_D3D11` or `WIN_BACKEND_D3D12`. Defining `WIN_BACKEND_ALL`
compiles all three and retains the process-start
`__paint=d2d|d3d11|d3d12` selector. A fixed build ignores unavailable backend
choices. `__msaa=1|2|4|8` controls the custom renderers' sample count (4 by
default). `__scene=off|replay|cache|skip|damage` selects how much scene work is
enabled, with `skip` as the default. `__gpu_reset_every=N` is the custom-GPU
recovery test seam used by `cmd/gpu-parity.ts`; normal runs leave it at zero.
`__layout_reuse=off|on` rebuilds the taffy tree every frame when off (default
on); `GPUI_LAYOUT_REUSE` is the same switch if argv did not set it. The runtime
consumes those reserved arguments before calling `GpuiMain`, so application
argument parsing never sees them. See `src/gpui/paint.h` for the quality, cost
and caching tradeoffs.

The custom renderers use checked-in FXC bytecode rather than compiling HLSL at
application startup. After editing `src/gpui/paintgpu_win.hlsl`, regenerate it
with `bun cmd/update-win-shaders.ts`; ordinary builds verify the source hash and
otherwise need neither `fxc.exe` nor `D3DCompiler_47.dll`.

Markdown defaults to the complete CommonMark + GFM parser. Applications that
prefer a smaller executable can select the basic parser at build time:

```
bun cmd/build.ts -markdown=mini -rel story
bun cmd/build.ts -markdown=full -rel story   # default
```

The mini feature list and intentional omissions are in
[`src/markdown-mini/readme.md`](src/markdown-mini/readme.md).

HTML parsing has the same build-time choice. The html5ever port is the
default; the former reader-mode parser is the smaller drop-in implementation:

```
bun cmd/build.ts -html=mini -rel story
bun cmd/build.ts -html=full -rel story   # default
```

See [`src/html5ever/readme.md`](src/html5ever/readme.md) and
[`src/html5ever-mini/readme.md`](src/html5ever-mini/readme.md).

## JavaScript shell

`gpui_shell` runs the port of `crates/shell` against the vendored, pinned
QuickJS-NG engine. The upstream todo application is included unchanged:

```
bun cmd/run.ts -rel gpui_shell -- examples/js_todolist --dev
bun cmd/run.ts -rel gpui_shell -- check examples/js_todolist
bun cmd/run.ts -rel gpui_shell -- types examples/js_todolist
```

The first command opens the application and reloads it after source changes.
`check` loads and renders once without a window; `types` writes the exact
upstream `gpui.d.ts` plus declarations for registered host modules. A directory
may optionally carry `gpui-shell.json` to set its entry point, capabilities and
plugin metadata. `gpui_shell` is a desktop target and is skipped by `-wasm`.

**Windows** needs [Bun](https://bun.sh) and the MSVC C++ toolset. `cl.exe` on
PATH is used as it is; otherwise Visual Studio is found through `vswhere` and
its `vcvars64.bat` is read for the environment, so a plain shell builds.
`bun cmd/build.ts -clang <example>` builds with `clang-cl` from the same
toolset instead.

**Linux** needs g++ (or clang++), pkg-config and the X11 / cairo / pango dev
packages. On Ubuntu or Debian:

```
bash cmd/ubuntu-install-deps.sh
```

**macOS** needs the Xcode command line tools (`xcode-select --install`).

**The browser** is a target rather than a host, so it is asked for by name and
builds from any of the three:

```
bun cmd/build.ts -wasm story
bun cmd/run.ts -wasm story         # builds, serves, opens a tab
```

It needs [emscripten](https://emscripten.org), found through `$EMCC`, `$EMSDK`,
`PATH`, or an emsdk checkout beside this one:

```
git clone https://github.com/emscripten-core/emsdk ../.emsdk
cd ../.emsdk && ./emsdk install latest && ./emsdk activate latest
```

There is no other dependency: the page draws through Canvas2D. What a tab
cannot do that a desktop can — a second window, a background thread, a
blocking fetch, the machine's process table — is listed in
[AGENTS.md](AGENTS.md).

From a Windows checkout you can build and run the Linux binaries under WSL
without leaving the shell, and compile the macOS ones on a Mac over ssh:

```
bun cmd/wsl-run.ts -rel system_monitor
bun cmd/mac-build.ts -rel -all
```

CI compiles every example and runs the tests on all three desktop platforms
and wasm on each push, and cross-compiles the library with the Android NDK and
iPhoneOS SDK. It also checks separate translation units on every
desktop, clang-cl/clang++ on Windows and Linux, the mini markdown parser, and
all Windows paint backends
([`.github/workflows/build.yml`](.github/workflows/build.yml)).

# Why port to C++?

- Do you know a good joke?
- Yes, Rust.
