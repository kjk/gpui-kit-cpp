#include "Showcase.h"
#include "gpui.h"

using namespace gpui;

static void OnTextarea(ShowcaseApp* app, Ctx* cx, const ClickEvent*) {
    app->textareaOn = true;
    InputFocus(&app->textarea, cx);
    Notify(cx);
}

El* ShowcaseTextarea(ShowcaseApp* app, Ctx* cx) {
    Arena* a = cx->a;
    return Div(a)
        ->FlexCol()
        ->W(224)
        ->Gap(4)
        ->ItemsStart()
        ->Child(Div(a)->H(16)->ItemsCenter()->Child(
            TextEl(a, StrL("Textarea"))->Font(12)->Fg(ExampleRgb(0x171717))))
        ->Child(InputBase::New(cx, StrL("example-textarea"), true)
                    ->OnClick(Listen(cx, &OnTextarea))
                    ->W(224)
                    ->H(64)
                    ->PadX(8)
                    ->PadY(8)
                    ->ClipY()
                    ->FocusId(0)
                    ->Border(1, app->textareaOn ? ExampleRgb(0x171717)
                                                : ExampleRgb(0xd4d4d4))
                    ->Child(Textarea::New(cx, &app->textarea,
                                          ShowcaseEditorStyle())));
}

SHOWCASE_PAGE(CompTextarea, ShowcaseTextarea);
