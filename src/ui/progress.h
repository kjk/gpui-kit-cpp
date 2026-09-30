#ifndef GPUI_SRC_UI_PROGRESS_H_
#define GPUI_SRC_UI_PROGRESS_H_
/* Themed progress — crates/ui/src/progress */

#include "ui/sizing.h"
#include "base/motion.h"

namespace gpui {

namespace component {

struct Progress {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    float value = 0;
    // w_full(), as progress.rs has it; W() sizes it otherwise.
    float w = kFill;
    float h = 8;
    // `loading`: the indeterminate bar, which sweeps rather than filling.
    bool loading = false;
    // The bar's own name, so two of them on a page transition apart.
    Str id = {};
    // The accessible name. Explicit only: a progress value is not inferred
    // as one.
    Str accessibilityLabel = {};

    static Progress* New(Ctx* cx);
    Progress* Value(float v);
    Progress* W(float v);
    Progress* H(float v);
    Progress* Loading(bool v);
    Progress* Id(Str v);
    // Set the accessible name exposed by the progress indicator.
    Progress* AccessibilityLabel(Str s);
    // Sizable: the bar's height, 4 / 6 / 8 / 10 px or a custom one.
    Progress* WithSize(UiSize s);
    El* IntoEl();
};

struct ProgressCircle {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    float value = 0;
    float size = 48;
    // Where the arc starts, which only the indeterminate one moves off zero.
    float startValue = 0;
    Rgba color = {};
    bool hasColor = false;
    bool showLabel = true;
    bool loading = false;
    Str id = {};
    // The accessible name. Explicit only, as the bar's is.
    Str accessibilityLabel = {};

    static ProgressCircle* New(Ctx* cx);
    ProgressCircle* Loading(bool v);
    ProgressCircle* Id(Str v);
    // Set the accessible name exposed by the progress indicator.
    ProgressCircle* AccessibilityLabel(Str s);
    ProgressCircle* Value(float v);
    ProgressCircle* Size(float v);
    ProgressCircle* Color(Rgba c);
    ProgressCircle* Label(bool v);
    El* IntoEl();
};

} // namespace component
} // namespace gpui
#endif // GPUI_SRC_UI_PROGRESS_H_
