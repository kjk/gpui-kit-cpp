#include "gpui.h"

using namespace gpui;

// examples/focus_trap — two Tab traps plus buttons outside them.
struct Example {
    static void OnBtn(Example*, Ctx*, const ClickEvent* ev) {
        logf("button %d clicked", ev->id);
    }

    static El* Render(Example*, Ctx* cx);
};

El* Example::Render(Example*, Ctx* cx) {
    Arena* frame = cx->a;
    const Theme& th = ThemeNow(cx->app);
    Listener onBtn = Listen(cx, &Example::OnBtn);

    // The click id doubles as the focus id, so buttons keep theirs.
    auto btn = [&](int id, const char* label) {
        return ButtonEl(frame, id, Str(label), BtnKind::Default)
            ->OnClick(onBtn);
    };
    auto trapBtn = [&](int id, const char* label, int trap) {
        return btn(id, label)->TrapId(trap);
    };

    El* trap1 = Div(frame)
                    ->FlexRow()
                    ->Gap(8)
                    ->Pad(16)
                    ->Radius(th.radius)
                    ->Bg(th.tokens.secondary)
                    ->Border(1, th.border)
                    ->Child(trapBtn(11, "Trap 1 - Button 1", 1))
                    ->Child(trapBtn(12, "Trap 1 - Button 2", 1))
                    ->Child(trapBtn(13, "Trap 1 - Button 3", 1));

    El* trap2 = Div(frame)
                    ->FlexRow()
                    ->Gap(8)
                    ->Pad(16)
                    ->Radius(th.radius)
                    ->Bg(RgbaOpacity(th.accent, 0.4f))
                    ->Border(1, th.blue)
                    ->Child(trapBtn(21, "Trap 2 - Button 1", 2))
                    ->Child(trapBtn(22, "Trap 2 - Button 2", 2))
                    ->Child(trapBtn(23, "Trap 2 - Button 3", 2))
                    ->Child(trapBtn(24, "Trap 2 - Button 4", 2));

    return Div(frame)
        ->FlexCol()
        ->SizeFull()
        ->Gap(24)
        ->Pad(32)
        ->Bg(th.tokens.background)
        ->Child(TextEl(frame, StrL("Focus Trap Example"))
                    ->Font(20)
                    ->Bold()
                    ->Fg(th.foreground))
        ->Child(
            TextEl(frame, StrL("Press Tab to navigate between buttons. Notice "
                               "how focus cycles within different areas."))
                ->Font(14)
                ->Fg(th.mutedFg)
                ->Wrap())
        ->Child(TextEl(frame, StrL("Outside Area (No Focus Trap)"))
                    ->Font(16)
                    ->Semibold()
                    ->Fg(th.foreground))
        ->Child(Div(frame)
                    ->FlexRow()
                    ->Gap(8)
                    ->Child(btn(1, "Outside Button 1"))
                    ->Child(btn(2, "Outside Button 2"))
                    ->Child(btn(3, "Outside Button 3")))
        ->Child(TextEl(frame, StrL("Focus Trap Area 1"))
                    ->Font(16)
                    ->Semibold()
                    ->Fg(th.foreground))
        ->Child(trap1)
        ->Child(TextEl(frame, StrL("-> Press Tab in this area, focus cycles "
                                   "through 3 buttons without escaping"))
                    ->Font(12)
                    ->Fg(th.mutedFg))
        ->Child(TextEl(frame, StrL("Outside Area (No Focus Trap)"))
                    ->Font(16)
                    ->Semibold()
                    ->Fg(th.foreground))
        ->Child(Div(frame)
                    ->FlexRow()
                    ->Gap(8)
                    ->Child(btn(4, "Outside Button 4"))
                    ->Child(btn(5, "Outside Button 5")))
        ->Child(TextEl(frame, StrL("Focus Trap Area 2"))
                    ->Font(16)
                    ->Semibold()
                    ->Fg(th.foreground))
        ->Child(trap2)
        ->Child(TextEl(frame, StrL("-> Press Tab in this area, focus cycles "
                                   "through 4 buttons without escaping"))
                    ->Font(12)
                    ->Fg(th.mutedFg));
}

int GpuiMain(int argc, char** argv) {
    (void)argc;
    (void)argv;
    App* app = AppNew();
    component::Init(app);
    ThemeSet(app, ThemeMode::Light);
    return KitRunView(StrL("Focus Trap C++"), 800, 600,
                      EntityNew<Example>(app).id, app, WinOpts{});
}
