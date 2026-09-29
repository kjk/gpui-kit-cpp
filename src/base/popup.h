#ifndef GPUI_BASE_POPUP_H_
#define GPUI_BASE_POPUP_H_
/* Unstyled popup — crates/base/src/popup.rs */

#include "gpui/gpui.h"
#include "base/positioner.h"

namespace gpui {

// Rust imports gpui::Anchor directly. Keep the older port spelling as an
// alias so existing examples do not need a flag-day rename.
using PopupAnchor = Anchor;

// popup.rs's two concrete overlay constants. Deferred priorities are mapped
// to the runtime's paint layers, but the public value remains available for
// components that compare or forward it.
constexpr int kPopupPriority = 100;
constexpr float kPopupWindowMargin = 8.f;

// Popup::resolved_corner, still public upstream: top anchors use the trigger
// origin; bottom anchors subtract its height. Placement no longer goes
// through it — see AnchorPosition.
Point PopupResolvedCorner(PopupAnchor anchor, Bounds triggerBounds);

// Place `anchor`'s point on content at AnchorPosition(anchor, trigger,
// offset) — the trigger's opposite edge, `offset` further out — clamp it
// eight pixels inside the window, and defer its paint. Every Popup-backed
// surface goes through this so the eight anchors cannot drift apart.
El* PopupPlaceContent(El* content, PopupAnchor anchor, float offset = 0);

// Popup::on_position's callback: the resolved popup bounds and the trigger
// bounds it was placed against, before the content paints. Rust's boxed
// closure is a function and the value it captured.
using PopupOnPositionFn = void (*)(void* user, ResolvedPosition position,
                                   Bounds trigger);

struct Popup {
    Arena* a = nullptr;
    El* root = nullptr;
    // Where the content hangs. Rust defaults to TopLeft, and so does this.
    PopupAnchor anchor = PopupAnchor::TopLeft;
    // Gap from the trigger along the anchor's outward direction, zero by
    // default.
    float offset = 0;
    PopupOnPositionFn onPosition = nullptr;
    void* onPositionUser = nullptr;
    // Rust withholds deferred content until the trigger's first prepaint has
    // captured bounds. This runtime can place from live layout, but keeps the
    // same first-frame visibility contract.
    bool contentReady = false;

    static Popup* New(Ctx* cx, Str id, El* trigger,
                      PopupAnchor anchor = PopupAnchor::TopLeft);
    Popup* Anchor(PopupAnchor a);
    // The older spelling, kept for the pages that only need the right edge
    // lined up: Anchor(TopRight).
    Popup* AnchorRight(bool on = true);
    Popup* Offset(float offset);
    // Observe resolved popup and trigger bounds before content paints.
    Popup* OnPosition(PopupOnPositionFn fn, void* user);
    Popup* Content(El* content);
    El* IntoEl();
};
} // namespace gpui
#endif // GPUI_BASE_POPUP_H_
