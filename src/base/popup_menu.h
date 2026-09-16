#ifndef GPUI_BASE_POPUP_MENU_H_
#define GPUI_BASE_POPUP_MENU_H_
/* Unstyled popup menu — crates/ui/src/menu/popup_menu.rs */

#include "gpui/gpui.h"

namespace gpui {

// What a keystroke asks an open menu to do. Rust binds enter, escape, up,
// down, left and right in the "PopupMenu" key context; this is that table,
// read as an answer rather than routed as an action.
enum class PopupMenuAction : uint8_t {
    None,
    SelectPrev,
    SelectNext,
    Confirm,
    Cancel,
    // The arrow that reaches into the selected item's submenu, and the one
    // that steps back out of it. Which arrow is which depends on the side the
    // submenus open towards, which is what Rust reads off submenu_anchor.
    OpenSubmenu,
    CloseSubmenu
};

// popup_menu.rs::init. The six chords Rust binds in the "PopupMenu" key
// context, bound here the same way; the element declares the context and the
// dispatch finds them while focus is in the menu. Called as a menu builds,
// and does its work once.
void PopupMenuInitKeys();

// The name of that context, for the element that declares it.
Str PopupMenuContext();

// The action the keymap resolved, read as something to do. `side` is the side
// submenus open towards, which decides which arrow reaches into one and which
// steps out — Rust reads the same off submenu_anchor inside select_left and
// select_right.
PopupMenuAction PopupMenuActionOf(uint32_t id, Side side);

// select_down / select_up over the clickable items. A separator or a label is
// not clickable and is stepped over; both ends wrap. Rust's select_down with
// nothing selected takes index 0 whether or not that row is clickable, and
// select_up with nothing selected takes the last clickable row — this keeps
// both, quirk included.
int PopupMenuNextIndex(const bool* clickable, int n, int selected);
int PopupMenuPrevIndex(const bool* clickable, int n, int selected);

// The most rows the keyboard reads off a menu. The themed menu builds at most
// this many, and a menu longer than a screen scrolls rather than growing.

// What one row looks like to the keyboard. Rust's PopupMenu owns its items,
// so select_down and confirm read them off itself; the rows belong to the
// caller here, so the element copies the part the actions need onto the state
// as it builds.
struct PopupMenuRow {
    bool clickable = false;
    bool submenu = false;
    bool link = false;
    Str href = {};
};

// What a menu is between frames. Rust keeps this in the PopupMenu entity
// along with its items; the items are the caller's here, so this is the part
// that answers keys and clicks.
struct PopupMenuState {
    bool open = false;
    // selected_index: none is -1.
    int selected = -1;
    // The item whose submenu is showing, or -1.
    int openSubmenu = -1;
    // Which side submenus open towards, which decides what Left and Right do.
    Side side = Side::Right;
    // Where the menu opens, relative to the element it belongs to. A dropdown
    // hangs under its trigger and leaves this at zero; a context menu opens at
    // the pointer, which is what Rust's ContextMenuState keeps as `position`.
    float x = 0;
    float y = 0;
    // ScrollHandle offset for a scrollable menu. Kept with the menu entity so
    // rebuilding the frame does not jump a long menu back to the top.
    float scrollY = 0;
    // The menu's own focus handle and where focus was before it came up.
    // Rust's PopupMenu holds both; so does this now, rather than deriving the
    // first from the element's name — the state is what outlives the frame,
    // so it is the right thing to own a handle.
    FocusHandle focus = {};
    FocusHandle previousFocus = {};
    // Registered on the trigger's dispatch path without taking focus. The
    // trigger existed in the previous frame, so context-scoped shortcut hints
    // can resolve on the first frame this menu appears.
    FocusHandle triggerFocus = {};
    // A submenu dismisses through its parents after an item is confirmed,
    // just as Rust's dismiss_all walks parent_menu.
    Entity<PopupMenuState> parent = {};
    // Where the menu was drawn, which handle_dismiss reads off the parent: a
    // press inside the menu a submenu came out of belongs to that menu.
    Bounds bounds = {};
    // What a confirmed item reports: the item's index, bound with
    // ListenerFill the way a component hands its caller the value it made.
    Listener onConfirm = {};
    // The rows as they were last built, which is what an action reads.
    // The rows as the last frame built them, as many as it built. Rust's
    // PopupMenu holds a Vec of menu items.
    Vec<PopupMenuRow> rows;

    // The six actions, all through one handler. Rust has a method per action
    // because it dispatches on the type; there is one id to switch on here,
    // and one place that switches on it.
    static void OnAction(PopupMenuState* self, Ctx* cx, const ActionEvent* ev);

    static void OnItemClick(PopupMenuState* self, Ctx* cx, const ClickEvent* ev,
                            intptr_t ix);
    static void OnItemHover(PopupMenuState* self, Ctx* cx, const HoverEvent* ev,
                            intptr_t ix);
    static void OnSubmenuClick(PopupMenuState* self, Ctx* cx,
                               const ClickEvent* ev, intptr_t ix);
    static void OnSubmenuHover(PopupMenuState* self, Ctx* cx,
                               const HoverEvent* ev, intptr_t ix);
    static void OnScroll(PopupMenuState* self, Ctx* cx, const ScrollEvent* ev);
    // A trigger that opens and closes the menu, and a right press that opens
    // it where the pointer is — the two ways Rust puts a PopupMenu on screen
    // (DropdownMenu and ContextMenu).
    // `wasOpen` is the menu as the frame that drew this trigger had it —
    // Rust's `state.set_open(open)` before `toggle_open`, where `open` was
    // read at render time.
    static void OnTriggerClick(PopupMenuState* self, Ctx* cx,
                               const ClickEvent* ev, intptr_t wasOpen);
    // on_mouse_down_out: the press that lands past the menu closes it. Here
    // it is the release rather than the press, because that is the outside
    // event this tree reports, and it runs before the click the same release
    // makes -- so a row still gets to run after the menu it was in has gone.
    static void OnPressOutside(PopupMenuState* self, Ctx* cx,
                               const MouseUpEvent* ev);
    static void OnContextDown(PopupMenuState* self, Ctx* cx,
                              const MouseDownEvent* ev);

    ~PopupMenuState() { VecReset(rows); }
};

// Open and close, which are `PopupMenu::show` and `dismiss(&Cancel)`. A menu
// that closes forgets what was selected in it.
void PopupMenuOpen(PopupMenuState* s, Ctx* cx);
void PopupMenuDismiss(PopupMenuState* s, Ctx* cx);
void PopupMenuDismissAll(PopupMenuState* s, Ctx* cx);

// The action, applied. `clickable` is the mask over the items as the caller
// built them this frame; `hasSubmenu` says which of them open onto one.
void PopupMenuPerform(PopupMenuState* s, Ctx* cx, PopupMenuAction act,
                      const bool* clickable, const bool* hasSubmenu, int n);

// The same, over the rows the element recorded. This is the one the actions
// take, and it is where the two things a row can be — a link, and a submenu
// whose parent is the menu that has to close it — are answered.
void PopupMenuPerformRows(PopupMenuState* s, Ctx* cx, PopupMenuAction act);

// What the element records as it builds, in row order. Rust's menu owns its
// items; this is the copy the keyboard reads.
void PopupMenuBeginRows(PopupMenuState* s);
void PopupMenuAddRow(PopupMenuState* s, const PopupMenuRow& row);

// confirm: run the item and dismiss the menu, which is what Rust does for
// both a keyboard Enter and a click.
void PopupMenuConfirm(PopupMenuState* s, Ctx* cx, int ix);

} // namespace gpui
#endif // GPUI_BASE_POPUP_MENU_H_
