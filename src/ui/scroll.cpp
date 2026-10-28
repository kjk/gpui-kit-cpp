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

// The bounce box is what the parent lays out. It keeps the caller's size,
// minimum, maximum and flex; the scroll area fills that box and drops the
// maximum on the scrolled axis, so the rows can grow past it (scrollable.rs).
static void HoldScrollArea(El* wrap, El* area, ScrollAxis axis) {
    Style& from = area->style;
    Style& to = wrap->style;
    to.width = from.width;
    to.height = from.height;
    to.widthFrac = from.widthFrac;
    to.heightFrac = from.heightFrac;
    to.minW = from.minW;
    to.minH = from.minH;
    to.maxW = from.maxW;
    to.maxH = from.maxH;
    to.flexGrow = from.flexGrow;
    to.flexShrink = from.flexShrink;
    to.flexBasis = from.flexBasis;
    to.flexBasisFrac = from.flexBasisFrac;
    to.flexBasisPercent = from.flexBasisPercent;
    to.alignSelf = from.alignSelf;
    to.hasAlignSelf = from.hasAlignSelf;
    to.relLengths = (uint16_t)((to.relLengths &
                                (uint16_t)~(kRelMinW | kRelMinH | kRelMaxH)) |
                               (from.relLengths &
                                (uint16_t)(kRelMinW | kRelMinH | kRelMaxH)));
    wrap->FlexCol();
    if (axis == ScrollAxis::Vertical || axis == ScrollAxis::Both) {
        from.maxH = 1e9f;
        from.height = kFill;
        from.heightFrac = 0;
        // min_h_full: 100% of the wrap, not the caller's minimum. kFill is
        // that percentage; the relative bit would read the float as the
        // fraction.
        from.minH = kFill;
        from.relLengths =
            (uint16_t)(from.relLengths & (uint16_t)~(kRelMaxH | kRelMinH));
    }
    if (axis == ScrollAxis::Both) {
        from.maxW = 1e9f;
        from.width = kFill;
        from.widthFrac = 0;
        from.minW = kFill;
        from.relLengths = (uint16_t)(from.relLengths & (uint16_t)~kRelMinW);
    }
    from.flexGrow = 0;
    from.flexShrink = 1;
    from.flexBasis = kAuto;
    from.flexBasisFrac = 0;
    from.flexBasisPercent = false;
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
    // A plain mouse wheel has only a vertical axis, which the locked
    // horizontal area above would ignore.
    if (axis == ScrollAxis::Horizontal) {
        ScrollableMask::ApplyVerticalWheel(box);
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
    El* wrapped = bounce->IntoEl();
    if (wrapped != box) HoldScrollArea(wrapped, box, axis);
    return wrapped;
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
