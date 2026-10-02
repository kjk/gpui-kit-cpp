#include "base/slider.h"
#include "base/element_ext.h"

#include <math.h>

namespace gpui {

SliderValue SliderValueClamp(SliderValue v, float min, float max) {
    v.hi = ClampF(v.hi, min, max);
    if (v.range) {
        v.lo = ClampF(v.lo, min, max);
    }
    return v;
}

void SliderValueSetStart(SliderValue* v, float value) {
    if (v->range) {
        v->lo = value < v->hi ? value : v->hi;
    } else {
        v->hi = value;
    }
}

void SliderValueSetEnd(SliderValue* v, float value) {
    if (v->range) {
        v->hi = value > v->lo ? value : v->lo;
    } else {
        v->hi = value;
    }
}

// A logarithmic scale divides by `min` and takes a log of `max / min`, so both
// have to be positive and apart. Rust asserts; here the pair is nudged to the
// nearest one that works, which keeps the widget drawing.
static void FixLogLimits(SliderState* s) {
    if (s->scale != SliderScale::Logarithmic) {
        return;
    }
    if (s->min <= 0) {
        s->min = 0.0001f;
    }
    if (s->max <= s->min) {
        s->max = s->min * 2.f;
    }
}

SliderState SliderStateNew(float min, float max, SliderValue value, float step,
                           SliderScale scale) {
    SliderState s = {};
    s.min = min;
    s.max = max;
    s.step = step;
    s.scale = scale;
    FixLogLimits(&s);
    s.value = value;
    SliderUpdateThumbPos(&s);
    return s;
}

void SliderSetLimits(SliderState* s, float min, float max) {
    s->min = min;
    s->max = max;
    FixLogLimits(s);
    SliderUpdateThumbPos(s);
}

void SliderSetStep(SliderState* s, float step) {
    s->step = step;
}

void SliderSetScale(SliderState* s, SliderScale scale) {
    s->scale = scale;
    FixLogLimits(s);
    SliderUpdateThumbPos(s);
}

void SliderSetValue(SliderState* s, SliderValue v) {
    s->value = v;
    SliderUpdateThumbPos(s);
}

float SliderPctToValue(const SliderState* s, float pct) {
    if (s->scale == SliderScale::Linear) {
        return s->min + (s->max - s->min) * pct;
    }
    // At pct 0 this is (max/min)^0 * min = min, and at 1 it is max; the clamp
    // is there for the floating point in between.
    float base = s->max / s->min;
    return ClampF(powf(base, pct) * s->min, s->min, s->max);
}

float SliderValueToPct(const SliderState* s, float value) {
    if (s->scale == SliderScale::Linear) {
        float range = s->max - s->min;
        if (range <= 0) {
            return 0;
        }
        return (value - s->min) / range;
    }
    // ::logf, because gpui::logf is this tree's logging call.
    float base = s->max / s->min;
    float logBase = ::logf(base);
    if (logBase == 0 || value <= 0) {
        return 0;
    }
    return ClampF(::logf(value / s->min) / logBase, 0.f, 1.f);
}

void SliderUpdateThumbPos(SliderState* s) {
    if (!s->value.range) {
        s->pctLo = 0;
        s->pctHi = SliderValueToPct(s, ClampF(s->value.hi, s->min, s->max));
        return;
    }
    s->pctLo = SliderValueToPct(s, ClampF(s->value.lo, s->min, s->max));
    s->pctHi = SliderValueToPct(s, ClampF(s->value.hi, s->min, s->max));
}

// The press as a fraction of the track: along x for a horizontal slider, and
// up from the bottom for a vertical one.
static float PctAt(const SliderState* s, Axis axis, Point pos) {
    float inner = AxisIsHorizontal(axis) ? pos.x - s->bounds.x
                                         : s->bounds.Bottom() - pos.y;
    float total = AxisIsHorizontal(axis) ? s->bounds.w : s->bounds.h;
    if (total <= 0) {
        return 0;
    }
    return ClampF(inner, 0.f, total) / total;
}

bool SliderIsStartAt(const SliderState* s, Axis axis, Point pos) {
    if (!s->value.range) {
        return false;
    }
    float center = (s->pctHi - s->pctLo) * 0.5f + s->pctLo;
    return PctAt(s, axis, pos) < center;
}

bool SliderUpdateByPosition(SliderState* s, Axis axis, Point pos,
                            bool isStart) {
    s->dragging = true;
    float pct = PctAt(s, axis, pos);
    pct = isStart ? ClampF(pct, 0.f, s->pctHi) : ClampF(pct, s->pctLo, 1.f);

    float value = SliderPctToValue(s, pct);
    if (s->step > 0) {
        value = roundf(value / s->step) * s->step;
    }

    SliderValue before = s->value;
    if (isStart) {
        s->pctLo = pct;
        SliderValueSetStart(&s->value, value);
    } else {
        s->pctHi = pct;
        SliderValueSetEnd(&s->value, value);
    }
    return s->value.lo != before.lo || s->value.hi != before.hi;
}

bool SliderStepBy(SliderState* s, int dir, bool isStart) {
    if (!s || dir == 0) {
        return false;
    }
    // A slider with no step of its own moves by a hundredth of its range,
    // which is what a percentage slider's arrow is worth.
    float step = s->step > 0 ? s->step : (s->max - s->min) / 100.f;
    if (step <= 0) {
        return false;
    }
    float from = isStart ? s->value.Start() : s->value.End();
    float want = from + step * (float)dir;
    // The same clamp a drag gets: an end never crosses the other one.
    float lo = isStart ? s->min : SliderPctToValue(s, s->pctLo);
    float hi = isStart ? SliderPctToValue(s, s->pctHi) : s->max;
    want = ClampF(want, lo, hi);
    if (want == from) {
        return false;
    }
    if (isStart) {
        SliderValueSetStart(&s->value, want);
    } else {
        SliderValueSetEnd(&s->value, want);
    }
    SliderUpdateThumbPos(s);
    return true;
}

bool SliderHandleRelease(SliderState* s) {
    if (!s->dragging) {
        return false;
    }
    s->dragging = false;
    return true;
}

El* Slider::New(Ctx* cx, SliderState* state, Axis axis, int clickId) {
    Arena* a = cx->a;
    El* e = UiRoot(a, StrL("example-slider"), clickId)
                ->Role(AccessibilityRole::Slider)
                ->AriaOrientation(axis == Axis::Vertical
                                      ? AccessibilityOrientation::Vertical
                                      : AccessibilityOrientation::Horizontal);
    if (state) {
        e->slider = state;
        e->AriaNumericValue(state->value.End())
            ->AriaMinNumericValue(state->min)
            ->AriaMaxNumericValue(state->max)
            ->AriaNumericValueStep(state->step);
    }
    return e;
}
El* SliderTrack::New(Ctx* cx, SliderState* state, Axis axis) {
    Arena* a = cx->a;
    El* e = Div(a);
    if (state) {
        e->BindSlider(state, axis);
    }
    return e;
}
El* SliderIndicator::New(Ctx* cx, SliderState* state) {
    Arena* a = cx->a;
    El* e = Div(a);
    if (state) {
        e->BindSliderBounds(state);
    }
    return e;
}
El* SliderThumb::New(Ctx* cx) {
    Arena* a = cx->a;
    return Div(a);
}
} // namespace gpui
