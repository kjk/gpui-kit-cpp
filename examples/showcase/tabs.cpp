#include "Showcase.h"
#include "gpui.h"

using namespace gpui;

static void PickTab(ShowcaseApp* app, Ctx* cx, const ClickEvent*, intptr_t i) {
    app->tab = (int)i;
    Notify(cx);
}

El* ShowcaseTabs(ShowcaseApp* app, Ctx* cx) {
    Arena* a = cx->a;
    const char* labels[] = {"Overview", "Activity", "Settings"};
    // Rust's border_b_1 runs the width of the card and sits under the tabs:
    // its border box holds them. A border here is drawn inside the box, so
    // the bar takes the card's width and keeps a row of its own for the line,
    // or the tabs' own border_b_2 would paint over it.
    El* bar = Tabs::New(cx, StrL("example-tabs"))
                  ->FlexRow()
                  ->W(kFill)
                  ->PadX(8)
                  ->PadT(4)
                  ->PadB(1)
                  ->BorderB(1, ExampleRgb(0xd4d4d4));
    for (int i = 0; i < 3; i++) {
        bool on = app->tab == i;
        El* tab =
            Tab::New(cx, DupFmt(cx, "tab-%d", i), false,
                     Listen(cx, &PickTab, i), on, Str(labels[i]), i + 1, 3)
                ->H(28)
                ->PadX(8)
                ->ItemsCenter()
                ->BorderB(2, on ? ExampleRgb(0x171717) : ExampleRgb(0xffffff))
                ->HoverBg(ExampleRgb(0xf5f5f5));
        El* lab = TextEl(a, Str(labels[i]))->Font(12)->Fg(ExampleRgb(0x171717));
        if (on) {
            lab->Semibold();
        }
        tab->Child(lab);
        bar->Child(tab);
    }

    const char* title = "Workspace overview";
    const char* sub = "12 components · 4 contributors · updated today";
    if (app->tab == 1) {
        title = "Recent activity";
        sub = "Button example was updated 8 minutes ago.";
    } else if (app->tab == 2) {
        title = "Project settings";
        sub = "Manage notifications and member access.";
    }

    return Div(a)
        ->FlexCol()
        ->W(288)
        ->Border(1, ScBorder())
        ->Child(bar)
        ->Child(Div(a)
                    ->MinH(80)
                    ->Pad(12)
                    ->FlexCol()
                    ->Gap(4)
                    ->Child(TextEl(a, Str(title))
                                ->Font(12)
                                ->Fg(ExampleRgb(0x171717)))
                    ->Child(TextEl(a, Str(sub))
                                ->Font(12)
                                ->Fg(ExampleRgb(0x737373))
                                ->Wrap()
                                ->MaxW(260)));
}

SHOWCASE_PAGE(CompTabs, ShowcaseTabs);
