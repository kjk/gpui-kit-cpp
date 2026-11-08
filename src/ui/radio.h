#ifndef GPUI_SRC_UI_RADIO_H_
#define GPUI_SRC_UI_RADIO_H_
/* Themed radio — crates/ui/src/radio.rs */

#include "ui/checkbox.h"

namespace gpui {

namespace component {

struct Radio : ChoiceBuilder<Radio> {
    // ParentElement: children follow the label in the text column.
    ArenaVec<El*> children;
    // Styled: a caller's whole refinement of the row.
    ElRefiner refiner = {};

    Radio* Child(El* child);
    El* IntoEl();
};

// crates/ui/src/radio.rs RadioGroup: a set of radios laid out on one axis,
// of which one is selected. The click reports the index, which is what Rust's
// `on_click(&usize)` hands its caller.
struct RadioGroup {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Str id = {};
    ArenaVec<Radio*> radios;
    bool horizontal = false;
    int selected = -1;
    bool disabled = false;
    UiSize size = UiSize::Medium;
    // Rust's RadioGroup has no size of its own: each radio keeps the one it
    // was given unless the group is told one.
    bool hasSize = false;
    Listener onClick;

    // The source's default constructor is a vertical group with no selected
    // item.
    static RadioGroup* New(Ctx* cx, Str id);
    static RadioGroup* Vertical(Ctx* cx, Str id);
    static RadioGroup* Horizontal(Ctx* cx, Str id);
    RadioGroup* Child(Radio* r);
    // `impl From<&str> for Radio`: a bare label is a radio of its own.
    RadioGroup* Child(Str label);
    RadioGroup* Selected(int ix);
    RadioGroup* Disabled(bool v);
    RadioGroup* WithSize(UiSize s);
    RadioGroup* OnClick(Listener fn);
    RadioGroup* OnChange(Listener fn);
    El* IntoEl();
};

} // namespace component
} // namespace gpui
#endif // GPUI_SRC_UI_RADIO_H_
