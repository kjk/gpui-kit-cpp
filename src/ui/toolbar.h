#ifndef GPUI_SRC_UI_TOOLBAR_H_
#define GPUI_SRC_UI_TOOLBAR_H_
/* Themed toolbar — crates/component/src/toolbar.rs

   A transparent horizontal container for commands in a window, pane or
   section; the surrounding header or surface owns its background and border.
   Behavior (the toolbar role, roving Left/Right focus) is Base's
   (`base/toolbar.h`); this adds density. `Child` takes a sizable control
   and gives it the toolbar's final size when the toolbar is built, whatever
   order the builder calls came in; `Content` takes separators, labels,
   flexible spacers and custom layout as they are. Items render in source
   order. A hosted Button becomes a compact ghost command.

   Rust stores each sized control as a boxed closure over its builder. Here a
   control is any builder with `WithSize(UiSize)` and `IntoEl()`, kept as a
   pointer and a function instantiated for its type. Rust's `Styled` on the
   toolbar is the element IntoEl returns: chain on it (width, border) and it
   wins over the density defaults, as `refine_style` does. */

#include "base/toolbar.h"
#include "ui/button.h"
#include "ui/sizing.h"

namespace gpui {

namespace component {

// Sizable::prepare_for_toolbar: presentation a control takes when a toolbar
// hosts it. Most keep their own; a Button becomes a compact ghost command.
template <typename T>
inline T* PrepareForToolbar(T* control) {
    return control;
}
inline Button* PrepareForToolbar(Button* button) {
    return button->Ghost()->Compact();
}

// A toolbar or group caps its density at Medium: Large falls back to it.
inline UiSize ToolbarSize(UiSize size) {
    return size == UiSize::Large ? UiSize(UiSize::Medium) : size;
}

// One item: a sized control, built at the final size, or content as it is.
struct ToolbarItem {
    void* control = nullptr;
    El* (*build)(void* control, UiSize size) = nullptr;
    El* content = nullptr;

    // ToolbarItem::into_element: a sized control is centred in a box one
    // input height tall; content passes through.
    El* IntoEl(Ctx* cx, UiSize size) const;
};

template <typename T>
El* ToolbarBuildSized(void* control, UiSize size) {
    return PrepareForToolbar((T*)control)->WithSize(size)->IntoEl();
}

template <typename T>
inline ToolbarItem ToolbarSized(T* control) {
    ToolbarItem item;
    item.control = control;
    item.build = &ToolbarBuildSized<T>;
    return item;
}

// A semantic subgroup of toolbar controls that shares one accessible label
// and passes its size to every control added with Child.
struct ToolbarGroup {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Str id = {};
    UiSize size = UiSize::Small;
    Str label = {};
    float gap = 0;
    ArenaVec<ToolbarItem> items;

    static ToolbarGroup* New(Ctx* cx, Str id);
    ToolbarGroup* Label(Str value);
    // Styled's gap: the spacing between the group's items.
    ToolbarGroup* Gap(float value);
    template <typename T>
    ToolbarGroup* Child(T* control) {
        if (control) {
            items.Append(a, ToolbarSized(control));
        }
        return this;
    }
    // Non-sized content.
    ToolbarGroup* Content(El* content);
    ToolbarGroup* WithSize(UiSize value);
    El* IntoEl();
};

struct Toolbar {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Str id = {};
    UiSize size = UiSize::Small;
    bool disabled = false;
    ArenaVec<ToolbarItem> items;

    // A new, empty toolbar at UiSize::Small. The id keeps its keyboard state
    // stable across frames; give each toolbar in a window its own.
    static Toolbar* New(Ctx* cx, Str id);
    // Disable the roving keyboard navigation. Hosted controls are disabled by
    // their owner.
    Toolbar* Disabled(bool value);
    // A sized control; the toolbar applies its final size when it is built.
    template <typename T>
    Toolbar* Child(T* control) {
        if (control) {
            items.Append(a, ToolbarSized(control));
        }
        return this;
    }
    // Non-sized content: separators, labels, spacers.
    Toolbar* Content(El* content);
    Toolbar* Contents(El* const* contents, int count);
    // XSmall, Small and Medium; Large is Medium.
    Toolbar* WithSize(UiSize value);
    El* IntoEl();
};

} // namespace component
} // namespace gpui
#endif // GPUI_SRC_UI_TOOLBAR_H_
