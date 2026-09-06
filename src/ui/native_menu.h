#ifndef GPUI_UI_NATIVE_MENU_H_
#define GPUI_UI_NATIVE_MENU_H_
/* A menu the OS draws — crates/ui/src/native_menu

   Unlike component::PopupMenu, which is drawn into the window and clipped to
   it, a native menu is the operating system's own and can extend past the
   window edge. Where a platform has no menu of its own (X11), the caller
   draws a PopupMenu built from the same rows instead, which is Rust's
   FallbackMenuOverlay. */

#include "ui/menu.h"

namespace gpui {

namespace component {

enum class NativeMenuItemKind : uint8_t {
    Item,
    Separator,
    Submenu
};

struct NativeMenu;

struct NativeMenuItem {
    NativeMenuItemKind kind = NativeMenuItemKind::Item;
    Str label = {};
    bool disabled = false;
    bool checked = false;
    // The icon beside the label, as the name, an asset path, or SVG source —
    // `Icon::data`, which wins over the other two and needs no asset lookup.
    // The drawn fallback shows it and each OS menu rasterizes it.
    IconName icon = IconName::None;
    Str iconPath = {};
    Str iconSvg = {};
    // What choosing this row reports — Rust dispatches the row's Action, and
    // this is the value handed to onSelect in its place.
    intptr_t id = 0;
    NativeMenu* submenu = nullptr;
};

struct NativeMenu {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    // As many rows as the caller adds; the builder is on the frame arena.
    ArenaVec<NativeMenuItem> items;
    // What a chosen row reports, bound with ListenerFill the way a component
    // hands its caller the value it made.
    Listener onSelect = {};

    static NativeMenu* New(Ctx* cx);
    NativeMenu* Menu(Str label, intptr_t id);
    NativeMenu* MenuWithDisabled(Str label, bool disabled, intptr_t id);
    NativeMenu* MenuWithCheck(Str label, bool checked, intptr_t id);
    NativeMenu* MenuWithIcon(Str label, IconName icon, intptr_t id);
    // menu_with_icon(label, impl Into<Icon>, action): a path or `Data` icon.
    // Icons created with `Icon::Data` use their SVG bytes directly, without
    // an asset lookup.
    NativeMenu* MenuWithIcon(Str label, component::Icon* icon, intptr_t id);
    NativeMenu* Separator();
    NativeMenu* Submenu(Str label, NativeMenu* menu);
    NativeMenu* OnSelect(Listener l);
    bool IsEmpty() const { return items.len == 0; }

    // Show the menu at (x, y) in the window, in logical pixels, and run
    // onSelect for the row that was chosen. False means this platform has no
    // menu of its own and nothing was shown — build the drawn menu instead.
    bool Show(float x, float y);

    // The same rows as a drawn menu, for the platforms without one of their
    // own and for a caller that would rather draw it anyway.
    PopupMenu* IntoPopupMenu(Str id) const;
};

// The rows that can be chosen, in the order the OS is given them: preorder
// over the submenus, skipping separators, submenu rows and disabled rows —
// Rust's `actions` vector, which is what makes the id the OS reports map back
// to the row that was built. Answers how many there are.
int NativeMenuSelectable(const NativeMenu* m, const NativeMenuItem** out,
                         int cap);

} // namespace component
} // namespace gpui
#endif // GPUI_UI_NATIVE_MENU_H_
