#include "gpui.h"

using namespace gpui;

// examples/text_selection — drag across markdown bubbles and copy them.
// The bubbles are selectable TextViews, so a drag that starts in the
// window can cross them. The button and the field stay out of that.
struct SelApp {
    InputState in;
    static El* Render(SelApp* self, Ctx* cx);
};

static const char* kMsgs[] = {
    "**Hello!** How can I help you today?",
    "I want to select text *across* multiple bubbles.",
    ("Sure — drag from anywhere, even from the blank space between "
     "bubbles, then press `cmd-c` to copy everything."),
    "Nice, it also keeps the top-to-bottom order.",
};
static const int kNMsgs = 4;
static const bool kMine[] = {false, true, false, true};

static El* Bubble(Ctx* cx, int ix, const Theme& th) {
    Arena* a = cx->a;
    bool mine = kMine[ix];
    El* inner = Div(a)
                    ->MaxW(420)
                    ->Pad(12)
                    ->Radius(th.radiusLg)
                    ->Bg(mine ? RgbaOpacity(th.primary, 0.1f) : th.muted)
                    ->Child(component::TextView::New(cx, Str(kMsgs[ix]))
                                ->Selectable()
                                ->IntoEl());
    El* row = Div(a)->FlexRow()->W(kFill);
    if (mine) row->JustifyEnd();
    row->Child(inner);
    return row;
}

El* SelApp::Render(SelApp* self, Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    El* col = Div(a)->FlexCol()->SizeFull()->Pad(16)->Gap(12)->Bg(
        th.tokens.background);
    for (int i = 0; i < kNMsgs; i++) col->Child(Bubble(cx, i, th));
    col->Child(Div(a)->Flex1());
    col->Child(ButtonEl(a, 1, StrL("Clicking me must not start selection")));
    col->Child(component::Input::New(cx, StrL("chat-input"), &self->in)
                   ->IntoEl());
    return col;
}

int GpuiMain(int argc, char** argv) {
    (void)argc;
    (void)argv;
    App* app = AppNew();
    component::Init(app);
    ThemeSet(app, ThemeMode::Light);
    Entity<SelApp> view = EntityNew<SelApp>(app);
    InputSetPlaceholder(&view.Get(app)->in,
                        StrL("Type here (selection must NOT start from here)"));
    return KitRunView(StrL("Text Selection C++"), 800, 600, view.id, app,
                      WinOpts{});
}
