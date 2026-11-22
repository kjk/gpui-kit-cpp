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
            ->Child(component::Label::New(cx, StrL("Hello, World!"))
                        ->Font(16)
                        ->IntoEl()
                        ->Fg(th.foreground))
            ->Child(component::Button::New(cx, StrL("ok"))
                        ->Primary()
                        ->Icon(IconName::Check)
                        ->Label(StrL("Let's Go!"))
                        ->OnClick(Listen(cx, &Example::OnGo))
                        ->IntoEl());
    }
};

int GpuiMain(int argc, char** argv) {
    (void)argc;
    (void)argv;
    // Rust adds `.with_assets(gpui_kit::assets::Assets)` here so its icons
    // load. The lucide set is compiled into the runtime (asset_icons.cpp),
    // so the check icon needs no asset root.
    App* app = AppNew();
    component::Init(app);
    ThemeSet(app, ThemeMode::Light);
    return KitRunView(StrL("Hello World C++"), 800, 600,
                      EntityNew<Example>(app).id, app, WinOpts{});
}
