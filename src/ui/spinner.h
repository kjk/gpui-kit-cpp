#ifndef GPUI_SRC_UI_SPINNER_H_
#define GPUI_SRC_UI_SPINNER_H_
/* Themed spinner — crates/ui/src/spinner.rs */

#include "ui/sizing.h"
#include "base/motion.h"

namespace gpui {

namespace component {

struct Spinner {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    UiSize size = UiSize::Medium;
    float px = 0;
    IconName icon = IconName::Loader;
    // `loading_icon(Icon::default().data(..))`: SVG source in place of the
    // named icon. Set, it is what turns.
    Str iconSvg = {};
    Rgba color = {};
    bool hasColor = false;
    // `speed` and `ease`: how long one turn takes, and along which curve.
    float speed = 0;
    EaseFn ease = nullptr;
    // Two spinners on one page turn from wherever each of them started, so the
    // phase is keyed per spinner. Rust names the animation "circle" inside the
    // element it belongs to, which comes to the same thing.
    Str id = {};

    static Spinner* New(Ctx* cx);
    Spinner* Speed(float ms);
    Spinner* Ease(EaseFn fn);
    Spinner* Id(Str v);
    Spinner* WithSize(UiSize s);
    Spinner* Size(float v);
    Spinner* Icon(IconName n);
    Spinner* IconData(Str svg);
    Spinner* Color(Rgba c);
    El* IntoEl();
};

} // namespace component
} // namespace gpui
#endif // GPUI_SRC_UI_SPINNER_H_
