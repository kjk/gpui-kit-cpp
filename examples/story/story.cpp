#include "Story.h"
#include "gpui.h"

using namespace gpui;

#include <stdlib.h>

#include <math.h>

static StoryPageNewFn gNew[StoryCount] = {};
static StoryPageKeyFn gKey[StoryCount] = {};

void StoryRegister(int story, StoryPageNewFn create, StoryPageKeyFn onKey) {
    if (story < 0 || story >= StoryCount) {
        return;
    }
    gNew[story] = create;
    gKey[story] = onKey;
}

// Resolve (creating on first view) the entity for the active story and render
// it with its own Ctx, so listeners inside a page bind to that page.
static EntityId StoryPageEntity(StoryApp* app, Ctx* cx) {
    int s = app->story;
    if (s < 0 || s >= StoryCount || !gNew[s]) {
        return EntityId{};
    }
    if (!app->pages[s].IsValid()) {
        app->pages[s] = gNew[s](cx->app);
    }
    return app->pages[s];
}

void StoryKeyRegistered(StoryApp* app, Ctx* cx, const KeyEvent* ev) {
    int s = app->story;
    if (s < 0 || s >= StoryCount || !gKey[s]) {
        return;
    }
    EntityId page = StoryPageEntity(app, cx);
    void* self = EntityGet(cx->app, page);
    if (!self) {
        return;
    }
    Ctx pageCx = *cx;
    pageCx.self = page;
    gKey[s](self, &pageCx, ev);
}

El* StoryRenderRegistered(StoryApp* app, Ctx* cx) {
    EntityId page = StoryPageEntity(app, cx);
    if (!page.IsValid()) {
        return StoryComingSoon(cx, app->story);
    }
    return EntityRender(cx->app, cx->win, cx->a, page);
}

static const StoryInfo kMeta[StoryCount] = {
    {"introduction", "Introduction",
     "UI components for building fantastic desktop application by using GPUI."},
    {"accordion", "Accordion",
     "The accordion uses collapse internally to make it collapsible."},
    {"alert", "Alert",
     "Communicate important status changes without interrupting the user's "
     "workflow."},
    {"alert-dialog", "AlertDialog",
     "Require a response before the user can continue."},
    {"attachment", "Attachment",
     "Composable file and media attachments with lifecycle states and "
     "actions."},
    {"avatar", "Avatar",
     "Represent a person or organization with an image or fallback."},
    {"badge", "Badge",
     "A red dot that indicates the number of unread messages."},
    {"breadcrumb", "Breadcrumb",
     "A breadcrumb navigation element that shows the current location in a "
     "hierarchy."},
    {"bubble", "Bubble",
     "A styleable chat surface for text, rich content, and reactions."},
    {"button", "Button",
     "Displays a button or a component that looks like a button."},
    {"calendar", "Calendar", "A calendar to select a date or date range."},
    {"chart", "Chart", "Beautiful Charts & Graphs."},
    {"checkbox", "Checkbox", "Select one or more independent options."},
    {"clipboard", "Clipboard",
     "Copy text or generated values to the clipboard."},
    {"collapsible", "Collapsible",
     "An interactive element that expands/collapses."},
    {"color-picker", "ColorPicker", "Choose and preview a color value."},
    {"combobox", "Combobox",
     "An autocomplete input paired with a searchable dropdown "
     "list."},
    {"command", "Command", "A searchable list of commands and quick actions."},
    {"data-table", "DataTable",
     "A complex data table with selection, sorting, column moving, "
     "and loading more."},
    {"date-picker", "DatePicker",
     "A date picker to select a date or date range."},
    {"description-list", "DescriptionList",
     "Present labels and values in a structured summary."},
    {"dialog", "Dialog", "Present focused content above the current view."},
    {"dock", "Dock",
     "A dockable layout of panels that can be moved, split and resized."},
    {"dropdown-button", "DropdownButton",
     "A button with an attached dropdown menu for additional "
     "options."},
    {"editor", "Editor",
     "Code editor with theme-aware syntax highlighting and "
     "folding."},
    {"form", "Form", "Form to collect multiple inputs."},
    {"group-box", "GroupBox",
     "A styled container element that with an optional title to groups "
     "related content together."},
    {"hover-card", "HoverCard",
     "A hover card displays content when hovering over a trigger "
     "element, with configurable delays."},
    {"icon", "Icon", "SVG Icons based on Lucide.dev"},
    {"image", "Image", "Image and SVG image supported."},
    {"input", "Input",
     "Capture and validate short-form text, credentials, "
     "identifiers, and formatted values."},
    {"kbd", "Kbd", "A tag style to display keyboard shortcuts"},
    {"label", "Label",
     "Display concise text with hierarchy, highlighting, and masking."},
    {"list", "List", "A list displays a series of items."},
    {"marker", "Marker",
     "A compact row for conversation status, notifications, and separators."},
    {"menu", "Menu", "Popup menu and context menu"},
    {"message", "Message",
     "Compose sender identity, metadata, rich content, and message "
     "actions."},
    {"message-scroller", "MessageScroller",
     "A virtualized message list with tail following, unread navigation, "
     "and anchor preservation."},
    {"native-menu", "NativeMenu",
     "A menu rendered by the operating system. Unlike `PopupMenu`, "
     "it is drawn by the OS and can extend beyond the window "
     "bounds — useful for small windows."},
    {"notification", "Notification",
     "Show transient feedback without interrupting the current task."},
    {"number-input", "NumberInput",
     "Adjust constrained numeric values precisely with typing or "
     "increment and decrement controls."},
    {"otp-input", "OtpInput",
     "Enter short verification and recovery codes with clear "
     "grouping and masking controls."},
    {"pagination", "Pagination",
     "Pagination with page navigation, next and previous links."},
    {"popover", "Popover", "Show focused content beside a trigger."},
    {"progress", "Progress",
     "Show task completion with determinate or loading indicators."},
    {"radio", "Radio", "Choose one option from a set."},
    {"rating", "Rating", "A simple interactive star rating component."},
    {"resizable", "Resizable", "The resizable panels."},
    {"scrollbar", "Scrollbar", "Add scrollbar to a scrollable element."},
    {"searchable-list", "SearchableList",
     "The searchable, sectioned list behind a Select and a ComboBox."},
    {"select", "Select",
     "Displays a list of options for the user to pick "
     "from—triggered by a button."},
    {"separator", "Separator",
     "A separator that can be either vertical or horizontal."},
    {"settings", "Settings",
     "A collection of settings groups and items for the "
     "application."},
    {"sheet", "Sheet", "Sheet for open a popup in the edge of the window"},
    {"shimmer", "Shimmer",
     "Reusable, theme-aware text loading effects with composable timing "
     "and appearance."},
    {"sidebar", "Sidebar",
     "A composable, themeable and customizable sidebar component."},
    {"skeleton", "Skeleton",
     "Use to show a placeholder while content is loading."},
    {"slider", "Slider",
     "Displays a slider control for selecting a value within a range."},
    {"spinner", "Spinner",
     "Displays an spinner showing the completion progress of a "
     "task."},
    {"status-bar", "StatusBar",
     "A horizontal bar with left/center/right regions, usually placed at the "
     "bottom."},
    {"stepper", "Stepper",
     "A step-by-step process for users to navigate through a series of "
     "steps."},
    {"switch", "Switch", "Turn a setting on or off."},
    {"table", "Table",
     "A basic table component for directly rendering tabular data."},
    {"tabs", "Tabs",
     "A set of layered sections of content—known as tab panels—that are "
     "displayed one at a time."},
    {"tag", "Tag",
     "A short item that can be used to categorize or label "
     "content."},
    {"textarea", "Textarea", "Input with multi-line mode."},
    {"theme-colors", "Theme Colors",
     "A color theme viewer to explore colors organized by "
     "categories."},
    {"tiles", "Tiles",
     "Panels that float over an area, each moved by its bar and resized by "
     "its edges."},
    {"toggle", "Toggle", "Turn an option on or off, alone or in a group."},
    {"tooltip", "Tooltip", "Describe a control on hover or keyboard focus."},
    // TreeStory has no description() in Rust, so its page has no line under
    // the title.
    {"tree", "Tree", nullptr},
    {"virtual-list", "VirtualList",
     "Add vertical or horizontal, or both scrollbars to a "
     "container, and use `virtual_list` to render a large number of "
     "items."},
};

const StoryInfo* StoryMeta(int i) {
    if (i < 0 || i >= StoryCount) {
        return &kMeta[0];
    }
    return &kMeta[i];
}

int StoryFromSlug(const char* slug) {
    if (!slug || !slug[0]) {
        return StoryWelcome;
    }
    for (int i = 0; i < StoryCount; i++) {
        if (base::StrEqI(Str(slug), kMeta[i].slug) ||
            base::StrEqI(Str(slug), kMeta[i].title)) {
            return i;
        }
    }
    return StoryWelcome;
}

Str StoryDup(Ctx* cx, const char* s) {
    Arena* a = cx->a;
    return StrDup(a, Str(s));
}

El* StoryTxt(Ctx* cx, Str s, float px, Rgba c) {
    Arena* a = cx->a;
    return TextEl(a, s)->Font(px)->Fg(c);
}

El* StorySection(Ctx* cx, const char* title, const char* desc) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    // Rust StorySection is an outline GroupBox: title sits above a bordered
    // content pane that centers its children (crates/story/src/lib.rs).
    // mb_6 on the GroupBox: every section carries its own bottom margin, on
    // top of whatever gap the page sets.
    El* wrap = Div(a)->FlexCol()->Gap(12)->PadB(24)->W(kFill);
    // GroupBox draws its title with line_height(relative(1.)), which the
    // description inherits, so the header is 16 + 4 + 12 tall.
    // The header is a row: the title column, and whatever sub-title the page
    // adds opposite it.
    El* headRow =
        Div(a)->FlexRow()->W(kFill)->Gap(16)->ItemsStart()->JustifyBetween();
    El* head = Div(a)->FlexCol()->MinW(0)->Flex1()->Gap(4);
    head->Child(StoryTxt(cx, StoryDup(cx, title), 16, th.mutedFg)
                    ->Medium()
                    ->LineHeight(1.f));
    if (desc && desc[0]) {
        head->Child(StoryTxt(cx, StoryDup(cx, desc), 12, th.mutedFg)
                        ->LineHeight(1.f)
                        ->Wrap());
    }
    // GroupBox's content pane, with StorySection's content_style on it:
    // bordered, p_4, rounded radius_lg, and centering the one child it has.
    El* pane = Div(a)
                   ->FlexCol()
                   ->Gap(16)
                   ->Pad(16)
                   ->W(kFill)
                   ->Border(1, th.border)
                   ->Radius(th.radiusLg)
                   // content_style's overflow_x_hidden: a section that names
                   // a width wider than the pane — the status bar page's
                   // .w(px(760.)) — is cut at the pane's edges rather than
                   // running out over the sidebar and the scrollbar.
                   ->ClipX()
                   ->ItemsCenter()
                   ->JustifyCenter();
    // section(): h_flex().w_full().flex_wrap().justify_center().items_center()
    // .gap_4() inside the pane. This is the element a page styles when it
    // says .w_128() or .v_flex() on its section.
    El* body = Div(a)
                   ->FlexRow()
                   ->FlexWrap()
                   ->Gap(16)
                   ->W(kFill)
                   ->ItemsCenter()
                   ->JustifyCenter();
    pane->Child(body);
    headRow->Child(head);
    wrap->Child(headRow);
    wrap->Child(pane);
    return wrap;
}

El* StorySectionSubTitle(El* section, El* sub) {
    if (!section || !sub || !section->first) {
        return section;
    }
    section->first->Child(sub);
    return section;
}

El* StorySectionBody(El* section) {
    if (!section || !section->first) {
        return nullptr;
    }
    // wrap = [headRow, pane]; pane = [body].
    El* pane = section->first;
    while (pane->next) {
        pane = pane->next;
    }
    return pane->first;
}

El* StorySectionAdd(El* section, El* child) {
    El* body = StorySectionBody(section);
    if (body && child) {
        body->Child(child);
    }
    return section;
}

static const char* StorySizeName(UiSize s) {
    switch (s) {
        case UiSize::XSmall:
            return "XSmall";
        case UiSize::Small:
            return "Small";
        case UiSize::Large:
            return "Large";
        default:
            return "Medium";
    }
}

// StoryToolbar::render joins its buttons into one segmented control: the
// group draws the outline, and the buttons after the first sit on their
// neighbour's border instead of drawing a second one.
// story_toolbar_group: `h_flex().w_full().justify_end()`, and no border of
// its own. Upstream's items are each a `Button::outline().small()` carrying
// their own, sharing an edge with the neighbour, so the row is exactly one
// item tall and the border lives inside it.
//
// This port paints the border once around the group instead, which draws the
// same joined pill — but a border takes room now, so putting it on the box
// would make the row two pixels taller than every one of upstream's. It goes
// on as the `ListActiveOverlay` ring does: an absolute child filling the
// group, drawing the stroke and costing no layout.
static El* ToolbarGroup(Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    return Div(a)
        ->FlexRow()
        ->ItemsStart()
        ->Bg(th.tokens.background)
        ->Radius(th.radius)
        ->Child(ListActiveOverlay(a, th.border, th.radius));
}

static El* ToolbarSep(Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    return Div(a)->W(1)->H(24)->Shrink0()->Bg(th.border);
}

// Button::outline().small(): h_6, px_2, text_sm. No Bg on the button — the
// group paints its background and border first, and an opaque child would
// cover the stroke that straddles the group's edge.
static El* ToolbarDropBtn(Ctx* cx, Str label) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    return Div(a)
        ->H(24)
        ->PadX(8)
        ->ItemsCenter()
        ->JustifyCenter()
        ->HoverBg(th.tokens.muted)
        ->Child(StoryTxt(cx, label, 14, th.foreground));
}

// PopupMenu::render_item: h 26, px_2, gap_x_1, text_sm, rounded, and a 12px
// icon gutter that Icon::empty() holds open on the rows that are not the
// checked one. `gutter` is Rust's has_left_icon: a menu with nothing checked
// has no column at all, so its rows sit flush left.
//
// Rust's rows fill the menu. A column here does not stretch its children, so
// they carry min_w(rems(8)) less the menu's padding instead, and the menu
// shrink-wraps around the widest of them.
static El* ToolbarCheckRow(Ctx* cx, Listener onAct, int act, const char* label,
                           bool on, bool gutter) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    El* row = Div(a)
                  ->H(26)
                  ->MinW(120)
                  ->PadX(8)
                  ->FlexRow()
                  ->Gap(4)
                  ->ItemsCenter()
                  ->Radius(th.radius)
                  ->HoverBg(th.tokens.accent);
    // The row needs a click id of its own, or HoverBg has nothing to match
    // against and the hovered row never lights up.
    row->Click(HashClickId(StoryFmt(cx, "story-toolbar-opt%d", act)))
        ->OnClick(ListenerArg(onAct, act));
    if (gutter) {
        El* mark = Div(a)->W(12)->H(12)->Shrink0();
        if (on) {
            mark->Child(IconEl(a, IconName::Check, 12)->Fg(th.foreground));
        }
        row->Child(mark);
    }
    row->Child(StoryTxt(cx, Str(label), 14, th.foreground));
    return row;
}

// popover_style, plus PopupMenu's p_1 and gap_y_0p5 around the items.
// PopupMenu::separator.
static El* ToolbarMenuSep(Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    // No width of its own: `align: stretch` is what makes it as wide as the
    // menu, where `W(kFill)` would make it as wide as whatever the menu is
    // floating over — and take the menu with it.
    return Div(a)->H(1)->Bg(th.border);
}

static El* ToolbarMenu(Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    return Div(a)
        ->FlexCol()
        ->Pad(4)
        ->Gap(2)
        ->Bg(th.tokens.background)
        ->Border(1, th.border)
        ->Radius(th.radius);
}

void StoryToolbarApply(StoryToolbarState* st, StoryAccordionOptions* opts,
                       int act) {
    switch (act) {
        case ToolbarOpenSize:
            st->sizeMenuOpen = !st->sizeMenuOpen;
            st->optsOpen = false;
            return;
        case ToolbarOpenOpts:
            st->optsOpen = !st->optsOpen;
            st->sizeMenuOpen = false;
            return;
        case ToolbarCloseAll:
            st->sizeMenuOpen = false;
            st->optsOpen = false;
            return;
        case ToolbarSizeXs:
            st->size = UiSize::XSmall;
            st->sizeMenuOpen = false;
            return;
        case ToolbarSizeSm:
            st->size = UiSize::Small;
            st->sizeMenuOpen = false;
            return;
        case ToolbarSizeMd:
            st->size = UiSize::Medium;
            st->sizeMenuOpen = false;
            return;
        case ToolbarSizeLg:
            st->size = UiSize::Large;
            st->sizeMenuOpen = false;
            return;
        default:
            break;
    }
    if (!opts) {
        return;
    }
    // Choosing a row runs it and the menu goes: PopupMenuConfirm calls
    // dismiss_all, so a checkbox row closes the dropdown even though what it
    // did was toggle rather than pick.
    st->optsOpen = false;
    switch (act) {
        case ToolbarOptMultiple:
            opts->multiple = !opts->multiple;
            return;
        case ToolbarOptIcon:
            opts->icon = !opts->icon;
            return;
        case ToolbarOptDisabled:
            opts->disabled = !opts->disabled;
            return;
        case ToolbarOptBordered:
            opts->bordered = !opts->bordered;
            return;
        default:
            return;
    }
}

// PopupMenu::on_mouse_down_out, which is what makes an open menu go away when
// the press lands past it. The listener sits on the Popup's root rather than
// on the menu it hangs, and the root is the trigger's own box: a press on the
// trigger is inside it and toggles the way it always did, and everything else
// -- the page behind, another dropdown's button, a row of the menu itself,
// which is out of flow and so outside these bounds -- closes it. A row's own
// handler still runs off the same release, and it closes the menu too, so the
// two agree.
static El* StoryToolbarDismissOnPressOut(El* el, bool open, Listener onAct) {
    if (open) {
        el->OnMouseUpOut(ListenerArg(onAct, ToolbarCloseAll));
    }
    return el;
}

// The size dropdown, which most pages carry.
static El* StorySizeMenu(Ctx* cx, StoryToolbarState* st, Listener onAct) {
    El* sizeTrig =
        ToolbarDropBtn(cx, StoryFmt(cx, "Size: %s", StorySizeName(st->size)))
            ->OnClick(ListenerArg(onAct, ToolbarOpenSize));
    El* sizeMenu = nullptr;
    if (st->sizeMenuOpen) {
        // One size is always the current one, so the check column is always
        // there.
        sizeMenu = ToolbarMenu(cx);
        sizeMenu->Child(ToolbarCheckRow(cx, onAct, ToolbarSizeXs, "XSmall",
                                        st->size == UiSize::XSmall, true));
        sizeMenu->Child(ToolbarCheckRow(cx, onAct, ToolbarSizeSm, "Small",
                                        st->size == UiSize::Small, true));
        sizeMenu->Child(ToolbarCheckRow(cx, onAct, ToolbarSizeMd, "Medium",
                                        st->size == UiSize::Medium, true));
        sizeMenu->Child(ToolbarCheckRow(cx, onAct, ToolbarSizeLg, "Large",
                                        st->size == UiSize::Large, true));
    }
    El* el = Popup::New(cx, StrL("story-size-menu"), sizeTrig)
                 ->AnchorRight()
                 ->Content(sizeMenu)
                 ->IntoEl();
    return StoryToolbarDismissOnPressOut(el, st->sizeMenuOpen, onAct);
}

El* StoryToolbarCore(Ctx* cx, StoryToolbarState* st,
                     const StoryToolbarOpt* rows, int nrows, Listener onAct,
                     bool withSize) {
    Arena* a = cx->a;
    El* row = Div(a)->FlexRow()->W(kFill)->JustifyEnd()->ItemsStart();
    El* group = ToolbarGroup(cx);
    row->Child(group);

    if (withSize) {
        group->Child(StorySizeMenu(cx, st, onAct));
    }

    if (rows && nrows > 0) {
        group->Child(ToolbarSep(cx));
        El* optTrig = ToolbarDropBtn(cx, StrL("Options"))
                          ->OnClick(ListenerArg(onAct, ToolbarOpenOpts));
        El* optMenu = nullptr;
        if (st->optsOpen) {
            // has_left_icon: the column is there only while something in the
            // menu is checked. A row built with menu() rather than
            // menu_with_check() never is.
            bool gutter = false;
            for (int i = 0; i < nrows; i++) {
                gutter = gutter || (rows[i].checked && !rows[i].plain);
            }
            optMenu = ToolbarMenu(cx);
            for (int i = 0; i < nrows; i++) {
                if (rows[i].sep) {
                    optMenu->Child(ToolbarMenuSep(cx));
                }
                optMenu->Child(
                    ToolbarCheckRow(cx, onAct, rows[i].act, rows[i].label,
                                    rows[i].checked && !rows[i].plain, gutter));
            }
        }
        group->Child(StoryToolbarDismissOnPressOut(
            Popup::New(cx, StrL("story-opts-menu"), optTrig)
                ->AnchorRight()
                ->Content(optMenu)
                ->IntoEl(),
            st->optsOpen, onAct));
    }
    return row;
}

El* StoryToolbarGroup(Ctx* cx) {
    return ToolbarGroup(cx);
}

El* StoryToolbarDivider(Ctx* cx) {
    return ToolbarSep(cx);
}

El* StoryToolbarDropdown(Ctx* cx, Str id, Str label, bool open, Listener onOpen,
                         const StoryToolbarOpt* rows, int nrows,
                         Listener onAct) {
    El* trigger = ToolbarDropBtn(cx, label)->OnClick(onOpen);
    El* menu = nullptr;
    if (open) {
        bool gutter = false;
        for (int i = 0; i < nrows; i++) {
            gutter = gutter || (rows[i].checked && !rows[i].plain);
        }
        menu = ToolbarMenu(cx);
        for (int i = 0; i < nrows; i++) {
            if (rows[i].sep) {
                menu->Child(ToolbarMenuSep(cx));
            }
            menu->Child(ToolbarCheckRow(cx, onAct, rows[i].act, rows[i].label,
                                        rows[i].checked && !rows[i].plain,
                                        gutter));
        }
    }
    return StoryToolbarDismissOnPressOut(
        Popup::New(cx, id, trigger)->AnchorRight()->Content(menu)->IntoEl(),
        open, onAct);
}

El* StoryComingSoon(Ctx* cx, int story) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    const StoryInfo* m = StoryMeta(story);
    return Div(a)
        ->FlexCol()
        ->Gap(8)
        ->Pad(8)
        ->Child(StoryTxt(cx, StoryDup(cx, m->title), 16, th.foreground)
                    ->Semibold())
        ->Child(StoryTxt(cx, StoryDup(cx, "This story is not ported yet."), 13,
                         th.mutedFg));
}

static bool StoryMatches(const StoryInfo* m, const char* q) {
    if (!q || !q[0]) {
        return true;
    }
    return StrContainsI(Str(m->title), Str(q)) ||
           StrContainsI(Str(m->slug), Str(q));
}

// Gallery::set_active_story
static void OpenStory(StoryApp* app, Ctx* cx, const ClickEvent*,
                      intptr_t story) {
    app->search.focused = false;
    cx->win->input = nullptr;
    app->story = (int)story;
    app->scrollY = 0;
    WindowSelectionClear(cx->win);
    Notify(cx);
}

static void FocusSearch(StoryApp* app, Ctx* cx, const ClickEvent*) {
    app->search.focused = true;
    cx->win->input = &app->search;
    Notify(cx);
}

static void ClearSearch(StoryApp* app, Ctx* cx, const ClickEvent*) {
    InputSetValue(&app->search, Str{});
    app->search.focused = false;
    cx->win->input = nullptr;
    Notify(cx);
}

// The pane that was scrolled reports where it should now be — by the wheel
// over it, or by a press or a drag on its bar. The view owns the offsets, so
// it is the one that stores them.
static int SidebarScrollId() {
    return HashClickId(StrL("story-sidebar-scroll"));
}

static int PageScrollId() {
    return HashClickId(StrL("story-page-scroll"));
}

static void OnPaneScroll(StoryApp* app, Ctx* cx, const ScrollEvent* ev) {
    if (ev->id == SidebarScrollId()) {
        app->sideScrollY = ev->offsetY;
    } else {
        app->scrollY = ev->offsetY;
    }
    Notify(cx);
}

static El* SidebarList(StoryApp* app, Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    El* list = Div(a)->FlexCol()->Gap(2)->Pad(8);
    const char* q = InputCStr(&app->search);
    for (int i = 0; i < StoryCount; i++) {
        const StoryInfo* m = StoryMeta(i);
        if (!StoryMatches(m, q)) {
            continue;
        }
        bool on = app->story == i;
        El* row = Div(a)
                      ->H(32)
                      ->W(kFill)
                      ->PadX(10)
                      ->ItemsCenter()
                      ->Radius(6)
                      ->OnClick(Listen(cx, &OpenStory, i))
                      ->FocusId(HashClickId(StrDup(a, fmt("story-nav-%d", i))));
        // SidebarMenuItem is text_sm.
        El* label = StoryTxt(cx, Str(m->title), 14, th.sidebarFg);
        if (on) {
            label->Semibold();
            row->Bg(th.tokens.secondary);
        } else {
            row->HoverBg(th.tokens.secondary);
        }
        row->Child(label);
        list->Child(row);
    }
    return list;
}

// gallery.rs frames the field itself rather than styling it:
//
//     div().bg(sidebar_accent).rounded_full().px_1()
//          .child(Input::new(&search).appearance(false).cleanable(true))
//
// so the pill is the sidebar's and everything inside it is the themed Input —
// which is where the field's text size, its padding and its clear button come
// from. The unstyled `gpui::Input` this used to hold is the editor underneath
// that one, and it draws at the base's own 12px rather than at input_text_size.
static El* SearchBox(StoryApp* app, Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    El* box = Div(a)
                  ->H(36)
                  ->W(kFill)
                  ->PadX(6)
                  ->FlexRow()
                  ->ItemsCenter()
                  ->Radius(18)
                  ->Bg(th.tokens.secondary)
                  ->FocusId(HashClickId(StrL("story-search")));
    box->Child(component::Input::New(cx, StrL("story-search"), &app->search)
                   ->Appearance(false)
                   ->Cleanable()
                   ->OnFocus(Listen(cx, &FocusSearch))
                   ->OnClear(Listen(cx, &ClearSearch))
                   ->IntoEl()
                   ->Flex1());
    return box;
}

// gallery.rs wraps the sidebar in
//   resizable_panel().size(px(255.)).size_range(px(200.)..px(320.))
// GPUI turns that 255 into a fraction of the group at first layout, and the
// story window opens at 1600 wide (crates/story/src/lib.rs), so the sidebar
// tracks 255/1600 of the window width, clamped to the size_range. It is 200 at
// half a 1920 screen and 221 at 1400, which is what the Rust app draws.
static float SidebarWidth(Ctx* cx) {
    float w = WindowSize(cx->win).dipW * (255.f / 1600.f);
    w = (float)lroundf(w); // GPUI rounds; truncating is off by one at 1400
    if (w < 200.f) {
        w = 200.f;
    }
    if (w > 320.f) {
        w = 320.f;
    }
    return w;
}

static El* Sidebar(StoryApp* app, Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    float w = app->collapsed ? 56.f : SidebarWidth(cx);
    // Rust puts this sidebar in a `resizable_panel()`, whose width is not up
    // for negotiation with the pane beside it. A plain flex item is, so it
    // says so: without this the content pane's own minimum squeezes the
    // sidebar below the 200 the width helper floors at.
    El* side = Div(a)->W(w)->H(kFill)->FlexCol()->Shrink0()->Bg(th.sidebar);
    El* header = Div(a)->FlexCol()->Pad(12)->Gap(16);
    El* brand = Div(a)->FlexRow()->Gap(10)->ItemsCenter();
    El* logo = Div(a)
                   ->W(32)
                   ->H(32)
                   ->Radius(8)
                   ->Bg(th.tokens.primary)
                   ->ItemsCenter()
                   ->JustifyCenter()
                   ->Shrink0()
                   ->Child(IconEl(a, IconName::GalleryVerticalEnd, 16)
                               ->Fg(th.primaryFg));
    brand->Child(logo);
    if (!app->collapsed) {
        El* names = Div(a)->FlexCol();
        names->Child(StoryTxt(cx, StrL("GPUI Kit"), 14, th.sidebarFg)
                         ->Semibold());
        names->Child(StoryTxt(cx, StrL("Component showcase"), 12, th.mutedFg));
        brand->Child(names);
    }
    header->Child(brand);
    if (!app->collapsed) {
        header->Child(SearchBox(app, cx));
    }
    side->Child(header);
    El* scroller = Div(a)
                       ->FlexCol()
                       ->Flex1()
                       ->MinH(0)
                       ->ClipY()
                       ->ScrollY(app->sideScrollY)
                       ->ScrollId(SidebarScrollId())
                       ->OnScroll(Listen(cx, &OnPaneScroll))
                       ->W(kFill);
    if (!app->collapsed) {
        scroller->Child(SidebarList(app, cx));
    }
    side->Child(scroller);
    return side;
}

static El* Header(StoryApp* app, Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    const StoryInfo* m = StoryMeta(app->story);
    return Div(a)
        ->W(kFill)
        ->Pad(16)
        ->FlexCol()
        ->Gap(4)
        ->Shrink0()
        ->BorderB(1, th.border)
        ->Child(StoryTxt(cx, Str(m->title), 24, th.foreground)->Semibold())
        // gallery.rs writes the description into a plain `div()` with no
        // width of its own, so a long one runs off the end of the pane
        // rather than wrapping under the title.
        ->Child(StoryTxt(cx, Str(m->description), 16, th.mutedFg));
}

// AppTitleBar in crates/story, on top of component::TitleBar: the menu and
// tool buttons claim their own hit rectangles, and the surface left over is
// the window drag region.
static El* StoryTitleMenuItem(Ctx* cx, Str label, bool semibold) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    El* text = StoryTxt(cx, label, 14, th.foreground);
    if (semibold) {
        text->Semibold();
    }
    return Div(a)
        ->H(kFill)
        ->PadX(8)
        ->ItemsCenter()
        ->Radius(th.radius)
        ->Click(HashClickId(StoryFmt(cx, "story-title-%s", label)))
        ->HoverBg(th.tokens.muted)
        ->Child(text);
}

// The window's notification list, which is now the runtime's:
// `window.notifications(cx)` and `window.push_notification(..)` are
// WindowExt, so a page pushes one without the app entity being in the way and
// a notification outlives leaving the page that raised it.
Entity<component::NotificationListState> StoryNotifications(Ctx* cx) {
    return WindowNotifications(cx);
}

void StoryPushNotification(Ctx* cx, Str message) {
    WindowPushNotification(cx, message);
}

static int StoryNotificationCount(Ctx* cx) {
    return WindowNotificationCount(cx);
}

// ─── the story's actions ──────────────────────────────────────────────────
//
// `actions!(story, [..])`, spelled out: an action here is its name hashed.
// Every row of every menu in this file names one of these and carries no
// handler of its own — which is what lets the same row be drawn into the
// title bar, installed into the macOS menu bar, or reached by the chord the
// keymap binds to it, and run the same thing all three ways.

#define STORY_ACTION(fn, spelled)                     \
    static uint32_t fn() {                            \
        static uint32_t id = ActionOf(StrL(spelled)); \
        return id;                                    \
    }

STORY_ACTION(ActAbout, "story::About")
STORY_ACTION(ActOpen, "story::Open")
STORY_ACTION(ActQuit, "story::Quit")
STORY_ACTION(ActNewWindow, "story::NewWindow")
STORY_ACTION(ActCloseWindow, "story::CloseWindow")
STORY_ACTION(ActDocumentation, "story::Documentation")
// The payload rides on the action, which is what `SwitchThemeMode(mode)`,
// `SelectTheme(name)`, `SelectFont(px)`, `SelectRadius(px)` and
// `SelectScrollbarMode(mode)` carry in Rust: the mode as 0 or 1, a theme as
// its place in the registry, and the other three as the value itself.
STORY_ACTION(ActSwitchThemeMode, "story::SwitchThemeMode")
STORY_ACTION(ActSelectTheme, "story::SelectTheme")
STORY_ACTION(ActSelectLocale, "story::SelectLocale")
STORY_ACTION(ActSelectFont, "story::SelectFont")
STORY_ACTION(ActSelectRadius, "story::SelectRadius")
STORY_ACTION(ActSelectScrollbarMode, "story::SelectScrollbarMode")
STORY_ACTION(ActToggleListActiveHighlight, "story::ToggleListActiveHighlight")
STORY_ACTION(ActToggleFpsMonitor, "story::ToggleFpsMonitor")
STORY_ACTION(ActToggleAppMenuBar, "story::ToggleAppMenuBar")
// The two rows Rust's menu does not have, and so the two actions it does not
// declare.
STORY_ACTION(ActToggleReduceMotion, "story::ToggleReduceMotion")
STORY_ACTION(ActToggleFocusRing, "story::ToggleFocusRing")

// language_menu(): the three the Rust story offers. The names are the
// language's own, which is what a language menu shows everywhere — a reader
// who wants Chinese is not looking for the word "Chinese" in English. The
// catalogue behind them carries more (zh-HK, zh-TW, it), and an application
// that wants those in its menu names them the same way.
struct StoryLocale {
    const char* code;
    const char* label;
};

static const StoryLocale kStoryLocales[] = {
    {"en", "English"},
    {"zh-CN", "简体中文"},
    {"fr", "Français"},
};

static const int kStoryLocaleCount =
    (int)(sizeof(kStoryLocales) / sizeof(kStoryLocales[0]));

// AppTitleBar's FontSizeSelector, which is the Appearance menu behind the
// Settings2 button. Every row names one of the actions above and carries the
// value it sets, which is what Rust's `SelectFont(18)` is; the table says
// which action a kind of row names, whether it is ticked, and what it reads
// back to say so.
enum class ApKind : uint8_t {
    Label,
    Sep,
    Font,
    Radius,
    Scroll,
    ListHighlight,
    Fps,
    MenuBar,
    Reduce,
    Ring
};

struct ApRow {
    ApKind kind;
    const char* label;
    // The font size or radius in DIPs, or the scrollbar mode; unused by the
    // two toggles, which read what they toggle.
    float value;
};

static const ApRow kAppearance[] = {
    {ApKind::Label, "Font Size", 0},
    {ApKind::Font, "Large", 18},
    {ApKind::Font, "Medium (default)", 16},
    {ApKind::Font, "Small", 14},
    {ApKind::Sep, nullptr, 0},
    {ApKind::Label, "Border Radius", 0},
    {ApKind::Radius, "8px", 8},
    {ApKind::Radius, "6px (default)", 6},
    {ApKind::Radius, "4px", 4},
    {ApKind::Radius, "0px", 0},
    {ApKind::Sep, nullptr, 0},
    {ApKind::Label, "Scrollbar", 0},
    {ApKind::Scroll, "Scrolling", (float)ScrollbarMode::Scrolling},
    {ApKind::Scroll, "Hover to show", (float)ScrollbarMode::Hover},
    {ApKind::Scroll, "Always show", (float)ScrollbarMode::Always},
    {ApKind::Sep, nullptr, 0},
    {ApKind::ListHighlight, "List Active Highlight", 0},
    {ApKind::Fps, "FPS Monitor", 0},
    // ToggleAppMenuBar. Rust's row is in this menu too, and switchable on
    // every platform: on a Mac the menus are already in the system bar, and
    // this is what puts the component itself on screen beside them.
    {ApKind::MenuBar, "App Menu Bar", 0},
    // Not a row Rust's menu has. `cx.reduce_motion()` is the desktop's own
    // setting, which a gallery of components that move is the one place you
    // would want to try both ways without leaving to change it.
    {ApKind::Reduce, "Reduce Motion", 0},
    // Nor this one. `Theme::focus_ring` is meant to be set once by an
    // application whose layout clips its containers; a gallery is where you
    // can see what the two look like side by side.
    {ApKind::Ring, "Focus Ring", 0},
};

static const int kAppearanceRows = (int)(sizeof(kAppearance) / sizeof(ApRow));

// menu_with_check: which row is the one in force.
static bool ApChecked(const StoryApp* app, Ctx* cx, const ApRow& r) {
    switch (r.kind) {
        case ApKind::Font:
            return ThemeFontSize(cx->app) == r.value;
        case ApKind::Radius:
            return ThemeNow(cx->app).radius == r.value;
        case ApKind::Scroll:
            return ScrollbarModeNow(cx->app) == (ScrollbarMode)(int)r.value;
        case ApKind::ListHighlight:
            return ListSettingsNow(cx->app).activeHighlight;
        case ApKind::Fps:
            return app->fpsMonitor;
        case ApKind::MenuBar:
            return app->appMenuBar;
        case ApKind::Reduce:
            return MotionReduced();
        case ApKind::Ring:
            return ThemeFocusRing(cx->app);
        default:
            return false;
    }
}

// Which action a row of the table dispatches. The three that carry a value
// hand it over as the action's payload — `SelectFont(18)` — and the toggles
// carry nothing, since what they flip is what they read.
static uint32_t ApAction(ApKind kind) {
    switch (kind) {
        case ApKind::Font:
            return ActSelectFont();
        case ApKind::Radius:
            return ActSelectRadius();
        case ApKind::Scroll:
            return ActSelectScrollbarMode();
        case ApKind::ListHighlight:
            return ActToggleListActiveHighlight();
        case ApKind::Fps:
            return ActToggleFpsMonitor();
        case ApKind::MenuBar:
            return ActToggleAppMenuBar();
        case ApKind::Reduce:
            return ActToggleReduceMotion();
        case ApKind::Ring:
            return ActToggleFocusRing();
        default:
            return 0;
    }
}

// create_new_window_with_size: everything one gallery window is, so the menu
// item below and GpuiMain open the same thing. Rust passes 1600x1200 and lets
// GPUI cap it at 85% of the display and centre it, which is what WindowOpen
// does here.
static void OnUnhandledClick(StoryApp* app, Ctx* cx, const ClickEvent* ev);
static void OnKey(StoryApp* app, Ctx* cx, const KeyEvent* ev);

// create_new_window("GPUI Kit", ..): the name the window is opened
// under, and so the name the title bar shows when the menus are not in it —
// Rust hands the same string to `create_new_window` and to `AppTitleBar`.
static Str StoryWindowTitle() {
    return StrL("GPUI Kit C++");
}

static void StoryInitKeys();

static Window* StoryOpenWindow(App* app, int story) {
    StoryInitKeys();
    Entity<StoryApp> view = EntityNew<StoryApp>(app);
    StoryApp* self = view.Get(app);
    if (!self) {
        return nullptr;
    }
    self->story = story;
    const char* envFps = getenv("GPUI_FPS");
    if (envFps && envFps[0] && envFps[0] != '0') {
        self->fpsMonitor = true;
    }
    InputSetPlaceholder(&self->search, StrL("Search…"));
    WinOpts opts = {};
    // TitleBar::window_options(): the story owns its title bar on every
    // platform. macOS keeps the traffic lights over a transparent one,
    // Windows and X11 have none, so component::TitleBar draws the minimize /
    // maximize / close controls there itself.
    opts.clientTitleBar = true;
    // `story_window_background()`: upstream stopped advertising an alpha
    // surface on Linux, because a compositor was showing the desktop through
    // a light theme even though every story is designed against an opaque
    // canvas. There is nothing to change here — `window_linux.cpp` asks X11
    // for an ordinary opaque visual and never sets an ARGB one — so this is
    // where that decision would live if the seam existed.
    Window* win =
        WindowOpenView(app, StoryWindowTitle(), 1600, 1200, view.id, opts);
    if (!win) {
        return nullptr;
    }
    WindowOnUnhandledClick(win, ListenTo(view, &OnUnhandledClick));
    WindowOnKey(win, ListenTo(view, &OnKey));
    return win;
}

// The About dialog, which the Help menu raises. It is an entity of its own
// rather than something a page renders, which is what WindowExt is for: the
// menu handler has no view that draws dialogs and does not need one, and the
// dialog outlives whichever page happens to be showing. Rust writes the same
// thing as `window.open_alert_dialog(cx, |alert, ..| ..)`.
struct AboutDialog {
    static void OnClose(AboutDialog*, Ctx* cx, const ClickEvent*) {
        WindowCloseDialog(cx);
    }

    static El* Render(AboutDialog*, Ctx* cx) {
        Arena* a = cx->a;
        const Theme& th = ThemeNow(cx->app);
        El* body = Div(a)->FlexCol()->Gap(8)->W(kFill);
        body->Child(
            StoryTxt(cx,
                     StrL("A C++ port of longbridge/gpui-kit: the "
                          "same components, the same theme, no Rust and "
                          "no STL."),
                     14, th.mutedFg)
                ->W(kFill)
                ->Wrap());
        body->Child(
            StoryTxt(cx, StrL("github.com/longbridge/gpui-kit"), 14, th.mutedFg)
                ->W(kFill));
        Listener close = Listen(cx, &AboutDialog::OnClose);
        return component::Dialog::New(cx)
            ->Open(true)
            ->Title(StrL("GPUI Kit"))
            ->Description(StrL("Component showcase  v0.5.1"))
            ->Body(body)
            ->W(420)
            ->CloseButton()
            ->OkText(StrL("Close"))
            ->OnOk(close)
            ->OnClose(close)
            ->OnCancel(close)
            ->IntoEl(WindowSize(cx->win));
    }
};

static El* AppearanceMenu(StoryApp* app, Ctx* cx) {
    component::PopupMenu* menu =
        component::PopupMenu::New(cx, StrL("story-appearance-menu"));
    for (int i = 0; i < kAppearanceRows; i++) {
        const ApRow& r = kAppearance[i];
        switch (r.kind) {
            case ApKind::Label:
                menu->Label(Str(r.label));
                break;
            case ApKind::Sep:
                menu->Separator();
                break;
            default:
                menu->MenuWithAction(Str(r.label), ApAction(r.kind),
                                     (intptr_t)r.value);
                menu->Checked(ApChecked(app, cx, r));
                break;
        }
    }
    // check_side(Right): the tick sits on the far edge, so the labels start
    // flush.
    menu->CheckSide(Side::Right);
    return component::DropdownMenu::New(cx, StrL("story-appearance"))
        ->Trigger(component::Button::New(cx, StrL("story-title-settings"))
                      ->Icon(IconName::Settings2)
                      ->Ghost()
                      ->Compact()
                      ->WithSize(UiSize::Small)
                      ->Tooltip(StrL("Appearance"))
                      ->IntoEl()
                      ->Cursor(CursorKind::Pointer))
        ->Menu(menu)
        // Anchor::TopRight: the menu's right edge lines up with the button's,
        // which is what keeps it on screen at the corner of the window.
        ->AnchorRight()
        ->IntoEl();
}

static void OnGithub(StoryApp*, Ctx*, const ClickEvent*) {
    OpenUrl(StrL("https://github.com/longbridge/gpui-kit"));
}

// ─── app_menus.rs ─────────────────────────────────────────────────────────
//
// One table, two bars. Rust builds its `Vec<Menu>` once and hands it to both
// `cx.set_menus` — the macOS menu bar, which belongs to the front application
// and sits at the top of the screen rather than in any of its windows — and
// to the AppMenuBar drawn into the title bar. The same rows here become the
// MenuDefs the runtime installs and the PopupMenus the title bar opens, so
// neither bar can drift from the other.
//
// A row carries an action and nothing else, the way `MenuItem::action` does.
// Whichever bar it was chosen from, and whether it was chosen with the
// pointer or with the chord the menu shows beside it, the same handler runs —
// the one an element registered for that action, which is also where the
// keystroke on its own would have arrived.
//
// One menu Rust has is not here: `Language` wants rust_i18n. One it does not
// is: `Window`, because `App` has held a window list and ended its loop with
// the last one for a while, and nothing else opens a second one.

static void OnAboutAction(StoryApp*, Ctx* cx, const ActionEvent*) {
    // window.open_dialog(cx, ..): the window takes the entity and Root draws
    // it, whatever page is up.
    WindowOpenDialog(cx, EntityNew<AboutDialog>(cx->app));
}

static void OnOpenAction(StoryApp*, Ctx*, const ActionEvent*) {
    // `cx.on_action(|_action: &Open, _cx| {})` in the gallery: the row is
    // there and the handler is empty, because a component gallery has nothing
    // to open a file into. What a real one does with the same action is
    // `on_action_open` in the markdown example — this tree has the dialog for
    // it (PromptForPathTemp), and it is that example that wants it, not this
    // menu.
}

// `cx.on_action(|_: &Quit, cx| cx.quit())`: every window, not the one the row
// was chosen from. Close Window below is the one that closes one.
static void OnQuitAction(StoryApp*, Ctx* cx, const ActionEvent*) {
    AppQuitAll(cx->app);
}

static void OnNewWindowAction(StoryApp*, Ctx* cx, const ActionEvent*) {
    StoryOpenWindow(cx->app, StoryFromSlug(""));
}

static void OnCloseWindowAction(StoryApp*, Ctx* cx, const ActionEvent*) {
    AppClose(cx->win);
}

static void OnDocumentationAction(StoryApp*, Ctx*, const ActionEvent*) {
    OpenUrl(StrL("https://github.com/longbridge/gpui-kit"));
}

// SwitchThemeMode(ThemeMode::Light | ::Dark), checked against the mode in
// force.
static void OnSwitchThemeModeAction(StoryApp*, Ctx* cx, const ActionEvent* ev) {
    ThemeSet(cx->app, ev->arg == 0 ? ThemeMode::Light : ThemeMode::Dark);
    Notify(cx);
}

// SelectTheme(name): the registry resolves the file into the palette for its
// own mode, and switching to that mode is what puts it on screen.
static void OnSelectThemeAction(StoryApp*, Ctx* cx, const ActionEvent* ev) {
    const ThemeConfig* cfg = ThemeRegistryAt(cx->app, (int)ev->arg);
    if (!cfg || !ThemeRegistryApply(cx->app, cfg)) {
        return;
    }
    ThemeSet(cx->app, cfg->mode);
    Notify(cx);
}

// cx.bind_keys([..]) in the story's init. A menu row shows the chord bound to
// its action and nothing else — there is no shortcut field on a row, here or
// in Rust — so an application that wants ⌘Q beside Quit binds ⌘Q to Quit.
// Upstream binds Open and Quit; the two Window rows are the port's own, and so
// are their chords.
static void StoryInitKeys() {
    static uint32_t bound = 0;
    if (bound == KeymapGeneration()) {
        return;
    }
    bound = KeymapGeneration();
    KeyBinding bindings[] = {
        // cmd-o on macOS, ctrl-o elsewhere, which is what `secondary-` is.
        {"secondary-o", ActOpen(), nullptr},
#if GPUI_OS_MAC
        {"cmd-q", ActQuit(), nullptr},
        // Not upstream's, because upstream has no such rows: on a Mac these
        // two chords are what every application uses for them. Elsewhere
        // there is no such convention, and an unscoped ctrl-w would close the
        // window out from under whatever the pointer was in the middle of.
        {"cmd-n", ActNewWindow(), nullptr},
        {"cmd-w", ActCloseWindow(), nullptr},
#else
        // Rust binds this too. Both Windows and most window managers close
        // the window on alt-F4 before a keystroke reaches the application, so
        // it is the fallback for the ones that do not rather than the path
        // this normally takes.
        {"alt-f4", ActQuit(), nullptr},
#endif
    };
    KeymapBind(bindings, (int)(sizeof(bindings) / sizeof(bindings[0])));
}

// FontSizeSelector's own handlers. Rust hangs these off the selector's
// element, which is where the menu is; they are on the root here because the
// window is what the menu is over, and the story is the entity that answers
// for both.
static void OnSelectFontAction(StoryApp*, Ctx* cx, const ActionEvent* ev) {
    ThemeSetFontSize(cx->app, (float)ev->arg);
    // window.refresh(), and the layout memo goes with it: a font size or a
    // radius changes every box that inherited one.
    Notify(cx);
}

static void OnSelectRadiusAction(StoryApp*, Ctx* cx, const ActionEvent* ev) {
    ThemeSetRadius(cx->app, (float)ev->arg);
    Notify(cx);
}

static void OnSelectScrollbarModeAction(StoryApp*, Ctx* cx,
                                        const ActionEvent* ev) {
    ScrollbarModeSet(cx->app, (ScrollbarMode)(int)ev->arg);
    Notify(cx);
}

static void OnToggleListActiveHighlightAction(StoryApp*, Ctx* cx,
                                              const ActionEvent*) {
    ListSettings s = ListSettingsNow(cx->app);
    s.activeHighlight = !s.activeHighlight;
    ListSettingsSet(cx->app, s);
    Notify(cx);
}

static void OnToggleFpsMonitorAction(StoryApp* app, Ctx* cx,
                                     const ActionEvent*) {
    app->fpsMonitor = !app->fpsMonitor;
    Notify(cx);
}

static void OnToggleAppMenuBarAction(StoryApp* app, Ctx* cx,
                                     const ActionEvent*) {
    app->appMenuBar = !app->appMenuBar;
    Notify(cx);
}

static void OnToggleReduceMotionAction(StoryApp*, Ctx* cx, const ActionEvent*) {
    MotionSetReduced(!MotionReduced());
    Notify(cx);
}

static void OnToggleFocusRingAction(StoryApp*, Ctx* cx, const ActionEvent*) {
    ThemeSetFocusRing(cx->app, !ThemeFocusRing(cx->app));
    Notify(cx);
}

// SelectLocale(code): rust_i18n::set_locale, and the frame after it reads
// every label out of the catalogue again. Rust reloads its menus here because
// its menu bar is built once; ours is built every frame and installed when a
// row moves, so the language reaches the system bar the same way.
static void OnSelectLocaleAction(StoryApp*, Ctx* cx, const ActionEvent* ev) {
    int ix = (int)ev->arg;
    if (ix < 0 || ix >= kStoryLocaleCount) {
        return;
    }
    component::LocaleSet(Str(kStoryLocales[ix].code));
    // The locale is not the theme: nothing observes it, and every label was
    // read at build time, so the whole window has to be built again.
    AppRefreshWindows(cx->app);
}

// Every handler above, hung off the root so a row chosen in either bar finds
// one. Rust registers these with `cx.on_action` on the app rather than on an
// element, which is the same reach — nothing between the root and the focused
// element claims them.
static El* StoryBindMenuActions(El* root, Ctx* cx) {
    return root->OnAction(ActAbout(), Listen(cx, &OnAboutAction))
        ->OnAction(ActOpen(), Listen(cx, &OnOpenAction))
        ->OnAction(ActQuit(), Listen(cx, &OnQuitAction))
        ->OnAction(ActNewWindow(), Listen(cx, &OnNewWindowAction))
        ->OnAction(ActCloseWindow(), Listen(cx, &OnCloseWindowAction))
        ->OnAction(ActDocumentation(), Listen(cx, &OnDocumentationAction))
        ->OnAction(ActSwitchThemeMode(), Listen(cx, &OnSwitchThemeModeAction))
        ->OnAction(ActSelectTheme(), Listen(cx, &OnSelectThemeAction))
        ->OnAction(ActSelectLocale(), Listen(cx, &OnSelectLocaleAction))
        ->OnAction(ActSelectFont(), Listen(cx, &OnSelectFontAction))
        ->OnAction(ActSelectRadius(), Listen(cx, &OnSelectRadiusAction))
        ->OnAction(ActSelectScrollbarMode(),
                   Listen(cx, &OnSelectScrollbarModeAction))
        ->OnAction(ActToggleListActiveHighlight(),
                   Listen(cx, &OnToggleListActiveHighlightAction))
        ->OnAction(ActToggleFpsMonitor(), Listen(cx, &OnToggleFpsMonitorAction))
        ->OnAction(ActToggleAppMenuBar(), Listen(cx, &OnToggleAppMenuBarAction))
        ->OnAction(ActToggleReduceMotion(),
                   Listen(cx, &OnToggleReduceMotionAction))
        ->OnAction(ActToggleFocusRing(), Listen(cx, &OnToggleFocusRingAction));
}

static const int kStoryMenus = 4;

static MenuRow* StoryRows(Ctx* cx, int n) {
    // Zeroed is what every MenuRow field defaults to, which is what makes a
    // row a plain enabled item until something is written into it.
    return (MenuRow*)cx->a
        ->Push((uint64_t)n * sizeof(MenuRow), alignof(MenuRow), true);
}

// build_menus(): the four menus as they stand right now — the mode that is
// checked, the theme in use, whatever `themes/` holds.
static int StoryBuildMenus(Ctx* cx, MenuDef* out, int cap) {
    if (!out || cap < kStoryMenus) {
        return 0;
    }
    // The same `themes/` directory the Theme Colors page reads, so the menu
    // lists whatever that page lists whichever of the two is opened first.
    ThemeRegistryLoadDir(cx->app, StrL("themes"));
    bool dark = ThemeGet(cx->app) == ThemeMode::Dark;

    MenuRow* appearance = StoryRows(cx, 2);
    appearance[0].label = StrL("Light");
    appearance[0].action = ActSwitchThemeMode();
    appearance[0].checked = !dark;
    appearance[1].label = StrL("Dark");
    appearance[1].action = ActSwitchThemeMode();
    appearance[1].arg = 1;
    appearance[1].checked = dark;

    Str active = ThemeRegistryActive(cx->app, ThemeGet(cx->app));
    int nThemes = ThemeRegistryCount(cx->app);
    MenuRow* themes = StoryRows(cx, nThemes > 0 ? nThemes : 1);
    for (int i = 0; i < nThemes; i++) {
        const ThemeConfig* cfg = ThemeRegistryAt(cx->app, i);
        themes[i].label = cfg->name;
        themes[i].action = ActSelectTheme();
        themes[i].arg = i;
        themes[i].checked = base::StrEq(cfg->name, active);
    }

    Str locale = component::LocaleNow();
    MenuRow* languages = StoryRows(cx, kStoryLocaleCount);
    for (int i = 0; i < kStoryLocaleCount; i++) {
        languages[i].label = Str(kStoryLocales[i].label);
        languages[i].action = ActSelectLocale();
        languages[i].arg = i;
        languages[i].checked = base::StrEq(Str(kStoryLocales[i].code), locale);
    }

    MenuRow* appRows = StoryRows(cx, 9);
    appRows[0].label = StrL("About GPUI Kit");
    appRows[0].action = ActAbout();
    appRows[1].separator = true;
    appRows[2].label = StrL("Open...");
    appRows[2].action = ActOpen();
    appRows[3].separator = true;
    appRows[4].label = StrL("Appearance");
    appRows[4].submenu = appearance;
    appRows[4].submenuN = 2;
    appRows[5].label = StrL("Theme");
    appRows[5].submenu = themes;
    appRows[5].submenuN = nThemes;
    // A submenu with nothing under it is a row that would report nothing;
    // disabled says so, and is what a themes directory that is not there
    // leaves behind.
    appRows[5].disabled = nThemes == 0;
    appRows[6].label = StrL("Language");
    appRows[6].submenu = languages;
    appRows[6].submenuN = kStoryLocaleCount;
    appRows[7].separator = true;
    appRows[8].label = StrL("Quit");
    appRows[8].action = ActQuit();
    out[0].name = StrL("GPUI Kit");
    out[0].items = appRows;
    out[0].n = 9;

    // Every row of the Edit menu names one of the input's actions and carries
    // no handler of its own: choosing it dispatches the action to whatever
    // field has the keyboard, which is the same handler the chord reaches,
    // and the shortcut beside it is looked up in the keymap rather than typed
    // here.
    struct EditRow {
        const char* label;
        uint32_t action;
    };
    const EditRow kEdit[] = {
        {"Undo", input::Undo()},
        {"Redo", input::Redo()},
        {nullptr, 0},
        {"Cut", input::Cut()},
        {"Copy", input::Copy()},
        {"Paste", input::Paste()},
        {nullptr, 0},
        {"Delete", input::Delete()},
        {"Delete Previous Word", input::DeleteToPreviousWordStart()},
        {"Delete Next Word", input::DeleteToNextWordEnd()},
        {nullptr, 0},
        {"Find", input::Search()},
        {nullptr, 0},
        {"Select All", input::SelectAll()},
    };
    const int nEdit = (int)(sizeof(kEdit) / sizeof(kEdit[0]));
    MenuRow* editRows = StoryRows(cx, nEdit);
    for (int i = 0; i < nEdit; i++) {
        if (!kEdit[i].label) {
            editRows[i].separator = true;
            continue;
        }
        editRows[i].label = Str(kEdit[i].label);
        editRows[i].action = kEdit[i].action;
    }
    out[1].name = StrL("Edit");
    out[1].items = editRows;
    out[1].n = nEdit;

    MenuRow* windowRows = StoryRows(cx, 2);
    windowRows[0].label = StrL("New Window");
    windowRows[0].action = ActNewWindow();
    windowRows[1].label = StrL("Close Window");
    windowRows[1].action = ActCloseWindow();
    out[2].name = StrL("Window");
    out[2].items = windowRows;
    out[2].n = 2;

    MenuRow* helpRows = StoryRows(cx, 2);
    helpRows[0].label = StrL("Documentation");
    helpRows[0].action = ActDocumentation();
    helpRows[1].label = StrL("About GPUI Kit");
    helpRows[1].action = ActAbout();
    out[3].name = StrL("Help");
    out[3].items = helpRows;
    out[3].n = 2;
    return kStoryMenus;
}

// What the menus look like right now, so the OS bar is only rebuilt when
// something in it moved: a checked row, a theme that appeared, a label. Rust
// re-runs `update_app_menu` from the theme observer and the locale action for
// the same reason — installing a menu bar is not a per-frame thing.
static uint32_t StoryMenuHash(const MenuRow* rows, int n, uint32_t h) {
    for (int i = 0; i < n; i++) {
        const MenuRow& r = rows[i];
        for (int c = 0; c < r.label.len; c++) {
            h = (h ^ (uint32_t)(uint8_t)r.label.s[c]) * 16777619u;
        }
        h = (h ^ r.action) * 16777619u;
        h = (h ^ (uint32_t)r.arg) * 16777619u;
        uint32_t flags = (r.separator ? 1u : 0u) | (r.disabled ? 2u : 0u) |
                         (r.checked ? 4u : 0u);
        h = (h ^ flags) * 16777619u;
        if (r.submenu && r.submenuN > 0) {
            h = StoryMenuHash(r.submenu, r.submenuN, h);
        }
    }
    return h;
}

// cx.set_menus(build_menus(..)). Called every frame and installing on almost
// none of them. The platforms without a menu bar of their own ignore it,
// which is why nothing here asks first: a call that only ran on a Mac is a
// call only a Mac would ever find wrong.
static void StorySetSystemMenus(StoryApp* app, Ctx* cx, const MenuDef* menus,
                                int n) {
    uint32_t h = 2166136261u;
    for (int i = 0; i < n; i++) {
        for (int c = 0; c < menus[i].name.len; c++) {
            h = (h ^ (uint32_t)(uint8_t)menus[i].name.s[c]) * 16777619u;
        }
        h = StoryMenuHash(menus[i].items, menus[i].n, h);
    }
    if (h == app->menuHash) {
        return;
    }
    app->menuHash = h;
    BaseSetAppMenus(cx->app, menus, n);
}

// The same rows as the menu the title bar draws. `id` keys the menu's state,
// and a submenu takes one of its own under it.
static component::PopupMenu* StoryPopupMenu(Ctx* cx, Str id,
                                            const MenuRow* rows, int n) {
    component::PopupMenu* menu = component::PopupMenu::New(cx, id);
    for (int i = 0; i < n; i++) {
        const MenuRow& r = rows[i];
        if (r.separator || r.label.len <= 0) {
            menu->Separator();
            continue;
        }
        if (r.submenu && r.submenuN > 0) {
            menu->Submenu(r.label,
                          StoryPopupMenu(cx, StoryFmt(cx, "%s-%d", id, i),
                                         r.submenu, r.submenuN));
            menu->Disabled(r.disabled);
            continue;
        }
        menu->MenuWithAction(r.label, r.action, r.arg);
        menu->Checked(r.checked);
        menu->Disabled(r.disabled);
    }
    // PopupMenu::scrollable, for the one submenu that is a list rather than a
    // handful of rows: `themes/` holds as many as somebody put there.
    if (n > 12) {
        menu->Scrollable();
    }
    return menu;
}

// AppMenuBar: the menus drawn into the title bar, for the platforms whose
// windows own their menus — and for a Mac that would rather see the component
// than the system bar.
static El* StoryMenuBar(Ctx* cx, const MenuDef* menus, int n) {
    Arena* a = cx->a;
    El* bar = Div(a)->FlexRow()->H(kFill)->ItemsCenter();
    for (int i = 0; i < n; i++) {
        component::PopupMenu* menu =
            StoryPopupMenu(cx, StoryFmt(cx, "story-menu-%d", i), menus[i].items,
                           menus[i].n)
                ->MinW(220);
        if (i == 1) {
            // The field's own key context, which is where the Edit menu's
            // actions are bound and so where their chords are found.
            menu->ActionContext("Input");
        }
        // The application's own menu is the one named for it, and is the one
        // set in the heavier weight — which is what makes it read as a title
        // rather than as the first of four.
        El* trigger = StoryTitleMenuItem(cx, menus[i].name, i == 0);
        bar->Child(component::DropdownMenu::New(
                       cx, StoryFmt(cx, "story-menu-trigger-%d", i))
                       ->Trigger(trigger)
                       ->Menu(menu)
                       ->IntoEl());
    }
    return bar;
}

// The three tools at the right of the title bar. They are ghost buttons, and
// button.rs gives a ghost button the arrow — the hand is for the variants that
// look like a link. Over a title bar there is nothing else to say an icon is a
// control rather than an ornament, so these three ask for it themselves.
static El* StoryTitleBar(StoryApp* app, Ctx* cx, const MenuDef* defs,
                         int nDefs) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);

    El* menus = Div(a)->FlexRow()->H(kFill)->ItemsCenter();
    if (app->appMenuBar) {
        menus->Child(StoryMenuBar(cx, defs, nDefs));
    } else {
        // The system menu bar owns the menus, so the freed up left side names
        // the window the way a Mac application does.
        menus->Child(Div(a)->PadX(8)->Child(
            StoryTxt(cx, StoryWindowTitle(), 14, th.foreground)->Medium()));
    }
    El* tools =
        Div(a)
            ->FlexRow()
            ->H(kFill)
            ->ItemsCenter()
            ->PadX(8)
            ->Gap(2)
            ->Child(AppearanceMenu(app, cx))
            ->Child(component::Button::New(cx, StrL("story-title-github"))
                        ->Icon(IconName::Github)
                        ->Ghost()
                        ->Compact()
                        ->WithSize(UiSize::Small)
                        ->Tooltip(StrL("GitHub"))
                        ->OnClick(Listen(cx, &OnGithub))
                        ->IntoEl()
                        ->Cursor(CursorKind::Pointer))
            // Badge::count: how many notifications are up, capped at 99. The
            // bell itself has nothing to do in Rust either — the count is the
            // whole of it.
            ->Child(
                component::Badge::New(cx)
                    ->Count(StoryNotificationCount(cx))
                    ->Max(99)
                    ->Child(component::Button::New(cx, StrL("story-title-bell"))
                                ->Icon(IconName::Bell)
                                ->Ghost()
                                ->Compact()
                                ->WithSize(UiSize::Small)
                                ->Tooltip(StrL("Notifications"))
                                ->IntoEl()
                                ->Cursor(CursorKind::Pointer))
                    ->IntoEl());

    return component::TitleBar::New(cx)->Child(menus)->Child(tools)->IntoEl();
}

static El* Footer(StoryApp* app, Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    const StoryInfo* m = StoryMeta(app->story);
    return Div(a)
        ->W(kFill)
        ->H(28)
        ->PadX(12)
        ->ItemsCenter()
        ->JustifyBetween()
        ->Shrink0()
        ->Bg(th.tokens.titleBar)
        ->BorderT(1, th.border)
        ->Child(
            Div(a)
                ->FlexRow()
                ->Gap(8)
                ->ItemsCenter()
                ->Child(IconEl(a, IconName::GalleryVerticalEnd, 12)
                            ->Fg(th.mutedFg))
                ->Child(StoryTxt(cx, StoryFmt(cx, "%d components", StoryCount),
                                 12, th.mutedFg))
                ->Child(Div(a)->W(1)->H(12)->Bg(th.border))
                ->Child(StoryTxt(cx, Str(m->title), 12, th.mutedFg)))
        ->Child(Div(a)
                    ->FlexRow()
                    ->Gap(12)
                    ->ItemsCenter()
                    // The theme in force, which is whatever the registry
                    // last installed for this mode rather than always one of
                    // the two defaults.
                    ->Child(StoryTxt(
                        cx, ThemeRegistryActive(cx->app, ThemeGet(cx->app)), 12,
                        th.mutedFg))
                    ->Child(StoryTxt(cx, StrL("v0.5.1"), 12, th.mutedFg))
                    // gallery.rs puts the repository link last in the bar's
                    // right group, as a ghost icon button.
                    ->Child(component::Button::New(cx, StrL("assistant"))
                                ->Ghost()
                                ->WithSize(UiSize::XSmall)
                                ->Icon(IconName::Github)
                                ->Tooltip(StrL("GPUI Kit GitHub repository"))
                                ->OnClick(Listen(cx, &OnGithub))
                                ->IntoEl()
                                ->Cursor(CursorKind::Pointer)));
}

El* StoryApp::Render(StoryApp* app, Ctx* cx) {
    Arena* frame = cx->a;
    // Pages that own a text field point the window at it from their Render.
    if (app->search.focused) {
        cx->win->input = &app->search;
    }
    const Theme& th = ThemeNow(cx->app);
    if (!app->seeded) {
        app->seeded = true;
    }
    // The window's outermost view is a Root, which is what Rust puts under
    // every window: the page, and over it the layers the window owns.
    El* root = Div(frame)->FlexCol()->SizeFull();
    // build_menus() once: the OS menu bar is installed from it when something
    // in it has moved, the title bar draws it, and the root answers for every
    // action either of them dispatches.
    MenuDef defs[kStoryMenus] = {};
    int nDefs = StoryBuildMenus(cx, defs, kStoryMenus);
    StorySetSystemMenus(app, cx, defs, nDefs);
    StoryBindMenuActions(root, cx);
    if (cx->win->opts.clientTitleBar) {
        root->Child(StoryTitleBar(app, cx, defs, nDefs));
    }
    El* body = Div(frame)->FlexRow()->Flex1()->W(kFill)->MinH(0)->H(kFill);
    body->Child(Sidebar(app, cx));
    // The resizable handle reads as a 1px rule. Rust anchors it over the
    // boundary rather than in the flow, so the content starts where the
    // sidebar ends; a border here is painted inside the box without taking
    // space from it, which puts the rule in the same place.
    El* main = Div(frame)->FlexCol()->Flex1()->H(kFill)->MinW(0)->BorderL(
        1, th.border);
    main->Child(Header(app, cx));
    if (app->story == StoryChart) {
        // The chart gallery owns its virtual scrolling and 16px inset.
        main->Child(StoryRenderRegistered(app, cx));
    } else {
        El* scroller = Div(frame)
                           ->FlexCol()
                           ->Flex1()
                           ->MinH(0)
                           ->ClipY()
                           ->ScrollY(app->scrollY)
                           ->ScrollId(PageScrollId())
                           ->OnScroll(Listen(cx, &OnPaneScroll))
                           ->W(kFill);
        scroller->Child(Div(frame)->Pad(16)->W(kFill)->Child(
            StoryRenderRegistered(app, cx)));
        main->Child(scroller);
    }
    body->Child(main);
    // The inspector docks on the right, as it does off Root in Rust.
    // Ctrl+Shift+I toggles it.
    if (El* inspector = component::Inspector::New(cx)->IntoEl()) {
        body->Child(inspector);
    }
    root->Child(body);
    root->Child(Footer(app, cx));
    // ToggleFpsMonitor: the HUD places itself in the top right corner of
    // whatever it is put in, so what it is put in is a strip that starts
    // under the title bar -- `div().absolute().top(TITLE_BAR_HEIGHT).left_0()
    // .right_0()` in StoryRoot::render. Without it the HUD is laid over the
    // caption's own buttons. A window with no title bar of its own, like the
    // fps_monitor example, hands it the whole window and it sits at the top.
    if (app->fpsMonitor) {
        root->Child(Div(frame)
                        ->Absolute()
                        ->Top(component::kTitleBarHeight)
                        ->Left(0)
                        ->Right(0)
                        ->Child(FpsMonitorEl(cx)));
    }
    // Bordered only where the window is client-decorated; a system frame
    // draws its own, and Rust's window_border is the Linux CSD wrapper.
    return component::Root::New(cx)
        ->Bordered(cx->win->opts.clientTitleBar)
        ->Child(root)
        ->IntoEl();
}

static void OnUnhandledClick(StoryApp* app, Ctx* cx, const ClickEvent* ev) {
    // A click that no element claimed lands here: dismiss the search field
    // the way GPUI dismisses an overlay on an outside click.
    if (ev->id == 0 && app->search.focused) {
        app->search.focused = false;
        cx->win->input = nullptr;
    }
}

static void OnChar(StoryApp* app, Ctx* cx, const KeyEvent* ev) {
    (void)cx;
    uint32_t cp = ev->ch;
    if (app->search.focused) {
        (void)cp;
    }
}

static void OnKey(StoryApp* app, Ctx* cx, const KeyEvent* ev) {
    int vk = ev->vk;
    bool down = ev->down;
    if (ev->ch != 0) {
        OnChar(app, cx, ev);
        return;
    }
    if (!down) {
        return;
    }
    // Ctrl+C is the window's now — WindowKeyDown copies the selection before
    // this handler runs — so the shell only has Escape left to answer.
    if (vk == KeyEscape) {
        app->search.focused = false;
        cx->win->input = nullptr;
        WindowSelectionClear(cx->win);
    }
    // Whatever the shell did not use is the page's. This is cx.propagate():
    // the shell handles its own chords first and then lets the action carry
    // on, which is how a page's arrows and Enter reach it at all.
    StoryKeyRegistered(app, cx, ev);
}

// The story to open, if one was named on the command line.
static Str ParseSlug(int argc, char** argv) {
    return argc >= 2 && argv[1] ? Str(argv[1]) : Str{};
}

int GpuiMain(int argc, char** argv) {
    App* app = AppNew();
    component::Init(app);
    // cx.set_app_identity(..): what the platform calls the application when it
    // shows one of its notifications. Windows names the notification area icon
    // with it; the other backends do not have one to name yet.
    SysNotifySetAppIdentity(StrL("com.longbridge.gpui-kit.story"),
                            StrL("GPUI Kit"));
    ThemeSet(app, ThemeMode::Light);
    AssetsClear();
    AssetsAddDefaultRoots(Str{});
    AssetsAddRoot(StrL("assets"));

    // A theme out of the registry named in the environment, so a screenshot
    // of one is reproducible the way GPUI_TODAY makes the calendar's today.
    if (const char* themeName = getenv("GPUI_THEME")) {
        ThemeRegistryLoadDir(app, StrL("themes"));
        const ThemeConfig* cfg = ThemeRegistryFind(app, Str(themeName));
        if (cfg) {
            ThemeRegistryApply(app, cfg);
            ThemeSet(app, cfg->mode);
        }
    }

    Str slug = ParseSlug(argc, argv);
    Window* win = StoryOpenWindow(app, StoryFromSlug(slug.s));
    if (!win) {
        AppFree(app);
        return 1;
    }
    // Rust Gallery::set_active_story puts the launch name in the sidebar
    // search box so the list filters to matching titles.
    if (slug) {
        Entity<StoryApp> view;
        view.id = win->root;
        StoryApp* self = view.Get(app);
        const StoryInfo* m = StoryMeta(self->story);
        InputSetValue(&self->search, Str(m->title));
    }
    int rc = AppRun(app);
    AppFree(app);
    return rc;
}
