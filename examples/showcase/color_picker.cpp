#include "Showcase.h"
#include "gpui.h"

using namespace gpui;

static const uint32_t kSwatches[] = {0xdc2626, 0xd97706, 0x16a34a, 0x2563eb,
                                     0x7c3aed};
static const int kNSwatches = (int)(sizeof(kSwatches) / sizeof(kSwatches[0]));

static Rgba FromHex(uint32_t h) {
    return Rgb((uint8_t)((h >> 16) & 0xff), (uint8_t)((h >> 8) & 0xff),
               (uint8_t)(h & 0xff));
}

static void WriteHex(ShowcaseApp* app, uint32_t hex) {
    InputSetValue(&app->hexIn, fmt("#%06X", hex & 0xffffff));
}

static void SetHexBuf(ShowcaseApp* app) {
    WriteHex(app, app->colorHex);
}

// displayed_color(): `self.preview.or(self.value)`. A swatch under the
// pointer previews without committing, so the trigger and the hex field show
// the preview while there is one and the committed color once there is not.
static uint32_t DisplayedColor(ShowcaseApp* app) {
    uint32_t shown = app->colorHasPreview ? app->colorPreview : app->colorHex;
    return shown & 0xffffff;
}

// on_hover on the swatch, with the color it stands for bound to the handler
// the way Rust's closure captures it — entering previews, leaving restores.
static void PreviewSwatch(ShowcaseApp* app, Ctx* cx, const HoverEvent* ev,
                          intptr_t ix) {
    if (ev->hovered) {
        app->colorPreview = kSwatches[ix];
        app->colorHasPreview = true;
    } else if (app->colorHasPreview && app->colorPreview == kSwatches[ix]) {
        app->colorHasPreview = false;
    } else {
        return;
    }
    Notify(cx);
}

static void ToggleColor(ShowcaseApp* app, Ctx* cx, const ClickEvent*) {
    app->colorOpen = !app->colorOpen;
    app->hexIn.focused = false;
    Notify(cx);
}

static void FocusHex(ShowcaseApp* app, Ctx* cx, const ClickEvent*) {
    app->hexIn.focused = true;
    app->input.focused = false;
    Notify(cx);
}

static void PickSwatch(ShowcaseApp* app, Ctx* cx, const ClickEvent*,
                       intptr_t ix) {
    app->colorHex = kSwatches[ix];
    // update_value: what was transient is now what the picker holds.
    app->colorHasPreview = false;
    SetHexBuf(app);
    app->colorOpen = false;
    app->hexIn.focused = false;
    Notify(cx);
}

El* ShowcaseColorPicker(ShowcaseApp* app, Ctx* cx) {
    Arena* a = cx->a;
    uint32_t shown = DisplayedColor(app);
    if (!app->hexIn.focused) {
        WriteHex(app, shown);
    }
    El* trigger =
        Div(a)
            ->Id(StrL("color-trigger"))
            ->H(28)
            ->PadX(8)
            ->Gap(8)
            ->Border(1, ExampleRgb(0x171717))
            ->Bg(ExampleRgb(0xffffff))
            ->OnClick(Listen(cx, &ToggleColor))
            ->FocusId(HashClickId(StrL("color-trigger")))
            ->Child(Div(a)
                        ->W(14)
                        ->H(14)
                        ->Bg(FromHex(shown))
                        ->Border(1, ExampleRgb(0x171717)))
            ->Child(TextEl(a, InputValue(&app->hexIn))
                        ->Font(12)
                        ->Fg(ExampleRgb(0x171717)))
            ->Child(Div(a)->Flex1())
            ->Child(ScChevron(cx, app->colorOpen, ExampleRgb(0x171717)));
    El* pop = nullptr;
    if (app->colorOpen) {
        pop = Div(a)
                  ->FlexCol()
                  ->W(220)
                  ->Pad(8)
                  ->Gap(8)
                  ->Border(1, ExampleRgb(0x171717))
                  ->Bg(ExampleRgb(0xffffff));
        El* sw = Div(a)->FlexRow()->Gap(4);
        for (int i = 0; i < kNSwatches; i++) {
            bool on = (app->colorHex & 0xffffff) == kSwatches[i];
            sw->Child(ColorSwatch::New(cx, DupFmt(cx, "swatch-%d", i),
                                       Listen(cx, &PickSwatch, i),
                                       Listen(cx, &PreviewSwatch, i),
                                       kSwatches[i], on)
                          ->W(24)
                          ->H(24)
                          ->Bg(FromHex(kSwatches[i]))
                          ->Border(1, on ? ExampleRgb(0x171717)
                                         : ExampleRgb(0xffffff)));
        }
        pop->Child(sw);
        pop->Child(InputBase::New(cx, StrL("color-hex-input"), true)
                       ->OnClick(Listen(cx, &FocusHex))
                       ->FocusId(0)
                       ->W(204)
                       ->H(28)
                       ->PadX(8)
                       ->Border(1, ExampleRgb(0xd4d4d4))
                       ->Child(Input::New(cx, &app->hexIn)));
    }
    El* root =
        ColorPicker::New(cx, StrL("example-color-picker"), app->colorOpen,
                         false, StrL("Color"), AccessibilityRole::Button,
                         Listen(cx, &ToggleColor))
            ->W(220)
            ->Child(trigger);
    return Popup::New(cx, StrL("example-color-picker-popup"), root)
        ->Content(pop)
        ->IntoEl();
}

SHOWCASE_PAGE(CompColorPicker, ShowcaseColorPicker);
