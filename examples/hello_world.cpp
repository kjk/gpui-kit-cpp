#include "gpui.h"

using namespace gpui;

// examples/hello_world/src/main.rs — a stateless view with a click listener.
struct Example {
    static void OnGo(Example*, Ctx*, const ClickEvent*) {
        log(StrL("Clicked!"));
    }

    static El* Render(Example*, Ctx* cx) {
        const Theme& th = ThemeNow(cx->app);
        return Div(cx->a)
            ->FlexCol()
            ->SizeFull()
            ->Gap(8)
            ->ItemsCenter()
            ->JustifyCenter()
            ->Bg(th.tokens.background)
            ->Child(TextEl(cx->a, StrL("Hello, World!"))
                        ->Font(16)
                        ->Fg(th.foreground))
            ->Child(ButtonEl(cx->a, 0, StrL("Let's Go!"), BtnKind::Primary)
                        ->OnClick(Listen(cx, &Example::OnGo)));
    }
};

int GpuiMain(int argc, char** argv) {
    (void)argc;
    (void)argv;
    App* app = AppNew();
    component::Init(app);
    ThemeSet(app, ThemeMode::Light);
    return KitRunView(StrL("Hello World C++"), 800, 600,
                      EntityNew<Example>(app).id, app, WinOpts{});
}
