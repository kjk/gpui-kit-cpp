#include "Showcase.h"
#include "gpui.h"

using namespace gpui;

// SliderEvent::Change, raised by the window while the track is pressed or
// dragged. The state already holds the new value; the page only has to ask
// for a repaint.
static void OnSlider(ShowcaseApp* app, Ctx* cx, const SliderEvent*) {
    (void)app;
    Notify(cx);
}

El* ShowcaseSlider(ShowcaseApp* app, Ctx* cx) {
    Arena* a = cx->a;
    float p = app->slider.pctHi;
    p = ClampF(p, 0, 1);
    float trackW = 224;
    float thumb = 14;
    float fillW = trackW * p;
    float thumbX = fillW - thumb / 2;
    thumbX = ClampF(thumbX, 0, trackW - thumb);

    // The track is what a press lands on, so the state is bound here and the
    // window moves it — SliderTrack::on_mouse_down and its on_drag_move.
    El* track = SliderTrack::New(cx, &app->slider)
                    ->Click(HashClickId(StrL("example-slider-track")))
                    ->W(trackW)
                    ->H(28);
    track->Child(SliderIndicator::New(cx, &app->slider)
                     ->Absolute()
                     ->Top(13)
                     ->Left(0)
                     ->W(trackW)
                     ->H(2)
                     ->Bg(ExampleRgb(0xd4d4d4)));
    track->Child(Div(a)->Absolute()->Top(13)->Left(0)->W(fillW)->H(2)->Bg(
        ExampleRgb(0x171717)));
    track->Child(SliderThumb::New(cx)
                     ->Absolute()
                     ->Top(7)
                     ->Left(thumbX)
                     ->W(thumb)
                     ->H(thumb)
                     ->Bg(ExampleRgb(0xffffff))
                     ->Border(1, ExampleRgb(0x171717)));

    app->slider.onChange = Listen(cx, &OnSlider);

    return Div(a)
        ->FlexCol()
        ->W(trackW)
        ->Child(Div(a)
                    ->FlexRow()
                    ->JustifyBetween()
                    ->W(kFill)
                    ->PadB(8)
                    ->Child(TextEl(a, StrL("Volume"))
                                ->Font(12)
                                ->Fg(ExampleRgb(0x171717)))
                    ->Child(TextEl(a, StrL("Drag to adjust"))
                                ->Font(12)
                                ->Fg(ExampleRgb(0x171717))))
        ->Child(Slider::New(cx, &app->slider)
                    ->AriaLabel(StrL("Volume"))
                    ->W(trackW)
                    ->H(28)
                    ->Child(track));
}

SHOWCASE_PAGE(CompSlider, ShowcaseSlider);
