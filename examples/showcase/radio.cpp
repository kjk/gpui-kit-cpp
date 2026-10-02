#include "Showcase.h"
#include "gpui.h"

using namespace gpui;

static void PickRadio(ShowcaseApp* app, Ctx* cx, const ClickEvent*,
                      int64_t ix) {
    app->radioSel = (int)ix;
    Notify(cx);
}

static El* RadioDot(Ctx* cx, bool on) {
    Arena* a = cx->a;
    El* outer =
        Div(a)->W(14)->H(14)->Shrink0()->ItemsCenter()->JustifyCenter()->Border(
            1, ExampleRgb(0x171717));
    if (on) {
        outer->Child(Div(a)->W(6)->H(6)->Bg(ExampleRgb(0x171717)));
    }
    return outer;
}

static El* RadioRow(Ctx* cx, Str id, Listener onClick, bool on,
                    const char* title, const char* sub) {
    Arena* a = cx->a;
    El* row =
        Radio::New(cx, id, on, false, onClick)->FlexRow()->ItemsStart()->Gap(8);
    row->Child(Div(a)->PadT(2)->Child(RadioDot(cx, on)));
    row->Child(
        Div(a)
            ->FlexCol()
            ->Child(TextEl(a, Str(title))->Font(12)->Fg(ExampleRgb(0x171717)))
            ->Child(TextEl(a, Str(sub))->Font(12)->Fg(ExampleRgb(0x737373))));
    return row;
}

// The group's third row, which is Rust's own shape rather than the other
// two's: a disabled radio at 0.45 opacity, an empty box with no dot branch at
// all, and a second line that keeps the ink colour the muted one has.
static El* RadioRowDisabled(Ctx* cx, Str id, const char* title,
                            const char* sub) {
    Arena* a = cx->a;
    return Radio::New(cx, id, false, true, Listener{})
        ->FlexRow()
        ->ItemsStart()
        ->Gap(8)
        ->Opacity(0.45f)
        ->Child(Div(a)->PadT(2)->Child(
            Div(a)->W(14)->H(14)->Shrink0()->Border(1, ExampleRgb(0x171717))))
        ->Child(
            Div(a)
                ->FlexCol()
                ->Child(
                    TextEl(a, Str(title))->Font(12)->Fg(ExampleRgb(0x171717)))
                ->Child(
                    TextEl(a, Str(sub))->Font(12)->Fg(ExampleRgb(0x171717))));
}

El* ShowcaseRadio(ShowcaseApp* app, Ctx* cx) {
    return RadioRow(cx, StrL("example-radio"), Listen(cx, &PickRadio, 0),
                    app->radioSel == 0, "Standard", "3–5 business days");
}

El* ShowcaseRadioGroup(ShowcaseApp* app, Ctx* cx) {
    return RadioGroup::New(cx, StrL("example-radio-group"))
        ->W(224)
        ->FlexCol()
        ->Gap(8)
        ->Child(ShowcaseRadio(app, cx))
        ->Child(RadioRow(cx, StrL("express-radio"), Listen(cx, &PickRadio, 1),
                         app->radioSel == 1, "Express", "Next business day"))
        ->Child(RadioRowDisabled(cx, StrL("pickup-radio"), "Local pickup",
                                 "Currently unavailable"));
}

SHOWCASE_PAGE(CompRadio, ShowcaseRadio);
SHOWCASE_PAGE(CompRadioGroup, ShowcaseRadioGroup);
