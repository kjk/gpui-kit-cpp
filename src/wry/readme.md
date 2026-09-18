# src/wry — the wry webview crate, ported to C++

This is a port of [wry](https://github.com/tauri-apps/wry) **0.53.3** — the
`lb-wry` fork gpui-kit's `crates/webview` (the `gpui-wry` crate) depends
on, and therefore the crate that defines what a webview in a gpui window
means. `src/webview/` is the gpui-side half — the port of `crates/webview`
itself — and it is the only thing in the tree that calls in here.

The pin lives in [`cmd/run.ts`](../../cmd/run.ts) (`wry`) next to
the gpui-kit, Zed GPUI, taffy and markdown pins, and moves the same way
— see [`/update-port`](../../.claude/skills/update-port/crates.md).

## Where the Rust went

| Rust                                                 | C++                            |
| ---------------------------------------------------- | ------------------------------ |
| `src/lib.rs` (types, attributes, the builder, the API) | `wry.h`                        |
| `src/webview2/mod.rs`                                | `wry_win.cpp`                  |
| `src/webview2/util.rs`                               | `wry_win.cpp` (the DPI helpers) |
| `src/proxy.rs`                                       | `wry.h` (`ProxyConfig`)        |
| `src/web_context.rs`                                 | `wry.h` (`dataDirectory`)      |
| `src/error.rs`                                       | — (see below)                  |
| `src/wkwebview/mod.rs`, `class/**`, `navigation.rs`  | `wry_mac.cpp`                  |
| `src/webkitgtk/**`                                   | `wry_linux.cpp` — a stub       |
| —                                                    | `wry_wasm.cpp` — a stub        |
| `src/android/**`, `src/wkwebview/ios/**`             | not ported                     |

Everything is in `namespace wry`, not `gpui`, and the directory is isolated
the way `src/taffy` and `src/markdown` are: it includes `base.h` and its own
header and nothing else, and names no `gpui::` symbol.
`cmd/update-dist.ts` fails the build if that stops being true. The parent
window arrives as a `void*` — Rust takes a `raw_window_handle::
HasWindowHandle`, and `src/gpui`'s `PlatWindowHandle` is what hands one over.

## The WebView2 dependency, and why there is none

Rust reaches WebView2 through two crates this tree may not have (hard rule 3):
`webview2-com`, which generates COM bindings from the SDK, and
`webview2-com-sys`, which links Microsoft's `WebView2LoaderStatic.lib`. Both
halves are written out instead, and both are in `wry_win.cpp`:

- **The interfaces.** A block of `ICoreWebView2*` declarations transcribed
  from the WebView2 SDK header: same vtable order, same IIDs, comments
  dropped. It declares an ABI the way `<d2d1.h>` does — nothing of the SDK is
  compiled in, and the runtime it talks to ships with Windows' Edge. An
  interface that only appears in a signature we never call is
  forward-declared; the ones we implement (`ICoreWebView2EnvironmentOptions`
  through 8, plus every event and completion handler) are there in full,
  because an implementation has to answer for every slot. A mechanical audit
  compares all 97 definitions and IIDs with Wry's generated pinned bindings.
- **The loader.** `CreateEnvironmentWithOptions` writes out the behavior of
  Wry's pinned `WebView2LoaderStatic.lib`. It applies the browser-folder,
  user-data-folder, browser-argument and release-channel environment and
  policy overrides; resolves relative fixed runtimes against the executable;
  and searches Stable, Beta, Dev and Canary in the same order and registry
  views. Both `ClientState\EBWebView` and legacy `Clients` registrations are
  understood, followed by the framework-package graph where that API exists.
  Installed candidates must meet the loader's `86.0.616.0` compatibility
  floor; a stale candidate falls through to the next source or channel.

  The selected runtime's `EBWebView\<arch>\EmbeddedBrowserWebView.dll` is
  loaded and its `CreateWebViewEnvironmentWithOptionsInternal` export called.
  The synchronous and completion-handler retry boundaries and the loader's
  conditional module-reference release are preserved. Version-only queries
  also retain the loader's unoverridden fallback. The internal argument list
  is the one the SDK's own static loader passes, read off its
  `CreateWebViewEnvironmentWithClientDll` — whose mangled name spells out the
  types: `(clientDllPath, bool, WebView2RunTimeType, userDataFolder,
  IUnknown* options, handler)`, of which everything but the path is
  forwarded.

  Two values the SDK's helper supplies and the runtime insists on: a user data
  folder (the exe's path with `.WebView2` after it, when the caller names
  none) and `TargetCompatibleBrowserVersion`, which is a *browser* version
  (`137.0.3296.44`, `CORE_WEBVIEW_TARGET_PRODUCT_VERSION` in Wry's pinned
  `webview2-com-sys 0.38.0`) and not the crate's own.

## The macOS backend

`wry_mac.cpp` is `wkwebview/` and takes no dependency at all: WKWebView is in
the system SDK, and `cmd/build.ts` links one more framework for it. The
amalgam is compiled `-x objective-c++ -fobjc-arc` on macOS, so an
Objective-C pointer in one of the C++ structs there is a strong reference and
Rust's `Retained<T>` fields need nothing said about them.

Rust's delegates are `define_class!` invocations with an ivars struct. Here
each is a real `@interface` at file scope — an Objective-C class cannot live
inside a C++ namespace — holding the `wry::WebView*` those ivars would have
held: the IPC message handler, the navigation delegate, the UI delegate (the
open panel, the media-capture permission and `window.open`), the title
observer, and one `WKURLSchemeHandler` class where Rust builds a class per
scheme at runtime to have somewhere to put the index. `WryWebView` is the
sixth class: like the source it implements `acceptsFirstMouse`, lets a child
pass Command-key equivalents back to the application's menu and turns mouse
buttons four and five into DOM back/forward events.

The Darwin builder state is carried in the portable attribute record:
persistent data-store identifiers on macOS 14+, an optional existing
`WKWebViewConfiguration`, link previews, the macOS 14 inactive scheduling
policy and the traffic-light inset. A configuration supplied by the caller
keeps its data store and any custom scheme it already registered, as Rust's
builder does. A child does not take first-responder status when it is made;
the pinned crate documents `focused` as unsupported on macOS and only makes
a non-child webview first responder.

Two things differ beyond that, and both are in the file's header comment:

- **A webview that is not a child is a subview**, not the window's
  `contentView`. Rust swaps in a `WryWebViewParent` so the page gets key
  events; the content view here is the gpui view that draws everything else
  and owns the window's input, so evicting it would take the application with
  it.
- **`reparent` is ours.** Rust has it on Windows only; on macOS it is
  `removeFromSuperview` plus `addSubview`, which is what the one caller means
  by it.

An asynchronous custom-protocol responder does not retain the C++ `WebView`.
It retains the live-task set and its `WKURLSchemeTask`; closing the view first
empties that set, so an answer posted later is discarded on the main thread.
This is the same lifetime boundary as Rust's global webview-id lookup plus
per-task UUID. Teardown also nulls every delegate's non-owning pointer before
ARC releases it, because WebKit is allowed to drain an already-queued
callback after the view leaves its hierarchy.

`set_theme` and `set_memory_usage_level` answer false there, because both are
`WebViewExtWindows` in Rust with no WKWebView counterpart.
`set_background_color` is a successful no-op — the exact macOS arm of the
Rust method — because on macOS the only creation-time knob is the
`transparent` attribute.

**The backend and headless suite are compile- and test-verified; the window is
not visually run-verified.** `bun cmd/mac-build.ts -rel webview` and the full
release test target build on a Mac over ssh, and that native test binary runs
20,839 checks. A Cocoa window needs a login session, so the example still has
to be run on the Mac itself (`bun cmd/run.ts -rel webview`) for anyone to see
and interact with the page.

## What is ported

The whole of the portable API in `lib.rs`, and under it the WebView2 and
WKWebView backends:
attributes and defaults, a webview built into a window or as a child of one,
bounds / visibility / focus, `evaluate_script` with and without a callback,
`load_url`, `load_url_with_headers`, `load_html`, `reload`, `url`, zoom,
background colour, theme, the memory usage level, `reparent`, `print`,
`clear_all_browsing_data`, devtools, `webview_version`, the IPC channel
(`window.ipc.postMessage`), initialization scripts, custom protocols with
their `http://<scheme>.host` work-around and their asynchronous responder,
navigation / page-load / document-title / new-window handlers, the clipboard
permission, incognito, background throttling, the proxy switches, the Darwin
builder extensions named above and the Windows-only builder extensions
(browser arguments, accelerator keys, context menus, the https scheme,
scrollbar style, extensions). Enabling extensions and naming an extension
path performs the pinned source's second step too: the Windows backend walks
that directory and loads each unpacked extension through
`ICoreWebView2Profile7`. Windows download handlers can allow or cancel a
download, replace its destination path and observe terminal completion with
the original URL, optional result path and success bit. Its default-enabled
drag/drop feature is also present: an `IDropTarget` replaces WebView2's on
each composition child and reports Enter, Over, Drop and Leave with UTF-8
paths and child-relative coordinates.

Windows' remaining linked builder seam is present as opaque borrowed ABI
handles: an existing `ICoreWebView2Environment` can be supplied to
construction, and controller, environment and native webview accessors mirror
`WebViewExtWindows`. The environment input is AddRef'd immediately, so the
source webview may then be released. New-window features name their opener;
the callback can build a target with that environment and return the target
through `NewWindowResponse::Create`, after which the backend supplies its
native webview to `SetNewWindow`.

The four Windows cookie methods are also complete. The repository cannot take
Wry's `cookie` and `time` crate dependencies, so their WebView-visible value is
the POD `Cookie`: owned UTF-8 name/value/domain/path, optional boolean and
SameSite settings, session/expiry state, and optional max-age seconds.
Queries synchronously pump WebView2's completion callback like Rust and return
an owned `Vec<Cookie>` released by `CookieListFree`; set and delete construct
the same native cookie and apply max-age against current Unix time.

The macOS backend has all of that except what the platform does not have: the
custom protocol work-around is Windows' alone (WKWebView takes a scheme
handler for the real scheme), the browser arguments and their neighbours are
WebView2 settings, and the two Windows-extension calls named in the section
above answer false.

Not ported, each for a reason:

- **Cookies on macOS**. The four WebView2 methods and their value semantics are
  ported without the external cookie/time crates; WKHTTPCookieStore is not.
- **Downloads on macOS**. The Windows `DownloadStarting` and per-operation
  `StateChanged` paths are ported; WKWebView's download delegates are not yet.
- **Drag and drop on macOS**. The default-enabled Windows `IDropTarget`
  implementation is ported; the `WryWebView` dragging-destination overrides
  are not yet.
- **The `_async` constructors**, which exist for callers with an async
  runtime. There is none here; `WebViewNew` is the blocking one, and it
  blocks the way Rust's does.
- **Android and iOS**, which are not platforms this tree builds for.
- **`error.rs`**. There are no exceptions here, so a call that can fail
  answers `bool` (or null) and logs. Nothing in the crate branches on an
  error kind.

## Deliberate differences

Each is also stated in a comment where it applies.

- **The builder is the attribute struct.** `WebViewBuilder::with_*` fills in
  `WebViewAttributes` one call at a time; a struct with defaults says the
  same thing in C++, so `WebViewAttributes` *is* the builder and `WebViewNew`
  is `build()` / `build_as_child()`.
- **A closure is a function pointer and a `void*`.** Every handler group
  shares the one `ctx` on the attributes, which is what a Rust closure would
  have captured.
- **Platform objects are opaque pointers.** The Windows environment and raw
  accessors, and macOS's target configuration for a created new window, keep
  COM and Objective-C types out of the portable header. The pointers are
  borrowed where documented; construction retains the ones it stores.
- **A cookie is the value WebView sees, not a parser.** Wry's public type comes
  from the `cookie` crate; this tree's POD preserves every field the Windows
  conversion reads or writes, but does not add cookie-header parsing or a
  builder DSL that no webview operation uses.
- **The new-window handler runs where the event arrives.** Rust spawns a
  thread and holds the request open with a deferral, because its closure may
  block. Nothing here can block on the UI thread, so there is no thread and
  no deferral.
- **No Windows 7 branches.** `is_windows_7()` gated the transparency and
  background-colour paths; nothing in this tree runs there.
The Windows custom-protocol responder owns its environment, event arguments,
deferral and dispatch coordinates independently of the C++ `WebView`. A
worker may therefore answer after the owner has closed without dereferencing
freed state, matching the ownership of Rust's responder closure. If teardown
has already destroyed the dispatch window, the copied response and COM
references are released instead of leaked; a missing handler receives a 500
response rather than a null call.

## The standalone extras/ pair

Besides being compiled into the gpui amalgam, this library is also
amalgamated on its own by `cmd/update-dist.ts` into an `extras/` pair (one
header + one source, base inlined, the implementation included) for using
it without gpui. `readme-dist.md` documents the pairs; never link one
beside `gpui.cpp`, which already contains this code.
