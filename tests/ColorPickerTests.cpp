/* Ported from crates/base/src/color_picker.rs.
 *
 * The committed color and the transient preview beside it are the module's
 * interaction model: a hover previews, leaving restores, and only a click
 * commits. Rust's own cases there drive a window; these are the rules the
 * state follows underneath. */

#include "Test.h"

static void APreviewHidesTheValueWithoutReplacingIt() {
    ColorPickerState s;
    ColorPickerSetValue(&s, 0x6366f0);
    uint32_t shown = 0;
    utassert(ColorPickerShown(&s, &shown) && shown == 0x6366f0);

    ColorPickerPreview(&s, 0x16a34a);
    utassert(ColorPickerShown(&s, &shown) && shown == 0x16a34a);
    // The committed color is untouched underneath.
    utassert(s.value == 0x6366f0);

    utassert(ColorPickerClearPreview(&s));
    utassert(ColorPickerShown(&s, &shown) && shown == 0x6366f0);
}

static void ClearingAPreviewOfTheCommittedColorChangesNothing() {
    ColorPickerState s;
    ColorPickerSetValue(&s, 0x6366f0);
    ColorPickerPreview(&s, 0x6366f0);
    // Rust returns early here: the pointer crossed the swatch that is already
    // picked, so there is nothing to restore and nothing to repaint.
    utassert(!ColorPickerClearPreview(&s));
    // `update_value` leaves the preview *at* the value rather than dropping
    // it, so what is displayed is still the committed colour.
    uint32_t shown = 0;
    utassert(ColorPickerShown(&s, &shown) && shown == 0x6366f0);
    // And with nothing to restore there is still nothing to do.
    utassert(!ColorPickerClearPreview(&s));
}

static void CommittingCarriesThePreviewWithIt() {
    ColorPickerState s;
    ColorPickerSetValue(&s, 0x6366f0);
    ColorPickerPreview(&s, 0x16a34a);
    ColorPickerSetValue(&s, 0x16a34a);
    uint32_t shown = 0;
    utassert(ColorPickerShown(&s, &shown) && shown == 0x16a34a);
    utassert(s.value == 0x16a34a);
}

// parses_every_supported_hex_width / rejects_malformed_hex.
static void HexParsing() {
    uint32_t c = 0;
    utassert(ColorPickerParseHex(StrL("#fff"), &c) && c == 0xffffff);
    utassert(ColorPickerParseHex(StrL("ffffff"), &c) && c == 0xffffff);
    utassert(ColorPickerParseHex(StrL("#6366F1"), &c) && c == 0x6366f1);
    // The alpha widths carry theirs in the top byte, and an opaque colour
    // packs the same as a plain six-digit one.
    utassert(ColorPickerParseHex(StrL("#ff000080"), &c) && c == 0x80ff0000);
    utassert(ColorPickerParseHex(StrL("#f008"), &c) && c == 0x88ff0000);
    utassert(ColorPickerParseHex(StrL("#ff0000ff"), &c) && c == 0xff0000);
    static const char* kBad[] = {"#nope", "#12",     "#1234567",
                                 "",      "#+f0000", "#-fffff"};
    for (int i = 0; i < 6; i++) {
        utassert(!ColorPickerParseHex(Str(kBad[i]), &c));
    }
}

// default_value_reaches_the_hex_field_and_sliders.
static void SyncPendingSeedsTheFieldAndSliders() {
    ColorPickerState s;
    s.value = 0xff0000;
    s.hasValue = true;
    ColorPickerSyncPending(&s);
    utassert(StrEqI(InputValue(&s.hexInput), "#FF0000"));
    // hsla(0, 1, 0.5): the lightness slider lands at a half.
    utassertnear(s.sliders.lightness.value.End(), 0.5f);
    // And it is a no-op the second time.
    utassert(!s.needsSliderSync);
}

// formats_alpha_only_when_translucent.
static void HexStringWidth() {
    Arena* a = ArenaNew();
    utassert(StrEqI(ColorPickerHexString(a, 0xff0000), "#FF0000"));
    utassert(StrEqI(ColorPickerHexString(a, 0x7fff0000), "#FF00007F"));
    ArenaDelete(a);
}

static void NoValueAndNoPreviewShowsNothing() {
    ColorPickerState s;
    uint32_t shown = 0xdeadbeef;
    // Rust's None, which draws as the empty square.
    utassert(!ColorPickerShown(&s, &shown));

    ColorPickerSetValue(&s, 0x123456);
    ColorPickerClearValue(&s);
    utassert(!ColorPickerShown(&s, &shown));
    // A preview still shows over no value at all.
    ColorPickerPreview(&s, 0x16a34a);
    utassert(ColorPickerShown(&s, &shown) && shown == 0x16a34a);
}

namespace {
struct ColorSink {
    int count = 0;
    int openChanges = 0;
    bool open = false;
    bool hasColor = false;
    Hsla color = {};

    static void OnChange(ColorSink* self, Ctx*, const ColorPickerEvent* ev) {
        self->count++;
        self->hasColor = ev->hasColor;
        self->color = ev->color;
    }

    static void OnOpen(ColorSink* self, Ctx*, const ClickEvent*,
                       intptr_t open) {
        self->openChanges++;
        self->open = open != 0;
    }
};
} // namespace

// ColorPickerState owns the four named slider states and emits the source's
// typed event through its entity, rather than relying on a frame callback.
static void RetainedSlidersEmitTypedChanges() {
    App app = {};
    Window* win = new Window();
    win->app = &app;
    Arena* arena = ArenaNew();
    Ctx cx = {&app, win, arena, {}};

    Entity<ColorPickerState> picker = ColorPickerStateNew(&cx);
    Entity<ColorSink> sink = EntityNewState<ColorSink>(&app);
    SubscribeTo(&app, picker, sink, &ColorSink::OnChange);
    ColorPickerState* state = picker.Get(&app);
    cx.self = picker.id;
    utassert(state && state->self.id == picker.id);
    utassert(state && state->focus.IsValid());
    utassert(state && state->sliders.At(0) == &state->sliders.hue);
    utassert(state && state->sliders.At(1) == &state->sliders.saturation);
    utassert(state && state->sliders.At(2) == &state->sliders.lightness);
    utassert(state && state->sliders.At(3) == &state->sliders.alpha);
    utassert(state && state->sliders.At(4) == nullptr);

    state->open = true;
    state->sliders.Write(Hsla{0.2f, 0.3f, 0.4f, 0.5f});
    SliderEvent slider = {};
    ColorPickerState::OnSlider(state, &cx, &slider);
    ColorSink* received = sink.Get(&app);
    utassert(received && received->count == 1 && received->hasColor);
    utassertnear(received->color.h, 0.2f);
    utassertnear(received->color.s, 0.3f);
    utassertnear(received->color.l, 0.4f);
    utassertnear(received->color.a, 0.5f);
    utassert(state->open);

    ClickEvent click = {};
    ColorPickerState::OnSwatchClick(state, &cx, &click, 0x6366f1);
    utassert(received->count == 2 && !state->open);
    utassertnear(received->color.a, 1.f);

    EntityDropAll(&app);
    ArenaDelete(arena);
    delete win;
}

// The unstyled root owns the source ColorPicker key context: Confirm requests
// the opposite controlled state and Cancel dismisses only while open.
static void ConfirmTogglesAndCancelDismisses() {
    KeymapClear();
    App app = {};
    Window* win = new Window();
    win->app = &app;
    Arena* arena = ArenaNew();
    Ctx cx = {&app, win, arena, {}};
    Entity<ColorSink> sink = EntityNewState<ColorSink>(&app);
    FocusHandle focus = FocusHandleNew(&cx);
    Listener open = ListenTo(sink, &ColorSink::OnOpen);

    El* root =
        ColorPicker::New(&cx, StrL("picker"), false, false, StrL("Theme color"),
                         AccessibilityRole::Button, open, focus, 3, false);
    utassert(root->style.focusId == focus.id);
    utassert(root->style.tabIndex == 3 && !root->style.tabStop);
    FocusCollect(win, root);
    win->focusId = focus.id;
    utassert(WindowDispatchKeyAction(win, KeyReturn, false, false, false));
    ColorSink* received = sink.Get(&app);
    utassert(received && received->openChanges == 1 && received->open);

    root =
        ColorPicker::New(&cx, StrL("picker"), true, false, StrL("Theme color"),
                         AccessibilityRole::Button, open, focus);
    FocusCollect(win, root);
    win->focusId = focus.id;
    utassert(WindowDispatchKeyAction(win, KeyEscape, false, false, false));
    utassert(received->openChanges == 2 && !received->open);

    EntityDropAll(&app);
    ArenaDelete(arena);
    delete win;
    KeymapClear();
}

// crates/component/src/color_picker.rs test_color_select_builder: the
// builder sets the field flag, and its size, placeholder, name and featured
// colors reach the picker underneath.
static void ColorSelectBuilder() {
    App app;
    component::Init(&app);
    Window* win = new Window();
    win->app = &app;
    Arena* arena = ArenaNew();
    Ctx cx = {&app, win, arena, {}};

    Entity<ColorPickerState> state = ColorPickerStateNew(&cx);
    static const uint32_t kFeatured[] = {0xff0000};
    component::ColorSelect* select =
        component::ColorSelect::New(&cx, state)
            ->WithSize(UiSize::Large)
            ->Placeholder(StrL("Pick a color"))
            ->AccessibilityLabel(StrL("Theme color"))
            ->FeaturedColors(kFeatured, 1);
    utassert(select->picker->field);
    utassert(select->picker->size == UiSize::Large);
    utassert(StrEq(select->picker->placeholder, "Pick a color"));
    utassert(StrEq(select->picker->accessibilityLabel, "Theme color"));
    utassert(select->picker->nFeatured == 1);

    EntityDropAll(&app);
    ArenaDelete(arena);
    delete win;
}

static const El* ClickTargetAt(const El* e, float x, float y) {
    if (!e) {
        return nullptr;
    }
    for (const El* c = e->first; c; c = c->next) {
        if (const El* hit = ClickTargetAt(c, x, y)) {
            return hit;
        }
    }
    if (e->listener.IsValid() && e->Bounds().Contains({x, y})) {
        return e;
    }
    return nullptr;
}

// color_picker.rs a_click_anywhere_on_the_color_select_opens_the_picker:
// Rust clicks far from the swatch, where only the field frame can take the
// click. There is no simulated click here, so the check is the one under
// it: the element holding the open toggle spans the whole 400px field, and
// the toggle it holds opens the picker.
static void AClickAnywhereOnTheColorSelectOpensThePicker() {
    App app;
    component::Init(&app);
    Window* win = new Window();
    win->app = &app;
    Arena* arena = ArenaNew();
    Ctx cx = {&app, win, arena, {}};

    Entity<ColorPickerState> state = ColorPickerStateNew(&cx);
    El* page = Div(arena)->W(400)->Child(component::ColorSelect::New(&cx, state)
                                             ->IntoEl());
    LayoutEl(nullptr, page, 0, 0, 400, 400, 14, Rgba{});
    ColorPickerState* s = state.Get(&app);
    utassert(s && !s->open);

    const El* target = ClickTargetAt(page, 300, 16);
    utassert(target != nullptr);
    utassertnear(target->w, 400.f);
    utassertnear(target->h, 32.f);
    ClickEvent click = {};
    ColorPickerState::OnToggleOpen(s, &cx, &click);
    utassert(s->open);

    EntityDropAll(&app);
    ArenaDelete(arena);
    delete win;
}

void TestColorPicker() {
    TestSuite("color_picker");
    APreviewHidesTheValueWithoutReplacingIt();
    ClearingAPreviewOfTheCommittedColorChangesNothing();
    CommittingCarriesThePreviewWithIt();
    HexParsing();
    SyncPendingSeedsTheFieldAndSliders();
    HexStringWidth();
    NoValueAndNoPreviewShowsNothing();
    RetainedSlidersEmitTypedChanges();
    ConfirmTogglesAndCancelDismisses();
    ColorSelectBuilder();
    AClickAnywhereOnTheColorSelectOpensThePicker();
}
