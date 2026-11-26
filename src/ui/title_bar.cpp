#include "ui/title_bar.h"
#include "base/layout_direction.h"
#include "ui/window_border.h"

namespace gpui {

namespace component {

// ControlIcon: a fixed 34x34 cell per window command. The press is the
// platform window's business — WM_NCHITTEST hands it back as HTMINBUTTON and
// friends on Windows, and the X11 loop claims it before the element tree sees
// it — so the click id here is identity only, the way El::Click always is.
static El* ControlIcon(Ctx* cx, IconName icon, int clickId, Listener onClose) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    bool isClose = clickId == ClickWinClose;
    // The icon takes the theme foreground it would default to anyway, so the
    // cell's hover color is the one that reaches it. Close needs that half:
    // its hover fills with danger, which the default foreground vanishes into.
    El* cell = Div(a)
                   ->W(kTitleBarHeight)
                   ->H(kFill)
                   ->Shrink0()
                   ->ItemsCenter()
                   ->JustifyCenter()
                   ->HoverBg(isClose ? th.danger : th.secondaryHover)
                   ->HoverFg(isClose ? th.dangerFg : th.secondaryFg);
    // Linux owns the close button. A listener replaces the window-manager
    // click; without one, the platform still closes the window. Other
    // platforms keep the chrome id either way (title_bar.rs cfg linux).
    bool customClose =
        isClose && onClose.fn && base::PlatTitleBarCloseIsElement();
    if (customClose)
        cell->OnClick(onClose);
    else
        cell->Click(clickId);
    return cell->Child(IconEl(a, icon, UiIconPx(cx, UiSize::Small)));
}

// WindowControls: nothing on macOS, where the native traffic lights sit over
// the bar instead.
static El* WindowControls(Ctx* cx, const WindowButton* buttons, Str id,
                          Corners corners, Listener onClose) {
    Arena* a = cx->a;
    Window* win = cx->win;
    // Under server-side decorations the window manager already draws a title
    // bar with its own min/max/close, and a second set on top of it is most
    // visible as two close buttons. gpui-kit gates this on
    // `Decorations::Client`; the same question here is whether the frame ever
    // came off — on X11 a manager may keep it whatever the window asked for.
    if (!WindowClientDecorated(win)) {
        return Div(a)->Id(id);
    }
    WindowButtonSupport support = WindowGetButtonSupport(win);
    El* row =
        Div(a)->Id(id)->FlexRowDir(cx->app)->H(kFill)->ItemsCenter()->Shrink0();
    for (int i = 0; i < kWindowButtonsPerSide; i++) {
        WindowButton button = buttons[i];
        if (!support.Has(button)) continue;
        switch (button) {
            case WindowButton::Minimize:
                row->Child(
                    ControlIcon(cx, IconName::WindowMinimize, ClickWinMin, {}));
                break;
            case WindowButton::Maximize:
                row->Child(ControlIcon(cx,
                                       win && win->maximized
                                           ? IconName::WindowRestore
                                           : IconName::WindowMaximize,
                                       ClickWinMax, {}));
                break;
            case WindowButton::Close:
                row->Child(ControlIcon(cx, IconName::WindowClose, ClickWinClose,
                                       onClose));
                break;
            case WindowButton::Unset:
                break;
        }
    }
    if (row->first) {
        // Children are clipped to a rectangle, so the outer buttons need
        // their own radii for hover and active backgrounds.
        bool rtl = LayoutIsRtl(cx->app);
        El* left = rtl ? row->last : row->first;
        El* right = rtl ? row->first : row->last;
        left->Corners(corners.tl, left == right ? corners.tr : 0, 0, 0);
        if (right != left) right->Corners(0, corners.tr, 0, 0);
    }
    return row;
}

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
    t->windowCorners = WindowCornerRadii(cx->win, cx->app);
    t->content = Div(a)
                     ->FlexRowDir(cx->app)
                     ->H(kFill)
                     ->Flex1()
                     ->MinW(0)
                     ->ItemsCenter()
                     ->JustifyBetween();
    t->bar = Div(a)
                 ->FlexRowDir(cx->app)
                 ->W(kFill)
                 ->H(kTitleBarHeight)
                 ->Shrink0()
                 ->ItemsCenter()
                 ->Bg(mixed)
                 ->Corners(t->windowCorners.tl, t->windowCorners.tr, 0, 0)
                 ->BorderB(1, th.titleBarBorder)
                 ->Click(ClickWinCaption)
                 ->SuppressTextSelection();
    bool physicalSides = WindowGetButtonLayout().physicalSides;
    if (physicalSides) t->bar->FlexRow();
    if (PlatSecondaryIsCommand() || physicalSides) {
        t->bar->PadL(TitleBarLeftPad());
    } else {
        t->bar->PadS(TitleBarLeftPad(), cx->app);
    }
    return t;
}

TitleBar* TitleBar::Child(El* e) {
    content->Child(e);
    return this;
}

TitleBar* TitleBar::OnCloseWindow(Listener listener) {
    onCloseWindow = listener;
    return this;
}

El* TitleBar::IntoEl() {
    WindowButtonLayout layout = WindowGetButtonLayout();
    bool showsControls = base::PlatShowsWindowControls();
    if (showsControls && layout.physicalSides) {
        float radius = windowCorners.tl - bar->style.pad.left;
        Corners start = {radius > 0 ? radius : 0};
        bar->Child(WindowControls(cx, layout.left,
                                  StrL("window-controls-start"), start,
                                  onCloseWindow));
    }
    bar->Child(content);
    if (showsControls) {
        Corners end = {};
        if (layout.physicalSides || !LayoutIsRtl(cx->app)) {
            end.tr = windowCorners.tr;
        } else {
            end.tl = windowCorners.tl;
        }
        bar->Child(WindowControls(cx, layout.right, StrL("window-controls"),
                                  end, onCloseWindow));
    }
    return bar;
}

} // namespace component
} // namespace gpui
