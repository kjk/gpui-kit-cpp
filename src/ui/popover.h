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

struct Popover {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Str id = {};
    El* trigger = nullptr;
    El* content = nullptr;
    // Set only by Open(). Without it the popover keeps its own state and the
    // trigger's press toggles it, which is Rust's uncontrolled default;
    // Open() is Rust's `.open(Some(b))`.
    bool controlled = false;
    bool open = false;
    bool defaultOpen = false;
    // Popover::anchor, default TopLeft.
    PopupAnchor anchor = PopupAnchor::TopLeft;
    // Popover::mouse_button. A right-button popover is a context menu.
    MouseButton button = MouseButton::Left;
    bool overlayClosable = true;
    Listener onOpenChange;
    Listener onClose;

    static Popover* New(Ctx* cx);
    static Popover* New(Ctx* cx, Str id);
    Popover* Trigger(El* e);
    Popover* Content(El* e);
    Popover* Open(bool v);
    Popover* DefaultOpen(bool v);
    Popover* Button(MouseButton b);
    Popover* Anchor(PopupAnchor v);
    Popover* OverlayClosable(bool v);
    // Receives PopoverOpenChangeEvent with the new state for both opening and
    // closing, matching Rust's on_open_change surface.
    Popover* OnOpenChange(Listener fn);
    // What escape runs on a controlled popover, whose open flag is the
    // caller's. Kept for source compatibility; OnOpenChange is the faithful
    // two-direction surface.
    Popover* OnClose(Listener fn);
    El* IntoEl();
};

// Whether the popover of this id is showing, for a page that has to know
// before it builds the content.
bool PopoverOpen(Ctx* cx, Str id);

} // namespace component
} // namespace gpui
#endif // GPUI_UI_POPOVER_H_
