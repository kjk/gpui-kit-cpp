#include "Showcase.h"
#include "gpui.h"

using namespace gpui;

static void OnInput(ShowcaseApp* app, Ctx* cx, const ClickEvent*) {
    (void)app;
    app->input.focused = true;
    Notify(cx);
}

El* ShowcaseInput(ShowcaseApp* app, Ctx* cx) {
    Arena* a = cx->a;
    return Div(a)
        ->FlexCol()
        ->W(224)
        ->Gap(4)
        ->ItemsStart()
        ->Child(
            Div(a)->H(16)->ItemsCenter()->Child(TextEl(a, StrL("Project name"))
                                                    ->Font(12)
                                                    ->Fg(ExampleRgb(0x171717))))
        ->Child(InputBase::New(cx, StrL("example-input"), true)
                    ->OnClick(Listen(cx, &OnInput))
                    ->W(224)
                    ->H(28)
                    ->PadX(8)
                    ->ItemsCenter()
                    ->FocusId(0)
                    ->Border(1, app->input.focused ? ExampleRgb(0x171717)
                                                   : ExampleRgb(0xd4d4d4))
                    ->Child(Input::New(cx, &app->input)));
}

SHOWCASE_PAGE(CompInput, ShowcaseInput);
