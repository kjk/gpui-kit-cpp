#include "ui/scroll.h"
#include "base/scroll_bounce.h"

namespace gpui {

namespace component {

Scrollable* Scrollable::New(Ctx* cx) {
    Arena* a = cx->a;
    Scrollable* s = ArenaNew<Scrollable>(a);
    s->a = a;
    s->cx = cx;
    s->element = Div(a)->W(kFill);
    return s;
}
Scrollable* Scrollable::New(Ctx* cx, Str id) {
    Scrollable* s = New(cx);
    s->id = id;
    return s;
}
Scrollable* Scrollable::New(Ctx* cx, El* element, ScrollAxis axis) {
    Scrollable* s = New(cx);
    s->element = element ? element : Div(cx->a);
    s->axis = axis;
    return s;
}
Scrollable* Scrollable::Id(Str v) {
    id = v;
    return this;
}
Scrollable* Scrollable::OnScroll(Listener fn) {
    onScroll = fn;
    return this;
}
Scrollable* Scrollable::Child(El* e) {
    if (element && e) {
        element->Child(e);
    }
    return this;
}
Scrollable* Scrollable::ScrollY(float v) {
    scrollY = v;
    return this;
}
Scrollable* Scrollable::ScrollX(float v) {
    scrollX = v;
    return this;
}
Scrollable* Scrollable::Axis(ScrollAxis v) {
    axis = v;
    return this;
}
Scrollable* Scrollable::Mode(ScrollbarMode v) {
    mode = v;
    modeSet = true;
    return this;
}
Scrollable* Scrollable::H(float v) {
    h = v;
    hSet = true;
    return this;
}
Scrollable* Scrollable::ScrollBounce(bool v) {
    scrollBounce = v ? 1 : 0;
    return this;
}

El* Scrollable::IntoEl() {
    // The scrolled box is the base one — the clip, the offsets, the id and
    // the listener all come together there, so the themed wrapper cannot end
    // up with a box that clips but does not take the wheel.
    Str scrollId = id.s ? id : StrL("scrollable");
    El* box =
        Scrollbar::Apply(cx, element, scrollId, scrollY, scrollX, onScroll,
                         axis, modeSet ? mode : ScrollbarModeNow(cx->app));
    if (hSet) {
        box->H(h);
    }
    // A horizontal-only area does not bounce. Every other axis owns its
    // vertical wheel, stretch or not, so a nested scroller stops at its end.
    if (axis == ScrollAxis::Horizontal) {
        return box;
    }
    Entity<ScrollBounceMetrics> met =
        ScrollBounceMetricsEntity(cx, scrollId, true);
    ScrollBounceMetrics* m = met.Get(cx);
    if (!m) return box;
    m->offset = scrollY;
    gpui::ScrollBounce* bounce =
        gpui::ScrollBounce::New(cx, StrL("scrollable-bounce"),
                                PositiveScrollHandle(&m->slot), box)
            ->Measure(&m->slot);
    if (scrollBounce >= 0) bounce->Enabled(scrollBounce == 1);
    if (onScroll.IsValid()) bounce->OnScroll(onScroll);
    return bounce->IntoEl();
}

El* ScrollableElement::Scrollbar(Ctx* cx, El* element, Str id, float scrollY,
                                 float scrollX, Listener onScroll,
                                 ScrollbarAxis axis) {
    return gpui::Scrollbar::Apply(cx, element, id, scrollY, scrollX, onScroll,
                                  axis);
}

El* ScrollableElement::VerticalScrollbar(Ctx* cx, El* element, Str id,
                                         float scrollY, Listener onScroll) {
    return Scrollbar(cx, element, id, scrollY, 0, onScroll,
                     ScrollbarAxis::Vertical);
}

El* ScrollableElement::HorizontalScrollbar(Ctx* cx, El* element, Str id,
                                           float scrollX, Listener onScroll) {
    return Scrollbar(cx, element, id, 0, scrollX, onScroll,
                     ScrollbarAxis::Horizontal);
}

Scrollable* ScrollableElement::OverflowScrollbar(Ctx* cx, El* element) {
    return Scrollable::New(cx, element, ScrollAxis::Both);
}

Scrollable* ScrollableElement::OverflowXScrollbar(Ctx* cx, El* element) {
    return Scrollable::New(cx, element, ScrollAxis::Horizontal);
}

Scrollable* ScrollableElement::OverflowYScrollbar(Ctx* cx, El* element) {
    return Scrollable::New(cx, element, ScrollAxis::Vertical);
}

// ScrollableMask itself now lives in base/scrollable_mask.cpp; scroll/mod.rs
// keeps only the re-export.

} // namespace component
} // namespace gpui
