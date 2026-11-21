/* The X11 window: event loop, chrome routing, timers, clipboard, and the
   process entry point. The mirror of Window_win.cpp; everything either of
   them decides is delegated to WindowCommon.cpp.

   Like the Direct2D target, the cairo target runs at 96 dpi, so one DIP is
   one device pixel and no coordinate here is scaled. */

#include "gpui/platform.h"
#include "gpui/paint.h"
#include "gpui/accessibility_linux.h"
#include "gpui/window_loop.h"
#include "sys/clock.h"
#include "sys/executor.h"
#include "wry/wry.h"

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/XF86keysym.h>
#include <X11/cursorfont.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <cairo/cairo.h>
#include <cairo/cairo-xlib.h>
#include <locale.h>
#include <fcntl.h>
#include <poll.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>

#if defined(GPUI_HAVE_XCURSOR) && GPUI_HAVE_XCURSOR
#include <X11/Xcursor/Xcursor.h>
#endif

namespace gpui {

// X11's `Window` is an unsigned long and its `Display` is a struct; inside
// namespace gpui both unqualified names are ours — `Window` and the `Display`
// style enum — so the X ones are always spelled ::Window and ::Display.
using XWindow = ::Window;
using XDisplay = ::Display;

struct PlatWindow {
    XWindow xwin = 0;
    cairo_surface_t* surf = nullptr; // the window itself
    cairo_surface_t* back = nullptr; // what a frame is drawn into
    XIC xic = nullptr;
    // The preedit string as the input method has built it up so far. XIM
    // hands over edits to it rather than the whole thing, so the client is
    // the one that keeps it. 512 bytes is more than anybody composes at once.
    char preedit[512] = {};
    int preeditLen = 0;
    int preeditCaret = 0;
    // The key whose auto-repeat release OnKeyRelease just dropped: the press
    // that follows it is the repeat, which GPUI calls is_held.
    unsigned int repeatKeycode = 0;
    int pxW = 0;
    int pxH = 0;
    // Monotonic deadline for the next tick; 0 when the timer is off.
    double nextTick = 0;
    // -1, or the _NET_WM_MOVERESIZE direction the pointer is currently over.
    // The shared move handler only asks for arrow or I-beam, so the edge
    // cursor has to be put back by hand once the pointer leaves the band, and
    // put back up again whenever that arrow/I-beam choice changed underneath
    // it.
    int edge = -1;
    CursorKind edgeUnder = CursorKind::Arrow;
    WindowButtonSupport buttonSupport;
    // Xdnd. The source window, whether it offered text/uri-list, and whether
    // the paths have been read yet. A drop can arrive before that read.
    XWindow xdndSource = 0;
    bool xdndUri = false;
    bool xdndInside = false;
    bool xdndAsked = false;
    bool xdndDrop = false;
    int xdndX = 0;
    int xdndY = 0;
    // The outer frame last seen, to tell a ConfigureNotify that moved or
    // sized the window from one that did not.
    ScreenBounds lastFrame = {};
    // WindowSetFrameBounds asked for `requested`; a window manager can land
    // it a few pixels off, which is not a move to report. `settling` lasts
    // until the frame is there, the user takes over, or 8 configures pass.
    ScreenBounds requested = {};
    bool settling = false;
    int settleCount = 0;
    // A move or resize reported with a button down; the end is reported once
    // the button is up.
    bool moveEndPending = false;
};

// One display per process. GPUI's App is a singleton in practice, and an X
// connection is the one piece of state every window here shares.
static XDisplay* gDpy = nullptr;
static App* gX11App = nullptr;
static WindowButtonLayout gButtonLayout = {true};
static int gScreen = 0;
static XWindow gRoot = 0;
static XIM gXim = nullptr;
static Str gClipboard = {};
// what ClipboardSetImage put there, served as image/png. Owning the selection
// with one clears the other.
static Str gClipboardPng = {};
// what ClipboardSetPaths put there: the paths, also served as text
static Str gClipboardPaths = {};
static bool gClipboardCut = false;

static Atom aWmDeleteWindow, aWmProtocols, aNetWmName, aUtf8String;
static Atom aNetWmState, aNetWmStateMaxVert, aNetWmStateMaxHorz;
static Atom aNetWmStateFullscreen, aNetWmStateHidden, aWmState;
static Atom aNetFrameExtents;
static Atom aNetAllowedActions, aNetActionMinimize, aNetActionMaxHorz,
    aNetActionMaxVert;
static Atom aNetWmMoveResize, aMotifWmHints, aGtkShowWindowMenu;
static Atom aGtkEdgeConstraints;
static Atom aClipboard, aTargets, aClipTarget, aIncr;
static Atom aImagePng, aImageJpeg, aImageBmp, aImageTiff;
static Atom aGnomeCopiedFiles;
static Atom aXdndAware, aXdndEnter, aXdndPosition, aXdndStatus, aXdndLeave;
static Atom aXdndDrop, aXdndFinished, aXdndSelection, aXdndTypeList;
static Atom aXdndActionCopy, aTextUriList, aXdndProp;

double TimeNow() {
    double simulated = 0;
    if (TestPlatformNow(&simulated)) {
        return simulated;
    }
    return ClockMonotonicSecondsSinceStart();
}

static Window* FindWindow(App* app, XWindow xwin) {
    if (!app) {
        return nullptr;
    }
    for (int i = 0; i < app->windows.len; i++) {
        Window* w = app->windows[i];
        if (w->plat && w->plat->xwin == xwin) {
            return w;
        }
    }
    return nullptr;
}

// ─── drawing ──────────────────────────────────────────────────────────────

static void EnsureSurfaces(Window* win) {
    PlatWindow* pw = win->plat;
    if (!pw || pw->pxW <= 0 || pw->pxH <= 0) {
        return;
    }
    if (!pw->surf) {
        pw->surf = cairo_xlib_surface_create(
            gDpy, pw->xwin, DefaultVisual(gDpy, gScreen), pw->pxW, pw->pxH);
    } else {
        cairo_xlib_surface_set_size(pw->surf, pw->pxW, pw->pxH);
    }
    if (pw->back) {
        if (cairo_image_surface_get_width(pw->back) != pw->pxW ||
            cairo_image_surface_get_height(pw->back) != pw->pxH) {
            cairo_surface_destroy(pw->back);
            pw->back = nullptr;
        }
    }
    if (!pw->back) {
        pw->back =
            cairo_image_surface_create(CAIRO_FORMAT_RGB24, pw->pxW, pw->pxH);
    }
}

static void Redraw(Window* win) {
    PlatWindow* pw = win->plat;
    if (!pw) {
        return;
    }
    win->dirty = false;
    EnsureSurfaces(win);
    if (!pw->surf || !pw->back) {
        return;
    }
    win->paint.dpi = 96;
    WindowDrawFrame(win, pw->back, pw->pxW, pw->pxH, (float)pw->pxW,
                    (float)pw->pxH);
    // Blit the finished frame in one operation, so a slow frame never shows
    // half-drawn.
    cairo_t* cr = cairo_create(pw->surf);
    cairo_set_source_surface(cr, pw->back, 0, 0);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    cairo_destroy(cr);
    cairo_surface_flush(pw->surf);
    XFlush(gDpy);
}

// ─── window state ─────────────────────────────────────────────────────────

// EWMH capability changes arrive as property notifications. Older window
// managers leave the property absent, so the default controls remain available.
static void ReadWindowButtonSupport(PlatWindow* pw) {
    Atom type = 0;
    int format = 0;
    unsigned long count = 0, after = 0;
    unsigned char* data = nullptr;
    WindowButtonSupport support;
    if (XGetWindowProperty(gDpy, pw->xwin, aNetAllowedActions, 0, 64, False,
                           XA_ATOM, &type, &format, &count, &after,
                           &data) == Success &&
        type == XA_ATOM && format == 32) {
        support.minimize = false;
        bool maxH = false, maxV = false;
        const auto* actions = (const Atom*)data;
        for (unsigned long i = 0; i < count; i++) {
            support.minimize |= actions[i] == aNetActionMinimize;
            maxH |= actions[i] == aNetActionMaxHorz;
            maxV |= actions[i] == aNetActionMaxVert;
        }
        support.maximize = maxH && maxV;
    }
    if (data) XFree(data);
    pw->buttonSupport = support;
}

WindowButtonSupport WindowGetButtonSupport(Window* win) {
    return win && win->plat ? win->plat->buttonSupport : WindowButtonSupport{};
}

// ICCCM WM_STATE: the window manager sets IconicState on an iconified window,
// for one that doesn't set _NET_WM_STATE_HIDDEN.
static bool ReadIconic(PlatWindow* pw) {
    Atom type = 0;
    int format = 0;
    unsigned long n = 0, after = 0;
    unsigned char* data = nullptr;
    if (XGetWindowProperty(gDpy, pw->xwin, aWmState, 0, 2, False, aWmState,
                           &type, &format, &n, &after, &data) != Success) {
        return false;
    }
    bool iconic =
        data && format == 32 && n >= 1 && ((long*)data)[0] == IconicState;
    if (data) {
        XFree(data);
    }
    return iconic;
}

// _NET_WM_STATE: maximized is both MAXIMIZED atoms; an EWMH window manager
// sets HIDDEN on an iconified (minimized) window
static void ReadWmState(Window* win) {
    PlatWindow* pw = win->plat;
    win->maximized = false;
    win->minimized = false;
    if (!pw) {
        return;
    }
    Atom type = 0;
    int format = 0;
    unsigned long n = 0, after = 0;
    unsigned char* data = nullptr;
    if (XGetWindowProperty(gDpy, pw->xwin, aNetWmState, 0, 32, False, XA_ATOM,
                           &type, &format, &n, &after, &data) != Success) {
        return;
    }
    bool vert = false;
    bool horz = false;
    if (data) {
        auto* atoms = (Atom*)data;
        for (unsigned long i = 0; i < n; i++) {
            if (atoms[i] == aNetWmStateMaxVert) {
                vert = true;
            }
            if (atoms[i] == aNetWmStateMaxHorz) {
                horz = true;
            }
            if (atoms[i] == aNetWmStateHidden) {
                win->minimized = true;
            }
        }
        XFree(data);
    }
    win->maximized = vert && horz;
    if (!win->minimized) {
        win->minimized = ReadIconic(pw);
    }
}

// Zed's X11 platform reads Mutter's `_GTK_EDGE_CONSTRAINTS` property. Its
// low eight bits alternate tiled/resizable for top, right, bottom and left;
// these are exactly the four booleans GPUI exposes as Tiling. A manager that
// does not publish the extension falls back to EWMH maximization below.
static Tiling ReadTiling(Window* win, bool maximized) {
    Tiling out = {};
    PlatWindow* pw = win ? win->plat : nullptr;
    if (pw) {
        Atom type = 0;
        int format = 0;
        unsigned long n = 0, after = 0;
        unsigned char* data = nullptr;
        if (XGetWindowProperty(gDpy, pw->xwin, aGtkEdgeConstraints, 0, 1, False,
                               XA_CARDINAL, &type, &format, &n, &after,
                               &data) == Success) {
            if (data && type == XA_CARDINAL && format == 32 && n >= 1) {
                unsigned long bits = ((unsigned long*)data)[0];
                out.top = (bits & (1ul << 0)) != 0;
                out.right = (bits & (1ul << 2)) != 0;
                out.bottom = (bits & (1ul << 4)) != 0;
                out.left = (bits & (1ul << 6)) != 0;
            }
            if (data) {
                XFree(data);
            }
        }
    }
    if (maximized) {
        out.top = out.bottom = out.left = out.right = true;
    }
    return out;
}

// A request about `win` to the window manager: a 32-bit ClientMessage of
// type `type` carrying l0..l4, sent to the root window the way EWMH asks.
static void SendWmMessage(Window* win, Atom type, long l0, long l1, long l2,
                          long l3, long l4) {
    PlatWindow* pw = win->plat;
    if (!pw) {
        return;
    }
    XEvent ev = {};
    ev.type = ClientMessage;
    ev.xclient.window = pw->xwin;
    ev.xclient.message_type = type;
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = l0;
    ev.xclient.data.l[1] = l1;
    ev.xclient.data.l[2] = l2;
    ev.xclient.data.l[3] = l3;
    ev.xclient.data.l[4] = l4;
    XSendEvent(gDpy, gRoot, False,
               SubstructureNotifyMask | SubstructureRedirectMask, &ev);
    XFlush(gDpy);
}

static void SendWmState(Window* win, Atom a, Atom b, int action) {
    // action: 0 remove, 1 add, 2 toggle. The last 1 says the request comes
    // from a normal application.
    SendWmMessage(win, aNetWmState, action, (long)a, (long)b, 1, 0);
}

static void SetUndecorated(XWindow xwin) {
    // The Motif hint is what every reasonable window manager still honours
    // for "no frame, but a normal window".
    struct MotifHints {
        unsigned long flags;
        unsigned long functions;
        unsigned long decorations;
        long input_mode;
        unsigned long status;
    };
    MotifHints hints = {};
    hints.flags = 2; // MWM_HINTS_DECORATIONS
    hints.decorations = 0;
    XChangeProperty(gDpy, xwin, aMotifWmHints, aMotifWmHints, 32,
                    PropModeReplace, (unsigned char*)&hints, 5);
}

// _NET_WM_WINDOW_TYPE and the initial _NET_WM_STATE for a window's kind.
static void SetWindowType(XWindow xwin, const WinOpts& opts) {
    const char* type = nullptr;
    if (opts.kind == WindowKind::Dialog) {
        type = "_NET_WM_WINDOW_TYPE_DIALOG";
    } else if (opts.utility) {
        type = "_NET_WM_WINDOW_TYPE_UTILITY";
    }
    if (type) {
        Atom typeProp = XInternAtom(gDpy, "_NET_WM_WINDOW_TYPE", False);
        Atom value = XInternAtom(gDpy, type, False);
        XChangeProperty(gDpy, xwin, typeProp, XA_ATOM, 32, PropModeReplace,
                        (unsigned char*)&value, 1);
    }
    if (opts.kind == WindowKind::PopUp) {
        Atom states[2] = {
            XInternAtom(gDpy, "_NET_WM_STATE_ABOVE", False),
            XInternAtom(gDpy, "_NET_WM_STATE_SKIP_TASKBAR", False)};
        XChangeProperty(gDpy, xwin, aNetWmState, XA_ATOM, 32, PropModeReplace,
                        (unsigned char*)states, 2);
    }
}

// ─── input translation ────────────────────────────────────────────────────

static int KeyFor(KeySym ks) {
    switch (ks) {
        case XK_BackSpace:
            return KeyBack;
        case XK_Tab:
        case XK_ISO_Left_Tab:
            return KeyTab;
        case XK_Return:
        case XK_KP_Enter:
            return KeyReturn;
        case XK_Shift_L:
        case XK_Shift_R:
            return KeyShift;
        case XK_Control_L:
        case XK_Control_R:
            return KeyControl;
        case XK_Alt_L:
        case XK_Alt_R:
            return KeyAlt;
        case XK_Escape:
            return KeyEscape;
        case XK_space:
            return KeySpace;
        case XK_Prior:
        case XK_KP_Prior:
            return KeyPageUp;
        case XK_Next:
        case XK_KP_Next:
            return KeyPageDown;
        case XK_End:
        case XK_KP_End:
            return KeyEnd;
        case XK_Home:
        case XK_KP_Home:
            return KeyHome;
        case XK_Left:
        case XK_KP_Left:
            return KeyLeft;
        case XK_Up:
        case XK_KP_Up:
            return KeyUp;
        case XK_Right:
        case XK_KP_Right:
            return KeyRight;
        case XK_Down:
        case XK_KP_Down:
            return KeyDown;
        case XK_Insert:
        case XK_KP_Insert:
            return KeyInsert;
        case XK_Delete:
        case XK_KP_Delete:
            return KeyDelete;
        case XK_Menu:
            return KeyApps;
        case XF86XK_Back:
            return KeyBrowserBack;
        case XF86XK_Forward:
            return KeyBrowserForward;
        case XF86XK_AudioMute:
            return KeyVolumeMute;
        case XF86XK_AudioLowerVolume:
            return KeyVolumeDown;
        case XF86XK_AudioRaiseVolume:
            return KeyVolumeUp;
        case XF86XK_AudioNext:
            return KeyMediaNextTrack;
        case XF86XK_AudioPrev:
            return KeyMediaPrevTrack;
        case XF86XK_AudioStop:
            return KeyMediaStop;
        // one key on most keyboards, and Win32 has one code for both
        case XF86XK_AudioPlay:
        case XF86XK_AudioPause:
            return KeyMediaPlayPause;
        case XF86XK_Cut:
            return KeyCut;
        case XF86XK_Copy:
            return KeyCopy;
        case XF86XK_Paste:
            return KeyPaste;
        case XF86XK_New:
            return KeyNew;
        case XF86XK_Open:
            return KeyOpen;
        case XF86XK_Save:
            return KeySave;
        case XK_minus:
        case XK_underscore:
            return KeyMinus;
        case XK_equal:
        case XK_plus:
            return KeyEqual;
        case XK_bracketleft:
        case XK_braceleft:
            return KeyLeftBracket;
        case XK_bracketright:
        case XK_braceright:
            return KeyRightBracket;
        case XK_backslash:
        case XK_bar:
            return KeyBackslash;
        case XK_semicolon:
        case XK_colon:
            return KeySemicolon;
        case XK_apostrophe:
        case XK_quotedbl:
            return KeyQuote;
        case XK_comma:
        case XK_less:
            return KeyComma;
        case XK_period:
        case XK_greater:
            return KeyPeriod;
        case XK_slash:
        case XK_question:
            return KeySlash;
        case XK_grave:
        case XK_asciitilde:
            return KeyBacktick;
        case XK_exclam:
            return '1';
        case XK_at:
            return '2';
        case XK_numbersign:
            return '3';
        case XK_dollar:
            return '4';
        case XK_percent:
            return '5';
        case XK_asciicircum:
            return '6';
        case XK_ampersand:
            return '7';
        case XK_asterisk:
            return '8';
        case XK_parenleft:
            return '9';
        case XK_parenright:
            return '0';
        case XK_KP_Add:
            return KeyKpAdd;
        case XK_KP_Subtract:
            return KeyKpSubtract;
        case XK_KP_Multiply:
            return KeyKpMultiply;
        case XK_KP_Divide:
            return KeyKpDivide;
        case XK_KP_Decimal:
            return KeyKpDecimal;
        case XK_KP_Separator:
            return KeyKpSeparator;
        case XK_KP_Equal:
            return KeyKpEqual;
        case XK_KP_Begin:
            return KeyKpBegin;
        case XK_KP_Space:
            return KeySpace;
        case XK_KP_Tab:
            return KeyTab;
        default:
            break;
    }
    if (ks >= XK_F1 && ks <= XK_F24) {
        return KeyF1 + (int)(ks - XK_F1);
    }
    if (ks >= XK_F25 && ks <= XK_F35) {
        return KeyF25 + (int)(ks - XK_F25);
    }
    if (ks >= XK_KP_F1 && ks <= XK_KP_F4) {
        return KeyF1 + (int)(ks - XK_KP_F1);
    }
    if (ks >= XK_KP_0 && ks <= XK_KP_9) {
        return KeyNumpad0 + (int)(ks - XK_KP_0);
    }
    // Letters and digits carry their ASCII uppercase code, as VK_* does.
    if (ks >= XK_a && ks <= XK_z) {
        return (int)(ks - XK_a) + 'A';
    }
    if (ks >= XK_A && ks <= XK_Z) {
        return (int)(ks - XK_A) + 'A';
    }
    if (ks >= XK_0 && ks <= XK_9) {
        return (int)(ks - XK_0) + '0';
    }
    return 0;
}

// --- input method ---------------------------------------------------------
//
// On-the-spot preedit: the input method hands over what it is composing and
// the field draws it, so the candidate lands in the field's own font and
// scrolls with the rest of the document. XIM builds the string by edits
// rather than sending it whole, so the window keeps it and replays the whole
// of it into the field on every change — which is what the mark is for.

// The byte offset of character `chars` into a UTF-8 run. XIM counts in
// characters; everything above here counts in bytes.
static int Utf8ByteOfChar(const char* s, int len, int chars) {
    int i = 0;
    int c = 0;
    while (i < len && c < chars) {
        uint32_t cp = 0;
        int adv = Utf8At(Str(s, len), i, &cp);
        if (adv <= 0) {
            break;
        }
        i += adv;
        c++;
    }
    return i;
}

// Push what the window has accumulated into the focused field.
static void PreeditApply(Window* win) {
    PlatWindow* pw = win->plat;
    InputState* in = win->input;
    if (!in || !in->focused) {
        return;
    }
    if (pw->preeditLen == 0) {
        InputReplaceAndMarkText(in, win->app, win, nullptr, Str{}, nullptr);
    } else {
        int caret =
            Utf8ByteOfChar(pw->preedit, pw->preeditLen, pw->preeditCaret);
        Selection sel = {caret, caret};
        InputReplaceAndMarkText(in, win->app, win, nullptr,
                                Str(pw->preedit, pw->preeditLen), &sel);
    }
    AppInvalidate(win);
}

static int PreeditStart(XIC, XPointer, XPointer) {
    // -1: no limit on how long the composition may get.
    return -1;
}

static void PreeditDone(XIC, XPointer client, XPointer) {
    Window* win = (Window*)client;
    if (!win || !win->plat) {
        return;
    }
    win->plat->preeditLen = 0;
    win->plat->preeditCaret = 0;
    if (win->input) {
        InputUnmarkText(win->input, win->app, win);
        AppInvalidate(win);
    }
}

// A composition with no focused field to land in is dropped rather than
// kept: what the window accumulated would otherwise be pushed whole into
// the next field that takes focus.
static bool PreeditHasField(Window* win) {
    if (win->input && win->input->focused) {
        return true;
    }
    win->plat->preeditLen = 0;
    win->plat->preeditCaret = 0;
    return false;
}

static void PreeditDraw(XIC, XPointer client, XPointer call) {
    Window* win = (Window*)client;
    auto* d = (XIMPreeditDrawCallbackStruct*)call;
    if (!win || !win->plat || !d) {
        return;
    }
    if (!PreeditHasField(win)) {
        return;
    }
    PlatWindow* pw = win->plat;
    // The replacement, decoded to UTF-8. A multibyte text is already in the
    // locale's encoding, which XSetLocaleModifiers has made UTF-8.
    TempStr ins = AllocStrTemp(511);
    int insLen = 0;
    if (d->text && d->text->length > 0) {
        if (d->text->encoding_is_wchar) {
            const wchar_t* w = d->text->string.wide_char;
            for (int i = 0; w && i < d->text->length && insLen < len(ins) - 8;
                 i++) {
                insLen += Utf8Encode(ins.s + insLen, (uint32_t)w[i]);
            }
        } else if (d->text->string.multi_byte) {
            const char* m = d->text->string.multi_byte;
            insLen = (int)strlen(m);
            if (insLen > len(ins)) {
                insLen = len(ins);
            }
            memcpy(ins.s, m, (size_t)insLen);
        }
    }
    int from = Utf8ByteOfChar(pw->preedit, pw->preeditLen, d->chg_first);
    int to = Utf8ByteOfChar(pw->preedit, pw->preeditLen,
                            d->chg_first + d->chg_length);
    if (to < from) {
        to = from;
    }
    int tail = pw->preeditLen - to;
    if (from + insLen + tail > (int)sizeof(pw->preedit)) {
        // More than anybody composes; drop the change rather than overrun.
        return;
    }
    memmove(pw->preedit + from + insLen, pw->preedit + to, (size_t)tail);
    memcpy(pw->preedit + from, ins.s, (size_t)insLen);
    pw->preeditLen = from + insLen + tail;
    pw->preeditCaret = d->caret;
    PreeditApply(win);
}

static void PreeditCaret(XIC, XPointer client, XPointer call) {
    Window* win = (Window*)client;
    auto* c = (XIMPreeditCaretCallbackStruct*)call;
    if (!win || !win->plat || !c) {
        return;
    }
    if (!PreeditHasField(win)) {
        return;
    }
    win->plat->preeditCaret = c->position;
    PreeditApply(win);
}

static void OnKeyPress(Window* win, XKeyEvent* ke) {
    PlatWindow* pw = win->plat;
    // The press after a dropped auto-repeat release is the repeat. Zed's X11
    // client drops the same release but reports every press as fresh; the
    // pair says which it is, so this one is held.
    KeyDownFlags flags;
    if (pw) {
        flags.held = pw->repeatKeycode == ke->keycode;
        pw->repeatKeycode = 0;
    }
    char buf[64] = {};
    KeySym ks = 0;
    int n = 0;
    if (pw && pw->xic) {
        int st = 0;
        n = Xutf8LookupString(pw->xic, ke, buf, (int)sizeof(buf) - 1, &ks, &st);
        if (st == XLookupNone) {
            return;
        }
        if (st != XLookupChars && st != XLookupBoth) {
            n = 0;
        }
    } else {
        n = XLookupString(ke, buf, (int)sizeof(buf) - 1, &ks, nullptr);
    }
    bool shift = (ke->state & ShiftMask) != 0;
    bool ctrl = (ke->state & ControlMask) != 0;
    bool alt = (ke->state & Mod1Mask) != 0;
    // Mod4 is Super, which is what `cmd-` names on this platform.
    bool platform = (ke->state & Mod4Mask) != 0;

    int key = KeyFor(ks);
    // is_ime_in_progress: a key that would type, with nothing typed and no
    // modifier that makes it a chord — the input method or a dead key has it.
    flags.imeInProgress =
        KeyIsPrintable(key) && n <= 0 && !ctrl && !alt && !platform;
    if (key) {
        WindowKeyDown(win, key, shift, ctrl, alt, platform, false, flags);
    }
    // Windows delivers backspace as WM_CHAR 8, and the bound InputState edits
    // on that; X11 only reports the keysym, so raise it here.
    if (key == KeyBack) {
        WindowChar(win, 8, ctrl, alt);
        return;
    }
    // Backspace and Return arrive as both a key and a control character on
    // X11; the text path only wants real typing.
    if (n <= 0 || ctrl || alt || key == KeyReturn || key == KeyTab ||
        key == KeyEscape) {
        return;
    }
    int i = 0;
    while (i < n) {
        uint32_t cp = 0;
        int adv = Utf8At(Str(buf, n), i, &cp);
        if (adv <= 0) {
            break;
        }
        i += adv;
        if (cp >= 32 && cp != 127) {
            WindowChar(win, cp, ctrl, alt);
        }
    }
}

static void OnKeyRelease(Window* win, XKeyEvent* ke) {
    // X11 spells auto-repeat as a release and a press at the same timestamp,
    // so a held Enter would otherwise make a click per repeat. The pair is
    // dropped here rather than by asking XKB for detectable auto-repeat, which
    // not every server has.
    if (XPending(gDpy) > 0) {
        XEvent next;
        XPeekEvent(gDpy, &next);
        if (next.type == KeyPress && next.xkey.time == ke->time &&
            next.xkey.keycode == ke->keycode) {
            if (win->plat) {
                win->plat->repeatKeycode = ke->keycode;
            }
            return;
        }
    }
    TempStr buf = AllocStrTemp(7);
    buf.s[0] = 0;
    KeySym ks = 0;
    XLookupString(ke, buf.s, len(buf), &ks, nullptr);
    int key = KeyFor(ks);
    if (key) {
        WindowKeyUp(win, key, (ke->state & ShiftMask) != 0,
                    (ke->state & ControlMask) != 0, (ke->state & Mod1Mask) != 0,
                    (ke->state & Mod4Mask) != 0);
    }
}

// _NET_WM_MOVERESIZE: the window manager takes the pointer grab and runs the
// drag. Directions 0..7 go clockwise from the top-left corner; 8 is a move.
static const int kMoveResizeMove = 8;

static void StartMoveResize(Window* win, int rootX, int rootY, int dir) {
    if (!win->plat) {
        return;
    }
    WindowCancelPointerGesture(win);
    XUngrabPointer(gDpy, CurrentTime);
    SendWmMessage(win, aNetWmMoveResize, rootX, rootY, dir, Button1, 1);
}

static void StartMoveDrag(Window* win, int rootX, int rootY) {
    StartMoveResize(win, rootX, rootY, kMoveResizeMove);
}

// _GTK_SHOW_WINDOW_MENU: what a right click on the title bar of a
// client-decorated window asks the window manager for. Windows gets the same
// menu from DefWindowProc on WM_NCRBUTTONUP over HTCAPTION.
static void ShowWindowMenu(Window* win, int rootX, int rootY) {
    if (!win->plat) {
        return;
    }
    XUngrabPointer(gDpy, CurrentTime);
    // The first word is the device, which we do not track.
    SendWmMessage(win, aGtkShowWindowMenu, 0, rootX, rootY, 0, 0);
}

static bool ClientDecorated(Window* win) {
    return win->opts.clientTitleBar || win->opts.borderless;
}

// An undecorated window has no frame to grab, so the band around the client
// area is the resize handle — the same job WM_NCHITTEST does on Windows. The
// rule is window_border.rs's `resize_edge`, and its answers are numbered the
// way _NET_WM_MOVERESIZE numbers its directions.
static int ResizeEdge(Window* win, int x, int y) {
    PlatWindow* pw = win->plat;
    // A maximized window is tiled on every side. A window that is not
    // resizable never starts a resize, which is window_border.rs's prepaint
    // returning before resize_edge.
    if (!pw || win->maximized ||
        !component::WindowBorderOffersResize(
            ClientDecorated(win), win->opts.resizable, win->tiling)) {
        return -1;
    }
    // No shadow padding here: the frame is the window, so the inner frame is
    // its whole box and the band straddles the edge.
    Tiling tiling = win->tiling;
    float inset = win->clientInset >= 0 ? win->clientInset : 0;
    Edges insets = component::WindowBorderInsets(inset, tiling);
    return (int)component::WindowResizeEdge((float)x, (float)y, (float)pw->pxW,
                                            (float)pw->pxH, insets, tiling,
                                            win->resizeHitSize);
}

static void SetEdgeCursor(Window* win, int dir) {
    PlatWindow* pw = win->plat;
    if (!pw || (pw->edge == dir && pw->edgeUnder == win->cursor)) {
        return;
    }
    pw->edge = dir;
    pw->edgeUnder = win->cursor;
    if (dir < 0) {
        // The shared move handler picked this one before the band overrode it.
        PlatSetCursor(win, win->cursor);
        return;
    }
    static const unsigned kShapes[8] = {XC_top_left_corner,     XC_top_side,
                                        XC_top_right_corner,    XC_right_side,
                                        XC_bottom_right_corner, XC_bottom_side,
                                        XC_bottom_left_corner,  XC_left_side};
    // The server owns cursors; one per shape is all this needs.
    static ::Cursor cache[8] = {};
    if (!cache[dir]) {
        cache[dir] = XCreateFontCursor(gDpy, kShapes[dir]);
    }
    XDefineCursor(gDpy, pw->xwin, cache[dir]);
    XFlush(gDpy);
}

// ─── clipboard ────────────────────────────────────────────────────────────

void PlatSetMouseCapture(Window* win, bool capture) {
    if (!win || !win->plat || !gDpy) {
        return;
    }
    if (!capture) {
        XUngrabPointer(gDpy, CurrentTime);
        XFlush(gDpy);
        return;
    }
    // owner_events True keeps the ordinary delivery for events inside the
    // window and adds the ones outside it, which is the whole point: a drag
    // that leaves still reports its moves and its release.
    XGrabPointer(gDpy, win->plat->xwin, True,
                 ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                 GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
    XFlush(gDpy);
}

#if defined(GPUI_HAVE_XCURSOR) && GPUI_HAVE_XCURSOR
static void CursorImageFreeX(void* plat) {
    if (gDpy) {
        XFreeCursor(gDpy, (::Cursor)(uintptr_t)plat);
    }
}

// libXcursor makes an ARGB cursor (XRender) where the server has one; it
// takes premultiplied ARGB words.
static ::Cursor CursorImageX(CursorImage* img) {
    if (!img || img->plat) {
        return img ? (::Cursor)(uintptr_t)img->plat : 0;
    }
    XcursorImage* xi = XcursorImageCreate(img->w, img->h);
    if (!xi) {
        return 0;
    }
    xi->xhot = (XcursorDim)img->hotX;
    xi->yhot = (XcursorDim)img->hotY;
    int n = img->w * img->h;
    for (int i = 0; i < n; i++) {
        const uint8_t* p = img->rgba + i * 4;
        xi->pixels[i] = ((XcursorPixel)p[3] << 24) |
                        ((XcursorPixel)p[0] << 16) | ((XcursorPixel)p[1] << 8) |
                        p[2];
    }
    ::Cursor cursor = XcursorImageLoadCursor(gDpy, xi);
    XcursorImageDestroy(xi);
    if (cursor) {
        img->plat = (void*)(uintptr_t)cursor;
        img->platFree = CursorImageFreeX;
    }
    return cursor;
}
#else
// No libXcursor at build time: the element's CursorKind shows.
static ::Cursor CursorImageX(CursorImage*) {
    return 0;
}
#endif

void PlatSetCursor(Window* win, CursorKind kind) {
    if (!win || !win->plat || !gDpy) {
        return;
    }
    if (!win->cursorHidden) {
        if (::Cursor image = CursorImageX(win->cursorImage)) {
            XDefineCursor(gDpy, win->plat->xwin, image);
            XFlush(gDpy);
            return;
        }
    }
    if (win->cursorHidden) {
        static ::Cursor hidden = 0;
        if (!hidden) {
            char bits[1] = {0};
            Pixmap bitmap =
                XCreateBitmapFromData(gDpy, win->plat->xwin, bits, 1, 1);
            XColor black = {};
            hidden =
                XCreatePixmapCursor(gDpy, bitmap, bitmap, &black, &black, 0, 0);
            XFreePixmap(gDpy, bitmap);
        }
        XDefineCursor(gDpy, win->plat->xwin, hidden);
        XFlush(gDpy);
        return;
    }
    // The server owns these; one of each per process is all this needs. The
    // cursor font's nearest glyph for each of gpui's shapes, in CursorKind
    // order.
    static const unsigned int shapes[(int)CursorKind::Count] = {
        XC_left_ptr,          // Arrow
        XC_xterm,             // IBeam
        XC_hand2,             // Pointer
        XC_sb_h_double_arrow, // ColResize
        XC_sb_v_double_arrow, // RowResize
        XC_crosshair,         // Crosshair
        XC_fleur,             // ClosedHand
        XC_hand1,             // OpenHand
        XC_left_side,         // ResizeLeft
        XC_right_side,        // ResizeRight
        XC_sb_h_double_arrow, // ResizeLeftRight
        XC_top_side,          // ResizeUp
        XC_bottom_side,       // ResizeDown
        XC_sb_v_double_arrow, // ResizeUpDown
        XC_top_left_corner,   // ResizeUpLeftDownRight
        XC_top_right_corner,  // ResizeUpRightDownLeft
        XC_xterm,             // IBeamVertical
        XC_X_cursor,          // NotAllowed
        XC_hand2,             // DragLink
        XC_plus,              // DragCopy
        XC_left_ptr,          // ContextMenu
    };
    static ::Cursor cursors[(int)CursorKind::Count] = {};
    int index = (int)kind;
    if (index < 0 || index >= (int)CursorKind::Count) {
        index = 0;
    }
    if (!cursors[index]) {
        cursors[index] = XCreateFontCursor(gDpy, shapes[index]);
    }
    ::Cursor want = cursors[index];
    XDefineCursor(gDpy, win->plat->xwin, want);
    XFlush(gDpy);
}

int PlatDoubleClickMs() {
    // X11 keeps no double-click interval of its own; 400 ms is what GTK and
    // Qt default to.
    return 400;
}

// Upstream asks Wayland's outputs; this window is X11, whose core protocol
// has no refresh rate to give (RandR would, and is a library this tree does
// not take on). No query, so no cap.
uint64_t PlatWindowDisplay(Window*) {
    return 0;
}

double PlatDisplayRefreshPeriod(uint64_t) {
    return 0;
}

// Only macOS has an NSWindow to teach; VoiceOver reaches the tree here through
// the platform's own means.
void* PlatWindowHandle(Window* win) {
    // The X11 window id, widened to a pointer: there is no handle here, only
    // a number the display names the window by.
    if (!win || !win->plat) {
        return nullptr;
    }
    return (void*)(uintptr_t)win->plat->xwin;
}

void PlatInstallAccessibilityHitTest(Window* win) {
    (void)win;
}

void PlatAccessibilityTreeChanged(Window* win) {
    AccessibilityLinuxTreeChanged(win);
}

void PlatAccessibilityFocusChanged(Window* win, int focusId) {
    AccessibilityLinuxFocusChanged(win, focusId);
}

// AT-SPI clients query over D-Bus without announcing themselves first.
bool PlatAccessibilityWanted(Window* win) {
    return win != nullptr;
}

Point AccessibilityLinuxWindowOrigin(Window* win) {
    Point result = {};
    if (!gDpy || !win || !win->plat) {
        return result;
    }
    XWindow child = 0;
    int x = 0;
    int y = 0;
    if (XTranslateCoordinates(gDpy, win->plat->xwin, gRoot, 0, 0, &x, &y,
                              &child)) {
        result.x = (float)x;
        result.y = (float)y;
    }
    return result;
}

// cx.open_url. xdg-open is the desktop's own answer to "what opens this";
// the fork keeps a browser that takes its time from holding up the frame, and
// the child replaces itself so nothing here waits on it.
// The XDG desktop portal answers reduced-motion asynchronously over D-Bus
// (org.freedesktop.appearance). The synchronous read has nothing to say.
bool PlatReduceMotion() {
    return false;
}

bool PlatReduceMotionKnown(bool* out) {
    (void)out;
    return false;
}

#include <gio/gio.h>
#include <pthread.h>

using ReduceMotionFn = void (*)(bool);
static ReduceMotionFn gReduceMotionOnChange = nullptr;
static bool gHaveReduceMotion = false;
static bool gReduceMotion = false;
static bool gPortalSettingsStarted = false;
static const char kPortalWmPreferences[] = "org.gnome.desktop.wm.preferences";
static const char kPortalAppearance[] = "org.freedesktop.appearance";

struct PortalPreferences {
    bool motionChanged = false;
    bool buttonsChanged = false;
    bool reduce = false;
    WindowButtonLayout layout;
};

static Mutex gPortalPendingLock;
static PortalPreferences gPortalPending;

static void ApplyPortalPreferences() {
    gPortalPendingLock.Lock();
    PortalPreferences preference = gPortalPending;
    gPortalPending = {};
    gPortalPendingLock.Unlock();
    if (preference.motionChanged) {
        gHaveReduceMotion = true;
        gReduceMotion = preference.reduce;
        if (gReduceMotionOnChange) gReduceMotionOnChange(gReduceMotion);
    }
    if (preference.buttonsChanged) {
        gButtonLayout = preference.layout;
        if (gX11App) {
            for (Window* win : gX11App->windows) AppInvalidate(win);
        }
    }
}

static void PostPortalPreference(const char* ns, const char* key,
                                 GVariant* value) {
    if (!ns || !key || !value) return;
    GVariant* inner = g_variant_ref(value);
    while (g_variant_is_of_type(inner, G_VARIANT_TYPE_VARIANT)) {
        GVariant* next = g_variant_get_variant(inner);
        g_variant_unref(inner);
        inner = next;
    }
    PortalPreferences preference;
    if (strcmp(ns, kPortalAppearance) == 0 &&
        strcmp(key, "reduced-motion") == 0 &&
        g_variant_is_of_type(inner, G_VARIANT_TYPE_UINT32)) {
        preference.motionChanged = true;
        preference.reduce = g_variant_get_uint32(inner) == 1;
    } else if (strcmp(ns, kPortalWmPreferences) == 0 &&
               strcmp(key, "button-layout") == 0 &&
               g_variant_is_of_type(inner, G_VARIANT_TYPE_STRING)) {
        preference.layout.physicalSides = true;
        WindowButtonLayoutParse(Str(g_variant_get_string(inner, nullptr)),
                                &preference.layout);
        preference.buttonsChanged = true;
    }
    g_variant_unref(inner);
    if (!preference.motionChanged && !preference.buttonsChanged) return;
    gPortalPendingLock.Lock();
    if (preference.motionChanged) {
        gPortalPending.motionChanged = true;
        gPortalPending.reduce = preference.reduce;
    }
    if (preference.buttonsChanged) {
        gPortalPending.buttonsChanged = true;
        gPortalPending.layout = preference.layout;
    }
    gPortalPendingLock.Unlock();
    ExecPost(MkFunc0Void(&ApplyPortalPreferences));
}

static void OnPortalSettingChanged(GDBusConnection*, const gchar*, const gchar*,
                                   const gchar*, const gchar*, GVariant* params,
                                   gpointer) {
    const gchar* ns = nullptr;
    const gchar* key = nullptr;
    GVariant* value = nullptr;
    g_variant_get(params, "(&s&sv)", &ns, &key, &value);
    PostPortalPreference(ns, key, value);
    if (value) g_variant_unref(value);
}

static void ReadPortalSetting(GDBusProxy* proxy, const char* ns,
                              const char* key) {
    GVariant* result =
        g_dbus_proxy_call_sync(proxy, "ReadOne", g_variant_new("(ss)", ns, key),
                               G_DBUS_CALL_FLAGS_NONE, 2000, nullptr, nullptr);
    if (!result) {
        result = g_dbus_proxy_call_sync(
            proxy, "Read", g_variant_new("(ss)", ns, key),
            G_DBUS_CALL_FLAGS_NONE, 2000, nullptr, nullptr);
    }
    if (!result) return;
    GVariant* value = nullptr;
    g_variant_get(result, "(v)", &value);
    PostPortalPreference(ns, key, value);
    if (value) g_variant_unref(value);
    g_variant_unref(result);
}

static void* FollowPortalSettings(void*) {
    GMainContext* context = g_main_context_new();
    g_main_context_push_thread_default(context);
    GDBusProxy* proxy = g_dbus_proxy_new_for_bus_sync(
        G_BUS_TYPE_SESSION, G_DBUS_PROXY_FLAGS_NONE, nullptr,
        "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
        "org.freedesktop.portal.Settings", nullptr, nullptr);
    if (proxy) {
        GDBusConnection* conn = g_dbus_proxy_get_connection(proxy);
        unsigned int watch = g_dbus_connection_signal_subscribe(
            conn, "org.freedesktop.portal.Desktop",
            "org.freedesktop.portal.Settings", "SettingChanged",
            "/org/freedesktop/portal/desktop", nullptr,
            G_DBUS_SIGNAL_FLAGS_NONE, OnPortalSettingChanged, nullptr, nullptr);
        ReadPortalSetting(proxy, kPortalWmPreferences, "button-layout");
        ReadPortalSetting(proxy, kPortalAppearance, "reduced-motion");
        GMainLoop* loop = g_main_loop_new(context, FALSE);
        g_main_loop_run(loop);
        g_main_loop_unref(loop);
        g_dbus_connection_signal_unsubscribe(conn, watch);
        g_object_unref(proxy);
    }
    g_main_context_pop_thread_default(context);
    g_main_context_unref(context);
    return nullptr;
}

static void EnsurePortalSettings() {
    if (gPortalSettingsStarted) return;
    pthread_t thread = {};
    if (pthread_create(&thread, nullptr, FollowPortalSettings, nullptr) == 0) {
        gPortalSettingsStarted = true;
        pthread_detach(thread);
    }
}

WindowButtonLayout WindowGetButtonLayout() {
    if (gX11App) EnsurePortalSettings();
    return gButtonLayout;
}

void PlatReduceMotionFollow(void (*onChange)(bool reduce)) {
    if (!onChange || gReduceMotionOnChange) return;
    gReduceMotionOnChange = onChange;
    if (gHaveReduceMotion) onChange(gReduceMotion);
    EnsurePortalSettings();
}

// Hand `target` to xdg-open and wait only for the intermediate child: the
// grandchild is orphaned deliberately, so nobody is left to reap it and
// nothing here waits on the program it opens.
static void XdgOpen(const char* target) {
    pid_t pid = fork();
    if (pid == 0) {
        if (fork() == 0) {
            execlp("xdg-open", "xdg-open", target, (char*)nullptr);
            _exit(127);
        }
        _exit(0);
    }
    if (pid > 0) {
        int st = 0;
        waitpid(pid, &st, 0);
    }
}

void OpenUrl(Str url) {
    if (!url.s || len(url) <= 0 || OpenExternalHooked(url, false)) {
        return;
    }
    TempStr value = StrDupTemp(len(url) < 1023 ? url : Str(url.s, 1023));
    XdgOpen(value.s);
}

void OpenPath(Str path) {
    if (!path.s || len(path) <= 0 || OpenExternalHooked(path, true)) {
        return;
    }
    // The whole path. A file name past 1023 bytes is still the file the
    // script named; truncating it would open something else. The grandchild
    // has its own copy at the second fork, so the parent frees after the
    // intermediate exits.
    Str value = StrDup(path);
    XdgOpen(value.s);
    StrFree(value);
}

// One helper's command line, built in the temp arena before the fork.
struct DialogArgv {
    static constexpr int kMax = 64;
    const char* argv[kMax + 1] = {};
    int n = 0;

    void Add(const char* arg) {
        if (n < kMax) {
            argv[n++] = arg;
        }
    }
    void Add(Str arg) { Add(StrDupTemp(arg).s); }
};

// The filters in the order the dialog lists them: the one to show first
// leads, as zenity and kdialog select the first.
static int32_t DialogFilterAt(const PathFilters& filters, int32_t i) {
    int32_t first =
        filters.index >= 0 && filters.index < filters.count ? filters.index : 0;
    if (i == 0) {
        return first;
    }
    return i <= first ? i - 1 : i;
}

// zenity's --file-filter "NAME | *.pdf *.xps" each, and kdialog's one
// "*.pdf *.xps|NAME" a line.
static void DialogAddFilters(DialogArgv* zenity, StrBuilder* kdialog,
                             const PathFilters& filters) {
    for (int32_t i = 0; i < filters.count; i++) {
        const PathFilter& f = filters.items[DialogFilterAt(filters, i)];
        TempStr pats =
            StrDupTemp(StrReplaceAll(f.patterns, StrL(";"), StrL(" ")));
        zenity->Add(fmt("--file-filter=%s | %s", f.name, pats));
        if (len(*kdialog) > 0) {
            kdialog->AppendChar('\n');
        }
        kdialog->Append(fmt("%s|%s", pats, f.name));
    }
}

// Where the dialog starts: the folder with a trailing '/', so zenity opens
// it rather than selecting it, then the name.
static TempStr DialogStartTemp(Str dir, Str name) {
    if (len(dir) == 0) {
        return len(name) > 0 ? StrDupTemp(name) : TempStr{};
    }
    bool slash = dir.s[len(dir) - 1] == '/';
    return fmt("%s%s%s", dir, slash ? StrL("") : StrL("/"), name);
}

// Runs zenity, or kdialog when zenity is not on the PATH (the exec that fails
// falls through to the next), and answers what it printed without the
// trailing newline: nothing when the user cancelled.
static TempStr RunPathDialogTemp(DialogArgv* zenity, DialogArgv* kdialog,
                                 int cap) {
    int fds[2] = {-1, -1};
    if (pipe(fds) != 0) {
        return {};
    }
    pid_t pid = fork();
    if (pid == 0) {
        dup2(fds[1], STDOUT_FILENO);
        close(fds[0]);
        close(fds[1]);
        execvp("zenity", (char* const*)zenity->argv);
        execvp("kdialog", (char* const*)kdialog->argv);
        _exit(127);
    }
    close(fds[1]);
    if (pid < 0) {
        close(fds[0]);
        return {};
    }
    TempStr result = AllocStrTemp(cap);
    if (!result.s) {
        close(fds[0]);
        int st = 0;
        waitpid(pid, &st, 0);
        return {};
    }
    int n = 0;
    for (;;) {
        ssize_t got = read(fds[0], result.s + n, (size_t)(len(result) - n));
        if (got <= 0) {
            break;
        }
        n += (int)got;
        if (n >= len(result)) {
            break;
        }
    }
    close(fds[0]);
    int st = 0;
    waitpid(pid, &st, 0);
    while (n > 0 && (result.s[n - 1] == '\n' || result.s[n - 1] == '\r')) {
        n--;
    }
    result.s[n] = 0;
    result.len = n;
    return result;
}

// cx.prompt_for_paths. X11 has no file dialog of its own and this tree has no
// toolkit to borrow one from, so it asks the desktop's: zenity on GTK
// desktops, kdialog on KDE, whichever is on the PATH. A session with neither
// answers nothing, which is what a caller has to be ready for anyway — the
// user can always cancel. Neither says which filter was left selected.
TempStr PromptForPathTemp(Window* win, const PathPrompt& opts) {
    (void)win;
    if (opts.filters.chosen) {
        *opts.filters.chosen = -1;
    }
    bool dirs = opts.directories && !opts.files;
    TempStr start =
        DialogStartTemp(opts.directory, dirs ? Str{} : opts.fileName);
    DialogArgv zenity;
    DialogArgv kdialog;
    zenity.Add("zenity");
    zenity.Add("--file-selection");
    kdialog.Add("kdialog");
    if (len(opts.title) > 0) {
        zenity.Add(fmt("--title=%s", opts.title));
        kdialog.Add("--title");
        kdialog.Add(opts.title);
    }
    if (len(start) > 0) {
        zenity.Add(fmt("--filename=%s", start));
    }
    if (dirs) {
        zenity.Add("--directory");
        kdialog.Add("--getexistingdirectory");
        kdialog.Add(len(start) > 0 ? Str(start) : StrL("."));
        return RunPathDialogTemp(&zenity, &kdialog, kMaxPath - 1);
    }
    if (opts.multiple) {
        // a path a line, as PromptForPathTemp answers several
        zenity.Add("--multiple");
        zenity.Add("--separator=\n");
        kdialog.Add("--multiple");
        kdialog.Add("--separate-output");
    }
    kdialog.Add("--getopenfilename");
    kdialog.Add(len(start) > 0 ? Str(start) : StrL("."));
    StrBuilder kfilter;
    DialogAddFilters(&zenity, &kfilter, opts.filters);
    if (len(kfilter) > 0) {
        kdialog.Add(Str(kfilter.els, len(kfilter)));
    }
    int cap = opts.multiple ? kMaxPath * 64 - 1 : kMaxPath - 1;
    return RunPathDialogTemp(&zenity, &kdialog, cap);
}

// cx.prompt_for_new_path. Rust asks the XDG desktop portal; this asks zenity
// or kdialog, as PromptForPathTemp does. Both ask before overwriting the name
// as typed; the default extension is added after that question.
TempStr PromptForNewPathTemp(Window* win, const NewPathPrompt& opts) {
    (void)win;
    if (opts.filters.chosen) {
        *opts.filters.chosen = -1;
    }
    TempStr start = DialogStartTemp(opts.directory, opts.suggestedName);
    DialogArgv zenity;
    DialogArgv kdialog;
    zenity.Add("zenity");
    zenity.Add("--file-selection");
    zenity.Add("--save");
    zenity.Add("--confirm-overwrite");
    kdialog.Add("kdialog");
    // Rust's portal request is titled "Save File"
    Str title = len(opts.title) > 0 ? opts.title : StrL("Save File");
    zenity.Add(fmt("--title=%s", title));
    kdialog.Add("--title");
    kdialog.Add(title);
    if (len(start) > 0) {
        zenity.Add(fmt("--filename=%s", start));
    }
    kdialog.Add("--getsavefilename");
    kdialog.Add(len(start) > 0 ? Str(start) : StrL("."));
    StrBuilder kfilter;
    DialogAddFilters(&zenity, &kfilter, opts.filters);
    if (len(kfilter) > 0) {
        kdialog.Add(Str(kfilter.els, len(kfilter)));
    }
    TempStr path = RunPathDialogTemp(&zenity, &kdialog, kMaxPath - 1);
    if (len(path) == 0) {
        return {};
    }
    return PathWithDefaultExtTemp(path, opts.defaultExt);
}

static void ClipboardDrop() {
    StrFree(gClipboard);
    StrFree(gClipboardPng);
    StrFree(gClipboardPaths);
    gClipboard = {};
    gClipboardPng = {};
    gClipboardPaths = {};
    gClipboardCut = false;
}

void PlatClipboardSetText(Window* win, Str text) {
    if (!win || !win->plat) {
        return;
    }
    ClipboardDrop();
    gClipboard = StrDup(text);
    XSetSelectionOwner(gDpy, aClipboard, win->plat->xwin, CurrentTime);
    XFlush(gDpy);
}

// Served in one property, without INCR: with BIG-REQUESTS (every current X
// server) that holds images of many megabytes.
// TODO: the web's prefers-color-scheme, Linux's XDG portal color-scheme
WindowAppearance PlatWindowAppearance() {
    return WindowAppearance::Light;
}

void PlatWatchWindowAppearance() {}

bool PlatClipboardSetImage(Window* win, const uint8_t* png, int pngLen) {
    if (!win || !win->plat || !gDpy) {
        return false;
    }
    ClipboardDrop();
    gClipboardPng = StrDup(Str((const char*)png, pngLen));
    XSetSelectionOwner(gDpy, aClipboard, win->plat->xwin, CurrentTime);
    XFlush(gDpy);
    return true;
}

// Newline-separated paths as file URIs, one per line, after `first`
static Str FileUrisTemp(Str paths, Str first, Str eol) {
    StrBuilder out(GetTempArena());
    out.Append(first);
    for (Str rest = paths; len(rest) > 0;) {
        int k = 0;
        while (k < len(rest) && rest.s[k] != '\n') {
            k++;
        }
        Str line(rest.s, k);
        int skip = k < len(rest) ? k + 1 : k;
        rest = Str(rest.s + skip, len(rest) - skip);
        if (len(line) == 0) {
            continue;
        }
        if (len(out) > 0 && out.els[len(out) - 1] != '\n') {
            out.Append(eol);
        }
        out.Append(StrL("file://"));
        for (int i = 0; i < len(line); i++) {
            auto c = (unsigned char)line.s[i];
            bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                         (c >= '0' && c <= '9') || c == '/' || c == '-' ||
                         c == '.' || c == '_' || c == '~';
            if (plain) {
                out.AppendChar((char)c);
            } else {
                out.Append(fmt("%%%02X", c));
            }
        }
    }
    return out.TakeStr();
}

bool PlatClipboardSetPaths(Window* win, Str paths, bool cut) {
    if (!win || !win->plat || !gDpy) {
        return false;
    }
    ClipboardDrop();
    gClipboardPaths = StrDup(paths);
    gClipboardCut = cut;
    XSetSelectionOwner(gDpy, aClipboard, win->plat->xwin, CurrentTime);
    XFlush(gDpy);
    return true;
}

void PlatClipboardClear(Window* win) {
    (void)win;
    if (!gDpy) {
        return;
    }
    ClipboardDrop();
    XSetSelectionOwner(gDpy, aClipboard, None, CurrentTime);
    XFlush(gDpy);
}

struct ClipboardTargets {
    bool valid = false;
    bool utf8 = false;
    bool text = false;
    bool uriList = false;
    bool gnomeFiles = false;
    Atom image = None;
};

struct SelectionNotifyQuery {
    XWindow xwin = None;
    Atom target = None;
};

static Bool MatchSelectionNotify(::Display*, XEvent* ev, XPointer arg) {
    auto* query = (SelectionNotifyQuery*)arg;
    return ev->type == SelectionNotify &&
           ev->xselection.requestor == query->xwin &&
           ev->xselection.selection == aClipboard &&
           ev->xselection.target == query->target;
}

static bool WaitSelectionNotify(XWindow xwin, Atom target, XEvent& ev) {
    SelectionNotifyQuery query = {xwin, target};
    double deadline = TimeNow() + 0.5;
    while (TimeNow() < deadline) {
        if (XCheckIfEvent(gDpy, &ev, MatchSelectionNotify, (XPointer)&query)) {
            return true;
        }
        struct timespec ts = {0, 2 * 1000 * 1000};
        nanosleep(&ts, nullptr);
    }
    return false;
}

static Bool MatchClipboardProperty(::Display*, XEvent* ev, XPointer arg) {
    auto xwin = *(XWindow*)arg;
    return ev->type == PropertyNotify && ev->xproperty.window == xwin &&
           ev->xproperty.atom == aClipTarget &&
           ev->xproperty.state == PropertyNewValue;
}

static bool RequestClipboardTarget(XWindow xwin, Atom target) {
    XDeleteProperty(gDpy, xwin, aClipTarget);
    XConvertSelection(gDpy, aClipboard, target, aClipTarget, xwin, CurrentTime);
    XFlush(gDpy);

    XEvent ev = {};
    if (!WaitSelectionNotify(xwin, target, ev)) {
        return false;
    }
    return ev.xselection.property != None;
}

static Str ReadClipboardTarget(Arena* a, XWindow xwin, Atom target) {
    if (!RequestClipboardTarget(xwin, target)) {
        return {};
    }
    Atom type = 0;
    int format = 0;
    unsigned long items = 0;
    unsigned long after = 0;
    unsigned char* data = nullptr;
    constexpr long kMaxClipboardLongs = 64 * 1024 * 1024;
    if (XGetWindowProperty(gDpy, xwin, aClipTarget, 0, kMaxClipboardLongs, True,
                           AnyPropertyType, &type, &format, &items, &after,
                           &data) != Success) {
        return {};
    }
    if (type != aIncr) {
        Str out = {};
        if (data && items > 0 && format == 8 && after == 0 &&
            items <= INT_MAX) {
            out = StrDup(a, Str((char*)data, (int)items));
        }
        if (data) {
            XFree(data);
        }
        return out;
    }
    if (data) {
        XFree(data);
    }

    // INCR transfers stream chunks after the requestor deletes the initial
    // property. XGetWindowProperty(..., True) above did that deletion.
    StrBuilder bytes(a);
    double deadline = TimeNow() + 5.0;
    while (TimeNow() < deadline) {
        XEvent ev = {};
        if (!XCheckIfEvent(gDpy, &ev, MatchClipboardProperty,
                           (XPointer)&xwin)) {
            struct timespec ts = {0, 2 * 1000 * 1000};
            nanosleep(&ts, nullptr);
            continue;
        }
        data = nullptr;
        items = 0;
        after = 0;
        if (XGetWindowProperty(gDpy, xwin, aClipTarget, 0, kMaxClipboardLongs,
                               True, AnyPropertyType, &type, &format, &items,
                               &after, &data) != Success) {
            return {};
        }
        if (items == 0) {
            if (data) {
                XFree(data);
            }
            return bytes.TakeStr();
        }
        bool ok = data && format == 8 && after == 0 && items <= INT_MAX &&
                  bytes.Append(Str((char*)data, (int)items));
        if (data) {
            XFree(data);
        }
        if (!ok) {
            return {};
        }
    }
    return {};
}

static ClipboardTargets ReadClipboardTargets(XWindow xwin) {
    ClipboardTargets out;
    if (!RequestClipboardTarget(xwin, aTargets)) {
        return out;
    }
    Atom type = 0;
    int format = 0;
    unsigned long items = 0;
    unsigned long after = 0;
    unsigned char* data = nullptr;
    if (XGetWindowProperty(gDpy, xwin, aClipTarget, 0, 4096, True, XA_ATOM,
                           &type, &format, &items, &after, &data) != Success) {
        return out;
    }
    if (type == XA_ATOM && format == 32 && after == 0 && data) {
        out.valid = true;
        auto* targets = (Atom*)data;
        for (unsigned long i = 0; i < items; i++) {
            Atom atom = targets[i];
            out.utf8 |= atom == aUtf8String;
            out.text |= atom == XA_STRING;
            out.uriList |= atom == aTextUriList;
            out.gnomeFiles |= atom == aGnomeCopiedFiles;
            if (atom == aImagePng ||
                (atom == aImageJpeg && out.image != aImagePng) ||
                (atom == aImageBmp && out.image != aImagePng &&
                 out.image != aImageJpeg) ||
                (atom == aImageTiff && out.image == None)) {
                out.image = atom;
            }
        }
    }
    if (data) {
        XFree(data);
    }
    return out;
}

// A paste on X11 is a round trip: ask the selection owner to write into a
// property on our window, then wait for SelectionNotify. Windows and Cocoa
// read the clipboard directly, so this platform signature hides the wait.
Str PlatClipboardGetText(Arena* a, Window* win) {
    if (!win || !win->plat || !gDpy) {
        return {};
    }
    XWindow xwin = win->plat->xwin;
    if (XGetSelectionOwner(gDpy, aClipboard) == xwin) {
        return StrDup(a, gClipboardPaths.s ? gClipboardPaths : gClipboard);
    }
    return ReadClipboardTarget(a, xwin, aUtf8String);
}

ClipboardItem PlatClipboardGetItem(Arena* a, Window* win) {
    ClipboardItem out;
    if (!win || !win->plat || !gDpy) {
        return out;
    }
    XWindow xwin = win->plat->xwin;
    if (XGetSelectionOwner(gDpy, aClipboard) == xwin) {
        out.text = StrDup(a, gClipboardPaths.s ? gClipboardPaths : gClipboard);
        if (gClipboardPaths.s) {
            out.externalPaths = StrDup(a, gClipboardPaths);
            out.externalPathsCut = gClipboardCut;
        }
        if (gClipboardPng.s) {
            Str image = StrDup(a, gClipboardPng);
            out.imageBytes = (const uint8_t*)image.s;
            out.imageBytesLen = len(image);
        }
        return out;
    }
    ClipboardTargets targets = ReadClipboardTargets(xwin);
    if (!targets.valid || targets.utf8) {
        out.text = ReadClipboardTarget(a, xwin, aUtf8String);
    } else if (targets.text) {
        out.text = ReadClipboardTarget(a, xwin, XA_STRING);
    }
    if (targets.image != None) {
        Str image = ReadClipboardTarget(a, xwin, targets.image);
        out.imageBytes = (const uint8_t*)image.s;
        out.imageBytesLen = len(image);
    }
    // "copy" or "cut", then a file URI per line
    if (targets.gnomeFiles) {
        Str list = ReadClipboardTarget(a, xwin, aGnomeCopiedFiles);
        int k = 0;
        while (k < len(list) && list.s[k] != '\n') {
            k++;
        }
        Str verb(list.s, k);
        int skip = k < len(list) ? k + 1 : k;
        out.externalPaths =
            FileUriListToPaths(a, Str(list.s + skip, len(list) - skip));
        out.externalPathsCut = len(out.externalPaths) > 0 && StrEq(verb, "cut");
    } else if (targets.uriList) {
        out.externalPaths =
            FileUriListToPaths(a, ReadClipboardTarget(a, xwin, aTextUriList));
    }
    return out;
}

bool PlatClipboardHasPaths(Window* win) {
    if (!win || !win->plat || !gDpy) {
        return false;
    }
    XWindow xwin = win->plat->xwin;
    if (XGetSelectionOwner(gDpy, aClipboard) == xwin) {
        return gClipboardPaths.s != nullptr;
    }
    ClipboardTargets targets = ReadClipboardTargets(xwin);
    return targets.gnomeFiles || targets.uriList;
}

static void OnSelectionRequest(XSelectionRequestEvent* req) {
    XEvent resp = {};
    resp.xselection.type = SelectionNotify;
    resp.xselection.requestor = req->requestor;
    resp.xselection.selection = req->selection;
    resp.xselection.target = req->target;
    resp.xselection.time = req->time;
    resp.xselection.property = None;

    Atom prop = req->property ? req->property : req->target;
    Str text = gClipboardPaths.s ? gClipboardPaths : gClipboard;
    if (req->target == aTargets) {
        Atom targets[4] = {aTargets, gClipboardPng.s ? aImagePng : aUtf8String,
                           aTextUriList, aGnomeCopiedFiles};
        int n = gClipboardPaths.s ? 4 : 2;
        XChangeProperty(gDpy, req->requestor, prop, XA_ATOM, 32,
                        PropModeReplace, (unsigned char*)targets, n);
        resp.xselection.property = prop;
    } else if ((req->target == aTextUriList ||
                req->target == aGnomeCopiedFiles) &&
               gClipboardPaths.s) {
        bool gnome = req->target == aGnomeCopiedFiles;
        Str first = !gnome          ? Str{}
                    : gClipboardCut ? StrL("cut\n")
                                    : StrL("copy\n");
        Str uris = FileUrisTemp(gClipboardPaths, first,
                                gnome ? StrL("\n") : StrL("\r\n"));
        XChangeProperty(gDpy, req->requestor, prop, req->target, 8,
                        PropModeReplace, (unsigned char*)uris.s, len(uris));
        resp.xselection.property = prop;
    } else if (req->target == aImagePng && gClipboardPng.s) {
        XChangeProperty(gDpy, req->requestor, prop, aImagePng, 8,
                        PropModeReplace, (unsigned char*)gClipboardPng.s,
                        len(gClipboardPng));
        resp.xselection.property = prop;
    } else if ((req->target == aUtf8String || req->target == XA_STRING) &&
               text.s) {
        XChangeProperty(gDpy, req->requestor, prop, req->target, 8,
                        PropModeReplace, (unsigned char*)text.s, len(text));
        resp.xselection.property = prop;
    }
    XSendEvent(gDpy, req->requestor, False, 0, &resp);
    XFlush(gDpy);
}

// ─── event dispatch ───────────────────────────────────────────────────────

static void DestroyPlatWindow(Window* win) {
    PlatWindow* pw = win->plat;
    if (!pw) {
        return;
    }
    // While the PlatWindow is still there to give the grab back.
    WindowModalRelease(win);
    if (pw->back) {
        cairo_surface_destroy(pw->back);
    }
    if (pw->surf) {
        cairo_surface_destroy(pw->surf);
    }
    if (pw->xic) {
        XDestroyIC(pw->xic);
    }
    XWindow xwin = pw->xwin;
    if (xwin) {
        wry::WebViewDetachX11Children((void*)(uintptr_t)xwin);
    }
    delete pw;
    WindowClosed(win);
    if (xwin) {
        XDestroyWindow(gDpy, xwin);
    }
}

// GPUI's Modifiers, out of an X state mask. Mod1 is Alt and Mod4 is Super,
// which is what `platform` means off macOS; X11 reports no Fn key.
static Modifiers ModsOf(unsigned state) {
    Modifiers m;
    m.control = (state & ControlMask) != 0;
    m.alt = (state & Mod1Mask) != 0;
    m.shift = (state & ShiftMask) != 0;
    m.platform = (state & Mod4Mask) != 0;
    return m;
}

// The X button number as a MouseButton. 4-7 are the two wheels, which are not
// buttons in GPUI's sense, so they answer false.
static bool ButtonOf(unsigned b, MouseButton* out) {
    switch (b) {
        case Button1:
            *out = MouseButton::Left;
            return true;
        case Button2:
            *out = MouseButton::Middle;
            return true;
        case Button3:
            *out = MouseButton::Right;
            return true;
        case 8:
            *out = MouseButton::NavigateBack;
            return true;
        case 9:
            *out = MouseButton::NavigateForward;
            return true;
        default:
            return false;
    }
}

// Rust's Option<MouseButton> on a move: the first button held in the mask.
static bool PressedButton(unsigned state, MouseButton* out) {
    if (state & Button1Mask) {
        *out = MouseButton::Left;
        return true;
    }
    if (state & Button2Mask) {
        *out = MouseButton::Middle;
        return true;
    }
    if (state & Button3Mask) {
        *out = MouseButton::Right;
        return true;
    }
    return false;
}

// A press of anything but the left button: it reaches the window's own
// subscription and never becomes a click, the way GPUI only turns a left
// press and release into one.
static void PressButton(Window* win, MouseButton button, float x, float y,
                        Modifiers mods) {
    PlatformInput in = InputMouseDown(
        button, x, y, mods, WindowClickCount(win, x, y, button), false);
    WindowDispatchInput(win, &in);
}

static void SendXdnd(XWindow target, Atom type, long a0, long a1, long a2,
                     long a3, long a4) {
    XEvent ev = {};
    ev.xclient.type = ClientMessage;
    ev.xclient.display = gDpy;
    ev.xclient.window = target;
    ev.xclient.message_type = type;
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = a0;
    ev.xclient.data.l[1] = a1;
    ev.xclient.data.l[2] = a2;
    ev.xclient.data.l[3] = a3;
    ev.xclient.data.l[4] = a4;
    XSendEvent(gDpy, target, False, NoEventMask, &ev);
}

static void XdndStatus(PlatWindow* pw, bool accept) {
    if (!pw->xdndSource) {
        return;
    }
    // An empty rectangle asks the source for a position on every move.
    SendXdnd(pw->xdndSource, aXdndStatus, (long)pw->xwin, accept ? 1 : 0, 0, 0,
             accept ? (long)aXdndActionCopy : 0);
}

static void XdndFinish(PlatWindow* pw, bool accepted) {
    if (!pw->xdndSource) {
        return;
    }
    SendXdnd(pw->xdndSource, aXdndFinished, (long)pw->xwin, accepted ? 1 : 0,
             accepted ? (long)aXdndActionCopy : 0, 0, 0);
}

static void XdndReset(PlatWindow* pw) {
    pw->xdndSource = 0;
    pw->xdndUri = false;
    pw->xdndInside = false;
    pw->xdndAsked = false;
    pw->xdndDrop = false;
}

static bool AtomIsUri(Atom atom) {
    return atom == aTextUriList;
}

static bool XdndTypes(XWindow source, bool more, Atom t0, Atom t1, Atom t2) {
    if (!more) {
        return AtomIsUri(t0) || AtomIsUri(t1) || AtomIsUri(t2);
    }
    Atom type = 0;
    int format = 0;
    unsigned long items = 0, after = 0;
    unsigned char* data = nullptr;
    if (XGetWindowProperty(gDpy, source, aXdndTypeList, 0, 64, False, XA_ATOM,
                           &type, &format, &items, &after, &data) != Success) {
        return false;
    }
    bool found = false;
    if (type == XA_ATOM && format == 32 && data) {
        auto* atoms = (unsigned long*)data;
        for (unsigned long i = 0; i < items; i++) {
            if (AtomIsUri((Atom)atoms[i])) {
                found = true;
                break;
            }
        }
    }
    if (data) {
        XFree(data);
    }
    return found;
}

static void XdndAsk(Window* win, Time time) {
    PlatWindow* pw = win->plat;
    if (!pw->xdndUri || pw->xdndAsked) {
        return;
    }
    pw->xdndAsked = true;
    XConvertSelection(gDpy, aXdndSelection, aTextUriList, aXdndProp, pw->xwin,
                      time ? time : CurrentTime);
}

static void XdndPosition(Window* win, int rootX, int rootY) {
    PlatWindow* pw = win->plat;
    int x = 0, y = 0;
    XWindow child = 0;
    XTranslateCoordinates(gDpy, gRoot, pw->xwin, rootX, rootY, &x, &y, &child);
    pw->xdndX = x;
    pw->xdndY = y;
    if (!pw->xdndInside) {
        return;
    }
    PlatformInput in =
        InputFileDrop(FileDropPhase::Pending, (float)x, (float)y, {});
    WindowDispatchInput(win, &in);
}

static void OnXdndSelection(Window* win, const XSelectionEvent* ev) {
    PlatWindow* pw = win->plat;
    if (!pw->xdndSource) {
        return;
    }
    Str list = {};
    if (ev->property != None) {
        Atom type = 0;
        int format = 0;
        unsigned long items = 0, after = 0;
        unsigned char* data = nullptr;
        constexpr long kMax = 1024 * 1024;
        if (XGetWindowProperty(gDpy, pw->xwin, aXdndProp, 0, kMax, True,
                               AnyPropertyType, &type, &format, &items, &after,
                               &data) == Success) {
            if (data && format == 8 && items > 0 && items <= INT_MAX) {
                Arena* scratch = ArenaNew();
                list =
                    FileUriListToPaths(scratch, Str((char*)data, (int)items));
                Str owned = len(list) > 0 ? StrDup(list) : Str{};
                PlatformInput in =
                    InputFileDrop(FileDropPhase::Entered, (float)pw->xdndX,
                                  (float)pw->xdndY, owned);
                WindowDispatchInput(win, &in);
                StrFree(owned);
                ArenaDelete(scratch);
                pw->xdndInside = true;
            }
            if (data) {
                XFree(data);
            }
        }
    }
    if (pw->xdndDrop) {
        bool accepted = pw->xdndInside;
        if (accepted) {
            PlatformInput in = InputFileDrop(
                FileDropPhase::Submit, (float)pw->xdndX, (float)pw->xdndY, {});
            WindowDispatchInput(win, &in);
        }
        XdndFinish(pw, accepted);
        XdndReset(pw);
    }
}

static void OnXdndClient(Window* win, const XClientMessageEvent* ev) {
    PlatWindow* pw = win->plat;
    if (ev->message_type == aXdndEnter) {
        XdndReset(pw);
        pw->xdndSource = (XWindow)ev->data.l[0];
        bool more = (ev->data.l[1] & 1) != 0;
        pw->xdndUri = XdndTypes(pw->xdndSource, more, (Atom)ev->data.l[2],
                                (Atom)ev->data.l[3], (Atom)ev->data.l[4]);
        return;
    }
    if (ev->message_type == aXdndPosition) {
        if ((XWindow)ev->data.l[0] != pw->xdndSource) {
            return;
        }
        int rootX = (int)((ev->data.l[2] >> 16) & 0xffff);
        int rootY = (int)(ev->data.l[2] & 0xffff);
        XdndPosition(win, rootX, rootY);
        XdndStatus(pw, pw->xdndUri);
        if (pw->xdndUri) {
            XdndAsk(win, (Time)ev->data.l[3]);
        }
        return;
    }
    if (ev->message_type == aXdndLeave) {
        if ((XWindow)ev->data.l[0] != pw->xdndSource) {
            return;
        }
        if (pw->xdndInside) {
            PlatformInput in = InputFileDrop(
                FileDropPhase::Exited, (float)pw->xdndX, (float)pw->xdndY, {});
            WindowDispatchInput(win, &in);
        }
        XdndReset(pw);
        return;
    }
    if (ev->message_type == aXdndDrop) {
        if ((XWindow)ev->data.l[0] != pw->xdndSource) {
            return;
        }
        if (pw->xdndInside) {
            PlatformInput in = InputFileDrop(
                FileDropPhase::Submit, (float)pw->xdndX, (float)pw->xdndY, {});
            WindowDispatchInput(win, &in);
            XdndFinish(pw, true);
            XdndReset(pw);
            return;
        }
        if (pw->xdndUri && !pw->xdndAsked) {
            pw->xdndDrop = true;
            XdndAsk(win, (Time)ev->data.l[2]);
            return;
        }
        if (pw->xdndAsked && !pw->xdndInside) {
            // The selection has been asked for and has not come back. The
            // notify finishes the drop.
            pw->xdndDrop = true;
            return;
        }
        XdndFinish(pw, false);
        XdndReset(pw);
    }
}

// ─── window frame ─────────────────────────────────────────────────────────

// What a decorated window adds before its window manager has said
// (_NET_FRAME_EXTENTS), and the last extents one did report: WSLg's frame is
// far from the guess, so the next window starts from what it saw.
constexpr int kTitleGuess = 28;
constexpr int kBorderGuess = 1;
struct FrameExtentsPx {
    int left = 0;
    int right = 0;
    int top = 0;
    int bottom = 0;
};
static FrameExtentsPx gSeenExtents = {};

// The extents the manager reports for `xwin`; zero until it has set them.
static FrameExtentsPx ReadFrameExtents(XWindow xwin) {
    FrameExtentsPx e;
    Atom type = 0;
    int format = 0;
    unsigned long n = 0, after = 0;
    unsigned char* data = nullptr;
    if (!xwin || XGetWindowProperty(gDpy, xwin, aNetFrameExtents, 0, 4, False,
                                    XA_CARDINAL, &type, &format, &n, &after,
                                    &data) != Success) {
        return e;
    }
    if (data && format == 32 && n >= 4) {
        auto* v = (long*)data;
        e = {(int)v[0], (int)v[1], (int)v[2], (int)v[3]};
        if (e.top > 0 || e.left > 0) {
            gSeenExtents = e;
        }
    }
    if (data) {
        XFree(data);
    }
    return e;
}

// The extents to place a window by: its own once known, else what another
// window showed, else the guess. None for a window without a frame.
static FrameExtentsPx PlacementExtents(const WinOpts& opts, XWindow xwin) {
    if (!WindowOptsTitled(opts)) {
        return {};
    }
    FrameExtentsPx e = ReadFrameExtents(xwin);
    if (e.top > 0 || e.left > 0) {
        return e;
    }
    if (gSeenExtents.top > 0 || gSeenExtents.left > 0) {
        return gSeenExtents;
    }
    return {kBorderGuess, kBorderGuess, kTitleGuess, kBorderGuess};
}

static bool PointerButtonsDown() {
    XWindow root = 0, child = 0;
    int rx = 0, ry = 0, wx = 0, wy = 0;
    unsigned mask = 0;
    if (!XQueryPointer(gDpy, gRoot, &root, &child, &rx, &ry, &wx, &wy, &mask)) {
        return false;
    }
    return (mask & (Button1Mask | Button2Mask | Button3Mask)) != 0;
}

static bool FrameNear(ScreenBounds a, ScreenBounds b) {
    auto near4 = [](int x, int y) { return x - y < 4 && y - x < 4; };
    return near4(a.x, b.x) && near4(a.y, b.y) && near4(a.w, b.w) &&
           near4(a.h, b.h);
}

// A ConfigureNotify: report a move or resize, unless it is the window
// manager settling a frame the program asked for.
static void FrameConfigured(Window* win) {
    PlatWindow* pw = win->plat;
    ScreenBounds now = WindowFrameBounds(win);
    if (now.IsEmpty() || now == pw->lastFrame) {
        return;
    }
    pw->lastFrame = now;
    bool down = PointerButtonsDown();
    if (pw->settling) {
        if (FrameNear(now, pw->requested)) {
            pw->settling = false;
            return;
        }
        if (!down && ++pw->settleCount <= 8) {
            return;
        }
        pw->settling = false;
        if (!down) {
            return;
        }
    }
    WindowBoundsNotify(win, WindowBoundsEvent::Changed);
    if (down) {
        pw->moveEndPending = true;
    }
}

// Reports the end of moves whose button has come up; true while one waits.
static bool PollMoveEnds(App* app) {
    bool waiting = false;
    for (int i = 0; i < app->windows.len; i++) {
        Window* w = app->windows[i];
        if (!w->plat || !w->plat->moveEndPending) {
            continue;
        }
        if (PointerButtonsDown()) {
            waiting = true;
            continue;
        }
        w->plat->moveEndPending = false;
        WindowBoundsNotify(w, WindowBoundsEvent::MoveResizeEnded);
    }
    return waiting;
}

// How often a pending move end is polled for.
constexpr int kMoveEndPollMs = 50;

static void HandleEvent(App* app, XEvent* ev) {
    if (ev->type == SelectionRequest) {
        OnSelectionRequest(&ev->xselectionrequest);
        return;
    }
    if (ev->type == SelectionClear) {
        if (gClipboard.s) {
            StrFree(gClipboard);
            gClipboard = {};
        }
        return;
    }
    XWindow xwin = ev->xany.window;
    Window* win = FindWindow(app, xwin);
    if (!win) {
        return;
    }
    PlatWindow* pw = win->plat;
    switch (ev->type) {
        case Expose:
            if (ev->xexpose.count == 0) {
                win->dirty = true;
            }
            break;
        case ConfigureNotify:
            if (ev->xconfigure.width != pw->pxW || ev->xconfigure
                                                           .height != pw->pxH) {
                pw->pxW = ev->xconfigure.width;
                pw->pxH = ev->xconfigure.height;
                win->dirty = true;
            }
            FrameConfigured(win);
            break;
        case PropertyNotify:
            if (ev->xproperty.atom == aNetAllowedActions) {
                ReadWindowButtonSupport(pw);
                win->dirty = true;
            }
            if (ev->xproperty.atom == aNetWmState ||
                ev->xproperty.atom == aWmState ||
                ev->xproperty.atom == aGtkEdgeConstraints) {
                ReadWmState(win);
                win->tiling = ReadTiling(win, win->maximized);
                win->dirty = true;
            }
            break;
        case KeyPress:
            OnKeyPress(win, &ev->xkey);
            break;
        case KeyRelease:
            OnKeyRelease(win, &ev->xkey);
            break;
        case MotionNotify: {
            MouseButton held = MouseButton::Left;
            bool any = PressedButton(ev->xmotion.state, &held);
            PlatformInput in =
                InputMouseMove((float)ev->xmotion.x, (float)ev->xmotion.y, any,
                               held, ModsOf(ev->xmotion.state));
            WindowDispatchInput(win, &in);
            SetEdgeCursor(win, ResizeEdge(win, ev->xmotion.x, ev->xmotion.y));
            break;
        }
        case LeaveNotify: {
            MouseButton held = MouseButton::Left;
            bool any = PressedButton(ev->xcrossing.state, &held);
            PlatformInput in =
                InputMouseExited((float)ev->xcrossing.x, (float)ev->xcrossing.y,
                                 any, held, ModsOf(ev->xcrossing.state));
            WindowDispatchInput(win, &in);
            // Whatever the pointer picks up outside, the band has to claim
            // the cursor again the next time it comes back.
            pw->edge = -1;
            break;
        }
        case ButtonPress: {
            float x = (float)ev->xbutton.x;
            float y = (float)ev->xbutton.y;
            unsigned b = ev->xbutton.button;
            Modifiers mods = ModsOf(ev->xbutton.state);
            // X11 sends the wheel as buttons 4 and 5, and the horizontal wheel
            // as 6 and 7. A notch is WheelNotchPixels(), the same step the
            // Windows window uses; positive scrolls the view up and left.
            if (b >= Button4 && b <= 7) {
                float notch = WheelNotchPixels(win->app);
                float dx = b == 6 ? notch : b == 7 ? -notch : 0.f;
                float dy = b == Button4 ? notch : b == Button5 ? -notch : 0.f;
                PlatformInput in = InputScrollWheel(x, y, dx, dy, false, mods,
                                                    TouchPhase::Moved);
                WindowDispatchInput(win, &in);
                break;
            }
            if (b == Button3) {
                // The title bar's drag region belongs to the window manager,
                // the way it does for a server-decorated one.
                if (WindowChromeHit(win, x, y) == ClickWinCaption) {
                    ShowWindowMenu(win, ev->xbutton.x_root, ev->xbutton.y_root);
                    break;
                }
                PressButton(win, MouseButton::Right, x, y, mods);
                break;
            }
            if (b == Button2) {
                PressButton(win, MouseButton::Middle, x, y, mods);
                break;
            }
            // 8 and 9 are the thumb buttons, GPUI's MouseButton::Navigate.
            if (b == 8 || b == 9) {
                PressButton(win,
                            b == 8 ? MouseButton::NavigateBack
                                   : MouseButton::NavigateForward,
                            x, y, mods);
                break;
            }
            if (b != Button1) {
                break;
            }
            // Before the chrome, because the chrome needs the answer: the
            // caption drags on the first press and zooms on the second, and
            // handing the drag to the window manager first would swallow it.
            int clicks = WindowClickCount(win, x, y, MouseButton::Left);
            // The resize band and the custom chrome are claimed before the
            // element tree sees the press, the way WM_NCHITTEST takes both
            // on Windows.
            int edge = ResizeEdge(win, ev->xbutton.x, ev->xbutton.y);
            if (edge >= 0) {
                StartMoveResize(win, ev->xbutton.x_root, ev->xbutton.y_root,
                                edge);
                break;
            }
            int chrome = WindowChromeHit(win, x, y);
            if (chrome == ClickWinMin) {
                AppMinimize(win);
                break;
            }
            if (chrome == ClickWinMax) {
                AppToggleMaximize(win);
                break;
            }
            if (chrome == ClickWinClose) {
                AppClose(win);
                break;
            }
            if (chrome == ClickWinCaption) {
                if (clicks == 2) {
                    AppToggleMaximize(win);
                } else {
                    StartMoveDrag(win, ev->xbutton.x_root, ev->xbutton.y_root);
                }
                break;
            }
            PlatformInput in =
                InputMouseDown(MouseButton::Left, x, y, mods, clicks, false);
            WindowDispatchInput(win, &in);
            break;
        }
        case ButtonRelease: {
            MouseButton button = MouseButton::Left;
            if (!ButtonOf(ev->xbutton.button, &button)) {
                break;
            }
            PlatformInput in = InputMouseUp(
                button, (float)ev->xbutton.x, (float)ev->xbutton.y,
                ModsOf(ev->xbutton.state), WindowCurrentClickCount(win));
            WindowDispatchInput(win, &in);
            break;
        }
        case FocusIn:
            WindowSetActive(win, true);
            break;
        case FocusOut:
            // NotifyInferior: the focus went into a child X window — a
            // webview (src/wry/wry_linux.cpp) — and this window, still the
            // active one, only stopped being where the keys go.
            if (ev->xfocus.detail != NotifyInferior) {
                WindowSetActive(win, false);
            }
            break;
        case SelectionNotify:
            if (ev->xselection.selection == aXdndSelection &&
                (ev->xselection.target == aTextUriList ||
                 ev->xselection.property == aXdndProp)) {
                OnXdndSelection(win, &ev->xselection);
            }
            break;
        case ClientMessage:
            if (ev->xclient.message_type == aXdndEnter ||
                ev->xclient.message_type == aXdndPosition ||
                ev->xclient.message_type == aXdndLeave ||
                ev->xclient.message_type == aXdndDrop) {
                OnXdndClient(win, &ev->xclient);
                break;
            }
            if (ev->xclient.message_type == aWmProtocols &&
                (Atom)ev->xclient.data.l[0] == aWmDeleteWindow &&
                WindowShouldClose(win)) {
                DestroyPlatWindow(win);
            }
            break;
        default:
            break;
    }
}

// ─── window commands ──────────────────────────────────────────────────────

void AppQuit(Window* win) {
    if (win && win->plat) {
        DestroyPlatWindow(win);
    }
}

bool WindowClientDecorated(Window* win) {
    if (!win || !win->plat || !ClientDecorated(win)) {
        return false;
    }
    // Asking is not getting: SetUndecorated is a Motif hint, and a window
    // manager is free to keep its frame — which it then draws its own title
    // bar and controls on. _NET_FRAME_EXTENTS is what a manager that framed
    // the window reports, so a non-zero extent means the decorations are the
    // server's after all.
    Atom type = 0;
    int format = 0;
    unsigned long n = 0, after = 0;
    unsigned char* data = nullptr;
    if (XGetWindowProperty(gDpy, win->plat->xwin, aNetFrameExtents, 0, 4, False,
                           XA_CARDINAL, &type, &format, &n, &after,
                           &data) != Success) {
        return true;
    }
    bool framed = false;
    if (data && format == 32) {
        auto* extents = (unsigned long*)data;
        for (unsigned long i = 0; i < n && i < 4; i++) {
            if (extents[i] != 0) {
                framed = true;
            }
        }
    }
    if (data) {
        XFree(data);
    }
    return !framed;
}

float WindowScaleFactor(Window*) {
    return 1;
}

// A window's last frame (its back buffer) and where it is on the root window.
static bool CaptureFrameRect(Window* w, int* x, int* y) {
    PlatWindow* pw = w ? w->plat : nullptr;
    if (!gDpy || !pw || !pw->back) {
        return false;
    }
    XWindowAttributes attrs = {};
    if (!XGetWindowAttributes(gDpy, pw->xwin, &attrs) ||
        attrs.map_state != IsViewable) {
        return false;
    }
    XWindow child = 0;
    return XTranslateCoordinates(gDpy, pw->xwin, gRoot, 0, 0, x, y, &child);
}

// The frames the app drew, without window manager decorations. With
// `allWindows` the app's other windows go over `win` in creation order, not
// z-order. WebKitGTK webviews are not in the frames.
TempStr WindowCapturePngTemp(Window* win, Str path, bool allWindows) {
    int mainX = 0;
    int mainY = 0;
    if (!CaptureFrameRect(win, &mainX, &mainY)) {
        return StrL("ERR no-window");
    }
    if (len(path) == 0) {
        return StrL("ERR no-path");
    }
    Vec<Window*> wins;
    VecAppend(wins, win);
    if (allWindows) {
        for (int i = 0; i < len(win->app->windows); i++) {
            Window* w = win->app->windows[i];
            if (w != win) {
                VecAppend(wins, w);
            }
        }
    }

    // each frame's origin, and the union of all of them
    Vec<Point> origins;
    int x0 = mainX;
    int y0 = mainY;
    int x1 = mainX + win->plat->pxW;
    int y1 = mainY + win->plat->pxH;
    Vec<Window*> shots;
    for (int i = 0; i < len(wins); i++) {
        int x = 0;
        int y = 0;
        if (!CaptureFrameRect(wins[i], &x, &y)) {
            continue;
        }
        PlatWindow* pw = wins[i]->plat;
        x0 = std::min(x0, x);
        y0 = std::min(y0, y);
        x1 = std::max(x1, x + pw->pxW);
        y1 = std::max(y1, y + pw->pxH);
        VecAppend(shots, wins[i]);
        VecAppend(origins, Point{(float)x, (float)y});
    }

    int w = x1 - x0;
    int h = y1 - y0;
    cairo_surface_t* out = cairo_image_surface_create(CAIRO_FORMAT_RGB24, w, h);
    cairo_t* cr = cairo_create(out);
    for (int i = 0; i < len(shots); i++) {
        cairo_set_source_surface(cr, shots[i]->plat->back, origins[i].x - x0,
                                 origins[i].y - y0);
        cairo_paint(cr);
    }
    cairo_destroy(cr);
    cairo_surface_flush(out);
    cairo_status_t st = cairo_surface_write_to_png(out, StrDupTemp(path).s);
    cairo_surface_destroy(out);
    if (st != CAIRO_STATUS_SUCCESS) {
        return StrL("ERR write");
    }
    return WindowCaptureOkTemp(w, h, 1, len(shots), 0);
}

void AppActivate(Window* win) {
    if (!win || !win->plat) {
        return;
    }
    XMapRaised(gDpy, win->plat->xwin);
    // The window manager may refuse this without a timestamp it likes; there
    // is nothing else a client can do about focus from here.
    XSetInputFocus(gDpy, win->plat->xwin, RevertToParent, CurrentTime);
    XFlush(gDpy);
}

void AppMinimize(Window* win) {
    if (win && win->plat) {
        XIconifyWindow(gDpy, win->plat->xwin, gScreen);
        XFlush(gDpy);
    }
}

// ─── window frame and placement ───────────────────────────────────────────

static XWindow XwinOf(Window* win) {
    return win && win->plat ? win->plat->xwin : 0;
}

// The X window is the client area; the manager's frame is around it.
ScreenBounds WindowContentBounds(Window* win) {
    XWindow xwin = XwinOf(win);
    XWindowAttributes attr = {};
    if (!xwin || !XGetWindowAttributes(gDpy, xwin, &attr)) {
        return {};
    }
    int x = 0, y = 0;
    XWindow child = 0;
    if (!XTranslateCoordinates(gDpy, xwin, gRoot, 0, 0, &x, &y, &child)) {
        return {};
    }
    return {x, y, attr.width, attr.height};
}

ScreenBounds WindowFrameBounds(Window* win) {
    ScreenBounds c = WindowContentBounds(win);
    if (c.IsEmpty()) {
        return {};
    }
    FrameExtentsPx e = ReadFrameExtents(XwinOf(win));
    return {c.x - e.left, c.y - e.top, c.w + e.left + e.right,
            c.h + e.top + e.bottom};
}

void WindowSetFrameBounds(Window* win, ScreenBounds outer) {
    XWindow xwin = XwinOf(win);
    if (!xwin || outer.IsEmpty()) {
        return;
    }
    FrameExtentsPx e = PlacementExtents(win->opts, xwin);
    int w = std::max(outer.w - e.left - e.right, 1);
    int h = std::max(outer.h - e.top - e.bottom, 1);
    win->plat->requested = outer;
    win->plat->settling = true;
    win->plat->settleCount = 0;
    XMoveResizeWindow(gDpy, xwin, outer.x + e.left, outer.y + e.top,
                      (unsigned)w, (unsigned)h);
    XFlush(gDpy);
}

ScreenBounds WindowFrameForContent(Window* nearby, const WinOpts& opts,
                                   ScreenBounds content) {
    (void)nearby;
    FrameExtentsPx e = PlacementExtents(opts, 0);
    return {content.x - e.left, content.y - e.top, content.w + e.left + e.right,
            content.h + e.top + e.bottom};
}

static void X11WorkArea(int* x, int* y, int* w, int* h);

// One X screen: _NET_WORKAREA is not per monitor.
ScreenBounds WindowDisplayBounds(Window* win) {
    (void)win;
    if (!gDpy) {
        return {};
    }
    return {0, 0, DisplayWidth(gDpy, gScreen), DisplayHeight(gDpy, gScreen)};
}

ScreenBounds WindowDisplayVisibleBounds(Window* win) {
    (void)win;
    if (!gDpy) {
        return {};
    }
    ScreenBounds r;
    X11WorkArea(&r.x, &r.y, &r.w, &r.h);
    return r;
}

// The root's child holding `xwin`: the manager's frame around it, or the
// window itself when there is no manager.
static XWindow TopLevelOf(XWindow xwin) {
    while (xwin) {
        XWindow root = 0, parent = 0;
        XWindow* kids = nullptr;
        unsigned n = 0;
        if (!XQueryTree(gDpy, xwin, &root, &parent, &kids, &n)) {
            return 0;
        }
        if (kids) {
            XFree(kids);
        }
        if (parent == root) {
            return xwin;
        }
        xwin = parent;
    }
    return 0;
}

// X11 draws in device pixels with a scale of 1, so DIPs are pixels. Asked of
// the root, XTranslateCoordinates names its topmost mapped child there.
Window* WindowAtPoint(Window* win, Point pt, Point* out) {
    XWindow xwin = XwinOf(win);
    if (!xwin) {
        return nullptr;
    }
    int ox = 0, oy = 0;
    XWindow child = 0;
    XTranslateCoordinates(gDpy, xwin, gRoot, 0, 0, &ox, &oy, &child);
    float sx = (float)ox + pt.x;
    float sy = (float)oy + pt.y;
    int rx = 0, ry = 0;
    child = 0;
    XTranslateCoordinates(gDpy, gRoot, gRoot, (int)floorf(sx), (int)floorf(sy),
                          &rx, &ry, &child);
    if (!child) {
        return nullptr;
    }
    for (Window* other : win->app->windows) {
        XWindow oxwin = XwinOf(other);
        if (!oxwin || TopLevelOf(oxwin) != child) {
            continue;
        }
        if (out) {
            int cx = 0, cy = 0;
            XWindow unused = 0;
            XTranslateCoordinates(gDpy, oxwin, gRoot, 0, 0, &cx, &cy, &unused);
            *out = {sx - (float)cx, sy - (float)cy};
        }
        return other;
    }
    return nullptr;
}

static bool XwinViewable(XWindow xwin);

void PlatWindowShow(Window* win, bool visible, bool activate) {
    XWindow xwin = XwinOf(win);
    if (!xwin) {
        return;
    }
    if (!visible) {
        XUnmapWindow(gDpy, xwin);
        XFlush(gDpy);
        return;
    }
    // Focus can go only to a viewable window (BadMatch otherwise); one being
    // mapped is focused by the window manager, as it takes input.
    bool viewable = XwinViewable(xwin);
    XMapRaised(gDpy, xwin);
    if (viewable && activate && win->opts.activatable) {
        XSetInputFocus(gDpy, xwin, RevertToParent, CurrentTime);
    }
    XFlush(gDpy);
}

bool WindowIsVisible(Window* win) {
    XWindow xwin = XwinOf(win);
    XWindowAttributes attr = {};
    if (!xwin || !XGetWindowAttributes(gDpy, xwin, &attr)) {
        return false;
    }
    return attr.map_state != IsUnmapped;
}

static bool XwinViewable(XWindow xwin) {
    XWindowAttributes attr = {};
    return xwin && XGetWindowAttributes(gDpy, xwin, &attr) &&
           attr.map_state == IsViewable;
}

static XWindow FocusedXwin() {
    XWindow focus = 0;
    int revert = 0;
    XGetInputFocus(gDpy, &focus, &revert);
    return focus;
}

// Only when one of this application's windows has the focus.
void WindowActivateInApp(Window* win) {
    XWindow xwin = XwinOf(win);
    XWindow focus = xwin ? FocusedXwin() : 0;
    if (!focus || focus == xwin || !FindWindow(win->app, focus) ||
        !XwinViewable(xwin)) {
        return;
    }
    XRaiseWindow(gDpy, xwin);
    XSetInputFocus(gDpy, xwin, RevertToParent, CurrentTime);
    XFlush(gDpy);
}

bool WindowIsActive(Window* win) {
    XWindow xwin = XwinOf(win);
    return xwin && FocusedXwin() == xwin;
}

void PlatWindowSetParent(Window* win, Window* parent) {
    XWindow xwin = XwinOf(win);
    XWindow p = XwinOf(parent);
    if (!xwin) {
        return;
    }
    if (p) {
        XSetTransientForHint(gDpy, xwin, p);
    } else {
        XDeleteProperty(gDpy, xwin, XA_WM_TRANSIENT_FOR);
    }
    XFlush(gDpy);
}

// The dialog takes the pointer and the keyboard. One grab at a time.
static XWindow gModalGrab = 0;

void PlatWindowSetModal(Window* dlg, Window* parent, bool on) {
    (void)parent;
    if (gModalGrab) {
        XUngrabPointer(gDpy, CurrentTime);
        XUngrabKeyboard(gDpy, CurrentTime);
        gModalGrab = 0;
    }
    XWindow xwin = XwinOf(dlg);
    if (on && xwin) {
        unsigned mask = ButtonPressMask | ButtonReleaseMask | PointerMotionMask;
        if (XGrabPointer(gDpy, xwin, True, mask, GrabModeAsync, GrabModeAsync,
                         0, 0, CurrentTime) == GrabSuccess) {
            XGrabKeyboard(gDpy, xwin, True, GrabModeAsync, GrabModeAsync,
                          CurrentTime);
            gModalGrab = xwin;
        }
    }
    XFlush(gDpy);
}

// WM_NORMAL_HINTS keeps the other hints the window has.
static void UpdateSizeHints(Window* win) {
    XWindow xwin = XwinOf(win);
    if (!xwin || !win->opts.resizable) {
        return;
    }
    XSizeHints hints = {};
    long supplied = 0;
    XGetWMNormalHints(gDpy, xwin, &hints, &supplied);
    const Size& mn = win->opts.minSize;
    const Size& mx = win->opts.maxSize;
    hints.flags &= ~(PMinSize | PMaxSize);
    if (mn.w > 0 && mn.h > 0) {
        hints.flags |= PMinSize;
        hints.min_width = (int)mn.w;
        hints.min_height = (int)mn.h;
    }
    if (mx.w > 0 || mx.h > 0) {
        constexpr int kUnbounded = 1 << 20;
        hints.flags |= PMaxSize;
        hints.max_width = mx.w > 0 ? (int)mx.w : kUnbounded;
        hints.max_height = mx.h > 0 ? (int)mx.h : kUnbounded;
    }
    XSetWMNormalHints(gDpy, xwin, &hints);
    XFlush(gDpy);
}

void WindowSetMinSize(Window* win, Size content) {
    if (win) {
        win->opts.minSize = content;
        UpdateSizeHints(win);
    }
}

void WindowSetMaxSize(Window* win, Size content) {
    if (win) {
        win->opts.maxSize = content;
        UpdateSizeHints(win);
    }
}

// GTK's own hint, which Mutter and other GTK-aware managers read.
void WindowSetFrameAppearance(Window* win, FrameAppearance appearance) {
    XWindow xwin = XwinOf(win);
    if (!xwin) {
        return;
    }
    Atom prop = XInternAtom(gDpy, "_GTK_THEME_VARIANT", False);
    if (appearance == FrameAppearance::System) {
        XDeleteProperty(gDpy, xwin, prop);
    } else {
        Str v =
            appearance == FrameAppearance::Dark ? StrL("dark") : StrL("light");
        XChangeProperty(gDpy, xwin, prop, aUtf8String, 8, PropModeReplace,
                        (unsigned char*)v.s, len(v));
    }
    XFlush(gDpy);
}

void AppToggleMaximize(Window* win) {
    if (win && win->plat) {
        SendWmState(win, aNetWmStateMaxVert, aNetWmStateMaxHorz, 2);
    }
}

void WindowSetFullScreen(Window* win, bool fullScreen) {
    if (win && win->plat) {
        SendWmState(win, aNetWmStateFullscreen, None, fullScreen ? 1 : 0);
    }
}

void AppWindowSetOpacity(Window* win, int percent) {
    if (!win || !win->plat) {
        return;
    }
    Atom atom = XInternAtom(gDpy, "_NET_WM_WINDOW_OPACITY", False);
    if (percent >= 100) {
        XDeleteProperty(gDpy, win->plat->xwin, atom);
    } else {
        // A CARDINAL where 0xFFFFFFFF is opaque. Format 32 is passed as longs.
        unsigned long value = (unsigned long)(0xFFFFFFFFu / 100.0 *
                                              (percent < 20 ? 20 : percent));
        XChangeProperty(gDpy, win->plat->xwin, atom, XA_CARDINAL, 32,
                        PropModeReplace, (unsigned char*)&value, 1);
    }
    XFlush(gDpy);
}

// A 32-bit visual and a compositing manager are what this would take; the
// window is made on the default visual.
bool PlatWindowCanBeTransparent() {
    return false;
}

// An empty input shape is what this would take, and the shape extension is
// not one of this tree's dependencies.
void AppWindowSetClickThrough(Window* win, bool on) {
    (void)win;
    (void)on;
}

void AppDrag(Window* win) {
    if (!win || !win->plat) {
        return;
    }
    XWindow child = 0;
    int rx = 0, ry = 0, wx = 0, wy = 0;
    unsigned mask = 0;
    XWindow rootRet = 0;
    XQueryPointer(gDpy, win->plat->xwin, &rootRet, &child, &rx, &ry, &wx, &wy,
                  &mask);
    StartMoveDrag(win, rx, ry);
}

void AppSetTitle(Window* win, Str title) {
    if (!win || !win->plat || !title.s) {
        return;
    }
    XWindow xwin = win->plat->xwin;
    // _NET_WM_NAME is the UTF-8 one modern window managers read; WM_NAME is
    // the Latin-1 fallback.
    XChangeProperty(gDpy, xwin, aNetWmName, aUtf8String, 8, PropModeReplace,
                    (unsigned char*)title.s, len(title));
    Str z = StrDup(title);
    if (z.s) {
        XStoreName(gDpy, xwin, z.s);
        StrFree(z);
    }
    XFlush(gDpy);
}

void PlatSetTimer(Window* win, int ms) {
    if (!win || !win->plat) {
        return;
    }
    win->plat->nextTick = ms > 0 ? TimeNow() + ms / 1000.0 : 0;
}

// ─── app lifecycle ────────────────────────────────────────────────────────

// ─── waking the loop ──────────────────────────────────────────────────────
//
// A pipe, because poll() below is what the loop blocks in and a pipe is a
// file descriptor it can watch alongside the X connection. The alternative —
// XSendEvent to our own window — would need a second Display connection to be
// safe from another thread, which is a heavier thing to own than two fds.

static int gWakeFd[2] = {-1, -1};

static void WakeInit() {
    if (gWakeFd[0] >= 0) {
        return;
    }
    if (pipe(gWakeFd) != 0) {
        gWakeFd[0] = -1;
        gWakeFd[1] = -1;
        return;
    }
    // Non-blocking both ways: a writer must never stall on a full pipe, and
    // the drain below reads until it would block.
    fcntl(gWakeFd[0], F_SETFL, O_NONBLOCK);
    fcntl(gWakeFd[1], F_SETFL, O_NONBLOCK);
}

static void WakeShutdown() {
    for (int i = 0; i < 2; i++) {
        if (gWakeFd[i] >= 0) {
            close(gWakeFd[i]);
            gWakeFd[i] = -1;
        }
    }
}

// Empty the pipe. Any number of bytes mean the same thing — look at the
// queue — so what matters is that poll() does not keep firing on them.
static void WakeConsume() {
    if (gWakeFd[0] < 0) {
        return;
    }
    char buf[64];
    while (read(gWakeFd[0], buf, sizeof(buf)) > 0) {
        // again
    }
}

void PlatWake(App* app) {
    (void)app;
    int fd = gWakeFd[1];
    if (fd < 0) {
        return;
    }
    char b = 1;
    ssize_t n = write(fd, &b, 1);
    (void)n; // a full pipe already says what this one would have
}

// ─── a second loop on this thread ─────────────────────────────────────────
//
// GLib's main context, when a WebKitGTK webview exists (platform.h). Its
// descriptors are polled beside the X connection's, and it is turned every
// time round, so a page keeps loading while this loop is busy drawing.

static PlatLoopPrepare gLoopPrepare = nullptr;
static PlatLoopDispatch gLoopDispatch = nullptr;

void PlatAddLoopSource(PlatLoopPrepare prepare, PlatLoopDispatch dispatch) {
    if (gLoopPrepare && gLoopPrepare != prepare) {
        logf("PlatAddLoopSource: a second loop source is not supported\n");
        return;
    }
    gLoopPrepare = prepare;
    gLoopDispatch = dispatch;
}

// The X connection, the wake pipe and the accessibility bus, then whatever
// the loop source asked for. Its revents are copied back for its dispatch.
static void PollAll(int xfd, int timeoutMs, PlatPollFd* extra, int nExtra) {
    static Vec<struct pollfd> pfd;
    VecClear(pfd);
    int accessibilityFd = AccessibilityLinuxFd();
    VecAppend(pfd, pollfd{xfd, POLLIN, 0});
    if (gWakeFd[0] >= 0) {
        VecAppend(pfd, pollfd{gWakeFd[0], POLLIN, 0});
    }
    if (accessibilityFd >= 0) {
        VecAppend(pfd, pollfd{accessibilityFd, POLLIN, 0});
    }
    int own = len(pfd);
    for (int i = 0; i < nExtra; i++) {
        VecAppend(pfd, pollfd{extra[i].fd, (short)extra[i].events, 0});
    }
    if (poll(pfd.els, (nfds_t)len(pfd), timeoutMs) < 0) {
        VecClear(pfd);
    }
    for (int i = 0; i < nExtra; i++) {
        extra[i].revents =
            own + i < len(pfd) ? (uint16_t)pfd.els[own + i].revents : 0;
    }
}

// update_ime_position: the over-the-spot fallback hangs from this point.
// On-the-spot preedit draws in the field and ignores it.
static void UpdateImePosition(Window* win) {
    if (!win || !win->plat || !win->plat->xic || !win->input) {
        return;
    }
    InputState* in = win->input;
    XPoint spot;
    spot.x = (short)(in->lastBounds.x + in->caretX - in->scrollX);
    spot.y = (short)(in->lastBounds.y - in->scrollY + in->lastLineH);
    XVaNestedList preedit =
        XVaCreateNestedList(0, XNSpotLocation, &spot, nullptr);
    if (!preedit) {
        return;
    }
    XSetICValues(win->plat->xic, XNPreeditAttributes, preedit, nullptr);
    XFree(preedit);
}

bool PlatInit(App* app) {
    PlatInstallImePositionUpdate(UpdateImePosition);
    WakeInit();
    if (gDpy) {
        return true;
    }
    setlocale(LC_ALL, "");
    XSetLocaleModifiers("");
    gDpy = XOpenDisplay(nullptr);
    if (!gDpy) {
        logf("XOpenDisplay failed: no DISPLAY?");
        return false;
    }
    gX11App = app;
    gScreen = DefaultScreen(gDpy);
    gRoot = RootWindow(gDpy, gScreen);
    gXim = XOpenIM(gDpy, nullptr, nullptr, nullptr);

    aWmProtocols = XInternAtom(gDpy, "WM_PROTOCOLS", False);
    aWmDeleteWindow = XInternAtom(gDpy, "WM_DELETE_WINDOW", False);
    aNetWmName = XInternAtom(gDpy, "_NET_WM_NAME", False);
    aUtf8String = XInternAtom(gDpy, "UTF8_STRING", False);
    aNetWmState = XInternAtom(gDpy, "_NET_WM_STATE", False);
    aNetFrameExtents = XInternAtom(gDpy, "_NET_FRAME_EXTENTS", False);
    aNetAllowedActions = XInternAtom(gDpy, "_NET_WM_ALLOWED_ACTIONS", False);
    aNetActionMinimize = XInternAtom(gDpy, "_NET_WM_ACTION_MINIMIZE", False);
    aNetActionMaxHorz =
        XInternAtom(gDpy, "_NET_WM_ACTION_MAXIMIZE_HORZ", False);
    aNetActionMaxVert =
        XInternAtom(gDpy, "_NET_WM_ACTION_MAXIMIZE_VERT", False);
    aNetWmStateMaxVert =
        XInternAtom(gDpy, "_NET_WM_STATE_MAXIMIZED_VERT", False);
    aNetWmStateMaxHorz =
        XInternAtom(gDpy, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
    aNetWmStateFullscreen =
        XInternAtom(gDpy, "_NET_WM_STATE_FULLSCREEN", False);
    aNetWmStateHidden = XInternAtom(gDpy, "_NET_WM_STATE_HIDDEN", False);
    aWmState = XInternAtom(gDpy, "WM_STATE", False);
    aNetWmMoveResize = XInternAtom(gDpy, "_NET_WM_MOVERESIZE", False);
    aMotifWmHints = XInternAtom(gDpy, "_MOTIF_WM_HINTS", False);
    aGtkShowWindowMenu = XInternAtom(gDpy, "_GTK_SHOW_WINDOW_MENU", False);
    aGtkEdgeConstraints = XInternAtom(gDpy, "_GTK_EDGE_CONSTRAINTS", False);
    aClipboard = XInternAtom(gDpy, "CLIPBOARD", False);
    aTargets = XInternAtom(gDpy, "TARGETS", False);
    aClipTarget = XInternAtom(gDpy, "GPUI_CLIPBOARD", False);
    aIncr = XInternAtom(gDpy, "INCR", False);
    aXdndAware = XInternAtom(gDpy, "XdndAware", False);
    aXdndEnter = XInternAtom(gDpy, "XdndEnter", False);
    aXdndPosition = XInternAtom(gDpy, "XdndPosition", False);
    aXdndStatus = XInternAtom(gDpy, "XdndStatus", False);
    aXdndLeave = XInternAtom(gDpy, "XdndLeave", False);
    aXdndDrop = XInternAtom(gDpy, "XdndDrop", False);
    aXdndFinished = XInternAtom(gDpy, "XdndFinished", False);
    aXdndSelection = XInternAtom(gDpy, "XdndSelection", False);
    aXdndTypeList = XInternAtom(gDpy, "XdndTypeList", False);
    aXdndActionCopy = XInternAtom(gDpy, "XdndActionCopy", False);
    aTextUriList = XInternAtom(gDpy, "text/uri-list", False);
    aXdndProp = XInternAtom(gDpy, "GPUI_XDND", False);
    aImagePng = XInternAtom(gDpy, "image/png", False);
    aImageJpeg = XInternAtom(gDpy, "image/jpeg", False);
    aImageBmp = XInternAtom(gDpy, "image/bmp", False);
    aImageTiff = XInternAtom(gDpy, "image/tiff", False);
    aGnomeCopiedFiles =
        XInternAtom(gDpy, "x-special/gnome-copied-files", False);
    const char* accessibilityBus = getenv("AT_SPI_BUS_ADDRESS");
    if (accessibilityBus && *accessibilityBus) {
        AccessibilityLinuxInit(app, Str(accessibilityBus));
    } else {
        // at-spi-bus-launcher publishes the private bus here for applications
        // that were started before its environment could be inherited.
        Atom property = XInternAtom(gDpy, "AT_SPI_BUS", True);
        Atom type = None;
        int format = 0;
        unsigned long count = 0;
        unsigned long after = 0;
        unsigned char* value = nullptr;
        if (property != None &&
            XGetWindowProperty(gDpy, gRoot, property, 0, 4096, False,
                               AnyPropertyType, &type, &format, &count, &after,
                               &value) == Success &&
            format == 8 && value && count) {
            AccessibilityLinuxInit(app, Str((char*)value, (int)count));
        }
        if (value) XFree(value);
    }
    return true;
}

void PlatShutdown(App* app) {
    if (gX11App == app) gX11App = nullptr;
    AccessibilityLinuxShutdown();
    WakeShutdown();
    if (gClipboard.s) {
        StrFree(gClipboard);
        gClipboard = {};
    }
    if (gXim) {
        XCloseIM(gXim);
        gXim = nullptr;
    }
    if (gDpy) {
        XCloseDisplay(gDpy);
        gDpy = nullptr;
    }
}

// One CARDINAL-array property of the root window, or false when the window
// manager did not set it. `out` takes at most `max` values from `first` on.
static bool RootCardinals(const char* name, long first, long max, long* out,
                          long* nOut) {
    Atom prop = XInternAtom(gDpy, name, True);
    if (prop == None) {
        return false;
    }
    Atom type = 0;
    int format = 0;
    unsigned long n = 0, after = 0;
    unsigned char* data = nullptr;
    if (XGetWindowProperty(gDpy, gRoot, prop, first, max, False, XA_CARDINAL,
                           &type, &format, &n, &after, &data) != Success) {
        return false;
    }
    bool ok = data && format == 32 && type == XA_CARDINAL && n > 0;
    *nOut = 0;
    if (ok) {
        // Xlib hands a 32-bit property back as longs.
        auto* v = (long*)data;
        for (unsigned long i = 0; i < n && (long)i < max; i++) {
            out[(*nOut)++] = v[i];
        }
    }
    if (data) {
        XFree(data);
    }
    return ok;
}

// Bounds::centered's display.visible_bounds(): the work area the window
// manager publishes in _NET_WORKAREA — x, y, width, height for each desktop,
// read for the current one — clear of panels and docks. The whole display
// when the manager says nothing, which is all a bare X server knows.
static void X11WorkArea(int* x, int* y, int* w, int* h) {
    *x = 0;
    *y = 0;
    *w = DisplayWidth(gDpy, gScreen);
    *h = DisplayHeight(gDpy, gScreen);
    long desk = 0, n = 0;
    long cur[1] = {};
    if (RootCardinals("_NET_CURRENT_DESKTOP", 0, 1, cur, &n) && n == 1 &&
        cur[0] >= 0) {
        desk = cur[0];
    }
    long wa[4] = {};
    if (!RootCardinals("_NET_WORKAREA", desk * 4, 4, wa, &n) || n != 4) {
        if (desk == 0 || !RootCardinals("_NET_WORKAREA", 0, 4, wa, &n) ||
            n != 4) {
            return;
        }
    }
    if (wa[2] <= 0 || wa[3] <= 0) {
        return;
    }
    *x = (int)wa[0];
    *y = (int)wa[1];
    *w = (int)wa[2];
    *h = (int)wa[3];
}

Window* WindowOpen(App* app, Str title, int dipW, int dipH, WinOpts opts) {
    if (!gDpy) {
        return nullptr;
    }
    Window* win = WindowAlloc(app, opts);
    if (!win) {
        return nullptr;
    }
    // Centred in and clamped to the work area, as window_win.cpp does with
    // SPI_GETWORKAREA and window_mac.cpp with visibleFrame.
    int wx = 0, wy = 0, sw = 0, sh = 0;
    X11WorkArea(&wx, &wy, &sw, &sh);
    WindowClampToDisplay(&dipW, &dipH, sw, sh);

    int x = wx + (sw - dipW) / 2;
    int y = wy + (sh - dipH) / 2;
    // WindowOptions::window_bounds is the outer frame; X11 places the client
    // inside the manager's frame.
    bool placed = !opts.bounds.IsEmpty();
    if (placed) {
        FrameExtentsPx e = PlacementExtents(opts, 0);
        x = opts.bounds.x + e.left;
        y = opts.bounds.y + e.top;
        dipW = std::max(opts.bounds.w - e.left - e.right, 1);
        dipH = std::max(opts.bounds.h - e.top - e.bottom, 1);
    }

    auto* pw = new PlatWindow();
    pw->pxW = dipW;
    pw->pxH = dipH;

    XSetWindowAttributes attrs = {};
    attrs.background_pixel = BlackPixel(gDpy, gScreen);
    attrs.event_mask = ExposureMask | KeyPressMask | KeyReleaseMask |
                       ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
                       LeaveWindowMask | StructureNotifyMask |
                       PropertyChangeMask | FocusChangeMask;
    pw->xwin = XCreateWindow(gDpy, gRoot, x, y, (unsigned)dipW, (unsigned)dipH,
                             0, CopyFromParent, InputOutput, CopyFromParent,
                             CWBackPixel | CWEventMask, &attrs);
    if (!pw->xwin) {
        delete pw;
        return nullptr;
    }
    win->plat = pw;

    XSetWMProtocols(gDpy, pw->xwin, &aWmDeleteWindow, 1);
    // Xdnd version 5. A file manager will not offer a drop without this.
    Atom xdndVersion = 5;
    XChangeProperty(gDpy, pw->xwin, aXdndAware, XA_ATOM, 32, PropModeReplace,
                    (unsigned char*)&xdndVersion, 1);
    if (opts.borderless || opts.clientTitleBar || opts.frameless ||
        opts.kind == WindowKind::PopUp) {
        SetUndecorated(pw->xwin);
    }
    // WindowOptions::is_resizable == false. The edge band also refuses the
    // grab; the hints stop the window manager from resizing it another way.
    // A placed window asks to be left where it was put.
    if (!opts.resizable || placed) {
        XSizeHints hints = {};
        if (!opts.resizable) {
            hints.flags = PMinSize | PMaxSize;
            hints.min_width = hints.max_width = dipW;
            hints.min_height = hints.max_height = dipH;
        }
        if (placed) {
            hints.flags |= USPosition | USSize;
            hints.x = x;
            hints.y = y;
            hints.width = dipW;
            hints.height = dipH;
        }
        XSetWMNormalHints(gDpy, pw->xwin, &hints);
    }
    SetWindowType(pw->xwin, opts);
    bool keepsParent =
        opts.kind == WindowKind::Floating || opts.kind == WindowKind::Dialog;
    if (keepsParent && opts.parent && opts.parent->plat) {
        XSetTransientForHint(gDpy, pw->xwin, opts.parent->plat->xwin);
    }
    if (!opts.activatable) {
        // ICCCM: a window that takes no input is never given the focus.
        XWMHints* wm = XAllocWMHints();
        if (wm) {
            wm->flags = InputHint;
            wm->input = False;
            XSetWMHints(gDpy, pw->xwin, wm);
            XFree(wm);
        }
    }
    AppSetTitle(win, title);

    if (gXim) {
        // On-the-spot first: the field draws the composition itself, in its
        // own font and in the right place. Not every input method offers the
        // style, and the ones that do not get the older arrangement where the
        // method shows its own window — which is what this did before.
        // XIMCallback holds every preedit callback in one XIMProc, which
        // none of the four actually has the shape of — Xlib's own headers
        // spell them differently. The cast is through void* so the compiler
        // takes the mismatch as deliberate, which it is.
        XIMCallback cbStart = {(XPointer)win, (XIMProc)(void*)PreeditStart};
        XIMCallback cbDone = {(XPointer)win, (XIMProc)(void*)PreeditDone};
        XIMCallback cbDraw = {(XPointer)win, (XIMProc)(void*)PreeditDraw};
        XIMCallback cbCaret = {(XPointer)win, (XIMProc)(void*)PreeditCaret};
        XVaNestedList preedit = XVaCreateNestedList(
            0, XNPreeditStartCallback, &cbStart, XNPreeditDoneCallback, &cbDone,
            XNPreeditDrawCallback, &cbDraw, XNPreeditCaretCallback, &cbCaret,
            nullptr);
        if (preedit) {
            pw->xic = XCreateIC(
                gXim, XNInputStyle, XIMPreeditCallbacks | XIMStatusNothing,
                XNClientWindow, pw->xwin, XNFocusWindow, pw->xwin,
                XNPreeditAttributes, preedit, nullptr);
            XFree(preedit);
        }
        if (!pw->xic) {
            pw->xic = XCreateIC(
                gXim, XNInputStyle, XIMPreeditNothing | XIMStatusNothing,
                XNClientWindow, pw->xwin, XNFocusWindow, pw->xwin, nullptr);
        }
    }

    WindowSetMinSize(win, opts.minSize);
    WindowSetMaxSize(win, opts.maxSize);
    if (opts.show) {
        WindowModalTake(win);
        XMapWindow(gDpy, pw->xwin);
    }
    if (placed) {
        // Again once mapped: a manager that placed it by its own policy
        // moves it where it was asked to be.
        WindowSetFrameBounds(win, opts.bounds);
    }
    ReadWindowButtonSupport(pw);
    XFlush(gDpy);
    PlatSetTimer(win, WindowTimerMs(win));
    return win;
}

int AppRun(App* app) {
    if (!app || !gDpy) {
        return 1;
    }
    int fd = ConnectionNumber(gDpy);
    while (AppAnyWindowOpen(app)) {
        while (XPending(gDpy) > 0) {
            XEvent ev = {};
            XNextEvent(gDpy, &ev);
            if (XFilterEvent(&ev, None)) {
                continue;
            }
            HandleEvent(app, &ev);
        }
        if (!AppAnyWindowOpen(app)) {
            break;
        }

        RunLoopRedrawDirty<PlatWindow>(app, Redraw);
        bool moveEndWaiting = PollMoveEnds(app);

        // Sleep until the next tick, or until X has something to say.
        bool anyDirty = false;
        double waitS = RunLoopWaitFor<PlatWindow>(app, &anyDirty);
        if (moveEndWaiting && waitS > kMoveEndPollMs / 1000.0) {
            waitS = kMoveEndPollMs / 1000.0;
        }
        bool idle = !anyDirty && XPending(gDpy) == 0 && ExecQueued() == 0;
        PlatPollFd* extra = nullptr;
        int extraTimeoutMs = -1;
        int nExtra = gLoopPrepare ? gLoopPrepare(&extra, &extraTimeoutMs) : 0;
        if (gLoopPrepare) {
            // With a second loop the poll always happens — without blocking
            // when this loop has work of its own — so a busy X connection
            // never starves GLib's descriptors.
            int timeoutMs =
                !idle ? 0 : (waitS <= 0 ? 0 : (int)(waitS * 1000.0));
            if (extraTimeoutMs >= 0 && extraTimeoutMs < timeoutMs) {
                timeoutMs = extraTimeoutMs;
            }
            PollAll(fd, timeoutMs, extra, nExtra);
        } else if (idle) {
            int timeoutMs = waitS <= 0 ? 0 : (int)(waitS * 1000.0);
            int accessibilityFd = AccessibilityLinuxFd();
            struct pollfd pfd[3] = {{fd, POLLIN, 0},
                                    {gWakeFd[0], POLLIN, 0},
                                    {accessibilityFd, POLLIN, 0}};
            int nfd = accessibilityFd >= 0 ? 3 : (gWakeFd[0] >= 0 ? 2 : 1);
            poll(pfd, nfd, timeoutMs);
        }
        if (gLoopDispatch) {
            gLoopDispatch();
        }
        // Whatever woke us, the queue is drained on the way past: a worker
        // that finished while we were asleep wrote the byte that ended the
        // poll, and one that finished while we were drawing did not have to.
        WakeConsume();
        AccessibilityLinuxPump();
        ExecDrain();

        RunLoopTickDue<PlatWindow>(app);
    }
    return app->exitCode;
}

} // namespace gpui

// The process entry point. Examples implement GpuiMain(argc, argv).
int main(int argc, char** argv) {
    // Strip the -gpui-* flags here too, so an example parses the same argv on
    // every platform. -gpui-window itself is only honoured on Windows, where
    // the screenshot harness runs.
    argc = gpui::GpuiTakeRuntimeArgs(argc, argv);
    return GpuiMain(argc, argv);
}
