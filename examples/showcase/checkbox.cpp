#include "Showcase.h"
#include "gpui.h"

using namespace gpui;

// The box hands the handler the state its activation produces, the way Rust's
// on_change reports `next_state`. The page stores what it is told instead of
// flipping its own copy.
static void OnCheckbox(ShowcaseApp* app, Ctx* cx, const ClickEvent*,
                       int64_t next) {
    app->checkboxOn = (CheckboxState)next == CheckboxState::Checked;
    Notify(cx);
}

El* ShowcaseCheckbox(ShowcaseApp* app, Ctx* cx) {
    Arena* a = cx->a;
    bool on = app->checkboxOn;
    El* indicator = CheckboxIndicator::New(cx)
                        ->W(16)
                        ->H(16)
                        ->Shrink0()
                        ->ItemsCenter()
                        ->JustifyCenter()
                        ->Border(1, ExampleRgb(0x171717));
    if (on) {
        indicator->Bg(ExampleRgb(0x171717))
            ->Child(TextEl(a, StrL("✓"))->Font(11)->Fg(ExampleRgb(0xffffff)));
    }
    return Checkbox::New(cx, StrL("example-checkbox"),
                         on ? CheckboxState::Checked : CheckboxState::Unchecked,
                         false, Listen(cx, &OnCheckbox))
        ->FlexRow()
        ->ItemsCenter()
        ->Gap(8)
        ->Child(indicator)
        ->Child(TextEl(a, StrL("Enable product updates"))
                    ->Font(12)
                    ->Fg(ExampleRgb(0x171717)));
}

SHOWCASE_PAGE(CompCheckbox, ShowcaseCheckbox);
