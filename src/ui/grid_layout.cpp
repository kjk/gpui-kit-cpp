#include "ui/grid_layout.h"

#include "base/motion.h"
#include "base/theme.h"
#include "ui/theme.h"

namespace gpui {
namespace component {

// styled.rs FOCUS_RING_WIDTH and FOCUS_RING_OPACITY. The same pair paints
// El::FocusRing; a grid item carries them as its shadow so the focus ring
// replaces the selection ring.
static const float kGridFocusRingWidth = 3.f;
static const float kGridFocusRingOpacity = 0.5f;

// styles.rs shadow_lg: two black layers, {x, y, blur, spread, alpha}.
static BoxShadow* DragShadow() {
    static BoxShadow shadows[2] = {
        {0, 10, 15, -3, RgbaHsla(0, 0, 0, 0.1f), false},
        {0, 4, 6, -4, RgbaHsla(0, 0, 0, 0.1f), false},
    };
    return shadows;
}

static El* GridPlaceholder(Ctx* cx, void*) {
    const Theme& th = ThemeNow(cx->app);
    return Div(cx->a)
        ->SizeFull()
        ->Radius(th.radiusLg)
        ->Bg(RgbaOpacity(th.primary, 0.08f))
        ->Border(1, RgbaOpacity(th.primary, 0.5f))
        ->Dashed();
}

static El* GridResizeGrip(Ctx* cx, void*) {
    const Theme& th = ThemeNow(cx->app);
    Rgba color = th.mutedFg;
    El* mark = Div(cx->a)
                   ->Absolute()
                   ->Right(3)
                   ->Bottom(3)
                   ->W(6)
                   ->H(6)
                   ->BorderR(2, RgbaOpacity(color, 0.35f))
                   ->BorderB(2, RgbaOpacity(color, 0.35f))
                   ->GroupHoverBorder(RgbaOpacity(color, 0.9f));
    return Div(cx->a)->Group()->SizeFull()->Child(mark);
}

GridLayout* grid_layout(Ctx* cx, Str id, Entity<GridLayoutState> state) {
    const Theme& th = ThemeNow(cx->app);
    const SemanticThemeTokens& tokens = BaseThemeGlobal(cx->app)->tokens;
    GridLayoutState* held = state.Get(cx);
    bool editable = held && (held->IsDraggable() || held->IsResizable());
    bool ringOn = ThemeFocusRing(cx->app);
    BoxShadow* selected = ArenaNew<BoxShadow>(cx->a);
    *selected = {0, 0, 0, 2.f, th.primary, false};
    BoxShadow* ring = ArenaNew<BoxShadow>(cx->a);
    *ring = {0,
             0,
             0,
             ringOn ? kGridFocusRingWidth : 1.f,
             ringOn ? RgbaOpacity(th.ring, kGridFocusRingOpacity) : th.ring,
             false};
    Motion settle = MotionNew(th.motion.durationNormalMs)
                        .Ease(th.motion.easingMove);
    GridLayout* grid = GridLayout::New(cx, id, state);
    grid->Gap(tokens.spacing.md);
    grid->Transition(settle);
    grid->DraggingItem(StateStyle().Shadows(DragShadow(), 2));
    if (editable) {
        grid->SelectedItem(StateStyle().Shadows(selected, 1));
    }
    grid->FocusedItem(StateStyle().Shadows(ring, 1));
    grid->Placeholder(GridPlaceholder, nullptr);
    grid->ResizeHandle(GridResizeGrip, nullptr);
    return grid;
}

} // namespace component
} // namespace gpui
