#include "Showcase.h"
#include "gpui.h"

using namespace gpui;

// crates/base/examples/showcase/components/time_field.rs: a seconds field
// seeded at 09:30, framed and decorated by the page itself, with the value
// read back under it.
static El* ShowcaseTimeSegment(void*, El* segment,
                               const TimeFieldSegmentState* state, Ctx*) {
    segment->PadX(2);
    if (state->IsSelected()) {
        segment->Bg(ExampleRgb(0xdbeafe));
    }
    return segment;
}

El* ShowcaseTimeField(ShowcaseApp* app, Ctx* cx) {
    Arena* a = cx->a;
    if (!app->timeField.IsValid()) {
        app->timeField = TimeFieldStateNew(cx, TimePrecision::Second);
        if (TimeFieldState* s = app->timeField.Get(cx)) {
            TimeFieldStateSetTime(s, LocalTime{9, 30, 0}, cx);
            // The page opens with the field focused, the way Rust focuses it
            // when the showcase starts on this component.
            TimeFieldStateFocus(s, cx->win);
        }
    }
    TimeFieldState* s = app->timeField.Get(cx);
    LocalTime value = s ? s->Time() : LocalTime{};
    El* field = TimeField::New(cx, StrL("example-time-field"), app->timeField)
                    ->RenderSegment(&ShowcaseTimeSegment)
                    ->IntoEl();
    field->FlexRow()
        ->ItemsCenter()
        ->H(28)
        ->PadX(8)
        ->Border(1, ExampleRgb(0xa3a3a3))
        ->Bg(ScWhite())
        ->Fg(ScInk())
        ->Font(12);
    return Div(a)
        ->FlexCol()
        ->W(224)
        ->Gap(4)
        ->Child(ScTxt(cx, StrL("Reminder time"), 12, ScInk()))
        ->Child(field)
        ->Child(ScTxt(cx,
                      DupFmt(cx, "Selected %02d:%02d:%02d", value.hour,
                             value.minute, value.second),
                      12, ScMutedC()));
}

SHOWCASE_PAGE(CompTimeField, ShowcaseTimeField);
