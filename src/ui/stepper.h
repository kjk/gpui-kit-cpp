#ifndef GPUI_SRC_UI_STEPPER_H_
#define GPUI_SRC_UI_STEPPER_H_
/* Themed stepper — crates/ui/src/stepper */

#include "ui/sizing.h"

namespace gpui {

namespace component {

// One step. Rust's StepperItem is an element of its own with the trigger
// inside it, so the icon, the content and the disabled flag belong to the
// step rather than to the row of them.
struct StepperItem {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    IconName icon = IconName::None;
    // ParentElement: the trigger holds them after the indicator.
    ArenaVec<El*> children;
    bool disabled = false;
    // Styled: a caller's whole refinement of the item root.
    ElRefiner refiner = {};

    // Filled in by Stepper, which is what knows them.
    int step = 0;
    int checkedStep = 0;
    Axis layout = Axis::Horizontal;
    bool isLast = false;
    bool textCenter = false;
    UiSize size = UiSize::Medium;
    Listener onClick;

    static StepperItem* New(Ctx* cx);
    StepperItem* Icon(IconName v);
    StepperItem* Child(El* e);
    StepperItem* Disabled(bool v);
    El* IntoEl();
};

struct Stepper {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Str id = {};
    ArenaVec<StepperItem*> items;
    int step = 0;
    Axis layout = Axis::Horizontal;
    bool disabled = false;
    bool textCenter = false;
    bool itemsCenter = false;
    UiSize size = UiSize::Medium;
    // A stepper fills the width it is given (w_full on the story's).
    float width = kFill;
    Listener onClick;

    static Stepper* New(Ctx* cx, Str id);
    Stepper* Item(StepperItem* item);
    Stepper* SelectedIndex(int i);
    Stepper* Layout(Axis v);
    Stepper* Vertical();
    Stepper* TextCenter(bool v);
    // `.items_center()` on the stepper itself: each step's row is centred on
    // the cross axis rather than pinned to its start.
    Stepper* ItemsCenter(bool v = true);
    Stepper* Disabled(bool v);
    Stepper* WithSize(UiSize s);
    Stepper* W(float px);
    Stepper* OnClick(Listener fn);
    El* IntoEl();
};

} // namespace component
} // namespace gpui
#endif // GPUI_SRC_UI_STEPPER_H_
