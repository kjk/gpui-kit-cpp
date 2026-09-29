#include "ui/resizable.h"
#include "base/motion.h"

namespace gpui {

namespace component {

gpui::Resizable* Resizable::New(Ctx* cx, Str id, Entity<ResizableState> state,
                                Axis axis) {
    // The state is the base group's business, including keying its own.
    return gpui::Resizable::New(cx, id, state, axis)
        ->WithHandleAppearance(nullptr, ResizeHandleAppearance());
}

ResizeIndicator ResizeHandleIndicator(ResizeHandleState state) {
    switch (state) {
        case ResizeHandleState::Hovered:
            return {20.f, 0.35f};
        case ResizeHandleState::Pressed:
            return {28.f, 0.6f};
        case ResizeHandleState::Dragging:
            return {44.f, 0.9f};
        case ResizeHandleState::Idle:
            break;
    }
    return {0.f, 0.f};
}

El* RenderResizeHandle(void*, const ResizeHandleContext* handle, Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    bool horizontal = AxisIsHorizontal(handle->AxisValue());
    ResizeIndicator target = ResizeHandleIndicator(handle->State());
    Motion policy = MotionNew(th.motion.durationFastMs)
                        .Ease(th.motion.easingMove);
    // Both values are sampled on every frame the handle is rendered, whatever
    // it is showing. A transition asks for a frame only while it is moving,
    // so a resting handle costs nothing; sampling only while the pill is up
    // would leave the retained value frozen where it was when the pill went
    // away, and the next hover would start from there.
    float length = MotionValue(
        cx, MotionName(cx, StrL("resizable-handle-indicator-length")),
        target.length, policy);
    float opacity = MotionValue(
        cx, MotionName(cx, StrL("resizable-handle-indicator-opacity")),
        target.opacity, policy);

    // The hairline fills the handle's content area exactly, so it has
    // nothing to give: shrinking it collapses the divider.
    El* line = Div(a)->FlexNone()->FlexRow()->Bg(th.border);
    if (horizontal) {
        line->W(1)->H(kFill)->ItemsCenter();
    } else {
        line->H(1)->W(kFill)->ItemsStart()->JustifyCenter();
    }
    if (length > 0.5f) {
        // Across the hairline the pill is thicker than the line and has to
        // overhang; neither flex alignment centres an item that overflows, so
        // that axis is offset by hand, half the overhang either side.
        float overhang = (kResizeIndicatorThickness - 1.f) * -0.5f;
        El* pill = Div(a)
                       ->FlexNone()
                       ->Radius(th.radiusFull)
                       ->Bg(th.mutedFg)
                       ->Opacity(opacity);
        if (horizontal) {
            pill->W(kResizeIndicatorThickness)->H(length)->MarginL(overhang);
        } else {
            pill->H(kResizeIndicatorThickness)->W(length)->MarginT(overhang);
        }
        line->Child(pill);
    }
    return line;
}

} // namespace component
} // namespace gpui
