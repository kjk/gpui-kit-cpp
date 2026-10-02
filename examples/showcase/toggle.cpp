#include "Showcase.h"
#include "gpui.h"

using namespace gpui;

// The toggle reports the value its activation produces, the way Rust's
// on_change hands the handler `!pressed`.
static void ToggleBold(ShowcaseApp* app, Ctx* cx, const ClickEvent*,
                       int64_t next) {
    app->toggleOn = next != 0;
    Notify(cx);
}

// The group's two other cells are bits of one selection, which is Rust's
// `toggle_group_selection: u8` and its `|= 1` / `&= !1` pair.
static void ToggleItalic(ShowcaseApp* app, Ctx* cx, const ClickEvent*,
                         int64_t next) {
    app->toggleGroup = next ? (uint8_t)(app->toggleGroup | 1)
                            : (uint8_t)(app->toggleGroup & ~1);
    Notify(cx);
}

static void ToggleUnderline(ShowcaseApp* app, Ctx* cx, const ClickEvent*,
                            int64_t next) {
    app->toggleGroup = next ? (uint8_t)(app->toggleGroup | 2)
                            : (uint8_t)(app->toggleGroup & ~2);
    Notify(cx);
}

// `borderLeft` is Rust's `border_l_0` on the group's second and third cells:
// the row is `gap_0`, so the cells share one hairline between them rather
// than drawing two side by side.
static El* ToggleCell(Ctx* cx, Str id, Listener onChange, const char* label,
                      bool on, bool borderLeft = true) {
    Arena* a = cx->a;
    El* b = Toggle::New(cx, id, on, false, onChange)
                ->W(28)
                ->H(28)
                ->ItemsCenter()
                ->JustifyCenter();
    if (borderLeft) {
        b->Border(1, ExampleRgb(0x171717));
    } else {
        b->BorderT(1, ExampleRgb(0x171717))
            ->BorderR(1, ExampleRgb(0x171717))
            ->BorderB(1, ExampleRgb(0x171717));
    }
    if (on) {
        b->Bg(ExampleRgb(0x171717))
            ->Child(TextEl(a, Str(label))
                        ->Font(12)
                        ->Fg(ExampleRgb(0xffffff))
                        ->Bold());
    } else {
        b->Bg(ExampleRgb(0xffffff))
            ->HoverBg(ExampleRgb(0xf5f5f5))
            ->Child(TextEl(a, Str(label))
                        ->Font(12)
                        ->Fg(ExampleRgb(0x171717))
                        ->Bold());
    }
    return b;
}

El* ShowcaseToggle(ShowcaseApp* app, Ctx* cx) {
    return ToggleCell(cx, StrL("example-toggle"), Listen(cx, &ToggleBold), "B",
                      app->toggleOn);
}

El* ShowcaseToggleGroup(ShowcaseApp* app, Ctx* cx) {
    bool italic = (app->toggleGroup & 1) != 0;
    bool under = (app->toggleGroup & 2) != 0;
    return ToggleGroup::New(cx, StrL("example-toggle-group"))
        ->FlexRow()
        ->Child(ShowcaseToggle(app, cx))
        ->Child(ToggleCell(cx, StrL("italic-toggle"), Listen(cx, &ToggleItalic),
                           "I", italic, false))
        ->Child(ToggleCell(cx, StrL("underline-toggle"),
                           Listen(cx, &ToggleUnderline), "U", under, false));
}

SHOWCASE_PAGE(CompToggle, ShowcaseToggle);
SHOWCASE_PAGE(CompToggleGroup, ShowcaseToggleGroup);
