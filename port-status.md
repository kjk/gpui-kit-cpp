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

Processed through `144fb7102bf5cbb9144c38010ca62b8b97670ed9` (2026-11-22,
fix(motion): Report a Presence that starts closed as Absent instead of Running
(#4062)).
A Presence first sampled closed reports Absent with no animation frame pending,
so a nav-stack page covered before it is drawn is dropped at once.
The current update target is
`1c7e7f7c6c8d6585da909804496614da01bb8663`.

## Known gaps vs Rust

- **Pinch comes from macOS and Windows touch screens.** macOS reports
  `magnifyWithEvent:`; Windows reports a touch-screen zoom (`WM_GESTURE`
  `GID_ZOOM`), with deltas that add up to the scale since the pinch began.
  Rust's Windows source is DirectManipulation, for precision touchpads only
  (pinch and two-finger scroll with inertia); it is not ported, so a touchpad
  pinch stays Windows' Ctrl+wheel and its scroll the wheel. Linux and wasm
  report none (`src/gpui/window_mac.cpp`, `src/gpui/window_win.cpp`).
- **`GestureEvent` / `El::OnGesture` have no Rust twin.** The Windows window
  reports touch-screen gestures from `WM_GESTURE`: Pan (with the system's
  inertia), Rotate, TwoFingerTap, PressAndTap. A touch on an element with
  `OnGesture` gets every gesture (`GC_ALLGESTURES`), a one-finger pan and
  rotation included, and none of its single-finger `WM_POINTER` scrolling;
  elsewhere a one-finger vertical drag scrolls through `WM_POINTER` as before
  and a pan nobody takes (two fingers) scrolls as a precise wheel. While a
  touch lasts (`GID_BEGIN`..`GID_END`) its pinches and gestures go to the
  elements where it began (`WindowGestureBegin`). Other platforms report none
  (`src/gpui/window_win.cpp`, `src/gpui/window_common.cpp`).
- **`TouchEvent` / `El::OnTouch` have no Rust twin.** The Windows window
  reports each touch-screen finger and pen contact from `WM_POINTER*` (down,
  in-contact moves, up, canceled, capture lost) as well as the gestures and
  mouse messages it makes. A contact's Started walks the chain under it; the
  handler that clears `propagate` keeps the contact until it ends, so a long
  press or a drag handle can be timed and followed alongside `OnGesture`.
  A kept pen contact makes no mouse messages (no press-and-hold right click
  or promotion delay), and `eraser` marks the pen's eraser end. The X11
  window, built with libXi (`GPUI_HAVE_XI`), selects XI 2.2 touch events,
  so the server sends it touches instead of the pointer events it emulates
  from them; it reports every finger and makes the core press, moves and
  release the server would have made of the first one (`primary`), so a
  touch acts as a mouse as before. It reports no Cancelled and no
  gestures, and a touch that begins under a pointer grab (a modal dialog)
  is only a mouse. A pen is a tablet's slave pointer (`PenToolOf`: a
  pressure valuator, or a "stylus" / "eraser" device; libinput, wacom and
  Xwayland name them so); its XI2 tip press marks the core left press of
  the same time as the contact, whose moves and release follow it and,
  when kept, make no mouse input (`PenContact`). Barrel buttons stay mouse
  buttons. Other platforms report none (`src/gpui/window_win.cpp`,
  `src/gpui/window_linux.cpp`, `src/gpui/window_common.cpp`).
- **Window frame and placement differ from `WindowOptions` / `Window`.**
  Bounds are `ScreenBounds` in the platform's own unit (device pixels on
  Windows and X11, points on macOS), not logical `Pixels`, and are the outer
  frame: `WindowFrameBounds` (`bounds()`), `WindowContentBounds`,
  `WindowSetFrameBounds` (Rust has only `resize()`), `WindowFrameForContent`,
  `WindowDisplayBounds` / `WindowDisplayVisibleBounds` (`PlatformDisplay`
  `bounds()` / `visible_bounds()`). `WinOpts` adds to `kind`, `focus`,
  `show`, `is_minimizable`, `window_min_size` and `window_bounds` things Rust
  lacks: `parent` (Rust's Dialog takes `GetActiveWindow()`), `frameless`
  (no frame at all), `utility` (tool caption), `maximizable`, `activatable`
  (`WS_EX_NOACTIVATE`; on macOS a frameless window that can be key) and
  `maxSize`. `WindowKind` has no `AnchoredPopup` / `LayerShell`. A Dialog
  keeps its Windows caption style (`WS_OVERLAPPED`, no
  `WS_EX_DLGMODALFRAME`), holds its parent only while shown
  (`WindowSetVisible`), and on release activates the parent if it was the
  active window; macOS bounces clicks and keys from the parent with a local
  event monitor and X11 grabs pointer and keyboard (one dialog at a time on
  both). `WindowActivateInApp` (never takes the focus from another
  application), `WindowIsEnabled`, `WindowSetFrameAppearance`,
  `WindowOnBoundsChanged`'s `MoveResizeEnded` (`WM_EXITSIZEMOVE`; macOS ends
  a live resize or polls the button after a move, X11 polls the button),
  `WindowSendNativeKey` (macOS test support) and `WindowAtPoint` (the app's
  window on top at a point, where a drag released outside its window lands)
  have no Rust twin. X11 hides
  the window manager settling a requested frame from the bounds observer
  (within 4 px, 8 configures, or the user taking over). Observers are one
  per window, and `AppOnWindowClosed` one per app, plain functions rather
  than subscriptions. A scrollbar over a `ClickWinCaption` area is not
  caption. The Windows class icon is resource 1, as Rust's `load_icon`.
  wasm, iOS and Android answer empty bounds
  (`window_frame_unsupported.cpp`; `src/gpui/window_*.cpp`).
- **`on_open_urls` is macOS only** and takes a plain function, not a
  closure (`AppOnOpenUrls`, `src/gpui/window_mac.cpp`).
- **`WindowCapturePngTemp` has no Rust twin.** A PNG of a window as drawn,
  for test automation without screen-recording permission: macOS
  `cacheDisplayInRect:` plus WKWebView snapshots, Windows `PrintWindow`,
  Linux the cairo back buffer (no WebKitGTK views, app windows in creation
  order). wasm answers "ERR unsupported" (`src/gpui/window_*.cpp`).
- **`WindowPrompt` (`window.prompt`) answers through a listener**, not a
  oneshot receiver; a canceled receiver is `PromptResponse::ix == -1`.
  A prompt still pending when its window closes answers -1 (macOS ends the
  sheet; Windows destroys the task dialog with its owner).
  Windows runs `TaskDialogIndirect` from the main-thread queue, not a
  dialog thread; it is centered on its
  window (`TDF_POSITION_RELATIVE_TO_WINDOW`) and needs the application's
  common-controls v6 manifest (the examples have none and draw the
  fallback). macOS is Rust's sheet `NSAlert`. Linux, wasm, iOS and Android
  draw the fallback renderer. `PromptOptions` (title, default button, RTL)
  has no Rust twin. `set_prompt_builder` takes only the fallback
  (`AppSetFallbackPrompts`); the fallback does not suppress mouse-down-out
  listeners under it (`src/gpui/window_*.cpp`).
- **`PromptForPathTemp` / `PromptForNewPathTemp` (`prompt_for_paths`,
  `prompt_for_new_path`) are synchronous** and answer a temp path, not a
  receiver. Ours, no Rust twin: file types (`PathFilters`, with the initial
  one and the one chosen), a start folder and name for open prompts, a save
  title and default extension. Windows has them all (IFileOpenDialog /
  IFileSaveDialog; only it reports the chosen type). macOS panels have no
  type menu: open takes the union of the types (anything when one is `*.*`),
  save the first type's or the default extension. Linux uses zenity/kdialog,
  not the XDG portal; the first filter listed is the initial one, and the
  default extension is added after the overwrite question. wasm answers
  empty; iOS and Android have neither (`src/gpui/window_*.cpp`).
- **`CursorImage` / `El::CustomCursor` have no Rust twin.** A pointer made
  from premultiplied RGBA pixels, owned and freed by the caller: Windows
  `CreateIconIndirect`, macOS `NSCursor initWithImage:hotSpot:`, X11
  libXcursor (soft dependency; without it, and on iOS/Android, the element's
  `CursorKind` shows), wasm a CSS `url()` cursor. The window also resolves
  the cursor after every frame, as Rust does, not only on a move
  (`src/gpui/window_*.cpp`).
- **`WindowStartDragOut` (drag out of the window) has no Rust twin.** It
  offers copy of text, an encoded image (a virtual file named
  `imageName`) and/or file paths, with an optional preview (the image
  scaled to `previewMaxEdge`, on white, framed), and reports
  `DragOutResult` through a listener, then sends the window the mouse up
  the drag ate. Windows runs `DoDragDrop` from the main-thread queue with
  its own `IDataObject` (`CF_UNICODETEXT`, `CF_HDROP`,
  `CFSTR_FILEDESCRIPTORW` + `CFSTR_FILECONTENTS`, Preferred DropEffect) and
  draws the preview with `ImageList_BeginDrag`; the last data object stays
  connected until the next drag or exit, for targets that read it late.
  macOS begins an `NSDraggingSession` (`NSPasteboardTypeString`, file URLs,
  an `NSFilePromiseProvider` for the image; no raw PNG pasteboard type).
  `allowMove` / `allowLink` offer those effects too. Files alone go out on
  Windows as the shell's own data object (`SHCreateDataObject` plus
  `CF_HDROP`) through `SHDoDragDrop` with the shell's drag image, as
  Explorer's do. Linux (XDND source), wasm, iOS and Android answer
  `Unsupported` (`src/gpui/window_common.cpp`, `window_win.cpp`,
  `window_mac.cpp`).
- **`ClipboardSetPaths` / `ClipboardHasPaths` / `ClipboardClear` have no Rust
  twin.** Files for a file manager to paste, copied or cut, and
  `ClipboardItem::externalPathsCut` reading which. Windows `CF_HDROP` plus
  Preferred DropEffect; Linux serves `text/uri-list` and
  `x-special/gnome-copied-files`; macOS writes file URLs and marks a cut with
  the private type `org.gpui.cut-paths`, which Finder ignores. wasm answers
  false (`src/gpui/window_*.cpp`).
- **An `ExternalText` drag has no Rust twin.** A drag carrying no files but
  text or a link (a browser's image) arrives as drag kind `ExternalText`,
  with `DropEvent::externalText` / `externalUrl` (Windows: `CF_UNICODETEXT`
  or `CF_TEXT`, `UniformResourceLocatorW` or `UniformResourceLocator`;
  macOS: `NSPasteboardTypeString`, the first non-file URL). Unlike
  `ExternalPaths` (always offered copy, as Rust does) the OS shows it as
  refused unless an element under the pointer takes `ExternalText`. Linux
  and wasm do not report it. Windows no longer shows a copy cursor for a
  drag that has neither files nor text.

- **Oversized inline code uses one wrapped box.** Each line shapes inside
  one pair of insets. The span shares its background and border across the
  lines, and the flex row can move the entire box below preceding prose.

- **Textarea decoration styling uses `TextSpan`.** The layer follows edits
  and supports text color, background and underline. Rust's highlight
  style can also change the glyph weight within the run.

- **The Markdown example renders MathJax on a background worker.** Native
  builds use Node and the optional website installation. The browser and a
  native build without that package render the same Unicode fallback as a
  formula awaiting its image (`examples/markdown.cpp`).
- **Selectable text highlight font ranges cross the mobile paint adapter.**
  iOS and Android hosts can read `PaintCtx::textFontRanges` when shaping
  `TextLayoutNew`. On wasm the Canvas2D backend positions styled font runs
  in logical order and relies on Canvas2D for shaping within each run. Core
  Text uses the italic face for an oblique highlight. Highlight strokes use
  the portable text decoration geometry (`src/gpui/gpui.cpp`).
- **The legacy C++ reduced-motion setter treats same-value writes as explicit.**
  Calls without an app set a process default. Applications can set their own
  preference and resume the shared platform reading
  (`src/base/motion.h`).
- **List continuations share the first paragraph's text column in C++.**
  The fixed marker column belongs to the entire item. Rust reserves a
  minimum marker width for the first paragraph and applies a separate
  continuation indent, so the two origins can differ
  (`src/base/text.cpp`).
- **Empty editor placeholders use the unindented layout.** The C++ text
  origin reader reports zero there. A populated code editor reports its
  laid-out gutter, including the 6 DIP gap with line numbers hidden
  (`src/base/input.cpp`).
- **Disabled selection controls retain path-based focus positions.** Their
  derived IDs resolve during layout, so C++ keeps their place in the focus
  order with Tab stops off. Explicit handles on disabled controls are kept
  only while focused (`src/base/button.cpp`).
- **The table frame benchmark measures arena use.** Its memory column
  reports bytes used to build the frame in C++ (`bench/TableFrameBench.cpp`).
  The Rust benchmark reports total allocation calls and bytes.
- **Link tooltips build frame-local elements.** Rust retains its tooltip
  view entity. C++ invokes the builder on each visible frame and passes it
  the owning view context and URL (`src/base/tooltip.cpp`).
- **Client corner radii shape the painted window.** The frame, root and
  title bar use `window.radius`. Rust also supplies the corner radii to
  its Wayland compositor integration. This port uses X11 on Linux
  (`src/ui/window_border.cpp`).
- **Transition history is caller-owned.** C++ `TransitionState` copies
  carry independent history. Callers that share history pass the same
  state by address (`src/base/motion.h`).
- **`examples/gpui_perf` is the windowed showcase, not the headless suite.**
  `--auto` runs the nine gpui-fast scenarios (spinner, sidebar, components
  page, 5000-row table, 33 ms refresh, 5000-message list, workspace
  quotes, scroll and hover) against the same 245-name page list. Components
  are raw divs. The workspace keeps the 200-by-15 watchlist, quote header,
  120 candles, 10-level book, 80 trades, a donut and 12 hidden feed
  subscribers, drawn with plain elements rather than Longbridge Pro widgets.
  Allsum chat is a title only. `--retention` does nothing until view
  retention exists. Instruction counts and the headless scenario crate are
  not ported (`examples/gpui_perf`).
- **Scroll layers blit a pure scroll.** When the only change inside a scroll
  viewport is the offset, the frame copies the overlap already on the swap
  chain and redraws the strip that entered, plus a scrollbar thumb that
  moved. It is always on (`__scene=off` is the only way around it, and
  `GPUI_SCROLL_VERIFY=1` checks each blit against a from-scratch replay).
  Content that changes (a quote tick, a refresh)
  still draws whole (`src/gpui/scene.cpp`).
- **The semantic tree is collected only for a listener.** Rust hands
  AccessKit a tree every frame. Here a frame walks the element tree for
  accessibility nodes only while `PlatAccessibilityWanted` says something
  reads it: on Windows a UIA client is listening or has asked the window
  (WM_GETOBJECT); macOS and Linux always collect; wasm never; a headless
  test window always does. An idle frame otherwise paid that walk and its
  hash for nobody (`src/gpui/window_common.cpp`).
- **A document's text can be read on demand.** Local addition for large
  read-only documents (a PDF viewer's pages): `El::AccessibilityDocument`
  gives a Document node text and selection callbacks that the bridge calls
  only when a client reads them, plus revisions that enter the tree hash.
  Windows raises UIA TextChanged / TextSelectionChanged on a revision change;
  macOS and Linux read the same text but announce only the tree change
  (`src/gpui/accessibility_win.cpp`).
- **A document's parts are enumerated on demand.** Local addition: an
  `AccessibilityDocumentSource` may list parts (a PDF's pages) with a name,
  text, bounds and text range each, read only when asked, so a large
  document adds no El per page per frame. Windows exposes them as UIA
  children after the node's element children (Custom control, read-only
  Value, hit testing, `RangeFromChild`). Linux exposes them as AT-SPI
  children at `.../n<id>/p<part>` (page role, read-only Text of the part's
  own text, Component extents, hit testing); AT-SPI has no
  `RangeFromChild`, so the part range is unused there. macOS does not expose
  them yet (`src/gpui/accessibility_win.cpp`,
  `src/gpui/accessibility_linux.cpp`).
- **Fitted text does not contribute its max-content width.** A
  shrink-to-fit bubble around a TextView sizes to its padding and list
  markers. A run is width 100%, and that percentage does not fall back
  to the text's max-content while the parent is being measured
  (`src/base/text.cpp`). Upstream's fitted-list checks are wider than
  the same words as a paragraph and wrap inside the container.
- **Painted text is not observed for tests.** Rust records each label,
  shortcut, text run and avatar name while painting when `test-support`
  is on, and `test_support::texts` returns that log (`observe.rs`,
  `test_support.rs`). The C++ test platform reads the element tree
  directly and does not keep a paint-order text log
  (`src/gpui/test_app.h`).
- **Gutter marks stay on document rows when ghost lines appear.** An
  inline completion that inserts rows under the caret does not push
  gutter bars and boundary pills down with those rows (`input.cpp`).
  Rust shifts the marks with the ghost text. The decorations story has
  no inline completion. Fold placeholders and fold backgrounds follow
  those same laid-out rows: Rust's `layout_folded_lines` moves rows
  below the ghost text down by its height, and this tree does not.
- **A folded line's placeholder is an element, not a prepaint root.**
  It is absolutely positioned on the last visual row and hit-tested
  from the box that row recorded (`input.cpp`). Rust prepaints it at
  the same place and `stop_propagation` keeps the press off the text.
  A click outside that box still places the caret. It is not part of
  the line's width, so a soft-wrapped row that already fills the
  column clips it, as upstream does.
- **Selection order is the caller's document order.** Rust numbers
  paint-order participants after a complete frame and refreshes once
  when a partial redraw cannot place a retained sibling
  (`text_selection.rs`). This runtime has no cached-view replay, so a
  participant that misses a frame is gone (`text_selection.cpp`).
  This also means the Shell command host builds again on an unchanged draw
  where GPUI Fast reuses the previous frame.
  Callers pass `WithDocumentOrder`. `begin_selection_frame` is a no-op
  and does not assign that order.
- **Data table cell labels are built whenever a hook is set.** Rust
  calls `cell_label` only while assistive technology is connected
  (`window.is_a11y_active`). This tree has no such query, so the hook
  runs every frame (`table.cpp`).
- **Inline code style overrides skip some text properties.** Font size,
  color, background, border, radius and padding are applied
  (`text.cpp`). Font family, features, fallbacks, weight, italic,
  underline and strikethrough on that refinement are not: the run stays
  on the monospace face, and weight still comes from the surrounding
  marks. Lengths are DIPs resolved when the style is built (`Rems`),
  which is how every other style in this tree names a size.
- **A dialog outside the window stack eases in on its own clock.**
  `Dialog::IntoEl` still runs the entrance when no window layer supplied
  a presence sample (`dialog.cpp`). Rust takes that clock only from the
  window dialog layer, so a trigger dialog there appears at rest and
  closes by unmounting. The window stack keeps a closing dialog mounted
  and plays the entrance back out.
- **Search uses per-character Unicode lowercasing.** Result highlights
  retain UTF-8 byte offsets and support expansions. The default item filter
  also searches across expansions, with the per-character lowercase
  mapping for Greek sigma (`src/ui/label.cpp`).
- **Mixed editor leaves report visible fragments.** Mixed paragraphs expose
  their laid-out word and atom boxes through `LeafGeometry` (`text.cpp`).
  Empty lines within a mixed leaf and editor decorations still need coverage.
- **High-DPI on Windows.** A window draws at its monitor's dpi and follows
  `WM_DPICHANGED` (`window_win.cpp`, `SetDpi` in `paint_win.cpp`).
  `GPUI_DPI=<dpi>` forces a dpi for checking layout on a 100% display.
- **A transparent window is Windows only.** GPUI's
  `WindowBackgroundAppearance::Transparent` is `WinOpts::transparent`: a
  composition swap chain with premultiplied alpha and grayscale text
  (`paint_win.cpp`). X11 would need a 32-bit visual and Cocoa a non-opaque layer; there the
  option is ignored and `WindowIsTransparent` says so. `Blurred` and the
  other appearances are not ported.
- **A focused slider takes keys.** `slider.rs` leaves them to the
  accessibility layer, which this tree doesn't have: the arrows step,
  Page Up / Page Down move a tenth of the range and Home / End go to its
  ends, as an HTML range input does (`SliderKeyMove`, `src/base/slider.cpp`).
- **A browser file drop carries names, not paths.** A desktop window accepts
  an OS file drop as gpui's `ExternalPaths` (`OnDrop(StrL("ExternalPaths"))`,
  newline-separated paths on `DropEvent::externalPaths`). Windows registers
  an `IDropTarget`, macOS an AppKit dragging destination, and Linux X11
  Xdnd for `text/uri-list`. The page can read only each file's name
  (`window_wasm.cpp`).
- **Soft wrap of a large document.** The native editor caches ASCII widths by
  character count as it needs them and reuses the bounded exact-width cache
  to validate Unicode prefix sums. Rows that share a Unicode prefix and a
  space-separated fixed-width integer suffix reuse the prefix's shaped width
  after comparing it with a complete line. The wrap tree skips valid subtrees
  on resize. Editor rows shape VT, FF, CR, NEL, LS and PS as spaces of the same
  byte length (`TextNoLineBreaks`) so the shaper keeps them in one row.
- **Media and volume keys have names.** Rust's gpui names none of them. This tree
  binds `medianext`, `mediaprev`, `mediastop`, `mediaplaypause`, `volumemute`,
  `volumedown` and `volumeup`, with the Win32 codes. X11 maps the XF86 audio
  keysyms, macOS the system-defined media key events, and Windows and the
  browser deliver the codes as they are.
- **One engine.** Upstream selects GPUI Fast with the `gpui-fast` Cargo
  feature. This tree is that engine's C++ port and has no second backend
  to switch.
- **Upstream package names.** `crates/component` remains `src/ui/` here;
  `gpui.h` and `AppNew`/`ThemeSet` provide the Kit facade and initialization.
  Rust procedural macros and Cargo publishing have no C++ runtime counterpart.
  The GPUI reference is `gpui-pre` 0.3.12 (Zed `d2a4fb6ded7c`). Layout is
  taffy 0.15.0. The other ported dependency versions are unchanged.
- **Base Root's Tab and copy actions are the runtime's.** Base Root's Tab /
  shift-Tab / copy actions are the runtime's for every window (`FocusNext`,
  `WindowSelectionCopy`), and WindowState's tooltip overlay is the window's
  own (`src/base/root.cpp`, `src/ui/root.cpp`).
- **Scrollbar track clicks follow Windows.** A click pages; Shift+click jumps
  the thumb to the pointer. Rust jumps on every click
  (`src/gpui/window_common.cpp`).
- **Editor range decorations do not notify their owning view.** Decorations
  are measured and painted at the editor column's current visual rows, but
  collection methods require the owning view to re-render
  (`src/base/input.cpp` PaintEditorUnderlay).
- **A scrollable menu's default height cap ignores half the window.**
  Rust uses the smaller of half the window and 28.125rem. This tree
  uses 28.125rem, or the caller's DIP max (`src/ui/menu.cpp`).
- **A context menu builder is not cancelled when its field is dropped.**
  The handler is posted with `ExecPost` and keeps the `InputState` pointer.
  Rust skips the call when the input entity is gone
  (`src/base/input.cpp` `RunContextMenuJob`).
- **Auto-grow row counts are logical lines.** `update_auto_grow` and the
  edit path count `RopeLinesLen`, not `display_map.wrap_row_count()`, so a
  soft-wrapped line does not add a row (`src/base/input.cpp`).
- **Scroll bounce builders do not panic.** `WithTracking` and
  `WithMaxStretch` leave a non-positive or NaN value unchanged. Rust
  panics (`src/base/scroll_bounce.h`).
- **DockToggleEvent is a listener on DockState.** Rust emits
  `EventEmitter<DockToggleEvent>` on the component `DockArea`, and only
  `DockSkin`'s controls raise it. The entity here is `DockState`
  (`src/base/dock.h`), so the event is `onUserToggle`, beside `onEvent`.
  A click on a collapsed side's tab opens that side and reports it; Rust
  only opens the bottom strip.

- **Shell stays on the portable QuickJS-NG interpreter.** Upstream Rust moved
  to the platform-specific quickjs-jit runtime in `88a1bdc8`; the C++ shell
  keeps the repository's sole vendored-source exception and identical host API
  on every target, including wasm (`src/quickjs`, `src/shell/runtime.cpp`).
- **`on_pinch` is accepted and never delivered.** The callback name is
  recognized so a script can pass a handler. gpui has `PinchEvent` and
  `El::OnPinch`, but materialize does not bind one (`src/shell/runtime.cpp`).
- **`fs.stat` does not follow a symlink.** Rust follows a link that stays
  inside the grant. Here `stat` refuses a symlink and a Windows junction,
  and `lstat` describes the link. POSIX `atime` and `birthtime` stay 0;
  Windows fills them (`src/shell/filesystem_posix.cpp`,
  `src/shell/filesystem_win.cpp`).
- **Fetch sends the default User-Agent; a WebSocket handshake cannot.**
  There is no WebSocket client. `fetch` adds `User-Agent: gpui-shell/<version>`
  when the script set none (`src/shell/fetch.cpp`).
- **A window has no application id.** A packaged run knows the manifest id,
  but `WinOpts` has nowhere to put it, so a Linux desktop file is not matched
  to `<id>.desktop` (`gpui_shell/main.cpp`).
- **`dev` does not raise a debug log.** The host has no tracing subscriber.
  `Invocation::verbose` is parsed and not applied (`src/shell/host.cpp`).
- **Bundle icons are a box filter, and only 8-bit PNG.** Rust scales with
  Lanczos3. Paletted and 16-bit icons are refused (`src/shell/bundle.cpp`).
- **A Windows bundle downloads its runtime through `HttpGet`.** That client
  stops at 16 MiB. The unit test injects the zip; a release binary larger
  than the cap fails the live download (`src/sys/http.h`,
  `src/shell/bundle.cpp`).

- **A key down's held and IME flags come from the platform, not from a
  keystroke.** A key event here has no `key_char`, so
  `Keystroke::is_ime_in_progress` is the platform saying an input method
  has the key — Win32 `VK_PROCESSKEY`, the browser's keyCode 229, marked
  text on macOS, a printable X11 key that looked up no character — rather
  than "a printable key with no character", which on Windows also covers
  Enter and Tab. X11 marks the press after a dropped auto-repeat release as
  held, where Zed's X11 client drops the same release but reports every
  press as fresh (`KeyDownFlags`, `src/gpui/platform.h`).
- **TextView parser callback payloads are caller-owned.** Parser tables and
  renderer names are copied out of frame storage into state and parse-job
  arenas; callback payloads must outlive outstanding jobs and be safe to read
  on the executor. The legacy `MdPlugin` hook takes a `Ctx` and remains a
  render-time block replacement and can read the original HTML around its
  projected nodes. Markdown parser plugins run on the background
  executor for appends and large replacements. Updates arriving during a parse
  are taken up when it lands rather than bounded by
  `MAX_COALESCED_UPDATES_PER_PARSE`. Invalid Markdown events produce a
  literal paragraph. Streamed fades start on the first frame that shows them.
  `RenderedText` reads the view's index rather than retaining the parsed
  document: read and convert through a fresh snapshot, only compare old ones
  (`src/base/text.cpp`).
- **Measured soft wrap probes cached character widths first.** Line-break
  opportunities come from the full `unicode-linebreak` 0.1.5 port (Unicode
  15.0), matching Rust. `MeasuredWrapBoundaries` starts each row's search
  from the cached character widths rather than from one grapheme, changing
  how many prefixes are shaped, not which one is chosen. Grapheme filtering
  still uses this tree's UAX #29 subset (`src/base/input_editor.cpp`).
- **A token's hover exit is reconciled as the field builds, not at
  prepaint.** Rust's `prepaint_tokens` compares the retained hover against
  the token elements it has just placed. The rows here are elements built
  before layout, so a field reconciles when it starts building its rows and
  reads the ones the last build placed: an exit for a token that was
  removed, replaced, masked or disabled arrives in the same frame, one for
  a token scrolled out of the built rows a frame later. Re-entry under a
  still pointer works by giving the chip a new id (`hoverEpoch`) rather than
  by resetting GPUI's retained element state (`src/base/input.cpp`,
  `src/base/input_tokens.cpp`).
- **Speech's platform halves are written against the OS, and two are
  untested on hardware.** Rust captures through cpal and reaches WinRT and
  the Speech framework through crates; `src/sys/audio_input.h` is WASAPI, an
  AudioQueue and ALSA directly, and `src/sys/speech_recognizer.h` is
  `Windows.Media.SpeechRecognition` through the SDK's ABI headers (combase
  loaded by name) and `SFSpeechRecognizer`. Both are always compiled in,
  where Rust puts them behind its `speech` feature; ALSA is a soft
  dependency (`GPUI_HAVE_ALSA`), and without it Linux has no microphone.
  The macOS microphone asks an AudioQueue for 48 kHz mono rather than
  taking the device's own format. A recognizer's results are posted to the
  main thread with `ExecPost` where Rust awaits them in a task, and a
  sink's deferred call and the stop timeout ride a window (`WindowPost`, a
  window timer) where Rust uses `cx.defer` and the executor's timer. The
  wasm build has neither half, so the Speech story feeds its demo
  recognizer a generated tone there and on a Linux build without ALSA.
  The WASAPI capture was run end to end; the Windows recognizer, the macOS
  halves and ALSA capture compile and pass their unit tests but have not
  been run against a speech pack, a granted microphone or a sound card.
- **`examples/speech` asks the tree for what Rust asks cpal and Cargo.** The
  input device's name comes from `Microphone::DeviceNameTemp()`
  (`SysAudioInputDeviceName`), which Rust's `Microphone` does not have: the
  Rust example calls cpal itself, and an example here names no audio API. On
  ALSA that name is `default`. The three extra icons are files under
  `assets/speech/icons` rather than embedded, the usage descriptions are
  `examples/speech_Info.plist`, linked into the macOS binary by
  `cmd/build.ts` where Rust has a `build.rs`, and the demo recognizer is
  `examples/speech_demo.h` so `tests/SpeechDemoTests.cpp` can reach it. The
  window has no minimum size (Rust: 760 × 520) because no window here has
  one. `speech --check` prints to stdout, which a Windows GUI build only has
  when its output is piped or redirected. The example was run on Windows
  with no input device and no speech pack, so a whole dictation — demo or
  system — has not been seen end to end.
- **A TextView's scroll layouts are flags, not `overflow` on a refinement.**
  Rust opts a table into horizontal scrolling with `overflow.x: Scroll` on
  `style.table` and a code block into vertical scrolling with `overflow.y:
Scroll` on `style.code_block`. A refinement here names no overflow field,
  so they are `TextView::TableScroll()` and `TextView::CodeBlockScroll()`;
  the max height still comes from the `code_block` refinement.
- **TextView reveal scrolling settles on the following layout.** The root
  checks the current frame's line against the active content mask and invokes
  `OnReveal` during that paint. Its own scroll offset, or an enclosing
  VirtualList's scroll handle, is updated then and laid out on the next frame;
  Rust can scroll and prepaint the line again within the same frame
  (`src/base/text.cpp`, `src/base/virtual_list.cpp`).
- **A default `<mark>` paints near-black ink.** Rust leaves the foreground
  alone (`html.rs`). `#fef08a` disappears on the dark theme's near-white
  text, so that default wash pins the ink to `#0a0a0a` (`src/base/text.cpp`).
  A tag that names another color keeps the run's own foreground.
- **TextView selection mapping uses painted runs.** The window owns selection
  rather than each parsed inline node. Source mapping includes images when a
  selected run reaches their boundary, and Select All follows committed
  appends until the selection moves. A painted block plugin is included whole
  when the drag starts or ends inside its box or covers it. An unpainted
  plugin inside a selection is not copied: Rust still emits one unless its
  node has observed an empty selection. Under `-markdown=mini` the parser
  keeps no positions, so source mapping is unavailable (`src/base/text.cpp`,
  `CopyTextHitsFiltered` in `src/gpui/gpui.cpp`).
- **The window's selection of painted text needs no layer.** A participant
  registers only while a `TextSelectionLayer` is rendering, as Rust's
  `WindowSelectionState::existing` asks, but the selection of the runs the
  frame paints is the window's own and works in a window with no layer,
  which Rust's would not. Participants register as their view builds rather
  than at prepaint, so a registration made before the layer renders on a
  window's first frame is dropped. A run under no `text_selection_scope`
  takes its focus trap as its scope, which is what confines a dialog's text:
  the dialogs name no scope of their own (`src/base/text_selection.cpp`,
  `ElSelectionScope` in `src/gpui/gpui.cpp`).
- **A chart's tooltip datum is what `Data(..)` gave it.** The charts hold
  numbers rather than mapping a `data: Vec<T>`, so the datum the tooltip
  closures receive is an item of what `Data(..)` gave the chart, or the
  chart's own number for the point (`ChartTooltipContent::Datum`,
  `src/gpui/gpui.h`).
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
- **The shell window host follows root.rs.**
  `ShellRootOf` is `window.root::<ShellRoot>()`, the root owns its own tooltip layer
  (`Window::rootTooltip`), a background press blurs the focused field, and
  an application mounts with the default policy. A focusable element taking
  a press marks it `pressTookFocus`, which `WindowDefaultPrevented` reports
  as GPUI's focus handler preventing the default does, but which still
  lets the field under it place its caret.
- **Registered typed parts can render twice.** A typed part is rendered
  standalone and again by its parent. A retained InputState hands a change
  to at most 16 components rendering it in one frame (`src/component_shell/`).
- **Shell scrollbars use the viewport's integrated bar.** Scroll and
  Scrollbar share current-frame axis and visibility settings regardless of
  sibling order, but `viewport_from_layout` uses the Scroll viewport's box
  rather than a separately laid-out overlay (`src/component_shell/scroll/`).
- **Registered-component counts are held to INT_MAX, which reads the same.**
  The registry reads a usize as 64 bits and checks it as Rust does, and the
  numbers a script can see are 64-bit: a Badge's count and max,
  Pagination's pages, a chart's values (f64 for its tooltip and labels) and
  a listener's argument. The other usizes reach components that count in
  `int` and are clamped to INT_MAX, which behaves as Rust's value does for
  any input a window can show: a selected index (tabs, toggle groups,
  steppers, a settings page) or a span past the last item selects or spans
  the same; a tick margin past the data labels the same points; and a count
  that is rendered (Rating stars, OTP groups, calendar months, radar grid
  levels, textarea rows) hangs at either size, as Rust's would. A
  MessageScrollerState or OtpState length past INT_MAX is refused, since
  this tree's int-indexed Vec cannot hold it; Rust would abort allocating
  it (`src/component_shell/compound/common.cpp`).
- **Component-shell gaps against the Rust components.** MenuItem/Menu
  `disabled` and the retained forms' `disabled()` are inert (upstream
  records them as common behaviors and drops the op — ported as-is).
- **A start or middle ellipsis measures off the shaped run.** It measures
  characters off the shaped run where Rust sums each one's advance, which
  differs only by kerning (`src/shell/style.cpp`,
  `TruncateTextStartOrMiddle`).
- **Font features are one flag, and the browser ignores it.** GPUI's
  `FontFeatures` is any list of OpenType (tag, value) pairs; here it is
  `FontFeatures::TabularFigures` or none — `tnum`, the one gpui-kit names —
  carried as a bit of the text weight word. Canvas2D has no
  `font-variant-numeric`, so in the browser the TimeField's digits stay
  proportional (`kFontTabularNums`, `src/gpui/paint.h`).

- **The highlighter state has no factory.** The themed layer installs the
  implementation itself. A multi-edit change past 64 edits or 64 MB of
  reconstructed per-edit text falls back to a whole-document update
  (`src/base/input.cpp`, `src/ui/highlighter.cpp`).
- **No language server.** Every seam in `input/editor/lsp` is ported —
  completion, resolve, ghost text, hover, code actions, document colours,
  semantic tokens, go-to-definition — but there is no JSON-RPC, no child
  process and no `lsp_types` (hard rule 3). A provider is a function pointer
  an application fills.
- **Syntax colouring is a scanner, not a parser.** `src/ui/syntax.cpp` in
  tree-sitter's place: comments, strings, numbers, keywords, type names, and
  what position alone settles. Nothing that needs a tree (rename, semantic
  scope) can be asked of it. Folding is brace-pair scanning, which is what
  upstream's own showcase highlighter does. The optional `syntect` feature
  (syntect and two-face) is the same exclusion and is not vendored.
- **Diff line numbers are `int`.** A hunk header whose start, count, or
  `start + max(count, 1)` span does not fit in `int` is an invalid
  header (`diff.cpp`). Rust keeps those as `usize`, so a start of
  `usize::MAX - 1` parses there and does not here.
- **Diff review text is line ranges, not a text selection.** `src/ui/diff.cpp`
  selects source lines and copies them. It does not drive GPUI's
  `TextSelection`, so the hit-tested caret, word sweeps and chunk clipping
  in `selection_tests.rs` and `header_tests.rs` are not ported. Inline
  emphasis is computed on the frame that asks. Character hover and click
  (`OnTextHover`, `OnTextClick`) find the character from the line's glyph
  boxes and the hit box rather than from TextSelection's painted runs, and
  the hover box spans the row height. An application syntax
  highlighter is prepared there too, once per hunk side, rather than on
  the background executor. A rebuild keeps the
  scroll index rather than the source line that was at the top. Soft wrap
  is stored and not applied. Both modes use the runtime's horizontal
  scrollbar around the vertical list. The source scrolls inside fixed
  gutters and headers, and the scroll range is estimated from the widest
  lines ranked by UTF-8 scalar count.
  The gutter marker lane is one row square inside the fill-width row.
  The add-annotation
  button inset is not
  applied because that button is not painted. Annotation content is
  painted inside the uniform row, which does not grow with the comment.
  Gutter `TextSelection::Cleared` is not a separate path: clearing goes
  through `SetSelectedLines`, which drops the shift anchor and does not
  emit. The story page shows every example patch from `diff_story.rs`
  (the pull request is the review, added, deleted, and mode files
  together), previous/next, and the options menu (example, mode,
  context, inline, separator, indicator, tint, hover, and wrap). The
  Alex review comment is on the pull request. `examples/tig`, the git history
  browser, is not ported. The
  per-hunk tree-sitter test stays under the scanner gap above.
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
  retained geometry path. Every paint backend records; the renderer half of
  GPUI's scene graph was ported as two more Windows backends and removed
  (`obsolete-windows-backends.md`).
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
- **No GPUI Fast native composition surface.** Upstream always
  reparents a macOS WKWebView, and on Linux an X11 overlay window,
  onto `WindowCompositionSurface` so overlays draw above the page
  (`crates/webview/src/composition.rs` and `linux.rs`). The
  `gpui-fast` feature is a no-op; composition is not optional. This
  tree has one engine and parents the webview on the window
  (`src/webview/webview.cpp`). Without that container, a content mask
  cannot clip the page without resizing it. A partial clip keeps the
  full document bounds, so the native view extends past its container
  and covers overlays in that box. It is hidden only when the clip
  misses it entirely, or the application hides it. A failed geometry
  update is logged once, until a later update succeeds. The hit box
  is already the masked intersection. The macOS whole-point rounding
  (`WebViewDocumentFrame`) is applied to the wry rect, whose origin is
  the top left, rather than to a composition child.
- **Linux webview bounds stay logical.** Upstream `device_bounds` passes
  physical pixels because lb-wry then scales by GDK. `WebViewSetBounds`
  in `wry_linux.cpp` already multiplies a logical rect by that factor
  for the X11 child, so the kit conversion is not applied again. Page
  zoom is `WebViewMatchPageScale`, which multiplies the script's zoom
  so a later scale match does not reset it. Wayland is unsupported here
  as upstream: the window is X11, which is the backend WebKitGTK can
  embed into. A failed create whose reason names X11 tells the script
  to start the shell with `WAYLAND_DISPLAY=` empty.
- **macOS webview drag positions account for the child view.** The pinned
  Wry backend flips a window-relative point using the view's height; this
  port first converts it into the child view, then normalizes the top-left
  origin (`src/wry/wry_mac.cpp` emitDrag).
- **No webview on wasm.** `src/wry/wry_unsupported.cpp` answers. The Linux one,
  `wry_linux.cpp` over WebKitGTK 4.1 (a soft dependency), is compiled and
  linked but not yet seen running in a window; without
  `libwebkit2gtk-4.1-dev` the same file answers. `src/wry/readme.md` has both.

- **A grid repetition with the wrong number of line-name sets does not
  abort.** Rust panics unless `line_names` is empty or exactly one set
  per line (`track count + 1`). This tree has no exceptions, so a
  mismatch contributes no names and the tracks are still counted
  (`compute_grid.cpp`).

## Not ported, on purpose

Per-crate exclusion lists live with the crate: `src/taffy/readme.md`,
`src/markdown/readme.md`, `src/markdown-mini/readme.md`,
`src/html5ever/readme.md`, `src/html5ever-mini/readme.md`, `src/wry/readme.md`
and `src/autocorrect/readme.md`. `.claude/skills/update-port/crates.md` lists
the dependencies we replace rather than port.
