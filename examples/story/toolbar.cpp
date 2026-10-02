#include "Story.h"

// crates/story/src/stories/toolbar_story.rs — commands and controls in one
// keyboard-navigable row, at the density the Size menu picks.

static const component::SearchableItem kFonts[] = {
    {.title = StrL("Inter"), .value = StrL("Inter")},
    {.title = StrL("SF Pro"), .value = StrL("SF Pro")},
    {.title = StrL("Helvetica"), .value = StrL("Helvetica")},
    {.title = StrL("Georgia"), .value = StrL("Georgia")},
};
static const component::SearchableItem kMarkets[] = {
    {.title = StrL("All markets"), .value = StrL("All markets")},
    {.title = StrL("US market"), .value = StrL("US market")},
    {.title = StrL("Hong Kong"), .value = StrL("Hong Kong")},
    {.title = StrL("Singapore"), .value = StrL("Singapore")},
};
static const component::SearchableItem kStatuses[] = {
    {.title = StrL("Open"), .value = StrL("Open")},
    {.title = StrL("In progress"), .value = StrL("In progress")},
    {.title = StrL("Filled"), .value = StrL("Filled")},
    {.title = StrL("Cancelled"), .value = StrL("Cancelled")},
};

struct ToolbarStory {
    UiSize size = UiSize::Medium;
    bool disabled = false;
    bool formats[3] = {true, false, false};
    bool menuOpen = false;
    bool seeded = false;
    Entity<component::SelectState> font = {};
    Entity<component::SelectState> market = {};
    Entity<component::ComboboxState> status = {};
    InputState query;

    static El* Render(ToolbarStory* self, Ctx* cx);
};

// ChangeStorySize / ToggleDisabled, and the dropdown's own open and close.
static void OnToolbarAct(ToolbarStory* self, Ctx* cx, const ClickEvent*,
                         int64_t act) {
    switch (act) {
        case ToolbarOpenOpts:
            self->menuOpen = !self->menuOpen;
            break;
        case ToolbarCloseAll:
            self->menuOpen = false;
            break;
        case ToolbarSizeXs:
            self->size = UiSize::XSmall;
            self->menuOpen = false;
            break;
        case ToolbarSizeSm:
            self->size = UiSize::Small;
            self->menuOpen = false;
            break;
        case ToolbarSizeMd:
            self->size = UiSize::Medium;
            self->menuOpen = false;
            break;
        case ToolbarOptDisabled:
            self->disabled = !self->disabled;
            self->menuOpen = false;
            break;
        default:
            break;
    }
    Notify(cx);
}

// Toggle's on_click hands over the new checked state.
template <int Ix>
static void OnFormat(ToolbarStory* self, Ctx* cx, const ClickEvent*,
                     int64_t checked) {
    self->formats[Ix] = checked != 0;
    Notify(cx);
}

// toolbar_options: one dropdown with the three densities and Disabled.
static El* ToolbarOptions(ToolbarStory* self, Ctx* cx) {
    const char* label = self->size == UiSize::XSmall  ? "XSmall"
                        : self->size == UiSize::Small ? "Small"
                                                      : "Medium";
    Listener act = Listen(cx, &OnToolbarAct, 0);
    StoryToolbarOpt rows[4] = {
        {"XSmall", self->size == UiSize::XSmall, ToolbarSizeXs},
        {"Small", self->size == UiSize::Small, ToolbarSizeSm},
        {"Medium", self->size == UiSize::Medium, ToolbarSizeMd},
        {"Disabled", self->disabled, ToolbarOptDisabled, false, true},
    };
    El* group = StoryToolbarGroup(cx);
    group->Child(StoryToolbarDropdown(
        cx, StrL("toolbar-options"), StoryFmt(cx, "Size: %s", label),
        self->menuOpen, ListenerArg(act, ToolbarOpenOpts), rows, 4, act));
    return group;
}

static component::Button* IconButton(Ctx* cx, const char* id, IconName icon,
                                     const char* tooltip, bool disabled) {
    return component::Button::New(cx, Str(id))
        ->Icon(icon)
        ->Tooltip(Str(tooltip))
        ->Disabled(disabled);
}

template <int Ix>
static component::Toggle* FormatToggle(ToolbarStory* self, Ctx* cx,
                                       const char* id, const char* label) {
    return component::Toggle::New(cx, Str(id))
        ->Label(Str(label))
        ->Checked(self->formats[Ix])
        ->Disabled(self->disabled)
        ->OnClick(Listen(cx, &OnFormat<Ix>));
}

static El* VerticalRule(Ctx* cx) {
    // Separator::vertical().h_5()
    return component::Separator::Vertical(cx)->IntoEl()->H(20);
}

El* ToolbarStory::Render(ToolbarStory* self, Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    if (!self->seeded) {
        self->seeded = true;
        self->font = component::SelectState::New(cx->app);
        self->market = component::SelectState::New(cx->app);
        self->status = component::ComboboxState::New(cx->app);
        // Some(IndexPath::default()): the first font and market are picked.
        if (component::SelectState* s = self->font.Get(cx)) {
            component::SearchableListSelectOnly(s->List(), 0);
        }
        if (component::SelectState* s = self->market.Get(cx)) {
            component::SearchableListSelectOnly(s->List(), 0);
        }
        if (component::ComboboxState* s = self->status.Get(cx)) {
            s->Searchable(true);
        }
        InputSetPlaceholder(&self->query, StrL("Search"));
    }
    bool off = self->disabled;
    El* page = Div(a)->FlexCol()->W(kFill)->ItemsCenter()->Gap(24);
    page->Child(ToolbarOptions(self, cx));

    El* def = StorySection(cx, "Default",
                           "Keep document, history, and formatting commands in "
                           "one compact editor toolbar.");
    StorySectionBody(def)->W(640);
    component::Toolbar* bar =
        component::Toolbar::New(cx, StrL("default-toolbar"))
            ->WithSize(self->size)
            ->Disabled(off);
    bar->Child(component::ToolbarGroup::New(cx, StrL("document-group"))
                   ->Label(StrL("Document"))
                   ->Gap(4)
                   ->Child(component::Button::New(cx, StrL("new-document"))
                               ->Icon(IconName::Plus)
                               ->Label(StrL("New"))
                               ->Disabled(off))
                   ->Child(component::Button::New(cx, StrL("save-document"))
                               ->Icon(component::ButtonIcon::New(
                                   cx, component::Icon::Empty(cx)
                                           ->Path(StrL("icons/save.svg"))))
                               ->Label(StrL("Save"))
                               ->Disabled(off)));
    bar->Content(VerticalRule(cx));
    bar->Child(
        component::ToolbarGroup::New(cx, StrL("history-group"))
            ->Label(StrL("History"))
            ->Gap(4)
            ->Child(IconButton(cx, "undo", IconName::Undo2, "Undo", off))
            ->Child(IconButton(cx, "redo", IconName::Redo2, "Redo", off)));
    bar->Content(VerticalRule(cx));
    bar->Child(component::ToolbarGroup::New(cx, StrL("formatting-group"))
                   ->Label(StrL("Formatting"))
                   ->Gap(4)
                   ->Child(FormatToggle<0>(self, cx, "bold", "B"))
                   ->Child(FormatToggle<1>(self, cx, "italic", "I"))
                   ->Child(FormatToggle<2>(self, cx, "underline", "U")));
    bar->Content(VerticalRule(cx));
    bar->Child(component::Select::New(cx, StrL("toolbar-font"), self->font)
                   ->Items(kFonts, 4)
                   ->Placeholder(StrL("Font"))
                   ->Disabled(off)
                   ->W(160));
    StorySectionAdd(
        def, bar->IntoEl()->W(kFill)->Border(1, th.border)->Radius(th.radius));
    page->Child(def);

    El* mixed = StorySection(cx, "Mixed controls",
                             "Select and Combobox inherit the same density "
                             "while preserving their own popup behavior.");
    StorySectionBody(mixed)->W(640);
    component::Toolbar* mixedBar =
        component::Toolbar::New(cx, StrL("mixed-toolbar"))
            ->WithSize(self->size)
            ->Disabled(off);
    mixedBar
        ->Child(component::Input::New(cx, StrL("toolbar-query"), &self->query)
                    ->Prefix(IconEl(a, IconName::Search, 16)->Fg(th.mutedFg))
                    ->Disabled(off)
                    ->W(160));
    mixedBar
        ->Child(component::Select::New(cx, StrL("toolbar-market"), self->market)
                    ->Items(kMarkets, 4)
                    ->Placeholder(StrL("Market"))
                    ->Disabled(off)
                    ->W(128));
    mixedBar->Child(
        component::Combobox::New(cx, StrL("toolbar-status"), self->status)
            ->Items(kStatuses, 4)
            ->Placeholder(StrL("Order status"))
            ->Disabled(off)
            ->W(160));
    mixedBar->Content(Div(a)->Flex1());
    mixedBar
        ->Child(IconButton(cx, "refresh", IconName::RotateCw, "Refresh", off));
    mixedBar->Child(IconButton(cx, "settings", IconName::Settings2,
                               "Configure columns", off));
    StorySectionAdd(
        mixed,
        mixedBar->IntoEl()->W(kFill)->Border(1, th.border)->Radius(th.radius));
    page->Child(mixed);
    return page;
}

STORY_PAGE(StoryToolbarStory, ToolbarStory);
