#include "ui/title_bar.h"

namespace gpui {

namespace component {

#if !GPUI_OS_MAC
// ControlIcon: a fixed 34x34 cell per window command. The press is the
// platform window's business — WM_NCHITTEST hands it back as HTMINBUTTON and
// friends on Windows, and the X11 loop claims it before the element tree sees
// it — so the click id here is identity only, the way El::Click always is.
static El* ControlIcon(Ctx* cx, IconName icon, int clickId) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    bool isClose = clickId == ClickWinClose;
    // The icon takes the theme foreground it would default to anyway, so the
    // cell's hover color is the one that reaches it. Close needs that half:
    // its hover fills with danger, which the default foreground vanishes into.
    return Div(a)
        ->W(kTitleBarHeight)
        ->H(kFill)
        ->Shrink0()
        ->ItemsCenter()
        ->JustifyCenter()
        ->Click(clickId)
        ->HoverBg(isClose ? th.danger : th.secondaryHover)
        ->HoverFg(isClose ? th.dangerFg : th.secondaryFg)
        ->Child(IconEl(a, icon, UiIconPx(cx, UiSize::Small)));
}

// WindowControls: nothing on macOS, where the native traffic lights sit over
// the bar instead.
static El* WindowControls(Ctx* cx) {
    Arena* a = cx->a;
    Window* win = cx->win;
    // Under server-side decorations the window manager already draws a title
    // bar with its own min/max/close, and a second set on top of it is most
    // visible as two close buttons. gpui-kit gates this on
    // `Decorations::Client`; the same question here is whether the frame ever
    // came off — on X11 a manager may keep it whatever the window asked for.
    if (!WindowClientDecorated(win)) {
        return Div(a);
    }
    bool maximized = win && win->maximized;
    return Div(a)
        ->FlexRow()
        ->H(kFill)
        ->ItemsCenter()
        ->Shrink0()
        ->Child(ControlIcon(cx, IconName::WindowMinimize, ClickWinMin))
        ->Child(ControlIcon(
            cx, maximized ? IconName::WindowRestore : IconName::WindowMaximize,
            ClickWinMax))
        ->Child(ControlIcon(cx, IconName::WindowClose, ClickWinClose));
}
#endif

TitleBar* TitleBar::New(Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    TitleBar* t = ArenaNew<TitleBar>(a);
    t->a = a;
    t->cx = cx;
    // default_title_bar_background mixes the title bar 55% into the window
    // background; Rust runs that mix down a 180° gradient to the plain title
    // bar color, which this fills flat.
    Rgba mixed = RgbaMix(th.titleBar, th.background, 0.55f);
    t->content = Div(a)
                     ->FlexRow()
                     ->H(kFill)
                     ->Flex1()
                     ->MinW(0)
                     ->ItemsCenter()
                     ->JustifyBetween();
    t->bar = Div(a)
                 ->FlexRow()
                 ->W(kFill)
                 ->H(kTitleBarHeight)
                 ->Shrink0()
                 ->PadL(kTitleBarLeftPad)
                 ->ItemsCenter()
                 ->Bg(mixed)
                 ->BorderB(1, th.titleBarBorder)
                 ->Click(ClickWinCaption)
                 ->Child(t->content);
    return t;
}

TitleBar* TitleBar::Child(El* e) {
    content->Child(e);
    return this;
}

El* TitleBar::IntoEl() {
#if !GPUI_OS_MAC
    bar->Child(WindowControls(cx));
#endif
    return bar;
}

} // namespace component
} // namespace gpui
