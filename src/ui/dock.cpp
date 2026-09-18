#include "ui/i18n.h"
#include "ui/dock.h"
#include "ui/menu.h"

namespace gpui {

namespace component {

PanelHandle PanelHandle::New(const PanelView& panel) {
    PanelHandle handle;
    handle.view = panel;
    return handle;
}

PanelHandle PanelHandle::FromView(const PanelView& panel) {
    return New(panel);
}

const PanelView* PanelHandle::Of(const PanelView* panel) {
    // Rust needs an Any downcast here because Base erased a sub-trait object.
    // DockPanelDef is the concrete function table in this port, so every
    // panel reaching this seam is already recoverable.
    return panel;
}

const PanelView* PanelHandle::Get() const {
    return &view;
}

PanelView PanelHandle::IntoPanelView() const {
    return view;
}

PanelHandle panel_handle(const PanelView& panel) {
    return PanelHandle::New(panel);
}

DockSkin DockSkin::New(Entity<DockState> state) {
    DockSkin skin;
    skin.state = state;
    return skin;
}

PanelStyle DockSkin::GetPanelStyle(App* app) const {
    DockState* value = state.Get(app);
    return value ? value->panelStyle : PanelStyle::Auto;
}

void DockSkin::SetPanelStyle(App* app, Window* win, PanelStyle style) {
    if (DockState* value = state.Get(app)) {
        value->panelStyle = style;
        NotifyEntity(app, state.id, win);
    }
}

bool DockSkin::IsToggleButtonVisible(App* app) const {
    DockState* value = state.Get(app);
    return value ? value->toggleButtonVisible : true;
}

void DockSkin::SetToggleButtonVisible(App* app, Window* win, bool visible) {
    if (DockState* value = state.Get(app)) {
        value->toggleButtonVisible = visible;
        NotifyEntity(app, state.id, win);
    }
}

El* DockInvalidPanelRender(Ctx* cx, void* data) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    auto* info = (DockInvalidPanel*)data;
    Str name = info ? info->name : StrL("");
    // my_6, centred, muted: Rust's sentence, with the name it was asked for.
    return Div(a)
        ->SizeFull()
        ->PadY(24)
        ->FlexCol()
        ->ItemsCenter()
        ->JustifyCenter()
        ->Child(TextEl(a, StrDup(a, fmt("The `%s` panel type is not "
                                        "registered in PanelRegistry.",
                                        name)))
                    ->Fg(th.mutedFg));
}

DockArea* DockArea::New(Ctx* cx, Str id, Entity<DockState> state) {
    Arena* a = cx->a;
    DockArea* d = ArenaNew<DockArea>(a);
    d->a = a;
    d->cx = cx;
    d->id = id;
    d->state = state;
    return d;
}

DockArea* DockArea::WithSkin(const DockSkin* value) {
    skin = value;
    return this;
}

// ---------------------------------------------------------------------------
// The theme's skin.
//
// Everything below is one implementation of base's `DockRenderer` — the
// `crates/ui` half of upstream's split, where `crates/base` owns the tree,
// the drag, the drop and the resize and has no opinion at all about how any
// of it looks. The base showcase is the other implementation.

static El* PanelTitle(Ctx* cx, const PanelView* panel, Rgba fallback) {
    if (!panel) {
        return TextEl(cx->a, StrL(""))->Fg(fallback);
    }
    if (panel->titleEl) {
        if (El* title = panel->titleEl(cx, panel->data)) {
            return title;
        }
    }
    Str title = panel->title.s ? panel->title : panel->name;
    return TextEl(cx->a, title)->Fg(fallback);
}

static bool PanelTitleStyle(Ctx* cx, const PanelView* panel, TitleStyle* out) {
    if (!panel || !panel->titleStyle || !out) {
        return false;
    }
    return panel
        ->titleStyle(cx, panel->data, &out->background, &out->foreground);
}

DragPanelPreview* DragPanelPreview::New(Ctx* cx, const PanelView* panel) {
    DragPanelPreview* preview = ArenaNew<DragPanelPreview>(cx->a);
    preview->cx = cx;
    preview->panel = panel;
    return preview;
}

El* DragPanelPreview::IntoEl() {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    return Div(a)
        ->Id(StrL("drag-panel"))
        // The runtime has no grab cursor yet; Arrow is the portable fallback.
        ->Cursor(CursorKind::Arrow)
        ->W(kDockDragPreviewW)
        ->PadY(4)
        ->PadX(12)
        ->ClipX()
        ->Radius(th.radius)
        ->Border(1, th.border)
        ->Bg(th.tokens.tabActiveBg)
        ->Fg(th.tabFg)
        ->Opacity(0.75f)
        ->Child(PanelTitle(cx, panel, th.tabFg)->LineHeight(1.f));
}

// The three toggle buttons Rust hangs off the tab panel it picked for each
// edge (DockArea::toggle_button_panels).
static El* ToggleButton(const DockTabGroup* g, DockPlacement p, IconName icon) {
    Ctx* cx = g->cx;
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    return DockBindToggle(g, p,
                          Div(a)
                              ->Pad(4)
                              ->Radius(th.radius * 0.5f)
                              ->HoverBg(th.tokens.secondary)
                              ->Child(IconEl(a, icon, 14)->Fg(th.mutedFg)));
}

// The leading pair — left and bottom — or the trailing one, right.
static El* RenderToggles(const DockTabGroup* g, bool trailing) {
    Arena* a = g->cx->a;
    DockState* s = g->state.Get(g->cx);
    El* row = Div(a)->FlexRow()->ItemsCenter()->Shrink0()->Gap(4);
    if (trailing) {
        if (DockGroupHasToggle(g, DockPlacement::Right)) {
            row->Child(ToggleButton(g, DockPlacement::Right,
                                    s->right.open ? IconName::PanelRight
                                                  : IconName::PanelRightOpen));
        }
        return row;
    }
    if (DockGroupHasToggle(g, DockPlacement::Left)) {
        row->Child(ToggleButton(
            g, DockPlacement::Left,
            s->left.open ? IconName::PanelLeft : IconName::PanelLeftOpen));
    }
    if (DockGroupHasToggle(g, DockPlacement::Bottom)) {
        row->Child(ToggleButton(g, DockPlacement::Bottom,
                                s->bottom.open ? IconName::PanelBottom
                                               : IconName::PanelBottomOpen));
    }
    return row;
}

static bool HasLeadingToggles(const DockTabGroup* g) {
    return DockGroupHasToggle(g, DockPlacement::Left) ||
           DockGroupHasToggle(g, DockPlacement::Bottom);
}

// TabPanel::render_toolbar: the panel's zoom button — only where its
// PanelControl says the toolbar is one of the places it shows — and the menu
// beside it. A collapsed group draws neither.
static El* RenderTools(const DockTabGroup* g) {
    Ctx* cx = g->cx;
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    DockState* s = g->state.Get(cx);
    El* row = Div(a)->FlexRow()->ItemsCenter()->Shrink0()->Gap(4);
    int activeIx = DockGroupActiveIx(g);
    if (g->collapsed || activeIx < 0) {
        return row;
    }
    int panelIx = s->nodes[g->node].panel[activeIx];
    const DockPanelDef& def = s->panels[panelIx];
    bool zoomed = s->zoomPanel == panelIx;
    // Panel::toolbar_buttons precede the skin's own controls.
    if (def.toolbarButtons) {
        if (El* buttons = def.toolbarButtons(cx, def.data)) {
            row->Child(buttons);
        }
    }
    // `zoomable_toolbar_visible`: a zoomed panel always shows the way back
    // out, and Zoom In is on the bar only for Toolbar and Both.
    if (zoomed || DockPanelControlToolbar(def.zoomable)) {
        row->Child(DockBindZoom(
            g, panelIx,
            Div(a)
                ->Pad(4)
                ->Radius(th.radius * 0.5f)
                ->HoverBg(th.tokens.secondary)
                ->Child(IconEl(a,
                               zoomed ? IconName::Minimize : IconName::Maximize,
                               14)
                            ->Fg(th.mutedFg))));
    }
    // The menu button: the same two actions, where a narrow bar can still
    // reach them.
    Str menuId = StrDup(a, fmt("menu-%d", g->node));
    component::PopupMenu* menu = component::PopupMenu::New(cx, menuId);
    if (def.dropdownMenu) {
        def.dropdownMenu(cx, def.data, menu);
        // PopupMenu ignores a leading separator and coalesces repeats, which
        // keeps the default panel's menu clean while separating real entries.
        menu->Separator();
    }
    menu->Menu(zoomed ? Tr("Dock.Zoom Out") : Tr("Dock.Zoom In"));
    if (!DockPanelControlMenu(def.zoomable) && !zoomed) {
        menu->Disabled(true);
    }
    // `closable`: the last panel of a dock has no Close, so a dock cannot be
    // emptied by hand.
    if (def.closable && !DockIsLastPanel(s, g->node) &&
        !DockNodeLocked(s, g->node)) {
        menu->Separator()->Menu(Tr("Dock.Close"));
    }
    if (PopupMenuState* ms = menu->state.Get(cx)) {
        // The menu hands its listener the row that was taken, so the node
        // travels the only other way it can: the group that has the menu open
        // says so as it builds it.
        ms->onConfirm = ListenTo(g->state, &DockState::OnMenuItem);
        DockGroupOpenMenu(g, ms->open);
    }
    row->Child(component::DropdownMenu::New(cx, menuId)
                   ->Trigger(component::Button::New(cx, menuId)
                                 ->Icon(IconName::Ellipsis)
                                 ->Ghost()
                                 ->Compact()
                                 ->WithSize(UiSize::XSmall)
                                 ->TabStop(false)
                                 ->IntoEl())
                   ->Menu(menu)
                   ->AnchorRight()
                   ->IntoEl());
    return row;
}

// The single-panel branch of render_title_bar: with PanelStyle::Auto — the
// default — a group showing one panel is a plain 30-DIP title row with no tab
// chrome at all, and the title itself is what a drag picks up.
static El* RenderTitleRow(const DockTabGroup* g) {
    Ctx* cx = g->cx;
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    int activeIx = DockGroupActiveIx(g);
    const DockPanelDef* def = DockGroupPanel(g, activeIx);
    if (!def) {
        return Div(a);
    }
    bool leading = HasLeadingToggles(g);
    bool trailing = DockGroupHasToggle(g, DockPlacement::Right);
    TitleStyle titleStyle;
    bool hasTitleStyle = PanelTitleStyle(cx, def, &titleStyle);
    El* row = Div(a)
                  ->FlexRow()
                  ->ItemsCenter()
                  ->JustifyBetween()
                  ->W(kFill)
                  ->H(30)
                  ->PadY(8)
                  ->PadL(leading ? 8.f : 12.f)
                  ->PadR(8);
    if (hasTitleStyle) {
        row->Bg(titleStyle.background)->Fg(titleStyle.foreground);
    }
    if (leading) {
        row->Child(RenderToggles(g, false));
    }
    // No line height of its own: `rems(1.0)` is exactly the 16px font size, so
    // the line box has no room for a descender and an `Agent` or a `Properties`
    // loses the tail of its letters wherever the wrapper clips. The row's own
    // 30px and its centring are what place the title.
    Rgba titleColor = hasTitleStyle ? titleStyle.foreground : th.foreground;
    El* title =
        Div(a)
            ->Flex1()
            ->MinW(64)
            ->ClipX()
            ->Fg(titleColor)
            ->Child(PanelTitle(cx, def, titleColor)->Font(14)->Truncate());
    row->Child(DockBindTitleDrag(g, activeIx, title));
    if (!g->collapsed && def->titleSuffix) {
        if (El* suffix = def->titleSuffix(cx, def->data)) {
            row->Child(suffix->Shrink0());
        }
    }
    El* tools = Div(a)->FlexRow()->ItemsCenter()->Shrink0()->Gap(4);
    tools->Child(RenderTools(g));
    if (trailing) {
        tools->Child(RenderToggles(g, true));
    }
    row->Child(tools);
    return row;
}

// TabGroupRenderer::render_tab_bar — the tab bar, or the plain title row a
// lone panel gets.
static El* SkinTabBar(Ctx* cx, void*, const DockTabGroup* g) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    DockState* s = g->state.Get(cx);
    int activeIx = DockGroupActiveIx(g);

    // `visible_panels.len() == 1 && panel_style == PanelStyle::default()`.
    if (DockVisibleCount(s, g->node) == 1 &&
        s->panelStyle == DockPanelStyle::Auto) {
        const DockPanelDef* def = DockGroupPanel(g, activeIx);
        if (def && !def->titleBar) {
            return nullptr;
        }
        return RenderTitleRow(g);
    }

    El* bar = Div(a)
                  ->FlexRow()
                  ->ItemsCenter()
                  ->W(kFill)
                  ->H(kDockTabBarH)
                  ->Bg(th.tokens.tabBar)
                  ->BorderB(1, th.border);
    if (HasLeadingToggles(g)) {
        bar->Child(RenderToggles(g, false)->PadX(8));
    }
    // TabBar::track_scroll: a row of tabs wider than the bar scrolls sideways
    // rather than being squeezed, and the wheel over it moves it.
    El* strip = DockBindTabStrip(g, Div(a)
                                        ->FlexRow()
                                        ->ItemsCenter()
                                        ->Flex1()
                                        ->H(kFill)
                                        ->MinW(0)
                                        ->ClipX()
                                        ->HideScrollbar());
    int count = DockGroupCount(g);
    for (int i = 0; i < count; i++) {
        const DockPanelDef* def = DockGroupPanel(g, i);
        // A hidden panel has no tab at all.
        if (!def->visible) {
            continue;
        }
        // "Always not show active tab style, if the panel is collapsed".
        bool on = i == activeIx && !g->collapsed;
        // A tab keeps its own width: a row too wide for the bar scrolls
        // rather than squeezing its labels.
        El* tab = Div(a)
                      ->FlexRow()
                      ->ItemsCenter()
                      ->Shrink0()
                      ->Gap(4)
                      ->PadX(10)
                      ->H(kFill)
                      ->Bg(on ? th.tabActiveBg : th.tabBar);
        if (i > 0) {
            tab->BorderL(1, th.border);
        }
        DockBindTab(g, i, tab);
        // `.drag_over::<DragPanel>(|this, ..| this.border_l_2()
        // .border_color(cx.theme().drag_border))`: the tab a drop would land
        // before is marked down its left edge, and the mark is a refinement
        // the element carries rather than a question the bar asks the window
        // while it builds.
        if (DockGroupDroppable(g)) {
            tab->DragOver(kDockPanelDrag, StateStyle()
                                              .BorderL(2, th.dragBorder));
        }
        // Tab::new().label(..): a tab's label is the tab bar's own size,
        // which is text_sm at Medium. `tab_name` is the short form a panel
        // offers for the bar; the title is the fallback.
        Rgba labelColor = on ? th.tabActiveFg : th.tabFg;
        if (def->tabName.s) {
            tab->Child(TextEl(a, def->tabName)
                           ->Font(14)
                           ->Fg(labelColor)
                           ->LineHeight(1.f));
        } else {
            tab->Fg(labelColor)
                ->Child(
                    PanelTitle(cx, def, labelColor)->Font(14)->LineHeight(1.f));
        }
        if (def->closable && !DockIsLastPanel(s, g->node) &&
            !DockNodeLocked(s, g->node)) {
            tab->Child(DockBindClose(
                g, i,
                Div(a)
                    ->Pad(2)
                    ->Radius(th.radius * 0.5f)
                    ->HoverBg(th.tokens.secondary)
                    ->Child(IconEl(a, IconName::X, 12)->Fg(th.mutedFg))));
        }
        strip->Child(tab);
    }
    // last_empty_space: the run of bar past the last tab, which takes a drop
    // as "put it at the end" and lights up while a panel is over it.
    El* rest = DockBindTabRest(g, Div(a)->Flex1()->H(kFill)->MinW(64));
    if (DockGroupDroppable(g)) {
        rest->DragOver(kDockPanelDrag, StateStyle().Bg(th.tokens.dropTarget));
    }
    strip->Child(rest);
    bar->Child(strip);
    // `.when(!self.collapsed, |this| this.suffix(..))`: the active panel's own
    // bar content, then the group's tools, then the right dock's toggle.
    if (!g->collapsed) {
        const DockPanelDef* def = DockGroupPanel(g, activeIx);
        if (def && def->titleSuffix) {
            if (El* suffix = def->titleSuffix(cx, def->data)) {
                bar->Child(suffix->Shrink0());
            }
        }
        bar->Child(RenderTools(g));
        if (DockGroupHasToggle(g, DockPlacement::Right)) {
            bar->Child(RenderToggles(g, true)->PadX(8));
        }
    }
    return bar;
}

static El* SkinFrame(Ctx* cx, void*) {
    return Div(cx->a)->Bg(ThemeNow(cx->app).tokens.background);
}

// content_frame: the fill, the clip and the collapsed-group exception are
// base's; the padding is this skin's, and is the only reason this hook is
// implemented.
static El* SkinTabContent(Ctx* cx, void*, const DockTabGroup* g) {
    El* frame = Div(cx->a)
                    ->Id(StrL("active-panel"))
                    ->Bg(ThemeNow(cx->app).tokens.background);
    const DockPanelDef* active = DockGroupPanel(g, DockGroupActiveIx(g));
    if (DockGroupCount(g) > 1 && (!active || active->innerPadding)) {
        frame->PadT(8);
    }
    return frame;
}

// render_split_handle: base keeps the four-DIP grab, the cursor and the drag;
// all this says is what it looks like under the pointer.
static El* SkinSplitHandle(Ctx* cx, void*, const DockHandleCtx* h) {
    // `div().bg(bg_color).group_hover("handle", |this| this.bg(bg_color))`:
    // the drag colours the line, and so does the pointer being anywhere in
    // the grab area around it -- which is the group base put on the handle.
    El* e = Div(cx->a)->SizeFull()->GroupHoverBg(ThemeNow(cx->app).border);
    if (h->active) {
        e->Bg(ThemeNow(cx->app).border);
    }
    return e;
}

// render_dock: the strip on one Dock's inner edge, and the rule beside it.
// No box here any more. A dock's extent is structural, so base's RenderDock
// applies DockFrame around whatever this returns -- which also means a skin
// that draws no chrome still gets a dock the right shape. This adds the edge
// you drag and nothing else; a shut bottom dock keeps its tab bar and loses
// the strip, and a shut side dock never reaches here.
static El* SkinDock(Ctx* cx, void*, const DockCtx* d, El* content) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    if (d->placement == DockPlacement::Bottom) {
        El* dock = Div(a)->FlexCol()->SizeFull()->BorderT(1, th.border);
        if (d->open) {
            dock->Child(DockBindResizeStrip(d, Div(a)
                                                   ->W(kFill)
                                                   ->H(kDockHandleW)
                                                   ->Shrink0()
                                                   ->HoverBg(th.border)));
        }
        dock->Child(Div(a)->FlexCol()->Flex1()->W(kFill)->Child(content));
        return dock;
    }
    if (!d->open) {
        return content;
    }
    bool left = d->placement == DockPlacement::Left;
    El* dock = Div(a)->FlexRow()->SizeFull();
    El* strip = DockBindResizeStrip(
        d, Div(a)->H(kFill)->W(kDockHandleW)->Shrink0()->HoverBg(th.border));
    El* body = Div(a)->FlexCol()->Flex1()->H(kFill)->Child(content);
    if (left) {
        dock->Child(body->BorderR(1, th.border))->Child(strip);
    } else {
        dock->Child(strip)->Child(body->BorderL(1, th.border));
    }
    return dock;
}

static El* SkinDropIndicator(Ctx* cx, void*, Bounds) {
    return Div(cx->a)
        ->Bg(BackgroundOpacity(ThemeNow(cx->app).tokens.primary, 0.2f));
}

// TabPanel::render_drag_panel: w_24, py_1, px_3, a border, the active tab's
// surface, and 0.75 of the opacity. Base puts it under the pointer.
static El* SkinDragPreview(Ctx* cx, void*, const DockPanelDef* def) {
    return DragPanelPreview::New(cx, def)->IntoEl();
}

static const DockRenderer& ThemedRenderer() {
    static const DockRenderer r = {
        nullptr, SkinFrame, nullptr, nullptr, SkinSplitHandle, SkinDock,
        nullptr, SkinTabContent, SkinTabBar,
        // render_empty: the themed skin draws nothing for a group with
        // nothing to show, which is what base's own default did.
        nullptr, SkinDropIndicator, SkinDragPreview};
    return r;
}

const DockRenderer* DockSkin::Renderer() const {
    return &ThemedRenderer();
}

El* DockArea::IntoEl() {
    const DockRenderer* renderer = skin ? skin->Renderer() : &ThemedRenderer();
    return gpui::DockArea::New(cx, id, state, renderer);
}

} // namespace component
} // namespace gpui
