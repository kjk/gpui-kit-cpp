#ifndef GPUI_UI_NATIVE_MENU_H_
#define GPUI_UI_NATIVE_MENU_H_
/* A menu the OS draws — crates/ui/src/native_menu

   Unlike component::PopupMenu, which is drawn into the window and clipped to
   it, a native menu is the operating system's own and can extend past the
   window edge. Where a platform has no menu of its own (X11, the browser,
   iOS, Android), Show draws a PopupMenu built from the same rows instead,
   anchored at the pointer through Root's overlay — Rust's
   FallbackMenuOverlay (native_menu/fallback.rs). */

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
    int64_t id = 0;
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
    NativeMenu* Menu(Str label, int64_t id);
    NativeMenu* MenuWithDisabled(Str label, bool disabled, int64_t id);
    NativeMenu* MenuWithCheck(Str label, bool checked, int64_t id);
    NativeMenu* MenuWithIcon(Str label, IconName icon, int64_t id);
    // menu_with_icon(label, impl Into<Icon>, action): a path or `Data` icon.
    // Icons created with `Icon::Data` use their SVG bytes directly, without
    // an asset lookup.
    NativeMenu* MenuWithIcon(Str label, component::Icon* icon, int64_t id);
    NativeMenu* Separator();
    NativeMenu* Submenu(Str label, NativeMenu* menu);
    NativeMenu* OnSelect(Listener l);
    bool IsEmpty() const { return len(items) == 0; }

    // Show the menu at (x, y) in the window, in logical pixels, and run
    // onSelect for the row that was chosen. The OS's menu where there is one;
    // elsewhere the drawn fallback, which a window without a Root has nowhere
    // to put (Rust's `native_menu_overlay` answering None). False only for an
    // empty menu.
    bool Show(float x, float y);

    // The same rows as a drawn menu, for a caller that would rather place it
    // itself.
    PopupMenu* IntoPopupMenu(Str id) const;
};

// fallback.rs FallbackMenuOverlay: the drawn menu a window is showing in place
// of an OS one. The rows are copied off the frame arena, since the menu stays
// up across frames; the menu and each submenu have a PopupMenu state of their
// own, `states[k]` drawing `menus[k]` in the preorder CopyMenu walks.
struct NativeMenuFallback {
    Arena* a = nullptr;
    ArenaVec<const NativeMenu*> menus;
    Entity<PopupMenuState>* states = nullptr;
    int nStates = 0;
    Listener onSelect = {};
    Point position = {};
    // The last press when the menu opened (Window::lastDownAt): its release
    // is not a click outside, only one ending a later press is.
    double openedPress = -1;
    bool open = false;

    ~NativeMenuFallback();
};

// The window's overlay, created on first ask; null only for a null window.
NativeMenuFallback* NativeMenuFallbackOf(Window* win);
// fallback::show: copy `m`, open its drawn menu at (x, y) and focus it.
bool NativeMenuShowFallback(Ctx* cx, const NativeMenu* m, float x, float y);
// What row `row` of the menu drawn by `state` reports when chosen: the item,
// or null for a separator, a submenu row, a greyed row or a closed overlay.
const NativeMenuItem* NativeMenuFallbackRow(const NativeMenuFallback* f,
                                            EntityId state, int row);
// FallbackMenuOverlay::render, which Root mounts above the page; null when
// nothing is open.
El* NativeMenuFallbackOverlay(Ctx* cx);

// The rows that can be chosen, in the order the OS is given them: preorder
// over the submenus, skipping separators, submenu rows and disabled rows —
// Rust's `actions` vector, which is what makes the id the OS reports map back
// to the row that was built. Answers how many there are.
int NativeMenuSelectable(const NativeMenu* m, const NativeMenuItem** out,
                         int cap);

} // namespace component
} // namespace gpui
#endif // GPUI_UI_NATIVE_MENU_H_
