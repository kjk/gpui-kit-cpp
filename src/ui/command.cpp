#include "ui/i18n.h"
#include "ui/lib.h"
#include "ui/command.h"
#include "ui/input.h"
#include "ui/icon.h"
#include "ui/spinner.h"
#include "ui/virtual_list.h"
#include "base/actions.h"
#include "base/input_keys.h"
#include "gpui/keymap.h"

namespace gpui {

namespace component {

// The rows a palette is made of. Rust measures each one with
// `layout_as_root` before handing the sizes to the virtual list, since a
// custom row may be any height, and so does MeasureRows below. These are
// what a row comes to when it cannot be laid out — a model installed without
// a Ctx, as the tests do — and SEPARATOR_ROW_HEIGHT, which Rust does not
// measure either: a one-pixel rule with a little air on either side.
//
// An item row is px_2 py_1p5 over text_sm, a heading the same over text_xs:
// their text's line box (GPUI's default phi line height) and py_1p5 above
// and below it.
static const float kItemRowH = 14.f * kLineHeight + 12.f;
static const float kHeadingRowH = 12.f * kLineHeight + 12.f;
static const float kSeparatorRowH = 9.f;

Str CommandContext() {
    return StrL("Command");
}

void CommandInitKeys() {
    static uint32_t bound = 0;
    if (bound == KeymapGeneration()) {
        return;
    }
    bound = KeymapGeneration();
    const char* ctx = "Command";
    KeyBinding bindings[] = {
        {"escape", action::Cancel(), ctx},
        {"enter", action::Confirm(), ctx},
        {"up", action::SelectUp(), ctx},
        {"down", action::SelectDown(), ctx},
    };
    KeymapBind(bindings, (int)(sizeof(bindings) / sizeof(bindings[0])));
}

CommandEntry CommandEntryOf(const CommandItem& item) {
    CommandEntry e;
    e.kind = CommandEntryKind::Item;
    e.item = item;
    return e;
}
CommandEntry CommandEntryOf(const CommandGroup& group) {
    CommandEntry e;
    e.kind = CommandEntryKind::Group;
    e.group = group;
    return e;
}
CommandEntry CommandSeparatorEntry() {
    CommandEntry e;
    e.kind = CommandEntryKind::Separator;
    return e;
}

bool CommandItemMatches(const CommandItem* item, Str query) {
    if (!item) {
        return false;
    }
    if (len(query) == 0) {
        return true;
    }
    if (StrContainsI(item->label, query)) {
        return true;
    }
    for (int i = 0; i < item->nKeywords; i++) {
        if (StrContainsI(item->keywords[i], query)) {
            return true;
        }
    }
    return false;
}

// `query.trim()`: the whitespace a caller typed either side of the term is
// not part of it.
static Str TrimQuery(Str s) {
    int lo = 0;
    int hi = len(s);
    while (lo < hi && (unsigned char)s.s[lo] <= ' ') {
        lo++;
    }
    while (hi > lo && (unsigned char)s.s[hi - 1] <= ' ') {
        hi--;
    }
    return Str(s.s + lo, hi - lo);
}

static Str AppliedQuery(const CommandState* s) {
    return Str(s->applied.els, s->applied.len);
}
static bool SameQuery(Str a, Str b) {
    if (len(a) != len(b)) {
        return false;
    }
    for (int i = 0; i < len(a); i++) {
        if (a.s[i] != b.s[i]) {
            return false;
        }
    }
    return true;
}
static void SetApplied(CommandState* s, Str q) {
    s->applied.len = 0;
    for (int i = 0; i < len(q); i++) {
        VecAppend(s->applied, q.s[i]);
    }
}

static const CommandItem* ItemOfMatch(const CommandState* s, int matchIx) {
    if (!s || matchIx < 0 || matchIx >= s->matched.len) {
        return nullptr;
    }
    const CommandMatch& m = s->matched[matchIx];
    if (m.entry < 0 || m.entry >= s->nEntries) {
        return nullptr;
    }
    const CommandEntry& e = s->entries[m.entry];
    if (e.kind == CommandEntryKind::Item) {
        return &e.item;
    }
    if (e.kind == CommandEntryKind::Group && m.itemIx >= 0 &&
        m.itemIx < e.group.nItems) {
        return &e.group.items[m.itemIx];
    }
    return nullptr;
}

static bool ItemMatchesQuery(const CommandState* s, const CommandItem* item,
                             Str query) {
    if (!s->searchable || !s->filterable || len(query) == 0) {
        return true;
    }
    return CommandItemMatches(item, query);
}

static float RowHeight(const CommandRow& row) {
    switch (row.kind) {
        case CommandRowKind::Separator:
            return kSeparatorRowH;
        case CommandRowKind::Heading:
            return kHeadingRowH;
        case CommandRowKind::Item:
            break;
    }
    return kItemRowH;
}

// CommandEntry::same_layout, folded into one number: FNV-1a over what
// filtering and measuring read — label, keywords, icon, action, checked,
// disabled, a group's heading and its item count, and the entry kinds.
static void LayoutHashBytes(uint64_t* h, const void* p, int n) {
    const uint8_t* b = (const uint8_t*)p;
    for (int i = 0; i < n; i++) {
        *h ^= b[i];
        *h *= 1099511628211ull;
    }
}
static void LayoutHashStr(uint64_t* h, Str s) {
    int n = len(s);
    LayoutHashBytes(h, &n, (int)sizeof(n));
    if (n > 0) {
        LayoutHashBytes(h, s.s, n);
    }
}
// False for an item with a custom child, which is never the same layout:
// the child can read state outside the item, so only laying it out again
// tells whether its height changed.
static bool LayoutHashItem(uint64_t* h, const CommandItem& it) {
    if (it.content) {
        return false;
    }
    LayoutHashStr(h, it.label);
    LayoutHashBytes(h, &it.nKeywords, (int)sizeof(it.nKeywords));
    for (int i = 0; i < it.nKeywords; i++) {
        LayoutHashStr(h, it.keywords[i]);
    }
    uint32_t icon = (uint32_t)it.icon;
    LayoutHashBytes(h, &icon, (int)sizeof(icon));
    LayoutHashBytes(h, &it.action, (int)sizeof(it.action));
    LayoutHashBytes(h, &it.actionArg, (int)sizeof(it.actionArg));
    LayoutHashStr(h, it.actionContext ? Str(it.actionContext) : Str{});
    uint8_t flags = (uint8_t)((it.checked ? 1 : 0) | (it.disabled ? 2 : 0));
    LayoutHashBytes(h, &flags, 1);
    return true;
}

// The model's fingerprint, or 0 for one that must be measured whatever came
// before it.
static uint64_t ModelLayoutKey(const CommandEntry* entries, int n,
                               bool searchable, bool filterable) {
    uint64_t h = 14695981039346656037ull;
    uint8_t flags = (uint8_t)((searchable ? 1 : 0) | (filterable ? 2 : 0));
    LayoutHashBytes(&h, &flags, 1);
    LayoutHashBytes(&h, &n, (int)sizeof(n));
    for (int i = 0; i < n; i++) {
        const CommandEntry& e = entries[i];
        uint8_t kind = (uint8_t)e.kind;
        LayoutHashBytes(&h, &kind, 1);
        if (e.kind == CommandEntryKind::Item) {
            if (!LayoutHashItem(&h, e.item)) {
                return 0;
            }
        } else if (e.kind == CommandEntryKind::Group) {
            LayoutHashStr(&h, e.group.heading);
            LayoutHashBytes(&h, &e.group.nItems, (int)sizeof(e.group.nItems));
            for (int j = 0; j < e.group.nItems; j++) {
                if (!LayoutHashItem(&h, e.group.items[j])) {
                    return 0;
                }
            }
        }
    }
    return h ? h : 1;
}

// update_matches: the visible rows and the matching items, for the query the
// field now holds. The index path an item reports is its place in the model
// as it was given — an ungrouped item is section 0 and its own position, so
// filtering never moves it onto somebody else's row.
static void UpdateMatches(CommandState* s, Str query) {
    s->rows.len = 0;
    s->matched.len = 0;
    bool hasUngrouped = false;
    for (int i = 0; i < s->nEntries; i++) {
        if (s->entries[i].kind == CommandEntryKind::Item) {
            hasUngrouped = true;
            break;
        }
    }
    int ungroupedIx = 0;
    int groupIx = 0;
    // A separator is only drawn once something follows it, which drops the
    // leading, trailing and doubled ones a filtered list leaves behind.
    bool pendingSeparator = false;

    for (int entryIx = 0; entryIx < s->nEntries; entryIx++) {
        const CommandEntry& e = s->entries[entryIx];
        if (e.kind == CommandEntryKind::Separator) {
            pendingSeparator = s->rows.len > 0;
            continue;
        }
        if (e.kind == CommandEntryKind::Item) {
            int itemIx = ungroupedIx++;
            if (!ItemMatchesQuery(s, &e.item, query)) {
                continue;
            }
            if (pendingSeparator) {
                CommandRow sep;
                sep.kind = CommandRowKind::Separator;
                VecAppend(s->rows, sep);
                pendingSeparator = false;
            }
            CommandMatch m;
            m.entry = entryIx;
            m.itemIx = 0;
            m.path = IndexPathNew(itemIx).Section(0);
            m.row = s->rows.len;
            m.disabled = e.item.disabled;
            m.data = e.item.data;
            VecAppend(s->matched, m);
            CommandRow row;
            row.kind = CommandRowKind::Item;
            row.match = s->matched.len - 1;
            VecAppend(s->rows, row);
            continue;
        }

        int section = groupIx + (hasUngrouped ? 1 : 0);
        groupIx++;
        bool any = false;
        for (int i = 0; i < e.group.nItems; i++) {
            if (ItemMatchesQuery(s, &e.group.items[i], query)) {
                any = true;
                break;
            }
        }
        if (!any) {
            continue;
        }
        if (pendingSeparator) {
            CommandRow sep;
            sep.kind = CommandRowKind::Separator;
            VecAppend(s->rows, sep);
            pendingSeparator = false;
        }
        // The heading is hidden while every item in the group is filtered out,
        // which the check above is.
        if (e.group.heading.len > 0) {
            CommandRow heading;
            heading.kind = CommandRowKind::Heading;
            heading.heading = e.group.heading;
            VecAppend(s->rows, heading);
        }
        for (int i = 0; i < e.group.nItems; i++) {
            const CommandItem& it = e.group.items[i];
            if (!ItemMatchesQuery(s, &it, query)) {
                continue;
            }
            CommandMatch m;
            m.entry = entryIx;
            m.itemIx = i;
            m.path = IndexPathNew(i).Section(section);
            m.row = s->rows.len;
            m.disabled = it.disabled;
            m.data = it.data;
            VecAppend(s->matched, m);
            CommandRow row;
            row.kind = CommandRowKind::Item;
            row.match = s->matched.len - 1;
            VecAppend(s->rows, row);
        }
    }

    if (s->selected >= s->matched.len) {
        s->selected = -1;
    }
}

bool CommandSelectedIndex(const CommandState* s, IndexPath* out) {
    if (!s || s->selected < 0 || s->selected >= s->matched.len) {
        return false;
    }
    const CommandMatch& m = s->matched[s->selected];
    if (m.disabled) {
        return false;
    }
    if (out) {
        *out = m.path;
    }
    return true;
}

int CommandMatchedCount(const CommandState* s) {
    return s ? s->matched.len : 0;
}

// reset_selection: the highlight moves to the first item that can be
// confirmed.
static void ResetSelection(CommandState* s) {
    s->selected = -1;
    for (int i = 0; i < s->matched.len; i++) {
        if (!s->matched[i].disabled) {
            s->selected = i;
            break;
        }
    }
    s->preserveNoSelection = false;
    s->pendingScroll = s->selected >= 0 ? s->matched[s->selected].row : 0;
}

// The select callback, only when the highlighted path actually changed. Rust
// defers it past the state's own update; a listener here is called once the
// state is done with, which is the same order.
static void FireSelect(CommandState* s, Ctx* cx, bool hadPrev, IndexPath prev) {
    IndexPath now = {};
    bool has = CommandSelectedIndex(s, &now);
    if (has == hadPrev && (!has || now == prev)) {
        return;
    }
    if (!has || !s->onSelect.IsValid()) {
        return;
    }
    CommandEvent ev;
    ev.kind = CommandEventKind::Select;
    ev.path = now;
    ev.data = s->matched[s->selected].data;
    ListenerCall(cx->app, cx->win, s->onSelect, &ev);
}

static El* BuildCommandRow(Ctx* cx, CommandState* s, int rowIx,
                           Entity<CommandState> entity, float itemRadius,
                           bool interactive);

// measure_rows: each row built and laid out on its own, at the width the
// list's content had last frame (MinContent before it has been laid out) and
// under the font its rows inherit there, before the sizes go to the virtual
// list. Nothing is done while the sizes are still those of the rows, the
// width, the rem size, the font and the keymap they were measured with. Without
// a Ctx the rows cannot be built and take the standard heights.
static void MeasureRows(CommandState* s, Ctx* cx) {
    float font = s->listFont > 0
                     ? s->listFont
                     : RuntimeStyleNow(cx ? cx->app : nullptr).fontSize;
    uint32_t keymap = KeymapGeneration();
    float rem = Rems(cx, 1);
    if (!s->needsMeasure && s->rowSizes.len == s->rows.len &&
        s->measuredW == s->listW && s->measuredRem == rem &&
        s->measuredFont == font && s->measuredKeymap == keymap) {
        return;
    }
    s->needsMeasure = false;
    s->measuredW = s->listW;
    s->measuredRem = rem;
    s->measuredFont = font;
    s->measuredKeymap = keymap;
    s->measureCount++;
    s->rowSizes.len = 0;
    bool canBuild = cx && cx->a && cx->app;
    PaintCtx* paint = cx && cx->win ? &cx->win->paint : nullptr;
    for (int i = 0; i < s->rows.len; i++) {
        const CommandRow& row = s->rows[i];
        float h = RowHeight(row);
        if (canBuild && row.kind != CommandRowKind::Separator) {
            El* made = BuildCommandRow(cx, s, i, {}, -1, false);
            if (made) {
                // `div().refine_style(&text_style).child(row)` laid out as a
                // root: the wrapper is what carries the definite width.
                El* probe = Div(cx->a)->FlexCol()->Child(made);
                if (s->listW >= 0) {
                    probe->W(s->listW);
                }
                float got = MeasureEl(paint, probe, font).h;
                if (got > 0) {
                    h = got;
                }
            }
        }
        VecAppend(s->rowSizes, h);
    }
}

void CommandInstall(CommandState* s, Ctx* cx, const CommandEntry* entries,
                    int nEntries, bool searchable, bool filterable) {
    if (!s) {
        return;
    }
    s->entries = entries;
    s->nEntries = nEntries;
    s->searchable = searchable;
    s->filterable = filterable;
    Str query = TrimQuery(InputValue(&s->query));
    // Rust hears the field's Change event; a change is seen here as the query
    // no longer being the one the matches were worked out for, which is what
    // `applied_query` compares in both.
    bool queryChanged = !SameQuery(query, AppliedQuery(s));

    IndexPath prev = {};
    bool hadPrev = CommandSelectedIndex(s, &prev);
    // The rows are worked out again on every install, which is cheap and
    // keeps what they point into this render's model; it is measuring them
    // that an equal model skips. A new query is new rows.
    uint64_t key = ModelLayoutKey(entries, nEntries, searchable, filterable);
    if (key == 0 || key != s->layoutKey || queryChanged) {
        s->needsMeasure = true;
    }
    s->layoutKey = key;
    UpdateMatches(s, query);
    MeasureRows(s, cx);

    if (queryChanged) {
        SetApplied(s, query);
        ResetSelection(s);
        FireSelect(s, cx, hadPrev, prev);
        if (searchable && s->onQuery.IsValid()) {
            CommandEvent ev;
            ev.kind = CommandEventKind::Query;
            ev.query = query;
            ListenerCall(cx->app, cx->win, s->onQuery, &ev);
        }
        return;
    }

    // install_model: the highlight follows the item it was on, by path — the
    // model may have been rebuilt around it — and an owner that cleared it
    // keeps it cleared.
    int preserved = -1;
    if (hadPrev) {
        for (int i = 0; i < s->matched.len; i++) {
            if (!s->matched[i].disabled && s->matched[i].path == prev) {
                preserved = i;
                break;
            }
        }
    }
    if (preserved >= 0) {
        // Preserving the selection is not a navigation: the model reinstalls
        // on every host re-render, so scrolling here would move the list one
        // frame after a hover selection.
        s->selected = preserved;
        s->preserveNoSelection = false;
    } else if (s->preserveNoSelection) {
        s->selected = -1;
        s->pendingScroll = -1;
    } else {
        ResetSelection(s);
    }
    // install_model reports nothing: the highlight a model lands on is not
    // a selection the user made, so on_select waits for one.
}

void CommandSetSelectedIndex(CommandState* s, Ctx* cx, const IndexPath* path) {
    if (!s) {
        return;
    }
    int found = -1;
    if (path) {
        for (int i = 0; i < s->matched.len; i++) {
            if (!s->matched[i].disabled && s->matched[i].path == *path) {
                found = i;
                break;
            }
        }
    }
    if (s->selected == found) {
        s->preserveNoSelection = found < 0;
        return;
    }
    IndexPath prev = {};
    bool hadPrev = CommandSelectedIndex(s, &prev);
    s->selected = found;
    s->preserveNoSelection = found < 0;
    s->pendingScroll = found >= 0 ? s->matched[found].row : -1;
    FireSelect(s, cx, hadPrev, prev);
    Notify(cx);
}

void CommandSetQuery(CommandState* s, Ctx* cx, Str query) {
    if (!s) {
        return;
    }
    if (SameQuery(InputValue(&s->query), query)) {
        return;
    }
    InputSetValue(&s->query, query);
    // The re-filter and the callback are CommandInstall's, at the next
    // render: a programmatic write raises no Change event there either.
    Notify(cx);
}

void CommandSetLoading(CommandState* s, Ctx* cx, bool loading) {
    if (!s || s->loading == loading) {
        return;
    }
    s->loading = loading;
    Notify(cx);
}

// select(): the highlight moves to one match, and the caller hears about it.
// It does not scroll — hover goes through here, and revealing a half-clipped
// edge row would slide the next row under the resting cursor, hover-selecting
// and scrolling in a loop. Keyboard navigation, a query change and the public
// SetSelectedIndex ask for the scroll themselves.
static void SelectMatch(CommandState* s, Ctx* cx, int matchIx) {
    if (s->selected == matchIx) {
        return;
    }
    IndexPath prev = {};
    bool hadPrev = CommandSelectedIndex(s, &prev);
    s->selected = matchIx;
    s->preserveNoSelection = false;
    FireSelect(s, cx, hadPrev, prev);
    Notify(cx);
}

// select_by: `step` items on, wrapping around and skipping the disabled ones.
void CommandSelectBy(CommandState* s, Ctx* cx, int step) {
    int len = s->matched.len;
    if (len == 0) {
        return;
    }
    int next = s->selected;
    if (next < 0) {
        next = step >= 0 ? len - 1 : 0;
    }
    for (int i = 0; i < len; i++) {
        next = ((next + step) % len + len) % len;
        if (!s->matched[next].disabled) {
            // Only a real move scrolls, and only the keyboard path asks.
            if (s->selected != next) {
                s->pendingScroll = s->matched[next].row;
            }
            SelectMatch(s, cx, next);
            return;
        }
    }
}

static void ConfirmMatch(CommandState* s, Ctx* cx, int matchIx) {
    const CommandItem* item = ItemOfMatch(s, matchIx);
    if (!item || item->disabled) {
        return;
    }
    IndexPath path = s->matched[matchIx].path;
    intptr_t data = s->matched[matchIx].data;
    // The Action first, as Rust dispatches it before deferring the callback.
    if (item->action && cx->win) {
        WindowDispatchAction(cx->win, item->action, item->actionArg);
    }
    if (s->onConfirm.IsValid()) {
        CommandEvent ev;
        ev.kind = CommandEventKind::Confirm;
        ev.path = path;
        ev.data = data;
        ListenerCall(cx->app, cx->win, s->onConfirm, &ev);
    }
}

void CommandState::OnRowClick(CommandState* self, Ctx* cx, const ClickEvent*,
                              intptr_t match) {
    if (!self) {
        return;
    }
    ConfirmMatch(self, cx, (int)match);
    Notify(cx);
}

void CommandState::OnRowHover(CommandState* self, Ctx* cx, const HoverEvent* ev,
                              intptr_t match) {
    if (!self || !ev || !ev->hovered) {
        return;
    }
    int m = (int)match;
    if (m < 0 || m >= self->matched.len || self->matched[m].disabled) {
        return;
    }
    SelectMatch(self, cx, m);
}

void CommandState::OnAction(CommandState* self, Ctx* cx,
                            const ActionEvent* ev) {
    if (!self || !ev) {
        return;
    }
    uint32_t id = ev->action;
    // Both spellings of each chord. A palette's field is focused while it is
    // being typed into, and the innermost key context then is the field's own
    // — "up" there resolves to `input::MoveUp`, which a single-line field
    // does not take, so the action that arrives is the input's rather than
    // the palette's.
    if (id == action::SelectUp() || id == input::MoveUp()) {
        CommandSelectBy(self, cx, -1);
        return;
    }
    if (id == action::SelectDown() || id == input::MoveDown()) {
        CommandSelectBy(self, cx, 1);
        return;
    }
    if (id == action::Confirm() || id == input::Enter()) {
        if (self->selected >= 0) {
            ConfirmMatch(self, cx, self->selected);
            if (cx->win) {
                cx->win->eatReturn = true;
            }
            Notify(cx);
        }
        return;
    }
    if (id == action::Cancel() || id == input::Escape()) {
        // Escape clears a non-empty query first, and only then leaves the
        // palette — the dialog hosting it closes on the second press.
        if (self->searchable && len(InputValue(&self->query)) > 0) {
            CommandSetQuery(self, cx, Str{});
            return;
        }
        // The one synchronous callback: propagation carries on in this
        // dispatch, so a hosting dialog sees the escape once and owns the pop.
        if (self->onCancel.IsValid()) {
            CommandEvent e;
            e.kind = CommandEventKind::Cancel;
            ListenerCall(cx->app, cx->win, self->onCancel, &e);
        }
        const_cast<ActionEvent*>(ev)->propagate = true;
        return;
    }
    const_cast<ActionEvent*>(ev)->propagate = true;
}

Command* Command::New(Ctx* cx, Str id, Entity<CommandState> state) {
    Arena* a = cx->a;
    Command* c = ArenaNew<Command>(a);
    c->a = a;
    c->cx = cx;
    c->id = id;
    c->state = state;
    return c;
}
Command* Command::Entries(const CommandEntry* e, int n) {
    entries = e;
    nEntries = n;
    return this;
}
Command* Command::Items(const CommandItem* items, int n) {
    CommandEntry* e =
        (CommandEntry*)Alloc(a, (int)sizeof(CommandEntry) * (n > 0 ? n : 1));
    for (int i = 0; i < n; i++) {
        e[i] = CommandEntryOf(items[i]);
    }
    entries = e;
    nEntries = n;
    return this;
}
Command* Command::Searchable(bool v) {
    searchable = v;
    return this;
}
Command* Command::Filterable(bool v) {
    filterable = v;
    return this;
}
Command* Command::Placeholder(Str s) {
    placeholder = s;
    return this;
}
Command* Command::Empty(El* e) {
    empty = e;
    return this;
}
Command* Command::Header(El* e) {
    header = e;
    return this;
}
Command* Command::Header(El* (*build)(Ctx*, const CommandState*)) {
    headerFn = build;
    return this;
}
Command* Command::Footer(El* e) {
    footer = e;
    return this;
}
Command* Command::MaxH(float v) {
    maxH = v;
    return this;
}
Command* Command::Bordered(bool v) {
    bordered = v;
    return this;
}
Command* Command::W(float v) {
    w = v;
    return this;
}
Command* Command::Refine(const Style& s, uint32_t fields) {
    StyleApplyFields(&style, s, fields);
    styleSet |= fields;
    return this;
}
Command* Command::OnQuery(Listener fn) {
    onQuery = fn;
    return this;
}
Command* Command::OnSelect(Listener fn) {
    onSelect = fn;
    return this;
}
Command* Command::OnConfirm(Listener fn) {
    onConfirm = fn;
    return this;
}
Command* Command::OnCancel(Listener fn) {
    onCancel = fn;
    return this;
}

// What Rust's row-builder closure captures. It is frame-owned and carries an
// Entity handle rather than the state itself, so two nested or concurrent
// window renders cannot overwrite each other's command palette.
struct CommandRowContext {
    Entity<CommandState> state = {};
    float itemRadius = 0;
};

// item_radius: row corner radius, concentric with the frame: the list's p_1
// (plus the border when bordered) sits between a row and the frame's
// corner. A borderless command keeps the theme radius, since its frame
// belongs to the host. Command has no `.rounded(..)` here (it is not
// Styled), so Rust's own-radius arms never apply.
static float CommandItemRadius(const Ctx* cx, const Theme& th, bool bordered) {
    if (!bordered) {
        return th.radius;
    }
    float r = th.radiusLg - Rems(cx, 0.25f) - 1.f;
    return r > 0 ? r : 0;
}

static El* CommandRowEl(void* user, Ctx* cx, int rowIx) {
    CommandRowContext* rowCx = (CommandRowContext*)user;
    Entity<CommandState> entity = rowCx ? rowCx->state : Entity<CommandState>{};
    return BuildCommandRow(cx, entity.Get(cx), rowIx, entity,
                           rowCx ? rowCx->itemRadius : -1, true);
}

// render_row: one row as the list shows it. Measuring builds the same row
// without its listeners, which add nothing to its size.
static El* BuildCommandRow(Ctx* cx, CommandState* s, int rowIx,
                           Entity<CommandState> entity, float itemRadius,
                           bool interactive) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    if (!s || rowIx < 0 || rowIx >= s->rows.len) {
        return Div(a);
    }
    const CommandRow& row = s->rows[rowIx];
    if (row.kind == CommandRowKind::Separator) {
        return Div(a)->W(kFill)->PadY(4)->Child(
            Div(a)->W(kFill)->H(1)->Bg(th.border));
    }
    if (row.kind == CommandRowKind::Heading) {
        return Div(a)
            ->W(kFill)
            ->PadX(Rems(cx, 0.5f))
            ->PadY(Rems(cx, 0.375f))
            ->Child(TextEl(a, row.heading)->Font(12)->Medium()->Fg(th.mutedFg));
    }

    int matchIx = row.match;
    const CommandItem* item = ItemOfMatch(s, matchIx);
    if (!item) {
        return Div(a);
    }
    bool disabled = item->disabled;
    bool selected = s->selected == matchIx && !disabled;
    Rgba iconFg = selected ? th.accentFg : th.mutedFg;
    El* line = Div(a)
                   ->Role(AccessibilityRole::ListBoxOption)
                   ->AriaSelected(selected)
                   ->AriaDisabled(disabled)
                   ->FlexRow()
                   ->W(kFill)
                   ->ItemsCenter()
                   ->Gap(Rems(cx, 0.5f))
                   ->PadX(Rems(cx, 0.5f))
                   ->PadY(Rems(cx, 0.375f))
                   ->Radius(itemRadius >= 0 ? itemRadius : th.radius);
    if (selected) {
        line->Bg(th.tokens.accent);
    }
    if (item->content) {
        line->Child(item->content(cx, item));
    } else {
        El* content = Div(a)
                          ->FlexRow()
                          ->Flex1()
                          ->Gap(Rems(cx, 0.5f))
                          ->ItemsCenter()
                          ->MinW(0);
        if (item->icon != IconName::None) {
            content->Child(IconEl(a, item->icon, Rems(cx, 1))->Fg(iconFg));
        }
        if (len(item->label) > 0) {
            content->Child(
                TextEl(a, item->label)
                    ->Font(14)
                    ->Fg(disabled ? th.mutedFg
                                  : (selected ? th.accentFg : th.foreground)));
        }
        line->Child(content);
        // The binding owns the trailing slot, so only an item without one
        // shows its check there. A custom row draws its own.
        Kbd* kbd = item->action
                       ? Kbd::ForAction(cx, item->action, item->actionContext)
                       : nullptr;
        if (kbd) {
            line->Child(Div(a)->Flex1());
            line->Child(kbd->IntoEl());
        } else if (item->checked) {
            line->Child(Div(a)->Flex1());
            line->Child(IconEl(a, IconName::Check, 14)
                            ->Fg(selected ? th.accentFg : th.foreground));
        }
    }
    if (interactive && !disabled) {
        Listener click = ListenTo(entity, &CommandState::OnRowClick, 0);
        Listener hover = ListenTo(entity, &CommandState::OnRowHover, 0);
        line->HoverBg(th.tokens.accent);
        BindClick(line, StrDup(cx->a, fmt("row-%d", rowIx)),
                  ListenerArg(click, matchIx));
        line->OnHover(ListenerArg(hover, matchIx));
    }
    return line;
}

// The list container's on_prepaint: the width its rows get (less the
// virtual list's p_1 either side) and the font they inherit, which is
// Rust's ListMeasurementKey. A key the rows were not measured with asks for
// one more frame, which measures them again.
struct CommandListProbe {
    Entity<CommandState> state = {};
};

static void CommandListPrePaint(PaintCtx* ctx, El* e, void* user) {
    auto* probe = (CommandListProbe*)user;
    CommandState* s = probe && ctx ? probe->state.Get(ctx->app) : nullptr;
    if (!s) {
        return;
    }
    float w = e->w - WindowRemSize(ctx->window) * 0.5f;
    if (w < 0) {
        w = 0;
    }
    float dw = w - s->listW;
    bool changed = s->listW < 0 || dw > 0.5f || dw < -0.5f;
    if (changed) {
        s->listW = w;
    }
    if (e->laidFont > 0 && e->laidFont != s->listFont) {
        s->listFont = e->laidFont;
        changed = true;
    }
    if (changed && s->rows.len > 0 && ctx->window) {
        WindowRequestAnimationFrame(ctx->window);
    }
}

static El* DefaultEmpty(Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    return Div(a)
        ->W(kFill)
        ->PadY(Rems(cx, 1.5f))
        ->ItemsCenter()
        ->JustifyCenter()
        ->TextCenter()
        ->Child(TextEl(a, Tr("Command.empty"))->Font(14)->Fg(th.mutedFg));
}

El* Command::IntoEl() {
    const Theme& th = ThemeNow(cx->app);
    CommandState* s = state.Get(cx);
    El* box = Div(a)
                  ->FlexCol()
                  ->W(w)
                  ->ClipY()
                  ->Bg(th.tokens.popover)
                  ->Fg(th.popoverFg);
    if (bordered) {
        box->Radius(th.radiusLg)->Border(1, th.border);
    }
    // `.refine_style(&self.options.style)`: after the surface and the
    // border, so a caller's `rounded` or `min_h` wins over them.
    box->Refine(style, styleSet);
    if (!s) {
        return box;
    }
    s->onQuery = onQuery;
    s->onSelect = onSelect;
    s->onConfirm = onConfirm;
    s->onCancel = onCancel;
    // t!("Command.placeholder") where the caller named none, which is Rust's
    // own `unwrap_or_else` on the same key.
    InputSetPlaceholder(&s->query, len(placeholder) > 0
                                       ? placeholder
                                       : Tr("Command.placeholder"));
    CommandInstall(s, cx, entries, nEntries, searchable, filterable);

    if (headerFn) {
        box->Child(headerFn(cx, s));
    } else if (header) {
        box->Child(header);
    }
    if (searchable) {
        // `div().flex_none().px_3().border_b_1()` around an Input with the
        // search icon as its prefix, no appearance and `p_0`.
        El* field = Div(a)
                        ->FlexRow()
                        ->W(kFill)
                        ->Shrink0()
                        ->PadX(Rems(cx, 0.75f))
                        ->Gap(8)
                        ->ItemsCenter()
                        ->BorderB(1, th.border);
        field->Child(Div(a)->Flex1()->Child(
            Input::New(cx, StrL("query"), &s->query)
                ->Prefix(IconEl(a, IconName::Search)->Fg(th.mutedFg))
                ->Appearance(false)
                ->IntoEl()
                ->Pad(0)));
        // `input.set_loading`: there is no spinner inside a field here, so the
        // palette puts one where the field ends.
        if (s->loading) {
            field->Child(Spinner::New(cx)
                             ->Id(StrL("spinner"))
                             ->WithSize(UiSize::Small)
                             ->IntoEl());
        }
        box->Child(field);
    }

    // The list's p_1, on the virtual list or, empty, on the box around it.
    float inset = Rems(cx, 0.25f);
    float listMaxH = maxH >= 0 ? maxH : Rems(cx, 18.75f);
    El* listBox = Div(a)
                      ->Role(AccessibilityRole::ListBox)
                      ->FlexCol()
                      ->W(kFill)
                      ->MaxH(listMaxH)
                      ->ClipY();
    CommandListProbe* probe = ArenaNew<CommandListProbe>(a);
    probe->state = state;
    listBox->prePaint = &CommandListPrePaint;
    listBox->customUser = probe;
    if (s->rows.len == 0) {
        // The inset is the list's own; only the empty slot needs it from the
        // box around it.
        listBox->Pad(inset);
        // While a search is in flight the list is empty because the answer
        // has not arrived, which is not the same as no match.
        if (!s->loading) {
            listBox->Child(empty ? empty : DefaultEmpty(cx));
        }
    } else {
        float content = VirtualListContentSize(s->rowSizes.els, s->rows.len);
        float viewMax = listMaxH - inset * 2;
        float viewH = content < viewMax ? content : viewMax;
        if (s->pendingScroll >= 0) {
            VirtualListScrollToItemDeferred(&s->scroll, s->pendingScroll,
                                            ScrollStrategy::Top);
            s->pendingScroll = -1;
        }
        CommandRowContext* rowCx = ArenaNew<CommandRowContext>(a);
        rowCx->state = state;
        rowCx->itemRadius = CommandItemRadius(cx, th, bordered);
        El* list = VirtualList::New(cx, s->rows.len)
                       ->Id(StrL("list"))
                       ->Sizes(s->rowSizes.els)
                       ->ViewH(viewH)
                       ->Handle(&s->scroll)
                       ->Axis(ScrollAxis::Vertical)
                       ->Pad(inset)
                       ->Row(CommandRowEl, rowCx)
                       ->IntoEl();
        listBox->Child(list);
    }
    box->Child(listBox);
    if (footer) {
        box->Child(footer);
    }

    // `.key_context(CONTEXT)` and the four handlers under it. The field is
    // inside the palette, so a chord it does not take reaches these on its way
    // out — which is what the pair of action ids each handler answers to is
    // about.
    CommandInitKeys();
    Listener onAction = ListenTo(state, &CommandState::OnAction);
    box->PathFocus(id)
        ->FocusRing(false)
        ->KeyContext(CommandContext())
        ->OnAction(action::SelectUp(), onAction)
        ->OnAction(action::SelectDown(), onAction)
        ->OnAction(action::Confirm(), onAction)
        ->OnAction(action::Cancel(), onAction)
        ->OnAction(input::MoveUp(), onAction)
        ->OnAction(input::MoveDown(), onAction)
        ->OnAction(input::Enter(), onAction)
        ->OnAction(input::Escape(), onAction);
    return box;
}

} // namespace component
} // namespace gpui
