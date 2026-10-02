#include "Showcase.h"
#include "gpui.h"

using namespace gpui;

// The switch reports the value its activation produces, the way Rust's
// on_change hands the handler `!checked`.
static void OnSwitch(ShowcaseApp* app, Ctx* cx, const ClickEvent*,
                     int64_t next) {
    app->switchOn = next != 0;
    Notify(cx);
}

El* ShowcaseSwitch(ShowcaseApp* app, Ctx* cx) {
    Arena* a = cx->a;
    bool on = app->switchOn;
    El* track = SwitchTrack::New(cx, StrL("example-switch-track"))
                    ->W(36)
                    ->H(20)
                    ->Pad(2)
                    ->Bg(on ? ExampleRgb(0x171717) : ExampleRgb(0xd4d4d4))
                    ->ItemsCenter()
                    ->Child(SwitchThumb::New(cx)->W(16)->H(16)->Bg(
                        ExampleRgb(0xffffff)));
    if (on) {
        track->JustifyEnd();
    } else {
        track->JustifyStart();
    }
    return Div(a)
        ->W(256)
        ->FlexRow()
        ->ItemsCenter()
        ->JustifyBetween()
        ->Child(
            Div(a)
                ->FlexCol()
                ->Child(TextEl(a, StrL("Automatic updates"))
                            ->Font(12)
                            ->Fg(ExampleRgb(0x171717)))
                ->Child(Div(a)->PadT(4)->Child(
                    TextEl(a, StrL("Install stable releases automatically."))
                        ->Font(12)
                        ->Fg(ExampleRgb(0x737373)))))
        ->Child(Switch::New(cx, StrL("example-switch"), on, false,
                            Listen(cx, &OnSwitch))
                    ->Child(track));
}

SHOWCASE_PAGE(CompSwitch, ShowcaseSwitch);
