#include "base/popup.h"

namespace gpui {

struct PopupAnchorState {
    bool captured = false;
};

Point PopupResolvedCorner(PopupAnchor anchor, Bounds b) {
    switch (anchor) {
        case PopupAnchor::TopLeft:
            return {b.x, b.y};
        case PopupAnchor::TopCenter:
            return {b.CenterX(), b.y};
        case PopupAnchor::TopRight:
            return {b.Right(), b.y};
        case PopupAnchor::BottomLeft:
            return {b.x, b.y - b.h};
        case PopupAnchor::BottomCenter:
            return {b.CenterX(), b.y - b.h};
        case PopupAnchor::BottomRight:
            return {b.Right(), b.y - b.h};
        default:
            // LeftCenter and RightCenter: Rust hands back the origin, since a
            // popup anchored sideways is placed by the positioner instead.
            return {b.x, b.y};
    }
}

El* PopupPlaceContent(El* content, PopupAnchor anchor, float offset) {
    if (!content) {
        return content;
    }
    // Positioner::corner with Popup's own 8 px viewport margin. The runtime
    // can measure the trigger and child in one layout pass, so it does not
    // need Rust's first-frame prepaint capture; the resulting bounds are the
    // same. Deferred is the structural equivalent of with_priority(100).
    //
    // `.occlude()`: the host blocks the mouse, so no caller has to remember —
    // what a popup covers belongs to the popup. That takes in Popover, the
    // dropdown menu over it and HoverCard in one place. The surface records
    // a hit rect ahead of its children, so its own content still hears the
    // pointer and the panel under it does not.
    return content->AnchorCorner(anchor, kPopupWindowMargin, offset)
        ->StopMouseDown()
        ->Deferred();
}

Popup* Popup::New(Ctx* cx, Str id, El* trigger, PopupAnchor anchor) {
    Arena* a = cx->a;
    Popup* p = ArenaNew<Popup>(a);
    p->a = a;
    p->anchor = anchor;
    PopupAnchorState* state =
        ElementState<PopupAnchorState>(cx, id, StrL("PopupAnchorState"));
    p->contentReady = state && state->captured;
    if (state && !state->captured) {
        state->captured = true;
        WindowRequestAnimationFrame(cx->win);
    }
    // Root sizes to the trigger only. Content is an overlay (Rust Positioner).
    p->root = Div(a)->Id(id);
    if (trigger) {
        p->root->Child(trigger);
    }
    return p;
}

Popup* Popup::Anchor(PopupAnchor value) {
    anchor = value;
    return this;
}

Popup* Popup::AnchorRight(bool on) {
    anchor = on ? PopupAnchor::TopRight : PopupAnchor::TopLeft;
    return this;
}

Popup* Popup::Offset(float v) {
    offset = v;
    return this;
}

Popup* Popup::OnPosition(PopupOnPositionFn fn, void* user) {
    onPosition = fn;
    onPositionUser = user;
    return this;
}

struct PopupPositionHook {
    AnchoredPlacedHook placed;
    PopupOnPositionFn fn = nullptr;
    void* user = nullptr;
};

static void PopupPlaced(void* user, AnchoredPosition placed, Bounds trigger) {
    PopupPositionHook* hook = (PopupPositionHook*)user;
    ResolvedPosition position = {};
    position.bounds = placed.bounds;
    hook->fn(hook->user, position, trigger);
}

Popup* Popup::Content(El* content) {
    if (!content || !contentReady) {
        return this;
    }
    // Positioner::corner is out of flow and deferred; in-flow content would
    // grow its trigger's page and paint below later siblings.
    if (!content->style.absolute) {
        PopupPlaceContent(content, anchor, offset);
    }
    if (onPosition) {
        // Positioner::on_position, which Rust's Popup wraps with the trigger
        // bounds it captured; here the anchored pass knows both.
        PopupPositionHook* hook = ArenaNew<PopupPositionHook>(a);
        hook->fn = onPosition;
        hook->user = onPositionUser;
        hook->placed.fn = &PopupPlaced;
        hook->placed.user = hook;
        content->onPlaced = &hook->placed;
    }
    root->Child(content);
    return this;
}

El* Popup::IntoEl() {
    return root;
}
} // namespace gpui
