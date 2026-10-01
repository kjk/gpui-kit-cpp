#include "Story.h"

// crates/story/src/stories/command_story.rs

static uint32_t CmdAction(const char* spelled) {
    return ActionOf(Str(spelled));
}

#define COMMAND_ACTION(fn, spelled)              \
    static uint32_t fn() {                       \
        static uint32_t id = CmdAction(spelled); \
        return id;                               \
    }

COMMAND_ACTION(ActOpenProfile, "command_story::OpenProfile")
COMMAND_ACTION(ActOpenBilling, "command_story::OpenBilling")
COMMAND_ACTION(ActOpenSettings, "command_story::OpenSettings")
COMMAND_ACTION(ActGoHome, "command_story::GoHome")
COMMAND_ACTION(ActOpenInbox, "command_story::OpenInbox")
COMMAND_ACTION(ActOpenDocuments, "command_story::OpenDocuments")
COMMAND_ACTION(ActOpenFolders, "command_story::OpenFolders")
COMMAND_ACTION(ActNewFile, "command_story::NewFile")
COMMAND_ACTION(ActCopyItem, "command_story::CopyItem")
COMMAND_ACTION(ActDeleteItem, "command_story::DeleteItem")

static const char* kCommandContext = "Command";

// The bindings `CommandStory::new` makes. `secondary-` is cmd on macOS and
// ctrl elsewhere, Rust's `primary`.
static void CommandStoryInitKeys() {
    static uint32_t bound = 0;
    if (bound == KeymapGeneration()) {
        return;
    }
    bound = KeymapGeneration();
    KeyBinding bindings[] = {
        {"secondary-p", ActOpenProfile(), kCommandContext},
        {"secondary-b", ActOpenBilling(), kCommandContext},
        {"secondary-s", ActOpenSettings(), kCommandContext},
        {"secondary-h", ActGoHome(), kCommandContext},
        {"secondary-i", ActOpenInbox(), kCommandContext},
        {"secondary-d", ActOpenDocuments(), kCommandContext},
        {"secondary-f", ActOpenFolders(), kCommandContext},
        {"secondary-n", ActNewFile(), kCommandContext},
        {"secondary-c", ActCopyItem(), kCommandContext},
        {"backspace", ActDeleteItem(), kCommandContext},
    };
    KeymapBind(bindings, (int)(sizeof(bindings) / sizeof(bindings[0])));
}

// A CommandItem with an action, whose row shows the chord bound to it under
// the "Command" context.
static component::CommandItem ActionItem(Str label, IconName icon,
                                         uint32_t action) {
    component::CommandItem item;
    item.label = label;
    item.icon = icon;
    item.action = action;
    item.actionContext = kCommandContext;
    return item;
}

static component::CommandItem IconItem(Str label, IconName icon) {
    component::CommandItem item;
    item.label = label;
    item.icon = icon;
    return item;
}

// suggestions(): the palette used by the inline and dialog examples — two
// groups of commands, one of them disabled, the second group carrying
// shortcut hints.
static const Str kEmojiKeywords[] = {StrL("smile"), StrL("icon")};
static component::CommandItem gSuggestions[3] = {};
static component::CommandItem gSettings[3] = {};
static component::CommandEntry gSuggestionEntries[3] = {};

// scrollable()
static component::CommandItem gNavigation[4] = {};
static component::CommandItem gActions[3] = {};
static component::CommandItem gAccount[3] = {};
static component::CommandItem gTools[3] = {};
static component::CommandEntry gScrollableEntries[7] = {};

// quick_actions(): actions that can be navigated without a search field, such
// as a compact context menu.
static component::CommandItem gQuickActions[3] = {};

// variable_rows(): two custom rows with different intrinsic heights.
static component::CommandItem gVariableRows[2] = {};

// text_sm and text_xs: 14px on a 20px line, 12px on a 16px one.
static const float kTextSmLine = 20.f / 14.f;
static const float kTextXsLine = 16.f / 12.f;
// A command row's own px_2 py_1p5 around whatever it holds.
static const float kRowPadY = 12.f;

static El* TextSm(Ctx* cx, Str s, Rgba c) {
    return StoryTxt(cx, s, 14, c)->LineHeight(kTextSmLine);
}
static El* TextXs(Ctx* cx, Str s, Rgba c) {
    return StoryTxt(cx, s, 12, c)->LineHeight(kTextXsLine);
}

// Rust measures every flattened row; a custom row here says how tall it is
// (CommandItem::contentH), worked out from the same text_sm/text_xs lines.
static El* CompactRow(Ctx* cx, const component::CommandItem*) {
    return Div(cx->a)->FlexRow()->W(kFill)->PadY(4)->Child(
        TextSm(cx, StrL("Compact custom row"), ThemeNow(cx->app).foreground));
}
static El* ExpandedRow(Ctx* cx, const component::CommandItem*) {
    const Theme& th = ThemeNow(cx->app);
    return Div(cx->a)
        ->FlexCol()
        ->W(kFill)
        ->PadY(16)
        ->Gap(4)
        ->Child(TextSm(cx, StrL("Expanded custom row"), th.foreground))
        ->Child(TextXs(
            cx, StrL("Its extra detail gives this row a different height."),
            th.mutedFg));
}

// The stock universe the search panel queries. Stands in for whatever a real
// application would go and fetch.
struct Stock {
    Str symbol;
    Str name;
    Str price;
    float change;
};

static const Stock kStocks[10] = {
    {StrL("AAPL.US"), StrL("Apple Inc."), StrL("228.52"), 1.24f},
    {StrL("NVDA.US"), StrL("NVIDIA Corporation"), StrL("134.81"), -0.62f},
    {StrL("TSLA.US"), StrL("Tesla, Inc."), StrL("251.44"), 3.18f},
    {StrL("MSFT.US"), StrL("Microsoft Corporation"), StrL("428.02"), 0.41f},
    {StrL("AMZN.US"), StrL("Amazon.com, Inc."), StrL("186.33"), -1.07f},
    {StrL("700.HK"), StrL("Tencent Holdings Ltd."), StrL("412.60"), 0.87f},
    {StrL("9988.HK"), StrL("Alibaba Group Holding Ltd."), StrL("82.15"),
     -2.31f},
    {StrL("3690.HK"), StrL("Meituan"), StrL("128.90"), 1.66f},
    {StrL("600519.SH"), StrL("Kweichow Moutai Co., Ltd."), StrL("1482.00"),
     -0.34f},
    {StrL("000858.SZ"), StrL("Wuliangye Yibin Co., Ltd."), StrL("142.77"),
     0.19f},
};
static const int kStockCount = 10;
static const int kPopularCount = 5;

// A two-line search result: symbol and name on the left, quote on the right.
static El* StockRow(Ctx* cx, const component::CommandItem* item) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    const Stock& stock = kStocks[item->data];
    Rgba changeColor = stock.change < 0 ? th.chartBearish : th.chartBullish;
    return Div(a)
        ->FlexRow()
        ->W(kFill)
        ->Gap(12)
        ->ItemsCenter()
        ->JustifyBetween()
        ->Child(Div(a)
                    ->FlexCol()
                    ->Gap(2)
                    ->Child(TextSm(cx, stock.symbol, th.foreground))
                    ->Child(TextXs(cx, stock.name, th.mutedFg)))
        ->Child(Div(a)
                    ->FlexCol()
                    ->Gap(2)
                    ->ItemsEnd()
                    ->Child(TextSm(cx, stock.price, th.foreground))
                    ->Child(TextXs(cx, StoryFmt(cx, "%+.2f%%", stock.change),
                                   changeColor)));
}

// stock_item(): the name is the label, the symbol a keyword.
static component::CommandItem gStockItems[kStockCount] = {};

static void SeedEntries() {
    gSuggestions[0] = IconItem(StrL("Calendar"), IconName::Calendar);
    gSuggestions[1] = IconItem(StrL("Search Emoji"), IconName::Search);
    gSuggestions[1].checked = true;
    gSuggestions[1].keywords = kEmojiKeywords;
    gSuggestions[1].nKeywords = 2;
    gSuggestions[2] = IconItem(StrL("Calculator"), IconName::Frame);
    gSuggestions[2].disabled = true;
    gSettings[0] =
        ActionItem(StrL("Profile"), IconName::User, ActOpenProfile());
    gSettings[1] =
        ActionItem(StrL("Billing"), IconName::CircleUser, ActOpenBilling());
    gSettings[2] =
        ActionItem(StrL("Settings"), IconName::Settings, ActOpenSettings());
    gSuggestionEntries[0] = component::CommandEntryOf(
        component::CommandGroup{StrL("Suggestions"), gSuggestions, 3});
    gSuggestionEntries[1] = component::CommandSeparatorEntry();
    gSuggestionEntries[2] = component::CommandEntryOf(
        component::CommandGroup{StrL("Settings"), gSettings, 3});

    gNavigation[0] =
        ActionItem(StrL("Home"), IconName::LayoutDashboard, ActGoHome());
    gNavigation[1] = ActionItem(StrL("Inbox"), IconName::Inbox, ActOpenInbox());
    gNavigation[2] =
        ActionItem(StrL("Documents"), IconName::File, ActOpenDocuments());
    gNavigation[3] =
        ActionItem(StrL("Folders"), IconName::Folder, ActOpenFolders());
    gActions[0] = ActionItem(StrL("New File"), IconName::Plus, ActNewFile());
    gActions[1] = ActionItem(StrL("Copy"), IconName::Copy, ActCopyItem());
    gActions[2] = ActionItem(StrL("Delete"), IconName::Delete, ActDeleteItem());
    gAccount[0] = IconItem(StrL("Profile"), IconName::User);
    gAccount[1] = IconItem(StrL("Notifications"), IconName::Bell);
    gAccount[2] = IconItem(StrL("Help & Support"), IconName::Info);
    gTools[0] = IconItem(StrL("Palette"), IconName::Palette);
    gTools[1] = IconItem(StrL("Terminal"), IconName::SquareTerminal);
    gTools[2] = IconItem(StrL("Globe"), IconName::Globe);
    gScrollableEntries[0] = component::CommandEntryOf(
        component::CommandGroup{StrL("Navigation"), gNavigation, 4});
    gScrollableEntries[1] = component::CommandSeparatorEntry();
    gScrollableEntries[2] = component::CommandEntryOf(
        component::CommandGroup{StrL("Actions"), gActions, 3});
    gScrollableEntries[3] = component::CommandSeparatorEntry();
    gScrollableEntries[4] = component::CommandEntryOf(
        component::CommandGroup{StrL("Account"), gAccount, 3});
    gScrollableEntries[5] = component::CommandSeparatorEntry();
    gScrollableEntries[6] = component::CommandEntryOf(
        component::CommandGroup{StrL("Tools"), gTools, 3});

    gQuickActions[0] = IconItem(StrL("New File"), IconName::Plus);
    gQuickActions[1] = IconItem(StrL("Duplicate"), IconName::Copy);
    gQuickActions[2] = IconItem(StrL("Move to Trash"), IconName::Delete);

    gVariableRows[0].label = StrL("small-row");
    gVariableRows[0].content = CompactRow;
    gVariableRows[0].contentH = 8.f + 14.f * kTextSmLine + kRowPadY;
    gVariableRows[1].label = StrL("large-row");
    gVariableRows[1].content = ExpandedRow;
    gVariableRows[1].contentH =
        32.f + 14.f * kTextSmLine + 4.f + 12.f * kTextXsLine + kRowPadY;

    for (int i = 0; i < kStockCount; i++) {
        component::CommandItem& item = gStockItems[i];
        item.label = kStocks[i].name;
        item.keywords = &kStocks[i].symbol;
        item.nKeywords = 1;
        item.content = StockRow;
        item.contentH =
            14.f * kTextSmLine + 2.f + 12.f * kTextXsLine + kRowPadY;
        item.data = i;
    }
}

struct CommandStory {
    Entity<component::CommandState> inlineState = {};
    Entity<component::CommandState> dialog = {};
    Entity<component::CommandState> quickActions = {};
    Entity<component::CommandState> scrollable = {};
    Entity<component::CommandState> variableRows = {};
    Entity<component::CommandState> search = {};
    // stock_entries / stock_results: the "Popular" group before anything is
    // typed, the ungrouped results after. A result's row is its index here.
    component::CommandEntry stockEntries[kStockCount] = {};
    int nStockEntries = 0;
    int stockResults[kStockCount] = {};
    int nStockResults = 0;
    // _search_task: the timer standing in for the remote round trip, so a
    // query that arrives while the last one is in flight cancels it.
    int searchTimer = 0;
    char lastCommand[64] = {};
    bool seeded = false;

    static El* Render(CommandStory* self, Ctx* cx);
};

// popular_entries(): what the panel shows before anything has been typed.
static void ShowPopular(CommandStory* self) {
    self->stockEntries[0] = component::CommandEntryOf(
        component::CommandGroup{StrL("Popular"), gStockItems, kPopularCount});
    self->nStockEntries = 1;
    for (int i = 0; i < kPopularCount; i++) {
        self->stockResults[i] = i;
    }
    self->nStockResults = kPopularCount;
}

static void CancelSearchTimer(CommandStory* self, Ctx* cx) {
    if (self->searchTimer) {
        WindowCancelTimer(cx->win, self->searchTimer);
        self->searchTimer = 0;
    }
}

// last_command, and the page that shows it redrawn — from whichever view the
// confirm arrived in.
static void SetLastCommand(Entity<CommandStory> owner, Ctx* cx, Str s) {
    CommandStory* self = owner.Get(cx);
    if (!self) {
        return;
    }
    int n = len(s) < (int)sizeof(self->lastCommand) - 1
                ? len(s)
                : (int)sizeof(self->lastCommand) - 1;
    memcpy(self->lastCommand, s.s, n);
    self->lastCommand[n] = 0;
    NotifyEntity(cx->app, owner.id, cx->win);
}

static Str ConfirmedPath(Ctx* cx, const component::CommandEvent* ev) {
    return StoryFmt(cx, "section %d, row %d", ev->path.section, ev->path.row);
}

// on_command_confirm
static void OnCommandConfirm(CommandStory*, Ctx* cx,
                             const component::CommandEvent* ev) {
    SetLastCommand(Entity<CommandStory>{cx->self}, cx, ConfirmedPath(cx, ev));
}

static El* KeyHint(Ctx* cx, const char* const* keys, int nKeys,
                   const char* label) {
    El* row = Div(cx->a)->FlexRow()->Gap(4)->ItemsCenter();
    for (int i = 0; i < nKeys; i++) {
        // Keystroke::parse, so the key reads the way the platform spells it.
        component::Keystroke k;
        if (component::KeystrokeParse(cx->a, Str(keys[i]), &k)) {
            row->Child(component::Kbd::New(cx, k)->IntoEl());
        }
    }
    row->Child(TextXs(cx, Str(label), ThemeNow(cx->app).mutedFg));
    return row;
}

// The dialog palette's header: built from the state once the model is
// installed, so the count is this query's.
static El* MatchesHeader(Ctx* cx, const component::CommandState* s) {
    const Theme& th = ThemeNow(cx->app);
    return Div(cx->a)
        ->FlexRow()
        ->W(kFill)
        ->JustifyBetween()
        ->PadX(12)
        ->PadY(8)
        ->BorderB(1, th.border)
        ->Child(TextSm(cx, StrL("Commands"), th.foreground))
        ->Child(TextSm(
            cx, StoryFmt(cx, "%d matches", component::CommandMatchedCount(s)),
            th.foreground));
}

static El* KeyHintsFooter(Ctx* cx) {
    const Theme& th = ThemeNow(cx->app);
    static const char* const kNavigate[] = {"up", "down"};
    static const char* const kSelect[] = {"enter"};
    static const char* const kClose[] = {"escape"};
    return Div(cx->a)
        ->FlexRow()
        ->FlexWrap()
        ->W(kFill)
        ->Gap(12)
        ->ItemsCenter()
        ->PadX(12)
        ->PadY(8)
        ->BorderT(1, th.border)
        ->Child(KeyHint(cx, kNavigate, 2, "Navigate"))
        ->Child(KeyHint(cx, kSelect, 1, "Select"))
        ->Child(KeyHint(cx, kClose, 1, "Close"));
}

// `close_button(false).p_0()` around a palette that is the dialog's whole
// content.
static El* PaletteDialog(Ctx* cx, El* palette, Listener close) {
    return component::Dialog::New(cx)
        ->Open(true)
        ->CloseButton(false)
        ->OnClose(close)
        ->OnCancel(close)
        ->Surface(Div(cx->a)->FlexCol()->W(kFill)->Child(palette))
        ->IntoEl(WindowSize(cx->win));
}

// The "Open Menu" dialog: `window.open_dialog` over the dialog palette.
struct CommandMenuDialog {
    Entity<CommandStory> owner = {};
    // focus_on_mount
    bool focused = false;

    // on_dialog_confirm
    static void OnConfirm(CommandMenuDialog* self, Ctx* cx,
                          const component::CommandEvent* ev) {
        SetLastCommand(self->owner, cx, ConfirmedPath(cx, ev));
        WindowCloseDialog(cx);
    }

    static void OnClose(CommandMenuDialog*, Ctx* cx, const ClickEvent*) {
        WindowCloseDialog(cx);
    }

    static El* Render(CommandMenuDialog* self, Ctx* cx) {
        CommandStory* story = self->owner.Get(cx);
        if (!story) {
            return Div(cx->a);
        }
        component::CommandState* state = story->dialog.Get(cx);
        if (!self->focused && state) {
            self->focused = true;
            InputFocus(&state->query, cx);
        }
        // Cancel intentionally has no local close callback: the propagated
        // action belongs to Dialog.
        El* palette =
            component::Command::New(cx, StrL("command-dialog"), story->dialog)
                ->Entries(gSuggestionEntries, 3)
                ->Bordered(false)
                ->Placeholder(StrL("Type a command or search..."))
                ->OnConfirm(Listen(cx, &CommandMenuDialog::OnConfirm))
                ->Header(MatchesHeader)
                ->Footer(KeyHintsFooter(cx))
                ->IntoEl();
        return PaletteDialog(cx, palette,
                             Listen(cx, &CommandMenuDialog::OnClose));
    }
};

static void OpenCommandDialog(CommandStory*, Ctx* cx, const ClickEvent*) {
    Entity<CommandMenuDialog> dialog = EntityNew<CommandMenuDialog>(cx->app);
    dialog.Get(cx)->owner = Entity<CommandStory>{cx->self};
    WindowOpenDialog(cx, dialog);
}

static El* StockEmpty(Ctx* cx) {
    const Theme& th = ThemeNow(cx->app);
    return Div(cx->a)
        ->FlexCol()
        ->W(kFill)
        ->ItemsCenter()
        ->Gap(8)
        ->PadY(24)
        ->Child(IconEl(cx->a, IconName::Search, 32)->Fg(th.mutedFg))
        ->Child(TextSm(cx, StrL("No stock found."), th.mutedFg));
}

// The stock search dialog. The entries and the search in flight are the
// story's, as in Rust; this view draws them and forwards the callbacks.
struct StockSearchDialog {
    Entity<CommandStory> owner = {};
    bool focused = false;

    // cancel_stock_search
    static void CancelSearch(StockSearchDialog* self, Ctx* cx) {
        CommandStory* story = self->owner.Get(cx);
        if (!story) {
            return;
        }
        CancelSearchTimer(story, cx);
        component::CommandSetLoading(story->search.Get(cx), cx, false);
    }

    static void OnClose(StockSearchDialog* self, Ctx* cx, const ClickEvent*) {
        CancelSearch(self, cx);
        WindowCloseDialog(cx);
    }

    // The round trip a real search would spend on the network is over:
    // replace the entries with the results for the query now in the field —
    // the one that started this timer, since a newer one would have replaced
    // it.
    static void OnResults(StockSearchDialog* self, Ctx* cx, const TickEvent*) {
        CommandStory* story = self->owner.Get(cx);
        component::CommandState* s = story ? story->search.Get(cx) : nullptr;
        if (!s) {
            return;
        }
        story->searchTimer = 0;
        Str query = InputValue(&s->query);
        int lo = 0;
        int hi = len(query);
        while (lo < hi && (unsigned char)query.s[lo] <= ' ') {
            lo++;
        }
        while (hi > lo && (unsigned char)query.s[hi - 1] <= ' ') {
            hi--;
        }
        query = Str(query.s + lo, hi - lo);
        story->nStockEntries = 0;
        story->nStockResults = 0;
        for (int i = 0; i < kStockCount; i++) {
            if (StrContainsI(kStocks[i].symbol, query) ||
                StrContainsI(kStocks[i].name, query)) {
                story->stockEntries[story->nStockEntries++] =
                    component::CommandEntryOf(gStockItems[i]);
                story->stockResults[story->nStockResults++] = i;
            }
        }
        component::CommandSetLoading(s, cx, false);
        Notify(cx);
    }

    // on_stock_query: answer the panel's queries the way a remote search
    // would — spin the field, wait, then replace the entries with the results.
    static void OnQuery(StockSearchDialog* self, Ctx* cx,
                        const component::CommandEvent* ev) {
        CommandStory* story = self->owner.Get(cx);
        if (!story) {
            return;
        }
        CancelSearchTimer(story, cx);
        component::CommandState* s = story->search.Get(cx);
        if (len(ev->query) == 0) {
            // Nothing typed is not the same as nothing found: fall back to
            // the list people would otherwise have to search for.
            ShowPopular(story);
            component::CommandSetLoading(s, cx, false);
            Notify(cx);
            return;
        }
        component::CommandSetLoading(s, cx, true);
        story->searchTimer = WindowSetTimeout(
            cx->win, 400, Listen(cx, &StockSearchDialog::OnResults));
    }

    // on_stock_confirm
    static void OnConfirm(StockSearchDialog* self, Ctx* cx,
                          const component::CommandEvent* ev) {
        CommandStory* story = self->owner.Get(cx);
        if (!story || ev->path.section != 0 || ev->path.row < 0 ||
            ev->path.row >= story->nStockResults) {
            return;
        }
        const Stock& stock = kStocks[story->stockResults[ev->path.row]];
        CancelSearch(self, cx);
        SetLastCommand(self->owner, cx, stock.symbol);
        WindowCloseDialog(cx);
    }

    static El* Render(StockSearchDialog* self, Ctx* cx) {
        CommandStory* story = self->owner.Get(cx);
        if (!story) {
            return Div(cx->a);
        }
        component::CommandState* state = story->search.Get(cx);
        if (!self->focused && state) {
            self->focused = true;
            InputFocus(&state->query, cx);
        }
        El* palette =
            component::Command::New(cx, StrL("command-stocks"), story->search)
                ->Entries(story->stockEntries, story->nStockEntries)
                ->Bordered(false)
                ->Placeholder(StrL("Search stocks..."))
                ->Empty(StockEmpty(cx))
                ->MaxH(320)
                ->OnQuery(Listen(cx, &StockSearchDialog::OnQuery))
                ->OnConfirm(Listen(cx, &StockSearchDialog::OnConfirm))
                ->IntoEl();
        // `.min_h(px(320.))` keeps the dialog from jumping around as results
        // arrive. Command is not Styled here, so the floor goes on a box
        // around it: the palette is borderless and shares the dialog's
        // surface, so the two look the same.
        return PaletteDialog(cx,
                             Div(cx->a)->W(kFill)->MinH(320)->Child(palette),
                             Listen(cx, &StockSearchDialog::OnClose));
    }
};

// open_stock_search: the stock search as a dialog, starting from an empty
// query.
static void OpenStockSearch(CommandStory* self, Ctx* cx, const ClickEvent*) {
    CancelSearchTimer(self, cx);
    ShowPopular(self);
    if (component::CommandState* s = self->search.Get(cx)) {
        component::CommandSetQuery(s, cx, Str{});
        component::CommandSetLoading(s, cx, false);
    }
    Entity<StockSearchDialog> dialog = EntityNew<StockSearchDialog>(cx->app);
    dialog.Get(cx)->owner = Entity<CommandStory>{cx->self};
    WindowOpenDialog(cx, dialog);
}

El* CommandStory::Render(CommandStory* self, Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    CommandStoryInitKeys();
    if (!self->seeded) {
        self->seeded = true;
        SeedEntries();
        self->inlineState = EntityNewState<component::CommandState>(cx->app);
        self->dialog = EntityNewState<component::CommandState>(cx->app);
        self->quickActions = EntityNewState<component::CommandState>(cx->app);
        self->scrollable = EntityNewState<component::CommandState>(cx->app);
        self->variableRows = EntityNewState<component::CommandState>(cx->app);
        self->search = EntityNewState<component::CommandState>(cx->app);
        ShowPopular(self);
    }
    Listener confirm = Listen(cx, &OnCommandConfirm);

    El* page = Div(a)->FlexCol()->W(kFill)->Gap(24);

    El* inl = StorySection(
        cx, "Inline",
        "A palette rendered in place, with groups, icons and shortcuts.");
    StorySectionAdd(inl, component::Command::New(cx, StrL("command-inline"),
                                                 self->inlineState)
                             ->Entries(gSuggestionEntries, 3)
                             ->W(380)
                             ->OnConfirm(confirm)
                             ->IntoEl());
    page->Child(inl);

    El* dlg = StorySection(cx, "Dialog",
                           "A dialog palette with its search field focused, a "
                           "live match count and key hints.");
    StorySectionAdd(dlg, component::Button::New(cx, StrL("open-command-dialog"))
                             ->Outline()
                             ->Label(StrL("Open Menu"))
                             ->OnClick(Listen(cx, &OpenCommandDialog))
                             ->IntoEl());
    page->Child(dlg);

    El* quick =
        StorySection(cx, "Quick actions",
                     "A no-search palette focused on arrow-key navigation.");
    StorySectionAdd(quick, component::Command::New(cx, StrL("command-quick"),
                                                   self->quickActions)
                               ->Searchable(false)
                               ->Items(gQuickActions, 3)
                               ->W(380)
                               ->OnConfirm(confirm)
                               ->IntoEl());
    page->Child(quick);

    El* scroll = StorySection(cx, "Scrollable",
                              "More commands than fit, capped at 220px.");
    StorySectionAdd(scroll,
                    component::Command::New(cx, StrL("command-scrollable"),
                                            self->scrollable)
                        ->Entries(gScrollableEntries, 7)
                        ->MaxH(220)
                        ->W(380)
                        ->OnConfirm(confirm)
                        ->IntoEl());
    page->Child(scroll);

    El* rows = StorySection(cx, "Variable-height rows",
                            "Each custom row keeps its own intrinsic height "
                            "while the list remains virtualized.");
    StorySectionAdd(rows, component::Command::New(cx, StrL("command-rows"),
                                                  self->variableRows)
                              ->Items(gVariableRows, 2)
                              ->W(380)
                              ->OnConfirm(confirm)
                              ->IntoEl());
    page->Child(rows);

    El* search = StorySection(
        cx, "Search panel",
        "A palette used as a search panel whose custom filter checks symbols "
        "before company names — try \"a\", \"hk\" or \"tesla\".");
    StorySectionAdd(search,
                    component::Button::New(cx, StrL("open-stock-search"))
                        ->Outline()
                        ->Label(StrL("Search Stocks"))
                        ->OnClick(Listen(cx, &OpenStockSearch))
                        ->IntoEl());
    page->Child(search);

    if (self->lastCommand[0]) {
        El* last =
            StorySection(cx, "Last confirmed",
                         "The value reported by the last on_confirm callback.");
        StorySectionAdd(
            last, StoryTxt(cx, Str(self->lastCommand), 14, th.foreground));
        page->Child(last);
    }

    return page;
}

STORY_PAGE(StoryCommand, CommandStory);
