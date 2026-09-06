/* The Win32 window: message loop, chrome hit-testing, timers, clipboard, and
   the process entry point. Everything it decides is delegated to
   WindowCommon.cpp. */

#include "gpui/platform.h"
#include "gpui/paint.h"
#include "gpui/accessibility_win.h"
#include "sys/executor.h"

#include <dwmapi.h>
#include <imm.h>
#include <ole2.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <uiautomation.h>
#include <math.h>

namespace gpui {

static const wchar_t* kWndClass = L"GpuiSystemMonitor";

struct PlatWindow {
    HWND hwnd = nullptr;
    // Installed when component::Root first renders (with a WM_GETOBJECT
    // fallback for a custom root). Nodes retain it, and close detaches the
    // Window before the last COM reference can disappear.
    WinAccessibility* accessibility = nullptr;
    // WM_SETCURSOR fires on every move over the client area and has to put it
    // back, so the window remembers what it last asked for.
    HCURSOR cursor = nullptr;
    // WM_MOUSEACTIVATE said this next press is the one that activated the
    // window: MouseDownEvent::first_mouse.
    bool firstMouse = false;
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
    float x = in->lastBounds.x + in->caretX - in->scrollX;
    float y = in->lastBounds.y - in->scrollY;
    // The render target is made at 96 dpi, so a DIP is a client pixel and
    // there is nothing to scale.
    POINT pt = {(LONG)x, (LONG)(y + in->lastLineH)};
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
        int n = ImeStringUtf8(imc, GCS_RESULTSTR, buf.s, buf.len + 1);
        if (n > 0) {
            // The commit replaces the marked run, which replace_text_in_range
            // does for a null range, and clears the mark with it.
            InputReplaceTextInRange(in, win->app, win, nullptr, Str(buf.s, n));
        } else {
            InputUnmarkText(in, win->app, win);
        }
    }
    if (lParam & GCS_COMPSTR) {
        int n = ImeStringUtf8(imc, GCS_COMPSTR, buf.s, buf.len + 1);
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
    static LARGE_INTEGER freq = {};
    static LARGE_INTEGER start = {};
    if (freq.QuadPart == 0) {
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&start);
    }
    LARGE_INTEGER now = {};
    QueryPerformanceCounter(&now);
    return (double)(now.QuadPart - start.QuadPart) / (double)freq.QuadPart;
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
    UINT dpi = getDpiForWindow != kNoDpiFn ? getDpiForWindow(hwnd) : 96;
    if (dpi == 0) {
        dpi = 96;
    }
    return (float)dpi;
}

static void RenderFrame(Window* win) {
    HWND hwnd = Hwnd(win);
    if (!hwnd || IsIconic(hwnd)) {
        return;
    }
    RECT rc = {};
    GetClientRect(hwnd, &rc);
    // The render target is created at 96 dpi, so a DIP is a pixel.
    win->paint.dpi = 96;
    WINDOWPLACEMENT wp = {sizeof(wp)};
    GetWindowPlacement(hwnd, &wp);
    win->maximized = wp.showCmd == SW_SHOWMAXIMIZED;
    int pxW = rc.right - rc.left;
    int pxH = rc.bottom - rc.top;
    WindowDrawFrame(win, hwnd, pxW, pxH, (float)pxW, (float)pxH);
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
            return 0;
        }
        case WM_GETOBJECT:
            if ((LONG)lParam == UiaRootObjectId) {
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
                          WinDown());
            return 0;
        case WM_SYSKEYDOWN: {
            // Rust GPUI's translate_accelerator routes both WM_KEYDOWN and
            // WM_SYSKEYDOWN through one key dispatcher. Keep an unhandled
            // system chord for DefWindowProc (Alt+F4/menu behavior), but
            // consume a binding such as Alt+Up before Windows takes it.
            bool alt = AltDown() || (lParam & (1ll << 29)) != 0;
            win->eatSysChar = false;
            if (WindowKeyDown(win, (int)wParam, ShiftDown(), CtrlDown(), alt,
                              WinDown())) {
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
            // The press that follows is the one that activated the window.
            win->plat->firstMouse = true;
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
            if (ClientDecorated(win) && !IsZoomed(hwnd) && pt.y < BorderYPx()) {
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
            // The capture went somewhere else — a title-bar drag handing off
            // to DefWindowProc, or the system taking it — so the button
            // coming back up will never reach this window. End the press
            // where it was rather than leaving it down for good.
            if (win->mouseDown) {
                PlatformInput in =
                    InputMouseUp(win->pressedButton, win->mouseX, win->mouseY,
                                 ModsNow(), WindowCurrentClickCount(win));
                WindowDispatchInput(win, &in);
            }
            return 0;
        }
        case WM_MOUSEMOVE: {
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
            MouseExited(win);
            break;
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK:
            MouseDown(win, MouseButton::Left, lParam);
            return 0;
        case WM_LBUTTONUP:
            MouseUp(win, MouseButton::Left, lParam);
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
                SetCursor(win->plat->cursor ? win->plat->cursor
                                            : LoadCursorW(nullptr, IDC_ARROW));
                return TRUE;
            }
            break;
        case WM_ERASEBKGND:
            return 1;
        case WM_DESTROY: {
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

void AppInvalidate(Window* win) {
    if (win) {
        win->invalidations++;
    }
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
static UINT TimerDelay(int ms) {
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
        SetTimer(hwnd, 1, TimerDelay(ms), nullptr);
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

void PlatSetCursor(Window* win, CursorKind kind) {
    if (!win || !win->plat) {
        return;
    }
    LPCWSTR name = IDC_ARROW;
    if (kind == CursorKind::IBeam) {
        name = IDC_IBEAM;
    } else if (kind == CursorKind::Pointer) {
        name = IDC_HAND;
    } else if (kind == CursorKind::ColResize) {
        name = IDC_SIZEWE;
    } else if (kind == CursorKind::RowResize) {
        name = IDC_SIZENS;
    } else if (kind == CursorKind::Crosshair) {
        name = IDC_CROSS;
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

// One icon as a bitmap the menu can show: the SVG rasterized into a 32-bit
// top-down DIB, premultiplied, so Windows blends it over whatever it draws
// behind the row.
static HBITMAP MenuIconBitmap(Window* win, const char* path, int px,
                              Rgba color) {
    if (!win || !path || !path[0] || px <= 0) {
        return nullptr;
    }
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = px;
    bi.bmiHeader.biHeight = -px;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp =
        CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp || !bits) {
        if (bmp) {
            DeleteObject(bmp);
        }
        return nullptr;
    }
    if (!SvgRasterize(win->paint.pa, Str(path), px, color, (uint8_t*)bits)) {
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
        if (it.iconPath) {
            HBITMAP bmp = MenuIconBitmap(win, it.iconPath, iconPx, iconColor);
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
    for (int i = 0; i < gMenuBitmaps.len; i++) {
        DeleteObject(gMenuBitmaps[i]);
    }
    VecReset(gMenuBitmaps);
    return selected;
}

bool PlatHasAppMenu() {
    // Windows puts an application's menus in its own window, not in a bar the
    // shell keeps: a menu bar here is a row of the window, which is what
    // component::AppMenuBar draws. Nothing to install.
    return false;
}

void PlatSetAppMenu(App* app, const PlatMenuItem* items, int n) {
    (void)app;
    (void)items;
    (void)n;
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
    if (!url.s || url.len <= 0) {
        return;
    }
    ShellExecuteW(nullptr, L"open", ToCWstrTemp(url), nullptr, nullptr,
                  SW_SHOWNORMAL);
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
    dlg->SetOptions(flags | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    if (opts.title.len > 0) {
        dlg->SetTitle(ToCWstrTemp(opts.title));
    }
    HWND owner = win && win->plat ? win->plat->hwnd : nullptr;
    hr = dlg->Show(owner);
    TempStr result;
    if (SUCCEEDED(hr)) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item)) && item) {
            PWSTR wide = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &wide)) &&
                wide) {
                int n = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0,
                                            nullptr, nullptr);
                if (n > 1) {
                    result = AllocStrTemp(n - 1);
                    if (result.s) {
                        int wrote =
                            WideCharToMultiByte(CP_UTF8, 0, wide, -1, result.s,
                                                n, nullptr, nullptr);
                        if (wrote != n) {
                            result = {};
                        }
                    }
                }
                CoTaskMemFree(wide);
            }
            item->Release();
        }
    }
    dlg->Release();
    return result;
}

void ClipboardSetText(Window* win, Str text) {
    if (!text.s || text.len <= 0) {
        return;
    }
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

void WindowSetTextContentType(Window* win, Str value) {
    (void)win;
    (void)value;
}

Str ClipboardGetText(Arena* a, Window* win) {
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

bool PlatInit(App* app) {
    (void)app;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

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
    CoUninitialize();
}

Window* WindowOpen(App* app, Str title, int dipW, int dipH, WinOpts opts) {
    Window* win = WindowAlloc(app, opts);
    if (!win) {
        return nullptr;
    }
    win->plat = new PlatWindow();

    DWORD style = WS_OVERLAPPEDWINDOW;
    if (ClientDecorated(win)) {
        // No caption, but every other part of a normal frame: the thick
        // frame keeps the resize borders, the drop shadow, snapping and the
        // minimize / maximize animations. WM_NCCALCSIZE above then pulls the
        // client area up over the band the caption would have used.
        style = WS_OVERLAPPEDWINDOW & ~WS_CAPTION;
        style |= WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
    }
    int sx = GetSystemMetrics(SM_CXSCREEN);
    int sy = GetSystemMetrics(SM_CYSCREEN);
    WindowClampToDisplay(&dipW, &dipH, sx, sy);
    RECT wr = {0, 0, dipW, dipH};
    AdjustWindowRectEx(&wr, style, FALSE, 0);
    int pxW = wr.right - wr.left;
    int pxH = wr.bottom - wr.top;
    int x = (sx - pxW) / 2;
    int y = (sy - pxH) / 2;
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
