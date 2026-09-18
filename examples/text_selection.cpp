#include "gpui.h"

using namespace gpui;

struct SelApp {
    static El* Render(SelApp* self, Ctx* cx);
    InputState in;
    char copied[2048];
    bool selecting;
    int selFrom;
    int selTo;
};

static const char* kMsgs[] = {
    "Hello! How can I help you today?",
    "I want to select text across multiple bubbles.",
    ("Sure — drag from anywhere, even from the blank space between bubbles, "
     "then press Ctrl+C to copy everything."),
    "Nice, it also keeps the top-to-bottom order.",
};
static const int kNMsgs = 4;
static const bool kMine[] = {false, true, false, true};

static void OnKey(SelApp* app, Ctx* cx, const KeyEvent* ev) {
    (void)cx;
    (void)app;
    int vk = ev->vk;
    bool down = ev->down;
    if (!down) {
        return;
    }
    if (vk == KeyC && ev->ctrl) {
        StrBuilder b;
        int a = app->selFrom, c = app->selTo;
        if (a > c) {
            int t = a;
            a = c;
            c = t;
        }
        if (a >= 0) {
            for (int i = a; i <= c && i < kNMsgs; i++) {
                if (i > a) {
                    b.Append(StrL("\n"));
                }
                b.Append(Str(kMsgs[i]));
            }
        }
        Str s = b.TakeStr();
        if (s.s) {
            ClipboardSetText(cx->win, s);
            StrCopyZ(app->copied, (int)sizeof(app->copied), s.s);
        }
        StrFree(s);
    }
}

static void PickBubble(SelApp* app, Ctx* cx, const ClickEvent*, intptr_t ix) {
    app->in.focused = false;
    if (app->selFrom < 0) {
        app->selFrom = (int)ix;
    }
    app->selTo = (int)ix;
    Notify(cx);
}

// A button must not start a selection.
static void ClearSelection(SelApp* app, Ctx* cx, const ClickEvent*) {
    app->selFrom = -1;
    app->selTo = -1;
    app->in.focused = false;
    Notify(cx);
}

static void FocusField(SelApp* app, Ctx* cx, const ClickEvent*) {
    app->in.focused = true;
    app->selFrom = -1;
    Notify(cx);
}

static El* Bubble(Ctx* cx, int ix, const Theme& th) {
    Arena* a = cx->a;
    bool mine = kMine[ix];
    El* inner =
        Div(a)
            ->MaxW(420)
            ->Pad(12)
            ->Radius(8)
            ->Bg(mine ? RgbaOpacity(th.primary, 0.1f) : th.muted)
            ->OnClick(Listen(cx, &PickBubble, ix))
            ->Child(
                TextEl(a, Str(kMsgs[ix]))->Font(14)->Fg(th.foreground)->Wrap());
    El* row = Div(a)->FlexRow()->W(kFill);
    if (mine) {
        row->JustifyBetween();
        row->Child(Div(a)->Flex1());
    }
    row->Child(inner);
    return row;
}

El* SelApp::Render(SelApp* app, Ctx* cx) {
    Arena* frame = cx->a;

    const Theme& th = ThemeNow(cx->app);
    El* col = Div(frame)->FlexCol()->SizeFull()->Pad(16)->Gap(12)->Bg(
        th.tokens.background);
    for (int i = 0; i < kNMsgs; i++) {
        col->Child(Bubble(cx, i, th));
    }
    col->Child(Div(frame)->Flex1());
    col->Child(ButtonEl(frame, 1, StrL("Clicking me must not start selection"))
                   ->OnClick(Listen(cx, &ClearSelection)));
    Str value = InputValue(&app->in);
    Str shown = len(value) > 0 ? value : app->in.placeholder;
    col->Child(Div(frame)
                   ->W(kFill)
                   ->H(36)
                   ->PadX(12)
                   ->ItemsCenter()
                   ->Radius(6)
                   ->Border(1, th.border)
                   ->OnClick(Listen(cx, &FocusField))
                   ->Child(TextEl(frame, shown)
                               ->Font(14)
                               ->Fg(len(value) ? th.foreground : th.mutedFg)));
    return col;
}

int GpuiMain(int argc, char** argv) {
    (void)argc;
    (void)argv;
    App* app = AppNew();
    component::Init(app);
    Entity<SelApp> view = EntityNew<SelApp>(app);
    SelApp* self = view.Get(app);
    (void)self;
    ThemeSet(app, ThemeMode::Light);
    InputSetPlaceholder(&self->in,
                        StrL("Type here (selection must NOT start from here)"));
    self->copied[0] = 0;
    self->selFrom = -1;
    self->selTo = -1;
    WinOpts opts = {};
    Window* win = WindowOpenView(app, StrL("Text Selection C++"), 800, 600,
                                 view.id, opts);
    WindowOnKey(win, ListenTo(view, &OnKey));
    win->input = &self->in;
    int rc = AppRun(app);
    AppFree(app);
    return rc;
}
