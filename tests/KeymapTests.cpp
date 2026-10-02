/* Ported from crates/gpui's keymap: Keystroke::parse, KeyBinding, key
 * contexts, and the dispatch that reads them — which gpui-kit builds
 * every component's keyboard on.
 *
 * An action is a name here rather than a type, so what is worth pinning is
 * the resolution: which binding a chord finds, which context wins, and how
 * far an action travels along the focused element's ancestry. */

#include "Test.h"

static void AChordIsReadTheWayRustSpellsIt() {
    KeyChord c = {};
    utassert(KeyChordParse(StrL("ctrl-c"), &c));
    utassert(c.vk == 'C' && c.ctrl && !c.shift && !c.alt);

    utassert(KeyChordParse(StrL("shift-tab"), &c));
    utassert(c.vk == KeyTab && c.shift && !c.ctrl);

    // cmd- is the platform key and stands apart from control: macOS binds
    // ctrl-backspace and cmd-backspace to different actions in the same
    // context, so folding them would lose one of the two.
    utassert(KeyChordParse(StrL("cmd-c"), &c));
    utassert(c.vk == 'C' && c.platform && !c.ctrl);
    utassert(KeyChordParse(StrL("ctrl-cmd-space"), &c));
    utassert(c.vk == KeySpace && c.ctrl && c.platform);

    // secondary- is the shortcut modifier, which is the platform key on
    // macOS and control everywhere else — one binding, the right chord on
    // each.
    utassert(KeyChordParse(StrL("secondary-enter"), &c));
#if GPUI_OS_MAC
    utassert(c.vk == KeyReturn && c.platform && !c.ctrl);
#else
    utassert(c.vk == KeyReturn && c.ctrl && !c.platform);
#endif

    // Two chords that differ only in the platform key are two chords.
    KeyChord withCmd = {}, withCtrl = {};
    utassert(KeyChordParse(StrL("cmd-backspace"), &withCmd));
    utassert(KeyChordParse(StrL("ctrl-backspace"), &withCtrl));
    utassert(!KeyChordEq(withCmd, withCtrl));

    utassert(KeyChordParse(StrL("ctrl-shift-alt-i"), &c));
    utassert(c.vk == 'I' && c.ctrl && c.shift && c.alt);
    utassert(KeyChordParse(StrL("fn-left"), &c));
    utassert(c.vk == KeyLeft && c.function && !c.shift);

    KeyChord uppercase = {}, explicitShift = {};
    utassert(KeyChordParse(StrL("A"), &uppercase));
    utassert(KeyChordParse(StrL("shift-a"), &explicitShift));
    utassert(KeyChordEq(uppercase, explicitShift));

    utassert(KeyChordParse(StrL("pagedown"), &c));
    utassert(c.vk == KeyPageDown && !c.ctrl);
    utassert(KeyChordParse(StrL("f12"), &c));
    utassert(c.vk == 123);
    utassert(KeyChordParse(StrL("f24"), &c));
    utassert(c.vk == KeyF24);
    utassert(KeyChordParse(StrL("f25"), &c));
    utassert(c.vk == KeyF25);
    utassert(KeyChordParse(StrL("f35"), &c));
    utassert(c.vk == KeyF35);
    utassert(!KeyChordParse(StrL("f36"), &c));

    utassert(KeyChordParse(StrL("insert"), &c));
    utassert(c.vk == KeyInsert);
    utassert(KeyChordParse(StrL("back"), &c));
    utassert(c.vk == KeyBrowserBack);
    utassert(KeyChordParse(StrL("forward"), &c));
    utassert(c.vk == KeyBrowserForward);
    utassert(KeyChordParse(StrL("menu"), &c));
    utassert(c.vk == KeyApps && c.vk != KeyMenu && !c.alt);

    // Rust spells shifted punctuation as the character itself. It and the
    // explicit modifier spelling are the same physical chord here.
    KeyChord question = {}, shiftedSlash = {};
    utassert(KeyChordParse(StrL("?"), &question));
    utassert(KeyChordParse(StrL("shift-/"), &shiftedSlash));
    utassert(KeyChordEq(question, shiftedSlash));

    const char* linuxCommands[] = {"cut", "copy", "paste",
                                   "new", "open", "save"};
    for (const char* command : linuxCommands) {
        utassert(KeyChordParse(Str(command), &c));
        utassert(KeyName(c.vk).len > 0);
    }
    const char* keypadNames[] = {"add",     "subtract",  "multiply", "divide",
                                 "decimal", "separator", "equal",    "begin"};
    for (const char* keypad : keypadNames) {
        utassert(KeyChordParse(Str(keypad), &c));
        utassert(KeyName(c.vk).len > 0);
    }

    // A dash is only a separator when something follows it, so the key can be
    // one: "alt-f4" and "ctrl--" both read.
    utassert(KeyChordParse(StrL("alt-f4"), &c));
    utassert(c.vk == 115 && c.alt);
    utassert(KeyChordParse(StrL("ctrl--"), &c));
    utassert(c.vk == 189 && c.ctrl);

    // A binding may be written as a sequence, which reads as its chords.
    KeyChord seq[kMaxStrokes] = {};
    utassert(KeyChordsParse(StrL("ctrl-k ctrl-o"), seq, kMaxStrokes) == 2);
    utassert(seq[0].vk == 'K' && seq[0].ctrl);
    utassert(seq[1].vk == 'O' && seq[1].ctrl);
    // Longer than the matcher can hold, and a chord it cannot read, are both
    // specs it cannot read.
    utassert(KeyChordsParse(StrL("a b c d"), seq, kMaxStrokes) == 0);
    utassert(KeyChordsParse(StrL("ctrl-k nosuchkey"), seq, kMaxStrokes) == 0);

    // Nonsense is a programming mistake, not something to dispatch.
    utassert(!KeyChordParse(StrL("hyper-c"), &c));
    utassert(!KeyChordParse(StrL("ctrl-nosuchkey"), &c));
    utassert(!KeyChordParse(Str{}, &c));
}

static KeyChord Chord(const char* spec) {
    KeyChord c = {};
    KeyChordParse(Str(spec), &c);
    return c;
}

// The context a binding is scoped to is tried before the ones outside it, and
// a binding with no context at all only after every scoped one has been.
static void TheInnermostContextWins() {
    KeymapClear();
    uint32_t cancel = ActionOf(StrL("menu::Cancel"));
    uint32_t close = ActionOf(StrL("dialog::Cancel"));
    uint32_t quit = ActionOf(StrL("app::Quit"));
    KeyBinding bindings[] = {
        {"escape", quit, nullptr},
        {"escape", close, "Dialog"},
        {"escape", cancel, "PopupMenu"},
    };
    KeymapBind(bindings, 3);

    uint32_t menu = KeyContextOf(StrL("PopupMenu"));
    uint32_t dialog = KeyContextOf(StrL("Dialog"));

    // A menu inside a dialog: the menu's binding is the one that answers.
    uint32_t stack[] = {menu, dialog};
    utassert(KeymapMatch(Chord("escape"), stack, 2).action == cancel);
    // The dialog alone gets its own.
    utassert(KeymapMatch(Chord("escape"), stack + 1, 1).action == close);
    // Neither, and the unscoped binding is what is left.
    utassert(KeymapMatch(Chord("escape"), nullptr, 0).action == quit);
    // Nothing is bound to this one.
    utassert(KeymapMatch(Chord("ctrl-j"), stack, 2).action == 0);
    KeymapClear();
}

// A later binding replaces an earlier one for the same chord and context,
// which is what a keymap layered over a default one has to do.
static void TheLastBindingForAChordWins() {
    KeymapClear();
    uint32_t first = ActionOf(StrL("a::First"));
    uint32_t second = ActionOf(StrL("a::Second"));
    KeyBinding a[] = {{"ctrl-k", first, nullptr}};
    KeyBinding b[] = {{"ctrl-k", second, nullptr}};
    KeymapBind(a, 1);
    KeymapBind(b, 1);
    utassert(KeymapMatch(Chord("ctrl-k"), nullptr, 0).action == second);
    KeymapClear();
}

// A binding's context is a predicate, not just a name: it reads the
// identifiers and the key=value pairs the innermost context carries.
static void APredicateReadsTheInnermostContext() {
    uint32_t fullEditor = KeyContextOf(StrL("Editor mode=full"));
    uint32_t oneLine = KeyContextOf(StrL("Editor mode=single_line"));
    uint32_t plain = KeyContextOf(StrL("Editor"));
    uint32_t term = KeyContextOf(StrL("Terminal"));

    KeymapClear();
    uint32_t act = ActionOf(StrL("p::Act"));
    KeyBinding b[] = {{"enter", act, "Editor && mode == full"}};
    KeymapBind(b, 1);
    utassert(KeymapMatch(Chord("enter"), &fullEditor, 1).action == act);
    utassert(KeymapMatch(Chord("enter"), &oneLine, 1).action == 0);
    // No mode at all is not the mode either.
    utassert(KeymapMatch(Chord("enter"), &plain, 1).action == 0);

    // `!=` is Rust's `context.get(k) != Some(v)`, so a context that does not
    // carry the key at all satisfies it.
    KeymapClear();
    KeyBinding b2[] = {{"enter", act, "mode != full"}};
    KeymapBind(b2, 1);
    utassert(KeymapMatch(Chord("enter"), &fullEditor, 1).action == 0);
    utassert(KeymapMatch(Chord("enter"), &oneLine, 1).action == act);
    utassert(KeymapMatch(Chord("enter"), &plain, 1).action == act);

    KeymapClear();
    KeyBinding b3[] = {{"enter", act, "Editor || Terminal"}};
    KeymapBind(b3, 1);
    utassert(KeymapMatch(Chord("enter"), &plain, 1).action == act);
    utassert(KeymapMatch(Chord("enter"), &term, 1).action == act);

    KeymapClear();
    KeyBinding b4[] = {{"enter", act, "!Editor"}};
    KeymapBind(b4, 1);
    utassert(KeymapMatch(Chord("enter"), &plain, 1).action == 0);
    utassert(KeymapMatch(Chord("enter"), &term, 1).action == act);

    // A predicate that cannot be read drops the binding, the same as a chord
    // that cannot be read.
    KeymapClear();
    KeyBinding b5[] = {{"enter", act, "Editor &&"}, {"enter", act, "(Editor"}};
    KeymapBind(b5, 2);
    utassert(KeymapMatch(Chord("enter"), &plain, 1).action == 0);
    KeymapClear();
}

// `a > b` is a b whose enclosing context is an a — the one immediately
// outside it, which is what Rust evaluates the left half against.
static void AChildPredicateNeedsTheEnclosingContext() {
    uint32_t ws = KeyContextOf(StrL("Workspace"));
    uint32_t ed = KeyContextOf(StrL("Editor"));
    uint32_t pane = KeyContextOf(StrL("Pane"));

    KeymapClear();
    uint32_t act = ActionOf(StrL("c::Act"));
    KeyBinding b[] = {{"ctrl-p", act, "Workspace > Editor"}};
    KeymapBind(b, 1);

    uint32_t inside[] = {ed, ws};
    utassert(KeymapMatch(Chord("ctrl-p"), inside, 2).action == act);
    // The editor on its own has nothing outside it.
    utassert(KeymapMatch(Chord("ctrl-p"), &ed, 1).action == 0);
    // Nor does a workspace with something else in between.
    uint32_t nested[] = {ed, pane, ws};
    utassert(KeymapMatch(Chord("ctrl-p"), nested, 3).action == 0);
    // The halves are not interchangeable.
    uint32_t upsideDown[] = {ws, ed};
    utassert(KeymapMatch(Chord("ctrl-p"), upsideDown, 2).action == 0);
    KeymapClear();
}

// A binding written as a sequence holds the first chord instead of
// dispatching it, and a chord that continues nothing drops what was held.
static void ASequenceIsHeldUntilItFinishes() {
    KeymapClear();
    uint32_t open = ActionOf(StrL("s::Open"));
    KeyBinding b[] = {{"ctrl-k ctrl-o", open, nullptr}};
    KeymapBind(b, 1);

    KeyMatch m = KeymapMatch(Chord("ctrl-k"), nullptr, 0);
    utassert(!m.action && m.pending);
    utassert(KeymapPending());
    m = KeymapMatch(Chord("ctrl-o"), nullptr, 0);
    utassert(m.action == open && !m.pending);
    utassert(!KeymapPending());

    // Half of it, then something else: neither fires and nothing is left
    // held, which is Rust's matcher clearing its pending keystrokes.
    utassert(KeymapMatch(Chord("ctrl-k"), nullptr, 0).pending);
    m = KeymapMatch(Chord("x"), nullptr, 0);
    utassert(!m.action && !m.pending);
    utassert(!KeymapPending());
    KeymapClear();
}

// A complete binding beats one that is only begun, which is why "ctrl-k"
// bound beside "ctrl-k ctrl-o" still fires at once.
static void AWholeBindingBeatsOneThatIsOnlyBegun() {
    KeymapClear();
    uint32_t open = ActionOf(StrL("s::Open2"));
    uint32_t split = ActionOf(StrL("s::Split"));
    KeyBinding b[] = {
        {"ctrl-k ctrl-o", open, nullptr},
        {"ctrl-k", split, nullptr},
    };
    KeymapBind(b, 2);
    utassert(KeymapMatch(Chord("ctrl-k"), nullptr, 0).action == split);
    utassert(!KeymapPending());
    KeymapClear();
}

// A window that loses the focus drops what the keyboard was part-way
// through: the rest of the sequence is going to be typed into whatever took
// the focus, so the chord must not still be held when this window comes back.
static void BlurDropsWhatTheKeyboardWasPartWayThrough() {
    KeymapClear();
    uint32_t open = ActionOf(StrL("s::Open3"));
    KeyBinding b[] = {{"ctrl-k ctrl-o", open, nullptr}};
    KeymapBind(b, 1);

    App app;
    Window* win = new Window();
    win->app = &app;
    utassert(KeymapMatch(Chord("ctrl-k"), nullptr, 0).pending);
    // The other two the keystroke left behind: the character it is also going
    // to arrive as, and the Enter held down over a focused element.
    win->eatChar = true;
    win->keyPressPending = true;

    WindowSetActive(win, false);
    utassert(!KeymapPending());
    utassert(!win->eatChar);
    utassert(!win->keyPressPending);

    // Coming back is not a keystroke: ctrl-o alone finishes nothing.
    WindowSetActive(win, true);
    utassert(KeymapMatch(Chord("ctrl-o"), nullptr, 0).action == 0);

    delete win;
    KeymapClear();
}

// Two names are two actions, the way two action types never compare equal.
static void AnActionIsItsName() {
    utassert(ActionOf(StrL("root::Tab")) == ActionOf(StrL("root::Tab")));
    utassert(ActionOf(StrL("root::Tab")) != ActionOf(StrL("root::TabPrev")));
    utassert(ActionOf(Str{}) != 0);
}

// --- dispatch -------------------------------------------------------------

static int gCalls = 0;
static uint32_t gSeen[8];
static int64_t gLastArg = 0;

// A listener needs a live view to call into, the way GPUI's does; the test
// makes one so the dispatch is exercised end to end rather than stopping at
// a handle that points at nothing.
struct Recorder {
    static El* Render(Recorder*, Ctx* cx) { return Div(cx->a); }

    static void Stop(Recorder*, Ctx*, const ActionEvent* ev) {
        gLastArg = ev->arg;
        gSeen[gCalls++ & 7] = ev->action;
    }
    static void Pass(Recorder*, Ctx*, const ActionEvent* ev) {
        gSeen[gCalls++ & 7] = ev->action;
        // cx.propagate(): looked, did not want it.
        const_cast<ActionEvent*>(ev)->propagate = true;
    }
};

// The action goes to the focused element's own handler first and then out
// through its ancestors, and a handler that does not propagate ends it.
static void AnActionWalksOutFromWhatHasFocus() {
    KeymapClear();
    uint32_t act = ActionOf(StrL("t::Go"));
    KeyBinding b[] = {{"ctrl-g", act, "Inner"}};
    KeymapBind(b, 1);

    Arena* a = ArenaNew();
    App app;
    Window* win = new Window();
    win->app = &app;
    Entity<Recorder> rec = EntityNew<Recorder>(&app);
    El* leaf =
        Div(a)->FocusId(5)->OnAction(act, ListenTo(rec, &Recorder::Stop));
    El* mid = Div(a)->KeyContext(StrL("Inner"))->Child(leaf);
    El* root =
        Div(a)->OnAction(act, ListenTo(rec, &Recorder::Stop))->Child(mid);
    FocusCollect(win, root);
    win->focusId = 5;

    gCalls = 0;
    utassert(WindowDispatchKeyAction(win, 'G', false, true, false));
    // Only the leaf's: it kept the action.
    utassert(gCalls == 1);

    delete win;
    ArenaDelete(a);
    KeymapClear();
}

// Windows delivers an Alt chord as WM_SYSKEYDOWN rather than WM_KEYDOWN.
// WndProc asks this dispatcher whether GPUI handled it: a bound chord must be
// consumed, while an unmatched system key must remain available to Windows.
static void AnAltChordIsAHandledKeystroke() {
    KeymapClear();
    uint32_t act = ActionOf(StrL("t::AltUp"));
    KeyBinding b[] = {{"alt-up", act, nullptr}};
    KeymapBind(b, 1);

    Arena* a = ArenaNew();
    App app;
    Window* win = new Window();
    win->app = &app;
    Entity<Recorder> rec = EntityNew<Recorder>(&app);
    El* root =
        Div(a)->FocusId(5)->OnAction(act, ListenTo(rec, &Recorder::Stop));
    FocusCollect(win, root);
    win->focusId = 5;

    gCalls = 0;
    utassert(WindowKeyDown(win, KeyUp, false, false, true, false));
    utassert(gCalls == 1 && gSeen[0] == act);
    // F10 is a Windows system-menu key and is deliberately not bound here.
    utassert(!WindowKeyDown(win, 121, false, false, false, false));

    delete win;
    ArenaDelete(a);
    KeymapClear();
}

// A handler that propagates hands the action on outwards, and the outer one
// is what ends it.
static void PropagateCarriesOnOutwards() {
    KeymapClear();
    uint32_t act = ActionOf(StrL("t::Go2"));
    KeyBinding b[] = {{"ctrl-g", act, nullptr}};
    KeymapBind(b, 1);

    Arena* a = ArenaNew();
    App app;
    Window* win = new Window();
    win->app = &app;
    Entity<Recorder> rec = EntityNew<Recorder>(&app);
    El* leaf =
        Div(a)->FocusId(5)->OnAction(act, ListenTo(rec, &Recorder::Pass));
    El* root =
        Div(a)->OnAction(act, ListenTo(rec, &Recorder::Stop))->Child(leaf);
    FocusCollect(win, root);
    win->focusId = 5;

    gCalls = 0;
    utassert(WindowDispatchKeyAction(win, 'G', false, true, false));
    utassert(gCalls == 2);
    utassert(gSeen[0] == act && gSeen[1] == act);

    delete win;
    ArenaDelete(a);
    KeymapClear();
}

// A binding scoped to a context nothing along the focused ancestry declares
// does not match, however deeply the element is nested.
static void AContextOffThePathDoesNotMatch() {
    KeymapClear();
    uint32_t act = ActionOf(StrL("t::Go3"));
    KeyBinding b[] = {{"ctrl-g", act, "Elsewhere"}};
    KeymapBind(b, 1);

    Arena* a = ArenaNew();
    App app;
    Window* win = new Window();
    win->app = &app;
    Entity<Recorder> rec = EntityNew<Recorder>(&app);
    // The context is on a sibling, not on an ancestor.
    El* leaf =
        Div(a)->FocusId(5)->OnAction(act, ListenTo(rec, &Recorder::Stop));
    El* root = Div(a)
                   ->Child(Div(a)->KeyContext(StrL("Elsewhere")))
                   ->Child(Div(a)->Child(leaf));
    FocusCollect(win, root);
    win->focusId = 5;

    gCalls = 0;
    utassert(!WindowDispatchKeyAction(win, 'G', false, true, false));
    utassert(gCalls == 0);

    delete win;
    ArenaDelete(a);
    KeymapClear();
}

// The same, for an element that declares neither a context nor a handler: it
// still has to sit inside its own subtree rather than at the end of the one
// beside it, or it inherits the context of a sibling that closed there.
static void AFocusableWithNothingOfItsOwnStaysOnItsOwnPath() {
    KeymapClear();
    uint32_t act = ActionOf(StrL("t::Go4"));
    KeyBinding b[] = {{"ctrl-g", act, "Elsewhere"}};
    KeymapBind(b, 1);

    Arena* a = ArenaNew();
    App app;
    Window* win = new Window();
    win->app = &app;
    Entity<Recorder> rec = EntityNew<Recorder>(&app);
    El* root = Div(a)
                   ->Child(Div(a)->KeyContext(StrL("Elsewhere")))
                   ->Child(Div(a)->FocusId(5))
                   ->OnAction(act, ListenTo(rec, &Recorder::Stop));
    FocusCollect(win, root);
    win->focusId = 5;

    gCalls = 0;
    utassert(!WindowDispatchKeyAction(win, 'G', false, true, false));
    utassert(gCalls == 0);

    delete win;
    ArenaDelete(a);
    KeymapClear();
}

// An action carries what its binding said. Rust puts fields on the action
// type — `Confirm { secondary: true }` — and matches the whole value; there
// are no types here, so the same action id is bound twice with two payloads
// and the matcher hands back the one that fired.
static void ABindingCarriesTheActionsPayload() {
    KeymapClear();
    uint32_t confirm = ActionOf(StrL("ui::Confirm"));
    uint32_t mode = ActionOf(StrL("story::SelectScrollbarMode"));
    KeyBinding bindings[] = {
        {"enter", confirm, "Pay"},
        {"secondary-enter", confirm, "Pay", 1},
        // Not a flag: an enum, which is the other shape upstream binds.
        {"ctrl-1", mode, "Pay", 7},
    };
    KeymapBind(bindings, 3);
    uint32_t ctx = KeyContextOf(StrL("Pay"));

    KeyChord c = {};
    utassert(KeyChordParse(StrL("enter"), &c));
    KeyMatch m = KeymapMatch(c, &ctx, 1);
    utassert(m.action == confirm);
    utassert(m.arg == 0);

    utassert(KeyChordParse(StrL("secondary-enter"), &c));
    m = KeymapMatch(c, &ctx, 1);
    utassert(m.action == confirm);
    utassert(m.arg == 1);

    utassert(KeyChordParse(StrL("ctrl-1"), &c));
    m = KeymapMatch(c, &ctx, 1);
    utassert(m.action == mode);
    utassert(m.arg == 7);

    // A chord that resolves to nothing carries nothing either.
    utassert(KeyChordParse(StrL("ctrl-9"), &c));
    m = KeymapMatch(c, &ctx, 1);
    utassert(m.action == 0 && m.arg == 0);
    KeymapClear();
}

// window.dispatch_action: an action with no keystroke behind it, which is
// how a dialog's Cancel button runs what its escape key runs. It starts from
// the focus and walks out, exactly as a chord's would.
static void AnActionCanBeDispatchedWithoutAKeystroke() {
    KeymapClear();
    uint32_t act = ActionOf(StrL("t::Go3"));

    Arena* a = ArenaNew();
    App app;
    Window* win = new Window();
    win->app = &app;
    Entity<Recorder> rec = EntityNew<Recorder>(&app);
    El* leaf =
        Div(a)->FocusId(5)->OnAction(act, ListenTo(rec, &Recorder::Stop));
    El* root = Div(a)->Child(leaf);
    FocusCollect(win, root);
    win->focusId = 5;

    gCalls = 0;
    gLastArg = 0;
    // No binding for this action at all — a keystroke could not have reached
    // it, and the dispatch does not care.
    utassert(WindowDispatchAction(win, act, 42));
    utassert(gCalls == 1);
    utassert(gSeen[0] == act);
    utassert(gLastArg == 42);

    // An action nothing handles answers false, so the caller can tell.
    utassert(!WindowDispatchAction(win, ActionOf(StrL("t::Nobody"))));
    // And a zero action is not dispatched at all.
    utassert(!WindowDispatchAction(win, 0));

    delete win;
    ArenaDelete(a);
    KeymapClear();
}

// A menu row dispatches with no focused field. The root's on_action is
// still the window's, so Quit (and every other app-menu action) must
// reach it rather than falling on the floor at the exclusive end of the
// dispatch list.
static void AnUnfocusedWindowStillRunsTheRootAction() {
    KeymapClear();
    uint32_t act = ActionOf(StrL("t::Quit"));

    Arena* a = ArenaNew();
    App app;
    Window* win = new Window();
    win->app = &app;
    Entity<Recorder> rec = EntityNew<Recorder>(&app);
    El* leaf = Div(a)->FocusId(5);
    El* root =
        Div(a)->OnAction(act, ListenTo(rec, &Recorder::Stop))->Child(leaf);
    FocusCollect(win, root);
    win->focusId = 0;

    gCalls = 0;
    utassert(WindowDispatchAction(win, act, 7));
    utassert(gCalls == 1);
    utassert(gSeen[0] == act);
    utassert(gLastArg == 7);

    delete win;
    ArenaDelete(a);
    KeymapClear();
}

// The input's chords come out of the keymap now, in the key context
// El::BindInput declares. This is the window's own path: resolve the chord
// against the contexts over the focused element, then read the action as the
// edit it names.
static void AFieldsChordsResolveInItsOwnContext() {
    KeymapClear();
    InputInitKeys();

    Arena* a = ArenaNew();
    App app;
    Window* win = new Window();
    win->app = &app;
    InputState field;
    // BindInput declares the context; nothing else in the tree does.
    El* editor = Div(a)->FocusId(9)->BindInput(&field);
    El* root = Div(a)->Child(editor);
    FocusCollect(win, root);
    win->focusId = 9;

    int64_t arg = 0;
    bool pending = false;
    auto resolve = [&](int vk, bool shift, bool ctrl, bool alt, bool plat) {
        arg = 0;
        pending = false;
        uint32_t id = WindowResolveKeyAction(win, vk, shift, ctrl, alt, plat,
                                             false, &arg, &pending);
        return InputActionOf(id, arg);
    };
    // The shortcut modifier, whichever key it is on this platform.
#if GPUI_OS_MAC
    const bool secCtrl = false, secPlat = true;
#else
    const bool secCtrl = true, secPlat = false;
#endif

    utassert(resolve(KeyLeft, false, false, false, false) ==
             InputAction::MoveLeft);
    utassert(resolve(KeyA, false, secCtrl, false, secPlat) ==
             InputAction::SelectAll);
    utassert(resolve(KeyC, false, secCtrl, false, secPlat) ==
             InputAction::Copy);
    utassert(resolve(KeyZ, false, secCtrl, false, secPlat) ==
             InputAction::Undo);
    // `Enter { shift }` is one action with a payload, so the chord that
    // carries it says so rather than being a second action.
    utassert(resolve(KeyReturn, false, false, false, false) ==
             InputAction::Enter);
    utassert(!InputEnterShift(arg));
    utassert(resolve(KeyReturn, true, false, false, false) ==
             InputAction::Enter);
    utassert(InputEnterShift(arg));

    // Nothing focused is no Input context, so the field's chords resolve to
    // nothing at all — which is what keeps a page's own ctrl-c out of a
    // field's hands and a field's out of the page's.
    win->focusId = 0;
    utassert(resolve(KeyA, false, secCtrl, false, secPlat) ==
             InputAction::None);

    delete win;
    ArenaDelete(a);
    KeymapClear();
}

// The other direction: the chord a caller would press to reach an action,
// which is what a menu row and a tooltip show beside a label. Rust asks
// `window.highest_precedence_binding_for_action`; the answer has to be the
// binding that chord would actually fire, so the search runs in the matcher's
// own order.
static void TheChordAnActionIsReachedBy() {
    KeymapClear();
    uint32_t save = ActionOf(StrL("t::Save"));
    uint32_t quit = ActionOf(StrL("t::Quit"));
    uint32_t none = ActionOf(StrL("t::Unbound"));
    KeyBinding b[] = {
        {"ctrl-s", save, nullptr},
        {"ctrl-q", quit, nullptr},
        // Scoped, and later — both reasons this one wins inside Editor.
        {"ctrl-shift-s", save, "Editor"},
        // A sequence answers with its first chord, the way keystrokes().first
        // does.
        {"ctrl-k ctrl-w", quit, "Editor"},
    };
    KeymapBind(b, 4);
    uint32_t editor = KeyContextOf(StrL("Editor"));

    KeyChord c = {};
    // With no context, only the unscoped bindings are in play.
    utassert(KeymapBindingForAction(save, nullptr, 0, &c));
    utassert(c.vk == 'S' && c.ctrl && !c.shift);

    // Inside Editor the scoped one wins.
    utassert(KeymapBindingForAction(save, &editor, 1, &c));
    utassert(c.vk == 'S' && c.ctrl && c.shift);

    utassert(KeymapBindingForAction(quit, &editor, 1, &c));
    utassert(c.vk == 'K' && c.ctrl);

    utassert(!KeymapBindingForAction(none, &editor, 1, &c));
    utassert(!KeymapBindingForAction(0, &editor, 1, &c));

    // The name a key goes by, which is what spells the chord back out.
    utassert(base::StrEq(KeyName('S'), StrL("s")));
    utassert(base::StrEq(KeyName(KeyPageDown), StrL("pagedown")));
    utassert(base::StrEq(KeyName('7'), StrL("7")));

    // A function key is spelled the way a binding writes it, both halves of
    // the range: a menu row bound on one has to be able to show it.
    utassert(base::StrEq(KeyName(112), StrL("f1")));
    utassert(base::StrEq(KeyName(115), StrL("f4")));
    utassert(base::StrEq(KeyName(120), StrL("f9")));
    utassert(base::StrEq(KeyName(121), StrL("f10")));
    utassert(base::StrEq(KeyName(123), StrL("f12")));
    utassert(base::StrEq(KeyName(KeyF24), StrL("f24")));
    utassert(base::StrEq(KeyName(KeyF25), StrL("f25")));
    utassert(base::StrEq(KeyName(KeyF35), StrL("f35")));
    utassert(KeyName(KeyKpBegin + 1).len == 0);
    utassert(base::StrEq(KeyName(KeyInsert), StrL("insert")));
    utassert(base::StrEq(KeyName(KeyBrowserBack), StrL("back")));
    utassert(base::StrEq(KeyName(KeyBrowserForward), StrL("forward")));
    utassert(base::StrEq(KeyName(KeyApps), StrL("menu")));

    // Round trip: every name KeyChordParse reads, KeyName spells again.
    const char* specs[] = {
        "enter",  "escape",   "tab",    "space", "backspace", "delete",
        "left",   "up",       "right",  "down",  "home",      "end",
        "pageup", "pagedown", "insert", "back",  "forward",   "menu",
        "f1",     "f7",       "f11",    "f12",   "f24",       "f35",
        "cut",    "copy",     "paste",  "new",   "open",      "save"};
    for (const char* spec : specs) {
        KeyChord parsed = {};
        utassert(KeyChordParse(Str(spec), &parsed));
        utassert(base::StrEq(KeyName(parsed.vk), spec));
    }
    KeymapClear();
}

// KeyDownEvent's is_held and prefer_character_input, and the keystroke's IME
// state, travel from the platform's WindowKeyDown to whoever reads the key.
struct KeyFlagsRecorder {
    KeyEvent last = {};
    static El* Render(KeyFlagsRecorder*, Ctx* cx) { return Div(cx->a); }
    static void OnKey(KeyFlagsRecorder* self, Ctx*, const KeyEvent* ev) {
        self->last = *ev;
    }
};

static void TheKeyDownFlagsReachTheListener() {
    KeymapClear();
    App app;
    Window* win = new Window();
    win->app = &app;
    Entity<KeyFlagsRecorder> rec = EntityNew<KeyFlagsRecorder>(&app);
    WindowOnKey(win, ListenTo(rec, &KeyFlagsRecorder::OnKey));

    KeyDownFlags flags;
    flags.held = true;
    flags.imeInProgress = true;
    WindowKeyDown(win, KeyA, false, false, false, false, false, flags);
    const KeyEvent& ev = rec.Get(&app)->last;
    utassert(ev.vk == KeyA && ev.down);
    utassert(ev.held && ev.imeInProgress && !ev.preferCharacterInput);

    flags = {};
    flags.preferCharacterInput = true;
    WindowKeyDown(win, KeyA, false, true, true, false, false, flags);
    utassert(!ev.held && !ev.imeInProgress && ev.preferCharacterInput);

    // A platform that knows none of it sends a fresh press.
    WindowKeyDown(win, KeyA, false, false, false);
    utassert(!ev.held && !ev.imeInProgress && !ev.preferCharacterInput);

    EntityDropAll(&app);
    delete win;
}

// keystroke.rs is_printable_key: the keys that type, as the IME test reads
// them — not the function, editing and navigation keys, and not a modifier.
static void OnlyTypingKeysArePrintable() {
    utassert(KeyIsPrintable(KeyA));
    utassert(KeyIsPrintable('7'));
    utassert(KeyIsPrintable(KeySpace));
    utassert(KeyIsPrintable(KeyReturn));
    utassert(KeyIsPrintable(KeyTab));
    utassert(KeyIsPrintable(KeySemicolon));
    utassert(!KeyIsPrintable(0));
    utassert(!KeyIsPrintable(KeyF1) && !KeyIsPrintable(KeyF24));
    utassert(!KeyIsPrintable(KeyF25) && !KeyIsPrintable(KeyF35));
    utassert(!KeyIsPrintable(KeyBack) && !KeyIsPrintable(KeyDelete));
    utassert(!KeyIsPrintable(KeyLeft) && !KeyIsPrintable(KeyDown));
    utassert(!KeyIsPrintable(KeyPageUp) && !KeyIsPrintable(KeyEnd));
    utassert(!KeyIsPrintable(KeyInsert) && !KeyIsPrintable(KeyEscape));
    utassert(!KeyIsPrintable(KeyBrowserBack));
    utassert(!KeyIsPrintable(KeyShift) && !KeyIsPrintable(KeyControl));
    utassert(!KeyIsPrintable(KeyAlt));
}

void TestKeymap() {
    TestSuite("keymap");
    TheKeyDownFlagsReachTheListener();
    OnlyTypingKeysArePrintable();
    ABindingCarriesTheActionsPayload();
    AnActionCanBeDispatchedWithoutAKeystroke();
    AnUnfocusedWindowStillRunsTheRootAction();
    AFieldsChordsResolveInItsOwnContext();
    TheChordAnActionIsReachedBy();
    AChordIsReadTheWayRustSpellsIt();
    TheInnermostContextWins();
    TheLastBindingForAChordWins();
    APredicateReadsTheInnermostContext();
    AChildPredicateNeedsTheEnclosingContext();
    ASequenceIsHeldUntilItFinishes();
    AWholeBindingBeatsOneThatIsOnlyBegun();
    BlurDropsWhatTheKeyboardWasPartWayThrough();
    AnActionIsItsName();
    AnActionWalksOutFromWhatHasFocus();
    AnAltChordIsAHandledKeystroke();
    PropagateCarriesOnOutwards();
    AContextOffThePathDoesNotMatch();
    AFocusableWithNothingOfItsOwnStaysOnItsOwnPath();
}
