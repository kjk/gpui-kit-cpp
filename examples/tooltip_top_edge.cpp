#include "gpui.h"

using namespace gpui;

// examples/tooltip_top_edge — a trigger pinned to the top edge, so the
// tooltip has to flip below it. The window draws its own title bar, the same
// one the rest of the examples use, so the top edge the trigger sits against
// is ours and not the window manager's.
struct Example {
    static El* Render(Example*, Ctx* cx) {
        Arena* a = cx->a;
        const Theme& th = ThemeNow(cx->app);
        El* btn = ButtonEl(a, 1, StrL("Hover for tooltip"), BtnKind::Primary);
        btn->Tip(StrL(
            "This tooltip should appear below the trigger near the top edge."));

        El* body =
            Div(a)
                ->Flex1()
                ->W(kFill)
                ->Bg(th.tokens.background)
                ->Child(Div(a)->Absolute()->Top(0)->Left(24)->Child(btn))
                ->Child(Div(a)->Absolute()->Top(64)->Left(24)->MaxW(420)->Child(
                    TextEl(a,
                           StrL("Hover the top button. The tooltip should flip "
                                "below the trigger without changing the "
                                "original visual gap."))
                        ->Font(14)
                        ->Fg(th.mutedFg)
                        ->Wrap()
                        ->MaxW(420)));

        return Div(a)
            ->FlexCol()
            ->SizeFull()
            ->Bg(th.tokens.background)
            ->Child(component::TitleBar::New(cx)
                        ->Child(TextEl(a, StrL("Tooltip Top Edge"))
                                    ->Font(14)
                                    ->Fg(th.foreground))
                        ->IntoEl())
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
    opts.clientTitleBar = true;
    return KitRunView(StrL("Tooltip Top Edge C++"), 520, 260,
                      EntityNew<Example>(app).id, app, opts);
}
