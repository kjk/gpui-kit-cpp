// The same small app as hello_world.cpp, deliberately including the source
// tree's public non-amalgam headers. This target exists to prove that every
// source file can be compiled into its own object and linked as a normal app.
#include "gpui/gpui.h"
#include "base/lib.h"
#include "ui/lib.h"
#include "gpui/paint.h"
#include "gpui/assets.h"
#include "gpui/svg.h"

using namespace gpui;

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
    return KitRunView(StrL("Hello World C++ (non-amalgam)"), 800, 600,
                      EntityNew<Example>(app).id, app, WinOpts{});
}
