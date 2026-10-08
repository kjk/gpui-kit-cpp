#ifndef GPUI_BASE_TOOLTIP_H_
#define GPUI_BASE_TOOLTIP_H_
/* Unstyled tooltip popup — crates/base/src/tooltip.rs
 *
 * Tooltip is the popup surface. TooltipOverlay is the per-window lifecycle:
 * it owns the delayed show, grace-period hide, switching transition and the
 * application content builder. TooltipPositioner is the source's named view
 * of the shared side Positioner.
 */

#include "base/positioner.h"

namespace gpui {

constexpr int kTooltipPriority = 200;
constexpr float kTooltipWindowMargin = 4.f;
constexpr int kTooltipGracePeriodMs = 300;
constexpr int kTooltipShowDelayMs = 500;

// tooltip.rs TooltipDefaults: application-wide timing for tooltips shown
// through TooltipOverlay. Read on every show and hide, so installing new
// defaults applies to windows that are already open.
struct TooltipDefaults {
    int showDelayMs = kTooltipShowDelayMs;
    int gracePeriodMs = kTooltipGracePeriodMs;

    static TooltipDefaults New() { return {}; }
    TooltipDefaults& WithShowDelay(int ms);
    TooltipDefaults& WithGracePeriod(int ms);
    int ShowDelay() const { return showDelayMs; }
    int GracePeriod() const { return gracePeriodMs; }
    void Install(App* app) const;
    static TooltipDefaults Global(const App* app);
};

struct Tooltip {
    static El* New(Ctx* cx, Str id);
};

using TooltipBuilder = El* (*)(Ctx * cx, void* data);

enum class TooltipTransitionKind : uint8_t {
    Enter,
    Switch
};

// Rust's data-carrying TooltipTransition enum is a tagged POD here.
struct TooltipTransition {
    TooltipTransitionKind kind = TooltipTransitionKind::Enter;
    uint64_t epoch = 0;
    Bounds previous = {};
    Bounds current = {};

    static TooltipTransition Enter(uint64_t epoch);
    static TooltipTransition Switch(uint64_t epoch, Bounds previous,
                                    Bounds current);
};

using TooltipRenderer = El* (*)(Ctx * cx, El* view,
                                const TooltipTransition& transition,
                                void* data);

// Content requested by a trigger. Callback data is application-owned; the
// overlay deep-copies text used by the convenience El::Tip path.
struct TooltipRequest {
    TooltipBuilder build = nullptr;
    void* buildData = nullptr;
    Bounds triggerBounds = {};
    gpui::Placement preferredPlacement = gpui::Placement::Top;
    bool hasPreferredPlacement = false;
    // with_show_delay: None uses TooltipDefaults. Zero shows at once.
    bool hasShowDelay = false;
    int showDelayMs = 0;
    Str text = {};

    static TooltipRequest New(Bounds triggerBounds, TooltipBuilder build,
                              void* data = nullptr);
    static TooltipRequest Text(Bounds triggerBounds, Str text);
    // with_placement. Placement remains the old name.
    TooltipRequest& WithPlacement(gpui::Placement value);
    TooltipRequest& Placement(gpui::Placement value);
    TooltipRequest& WithShowDelay(int ms);
};

// Per-window provider and overlay. `pending` is what Rust's show_task owns;
// `content` is Some only after the delay or during an immediate switch.
struct TooltipOverlay {
    // Touch-only targets do not synthesize hover tooltips. Kept mutable so
    // the policy can be regression-tested on desktop.
    bool enabled = !(GPUI_OS_IOS || GPUI_OS_ANDROID);
    TooltipRequest content = {};
    TooltipRequest pending = {};
    Bounds previousBounds = {};
    bool hasContent = false;
    bool hasPending = false;
    bool hasPreviousBounds = false;
    bool hadRecentTooltip = false;
    bool isSwitching = false;
    uint64_t epoch = 0;
    uint64_t animationEpoch = 0;
    int showTask = 0;
    int hideTask = 0;
    TooltipRenderer renderer = nullptr;
    void* rendererData = nullptr;

    ~TooltipOverlay();

    TooltipOverlay* RenderWith(TooltipRenderer renderer, void* data = nullptr);
    uint64_t NextEpoch();
    void RequestShow(const TooltipRequest& request, Window* window, Ctx* cx);
    void RequestHide(Window* window, Ctx* cx);
    void Hide(Ctx* cx);
    static El* Render(TooltipOverlay* self, Ctx* cx);
    static void OnShow(TooltipOverlay* self, Ctx* cx, const TickEvent* ev);
    static void OnHide(TooltipOverlay* self, Ctx* cx, const TickEvent* ev);
};

struct TooltipPositioner {
    Positioner* positioner = nullptr;

    static TooltipPositioner* New(Ctx* cx, Bounds triggerBounds);
    TooltipPositioner* Placement(gpui::Placement value);
    TooltipPositioner* Child(El* child);
    El* IntoEl();
};

// Runtime convenience driven by El::Tip hover state. It uses the same source
// overlay and merely supplies a themed text builder for the common case.
// `placement` is the trigger's preferred side as the value of `Placement`,
// or -1 for none — El::TipPlacement's spelling, since the trigger is a style
// flag below this header. managed_tooltip_with_placement's `Option`.
// `rootLayer` is a trigger the window's root view drew (El::RootTooltip),
// which asks the overlay that view owns (Window::rootTooltip) when it has
// one.
void TooltipRequestShow(Window* win, Str text, Bounds triggerBounds,
                        int placement = -1, bool rootLayer = false,
                        int showDelayMs = -1);
void TooltipRequestHide(Window* win);
void TooltipHide(Window* win);
const TooltipOverlay* TooltipShowing(Window* win);

} // namespace gpui
#endif // GPUI_BASE_TOOLTIP_H_
