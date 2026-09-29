#include "gpui.h"

using namespace gpui;

// Port of examples/app_assets: load IconName SVGs from an assets folder
// (rust-embed + AssetSource) and show Inbox + Bot centered in a light window.
struct Example {
    static El* Render(Example*, Ctx* cx) {
        Arena* a = cx->a;
        const Theme& th = ThemeNow(cx->app);
        // Rust Icon default is size_4 / text size = 16px, two icons, gap_2.
        return Div(a)
            ->FlexCol()
            ->SizeFull()
            ->Gap(8)
            ->ItemsCenter()
            ->JustifyCenter()
            ->Bg(th.tokens.background)
            ->Child(IconEl(a, IconName::Inbox, 16))
            ->Child(IconEl(a, IconName::Bot, 16));
    }
};

int GpuiMain(int argc, char** argv) {
    (void)argc;
    (void)argv;
    App* app = AppNew();
    component::Init(app);
    ThemeSet(app, ThemeMode::Light);
    AssetsClear();
    AssetsAddDefaultRoots(StrL("app_assets"));
    return KitRunView(StrL("App Assets C++"), 800, 600,
                      EntityNew<Example>(app).id, app, WinOpts{});
}
