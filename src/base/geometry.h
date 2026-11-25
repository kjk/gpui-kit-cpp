#ifndef GPUI_BASE_GEOMETRY_H_
#define GPUI_BASE_GEOMETRY_H_
/* Placement, Side and the axis helper — crates/base/src/geometry.rs
 *
 * The small geometry vocabulary the base layer adds to gpui's own. Rust
 * writes `AxisExt` and `LengthExt` as traits on `gpui::Axis` and
 * `gpui::Length`, which is the only way to hang a method on another
 * crate's type; here they are free functions on the same enums. gpui's
 * `Point`, `Size`, `Bounds` and `Edges` are in `gpui/gpui.h`, where the
 * crate that owns them is. `LengthExt::to_pixels` has no counterpart: a
 * length here is resolved by taffy, not by the caller. */

#include "gpui/gpui.h"

namespace gpui {

// The side of the trigger the popup is placed on.
enum class Placement : uint8_t {
    Top,
    Bottom,
    Left,
    Right
};

inline bool SideIsLeft(Side s) {
    return s == Side::Left;
}

inline bool SideIsRight(Side s) {
    return s == Side::Right;
}

inline bool AxisIsHorizontal(Axis a) {
    return a == Axis::Horizontal;
}

inline bool AxisIsVertical(Axis a) {
    return a == Axis::Vertical;
}

inline bool PlacementIsHorizontal(Placement p) {
    return p == Placement::Left || p == Placement::Right;
}

inline bool PlacementIsVertical(Placement p) {
    return p == Placement::Top || p == Placement::Bottom;
}

inline Axis PlacementAxis(Placement p) {
    return PlacementIsHorizontal(p) ? Axis::Horizontal : Axis::Vertical;
}

// Edges<T>::all. The runtime's Edges is the one retained DIP instantiation
// of Rust's serializable generic wrapper.
inline Edges EdgesAll(float value) {
    return Edges::New(value, value, value, value);
}

} // namespace gpui
#endif // GPUI_BASE_GEOMETRY_H_
