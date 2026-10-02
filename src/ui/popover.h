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
// arrow reaches past the surface, 0.375rem, in rems at the window's rem size.
const float kPopoverOffsetRems = 0.25f;
const float kPopoverArrowRems = 0.375f;

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

// popover.rs Popover. Its surface is Rust's: a v_flex that takes
// popover_style().p_3() while `appearance` is on, holds the content and then
// the children, and takes the caller's refinement last -- so `Refine` styles
// the surface the way Rust's Styled impl does, and `Appearance(false)`
// leaves a bare box for the caller to dress.
struct Popover {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Str id = {};
    El* trigger = nullptr;
    // trigger(Button): a Selectable trigger, built once the popover knows
    // whether it is open so it can show it (`trigger.open(open || is_open)`).
    component::Button* triggerButton = nullptr;
    // content(..): the surface's first child. `Content` takes it built;
    // `ContentBuilder` is Rust's closure, run only while the popover is open.
    El* content = nullptr;
    El* (*contentFn)(void* user, Ctx* cx) = nullptr;
    void* contentUser = nullptr;
    // ParentElement: the surface's children after the content.
    ArenaVec<El*> children;
    // appearance(..): popover_style().p_3() on the surface, and the ring
    // around the arrow. On by default.
    bool appearance = true;
    // Styled: the refinement laid on the surface, by field, and a caller's
    // whole refinement (a script's style) applied after it.
    Style style = {};
    uint32_t styleSet = 0;
    ElRefiner refiner = {};
    // Set only by Open(). Without it the popover keeps its own state and the
    // trigger's press toggles it, which is Rust's uncontrolled default;
    // Open() is Rust's `.open(Some(b))`.
    bool controlled = false;
    bool open = false;
    bool defaultOpen = false;
    // Popover::anchor, default TopLeft.
    PopupAnchor anchor = PopupAnchor::TopLeft;
    // Popover::offset, None until set: the gap from the trigger to the
    // surface (or the arrow tip), kPopoverOffsetRems by default.
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
    // child(..): another child of the surface, after the content.
    Popover* Child(El* e);
    // appearance(false): no popover_style or padding on the surface, and no
    // ring around the arrow.
    Popover* Appearance(bool v);
    // Styled: refine the surface's `fields` from `s` (its width, padding,
    // gap, text size, background ...). A background set here is also what
    // the arrow fills with.
    Popover* Refine(const Style& s, uint32_t fields);
    // A refinement Style's fields cannot name (a shadow, a script's whole
    // style), applied to the surface after Refine.
    Popover* RefineWith(ElRefiner r);
    Popover* Open(bool v);
    Popover* DefaultOpen(bool v);
    Popover* Button(MouseButton b);
    Popover* Anchor(PopupAnchor v);
    // Gap from the trigger to the surface (or arrow tip), default 0.25rem.
    // Preserves the anchor and does not enable automatic flipping.
    Popover* Offset(float v);
    // Show an arrow pointing toward the trigger. Follows the anchor, with its
    // base inset to avoid rounded corners, and uses the background Refine
    // set, falling back to the theme's popover colour; outlined with the
    // popover ring while `appearance` is on.
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
    Popover* TriggerStyle(const Style& s, uint32_t fields);
    El* IntoEl();
};

// Whether the popover of this id is showing, for a page that has to know
// before it builds the content.
bool PopoverOpen(Ctx* cx, Str id);
// The keyed state itself, resolved while building (the key is the id under
// the element path being built), for a handler that later has to close the
// popover from inside its content: Rust's `cx.emit(DismissEvent)`, which here
// is PopoverSetOpen(cx, state, false).
Entity<PopoverState> PopoverStateOf(Ctx* cx, Str id);

} // namespace component
} // namespace gpui
#endif // GPUI_UI_POPOVER_H_
