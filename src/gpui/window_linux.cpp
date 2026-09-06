/* The X11 window: event loop, chrome routing, timers, clipboard, and the
   process entry point. The mirror of Window_win.cpp; everything either of
   them decides is delegated to WindowCommon.cpp.

   Like the Direct2D target, the cairo target runs at 96 dpi, so one DIP is
   one device pixel and no coordinate here is scaled. */

#include "gpui/platform.h"
#include "gpui/paint.h"
#include "gpui/accessibility_linux.h"
#include "sys/executor.h"

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
    int pxW = 0;
    int pxH = 0;
    bool dirty = true;
    // Monotonic deadline for the next tick; 0 when the timer is off.
    double nextTick = 0;
    // -1, or the _NET_WM_MOVERESIZE direction the pointer is currently over.
    // The shared move handler only asks for arrow or I-beam, so the edge
    // cursor has to be put back by hand once the pointer leaves the band, and
    // put back up again whenever that arrow/I-beam choice changed underneath
    // it.
    int edge = -1;
    CursorKind edgeUnder = CursorKind::Arrow;
};

// One display per process. GPUI's App is a singleton in practice, and an X
// connection is the one piece of state every window here shares.
static XDisplay* gDpy = nullptr;
static int gScreen = 0;
static XWindow gRoot = 0;
static XIM gXim = nullptr;
static Str gClipboard = {};

static Atom aWmDeleteWindow, aWmProtocols, aNetWmName, aUtf8String;
static Atom aNetWmState, aNetWmStateMaxVert, aNetWmStateMaxHorz;
static Atom aNetFrameExtents;
static Atom aNetWmMoveResize, aMotifWmHints, aGtkShowWindowMenu;
static Atom aGtkEdgeConstraints;
static Atom aClipboard, aTargets, aClipTarget;

double TimeNow() {
    static bool started = false;
    static struct timespec start = {};
    struct timespec now = {};
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (!started) {
        start = now;
        started = true;
    }
    return (double)(now.tv_sec - start.tv_sec) +
           (double)(now.tv_nsec - start.tv_nsec) / 1e9;
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
    pw->dirty = false;
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

static bool ReadMaximized(Window* win) {
    PlatWindow* pw = win->plat;
    if (!pw) {
        return false;
    }
    Atom type = 0;
    int format = 0;
    unsigned long n = 0, after = 0;
    unsigned char* data = nullptr;
    if (XGetWindowProperty(gDpy, pw->xwin, aNetWmState, 0, 32, False, XA_ATOM,
                           &type, &format, &n, &after, &data) != Success) {
        return false;
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
        }
        XFree(data);
    }
    return vert && horz;
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

static void SendWmState(Window* win, Atom a, Atom b, int action) {
    PlatWindow* pw = win->plat;
    if (!pw) {
        return;
    }
    XEvent ev = {};
    ev.type = ClientMessage;
    ev.xclient.window = pw->xwin;
    ev.xclient.message_type = aNetWmState;
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = action; // 0 remove, 1 add, 2 toggle
    ev.xclient.data.l[1] = (long)a;
    ev.xclient.data.l[2] = (long)b;
    ev.xclient.data.l[3] = 1; // normal application
    XSendEvent(gDpy, gRoot, False,
               SubstructureNotifyMask | SubstructureRedirectMask, &ev);
    XFlush(gDpy);
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
        return '0' + (int)(ks - XK_KP_0);
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

// Decode one UTF-8 codepoint; returns how many bytes it used.
static int Utf8Next(const char* s, int len, uint32_t* out) {
    if (len <= 0) {
        return 0;
    }
    auto b = (const unsigned char*)s;
    if (b[0] < 0x80) {
        *out = b[0];
        return 1;
    }
    if ((b[0] & 0xe0) == 0xc0 && len >= 2) {
        *out = ((uint32_t)(b[0] & 0x1f) << 6) | (b[1] & 0x3f);
        return 2;
    }
    if ((b[0] & 0xf0) == 0xe0 && len >= 3) {
        *out = ((uint32_t)(b[0] & 0x0f) << 12) |
               ((uint32_t)(b[1] & 0x3f) << 6) | (b[2] & 0x3f);
        return 3;
    }
    if ((b[0] & 0xf8) == 0xf0 && len >= 4) {
        *out = ((uint32_t)(b[0] & 0x07) << 18) |
               ((uint32_t)(b[1] & 0x3f) << 12) |
               ((uint32_t)(b[2] & 0x3f) << 6) | (b[3] & 0x3f);
        return 4;
    }
    *out = b[0];
    return 1;
}

// --- input method ---------------------------------------------------------
//
// On-the-spot preedit: the input method hands over what it is composing and
// the field draws it, so the candidate lands in the field's own font and
// scrolls with the rest of the document. XIM builds the string by edits
// rather than sending it whole, so the window keeps it and replays the whole
// of it into the field on every change — which is what the mark is for.

// One code point, encoded. XIM may hand the preedit over as wide characters.
static int PreeditUtf8Encode(uint32_t cp, char* out) {
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

// The byte offset of character `chars` into a UTF-8 run. XIM counts in
// characters; everything above here counts in bytes.
static int Utf8ByteOfChar(const char* s, int len, int chars) {
    int i = 0;
    int c = 0;
    while (i < len && c < chars) {
        uint32_t cp = 0;
        int adv = Utf8Next(s + i, len - i, &cp);
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

static void PreeditDraw(XIC, XPointer client, XPointer call) {
    Window* win = (Window*)client;
    auto* d = (XIMPreeditDrawCallbackStruct*)call;
    if (!win || !win->plat || !d) {
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
            for (int i = 0; w && i < d->text->length && insLen < ins.len - 8;
                 i++) {
                insLen += PreeditUtf8Encode((uint32_t)w[i], ins.s + insLen);
            }
        } else if (d->text->string.multi_byte) {
            const char* m = d->text->string.multi_byte;
            insLen = (int)strlen(m);
            if (insLen > ins.len) {
                insLen = ins.len;
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
    win->plat->preeditCaret = c->position;
    PreeditApply(win);
}

static void OnKeyPress(Window* win, XKeyEvent* ke) {
    PlatWindow* pw = win->plat;
    char buf[64] = {};
    KeySym ks = 0;
    int n = 0;
    if (pw && pw->xic) {
        Status st = 0;
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
    if (key) {
        WindowKeyDown(win, key, shift, ctrl, alt, platform);
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
        int adv = Utf8Next(buf + i, n - i, &cp);
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
            return;
        }
    }
    TempStr buf = AllocStrTemp(7);
    buf.s[0] = 0;
    KeySym ks = 0;
    XLookupString(ke, buf.s, buf.len, &ks, nullptr);
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
    PlatWindow* pw = win->plat;
    if (!pw) {
        return;
    }
    XUngrabPointer(gDpy, CurrentTime);
    XEvent ev = {};
    ev.type = ClientMessage;
    ev.xclient.window = pw->xwin;
    ev.xclient.message_type = aNetWmMoveResize;
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = rootX;
    ev.xclient.data.l[1] = rootY;
    ev.xclient.data.l[2] = dir;
    ev.xclient.data.l[3] = Button1;
    ev.xclient.data.l[4] = 1;
    XSendEvent(gDpy, gRoot, False,
               SubstructureNotifyMask | SubstructureRedirectMask, &ev);
    XFlush(gDpy);
}

static void StartMoveDrag(Window* win, int rootX, int rootY) {
    StartMoveResize(win, rootX, rootY, kMoveResizeMove);
}

// _GTK_SHOW_WINDOW_MENU: what a right click on the title bar of a
// client-decorated window asks the window manager for. Windows gets the same
// menu from DefWindowProc on WM_NCRBUTTONUP over HTCAPTION.
static void ShowWindowMenu(Window* win, int rootX, int rootY) {
    PlatWindow* pw = win->plat;
    if (!pw) {
        return;
    }
    XUngrabPointer(gDpy, CurrentTime);
    XEvent ev = {};
    ev.type = ClientMessage;
    ev.xclient.window = pw->xwin;
    ev.xclient.message_type = aGtkShowWindowMenu;
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = 0; // the device, which we do not track
    ev.xclient.data.l[1] = rootX;
    ev.xclient.data.l[2] = rootY;
    XSendEvent(gDpy, gRoot, False,
               SubstructureNotifyMask | SubstructureRedirectMask, &ev);
    XFlush(gDpy);
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
    if (!pw || !ClientDecorated(win) || win->maximized) {
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

void PlatSetCursor(Window* win, CursorKind kind) {
    if (!win || !win->plat || !gDpy) {
        return;
    }
    // The server owns these; one of each per process is all this needs.
    static ::Cursor arrow = 0;
    static ::Cursor ibeam = 0;
    static ::Cursor colResize = 0;
    static ::Cursor rowResize = 0;
    static ::Cursor pointer = 0;
    static ::Cursor crosshair = 0;
    if (!arrow) {
        arrow = XCreateFontCursor(gDpy, XC_left_ptr);
    }
    if (!ibeam) {
        ibeam = XCreateFontCursor(gDpy, XC_xterm);
    }
    if (!colResize) {
        colResize = XCreateFontCursor(gDpy, XC_sb_h_double_arrow);
    }
    if (!rowResize) {
        rowResize = XCreateFontCursor(gDpy, XC_sb_v_double_arrow);
    }
    if (!pointer) {
        pointer = XCreateFontCursor(gDpy, XC_hand2);
    }
    if (!crosshair) {
        crosshair = XCreateFontCursor(gDpy, XC_crosshair);
    }
    ::Cursor want = arrow;
    if (kind == CursorKind::IBeam) {
        want = ibeam;
    } else if (kind == CursorKind::ColResize) {
        want = colResize;
    } else if (kind == CursorKind::RowResize) {
        want = rowResize;
    } else if (kind == CursorKind::Pointer) {
        want = pointer;
    } else if (kind == CursorKind::Crosshair) {
        want = crosshair;
    }
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

bool PlatHasMenu() {
    // X11 has no popup menu of its own — a toolkit draws its own. The caller
    // falls back to the drawn menu, which is what Rust does here too.
    return false;
}

int PlatShowMenu(Window* win, const PlatMenuItem* items, int n, float x,
                 float y, bool dark) {
    (void)win;
    (void)items;
    (void)n;
    (void)x;
    (void)y;
    (void)dark;
    return 0;
}

bool PlatHasAppMenu() {
    // The same on X11. A desktop can export a window's menus to a panel over
    // dbus — Unity's appmenu, and KDE's global menu after it — but that is a
    // protocol and a service, not an X11 call, and a window whose menus went
    // unexported would be left with none at all.
    return false;
}

void PlatSetAppMenu(App* app, const PlatMenuItem* items, int n) {
    (void)app;
    (void)items;
    (void)n;
}

// cx.open_url. xdg-open is the desktop's own answer to "what opens this";
// the fork keeps a browser that takes its time from holding up the frame, and
// the child replaces itself so nothing here waits on it.
// X11 has nothing to ask. GNOME keeps `gtk-enable-animations` in its own
// settings daemon, which is not something this window talks to.
bool PlatReduceMotion() {
    return false;
}

void OpenUrl(Str url) {
    if (!url.s || url.len <= 0) {
        return;
    }
    TempStr value = StrDupTemp(url.len < 1023 ? url : Str(url.s, 1023));
    pid_t pid = fork();
    if (pid == 0) {
        // The grandchild is orphaned deliberately: nobody is left to reap it.
        if (fork() == 0) {
            execlp("xdg-open", "xdg-open", value.s, (char*)nullptr);
            _exit(127);
        }
        _exit(0);
    }
    if (pid > 0) {
        int st = 0;
        waitpid(pid, &st, 0);
    }
}

// cx.prompt_for_paths. X11 has no file dialog of its own and this tree has no
// toolkit to borrow one from, so it asks the desktop's: zenity on GTK
// desktops, kdialog on KDE, whichever is on the PATH. A session with neither
// answers nothing, which is what a caller has to be ready for anyway — the
// user can always cancel.
TempStr PromptForPathTemp(Window* win, const PathPrompt& opts) {
    (void)win;
    TempStr title =
        StrDupTemp(opts.title.len < 255 ? opts.title : Str(opts.title.s, 255));
    bool dirs = opts.directories && !opts.files;
    int fds[2] = {-1, -1};
    if (pipe(fds) != 0) {
        return {};
    }
    pid_t pid = fork();
    if (pid == 0) {
        dup2(fds[1], STDOUT_FILENO);
        close(fds[0]);
        close(fds[1]);
        // Two spellings of the same question. Whichever is there runs; the
        // exec that fails falls through to the next.
        if (dirs) {
            execlp("zenity", "zenity", "--file-selection", "--directory",
                   title ? "--title" : (char*)nullptr,
                   title ? title.s : (char*)nullptr, (char*)nullptr);
            execlp("kdialog", "kdialog", "--getexistingdirectory", ".",
                   (char*)nullptr);
        } else {
            execlp("zenity", "zenity", "--file-selection",
                   title ? "--title" : (char*)nullptr,
                   title ? title.s : (char*)nullptr, (char*)nullptr);
            execlp("kdialog", "kdialog", "--getopenfilename", ".",
                   (char*)nullptr);
        }
        _exit(127);
    }
    close(fds[1]);
    if (pid < 0) {
        close(fds[0]);
        return {};
    }
    TempStr result = AllocStrTemp(kMaxPath - 1);
    if (!result.s) {
        close(fds[0]);
        int st = 0;
        waitpid(pid, &st, 0);
        return {};
    }
    int n = 0;
    for (;;) {
        ssize_t got = read(fds[0], result.s + n, (size_t)(result.len - n));
        if (got <= 0) {
            break;
        }
        n += (int)got;
        if (n >= result.len) {
            break;
        }
    }
    close(fds[0]);
    int st = 0;
    waitpid(pid, &st, 0);
    // The helper prints the path and a newline, and nothing at all when the
    // user cancelled.
    while (n > 0 && (result.s[n - 1] == '\n' || result.s[n - 1] == '\r')) {
        n--;
    }
    result.s[n] = 0;
    result.len = n;
    return result;
}

void ClipboardSetText(Window* win, Str text) {
    if (!win || !win->plat || !text.s || text.len <= 0) {
        return;
    }
    if (gClipboard.s) {
        StrFree(gClipboard);
    }
    gClipboard = StrDup(text);
    XSetSelectionOwner(gDpy, aClipboard, win->plat->xwin, CurrentTime);
    XFlush(gDpy);
}

void WindowSetTextContentType(Window* win, Str value) {
    (void)win;
    (void)value;
}

// A paste on X11 is a round trip: ask the selection owner to write the text
// into a property on our window, then wait for the SelectionNotify that says
// it is there. Windows and Cocoa read the clipboard straight out, so this is
// the one platform where the portable signature hides a wait — half a second,
// after which an owner that never answered is given up on.
Str ClipboardGetText(Arena* a, Window* win) {
    if (!win || !win->plat || !gDpy) {
        return {};
    }
    XWindow xwin = win->plat->xwin;
    // We own the selection: no round trip, the text is already here.
    if (XGetSelectionOwner(gDpy, aClipboard) == xwin) {
        return StrDup(a, gClipboard);
    }
    XConvertSelection(gDpy, aClipboard, aUtf8String, aClipTarget, xwin,
                      CurrentTime);
    XFlush(gDpy);

    XEvent ev = {};
    bool got = false;
    double deadline = TimeNow() + 0.5;
    while (TimeNow() < deadline) {
        if (XCheckTypedWindowEvent(gDpy, xwin, SelectionNotify, &ev)) {
            got = true;
            break;
        }
        struct timespec ts = {0, 2 * 1000 * 1000};
        nanosleep(&ts, nullptr);
    }
    if (!got || ev.xselection.property == None) {
        return {};
    }
    Atom type = 0;
    int format = 0;
    unsigned long items = 0;
    unsigned long after = 0;
    unsigned char* data = nullptr;
    if (XGetWindowProperty(gDpy, xwin, aClipTarget, 0, 1 << 20, True,
                           AnyPropertyType, &type, &format, &items, &after,
                           &data) != Success) {
        return {};
    }
    Str out = {};
    if (data && items > 0 && format == 8) {
        out = StrDup(a, Str((char*)data, (int)items));
    }
    if (data) {
        XFree(data);
    }
    return out;
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
    if (req->target == aTargets) {
        Atom targets[2] = {aTargets, aUtf8String};
        XChangeProperty(gDpy, req->requestor, prop, XA_ATOM, 32,
                        PropModeReplace, (unsigned char*)targets, 2);
        resp.xselection.property = prop;
    } else if ((req->target == aUtf8String || req->target == XA_STRING) &&
               gClipboard.s) {
        XChangeProperty(gDpy, req->requestor, prop, req->target, 8,
                        PropModeReplace, (unsigned char*)gClipboard.s,
                        gClipboard.len);
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
                pw->dirty = true;
            }
            break;
        case ConfigureNotify:
            if (ev->xconfigure.width != pw->pxW || ev->xconfigure
                                                           .height != pw->pxH) {
                pw->pxW = ev->xconfigure.width;
                pw->pxH = ev->xconfigure.height;
                pw->dirty = true;
            }
            break;
        case PropertyNotify:
            if (ev->xproperty.atom == aNetWmState ||
                ev->xproperty.atom == aGtkEdgeConstraints) {
                win->maximized = ReadMaximized(win);
                win->tiling = ReadTiling(win, win->maximized);
                pw->dirty = true;
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
            WindowSetActive(win, false);
            break;
        case ClientMessage:
            if (ev->xclient.message_type == aWmProtocols &&
                (Atom)ev->xclient.data.l[0] == aWmDeleteWindow) {
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

void AppInvalidate(Window* win) {
    if (win) {
        win->invalidations++;
    }
    if (win && win->plat) {
        win->plat->dirty = true;
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

void AppToggleMaximize(Window* win) {
    if (win && win->plat) {
        SendWmState(win, aNetWmStateMaxVert, aNetWmStateMaxHorz, 2);
    }
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
                    (unsigned char*)title.s, title.len);
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

bool PlatInit(App* app) {
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
    gScreen = DefaultScreen(gDpy);
    gRoot = RootWindow(gDpy, gScreen);
    gXim = XOpenIM(gDpy, nullptr, nullptr, nullptr);

    aWmProtocols = XInternAtom(gDpy, "WM_PROTOCOLS", False);
    aWmDeleteWindow = XInternAtom(gDpy, "WM_DELETE_WINDOW", False);
    aNetWmName = XInternAtom(gDpy, "_NET_WM_NAME", False);
    aUtf8String = XInternAtom(gDpy, "UTF8_STRING", False);
    aNetWmState = XInternAtom(gDpy, "_NET_WM_STATE", False);
    aNetFrameExtents = XInternAtom(gDpy, "_NET_FRAME_EXTENTS", False);
    aNetWmStateMaxVert =
        XInternAtom(gDpy, "_NET_WM_STATE_MAXIMIZED_VERT", False);
    aNetWmStateMaxHorz =
        XInternAtom(gDpy, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
    aNetWmMoveResize = XInternAtom(gDpy, "_NET_WM_MOVERESIZE", False);
    aMotifWmHints = XInternAtom(gDpy, "_MOTIF_WM_HINTS", False);
    aGtkShowWindowMenu = XInternAtom(gDpy, "_GTK_SHOW_WINDOW_MENU", False);
    aGtkEdgeConstraints = XInternAtom(gDpy, "_GTK_EDGE_CONSTRAINTS", False);
    aClipboard = XInternAtom(gDpy, "CLIPBOARD", False);
    aTargets = XInternAtom(gDpy, "TARGETS", False);
    aClipTarget = XInternAtom(gDpy, "GPUI_CLIPBOARD", False);
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
    (void)app;
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

Window* WindowOpen(App* app, Str title, int dipW, int dipH, WinOpts opts) {
    if (!gDpy) {
        return nullptr;
    }
    Window* win = WindowAlloc(app, opts);
    if (!win) {
        return nullptr;
    }
    int sw = DisplayWidth(gDpy, gScreen);
    int sh = DisplayHeight(gDpy, gScreen);
    WindowClampToDisplay(&dipW, &dipH, sw, sh);

    auto* pw = new PlatWindow();
    pw->pxW = dipW;
    pw->pxH = dipH;

    int x = (sw - dipW) / 2;
    int y = (sh - dipH) / 2;

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
    if (opts.borderless || opts.clientTitleBar) {
        SetUndecorated(pw->xwin);
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

    XMapWindow(gDpy, pw->xwin);
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

        for (int i = 0; i < app->windows.len; i++) {
            Window* w = app->windows[i];
            if (w->plat && w->plat->dirty) {
                Redraw(w);
            }
        }

        // Sleep until the next tick, or until X has something to say.
        double now = TimeNow();
        double waitS = 1.0;
        bool anyDirty = false;
        for (int i = 0; i < app->windows.len; i++) {
            Window* w = app->windows[i];
            if (!w->plat) {
                continue;
            }
            if (w->plat->dirty) {
                anyDirty = true;
            }
            if (w->plat->nextTick > 0) {
                double d = w->plat->nextTick - now;
                if (d < waitS) {
                    waitS = d;
                }
            }
        }
        if (!anyDirty && XPending(gDpy) == 0 && ExecQueued() == 0) {
            int timeoutMs = waitS <= 0 ? 0 : (int)(waitS * 1000.0);
            int accessibilityFd = AccessibilityLinuxFd();
            struct pollfd pfd[3] = {{fd, POLLIN, 0},
                                    {gWakeFd[0], POLLIN, 0},
                                    {accessibilityFd, POLLIN, 0}};
            int nfd = accessibilityFd >= 0 ? 3 : (gWakeFd[0] >= 0 ? 2 : 1);
            poll(pfd, nfd, timeoutMs);
        }
        // Whatever woke us, the queue is drained on the way past: a worker
        // that finished while we were asleep wrote the byte that ended the
        // poll, and one that finished while we were drawing did not have to.
        WakeConsume();
        AccessibilityLinuxPump();
        ExecDrain();

        now = TimeNow();
        for (int i = 0; i < app->windows.len; i++) {
            Window* w = app->windows[i];
            if (!w->plat || w->plat->nextTick <= 0) {
                continue;
            }
            if (now >= w->plat->nextTick) {
                // WindowTimerTick re-arms through PlatSetTimer.
                WindowTimerTick(w);
            }
        }
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
