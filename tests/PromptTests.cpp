/* window.prompt: crates/gpui/src/window/prompts.rs, platform.rs PromptButton,
   and the test platform's prompt queue (platform/test/platform.rs). The
   answer reaches the listener once, a replaced fallback prompt answers -1,
   and the fallback renderer draws buttons that answer by index. */

#include "Test.h"

struct PromptView {
    int answers = 0;
    int lastIx = -2;

    static El* Render(PromptView*, Ctx* cx) { return Div(cx->a)->SizeFull(); }
    static void OnAnswer(PromptView* self, Ctx*, const PromptResponse* r) {
        self->answers++;
        self->lastIx = r->ix;
    }
};

static void ButtonFromStr() {
    PromptButton ok = PromptButton::From(StrL("OK"));
    utassert(ok.kind == PromptButton::Kind::Ok);
    utassert(base::StrEq(ok.label, StrL("OK")));
    PromptButton cancel = PromptButton::From(StrL("cancel"));
    utassert(cancel.IsCancel());
    utassert(base::StrEq(cancel.label, StrL("Cancel")));
    PromptButton other = PromptButton::From(StrL("Save"));
    utassert(other.kind == PromptButton::Kind::Other);
    utassert(base::StrEq(other.label, StrL("Save")));
}

static void TestPlatformQueuesPrompts() {
    App* app = TestAppNew();
    auto view = EntityNew<PromptView>(app);
    Window* win = TestWindowOpen(app, view, 400, 300, 1);
    utassert(!TestHasPendingPrompt());

    PromptButton buttons[] = {PromptButton::New(StrL("Yes")),
                              PromptButton::New(StrL("No")),
                              PromptButton::Cancel(StrL("Cancel"))};
    WindowPrompt(win, PromptLevel::Warning, StrL("Quit?"), StrL("Unsaved"),
                 buttons, 3, ListenTo(view, &PromptView::OnAnswer));
    utassert(TestHasPendingPrompt());
    utassert(!WindowHasActivePrompt(win));
    Str message, detail;
    utassert(TestPendingPrompt(&message, &detail));
    utassert(base::StrEq(message, StrL("Quit?")));
    utassert(base::StrEq(detail, StrL("Unsaved")));

    utassert(!TestSimulatePromptAnswer(StrL("Maybe")));
    utassert(view.Get(app)->answers == 0);
    utassert(TestSimulatePromptAnswer(StrL("No")));
    utassert(view.Get(app)->answers == 1);
    utassert(view.Get(app)->lastIx == 1);
    utassert(!TestHasPendingPrompt());
    TestAppFree(app);
}

static const HitRect* PromptButtonHit(Window* win, int ix) {
    for (int i = 0; i < len(win->paint.hits); i++) {
        const HitRect& hr = win->paint.hits[i];
        if (hr.listener.view == win->prompt && hr.listener.arg == ix &&
            hr.bounds.w > 0) {
            return &hr;
        }
    }
    return nullptr;
}

static void FallbackRendererAnswers() {
    App* app = TestAppNew();
    AppSetFallbackPrompts(app, true);
    auto view = EntityNew<PromptView>(app);
    Window* win = TestWindowOpen(app, view, 400, 300, 1);
    PromptButton first[] = {PromptButton::From(StrL("ok"))};
    WindowPrompt(win, PromptLevel::Info, StrL("first"), {}, first, 1,
                 ListenTo(view, &PromptView::OnAnswer));
    utassert(WindowHasActivePrompt(win));
    utassert(!TestHasPendingPrompt());

    // A second prompt replaces the first, whose receiver is canceled.
    PromptButton second[] = {PromptButton::New(StrL("Save")),
                             PromptButton::New(StrL("Discard"))};
    WindowPrompt(win, PromptLevel::Critical, StrL("second"), StrL("detail"),
                 second, 2, ListenTo(view, &PromptView::OnAnswer));
    utassert(view.Get(app)->answers == 1);
    utassert(view.Get(app)->lastIx == -1);
    TestDraw(win);

    const HitRect* discard = PromptButtonHit(win, 1);
    utassert(discard != nullptr);
    if (discard) {
        Bounds b = discard->bounds;
        TestSimulateClick(win, {b.x + b.w / 2, b.y + b.h / 2});
    }
    utassert(view.Get(app)->answers == 2);
    utassert(view.Get(app)->lastIx == 1);
    utassert(!WindowHasActivePrompt(win));
    TestDraw(win);
    utassert(PromptButtonHit(win, 0) == nullptr);
    TestAppFree(app);
}

// Rust's receiver is canceled when the window drops its prompt: a fallback
// prompt and a queued one each answer -1 once when their window closes.
static void WindowCloseAnswersPending() {
    App* app = TestAppNew();
    auto view = EntityNew<PromptView>(app);
    Window* queued = TestWindowOpen(app, view, 400, 300, 1);
    PromptButton ok[] = {PromptButton::From(StrL("ok"))};
    WindowPrompt(queued, PromptLevel::Info, StrL("queued"), {}, ok, 1,
                 ListenTo(view, &PromptView::OnAnswer));
    utassert(TestHasPendingPrompt());
    WindowClosed(queued);
    utassert(view.Get(app)->answers == 1);
    utassert(view.Get(app)->lastIx == -1);
    // the test platform still holds it; answering it now reaches no one
    utassert(TestSimulatePromptAnswer(StrL("OK")));
    utassert(view.Get(app)->answers == 1);

    AppSetFallbackPrompts(app, true);
    Window* drawn = TestWindowOpen(app, view, 400, 300, 1);
    WindowPrompt(drawn, PromptLevel::Info, StrL("drawn"), {}, ok, 1,
                 ListenTo(view, &PromptView::OnAnswer));
    utassert(WindowHasActivePrompt(drawn));
    WindowClosed(drawn);
    utassert(!WindowHasActivePrompt(drawn));
    utassert(view.Get(app)->answers == 2);
    utassert(view.Get(app)->lastIx == -1);
    TestAppFree(app);
}

void TestPrompts() {
    TestSuite("prompts");
    ButtonFromStr();
    TestPlatformQueuesPrompts();
    FallbackRendererAnswers();
    WindowCloseAnswersPending();
}
