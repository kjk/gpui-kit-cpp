/* The browser window: the canvas, the DOM events, the animation-frame loop,
   and the process entry point. The mirror of Window_win.cpp and
   Window_linux.cpp; everything any of them decides is delegated to
   WindowCommon.cpp.

   Like the cairo target, the canvas runs at 96 dpi, so one DIP is one CSS
   pixel and no coordinate here is scaled — Paint_wasm.cpp puts the device
   pixel ratio on the context so the glyphs are still drawn at the display's
   own resolution.

   Where a page is not a desktop:

   - One window. A tab has one canvas, and a second WindowOpen answers null
     rather than pretending. Every example in this tree opens one.
   - The window is the viewport. There is no frame to minimize, and the
     maximize control asks for fullscreen; a drag on the caption is the
     browser's, not ours.
   - The clipboard is asynchronous. ClipboardSetText writes through
     navigator.clipboard, which works because every path to it here is inside
     a keystroke; ClipboardGetText answers a mirror that the DOM `paste` event
     fills in, and the paste chord is driven by that event rather than by its
     keydown so the mirror is never a keystroke behind.
   - AppRun does not return. emscripten_set_main_loop unwinds the stack and
     hands the tab back to the browser, which is the only way a C main loop
     and an event loop can share one thread. Nothing after AppRun in an
     example runs, which on a page is what closing the tab is for. */

#include "gpui/platform.h"
#include "gpui/paint.h"
#include "sys/executor.h"

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

namespace gpui {

struct PlatWindow {
    // The viewport, in CSS pixels, which are DIPs here.
    int cssW = 0;
    int cssH = 0;
    bool dirty = true;
    // Monotonic deadline for the next tick; 0 when the timer is off.
    double nextTick = 0;
    bool open = true;
};

// One canvas per tab, so one window per App, held here the way the X11 half
// holds its Display.
static Window* gWin = nullptr;
static Str gClipboard = {};

// The canvas id the shell page provides, and which Paint_wasm.cpp binds.
static const char* kCanvasSel = "#gpui-canvas";

double TimeNow() {
    static double start = -1;
    double now = emscripten_get_now() / 1000.0;
    if (start < 0) {
        start = now;
    }
    return now - start;
}

// ─── the page ─────────────────────────────────────────────────────────────

// clang-format off

// Find or make the canvas, hand it to the paint backend, and answer the
// viewport size. The shell page normally supplies the element; making one
// here means a bare page still runs.
EM_JS(void, GpJsAttach, (int* outW, int* outH), {
    let c = document.getElementById("gpui-canvas");
    if (!c) {
        c = document.createElement("canvas");
        c.id = "gpui-canvas";
        c.style.position = "fixed";
        c.style.inset = "0";
        c.style.width = "100%";
        c.style.height = "100%";
        c.style.display = "block";
        document.body.appendChild(c);
    }
    // The canvas takes keystrokes, so it has to be able to hold the focus.
    if (!c.hasAttribute("tabindex")) {
        c.setAttribute("tabindex", "0");
    }
    c.style.outline = "none";
    c.focus();
    const G = globalThis.__gpui;
    G.canvas = c;
    G.ctx = c.getContext("2d");
    // Every mouse coordinate below is a clientX/clientY, so the canvas has to
    // sit at the top left of the viewport. The shell page's CSS says so; this
    // is here for the page that has none.
    const r = c.getBoundingClientRect();
    HEAP32[outW >> 2] = Math.max(1, Math.round(r.width));
    HEAP32[outH >> 2] = Math.max(1, Math.round(r.height));
});

EM_JS(void, GpJsViewport, (int* outW, int* outH), {
    const c = globalThis.__gpui.canvas;
    const r = c.getBoundingClientRect();
    HEAP32[outW >> 2] = Math.max(1, Math.round(r.width));
    HEAP32[outH >> 2] = Math.max(1, Math.round(r.height));
});

EM_JS(void, GpJsSetTitle, (const char* s, int len), {
    document.title = globalThis.__gpui.str(s, len);
});

EM_JS(void, GpJsSetCursor, (int kind), {
    const names =
        ["default", "text", "pointer", "col-resize", "row-resize", "crosshair"];
    const c = globalThis.__gpui.canvas;
    if (c) {
        c.style.cursor = names[kind] || "default";
    }
});

EM_JS(void, GpJsOpenUrl, (const char* s, int len), {
    globalThis.open(globalThis.__gpui.str(s, len), "_blank", "noopener");
});

EM_JS(void, GpJsClipboardWrite, (const char* s, int len), {
    const text = globalThis.__gpui.str(s, len);
    globalThis.__gpuiClip = text;
    if (navigator.clipboard && navigator.clipboard.writeText) {
        navigator.clipboard.writeText(text).catch(function() {});
    }
});

// How many bytes the mirror holds, so the caller can size a buffer for it.
EM_JS(int, GpJsClipboardLen, (), {
    const t = globalThis.__gpuiClip;
    return t ? globalThis.__gpui.u8len(t) : 0;
});

EM_JS(void, GpJsClipboardRead, (char* out, int cap), {
    const t = globalThis.__gpuiClip || "";
    const b = new TextEncoder().encode(t);
    const n = Math.min(b.length, cap);
    HEAPU8.set(b.subarray(0, n), out);
});

EM_JS(int, GpJsReduceMotion, (), {
    return globalThis.matchMedia &&
           globalThis.matchMedia("(prefers-reduced-motion: reduce)").matches
        ? 1 : 0;
});

EM_JS(int, GpJsFullscreen, (), {
    return document.fullscreenElement ? 1 : 0;
});

EM_JS(void, GpJsToggleFullscreen, (), {
    if (document.fullscreenElement) {
        document.exitFullscreen();
    } else if (document.documentElement.requestFullscreen) {
        document.documentElement.requestFullscreen().catch(function() {});
    }
});

// The paste chord is driven by the DOM `paste` event rather than by its
// keydown: the event is where the text actually is, and reading
// navigator.clipboard instead would answer a promise the keystroke cannot
// wait for. The keydown lets ctrl-V through untouched so this fires, and
// this then raises the chord with the mirror already filled in.
EM_JS(void, GpJsInstallClipboard, (), {
    globalThis.__gpuiClip = globalThis.__gpuiClip || "";
    document.addEventListener("paste", function(e) {
        if (!e.clipboardData) {
            return;
        }
        globalThis.__gpuiClip = e.clipboardData.getData("text/plain") || "";
        e.preventDefault();
        _gpui_wasm_paste();
    });
});
// clang-format on

// ─── waking and repainting ────────────────────────────────────────────────

// Called from JavaScript when something the page owns has finished and the
// frame that asked for it was drawn without it: a picture that has decoded,
// or a paste that has arrived. Both are reasons to draw again.
extern "C" EMSCRIPTEN_KEEPALIVE void gpui_wasm_wake(void) {
    if (gWin && gWin->plat) {
        gWin->plat->dirty = true;
    }
}

extern "C" EMSCRIPTEN_KEEPALIVE void gpui_wasm_paste(void) {
    if (!gWin || !gWin->plat) {
        return;
    }
    // ctrl-V everywhere, cmd-V on a Mac keyboard: the keymap binds
    // `cmd-v` on macOS and `ctrl-v` elsewhere, and GPUI_OS_MAC is 0 for a
    // wasm build, so this is the chord the keymap is holding.
    WindowKeyDown(gWin, KeyV, false, true, false, false);
}

void AppInvalidate(Window* win) {
    if (win) {
        win->invalidations++;
    }
    if (win && win->plat) {
        win->plat->dirty = true;
    }
}

void PlatWake(App* app) {
    (void)app;
    // Nothing to do: the animation-frame loop drains the queue every pass, so
    // a worker's completion is picked up on the next frame at the latest.
    // There is no worker either — see sys/executor.h, which runs a spawned
    // job on this queue when no thread can be started.
}

static void Redraw(Window* win) {
    PlatWindow* pw = win->plat;
    if (!pw || pw->cssW <= 0 || pw->cssH <= 0) {
        return;
    }
    pw->dirty = false;
    win->paint.dpi = 96;
    WindowDrawFrame(win, (void*)pw, pw->cssW, pw->cssH, (float)pw->cssW,
                    (float)pw->cssH);
}

// ─── keys ─────────────────────────────────────────────────────────────────
//
// A DOM keyCode is a Windows VK code — that is where the numbering came from
// — so the Key* constants in gpui.h need no table at all. `code` is only
// consulted for the two bracket keys, which older engines number differently.

static int KeyFor(const EmscriptenKeyboardEvent* e) {
    int vk = (int)e->keyCode;
    if (vk == 0) {
        vk = (int)e->which;
    }
    if (vk == 0) {
        Str code = Str(e->code);
        if (StrEq(code, StrL("BracketLeft"))) {
            return KeyLeftBracket;
        }
        if (StrEq(code, StrL("BracketRight"))) {
            return KeyRightBracket;
        }
    }
    return vk;
}

// The one codepoint `key` names, or 0 when it names a key rather than a
// character ("Enter", "ArrowLeft", "Shift").
static uint32_t CharOf(const EmscriptenKeyboardEvent* e) {
    Str key = Str(e->key);
    if (!key) {
        return 0;
    }
    uint8_t c0 = (uint8_t)key.s[0];
    int need = c0 < 0x80 ? 1 : (c0 < 0xe0 ? 2 : (c0 < 0xf0 ? 3 : 4));
    if (key.len != need) {
        // More than one codepoint: a name, not a character.
        return 0;
    }
    uint32_t cp = 0;
    if (need == 1) {
        cp = c0;
    } else if (need == 2) {
        cp = ((uint32_t)(c0 & 0x1f) << 6) | (uint8_t)(key.s[1] & 0x3f);
    } else if (need == 3) {
        cp = ((uint32_t)(c0 & 0x0f) << 12) |
             ((uint32_t)(key.s[1] & 0x3f) << 6) | (uint8_t)(key.s[2] & 0x3f);
    } else {
        cp = ((uint32_t)(c0 & 0x07) << 18) |
             ((uint32_t)(key.s[1] & 0x3f) << 12) |
             ((uint32_t)(key.s[2] & 0x3f) << 6) | (uint8_t)(key.s[3] & 0x3f);
    }
    return cp;
}

// Whether the browser should be left to do whatever it would have done. Its
// own chords — reload, the address bar, the developer tools — stay the
// browser's; everything else belongs to the element tree, which is what a
// desktop window would get.
static bool BrowserKeepsIt(const EmscriptenKeyboardEvent* e, int vk) {
    if (e->metaKey) {
        return true;
    }
    if (e->ctrlKey) {
        // The editing chords the tree binds, and nothing else. V is not one
        // of them: it is let through so the DOM paste event fires.
        return !(vk == KeyA || vk == KeyC || vk == KeyX || vk == KeyZ ||
                 vk == KeyY);
    }
    // F1..F12 and the function-key row.
    return vk >= 112 && vk <= 123;
}

static EM_BOOL OnKeyDown(int, const EmscriptenKeyboardEvent* e, void*) {
    Window* win = gWin;
    if (!win || !win->plat || !win->plat->open) {
        return EM_FALSE;
    }
    int vk = KeyFor(e);
    bool keep = BrowserKeepsIt(e, vk);
    if (vk) {
        WindowKeyDown(win, vk, e->shiftKey != 0, e->ctrlKey != 0,
                      e->altKey != 0, e->metaKey != 0);
    }
    // Backspace arrives as WM_CHAR 8 on Windows and the bound InputState
    // edits on that; the DOM only reports the key, so raise it here the way
    // the X11 window does.
    if (vk == KeyBack) {
        WindowChar(win, 8, e->ctrlKey != 0, e->altKey != 0);
        return keep ? EM_FALSE : EM_TRUE;
    }
    if (e->ctrlKey || e->metaKey || e->altKey || vk == KeyReturn ||
        vk == KeyTab || vk == KeyEscape) {
        return keep ? EM_FALSE : EM_TRUE;
    }
    uint32_t cp = CharOf(e);
    if (cp >= 32 && cp != 127) {
        WindowChar(win, cp, false, false);
    }
    return keep ? EM_FALSE : EM_TRUE;
}

static EM_BOOL OnKeyUp(int, const EmscriptenKeyboardEvent* e, void*) {
    Window* win = gWin;
    if (!win || !win->plat || !win->plat->open) {
        return EM_FALSE;
    }
    int vk = KeyFor(e);
    if (vk) {
        WindowKeyUp(win, vk, e->shiftKey != 0, e->ctrlKey != 0, e->altKey != 0,
                    e->metaKey != 0);
    }
    return BrowserKeepsIt(e, vk) ? EM_FALSE : EM_TRUE;
}

// ─── the mouse ────────────────────────────────────────────────────────────
//
// The canvas fills the viewport and sits at its top left, so a clientX is
// already a window coordinate and nothing has to ask for a bounding box on
// the way through.

static Modifiers ModsOf(const EmscriptenMouseEvent* e) {
    Modifiers m;
    m.control = e->ctrlKey != 0;
    m.alt = e->altKey != 0;
    m.shift = e->shiftKey != 0;
    m.platform = e->metaKey != 0;
    return m;
}

static bool ButtonOf(unsigned short b, MouseButton* out) {
    switch (b) {
        case 0:
            *out = MouseButton::Left;
            return true;
        case 1:
            *out = MouseButton::Middle;
            return true;
        case 2:
            *out = MouseButton::Right;
            return true;
        case 3:
            *out = MouseButton::NavigateBack;
            return true;
        case 4:
            *out = MouseButton::NavigateForward;
            return true;
        default:
            return false;
    }
}

// Rust's Option<MouseButton> on a move: the first button held in the mask.
static bool HeldButton(unsigned short buttons, MouseButton* out) {
    if (buttons & 1) {
        *out = MouseButton::Left;
        return true;
    }
    if (buttons & 4) {
        *out = MouseButton::Middle;
        return true;
    }
    if (buttons & 2) {
        *out = MouseButton::Right;
        return true;
    }
    return false;
}

static EM_BOOL OnMouseDown(int, const EmscriptenMouseEvent* e, void*) {
    Window* win = gWin;
    if (!win || !win->plat || !win->plat->open) {
        return EM_FALSE;
    }
    float x = (float)e->clientX;
    float y = (float)e->clientY;
    MouseButton button = MouseButton::Left;
    if (!ButtonOf(e->button, &button)) {
        return EM_FALSE;
    }
    Modifiers mods = ModsOf(e);
    // Before the chrome, because the chrome needs the answer: the caption
    // zooms on the second click.
    int clicks = WindowClickCount(win, x, y, button);
    if (button == MouseButton::Left) {
        int chrome = WindowChromeHit(win, x, y);
        if (chrome == ClickWinMin) {
            AppMinimize(win);
            return EM_TRUE;
        }
        if (chrome == ClickWinMax) {
            AppToggleMaximize(win);
            return EM_TRUE;
        }
        if (chrome == ClickWinClose) {
            AppClose(win);
            return EM_TRUE;
        }
        if (chrome == ClickWinCaption) {
            if (clicks == 2) {
                AppToggleMaximize(win);
            }
            // A single press on the caption would start a window drag on a
            // desktop. A tab cannot move itself, so it does nothing.
            return EM_TRUE;
        }
    }
    PlatformInput in = InputMouseDown(button, x, y, mods, clicks, false);
    WindowDispatchInput(win, &in);
    return EM_TRUE;
}

static EM_BOOL OnMouseUp(int, const EmscriptenMouseEvent* e, void*) {
    Window* win = gWin;
    if (!win || !win->plat || !win->plat->open) {
        return EM_FALSE;
    }
    MouseButton button = MouseButton::Left;
    if (!ButtonOf(e->button, &button)) {
        return EM_FALSE;
    }
    PlatformInput in =
        InputMouseUp(button, (float)e->clientX, (float)e->clientY, ModsOf(e),
                     WindowCurrentClickCount(win));
    WindowDispatchInput(win, &in);
    return EM_TRUE;
}

static EM_BOOL OnMouseMove(int, const EmscriptenMouseEvent* e, void*) {
    Window* win = gWin;
    if (!win || !win->plat || !win->plat->open) {
        return EM_FALSE;
    }
    MouseButton held = MouseButton::Left;
    bool any = HeldButton(e->buttons, &held);
    PlatformInput in = InputMouseMove((float)e->clientX, (float)e->clientY, any,
                                      held, ModsOf(e));
    WindowDispatchInput(win, &in);
    return EM_TRUE;
}

static EM_BOOL OnMouseLeave(int, const EmscriptenMouseEvent* e, void*) {
    Window* win = gWin;
    if (!win || !win->plat || !win->plat->open) {
        return EM_FALSE;
    }
    MouseButton held = MouseButton::Left;
    bool any = HeldButton(e->buttons, &held);
    PlatformInput in = InputMouseExited((float)e->clientX, (float)e->clientY,
                                        any, held, ModsOf(e));
    WindowDispatchInput(win, &in);
    return EM_TRUE;
}

static EM_BOOL OnWheel(int, const EmscriptenWheelEvent* e, void*) {
    Window* win = gWin;
    if (!win || !win->plat || !win->plat->open) {
        return EM_FALSE;
    }
    // A DOM wheel delta grows downwards and rightwards; GPUI's grows the way
    // the content moves, so both are negated. A line-mode delta gets the same
    // notch that the Windows and X11 windows use; a pixel-mode one
    // is already in the units this tree draws in.
    float scale = 1.f;
    if (e->deltaMode == DOM_DELTA_LINE) {
        scale = WheelNotchPixels(win->app);
    } else if (e->deltaMode == DOM_DELTA_PAGE) {
        scale = (float)win->plat->cssH;
    }
    PlatformInput in = InputScrollWheel(
        (float)e->mouse.clientX, (float)e->mouse.clientY,
        -(float)e->deltaX * scale, -(float)e->deltaY * scale,
        e->deltaMode == DOM_DELTA_PIXEL, ModsOf(&e->mouse), TouchPhase::Moved);
    WindowDispatchInput(win, &in);
    return EM_TRUE;
}

static EM_BOOL OnResize(int, const EmscriptenUiEvent*, void*) {
    Window* win = gWin;
    if (!win || !win->plat) {
        return EM_FALSE;
    }
    int w = 0, h = 0;
    GpJsViewport(&w, &h);
    if (w != win->plat->cssW || h != win->plat->cssH) {
        win->plat->cssW = w;
        win->plat->cssH = h;
        win->plat->dirty = true;
    }
    return EM_TRUE;
}

static EM_BOOL OnFocus(int type, const EmscriptenFocusEvent*, void*) {
    if (gWin) {
        WindowSetActive(gWin, type == EMSCRIPTEN_EVENT_FOCUS);
    }
    return EM_FALSE;
}

// ─── window commands ──────────────────────────────────────────────────────

static void DestroyPlatWindow(Window* win) {
    PlatWindow* pw = win ? win->plat : nullptr;
    if (!pw) {
        return;
    }
    pw->open = false;
    delete pw;
    // Clears win->plat and stops the window; the loop then cancels itself.
    WindowClosed(win);
    if (gWin == win) {
        gWin = nullptr;
    }
}

void AppQuit(Window* win) {
    if (win && win->plat) {
        DestroyPlatWindow(win);
    }
}

bool WindowClientDecorated(Window* win) {
    // A tab has no frame at all, so what the page draws is all there is.
    return win && (win->opts.clientTitleBar || win->opts.borderless);
}

// A tab cannot raise itself either — only the browser decides which one is
// in front, and window.focus() is ignored for anything but a popup a script
// opened.
void AppActivate(Window* win) {
    (void)win;
}

// A tab cannot iconify itself. GPUI's minimize has no meaning here and the
// title bar's control is left drawing nothing.
void AppMinimize(Window* win) {
    (void)win;
}

void AppToggleMaximize(Window* win) {
    if (!win || !win->plat) {
        return;
    }
    GpJsToggleFullscreen();
    win->maximized = !GpJsFullscreen();
    win->plat->dirty = true;
}

// The window is the tab, and the tab belongs to the browser.
void AppDrag(Window* win) {
    (void)win;
}

void AppSetTitle(Window* win, Str title) {
    (void)win;
    if (title.s) {
        GpJsSetTitle(title.s, title.len);
    }
}

void PlatSetTimer(Window* win, int ms) {
    if (!win || !win->plat) {
        return;
    }
    win->plat->nextTick = ms > 0 ? TimeNow() + ms / 1000.0 : 0;
}

void PlatSetCursor(Window* win, CursorKind kind) {
    (void)win;
    GpJsSetCursor((int)kind);
}

// The browser holds the pointer for a press on its own — a mousemove and a
// mouseup outside the canvas still reach a listener on the document, which is
// where the two are registered — so there is nothing to take or give back.
void PlatSetMouseCapture(Window* win, bool capture) {
    (void)win;
    (void)capture;
}

// No setting to read: 500 ms is what every engine uses for dblclick.
int PlatDoubleClickMs() {
    return 500;
}

// A tab is not told what its panel runs at, so the headline stays uncapped.
uint64_t PlatWindowDisplay(Window*) {
    return 0;
}

double PlatDisplayRefreshPeriod(uint64_t) {
    return 0;
}

void* PlatWindowHandle(Window*) {
    // A tab has no window handle; whatever would want one draws into the
    // same canvas everything else does.
    return nullptr;
}

void PlatInstallAccessibilityHitTest(Window* win) {
    (void)win;
}

void PlatAccessibilityTreeChanged(Window* win) {
    (void)win;
}

void PlatAccessibilityFocusChanged(Window* win, int focusId) {
    (void)win;
    (void)focusId;
}

// A page has no popup menu of its own, so the caller draws one — Rust's
// FallbackMenuOverlay, which is what the X11 window gets too.
bool PlatHasMenu() {
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
    // A tab has no menu bar to put anything in.
    return false;
}

void PlatSetAppMenu(App* app, const PlatMenuItem* items, int n) {
    (void)app;
    (void)items;
    (void)n;
}

bool PlatReduceMotion() {
    return GpJsReduceMotion() != 0;
}

void OpenUrl(Str url) {
    if (url.s && url.len > 0) {
        GpJsOpenUrl(url.s, url.len);
    }
}

// cx.prompt_for_paths. A page cannot open a file picker of its own and read
// what it chose without a user gesture and a callback, and nothing here has
// asked for one, so this says so rather than pretending.
TempStr PromptForPathTemp(Window* win, const PathPrompt& opts) {
    (void)win;
    (void)opts;
    return {};
}

void ClipboardSetText(Window* win, Str text) {
    (void)win;
    if (text.s && text.len > 0) {
        GpJsClipboardWrite(text.s, text.len);
    }
}

void WindowSetTextContentType(Window* win, Str value) {
    (void)win;
    (void)value;
}

Str ClipboardGetText(Arena* a, Window* win) {
    (void)win;
    int n = GpJsClipboardLen();
    if (n <= 0) {
        return {};
    }
    Str tmp = AllocStrTemp(n);
    if (!tmp.s) {
        return {};
    }
    GpJsClipboardRead(tmp.s, n);
    return StrDup(a, tmp);
}

// ─── app lifecycle ────────────────────────────────────────────────────────

bool PlatInit(App* app) {
    (void)app;
    return true;
}

void PlatShutdown(App* app) {
    (void)app;
    if (gClipboard.s) {
        StrFree(gClipboard);
        gClipboard = {};
    }
}

Window* WindowOpen(App* app, Str title, int dipW, int dipH, WinOpts opts) {
    (void)dipW;
    (void)dipH;
    if (gWin) {
        logf("gpui/wasm: a tab has one canvas, so it has one window");
        return nullptr;
    }
    Window* win = WindowAlloc(app, opts);
    if (!win) {
        return nullptr;
    }
    auto* pw = new PlatWindow();
    win->plat = pw;
    gWin = win;

    // The requested size is the desktop's business. A page is as big as its
    // viewport, which is where the window's size comes from instead.
    int w = 0, h = 0;
    GpJsAttach(&w, &h);
    pw->cssW = w;
    pw->cssH = h;

    AppSetTitle(win, title);
    GpJsInstallClipboard();

    const char* canvas = kCanvasSel;
    // Press and wheel on the canvas; move and release on the document, so a
    // drag that leaves the canvas still delivers its moves and its release.
    emscripten_set_mousedown_callback(canvas, nullptr, EM_FALSE, OnMouseDown);
    emscripten_set_mouseup_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, nullptr,
                                    EM_FALSE, OnMouseUp);
    emscripten_set_mousemove_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, nullptr,
                                      EM_FALSE, OnMouseMove);
    emscripten_set_mouseleave_callback(canvas, nullptr, EM_FALSE, OnMouseLeave);
    emscripten_set_wheel_callback(canvas, nullptr, EM_FALSE, OnWheel);
    emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr,
                                    EM_FALSE, OnKeyDown);
    emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr,
                                  EM_FALSE, OnKeyUp);
    emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr,
                                   EM_FALSE, OnResize);
    emscripten_set_focus_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr,
                                  EM_FALSE, OnFocus);
    emscripten_set_blur_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr,
                                 EM_FALSE, OnFocus);

    PlatSetTimer(win, WindowTimerMs(win));
    return win;
}

// One pass of the loop, run from an animation frame. The same three things
// the X11 loop does between polls: drain what the main thread was handed,
// fire whatever timers are due, and repaint a window that has asked for it.
static void Tick(void* arg) {
    App* app = (App*)arg;
    ExecDrain();

    if (!AppAnyWindowOpen(app)) {
        emscripten_cancel_main_loop();
        return;
    }

    double now = TimeNow();
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
    for (int i = 0; i < app->windows.len; i++) {
        Window* w = app->windows[i];
        if (w->plat && w->plat->dirty && w->plat->open) {
            Redraw(w);
        }
    }
}

// Does not return. emscripten_set_main_loop with simulate_infinite_loop
// unwinds this stack and gives the tab back to the browser, which then calls
// Tick from an animation frame for as long as a window is open. There is no
// other shape available: a C loop that blocked would block the page.
int AppRun(App* app) {
    if (!app) {
        return 1;
    }
    // 0 fps means requestAnimationFrame, which is the display's own cadence
    // and what AppRequestAnim wants anyway.
    emscripten_set_main_loop_arg(Tick, app, 0, 1);
    return app->exitCode;
}

} // namespace gpui

// The process entry point. Examples implement GpuiMain(argc, argv).
int main(int argc, char** argv) {
    // Strip the -gpui-* flags here too, so an example parses the same argv on
    // every platform.
    argc = gpui::GpuiTakeRuntimeArgs(argc, argv);
    return GpuiMain(argc, argv);
}
