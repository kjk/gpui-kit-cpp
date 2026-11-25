/* Ported from crates/ui/src/kbd.rs.
 *
 * `Kbd::format` is the platform's spelling of a keystroke: the modifiers in
 * the order that platform lists them, joined the way it joins them, then the
 * key — named where it has a name, capitalised where it does not. The pinned
 * Rust test has separate macOS and non-macOS answers; keep both here. */

#include "Test.h"

using namespace gpui::component;

enum ExpectedKbd {
    ExpectedCtrlP,
    ExpectedAllModifiers,
    ExpectedShiftPlatformT,
    ExpectedEscape,
    ExpectedBackspace,
    ExpectedLeft,
    ExpectedEnter,
    ExpectedShortBuffer,
};

#if GPUI_OS_MAC
static const char* const kExpected[] = {
    "⌃P", "⌃⌥⇧⌘P", "⇧⌘T", "⎋", "⌫", "←", "⏎", "⌃",
};
static const int kShortBufferCap = 4;
#else
static const char* const kExpected[] = {
    "Ctrl+P",      "Ctrl+Alt+Shift+Win+P",
    "Shift+Win+T", "Esc",
    "Backspace",   "Left",
    "Enter",       "Ctrl+",
};
static const int kShortBufferCap = 6;
#endif

static TempStr KbdFmtTemp(Keystroke k, int cap = 64) {
    TempStr buf = AllocStrTemp(cap - 1);
    buf.len = KbdFormat(k, buf.s, cap);
    return buf;
}

static void TheModifiersComeFirstInPlatformOrder() {
    Keystroke k;
    k.key = StrL("p");
    k.ctrl = true;
    utassert(base::StrEq(KbdFmtTemp(k), kExpected[ExpectedCtrlP]));

    k = Keystroke{};
    k.key = StrL("p");
    k.ctrl = true;
    k.alt = true;
    k.shift = true;
    k.platform = true;
    // Ctrl, then Alt, then Shift, then the platform key — Rust's order,
    // spelled as symbols with no separators on macOS.
    utassert(base::StrEq(KbdFmtTemp(k), kExpected[ExpectedAllModifiers]));

    k = Keystroke{};
    k.key = StrL("t");
    k.shift = true;
    k.platform = true;
    utassert(base::StrEq(KbdFmtTemp(k), kExpected[ExpectedShiftPlatformT]));
}

static void ANamedKeyKeepsItsName() {
    Keystroke k;
    k.key = StrL("escape");
    utassert(base::StrEq(KbdFmtTemp(k), kExpected[ExpectedEscape]));
    k.key = StrL("backspace");
    utassert(base::StrEq(KbdFmtTemp(k), kExpected[ExpectedBackspace]));
    k.key = StrL("pagedown");
    utassert(base::StrEq(KbdFmtTemp(k), StrL("Page Down")));
    k.key = StrL("pageup");
    utassert(base::StrEq(KbdFmtTemp(k), StrL("Page Up")));
    k.key = StrL("left");
    utassert(base::StrEq(KbdFmtTemp(k), kExpected[ExpectedLeft]));
    k.key = StrL("enter");
    utassert(base::StrEq(KbdFmtTemp(k), kExpected[ExpectedEnter]));
}

static void AnythingElseIsCapitalised() {
    Keystroke k;
    // One character is upper-cased...
    k.key = StrL("c");
    utassert(base::StrEq(KbdFmtTemp(k), StrL("C")));
    // ...and a symbol is left as it is.
    k.key = StrL("/");
    utassert(base::StrEq(KbdFmtTemp(k), StrL("/")));
    k.key = StrL("-");
    utassert(base::StrEq(KbdFmtTemp(k), StrL("-")));
    // A longer name keeps its spelling with the first letter raised, which is
    // how the keys with no table entry of their own read.
    k.key = StrL("home");
    utassert(base::StrEq(KbdFmtTemp(k), StrL("Home")));
    k.key = StrL("f12");
    utassert(base::StrEq(KbdFmtTemp(k), StrL("F12")));
    k.key = StrL("space");
    utassert(base::StrEq(KbdFmtTemp(k), StrL("Space")));
}

static void ABufferTooSmallStillEndsTheString() {
    Keystroke k;
    k.ctrl = true;
    k.key = StrL("c");
    TempStr buf = KbdFmtTemp(k, kShortBufferCap);
    int n = len(buf);
    Str expected = Str(kExpected[ExpectedShortBuffer]);
    utassert(n == len(expected));
    utassert(buf.s[n] == 0);
    utassert(base::StrEq(buf, expected));
}

// Kbd::binding_for_action_in: the shortcut a row shows is looked up in the
// keymap rather than typed by the caller, so it cannot drift from what is
// actually bound. The input's chords are the ones a menu shows most.
static void AShortcutComesFromTheBinding() {
    InputInitKeys();
    Keystroke k;
    // The field's actions live in its own key context, and asking without one
    // finds nothing — which is the same rule the matcher applies.
    utassert(!KeystrokeForAction(input::Copy(), nullptr, &k));
    utassert(KeystrokeForAction(input::Copy(), "Input", &k));
    utassert(base::StrEq(k.key, StrL("c")));
#if GPUI_OS_MAC
    utassert(k.platform && !k.ctrl);
#else
    utassert(k.ctrl && !k.platform);
#endif

    // And it spells out the way this platform spells it.
    TempStr buf = KbdFmtTemp(k, 32);
    utassert(buf);
#if GPUI_OS_MAC
    utassert(base::StrEq(buf, StrL("\u2318C")));
#else
    utassert(base::StrEq(buf, StrL("Ctrl+C")));
#endif

    // An action nothing binds has no shortcut to show, which is a row with
    // nothing on its right rather than a blank box.
    utassert(
        !KeystrokeForAction(ActionOf(StrL("t::NoSuchThing")), "Input", &k));
}

// kbd.rs test_from_keybinding_keystroke.
static void AShortcutKeepsItsBoundSymbolAndModifiers() {
    uint32_t shifted = ActionOf(StrL("kbd_test::ShiftEqual"));
    uint32_t plus = ActionOf(StrL("kbd_test::Plus"));
    KeyBinding bindings[] = {{"alt-shift-=", shifted, "KbdEqual"},
                             {"alt-+", plus, "KbdPlus"}};
    KeymapBind(bindings, 2);
    Keystroke stroke;
    utassert(KeystrokeForAction(shifted, "KbdEqual", &stroke));
    utassert(stroke.alt && stroke.shift && StrEq(stroke.key, StrL("=")));
    utassert(KeystrokeForAction(plus, "KbdPlus", &stroke));
    utassert(stroke.alt && !stroke.shift && StrEq(stroke.key, StrL("+")));
    KeyChord lhs, rhs;
    utassert(KeyChordParse(StrL("alt-shift-="), &lhs));
    utassert(KeyChordParse(StrL("alt-+"), &rhs));
    utassert(KeyChordEq(lhs, rhs));
}

static void AnUnfocusedTriggerResolvesItsContextBinding() {
    KeymapClear();
    uint32_t copy = ActionOf(StrL("kbd_test::Copy"));
    KeyBinding binding = {"ctrl-c", copy, "KbdTrigger"};
    KeymapBind(&binding, 1);

    Arena* a = ArenaNew();
    App app;
    Window* win = new Window();
    win->app = &app;
    Ctx cx = {};
    cx.app = &app;
    cx.win = win;
    FocusHandle trigger = FocusHandleNew(&app);
    El* root =
        Div(a)
            ->Child(Div(a)
                        ->KeyContext(StrL("KbdTrigger"))
                        ->Child(Div(a)->TrackFocus(trigger)->TabStop(false)))
            ->Child(Div(a));
    FocusCollect(win, root);

    Keystroke k;
    utassert(WindowFocusedId(win) == 0);
    utassert(KeystrokeForActionAtFocus(&cx, copy, trigger, &k));
    utassert(base::StrEq(k.key, StrL("c")) && k.ctrl);

    delete win;
    ArenaDelete(a);
    KeymapClear();
}

// kbd.rs label_shows_its_text_as_given.
static void LabelShowsItsTextAsGiven() {
    Arena* arena = ArenaNew();
    App app;
    Ctx cx = {&app, nullptr, arena};
    for (Str label : {StrL("Esc"), StrL("⌘1–9"), StrL("↑↓"),
                      StrL("Enter / Tab"), StrL("cmd-a")}) {
        utassert(StrEq(Kbd::Label(&cx, label)->IntoEl()->first->text, label));
        utassert(StrEq(
            Kbd::Label(&cx, label)->Appearance(false)->IntoEl()->text, label));
    }
    Keystroke stroke;
    utassert(KeystrokeParse(arena, StrL("cmd-a"), &stroke));
    utassert(StrEq(Kbd::New(&cx, stroke)->IntoEl()->first->text,
                   KbdFormatStr(&cx, stroke)));
    ArenaDelete(arena);
    AppGlobalClear(&app);
}

void TestKbd() {
    TestSuite("kbd");
    AShortcutComesFromTheBinding();
    AShortcutKeepsItsBoundSymbolAndModifiers();
    AnUnfocusedTriggerResolvesItsContextBinding();
    TheModifiersComeFirstInPlatformOrder();
    ANamedKeyKeepsItsName();
    AnythingElseIsCapitalised();
    ABufferTooSmallStillEndsTheString();
    LabelShowsItsTextAsGiven();
}
