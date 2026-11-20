/* The Win32 window: message loop, chrome hit-testing, timers, clipboard, and
   the process entry point. Everything it decides is delegated to
   WindowCommon.cpp. */

#include "gpui/platform.h"
#include "base/input.h"
#include "gpui/paint.h"
#include "gpui/accessibility_win.h"
#include "sys/clock.h"
#include "sys/executor.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <imm.h>
#include <ole2.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <uiautomation.h>
#include <wincodec.h>
#include <math.h>

namespace gpui {

static const wchar_t* kWndClass = L"GpuiSystemMonitor";

struct PlatWindow {
    HWND hwnd = nullptr;
    // RegisterDragDrop holds the drop target until RevokeDragDrop.
    bool fileDrop = false;
    // Installed when component::Root first renders (with a WM_GETOBJECT
    // fallback for a custom root). Nodes retain it, and close detaches the
    // Window before the last COM reference can disappear.
    WinAccessibility* accessibility = nullptr;
    // An automation client asked this window for its tree (WM_GETOBJECT), so
    // frames collect it from then on even before UiaClientsAreListening.
    bool accessibilityAsked = false;
    // WM_SETCURSOR fires on every move over the client area and has to put it
    // back, so the window remembers what it last asked for.
    HCURSOR cursor = nullptr;
    // WM_MOUSEACTIVATE said this next press is the one that activated the
    // window: MouseDownEvent::first_mouse.
    bool firstMouse = false;
    // A finger on the glass (WM_POINTER, PT_TOUCH). Windows also turns it into
    // mouse messages, which keep taps, press-and-hold and drags of scrollbars
    // and sliders working; a vertical drag over content scrolls it instead.
    // Only the primary finger is followed; a second one makes it a gesture.
    bool touchDown = false;
    bool touchScroll = false;
    float touchStartX = 0;
    float touchStartY = 0;
    float touchLastY = 0;
    // WM_GESTURE: the zoom in progress, and where the pan was last.
    TouchZoom zoom = {};
    float panX = 0;
    float panY = 0;
    LONG_PTR windowedStyle = 0;
    WINDOWPLACEMENT windowedPlacement = {sizeof(WINDOWPLACEMENT)};
    bool fullScreen = false;
    // AppWindowSetOpacity / AppWindowSetClickThrough: both are the layered
    // extended style, so each call applies the pair.
    int opacity = 100;
    bool clickThrough = false;
};

static HWND Hwnd(Window* win) {
    return (win && win->plat) ? win->plat->hwnd : nullptr;
}

// --- input method ---------------------------------------------------------
//
// GPUI's Windows platform answers the IMM32 messages against whatever the
// window's focused EntityInputHandler is; the same handler here is
// `win->input`. The system's own inline composition window is turned off
// (WM_IME_SETCONTEXT), because the marked text is drawn in the field with the
// rest of the document, underlined — which is what makes the candidate land
// in the right font, at the right place, scrolled with everything else.

// The composition or result string, decoded. Answers the byte length written,
// or -1 when there is nothing of that kind to read.
static int ImeStringUtf8(HIMC imc, DWORD which, char* out, int cap) {
    LONG bytes = ImmGetCompositionStringW(imc, which, nullptr, 0);
    if (bytes < 0) {
        return -1;
    }
    if (bytes == 0) {
        return 0;
    }
    int wlen = (int)((size_t)bytes / sizeof(wchar_t));
    // A composition long enough to overrun this is not one anybody is typing.
    wchar_t wbuf[512];
    if (wlen > (int)(sizeof(wbuf) / sizeof(wbuf[0]))) {
        wlen = (int)(sizeof(wbuf) / sizeof(wbuf[0]));
    }
    ImmGetCompositionStringW(imc, which, wbuf,
                             (DWORD)wlen * (DWORD)sizeof(wchar_t));
    int n =
        WideCharToMultiByte(CP_UTF8, 0, wbuf, wlen, out, cap, nullptr, nullptr);
    return n;
}

// Put the candidate list under the caret. Without this it lands at the
// window's origin, which on a tall field is nowhere near what is being typed.
static void ImeMoveCandidateWindow(Window* win, HIMC imc) {
    InputState* in = win->input;
    if (!in) {
        return;
    }
    int cursor = InputCursor(in);
    Bounds caret;
    if (!InputImeCandidateBounds(in, win, {cursor, cursor}, &caret)) {
        return;
    }
    POINT pt = {DipToPx(&win->paint, caret.x),
                DipToPx(&win->paint, caret.Bottom())};
    COMPOSITIONFORM cf = {};
    cf.dwStyle = CFS_POINT;
    cf.ptCurrentPos = pt;
    ImmSetCompositionWindow(imc, &cf);
    CANDIDATEFORM cand = {};
    cand.dwStyle = CFS_CANDIDATEPOS;
    cand.ptCurrentPos = pt;
    ImmSetCandidateWindow(imc, &cand);
}

// WM_IME_COMPOSITION. True when the field took it, so the default window
// procedure does not also draw a composition of its own.
static bool ImeComposition(Window* win, LPARAM lParam) {
    InputState* in = win->input;
    if (!in || !in->focused) {
        return false;
    }
    HIMC imc = ImmGetContext(Hwnd(win));
    if (!imc) {
        return false;
    }
    TempStr buf = AllocStrTemp(1023);
    if (lParam & GCS_RESULTSTR) {
        int n = ImeStringUtf8(imc, GCS_RESULTSTR, buf.s, len(buf) + 1);
        if (n > 0) {
            // The commit replaces the marked run, which replace_text_in_range
            // does for a null range, and clears the mark with it.
            InputReplaceTextInRange(in, win->app, win, nullptr, Str(buf.s, n));
        } else {
            InputUnmarkText(in, win->app, win);
        }
    }
    if (lParam & GCS_COMPSTR) {
        int n = ImeStringUtf8(imc, GCS_COMPSTR, buf.s, len(buf) + 1);
        if (n >= 0) {
            Str text = Str(buf.s, n);
            LONG caret =
                ImmGetCompositionStringW(imc, GCS_CURSORPOS, nullptr, 0);
            Selection sel = {};
            bool hasSel = caret >= 0;
            if (hasSel) {
                int off = Utf16OffsetToUtf8(text, (int)caret);
                sel = Selection{off, off};
            }
            InputReplaceAndMarkText(in, win->app, win, nullptr, text,
                                    hasSel ? &sel : nullptr);
        }
    }
    ImeMoveCandidateWindow(win, imc);
    ImmReleaseContext(Hwnd(win), imc);
    AppInvalidate(win);
    return true;
}

double TimeNow() {
    double simulated = 0;
    if (TestPlatformNow(&simulated)) {
        return simulated;
    }
    return ClockMonotonicSecondsSinceStart();
}

// user32!GetDpiForWindow is Windows 10 1607 and later, so it has to be
// resolved at runtime. HostDpi runs on every mouse move, so resolve it once
// and keep the answer.
typedef UINT(WINAPI* GetDpiForWindowFn)(HWND);

// The "looked it up and this Windows does not have it" sentinel. An address
// of 1 is not a possible GetProcAddress result, so one pointer carries all
// three states: null is not looked up yet, kNoDpiFn is missing, anything
// else is the function.
static GetDpiForWindowFn kNoDpiFn = (GetDpiForWindowFn)1;

typedef UINT(WINAPI* GetDpiForSystemFn)();

// GPUI_DPI=<dpi> draws every window at that dpi whatever its monitor says, so
// high-DPI layout can be checked on a 100% display.
static float DpiOverride() {
    static float dpi = -1;
    if (dpi < 0) {
        const char* env = getenv("GPUI_DPI");
        dpi = env ? (float)atoi(env) : 0;
        if (dpi < 48 || dpi > 960) {
            dpi = 0;
        }
    }
    return dpi;
}

// The dpi a window draws at.
static float DrawDpi(UINT monitorDpi) {
    float dpi = DpiOverride();
    if (dpi > 0) {
        return dpi;
    }
    return monitorDpi > 0 ? (float)monitorDpi : 96.f;
}

static float HostDpi(HWND hwnd) {
    static GetDpiForWindowFn getDpiForWindow = nullptr;
    if (!getDpiForWindow) {
        HMODULE user = GetModuleHandleW(L"user32.dll");
        if (user) {
            getDpiForWindow =
                (GetDpiForWindowFn)GetProcAddress(user, "GetDpiForWindow");
        }
        if (!getDpiForWindow) {
            getDpiForWindow = kNoDpiFn;
        }
    }
    return DrawDpi(getDpiForWindow != kNoDpiFn ? getDpiForWindow(hwnd) : 96);
}

// The primary monitor's dpi, for sizing a window before it exists.
static float SystemDpi() {
    HMODULE user = GetModuleHandleW(L"user32.dll");
    auto getDpiForSystem =
        user ? (GetDpiForSystemFn)GetProcAddress(user, "GetDpiForSystem")
             : nullptr;
    return DrawDpi(getDpiForSystem ? getDpiForSystem() : 96);
}

static void RenderFrame(Window* win) {
    HWND hwnd = Hwnd(win);
    if (!hwnd || IsIconic(hwnd)) {
        return;
    }
    RECT rc = {};
    GetClientRect(hwnd, &rc);
    win->paint.dpi = HostDpi(hwnd);
    WINDOWPLACEMENT wp = {sizeof(wp)};
    GetWindowPlacement(hwnd, &wp);
    win->maximized = wp.showCmd == SW_SHOWMAXIMIZED;
    int pxW = rc.right - rc.left;
    int pxH = rc.bottom - rc.top;
    WindowDrawFrame(win, hwnd, pxW, pxH, PxToDip(&win->paint, pxW),
                    PxToDip(&win->paint, pxH));
}

static int BorderPx() {
    return GetSystemMetrics(SM_CXFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
}

static int BorderYPx() {
    return GetSystemMetrics(SM_CYFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
}

// A window whose client area reaches the top edge because the view draws the
// title bar: WinOpts::clientTitleBar, and the older borderless flag that means
// the same thing here.
static bool ClientDecorated(Window* win) {
    return win->opts.clientTitleBar || win->opts.borderless;
}

static bool ShiftDown() {
    return (GetKeyState(VK_SHIFT) & 0x8000) != 0;
}
static bool CtrlDown() {
    return (GetKeyState(VK_CONTROL) & 0x8000) != 0;
}
// The platform modifier here, which is what `cmd-` names on this platform.
// Nothing in the tree binds it; a chord that does can now say so.
static bool WinDown() {
    return (GetKeyState(VK_LWIN) & 0x8000) != 0 ||
           (GetKeyState(VK_RWIN) & 0x8000) != 0;
}

// The rest of GPUI's KeyDownEvent for one WM_KEYDOWN / WM_SYSKEYDOWN, as
// gpui_windows' handle_keydown_msg and process_key work it out: lParam bit 30
// is the auto-repeat, and ToUnicode (flag 4: leave the dead-key state alone)
// says whether the modifiers are part of the character — a dead key, or a
// Ctrl/Alt/Win chord that types something other than the bare key (AltGr).
// VK_PROCESSKEY is the key an IME took for its composition.
static KeyDownFlags WinKeyDownFlags(WPARAM wParam, LPARAM lParam) {
    KeyDownFlags f;
    f.held = (lParam & (1ll << 30)) != 0;
    f.imeInProgress = wParam == VK_PROCESSKEY;
    BYTE state[256] = {};
    if (!GetKeyboardState(state)) {
        return f;
    }
    UINT vk = (UINT)wParam;
    UINT scan = (UINT)((lParam >> 16) & 0xFFFF);
    WCHAR buf[8] = {};
    int n = ToUnicode(vk, scan, state, buf, 8, 0x4);
    if (n == 0) {
        return f;
    }
    if (n < 0) {
        f.preferCharacterInput = true;
        return f;
    }
    // key_char: a character that is not a control code.
    if (buf[0] < 0x20 || buf[0] == 0x7F) {
        return f;
    }
    bool ctrl = (state[VK_CONTROL] & 0x80) != 0;
    bool alt = (state[VK_MENU] & 0x80) != 0;
    bool win = (state[VK_LWIN] & 0x80) != 0 || (state[VK_RWIN] & 0x80) != 0;
    if (!ctrl && !alt && !win) {
        return f;
    }
    BYTE bare[256];
    memcpy(bare, state, sizeof(bare));
    const int mods[] = {VK_CONTROL, VK_LCONTROL, VK_RCONTROL, VK_MENU,
                        VK_LMENU,   VK_RMENU,    VK_LWIN,     VK_RWIN};
    for (int m : mods) {
        bare[m] = 0;
    }
    WCHAR bufBare[8] = {};
    int nBare = ToUnicode(vk, scan, bare, bufBare, 8, 0x4);
    int cmp = n < 0 ? -n : n;
    int cmpBare = nBare < 0 ? -nBare : nBare;
    f.preferCharacterInput =
        n != nBare || cmp != cmpBare ||
        memcmp(buf, bufBare, (size_t)cmp * sizeof(WCHAR)) != 0;
    return f;
}

static bool AltDown() {
    return (GetKeyState(VK_MENU) & 0x8000) != 0;
}

// GPUI's Modifiers. Windows reports no Fn key, so `function` stays false.
static Modifiers ModsNow() {
    Modifiers m;
    m.control = CtrlDown();
    m.alt = AltDown();
    m.shift = ShiftDown();
    m.platform = (GetKeyState(VK_LWIN) & 0x8000) != 0 ||
                 (GetKeyState(VK_RWIN) & 0x8000) != 0;
    return m;
}

// Rust's Option<MouseButton> on a move: the first button that is down, or
// none. The MK_* bits in wParam say the same thing, but only for the messages
// that carry them -- WM_NCMOUSEMOVE's wParam is a hit-test code -- so this
// asks the keyboard state, which every path can.
static bool PressedButton(MouseButton* out) {
    struct {
        int vk;
        MouseButton button;
    } kButtons[] = {
        {VK_LBUTTON, MouseButton::Left},
        {VK_RBUTTON, MouseButton::Right},
        {VK_MBUTTON, MouseButton::Middle},
        {VK_XBUTTON1, MouseButton::NavigateBack},
        {VK_XBUTTON2, MouseButton::NavigateForward},
    };
    for (const auto& b : kButtons) {
        if (GetKeyState(b.vk) & 0x8000) {
            *out = b.button;
            return true;
        }
    }
    return false;
}

// One press, whichever button it came from. WM_LBUTTONDBLCLK arrives here too:
// the class has CS_DBLCLKS, so that message replaces the second WM_LBUTTONDOWN
// of a run. It is still a press and still has to reach the element under it --
// Win32 only renamed the message. WindowClickCount is what numbers it, and
// what numbers the third press, which Win32 has no message for at all.
static void MouseDown(Window* win, MouseButton button, LPARAM lParam) {
    win->paint.dpi = HostDpi(Hwnd(win));
    float x = PxToDip(&win->paint, GET_X_LPARAM(lParam));
    float y = PxToDip(&win->paint, GET_Y_LPARAM(lParam));
    bool first = win->plat->firstMouse;
    win->plat->firstMouse = false;
    PlatformInput in = InputMouseDown(
        button, x, y, ModsNow(), WindowClickCount(win, x, y, button), first);
    WindowDispatchInput(win, &in);
}

static void MouseUp(Window* win, MouseButton button, LPARAM lParam) {
    float x = PxToDip(&win->paint, GET_X_LPARAM(lParam));
    float y = PxToDip(&win->paint, GET_Y_LPARAM(lParam));
    PlatformInput in =
        InputMouseUp(button, x, y, ModsNow(), WindowCurrentClickCount(win));
    WindowDispatchInput(win, &in);
}

static void MouseMove(Window* win, float x, float y) {
    MouseButton pressed = MouseButton::Left;
    bool any = PressedButton(&pressed);
    PlatformInput in = InputMouseMove(x, y, any, pressed, ModsNow());
    WindowDispatchInput(win, &in);
}

// GPUI_HOVER_HOLD: see WM_MOUSELEAVE. Read once, since getenv is not free and
// this is on the message path.
static bool HoverHoldForTests() {
    static int hold = -1;
    if (hold < 0) {
        const char* env = getenv("GPUI_HOVER_HOLD");
        hold = (env && env[0] && env[0] != '0') ? 1 : 0;
    }
    return hold != 0;
}

// --- touch -----------------------------------------------------------------

// How far a finger moves before it is a drag rather than a tap, in dips.
static const float kTouchSlopDips = 10.f;

static bool IsTouchPointer(WPARAM wParam) {
    POINTER_INPUT_TYPE type = PT_POINTER;
    return GetPointerType(GET_POINTERID_WPARAM(wParam), &type) &&
           type == PT_TOUCH;
}

// The mouse message being handled is one Windows made from a touch
// (MI_WP_SIGNATURE in its extra info, with the touch bit).
static bool IsMouseFromTouch() {
    return ((ULONG_PTR)GetMessageExtraInfo() & 0xFFFFFF80) == 0xFF515780;
}

static void TouchEmitScroll(Window* win, TouchPhase phase, float dy) {
    PlatWindow* p = win->plat;
    PlatformInput in = InputScrollWheel(p->touchStartX, p->touchStartY, 0.f, dy,
                                        true, {}, phase);
    WindowDispatchInput(win, &in);
}

// A screen point (a pointer or gesture message) in window dips.
static Point ScreenToDips(Window* win, int sx, int sy) {
    PlatWindow* p = win->plat;
    POINT pt = {sx, sy};
    ScreenToClient(p->hwnd, &pt);
    win->paint.dpi = HostDpi(p->hwnd);
    return {PxToDip(&win->paint, pt.x), PxToDip(&win->paint, pt.y)};
}

// WM_POINTERDOWN / UPDATE / UP of a finger. The messages still go on to
// DefWindowProc, which makes the mouse messages (and the gestures) out of
// them.
static void TouchPointer(Window* win, UINT msg, WPARAM wParam, LPARAM lParam) {
    PlatWindow* p = win->plat;
    Point at = ScreenToDips(win, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
    float x = at.x;
    float y = at.y;
    if (!IS_POINTER_PRIMARY_WPARAM(wParam)) {
        // a second finger: a pinch or a two-finger pan, WM_GESTURE's
        if (msg == WM_POINTERDOWN && p->touchDown) {
            if (p->touchScroll) {
                TouchEmitScroll(win, TouchPhase::Ended, 0.f);
            }
            p->touchDown = false;
            p->touchScroll = false;
        }
        return;
    }
    if (msg == WM_POINTERDOWN) {
        // an element that takes gestures gets every one, the pan included,
        // as WM_GESTURE (GestureNotify)
        p->touchDown = !WindowWantsGestures(win, x, y);
        p->touchScroll = false;
        p->touchStartX = x;
        p->touchStartY = y;
        p->touchLastY = y;
        return;
    }
    if (!p->touchDown) {
        return;
    }
    if (msg == WM_POINTERUP) {
        if (p->touchScroll) {
            TouchEmitScroll(win, TouchPhase::Ended, 0.f);
        }
        p->touchDown = false;
        return;
    }
    if (!p->touchScroll) {
        float dx = x - p->touchStartX;
        float dy = y - p->touchStartY;
        if (dx * dx + dy * dy < kTouchSlopDips * kTouchSlopDips) {
            return;
        }
        // a sideways drag, or one on a scrollbar, stays the mouse drag it is
        if (fabsf(dx) >= fabsf(dy) || win->scrollDragId != 0) {
            p->touchDown = false;
            return;
        }
        p->touchScroll = true;
        // the press Windows already made must not end in a click or a drag
        if (win->mouseDown) {
            PlatformInput in =
                InputMouseUp(win->pressedButton, -1e6f, -1e6f, ModsNow(),
                             WindowCurrentClickCount(win));
            WindowDispatchInput(win, &in);
        }
        TouchEmitScroll(win, TouchPhase::Started, y - p->touchStartY);
    } else {
        TouchEmitScroll(win, TouchPhase::Moved, y - p->touchLastY);
    }
    p->touchLastY = y;
}

// Mouse messages made from a finger that is scrolling.
static bool IsTouchScrollMouse(Window* win) {
    return win->plat->touchScroll && IsMouseFromTouch();
}

// WM_GESTURENOTIFY, as a finger first lands. A touch on an element that takes
// gestures gets all of them, a one-finger pan either way and rotation
// included. Anywhere else Windows' defaults apply, minus the one-finger pan:
// a vertical drag scrolls through TouchPointer and a sideways one stays a
// mouse drag. Set every time, since the last touch may have changed it.
static void GestureNotify(Window* win, LPARAM lParam) {
    auto* gn = (GESTURENOTIFYSTRUCT*)lParam;
    HWND hwnd = win->plat->hwnd;
    Point at = ScreenToDips(win, gn->ptsLocation.x, gn->ptsLocation.y);
    if (WindowWantsGestures(win, at.x, at.y)) {
        GESTURECONFIG all = {0, GC_ALLGESTURES, 0};
        SetGestureConfig(hwnd, 0, 1, &all, sizeof(GESTURECONFIG));
        return;
    }
    DWORD panWant = GC_PAN | GC_PAN_WITH_GUTTER | GC_PAN_WITH_INERTIA;
    DWORD panBlock = GC_PAN_WITH_SINGLE_FINGER_VERTICALLY |
                     GC_PAN_WITH_SINGLE_FINGER_HORIZONTALLY;
    GESTURECONFIG gcs[] = {
        {GID_ZOOM, GC_ZOOM, 0},
        {GID_PAN, panWant, panBlock},
        {GID_ROTATE, 0, GC_ROTATE},
        {GID_TWOFINGERTAP, GC_TWOFINGERTAP, 0},
        {GID_PRESSANDTAP, GC_PRESSANDTAP, 0},
    };
    SetGestureConfig(hwnd, 0, sizeof(gcs) / sizeof(gcs[0]), gcs,
                     sizeof(GESTURECONFIG));
}

// GF_BEGIN / GF_END. A tap is one message with both: it is over.
static TouchPhase GesturePhase(DWORD flags) {
    if (flags & GF_END) {
        return TouchPhase::Ended;
    }
    return (flags & GF_BEGIN) ? TouchPhase::Started : TouchPhase::Moved;
}

static void DispatchTouchPinch(Window* win, Point at, float delta,
                               TouchPhase phase) {
    PlatformInput in = InputPinch(at.x, at.y, delta, ModsNow(), phase);
    WindowDispatchInput(win, &in);
}

// GID_ZOOM: the distance between the fingers, as PinchEvent deltas. A zoom
// whose begin went elsewhere starts here.
static void TouchZoomPinch(Window* win, float distance, TouchPhase phase,
                           Point at) {
    TouchZoom* z = &win->plat->zoom;
    bool ending = phase == TouchPhase::Ended || phase == TouchPhase::Cancelled;
    if (phase == TouchPhase::Started || z->first <= 0) {
        TouchZoomDelta(z, distance, TouchPhase::Started);
        DispatchTouchPinch(win, at, 0, TouchPhase::Started);
        if (ending) {
            *z = {};
            DispatchTouchPinch(win, at, 0, phase);
        }
        return;
    }
    float delta = TouchZoomDelta(z, distance, phase);
    if (!ending) {
        DispatchTouchPinch(win, at, delta, TouchPhase::Moved);
        return;
    }
    if (delta != 0) {
        DispatchTouchPinch(win, at, delta, TouchPhase::Moved);
    }
    DispatchTouchPinch(win, at, 0, phase);
}

// WM_GESTURE. A zoom is a PinchEvent, everything else a GestureEvent. True
// when it was handled; GID_BEGIN and GID_END must reach DefWindowProc.
static bool TouchGesture(Window* win, LPARAM lParam) {
    HGESTUREINFO hgi = (HGESTUREINFO)lParam;
    GESTUREINFO gi = {};
    gi.cbSize = sizeof(GESTUREINFO);
    if (!GetGestureInfo(hgi, &gi)) {
        return false;
    }
    PlatWindow* p = win->plat;
    Point at = ScreenToDips(win, gi.ptsLocation.x, gi.ptsLocation.y);
    if (gi.dwID == GID_BEGIN) {
        WindowGestureBegin(win, at.x, at.y);
        return false;
    }
    if (gi.dwID == GID_END) {
        WindowGestureEnd(win);
        return false;
    }
    uint32_t arg = (uint32_t)gi.ullArguments;
    GestureEvent ev = {};
    ev.phase = GesturePhase(gi.dwFlags);
    ev.x = at.x;
    ev.y = at.y;
    ev.modifiers = ModsNow();
    switch (gi.dwID) {
        case GID_ZOOM:
            TouchZoomPinch(win, (float)arg, ev.phase, at);
            CloseGestureInfoHandle(hgi);
            return true;
        case GID_PAN:
            ev.kind = GestureKind::Pan;
            if (ev.phase != TouchPhase::Started) {
                ev.dx = at.x - p->panX;
                ev.dy = at.y - p->panY;
            }
            p->panX = at.x;
            p->panY = at.y;
            ev.inertia = (gi.dwFlags & GF_INERTIA) != 0;
            break;
        case GID_ROTATE:
            // the begin carries the fingers' angle, the rest the turn since
            ev.kind = GestureKind::Rotate;
            if (ev.phase != TouchPhase::Started) {
                ev.angle = (float)GID_ROTATE_ANGLE_FROM_ARGUMENT(arg);
            }
            break;
        case GID_TWOFINGERTAP:
            ev.kind = GestureKind::TwoFingerTap;
            ev.phase = TouchPhase::Ended;
            break;
        case GID_PRESSANDTAP:
            ev.kind = GestureKind::PressAndTap;
            break;
        default:
            return false;
    }
    PlatformInput in = InputGesture(ev);
    WindowDispatchInput(win, &in);
    CloseGestureInfoHandle(hgi);
    return true;
}

static void MouseExited(Window* win) {
    MouseButton pressed = MouseButton::Left;
    bool any = PressedButton(&pressed);
    PlatformInput in =
        InputMouseExited(win->mouseX, win->mouseY, any, pressed, ModsNow());
    WindowDispatchInput(win, &in);
}

// gpui_windows/util.rs `system_appearance`. The caption bar is drawn by the
// OS, so it follows the OS's setting rather than the theme the application
// paints with: Rust puts a light caption over a dark client area when Windows
// is in light mode, and so does this.
//
// Rust asks WinRT -- `UISettings::GetColorValue(Foreground)`, dark when the
// text colour is light. There is no WinRT here, so this reads the value that
// setting writes. It is the other route the page Rust cites gives
// (learn.microsoft.com/windows/apps/desktop/modernize/apply-windows-themes):
// AppsUseLightTheme under Themes\Personalize, which is the *apps* setting,
// the one UISettings' foreground colour reflects, and not SystemUsesLightTheme
// next to it, which is the taskbar's. A missing value means light, which is
// what Windows shows before anyone has touched the setting.
static bool SystemIsDarkMode() {
    DWORD light = 1;
    DWORD size = sizeof(light);
    LSTATUS st = RegGetValueW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &size);
    return st == ERROR_SUCCESS && light == 0;
}

// util.rs `configure_dwm_dark_mode`, called where Rust calls it: once as the
// window is created, and again on every ImmersiveColorSet.
static void ConfigureDwmDarkMode(HWND hwnd) {
    BOOL dark = SystemIsDarkMode() ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark,
                          sizeof(dark));
}

static Str ClipboardReadPaths(Arena* a, HANDLE handle);

// The next line of newline-separated `rest`, which moves past it.
static Str NextLine(Str* rest) {
    int i = 0;
    while (i < len(*rest) && rest->s[i] != '\n') {
        i++;
    }
    Str line(rest->s, i);
    if (i > 0 && line.s[i - 1] == '\r') {
        line.len--;
    }
    int skip = i < len(*rest) ? i + 1 : i;
    *rest = Str(rest->s + skip, len(*rest) - skip);
    return line;
}

// A registered clipboard format's id, by name.
static CLIPFORMAT ClipFormat(const WCHAR* name) {
    return (CLIPFORMAT)RegisterClipboardFormatW(name);
}

// One HGLOBAL format of a dropped data object as UTF-8, from `a`; `wide`
// says whether it holds UTF-16 or ANSI. Empty when it is not there.
static Str DataObjectReadText(Arena* a, IDataObject* data, CLIPFORMAT cf,
                              bool wide) {
    FORMATETC fmt = {cf, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM medium = {};
    if (!cf || FAILED(data->GetData(&fmt, &medium))) {
        return {};
    }
    Str out = {};
    const void* p = medium.hGlobal ? GlobalLock(medium.hGlobal) : nullptr;
    SIZE_T size = p ? GlobalSize(medium.hGlobal) : 0;
    if (p && wide) {
        // Not trusted to be terminated: stop at the first NUL or the end.
        const auto* w = (const WCHAR*)p;
        int n = 0;
        int maxChars = (int)(size / sizeof(WCHAR));
        while (n < maxChars && w[n]) {
            n++;
        }
        int bytes =
            WideCharToMultiByte(CP_UTF8, 0, w, n, nullptr, 0, nullptr, nullptr);
        char* buf = bytes > 0 ? (char*)Alloc(a, bytes + 1) : nullptr;
        if (buf) {
            WideCharToMultiByte(CP_UTF8, 0, w, n, buf, bytes, nullptr, nullptr);
            buf[bytes] = 0;
            out = Str(buf, bytes);
        }
    } else if (p) {
        const auto* s = (const char*)p;
        int n = 0;
        while (n < (int)size && s[n]) {
            n++;
        }
        char* buf = n > 0 ? (char*)Alloc(a, n + 1) : nullptr;
        if (buf) {
            memcpy(buf, s, (size_t)n);
            buf[n] = 0;
            out = Str(buf, n);
        }
    }
    if (p) {
        GlobalUnlock(medium.hGlobal);
    }
    ReleaseStgMedium(&medium);
    return out;
}

// What an ExternalText drag carries: the text, and the link a browser puts
// beside it ("UniformResourceLocatorW", or the ANSI one).
static void DataObjectReadTextDrag(Arena* a, IDataObject* data, Str* text,
                                   Str* url) {
    *text = DataObjectReadText(a, data, CF_UNICODETEXT, true);
    if (len(*text) == 0) {
        *text = DataObjectReadText(a, data, CF_TEXT, false);
    }
    *url = DataObjectReadText(a, data, ClipFormat(L"UniformResourceLocatorW"),
                              true);
    if (len(*url) == 0) {
        *url = DataObjectReadText(a, data,
                                  ClipFormat(L"UniformResourceLocator"), false);
    }
}

// IDropTarget for a drag onto the window. gpui-pre-windows does the same for
// files: CF_HDROP on enter, then pending / leave / drop as FileDropEvent.
// Not in Rust: text and links are an ExternalText drag, shown as refused
// unless an element under the pointer takes it.
struct FileDropTarget : ComObject<IDropTarget> {
    enum class Kind : uint8_t {
        None,
        Paths,
        Text
    };
    Window* win = nullptr;
    Kind kind = Kind::None;
    DWORD lastEffect = DROPEFFECT_NONE;

    explicit FileDropTarget(Window* w) : win(w) {}

    void At(POINTL screen, float* x, float* y) {
        POINT pt = {screen.x, screen.y};
        HWND hwnd = Hwnd(win);
        ScreenToClient(hwnd, &pt);
        win->paint.dpi = HostDpi(hwnd);
        *x = PxToDip(&win->paint, (int)pt.x);
        *y = PxToDip(&win->paint, (int)pt.y);
    }

    // Files are always offered a copy, as in Rust; text only over a taker.
    DWORD Effect() {
        if (kind == Kind::Paths) {
            return DROPEFFECT_COPY;
        }
        bool taken =
            kind == Kind::Text && win && win->fileDrop && win->dragOverId != 0;
        return taken ? DROPEFFECT_COPY : DROPEFFECT_NONE;
    }

    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* data, DWORD, POINTL pt,
                                        DWORD* effect) override {
        kind = Kind::None;
        lastEffect = DROPEFFECT_NONE;
        if (effect) {
            *effect = DROPEFFECT_NONE;
        }
        if (!win || !data) {
            return S_OK;
        }
        Arena* a = ArenaNew();
        float x = 0, y = 0;
        At(pt, &x, &y);
        FORMATETC fmt = {CF_HDROP, nullptr, DVASPECT_CONTENT, -1,
                         TYMED_HGLOBAL};
        STGMEDIUM medium = {};
        if (data->QueryGetData(&fmt) == S_OK &&
            SUCCEEDED(data->GetData(&fmt, &medium))) {
            Str paths = ClipboardReadPaths(a, medium.hGlobal);
            ReleaseStgMedium(&medium);
            PlatformInput in =
                InputFileDrop(FileDropPhase::Entered, x, y, paths);
            kind = Kind::Paths;
            WindowDispatchInput(win, &in);
        } else {
            Str text = {}, url = {};
            DataObjectReadTextDrag(a, data, &text, &url);
            if (len(text) > 0 || len(url) > 0) {
                PlatformInput in =
                    InputTextDrop(FileDropPhase::Entered, x, y, text, url);
                kind = Kind::Text;
                WindowDispatchInput(win, &in);
            }
        }
        ArenaDelete(a);
        lastEffect = Effect();
        if (effect) {
            *effect = lastEffect;
        }
        return S_OK;
    }
    // A drag moving over the window, or let go on it: the payload came with
    // DragEnter, so only the position goes with the phase.
    HRESULT DispatchAt(FileDropPhase phase, POINTL pt, DWORD* effect) {
        if (effect) {
            *effect = DROPEFFECT_NONE;
        }
        if (!win || kind == Kind::None) {
            return S_OK;
        }
        // The drop lands where the last move said it could.
        DWORD dropEffect = lastEffect;
        float x = 0, y = 0;
        At(pt, &x, &y);
        PlatformInput in = InputFileDrop(phase, x, y, {});
        WindowDispatchInput(win, &in);
        if (phase == FileDropPhase::Submit) {
            kind = Kind::None;
        } else {
            lastEffect = Effect();
            dropEffect = lastEffect;
        }
        if (effect) {
            *effect = dropEffect;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DragOver(DWORD, POINTL pt,
                                       DWORD* effect) override {
        return DispatchAt(FileDropPhase::Pending, pt, effect);
    }
    HRESULT STDMETHODCALLTYPE DragLeave() override {
        if (win && kind != Kind::None) {
            PlatformInput in = InputFileDrop(FileDropPhase::Exited, win->mouseX,
                                             win->mouseY, {});
            WindowDispatchInput(win, &in);
        }
        kind = Kind::None;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Drop(IDataObject*, DWORD, POINTL pt,
                                   DWORD* effect) override {
        return DispatchAt(FileDropPhase::Submit, pt, effect);
    }
};

// ─── drag out (no Rust twin) ─────────────────────────────────────────────────

static HGLOBAL GlobalFromBytes(const void* p, size_t n) {
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, n > 0 ? n : 1);
    void* dst = h ? GlobalLock(h) : nullptr;
    if (!dst) {
        if (h) {
            GlobalFree(h);
        }
        return nullptr;
    }
    if (n > 0) {
        memcpy(dst, p, n);
    }
    GlobalUnlock(h);
    return h;
}

// UTF-8 into `dst` as UTF-16 with its NUL; returns the chars written,
// NUL included. `dst` null: how many it takes.
static int WideInto(Str s, WCHAR* dst, int room) {
    int n = len(s) > 0 ? MultiByteToWideChar(CP_UTF8, 0, s.s, len(s), dst,
                                             dst ? room - 1 : 0)
                       : 0;
    if (dst) {
        dst[n] = 0;
    }
    return n + 1;
}

static HGLOBAL GlobalFromText(Str text) {
    int n = WideInto(text, nullptr, 0);
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (size_t)n * sizeof(WCHAR));
    auto* w = h ? (WCHAR*)GlobalLock(h) : nullptr;
    if (!w) {
        if (h) {
            GlobalFree(h);
        }
        return nullptr;
    }
    WideInto(text, w, n);
    GlobalUnlock(h);
    return h;
}

// CF_HDROP: DROPFILES, then each path and its NUL, then one more NUL.
static HGLOBAL GlobalFromPaths(Str paths) {
    int chars = 1;
    for (Str rest = paths; len(rest) > 0;) {
        Str line = NextLine(&rest);
        if (len(line) > 0) {
            chars += WideInto(line, nullptr, 0);
        }
    }
    size_t size = sizeof(DROPFILES) + (size_t)chars * sizeof(WCHAR);
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, size);
    auto* df = h ? (DROPFILES*)GlobalLock(h) : nullptr;
    if (!df) {
        if (h) {
            GlobalFree(h);
        }
        return nullptr;
    }
    df->pFiles = sizeof(DROPFILES);
    df->fWide = TRUE;
    auto* w = (WCHAR*)(df + 1);
    int at = 0;
    for (Str rest = paths; len(rest) > 0;) {
        Str line = NextLine(&rest);
        if (len(line) > 0) {
            at += WideInto(line, w + at, chars - at);
        }
    }
    w[at] = 0;
    GlobalUnlock(h);
    return h;
}

// CFSTR_FILEDESCRIPTORW for the one virtual file the image becomes.
static HGLOBAL GlobalFromFileDescriptor(Str name, int size) {
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT,
                            sizeof(FILEGROUPDESCRIPTORW));
    auto* fgd = h ? (FILEGROUPDESCRIPTORW*)GlobalLock(h) : nullptr;
    if (!fgd) {
        if (h) {
            GlobalFree(h);
        }
        return nullptr;
    }
    fgd->cItems = 1;
    fgd->fgd[0].dwFlags = FD_FILESIZE | FD_ATTRIBUTES;
    fgd->fgd[0].dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
    fgd->fgd[0].nFileSizeLow = (DWORD)size;
    WideInto(name, fgd->fgd[0].cFileName, MAX_PATH);
    GlobalUnlock(h);
    return h;
}

struct DragOutFormatEnum : ComObject<IEnumFORMATETC> {
    static const int kMax = 6;
    FORMATETC fmts[kMax] = {};
    ULONG n = 0;
    ULONG at = 0;

    HRESULT STDMETHODCALLTYPE Next(ULONG want, FORMATETC* out,
                                   ULONG* got) override {
        if (!out) {
            return E_POINTER;
        }
        ULONG k = 0;
        while (k < want && at < n) {
            out[k++] = fmts[at++];
        }
        if (got) {
            *got = k;
        }
        return k == want ? S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Skip(ULONG k) override {
        at = std::min(n, at + k);
        return at < n ? S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Reset() override {
        at = 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Clone(IEnumFORMATETC** out) override {
        if (!out) {
            return E_POINTER;
        }
        auto* e = new DragOutFormatEnum();
        memcpy(e->fmts, fmts, sizeof(fmts));
        e->n = n;
        e->at = at;
        *out = e;
        return S_OK;
    }
};

// The data a drag out hands DoDragDrop. It owns copies: a target in another
// process may read it after the drag is over.
struct DragOutDataObject : ComObject<IDataObject> {
    Str text = {};
    Str paths = {};
    Str imageName = {};
    uint8_t* image = nullptr;
    int imageLen = 0;
    CLIPFORMAT cfDescriptor = 0;
    CLIPFORMAT cfContents = 0;
    CLIPFORMAT cfDropEffect = 0;
    FORMATETC fmts[DragOutFormatEnum::kMax] = {};
    ULONG n = 0;

    ~DragOutDataObject() override {
        StrFree(text);
        StrFree(paths);
        StrFree(imageName);
        delete[] image;
    }

    void Add(CLIPFORMAT cf, LONG lindex, DWORD tymed) {
        fmts[n++] = {cf, nullptr, DVASPECT_CONTENT, lindex, tymed};
    }

    const FORMATETC* Find(const FORMATETC* want) const {
        for (ULONG i = 0; want && i < n; i++) {
            const FORMATETC& f = fmts[i];
            if (want->cfFormat != f.cfFormat || !(want->tymed & f.tymed)) {
                continue;
            }
            if (f.lindex >= 0 && want->lindex != f.lindex) {
                continue;
            }
            return &f;
        }
        return nullptr;
    }

    HRESULT STDMETHODCALLTYPE GetData(FORMATETC* want,
                                      STGMEDIUM* out) override {
        if (!want || !out) {
            return E_POINTER;
        }
        const FORMATETC* f = Find(want);
        if (!f) {
            return DV_E_FORMATETC;
        }
        *out = {};
        CLIPFORMAT cf = f->cfFormat;
        if (cf == cfContents && (want->tymed & TYMED_ISTREAM)) {
            IStream* stream = nullptr;
            if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream))) {
                return E_OUTOFMEMORY;
            }
            stream->Write(image, (ULONG)imageLen, nullptr);
            LARGE_INTEGER zero = {};
            stream->Seek(zero, STREAM_SEEK_SET, nullptr);
            out->tymed = TYMED_ISTREAM;
            out->pstm = stream;
            return S_OK;
        }
        HGLOBAL h = nullptr;
        DWORD copyEffect = DROPEFFECT_COPY;
        if (cf == CF_UNICODETEXT) {
            h = GlobalFromText(text);
        } else if (cf == CF_HDROP) {
            h = GlobalFromPaths(paths);
        } else if (cf == cfDescriptor) {
            h = GlobalFromFileDescriptor(imageName, imageLen);
        } else if (cf == cfContents) {
            h = GlobalFromBytes(image, (size_t)imageLen);
        } else if (cf == cfDropEffect) {
            h = GlobalFromBytes(&copyEffect, sizeof(copyEffect));
        }
        if (!h) {
            return E_OUTOFMEMORY;
        }
        out->tymed = TYMED_HGLOBAL;
        out->hGlobal = h;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*, STGMEDIUM*) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* want) override {
        return Find(want) ? S_OK : DV_E_FORMATETC;
    }
    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*,
                                                    FORMATETC* out) override {
        if (out) {
            out->ptd = nullptr;
        }
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE SetData(FORMATETC*, STGMEDIUM*, BOOL) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD dir,
                                            IEnumFORMATETC** out) override {
        if (!out) {
            return E_POINTER;
        }
        *out = nullptr;
        if (dir != DATADIR_GET) {
            return E_NOTIMPL;
        }
        auto* e = new DragOutFormatEnum();
        memcpy(e->fmts, fmts, sizeof(fmts));
        e->n = n;
        *out = e;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*, DWORD, IAdviseSink*,
                                      DWORD*) override {
        return OLE_E_ADVISENOTSUPPORTED;
    }
    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override {
        return OLE_E_ADVISENOTSUPPORTED;
    }
    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**) override {
        return OLE_E_ADVISENOTSUPPORTED;
    }
};

IDataObject* DragOutDataObjectNew(const DragOutData& data) {
    DragOutFormat formats[3];
    int nf = DragOutFormats(data, formats);
    if (nf == 0) {
        return nullptr;
    }
    auto* obj = new DragOutDataObject();
    for (int i = 0; i < nf; i++) {
        switch (formats[i]) {
            case DragOutFormat::Text:
                obj->text = StrDup(data.text);
                obj->Add(CF_UNICODETEXT, -1, TYMED_HGLOBAL);
                break;
            case DragOutFormat::Paths:
                obj->paths = StrDup(data.paths);
                obj->Add(CF_HDROP, -1, TYMED_HGLOBAL);
                break;
            case DragOutFormat::ImageFile:
                obj->imageName = StrDup(DragOutImageName(data));
                obj->imageLen = data.imageBytesLen;
                obj->image = new uint8_t[data.imageBytesLen];
                memcpy(obj->image, data.imageBytes, (size_t)data.imageBytesLen);
                obj->cfDescriptor = ClipFormat(CFSTR_FILEDESCRIPTORW);
                obj->cfContents = ClipFormat(CFSTR_FILECONTENTS);
                obj->Add(obj->cfDescriptor, -1, TYMED_HGLOBAL);
                obj->Add(obj->cfContents, 0, TYMED_ISTREAM | TYMED_HGLOBAL);
                break;
        }
    }
    // Files are copied, not moved, by Explorer.
    if (len(obj->paths) > 0 || obj->image) {
        obj->cfDropEffect = ClipFormat(CFSTR_PREFERREDDROPEFFECT);
        obj->Add(obj->cfDropEffect, -1, TYMED_HGLOBAL);
    }
    return obj;
}

// The drop source. With a preview it draws the image list under the
// pointer itself (IDragSourceHelper shows nothing for a data object that
// does not store its private formats), else OLE's cursors show.
struct DragOutSource : ComObject<IDropSource> {
    bool imageDrag = false;

    HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escape,
                                                DWORD keys) override {
        if (escape) {
            return DRAGDROP_S_CANCEL;
        }
        if (!(keys & MK_LBUTTON)) {
            return DRAGDROP_S_DROP;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD) override {
        if (!imageDrag) {
            return DRAGDROP_S_USEDEFAULTCURSORS;
        }
        POINT pt = {};
        GetCursorPos(&pt);
        ImageList_DragMove(pt.x, pt.y);
        return S_OK;
    }
};

// The preview as a one-image list, or null: the PNG decoded with WIC,
// scaled, on white with its border.
static HIMAGELIST DragOutPreviewList(const DragOutData& data, float scale,
                                     POINT* hot) {
    if (!data.imageBytes || data.imageBytesLen <= 0 ||
        data.previewMaxEdge <= 0) {
        return nullptr;
    }
    IWICImagingFactory* wic = nullptr;
    IWICStream* stream = nullptr;
    IWICBitmapDecoder* dec = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* conv = nullptr;
    IWICBitmapScaler* scaler = nullptr;
    HIMAGELIST list = nullptr;
    UINT w = 0, h = 0;
    DragOutPreview p;
    HRESULT hr =
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                         __uuidof(IWICImagingFactory), (void**)&wic);
    if (SUCCEEDED(hr)) {
        hr = wic->CreateStream(&stream);
    }
    if (SUCCEEDED(hr)) {
        hr = stream->InitializeFromMemory((BYTE*)data.imageBytes,
                                          (DWORD)data.imageBytesLen);
    }
    if (SUCCEEDED(hr)) {
        hr = wic->CreateDecoderFromStream(stream, nullptr,
                                          WICDecodeMetadataCacheOnDemand, &dec);
    }
    if (SUCCEEDED(hr)) {
        hr = dec->GetFrame(0, &frame);
    }
    if (SUCCEEDED(hr)) {
        hr = frame->GetSize(&w, &h);
    }
    if (SUCCEEDED(hr)) {
        p = DragOutPreviewSize(data, (int)w, (int)h, scale);
        hr = p.w > 0 ? wic->CreateFormatConverter(&conv) : E_FAIL;
    }
    if (SUCCEEDED(hr)) {
        hr = conv->Initialize(frame, GUID_WICPixelFormat32bppPBGRA,
                              WICBitmapDitherTypeNone, nullptr, 0,
                              WICBitmapPaletteTypeCustom);
    }
    if (SUCCEEDED(hr)) {
        hr = wic->CreateBitmapScaler(&scaler);
    }
    if (SUCCEEDED(hr)) {
        hr = scaler->Initialize(conv, (UINT)p.w, (UINT)p.h,
                                WICBitmapInterpolationModeHighQualityCubic);
    }
    void* bits = nullptr;
    HBITMAP bmp = nullptr;
    if (SUCCEEDED(hr)) {
        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = p.w;
        bmi.bmiHeader.biHeight = -p.h; // top-down
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        bmp =
            CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        hr = bmp && bits ? S_OK : E_OUTOFMEMORY;
    }
    UINT stride = (UINT)p.w * 4;
    if (SUCCEEDED(hr)) {
        hr = scaler
                 ->CopyPixels(nullptr, stride, stride * (UINT)p.h, (BYTE*)bits);
    }
    if (SUCCEEDED(hr)) {
        DragOutPreviewFrame((uint8_t*)bits, p.w, p.h, (int)stride);
        list = ImageList_Create(p.w, p.h, ILC_COLOR32, 1, 1);
        if (list && ImageList_Add(list, bmp, nullptr) < 0) {
            ImageList_Destroy(list);
            list = nullptr;
        }
        hot->x = p.hotX;
        hot->y = p.hotY;
    }
    if (bmp) {
        DeleteObject(bmp);
    }
    IUnknown* objs[] = {scaler, conv, frame, dec, stream, wic};
    for (IUnknown* o : objs) {
        if (o) {
            o->Release();
        }
    }
    return list;
}

// The last drag's data. DoDragDrop marshals it for targets in other
// processes and never releases the stub's references, so it is disconnected
// -- but only at the next drag or exit: Explorer reads CFSTR_FILECONTENTS
// after Drop returns.
static IDataObject* gLastDragOut = nullptr;

void DragOutDisconnectLast() {
    if (!gLastDragOut) {
        return;
    }
    CoDisconnectObject(gLastDragOut, 0);
    gLastDragOut->Release();
    gLastDragOut = nullptr;
}

static bool PrimaryButtonDown() {
    int vk = GetSystemMetrics(SM_SWAPBUTTON) ? VK_RBUTTON : VK_LBUTTON;
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

static DragOutResult DragOutResultOf(HRESULT hr, DWORD effect) {
    if (hr != DRAGDROP_S_DROP) {
        return DragOutResult::Canceled;
    }
    if (effect & DROPEFFECT_MOVE) {
        return DragOutResult::Moved;
    }
    if (effect & DROPEFFECT_COPY) {
        return DragOutResult::Copied;
    }
    if (effect & DROPEFFECT_LINK) {
        return DragOutResult::Linked;
    }
    return DragOutResult::Canceled;
}

// Run from the main-thread queue: DoDragDrop has a loop of its own, which
// must not run inside the dispatch of the move that started it.
static void DragOutRun(DragOutRequest* req) {
    Window* win = req->win;
    HWND hwnd = win && win->plat ? win->plat->hwnd : nullptr;
    // The button came up before the queue got here: OLE would wait in its
    // loop for the next input to notice.
    if (!hwnd || !PrimaryButtonDown()) {
        DragOutFinish(req, DragOutResult::Canceled);
        return;
    }
    IDataObject* data = DragOutDataObjectNew(req->data);
    if (!data) {
        DragOutFinish(req, DragOutResult::Canceled);
        return;
    }
    POINT hot = {};
    HIMAGELIST list = DragOutPreviewList(req->data, HostDpi(hwnd) / 96.f, &hot);
    auto* src = new DragOutSource();
    if (list && ImageList_BeginDrag(list, 0, hot.x, hot.y)) {
        POINT pt = {};
        GetCursorPos(&pt);
        // The desktop, so the preview is not clipped to this window.
        src->imageDrag = ImageList_DragEnter(GetDesktopWindow(), pt.x, pt.y);
        if (!src->imageDrag) {
            ImageList_EndDrag();
        }
    }
    DWORD effect = DROPEFFECT_NONE;
    HRESULT hr = DoDragDrop(data, src, DROPEFFECT_COPY, &effect);
    if (src->imageDrag) {
        ImageList_DragLeave(GetDesktopWindow());
        ImageList_EndDrag();
    }
    if (list) {
        ImageList_Destroy(list);
    }
    src->Release();
    DragOutDisconnectLast();
    gLastDragOut = data;
    DragOutFinish(req, DragOutResultOf(hr, effect));
}

bool PlatStartDragOut(Window* win, DragOutRequest* req) {
    if (!win || !win->plat || !win->plat->hwnd) {
        return false;
    }
    ExecPost(MkFunc0(DragOutRun, req));
    return true;
}

static void RegisterFileDrop(Window* win, HWND hwnd) {
    if (!win || !win->plat || win->plat->fileDrop) {
        return;
    }
    auto* target = new FileDropTarget(win);
    if (SUCCEEDED(RegisterDragDrop(hwnd, target))) {
        win->plat->fileDrop = true;
    }
    target->Release();
}

static void RevokeFileDrop(Window* win, HWND hwnd) {
    if (!win || !win->plat || !win->plat->fileDrop) {
        return;
    }
    RevokeDragDrop(hwnd);
    win->plat->fileDrop = false;
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam,
                                LPARAM lParam) {
    Window* win = (Window*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (msg == WM_NCCREATE) {
        auto* cs = (CREATESTRUCTW*)lParam;
        win = (Window*)cs->lpCreateParams;
        win->plat->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)win);
    }
    if (!win) {
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    switch (msg) {
        case WM_CREATE: {
            PlatSetTimer(win, WindowTimerMs(win));
            RegisterFileDrop(win, hwnd);
            return 0;
        }
        case WM_GETOBJECT:
            if ((LONG)lParam == UiaRootObjectId) {
                if (!win->plat->accessibilityAsked) {
                    // Frames were not collecting the tree; the next one does.
                    win->plat->accessibilityAsked = true;
                    AppInvalidate(win);
                }
                if (!win->plat->accessibility) {
                    win->plat
                        ->accessibility = AccessibilityWinNew(win, (void*)hwnd);
                }
                return (LRESULT)AccessibilityWinGetObject(
                    win->plat->accessibility, (uintptr_t)wParam,
                    (intptr_t)lParam);
            }
            break;
        case WM_KEYDOWN:
            WindowKeyDown(win, (int)wParam, ShiftDown(), CtrlDown(), AltDown(),
                          WinDown(), false, WinKeyDownFlags(wParam, lParam));
            return 0;
        case WM_SYSKEYDOWN: {
            // Rust GPUI's translate_accelerator routes both WM_KEYDOWN and
            // WM_SYSKEYDOWN through one key dispatcher. Keep an unhandled
            // system chord for DefWindowProc (Alt+F4/menu behavior), but
            // consume a binding such as Alt+Up before Windows takes it.
            bool alt = AltDown() || (lParam & (1ll << 29)) != 0;
            win->eatSysChar = false;
            if (WindowKeyDown(win, (int)wParam, ShiftDown(), CtrlDown(), alt,
                              WinDown(), false,
                              WinKeyDownFlags(wParam, lParam))) {
                win->eatSysChar = true;
                return 0;
            }
            break;
        }
        case WM_KEYUP:
            WindowKeyUp(win, (int)wParam, ShiftDown(), CtrlDown(), AltDown(),
                        WinDown());
            return 0;
        case WM_SYSKEYUP: {
            bool alt = AltDown() || (lParam & (1ll << 29)) != 0;
            WindowKeyUp(win, (int)wParam, ShiftDown(), CtrlDown(), alt,
                        WinDown());
            // Rust always consumes this release so ModifiersChanged remains
            // coherent and the paired key-up is not interpreted twice.
            return 0;
        }
        case WM_CHAR:
            WindowChar(win, (uint32_t)wParam, CtrlDown(), AltDown());
            return 0;
        case WM_SYSCHAR:
            if (win->eatSysChar) {
                win->eatSysChar = false;
                return 0;
            }
            break;
        case WM_IME_SETCONTEXT:
            // The field draws the marked text itself, so the system's inline
            // composition window would be a second copy of it.
            lParam &= ~(LPARAM)ISC_SHOWUICOMPOSITIONWINDOW;
            break;
        case WM_IME_STARTCOMPOSITION:
            if (win->input && win->input->focused) {
                HIMC imc = ImmGetContext(hwnd);
                if (imc) {
                    ImeMoveCandidateWindow(win, imc);
                    ImmReleaseContext(hwnd, imc);
                }
                return 0;
            }
            break;
        case WM_IME_COMPOSITION:
            if (ImeComposition(win, lParam)) {
                return 0;
            }
            break;
        case WM_IME_ENDCOMPOSITION:
            if (win->input && win->input->focused) {
                InputUnmarkText(win->input, win->app, win);
                AppInvalidate(win);
            }
            break;
        case WM_ACTIVATE:
            // is_window_active: the frame dims while another window has the
            // focus.
            WindowSetActive(win, LOWORD(wParam) != WA_INACTIVE);
            break;
        case WM_MOUSEACTIVATE:
            // The press that follows is the one that activated the window —
            // when it is a client-area press. A caption or border click
            // never reaches MouseDown, so arming for it would hand
            // first_mouse to whatever client press came next.
            if (LOWORD(lParam) == HTCLIENT) {
                win->plat->firstMouse = true;
            }
            break;
        case WM_RBUTTONDOWN:
            MouseDown(win, MouseButton::Right, lParam);
            return 0;
        case WM_RBUTTONUP:
            MouseUp(win, MouseButton::Right, lParam);
            return 0;
        case WM_MBUTTONDOWN:
            MouseDown(win, MouseButton::Middle, lParam);
            return 0;
        case WM_MBUTTONUP:
            MouseUp(win, MouseButton::Middle, lParam);
            return 0;
        // The two thumb buttons, GPUI's MouseButton::Navigate. Win32 wants
        // TRUE back rather than 0 from these two.
        case WM_XBUTTONDOWN:
            MouseDown(win,
                      GET_XBUTTON_WPARAM(wParam) == XBUTTON1
                          ? MouseButton::NavigateBack
                          : MouseButton::NavigateForward,
                      lParam);
            return TRUE;
        case WM_XBUTTONUP:
            MouseUp(win,
                    GET_XBUTTON_WPARAM(wParam) == XBUTTON1
                        ? MouseButton::NavigateBack
                        : MouseButton::NavigateForward,
                    lParam);
            return TRUE;
        case WM_NCCALCSIZE: {
            // The client title bar owns the top edge: keep the frame the
            // default handler puts on the other three sides but hand the
            // caption band back, so the view paints from y = 0. Maximized,
            // Windows sizes the window past the work area by the frame
            // thickness, so that much of the top inset has to come back or
            // the title bar lands under the screen edge.
            if (!ClientDecorated(win) || wParam == 0) {
                break;
            }
            auto* p = (NCCALCSIZE_PARAMS*)lParam;
            LONG top = p->rgrc[0].top;
            DefWindowProcW(hwnd, msg, wParam, lParam);
            p->rgrc[0].top = top;
            if (IsZoomed(hwnd)) {
                p->rgrc[0].top += BorderYPx();
            }
            // DefWindowProc answers WVR_VALIDRECTS and copies the old client
            // into the new one. With the caption inset removed, that copy is
            // one frame off the layout — menus and the file tree walk a few
            // pixels during a right-edge drag. WVR_REDRAW invalidates the
            // whole client instead; WM_SIZE paints it at the new size.
            return WVR_REDRAW;
        }
        case WM_NCHITTEST: {
            LRESULT hit = DefWindowProcW(hwnd, msg, wParam, lParam);
            if (hit != HTCLIENT) {
                return hit;
            }
            POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ScreenToClient(hwnd, &pt);
            // WM_NCCALCSIZE gave the top frame to the client, so the resize
            // band along it is ours to report. The other three sides are
            // still the default handler's, which answered above.
            // The top band is client-drawn. A window that is not resizable
            // does not offer it, matching window_border.rs.
            if (ClientDecorated(win) && win->opts.resizable &&
                !IsZoomed(hwnd) && pt.y < BorderYPx()) {
                RECT rc = {};
                GetClientRect(hwnd, &rc);
                if (pt.x < BorderPx()) {
                    return HTTOPLEFT;
                }
                if (pt.x >= rc.right - BorderPx()) {
                    return HTTOPRIGHT;
                }
                return HTTOP;
            }
            float dipX = PxToDip(&win->paint, pt.x);
            float dipY = PxToDip(&win->paint, pt.y);
            switch (WindowChromeHit(win, dipX, dipY)) {
                case ClickWinMin:
                    return HTMINBUTTON;
                case ClickWinMax:
                    return HTMAXBUTTON;
                case ClickWinClose:
                    return HTCLOSE;
                case ClickWinCaption:
                    return HTCAPTION;
                default:
                    return HTCLIENT;
            }
        }
        case WM_SETTINGCHANGE:
            // events.rs `handle_system_settings_changed`: wParam 0 is the
            // string-named half of this message, and one string out of the
            // many it arrives for is the theme having changed.
            if (wParam == 0 && lParam &&
                wcscmp((const wchar_t*)lParam, L"ImmersiveColorSet") == 0) {
                ConfigureDwmDarkMode(hwnd);
            }
            return 0;
        case WM_SIZE:
            win->paint.dpi = HostDpi(hwnd);
            win->minimized = wParam == SIZE_MINIMIZED;
            // Paint now, not after a coalesced WM_PAINT: during a live
            // resize DWM otherwise composites the last flip buffer stretched
            // to the new client, which is the same walk the menus do.
            if (wParam != SIZE_MINIMIZED) {
                RenderFrame(win);
                ValidateRect(hwnd, nullptr);
            }
            return 0;
        case WM_DPICHANGED: {
            auto* r = (RECT*)lParam;
            SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left,
                         r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
            PaintTargetFree(&win->paint);
            // The suggested rect can be the size the window already has, and
            // then no WM_SIZE comes to paint it at the new dpi.
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_TIMER:
            WindowTimerTick(win);
            return 0;
        case WM_PAINT: {
            // BeginPaint/EndPaint only to clear the update region — the
            // frame goes to the window's own swap chain, not to this HDC.
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            RenderFrame(win);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_CAPTURECHANGED: {
            // The compositor now owns the pointer. A native release
            // cannot click through to the current GPUI hit targets.
            WindowCancelPointerGesture(win);
            return 0;
        }
        case WM_POINTERDOWN:
        case WM_POINTERUPDATE:
        case WM_POINTERUP:
            if (IsTouchPointer(wParam)) {
                TouchPointer(win, msg, wParam, lParam);
            }
            break;
        case WM_GESTURENOTIFY:
            GestureNotify(win, lParam);
            break;
        case WM_GESTURE:
            if (TouchGesture(win, lParam)) {
                return 0;
            }
            break;
        case WM_MOUSEMOVE: {
            if (IsTouchScrollMouse(win)) {
                return 0;
            }
            win->paint.dpi = HostDpi(hwnd);
            float x = PxToDip(&win->paint, GET_X_LPARAM(lParam));
            float y = PxToDip(&win->paint, GET_Y_LPARAM(lParam));
            MouseMove(win, x, y);
            TRACKMOUSEEVENT tme = {sizeof(tme)};
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);
            return 0;
        }
        case WM_MOUSELEAVE:
            // GPUI_HOVER_HOLD=1 ignores it, the way GPUI_TODAY pins the date:
            // a session that will not let SetCursorPos move the pointer —
            // locked, or a CI agent — answers every synthetic WM_MOUSEMOVE
            // with a leave, so no hover state ever lasts long enough to be
            // photographed. Nothing but a screenshot tool sets it.
            if (!HoverHoldForTests()) {
                MouseExited(win);
            }
            return 0;
        case WM_NCMOUSEMOVE: {
            // The title bar's own cells answer WM_NCHITTEST as HTMINBUTTON,
            // HTMAXBUTTON, HTCLOSE and HTCAPTION, so the pointer over them is
            // non-client and never reaches WM_MOUSEMOVE. Hover still has to
            // follow it. Falls through to the default handler afterwards,
            // which is what puts up the Windows 11 snap layout flyout.
            // Only those four: over a resize border the default handler owns
            // the cursor, and a move event would put the arrow back.
            if (wParam != HTCAPTION && wParam != HTMINBUTTON &&
                wParam != HTMAXBUTTON && wParam != HTCLOSE) {
                break;
            }
            win->paint.dpi = HostDpi(hwnd);
            POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ScreenToClient(hwnd, &pt);
            MouseMove(win, PxToDip(&win->paint, pt.x),
                      PxToDip(&win->paint, pt.y));
            TRACKMOUSEEVENT tme = {sizeof(tme)};
            tme.dwFlags = TME_LEAVE | TME_NONCLIENT;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);
            break;
        }
        case WM_NCMOUSELEAVE:
            // The same hold as WM_MOUSELEAVE: a screenshot session's hover
            // must survive the leave the non-client tracking sends too.
            if (!HoverHoldForTests()) {
                MouseExited(win);
            }
            break;
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK:
            if (!IsTouchScrollMouse(win)) {
                MouseDown(win, MouseButton::Left, lParam);
            }
            return 0;
        case WM_LBUTTONUP:
            if (!IsTouchScrollMouse(win)) {
                MouseUp(win, MouseButton::Left, lParam);
            }
            return 0;
        case WM_NCLBUTTONDOWN:
            if (wParam == HTMINBUTTON) {
                AppMinimize(win);
                return 0;
            }
            if (wParam == HTMAXBUTTON) {
                AppToggleMaximize(win);
                return 0;
            }
            if (wParam == HTCLOSE) {
                AppClose(win);
                return 0;
            }
            break;
        // Both wheels report in WHEEL_DELTA detents; a notch is
        // WheelNotchPixels(), which is GPUI's three lines of the window's own
        // text. The horizontal wheel counts the
        // other way round, so its sign is flipped to match: positive scrolls
        // the view left, as positive scrolls it up.
        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL: {
            POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ScreenToClient(hwnd, &pt);
            float x = PxToDip(&win->paint, pt.x);
            float y = PxToDip(&win->paint, pt.y);
            float delta = (float)GET_WHEEL_DELTA_WPARAM(wParam) /
                          (float)WHEEL_DELTA * WheelNotchPixels(win->app);
            bool horizontal = msg == WM_MOUSEHWHEEL;
            PlatformInput in = InputScrollWheel(x, y, horizontal ? -delta : 0.f,
                                                horizontal ? 0.f : delta, false,
                                                ModsNow(), TouchPhase::Moved);
            WindowDispatchInput(win, &in);
            return 0;
        }
        case WM_SETCURSOR:
            // Only the client area; the frame's resize arrows are the
            // default handler's business.
            if (LOWORD(lParam) == HTCLIENT) {
                SetCursor(win->cursorHidden
                              ? nullptr
                              : (win->plat->cursor
                                     ? win->plat->cursor
                                     : LoadCursorW(nullptr, IDC_ARROW)));
                return TRUE;
            }
            break;
        case WM_ERASEBKGND:
            return 1;
        case WM_CLOSE:
            if (!WindowShouldClose(win)) {
                return 0;
            }
            break;
        case WM_DESTROY: {
            RevokeFileDrop(win, hwnd);
            KillTimer(hwnd, 1);
            App* app = win->app;
            PlatWindow* plat = win->plat;
            AccessibilityWinClose(plat->accessibility);
            plat->accessibility = nullptr;
            WindowClosed(win);
            delete plat;
            // The message loop ends when the last window closes.
            if (!AppAnyWindowOpen(app)) {
                PostQuitMessage(0);
            }
            return 0;
        }
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ─── window commands ──────────────────────────────────────────────────────

void AppQuit(Window* win) {
    HWND hwnd = Hwnd(win);
    if (hwnd) {
        DestroyWindow(hwnd);
    }
}

void PlatInvalidate(Window* win) {
    HWND hwnd = Hwnd(win);
    if (hwnd) {
        InvalidateRect(hwnd, nullptr, FALSE);
    }
}

bool WindowClientDecorated(Window* win) {
    // The caption is dropped for both of these and the frame is left; nothing
    // else draws min/max/close.
    return win && (win->opts.clientTitleBar || win->opts.borderless);
}

float WindowScaleFactor(Window* win) {
    return win && win->paint.dpi > 0 ? win->paint.dpi / 96.f : 1.f;
}

// --- WindowCapturePngTemp ---------------------------------------------------
//
// PrintWindow with PW_RENDERFULLCONTENT reads what DWM composes: the flip
// chain's last frame and child windows such as WebView2's.

constexpr UINT kPwClientOnly = 0x1;
constexpr UINT kPwRenderFullContent = 0x2;

// One window to capture and its screen rect in physical pixels.
struct CaptureShot {
    HWND hwnd;
    RECT r;
    bool clientOnly;
};

struct CaptureEnum {
    HWND main;
    DWORD pid;
    Vec<CaptureShot>* shots;
};

static bool CaptureShotFor(HWND hwnd, bool clientOnly, CaptureShot* out) {
    RECT r = {};
    if (clientOnly) {
        GetClientRect(hwnd, &r);
        MapWindowPoints(hwnd, nullptr, (POINT*)&r, 2);
    } else {
        GetWindowRect(hwnd, &r);
    }
    if (r.right <= r.left || r.bottom <= r.top) {
        return false;
    }
    *out = {hwnd, r, clientOnly};
    return true;
}

// The process's other visible top-level windows, top of the z-order first.
static BOOL CALLBACK CaptureEnumProc(HWND hwnd, LPARAM lp) {
    auto* e = (CaptureEnum*)lp;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (hwnd == e->main || pid != e->pid || !IsWindowVisible(hwnd) ||
        IsIconic(hwnd)) {
        return TRUE;
    }
    CaptureShot shot = {};
    if (CaptureShotFor(hwnd, false, &shot)) {
        VecAppend(*e->shots, shot);
    }
    return TRUE;
}

static bool CaptureWritePng(Str path, const void* bits, int w, int h) {
    IWICImagingFactory* wic = nullptr;
    IWICStream* stream = nullptr;
    IWICBitmapEncoder* enc = nullptr;
    IWICBitmapFrameEncode* frame = nullptr;
    HRESULT hr =
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                         __uuidof(IWICImagingFactory), (void**)&wic);
    if (SUCCEEDED(hr)) {
        hr = wic->CreateStream(&stream);
    }
    if (SUCCEEDED(hr)) {
        hr = stream->InitializeFromFilename(ToCWstrTemp(path), GENERIC_WRITE);
    }
    if (SUCCEEDED(hr)) {
        hr = wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc);
    }
    if (SUCCEEDED(hr)) {
        hr = enc->Initialize(stream, WICBitmapEncoderNoCache);
    }
    if (SUCCEEDED(hr)) {
        hr = enc->CreateNewFrame(&frame, nullptr);
    }
    if (SUCCEEDED(hr)) {
        hr = frame->Initialize(nullptr);
    }
    if (SUCCEEDED(hr)) {
        hr = frame->SetSize((UINT)w, (UINT)h);
    }
    WICPixelFormatGUID pixFmt = GUID_WICPixelFormat32bppBGRA;
    if (SUCCEEDED(hr)) {
        hr = frame->SetPixelFormat(&pixFmt);
    }
    if (SUCCEEDED(hr) && !IsEqualGUID(pixFmt, GUID_WICPixelFormat32bppBGRA)) {
        hr = E_FAIL;
    }
    UINT stride = (UINT)w * 4;
    if (SUCCEEDED(hr)) {
        hr = frame->WritePixels((UINT)h, stride, stride * (UINT)h, (BYTE*)bits);
    }
    if (SUCCEEDED(hr)) {
        hr = frame->Commit();
    }
    if (SUCCEEDED(hr)) {
        hr = enc->Commit();
    }
    if (frame) {
        frame->Release();
    }
    if (enc) {
        enc->Release();
    }
    if (stream) {
        stream->Release();
    }
    if (wic) {
        wic->Release();
    }
    return SUCCEEDED(hr);
}

// `win`'s client area; other windows with their frame.
TempStr WindowCapturePngTemp(Window* win, Str path, bool allWindows) {
    HWND main = Hwnd(win);
    if (!main || IsIconic(main)) {
        return StrL("ERR no-window");
    }
    if (len(path) == 0) {
        return StrL("ERR no-path");
    }

    // back to front: `win` first, then the others bottom-up
    Vec<CaptureShot> shots;
    CaptureShot mainShot = {};
    if (!CaptureShotFor(main, true, &mainShot)) {
        return StrL("ERR empty");
    }
    VecAppend(shots, mainShot);
    if (allWindows) {
        Vec<CaptureShot> others;
        CaptureEnum e = {main, GetCurrentProcessId(), &others};
        EnumWindows(CaptureEnumProc, (LPARAM)&e);
        for (int i = len(others) - 1; i >= 0; i--) {
            VecAppend(shots, others[i]);
        }
    }
    RECT total = shots[0].r;
    for (int i = 1; i < len(shots); i++) {
        UnionRect(&total, &total, &shots[i].r);
    }
    int w = total.right - total.left;
    int h = total.bottom - total.top;

    void* bits = nullptr;
    HBITMAP canvas = WinDib32New(w, h, &bits);
    if (!canvas) {
        return StrL("ERR alloc");
    }
    HDC canvasDc = CreateCompatibleDC(nullptr);
    HGDIOBJ canvasOld = SelectObject(canvasDc, canvas);
    int captured = 0;
    for (int i = 0; i < len(shots); i++) {
        const CaptureShot& s = shots[i];
        int sw = s.r.right - s.r.left;
        int sh = s.r.bottom - s.r.top;
        void* shotBits = nullptr;
        HBITMAP bmp = WinDib32New(sw, sh, &shotBits);
        if (!bmp) {
            continue;
        }
        HDC dc = CreateCompatibleDC(nullptr);
        HGDIOBJ old = SelectObject(dc, bmp);
        UINT flags = kPwRenderFullContent | (s.clientOnly ? kPwClientOnly : 0);
        if (PrintWindow(s.hwnd, dc, flags)) {
            BitBlt(canvasDc, s.r.left - total.left, s.r.top - total.top, sw, sh,
                   dc, 0, 0, SRCCOPY);
            captured++;
        }
        SelectObject(dc, old);
        DeleteDC(dc);
        DeleteObject(bmp);
    }
    GdiFlush();
    SelectObject(canvasDc, canvasOld);
    DeleteDC(canvasDc);

    // GDI leaves alpha undefined; the capture is opaque
    auto* px = (uint8_t*)bits;
    for (int i = 0; i < w * h; i++) {
        px[i * 4 + 3] = 0xff;
    }
    bool ok = captured > 0 && CaptureWritePng(path, bits, w, h);
    DeleteObject(canvas);
    if (captured == 0) {
        return StrL("ERR empty");
    }
    if (!ok) {
        return StrL("ERR write");
    }
    return WindowCaptureOkTemp(w, h, WindowScaleFactor(win), captured, 0);
}

void AppActivate(Window* win) {
    HWND hwnd = Hwnd(win);
    if (!hwnd) {
        return;
    }
    if (IsIconic(hwnd)) {
        ShowWindow(hwnd, SW_RESTORE);
    }
    SetForegroundWindow(hwnd);
}

void AppMinimize(Window* win) {
    HWND hwnd = Hwnd(win);
    if (hwnd) {
        ShowWindow(hwnd, SW_MINIMIZE);
    }
}

void AppToggleMaximize(Window* win) {
    HWND hwnd = Hwnd(win);
    if (!hwnd) {
        return;
    }
    WINDOWPLACEMENT wp = {sizeof(wp)};
    GetWindowPlacement(hwnd, &wp);
    ShowWindow(hwnd, wp.showCmd == SW_SHOWMAXIMIZED ? SW_RESTORE : SW_MAXIMIZE);
}

void WindowSetFullScreen(Window* win, bool fullScreen) {
    HWND hwnd = Hwnd(win);
    PlatWindow* pw = win ? win->plat : nullptr;
    if (!hwnd || !pw || pw->fullScreen == fullScreen) {
        return;
    }
    if (fullScreen) {
        pw->windowedStyle = GetWindowLongPtrW(hwnd, GWL_STYLE);
        pw->windowedPlacement = {sizeof(WINDOWPLACEMENT)};
        GetWindowPlacement(hwnd, &pw->windowedPlacement);
        MONITORINFO monitor = {sizeof(monitor)};
        GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST),
                        &monitor);
        SetWindowLongPtrW(hwnd, GWL_STYLE,
                          pw->windowedStyle & ~(WS_CAPTION | WS_THICKFRAME));
        RECT r = monitor.rcMonitor;
        SetWindowPos(hwnd, HWND_TOP, r.left, r.top, r.right - r.left,
                     r.bottom - r.top, SWP_FRAMECHANGED | SWP_NOACTIVATE);
    } else {
        SetWindowLongPtrW(hwnd, GWL_STYLE, pw->windowedStyle);
        SetWindowPlacement(hwnd, &pw->windowedPlacement);
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                         SWP_NOACTIVATE);
    }
    pw->fullScreen = fullScreen;
}

// A window created with WS_EX_NOREDIRECTIONBITMAP can still be layered: DWM
// applies the alpha to the composition surface. The style stays on once
// either call has needed it; taking it off again buys nothing.
static void ApplyLayered(HWND hwnd, const PlatWindow* pw) {
    LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    bool layered = (ex & WS_EX_LAYERED) != 0;
    if (!layered && pw->opacity >= 100 && !pw->clickThrough) {
        return;
    }
    LONG_PTR want = (ex | WS_EX_LAYERED) & ~(LONG_PTR)WS_EX_TRANSPARENT;
    if (pw->clickThrough) {
        want |= WS_EX_TRANSPARENT;
    }
    if (want != ex) {
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, want);
    }
    SetLayeredWindowAttributes(hwnd, 0, (BYTE)(pw->opacity * 255 / 100),
                               LWA_ALPHA);
    if (!layered) {
        // A window that turns layered after it was shown is blank until it
        // is drawn again.
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                         SWP_NOACTIVATE);
        RedrawWindow(hwnd, nullptr, nullptr,
                     RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    }
}

void AppWindowSetOpacity(Window* win, int percent) {
    HWND hwnd = Hwnd(win);
    PlatWindow* pw = win ? win->plat : nullptr;
    if (!hwnd || !pw) {
        return;
    }
    pw->opacity = percent < 20 ? 20 : percent > 100 ? 100 : percent;
    ApplyLayered(hwnd, pw);
}

bool PlatWindowCanBeTransparent() {
    return PaintWindowCanBeTransparent();
}

void AppWindowSetClickThrough(Window* win, bool on) {
    HWND hwnd = Hwnd(win);
    PlatWindow* pw = win ? win->plat : nullptr;
    if (!hwnd || !pw) {
        return;
    }
    pw->clickThrough = on;
    ApplyLayered(hwnd, pw);
}

void AppDrag(Window* win) {
    HWND hwnd = Hwnd(win);
    if (hwnd) {
        ReleaseCapture();
        SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
    }
}

void AppSetTitle(Window* win, Str title) {
    HWND hwnd = Hwnd(win);
    if (hwnd) {
        SetWindowTextW(hwnd, ToCWstrTemp(title));
    }
}

// What a SetTimer delay of `ms` has to be asked for as.
//
// A USER timer runs off the system clock tick — 15.625 ms unless something
// has raised the resolution — and SetTimer rounds a request *up* to the next
// whole tick. So the 16 ms an animating window asks for lands on the second
// tick, 31.25 ms, and the window runs at 32 FPS on a 60 Hz display no matter
// how little it spends drawing. That is what `fps_monitor` was showing: a
// 1.1 ms frame arriving every 29 ms.
//
// Nothing can deliver a deadline finer than the tick, so the ask is rounded
// *down* to a whole number of ticks instead: the timer then fires at the last
// tick before the deadline rather than the first one after it. Early is safe
// and late is not — WindowTimerTick skips the timers that are not due yet and
// re-arms from what is left, so an early wake costs one more pass through the
// loop, while a late one is a frame that did not happen.
//
// GPUI does not use a timer here at all: its Windows backend waits on the
// compositor clock (`DCompositionWaitForCompositorClock`), which is the
// display's own cadence rather than an approximation of it. This lands within
// a tick of the same rate without a second thread; what it does not get is
// the phase, so a frame can still be handed to DWM just after a vblank.
//
// That round-down is only for a window that is actually animating. Anything
// else asks for the delay it was given. Rounding a 16 ms interval down to the
// tick woke an idle window on every tick, about 64 times a second, to run a
// handler that was due about 35 of those times.
static UINT TimerDelay(int ms, bool animating) {
    if (!animating) {
        return ms >= 1 ? (UINT)ms : 1;
    }
    static double tick = 0;
    if (tick <= 0) {
        DWORD adjust = 0;
        DWORD increment = 0;
        BOOL disabled = FALSE;
        // The tick in 100 ns units, which is what the timer counts in.
        if (GetSystemTimeAdjustment(&adjust, &increment, &disabled) &&
            increment > 0) {
            tick = (double)increment / 10000.0;
        } else {
            tick = 15.625;
        }
    }
    double whole = (double)((int)((double)ms / tick)) * tick;
    // Shorter than one tick is as short as it goes: the next tick, whenever
    // it comes.
    return whole >= 1.0 ? (UINT)whole : 1;
}

void PlatSetTimer(Window* win, int ms) {
    HWND hwnd = Hwnd(win);
    if (!hwnd) {
        return;
    }
    if (ms > 0) {
        bool animating = win->anim || win->opts.anim || win->animFrame;
        SetTimer(hwnd, 1, TimerDelay(ms, animating), nullptr);
    } else {
        KillTimer(hwnd, 1);
    }
}

void PlatSetMouseCapture(Window* win, bool capture) {
    HWND hwnd = Hwnd(win);
    if (!hwnd) {
        return;
    }
    if (capture) {
        SetCapture(hwnd);
        return;
    }
    // Only ours to release. A title-bar drag hands the capture to the system
    // (AppDrag releases it and lets DefWindowProc take over), so the button
    // coming up afterwards must not take somebody else's.
    if (GetCapture() == hwnd) {
        ReleaseCapture();
    }
}

static void CursorImageDestroy(void* plat) {
    DestroyCursor((HCURSOR)plat);
}

// A 32-bit top-down DIB carries the alpha; CreateIconIndirect still wants a
// mask, and an all-zero one leaves the color bitmap in charge.
static HCURSOR CursorImageHandle(CursorImage* img) {
    if (!img || img->plat) {
        return img ? (HCURSOR)img->plat : nullptr;
    }
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = img->w;
    bmi.bmiHeader.biHeight = -img->h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP color =
        CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!color || !bits) {
        if (color) DeleteObject(color);
        return nullptr;
    }
    int n = img->w * img->h;
    uint8_t* d = (uint8_t*)bits;
    for (int i = 0; i < n; i++) {
        const uint8_t* s = img->rgba + i * 4;
        d[i * 4 + 0] = s[2];
        d[i * 4 + 1] = s[1];
        d[i * 4 + 2] = s[0];
        d[i * 4 + 3] = s[3];
    }
    int maskStride = ((img->w + 15) / 16) * 2;
    uint8_t* maskBits = (uint8_t*)calloc((size_t)maskStride * img->h, 1);
    HBITMAP mask =
        maskBits ? CreateBitmap(img->w, img->h, 1, 1, maskBits) : nullptr;
    free(maskBits);
    HCURSOR cursor = nullptr;
    if (mask) {
        ICONINFO ii = {};
        ii.fIcon = FALSE;
        ii.xHotspot = (DWORD)img->hotX;
        ii.yHotspot = (DWORD)img->hotY;
        ii.hbmMask = mask;
        ii.hbmColor = color;
        cursor = (HCURSOR)CreateIconIndirect(&ii);
        DeleteObject(mask);
    }
    DeleteObject(color);
    if (cursor) {
        img->plat = cursor;
        img->platFree = CursorImageDestroy;
    }
    return cursor;
}

void PlatSetCursor(Window* win, CursorKind kind) {
    if (!win || !win->plat) {
        return;
    }
    if (win->cursorHidden) {
        SetCursor(nullptr);
        return;
    }
    if (HCURSOR image = CursorImageHandle(win->cursorImage)) {
        win->plat->cursor = image;
        SetCursor(image);
        return;
    }
    // gpui's Windows backend, platform/windows/util.rs load_cursor: the
    // shapes Windows has no cursor for fall back to the arrow.
    LPCWSTR name = IDC_ARROW;
    switch (kind) {
        case CursorKind::IBeam:
        case CursorKind::IBeamVertical:
            name = IDC_IBEAM;
            break;
        case CursorKind::Pointer:
        case CursorKind::DragLink:
            name = IDC_HAND;
            break;
        case CursorKind::ColResize:
        case CursorKind::ResizeLeft:
        case CursorKind::ResizeRight:
        case CursorKind::ResizeLeftRight:
            name = IDC_SIZEWE;
            break;
        case CursorKind::RowResize:
        case CursorKind::ResizeUp:
        case CursorKind::ResizeDown:
        case CursorKind::ResizeUpDown:
            name = IDC_SIZENS;
            break;
        case CursorKind::ResizeUpLeftDownRight:
            name = IDC_SIZENWSE;
            break;
        case CursorKind::ResizeUpRightDownLeft:
            name = IDC_SIZENESW;
            break;
        case CursorKind::NotAllowed:
            name = IDC_NO;
            break;
        case CursorKind::Crosshair:
            name = IDC_CROSS;
            break;
        case CursorKind::ClosedHand:
            name = IDC_SIZEALL;
            break;
        default:
            break;
    }
    win->plat->cursor = LoadCursorW(nullptr, name);
    SetCursor(win->plat->cursor);
}

int PlatDoubleClickMs() {
    return (int)GetDoubleClickTime();
}

void* PlatWindowHandle(Window* win) {
    return (void*)Hwnd(win);
}

uint64_t PlatWindowDisplay(Window* win) {
    HWND hwnd = Hwnd(win);
    if (!hwnd) {
        return 0;
    }
    return (uint64_t)(uintptr_t)MonitorFromWindow(hwnd,
                                                  MONITOR_DEFAULTTONEAREST);
}

double PlatDisplayRefreshPeriod(uint64_t display) {
    HMONITOR monitor = (HMONITOR)(uintptr_t)display;
    if (!monitor) {
        return 0;
    }
    MONITORINFOEXW info = {};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) {
        return 0;
    }
    DEVMODEW mode = {};
    mode.dmSize = sizeof(mode);
    if (!EnumDisplaySettingsW(info.szDevice, ENUM_CURRENT_SETTINGS, &mode)) {
        return 0;
    }
    // Zero and one both mean "whatever the hardware defaults to" rather than
    // a rate, which is what a driver reports when it has none to give.
    if (mode.dmDisplayFrequency <= 1) {
        return 0;
    }
    return 1.0 / (double)mode.dmDisplayFrequency;
}

void PlatInstallAccessibilityHitTest(Window* win) {
    // Root::Render calls the cross-platform seam. Windows exposes the tree
    // through WM_GETOBJECT, so installing it means making the retained root
    // ready before an automation client asks.
    if (win && win->plat && !win->plat->accessibility) {
        win->plat
            ->accessibility = AccessibilityWinNew(win, (void*)win->plat->hwnd);
    }
}

void PlatAccessibilityTreeChanged(Window* win) {
    if (win && win->plat) {
        AccessibilityWinTreeChanged(win->plat->accessibility);
    }
}

void PlatAccessibilityFocusChanged(Window* win, int focusId) {
    if (win && win->plat) {
        AccessibilityWinFocusChanged(win->plat->accessibility, focusId);
    }
}

bool PlatAccessibilityWanted(Window* win) {
    if (!win || !win->plat) {
        return false;
    }
    return win->plat->accessibilityAsked || UiaClientsAreListening();
}

bool PlatHasMenu() {
    return true;
}

// Windows has no documented way to ask for a dark HMENU. These three uxtheme
// entry points, resolved by ordinal, are the ones Windows itself uses, and a
// Windows that does not have them draws the ordinary system menu.
static void ApplyMenuTheme(HWND hwnd, bool dark) {
    typedef BOOL(WINAPI * AllowDarkModeForWindowFn)(HWND, BOOL);
    typedef int(WINAPI * SetPreferredAppModeFn)(int);
    typedef void(WINAPI * FlushMenuThemesFn)();

    static HMODULE uxtheme = nullptr;
    static bool tried = false;
    if (!tried) {
        tried = true;
        // Restrict this system DLL to System32 to prevent application-directory
        // DLL planting: the default search order would consider a file beside
        // the executable before the operating system's copy.
        uxtheme = LoadLibraryExW(L"uxtheme.dll", nullptr,
                                 LOAD_LIBRARY_SEARCH_SYSTEM32);
    }
    if (!uxtheme) {
        return;
    }
    auto allowDark = (AllowDarkModeForWindowFn)GetProcAddress(
        uxtheme, MAKEINTRESOURCEA(133));
    auto setMode =
        (SetPreferredAppModeFn)GetProcAddress(uxtheme, MAKEINTRESOURCEA(135));
    auto flush =
        (FlushMenuThemesFn)GetProcAddress(uxtheme, MAKEINTRESOURCEA(136));
    if (allowDark) {
        allowDark(hwnd, dark ? TRUE : FALSE);
    }
    if (setMode) {
        // ForceDark and ForceLight, the two PreferredAppMode values that say
        // which one regardless of what the system is set to.
        const int kForceDark = 2;
        const int kForceLight = 3;
        setMode(dark ? kForceDark : kForceLight);
    }
    if (flush) {
        flush();
    }
}

// MENU_IMAGE_SIZE: the side a menu item's image is scaled to, in logical
// pixels. The physical bitmap is this times the window's scale, so the icon
// stays sharp on a high-DPI display.
static const int kMenuImageSize = 16;

HBITMAP WinDib32New(int w, int h, void** bits) {
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    // Negative: the rows run top down, the way every caller reads them.
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    *bits = nullptr;
    HBITMAP bmp =
        CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, bits, nullptr, 0);
    if (bmp && !*bits) {
        DeleteObject(bmp);
        bmp = nullptr;
    }
    return bmp;
}

// One icon as a bitmap the menu can show: the SVG rasterized into a 32-bit
// top-down DIB, premultiplied, so Windows blends it over whatever it draws
// behind the row.
static HBITMAP MenuIconBitmap(Window* win, const PlatMenuItem& it, int px,
                              Rgba color) {
    bool fromSvg = it.iconSvg && it.iconSvgLen > 0;
    if (!win || px <= 0 || (!fromSvg && (!it.iconPath || !it.iconPath[0]))) {
        return nullptr;
    }
    void* bits = nullptr;
    HBITMAP bmp = WinDib32New(px, px, &bits);
    if (!bmp) {
        return nullptr;
    }
    // `Icon::data` rasterizes from its bytes; a path goes through the assets.
    bool drew =
        fromSvg ? SvgRasterizeXml(win->paint.pa, Str(it.iconSvg, it.iconSvgLen),
                                  px, color, (uint8_t*)bits)
                : SvgRasterize(win->paint.pa, Str(it.iconPath), px, color,
                               (uint8_t*)bits);
    if (!drew) {
        DeleteObject(bmp);
        return nullptr;
    }
    return bmp;
}

// The bitmaps a menu is showing. They have to outlive the menu, so they are
// kept until it has been destroyed.
static Vec<HBITMAP> gMenuBitmaps;

// The rows, as an HMENU. A submenu is built the same way and attached with
// MF_POPUP; destroying the menu destroys them with it.
static HMENU BuildMenu(Window* win, const PlatMenuItem* items, int n,
                       int iconPx, Rgba iconColor) {
    HMENU menu = CreatePopupMenu();
    if (!menu) {
        return nullptr;
    }
    // The position of the next row appended, which is how a bitmap is
    // attached — separators and submenus advance it too.
    UINT position = 0;
    for (int i = 0; i < n; i++) {
        const PlatMenuItem& it = items[i];
        if (it.separator) {
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            position++;
            continue;
        }
        WCHAR* label = ToCWstrTemp(Str(it.label ? it.label : ""));
        if (it.submenu && it.submenuN > 0) {
            HMENU sub =
                BuildMenu(win, it.submenu, it.submenuN, iconPx, iconColor);
            if (!sub) {
                continue;
            }
            UINT flags = MF_POPUP | (it.disabled ? MF_GRAYED : 0u);
            AppendMenuW(menu, flags, (UINT_PTR)sub, label);
            position++;
            continue;
        }
        UINT flags = MF_STRING | (it.disabled ? MF_GRAYED : 0u) |
                     (it.checked ? MF_CHECKED : 0u);
        AppendMenuW(menu, flags, (UINT_PTR)it.id, label);
        if (it.iconPath || it.iconSvg) {
            HBITMAP bmp = MenuIconBitmap(win, it, iconPx, iconColor);
            if (bmp) {
                // The item's content bitmap, not its check mark: it sits
                // beside the label the way an icon does.
                MENUITEMINFOW info = {};
                info.cbSize = sizeof(info);
                info.fMask = MIIM_BITMAP;
                info.hbmpItem = bmp;
                SetMenuItemInfoW(menu, position, TRUE, &info);
                VecAppend(gMenuBitmaps, bmp);
            }
        }
        position++;
    }
    return menu;
}

int PlatShowMenu(Window* win, const PlatMenuItem* items, int n, float x,
                 float y, bool dark) {
    HWND hwnd = Hwnd(win);
    if (!hwnd || !items || n <= 0) {
        return 0;
    }
    // The position is in logical pixels; Win32 wants physical ones, and on
    // the screen rather than in the client area.
    float scale = win->paint.dpi / 96.f;
    // The menu draws an item's bitmap at its own pixel size, so it is
    // rasterized at the device size to stay sharp on a high-DPI display.
    int iconPx = (int)lroundf((float)kMenuImageSize * scale);
    if (iconPx < 1) {
        iconPx = 1;
    }
    // An icon takes the colour the menu writes its text in, which is what
    // makes it read as part of the row in either theme.
    Rgba iconColor = dark ? Rgb(255, 255, 255) : Rgb(0, 0, 0);
    HMENU menu = BuildMenu(win, items, n, iconPx, iconColor);
    if (!menu) {
        return 0;
    }
    POINT pt = {(LONG)lroundf(x * scale), (LONG)lroundf(y * scale)};
    ClientToScreen(hwnd, &pt);
    // Without this the menu does not dismiss when the click lands elsewhere.
    SetForegroundWindow(hwnd);
    ApplyMenuTheme(hwnd, dark);
    UINT flags = TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_NONOTIFY;
    // TrackPopupMenuEx runs the OS tracking loop and answers with the id the
    // row was appended with, or 0 for a menu that was dismissed.
    int selected =
        (int)TrackPopupMenuEx(menu, flags, pt.x, pt.y, hwnd, nullptr);
    DestroyMenu(menu);
    // The menu is gone, so nothing refers to the bitmaps any more.
    for (int i = 0; i < len(gMenuBitmaps); i++) {
        DeleteObject(gMenuBitmaps[i]);
    }
    VecReset(gMenuBitmaps);
    return selected;
}

// cx.open_url. ShellExecute with no verb runs the shell's default action for
// the scheme, which is what the user has chosen as their browser.
// SPI_GETCLIENTAREAANIMATION is the Windows switch behind Settings ▸
// Accessibility ▸ Visual effects ▸ Animation effects: it answers whether
// animation is wanted, so reduce-motion is the negative of it.
bool PlatReduceMotion() {
    BOOL wanted = TRUE;
    if (!SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &wanted, 0)) {
        return false;
    }
    return !wanted;
}

void OpenUrl(Str url) {
    if (!url.s || len(url) <= 0) {
        return;
    }
    ShellExecuteW(nullptr, L"open", ToCWstrTemp(url), nullptr, nullptr,
                  SW_SHOWNORMAL);
}

void OpenPath(Str path) {
    if (!path.s || len(path) <= 0) {
        return;
    }
    ShellExecuteW(nullptr, L"open", ToCWstrTemp(path), nullptr, nullptr,
                  SW_SHOWNORMAL);
}

// ─── window.prompt (gpui_windows window.rs prompt, dialog.rs) ───────────────

using TaskDialogIndirectFn = HRESULT(WINAPI*)(const TASKDIALOGCONFIG*, int*,
                                              int*, BOOL*);

// comctl32 v6 has it, and only an application manifest gets v6; without one
// the prompt is the fallback renderer's.
static TaskDialogIndirectFn TaskDialogIndirectProc() {
    static bool looked = false;
    static TaskDialogIndirectFn fn = nullptr;
    if (!looked) {
        looked = true;
        if (HMODULE m = LoadLibraryW(L"comctl32.dll")) {
            fn = (TaskDialogIndirectFn)(void*)GetProcAddress(
                m, "TaskDialogIndirect");
        }
    }
    return fn;
}

// task_dialog_callback: Esc, the close box and WM_CLOSE answer IDCANCEL,
// which is only allowed when the prompt offered a Cancel button.
static HRESULT CALLBACK PromptDialogCallback(HWND, UINT msg, WPARAM wp, LPARAM,
                                             LONG_PTR hasCancel) {
    if (msg == TDN_BUTTON_CLICKED && wp == IDCANCEL && !hasCancel) {
        return S_FALSE;
    }
    return S_OK;
}

// On the heap: the dialog's loop draws frames, which reset the temp arena.
static WCHAR* PromptWideDup(Str s) {
    int n = len(s) > 0
                ? MultiByteToWideChar(CP_UTF8, 0, s.s, len(s), nullptr, 0)
                : 0;
    n = n > 0 ? n : 0;
    WCHAR* res = new WCHAR[n + 1];
    if (n > 0) {
        MultiByteToWideChar(CP_UTF8, 0, s.s, len(s), res, n);
    }
    res[n] = 0;
    return res;
}

static HWND PromptOwner(PromptRequest* req) {
    for (int i = 0; req->app && i < len(req->app->windows); i++) {
        Window* w = req->app->windows[i];
        if (w == req->win && w->plat) {
            return w->plat->hwnd;
        }
    }
    return nullptr;
}

// Run from the main-thread queue rather than inside the listener that asked:
// TaskDialogIndirect runs a modal loop until it is answered.
static void PromptRun(PromptRequest* req) {
    HWND owner = PromptOwner(req);
    TaskDialogIndirectFn taskDialog = TaskDialogIndirectProc();
    if (!owner || !taskDialog) {
        PromptFinish(req, -1);
        return;
    }
    TASKDIALOGCONFIG config = {};
    config.cbSize = sizeof(config);
    config.hwndParent = owner;
    // Not in Rust: centered on its window, as a macOS sheet is.
    DWORD flags =
        TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
    if (req->opts.rtl) {
        flags |= TDF_RTL_LAYOUT;
    }
    config.dwFlags = (TASKDIALOG_FLAGS)flags;
    const WCHAR* levelTitle = L"Info";
    config.pszMainIcon = TD_INFORMATION_ICON;
    if (req->level == PromptLevel::Warning) {
        levelTitle = L"Warning";
        config.pszMainIcon = TD_WARNING_ICON;
    } else if (req->level == PromptLevel::Critical) {
        levelTitle = L"Critical";
        config.pszMainIcon = TD_ERROR_ICON;
    }
    WCHAR* title =
        len(req->opts.title) > 0 ? PromptWideDup(req->opts.title) : nullptr;
    config.pszWindowTitle = title ? title : levelTitle;
    WCHAR* instruction = PromptWideDup(req->message);
    config.pszMainInstruction = instruction;
    WCHAR* content =
        len(req->detail) > 0 ? PromptWideDup(req->detail) : nullptr;
    config.pszContent = content;

    // Other buttons count down from -1: the low positive ids are the
    // dialog's own (IDOK, IDCANCEL, ...).
    int n = req->n;
    TASKDIALOG_BUTTON* buttons = new TASKDIALOG_BUTTON[n > 0 ? n : 1];
    bool hasCancel = false;
    for (int i = 0; i < n; i++) {
        const PromptButton& b = req->answers[i];
        int id = -i - 1;
        if (b.kind == PromptButton::Kind::Ok) {
            id = IDOK;
        } else if (b.kind == PromptButton::Kind::Cancel) {
            id = IDCANCEL;
            hasCancel = true;
        }
        buttons[i].nButtonID = id;
        buttons[i].pszButtonText = PromptWideDup(b.label);
    }
    config.cButtons = (UINT)n;
    config.pButtons = buttons;
    if (req->opts.defaultIx > 0 && req->opts.defaultIx < n) {
        config.nDefaultButton = buttons[req->opts.defaultIx].nButtonID;
    }
    config.pfCallback = PromptDialogCallback;
    config.lpCallbackData = hasCancel ? 1 : 0;

    int pressed = 0;
    HRESULT hr = taskDialog(&config, &pressed, nullptr, nullptr);
    int ix = -1;
    for (int i = 0; SUCCEEDED(hr) && i < n && ix < 0; i++) {
        if (buttons[i].nButtonID == pressed) {
            ix = i;
        }
    }
    for (int i = 0; i < n; i++) {
        delete[] buttons[i].pszButtonText;
    }
    delete[] buttons;
    delete[] title;
    delete[] instruction;
    delete[] content;
    PromptFinish(req, ix);
}

bool PlatWindowPrompt(Window* win, PromptRequest* req) {
    if (!win || !win->plat || !win->plat->hwnd || !TaskDialogIndirectProc()) {
        return false;
    }
    ExecPost(MkFunc0(PromptRun, req));
    return true;
}

// The wide strings a file dialog points at, on the heap until it is done:
// its loop draws frames, which reset the temp arena.
struct FileDialogStrs {
    Vec<WCHAR*> strs;
    Vec<COMDLG_FILTERSPEC> specs;

    WCHAR* Dup(Str s) {
        WCHAR* w = PromptWideDup(s);
        VecAppend(strs, w);
        return w;
    }
    ~FileDialogStrs() {
        for (int i = 0; i < len(strs); i++) {
            delete[] strs[i];
        }
    }
};

// What the open and save dialogs share: title, starting folder and name, and
// the file types (SetFileTypeIndex is 1-based).
static void FileDialogSetup(IFileDialog* dlg, FileDialogStrs* strs, Str title,
                            Str dir, Str name, const PathFilters& filters) {
    if (len(title) > 0) {
        dlg->SetTitle(strs->Dup(title));
    }
    if (len(dir) > 0) {
        // Rust canonicalizes it first; a folder the shell cannot parse is
        // left to the dialog's own choice, as Rust logs and goes on
        IShellItem* folder = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(strs->Dup(dir), nullptr,
                                                  IID_PPV_ARGS(&folder))) &&
            folder) {
            dlg->SetFolder(folder);
            folder->Release();
        }
    }
    if (len(name) > 0) {
        dlg->SetFileName(strs->Dup(name));
    }
    for (int32_t i = 0; i < filters.count; i++) {
        COMDLG_FILTERSPEC spec{};
        spec.pszName = strs->Dup(filters.items[i].name);
        spec.pszSpec = strs->Dup(filters.items[i].patterns);
        VecAppend(strs->specs, spec);
    }
    if (len(strs->specs) > 0) {
        dlg->SetFileTypes((UINT)len(strs->specs), strs->specs.els);
        int32_t ix = filters.index;
        ix = ix >= 0 && ix < filters.count ? ix : 0;
        dlg->SetFileTypeIndex((UINT)ix + 1);
    }
}

// The file type left selected, 0-based, into `filters.chosen`.
static void FileDialogChosen(IFileDialog* dlg, const PathFilters& filters,
                             bool ok) {
    if (!filters.chosen) {
        return;
    }
    UINT ix = 0;
    bool known = ok && filters.count > 0 &&
                 SUCCEEDED(dlg->GetFileTypeIndex(&ix)) && ix > 0;
    *filters.chosen = known ? (int32_t)ix - 1 : -1;
}

// An item's file system path, appended to `paths` a path a line.
static void AppendShellItemPath(IShellItem* item, StrBuilder* paths) {
    PWSTR wide = nullptr;
    if (!item || FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &wide)) ||
        !wide) {
        return;
    }
    int n =
        WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (n > 1) {
        TempStr path = AllocStrTemp(n - 1);
        if (path.s && WideCharToMultiByte(CP_UTF8, 0, wide, -1, path.s, n,
                                          nullptr, nullptr) == n) {
            if (len(*paths) > 0) {
                paths->AppendChar('\n');
            }
            paths->Append(path);
        }
    }
    CoTaskMemFree(wide);
}

// cx.prompt_for_paths. The shell's own dialog, which is IFileOpenDialog on
// everything this runs on; COM is already up (apartment threaded) because the
// drag-and-drop registration needs it. The dialog runs its own loop, so this
// blocks until the user is done, which is what the platform does either way.
TempStr PromptForPathTemp(Window* win, const PathPrompt& opts) {
    IFileOpenDialog* dlg = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr,
                                  CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg));
    if (FAILED(hr) || !dlg) {
        return {};
    }
    DWORD flags = 0;
    dlg->GetOptions(&flags);
    // FOS_PICKFOLDERS is the whole difference between the two dialogs: with
    // it the shell shows folders and answers one, without it files. A prompt
    // that asks for both gets the file one, which is what a folder cannot be
    // chosen from — Windows has no dialog that answers either.
    if (opts.directories && !opts.files) {
        flags |= FOS_PICKFOLDERS;
    } else {
        flags &= ~(DWORD)FOS_PICKFOLDERS;
    }
    if (opts.multiple) {
        flags |= FOS_ALLOWMULTISELECT;
    }
    dlg->SetOptions(flags | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    FileDialogStrs strs;
    FileDialogSetup(dlg, &strs, opts.title, opts.directory, opts.fileName,
                    opts.filters);
    HWND owner = win && win->plat ? win->plat->hwnd : nullptr;
    hr = dlg->Show(owner);
    FileDialogChosen(dlg, opts.filters, SUCCEEDED(hr));
    TempStr result;
    if (SUCCEEDED(hr)) {
        IShellItemArray* items = nullptr;
        IShellItem* item = nullptr;
        DWORD count = 0;
        if (opts.multiple && SUCCEEDED(dlg->GetResults(&items)) && items) {
            items->GetCount(&count);
        } else if (SUCCEEDED(dlg->GetResult(&item)) && item) {
            count = 1;
        }
        StrBuilder paths;
        for (DWORD i = 0; i < count; i++) {
            IShellItem* one = item;
            if (items && FAILED(items->GetItemAt(i, &one))) {
                continue;
            }
            AppendShellItemPath(one, &paths);
            if (items && one) {
                one->Release();
            }
        }
        if (item) {
            item->Release();
        }
        if (items) {
            items->Release();
        }
        if (len(paths) > 0) {
            result = StrDupTemp(Str(paths.els, len(paths)));
        }
    }
    dlg->Release();
    return result;
}

// cx.prompt_for_new_path: IFileSaveDialog, as Rust's file_save_dialog. Rust
// offers only "All files"; the file types and the default extension here are
// ours. The dialog asks before overwriting (FOS_OVERWRITEPROMPT, on by
// default) and adds the selected type's extension, or the default one, to a
// name typed without one.
TempStr PromptForNewPathTemp(Window* win, const NewPathPrompt& opts) {
    IFileSaveDialog* dlg = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileSaveDialog, nullptr,
                                  CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg));
    if (FAILED(hr) || !dlg) {
        return {};
    }
    DWORD flags = 0;
    dlg->GetOptions(&flags);
    dlg->SetOptions(flags | FOS_OVERWRITEPROMPT | FOS_FORCEFILESYSTEM |
                    FOS_PATHMUSTEXIST);
    FileDialogStrs strs;
    PathFilters filters = opts.filters;
    PathFilter all{StrL("All files"), StrL("*.*")};
    if (filters.count == 0) {
        filters.items = &all;
        filters.count = 1;
        filters.index = 0;
    }
    FileDialogSetup(dlg, &strs, opts.title, opts.directory, opts.suggestedName,
                    filters);
    Str ext = opts.defaultExt;
    if (len(ext) > 0 && ext.s[0] == '.') {
        ext = Str(ext.s + 1, len(ext) - 1);
    }
    if (len(ext) > 0) {
        dlg->SetDefaultExtension(strs.Dup(ext));
    }
    HWND owner = win && win->plat ? win->plat->hwnd : nullptr;
    hr = dlg->Show(owner);
    FileDialogChosen(dlg, opts.filters, SUCCEEDED(hr));
    TempStr result;
    IShellItem* item = nullptr;
    if (SUCCEEDED(hr) && SUCCEEDED(dlg->GetResult(&item)) && item) {
        StrBuilder path;
        AppendShellItemPath(item, &path);
        item->Release();
        if (len(path) > 0) {
            result = StrDupTemp(Str(path.els, len(path)));
        }
    }
    dlg->Release();
    return result;
}

void PlatClipboardSetText(Window* win, Str text) {
    WCHAR* w = ToCWstrTemp(text);
    int wn = (int)wcslen(w);
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)(wn + 1) * sizeof(WCHAR));
    if (!h) {
        return;
    }
    auto* dst = (WCHAR*)GlobalLock(h);
    if (!dst) {
        GlobalFree(h);
        return;
    }
    memcpy(dst, w, (size_t)(wn + 1) * sizeof(WCHAR));
    GlobalUnlock(h);
    // OpenClipboard often fails right after TrackPopupMenu or while OLE
    // holds the clipboard; retry and fall back to a null owner.
    HWND hwnd = Hwnd(win);
    bool ok = false;
    for (int i = 0; i < 16 && !ok; i++) {
        HWND owner = (i < 8 && hwnd) ? hwnd : nullptr;
        if (!OpenClipboard(owner)) {
            Sleep(8);
            continue;
        }
        EmptyClipboard();
        if (SetClipboardData(CF_UNICODETEXT, h)) {
            ok = true;
            h = nullptr;
        }
        CloseClipboard();
        if (!ok) {
            Sleep(8);
        }
    }
    if (h) {
        GlobalFree(h);
    }
}

Str PlatClipboardGetText(Arena* a, Window* win) {
    if (!OpenClipboard(Hwnd(win))) {
        return {};
    }
    Str out = {};
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    auto* w = h ? (const WCHAR*)GlobalLock(h) : nullptr;
    if (w) {
        int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr,
                                    nullptr);
        if (n > 1) {
            char* buf = (char*)Alloc(a, n);
            if (buf) {
                WideCharToMultiByte(CP_UTF8, 0, w, -1, buf, n, nullptr,
                                    nullptr);
                out = Str(buf, n - 1);
            }
        }
        GlobalUnlock(h);
    }
    CloseClipboard();
    return out;
}

static void ClipboardReadDib(Arena* a, HANDLE handle, ClipboardItem* out) {
    const auto* dib = handle ? (const uint8_t*)GlobalLock(handle) : nullptr;
    SIZE_T dibLen = handle ? GlobalSize(handle) : 0;
    if (!dib || dibLen < sizeof(BITMAPINFOHEADER)) {
        if (dib) GlobalUnlock(handle);
        return;
    }
    const auto* info = (const BITMAPINFOHEADER*)dib;
    if (info->biSize < sizeof(BITMAPINFOHEADER) || info->biSize > dibLen) {
        GlobalUnlock(handle);
        return;
    }
    DWORD colors = info->biClrUsed;
    if (!colors && info->biBitCount <= 8) {
        colors = 1u << info->biBitCount;
    }
    DWORD masks =
        info->biCompression == BI_BITFIELDS && info->biSize == sizeof(*info)
            ? 3u * sizeof(DWORD)
            : 0u;
    SIZE_T pixels = sizeof(BITMAPFILEHEADER) + info->biSize + masks +
                    (SIZE_T)colors * sizeof(RGBQUAD);
    SIZE_T total = sizeof(BITMAPFILEHEADER) + dibLen;
    if (pixels > total || total > INT_MAX) {
        GlobalUnlock(handle);
        return;
    }
    auto* bytes = (uint8_t*)Alloc(a, (int)total);
    if (bytes) {
        BITMAPFILEHEADER file = {};
        file.bfType = 0x4d42;
        file.bfSize = (DWORD)total;
        file.bfOffBits = (DWORD)pixels;
        memcpy(bytes, &file, sizeof(file));
        memcpy(bytes + sizeof(file), dib, dibLen);
        out->imageBytes = bytes;
        out->imageBytesLen = (int)total;
    }
    GlobalUnlock(handle);
}

static Str ClipboardReadPaths(Arena* a, HANDLE handle) {
    HDROP drop = (HDROP)handle;
    UINT count = drop ? DragQueryFileW(drop, 0xffffffffu, nullptr, 0) : 0;
    StrBuilder paths(a);
    for (UINT i = 0; i < count; i++) {
        UINT chars = DragQueryFileW(drop, i, nullptr, 0);
        WCHAR* wide = (WCHAR*)Alloc(a, (int)((chars + 1) * sizeof(WCHAR)));
        if (!wide || DragQueryFileW(drop, i, wide, chars + 1) != chars) {
            continue;
        }
        int bytes = WideCharToMultiByte(CP_UTF8, 0, wide, (int)chars, nullptr,
                                        0, nullptr, nullptr);
        if (bytes <= 0) continue;
        if (paths.len > 0) paths.AppendChar('\n');
        int at = paths.len;
        paths.Reserve(at + bytes + 1);
        if (!paths.els) continue;
        WideCharToMultiByte(CP_UTF8, 0, wide, (int)chars, paths.els + at, bytes,
                            nullptr, nullptr);
        paths.len += bytes;
        paths.els[paths.len] = 0;
    }
    return paths.TakeStr();
}

ClipboardItem PlatClipboardGetItem(Arena* a, Window* win) {
    ClipboardItem out;
    out.text = PlatClipboardGetText(a, win);
    if (!OpenClipboard(Hwnd(win))) {
        return out;
    }
    HANDLE dib = GetClipboardData(CF_DIBV5);
    if (!dib) dib = GetClipboardData(CF_DIB);
    ClipboardReadDib(a, dib, &out);
    out.externalPaths = ClipboardReadPaths(a, GetClipboardData(CF_HDROP));
    CloseClipboard();
    return out;
}

// ─── app lifecycle ────────────────────────────────────────────────────────

// ─── waking the loop ──────────────────────────────────────────────────────
//
// A message-only window of its own, the way SumatraPDF's uitask does it,
// rather than PostThreadMessage to the main thread. A thread message is only
// ever seen by the one GetMessage call that pulls it off the queue, and every
// modal loop Windows runs for us — TrackPopupMenu, the resize loop, a drag —
// throws it away. A posted window message survives all of those: their
// DispatchMessage finds the window and calls the proc below.

static HWND gWakeHwnd = nullptr;
static UINT gWakeMsg = 0;
static const wchar_t* kWakeClass = L"GpuiExecDispatch";

static LRESULT CALLBACK WakeProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == gWakeMsg && gWakeMsg != 0) {
        ExecDrain();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void WakeInit() {
    if (gWakeHwnd) {
        return;
    }
    // A registered window message rather than WM_APP+n: nothing else can be
    // using it, whatever else ends up on this queue.
    gWakeMsg = RegisterWindowMessageW(L"GpuiExecDrain");
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {sizeof(wc)};
        wc.lpfnWndProc = WakeProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kWakeClass;
        RegisterClassExW(&wc);
        registered = true;
    }
    gWakeHwnd = CreateWindowExW(0, kWakeClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE,
                                nullptr, GetModuleHandleW(nullptr), nullptr);
}

static void WakeShutdown() {
    if (!gWakeHwnd) {
        return;
    }
    DestroyWindow(gWakeHwnd);
    gWakeHwnd = nullptr;
}

void PlatWake(App* app) {
    (void)app;
    HWND hwnd = gWakeHwnd;
    if (hwnd) {
        PostMessageW(hwnd, gWakeMsg, 0, 0);
    }
}

// update_ime_position: the candidate list follows a caret that moved
// without a composition message to carry it.
static void UpdateImePosition(Window* win) {
    if (!win || !win->input || !win->input->focused) {
        return;
    }
    HWND hwnd = Hwnd(win);
    if (!hwnd) {
        return;
    }
    HIMC imc = ImmGetContext(hwnd);
    if (!imc) {
        return;
    }
    ImeMoveCandidateWindow(win, imc);
    ImmReleaseContext(hwnd, imc);
}

bool PlatInit(App* app) {
    (void)app;
    PlatInstallImePositionUpdate(UpdateImePosition);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    // RegisterDragDrop requires OLE. CoInitializeEx above already started
    // the apartment; OleInitialize attaches OLE to it.
    OleInitialize(nullptr);

    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        typedef BOOL(WINAPI * SetDpiFn)(HANDLE);
        auto setDpi =
            (SetDpiFn)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
        if (setDpi) {
            setDpi((HANDLE)-4); // PER_MONITOR_AWARE_V2
        }
    }

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {sizeof(wc)};
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        wc.lpfnWndProc = WndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        // Null, so the default handler does not reset the pointer to an
        // arrow on every move; WM_SETCURSOR above owns it.
        wc.hCursor = nullptr;
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        wc.lpszClassName = kWndClass;
        RegisterClassExW(&wc);
        registered = true;
    }
    WakeInit();
    return true;
}

void PlatShutdown(App* app) {
    (void)app;
    WakeShutdown();
    DragOutDisconnectLast();
    OleUninitialize();
    CoUninitialize();
}

Window* WindowOpen(App* app, Str title, int dipW, int dipH, WinOpts opts) {
    Window* win = WindowAlloc(app, opts);
    if (!win) {
        return nullptr;
    }
    win->plat = new PlatWindow();

    DWORD style = WS_OVERLAPPEDWINDOW;
    if (!opts.resizable) {
        style &= ~WS_THICKFRAME;
    }
    if (ClientDecorated(win)) {
        // No caption, but every other part of a normal frame: the thick
        // frame keeps the resize borders, the drop shadow, snapping and the
        // minimize / maximize animations. WM_NCCALCSIZE above then pulls the
        // client area up over the band the caption would have used. A window
        // that is not resizable keeps the rest and drops the thick frame.
        style = WS_OVERLAPPEDWINDOW & ~WS_CAPTION;
        style |= WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
        if (opts.resizable) {
            style |= WS_THICKFRAME;
        }
    }
    // Bounds::centered: the primary display's visible bounds — the work area
    // clear of the taskbar — is what the window is centred in and clamped
    // to, not the whole display.
    RECT wa = {0, 0, GetSystemMetrics(SM_CXSCREEN),
               GetSystemMetrics(SM_CYSCREEN)};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int sx = wa.right - wa.left;
    int sy = wa.bottom - wa.top;
    float scale = SystemDpi() / 96.f;
    WindowClampToDisplay(&dipW, &dipH, (int)((float)sx / scale),
                         (int)((float)sy / scale));
    RECT wr = {0, 0, (LONG)lroundf((float)dipW * scale),
               (LONG)lroundf((float)dipH * scale)};
    AdjustWindowRectEx(&wr, style, FALSE, 0);
    int pxW = wr.right - wr.left;
    int pxH = wr.bottom - wr.top;
    int x = wa.left + (sx - pxW) / 2;
    int y = wa.top + (sy - pxH) / 2;
    // -gpui-window: open where the caller asked instead of centred at the
    // caller's size. The numbers are the outer window rect, so they are the
    // same ones MoveWindow and GetWindowRect use.
    WindowGeomRequested(&x, &y, &pxW, &pxH);

    // WS_EX_NOREDIRECTIONBITMAP, the way GPUI's window.rs creates its windows:
    // without it the window keeps a GDI redirection surface the system fills
    // white, and DWM composites that surface — not the flip chain — whenever
    // the two part company for a moment, which is the white flash a scroll or
    // a click could show, and reliably did over RDP. Every frame here goes
    // through the flip-model swap chain, so nothing ever draws on the surface
    // this drops.
    HWND hwnd = CreateWindowExW(
        WS_EX_NOREDIRECTIONBITMAP, kWndClass, ToCWstrTemp(title), style, x, y,
        pxW, pxH, nullptr, nullptr, GetModuleHandleW(nullptr), win);
    if (!hwnd) {
        delete win->plat;
        win->plat = nullptr;
        return nullptr;
    }

    ConfigureDwmDarkMode(hwnd);
    if (ClientDecorated(win)) {
        // Creation only asks WM_NCCALCSIZE the wParam == FALSE question,
        // which cannot say where the client area goes. SWP_FRAMECHANGED is
        // what makes Windows ask the real one, so the caption band the
        // handler above reclaims is gone before the window is ever shown.
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                         SWP_NOACTIVATE);
    }
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    return win;
}

int AppRun(App* app) {
    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (app) {
        app->exitCode = (int)msg.wParam;
    }
    return (int)msg.wParam;
}

// Every desktop clipboard is read synchronously, so ClipboardGetItem is the
// real read and there is nothing to fall back to.
bool ClipboardReadAsync(Window* win, ClipboardReadFn done, void* data) {
    (void)win;
    (void)done;
    (void)data;
    return false;
}

} // namespace gpui

// The process entry point. Examples implement GpuiMain(argc, argv) and never
// see wWinMain or a UTF-16 command line.
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argc = 0;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!wargv || argc <= 0) {
        char* argv0 = (char*)"gpui";
        char* argv[2] = {argv0, nullptr};
        return GpuiMain(1, argv);
    }
    // One UTF-8 block for the strings plus the pointer array; both live for
    // the whole process, so nothing frees them.
    auto** argv = (char**)calloc((size_t)argc + 1, sizeof(char*));
    if (!argv) {
        return 1;
    }
    for (int i = 0; i < argc; i++) {
        int n = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, nullptr, 0,
                                    nullptr, nullptr);
        if (n <= 0) {
            n = 1;
        }
        auto* buf = (char*)calloc((size_t)n, 1);
        if (!buf) {
            return 1;
        }
        WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, buf, n, nullptr, nullptr);
        argv[i] = buf;
    }
    LocalFree(static_cast<void*>(wargv));
    argc = gpui::GpuiTakeRuntimeArgs(argc, argv);
    return GpuiMain(argc, argv);
}
