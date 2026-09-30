#ifndef GPUI_UI_POPOVER_H_
#define GPUI_UI_POPOVER_H_
/* Themed popover — crates/ui/src/popover.rs */

#include "ui/sizing.h"

namespace gpui {

namespace component {

// styled.rs `popover_style`: the one surface every popup shares — Popover,
// PopupMenu, Select, Combobox, DatePicker and the editor's hover and
// completion popovers — so they cannot drift apart into three radii and two
// shadows the way upstream's had.
//
// Upstream draws **no border** on a popup. Its edge is a 1px translucent
// ring spent as a shadow layer, so the shadow shows through it. The two
// blurred layers use Tailwind's radii halved (CSS blur is 2σ; PaintBoxShadow
// takes σ), matching styled.rs popover_shadow.
El* PopoverSurface(Ctx* cx, El* e);

// popover.rs: how long a dropdown takes to settle into place after it opens,
// which is shadcn/ui's `animate-in` duration.
const float kDropdownEnterMs = 150.f;
// Where it starts, relative to where it comes to rest: negative is above, so
// the surface slides *down* out of the trigger's edge —
// `data-[side=bottom]:slide-in-from-top-2`, whose 2 is 0.5rem.
const float kDropdownEnterOffset = -8.f;

// `animate_dropdown_open`: the shared open motion for Select, Combobox,
// DatePicker and the menus. Over 150 ms the surface fades up from nothing
// while sliding the last 8 px out of the trigger's edge, on an ease-out curve
// so it decelerates into place. `key` names the popup, so one that closes and
// opens again plays it again.
//
// Upstream also scales the surface up from 95% (`zoom-in-95`); nothing here
// scales an element, so that half is not ported, and its shadow ramp has no
// shadow to ramp.
El* DropdownOpen(Ctx* cx, El* surface, uint32_t key);

// `dropdown_positioner`: where a dropdown surface goes, which is the one
// place upstream reaches for `Positioner::side` rather than the corner
// placement every other popup uses. Side placement is what flips -- a select
// with no room below its trigger opens above it instead of being clamped
// against the window's edge, which would leave it over the trigger it came
// from. Select, Combobox and DatePicker are the three that ask for it.
//
// The surface is placed here, so `Popup::Content` leaves it as it is.
El* DropdownPlaceContent(El* content, float gap = 4);

// popover.rs: the default trigger-to-surface gap, 0.25rem, and how far an
// arrow reaches past the surface, 0.375rem (the port's rem is 16 px).
const float kPopoverOffset = 4.f;
const float kPopoverArrowSize = 6.f;

// popover.rs arrow_anchor: the side of the surface the arrow sits on and the
// trigger point it aims at. The arrow follows the named anchor instead of
// always aiming at the trigger's centre.
struct PopoverArrowAnchor {
    gpui::Placement side = gpui::Placement::Bottom;
    Point target = {};
};
PopoverArrowAnchor ArrowAnchor(PopupAnchor anchor, Bounds trigger);

// popover.rs arrow_points: the arrow's base corners and tip, the base clamped
// clear of the surface's rounded corners while aiming at the trigger.
void ArrowPoints(Bounds surface, Bounds trigger, gpui::Placement side,
                 float depth, float radius, Point out[3]);

// popover.rs arrow_join_bounds: the patch that covers the ring and the
// antialiased base on both sides of the surface edge, inset by the stroke so
// it stays inside the triangle's slopes.
Bounds ArrowJoinBounds(const Point points[3], gpui::Placement side,
                       float stroke);

struct Button;

struct Popover {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Str id = {};
    El* trigger = nullptr;
    // trigger(Button): a Selectable trigger, built once the popover knows
    // whether it is open so it can show it (`trigger.open(open || is_open)`).
    component::Button* triggerButton = nullptr;
    El* content = nullptr;
    // Popover::content(closure): built only while the popover is open, in
    // place of `content`, which is built whether or not it shows.
    El* (*contentFn)(void* user, Ctx* cx) = nullptr;
    void* contentUser = nullptr;
    // Set only by Open(). Without it the popover keeps its own state and the
    // trigger's press toggles it, which is Rust's uncontrolled default;
    // Open() is Rust's `.open(Some(b))`.
    bool controlled = false;
    bool open = false;
    bool defaultOpen = false;
    // Popover::anchor, default TopLeft.
    PopupAnchor anchor = PopupAnchor::TopLeft;
    // Popover::offset, None until set: the gap from the trigger to the
    // surface (or the arrow tip), kPopoverOffset by default.
    float offset = 0;
    bool hasOffset = false;
    // Popover::arrow: an arrow pointing toward the trigger. Default false.
    bool arrow = false;
    // Popover::mouse_button. A right-button popover is a context menu.
    MouseButton button = MouseButton::Left;
    bool overlayClosable = true;
    Listener onOpenChange;
    Listener onClose;
    // trigger_style: the refinement laid onto the trigger container.
    Style triggerStyle = {};
    uint32_t triggerStyleSet = 0;

    static Popover* New(Ctx* cx);
    static Popover* New(Ctx* cx, Str id);
    Popover* Trigger(El* e);
    Popover* Trigger(component::Button* triggerBtn);
    Popover* Content(El* e);
    Popover* ContentBuilder(El* (*fn)(void* user, Ctx* cx), void* user);
    Popover* Open(bool v);
    Popover* DefaultOpen(bool v);
    Popover* Button(MouseButton b);
    Popover* Anchor(PopupAnchor v);
    // Gap from the trigger to the surface (or arrow tip), default 0.25rem.
    // Preserves the anchor and does not enable automatic flipping.
    Popover* Offset(float v);
    // Show an arrow pointing toward the trigger. Follows the anchor, with its
    // base inset to avoid rounded corners, and uses the surface background,
    // falling back to the theme's popover colour.
    Popover* Arrow(bool v);
    Popover* OverlayClosable(bool v);
    // Receives PopoverOpenChangeEvent with the new state for both opening and
    // closing, matching Rust's on_open_change surface.
    Popover* OnOpenChange(Listener fn);
    // What escape runs on a controlled popover, whose open flag is the
    // caller's. Kept for source compatibility; OnOpenChange is the faithful
    // two-direction surface.
    Popover* OnClose(Listener fn);
    // trigger_style: style the trigger container — the element laid out in
    // the parent and measured to anchor the popup — so this is where a full
    // width or flex_1 goes for the trigger to fill its slot. Only the fields
    // in `fields` apply.
    Popover* TriggerStyle(const Style& style, uint32_t fields);
    El* IntoEl();
};

// Whether the popover of this id is showing, for a page that has to know
// before it builds the content.
bool PopoverOpen(Ctx* cx, Str id);

} // namespace component
} // namespace gpui
#endif // GPUI_UI_POPOVER_H_
