#include "base/color_picker.h"
#include "base/actions.h"
#include "gpui/keymap.h"

namespace gpui {

HslaSliders::HslaSliders() {
    hue = SliderStateNew(0.f, 1.f, SliderSingle(0.f), 0.01f);
    saturation = SliderStateNew(0.f, 1.f, SliderSingle(0.f), 0.01f);
    lightness = SliderStateNew(0.f, 1.f, SliderSingle(0.f), 0.01f);
    alpha = SliderStateNew(0.f, 1.f, SliderSingle(0.f), 0.01f);
}

SliderState* HslaSliders::At(int index) {
    switch (index) {
        case 0:
            return &hue;
        case 1:
            return &saturation;
        case 2:
            return &lightness;
        case 3:
            return &alpha;
        default:
            break;
    }
    return nullptr;
}

const SliderState* HslaSliders::At(int index) const {
    return const_cast<HslaSliders*>(this)->At(index);
}

Hsla HslaSliders::Read() const {
    return HslaNew(hue.value.End(), saturation.value.End(),
                   lightness.value.End(), alpha.value.End());
}

void HslaSliders::Write(Hsla color) {
    SliderSetValue(&hue, SliderSingle(color.h));
    SliderSetValue(&saturation, SliderSingle(color.s));
    SliderSetValue(&lightness, SliderSingle(color.l));
    SliderSetValue(&alpha, SliderSingle(color.a));
}

ColorPickerState::ColorPickerState() = default;

void ColorPickerStateInit(ColorPickerState* s, Ctx* cx) {
    if (s && cx && !s->focus.IsValid()) {
        s->focus = FocusHandleNew(cx);
    }
}

Entity<ColorPickerState> ColorPickerStateNew(Ctx* cx) {
    Entity<ColorPickerState> state = EntityNewState<ColorPickerState>(cx->app);
    if (ColorPickerState* s = state.Get(cx)) {
        s->self = state;
        ColorPickerStateInit(s, cx);
    }
    return state;
}

bool ColorPickerShown(const ColorPickerState* s, uint32_t* out) {
    if (s->hasPreview) {
        *out = s->preview;
        return true;
    }
    if (s->hasValue) {
        *out = s->value;
        return true;
    }
    return false;
}

// hex_string: eight digits only when the colour is translucent, which is what
// `formats_alpha_only_when_translucent` pins. The packed value is 0xAARRGGBB
// — `RgbaHex`'s convention — and the text is #RRGGBBAA.
Str ColorPickerHexString(Arena* a, uint32_t color) {
    // Through `RgbaToHex`, which is `Colorize::to_hex`: the digits are the
    // ones Rust prints for the same colour, an Hsla round trip and all.
    return RgbaToHex(a, RgbaHex(color));
}

// write_hex_input: the field always shows what the picker is showing, unless
// the field is what is being typed into.
static void WriteHexInput(ColorPickerState* s, uint32_t color, bool has) {
    if (s->hexInput.focused) {
        return;
    }
    if (!has) {
        InputSetValue(&s->hexInput, Str{});
        return;
    }
    InputSetValue(&s->hexInput, ColorPickerHexString(GetTempArena(), color));
}

// HslaSliders::write: the four take the color's components straight, rather
// than letting a hex round-trip round them.
static void WriteSliders(ColorPickerState* s, uint32_t color) {
    s->sliders.Write(HslaFromRgba(RgbaHex(color)));
}

void ColorPickerPreview(ColorPickerState* s, uint32_t color) {
    s->preview = color;
    s->hasPreview = true;
    WriteHexInput(s, color, true);
}

bool ColorPickerClearPreview(ColorPickerState* s) {
    // Rust returns early when the preview already equals the value: there is
    // nothing to restore, so nothing to repaint. `update_value` leaves the
    // preview *at* the value rather than dropping it, so a picker with a
    // colour always has one to name — which is what keeps the hex row up.
    if (s->hasPreview == s->hasValue && s->preview == s->value) {
        return false;
    }
    s->preview = s->value;
    s->hasPreview = s->hasValue;
    WriteHexInput(s, s->value, s->hasValue);
    return true;
}

// update_value: the committed color, the preview that follows it, the field
// and the sliders, in one place.
static void UpdateValue(ColorPickerState* s, uint32_t color, bool has) {
    s->needsSliderSync = false;
    s->value = color;
    s->hasValue = has;
    s->preview = color;
    s->hasPreview = has;
    WriteHexInput(s, color, has);
    if (has) {
        WriteSliders(s, color);
    }
}

void ColorPickerSetValue(ColorPickerState* s, uint32_t color) {
    UpdateValue(s, color, true);
}

void ColorPickerClearValue(ColorPickerState* s) {
    UpdateValue(s, 0, false);
}

void ColorPickerSelect(ColorPickerState* s, uint32_t color) {
    s->open = false;
    UpdateValue(s, color, true);
}

void ColorPickerUpdateColor(ColorPickerState* s, uint32_t color) {
    UpdateValue(s, color, true);
}

void ColorPickerSetOpen(ColorPickerState* s, bool open) {
    s->open = open;
}

void ColorPickerSetActiveTab(ColorPickerState* s, int tab) {
    s->activeTab = tab;
}

void ColorPickerSyncPending(ColorPickerState* s) {
    if (!s->needsSliderSync) {
        return;
    }
    UpdateValue(s, s->value, s->hasValue);
}

uint32_t ColorPickerSliderColor(const ColorPickerState* s) {
    Rgba c = HslaToRgba(s->sliders.Read());
    uint32_t rgb = ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | (uint32_t)c.b;
    // An opaque colour packs as 0xRRGGBB, so it reads the same as every hex
    // in the palette; a translucent one carries its alpha in the top byte,
    // which is what `RgbaHex` decodes.
    return c.a == 255 ? rgb : ((uint32_t)c.a << 24) | rgb;
}

static int HexDigit(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

bool ColorPickerParseHex(Str text, uint32_t* out) {
    if (!text.s) {
        return false;
    }
    const char* p = text.s;
    int n = len(text);
    while (n > 0 && (*p == ' ' || *p == '\t')) {
        p++;
        n--;
    }
    while (n > 0 && (p[n - 1] == ' ' || p[n - 1] == '\t')) {
        n--;
    }
    if (n > 0 && *p == '#') {
        p++;
        n--;
    }
    // parse_hex takes 3, 4, 6 and 8 digits, the wider two carrying an alpha.
    if (n != 3 && n != 4 && n != 6 && n != 8) {
        return false;
    }
    int v[8] = {};
    for (int i = 0; i < n; i++) {
        v[i] = HexDigit(p[i]);
        if (v[i] < 0) {
            return false;
        }
    }
    uint32_t r, g, b, alpha = 255;
    if (n == 3 || n == 4) {
        r = (uint32_t)(v[0] * 17);
        g = (uint32_t)(v[1] * 17);
        b = (uint32_t)(v[2] * 17);
        if (n == 4) {
            alpha = (uint32_t)(v[3] * 17);
        }
    } else {
        r = (uint32_t)(v[0] * 16 + v[1]);
        g = (uint32_t)(v[2] * 16 + v[3]);
        b = (uint32_t)(v[4] * 16 + v[5]);
        if (n == 8) {
            alpha = (uint32_t)(v[6] * 16 + v[7]);
        }
    }
    uint32_t rgb = (r << 16) | (g << 8) | b;
    *out = alpha == 255 ? rgb : (alpha << 24) | rgb;
    return true;
}

// ─── the handlers the themed picker binds ─────────────────────────────────

// cx.emit(ColorPickerEvent::Change(value)), as a listener the view supplied.
static void EmitChange(ColorPickerState* s, Ctx* cx, Hsla color,
                       bool hasColor = true) {
    ColorPickerEvent event;
    event.hasColor = hasColor;
    event.color = color;
    if (s->self.IsValid()) {
        EntityEmit(cx->app, cx->win, s->self, &event);
    }
    // Compatibility for the first themed surface. New retained callers
    // subscribe to the state and receive the typed event above.
    if (s->onChange.IsValid()) {
        ClickEvent ev = {};
        ListenerCall(cx->app, cx->win,
                     ListenerFill(s->onChange, (intptr_t)s->value), &ev);
    }
}

void ColorPickerState::OnToggleOpen(ColorPickerState* s, Ctx* cx,
                                    const ClickEvent*) {
    s->open = !s->open;
    if (!s->open) {
        s->hexInput.focused = false;
    }
    Notify(cx);
}

void ColorPickerState::OnOpenChange(ColorPickerState* s, Ctx* cx,
                                    const ClickEvent*, intptr_t open) {
    bool next = open != 0;
    if (s->open == next) {
        return;
    }
    s->open = next;
    if (!next) {
        s->hexInput.focused = false;
    }
    Notify(cx);
}

void ColorPickerState::OnTab(ColorPickerState* s, Ctx* cx, const ClickEvent*,
                             intptr_t ix) {
    if (s->activeTab == (int)ix) {
        return;
    }
    s->activeTab = (int)ix;
    Notify(cx);
}

void ColorPickerState::OnSwatchClick(ColorPickerState* s, Ctx* cx,
                                     const ClickEvent*, intptr_t hex) {
    ColorPickerSelect(s, (uint32_t)hex);
    EmitChange(s, cx, HslaFromRgba(RgbaHex((uint32_t)hex)));
    Notify(cx);
}

void ColorPickerState::OnSwatchHover(ColorPickerState* s, Ctx* cx,
                                     const HoverEvent* ev, intptr_t hex) {
    if (ev->hovered) {
        ColorPickerPreview(s, (uint32_t)hex);
    } else if (!ColorPickerClearPreview(s)) {
        return;
    }
    Notify(cx);
}

void ColorPickerState::OnSlider(ColorPickerState* s, Ctx* cx,
                                const SliderEvent*) {
    // update_value_from_slider: the sliders are the source, so the value they
    // describe is committed without re-seeding them from it.
    Hsla hsla = s->sliders.Read();
    uint32_t color = ColorPickerSliderColor(s);
    s->needsSliderSync = false;
    s->value = color;
    s->hasValue = true;
    s->preview = color;
    s->hasPreview = true;
    WriteHexInput(s, color, true);
    EmitChange(s, cx, hsla);
    Notify(cx);
}

void ColorPickerState::OnHexChange(ColorPickerState* s, Ctx* cx,
                                   const InputEvent* ev) {
    Str text = InputValue(&s->hexInput);
    uint32_t color = 0;
    bool ok = ColorPickerParseHex(text, &color);
    if (ev && ev->kind == InputEventKind::PressEnter) {
        // commit_hex: a colour closes the picker, anything else stands.
        if (ok) {
            ColorPickerSelect(s, color);
            EmitChange(s, cx, HslaFromRgba(RgbaHex(color)));
        }
        Notify(cx);
        return;
    }
    // preview_hex: an incomplete value leaves the preview where it was, so
    // the panel does not flicker while the field is being typed into.
    if (ok) {
        s->preview = color;
        s->hasPreview = true;
        WriteSliders(s, color);
        Notify(cx);
    }
}

void ColorPickerState::OnHexFocus(ColorPickerState* s, Ctx* cx,
                                  const ClickEvent*) {
    s->hexInput.focused = true;
    Notify(cx);
}

struct ColorPickerKeys {
    bool open = false;
    bool disabled = false;
    Listener onOpenChange = {};

    static void OnAction(ColorPickerKeys* self, Ctx* cx,
                         const ActionEvent* ev) {
        bool handled = false;
        bool next = self->open;
        if (ev->action == action::Confirm()) {
            if (!self->disabled) {
                next = !self->open;
                handled = true;
            }
        } else if (ev->action == action::Cancel() && self->open) {
            next = false;
            handled = true;
        }
        if (!handled) {
            const_cast<ActionEvent*>(ev)->propagate = true;
            return;
        }
        if (self->onOpenChange.IsValid()) {
            ClickEvent click = {};
            ListenerCall(cx->app, cx->win,
                         ListenerFill(self->onOpenChange, next), &click);
        }
    }
};

struct ColorPickerBoundKeys {
    uint32_t context = 0;
    uint32_t generation = 0;
};

static Vec<ColorPickerBoundKeys> gColorPickerBoundKeys;

static void ColorPickerInitKeys(const char* context) {
    uint32_t id = KeyContextOf(Str(context));
    uint32_t generation = KeymapGeneration();
    for (int i = 0; i < len(gColorPickerBoundKeys); i++) {
        if (gColorPickerBoundKeys[i].context != id) {
            continue;
        }
        if (gColorPickerBoundKeys[i].generation == generation) {
            return;
        }
        gColorPickerBoundKeys[i].generation = generation;
        KeyBinding bindings[] = {{"enter", action::Confirm(), context},
                                 {"escape", action::Cancel(), context}};
        KeymapBind(bindings, 2);
        return;
    }
    VecAppend(gColorPickerBoundKeys, {id, generation});
    KeyBinding bindings[] = {{"enter", action::Confirm(), context},
                             {"escape", action::Cancel(), context}};
    KeymapBind(bindings, 2);
}

El* ColorPicker::New(Ctx* cx, Str id, bool open, bool disabled,
                     Str accessibilityLabel, AccessibilityRole role,
                     Listener onOpenChange, FocusHandle focus, int tabIndex,
                     bool tabStop, const char* keyContext) {
    Arena* a = cx->a;
    El* e =
        Div(a)->Id(id)->Role(role)->AriaExpanded(open)->AriaDisabled(disabled);
    if (accessibilityLabel.s) {
        e->AriaLabel(accessibilityLabel);
    }
    if (!disabled && focus.IsValid()) {
        e->TrackFocus(focus)->TabIndex(tabIndex)->TabStop(tabStop);
    }
    if (!disabled && onOpenChange.IsValid()) {
        e->OnAccessibilityDefault(ListenerFill(onOpenChange, !open));
    }
    if (keyContext && *keyContext) {
        ColorPickerInitKeys(keyContext);
        Entity<ColorPickerKeys> keys = ElementStateEntity<ColorPickerKeys>(
            cx, id, StrL("gpui::ColorPickerKeys"));
        if (ColorPickerKeys* state = keys.Get(cx)) {
            state->open = open;
            state->disabled = disabled;
            state->onOpenChange = onOpenChange;
        }
        e->KeyContext(Str(keyContext))
            ->OnAction(action::Confirm(),
                       ListenTo(keys, &ColorPickerKeys::OnAction))
            ->OnAction(action::Cancel(),
                       ListenTo(keys, &ColorPickerKeys::OnAction));
    }
    return e;
}

El* ColorSwatch::New(Ctx* cx, Str id, Listener onClick, Listener onHover,
                     uint32_t color, bool selected, bool disabled,
                     Str accessibilityLabel, int tabIndex, bool tabStop,
                     AccessibilityRole role) {
    Arena* a = cx->a;
    El* e =
        Div(a)
            ->PathClick(id)
            ->Role(role)
            ->AriaLabel(accessibilityLabel.s ? accessibilityLabel
                                             : ColorPickerHexString(a, color))
            ->AriaToggled(selected ? AccessibilityToggled::True
                                   : AccessibilityToggled::False)
            ->AriaSelected(selected)
            ->AriaDisabled(disabled);
    if (!disabled) {
        e->PathId(id)->TabIndex(tabIndex)->TabStop(tabStop);
    }
    if (!disabled && onClick.IsValid()) {
        e->OnClick(onClick);
    }
    if (!disabled && onHover.IsValid()) {
        e->OnHover(onHover);
    }
    return e;
}
} // namespace gpui
