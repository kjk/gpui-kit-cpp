#include "gpui.h"

using namespace gpui;

struct LinkTooltipExample {
    Entity<TextViewState> text;
    bool showPreview = true;

    static El* Preview(Ctx* cx, Str url, void*) {
        return component::Tooltip::New(cx, url)->MaxW(Rems(cx, 32.f))->IntoEl();
    }

    static El* Render(LinkTooltipExample* self, Ctx* cx) {
        const Theme& theme = ThemeNow(cx->app);
        TextView* text = TextView::New(cx, self->text)->Selectable(true);
        if (self->showPreview) {
            text->LinkTooltip(&Preview);
        }
        return Div(cx->a)
            ->SizeFull()
            ->Pad(Rems(cx, 1.5f))
            ->Bg(theme.tokens.background)
            ->Fg(theme.foreground)
            ->Child(text->IntoEl()->W(kFill));
    }
};

int GpuiMain(int argc, char** argv) {
    App* app = AppNew();
    component::Init(app);
    auto view = EntityNew<LinkTooltipExample>(app);
    auto* self = view.Get(app);
    for (int i = 1; i < argc; i++) {
        if (StrEq(Str(argv[i]), StrL("--no-preview"))) {
            self->showPreview = false;
        }
    }
    self->text = TextViewState::Markdown(
        app,
        StrL("# Reports\n\n"
             "Move the pointer along a link to inspect its destination.\n\n"
             "[Cleanup "
             "report](file:///home/demo/reports/cleanup-report.json)\n\n"
             "[Review the complete report with a link label that wraps across "
             "multiple lines and remains selectable]"
             "(https://example.com/reports/2026/11/cleanup-report.json)\n\n"
             "[GPUI Kit "
             "documentation](https://gpui-kit.com/docs/getting-started)"));
    return KitRunView(StrL("Link previews"), 720, 360, view.id, app, {});
}
