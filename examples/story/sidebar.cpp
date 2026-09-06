#include "Story.h"

// The two sidebar groups, in the order the Rust story lists them, and the
// children each Platform item opens onto.
struct SidebarItemDef {
    const char* label;
    IconName icon;
};

static const SidebarItemDef kPlatform[] = {
    {"Playground", IconName::SquareTerminal},
    {"Models", IconName::Bot},
    {"Documentation", IconName::BookOpen},
    {"Settings", IconName::Settings2},
};
static const SidebarItemDef kProjects[] = {
    {"Design Engineering", IconName::Frame},
    {"Sales and Marketing", IconName::ChartPie},
    {"Travel", IconName::Map},
};
// Item::items(): one list per Platform item. Models' third child is the
// disabled one.
static const char* kSubs[4][4] = {
    {"History", "Starred", "Settings", nullptr},
    {"Genesis", "Explorer", "Quantum", nullptr},
    {"Introduction", "Get Started", "Tutorial", "Changelog"},
    {"General", "Team", "Billing", "Limits"},
};

enum {
    SidebarOptIcon = 600,
    SidebarOptOffcanvas,
    SidebarOptFixed,
    SidebarOptRight,
    SidebarOptClickToOpen,
    SidebarOptDynamic
};

struct SidebarStory {
    int active = 0;
    int activeSub = -1;
    bool optionsOpen = false;
    bool historySwitch = false;
    int collapsible = 0; // Icon
    bool collapsed = false;
    bool rightSide = false;
    bool clickToOpen = false;
    bool dynamicChildren = false;

    static El* Render(SidebarStory* self, Ctx* cx);
};

static void SidebarPick(SidebarStory* self, Ctx* cx, const ClickEvent*,
                        intptr_t ix) {
    self->active = (int)ix;
    self->activeSub = -1;
    Notify(cx);
}
// A sub-item click carries both which item and which child, which is the pair
// Rust's SubItem::handler captures.
static void SidebarPickSub(SidebarStory* self, Ctx* cx, const ClickEvent*,
                           intptr_t v) {
    self->active = (int)(v >> 8);
    self->activeSub = (int)(v & 0xff);
    Notify(cx);
}
static void ToggleSidebarOptions(SidebarStory* self, Ctx* cx,
                                 const ClickEvent*) {
    self->optionsOpen = !self->optionsOpen;
    Notify(cx);
}
static void ToggleCollapsed(SidebarStory* self, Ctx* cx, const ClickEvent*) {
    self->collapsed = !self->collapsed;
    Notify(cx);
}
static void SidebarOptionAct(SidebarStory* self, Ctx* cx, const ClickEvent*,
                             intptr_t act) {
    switch (act) {
        case SidebarOptIcon:
            self->collapsible = 0;
            break;
        case SidebarOptOffcanvas:
            self->collapsible = 1;
            break;
        case SidebarOptFixed:
            self->collapsible = 2;
            break;
        case SidebarOptRight:
            self->rightSide = !self->rightSide;
            break;
        case SidebarOptClickToOpen:
            self->clickToOpen = !self->clickToOpen;
            break;
        case SidebarOptDynamic:
            self->dynamicChildren = !self->dynamicChildren;
            break;
        default:
            // ToolbarCloseAll, and anything else that only wants the menu
            // shut, lands here and changes nothing else.
            break;
    }
    self->optionsOpen = false;
    Notify(cx);
}
static void ToggleHistory(SidebarStory* self, Ctx* cx, const ClickEvent*) {
    self->historySwitch = !self->historySwitch;
    Notify(cx);
}

static const component::SidebarCollapsible kCollapsibles[3] = {
    component::SidebarCollapsible::Icon,
    component::SidebarCollapsible::Offcanvas,
    component::SidebarCollapsible::None};

El* SidebarStory::Render(SidebarStory* self, Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    Listener pick = Listen(cx, &SidebarPick);
    Listener pickSub = Listen(cx, &SidebarPickSub);
    component::SidebarCollapsible collapsible =
        kCollapsibles[self->collapsible];
    bool iconCollapsed =
        self->collapsed && collapsible == component::SidebarCollapsible::Icon;

    // .when(side.is_right(), flex_row_reverse()): the sidebar is always the
    // first child and the row runs the other way when it sits on the right,
    // rather than the two being appended in a different order.
    El* frame = Div(a)
                    ->FlexRow()
                    ->W(kFill)
                    ->H(WindowSize(cx->win).dipH - 190)
                    ->Radius(th.radius)
                    ->Border(1, th.border);

    // The header: the company the workspace belongs to.
    El* brand = Div(a)->FlexRow()->W(kFill)->Gap(8)->ItemsCenter();
    brand->Child(Div(a)
                     ->W(32)
                     ->H(32)
                     ->Shrink0()
                     ->ItemsCenter()
                     ->JustifyCenter()
                     ->Radius(th.radius)
                     ->Bg(th.tokens.success)
                     ->Child(IconEl(a, IconName::GalleryVerticalEnd, 16)
                                 ->Fg(th.successFg)));
    if (!iconCollapsed) {
        El* company = Div(a)->FlexCol()->Flex1();
        company->Child(StoryTxt(cx, StrL("Company Name"), 14, th.foreground)
                           ->LineHeight(1.25f));
        company->Child(StoryTxt(cx, StrL("Enterprise"), 12, th.foreground)
                           ->LineHeight(1.25f));
        brand->Child(company);
        brand->Child(IconEl(a, IconName::ChevronsUpDown, 16)
                         ->Fg(th.foreground)
                         ->Shrink0());
    }

    // The story's two Styled demonstrations: the active item outlines itself
    // (`.when(is_active, |this| this.border_1().border_color(Hsla::white()))`)
    // and every sub item's label is `red_500()` through `label_style`.
    Style outlined = {};
    outlined.border = 1;
    outlined.borderColor = Rgb(0xff, 0xff, 0xff);
    Style redLabel = {};
    redLabel.color = Rgb(0xef, 0x44, 0x44);

    component::SidebarMenu* platform = component::SidebarMenu::New(cx);
    for (int i = 0; i < 4; i++) {
        bool isActive = self->active == i && self->activeSub < 0;
        component::SidebarMenuItem* item =
            component::SidebarMenuItem::New(cx, Str(kPlatform[i].label))
                ->Icon(kPlatform[i].icon)
                ->Active(isActive)
                ->DefaultOpen(i == 0)
                ->ClickToOpen(self->clickToOpen)
                ->OnClick(ListenerArg(pick, i));
        if (isActive) {
            item->Refine(outlined, StyleFieldBorder | StyleFieldBorderColor);
        }
        for (int j = 0; j < 4 && kSubs[i][j]; j++) {
            component::SidebarMenuItem* sub =
                component::SidebarMenuItem::New(cx, Str(kSubs[i][j]))
                    ->Active(self->active == i && self->activeSub == j)
                    // SubItem::Quantum is the disabled one.
                    ->Disabled(i == 1 && j == 2)
                    ->LabelStyle(redLabel, StyleFieldColor)
                    ->OnClick(ListenerArg(pickSub, (i << 8) | j));
            if (i == 0 && j == 0) {
                // The first child carries a switch, as the Rust story shows.
                sub->Suffix(component::Switch::New(cx, StrL("sidebar-history"))
                                ->Checked(self->historySwitch)
                                ->WithSize(UiSize::XSmall)
                                ->OnClick(Listen(cx, &ToggleHistory))
                                ->IntoEl());
            }
            item->Child(sub);
        }
        platform->Child(item);
    }

    component::SidebarMenu* projects = component::SidebarMenu::New(cx);
    for (int i = 0; i < 3; i++) {
        component::SidebarMenuItem* item =
            component::SidebarMenuItem::New(cx, Str(kProjects[i].label))
                ->Icon(kProjects[i].icon)
                ->Active(self->active == 4 + i && self->activeSub < 0)
                // Item::Travel is the disabled one.
                ->Disabled(i == 2)
                ->ClickToOpen(self->clickToOpen)
                ->OnClick(ListenerArg(pick, 4 + i));
        if (i == 0) {
            item->Suffix(component::Badge::New(cx)
                             ->Dot()
                             ->Child(IconEl(a, IconName::Bell, 16))
                             ->IntoEl());
            if (self->dynamicChildren) {
                // The option that gives an item children it did not have.
                item->DefaultOpen(true);
                item->Child(
                    component::SidebarMenuItem::New(cx, StrL("Child A")));
                item->Child(
                    component::SidebarMenuItem::New(cx, StrL("Child B")));
            }
        } else if (i == 1) {
            item->Suffix(IconEl(a, IconName::Settings2, 16));
            // context_menu(..): the row's own right-click actions, which the
            // Rust story hangs off this item too.
            item->ContextMenu(
                component::PopupMenu::New(cx, StrL("sidebar-project-menu"))
                    ->Menu(StrL("Rename"))
                    ->Menu(StrL("Duplicate"))
                    ->Separator()
                    ->Menu(StrL("Delete")));
        }
        projects->Child(item);
    }

    El* user = Div(a)->FlexRow()->W(kFill)->Gap(8)->ItemsCenter();
    user->Child(IconEl(a, IconName::CircleUser, 16));
    if (!iconCollapsed) {
        user->Child(Div(a)->Flex1()->Child(
            StoryTxt(cx, StrL("Jason Lee"), 14, th.foreground)));
        user->Child(IconEl(a, IconName::ChevronsUpDown, 16));
    }

    El* sidebar = component::Sidebar::New(cx, StrL("sidebar-story"))
                      ->WithSide(self->rightSide ? Side::Right : Side::Left)
                      ->Collapsible(collapsible)
                      ->Collapsed(self->collapsed)
                      ->W(220)
                      ->Header(component::SidebarHeader::New(cx)->Child(brand))
                      ->Footer(component::SidebarFooter::New(cx)->Child(user))
                      ->Child(component::SidebarGroup::New(cx, StrL("Platform"))
                                  ->Child(platform))
                      ->Child(component::SidebarGroup::New(cx, StrL("Projects"))
                                  ->Child(projects))
                      ->IntoEl();
    if (self->rightSide) {
        frame->FlexRowReverse();
    }
    frame->Child(sidebar);

    // The content pane: breadcrumb, heading with the Options menu, metric
    // cards and the activity list.
    El* content = Div(a)->FlexCol()->Flex1()->H(kFill)->Pad(16)->Gap(16);
    El* crumbs = Div(a)->FlexRow()->W(kFill)->Gap(8)->ItemsCenter();
    // .when(side.is_right() && collapsible != None, flex_row_reverse()
    // .justify_between()): the toggle button leads the row on the side the
    // sidebar is on.
    if (self->rightSide && collapsible != component::SidebarCollapsible::None) {
        crumbs->FlexRowReverse()->JustifyBetween();
    }
    crumbs->Child(
        component::SidebarToggleButton::New(cx)
            ->WithSide(self->rightSide ? Side::Right : Side::Left)
            ->Collapsed(self->collapsed &&
                        collapsible != component::SidebarCollapsible::None)
            ->OnClick(Listen(cx, &ToggleCollapsed))
            ->IntoEl());
    crumbs->Child(component::Separator::Vertical(cx)->IntoEl()->H(16));
    crumbs->Child(component::Breadcrumb::New(cx)
                      ->Child(StrL("Breadcrumb"))
                      ->Child(StrL("Home"))
                      ->Child(StrL("Playground"))
                      ->IntoEl());
    content->Child(crumbs);

    El* headRow =
        Div(a)->FlexRow()->W(kFill)->Gap(16)->ItemsStart()->JustifyBetween();
    El* headText = Div(a)->FlexCol()->Gap(4);
    const char* activeLabel = self->active < 4
                                  ? kPlatform[self->active].label
                                  : kProjects[self->active - 4].label;
    headText
        ->Child(StoryTxt(cx, Str(activeLabel), 24, th.foreground)->Semibold());
    headText->Child(StoryTxt(
        cx, StrL("A quick view of your workspace activity."), 14, th.mutedFg));
    headRow->Child(headText);
    El* optGroup = StoryToolbarGroup(cx);
    StoryToolbarOpt opts[6] = {
        {"Icon mode", self->collapsible == 0, SidebarOptIcon},
        {"Offcanvas mode", self->collapsible == 1, SidebarOptOffcanvas},
        {"Fixed mode", self->collapsible == 2, SidebarOptFixed},
        {"Right Side", self->rightSide, SidebarOptRight},
        {"Click to Open", self->clickToOpen, SidebarOptClickToOpen},
        {"Dynamic Children", self->dynamicChildren, SidebarOptDynamic},
    };
    optGroup->Child(StoryToolbarDropdown(
        cx, StrL("sidebar-options"), StrL("Options"), self->optionsOpen,
        Listen(cx, &ToggleSidebarOptions), opts, 6,
        Listen(cx, &SidebarOptionAct)));
    headRow->Child(optGroup);
    content->Child(headRow);

    struct Metric {
        const char* label;
        const char* value;
        const char* detail;
    };
    static const Metric kMetrics[] = {
        {"Active projects", "12", "+2 this week"},
        {"Team members", "28", "4 online"},
        {"Tasks completed", "84%", "+6% this month"},
    };
    El* metrics = Div(a)->FlexRow()->W(kFill)->Gap(12);
    for (int i = 0; i < 3; i++) {
        El* card = Div(a)
                       ->FlexCol()
                       ->Flex1()
                       ->Gap(8)
                       ->Pad(16)
                       ->Radius(th.radiusLg)
                       ->Border(1, th.border);
        card->Child(StoryTxt(cx, Str(kMetrics[i].label), 16, th.mutedFg));
        card->Child(StoryTxt(cx, Str(kMetrics[i].value), 24, th.foreground)
                        ->Semibold());
        card->Child(StoryTxt(cx, Str(kMetrics[i].detail), 12, th.mutedFg));
        metrics->Child(card);
    }
    content->Child(metrics);

    struct Activity {
        IconName icon;
        const char* title;
        const char* time;
    };
    static const Activity kActivity[] = {
        {IconName::CircleCheck, "Design review completed", "12 minutes ago"},
        {IconName::File, "Project brief updated", "1 hour ago"},
        {IconName::CircleUser, "Maya joined the workspace", "3 hours ago"},
    };
    El* activity = Div(a)
                       ->FlexCol()
                       ->W(kFill)
                       ->Flex1()
                       ->Radius(th.radiusLg)
                       ->Border(1, th.border);
    El* actHead = Div(a)
                      ->FlexRow()
                      ->W(kFill)
                      ->PadX(16)
                      ->PadY(8)
                      ->ItemsCenter()
                      ->JustifyBetween();
    actHead->Child(StoryTxt(cx, StrL("Recent activity"), 16, th.foreground)
                       ->Medium());
    actHead->Child(StoryTxt(cx, StrL("Today"), 12, th.mutedFg));
    activity->Child(actHead);
    activity->Child(component::Separator::Horizontal(cx)->IntoEl());
    for (int i = 0; i < 3; i++) {
        El* row = Div(a)
                      ->FlexRow()
                      ->W(kFill)
                      ->PadX(16)
                      ->PadY(12)
                      ->Gap(12)
                      ->ItemsCenter();
        row->Child(
            Div(a)
                ->W(32)
                ->H(32)
                ->ItemsCenter()
                ->JustifyCenter()
                ->Radius(16)
                ->Bg(th.tokens.muted)
                ->Child(IconEl(a, kActivity[i].icon, 16)->Fg(th.foreground)));
        El* col = Div(a)->FlexCol()->Flex1()->Gap(2);
        col->Child(StoryTxt(cx, Str(kActivity[i].title), 14, th.foreground));
        col->Child(StoryTxt(cx, Str(kActivity[i].time), 12, th.mutedFg));
        row->Child(col);
        activity->Child(row);
    }
    content->Child(activity);
    frame->Child(content);

    El* page = Div(a)->FlexCol()->W(kFill);
    page->Child(frame);
    return page;
}

STORY_PAGE(StorySidebar, SidebarStory);
