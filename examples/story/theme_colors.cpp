#include "Story.h"

#include <math.h>

// The theme viewer groups every token the way the Rust story's mapper does;
// ours lists the tokens this port has.
struct ColorRow {
    const char* group;
    const char* name;
    // The key that decides whether the row is shown before the Options menu
    // asks for the inherited ones. Rust compares two names that are only
    // sometimes the same: `mapper.rs`'s `canonical_key` against the set of
    // keys `ThemeConfigColors` serializes, which is schema.rs's `rename`. A
    // token whose mapper name and schema name differ — `accordion` against
    // `accordion.background`, `drag_border` against `drag.border`, every
    // `button.*` — can never match and reads as inherited however the file is
    // written. That is upstream's wart, and it is kept rather than improved
    // on, so the page opens on the same list on both sides; the rows where
    // the two names agree carry the key here and the rest carry none.
    const char* key;
    // A fill, not a colour: the tokens schema.rs lets a theme spell as a
    // gradient are shown as one, so the swatch says what the token paints
    // rather than what its first stop is.
    Background color;
};

// The themes the picker offers: what the registry holds, which is the two
// out of default-theme.json plus every theme file it found. A SearchableList
// keeps a pointer to the items, so they outlive the frame — and the names
// point into the registry's own arena, which outlives everything.
// One entry per theme in the registry, in its sorted order, with the light
// ones in a section of their own so the list reads the way Rust's does.
struct ThemeColorsStory;
static void FillThemeItems(ThemeColorsStory* self, App* app);

struct ThemeColorsStory {
    Entity<component::SelectState> themes = {};
    Vec<component::SearchableItem> themeItems;
    int openGroup = 0;
    bool showInherited = false;
    bool expandAll = false;
    bool optionsOpen = false;
    float rightScrollY = 0;
    InputState filter;
    bool seeded = false;

    ~ThemeColorsStory() { VecReset(themeItems); }

    static El* Render(ThemeColorsStory* self, Ctx* cx);
};

static void FillThemeItems(ThemeColorsStory* self, App* app) {
    if (self->themeItems.len > 0) {
        return;
    }
    // `themes/`, wherever an asset root has one — the pinned Rust clone ships
    // twenty of them. Two entries are all the picker has without it.
    ThemeRegistryLoadDir(app, StrL("themes"));
    for (int i = 0; i < ThemeRegistryCount(app); i++) {
        const ThemeConfig* cfg = ThemeRegistryAt(app, i);
        component::SearchableItem it = {};
        it.title = cfg->name;
        it.value = cfg->name;
        it.section = cfg->mode == ThemeMode::Dark ? 1 : 0;
        VecAppend(self->themeItems, it);
    }
}

enum {
    ThemeActInherited = 400,
    ThemeActExpandAll
};

static void ThemeOptionsToggle(ThemeColorsStory* self, Ctx* cx,
                               const ClickEvent*) {
    self->optionsOpen = !self->optionsOpen;
    Notify(cx);
}
static void ThemeOptionAct(ThemeColorsStory* self, Ctx* cx, const ClickEvent*,
                           intptr_t act) {
    if (act == ThemeActInherited) {
        self->showInherited = !self->showInherited;
    } else if (act == ThemeActExpandAll) {
        self->expandAll = !self->expandAll;
    }
    self->optionsOpen = false;
    Notify(cx);
}
static void ToggleColorGroup(ThemeColorsStory* self, Ctx* cx, const ClickEvent*,
                             intptr_t ix) {
    self->openGroup = self->openGroup == (int)ix ? -1 : (int)ix;
    Notify(cx);
}
static void ToggleThemeSelect(ThemeColorsStory* self, Ctx* cx,
                              const ClickEvent*) {
    component::SelectToggleOpen(self->themes.Get(cx), cx);
}
// Theme::apply_config, from the row the picker has selected. Rust does it
// from the registry the same way; the palette the file resolves to replaces
// the one for its mode, and the window switches to that mode so the change is
// on screen rather than one menu away.
static void SetTheme(ThemeColorsStory* self, Ctx* cx, const ClickEvent*) {
    component::SelectState* st = self->themes.Get(cx);
    if (!st || st->state.selected.len == 0) {
        return;
    }
    int ix = st->state.selected[0];
    if (ix < 0 || ix >= self->themeItems.len) {
        return;
    }
    const ThemeConfig* cfg =
        ThemeRegistryFind(cx->app, self->themeItems[ix].title);
    if (!cfg || !ThemeRegistryApply(cx->app, cfg)) {
        return;
    }
    ThemeSet(cx->app, cfg->mode);
    Notify(cx);
}

static void OnRightScroll(ThemeColorsStory* self, Ctx* cx,
                          const ScrollEvent* ev) {
    self->rightScrollY = ev->offsetY;
    Notify(cx);
}

static void FocusFilter(ThemeColorsStory* self, Ctx* cx, const ClickEvent*) {
    self->filter.focused = true;
    Notify(cx);
}

// Checkerboard: the wash the theme viewer's swatches sit on, so a colour
// with alpha in it reads as translucent rather than as a darker opaque one.
// Rust paints it from a gpui::canvas over the panel background; the same two
// greys come out of customPaint here, which runs after the element's own fill
// and before its children.
static const float kCheckerSquare = 12.f;

static Rgba CheckerBase(const App* app) {
    return ThemeGet(app) == ThemeMode::Dark ? RgbaHsla(0.f, 0.f, 0.1f, 1.f)
                                            : RgbaHsla(0.f, 0.f, 1.f, 1.f);
}

static void PaintCheckerboard(PaintCtx* ctx, El* e, void*) {
    Rgba c2 = ThemeGet(ctx->app) == ThemeMode::Dark
                  ? RgbaHsla(0.f, 0.f, 0.13f, 1.f)
                  : RgbaHsla(0.f, 0.f, 0.95f, 1.f);
    int rows = (int)ceilf(e->h / kCheckerSquare);
    int cols = (int)ceilf(e->w / kCheckerSquare);
    for (int row = 0; row < rows; row++) {
        for (int col = 0; col < cols; col++) {
            if ((row + col) % 2 != 0) {
                continue;
            }
            CanvasFillRect(ctx, e->x + kCheckerSquare * (float)col,
                           e->y + kCheckerSquare * (float)row, kCheckerSquare,
                           kCheckerSquare, c2);
        }
    }
}

// hsla_to_hex, lower case: the same round trip `Colorize::to_hex` makes,
// which is what puts a token whose file spells `#6366f1` on the page as
// `#6366f0`.
static Str HexOf(Ctx* cx, Rgba c) {
    return RgbaToHex(cx->a, c, false);
}

// The value a theme file would have to write to get this fill back.
static Str ValueOf(Ctx* cx, const Background& b) {
    if (!b.gradient) {
        return HexOf(cx, b.color);
    }
    return StoryFmt(cx, "linear-gradient(%gdeg, %s, %s)", (double)b.angle,
                    HexOf(cx, b.from.color), HexOf(cx, b.to.color));
}

// is_explicit: the active theme's file names this key itself, rather than
// leaving it to the fallback chain. The config keeps its `colors` object as
// the parsed document, which is the same thing Rust's `config_keys` set is
// built from.
static bool RowIsExplicit(const ThemeConfig* cfg, const char* key) {
    return key[0] != 0 && ThemeConfigNames(cfg, key);
}

El* ThemeColorsStory::Render(ThemeColorsStory* self, Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    if (!self->seeded) {
        self->seeded = true;
        FillThemeItems(self, cx->app);
        InputSetPlaceholder(&self->filter, StrL("Search..."));
        self->themes = component::SelectState::New(cx->app);
        component::SelectState* t = self->themes.Get(cx);
        if (t) {
            // The picker opens on the theme that is showing, which is the
            // one the registry has installed for the mode in force.
            Str active = ThemeRegistryActive(cx->app, ThemeGet(cx->app));
            int at = 0;
            for (int i = 0; i < self->themeItems.len; i++) {
                if (base::StrEq(self->themeItems[i].title, active)) {
                    at = i;
                    break;
                }
            }
            component::SearchableListSelectOnly(t->List(), at);
        }
    }
    if (self->filter.focused) {
        cx->win->input = &self->filter;
    }

    // Every field of Rust's `ThemeColor`, with the category and the name
    // `mapper.rs` splits its key into and the key `schema.rs` reads it from.
    // The list is generated from those two Rust files rather than picked by
    // hand, so the table is upstream's field set and not a subset of it — 139
    // rows, where this used to carry 78. The order is the story's own: Global,
    // Primary, Secondary, Accent and Base lead, the rest run by category, and
    // each category's rows by name.
    const ColorRow rows[] = {
        {"Global", "Accordion", "", th.tokens.accordion},
        {"Global", "Background", "background", th.tokens.background},
        {"Global", "Border", "border", th.border},
        {"Global", "Button", "", th.tokens.button},
        {"Global", "Foreground", "foreground", th.foreground},
        {"Global", "Link", "link", th.link},
        {"Global", "Overlay", "overlay", th.tokens.overlay},
        {"Global", "Popover", "", th.tokens.popover},
        {"Global", "Ring", "ring", th.ring},
        {"Global", "Scrollbar", "", th.tokens.scrollbarBg},
        {"Global", "Tiles", "", th.tokens.tiles},
        {"Primary", "Active Background", "primary.active.background",
         th.tokens.primaryActive},
        {"Primary", "Background", "primary.background", th.tokens.primary},
        {"Primary", "Foreground", "primary.foreground", th.primaryFg},
        {"Primary", "Hover Background", "primary.hover.background",
         th.tokens.primaryHover},
        {"Secondary", "Active Background", "secondary.active.background",
         th.tokens.secondaryActive},
        {"Secondary", "Background", "secondary.background",
         th.tokens.secondary},
        {"Secondary", "Foreground", "secondary.foreground", th.secondaryFg},
        {"Secondary", "Hover Background", "secondary.hover.background",
         th.tokens.secondaryHover},
        {"Accent", "Background", "accent.background", th.tokens.accent},
        {"Accent", "Foreground", "accent.foreground", th.accentFg},
        {"Base", "Blue", "base.blue", th.blue},
        {"Base", "Blue Light", "base.blue.light", th.blueLight},
        {"Base", "Cyan", "base.cyan", th.cyan},
        {"Base", "Cyan Light", "base.cyan.light", th.cyanLight},
        {"Base", "Green", "base.green", th.green},
        {"Base", "Green Light", "base.green.light", th.greenLight},
        {"Base", "Magenta", "base.magenta", th.magenta},
        {"Base", "Magenta Light", "base.magenta.light", th.magentaLight},
        {"Base", "Red", "base.red", th.red},
        {"Base", "Red Light", "base.red.light", th.redLight},
        {"Base", "Yellow", "base.yellow", th.yellow},
        {"Base", "Yellow Light", "base.yellow.light", th.yellowLight},
        {"Button", "Active", "", th.tokens.buttonActive},
        {"Button", "Danger", "", th.tokens.buttonDanger},
        {"Button", "Danger Active", "", th.tokens.buttonDangerActive},
        {"Button", "Danger Foreground", "", th.buttonDangerFg},
        {"Button", "Danger Hover", "", th.tokens.buttonDangerHover},
        {"Button", "Foreground", "", th.buttonFg},
        {"Button", "Hover", "", th.tokens.buttonHover},
        {"Button", "Info", "", th.tokens.buttonInfo},
        {"Button", "Info Active", "", th.tokens.buttonInfoActive},
        {"Button", "Info Foreground", "", th.buttonInfoFg},
        {"Button", "Info Hover", "", th.tokens.buttonInfoHover},
        {"Button", "Primary", "", th.tokens.buttonPrimary},
        {"Button", "Primary Active", "", th.tokens.buttonPrimaryActive},
        {"Button", "Primary Foreground", "", th.buttonPrimaryFg},
        {"Button", "Primary Hover", "", th.tokens.buttonPrimaryHover},
        {"Button", "Secondary", "", th.tokens.buttonSecondary},
        {"Button", "Secondary Active", "", th.tokens.buttonSecondaryActive},
        {"Button", "Secondary Foreground", "", th.buttonSecondaryFg},
        {"Button", "Secondary Hover", "", th.tokens.buttonSecondaryHover},
        {"Button", "Success", "", th.tokens.buttonSuccess},
        {"Button", "Success Active", "", th.tokens.buttonSuccessActive},
        {"Button", "Success Foreground", "", th.buttonSuccessFg},
        {"Button", "Success Hover", "", th.tokens.buttonSuccessHover},
        {"Button", "Warning", "", th.tokens.buttonWarning},
        {"Button", "Warning Active", "", th.tokens.buttonWarningActive},
        {"Button", "Warning Foreground", "", th.buttonWarningFg},
        {"Button", "Warning Hover", "", th.tokens.buttonWarningHover},
        {"Chart", "Bearish", "chart_bearish", th.chartBearish},
        {"Chart", "Bullish", "chart_bullish", th.chartBullish},
        {"Chart", "Color 1", "chart.1", th.chart1},
        {"Chart", "Color 2", "chart.2", th.chart2},
        {"Chart", "Color 3", "chart.3", th.chart3},
        {"Chart", "Color 4", "chart.4", th.chart4},
        {"Chart", "Color 5", "chart.5", th.chart5},
        {"Danger", "Active", "danger.active.background",
         th.tokens.dangerActive},
        {"Danger", "Background", "danger.background", th.tokens.danger},
        {"Danger", "Foreground", "danger.foreground", th.dangerFg},
        {"Danger", "Hover", "danger.hover.background", th.tokens.dangerHover},
        {"Description", "List Label", "", th.tokens.descListLabel},
        {"Description", "List Label Foreground", "", th.descListLabelFg},
        {"Drag", "Border", "", th.dragBorder},
        {"Drop", "Target", "", th.tokens.dropTarget},
        {"Group", "Box", "", th.tokens.groupBox},
        {"Group", "Box Foreground", "", th.groupBoxFg},
        {"Info", "Active", "info.active.background", th.tokens.infoActive},
        {"Info", "Background", "info.background", th.tokens.info},
        {"Info", "Foreground", "info.foreground", th.infoFg},
        {"Info", "Hover", "info.hover.background", th.tokens.infoHover},
        {"Input", "Border", "input.border", th.inputBorder},
        {"Input", "Caret", "caret", th.caret},
        {"Input", "Selection", "selection.background", th.tokens.selection},
        {"Link", "Active", "", th.linkActive},
        {"Link", "Hover", "", th.linkHover},
        {"List", "Active Background", "list.active.background",
         th.tokens.listActive},
        {"List", "Active Border", "list.active.border", th.listActiveBorder},
        {"List", "Background", "list.background", th.tokens.list},
        {"List", "Even Background", "list.even.background", th.tokens.listEven},
        {"List", "Head Background", "list.head.background", th.tokens.listHead},
        {"List", "Hover Background", "list.hover.background",
         th.tokens.listHover},
        {"Muted", "Background", "muted.background", th.tokens.muted},
        {"Muted", "Foreground", "muted.foreground", th.mutedFg},
        {"Popover", "Foreground", "", th.popoverFg},
        {"Progress", "Bar", "", th.tokens.progress},
        {"Scrollbar", "Thumb", "", th.tokens.scrollbarThumb},
        {"Scrollbar", "Thumb Hover", "", th.tokens.scrollbarThumbHover},
        {"Sidebar", "Accent Background", "sidebar.accent.background",
         th.tokens.sidebarAccent},
        {"Sidebar", "Accent Foreground", "", th.sidebarAccentFg},
        {"Sidebar", "Background", "sidebar.background", th.tokens.sidebar},
        {"Sidebar", "Border", "sidebar.border", th.sidebarBorder},
        {"Sidebar", "Foreground", "sidebar.foreground", th.sidebarFg},
        {"Sidebar", "Primary Background", "sidebar.primary.background",
         th.tokens.sidebarPrimary},
        {"Sidebar", "Primary Foreground", "sidebar.primary.foreground",
         th.sidebarPrimaryFg},
        {"Skeleton", "Background", "skeleton.background", th.tokens.skeleton},
        {"Slider", "Bar", "slider.background", th.tokens.sliderBar},
        {"Slider", "Thumb", "slider.thumb.background", th.tokens.sliderThumb},
        {"Status", "Bar", "", th.tokens.statusBar},
        {"Status", "Bar Border", "", th.statusBarBorder},
        {"Success", "Active", "success.active.background",
         th.tokens.successActive},
        {"Success", "Background", "success.background", th.tokens.success},
        {"Success", "Foreground", "success.foreground", th.successFg},
        {"Success", "Hover", "success.hover.background",
         th.tokens.successHover},
        {"Switch", "Background", "switch.background", th.tokens.switchBg},
        {"Switch", "Thumb", "switch.thumb.background", th.tokens.switchThumb},
        {"Tab", "Active Background", "tab.active.background",
         th.tokens.tabActiveBg},
        {"Tab", "Active Foreground", "tab.active.foreground", th.tabActiveFg},
        {"Tab", "Background", "tab.background", th.tokens.tab},
        {"Tab", "Foreground", "tab.foreground", th.tabFg},
        {"Tab Bar", "Background", "tab_bar.background", th.tokens.tabBar},
        {"Tab Bar", "Segmented Background", "tab_bar.segmented.background",
         th.tokens.tabBarSegmented},
        {"Table", "Active Background", "table.active.background",
         th.tokens.tableActive},
        {"Table", "Active Border", "table.active.border", th.tableActiveBorder},
        {"Table", "Background", "table.background", th.tokens.tableBg},
        {"Table", "Even Background", "table.even.background",
         th.tokens.tableEven},
        {"Table", "Foot", "", th.tokens.tableFoot},
        {"Table", "Foot Foreground", "", th.tableFootFg},
        {"Table", "Head Background", "table.head.background",
         th.tokens.tableHead},
        {"Table", "Head Foreground", "table.head.foreground", th.tableHeadFg},
        {"Table", "Hover Background", "table.hover.background",
         th.tokens.tableHover},
        {"Table", "Row Border", "table.row.border", th.tableRowBorder},
        {"Title", "Bar", "", th.tokens.titleBar},
        {"Title", "Bar Border", "", th.titleBarBorder},
        {"Warning", "Active", "warning.active.background",
         th.tokens.warningActive},
        {"Warning", "Background", "warning.background", th.tokens.warning},
        {"Warning", "Foreground", "warning.foreground", th.warningFg},
        {"Warning", "Hover", "warning.hover.background",
         th.tokens.warningHover},
        {"Window", "Border", "", th.windowBorder},
    };
    const int nAll = (int)(sizeof(rows) / sizeof(rows[0]));
    // The two filters the page carries, in the order Rust applies them: the
    // rows the theme file does not name are dropped unless the Options menu
    // asks for them, and then the query has to match the category, the name
    // or the start of the hex. What is left is a list of pointers, since a
    // group is a run of rows with the same category and a filter can empty
    // one out.
    const ThemeConfig* active = ThemeRegistryFind(
        cx->app, ThemeRegistryActive(cx->app, ThemeGet(cx->app)));
    Str query = InputValue(&self->filter);
    while (len(query) > 0 && query.s[0] == '#') {
        query = Str(query.s + 1, len(query) - 1);
    }
    const ColorRow** shown =
        (const ColorRow**)Alloc(a, (int)sizeof(ColorRow*) * nAll);
    int nRows = 0;
    for (int i = 0; i < nAll; i++) {
        if (!self->showInherited && !RowIsExplicit(active, rows[i].key)) {
            continue;
        }
        if (len(query) > 0) {
            Str hex = HexOf(cx, rows[i].color.color);
            bool hit = base::StrContainsI(Str(rows[i].group), query) ||
                       base::StrContainsI(Str(rows[i].name), query) ||
                       (len(hex) > len(query) + 1 &&
                        base::StrContainsI(Str(hex.s + 1, len(query)), query));
            if (!hit) {
                continue;
            }
        }
        shown[nRows++] = &rows[i];
    }

    El* page = Div(a)->FlexCol()->Gap(16)->W(kFill);

    // The theme picker on one row and the Options group on the next, the way
    // the Rust story stacks them.
    El* top = Div(a)->FlexCol()->W(kFill)->Gap(12);
    El* pick = Div(a)->FlexRow()->W(kFill)->Gap(8)->ItemsCenter();
    pick->Child(component::Select::New(cx, StrL("theme-select"), self->themes)
                    ->Items(self->themeItems.els, self->themeItems.len)
                    ->W(300)
                    ->OnToggle(Listen(cx, &ToggleThemeSelect))
                    ->IntoEl());
    pick->Child(component::Button::New(cx, StrL("set_theme"))
                    ->Label(StrL("Set Theme"))
                    ->Primary()
                    ->OnClick(Listen(cx, &SetTheme))
                    ->IntoEl());
    top->Child(pick);
    El* optRow = Div(a)->FlexRow()->W(kFill)->JustifyEnd();
    El* group = StoryToolbarGroup(cx);
    StoryToolbarOpt opts[2] = {
        {"Inherited Colors", self->showInherited, ThemeActInherited},
        {"Expand All", self->expandAll, ThemeActExpandAll},
    };
    group->Child(StoryToolbarDropdown(
        cx, StrL("theme-options"), StrL("Options"), self->optionsOpen,
        Listen(cx, &ThemeOptionsToggle), opts, 2, Listen(cx, &ThemeOptionAct)));
    optRow->Child(group);
    top->Child(optRow);
    page->Child(top);

    El* body = Div(a)->FlexRow()->W(kFill)->Gap(16)->ItemsStart();

    // Left: the search field over the category list.
    El* left = Div(a)->FlexCol()->W(300)->Gap(8);
    left->Child(component::Input::New(cx, StrL("theme-filter"), &self->filter)
                    ->Prefix(Div(a)->PadL(10)->Child(
                        IconEl(a, IconName::Search, 16)->Fg(th.mutedFg)))
                    ->OnFocus(Listen(cx, &FocusFilter))
                    ->IntoEl());
    // The categories stack with nothing between them — the `gap_2` above is
    // between the query field and the list, not between two rows of it.
    El* cats = Div(a)->FlexCol()->W(kFill);
    left->Child(cats);
    Listener toggleGroup = Listen(cx, &ToggleColorGroup);
    int groupIx = 0;
    for (int i = 0; i < nRows;) {
        Str name = Str(shown[i]->group);
        int end = i;
        while (end < nRows && StrEq(Str(shown[end]->group), name)) {
            end++;
        }
        bool open = self->expandAll || self->openGroup == groupIx;
        El* head = Div(a)
                       ->FlexRow()
                       ->W(kFill)
                       ->H(36)
                       ->PadX(8)
                       ->ItemsCenter()
                       ->JustifyBetween()
                       ->Radius(th.radius)
                       ->HoverBg(th.tokens.muted);
        head->Child(StoryTxt(cx, name, 16, th.foreground));
        head->Child(
            IconEl(a, open ? IconName::ChevronDown : IconName::ChevronRight, 16)
                ->Fg(th.mutedFg));
        head->Click(HashClickId(StoryFmt(cx, "theme-group-%d", groupIx)))
            ->OnClick(ListenerArg(toggleGroup, groupIx));
        cats->Child(head);
        if (open) {
            // The rows sit under a rail, indented from the group.
            El* items = Div(a)->FlexRow()->W(kFill)->PadL(12);
            items->Child(Div(a)->W(1)->H(kFill)->Bg(th.border));
            El* itemCol = Div(a)->FlexCol()->Flex1();
            items->Child(itemCol);
            for (int r = i; r < end; r++) {
                El* row = Div(a)
                              ->FlexRow()
                              ->W(kFill)
                              ->H(32)
                              ->PadX(12)
                              ->ItemsCenter()
                              ->JustifyBetween();
                row->Child(
                    StoryTxt(cx, Str(shown[r]->name), 16, th.foreground));
                row->Child(Div(a)
                               ->W(16)
                               ->H(16)
                               ->Radius(3)
                               ->Bg(shown[r]->color)
                               ->Border(1, th.border));
                itemCol->Child(row);
            }
            cats->Child(items);
        }
        i = end;
        groupIx++;
    }
    body->Child(left);

    // Right: every group with its swatch, name and hex, over the
    // checkerboard that shows through a translucent colour.
    // size_full(): the panel takes what the page has left rather than a
    // height of its own, which is what puts the last category as far down as
    // the window goes.
    float rightH = WindowSize(cx->win).dipH - 296;
    El* right = Div(a)
                    ->FlexCol()
                    ->Flex1()
                    ->H(rightH)
                    ->ClipY()
                    ->Radius(th.radiusLg)
                    ->Border(1, th.border)
                    ->Bg(CheckerBase(cx->app));
    right->customPaint = PaintCheckerboard;
    El* inner = Div(a)->FlexCol()->W(kFill)->PadX(16);
    right->Child(component::Scrollable::New(cx, StrL("theme-colors-right"))
                     ->H(rightH - 2)
                     ->ScrollY(self->rightScrollY)
                     ->OnScroll(Listen(cx, &OnRightScroll))
                     ->Child(inner)
                     ->IntoEl());
    for (int i = 0; i < nRows;) {
        Str name = Str(shown[i]->group);
        int end = i;
        while (end < nRows && StrEq(Str(shown[end]->group), name)) {
            end++;
        }
        // v_flex().w_full().gap_3().pt_4()
        El* cat = Div(a)->FlexCol()->W(kFill)->Gap(12)->PadT(16);
        // text_base().font_semibold().pb_2().border_b_1()
        cat->Child(
            Div(a)
                ->W(kFill)
                ->PadB(8)
                ->BorderB(1, th.border)
                ->Child(StoryTxt(cx, name, 16, th.foreground)->Semibold()));
        // div().flex().flex_wrap().gap_4(), one w(px(220.)) cell per colour.
        El* wrap = Div(a)->FlexRow()->FlexWrap()->W(kFill)->Gap(16);
        for (int r = i; r < end; r++) {
            // h_flex().gap_3().items_center()
            El* row = Div(a)->FlexRow()->Gap(12)->ItemsCenter();
            // div().size_16().rounded(radius).border_1().flex_shrink_0()
            row->Child(Div(a)
                           ->W(64)
                           ->H(64)
                           ->Shrink0()
                           ->Radius(th.radius)
                           ->Bg(shown[r]->color)
                           ->Border(1, th.border));
            El* text = Div(a)->FlexCol()->Gap(4)->Flex1();
            text->Child(StoryTxt(cx, Str(shown[r]->name), 14, th.foreground)
                            ->Medium());
            // A gradient's value is long; the cell is 220 wide either way.
            text->Child(
                StoryTxt(cx, ValueOf(cx, shown[r]->color), 14, th.mutedFg)
                    ->Truncate());
            row->Child(text);
            wrap->Child(Div(a)->W(220)->ClipX()->Child(row));
        }
        cat->Child(wrap);
        inner->Child(cat);
        i = end;
    }
    // pb_4 under the last category.
    inner->Child(Div(a)->H(16));
    body->Child(right);
    page->Child(body);
    return page;
}

STORY_PAGE(StoryThemeColors, ThemeColorsStory);
