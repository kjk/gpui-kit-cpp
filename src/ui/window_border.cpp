#include "ui/window_border.h"

namespace gpui {

namespace component {

Edges WindowBorderInsets(float shadowSize, WindowTiling tiling) {
    Edges e = {shadowSize, shadowSize, shadowSize, shadowSize};
    if (tiling.top) {
        e.top = 0;
    }
    if (tiling.bottom) {
        e.bottom = 0;
    }
    if (tiling.left) {
        e.left = 0;
    }
    if (tiling.right) {
        e.right = 0;
    }
    return e;
}

Edges WindowPaddings(Window* window) {
    if (!window || !WindowClientDecorated(window)) {
        return {};
    }
    float shadow =
        window->clientInset >= 0 ? window->clientInset : WindowShadowSize();
    return WindowBorderInsets(shadow, window->tiling);
}

Edges WindowContentInsets(Window* window) {
    if (!window || !WindowClientDecorated(window)) {
        return {};
    }
    float shadow =
        window->clientInset >= 0 ? window->clientInset : WindowShadowSize();
    Edges insets = WindowBorderInsets(shadow, window->tiling);
    const WindowTiling& tiling = window->tiling;
    if (!tiling.top) {
        insets.top += kWindowBorderSize;
    }
    if (!tiling.bottom) {
        insets.bottom += kWindowBorderSize;
    }
    if (!tiling.left) {
        insets.left += kWindowBorderSize;
    }
    if (!tiling.right) {
        insets.right += kWindowBorderSize;
    }
    return insets;
}

WindowEdge WindowResizeEdge(float x, float y, float w, float h, Edges insets,
                            WindowTiling tiling, float hitSize) {
    float innerLeft = insets.left;
    float innerRight = w - insets.right;
    float innerTop = insets.top;
    float innerBottom = h - insets.bottom;

    // Each edge only counts along its own segment of the inner frame: it does
    // not run on down the extension lines of the shadow padding.
    bool onLeft = x >= innerLeft - hitSize && x <= innerLeft + hitSize &&
                  y >= innerTop - hitSize && y <= innerBottom + hitSize;
    bool onRight = x >= innerRight - hitSize && x <= innerRight + hitSize &&
                   y >= innerTop - hitSize && y <= innerBottom + hitSize;
    bool onTop = y >= innerTop - hitSize && y <= innerTop + hitSize &&
                 x >= innerLeft - hitSize && x <= innerRight + hitSize;
    bool onBottom = y >= innerBottom - hitSize && y <= innerBottom + hitSize &&
                    x >= innerLeft - hitSize && x <= innerRight + hitSize;

    if (!tiling.top && !tiling.left && onTop && onLeft) {
        return WindowEdge::TopLeft;
    }
    if (!tiling.top && !tiling.right && onTop && onRight) {
        return WindowEdge::TopRight;
    }
    if (!tiling.bottom && !tiling.left && onBottom && onLeft) {
        return WindowEdge::BottomLeft;
    }
    if (!tiling.bottom && !tiling.right && onBottom && onRight) {
        return WindowEdge::BottomRight;
    }
    if (!tiling.top && onTop) {
        return WindowEdge::Top;
    }
    if (!tiling.bottom && onBottom) {
        return WindowEdge::Bottom;
    }
    if (!tiling.left && onLeft) {
        return WindowEdge::Left;
    }
    if (!tiling.right && onRight) {
        return WindowEdge::Right;
    }
    return WindowEdge::None;
}

WindowBorder* WindowBorder::New(Ctx* cx) {
    Arena* a = cx->a;
    WindowBorder* w = ArenaNew<WindowBorder>(a);
    w->a = a;
    w->cx = cx;
    return w;
}
WindowBorder* WindowBorder::Child(El* e) {
    child = e;
    return this;
}
WindowBorder* WindowBorder::ShadowSize(float v) {
    shadowSize = v;
    return this;
}
WindowBorder* WindowBorder::ResizeHitSize(float v) {
    resizeHitSize = v;
    return this;
}
WindowBorder* WindowBorder::Tiling(WindowTiling v) {
    tiling = v;
    hasTiling = true;
    return this;
}

El* WindowBorder::IntoEl() {
    const Theme& th = ThemeNow(cx->app);
    bool clientDecorated = !cx->win || WindowClientDecorated(cx->win);
    WindowTiling effectiveTiling =
        hasTiling || !cx->win ? tiling : cx->win->tiling;
    if (!hasTiling && cx->win && cx->win->maximized) {
        effectiveTiling.top = effectiveTiling.bottom = true;
        effectiveTiling.left = effectiveTiling.right = true;
    }
    // window.set_client_inset(platform_inset). Rust keeps the full platform
    // inset even when tiling suppresses the visual shadow on one or all
    // edges, so Positioner never places a popup under the resize frame.
    if (cx->win) {
        cx->win->paint.clientInset = shadowSize;
        cx->win->clientInset = shadowSize;
        cx->win->tiling = effectiveTiling;
        cx->win->resizeHitSize = resizeHitSize;
    }
    // uses_client_border: a server-decorated window's platform owns the
    // frame, border and shadow, and the content passes through unchanged.
    if (!clientDecorated) {
        if (cx->win) {
            cx->win->paint.clientInset = 0;
            cx->win->clientInset = 0;
        }
        return child ? child : Div(a)->SizeFull();
    }
    // A window tiled on every side keeps its platform inset but draws no
    // shadow: there is nothing for one to fall on.
    float visualShadow = effectiveTiling.AllTiled() ? 0.f : shadowSize;
    Edges insets = WindowBorderInsets(visualShadow, effectiveTiling);

    El* backdrop =
        Div(a)->FlexCol()->SizeFull()->ClipX()->ClipY()->Bg(Rgba8(0, 0, 0, 0));
    if (insets.top > 0) {
        backdrop->PadT(insets.top);
    }
    if (insets.bottom > 0) {
        backdrop->PadB(insets.bottom);
    }
    if (insets.left > 0) {
        backdrop->PadL(insets.left);
    }
    if (insets.right > 0) {
        backdrop->PadR(insets.right);
    }

    // The frame itself: a one-pixel border on every side the window is not
    // tiled against, dimmed while the window is not the active one.
    El* frame = Div(a)
                    ->FlexCol()
                    ->Flex1()
                    ->MinW(0)
                    ->MinH(0)
                    ->W(kFill)
                    ->ClipX()
                    ->ClipY()
                    ->Bg(Rgba8(0, 0, 0, 0));
    // The source deliberately uses neutral 20%/80%, independent of the
    // theme's semantic border token.
    Rgba border =
        th.mode == ThemeMode::Dark ? Rgb(51, 51, 51) : Rgb(204, 204, 204);
    float activeOpacity = 1.f;
    if (!WindowIsActive(cx)) {
        border = RgbaOpacity(border, 0.7f);
        activeOpacity = 0.7f;
    }
    if (!effectiveTiling.top) {
        frame->BorderT(kWindowBorderSize, border);
    }
    if (!effectiveTiling.bottom) {
        frame->BorderB(kWindowBorderSize, border);
    }
    if (!effectiveTiling.left) {
        frame->BorderL(kWindowBorderSize, border);
    }
    if (!effectiveTiling.right) {
        frame->BorderR(kWindowBorderSize, border);
    }
    if (!effectiveTiling.IsTiled() && shadowSize > 0) {
        // The exact two layers from window_border.rs: ambient first, contact
        // second. El copies them into the frame arena.
        BoxShadow shadows[2] = {
            {0, 2, 10, -1, Rgba8(0, 0, 0, (uint8_t)(46 * activeOpacity)),
             false},
            {0, 1, 3, 0, Rgba8(0, 0, 0, (uint8_t)(46 * activeOpacity)), false},
        };
        frame->Shadows(shadows, 2);
    }
    if (child) {
        frame->Child(child);
    }
    backdrop->Child(frame);
    return backdrop;
}

} // namespace component
} // namespace gpui
