/* Ported from crates/ui/src/sidebar/mod.rs.
 *
 * `SidebarLayout::new` is what a collapsible mode and a collapsed flag come
 * to: which rendering the rows take, what the wrapper does with its width, and
 * which end the content is pinned to. Rust's own tests are the five cases
 * below. */

#include "Test.h"

using namespace gpui::component;

static SidebarLayout Layout(SidebarCollapsible collapsible, bool collapsed,
                            float expandedWidth, Side side) {
    return SidebarLayoutFor(collapsible, collapsed, expandedWidth, side);
}

static void IconCollapsedUsesTheIconWidth() {
    SidebarLayout l = Layout(SidebarCollapsible::Icon, true, 240, Side::Left);
    utassert(l.iconCollapsed);
    utassert(!l.offcanvasCollapsed);
    utassert(!l.alignChildToEnd);
    utassert(l.wrapper == SidebarWrapperKind::Animated);
    utassertnear(l.wrapperWidth, kSidebarCollapsedWidth);
}

static void BoolCollapsibleRemainsBackwardCompatible() {
    App app = {};
    component::Init(&app);
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.app = &app;
    cx.a = a;
    Sidebar* icon = Sidebar::New(&cx, StrL("icon"))->Collapsible(true);
    Sidebar* fixed = Sidebar::New(&cx, StrL("fixed"))->Collapsible(false);
    utassert(icon->collapsible == SidebarCollapsible::Icon);
    utassert(fixed->collapsible == SidebarCollapsible::None);
    AppGlobalClear(&app);
    ArenaDelete(a);
}

static void IconExpandedUsesTheExpandedWidth() {
    SidebarLayout l = Layout(SidebarCollapsible::Icon, false, 240, Side::Left);
    utassert(!l.iconCollapsed);
    utassert(!l.offcanvasCollapsed);
    utassert(l.wrapper == SidebarWrapperKind::Animated);
    utassertnear(l.wrapperWidth, 240.f);
}

static void AWidthThatIsNotInPixelsLeavesTheWrapperAlone() {
    // Rust's `None` width: the sidebar sizes itself and the wrapper stays out
    // of the way.
    SidebarLayout l = Layout(SidebarCollapsible::Icon, false, 0, Side::Left);
    utassert(!l.iconCollapsed);
    utassert(!l.offcanvasCollapsed);
    utassert(l.wrapper == SidebarWrapperKind::None);
}

static void NoneIgnoresTheCollapsedFlag() {
    SidebarLayout l = Layout(SidebarCollapsible::None, true, 240, Side::Right);
    utassert(!l.iconCollapsed);
    utassert(!l.offcanvasCollapsed);
    // On the right, the content is pinned to the far end.
    utassert(l.alignChildToEnd);
    utassert(l.wrapper == SidebarWrapperKind::None);
}

static void OffcanvasCollapsesToNothing() {
    SidebarLayout l =
        Layout(SidebarCollapsible::Offcanvas, true, 240, Side::Left);
    utassert(!l.iconCollapsed);
    utassert(l.offcanvasCollapsed);
    // Offcanvas flips which end the content is pinned to: on the left, it
    // holds the right edge as the width goes to nothing.
    utassert(l.alignChildToEnd);
    utassert(l.wrapper == SidebarWrapperKind::Animated);
    utassertnear(l.wrapperWidth, 0.f);

    SidebarLayout open =
        Layout(SidebarCollapsible::Offcanvas, false, 240, Side::Left);
    utassert(!open.offcanvasCollapsed);
    utassert(open.wrapper == SidebarWrapperKind::Animated);
    utassertnear(open.wrapperWidth, 240.f);

    // With no width of its own there is nothing to animate, so a collapsed
    // one is simply zero wide.
    SidebarLayout noWidth =
        Layout(SidebarCollapsible::Offcanvas, true, 0, Side::Left);
    utassert(noWidth.wrapper == SidebarWrapperKind::Static);
    utassertnear(noWidth.wrapperWidth, 0.f);
    SidebarLayout noWidthOpen =
        Layout(SidebarCollapsible::Offcanvas, false, 0, Side::Left);
    utassert(noWidthOpen.wrapper == SidebarWrapperKind::None);

    SidebarLayout right =
        Layout(SidebarCollapsible::Offcanvas, true, 240, Side::Right);
    utassert(!right.alignChildToEnd);
}

namespace {
struct CustomSidebarItem {
    int renders = 0;
    bool collapsed = false;
    Str id = {};
};
} // namespace

static El* RenderCustomSidebarItem(void* data, Ctx* cx, Str id,
                                   bool collapsed) {
    CustomSidebarItem* item = (CustomSidebarItem*)data;
    item->renders++;
    item->collapsed = collapsed;
    item->id = id;
    return Div(cx->a)->Id(id)->H(20);
}

static void SidebarItemAllowsGenericNestedContent() {
    App app = {};
    component::Init(&app);
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.app = &app;
    cx.a = a;

    CustomSidebarItem nested = {};
    SidebarGroup* group =
        SidebarGroup::New(&cx, StrL("Custom"))
            ->Child(SidebarItem::New(&nested, &RenderCustomSidebarItem));
    El* groupEl = group->IntoEl(StrL("group"));
    utassert(groupEl != nullptr);
    utassert(nested.renders == 1);
    utassert(!nested.collapsed);
    utassert(base::StrEq(nested.id, StrL("0")));

    CustomSidebarItem direct = {};
    Sidebar* sidebar =
        Sidebar::New(&cx, StrL("generic-sidebar"))
            ->Collapsed(true)
            ->Child(SidebarItem::New(&direct, &RenderCustomSidebarItem));
    El* wrapper = sidebar->IntoEl();
    utassert(wrapper != nullptr);
    utassert(direct.renders == 1);
    utassert(direct.collapsed);
    utassert(sidebar->content.len == 1);

    AppGlobalClear(&app);
    ArenaDelete(a);
}

static int ChildCount(El* e) {
    int count = 0;
    for (El* child = e ? e->first : nullptr; child; child = child->next) {
        count++;
    }
    return count;
}

static void HeaderFooterAndMenuRetainTheirBuilderSurface() {
    App app = {};
    component::Init(&app);
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.app = &app;
    cx.a = a;

    Style refined = {};
    refined.pad = EdgesAll(3);
    SidebarHeader* header = SidebarHeader::New(&cx)
                                ->Selected(true)
                                ->Collapsed(true)
                                ->Refine(refined, StyleFieldPad);
    SidebarFooter* footer = SidebarFooter::New(&cx)->Selected(true);
    for (int i = 0; i < 40; i++) {
        header->Child(Div(a));
        footer->Child(Div(a));
    }
    El* headerEl = header->IntoEl();
    El* footerEl = footer->IntoEl();
    utassert(header->children.len == 40);
    utassert(footer->children.len == 40);
    utassert(ChildCount(headerEl) == 40);
    utassert(ChildCount(footerEl) == 1);
    utassert(ChildCount(footerEl ? footerEl->first : nullptr) == 40);
    utassertnear(headerEl->style.pad.left, 3);
    utassert(headerEl->style.hasBg);
    utassert(footerEl->style.hasBg);

    Style menuStyle = {};
    menuStyle.gapX = 17;
    menuStyle.gapY = 19;
    El* menuEl = SidebarMenu::New(&cx)
                     ->Refine(menuStyle, StyleFieldGap)
                     ->Child(SidebarMenuItem::New(&cx, StrL("row")))
                     ->IntoEl(StrL("menu"));
    utassertnear(menuEl->style.gapX, 17);
    utassertnear(menuEl->style.gapY, 19);

    AppGlobalClear(&app);
    ArenaDelete(a);
}

static void CollapsedTooltipRequiresAnIconAndContentScrolls() {
    App app = {};
    component::Init(&app);
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.app = &app;
    cx.a = a;

    // Set collapsed before rendering, as SidebarItem::Render does.
    SidebarMenuItem* iconItem = SidebarMenuItem::New(&cx, StrL("Projects"))
                                    ->Icon(IconName::Folder);
    iconItem->collapsed = true;
    El* iconRow = iconItem->IntoEl(StrL("collapsed-icon"))->first;
    SidebarMenuItem* plainItem = SidebarMenuItem::New(&cx, StrL("Plain"));
    plainItem->collapsed = true;
    El* plainRow = plainItem->IntoEl(StrL("collapsed-plain"))->first;
    utassert(iconRow && base::StrEq(iconRow->style.tooltip, StrL("Projects")));
    utassert(plainRow && !plainRow->style.tooltip.s);
    utassert(iconRow && base::StrEq(iconRow->id, StrL("item")));
    utassert(plainRow && base::StrEq(plainRow->id, StrL("item")));
    El* activeRow = SidebarMenuItem::New(&cx, StrL("Active"))
                        ->Active(true)
                        ->IntoEl(StrL("active"))
                        ->first;
    utassert(activeRow && activeRow->style.fontMedium);

    // menu.rs after 4bb44c7b: the item implements Styled, and label_style
    // refines the label's box on its own — the row keeps its border while
    // the label alone changes colour.
    Style outlined = {};
    outlined.border = 1;
    outlined.borderColor = Rgb(0xff, 0xff, 0xff);
    Style redLabel = {};
    redLabel.color = Rgb(0xef, 0x44, 0x44);
    El* styledRow =
        SidebarMenuItem::New(&cx, StrL("Styled"))
            ->Refine(outlined, StyleFieldBorder | StyleFieldBorderColor)
            ->LabelStyle(redLabel, StyleFieldColor)
            ->IntoEl(StrL("styled"))
            ->first;
    utassert(styledRow && styledRow->style.border == 1);
    utassert(styledRow && styledRow->style.borderColor.r == 0xff);
    // row > mid > label box > text.
    El* labelBox =
        styledRow && styledRow->first ? styledRow->first->first : nullptr;
    El* labelText = labelBox ? labelBox->first : nullptr;
    utassert(labelBox && labelBox->style.color.r == 0xef);
    utassert(labelText && labelText->style.color.r == 0xef &&
             labelText->style.color.g == 0x44);

    Sidebar* sidebar = Sidebar::New(&cx, StrL("scrolling-sidebar"))
                           ->Collapsible(SidebarCollapsible::None)
                           ->Child(SidebarMenuItem::New(&cx, StrL("Direct")));
    El* root = sidebar->IntoEl();
    El* content = root ? root->first : nullptr;
    El* viewport = content ? content->first : nullptr;
    utassert(root && base::StrEq(root->id, StrL("scrolling-sidebar")));
    utassert(content && content->style.minH == 0);
    utassert(viewport && viewport->style.overflowY == Overflow::Scroll);
    utassert(viewport && viewport->onScroll.IsValid());

    Style fill = {};
    fill.width = kFill;
    fill.borderR = 5;
    fill.pad = EdgesAll(99);
    El* nonPixel =
        Sidebar::New(&cx, StrL("fill-sidebar"))
            ->Refine(fill, StyleFieldWidth | StyleFieldBorderR | StyleFieldPad)
            ->IntoEl();
    utassert(nonPixel && base::StrEq(nonPixel->id, StrL("fill-sidebar")));
    utassertnear(nonPixel->style.width, kFill);
    utassertnear(nonPixel->style.borderR, 5);
    utassertnear(nonPixel->style.pad.left, 0);

    AppGlobalClear(&app);
    ArenaDelete(a);
}

void TestSidebar() {
    TestSuite("sidebar");
    BoolCollapsibleRemainsBackwardCompatible();
    IconCollapsedUsesTheIconWidth();
    IconExpandedUsesTheExpandedWidth();
    AWidthThatIsNotInPixelsLeavesTheWrapperAlone();
    NoneIgnoresTheCollapsedFlag();
    OffcanvasCollapsesToNothing();
    SidebarItemAllowsGenericNestedContent();
    HeaderFooterAndMenuRetainTheirBuilderSurface();
    CollapsedTooltipRequiresAnIconAndContentScrolls();
}
