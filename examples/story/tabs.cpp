#include "Story.h"

// MAX_WIDTHS and the toolbar row that picks one.
static const float kTabMaxWidths[] = {0, 80, 120, 200};
static const char* const kTabMaxWidthLabels[] = {"Unlimited", "80px", "120px",
                                                 "200px"};
static const int kNTabMaxWidths = 4;
enum {
    TabsMenuMaxWidth = 1,
    TabsMenuOptions
};
enum {
    TabsActMoreMenu = 3799,
    TabsActMaxWidth = 3800
};

struct TabsStory {
    int tab = 0;
    int maxWidthIx = 0;
    // TabBar::menu, which the Options dropdown's one row turns on.
    bool menu = false;
    int openMenu = 0;
    // The Dynamic Tabs section keeps its own bar: ids that keep counting up
    // as tabs come and go, and which of them is selected.
    int dynamicIds[12] = {0, 1, 2};
    int dynamicCount = 3;
    int dynamicNext = 3;
    int dynamicTab = 0;
    StoryToolbarState toolbar;

    static El* Render(TabsStory* self, Ctx* cx);
};

static void TabsMenuOpen(TabsStory* self, Ctx* cx, const ClickEvent*,
                         int64_t which) {
    self->openMenu = self->openMenu == (int)which ? 0 : (int)which;
    Notify(cx);
}
static void TabsMenuAct(TabsStory* self, Ctx* cx, const ClickEvent*,
                        int64_t act) {
    if (act >= TabsActMaxWidth) {
        self->maxWidthIx = (int)(act - TabsActMaxWidth);
    } else if (act == TabsActMoreMenu) {
        self->menu = !self->menu;
    } else {
        StoryToolbarApply(&self->toolbar, nullptr, (int)act);
    }
    self->openMenu = 0;
    Notify(cx);
}

static void SetTab(TabsStory* self, Ctx* cx, const ClickEvent*, int64_t ix) {
    self->tab = (int)ix;
    Notify(cx);
}
static void SetDynamicTab(TabsStory* self, Ctx* cx, const ClickEvent*,
                          int64_t ix) {
    self->dynamicTab = (int)ix;
    Notify(cx);
}
static void AddDynamicTab(TabsStory* self, Ctx* cx, const ClickEvent*) {
    if (self->dynamicCount < 12) {
        self->dynamicIds[self->dynamicCount++] = self->dynamicNext++;
    }
    Notify(cx);
}
static void RemoveDynamicTab(TabsStory* self, Ctx* cx, const ClickEvent*) {
    // remove_last_dynamic_tab keeps the last tab: a bar with none of them is
    // not a state the story goes to.
    if (self->dynamicCount > 1) {
        self->dynamicCount--;
    }
    if (self->dynamicTab >= self->dynamicCount) {
        self->dynamicTab = self->dynamicCount - 1;
    }
    if (self->dynamicTab < 0) {
        self->dynamicTab = 0;
    }
    Notify(cx);
}
// The close button on a tab drops that one rather than the last.
static void CloseDynamicTab(TabsStory* self, Ctx* cx, const ClickEvent*,
                            int64_t ix) {
    int i = (int)ix;
    if (i < 0 || i >= self->dynamicCount) {
        return;
    }
    for (int k = i; k + 1 < self->dynamicCount; k++) {
        self->dynamicIds[k] = self->dynamicIds[k + 1];
    }
    self->dynamicCount--;
    if (self->dynamicTab >= self->dynamicCount) {
        self->dynamicTab = self->dynamicCount - 1;
    }
    if (self->dynamicTab < 0) {
        self->dynamicTab = 0;
    }
    Notify(cx);
}

// The Rust story runs the same eight tabs through every variant; Profile is
// disabled in the first bar.
static const char* kTabNames[] = {"Account", "Profile",    "Documents",
                                  "Mail",    "Appearance", "Settings",
                                  "About",   "License"};
static const int kTabCount = 8;

El* TabsStory::Render(TabsStory* self, Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    El* page = Div(a)->FlexCol()->Gap(12)->W(kFill);
    Listener openMenu = Listen(cx, &TabsMenuOpen);
    Listener act = Listen(cx, &TabsMenuAct);
    El* toolbarRow = Div(a)->FlexRow()->W(kFill)->JustifyEnd()->ItemsStart();
    El* group = StoryToolbarGroup(cx);
    StoryToolbarOpt sizes[4] = {
        {"XSmall", self->toolbar.size == UiSize::XSmall, ToolbarSizeXs},
        {"Small", self->toolbar.size == UiSize::Small, ToolbarSizeSm},
        {"Medium", self->toolbar.size == UiSize::Medium, ToolbarSizeMd},
        {"Large", self->toolbar.size == UiSize::Large, ToolbarSizeLg},
    };
    const char* sizeName = self->toolbar.size == UiSize::XSmall  ? "XSmall"
                           : self->toolbar.size == UiSize::Small ? "Small"
                           : self->toolbar.size == UiSize::Large ? "Large"
                                                                 : "Medium";
    group->Child(StoryToolbarDropdown(
        cx, StrL("tabs-size"), StoryFmt(cx, "Size: %s", sizeName),
        self->toolbar.sizeMenuOpen, ListenerArg(openMenu, 0), sizes, 4, act));
    group->Child(StoryToolbarDivider(cx));
    StoryToolbarOpt widths[kNTabMaxWidths];
    for (int i = 0; i < kNTabMaxWidths; i++) {
        widths[i].label = kTabMaxWidthLabels[i];
        widths[i].checked = self->maxWidthIx == i;
        widths[i].act = TabsActMaxWidth + i;
    }
    group->Child(StoryToolbarDropdown(
        cx, StrL("tabs-max-width"),
        StoryFmt(cx, "Max width: %s", kTabMaxWidthLabels[self->maxWidthIx]),
        self->openMenu == TabsMenuMaxWidth,
        ListenerArg(openMenu, TabsMenuMaxWidth), widths, kNTabMaxWidths, act));
    StoryToolbarOpt more[1] = {
        {"More menu", self->menu, TabsActMoreMenu},
    };
    group->Child(StoryToolbarDropdown(cx, StrL("tabs-options"), StrL("Options"),
                                      self->openMenu == TabsMenuOptions,
                                      ListenerArg(openMenu, TabsMenuOptions),
                                      more, 1, act));
    toolbarRow->Child(group);
    page->Child(toolbarRow);
    float maxWidth = kTabMaxWidths[self->maxWidthIx];

    // One bar per variant, all over the same eight tabs and the same
    // selection — which is what the Rust story does. Profile is disabled in
    // the folder, pill and outline bars; the pill and outline ones spell the
    // third tab out in full.
    struct VariantRow {
        const char* title;
        component::TabVariant variant;
        bool disabledProfile;
        bool longLabel;
    };
    static const VariantRow kVariants[] = {
        {"Tabs", component::TabVariant::Tab, true, false},
        {"Underline Tabs", component::TabVariant::Underline, false, false},
        {"Pill Tabs", component::TabVariant::Pill, true, true},
        {"Outline Tabs", component::TabVariant::Outline, true, true},
        {"Segmented Tabs", component::TabVariant::Segmented, false, false},
    };
    static const IconName kSegmentedIcons[3] = {
        IconName::Bot, IconName::Calendar, IconName::Map};
    for (size_t v = 0; v < sizeof(kVariants) / sizeof(kVariants[0]); v++) {
        const VariantRow& row = kVariants[v];
        El* sec = StorySection(cx, row.title, nullptr);
        StorySectionBody(sec)->W(kFill);
        component::TabBar* bar =
            component::TabBar::New(cx, StoryFmt(cx, "tabs-%d", (int)v))
                ->WFill()
                ->Variant(row.variant)
                ->Size(self->toolbar.size)
                ->Menu(self->menu);
        if (maxWidth > 0) {
            bar->MaxWidth(maxWidth);
        }
        if (row.variant == component::TabVariant::Segmented) {
            // The segmented bar is three icon-only tabs and the last four
            // names — no Profile, Documents or Mail.
            for (int i = 0; i < 3; i++) {
                bar->Tab(Str{}, kSegmentedIcons[i]);
            }
            for (int i = 4; i < kTabCount; i++) {
                bar->Tab(Str(kTabNames[i]));
            }
        } else {
            for (int i = 0; i < kTabCount; i++) {
                Str label = (row.longLabel && i == 2)
                                ? StrL("Documents & Files")
                                : Str(kTabNames[i]);
                bar->Tab(label);
            }
        }
        if (row.disabledProfile) {
            bar->Disabled(1);
        }
        // The first bar carries navigation before its tabs and two actions
        // after them.
        if (v == 0) {
            El* pre = Div(a)->FlexRow()->PadX(4);
            pre->Child(component::Button::New(cx, StrL("back"))
                           ->Ghost()
                           ->WithSize(UiSize::XSmall)
                           ->Icon(IconName::ArrowLeft)
                           ->IntoEl());
            pre->Child(component::Button::New(cx, StrL("forward"))
                           ->Ghost()
                           ->WithSize(UiSize::XSmall)
                           ->Icon(IconName::ArrowRight)
                           ->IntoEl());
            El* suf = Div(a)->FlexRow()->PadX(4);
            suf->Child(component::Button::New(cx, StrL("inbox"))
                           ->Ghost()
                           ->WithSize(UiSize::XSmall)
                           ->Icon(IconName::Inbox)
                           ->IntoEl());
            suf->Child(component::Button::New(cx, StrL("more"))
                           ->Ghost()
                           ->WithSize(UiSize::XSmall)
                           ->Icon(IconName::Ellipsis)
                           ->IntoEl());
            bar->Prefix(pre)->Suffix(suf);
        }
        StorySectionAdd(sec, bar->Selected(self->tab)
                                 ->OnChange(Listen(cx, &SetTab))
                                 ->IntoEl());
        page->Child(sec);
    }

    // Folder Tabs: the selected tab joins the panel under the bar. Each tab
    // carries a close button, and the bar a new-tab button.
    El* folderSec = StorySection(cx, "Folder Tabs", nullptr);
    StorySectionBody(folderSec)->W(kFill);
    component::TabBar* folderBar = component::TabBar::New(cx, StrL("folder"))
                                       ->WFill()
                                       ->Folder()
                                       ->Size(self->toolbar.size)
                                       ->Menu(self->menu)
                                       ->Selected(self->tab)
                                       ->OnChange(Listen(cx, &SetTab));
    if (maxWidth > 0) {
        folderBar->MaxWidth(maxWidth);
    }
    for (int i = 0; i < kTabCount; i++) {
        component::Tab* tab = component::Tab::New(cx, Str(kTabNames[i]));
        tab->Suffix(component::Button::New(
                        cx, StoryFmt(cx, "folder-tab-close-%s", kTabNames[i]))
                        ->Ghost()
                        ->WithSize(UiSize::XSmall)
                        ->Icon(IconName::Close)
                        ->IntoEl());
        folderBar->Child(tab);
    }
    El* folderNew = component::Button::New(cx, StrL("folder-new-tab"))
                        ->Ghost()
                        ->WithSize(UiSize::XSmall)
                        ->Icon(IconName::Plus)
                        ->IntoEl();
    folderNew->MarginX(4);
    folderBar->Suffix(folderNew);
    El* folderFrame = Div(a)
                          ->W(kFill)
                          ->Border(1, th.border)
                          ->Radius(th.radiusLg)
                          ->ClipX()
                          ->ClipY()
                          ->Child(folderBar->IntoEl())
                          ->Child(Div(a)->H(Rems(cx, 5.f))->Bg(th.tabActiveBg));
    StorySectionAdd(folderSec, folderFrame);
    page->Child(folderSec);

    // Dynamic Tabs: a ButtonGroup that grows and shrinks the bar, and tabs
    // that carry a prefix icon and their own close button.
    El* dynamic =
        StorySection(cx, "Dynamic Tabs",
                     "Tabs can be added, removed, and composed with prefix "
                     "and suffix content.");
    StorySectionBody(dynamic)->W(kFill);
    El* actions = Div(a)->FlexRow()->Border(1, th.border)->Radius(th.radius);
    actions->Child(component::Button::New(cx, StrL("add-tab"))
                       ->Label(StrL("Add Tab"))
                       ->Compact()
                       ->OnClick(Listen(cx, &AddDynamicTab))
                       ->IntoEl());
    actions->Child(Div(a)->W(1)->H(24)->Shrink0()->Bg(th.border));
    actions->Child(component::Button::New(cx, StrL("remove-tab"))
                       ->Label(StrL("Remove Last"))
                       ->Compact()
                       ->OnClick(Listen(cx, &RemoveDynamicTab))
                       ->IntoEl());
    StorySectionAdd(dynamic, actions);
    El* dynBar = Div(a)
                     ->FlexRow()
                     ->W(kFill)
                     ->Pad(2)
                     ->Gap(2)
                     ->Bg(th.tokens.muted)
                     ->Radius(th.radius);
    for (int i = 0; i < self->dynamicCount; i++) {
        El* t = Div(a)
                    ->FlexRow()
                    ->H(26)
                    ->PadX(8)
                    ->Gap(4)
                    ->ItemsCenter()
                    ->Radius(th.radius)
                    ->OnClick(Listen(cx, &SetDynamicTab, i));
        if (i == self->dynamicTab) {
            t->Bg(th.tokens.background);
        }
        t->Child(IconEl(a, IconName::BookOpen, 16)->Fg(th.mutedFg));
        // inner_paddings: the label sits in its own box inside the tab, which
        // is what holds the prefix and the suffix off it.
        t->Child(Div(a)->PadX(12)->Child(
            StoryTxt(cx, StoryFmt(cx, "Tab %d", self->dynamicIds[i]), 14,
                     th.foreground)));
        t->Child(component::Button::New(cx, StoryFmt(cx, "dynamic-tab-close-%d",
                                                     self->dynamicIds[i]))
                     ->Ghost()
                     ->WithSize(UiSize::XSmall)
                     ->Icon(IconName::X)
                     ->OnClick(Listen(cx, &CloseDynamicTab, i))
                     ->IntoEl()
                     // The X is inside the tab, which takes the same click:
                     // closing a tab must not also select it on the way.
                     ->StopClick());
        dynBar->Child(t);
    }
    StorySectionAdd(dynamic, dynBar);
    page->Child(dynamic);

    // Filling Space: two segmented tabs. The section says they share the
    // width and `Tab::new().flex_1()` asks them to, but TabBar wraps every
    // tab of an indicator variant in a flex_shrink_0 div, so upstream renders
    // them at their labels' width — which is what this draws.
    El* filling =
        StorySection(cx, "Filling Space",
                     "Segmented tabs can share the available width equally.");
    StorySectionBody(filling)->W(kFill);
    El* fillBar = Div(a)
                      ->FlexRow()
                      ->W(kFill)
                      ->Pad(2)
                      ->Gap(2)
                      ->Bg(th.tokens.muted)
                      ->Radius(th.radius);
    static const char* kFillNames[2] = {"About", "Profile"};
    for (int i = 0; i < 2; i++) {
        El* t =
            Div(a)
                ->H(26)
                ->Shrink0()
                ->PadX(12)
                ->ItemsCenter()
                ->JustifyCenter()
                ->Radius(th.radius)
                ->OnClick(Listen(cx, &SetTab, i))
                ->Child(StoryTxt(cx, Str(kFillNames[i]), 14, th.foreground));
        if (i == self->tab) {
            t->Bg(th.tokens.background);
        }
        fillBar->Child(t);
    }
    StorySectionAdd(filling, fillBar);
    page->Child(filling);
    return page;
}

STORY_PAGE(StoryTabs, TabsStory);
