#ifndef GPUI_BASE_COLOR_PICKER_H_
#define GPUI_BASE_COLOR_PICKER_H_
/* Unstyled color picker — crates/base/src/color_picker.rs */

#include "gpui/gpui.h"

namespace gpui {

// ColorPickerEvent::Change(Option<Hsla>). An optional payload remains a POD
// pair so it can pass through EntityEmit without allocation.
enum class ColorPickerEventKind : uint8_t {
    Change
};

struct ColorPickerEvent {
    ColorPickerEventKind kind = ColorPickerEventKind::Change;
    bool hasColor = false;
    Hsla color = {};
};

// The four retained SliderState values Rust owns through entities. A C++
// Slider takes its stable state by pointer, so direct ownership gives it the
// same lifetime without four extra handles.
struct HslaSliders {
    SliderState hue = {};
    SliderState saturation = {};
    SliderState lightness = {};
    SliderState alpha = {};

    HslaSliders();
    SliderState* At(int index);
    const SliderState* At(int index) const;
    Hsla Read() const;
    void Write(Hsla color);
};

// Rust's ColorPickerState holds the committed color *and* a transient preview
// beside it, and that pair is the module's whole interaction model: hovering a
// swatch previews the color, leaving restores what was committed, and only a
// click commits. Everything a picker shows reads the preview when there is one
// and the value otherwise.
//
// The rest of the state is an open flag, an active tab, a hex field and four
// HSLA sliders. Those last two are here rather than in the themed widget for
// the reason they are in Rust: the panel is rebuilt every frame and the thumb
// the pointer is dragging has to survive it.
//
// A picker's state is an entity, the way `Entity<ColorPickerState>` is, so the
// widget can bind its own handlers to it — `component::ColorPickerStateFor`
// is what hands one out.
struct ColorPickerState {
    Entity<ColorPickerState> self = {};
    FocusHandle focus = {};
    uint32_t value = 0;
    bool hasValue = false;
    uint32_t preview = 0;
    bool hasPreview = false;
    bool open = false;
    int activeTab = 0;
    HslaSliders sliders;
    InputState hexInput;
    // needs_slider_sync: a value handed in before the first render has not
    // reached the sliders or the field yet. `ColorPickerSyncPending` is the
    // render-time flush.
    bool needsSliderSync = true;
    // The view's own listener, told the committed color as its argument.
    // Rust emits ColorPickerEvent::Change and the view subscribes.
    Listener onChange = {};

    ColorPickerState();

    // Handlers the themed picker binds to. They are here because the state is
    // the entity — a widget rebuilt every frame has nothing to bind to.
    static void OnToggleOpen(ColorPickerState* s, Ctx* cx, const ClickEvent*);
    static void OnOpenChange(ColorPickerState* s, Ctx* cx, const ClickEvent*,
                             int64_t open);
    static void OnTab(ColorPickerState* s, Ctx* cx, const ClickEvent*,
                      int64_t ix);
    static void OnSwatchClick(ColorPickerState* s, Ctx* cx, const ClickEvent*,
                              int64_t hex);
    static void OnSwatchHover(ColorPickerState* s, Ctx* cx,
                              const HoverEvent* ev, int64_t hex);
    static void OnSlider(ColorPickerState* s, Ctx* cx, const SliderEvent*);
    static void OnHexChange(ColorPickerState* s, Ctx* cx, const InputEvent* ev);
    static void OnHexFocus(ColorPickerState* s, Ctx* cx, const ClickEvent*);
};

void ColorPickerStateInit(ColorPickerState* s, Ctx* cx);
Entity<ColorPickerState> ColorPickerStateNew(Ctx* cx);

// What the picker shows: preview_color while one is up, the committed value
// otherwise. Answers false when there is neither, which is Rust's `None` and
// draws as the empty square.
bool ColorPickerShown(const ColorPickerState* s, uint32_t* out);

// preview_color / clear_preview. Rust's clear is a no-op when the preview is
// already the committed color, so a pointer crossing the swatch that is
// already picked does not repaint anything.
void ColorPickerPreview(ColorPickerState* s, uint32_t color);
bool ColorPickerClearPreview(ColorPickerState* s);

// set_value / clear_value: replace the committed color without emitting a
// change. Committing drops the preview, since what was transient is now what
// the picker holds.
void ColorPickerSetValue(ColorPickerState* s, uint32_t color);
void ColorPickerClearValue(ColorPickerState* s);

// select_color: commit and close, which is what a palette swatch does.
void ColorPickerSelect(ColorPickerState* s, uint32_t color);
// update_color: commit without closing, which is what a slider drag does.
void ColorPickerUpdateColor(ColorPickerState* s, uint32_t color);
// set_open / toggle_open / set_active_tab.
void ColorPickerSetOpen(ColorPickerState* s, bool open);
void ColorPickerSetActiveTab(ColorPickerState* s, int tab);
// sync_pending_value: push a value handed in before the first render out to
// the sliders and the hex field. Called from the widget's build; a no-op once
// nothing is pending.
void ColorPickerSyncPending(ColorPickerState* s);
// The color the four sliders currently describe.
uint32_t ColorPickerSliderColor(const ColorPickerState* s);
// "#rrggbb" / "#rgb" / "#rrggbbaa" / "#rgba" / bare hex. False when the text
// is not a colour yet, which is every prefix of one while it is being typed.
// A colour is packed 0xAARRGGBB when it is translucent and 0xRRGGBB when it
// is not, which is what `RgbaHex` reads.
bool ColorPickerParseHex(Str text, uint32_t* out);
// hex_string: eight digits only when the colour is translucent.
Str ColorPickerHexString(Arena* a, uint32_t color);

struct ColorPicker {
    static El* New(Ctx* cx, Str id, bool open = false, bool disabled = false,
                   Str accessibilityLabel = {},
                   AccessibilityRole role = AccessibilityRole::Button,
                   Listener onOpenChange = {}, FocusHandle focus = {},
                   int tabIndex = 0, bool tabStop = true,
                   const char* keyContext = "ColorPicker");
};
// A swatch previews on hover and commits on click, so it takes both.
struct ColorSwatch {
    static El* New(Ctx* cx, Str id, Listener onClick = {},
                   Listener onHover = {}, uint32_t color = 0,
                   bool selected = false, bool disabled = false,
                   Str accessibilityLabel = {}, int tabIndex = 0,
                   bool tabStop = true,
                   AccessibilityRole role = AccessibilityRole::RadioButton);
};

template <>
struct EventEmitter<ColorPickerState, ColorPickerEvent> {};
} // namespace gpui
#endif // GPUI_BASE_COLOR_PICKER_H_
