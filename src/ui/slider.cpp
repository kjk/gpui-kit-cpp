#include "ui/slider.h"

namespace gpui {

namespace component {

Slider* Slider::New(Ctx* cx, Str id, SliderState* state) {
    Arena* a = cx->a;
    Slider* s = ArenaNew<Slider>(a);
    s->a = a;
    s->cx = cx;
    s->id = id;
    s->state = state;
    return s;
}
Slider* Slider::Reverse(bool v) {
    reverse = v;
    return this;
}
Slider* Slider::Vertical(bool v) {
    axis = v ? Axis::Vertical : Axis::Horizontal;
    return this;
}
Slider* Slider::WithAxis(Axis v) {
    axis = v;
    return this;
}
Slider* Slider::Disabled(bool v) {
    disabled = v;
    return this;
}
Slider* Slider::W(float px) {
    width = px;
    return this;
}
Slider* Slider::OnChange(Listener fn) {
    onChange = fn;
    return this;
}

static float Clamp01f(float v) {
    if (v < 0) {
        return 0;
    }
    return v > 1 ? 1 : v;
}

Slider* Slider::WFill() {
    width = kFill;
    return this;
}
Slider* Slider::Bg(Rgba c) {
    bar = c;
    hasBar = true;
    return this;
}

El* Slider::IntoEl() {
    const Theme& th = ThemeNow(cx->app);
    // The percentages the state already worked out from the value; a
    // single-value slider pins the low end at 0.
    float low = state ? Clamp01f(state->pctLo) : 0.f;
    float hi = state ? Clamp01f(state->pctHi) : 0.f;
    bool range = state && state->value.range;
    if (low > hi) {
        float t = low;
        low = hi;
        hi = t;
    }
    // The subscription belongs to the state, the way InputState's does: the
    // window raises the event, not the element.
    if (state && !disabled) {
        state->onChange = onChange;
    }
    // h_1p5: the indicator. size_4: the thumb, a 1px ring of bar_color at
    // 50%. h_6: the track.
    const float kBar = Rems(cx, 0.375f);
    const float kThumb = Rems(cx, 1);
    const float kH = Rems(cx, 1.5f);
    // THUMB_RING_WIDTH / THUMB_RING_OPACITY / THUMB_RING_DURATION: a
    // translucent ring grows outside the thumb while the pointer is over it.
    const float kRingWidth = 3.f;
    const float kRingOpacity = 0.5f;
    // w_full() across, h(px(120.)) upright, unless the caller sized it.
    float w = width == kAuto ? (axis == Axis::Vertical ? 120.f : kFill) : width;
    float mid = (kH - kBar) * 0.5f;

    // bar_color, the theme's slider_bar unless the style's background named
    // one: the indicator at 20%, the fill whole, the thumb's ring at 50%.
    Rgba barColor = hasBar ? bar : th.sliderBar;
    Background railBg = RgbaOpacity(barColor, 0.2f);
    Background fillBg = barColor;
    Rgba thumbBorder = RgbaOpacity(barColor, 0.5f);
    SliderState* bind = disabled ? nullptr : state;

    // The ring shows while the pointer is over the thumb, and stays while the
    // thumb is dragged: dragging moves the thumb under the pointer, so hover
    // alone drops out for a frame on every move and the ring would flicker.
    // A drag here is the window holding the slider's own state, which is what
    // `ThumbInteraction::pressed` stands for upstream.
    auto ThumbRing = [&](El* thumb, int thumbId, Rgba color) -> El* {
        if (!thumb) {
            return thumb;
        }
        bool active = cx->win &&
                      (cx->win->hoverId == thumbId || (bind && bind->dragging));
        // spring_control: a click presses and releases faster than the ring
        // finishes growing, so it is sprung to decelerate through the
        // reversal. Critically damped, so it never grows past the width the
        // thumb reserves for it. slider.rs names no spring of its own now.
        float progress = SpringValue(
            cx,
            MotionId(StrL("slider-thumb-ring"), StrDup(a, fmt("%d", thumbId))),
            active ? 1.f : 0.f, th.motion.springControl);
        if (progress <= 0.f) {
            return thumb;
        }
        float grown = kRingWidth * progress;
        // Behind the thumb and centred on it, so the thumb's own edge stays
        // where it is and the ring is what grows.
        return thumb
            ->Child(Div(a)
                        ->Absolute()
                        ->Left(-grown)
                        ->Top(-grown)
                        ->W(kThumb + grown * 2.f)
                        ->H(kThumb + grown * 2.f)
                        ->Radius(th.radiusFull)
                        ->Bg(RgbaOpacity(color, kRingOpacity * progress)));
    };

    if (axis == Axis::Vertical) {
        // The same three parts turned on their side, filling upward from the
        // bottom of the track.
        int vid = HashClickId(id.s ? id : StrL("slider-v"));
        El* vtrack = SliderTrack::New(cx, bind, axis)->W(kH)->H(w)->Click(vid);
        // Focusable, so the arrows can reach it: `on_a11y_action(Increment |
        // Decrement)` is what they stand in for, and a track that cannot take
        // focus has no way to hear them.
        if (!disabled) {
            vtrack->FocusId(vid);
        }
        vtrack->Child(SliderIndicator::New(cx, bind)
                          ->Absolute()
                          ->Left(mid)
                          ->Top(0)
                          ->W(kBar)
                          ->H(w)
                          ->Radius(th.radiusFull)
                          ->Bg(railBg));
        float vFrom = reverse ? hi : low;
        float vTo = reverse ? 1.f : hi;
        vtrack->Child(Div(a)
                          ->Absolute()
                          ->Left(mid)
                          ->Top(w * (1.f - vTo))
                          ->W(kBar)
                          ->H(w * (vTo - vFrom))
                          ->Radius(th.radiusFull)
                          ->Bg(fillBg));
        for (int i = 0; i < (range ? 2 : 1); i++) {
            float at = (range && i == 0) ? low : hi;
            int thumbId = vid * 31 + (i + 1);
            vtrack->Child(ThumbRing(SliderThumb::New(cx)
                                        ->Absolute()
                                        ->Left((kH - kThumb) * 0.5f)
                                        ->Top(w * (1.f - at) - kThumb * 0.5f)
                                        ->W(kThumb)
                                        ->H(kThumb)
                                        ->Radius(th.radiusFull)
                                        ->Bg(th.tokens.sliderThumb)
                                        ->Border(1, thumbBorder)
                                        ->Click(thumbId)
                                        ->BindSlider(bind, axis),
                                    thumbId, thumbBorder));
        }
        El* root = gpui::Slider::New(cx, state, axis)
                       ->Id(id.s ? id : StrL("slider"))
                       ->AriaDisabled(disabled)
                       ->AriaOrientation(AccessibilityOrientation::Vertical)
                       ->W(kH)
                       ->H(w)
                       ->Child(vtrack);
        return root;
    }

    int tid = HashClickId(id.s ? id : StrL("slider"));
    // w_full(): the parts are placed by `left(relative(..))` the way Rust's
    // are, so the row the slider sits in decides how long it is. A pixel
    // width keeps the arithmetic it always had.
    bool fill = w == kFill;
    El* track = SliderTrack::New(cx, bind, axis)->H(kH)->Click(tid);
    if (fill) {
        track->W(kFill);
    } else {
        track->W(w);
    }
    if (!disabled) {
        track->FocusId(tid);
    }
    El* rail =
        SliderIndicator::New(cx, bind)->Absolute()->Top(mid)->Left(0)->H(kBar);
    rail->Radius(th.radiusFull)->Bg(railBg);
    if (fill) {
        rail->Right(0);
    } else {
        rail->W(w);
    }
    track->Child(rail);
    // Reversed, the filled part is what is left beyond the thumb.
    float fillFrom = reverse ? hi : low;
    float fillTo = reverse ? 1.f : hi;
    El* bar2 = Div(a)->Absolute()->Top(mid)->H(kBar);
    bar2->Radius(th.radiusFull)->Bg(fillBg);
    if (fill) {
        bar2->Left(0)->LeftRel(fillFrom)->Right(0)->RightRel(1.f - fillTo);
    } else {
        bar2->Left(w * fillFrom)->W(w * (fillTo - fillFrom));
    }
    track->Child(bar2);
    for (int i = 0; i < (range ? 2 : 1); i++) {
        float at = (range && i == 0) ? low : hi;
        El* thumb = SliderThumb::New(cx)
                        ->Absolute()
                        ->Top((kH - kThumb) * 0.5f)
                        ->W(kThumb)
                        ->H(kThumb)
                        ->Radius(th.radiusFull)
                        ->Bg(th.tokens.sliderThumb)
                        ->Border(1, thumbBorder);
        if (fill) {
            thumb->Left(-kThumb * 0.5f)->LeftRel(at);
        } else {
            thumb->Left(w * at - kThumb * 0.5f);
        }
        int thumbId = HashClickId(id.s ? id : StrL("slider")) * 31 + (i + 1);
        // The click id is what makes the window report the thumb as hovered;
        // the binding is what keeps a press on it starting the drag, since a
        // hit rect of its own would otherwise shadow the track's.
        thumb->Click(thumbId)->BindSlider(bind, axis);
        track->Child(ThumbRing(thumb, thumbId, thumbBorder));
    }
    El* root = gpui::Slider::New(cx, state, axis)
                   ->Id(id.s ? id : StrL("slider"))
                   ->AriaDisabled(disabled)
                   ->AriaOrientation(AccessibilityOrientation::Horizontal)
                   ->H(kH)
                   ->Child(track);
    return fill ? root->W(kFill) : root->W(w);
}

} // namespace component
} // namespace gpui
