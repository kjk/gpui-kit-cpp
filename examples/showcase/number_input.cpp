#include "Showcase.h"
#include "gpui.h"

using namespace gpui;

static bool ParseNum(const char* s, int* out) {
    if (!s || !s[0]) {
        return false;
    }
    int n = 0;
    const char* p = s;
    if (*p == '-' || *p == '+') {
        p++;
    }
    if (!*p) {
        return false;
    }
    while (*p) {
        if (*p < '0' || *p > '9') {
            return false;
        }
        n = n * 10 + (*p - '0');
        p++;
    }
    *out = StrToIntUnchecked(Str(s));
    return true;
}

static void FocusNum(ShowcaseApp* app, Ctx* cx, const ClickEvent*) {
    app->input.focused = true;
    Notify(cx);
}

static void StepNum(ShowcaseApp* app, Ctx* cx, const ClickEvent*,
                    intptr_t delta) {
    int n = 0;
    if (!ParseNum(InputCStr(&app->input), &n)) {
        n = 0;
    }
    n += (int)delta;
    InputSetValue(&app->input, fmt("%d", n));
    Notify(cx);
}

El* ShowcaseNumberInput(ShowcaseApp* app, Ctx* cx) {
    Arena* a = cx->a;
    int dummy = 0;
    bool valid = ParseNum(InputCStr(&app->input), &dummy);
    El* controls = Div(a)->FlexCol()->W(24)->Shrink0();
    controls->Child(
        ScButton(cx, StrL("inc"))
            ->OnClick(Listen(cx, &StepNum, 1))
            ->Flex1()
            ->W(24)
            ->ItemsCenter()
            ->JustifyCenter()
            ->Bg(Rgb(0, 0, 0))
            ->HoverBg(ExampleRgb(0x404040))
            ->Child(TextEl(a, StrL("+"))->Font(12)->Fg(ExampleRgb(0xffffff))));
    controls->Child(
        ScButton(cx, StrL("dec"))
            ->OnClick(Listen(cx, &StepNum, -1))
            ->Flex1()
            ->W(24)
            ->ItemsCenter()
            ->JustifyCenter()
            ->Bg(Rgb(0, 0, 0))
            ->HoverBg(ExampleRgb(0x404040))
            ->Child(TextEl(a, StrL("−"))->Font(12)->Fg(ExampleRgb(0xffffff))));

    return Div(a)
        ->FlexCol()
        ->W(200)
        ->Gap(4)
        ->Child(TextEl(a, StrL("Quantity"))->Font(12)->Fg(ExampleRgb(0x171717)))
        ->Child(
            NumberInput::New(cx, StrL("quantity"))
                ->FlexRow()
                ->W(kFill)
                ->H(28)
                ->ItemsCenter()
                ->Border(1, valid ? ExampleRgb(0x171717) : ExampleRgb(0x737373))
                ->Child(InputBase::New(cx, StrL("number-field"), true)
                            ->OnClick(Listen(cx, &FocusNum))
                            ->FocusId(0)
                            ->Flex1()
                            ->H(28)
                            ->PadX(8)
                            ->ItemsCenter()
                            ->Child(Input::New(cx, &app->input)))
                ->Child(controls))
        ->Child(TextEl(a, valid ? StrL("Step: 1") : StrL("Enter a number"))
                    ->Font(12)
                    ->Fg(ExampleRgb(0x737373)));
}

SHOWCASE_PAGE(CompNumberInput, ShowcaseNumberInput);
