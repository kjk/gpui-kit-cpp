#include "gpui.h"

using namespace gpui;

// examples/window_title — TitleBar::new() over the body, on a window opened
// with TitleBar::window_options().
struct Example {
    static void OnGo(Example*, Ctx*, const ClickEvent*) {
        log(StrL("Clicked!"));
    }

    static El* Render(Example*, Ctx* cx) {
        Arena* a = cx->a;
        const Theme& th = ThemeNow(cx->app);
        // One child, the full-width justify-between row Rust puts in the bar.
        El* bar =
            component::TitleBar::New(cx)
                ->Child(Div(a)
                            ->FlexRow()
                            ->W(kFill)
                            ->ItemsCenter()
                            ->PadR(8)
                            ->JustifyBetween()
                            ->Child(TextEl(a, StrL("App with Custom title bar"))
                                        ->Font(14)
                                        ->Fg(th.foreground))
                            ->Child(TextEl(a, StrL("Right Item"))
                                        ->Font(14)
                                        ->Fg(th.mutedFg)))
                ->IntoEl();

        El* body =
            Div(a)
                ->FlexCol()
                ->Flex1()
                ->Pad(20)
                ->ItemsCenter()
                ->JustifyCenter()
                ->Gap(8)
                ->Child(TextEl(a, StrL("Hello, World!"))
                            ->Font(16)
                            ->Fg(th.foreground))
                ->Child(ButtonEl(a, 0, StrL("Let's Go!"), BtnKind::Primary)
                            ->OnClick(Listen(cx, &Example::OnGo)));

        return Div(a)
            ->FlexCol()
            ->SizeFull()
            ->Bg(th.tokens.background)
            ->Child(bar)
            ->Child(body);
    }
};

int GpuiMain(int argc, char** argv) {
    (void)argc;
    (void)argv;
    App* app = AppNew();
    component::Init(app);
    ThemeSet(app, ThemeMode::Light);
    WinOpts opts = {};
    // TitleBar::window_options(): the example draws its own title bar.
    opts.clientTitleBar = true;
    return KitRunView(StrL("Window Title C++"), 800, 600,
                      EntityNew<Example>(app).id, app, opts);
}
