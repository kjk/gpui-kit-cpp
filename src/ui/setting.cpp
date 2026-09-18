#include "ui/i18n.h"
#include "ui/setting.h"
#include "ui/button.h"
#include "ui/checkbox.h"
#include "ui/input.h"
#include "ui/select.h"
#include "ui/switch.h"

namespace gpui {

namespace component {

RenderOptions RenderOptions::New() {
    return {};
}

RenderOptions RenderOptions::WithPageIx(int value) const {
    RenderOptions out = *this;
    out.pageIx = value;
    return out;
}

RenderOptions RenderOptions::WithGroupIx(int value) const {
    RenderOptions out = *this;
    out.groupIx = value;
    return out;
}

RenderOptions RenderOptions::WithItemIx(int value) const {
    RenderOptions out = *this;
    out.itemIx = value;
    return out;
}

RenderOptions RenderOptions::WithSize(UiSize value) const {
    RenderOptions out = *this;
    out.size = value;
    return out;
}

RenderOptions RenderOptions::WithGroupVariant(GroupBoxVariant value) const {
    RenderOptions out = *this;
    out.groupVariant = value;
    return out;
}

RenderOptions RenderOptions::WithLayout(Axis value) const {
    RenderOptions out = *this;
    out.layout = value;
    return out;
}

RenderOptions RenderOptions::WithDisabled(bool value) const {
    RenderOptions out = *this;
    out.disabled = value;
    return out;
}

bool SettingItemMatches(const SettingItem* it, Str query) {
    if (len(query) <= 0) {
        return true;
    }
    if (base::StrContainsI(it->title, query) ||
        base::StrContainsI(it->description, query)) {
        return true;
    }
    for (int i = 0; i < it->keywords.len; i++) {
        if (base::StrContainsI(it->keywords[i], query)) {
            return true;
        }
    }
    return false;
}

bool SettingGroupMatches(const SettingGroup* g, Str query) {
    if (len(query) <= 0) {
        return true;
    }
    // A group is shown when anything in it is: Rust drops a group whose
    // filtered items came out empty.
    for (const SettingItem& it : g->items) {
        if (SettingItemMatches(&it, query)) {
            return true;
        }
    }
    return false;
}

bool SettingPageMatches(const SettingPage* p, Str query) {
    if (len(query) <= 0) {
        return true;
    }
    for (const SettingGroup& g : p->groups) {
        if (SettingGroupMatches(&g, query)) {
            return true;
        }
    }
    return false;
}

void SettingsState::OnPageClick(SettingsState* self, Ctx* cx, const ClickEvent*,
                                intptr_t page) {
    self->page = (int)page;
    self->group = -1;
    Notify(cx);
}

void SettingsState::OnGroupClick(SettingsState* self, Ctx* cx,
                                 const ClickEvent*, intptr_t packed) {
    self->page = (int)(packed / 64);
    self->group = (int)(packed % 64);
    Notify(cx);
}

// The one selected index, or -1. A setting dropdown is single-select, which
// is what Rust's `SettingField<SharedString>::dropdown` is.
static int DropdownIndex(const SearchableListState* st) {
    return st && st->selected.len > 0 ? st->selected[0] : -1;
}

static SettingBinding* FieldAt(SettingsState* self, intptr_t ix) {
    if (!self || ix < 0 || ix >= self->fields.len) {
        return nullptr;
    }
    return &self->fields[(int)ix];
}

void SettingsState::OnFieldClick(SettingsState* self, Ctx* cx,
                                 const ClickEvent*, intptr_t ix) {
    SettingBinding* f = FieldAt(self, ix);
    if (!f) {
        return;
    }
    if (f->kind == SettingFieldKind::Switch ||
        f->kind == SettingFieldKind::Checkbox) {
        if (f->boolValue) {
            *f->boolValue = !*f->boolValue;
        }
    } else if (f->kind == SettingFieldKind::Dropdown) {
        SelectToggleOpen(f->list.Get(cx), cx);
    }
    Notify(cx);
}

void SettingsState::OnFieldReset(SettingsState* self, Ctx* cx,
                                 const ClickEvent*, intptr_t ix) {
    SettingBinding* f = FieldAt(self, ix);
    if (!f) {
        return;
    }
    switch (f->kind) {
        case SettingFieldKind::Switch:
        case SettingFieldKind::Checkbox:
            if (f->boolValue) {
                *f->boolValue = f->defBool;
            }
            break;
        case SettingFieldKind::Input:
        case SettingFieldKind::NumberInput:
            if (f->input) {
                InputSetValue(f->input, f->defStr);
            }
            break;
        case SettingFieldKind::Dropdown:
            if (SearchableListState* st = f->list.Get(cx)) {
                SearchableListSelectOnly(st, f->defIndex);
            }
            break;
        default:
            break;
    }
    Notify(cx);
}

void SettingsState::OnResetPage(SettingsState* self, Ctx* cx,
                                const ClickEvent* ev, intptr_t) {
    if (!self) {
        return;
    }
    for (int i = 0; i < self->fields.len; i++) {
        OnFieldReset(self, cx, ev, (intptr_t)i);
    }
}

void SettingsState::OnSearchFocus(SettingsState* self, Ctx* cx,
                                  const ClickEvent*) {
    self->search.focused = true;
    Notify(cx);
}

Settings* Settings::New(Ctx* cx, Str id, Entity<SettingsState> state) {
    Arena* a = cx->a;
    Settings* s = ArenaNew<Settings>(a);
    s->a = a;
    s->cx = cx;
    s->id = id;
    s->state = state.IsValid() ? state
                               : ElementStateEntity<SettingsState>(
                                     cx, id, StrL("gpui::SettingsState"));
    if (SettingsState* st = s->state.Get(cx)) {
        // settings.rs sets the field's placeholder itself rather than leaving
        // it to the caller, so the search box reads the same in every
        // application that shows one.
        if (!st->search.placeholder.s) {
            InputSetPlaceholder(&st->search, Tr("Settings.search_placeholder"));
        }
    }
    return s;
}

Settings* Settings::Page(Str title, IconName icon, Str description) {
    SettingPage pg;
    pg.title = title;
    pg.icon = icon;
    pg.description = description;
    pages.Append(a, pg);
    return this;
}

Settings* Settings::Group(Str title, Str description) {
    if (pages.len == 0) {
        Page(StrL("Settings"));
    }
    SettingPage& p = pages[pages.len - 1];
    SettingGroup g;
    g.title = title;
    g.description = description;
    p.groups.Append(a, g);
    return this;
}

static SettingItem* LastItem(Settings* s);

Settings* Settings::Item(Str title, Str description, El* control) {
    if (pages.len == 0) {
        Group({});
    }
    SettingPage& p = pages[pages.len - 1];
    if (p.groups.len == 0) {
        Group({});
    }
    SettingGroup& g = p.groups[p.groups.len - 1];
    SettingItem it;
    it.title = title;
    it.description = description;
    it.control = control;
    g.items.Append(a, it);
    return this;
}

Settings* Settings::FieldElement(SettingFieldElement element) {
    SettingItem* it = LastItem(this);
    if (it) {
        it->field = SettingFieldType::Element;
        it->fieldElement = element;
        it->control = nullptr;
    }
    return this;
}

// The item last added, which is what every modifier below reads.
static SettingItem* LastItem(Settings* s) {
    if (s->pages.len == 0) {
        return nullptr;
    }
    SettingPage& p = s->pages[s->pages.len - 1];
    if (p.groups.len == 0) {
        return nullptr;
    }
    SettingGroup& g = p.groups[p.groups.len - 1];
    return g.items.len > 0 ? &g.items[g.items.len - 1] : nullptr;
}

Settings* Settings::Keywords(Str a1, Str a2, Str a3) {
    SettingItem* it = LastItem(this);
    if (it) {
        it->keywords.Truncate(0);
        if (a1.s) {
            it->keywords.Append(a, a1);
        }
        if (a2.s) {
            it->keywords.Append(a, a2);
        }
        if (a3.s) {
            it->keywords.Append(a, a3);
        }
    }
    return this;
}

Settings* Settings::Keyword(Str keyword) {
    SettingItem* it = LastItem(this);
    if (it && keyword.s) {
        it->keywords.Append(a, keyword);
    }
    return this;
}

Settings* Settings::Disabled(bool v) {
    SettingItem* it = LastItem(this);
    if (it) {
        it->disabled = v;
    }
    return this;
}

Settings* Settings::Resettable(bool dirty, Listener onReset) {
    SettingItem* it = LastItem(this);
    if (it) {
        it->dirty = dirty;
        it->onReset = onReset;
    }
    return this;
}

Settings* Settings::Layout(Axis axis) {
    SettingItem* it = LastItem(this);
    if (it) {
        it->layout = axis;
    }
    return this;
}

Settings* Settings::SwitchField(bool* value, bool defValue, bool hasDefault) {
    SettingItem* it = LastItem(this);
    if (it) {
        it->field = SettingFieldKind::Switch;
        it->boolValue = value;
        it->defBool = defValue;
        it->hasDefault = hasDefault;
    }
    return this;
}

Settings* Settings::CheckboxField(bool* value, bool defValue, bool hasDefault) {
    SwitchField(value, defValue, hasDefault);
    SettingItem* it = LastItem(this);
    if (it) {
        it->field = SettingFieldKind::Checkbox;
    }
    return this;
}

Settings* Settings::InputField(Str value, Str defValue) {
    SettingItem* it = LastItem(this);
    if (it) {
        it->field = SettingFieldKind::Input;
        it->value = value;
        it->defStr = defValue;
        it->hasDefault = defValue.s != nullptr;
    }
    return this;
}

Settings* Settings::NumberField(Str value, NumberFieldOptions opts,
                                Str defValue) {
    InputField(value, defValue);
    SettingItem* it = LastItem(this);
    if (it) {
        it->field = SettingFieldKind::NumberInput;
        it->num = opts;
    }
    return this;
}

Settings* Settings::DropdownField(Entity<SearchableListState> list,
                                  const SearchableItem* items, int nItems,
                                  int defIndex) {
    SettingItem* it = LastItem(this);
    if (it) {
        it->field = SettingFieldKind::Dropdown;
        it->list = list;
        it->items = items;
        it->nItems = nItems;
        it->defIndex = defIndex;
        it->hasDefault = defIndex >= 0;
    }
    return this;
}

Settings* Settings::PageResettable(bool v) {
    if (pages.len > 0) {
        pages[pages.len - 1].resettable = v;
    }
    return this;
}

Settings* Settings::PageTitleSuffix(El* e) {
    if (pages.len > 0) {
        pages[pages.len - 1].titleSuffix = e;
    }
    return this;
}

Settings* Settings::FieldWidth(float v) {
    SettingItem* it = LastItem(this);
    if (it) {
        it->fieldW = v;
    }
    return this;
}

Settings* Settings::SidebarWidth(float v) {
    sidebarWidth = v;
    return this;
}

Settings* Settings::SidebarSizeRange(float minWidth, float maxWidth) {
    sidebarMinWidth = minWidth;
    sidebarMaxWidth = maxWidth;
    return this;
}

Settings* Settings::WithSize(UiSize value) {
    size = value;
    return this;
}

Settings* Settings::DefaultSelectedIndex(SelectIndex value) {
    defaultSelectedIndex = value;
    return this;
}

Settings* Settings::H(float v) {
    h = v;
    return this;
}
Settings* Settings::Bordered(bool v) {
    bordered = v;
    return this;
}

// The control a typed field renders as, and the two answers that come with
// it: whether the value has left its default, and the index the field's
// listeners bind. Rust's field renders itself out of the getter and the
// setter; here the getter is the pointer the caller handed over.
struct FieldEl {
    El* el = nullptr;
    bool dirty = false;
    bool resettable = false;
    Listener onReset = {};
};

static FieldEl RenderField(Ctx* cx, Settings* s, const SettingItem& it, Str id,
                           bool pageResettable, const RenderOptions& options) {
    FieldEl out;
    SettingsState* st = s->state.Get(cx);
    if (it.field == SettingFieldKind::Element || !st) {
        out.el = it.fieldElement.IsValid()
                     ? it.fieldElement.Render(&options, cx)
                     : it.control;
        out.dirty = it.dirty;
        out.resettable = it.onReset.IsValid();
        out.onReset = it.onReset;
        return out;
    }

    // The binding the listeners find this field again by, appended in the
    // order the fields paint.
    // The row's own field, keyed where the row is. Nothing outside asks for
    // it, so nothing outside holds it.
    InputState* input = nullptr;
    if (it.field == SettingFieldKind::Input ||
        it.field == SettingFieldKind::NumberInput) {
        Entity<SettingFieldInput> fe = ElementStateEntity<SettingFieldInput>(
            cx, StrL("field"), StrL("gpui::SettingFieldInput"));
        if (SettingFieldInput* f = fe.Get(cx)) {
            if (!f->seeded) {
                f->seeded = true;
                if (it.value.s) {
                    InputSetValue(&f->input, it.value);
                }
            }
            input = &f->input;
        }
    }

    SettingBinding b;
    b.kind = it.field;
    b.boolValue = it.boolValue;
    b.defBool = it.defBool;
    b.input = input;
    b.defStr = it.defStr;
    b.num = it.num;
    b.list = it.list;
    b.defIndex = it.defIndex;
    intptr_t ix = (intptr_t)st->fields.len;
    VecAppend(st->fields, b);

    Listener click = ListenTo(s->state, &SettingsState::OnFieldClick, ix);
    // layout(Axis): a field beside the text is w_32, one under it fills.
    float w = it.fieldW > 0
                  ? it.fieldW
                  : (options.layout == Axis::Horizontal ? 128.f : kFill);
    switch (it.field) {
        case SettingFieldKind::Switch:
            out.el = Switch::New(cx, id)
                         ->Checked(it.boolValue && *it.boolValue)
                         ->Disabled(options.disabled)
                         ->OnClick(click)
                         ->WithSize(options.size)
                         ->IntoEl();
            out.dirty = it.boolValue && *it.boolValue != it.defBool;
            break;
        case SettingFieldKind::Checkbox:
            out.el = Checkbox::New(cx, id)
                         ->Checked(it.boolValue && *it.boolValue)
                         ->Disabled(options.disabled)
                         ->OnClick(click)
                         ->WithSize(options.size)
                         ->IntoEl();
            out.dirty = it.boolValue && *it.boolValue != it.defBool;
            break;
        case SettingFieldKind::Input:
            out.el = Input::New(cx, id, input)
                         ->W(w)
                         ->Disabled(options.disabled)
                         ->WithSize(options.size)
                         ->IntoEl();
            out.dirty = input && !base::StrEq(InputValue(input), it.defStr);
            break;
        case SettingFieldKind::NumberInput:
            // The number engine owns stepping and range policy. In
            // particular, it keeps out-of-range intermediate text while the
            // user types and clamps only on blur.
            out.el = NumberInput::New(cx, id, input)
                         ->W(w)
                         ->Disabled(options.disabled)
                         ->WithSize(options.size)
                         ->Step(it.num.step)
                         ->Min(it.num.min)
                         ->Max(it.num.max)
                         ->IntoEl();
            out.dirty = input && !base::StrEq(InputValue(input), it.defStr);
            break;
        case SettingFieldKind::Dropdown: {
            out.el = Select::New(cx, id, it.list)
                         ->Items(it.items, it.nItems)
                         ->W(w)
                         ->Disabled(options.disabled)
                         ->WithSize(options.size)
                         ->OnToggle(click)
                         ->IntoEl();
            out.dirty = DropdownIndex(it.list.Get(cx)) != it.defIndex;
            break;
        }
        default:
            break;
    }
    // default_value: naming one is what puts the reset button behind the item.
    out.resettable = it.hasDefault && pageResettable;
    out.onReset = ListenTo(s->state, &SettingsState::OnFieldReset, ix);
    if (it.onReset.IsValid()) {
        // An explicit Resettable() still wins, the way Rust's reset_handler
        // overrides the typed default.
        out.resettable = pageResettable;
        out.dirty = it.dirty;
        out.onReset = it.onReset;
    }
    return out;
}

// One row: the title and description on the left, the field on the right —
// or under it, when the item asked for a vertical layout.
static El* RenderItem(Ctx* cx, Settings* s, const SettingItem& it, Str id,
                      int pageIx, int groupIx, int itemIx, bool first,
                      bool pageResettable, bool* anyDirty) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    // The row is what names the item, so the control on it and the reset
    // button beside it are named by their place on the row rather than by the
    // item's id spelled into each.
    IdScope scope(cx, id);
    // item.rs: `div().w_full()` with `gap_3`, `justify_between().items_start()`
    // when it is horizontal — and no padding and no rule of its own. The
    // padding is the GroupBox's `p_4` and the space between two items is its
    // `gap_4`; the port gave every item a box of its own and drew a line
    // between them, which is a table where Rust has a stack.
    El* line = Div(a)->Id(id)->W(kFill)->Gap(12);
    if (it.layout == Axis::Horizontal) {
        line->FlexRow()->ItemsCenter()->JustifyBetween();
    } else {
        line->FlexCol();
    }
    (void)first;
    El* text = Div(a)->FlexCol()->Flex1();
    text->Child(TextEl(a, it.title)
                    ->Font(16)
                    ->Fg(it.disabled ? th.mutedFg : th.foreground));
    if (it.description.s) {
        text->Child(
            TextEl(a, it.description)->Font(14)->Fg(th.mutedFg)->Wrap());
    }
    line->Child(text);
    El* right = Div(a)->FlexRow()->Gap(8)->ItemsCenter();
    RenderOptions options =
        RenderOptions::New()
            .WithPageIx(pageIx)
            .WithGroupIx(groupIx)
            .WithItemIx(itemIx)
            .WithSize(s->size)
            .WithGroupVariant(s->bordered ? GroupBoxVariant::Outline
                                          : GroupBoxVariant::Normal)
            .WithLayout(it.layout)
            .WithDisabled(it.disabled);
    FieldEl f = RenderField(cx, s, it, StrL("field"), pageResettable, options);
    if (f.dirty && f.resettable && f.onReset.IsValid()) {
        *anyDirty = true;
    }
    if (f.el) {
        right->Child(f.el);
    }
    // The reset button, which is only there once the item has been changed.
    if (f.dirty && f.resettable && f.onReset.IsValid()) {
        // Rust's reset button carries an Undo2 icon; the nearest one this
        // tree has is the arrow that points back.
        right->Child(Button::New(cx, StrL("reset"))
                         ->Icon(IconName::ArrowLeft)
                         ->Ghost()
                         ->WithSize(UiSize::XSmall)
                         ->OnClick(f.onReset)
                         ->IntoEl());
    }
    line->Child(right);
    return line;
}

El* Settings::IntoEl() {
    const Theme& th = ThemeNow(cx->app);
    SettingsState* st = state.Get(cx);
    Str query = st ? InputValue(&st->search) : Str{};
    // The bindings are this frame's, in the order the fields paint. The
    // listeners hung off the last frame's are still resolving against it
    // until this one has painted, which is the same table with the same
    // contents unless the tree itself changed.
    if (st) {
        if (!st->selectionInitialized) {
            st->selectionInitialized = true;
            st->page = defaultSelectedIndex.pageIx;
            st->group = defaultSelectedIndex.groupIx;
        }
        VecClear(st->fields);
    }

    // The whole settings pane is one widget, so its name is what scopes the
    // search field, the page rows, the group rows and every item under them —
    // and the states the fields keep, which is what the id stack is for.
    IdScope scope(cx, id);
    El* row = Div(a)->Id(id)->FlexRow()->W(kFill)->H(h)->ItemsStart();
    float sideWidth =
        std::max(sidebarMinWidth, std::min(sidebarWidth, sidebarMaxWidth));

    // The sidebar: the search field, then a row per page, with the groups of
    // the open page under it.
    El* side = Div(a)
                   ->FlexCol()
                   ->W(sideWidth)
                   ->H(kFill)
                   ->Pad(8)
                   ->Gap(4)
                   ->ClipX()
                   ->BorderR(1, th.border);
    if (st) {
        side->Child(
            Input::New(cx, StrL("search"), &st->search)
                ->Prefix(Div(a)->PadL(10)->Child(IconEl(a, IconName::Search, 16)
                                                     ->Fg(th.mutedFg)))
                ->WithSize(UiSize::Small)
                ->OnFocus(ListenTo(state, &SettingsState::OnSearchFocus))
                ->IntoEl());
        if (st->search.focused) {
            cx->win->input = &st->search;
        }
    }
    int selected = st ? st->page : 0;
    int i = -1;
    for (const SettingPage& p : pages) {
        i++;
        if (!SettingPageMatches(&p, query)) {
            continue;
        }
        bool active = i == selected;
        El* item = Div(a)
                       ->FlexRow()
                       ->W(kFill)
                       ->H(32)
                       ->PadX(8)
                       ->Gap(8)
                       ->ItemsCenter()
                       ->Radius(th.radius)
                       ->HoverBg(th.tokens.muted);
        if (active) {
            item->Bg(th.tokens.accent);
        }
        if (p.icon != IconName::None) {
            item->Child(IconEl(a, p.icon, 16)->Fg(th.foreground));
        }
        item->Child(Div(a)->Flex1()->ClipY()->Child(TextEl(a, p.title)
                                                        ->Font(16)
                                                        ->Fg(th.foreground)
                                                        ->MaxW(sideWidth - 80)
                                                        ->Truncate()));
        if (p.groups.len > 0) {
            item->Child(
                IconEl(a,
                       active ? IconName::ChevronDown : IconName::ChevronRight,
                       16)
                    ->Fg(th.mutedFg));
        }
        BindClick(item, StrDup(a, fmt("page-%d", i)),
                  ListenTo(state, &SettingsState::OnPageClick, (intptr_t)i));
        side->Child(item);
        // click_to_open: the open page lists its groups under it, and each
        // one jumps to that part of the page.
        if (!active) {
            continue;
        }
        int g = -1;
        for (const SettingGroup& group : p.groups) {
            g++;
            if (!SettingGroupMatches(&group, query)) {
                continue;
            }
            El* sub = Div(a)
                          ->FlexRow()
                          ->W(kFill)
                          ->H(32)
                          ->PadL(28)
                          ->ItemsCenter()
                          ->Radius(th.radius)
                          ->HoverBg(th.tokens.muted);
            if (st && st->group == g) {
                sub->Bg(BackgroundOpacity(th.tokens.accent, 0.6f));
            }
            sub->Child(TextEl(a, group.title)->Font(16)->Fg(th.foreground));
            BindClick(sub, StrDup(a, fmt("group-%d-%d", i, g)),
                      ListenTo(state, &SettingsState::OnGroupClick,
                               (intptr_t)i * 64 + (intptr_t)g));
            side->Child(sub);
        }
    }
    row->Child(side);

    // The page: its title, then a card per group.
    El* pane = Div(a)->FlexCol()->Flex1()->H(kFill)->ClipY();
    if (selected >= 0 && selected < pages.len) {
        const SettingPage& p = pages[selected];
        // The body first: whether the page offers Reset All is whether
        // anything on it came out dirty, which only the fields know.
        bool anyDirty = false;
        El* body = Div(a)->FlexCol()->W(kFill)->Pad(16)->Gap(8);
        int g = -1;
        for (const SettingGroup& grp : p.groups) {
            g++;
            if (!SettingGroupMatches(&grp, query)) {
                continue;
            }
            if (grp.title.s) {
                body->Child(
                    TextEl(a, grp.title)->Font(16)->Fg(th.mutedFg)->PadY(4));
            }
            // GroupBox's content pane: `p_4` and `gap_4`, `rounded(radius)`,
            // bordered only for the Outline variant.
            El* card = Div(a)->FlexCol()->W(kFill)->Gap(16)->Radius(th.radius);
            if (bordered) {
                card->Pad(16)->Border(1, th.border);
            }
            int shown = 0;
            int itemIx = -1;
            for (const SettingItem& it : grp.items) {
                itemIx++;
                if (!SettingItemMatches(&it, query)) {
                    continue;
                }
                card->Child(RenderItem(
                    cx, this, it,
                    StrDup(a, fmt("%d-%d-%d", selected, g, itemIx)), selected,
                    g, itemIx, shown == 0, p.resettable, &anyDirty));
                shown++;
            }
            body->Child(card);
        }

        // page.rs: the header is `v_flex().p_4().gap_3().border_b_1()`, and
        // the title sits in an `h_flex().gap_1()` with whatever `title_suffix`
        // the caller gave beside it.
        El* head = Div(a)->FlexCol()->W(kFill)->Pad(16)->Gap(12)->BorderB(
            1, th.border);
        El* titleRow =
            Div(a)->FlexRow()->W(kFill)->ItemsCenter()->JustifyBetween();
        El* titleCell = Div(a)->FlexRow()->ItemsCenter()->Gap(4);
        // page.rs puts the title in the header with no styling of its own,
        // so it is the page's own text and not a heading.
        titleCell->Child(TextEl(a, p.title)->Font(16)->Fg(th.foreground));
        if (p.titleSuffix) {
            titleCell->Child(p.titleSuffix);
        }
        titleRow->Child(titleCell);
        // reset_all: the page's own button, there once anything on it has
        // left its default.
        if (anyDirty) {
            titleRow->Child(
                Button::New(cx, StrL("reset-all"))
                    ->Icon(IconName::ArrowLeft)
                    ->Tooltip(Tr("Settings.Reset All"))
                    ->Ghost()
                    ->WithSize(UiSize::Small)
                    ->OnClick(ListenTo(state, &SettingsState::OnResetPage, 0))
                    ->IntoEl());
        }
        head->Child(titleRow);
        if (p.description.s) {
            head->Child(
                TextEl(a, p.description)->Font(14)->Fg(th.mutedFg)->Wrap());
        }
        pane->Child(head);
        pane->Child(body);
    }
    row->Child(pane);
    return row;
}

} // namespace component
} // namespace gpui
