#include "ui/popover.h"
#include "base/actions.h"

namespace gpui {

namespace component {

El* PopoverSurface(Ctx* cx, El* e) {
    if (!e) {
        return e;
    }
    const Theme& th = ThemeNow(cx->app);
    // styled.rs popover_shadow: a 1px translucent ring spent as a shadow
    // layer, plus two blurred layers. No border — an opaque border would
    // composite over the fill instead of letting the shadow show through.
    Rgba ring = RgbaOpacity(th.foreground, 0.1f);
    Rgba ink = Rgba8(0, 0, 0, 26);
    BoxShadow shadows[3] = {
        {0, 0, 0, 1.f, ring, false},
        {0, 4.f, 3.f, -1.f, ink, false},
        {0, 2.f, 2.f, -2.f, ink, false},
    };
    return e->Bg(th.tokens.popover)
        ->Fg(th.popoverFg)
        ->Shadows(shadows, 3)
        ->Radius(th.radius);
}

El* DropdownOpen(Ctx* cx, El* surface, uint32_t key) {
    if (!surface) {
        return surface;
    }
    float t = MotionAppear(cx, key, kDropdownEnterMs, EaseOutCubic);
    if (t >= 1.f) {
        return surface;
    }
    // The fade and the slide are the same curve: `t` is already eased, so the
    // surface decelerates into place rather than arriving at a constant rate.
    surface->Opacity(t);
    surface->Top(kDropdownEnterOffset * (1.f - t));
    return surface;
}

El* DropdownPlaceContent(El* content, float gap) {
    if (!content) {
        return content;
    }
    // dropdown_positioner is the side strategy, not Popup's corner strategy:
    // it opens below with Start alignment, flips if needed, and keeps the
    // same 8 px viewport margin.
    content->AnchorBelow(gap)->Left(0)->Fixed()->Deferred()->AnchorFlip();
    content->style.anchorMargin = kPopupWindowMargin;
    return content;
}

Popover* Popover::New(Ctx* cx) {
    Arena* a = cx->a;
    Popover* p = ArenaNew<Popover>(a);
    p->a = a;
    p->cx = cx;
    return p;
}
Popover* Popover::Trigger(El* e) {
    trigger = e;
    return this;
}
Popover* Popover::Content(El* e) {
    content = e;
    return this;
}
Popover* Popover::ContentBuilder(El* (*fn)(void* user, Ctx* cx), void* user) {
    contentFn = fn;
    contentUser = user;
    return this;
}
Popover* Popover::New(Ctx* cx, Str id) {
    Popover* p = New(cx);
    p->id = id;
    return p;
}
Popover* Popover::Open(bool v) {
    controlled = true;
    open = v;
    return this;
}
Popover* Popover::DefaultOpen(bool v) {
    defaultOpen = v;
    return this;
}
Popover* Popover::OnClose(Listener fn) {
    onClose = fn;
    return this;
}
Popover* Popover::TriggerStyle(const Style& style, uint32_t fields) {
    triggerStyle = style;
    triggerStyleSet = fields;
    return this;
}
Popover* Popover::OverlayClosable(bool v) {
    overlayClosable = v;
    return this;
}
Popover* Popover::OnOpenChange(Listener fn) {
    onOpenChange = fn;
    return this;
}
Popover* Popover::Anchor(PopupAnchor v) {
    anchor = v;
    return this;
}
Popover* Popover::Offset(float v) {
    offset = v;
    hasOffset = true;
    return this;
}
Popover* Popover::Arrow(bool v) {
    arrow = v;
    return this;
}
Popover* Popover::Button(MouseButton b) {
    button = b;
    return this;
}

// The keyed state behind one popover id — Rust's
// `window.use_keyed_state(self.id, |cx| PopoverState::new(default_open, cx))`.
static Entity<PopoverState> PopState(Ctx* cx, Str id) {
    return KeyedEntity<PopoverState>(cx, KeyedName(cx, id));
}

bool PopoverOpen(Ctx* cx, Str id) {
    return PopoverIsOpen(cx, PopState(cx, id));
}

PopoverArrowAnchor ArrowAnchor(PopupAnchor anchor, Bounds t) {
    using gpui::Placement;
    switch (anchor) {
        case PopupAnchor::TopLeft:
            return {Placement::Bottom, {t.x, t.Bottom()}};
        case PopupAnchor::TopCenter:
            return {Placement::Bottom, {t.CenterX(), t.Bottom()}};
        case PopupAnchor::TopRight:
            return {Placement::Bottom, {t.Right(), t.Bottom()}};
        case PopupAnchor::BottomLeft:
            return {Placement::Top, {t.x, t.y}};
        case PopupAnchor::BottomCenter:
            return {Placement::Top, {t.CenterX(), t.y}};
        case PopupAnchor::BottomRight:
            return {Placement::Top, {t.Right(), t.y}};
        case PopupAnchor::LeftCenter:
            return {Placement::Right, {t.Right(), t.CenterY()}};
        case PopupAnchor::RightCenter:
            return {Placement::Left, {t.x, t.CenterY()}};
    }
    return {Placement::Bottom, {t.x, t.Bottom()}};
}

void ArrowPoints(Bounds surface, Bounds trigger, gpui::Placement side,
                 float depth, float radius, Point out[3]) {
    using gpui::Placement;
    bool horizontal = PlacementIsHorizontal(side);
    float start = horizontal ? surface.y : surface.x;
    float end = horizontal ? surface.Bottom() : surface.Right();
    float target = horizontal ? trigger.CenterY() : trigger.CenterX();
    float span = (end - start) * 0.5f;
    float half = depth < span ? depth : span;
    float inset = radius + half < span ? radius + half : span;
    float center = target;
    if (center < start + inset) {
        center = start + inset;
    }
    if (center > end - inset) {
        center = end - inset;
    }
    switch (side) {
        case Placement::Bottom:
            out[0] = {center - half, surface.y};
            out[1] = {center, surface.y - depth};
            out[2] = {center + half, surface.y};
            break;
        case Placement::Top:
            out[0] = {center - half, surface.Bottom()};
            out[1] = {center, surface.Bottom() + depth};
            out[2] = {center + half, surface.Bottom()};
            break;
        case Placement::Right:
            out[0] = {surface.x, center - half};
            out[1] = {surface.x - depth, center};
            out[2] = {surface.x, center + half};
            break;
        case Placement::Left:
            out[0] = {surface.Right(), center - half};
            out[1] = {surface.Right() + depth, center};
            out[2] = {surface.Right(), center + half};
            break;
    }
}

Bounds ArrowJoinBounds(const Point points[3], gpui::Placement side,
                       float stroke) {
    // Bounds::from_corners of the two base corners, pushed a stroke width
    // across the edge each way and pulled a stroke width in along it.
    if (PlacementIsHorizontal(side)) {
        float span = (points[2].y - points[0].y) * 0.5f;
        float inset = stroke < span ? stroke : span;
        float x0 = points[0].x - stroke;
        float y0 = points[0].y + inset;
        return {x0, y0, points[2].x + stroke - x0, points[2].y - inset - y0};
    }
    float span = (points[2].x - points[0].x) * 0.5f;
    float inset = stroke < span ? stroke : span;
    float x0 = points[0].x + inset;
    float y0 = points[0].y - stroke;
    return {x0, y0, points[2].x - inset - x0, points[2].y + stroke - y0};
}

// What the arrow canvas paints from: the anchor it follows and the trigger
// bounds on_position reported for this frame.
struct PopoverArrowState {
    PopupAnchor anchor = PopupAnchor::TopLeft;
    float size = 0;
    float radius = 0;
    Rgba background = {};
    Rgba ring = {};
    bool outline = false;
    bool placed = false;
    Bounds trigger = {};
};

static void PopoverArrowPositioned(void* user, ResolvedPosition,
                                   Bounds trigger) {
    PopoverArrowState* st = (PopoverArrowState*)user;
    st->trigger = trigger;
    st->placed = true;
}

static void PaintPopoverArrow(PaintCtx* ctx, El* e, void* user) {
    PopoverArrowState* st = (PopoverArrowState*)user;
    if (!st || !st->placed || !ctx->rt) {
        return;
    }
    PopoverArrowAnchor at = ArrowAnchor(st->anchor, st->trigger);
    Bounds surface = {e->x, e->y, e->w, e->h};
    Point points[3];
    ArrowPoints(surface, Bounds{at.target.x, at.target.y, 0, 0}, at.side,
                st->size, st->radius, points);
    if (Path* fill = PathNew(ctx, true)) {
        PathMoveTo(fill, points[0].x, points[0].y);
        PathLineTo(fill, points[1].x, points[1].y);
        PathLineTo(fill, points[2].x, points[2].y);
        PathClose(fill);
        PathFill(ctx, fill, st->background);
        PathFree(fill);
    }
    // The triangle ends exactly at the surface edge. Cover the ring and the
    // antialiased base on both sides of that edge before drawing its two
    // slopes.
    Bounds join = ArrowJoinBounds(points, at.side, 1.f);
    CanvasFillRect(ctx, join.x, join.y, join.w, join.h, st->background);
    if (st->outline) {
        if (Path* outline = PathNew(ctx, true)) {
            PathMoveTo(outline, points[0].x, points[0].y);
            PathLineTo(outline, points[1].x, points[1].y);
            PathLineTo(outline, points[2].x, points[2].y);
            PathStroke(ctx, outline, 1.f, st->ring);
            PathFree(outline);
        }
    }
}

El* Popover::IntoEl() {
    Str popId = id.s ? id : StrL("popover");
    Entity<PopoverState> st = PopState(cx, popId);
    PopoverState* s = st.Get(cx);
    // default_open only counts the first time this key is seen, the way it
    // only reaches PopoverState::new once.
    if (s && !s->seeded) {
        s->seeded = true;
        s->open = defaultOpen;
    }
    if (controlled) {
        PopoverSetOpen(cx, st, open);
    }
    bool isOpen = PopoverIsOpen(cx, st);
    if (isOpen && contentFn) {
        content = contentFn(contentUser, cx);
    }
    const Theme& th = ThemeNow(cx->app);
    float arrowSize = arrow ? kPopoverArrowSize : 0.f;
    float gap = (hasOffset ? offset : kPopoverOffset) + arrowSize;
    PopoverArrowState* arrowState = nullptr;
    if (isOpen && content && arrow) {
        arrowState = ArenaNew<PopoverArrowState>(a);
        arrowState->anchor = anchor;
        arrowState->size = arrowSize;
        arrowState->radius = th.radius;
        // The surface's own background, falling back to the theme's popover
        // colour.
        bool solid = content->style.hasBg && !content->style.bg.gradient;
        arrowState->background = solid ? content->style.bg.color : th.popover;
        // Rust outlines the arrow with popover_ring when `appearance` gave
        // the surface popover_style. The content here is styled by its
        // caller, so the arrow follows what that surface draws: its border,
        // or the ring PopoverSurface spends as a shadow.
        if (content->style.border > 0) {
            arrowState->outline = true;
            arrowState->ring = content->style.borderColor;
        } else if (content->style.shadowCount > 0) {
            arrowState->outline = true;
            arrowState->ring = RgbaOpacity(th.foreground, 0.1f);
        }
        El* canvas = Div(a)->Absolute()->Left(0)->Top(0)->W(kFill)->H(kFill);
        canvas->customPaint = &PaintPopoverArrow;
        canvas->customUser = arrowState;
        content->Child(canvas);
    }
    El* root = gpui::Popover::New(cx, popId, st, button)
                   ->Anchor(anchor)
                   ->Offset(gap)
                   ->OnPosition(arrowState ? &PopoverArrowPositioned : nullptr,
                                arrowState)
                   ->OverlayClosable(overlayClosable)
                   ->OnOpenChange(onOpenChange)
                   ->OnDismiss(onClose)
                   ->Trigger(trigger)
                   ->Content(isOpen ? content : nullptr)
                   ->IntoEl();
    // Base's Popover is Styled, and its style lands on the trigger
    // container; the returned root is that container here.
    if (triggerStyleSet) {
        root->Refine(triggerStyle, triggerStyleSet);
    }
    // popover.rs binds escape to Cancel in the "Popover" context and closes
    // on it. A controlled popover's flag is the caller's, so it says what to
    // run; an uncontrolled one closes its own state.
    if (isOpen) {
        CancelBindKeys(cx, root, "Popover", popId,
                       ListenTo(st, &PopoverDismiss));
    }
    return root;
}

} // namespace component
} // namespace gpui
