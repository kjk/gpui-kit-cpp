#ifndef GPUI_UI_COLOR_PICKER_H_
#define GPUI_UI_COLOR_PICKER_H_
/* Themed color picker — crates/ui/src/color_picker.rs */

#include "ui/sizing.h"

namespace gpui {

namespace component {

// Compatibility state lookup for the id-based constructor. The source-shaped
// overload below takes the application-owned Entity<ColorPickerState>
// directly.
Entity<ColorPickerState> ColorPickerStateFor(Ctx* cx, Str id);

struct ColorPicker {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Str id = {};
    Str label = {};
    // The announced name, when the visible label is not it.
    Str accessibilityLabel = {};
    // icon(): the trigger is that icon rather than a square of the value.
    IconName icon = IconName::None;
    UiSize size = UiSize::Medium;
    // featured_colors(): the top row of the palette panel. Null is the
    // theme's own twelve — the six base hues and their light halves.
    const uint32_t* featured = nullptr;
    int nFeatured = 0;
    // Compatibility callback for the id-based surface. Retained callers
    // subscribe to ColorPickerEvent on `state`.
    Listener onChange;
    Entity<ColorPickerState> state = {};
    // Draws the trigger as a framed field, see ColorSelect.
    bool field = false;
    Str placeholder = {};
    // Styled: the refinements land on the picker's root.
    Style style = {};
    uint32_t styleSet = 0;

    static ColorPicker* New(Ctx* cx, Str id);
    static ColorPicker* New(Ctx* cx, Entity<ColorPickerState> state);
    ColorPicker* Label(Str s);
    // Set the name a screen reader announces, when the visible label is not
    // it. A color picker's name comes from its Label by default; setting
    // this replaces the announced name without changing the visible label.
    ColorPicker* AccessibilityLabel(Str s);
    ColorPicker* Icon(IconName v);
    ColorPicker* WithSize(UiSize s);
    ColorPicker* FeaturedColors(const uint32_t* colors, int n);
    ColorPicker* OnChange(Listener fn);
    ColorPicker* Refine(const Style& s, uint32_t fields);
    // Focusable: the state's own handle.
    FocusHandle FocusHandleOf(Ctx* cx) const;
    El* IntoEl();
};

// A color picker drawn as a framed field, like a Select.
//
// The field shows a swatch of the current color and its hex value; clicking
// anywhere on it opens the same popover as ColorPicker. Use it in forms,
// where a control is expected to share the height and frame of the inputs
// around it; use ColorPicker for a compact swatch in a toolbar.
struct ColorSelect {
    ColorPicker* picker = nullptr;

    static ColorSelect* New(Ctx* cx, Entity<ColorPickerState> state);
    // Set the featured colors shown at the top of the palette.
    ColorSelect* FeaturedColors(const uint32_t* colors, int n);
    // Set the text shown while no color is selected. Default is the same
    // placeholder as Select.
    ColorSelect* Placeholder(Str s);
    // Set the name a screen reader announces.
    ColorSelect* AccessibilityLabel(Str s);
    ColorSelect* WithSize(UiSize s);
    ColorSelect* Refine(const Style& s, uint32_t fields);
    FocusHandle FocusHandleOf(Ctx* cx) const;
    El* IntoEl();
};

} // namespace component
} // namespace gpui
#endif // GPUI_UI_COLOR_PICKER_H_
