/* Ported from crates/ui/src/title_bar.rs, mod tests.
 *
 * `default_title_bar_background` mixes the two colours' channels by hand
 * rather than through `Colorize::mix`, so what it asks for is RgbaMix — the
 * plain channel blend, not the HSL arc RgbaMixHsl walks (ColorTests has that
 * one). This is the assertion upstream makes about it. */

#include "Test.h"

static void DefaultTitleBarBackground() {
    // Black title bar over a white background, 55% of the way to the title
    // bar, is the 0.45 grey Rust asserts: 0.45 * 255 = 114.75.
    Rgba mixed = RgbaMix(Rgb(0, 0, 0), Rgb(255, 255, 255), 0.55f);
    utassert(mixed.r == 115);
    utassert(mixed.g == 115);
    utassert(mixed.b == 115);
    utassert(mixed.a == 255);

    // The ends of the blend are the inputs themselves.
    Rgba all = RgbaMix(Rgb(0x0a, 0x0a, 0x0a), Rgb(0xfa, 0xfa, 0xfa), 1.f);
    utassert(all.r == 0x0a && all.g == 0x0a && all.b == 0x0a);
    Rgba none = RgbaMix(Rgb(0x0a, 0x0a, 0x0a), Rgb(0xfa, 0xfa, 0xfa), 0.f);
    utassert(none.r == 0xfa && none.g == 0xfa && none.b == 0xfa);
}

struct TitleBarClickView {
    int clicks = 0;

    static void OnOpen(TitleBarClickView* self, Ctx*, const ClickEvent*) {
        self->clicks++;
    }

    static El* Render(TitleBarClickView*, Ctx* cx) {
        return component::TitleBar::New(cx)
            ->Child(Div(cx->a)->W(200)->Child(TextEl(cx->a, StrL("App"))
                                                  ->Selectable()))
            ->Child(component::Button::New(cx, StrL("open"))
                        ->Label(StrL("Open"))
                        ->OnClick(Listen(cx, &TitleBarClickView::OnOpen))
                        ->IntoEl())
            ->IntoEl();
    }
};

// title_bar.rs a_press_on_a_control_keeps_its_click and
// a_press_on_the_bar_itself_still_moves_the_window. C++ native backends use
// WindowChromeHit to decide which presses start a window move.
static void TitleBarSeparatesControlClicksFromWindowMoves() {
    App* app = TestAppNew();
    component::Init(app);
    auto view = EntityNew<TitleBarClickView>(app);
    Window* win = TestWindowOpen(app, view, 600, 200, 1);
    Bounds button = {};
    for (const AccessibilityNode& node : win->accessibility) {
        if (node.info.role == AccessibilityRole::Button &&
            StrEq(node.info.label, StrL("Open"))) {
            button = node.bounds;
            break;
        }
    }
    utassert(button.w > 1);
    Point at = {button.CenterX(), button.CenterY()};
    Point drift = {at.x + 1, at.y};
    utassert(WindowChromeHit(win, 150, at.y) == ClickWinCaption);
    utassert(WindowChromeHit(win, at.x, at.y) == 0);
    utassert(WindowChromeHit(win, drift.x, drift.y) == 0);
    TestSimulateMouseDown(win, at);
    TestSimulateMouseMove(win, drift, true);
    TestSimulateMouseUp(win, drift);
    utassert(view.Get(app)->clicks == 1);
    // title_bar.rs a_press_on_the_bar_does_not_start_a_text_selection.
    Bounds title = {};
    for (const TextHit& hit : win->paint.texts) {
        if (StrEq(hit.text, StrL("App"))) title = hit.bounds;
    }
    utassert(title.w > 0);
    Point titleCenter = {title.CenterX(), title.CenterY()};
    utassert(WindowChromeHit(win, titleCenter.x, titleCenter.y) ==
             ClickWinCaption);
    TestSimulateMouseDown(win, titleCenter);
    utassert(!win->sel || !win->sel->gesture.selecting);
    TestSimulateMouseUp(win, titleCenter);
    TestAppFree(app);
}

// title_bar.rs linux_window_controls_follow_layout_order_and_capabilities.
static void DesktopButtonLayoutKeepsBothSidesAndOrder() {
    WindowButtonLayout layout;
    utassert(WindowButtonLayoutParse(
        StrL("close, menu,minimize:maximize,close"), &layout));
    utassert(layout.physicalSides);
    utassert(layout.left[0] == WindowButton::Close);
    utassert(layout.left[1] == WindowButton::Minimize);
    utassert(layout.left[2] == WindowButton::Unset);
    utassert(layout.right[0] == WindowButton::Maximize);
    utassert(layout.right[1] == WindowButton::Close);
    utassert(layout.right[2] == WindowButton::Unset);
    WindowButtonSupport support = {true, false};
    for (WindowButton button : layout.left)
        utassert(support.Has(button) == (button != WindowButton::Unset));
    utassert(!support.Has(layout.right[0]));
    utassert(support.Has(layout.right[1]));
    utassert(!WindowButtonLayoutParse(StrL("invalid"), &layout));
    utassert(WindowButtonLayoutParse(StrL(":close"), &layout));
    utassert(layout.left[0] == WindowButton::Unset &&
             layout.right[0] == WindowButton::Close);
}

void TestTitleBar() {
    DesktopButtonLayoutKeepsBothSidesAndOrder();
    TestSuite("title_bar");
    DefaultTitleBarBackground();
    TitleBarSeparatesControlClicksFromWindowMoves();
}
