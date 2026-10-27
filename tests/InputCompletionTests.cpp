/* Ported from crates/story/src/stories/input_completion.rs.
   The story binary compiles that file on its own; this one includes it
   so the same menu can be driven here. */

#include "Test.h"

#include "../examples/story/input_completion.cpp"

static void trigger_opens_at_line_start_and_after_whitespace() {
    Str a = StrL("$HO");
    VariableQuery q = ActiveVariableQuery(a, 3);
    utassert(q.ok && q.tokenStart == 0 &&
             StrEq(VariableQueryText(a, q), StrL("HO")));

    Str b = StrL("echo $HO");
    q = ActiveVariableQuery(b, 8);
    utassert(q.ok && q.tokenStart == 5 &&
             StrEq(VariableQueryText(b, q), StrL("HO")));

    Str c = StrL("echo $");
    q = ActiveVariableQuery(c, 6);
    utassert(q.ok && q.tokenStart == 5 && q.queryBegin == q.queryEnd);

    utassert(!ActiveVariableQuery(StrL("a$b"), 3).ok);
    utassert(!ActiveVariableQuery(StrL("mail$x"), 6).ok);
    utassert(!ActiveVariableQuery(StrL("echo $HO "), 9).ok);
}

static void ranking_puts_prefix_matches_first() {
    int ix[8];
    int n = RankVariables(StrL("P"), ix, 8);
    utassert(n == 2);
    utassert(StrEq(Str(VariableName(ix[0])), StrL("PATH")));
    utassert(StrEq(Str(VariableName(ix[1])), StrL("PWD")));

    n = RankVariables(StrL("e"), ix, 8);
    utassert(n == 5);
    utassert(StrEq(Str(VariableName(ix[0])), StrL("EDITOR")));
    utassert(StrEq(Str(VariableName(ix[1])), StrL("HOME")));
    utassert(StrEq(Str(VariableName(ix[2])), StrL("SHELL")));
    utassert(StrEq(Str(VariableName(ix[3])), StrL("TERM")));
    utassert(StrEq(Str(VariableName(ix[4])), StrL("USER")));

    utassert(RankVariables(StrL("ZZZ"), ix, 8) == 0);
}

static CompletionExample* Mount(App* app, Window** winOut) {
    component::Init(app);
    ThemeSet(app, ThemeMode::Light);
    Entity<CompletionExample> page = EntityNew<CompletionExample>(app);
    Window* win = TestWindowOpen(app, page, 800, 400);
    CompletionExample* self = page.Get(app);
    InputFocus(&self->field, app, win);
    TestDraw(win);
    *winOut = win;
    return self;
}

static void keyboard_selects_and_accepts_a_candidate() {
    App* app = TestAppNew();
    Window* win = nullptr;
    CompletionExample* self = Mount(app, &win);
    TestSimulateInput(win, StrL("echo $P"));
    utassert(self->open && self->n == 2);
    utassert(StrEq(Str(VariableName(self->items[0])), StrL("PATH")));
    utassert(StrEq(Str(VariableName(self->items[1])), StrL("PWD")));

    TestSimulateKeystrokes(win, "down tab");
    utassert(StrEq(Str(InputCStr(&self->field)), StrL("echo $PWD ")));
    utassert(!self->open);

    TestSimulateInput(win, StrL("$HO"));
    TestSimulateKeystrokes(win, "enter");
    utassert(StrEq(Str(InputCStr(&self->field)), StrL("echo $PWD $HOME ")));
    TestAppFree(app);
}

static void leaving_the_token_without_editing_releases_the_keyboard() {
    App* app = TestAppNew();
    Window* win = nullptr;
    CompletionExample* self = Mount(app, &win);
    TestSimulateInput(win, StrL("echo $HO"));
    utassert(self->open && self->n == 1);
    utassert(StrEq(Str(VariableName(self->items[0])), StrL("HOME")));

    // No draw between the keys: Enter has to notice the caret left the
    // token. A flush would rescan in render and hide that check.
    WindowKeyDown(win, KeyHome, false, false, false, false, false);
    WindowKeyDown(win, KeyReturn, false, false, false, false, false);
    utassert(StrEq(Str(InputCStr(&self->field)), StrL("echo $HO")));
    utassert(!self->open);
    utassert(StrEq(Str(self->status), StrL("Submitted \"echo $HO\"")));
    TestAppFree(app);
}

void TestInputCompletion() {
    TestSuite("input_completion");
    trigger_opens_at_line_start_and_after_whitespace();
    ranking_puts_prefix_matches_first();
    keyboard_selects_and_accepts_a_candidate();
    leaving_the_token_without_editing_releases_the_keyboard();
}
