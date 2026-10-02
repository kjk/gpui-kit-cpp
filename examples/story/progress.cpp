#include "Story.h"

// The toolbar's two dropdowns, the way the Rust story spells them: a value
// with four presets, and an Options menu holding the indeterminate switch.
enum {
    ProgMenuNone = 0,
    ProgMenuValue,
    ProgMenuOptions,
    ProgActValue0 = 3200,
    ProgActValue25,
    ProgActValue75,
    ProgActValue100,
    ProgActLoading,
    ProgMenuSize = 3
};

struct ProgressStory {
    // The story's value starts at 25 and its toolbar steps it.
    float value = 25;
    bool loading = false;
    int openMenu = ProgMenuNone;
    // The play button's animation: value climbs by 2 every 15ms until it
    // reaches 100, which is Rust's `start_animation` spawn.
    int animTimer = 0;
    StoryToolbarState toolbar;

    static El* Render(ProgressStory* self, Ctx* cx);
};

static void ProgTick(ProgressStory* self, Ctx* cx, const TickEvent*) {
    self->value += 2;
    if (self->value >= 100) {
        self->value = 100;
        WindowCancelTimer(cx->win, self->animTimer);
        self->animTimer = 0;
    }
    Notify(cx);
}

static void ProgPlay(ProgressStory* self, Ctx* cx, const ClickEvent*) {
    self->value = 0;
    if (self->animTimer) {
        WindowCancelTimer(cx->win, self->animTimer);
    }
    self->animTimer = WindowSetInterval(cx->win, 15, Listen(cx, &ProgTick));
    Notify(cx);
}

static void ProgMenuOpen(ProgressStory* self, Ctx* cx, const ClickEvent*,
                         int64_t which) {
    self->openMenu = self->openMenu == (int)which ? ProgMenuNone : (int)which;
    Notify(cx);
}

static void ProgMenuAct(ProgressStory* self, Ctx* cx, const ClickEvent*,
                        int64_t act) {
    switch (act) {
        case ProgActValue0:
            self->value = 0;
            break;
        case ProgActValue25:
            self->value = 25;
            break;
        case ProgActValue75:
            self->value = 75;
            break;
        case ProgActValue100:
            self->value = 100;
            break;
        case ProgActLoading:
            self->loading = !self->loading;
            break;
        default:
            StoryToolbarApply(&self->toolbar, nullptr, (int)act);
            break;
    }
    self->openMenu = ProgMenuNone;
    Notify(cx);
}

El* ProgressStory::Render(ProgressStory* self, Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    El* page = Div(a)->FlexCol()->Gap(12)->W(kFill);
    Listener openMenu = Listen(cx, &ProgMenuOpen);
    Listener act = Listen(cx, &ProgMenuAct);
    El* toolbarRow = Div(a)->FlexRow()->W(kFill)->JustifyEnd()->ItemsStart();
    El* group = StoryToolbarGroup(cx);
    // story_toolbar(self.size) puts the size menu first.
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
        cx, StrL("progress-size"), StoryFmt(cx, "Size: %s", sizeName),
        self->openMenu == ProgMenuSize, ListenerArg(openMenu, ProgMenuSize),
        sizes, 4, act));
    group->Child(StoryToolbarDivider(cx));
    StoryToolbarOpt presets[4] = {
        {"0%", self->value == 0, ProgActValue0},
        {"25%", self->value == 25, ProgActValue25},
        {"75%", self->value == 75, ProgActValue75},
        {"100%", self->value == 100, ProgActValue100},
    };
    group->Child(StoryToolbarDropdown(
        cx, StrL("progress-value"), StoryFmt(cx, "Value: %.0f%%", self->value),
        self->openMenu == ProgMenuValue, ListenerArg(openMenu, ProgMenuValue),
        presets, 4, act));
    group->Child(StoryToolbarDivider(cx));
    StoryToolbarOpt options[1] = {{"Loading", self->loading, ProgActLoading}};
    group->Child(StoryToolbarDropdown(
        cx, StrL("progress-options"), StrL("Options"),
        self->openMenu == ProgMenuOptions,
        ListenerArg(openMenu, ProgMenuOptions), options, 1, act));
    group->Child(StoryToolbarDivider(cx));
    // The play button: value back to zero, then a step every 15ms.
    El* play = Div(a)
                   ->H(24)
                   ->PadX(8)
                   ->ItemsCenter()
                   ->JustifyCenter()
                   ->HoverBg(th.tokens.muted)
                   ->Child(IconEl(a, IconName::Play, 14)->Fg(th.foreground));
    play->Click(HashClickId(StrL("progress-play")))
        ->OnClick(Listen(cx, &ProgPlay));
    group->Child(play);
    toolbarRow->Child(group);
    page->Child(toolbarRow);

    El* upload = StorySection(
        cx, "Upload", "Pair progress with a clear label, value, and status.");
    StorySectionBody(upload)->W(560)->ItemsCenter();
    El* card = Div(a)
                   ->FlexCol()
                   ->Gap(12)
                   ->W(400)
                   ->Pad(16)
                   ->Border(1, th.border)
                   ->Radius(th.radius);
    El* head = Div(a)->FlexRow()->W(kFill)->JustifyBetween()->ItemsCenter();
    // div().font_medium() beside a div().text_sm().text_color(muted).
    head->Child(
        StoryTxt(cx, StrL("Uploading design-assets.zip"), 16, th.foreground)
            ->Medium());
    head->Child(StoryTxt(cx,
                         self->loading ? StrL("Uploading…")
                                       : StoryFmt(cx, "%.0f%%", self->value),
                         14, th.mutedFg));
    card->Child(head);
    card->Child(component::Progress::New(cx)
                    ->Id(StrL("upload"))
                    ->Value(self->value)
                    ->Loading(self->loading)
                    ->W(kFill)
                    ->IntoEl());
    El* foot = Div(a)->FlexRow()->W(kFill)->JustifyBetween()->ItemsCenter();
    foot->Child(StoryTxt(cx, StrL("24.8 MB of 96 MB"), 12, th.mutedFg));
    foot->Child(StoryTxt(cx, StrL("About 1 min left"), 12, th.mutedFg));
    card->Child(foot);
    StorySectionAdd(upload, card);
    page->Child(upload);

    El* circ = StorySection(
        cx, "Circular", "Use a compact radial indicator for focused tasks.");
    StorySectionBody(circ)->W(560)->ItemsCenter();
    El* circBox = Div(a)
                      ->FlexRow()
                      ->W(400)
                      ->ItemsCenter()
                      ->Gap(20)
                      ->Pad(16)
                      ->Radius(th.radius)
                      ->Bg(RgbaOpacity(th.muted, 0.4f));
    circBox->Child(component::ProgressCircle::New(cx)
                       ->Id(StrL("analyze"))
                       ->Value(self->value)
                       ->Loading(self->loading)
                       ->Label(!self->loading)
                       ->Size(80)
                       ->IntoEl());
    El* circText = Div(a)->FlexCol()->Gap(4);
    circText->Child(StoryTxt(cx, StrL("Analyzing project"), 16, th.foreground)
                        ->Medium());
    circText->Child(StoryTxt(cx, StrL("Scanning components and dependencies."),
                             14, th.mutedFg));
    circBox->Child(circText);
    StorySectionAdd(circ, circBox);
    page->Child(circ);
    return page;
}

STORY_PAGE(StoryProgress, ProgressStory);
