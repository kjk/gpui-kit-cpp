#include "base/positioner.h"

namespace gpui {

ResolvedPosition PositionSide(Bounds trigger, Size popup, Size view,
                              float margin, const Placement* preferred,
                              Align align, float offset) {
    AnchoredPosition resolved = AnchoredSideResolve(
        trigger, popup, view, margin, preferred ? (int)*preferred : -1,
        (int)align, offset);
    ResolvedPosition out = {};
    out.bounds = resolved.bounds;
    out.placement = (Placement)resolved.placement;
    out.hasPlacement = true;
    return out;
}

ResolvedPosition PositionCorner(Anchor anchor, Point at, Size popup, Size view,
                                float margin) {
    return PositionCorner(anchor, at, popup, view, EdgesAll(margin));
}

ResolvedPosition PositionCorner(Anchor anchor, Point at, Size popup, Size view,
                                Edges margin) {
    AnchoredPosition resolved =
        AnchoredCornerResolve(anchor, at, popup, view, margin);
    ResolvedPosition out = {};
    out.bounds = resolved.bounds;
    out.hasPlacement = false;
    return out;
}

Edges PositionerFrameInsets(bool clientDecorated, Tiling tiling,
                            float clientInset) {
    if (!clientDecorated) {
        return {};
    }
    return Edges::New(
        tiling.left ? 0.f : clientInset, tiling.right ? 0.f : clientInset,
        tiling.top ? 0.f : clientInset, tiling.bottom ? 0.f : clientInset);
}

Positioner* Positioner::Side(Ctx* cx, Bounds trigger) {
    Positioner* p = ArenaNew<Positioner>(cx->a);
    p->a = cx->a;
    p->strategy = Strategy::Side;
    p->trigger = trigger;
    return p;
}

Positioner* Positioner::Corner(Ctx* cx, Anchor anchor, Point point) {
    Positioner* p = ArenaNew<Positioner>(cx->a);
    p->a = cx->a;
    p->strategy = Strategy::Corner;
    p->anchor = anchor;
    p->point = point;
    return p;
}

Positioner* Positioner::Placement(gpui::Placement value) {
    if (strategy == Strategy::Side) {
        placement = value;
        hasPlacement = true;
    }
    return this;
}

Positioner* Positioner::Align(gpui::Align value) {
    if (strategy == Strategy::Side) {
        align = value;
    }
    return this;
}

Positioner* Positioner::Offset(float value) {
    if (strategy == Strategy::Side) {
        offset = value;
    }
    return this;
}

Positioner* Positioner::Occlude() {
    occlude = true;
    return this;
}

Positioner* Positioner::Margin(float value) {
    margin = value;
    return this;
}

Positioner* Positioner::Child(El* child) {
    if (child) {
        children.Append(a, child);
    }
    return this;
}

El* Positioner::IntoEl() {
    El* group = Div(a)->Flex()->Absolute();
    if (occlude) {
        // `insert_hitbox(bounds, HitboxBehavior::BlockMouse)`, ahead of the
        // children so it blocks what is behind the popup without blocking
        // the popup's own content. A box that stops the press records a hit
        // rect of its own here, and its children record theirs after it, so
        // the hit test finds the content first and the surface next — and
        // the panel under the surface not at all.
        group->StopMouseDown();
    }
    for (El* child : children) {
        group->Child(child);
    }
    Style& s = group->style;
    s.explicitPositioner = true;
    s.positionerCorner = strategy == Strategy::Corner;
    s.positionerTrigger = trigger;
    s.positionerPoint = point;
    s.positionerPlacement = hasPlacement ? (int8_t)placement : (int8_t)-1;
    s.positionerAlign = (uint8_t)align;
    s.anchor = anchor;
    s.anchorGap = offset;
    s.anchorMargin = margin > 0 ? margin : 0;
    return group;
}

} // namespace gpui
