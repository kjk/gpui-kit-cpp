#ifndef GPUI_GPUI_PLATFORM_H_
#define GPUI_GPUI_PLATFORM_H_
/* The seam between the portable window logic (WindowCommon.cpp) and the OS
   window (Window_win.cpp / Window_linux.cpp).

   The platform file owns the message loop and translates native events into
   the Window* calls below; everything those calls do — hit-testing, focus,
   listener dispatch, drawing — is shared. */

#include "gpui/gpui.h"

namespace gpui {

// ─── called by the platform window ───────────────────────────────────────

// Build, lay out and paint one frame onto `native` — the HDC on Windows, the
// cairo surface on Linux.
void WindowDrawFrame(Window* win, void* native, int pxW, int pxH, float dipW,
                     float dipH);

// cx.reduce_motion(): whether the desktop has asked for less animation.
// Windows and macOS each have a switch to read; X11 has no such setting, so
// it answers false.
bool PlatReduceMotion();

// `key` is one of the Key* codes in Gpui.h.
// `platform` is Command on macOS and the Windows/Super key elsewhere. It
// is a modifier of its own: macOS binds ctrl- and cmd- chords to
// different actions in the same context, so folding them would lose one.
// Answers whether GPUI kept the keystroke. Windows needs the answer for
// WM_SYSKEYDOWN: an unhandled Alt chord must still reach DefWindowProc for
// system behavior such as Alt+F4 and menu activation.
bool WindowKeyDown(Window* win, int key, bool shift, bool ctrl, bool alt,
                   bool platform = false, bool function = false);
// The release of one. Only Enter and Space do anything with it — they make
// the click on a focused element from the release, the way a mouse click is
// made from the button coming back up — so a platform that has no key-up to
// give loses nothing else.
void WindowKeyUp(Window* win, int key, bool shift, bool ctrl, bool alt,
                 bool platform = false, bool function = false);
// A typed character, already decoded to a codepoint.
void WindowChar(Window* win, uint32_t ch, bool ctrl, bool alt);
// GPUI's Window::dispatch_event: every mouse event the platform produces
// arrives here, and everything it then does — hit-testing, focus, listener
// dispatch — is shared. Each press comes through whatever its click count: a
// double click is two presses, the way GPUI delivers two MouseDownEvents with
// click_count 1 then 2.
void WindowDispatchInput(Window* win, const PlatformInput* input);

// The tuple-variant constructors, which Rust writes as
// PlatformInput::MouseDown(MouseDownEvent { .. }). A platform builds one and
// hands it straight to WindowDispatchInput.
PlatformInput InputMouseDown(MouseButton button, float x, float y,
                             Modifiers modifiers, int clickCount,
                             bool firstMouse);
PlatformInput InputMouseUp(MouseButton button, float x, float y,
                           Modifiers modifiers, int clickCount);
PlatformInput InputMouseMove(float x, float y, bool pressed,
                             MouseButton pressedButton, Modifiers modifiers);
PlatformInput InputMouseExited(float x, float y, bool pressed,
                               MouseButton pressedButton, Modifiers modifiers);
PlatformInput InputScrollWheel(float x, float y, float deltaX, float deltaY,
                               bool precise, Modifiers modifiers,
                               TouchPhase phase);

// Count this press against the run before it and answer 1, 2, 3… Called once
// per press, before the platform decides what the press means — the title bar
// has to tell a drag from a zoom, and only the count separates them. Windows
// calls it for WM_LBUTTONDBLCLK too: that message replaces the second
// WM_LBUTTONDOWN of a run, and there is no message at all for the third.
int WindowClickCount(Window* win, float x, float y, MouseButton button);
// The count of the run in progress, for the release that ends it: a
// MouseUpEvent carries the click_count of the press it completes.
int WindowCurrentClickCount(Window* win);
// The timer fired: run whatever is due — the caret flip and any armed
// timers — repaint if that changed anything, and re-arm through PlatSetTimer
// for the next deadline. The platform never computes an interval itself.
void WindowTimerTick(Window* win);
// The window chrome under the cursor: ClickWinMin / Max / Close / Caption, or
// 0 for ordinary content. Windows answers WM_NCHITTEST with it; X11 uses it
// to route a press on the custom title bar.
int WindowChromeHit(Window* win, float x, float y);

// Milliseconds until the window next wants waking, or 0 if nothing does.
int WindowTimerMs(Window* win);

// The OS window went away. Frees the paint target and clears `plat`.
void WindowClosed(Window* win);
bool AppAnyWindowOpen(App* app);
// Allocate and register the Window, minus its OS half. WindowOpen fills in
// `plat` and shows it.
Window* WindowAlloc(App* app, WinOpts opts);
// Cap a requested window size at 85% of the display, the way Rust's
// create_new_window_with_size does. Each WindowOpen calls it with the metrics
// its platform reports.
void WindowClampToDisplay(int* dipW, int* dipH, int screenW, int screenH);

// -gpui-inspector opens the inspector panel on the first frame, so a
// screenshot harness can reach a panel whose only other way in is a chord.
// -gpui-window=X,Y,W,H asks for a window at an exact place and size, in the
// coordinates the platform's own create call takes -- on Windows the outer
// window rect, the one GetWindowRect reports. cmd/shot.ts passes it so a
// window opens at the size it will be photographed at, rather than being moved
// there afterwards and laying out twice. False when the flag was not given.
// Applied on Windows; the other platforms accept the flag and ignore it.
bool WindowGeomRequested(int* x, int* y, int* w, int* h);
// Takes runtime-owned flags out of argv before the app parses it, so an
// example that reads argv[1] never sees one. Those include
// __layout_reuse=off|on on every platform, and on Windows also __paint,
// __msaa and __scene. Returns the argument count that is left.
int GpuiTakeRuntimeArgs(int argc, char** argv);

// ─── implemented per platform ────────────────────────────────────────────

// Process-wide setup: DPI awareness and the window class on Windows, the X11
// display on Linux. False aborts AppNew.
bool PlatInit(App* app);
void PlatShutdown(App* app);
// Restart the window's repeating timer at `ms`; 0 stops it.
void PlatSetTimer(Window* win, int ms);
// Get the event loop to come back and drain the main-thread queue
// (sys/executor.h). Called from any thread, including a worker, so each
// platform picks the one wake its loop can take from outside: a posted
// message on Windows, a byte down a pipe on X11, a block on the main dispatch
// queue on macOS. AppNew installs it with ExecSetWake and nothing else calls
// it.
void PlatWake(App* app);
// Ask the OS for a pointer shape. Only called when it changes.
void PlatSetCursor(Window* win, CursorKind kind);
// GPUI holds the pointer for the length of a press, so a drag that leaves the
// window still delivers its moves and, above all, its release. Windows has
// SetCapture, X11 an active pointer grab; Cocoa already sends dragged and up
// to the window the press went to, so its half has nothing to do. Called with
// true as a press is taken and false as it is let go, never nested.
void PlatSetMouseCapture(Window* win, bool capture);
// The longest pause the OS allows inside one multi-click run, in ms:
// GetDoubleClickTime() on Windows, [NSEvent doubleClickInterval] on macOS. X11
// has no such setting, so Linux answers with the 400 ms every toolkit picks.
int PlatDoubleClickMs();

// `Window::display()`: the display the window is on, as the platform names
// it — GPUI's `DisplayId`, which is the HMONITOR on Windows and the
// CGDirectDisplayID on macOS. The number changes when the window moves to
// another monitor and means nothing else; 0 is a platform with no answer.
uint64_t PlatWindowDisplay(Window* win);
// crates/fps/src/refresh.rs: the period between refreshes of that display, in
// seconds, when the platform reports one. Asked of the platform rather than
// inferred from the frames a window presented: those gaps are whole multiples
// of the panel's period, so they bound it from below and never from above —
// 41.7ms is six refreshes at 144Hz and one at 24Hz, and nothing in the timing
// says which. 0 means nobody could say — a platform without a query here, a
// virtual display, or a panel with no fixed rate — and the caller shows an
// uncapped reading rather than one held to a guess. EnumDisplaySettingsW on
// Windows, CGDisplayCopyDisplayMode on macOS; upstream's third query is
// Wayland's and X11 has none, so Linux and the browser answer 0.
double PlatDisplayRefreshPeriod(uint64_t display);

// GPUI's `Window::window_handle()`, which Rust answers as a
// `raw_window_handle::RawWindowHandle`. One thing needs it: a webview is an
// OS control parented into the window it sits in, so `src/webview` has to
// name the window the way the OS does. The HWND on Windows, the X11 Window
// as an integer on Linux, the NSView* on macOS, null in the browser.
void* PlatWindowHandle(Window* win);

// crates/base/src/macos_accessibility.rs. VoiceOver hit-tests the window, and
// AppKit's NSWindow answers for itself rather than asking the view that drew
// everything, so nothing inside is reachable. This adds an
// accessibilityHitTest: install the native adapter that publishes the
// portable semantic tree. Windows answers WM_GETOBJECT, macOS publishes
// NSAccessibility elements, and Linux serves AT-SPI over its private D-Bus.
// wasm is the intentional no-op: a canvas has no native accessibility API.
// C++ portable spelling of Base's public
// `install_window_hit_test_forwarder`; the macOS implementation is the source
// operation and the other platform definitions are intentional no-ops.
void PlatInstallAccessibilityHitTest(Window* win);
// The portable tree is live data; native accessibility clients need change
// notifications beside their queries. The focus id is GPUI's internal id,
// which the adapter resolves back to the semantic node that exposes it.
void PlatAccessibilityTreeChanged(Window* win);
void PlatAccessibilityFocusChanged(Window* win, int focusId);

// ─── OS popup menus (crates/ui/src/native_menu) ──────────────────────────

// One row of a menu the OS draws. A row with no label is a separator, and a
// row with rows under it opens onto them rather than reporting anything
// itself.
struct PlatMenuItem {
    const char* label = nullptr;
    // What choosing this row reports. The caller numbers them; 0 is a row
    // that cannot be chosen at all.
    int id = 0;
    bool separator = false;
    bool disabled = false;
    bool checked = false;
    // The icon beside the label, as an asset path — Rust hands the platform
    // the icon's path too, and each backend turns it into whatever kind of
    // bitmap its menus take.
    const char* iconPath = nullptr;
    // Or as SVG source (`Icon::data`), which wins over the path: an icon
    // embedded in the binary is rasterized from its bytes with no asset
    // lookup.
    const char* iconSvg = nullptr;
    int iconSvgLen = 0;
    const PlatMenuItem* submenu = nullptr;
    int submenuN = 0;
    // The chord the row fires on, for the menus the OS keeps open across
    // keystrokes: a menu bar's rows work whether their menu is showing or
    // not, so the shortcut is the OS's to match rather than a hint drawn
    // beside the label. `key` is the key's name as a binding spells it — "s",
    // "z", "left" — and null is a row with no shortcut, which is every row of
    // a popup menu: those are chosen with the pointer, and the keystroke that
    // would have reached the same action never goes through the menu at all.
    const char* key = nullptr;
    Modifiers keyMods = {};
};

// Whether this platform has a popup menu of its own. False sends the caller
// to a drawn menu instead, which is Rust's FallbackMenuOverlay.
bool PlatHasMenu();
// Show `items` at (x, y) in the window's client area, in logical pixels, and
// answer the id of the row that was chosen — 0 for a menu dismissed without
// choosing one. The OS runs the tracking loop, so this returns once the menu
// is gone. `dark` asks for the dark rendering where the OS can be told.
int PlatShowMenu(Window* win, const PlatMenuItem* items, int n, float x,
                 float y, bool dark);

// ─── the application menu bar (App::set_menus) ───────────────────────────

// Whether this platform draws the application's menus itself. macOS does:
// the front application's menus live at the top of the screen and not in any
// of its windows, so a Mac application that draws its own has two copies of
// them. Nothing else here does — Windows and X11 leave a menu bar to the
// application, which is what component::AppMenuBar draws, and a browser tab
// has nowhere to put one.
bool PlatHasAppMenu();

// Install `items` as the application's menu bar, replacing whatever was
// there. Every top-level row is a submenu, because a menu bar has no bare
// rows, and the first one is the application menu — macOS titles that one
// after the process whatever it is called here, and adds its own rows to it.
// The rows are numbered the way PlatShowMenu's are, and a row that is chosen
// is reported by AppMenuChosen(id) rather than returned: the OS holds the
// menu bar for the life of the process and reports a choice whenever one is
// made, on the main thread.
//
// `items` and everything they point at belong to the caller and are not kept
// past the call; a platform that needs the labels later copies them, which
// AppKit does for itself.
void PlatSetAppMenu(App* app, const PlatMenuItem* items, int n);

// The platform reporting which row of the application menu bar was chosen.
// Implemented in WindowCommon.cpp: the id is looked up in the table the last
// AppSetMenus built and the row's action is dispatched to the front window.
void AppMenuChosen(int id);

} // namespace gpui
#endif // GPUI_GPUI_PLATFORM_H_
