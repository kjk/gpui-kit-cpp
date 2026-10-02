#include "ui/i18n.h"
#include "ui/color_picker.h"
#include "ui/theme.h"
#include "ui/select.h"

namespace gpui {

namespace component {

Entity<ColorPickerState> ColorPickerStateFor(Ctx* cx, Str id) {
    Entity<ColorPickerState> state =
        KeyedEntity<ColorPickerState>(cx, KeyedName(cx, id));
    if (ColorPickerState* s = state.Get(cx)) {
        if (!s->self.IsValid()) {
            s->self = state;
        }
        ColorPickerStateInit(s, cx);
    }
    return state;
}

ColorPicker* ColorPicker::New(Ctx* cx, Str id) {
    Arena* a = cx->a;
    ColorPicker* c = ArenaNew<ColorPicker>(a);
    c->a = a;
    c->cx = cx;
    c->id = id;
    return c;
}
ColorPicker* ColorPicker::New(Ctx* cx, Entity<ColorPickerState> state) {
    ColorPicker* c = New(cx, StrDup(cx->a, fmt("color-picker-%d-%u",
                                               state.id.index, state.id.gen)));
    c->state = state;
    return c;
}
ColorPicker* ColorPicker::Label(Str s) {
    label = s;
    return this;
}
ColorPicker* ColorPicker::AccessibilityLabel(Str s) {
    accessibilityLabel = s;
    return this;
}
ColorPicker* ColorPicker::Icon(IconName v) {
    icon = v;
    return this;
}
ColorPicker* ColorPicker::WithSize(UiSize s) {
    size = s;
    return this;
}
ColorPicker* ColorPicker::FeaturedColors(const uint32_t* colors, int n) {
    featured = colors;
    nFeatured = n;
    return this;
}
ColorPicker* ColorPicker::OnChange(Listener fn) {
    onChange = fn;
    return this;
}
ColorPicker* ColorPicker::Refine(const Style& s, uint32_t fields) {
    StyleApplyFields(&style, s, fields);
    styleSet |= fields;
    return this;
}
FocusHandle ColorPicker::FocusHandleOf(Ctx* ctx) const {
    Entity<ColorPickerState> st =
        state.IsValid() ? state : ColorPickerStateFor(ctx, id);
    ColorPickerState* s = st.Get(ctx);
    return s ? s->focus : FocusHandle{};
}

ColorSelect* ColorSelect::New(Ctx* cx, Entity<ColorPickerState> state) {
    ColorSelect* c = ArenaNew<ColorSelect>(cx->a);
    c->picker = ColorPicker::New(cx, state);
    // ("color-select", state.entity_id())
    c->picker->id =
        StrDup(cx->a, fmt("color-select-%d-%u", state.id.index, state.id.gen));
    c->picker->field = true;
    return c;
}
ColorSelect* ColorSelect::FeaturedColors(const uint32_t* colors, int n) {
    picker->FeaturedColors(colors, n);
    return this;
}
ColorSelect* ColorSelect::Placeholder(Str s) {
    picker->placeholder = s;
    return this;
}
ColorSelect* ColorSelect::AccessibilityLabel(Str s) {
    picker->AccessibilityLabel(s);
    return this;
}
ColorSelect* ColorSelect::WithSize(UiSize s) {
    picker->WithSize(s);
    return this;
}
ColorSelect* ColorSelect::Refine(const Style& s, uint32_t fields) {
    picker->Refine(s, fields);
    return this;
}
FocusHandle ColorSelect::FocusHandleOf(Ctx* cx) const {
    return picker->FocusHandleOf(cx);
}
El* ColorSelect::IntoEl() {
    return picker->IntoEl();
}

// ─── the palette ──────────────────────────────────────────────────────────

// color_palettes(): nine hues out of DEFAULT_COLORS, each of its eleven
// scales. `stone` is the only one no ColorName can reach, which is why it has
// a table of its own in theme_data.cpp.
static const char* const kPaletteHues[] = {"red",  "orange", "yellow", "green",
                                           "cyan", "blue",   "purple", "pink"};
static const int kNumPaletteHues = 8; // plus stone, which leads the list

static const uint32_t* PaletteRow(int row) {
    if (row == 0) {
        return kShadcnStone;
    }
    const char* want = kPaletteHues[row - 1];
    for (int i = 0; i < kNumShadcnScales; i++) {
        if (base::StrEqI(Str(kShadcnScales[i].name), want)) {
            return kShadcnScales[i].hex;
        }
    }
    return kShadcnStone;
}

static uint32_t HexOf(Rgba c) {
    return ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | (uint32_t)c.b;
}

// render_item: a 20x20 swatch that previews on hover and commits on click,
// bordered in its own colour darkened a tenth.
static El* Swatch(Ctx* cx, Entity<ColorPickerState> st, Str id, uint32_t hex) {
    Rgba c = RgbaHex(hex);
    ColorPickerState* state = st.Get(cx);
    bool selected = state && state->hasValue &&
                    (state->value & 0xffffffu) == (hex & 0xffffffu);
    return ColorSwatch::New(cx, id,
                            ListenTo(st, &ColorPickerState::OnSwatchClick, hex),
                            ListenTo(st, &ColorPickerState::OnSwatchHover, hex),
                            hex, selected)
        ->W(20)
        ->H(20)
        ->Shrink0()
        ->Bg(c)
        ->Border(1, RgbaDarken(c, 0.1f));
}

static El* PalettePanel(Ctx* cx, Entity<ColorPickerState> st,
                        const uint32_t* featured, int nFeatured) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    // The twelve the theme names, when the caller did not bring its own.
    uint32_t owned[12] = {
        HexOf(th.red),       HexOf(th.redLight),    HexOf(th.blue),
        HexOf(th.blueLight), HexOf(th.green),       HexOf(th.greenLight),
        HexOf(th.yellow),    HexOf(th.yellowLight), HexOf(th.cyan),
        HexOf(th.cyanLight), HexOf(th.magenta),     HexOf(th.magentaLight)};
    const uint32_t* row = featured ? featured : owned;
    int n = featured ? nFeatured : 12;

    El* panel = Div(a)->FlexCol()->Gap(12);
    El* top = Div(a)->FlexRow()->Gap(4);
    for (int i = 0; i < n; i++) {
        top->Child(
            Swatch(cx, st, StrDup(a, fmt("cp-f%d", i)), row[i] & 0xffffffu));
    }
    panel->Child(top);
    panel->Child(Separator::Horizontal(cx)->IntoEl());
    El* grid = Div(a)->FlexCol()->Gap(4);
    for (int r = 0; r <= kNumPaletteHues; r++) {
        const uint32_t* scale = PaletteRow(r);
        El* line = Div(a)->FlexRow()->Gap(4);
        // `.rev()`: the darkest end leads each row.
        for (int i = kNumShadcnColumns - 1; i >= 0; i--) {
            line->Child(Swatch(cx, st, StrDup(a, fmt("cp-%d-%d", r, i)),
                               scale[i] & 0xffffffu));
        }
        grid->Child(line);
    }
    panel->Child(grid);
    return panel;
}

// ─── the HSLA panel ───────────────────────────────────────────────────────

// render_slider_track: the gradient the thumb runs over, as 96 stripes.
// h_2_5 is ten pixels, and the strip is absolute so the slider lies on it.
static El* StripeTrack(Ctx* cx, uint32_t (*at)(float t, float h), float hue) {
    Arena* a = cx->a;
    const int kSteps = 96;
    El* strip = Div(a)
                    ->FlexRow()
                    ->Absolute()
                    ->Left(0)
                    ->Right(0)
                    ->H(10)
                    ->ClipX()
                    ->ClipY();
    for (int i = 0; i < kSteps; i++) {
        float t = (float)i / (float)(kSteps - 1);
        strip->Child(Div(a)->Flex1()->H(kFill)->Bg(RgbaHex(at(t, hue))));
    }
    return strip;
}

// render_slider_track_gradient: the same strip as one linear gradient, which
// is what the saturation and alpha tracks are.
static El* GradientTrack(Ctx* cx, Rgba from, Rgba to) {
    Arena* a = cx->a;
    return Div(a)->Absolute()->Left(0)->Right(0)->H(10)->ClipX()->ClipY()->Bg(
        BackgroundLinear(90.f, ColorStopAt(from, 0.f), ColorStopAt(to, 1.f)));
}

static uint32_t HueAt(float t, float) {
    return HexOf(RgbaHsla(t, 1.f, 0.5f, 1.f));
}
static uint32_t LightnessAt(float t, float h) {
    return HexOf(RgbaHsla(h, 1.f, t, 1.f));
}

static El* SliderRow(Ctx* cx, Entity<ColorPickerState> st, Str label, El* track,
                     int slot, Str value) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    Rgba labelColor = RgbaOpacity(th.foreground, 0.7f);
    ColorPickerState* s = st.Get(cx);
    El* row = Div(a)->FlexRow()->Gap(8)->ItemsCenter();
    row->Child(
        Div(a)->MinW(64)->Child(TextEl(a, label)->Font(12)->Fg(labelColor)));
    // div().relative().flex().items_center().flex_1().h_8(): the track lies
    // under the slider, and the slider is what takes the pointer.
    // div().relative(): taffy lays an absolute child out against its parent
    // box, so the strip under the slider needs nothing said here.
    El* mid = Div(a)->FlexRow()->ItemsCenter()->Flex1()->H(32);
    mid->Child(track);
    mid->Child(Slider::New(cx, StrDup(a, fmt("cp-sl%d", slot)),
                           s ? s->sliders.At(slot) : nullptr)
                   ->WFill()
                   ->Bg(Rgba8(0, 0, 0, 0))
                   ->OnChange(ListenTo(st, &ColorPickerState::OnSlider))
                   ->IntoEl());
    row->Child(mid);
    // text_align(Right) in a w_10 box, which a row that justifies to its end
    // comes to.
    row->Child(Div(a)->FlexRow()->W(40)->JustifyEnd()->Child(
        TextEl(a, value)->Font(12)->Fg(labelColor)));
    return row;
}

static El* SliderPanel(Ctx* cx, Entity<ColorPickerState> st) {
    Arena* a = cx->a;
    ColorPickerState* s = st.Get(cx);
    if (!s) {
        return Div(a);
    }
    float h = s->sliders.hue.value.End();
    float sat = s->sliders.saturation.value.End();
    float l = s->sliders.lightness.value.End();
    float alpha = s->sliders.alpha.value.End();

    El* panel = Div(a)->FlexCol()->Gap(8);
    panel->Child(SliderRow(cx, st, Tr("ColorPicker.Hue"),
                           StripeTrack(cx, HueAt, h), 0,
                           StrDup(a, fmt("%.0f", (double)(h * 360.f)))));
    panel->Child(SliderRow(
        cx, st, Tr("ColorPicker.Saturation"),
        GradientTrack(cx, RgbaHsla(h, 0.f, l, 1.f), RgbaHsla(h, 1.f, l, 1.f)),
        1, StrDup(a, fmt("%.0f", (double)(sat * 100.f)))));
    panel->Child(SliderRow(cx, st, Tr("ColorPicker.Lightness"),
                           StripeTrack(cx, LightnessAt, h), 2,
                           StrDup(a, fmt("%.0f", (double)(l * 100.f)))));
    panel->Child(
        SliderRow(cx, st, Tr("ColorPicker.Alpha"),
                  GradientTrack(cx, RgbaOpacity(RgbaHsla(h, sat, l, 1.f), 0.f),
                                RgbaHsla(h, sat, l, 1.f)),
                  3, StrDup(a, fmt("%.0f", (double)(alpha * 100.f)))));
    return panel;
}

// ColorPickerButton::render_field: the framed trigger of a ColorSelect — the
// swatch, the hex value and a caret, laid out like a Select trigger. Rust
// reads the committed value here, not the preview.
static El* ColorFieldTrigger(Ctx* cx, const ColorPickerState* s, UiSize size,
                             Str placeholder, bool outlineVisible) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    // input_style(false, cx)
    El* field = Div(a)
                    ->FlexRow()
                    ->ItemsCenter()
                    ->W(kFill)
                    ->Gap(8)
                    ->Bg(th.inputBg)
                    ->Fg(th.foreground)
                    ->Radius(th.radius)
                    ->Border(1, outlineVisible ? th.ring : th.inputBorder);
    UiInputTextSize(UiInputSize(cx, field, size), size);
    float sw = 16;
    if (size == UiSize::XSmall) {
        sw = 12;
    } else if (size == UiSize::Small) {
        sw = 14;
    } else if (size == UiSize::Large) {
        sw = 20;
    }
    El* swatch = Div(a)->W(sw)->H(sw)->Shrink0()->Radius(th.radius / 2.f);
    if (s->hasValue) {
        Rgba value = RgbaHex(s->value);
        swatch->Bg(value)->Border(1, RgbaDarken(value, 0.3f));
    } else {
        swatch->Border(1, th.inputBorder);
    }
    field->Child(swatch);
    // flex_1().min_w_0().overflow_hidden().whitespace_nowrap(); a TextEl
    // does not wrap unless asked to.
    El* text = Div(a)->Flex1()->MinW(0)->ClipX();
    float font = UiInputFontPx(size);
    if (s->hasValue) {
        text->Child(TextEl(a, RgbaToHex(a, RgbaHex(s->value)))
                        ->Font(font)
                        ->Fg(th.foreground));
    } else {
        text->Child(
            TextEl(a, placeholder.s ? placeholder : Tr("Select.placeholder"))
                ->Font(font)
                ->Fg(th.mutedFg));
    }
    field->Child(text);
    field->Child(Caret::New(size).TextColor(th.mutedFg).IntoEl(a));
    return field;
}

// ─── the whole thing ──────────────────────────────────────────────────────

El* ColorPicker::IntoEl() {
    const Theme& th = ThemeNow(cx->app);
    Entity<ColorPickerState> st =
        state.IsValid() ? state : ColorPickerStateFor(cx, id);
    ColorPickerState* s = st.Get(cx);
    if (!s) {
        return Div(a);
    }
    s->onChange = onChange;
    // sync_pending_value: a value handed in before the first render reaches
    // the sliders and the field here.
    ColorPickerSyncPending(s);

    uint32_t shownHex = 0;
    bool hasShown = ColorPickerShown(s, &shownHex);
    Rgba shown = RgbaHex(shownHex);

    // ColorPickerButton: the icon if there is one, else a square of the value
    // sized with the picker.
    float sq = 32;
    if (size == UiSize::Large) {
        sq = 44;
    } else if (size == UiSize::Small) {
        sq = 20;
    } else if (size == UiSize::XSmall) {
        sq = 16;
    }
    bool focused =
        s->focus.IsValid() && FocusHandleIsFocused(cx->win, s->focus);
    El* trigger = nullptr;
    if (field) {
        trigger = ColorFieldTrigger(cx, s, size, placeholder, focused);
    } else {
        trigger = Div(a)->FlexRow()->Gap(8)->ItemsCenter();
    }
    if (field) {
        // The field is the whole trigger.
    } else if (icon != IconName::None) {
        trigger->Child(IconEl(a, icon, UiIconPx(cx, size)));
    } else {
        trigger->Child(Div(a)
                           ->W(sq)
                           ->H(sq)
                           ->Radius(th.radius)
                           ->ClipX()
                           ->ClipY()
                           ->Bg(hasShown ? shown : th.tokens.background)
                           // darken(0.3) on the value, the input border when
                           // empty.
                           ->Border(1, hasShown ? RgbaDarken(shown, 0.3f)
                                                : th.inputBorder));
    }
    if (label.s && !field) {
        trigger->Child(TextEl(a, label)->Font(16)->Fg(th.foreground));
    }
    BindClick(trigger, StrL("trigger"),
              ListenTo(st, &ColorPickerState::OnToggleOpen));

    El* pop = nullptr;
    if (s->open) {
        // Popover::w_72() over v_flex().p_0p5().gap_3().
        pop = Div(a)
                  ->FlexCol()
                  ->W(288)
                  ->Gap(12)
                  ->Pad(2)
                  ->Radius(th.radiusLg)
                  ->Border(1, th.border)
                  ->Bg(th.tokens.background);
        // `TabBar::new("mode").segmented()` names no size, so it is the
        // default one; the port asked for Small, which is a shorter strip
        // with a smaller label than Rust draws.
        pop->Child(Tabs::New(cx, StrL("mode"))
                       ->Segmented()
                       ->Tab(Tr("ColorPicker.Palette"))
                       ->Flex1()
                       ->Tab(Tr("ColorPicker.HSLA"))
                       ->Flex1()
                       ->Selected(s->activeTab)
                       ->OnChange(ListenTo(st, &ColorPickerState::OnTab))
                       ->IntoEl());
        pop->Child(s->activeTab == 0 ? PalettePanel(cx, st, featured, nFeatured)
                                     : SliderPanel(cx, st));
        // The hovered colour, with the hex field beside it. Rust shows the
        // row only while there is a preview to name.
        if (s->hasPreview) {
            Rgba hovered = RgbaHex(s->preview);
            pop->Child(Separator::Horizontal(cx)->IntoEl());
            El* row = Div(a)->FlexRow()->Gap(8)->ItemsCenter();
            row->Child(Div(a)
                           ->W(20)
                           ->H(20)
                           ->Shrink0()
                           ->Radius(th.radius)
                           ->Bg(hovered)
                           ->Border(1, RgbaDarken(hovered, 0.2f)));
            s->hexInput.onChange = ListenTo(st, &ColorPickerState::OnHexChange);
            row->Child(
                Input::New(cx, StrL("hex"), &s->hexInput)
                    ->WithSize(UiSize::Small)
                    ->OnFocus(ListenTo(st, &ColorPickerState::OnHexFocus))
                    ->IntoEl());
            pop->Child(row);
        }
        if (s->hexInput.focused) {
            cx->win->input = &s->hexInput;
        }
    }
    // `BaseColorPicker::new(id).child(Popover::new("popover").trigger(..))`:
    // the picker is the outer element and the popover is inside it, holding
    // the trigger and the panel. The port had the two the other way up, which
    // is what left the popover with nothing named above it.
    // The explicit name wins over the visible label.
    Str name = accessibilityLabel.s ? accessibilityLabel : label;
    El* popover =
        Popup::New(cx, StrL("popover"), trigger)->Content(pop)->IntoEl();
    El* root = gpui::ColorPicker::New(
                   cx, id, s->open, false, name, AccessibilityRole::Button,
                   ListenTo(st, &ColorPickerState::OnOpenChange), s->focus)
                   ->Child(popover);
    if (field) {
        // .when(self.field, w_full) on the picker and trigger_style(w_full)
        // on the popover, so a click anywhere on the field opens it.
        root->W(kFill);
        popover->W(kFill);
        // focus_ring_style on the field. The focus handle is tracked by the
        // picker's root, whose bounds are the field's, so the outer ring is
        // drawn there with the field's radius; the field takes the ring
        // border itself above.
        root->Radius(th.radius)->FocusRing(true);
    }
    if (styleSet) {
        root->Refine(style, styleSet);
    }
    return root;
}

} // namespace component
} // namespace gpui
