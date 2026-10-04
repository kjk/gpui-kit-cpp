#ifndef GPUI_SRC_UI_WINDOW_BORDER_H_
#define GPUI_SRC_UI_WINDOW_BORDER_H_
/* Window border — crates/ui/src/window_border.rs

   A client-decorated window draws its own frame: a band of shadow padding
   around it, a one-pixel border inside that, and a resize hit band along the
   inner frame. A server-decorated one — which is what Windows and macOS give
   us — draws none of it, and `WindowBorderInsets` answers zero. */

#include "ui/sizing.h"

namespace gpui {

namespace component {

// SHADOW_SIZE: the padding a client-decorated window keeps around its frame
// for the shadow. Zero on the platforms whose window manager draws one.
inline float WindowShadowSize() {
    return PlatWindowShadowSize();
}
const float kWindowBorderSize = 1;
// Half the width of the band a press counts as a resize, either side of the
// visible frame.
const float kWindowResizeHitSize = 4;
// GPUI clips children to a rectangular mask, so a rounded frame would leave
// the corners of a child's background showing. Square until that changes,
// which is what Rust says here too.
const float kWindowBorderRadius = 0;

// Compatibility spelling retained for callers of the earlier port. The
// state itself belongs to the platform window, as gpui::Tiling does in Rust.
using WindowTiling = gpui::Tiling;

// client_frame_insets: how far the visible frame sits inside the window, per
// side.
Edges WindowBorderInsets(float shadowSize, WindowTiling tiling);

// window_paddings: visible-frame insets for fixed overlays. Server-decorated
// windows have none; client decorations use the stable inset WindowBorder
// installed, or the platform default before its first render.
Edges WindowPaddings(Window* window);

// window_content_insets: the per-side inset from the window bounds to the
// visible frame's content area. This is WindowPaddings plus the frame's own
// border, which the window wrapper draws on every side it is not tiled
// against, and it is what lays an element flush against the inside of the
// frame rather than a border's width out from it.
Edges WindowContentInsets(Window* window);

// ResizeEdge, numbered as _NET_WM_MOVERESIZE numbers its directions —
// clockwise from the top-left corner — because that is what the X11 window
// sends when one of them is grabbed. None is -1.
enum class WindowEdge : int8_t {
    None = -1,
    TopLeft = 0,
    Top = 1,
    TopRight = 2,
    Right = 3,
    BottomRight = 4,
    Bottom = 5,
    BottomLeft = 6,
    Left = 7
};

// resize_edge: which edge a press at (x, y) grabs, or None. Each edge only
// counts along its own segment of the inner frame — it does not run on down
// the shadow padding past the corner — and a tiled side is never grabbed.
WindowEdge WindowResizeEdge(float x, float y, float w, float h, Edges insets,
                            WindowTiling tiling, float hitSize);

struct WindowBorder {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    El* child = nullptr;
    float shadowSize = WindowShadowSize();
    float resizeHitSize = kWindowResizeHitSize;
    WindowTiling tiling = {};
    bool hasTiling = false;

    static WindowBorder* New(Ctx* cx);
    WindowBorder* Child(El* e);
    WindowBorder* ShadowSize(float v);
    WindowBorder* ResizeHitSize(float v);
    WindowBorder* Tiling(WindowTiling v);
    El* IntoEl();
};

} // namespace component
} // namespace gpui
#endif // GPUI_SRC_UI_WINDOW_BORDER_H_
