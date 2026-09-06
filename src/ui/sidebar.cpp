#include "ui/sidebar.h"
#include "base/motion.h"
#include "ui/button.h"
#include "ui/scroll.h"

namespace gpui {

namespace component {

// sidebar/mod.rs: SIDEBAR_TRANSITION_DURATION.
static const float kSidebarMotionMs = 200.f;

// DEFAULT_WIDTH is the caller's; COLLAPSED_WIDTH is not.

struct SidebarScrollState {
    float y = 0;

    static void OnScroll(SidebarScrollState* self, Ctx* cx,
                         const ScrollEvent* ev) {
        self->y = ev->offsetY;
        Notify(cx);
    }
};

SidebarItem SidebarItem::New(void* value, SidebarItemRender render) {
    SidebarItem item;
    item.value = value;
    item.render = render;
    return item;
}

static El* RenderSidebarMenuItem(void* value, Ctx*, Str id, bool collapsed) {
    SidebarMenuItem* item = (SidebarMenuItem*)value;
    item->collapsed = collapsed;
    return item->IntoEl(id);
}

static El* RenderSidebarMenu(void* value, Ctx*, Str id, bool collapsed) {
    SidebarMenu* menu = (SidebarMenu*)value;
    menu->collapsed = collapsed;
    return menu->IntoEl(id);
}

static El* RenderSidebarGroup(void* value, Ctx*, Str id, bool collapsed) {
    SidebarGroup* group = (SidebarGroup*)value;
    group->collapsed = collapsed;
    return group->IntoEl(id);
}

SidebarItem SidebarItem::From(SidebarMenuItem* item) {
    return New(item, item ? &RenderSidebarMenuItem : nullptr);
}

SidebarItem SidebarItem::From(SidebarMenu* menu) {
    return New(menu, menu ? &RenderSidebarMenu : nullptr);
}

SidebarItem SidebarItem::From(SidebarGroup* group) {
    return New(group, group ? &RenderSidebarGroup : nullptr);
}

El* SidebarItem::Render(Ctx* cx, Str id, bool collapsed) const {
    return IsValid() ? render(value, cx, id, collapsed) : nullptr;
}

void SidebarMenuState::OnItemClick(SidebarMenuState* self, Ctx* cx,
                                   const ClickEvent* ev) {
    // click_to_open opens and leaves it open; click_to_toggle flips it. The
    // caller's handler runs either way, which is what Rust's closure does
    // after it has dealt with the submenu.
    if (self->clickToOpen) {
        self->open = true;
    } else if (self->clickToToggle) {
        self->open = !self->open;
    }
    Notify(cx);
    if (self->onClick.IsValid()) {
        ListenerCall(cx->app, cx->win, self->onClick, ev);
    }
}

void SidebarMenuState::OnCaretClick(SidebarMenuState* self, Ctx* cx,
                                    const ClickEvent*) {
    // stop_propagation: the caret expands the submenu and is not a click on
    // the item. Here the caret is the innermost hit rect, so the item never
    // hears it in the first place.
    self->open = !self->open;
    Notify(cx);
}

SidebarMenuItem* SidebarMenuItem::New(Ctx* cx, Str label) {
    Arena* a = cx->a;
    SidebarMenuItem* it = ArenaNew<SidebarMenuItem>(a);
    it->a = a;
    it->cx = cx;
    it->label = label;
    return it;
}
SidebarMenuItem* SidebarMenuItem::Icon(IconName v) {
    icon = v;
    return this;
}
SidebarMenuItem* SidebarMenuItem::Refine(const Style& v, uint32_t fields) {
    StyleApplyFields(&style, v, fields);
    styleSet |= fields;
    return this;
}
SidebarMenuItem* SidebarMenuItem::LabelStyle(const Style& v, uint32_t fields) {
    StyleApplyFields(&labelStyle, v, fields);
    labelStyleSet |= fields;
    return this;
}
SidebarMenuItem* SidebarMenuItem::Active(bool v) {
    active = v;
    return this;
}
SidebarMenuItem* SidebarMenuItem::Disabled(bool v) {
    disabled = v;
    return this;
}
SidebarMenuItem* SidebarMenuItem::DefaultOpen(bool v) {
    defaultOpen = v;
    return this;
}
SidebarMenuItem* SidebarMenuItem::ClickToOpen(bool v) {
    clickToOpen = v;
    return this;
}
SidebarMenuItem* SidebarMenuItem::ClickToToggle(bool v) {
    clickToToggle = v;
    return this;
}
SidebarMenuItem* SidebarMenuItem::Suffix(El* e) {
    suffix = e;
    return this;
}
SidebarMenuItem* SidebarMenuItem::Child(SidebarMenuItem* item) {
    if (item) {
        children.Append(a, item);
    }
    return this;
}
SidebarMenuItem* SidebarMenuItem::OnClick(Listener fn) {
    onClick = fn;
    return this;
}

El* SidebarMenuItem::IntoEl(Str id) {
    const Theme& th = ThemeNow(cx->app);
    bool isSubmenu = children.len > 0;
    Entity<SidebarMenuState> st = {};
    bool isOpen = false;
    if (isSubmenu) {
        st = KeyedEntity<SidebarMenuState>(cx, KeyedName(cx, id));
        SidebarMenuState* s = st.Get(cx);
        if (s) {
            if (!s->seeded) {
                s->seeded = true;
                s->open = defaultOpen;
            }
            s->clickToOpen = clickToOpen;
            s->clickToToggle = clickToToggle;
            s->onClick = onClick;
            isOpen = !collapsed && s->open;
        }
    }

    // The item names itself, so the row, its caret, the context menu over it
    // and every child under it are named by their place in the item.
    IdScope scope(cx, id);
    El* root = Div(a)->Id(id)->FlexCol()->W(kFill);
    El* row = Div(a)
                  ->FlexRow()
                  ->W(kFill)
                  ->Shrink0()
                  ->Pad(8)
                  ->Gap(8)
                  ->ItemsCenter()
                  ->Radius(th.radius)
                  ->Font(14)
                  ->PathId(StrL("item"));
    // refine_style(&self.style): after the row's own styling, before its
    // hover and active states, which is where Rust applies it.
    StyleApplyFields(&row->style, style, styleSet);
    bool hoverable = !active && !disabled;
    if (hoverable) {
        row->HoverBg(BackgroundOpacity(th.tokens.sidebarAccent, 0.8f))
            ->HoverFg(th.sidebarAccentFg);
    }
    if (active) {
        row->Bg(th.tokens.sidebarAccent)->Fg(th.sidebarAccentFg)->Medium();
    }
    Rgba fg =
        disabled ? th.mutedFg : (active ? th.sidebarAccentFg : th.sidebarFg);
    row->Fg(fg);
    if (icon != IconName::None) {
        row->Child(IconEl(a, icon, 16)->Fg(fg));
    }
    if (collapsed) {
        row->JustifyCenter();
        // The label has nowhere to go, so it becomes the tooltip. Rust places
        // it to the right of the item; a tip here sits where the window puts
        // it.
        if (icon != IconName::None) {
            row->Tip(label);
        }
    } else {
        row->H(28);
        El* mid = Div(a)->FlexRow()->Flex1()->Gap(8)->JustifyBetween();
        // refine_style(&self.label_style) on the label's own box. The text
        // takes the row's colour unless the refinement names one.
        El* labelBox = Div(a)->FlexRow()->Flex1();
        StyleApplyFields(&labelBox->style, labelStyle, labelStyleSet);
        Rgba labelFg =
            (labelStyleSet & StyleFieldColor) ? labelStyle.color : fg;
        mid->Child(labelBox->Child(TextEl(a, label)->Font(14)->Fg(labelFg)));
        if (suffix) {
            mid->Child(suffix);
        }
        row->Child(mid);
        if (isSubmenu) {
            // The caret is its own button: it opens the submenu without
            // being a click on the item.
            row->Child(
                Button::New(cx, StrL("caret"))
                    ->Icon(isOpen ? IconName::ChevronDown
                                  : IconName::ChevronRight)
                    ->Ghost()
                    ->WithSize(UiSize::XSmall)
                    ->OnClick(ListenTo(st, &SidebarMenuState::OnCaretClick))
                    ->IntoEl()
                    // "without being a click on the item" is what
                    // stop_propagation says now that a click bubbles.
                    ->StopClick());
        }
    }
    if (!disabled) {
        // A submenu item's click goes through the state, which applies the
        // open rules before handing over to the caller.
        Listener l =
            isSubmenu ? ListenTo(st, &SidebarMenuState::OnItemClick) : onClick;
        BindClick(row, StrL("item"), l);
    }
    // context_menu(..): a right press on the row opens the caller's menu
    // where the pointer is.
    if (contextMenu) {
        row = ContextMenu::New(cx, StrL("ctx"))
                  ->Child(row)
                  ->Menu(contextMenu)
                  ->IntoEl();
    }
    root->Child(row);

    if (isOpen) {
        El* sub = Div(a)->FlexCol()->Gap(4)->PadY(2)->PadL(10)->BorderL(
            1, th.sidebarBorder);
        for (int i = 0; i < children.len; i++) {
            children[i]->collapsed = collapsed;
            sub->Child(children[i]->IntoEl(StrDup(a, fmt("%d", i))));
        }
        // ml_3p5: the rule down the submenu sits in from the parent's edge,
        // under the icon column rather than beside it.
        root->Child(Div(a)->PadL(14)->W(kFill)->Child(sub));
    }
    return root;
}

SidebarMenuItem* SidebarMenuItem::ContextMenu(PopupMenu* menu) {
    contextMenu = menu;
    return this;
}

SidebarMenu* SidebarMenu::New(Ctx* cx) {
    Arena* a = cx->a;
    SidebarMenu* m = ArenaNew<SidebarMenu>(a);
    m->a = a;
    m->cx = cx;
    return m;
}
SidebarMenu* SidebarMenu::Child(SidebarMenuItem* item) {
    if (item) {
        items.Append(a, item);
    }
    return this;
}
SidebarMenu* SidebarMenu::Refine(const Style& v, uint32_t fields) {
    StyleApplyFields(&style, v, fields);
    styleSet |= fields;
    return this;
}

El* SidebarMenu::IntoEl(Str id) {
    IdScope scope(cx, id);
    El* col = Div(a)->FlexCol()->W(kFill)->Gap(8);
    StyleApplyFields(&col->style, style, styleSet);
    for (int i = 0; i < items.len; i++) {
        items[i]->collapsed = collapsed;
        col->Child(items[i]->IntoEl(StrDup(a, fmt("%d", i))));
    }
    return col;
}

SidebarGroup* SidebarGroup::New(Ctx* cx, Str label) {
    Arena* a = cx->a;
    SidebarGroup* g = ArenaNew<SidebarGroup>(a);
    g->a = a;
    g->cx = cx;
    g->label = label;
    return g;
}
SidebarGroup* SidebarGroup::Child(SidebarMenu* menu) {
    if (menu) {
        menus.Append(a, menu);
        children.Append(a, SidebarItem::From(menu));
    }
    return this;
}
SidebarGroup* SidebarGroup::Child(SidebarMenuItem* item) {
    return Child(SidebarItem::From(item));
}
SidebarGroup* SidebarGroup::Child(SidebarItem item) {
    if (item.IsValid()) {
        children.Append(a, item);
    }
    return this;
}

El* SidebarGroup::IntoEl(Str id) {
    const Theme& th = ThemeNow(cx->app);
    IdScope scope(cx, id);
    El* col = Div(a)->FlexCol()->W(kFill);
    if (!collapsed && label.s) {
        col->Child(Div(a)
                       ->FlexRow()
                       ->Shrink0()
                       ->H(32)
                       ->PadX(8)
                       ->ItemsCenter()
                       ->Radius(th.radius)
                       ->Child(TextEl(a, label)->Font(12)->Fg(
                           RgbaOpacity(th.sidebarFg, 0.7f))));
    }
    El* inner = Div(a)->FlexCol()->W(kFill)->Gap(8);
    for (int i = 0; i < children.len; i++) {
        inner
            ->Child(children[i].Render(cx, StrDup(a, fmt("%d", i)), collapsed));
    }
    col->Child(inner);
    return col;
}

static El* SidebarBand(Ctx* cx, const ArenaVec<El*>& children, bool selected,
                       Listener onClick, Str id, const Style& style,
                       uint32_t styleSet) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    El* row = Div(a)
                  ->FlexRow()
                  ->W(kFill)
                  ->Gap(8)
                  ->Pad(8)
                  ->ItemsCenter()
                  ->JustifyBetween()
                  ->Radius(th.radius)
                  ->HoverBg(th.tokens.sidebarAccent)
                  ->HoverFg(th.sidebarAccentFg);
    StyleApplyFields(&row->style, style, styleSet);
    if (selected) {
        row->Bg(th.tokens.sidebarAccent)->Fg(th.sidebarAccentFg);
    }
    for (int i = 0; i < children.len; i++) {
        row->Child(children[i]);
    }
    return BindClick(row, id, onClick);
}

SidebarHeader* SidebarHeader::New(Ctx* cx) {
    SidebarHeader* header = ArenaNew<SidebarHeader>(cx->a);
    header->a = cx->a;
    header->cx = cx;
    return header;
}
SidebarHeader* SidebarHeader::Child(El* child) {
    if (child) {
        children.Append(a, child);
    }
    return this;
}
SidebarHeader* SidebarHeader::Selected(bool v) {
    selected = v;
    return this;
}
SidebarHeader* SidebarHeader::Collapsed(bool v) {
    collapsed = v;
    return this;
}
SidebarHeader* SidebarHeader::OnClick(Listener fn) {
    onClick = fn;
    return this;
}
SidebarHeader* SidebarHeader::Refine(const Style& v, uint32_t fields) {
    StyleApplyFields(&style, v, fields);
    styleSet |= fields;
    return this;
}
El* SidebarHeader::IntoEl() {
    return SidebarBand(cx, children, selected, onClick, StrL("sidebar-header"),
                       style, styleSet);
}

SidebarFooter* SidebarFooter::New(Ctx* cx) {
    SidebarFooter* footer = ArenaNew<SidebarFooter>(cx->a);
    footer->a = cx->a;
    footer->cx = cx;
    return footer;
}
SidebarFooter* SidebarFooter::Child(El* child) {
    if (child) {
        children.Append(a, child);
    }
    return this;
}
SidebarFooter* SidebarFooter::Selected(bool v) {
    selected = v;
    return this;
}
SidebarFooter* SidebarFooter::Collapsed(bool v) {
    collapsed = v;
    return this;
}
SidebarFooter* SidebarFooter::OnClick(Listener fn) {
    onClick = fn;
    return this;
}
SidebarFooter* SidebarFooter::Refine(const Style& v, uint32_t fields) {
    StyleApplyFields(&style, v, fields);
    styleSet |= fields;
    return this;
}
El* SidebarFooter::IntoEl() {
    // Footer's Styled/InteractiveElement implementation belongs to `base`,
    // which the themed outer row wraps as one child upstream.
    El* base = Div(a)->FlexRow()->Gap(8)->W(kFill);
    StyleApplyFields(&base->style, style, styleSet);
    for (int i = 0; i < children.len; i++) {
        base->Child(children[i]);
    }
    if (onClick.IsValid()) {
        BindClick(base, StrL("sidebar-footer-base"), onClick);
    }
    ArenaVec<El*> one;
    one.Append(a, base);
    return SidebarBand(cx, one, selected, {}, StrL("sidebar-footer"), {}, 0);
}

SidebarToggleButton* SidebarToggleButton::New(Ctx* cx) {
    Arena* a = cx->a;
    SidebarToggleButton* b = ArenaNew<SidebarToggleButton>(a);
    b->a = a;
    b->cx = cx;
    return b;
}
SidebarToggleButton* SidebarToggleButton::Collapsed(bool v) {
    collapsed = v;
    return this;
}
SidebarToggleButton* SidebarToggleButton::WithSide(Side v) {
    side = v;
    return this;
}
SidebarToggleButton* SidebarToggleButton::OnClick(Listener fn) {
    onClick = fn;
    return this;
}

El* SidebarToggleButton::IntoEl() {
    IconName icon;
    if (collapsed) {
        icon = SideIsLeft(side) ? IconName::PanelLeftOpen
                                : IconName::PanelRightOpen;
    } else {
        icon = SideIsLeft(side) ? IconName::PanelLeftClose
                                : IconName::PanelRightClose;
    }
    return Button::New(cx, StrL("collapse"))
        ->Icon(icon)
        ->Ghost()
        ->WithSize(UiSize::Small)
        ->OnClick(onClick)
        ->IntoEl();
}

Sidebar* Sidebar::New(Ctx* cx, Str id) {
    Arena* a = cx->a;
    Sidebar* s = ArenaNew<Sidebar>(a);
    s->a = a;
    s->cx = cx;
    s->id = id;
    return s;
}
Sidebar* Sidebar::WithSide(Side v) {
    side = v;
    return this;
}
Sidebar* Sidebar::Collapsible(SidebarCollapsible v) {
    collapsible = v;
    return this;
}
Sidebar* Sidebar::Collapsible(bool v) {
    collapsible = v ? SidebarCollapsible::Icon : SidebarCollapsible::None;
    return this;
}
Sidebar* Sidebar::Collapsed(bool v) {
    collapsed = v;
    return this;
}
Sidebar* Sidebar::Header(El* e) {
    header = e;
    return this;
}
Sidebar* Sidebar::Header(SidebarHeader* e) {
    return Header(e ? e->IntoEl() : nullptr);
}
Sidebar* Sidebar::Footer(El* e) {
    footer = e;
    return this;
}
Sidebar* Sidebar::Footer(SidebarFooter* e) {
    return Footer(e ? e->IntoEl() : nullptr);
}
Sidebar* Sidebar::Child(SidebarGroup* group) {
    if (group) {
        groups.Append(a, group);
        content.Append(a, SidebarItem::From(group));
    }
    return this;
}
Sidebar* Sidebar::Child(SidebarMenu* menu) {
    return Child(SidebarItem::From(menu));
}
Sidebar* Sidebar::Child(SidebarMenuItem* item) {
    return Child(SidebarItem::From(item));
}
Sidebar* Sidebar::Child(SidebarItem item) {
    if (item.IsValid()) {
        content.Append(a, item);
    }
    return this;
}
Sidebar* Sidebar::W(float px) {
    width = px;
    style.width = px;
    styleSet |= StyleFieldWidth;
    return this;
}
Sidebar* Sidebar::Refine(const Style& v, uint32_t fields) {
    StyleApplyFields(&style, v, fields);
    styleSet |= fields;
    if ((fields & StyleFieldWidth) && v.width > 0) {
        width = v.width;
    }
    return this;
}

SidebarLayout SidebarLayoutFor(SidebarCollapsible collapsible, bool collapsed,
                               float expandedWidth, Side side) {
    SidebarLayout out;
    // A collapsible of None ignores the flag entirely.
    bool isCollapsed = collapsed && collapsible != SidebarCollapsible::None;
    bool hasWidth = expandedWidth > 0;
    switch (collapsible) {
        case SidebarCollapsible::None:
            break;
        case SidebarCollapsible::Icon:
            if (hasWidth) {
                out.wrapper = SidebarWrapperKind::Animated;
                out.wrapperWidth =
                    isCollapsed ? kSidebarCollapsedWidth : expandedWidth;
            }
            break;
        case SidebarCollapsible::Offcanvas:
            if (hasWidth) {
                out.wrapper = SidebarWrapperKind::Animated;
                out.wrapperWidth = isCollapsed ? 0.f : expandedWidth;
            } else if (isCollapsed) {
                out.wrapper = SidebarWrapperKind::Static;
                out.wrapperWidth = 0;
            }
            break;
    }
    // Offcanvas on the left and everything else on the right: the side the
    // content is pinned to while the width changes under it.
    out.alignChildToEnd = collapsible == SidebarCollapsible::Offcanvas
                              ? SideIsLeft(side)
                              : !SideIsLeft(side);
    out.iconCollapsed = isCollapsed && collapsible == SidebarCollapsible::Icon;
    out.offcanvasCollapsed =
        isCollapsed && collapsible == SidebarCollapsible::Offcanvas;
    return out;
}

El* Sidebar::IntoEl() {
    const Theme& th = ThemeNow(cx->app);
    // SidebarLayout::new says what the mode and the flag come to: the width
    // the wrapper takes, which rendering the rows use, and which end the
    // content is pinned to.
    float expandedWidth = (styleSet & StyleFieldWidth)
                              ? (style.width > 0 ? style.width : 0.f)
                              : width;
    SidebarLayout layout =
        SidebarLayoutFor(collapsible, collapsed, expandedWidth, side);
    bool iconCollapsed = layout.iconCollapsed;
    // EffectTransition::width over SIDEBAR_TRANSITION_DURATION: the box around
    // the sidebar takes the width and clips, while the sidebar inside keeps
    // its own — which is what slides the content out of view rather than
    // squeezing it. The end the content is pinned to is what decides which way
    // it goes.
    float wrapW = layout.wrapperWidth;
    if (layout.wrapper == SidebarWrapperKind::Animated) {
        Motion motion = MotionNew(kSidebarMotionMs).Ease(EaseInOutCubic);
        wrapW = MotionValue(cx, MotionId(id, StrL("sidebar-width")),
                            layout.wrapperWidth, motion);
    }
    // render_child: the sidebar is still built while it is on its way out, and
    // only dropped once there is no room left to show it in.
    if (layout.offcanvasCollapsed && wrapW <= 0.5f) {
        return Div(a)->W(0)->H(kFill)->Shrink0();
    }
    // The sidebar's own width is the one it is heading for, so its rows are
    // laid out at their final size while the wrapper reveals them.
    float natural = iconCollapsed ? kSidebarCollapsedWidth : width;

    // The sidebar names itself, and the groups under it are named by their
    // place in it.
    IdScope scope(cx, id);
    El* root = Div(a)
                   ->Id(id)
                   ->FlexCol()
                   ->Shrink0()
                   ->W(natural)
                   ->H(kFill)
                   ->ClipX()
                   ->ClipY()
                   ->Bg(th.sidebar)
                   ->Fg(th.sidebarFg);
    if (SideIsLeft(side)) {
        root->BorderR(1, th.sidebarBorder);
    } else {
        root->BorderL(1, th.sidebarBorder);
    }
    // Sidebar clears caller padding before refine_style. The C++ Style is a
    // field mask rather than Option<Edge>, so omitting that bit is the same
    // reset. Every other Styled field is retained. The refinement follows
    // the side border upstream, so callers may replace that border too.
    StyleApplyFields(&root->style, style, styleSet & ~StyleFieldPad);
    if (iconCollapsed) {
        root->W(kSidebarCollapsedWidth);
    }
    if (iconCollapsed) {
        root->Gap(8);
    }
    if (header) {
        El* box = Div(a)->FlexRow()->W(kFill)->Gap(8);
        if (iconCollapsed) {
            box->PadT(8)->PadX(8);
        } else {
            box->PadT(12)->PadX(12);
        }
        box->Child(header);
        root->Child(box);
    }
    El* body = Div(a)->FlexCol()->W(kFill)->Flex1()->MinH(0);
    El* inner = Div(a)->FlexCol()->W(kFill)->Shrink0();
    if (iconCollapsed) {
        inner->Pad(8);
    } else {
        inner->PadX(12);
    }
    for (int i = 0; i < this->content.len; i++) {
        // The groups are rows of a `list(..)` in Rust, which has no gap of
        // its own: `pt_3` on the first and `pb_3` on the last are the whole
        // of the spacing around them, and two groups touch. `inner`'s
        // `gap_y_3` never applies, since the list is its only child.
        El* box = Div(a)->FlexCol()->W(kFill)->Child(this->content[i].Render(
            cx, StrDup(a, fmt("%d", i)), iconCollapsed));
        if (i == 0) {
            box->PadT(12);
        }
        if (i + 1 == this->content.len) {
            box->PadB(12);
        }
        inner->Child(box);
    }
    Entity<SidebarScrollState> scrollState = KeyedEntity<SidebarScrollState>(
        cx, KeyedName(cx, StrL("sidebar-content-scroll")));
    SidebarScrollState* scroll = scrollState.Get(cx);
    El* viewport =
        gpui::Scrollbar::Vertical(
            cx, StrL("sidebar-content-scroll"), scroll ? scroll->y : 0,
            ListenTo(scrollState, &SidebarScrollState::OnScroll))
            ->W(kFill)
            ->H(kFill)
            ->Child(inner);
    body->Child(viewport);
    root->Child(body);
    if (footer) {
        El* box = Div(a)->FlexRow()->W(kFill)->PadX(iconCollapsed ? 8.f : 12.f);
        if (iconCollapsed) {
            box->PadT(8);
        }
        box->PadB(12);
        box->Child(footer);
        root->Child(box);
    }
    if (layout.wrapper == SidebarWrapperKind::None) {
        return root;
    }
    if (layout.wrapper == SidebarWrapperKind::Static) {
        return Div(a)
            ->FlexRow()
            ->W(layout.wrapperWidth)
            ->H(kFill)
            ->Shrink0()
            ->ClipX();
    }
    // sidebar_wrapper: a clipping box of the animated width, with the sidebar
    // pinned to whichever end it slides from. At rest it is exactly the
    // sidebar's own width, so nothing moves that was not moving anyway.
    El* wrapper = Div(a)->FlexRow()->W(wrapW)->H(kFill)->Shrink0()->ClipX();
    if (layout.alignChildToEnd) {
        wrapper->JustifyEnd();
    }
    return wrapper->Child(root);
}

} // namespace component
} // namespace gpui
