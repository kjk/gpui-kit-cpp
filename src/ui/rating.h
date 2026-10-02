#ifndef GPUI_SRC_UI_RATING_H_
#define GPUI_SRC_UI_RATING_H_
/* Themed rating — crates/ui/src/rating.rs */

#include "ui/sizing.h"

namespace gpui {

namespace component {

// Rust's RaingState [sic], from `window.use_keyed_state(id, ..)`. The rating
// keeps the value it is showing between frames, so a click lands before the
// caller has answered — and the hovered star, which is what previews a value
// the pointer has not committed to yet.
struct RatingState {
    // The caller's `value` as of the last frame, so an external change can be
    // told apart from one the user made.
    int defaultValue = 0;
    int value = 0;
    // 0 when nothing is hovered; otherwise the star the pointer is on.
    int hoveredValue = 0;
    Listener onClick = {};

    static void OnStarHover(RatingState* self, Ctx* cx, const HoverEvent* ev,
                            int64_t ix);
    static void OnStarClick(RatingState* self, Ctx* cx, const ClickEvent* ev,
                            int64_t ix);
};

struct Rating {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Str id = {};
    int value = 0;
    int max = 5;
    bool disabled = false;
    Rgba color = {};
    bool hasColor = false;
    UiSize size = UiSize::Medium;
    Listener onClick;

    static Rating* New(Ctx* cx, Str id);
    Rating* Value(int v);
    Rating* Max(int v);
    Rating* Disabled(bool v);
    Rating* Color(Rgba c);
    Rating* WithSize(UiSize s);
    Rating* OnClick(Listener fn);
    El* IntoEl();
};

} // namespace component
} // namespace gpui
#endif // GPUI_SRC_UI_RATING_H_
