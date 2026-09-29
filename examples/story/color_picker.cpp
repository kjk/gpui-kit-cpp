#include "Story.h"

struct ColorPickerStory {
    // Rust owns this state entity and subscribes to ColorPickerEvent::Change.
    Entity<ColorPickerState> color = {};
    Entity<ColorPickerState> selectColor = {};
    Entity<ColorPickerState> emptySelectColor = {};
    Subscription subscription = {};
    uint32_t shown = 0x6366f1;
    bool seeded = false;
    StoryToolbarState toolbar;
    static void OnChange(ColorPickerStory* self, Ctx* cx,
                         const ColorPickerEvent* ev);
    static El* Render(ColorPickerStory* self, Ctx* cx);
};

void ColorPickerStory::OnChange(ColorPickerStory* self, Ctx* cx,
                                const ColorPickerEvent* ev) {
    if (!ev || ev->kind != ColorPickerEventKind::Change || !ev->hasColor) {
        return;
    }
    Rgba color = HslaToRgba(ev->color);
    uint32_t rgb = ((uint32_t)color.r << 16) | ((uint32_t)color.g << 8) |
                   (uint32_t)color.b;
    self->shown = color.a == 255 ? rgb : ((uint32_t)color.a << 24) | rgb;
    Notify(cx);
}

static void InitializeStory(ColorPickerStory* self, Ctx* cx) {
    if (self->seeded) {
        return;
    }
    self->seeded = true;
    self->color = ColorPickerStateNew(cx);
    if (ColorPickerState* state = self->color.Get(cx)) {
        ColorPickerSetValue(state, 0x6366f1);
    }
    self->selectColor = ColorPickerStateNew(cx);
    if (ColorPickerState* state = self->selectColor.Get(cx)) {
        ColorPickerSetValue(state, 0x6366f1);
    }
    self->emptySelectColor = ColorPickerStateNew(cx);
    self->subscription =
        Subscribe(cx, self->color, &ColorPickerStory::OnChange);
}

El* ColorPickerStory::Render(ColorPickerStory* self, Ctx* cx) {
    InitializeStory(self, cx);
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    if (ColorPickerState* s = self->color.Get(cx)) {
        // What the card shows follows the picker, preview and all.
        uint32_t hex = 0;
        if (ColorPickerShown(s, &hex)) {
            self->shown = hex;
        }
    }
    uint32_t shown = self->shown;
    Rgba color = RgbaHex(shown);

    El* page = Div(a)->FlexCol()->Gap(24)->W(kFill)->ItemsCenter();
    page->Child(StoryToolbar(cx, self));

    El* sec = StorySection(cx, "Theme Color",
                           "Select a color and preview the resulting value.");
    StorySectionBody(sec)->W(440);
    El* card = Div(a)
                   ->FlexCol()
                   ->W(kFill)
                   ->Gap(16)
                   ->Pad(16)
                   ->Radius(th.radiusLg)
                   ->Border(1, th.border);

    El* head =
        Div(a)->FlexRow()->W(kFill)->Gap(16)->ItemsCenter()->JustifyBetween();
    El* text = Div(a)->FlexCol()->Gap(4);
    text->Child(StoryTxt(cx, StrL("Accent color"), 16, th.foreground)
                    ->Medium());
    text->Child(StoryTxt(cx, StrL("Used for primary actions and highlights."),
                         14, th.mutedFg));
    head->Child(text);
    head->Child(component::ColorPicker::New(cx, self->color)
                    ->WithSize(self->toolbar.size)
                    ->IntoEl());
    card->Child(head);

    // The preview: the color over a muted footer naming its hex.
    El* preview = Div(a)
                      ->FlexCol()
                      ->W(kFill)
                      ->ClipY()
                      ->Radius(th.radiusLg)
                      ->Border(1, th.border);
    preview->Child(Div(a)->W(kFill)->H(96)->Bg(color));
    El* foot = Div(a)
                   ->FlexRow()
                   ->W(kFill)
                   ->PadX(12)
                   ->PadY(8)
                   ->ItemsCenter()
                   ->JustifyBetween()
                   ->Bg(th.tokens.muted);
    foot->Child(StoryTxt(cx, StrL("Selected color"), 14, th.mutedFg));
    foot->Child(
        // `color.to_hex()`, which is the Hsla round trip and not the digits
        // the palette entry was written with.
        StoryTxt(cx, RgbaToHex(cx->a, RgbaHex(shown & 0xffffffu)), 16,
                 th.foreground)
            ->Mono()
            ->Medium());
    preview->Child(foot);
    card->Child(preview);

    StorySectionAdd(sec, card);
    page->Child(sec);

    El* selectSec = StorySection(cx, "Color Select",
                                 "A framed field for forms: the whole field "
                                 "opens the picker, like a Select.");
    StorySectionBody(selectSec)->W(440);
    El* selects = Div(a)->FlexCol()->W(kFill)->Gap(12);
    selects->Child(component::ColorSelect::New(cx, self->selectColor)
                       ->WithSize(self->toolbar.size)
                       ->IntoEl());
    selects->Child(component::ColorSelect::New(cx, self->emptySelectColor)
                       ->WithSize(self->toolbar.size)
                       ->Placeholder(StrL("Pick a color"))
                       ->IntoEl());
    StorySectionAdd(selectSec, selects);
    page->Child(selectSec);
    return page;
}

STORY_PAGE(StoryColorPicker, ColorPickerStory);
