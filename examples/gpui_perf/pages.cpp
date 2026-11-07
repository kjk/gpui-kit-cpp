#include "perf.h"

#include <stdio.h>

namespace gpui {

static const char* kComponents[48] = {
    "Accordion",   "Alert",        "Avatar",      "Badge",      "Breadcrumb",
    "Button",      "Calendar",     "Card",        "Checkbox",   "Clipboard",
    "Collapsible", "Combobox",     "DataTable",   "DatePicker", "Dialog",
    "Dropdown",    "Editor",       "Form",        "GroupBox",   "Icon",
    "Image",       "Input",        "Kbd",         "Label",      "List",
    "Menu",        "Notification", "NumberInput", "Pagination", "Popover",
    "Progress",    "Radio",        "Rating",      "Resizable",  "Scrollbar",
    "Select",      "Separator",    "Settings",    "Sheet",      "Sidebar",
    "Skeleton",    "Slider",       "Spinner",     "Switch",     "Table",
    "Tabs",        "Tag",          "Tooltip",
};

static const char* kTails[4] = {"recipes", "patterns", "layouts",
                                "accessibility"};

const char* PerfPageName(int page) {
    static char extra[4][48][40];
    static bool ready = false;
    if (!ready) {
        for (int g = 0; g < 4; g++) {
            for (int i = 0; i < 48; i++) {
                snprintf(extra[g][i], sizeof(extra[g][i]), "%s %s",
                         kComponents[i], kTails[g]);
            }
        }
        ready = true;
    }
    if (page <= 0) {
        return "Allsum chat";
    }
    if (page == 1) {
        return "Introduction";
    }
    if (page == 2) {
        return "Installation";
    }
    if (page == 3) {
        return "Theming";
    }
    if (page < 4 + 48) {
        return kComponents[page - 4];
    }
    if (page < 4 + 48 * 5) {
        int rel = page - (4 + 48);
        return extra[rel / 48][rel % 48];
    }
    return "Trading workspace";
}

const char* PerfScenarioName(PerfScenario scenario) {
    switch (scenario) {
        case PerfScenario::Spinner:
            return "Spinner";
        case PerfScenario::ScrollSidebar:
            return "ScrollSidebar";
        case PerfScenario::ScrollPage:
            return "ScrollPage";
        case PerfScenario::ScrollTable:
            return "ScrollTable";
        case PerfScenario::RefreshTable:
            return "RefreshTable";
        case PerfScenario::ScrollList:
            return "ScrollList";
        case PerfScenario::WorkspaceQuotes:
            return "WorkspaceQuotes";
        case PerfScenario::WorkspaceScroll:
            return "WorkspaceScroll";
        case PerfScenario::WorkspaceHover:
            return "WorkspaceHover";
        case PerfScenario::Count:
            break;
    }
    return "?";
}

static Str Dup(Ctx* cx, TempStr text) {
    return StrDup(cx->a, text);
}

void TableInit(PerfTable* table) {
    if (!table || table->ready) {
        return;
    }
    for (int ix = 0; ix < kPerfTableRows; ix++) {
        float price = 10.f + (float)((ix * 37) % 1000) / 7.f;
        Stock* row = &table->rows[ix];
        row->price = price;
        row->change = 0;
        row->high = price * 1.05f;
        row->low = price * 0.95f;
        row->volume =
            (uint32_t)(((unsigned long long)ix * 7919ull) % 1000000ull);
    }
    table->ready = true;
}

void TableTick(PerfTable* table) {
    if (!table) {
        return;
    }
    TableInit(table);
    table->tick++;
    int tick = table->tick;
    int n = 2000;
    if (n > kPerfTableRows) {
        n = kPerfTableRows;
    }
    for (int ix = 0; ix < n; ix++) {
        if ((ix + tick) % 3 != 0) {
            continue;
        }
        Stock* row = &table->rows[ix];
        float delta = (float)(((ix * 13 + tick * 7) % 21) - 10) / 100.f;
        row->price += delta;
        if (row->price < 0.01f) {
            row->price = 0.01f;
        }
        row->change = delta;
        row->volume += (uint32_t)(ix % 50);
        if (row->price > row->high) {
            row->high = row->price;
        }
        if (row->price < row->low) {
            row->low = row->price;
        }
    }
}

void PerfTable::OnScroll(PerfTable* self, Ctx* cx, const ScrollEvent* ev) {
    self->handle.offset = ev->offsetY;
    Notify(cx);
}

static El* TableCell(Ctx* cx, float width, bool end, Str text, Rgba color) {
    El* cell = Div(cx->a)->H(kPerfRowH)->ItemsCenter()->PadX(8)->Shrink0();
    if (width > 0) {
        cell->W(width);
    } else {
        cell->Flex1()->MinW(96);
    }
    if (end) {
        cell->JustifyEnd();
    }
    cell->Child(TextEl(cx->a, text)->Font(12)->Fg(color)->Truncate());
    return cell;
}

static El* TableRow(void* user, Ctx* cx, int ix) {
    PerfTable* table = (PerfTable*)user;
    const RuntimeStyle& th = RuntimeStyleNow(cx->app);
    const Stock& stock = table->rows[ix];
    Rgba change = th.mutedForeground;
    if (stock.change > 0) {
        change = Rgb(0x16, 0xa3, 0x4a);
    } else if (stock.change < 0) {
        change = Rgb(0xdc, 0x26, 0x26);
    }
    float pct = stock.price > 0 ? stock.change * 100.f / stock.price : 0;
    StrBuilder pctText(cx->a);
    pctText.Append(Dup(cx, fmt("%+.2f", pct)));
    pctText.Append(StrL("%"));
    El* row = Div(cx->a)
                  ->FlexRow()
                  ->W(kFill)
                  ->H(kPerfRowH)
                  ->ItemsCenter()
                  ->BorderB(1, th.border)
                  ->HoverBg(Rgb(0xee, 0xee, 0xee));
    if (ix % 2 == 1) {
        row->Bg(Rgb(0xf4, 0xf4, 0xf5));
    }
    row->Child(TableCell(cx, 56, true, Dup(cx, fmt("%d", ix + 1)),
                         th.mutedForeground));
    row->Child(
        TableCell(cx, 72, false, Dup(cx, fmt("S%04d", ix)), th.foreground));
    row->Child(
        TableCell(cx, 0, false, Dup(cx, fmt("Company %d", ix)), th.foreground));
    row->Child(TableCell(cx, 80, true, Dup(cx, fmt("%.2f", stock.price)),
                         th.foreground));
    row->Child(TableCell(cx, 80, true, pctText.TakeStr(), change));
    row->Child(TableCell(cx, 88, true, Dup(cx, fmt("%u", stock.volume)),
                         th.foreground));
    row->Child(TableCell(cx, 80, true, Dup(cx, fmt("%.2f", stock.high)),
                         th.foreground));
    row->Child(TableCell(cx, 80, true, Dup(cx, fmt("%.2f", stock.low)),
                         th.foreground));
    return row;
}

El* PerfTable::Render(PerfTable* self, Ctx* cx) {
    TableInit(self);
    const RuntimeStyle& th = RuntimeStyleNow(cx->app);
    El* head = Div(cx->a)
                   ->FlexRow()
                   ->H(kPerfRowH)
                   ->ItemsCenter()
                   ->Bg(Rgb(0xf5, 0xf5, 0xf5))
                   ->BorderB(1, th.border);
    const char* titles[] = {"#",      "Symbol", "Name", "Price",
                            "Change", "Volume", "High", "Low"};
    float widths[] = {56, 72, 0, 80, 80, 88, 80, 80};
    bool ends[] = {true, false, false, true, true, true, true, true};
    for (int i = 0; i < 8; i++) {
        head->Child(TableCell(cx, widths[i], ends[i], Str(titles[i]),
                              th.mutedForeground));
    }
    VirtualListOpts o;
    o.count = kPerfTableRows;
    o.rowH = kPerfRowH;
    o.handle = &self->handle;
    o.scrollY = self->handle.offset;
    o.scrollId = HashClickId(StrL("perf-table"));
    o.onScroll = Listen(cx, &PerfTable::OnScroll);
    o.axis = ScrollAxis::Vertical;
    o.row = &TableRow;
    o.user = self;
    El* list = VirtualList::New(cx, StrL("perf-table"), o)->Flex1()->MinH(0);
    return Div(cx->a)
        ->Flex1()
        ->MinH(0)
        ->FlexCol()
        ->Border(1, th.border)
        ->Radius(th.radius)
        ->ClipY()
        ->Child(head)
        ->Child(list);
}

void MessageListInit(MessageList* list) {
    if (!list || list->ready) {
        return;
    }
    for (int ix = 0; ix < kPerfMessages; ix++) {
        int sentences = 1 + (ix * 11) % 4;
        list->heights[ix] = 40.f + (float)sentences * 18.f;
    }
    list->ready = true;
}

void MessageList::OnScroll(MessageList* self, Ctx* cx, const ScrollEvent* ev) {
    self->handle.offset = ev->offsetY;
    Notify(cx);
}

static El* MessageRow(void* user, Ctx* cx, int ix) {
    (void)user;
    const RuntimeStyle& th = RuntimeStyleNow(cx->app);
    static const char* authors[] = {"Jason Lee", "Ada Byron", "Zhang Yi",
                                    "Mira Kant", "Theo Rust"};
    static const char* initials[] = {"JL", "AB", "ZY", "MK", "TR"};
    static const char* sentences[] = {
        "The table scrolls smoothly now.",
        "Retained views keep what did not change from the last frame, so a "
        "frame only pays for what moved.",
        "Could you check the sidebar as well?",
        "I measured it with the showcase: most of the time goes to laying out "
        "text that did not change, which the new measurement cache skips.",
        "Looks good to me.",
        "Scrolling a long page moves every view in it, and each of them is "
        "built again where it lands.",
    };
    int who = (ix * 7) % 5;
    int count = 1 + (ix * 11) % 4;
    StrBuilder body(cx->a);
    for (int n = 0; n < count; n++) {
        if (n > 0) {
            body.Append(StrL(" "));
        }
        body.Append(Str(sentences[(ix + n * 5) % 6]));
    }
    float hue = (float)who / 5.f;
    El* avatar = Div(cx->a)
                     ->W(32)
                     ->H(32)
                     ->Radius(kRadiusFull)
                     ->Shrink0()
                     ->ItemsCenter()
                     ->JustifyCenter()
                     ->Bg(RgbaHsla(hue, 0.45f, 0.55f, 1.f))
                     ->Child(TextEl(cx->a, Str(initials[who]))
                                 ->Font(11)
                                 ->Fg(th.background));
    El* text =
        Div(cx->a)
            ->FlexCol()
            ->Flex1()
            ->Gap(2)
            ->Child(Div(cx->a)
                        ->FlexRow()
                        ->Gap(8)
                        ->Child(TextEl(cx->a, Str(authors[who]))
                                    ->Font(13)
                                    ->Semibold()
                                    ->Fg(th.foreground))
                        ->Child(TextEl(cx->a, Dup(cx, fmt("%02d:%02d",
                                                          9 + (ix / 60) % 12,
                                                          ix % 60)))
                                    ->Font(11)
                                    ->Fg(th.mutedForeground)))
            ->Child(TextEl(cx->a, body.TakeStr())
                        ->Font(13)
                        ->Fg(th.foreground)
                        ->Wrap());
    El* row = Div(cx->a)
                  ->FlexRow()
                  ->W(kFill)
                  ->Gap(12)
                  ->PadX(16)
                  ->PadY(8)
                  ->Child(avatar)
                  ->Child(text);
    if (ix < 3) {
        row->Bg(Rgb(0xf5, 0xf5, 0xf5));
    }
    return row;
}

El* MessageList::Render(MessageList* self, Ctx* cx) {
    MessageListInit(self);
    VirtualListOpts o;
    o.count = kPerfMessages;
    o.rowH = 72;
    o.sizes = self->heights;
    o.handle = &self->handle;
    o.scrollY = self->handle.offset;
    o.scrollId = HashClickId(StrL("perf-messages"));
    o.onScroll = Listen(cx, &MessageList::OnScroll);
    o.axis = ScrollAxis::Vertical;
    o.row = &MessageRow;
    o.user = self;
    return VirtualList::New(cx, StrL("perf-messages"), o)->Flex1()->MinH(0);
}

// Keyed input / switch state. `ready` is the first-frame init, because
// KeyedState default-constructs and the switch's starting position is data.
struct PerfSlot {
    bool on = false;
    bool ready = false;
};

void Components::OnToggle(Components*, Ctx* cx, const ClickEvent*,
                          int64_t key) {
    PerfSlot* slot = KeyedState<PerfSlot>(cx, (uint32_t)key);
    if (!slot) {
        return;
    }
    slot->on = !slot->on;
    Notify(cx);
}

static El* PerfButton(Ctx* cx, int section, int item, const RuntimeStyle& th) {
    static const char* labels[] = {"Save",     "Duplicate", "Export...",
                                   "Share...", "Archive",   "Rename..."};
    El* button = Div(cx->a)
                     ->FlexRow()
                     ->ItemsCenter()
                     ->Gap(8)
                     ->H(32)
                     ->PadX(12)
                     ->Radius(th.radius)
                     ->Child(TextEl(cx->a, Str(labels[item % 6]))
                                 ->Font(13)
                                 ->Semibold()
                                 ->Fg(th.foreground));
    if (item % 3 == 0) {
        button->Border(1, th.border)->Bg(Rgb(0xf5, 0xf5, 0xf5));
    } else if (item % 3 == 1) {
        button->Border(1, th.border);
    }
    (void)section;
    return button;
}

static El* PerfAvatar(Ctx* cx, int section, int item, int unread,
                      const RuntimeStyle& th) {
    static const char* initials[] = {"JL", "AB", "ZY", "MK"};
    float hue = (float)((section * 31 + item * 17) % 100) / 100.f;
    int count = (item * 7 + unread) % 120;
    El* face = Div(cx->a)
                   ->W(40)
                   ->H(40)
                   ->Radius(kRadiusFull)
                   ->ItemsCenter()
                   ->JustifyCenter()
                   ->Bg(RgbaHsla(hue, 0.45f, 0.55f, 1.f))
                   ->Child(TextEl(cx->a, Str(initials[item % 4]))
                               ->Font(13)
                               ->Fg(th.background));
    El* box = Div(cx->a)->W(40)->H(40)->Child(face);
    if (count > 0) {
        const char* badge = count > 99 ? "99+" : nullptr;
        Str label = badge ? Str(badge) : Dup(cx, fmt("%d", count));
        box->Child(
            Div(cx->a)
                ->Absolute()
                ->Top(-4)
                ->Right(-6)
                ->PadX(4)
                ->Radius(kRadiusFull)
                ->Bg(Rgb(0xdc, 0x26, 0x26))
                ->Child(TextEl(cx->a, label)->Font(10)->Fg(th.background)));
    }
    return box;
}

static El* PerfBadge(Ctx* cx, int item, const RuntimeStyle& th) {
    static const char* labels[] = {"Draft", "Shared", "Archived", "Internal",
                                   "Beta"};
    return Div(cx->a)
        ->PadX(8)
        ->PadY(2)
        ->Radius(kRadiusFull)
        ->Border(1, th.border)
        ->Child(TextEl(cx->a, Str(labels[item % 5]))
                    ->Font(11)
                    ->Semibold()
                    ->Fg(th.foreground));
}

static El* PerfInput(Ctx* cx, int section, int item, const RuntimeStyle& th) {
    static const char* placeholders[] = {"Name", "Email address", "Search",
                                         "Project"};
    uint32_t key = (1u << 20) | (uint32_t)(section * 100 + item);
    PerfSlot* slot = KeyedState<PerfSlot>(cx, key);
    if (slot && !slot->ready) {
        slot->ready = true;
        slot->on = false;
    }
    bool on = slot && slot->on;
    return Div(cx->a)
        ->W(192)
        ->H(32)
        ->PadX(12)
        ->ItemsCenter()
        ->Radius(th.radius)
        ->Border(1, on ? th.foreground : th.border)
        ->OnClick(Listen(cx, &Components::OnToggle, (int64_t)key))
        ->Child(TextEl(cx->a, Str(placeholders[item % 4]))
                    ->Font(13)
                    ->Fg(th.mutedForeground));
}

static El* PerfSwitch(Ctx* cx, int section, int item, const RuntimeStyle& th) {
    static const char* labels[] = {"Notifications", "Sync", "Auto-save",
                                   "Dark mode"};
    uint32_t key = (2u << 20) | (uint32_t)(section * 100 + item);
    PerfSlot* slot = KeyedState<PerfSlot>(cx, key);
    if (slot && !slot->ready) {
        slot->ready = true;
        slot->on = (item % 2) == 0;
    }
    bool on = slot && slot->on;
    El* knob = Div(cx->a)->W(16)->H(16)->Radius(kRadiusFull)->Bg(th.background);
    El* track = Div(cx->a)
                    ->W(36)
                    ->H(20)
                    ->Radius(kRadiusFull)
                    ->ItemsCenter()
                    ->PadX(2)
                    ->Bg(on ? th.foreground : th.border)
                    ->Child(knob);
    return Div(cx->a)
        ->FlexRow()
        ->ItemsCenter()
        ->Gap(8)
        ->OnClick(Listen(cx, &Components::OnToggle, (int64_t)key))
        ->Child(track)
        ->Child(
            TextEl(cx->a, Str(labels[item % 4]))->Font(13)->Fg(th.foreground));
}

static El* PerfSection(Components* self, Ctx* cx, int section,
                       const RuntimeStyle& th) {
    int kind = (self->seed + section) % 5;
    int items = 3 + (self->seed * 7 + section * 5) % 9;
    const char* title = "Buttons";
    const char* description =
        "Default, outline and ghost buttons, at the medium size.";
    if (kind == 1) {
        title = "Avatars";
        description = "Avatars with a count of unread messages.";
    } else if (kind == 2) {
        title = "Badges";
        description = "Neutral badges classifying an item.";
    } else if (kind == 3) {
        title = "Inputs";
        description =
            "Inputs holding their state in keyed entities; click one to focus "
            "it.";
    } else if (kind == 4) {
        title = "Switches";
        description =
            "Switches holding their state in keyed entities; click one to "
            "toggle it.";
    }
    El* wrap = Div(cx->a)
                   ->FlexRow()
                   ->FlexWrap()
                   ->W(kFill)
                   ->Gap(16)
                   ->ItemsCenter()
                   ->JustifyCenter();
    for (int item = 0; item < items; item++) {
        El* child = nullptr;
        if (kind == 0) {
            child = PerfButton(cx, section, item, th);
        } else if (kind == 1) {
            child = PerfAvatar(cx, section, item, self->unread, th);
        } else if (kind == 2) {
            child = PerfBadge(cx, item, th);
        } else if (kind == 3) {
            child = PerfInput(cx, section, item, th);
        } else {
            child = PerfSwitch(cx, section, item, th);
        }
        wrap->Child(child);
    }
    return Div(cx->a)
        ->FlexCol()
        ->Gap(12)
        ->Child(Div(cx->a)
                    ->FlexCol()
                    ->Gap(4)
                    ->Child(TextEl(cx->a, Dup(cx, fmt("%s %d", Str(title),
                                                      section + 1)))
                                ->Font(14)
                                ->Semibold()
                                ->Fg(th.foreground))
                    ->Child(TextEl(cx->a, Str(description))
                                ->Font(12)
                                ->Fg(th.mutedForeground)))
        ->Child(Div(cx->a)
                    ->FlexCol()
                    ->ItemsCenter()
                    ->Pad(16)
                    ->Radius(th.radius)
                    ->Border(1, th.border)
                    ->Child(wrap));
}

El* Components::Render(Components* self, Ctx* cx) {
    const RuntimeStyle& th = RuntimeStyleNow(cx->app);
    El* col = Div(cx->a)->FlexCol()->Gap(24)->W(kFill);
    for (int section = 0; section < 24; section++) {
        col->Child(PerfSection(self, cx, section, th));
    }
    return col;
}

void Sidebar::OnScroll(Sidebar* self, Ctx* cx, const ScrollEvent* ev) {
    self->scroll = ev->offsetY;
    Notify(cx);
}

static void SidebarGroup(El* col, Ctx* cx, Sidebar* self, const char* title,
                         int first, int count) {
    const RuntimeStyle& th = RuntimeStyleNow(cx->app);
    col->Child(
        Div(cx->a)->PadX(12)->PadY(6)->Child(TextEl(cx->a, Str(title))
                                                 ->Font(11)
                                                 ->Semibold()
                                                 ->Fg(th.mutedForeground)));
    for (int i = 0; i < count; i++) {
        int page = first + i;
        El* row = Div(cx->a)->H(28)->W(kFill)->ItemsCenter()->PadX(12);
        if (page == self->active) {
            row->Bg(Rgb(0xee, 0xee, 0xee));
        }
        row->Child(TextEl(cx->a, Str(PerfPageName(page)))
                       ->Font(13)
                       ->Fg(th.foreground)
                       ->Truncate());
        col->Child(row);
    }
}

El* Sidebar::Render(Sidebar* self, Ctx* cx) {
    const RuntimeStyle& th = RuntimeStyleNow(cx->app);
    El* col = Div(cx->a)->FlexCol()->W(kFill)->PadY(8);
    SidebarGroup(col, cx, self, "Chat", 0, 1);
    SidebarGroup(col, cx, self, "Getting started", 1, 3);
    SidebarGroup(col, cx, self, "Components", 4, 48);
    SidebarGroup(col, cx, self, "Recipes", 52, 48);
    SidebarGroup(col, cx, self, "Patterns", 100, 48);
    SidebarGroup(col, cx, self, "Layouts", 148, 48);
    SidebarGroup(col, cx, self, "Accessibility", 196, 48);
    SidebarGroup(col, cx, self, "Applications", kPerfPageWorkspace, 1);
    return Div(cx->a)
        ->W(240)
        ->H(kFill)
        ->FlexCol()
        ->BorderR(1, th.border)
        ->Bg(th.background)
        ->Child(Div(cx->a)
                    ->Flex1()
                    ->MinH(0)
                    ->ScrollY(self->scroll)
                    ->ScrollId(HashClickId(StrL("perf-sidebar")))
                    ->OnScroll(Listen(cx, &Sidebar::OnScroll))
                    ->Child(col));
}

void Container::OnPageScroll(Container* self, Ctx* cx, const ScrollEvent* ev) {
    self->scroll = ev->offsetY;
    Notify(cx);
}

void Container::Show(App* app, int index) {
    page = index;
    this->scroll = 0;
    if (!components.IsValid()) {
        components = EntityNew<Components>(app);
    }
    if (index == kPerfPageTable) {
        kind = PerfKind::Table;
        if (!table.IsValid()) {
            table = EntityNew<PerfTable>(app);
            TableInit(table.Get(app));
        }
    } else if (index == kPerfPageList) {
        kind = PerfKind::List;
        if (!messages.IsValid()) {
            messages = EntityNew<MessageList>(app);
            MessageListInit(messages.Get(app));
        }
    } else if (index == kPerfPageWorkspace) {
        kind = PerfKind::Workspace;
        if (!workspace.IsValid()) {
            workspace = EntityNew<Workspace>(app);
        }
        WorkspacePrepare(app, workspace.Get(app));
    } else if (index == 0) {
        kind = PerfKind::Chat;
    } else {
        kind = PerfKind::Components;
        Components* pageView = components.Get(app);
        if (pageView) {
            pageView->seed = index > 0 ? index - 1 : 0;
        }
    }
}

El* Container::Render(Container* self, Ctx* cx) {
    if (self->kind == PerfKind::Workspace && self->workspace.IsValid()) {
        return Div(cx->a)->Flex1()->MinH(0)->Child(
            EntityRender(cx->app, cx->win, cx->a, self->workspace.id));
    }
    if (self->kind == PerfKind::Chat) {
        const RuntimeStyle& th = RuntimeStyleNow(cx->app);
        return Div(cx->a)->Flex1()->Pad(24)->Child(
            TextEl(cx->a,
                   StrL("Allsum chat runs in gpui-fast's scenario crate."))
                ->Font(14)
                ->Fg(th.mutedForeground));
    }
    if (self->kind == PerfKind::Table && self->table.IsValid()) {
        return Div(cx->a)->Flex1()->MinH(0)->Pad(16)->FlexCol()->Child(
            EntityRender(cx->app, cx->win, cx->a, self->table.id));
    }
    if (self->kind == PerfKind::List && self->messages.IsValid()) {
        return Div(cx->a)->Flex1()->MinH(0)->Child(
            EntityRender(cx->app, cx->win, cx->a, self->messages.id));
    }
    El* page = nullptr;
    if (self->components.IsValid()) {
        page = EntityRender(cx->app, cx->win, cx->a, self->components.id);
    }
    El* scroll = Div(cx->a)
                     ->Flex1()
                     ->MinH(0)
                     ->ScrollY(self->scroll)
                     ->ScrollId(HashClickId(StrL("perf-page")))
                     ->OnScroll(Listen(cx, &Container::OnPageScroll));
    if (page) {
        scroll->Child(Div(cx->a)->Pad(16)->W(kFill)->Child(page));
    }
    return scroll;
}

} // namespace gpui
