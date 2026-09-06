#ifndef GPUI_SRC_UI_SIDEBAR_H_
#define GPUI_SRC_UI_SIDEBAR_H_
/* Themed sidebar — crates/ui/src/sidebar */

#include "ui/sizing.h"
#include "ui/menu.h"

namespace gpui {

namespace component {

// SidebarCollapsible, the shadcn modes: collapse to icon width, slide out of
// the layout entirely, or refuse to collapse at all.
enum class SidebarCollapsible : uint8_t {
    Icon,
    Offcanvas,
    None
};

// SidebarWrapperLayout: what the box around the sidebar does with its width.
// The width named here is the one it is heading for; the wrapper transitions
// to it over SIDEBAR_TRANSITION_DURATION, as Rust's does.
enum class SidebarWrapperKind : uint8_t {
    // The sidebar sizes itself and the wrapper stays out of the way.
    None,
    // A fixed width, which is what an offcanvas sidebar with no width of its
    // own collapses to.
    Static,
    // The width the wrapper is heading for, which it transitions to.
    Animated
};

// SidebarLayout::new, whole: what a collapsible mode and a collapsed flag come
// to. `expandedWidth` of 0 is Rust's `None` — a sidebar whose width is not in
// pixels, which leaves the wrapper alone.
struct SidebarLayout {
    bool iconCollapsed = false;
    bool offcanvasCollapsed = false;
    // Which end of the wrapper the sidebar sits at, so a sidebar sliding out
    // on one side does not drag its content across.
    bool alignChildToEnd = false;
    SidebarWrapperKind wrapper = SidebarWrapperKind::None;
    float wrapperWidth = 0;
};

SidebarLayout SidebarLayoutFor(SidebarCollapsible collapsible, bool collapsed,
                               float expandedWidth, Side side);

struct SidebarMenuItem;
struct SidebarMenu;
struct SidebarGroup;

// SidebarItem. Rust uses a Clone + Collapsible trait so Sidebar and
// SidebarGroup can contain any one item type. Frame builders here own their
// values in the arena, so the same contract is a POD function table: the
// container supplies the collapsed state and stable path id at render time.
using SidebarItemRender = El* (*)(void* value, Ctx* cx, Str id, bool collapsed);

struct SidebarItem {
    void* value = nullptr;
    SidebarItemRender render = nullptr;

    static SidebarItem New(void* value, SidebarItemRender render);
    static SidebarItem From(SidebarMenuItem* item);
    static SidebarItem From(SidebarMenu* menu);
    static SidebarItem From(SidebarGroup* group);
    bool IsValid() const { return value && render; }
    El* Render(Ctx* cx, Str id, bool collapsed) const;
};

// COLLAPSED_WIDTH: the width an icon-collapsed sidebar keeps.
const float kSidebarCollapsedWidth = 48;

// The keyed state behind one menu item that has children: whether its submenu
// is open. Rust hangs it off `window.use_keyed_state(id)`, and the item's
// click closure captures it along with the caller's handler — which is why
// the handler and the two click rules live here too.
struct SidebarMenuState {
    bool open = false;
    // default_open is what the first render says and nothing after it.
    bool seeded = false;
    bool clickToOpen = false;
    bool clickToToggle = false;
    Listener onClick = {};

    static void OnItemClick(SidebarMenuState* self, Ctx* cx,
                            const ClickEvent* ev);
    static void OnCaretClick(SidebarMenuState* self, Ctx* cx,
                             const ClickEvent* ev);
};

struct SidebarMenuItem {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    IconName icon = IconName::None;
    Str label = {};
    Listener onClick;
    bool active = false;
    bool disabled = false;
    bool defaultOpen = false;
    bool clickToOpen = false;
    bool clickToToggle = false;
    El* suffix = nullptr;
    ArenaVec<SidebarMenuItem*> children;
    PopupMenu* contextMenu = nullptr;
    // `impl Styled`: the caller's refinement of the row, applied after the
    // item's own styling and before its hover and active states.
    Style style = {};
    uint32_t styleSet = 0;
    // label_style: the refinement of the label's box, independent of the row.
    Style labelStyle = {};
    uint32_t labelStyleSet = 0;
    // Filled in by whatever holds it.
    bool collapsed = false;

    static SidebarMenuItem* New(Ctx* cx, Str label);
    SidebarMenuItem* Icon(IconName v);
    SidebarMenuItem* Refine(const Style& v, uint32_t fields);
    SidebarMenuItem* LabelStyle(const Style& v, uint32_t fields);
    SidebarMenuItem* Active(bool v);
    SidebarMenuItem* Disabled(bool v);
    SidebarMenuItem* DefaultOpen(bool v);
    SidebarMenuItem* ClickToOpen(bool v);
    SidebarMenuItem* ClickToToggle(bool v);
    SidebarMenuItem* Suffix(El* e);
    SidebarMenuItem* Child(SidebarMenuItem* item);
    SidebarMenuItem* OnClick(Listener fn);
    // context_menu(..): the menu a right press on this row opens.
    SidebarMenuItem* ContextMenu(PopupMenu* menu);
    // The id is the path through the sidebar, which is what keys the submenu
    // state: "menu-0-2".
    El* IntoEl(Str id);
};

struct SidebarMenu {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    ArenaVec<SidebarMenuItem*> items;
    bool collapsed = false;
    Style style = {};
    uint32_t styleSet = 0;

    static SidebarMenu* New(Ctx* cx);
    SidebarMenu* Child(SidebarMenuItem* item);
    SidebarMenu* Refine(const Style& v, uint32_t fields);
    El* IntoEl(Str id);
};

struct SidebarGroup {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Str label = {};
    // `menus` remains the compatibility view; `children` is SidebarItem's
    // source-shaped generic content and is what rendering consumes.
    ArenaVec<SidebarMenu*> menus;
    ArenaVec<SidebarItem> children;
    bool collapsed = false;

    static SidebarGroup* New(Ctx* cx, Str label);
    SidebarGroup* Child(SidebarMenu* menu);
    SidebarGroup* Child(SidebarMenuItem* item);
    SidebarGroup* Child(SidebarItem item);
    El* IntoEl(Str id);
};

struct SidebarHeader {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    ArenaVec<El*> children;
    Style style = {};
    uint32_t styleSet = 0;
    bool selected = false;
    bool collapsed = false;
    Listener onClick = {};

    static SidebarHeader* New(Ctx* cx);
    SidebarHeader* Child(El* child);
    SidebarHeader* Selected(bool v);
    SidebarHeader* Collapsed(bool v);
    SidebarHeader* OnClick(Listener fn);
    SidebarHeader* Refine(const Style& v, uint32_t fields);
    El* IntoEl();
};

struct SidebarFooter {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    ArenaVec<El*> children;
    Style style = {};
    uint32_t styleSet = 0;
    bool selected = false;
    bool collapsed = false;
    Listener onClick = {};

    static SidebarFooter* New(Ctx* cx);
    SidebarFooter* Child(El* child);
    SidebarFooter* Selected(bool v);
    SidebarFooter* Collapsed(bool v);
    SidebarFooter* OnClick(Listener fn);
    SidebarFooter* Refine(const Style& v, uint32_t fields);
    El* IntoEl();
};

// The button that collapses the sidebar. The icon says which way it will go
// and which edge it is on.
struct SidebarToggleButton {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    bool collapsed = false;
    Side side = Side::Left;
    Listener onClick;

    static SidebarToggleButton* New(Ctx* cx);
    SidebarToggleButton* Collapsed(bool v);
    SidebarToggleButton* WithSide(Side v);
    SidebarToggleButton* OnClick(Listener fn);
    El* IntoEl();
};

struct Sidebar {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Str id = {};
    El* header = nullptr;
    El* footer = nullptr;
    // `groups` remains for source compatibility with the first C++ surface;
    // generic SidebarItem content is the structural port of Sidebar<E>.
    ArenaVec<SidebarGroup*> groups;
    ArenaVec<SidebarItem> content;
    Side side = Side::Left;
    SidebarCollapsible collapsible = SidebarCollapsible::Icon;
    bool collapsed = false;
    // DEFAULT_WIDTH; COLLAPSED_WIDTH is 48 and not the caller's to set.
    float width = 255;
    Style style = {};
    uint32_t styleSet = 0;

    static Sidebar* New(Ctx* cx, Str id);
    Sidebar* WithSide(Side v);
    Sidebar* Collapsible(SidebarCollapsible v);
    Sidebar* Collapsible(bool v);
    Sidebar* Collapsed(bool v);
    Sidebar* Header(El* e);
    Sidebar* Header(SidebarHeader* e);
    Sidebar* Footer(El* e);
    Sidebar* Footer(SidebarFooter* e);
    Sidebar* Child(SidebarGroup* group);
    Sidebar* Child(SidebarMenu* menu);
    Sidebar* Child(SidebarMenuItem* item);
    Sidebar* Child(SidebarItem item);
    Sidebar* W(float px);
    Sidebar* Refine(const Style& v, uint32_t fields);
    El* IntoEl();
};

} // namespace component
} // namespace gpui
#endif // GPUI_SRC_UI_SIDEBAR_H_
