#include "Story.h"

// ButtonAction: the five toggles of this page's Options dropdown, then the
// rows of the three split buttons' menus, each of which reports itself as
// the page's last action.
enum {
    DropActDisabled = 3300,
    DropActLoading,
    DropActSelected,
    DropActCompact,
    DropActShadow,
    DropActExportCsv,
    DropActExportPdf,
    DropActSaveCopy,
    DropActSaveTemplate,
    DropActOpenQuarterlyReport,
    DropActOpenWatchlistLayout,
    // The inner buttons' own on_click handlers.
    DropActExportDefault,
    DropActSaveDefault,
    DropActRecentDefault,
};

struct DropdownButtonStory {
    bool disabled = false;
    bool loading = false;
    bool selected = false;
    bool compact = false;
    const char* lastAction = "Nothing yet";
    StoryToolbarState toolbar;

    static El* Render(DropdownButtonStory* self, Ctx* cx);
};

static void DropAct(DropdownButtonStory* self, Ctx* cx, const ClickEvent*,
                    int64_t act) {
    switch (act) {
        case DropActDisabled:
            self->disabled = !self->disabled;
            break;
        case DropActLoading:
            self->loading = !self->loading;
            break;
        case DropActSelected:
            self->selected = !self->selected;
            break;
        case DropActCompact:
            self->compact = !self->compact;
            break;
        case DropActShadow:
            ThemeUpdate(cx->app, [](Theme* t) { t->shadow = !t->shadow; });
            break;
        case DropActExportCsv:
            self->lastAction = "Exported as CSV";
            break;
        case DropActExportPdf:
            self->lastAction = "Exported as PDF";
            break;
        case DropActSaveCopy:
            self->lastAction = "Saved as a new file";
            break;
        case DropActSaveTemplate:
            self->lastAction = "Saved as a template";
            break;
        case DropActOpenQuarterlyReport:
            self->lastAction = "Opened Quarterly Report.gpui";
            break;
        case DropActOpenWatchlistLayout:
            self->lastAction = "Opened Watchlist Layout.gpui";
            break;
        case DropActExportDefault:
            self->lastAction = "Exported current view";
            break;
        case DropActSaveDefault:
            self->lastAction = "Saved document";
            break;
        case DropActRecentDefault:
            self->lastAction = "Opened latest file";
            break;
        default:
            StoryToolbarApply(&self->toolbar, nullptr, (int)act);
            break;
    }
    Notify(cx);
}

// Each split's menu is two rows; the menu reports the confirmed row, which
// maps onto the pair of actions starting at `first`.
static void DropExportPick(DropdownButtonStory* self, Ctx* cx,
                           const ClickEvent* ev, int64_t ix) {
    DropAct(self, cx, ev, DropActExportCsv + ix);
}
static void DropSavePick(DropdownButtonStory* self, Ctx* cx,
                         const ClickEvent* ev, int64_t ix) {
    DropAct(self, cx, ev, DropActSaveCopy + ix);
}
static void DropRecentPick(DropdownButtonStory* self, Ctx* cx,
                           const ClickEvent* ev, int64_t ix) {
    DropAct(self, cx, ev, DropActOpenQuarterlyReport + ix);
}

using DropPickFn = void (*)(DropdownButtonStory*, Ctx*, const ClickEvent*,
                            int64_t);

static component::PopupMenu* DropMenu(Ctx* cx, Str id, DropPickFn pick,
                                      const char* first, const char* second) {
    Entity<PopupMenuState> st = component::PopupMenuStateFor(cx, id);
    if (PopupMenuState* s = st.Get(cx)) {
        s->onConfirm = Listen(cx, pick);
    }
    return component::PopupMenu::New(cx, id, st)
        ->Menu(StoryDup(cx, first))
        ->Menu(StoryDup(cx, second));
}

El* DropdownButtonStory::Render(DropdownButtonStory* self, Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    UiSize size = self->toolbar.size;
    El* page = Div(a)->FlexCol()->Gap(24)->W(kFill);
    StoryToolbarOpt opts[5] = {
        {"Disabled", self->disabled, DropActDisabled},
        {"Loading", self->loading, DropActLoading},
        {"Selected", self->selected, DropActSelected},
        {"Compact", self->compact, DropActCompact},
        {"Shadow", th.shadow, DropActShadow},
    };
    page->Child(StoryToolbarOptions(cx, self, opts, 5, Listen(cx, &DropAct)));

    // h_flex().gap_1().text_sm().text_color(muted_foreground)
    page->Child(Div(a)
                    ->FlexRow()
                    ->Gap(4)
                    ->Child(StoryTxt(cx, StrL("Last action:"), 14, th.mutedFg))
                    ->Child(StoryTxt(cx, StoryDup(cx, self->lastAction), 14,
                                     th.mutedFg)));

    El* basic = StorySection(cx, "Basic split", nullptr);
    component::Button* exportBtn =
        component::Button::New(cx, StrL("export-default"))
            ->Label(StrL("Export"))
            ->OnClick(Listen(cx, &DropAct, DropActExportDefault));
    if (self->compact) {
        exportBtn->Compact();
    }
    // dropdown_menu_with_anchor(Anchor::TopRight, ..), which is also
    // DropdownButton's default anchor.
    StorySectionAdd(
        basic,
        component::DropdownButton::New(cx, StrL("export"))
            ->WithSize(size)
            ->Primary()
            ->Button_(exportBtn)
            ->Disabled(self->disabled)
            ->Selected(self->selected)
            ->Menu(DropMenu(cx, StrL("export-menu"), &DropExportPick,
                            "Export all rows (.csv)", "Download report (.pdf)"))
            ->IntoEl());
    page->Child(basic);

    El* inner = StorySection(cx, "Inner button options", nullptr);
    component::Button* saveBtn =
        component::Button::New(cx, StrL("save-default"))
            ->Label(StrL("Save"))
            ->Tooltip(StrL("Save the current document"))
            ->Loading(self->loading)
            ->OnClick(Listen(cx, &DropAct, DropActSaveDefault));
    if (self->compact) {
        saveBtn->Compact();
    }
    StorySectionAdd(
        inner, component::DropdownButton::New(cx, StrL("save"))
                   ->WithSize(size)
                   ->Outline()
                   ->Button_(saveBtn)
                   ->Disabled(self->disabled)
                   ->Menu(DropMenu(cx, StrL("save-menu"), &DropSavePick,
                                   "Save as new file…", "Save as template…"))
                   ->IntoEl());
    page->Child(inner);

    // The split sets no variant or size of its own, so the caret takes the
    // inner button's ghost and small.
    El* inherited = StorySection(cx, "Inherited styling", nullptr);
    StorySectionAdd(
        inherited,
        component::DropdownButton::New(cx, StrL("recent"))
            ->Button_(component::Button::New(cx, StrL("recent-default"))
                          ->Label(StrL("Open latest"))
                          ->Ghost()
                          ->WithSize(UiSize::Small)
                          ->OnClick(Listen(cx, &DropAct, DropActRecentDefault)))
            ->Selected(self->selected)
            ->Disabled(self->disabled)
            ->Menu(DropMenu(cx, StrL("recent-menu"), &DropRecentPick,
                            "Quarterly Report.gpui", "Watchlist Layout.gpui"))
            ->IntoEl());
    page->Child(inherited);
    return page;
}

STORY_PAGE(StoryDropdownButton, DropdownButtonStory);
