/* gpui's TestAppContext and VisualTestContext, over no OS window. See
   test_app.h. Portable: everything here goes through platform.h's shared
   half and the two test hooks every platform's clock and clipboard ask. */

#include "gpui/test_app.h"

#include "gpui/keymap.h"
#include "gpui/platform.h"
#include "sys/executor.h"
#include "base/input_keys.h"

namespace gpui {

// The test platform's clock and clipboard. A real instant is never 0, and a
// handful of fields use 0 for "never", so the clock starts well clear of it.
struct TestPlatform {
    bool on = false;
    double now = 0;
    Str clipboard = {};
};

static TestPlatform gTestPlatform;
static const double kTestClockStart = 1000.0;
// A run that has not settled after this many rounds is a frame or a timer
// asking for itself forever, which no test means to wait out.
static const int kMaxParkRounds = 1000;

bool TestPlatformNow(double* out) {
    if (!gTestPlatform.on) {
        return false;
    }
    if (out) {
        *out = gTestPlatform.now;
    }
    return true;
}

bool TestPlatformClipboardWrite(Str text) {
    if (!gTestPlatform.on) {
        return false;
    }
    StrFree(gTestPlatform.clipboard);
    gTestPlatform.clipboard = len(text) > 0 ? StrDup(text) : Str{};
    return true;
}

bool TestPlatformClipboardRead(Arena* a, ClipboardItem* out) {
    if (!gTestPlatform.on) {
        return false;
    }
    if (out) {
        *out = ClipboardItem{};
        if (len(gTestPlatform.clipboard) > 0) {
            out->text = StrDup(a, gTestPlatform.clipboard);
        }
    }
    return true;
}

double TestClockNow() {
    return gTestPlatform.now;
}

App* TestAppNew() {
    App* app = AppNewHeadless();
    if (!app) {
        return nullptr;
    }
    StrFree(gTestPlatform.clipboard);
    gTestPlatform = TestPlatform{};
    gTestPlatform.on = true;
    gTestPlatform.now = kTestClockStart;
    return app;
}

void TestAppFree(App* app) {
    AppFreeHeadless(app);
    StrFree(gTestPlatform.clipboard);
    gTestPlatform = TestPlatform{};
}

Window* TestWindowOpen(App* app, EntityId root, float w, float h, float scale) {
    if (!app) {
        return nullptr;
    }
    Window* win = WindowAlloc(app, WinOpts{});
    if (!win) {
        return nullptr;
    }
    win->root = root;
    win->paint.dpi = 96.f * (scale > 0 ? scale : 1.f);
    win->paint.viewW = w;
    win->paint.viewH = h;
    // A window that has never drawn is dirty: opening one ends with its
    // first frame, as the update that opened it flushes.
    TestDraw(win);
    return win;
}

void TestDraw(Window* win) {
    if (!win) {
        return;
    }
    WindowDrawFrameHeadless(win, win->paint.viewW, win->paint.viewH);
}

static bool DrawDirty(App* app) {
    bool drew = false;
    for (int i = 0; i < app->windows.len; i++) {
        Window* win = app->windows[i];
        if (win && win->invalidations > 0) {
            TestDraw(win);
            drew = true;
        }
    }
    return drew;
}

void TestFlushEffects(App* app) {
    if (!app) {
        return;
    }
    // Drawing can invalidate another window (an observer of something a view
    // read), so a pass that drew is followed by another until none does.
    for (int round = 0; round < kMaxParkRounds && DrawDirty(app); round++) {
    }
}

void TestSimulateResize(Window* win, float w, float h) {
    if (!win) {
        return;
    }
    win->paint.viewW = w;
    win->paint.viewH = h;
    TestDraw(win);
    TestFlushEffects(win->app);
}

// The soonest instant something on `win` wants the clock to reach, or -1.
// The same deadlines WindowTimerMs reads, without its rounding to whole
// milliseconds: the simulated clock steps onto a deadline exactly.
static double NextDeadline(Window* win) {
    double soonest = -1;
    for (int i = 0; i < win->timers.len; i++) {
        double due = win->timers[i].dueAt;
        if (due > 0 && (soonest < 0 || due < soonest)) {
            soonest = due;
        }
    }
    if (win->scrollDragNotifyPending && win->scrollDragNotifyDue > 0 &&
        (soonest < 0 || win->scrollDragNotifyDue < soonest)) {
        soonest = win->scrollDragNotifyDue;
    }
    if (win->anim || win->opts.anim || win->animFrame) {
        // WindowTimerTick's own pacing: one frame every 16 ms.
        double due = win->lastDrawTime + 0.016;
        if (soonest < 0 || due < soonest) {
            soonest = due;
        }
    }
    return soonest;
}

static bool FireDueTimers(App* app) {
    bool fired = false;
    double now = gTestPlatform.now;
    for (int i = 0; i < app->windows.len; i++) {
        Window* win = app->windows[i];
        if (!win) {
            continue;
        }
        double due = NextDeadline(win);
        if (due > 0 && due <= now) {
            uint64_t before = win->invalidations;
            int timersBefore = win->timers.len;
            WindowTimerTick(win);
            // A tick that only re-armed an animation frame nothing is
            // waiting on has done nothing a test could see.
            if (win->invalidations != before ||
                win->timers.len != timersBefore || NextDeadline(win) != due) {
                fired = true;
            }
        }
    }
    return fired;
}

void TestRunUntilParked(App* app) {
    if (!app) {
        return;
    }
    for (int round = 0; round < kMaxParkRounds; round++) {
        bool busy = false;
        if (ExecDrain() > 0) {
            busy = true;
        }
        if (ExecPending() > 0) {
            ExecWaitIdle(10000);
            busy = true;
        }
        if (FireDueTimers(app)) {
            busy = true;
        }
        if (DrawDirty(app)) {
            busy = true;
        }
        if (!busy && ExecQueued() == 0) {
            return;
        }
    }
}

void TestAdvanceClock(App* app, double ms) {
    if (!app) {
        return;
    }
    TestRunUntilParked(app);
    double target = gTestPlatform.now + ms / 1000.0;
    // Step from deadline to deadline, so each timer fires at its own instant
    // and sees the clock where it expected it.
    for (int round = 0; round < kMaxParkRounds * 10; round++) {
        double soonest = -1;
        for (int i = 0; i < app->windows.len; i++) {
            Window* win = app->windows[i];
            if (!win) {
                continue;
            }
            double due = NextDeadline(win);
            if (due > 0 && (soonest < 0 || due < soonest)) {
                soonest = due;
            }
        }
        if (soonest < 0 || soonest > target) {
            break;
        }
        if (soonest > gTestPlatform.now) {
            gTestPlatform.now = soonest;
        }
        TestRunUntilParked(app);
        // A deadline that did not move after its turn would hold the clock
        // where it is; step past it.
        double after = -1;
        for (int i = 0; i < app->windows.len; i++) {
            Window* win = app->windows[i];
            double due = win ? NextDeadline(win) : -1;
            if (due > 0 && (after < 0 || due < after)) {
                after = due;
            }
        }
        if (after > 0 && after <= gTestPlatform.now) {
            break;
        }
    }
    gTestPlatform.now = target;
    TestRunUntilParked(app);
}

// ─── keyboard ─────────────────────────────────────────────────────────────

// The character a keystroke types under Rust's with_simulated_ime: the key
// itself when it is one character (upper case under shift), a space for
// "space", and nothing for any other named key or for a chord.
static uint32_t TypedChar(Str key, const KeyChord& chord) {
    if (chord.ctrl || chord.alt || chord.platform || chord.function) {
        return 0;
    }
    if (base::StrEq(key, "space")) {
        return ' ';
    }
    if (len(key) != 1) {
        return 0;
    }
    char c = key.s[0];
    if (chord.shift && c >= 'a' && c <= 'z') {
        c = (char)(c - 'a' + 'A');
    }
    return (uint32_t)(uint8_t)c;
}

// Window::dispatch_keystroke: the key down, and then the character it types
// through the input handler, which is the order a platform delivers them.
static void PressKeystroke(Window* win, Str stroke) {
    int n = len(stroke);
    if (n == 0 || n > 48) {
        return;
    }
    // The key is what follows the last `-`, unless the stroke ends in one,
    // in which case the key is `-` itself.
    int keyAt = 0;
    for (int i = 0; i < n - 1; i++) {
        if (stroke.s[i] == '-') {
            keyAt = i + 1;
        }
    }
    // Keystroke::parse reads a lone capital as shift plus the lowercase key.
    char spec[64];
    int at = 0;
    char last = stroke.s[n - 1];
    bool upper = keyAt == n - 1 && last >= 'A' && last <= 'Z';
    if (upper) {
        memcpy(spec, "shift-", 6);
        at = 6;
    }
    memcpy(spec + at, stroke.s, (size_t)n);
    at += n;
    if (upper) {
        spec[at - 1] = (char)(last - 'A' + 'a');
    }
    spec[at] = 0;
    KeyChord chord;
    if (!KeyChordParse(Str(spec, at), &chord)) {
        // A key no binding could name: a character an input method types
        // with no key of its own behind it.
        uint32_t cp = 0;
        if (keyAt == 0 && Utf8At(stroke, 0, &cp) == n) {
            WindowChar(win, cp, false, false);
        }
        return;
    }
    uint32_t ch = TypedChar(Str(spec + at - (n - keyAt), n - keyAt), chord);
    WindowKeyDown(win, chord.vk, chord.shift, chord.ctrl, chord.alt,
                  chord.platform, chord.function);
    if (ch) {
        WindowChar(win, ch, false, false);
    }
}

void TestSimulateKeystrokes(Window* win, const char* keystrokes) {
    if (!win || !keystrokes) {
        return;
    }
    const char* p = keystrokes;
    while (*p) {
        while (*p == ' ') {
            p++;
        }
        const char* start = p;
        while (*p && *p != ' ') {
            p++;
        }
        if (p > start) {
            PressKeystroke(win, Str(start, (int)(p - start)));
            TestFlushEffects(win->app);
        }
    }
    TestRunUntilParked(win->app);
}

void TestSimulateInput(Window* win, Str text) {
    if (!win) {
        return;
    }
    int i = 0;
    while (i < len(text)) {
        uint32_t cp = 0;
        int step = Utf8At(text, i, &cp);
        if (step <= 0) {
            break;
        }
        Str one = Str(text.s + i, step);
        i += step;
        if (cp < 0x80 && cp > ' ' && cp != 0x7f) {
            PressKeystroke(win, one);
        } else if (cp == ' ') {
            PressKeystroke(win, StrL("space"));
        } else {
            WindowChar(win, cp, false, false);
        }
        TestFlushEffects(win->app);
    }
    TestRunUntilParked(win->app);
}

// ─── mouse ────────────────────────────────────────────────────────────────

static void Simulate(Window* win, const PlatformInput& input) {
    if (!win) {
        return;
    }
    WindowDispatchInput(win, &input);
    TestFlushEffects(win->app);
    TestRunUntilParked(win->app);
}

void TestSimulateMouseMove(Window* win, Point position, bool pressed,
                           MouseButton button, Modifiers modifiers) {
    Simulate(win, InputMouseMove(position.x, position.y, pressed, button,
                                 modifiers));
}

void TestSimulateMouseDown(Window* win, Point position, MouseButton button,
                           Modifiers modifiers) {
    Simulate(win, InputMouseDown(button, position.x, position.y, modifiers, 1,
                                 false));
}

void TestSimulateMouseUp(Window* win, Point position, MouseButton button,
                         Modifiers modifiers) {
    Simulate(win, InputMouseUp(button, position.x, position.y, modifiers, 1));
}

void TestSimulateClick(Window* win, Point position, Modifiers modifiers) {
    TestSimulateMouseDown(win, position, MouseButton::Left, modifiers);
    TestSimulateMouseUp(win, position, MouseButton::Left, modifiers);
}

// ─── actions, clipboard, focus ────────────────────────────────────────────

bool TestDispatchAction(Window* win, uint32_t action, intptr_t arg) {
    if (!win || !action) {
        return false;
    }
    bool taken = false;
    // The focused field's own handlers are the innermost on the chain, as
    // they are for a keystroke bound to the action (WindowKeyDown).
    if (win->input && win->input->focused) {
        InputAction act = InputActionOf(action, arg);
        if (act != InputAction::None) {
            bool shift = act == InputAction::Enter && InputEnterShift(arg);
            taken = InputPerform(win->input, win->app, win, act, shift);
        }
    }
    if (!taken) {
        taken = WindowDispatchAction(win, action, arg);
    }
    TestFlushEffects(win->app);
    TestRunUntilParked(win->app);
    return taken;
}

void TestWriteToClipboard(Str text) {
    TestPlatformClipboardWrite(text);
}

Str TestReadFromClipboard(Arena* a) {
    ClipboardItem item;
    if (!TestPlatformClipboardRead(a, &item)) {
        return {};
    }
    return item.text;
}

void TestFocus(Window* win, FocusHandle handle) {
    if (!win) {
        return;
    }
    FocusHandleFocus(win, handle);
    AppInvalidate(win);
    TestFlushEffects(win->app);
}

} // namespace gpui
