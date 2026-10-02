#include "ui/i18n.h"
#include "ui/setting.h"
#include "ui/button.h"
#include "ui/checkbox.h"
#include "ui/input.h"
#include "ui/menu.h"
#include "ui/resizable.h"
#include "ui/select.h"
#include "ui/sidebar.h"
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
    // A custom element has no text to match, only its keywords.
    if (!it->isElement && (base::StrContainsI(it->title, query) ||
                           base::StrContainsI(it->description, query))) {
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

// Visible groups in each original page. Filtering never renumbers source data.
static int MatchingGroupCount(const SettingPage& p, Str query) {
    int n = 0;
    for (const SettingGroup& g : p.groups) {
        if (SettingGroupMatches(&g, query)) {
            n++;
        }
    }
    return n;
}

static bool PageHasMatchingGroup(const SettingPage& p, Str query) {
    return MatchingGroupCount(p, query) > 0;
}

SelectIndex SettingsResolveSelectedIndex(const ArenaVec<SettingPage>& pages,
                                         Str query, SelectIndex selected) {
    int pageIx = -1;
    if (selected.pageIx >= 0 && selected.pageIx < pages.len &&
        PageHasMatchingGroup(pages[selected.pageIx], query)) {
        pageIx = selected.pageIx;
    } else {
        for (int i = 0; i < pages.len; i++) {
            if (PageHasMatchingGroup(pages[i], query)) {
                pageIx = i;
                break;
            }
        }
    }
    if (pageIx < 0) {
        // Keep the selection while there are no results so clearing the query
        // can restore it. The empty filter prevents rendering a stale page.
        return selected;
    }
    SelectIndex out;
    out.pageIx = pageIx;
    out.groupIx = -1;
    if (selected.groupIx >= 0 && pageIx == selected.pageIx) {
        const SettingPage& p = pages[pageIx];
        if (selected.groupIx < p.groups.len &&
            SettingGroupMatches(&p.groups[selected.groupIx], query)) {
            out.groupIx = selected.groupIx;
        }
    }
    return out;
}

static bool SettingItemIsResettable(const SettingItem* it) {
    if (!it) {
        return false;
    }
    if (it->onReset.IsValid()) {
        return it->dirty;
    }
    if (!it->hasDefault) {
        return false;
    }
    if ((it->field == SettingFieldKind::Switch ||
         it->field == SettingFieldKind::Checkbox) &&
        it->boolValue) {
        return *it->boolValue != it->defBool;
    }
    return it->dirty;
}

bool SettingGroupIsResettable(const SettingGroup* g, Str query) {
    if (!g) {
        return false;
    }
    for (const SettingItem& it : g->items) {
        if (SettingItemMatches(&it, query) && SettingItemIsResettable(&it)) {
            return true;
        }
    }
    return false;
}

void SettingsState::OnPageClick(SettingsState* self, Ctx* cx, const ClickEvent*,
                                int64_t page) {
    self->page = (int)page;
    self->group = -1;
    self->deferredScrollGroup = -1;
    Notify(cx);
}

void SettingsState::OnGroupClick(SettingsState* self, Ctx* cx,
                                 const ClickEvent*, int64_t packed) {
    self->page = (int)(packed / 64);
    self->group = (int)(packed % 64);
    self->deferredScrollGroup = self->group;
    Notify(cx);
}

void SettingsState::OnPageScroll(SettingsState* self, Ctx* cx,
                                 const ScrollEvent* ev) {
    self->scrollY = ev->offsetY;
    Notify(cx);
}

// What the page's scroll hangs on at paint: its state, and the element the
// group being scrolled to starts with.
struct SettingsPageScroll {
    Entity<SettingsState> state = {};
    El* target = nullptr;
};

// ListState::scroll_to(ListOffset { item_ix, offset_in_item: 0 }): the group
// at the top of the page, clamped at the end. Scrolling by the group's own
// laid-out position rather than a measured list is what keeps a page that was
// just switched to from resolving to the top.
static void SettingsPageScrollPaint(PaintCtx* ctx, El* e, void* user) {
    auto* m = (SettingsPageScroll*)user;
    SettingsState* st = m ? m->state.Get(ctx->app) : nullptr;
    if (!st || st->pendingScrollGroup < 0 || !m->target) {
        return;
    }
    float top = m->target->y - (e->y - e->scrollY) - e->style.pad.top;
    float most = e->contentH - e->h;
    most = most > 0 ? most : 0;
    st->scrollY = top < 0 ? 0 : (top > most ? most : top);
    st->pendingScrollGroup = -1;
    Ctx cx = {ctx->app, ctx->window, nullptr, m->state.id};
    Notify(&cx);
}

// settings.rs STACKED_LAYOUT_MAX_WIDTH: a page panel this narrow or
// narrower lays every item out stacked.
static const float kStackedLayoutMaxWidth = 480;

// What the page panel's prepaint needs: the state the width is kept in, the
// layout this frame was built with, and what the page was built from.
struct SettingsContainerQuery {
    Entity<SettingsState> state = {};
    Axis layout = Axis::Horizontal;
    // Null when there is no page to build.
    Settings* settings = nullptr;
    Ctx cx = {};
    int selected = -1;
    Str query = {};
    int scrollGroup = -1;
    // The field bindings before the page's: a page built again binds its
    // fields again, at the same indices.
    int fieldsLen = 0;
};

static void SettingsBuildPage(Ctx* cx, Settings* s, El* pane, int selected,
                              Str query, Axis pageLayout, int scrollGroup);

// container_query's size, once layout has given the panel one. A width on
// the other side of the line from the layout this frame used builds the page
// again with the other layout, inside the panel it already has.
static void SettingsContainerPrePaint(PaintCtx* ctx, El* e, void* user) {
    auto* q = (SettingsContainerQuery*)user;
    SettingsState* st = q ? q->state.Get(ctx->app) : nullptr;
    if (!st) {
        return;
    }
    // Not inside a measure, which uses the scratch cache this lays out in.
    if (LayoutInScratchPass()) {
        return;
    }
    st->containerWidth = e->w;
    Axis want =
        e->w <= kStackedLayoutMaxWidth ? Axis::Vertical : Axis::Horizontal;
    if (want == q->layout) {
        return;
    }
    q->layout = want;
    if (!q->settings) {
        return;
    }
    if (st->fields.len > q->fieldsLen) {
        VecRemoveAtN(st->fields, q->fieldsLen, st->fields.len - q->fieldsLen);
    }
    e->first = nullptr;
    e->last = nullptr;
    Ctx cx = q->cx;
    SettingsBuildPage(&cx, q->settings, e, q->selected, q->query, want,
                      q->scrollGroup);
    IdsCollectChildren(e);
    LayoutEl(ctx, e, e->x, e->y, e->w, e->h, e->laidFont, e->style.color);
}

// The one selected index, or -1. A setting dropdown is single-select, which
// is what Rust's `SettingField<SharedString>::dropdown` is.
static int DropdownIndex(const SearchableListState* st) {
    return st && st->selected.len > 0 ? st->selected[0] : -1;
}

static SettingBinding* FieldAt(SettingsState* self, int64_t ix) {
    if (!self || ix < 0 || ix >= self->fields.len) {
        return nullptr;
    }
    return &self->fields[(int)ix];
}

void SettingsState::OnFieldClick(SettingsState* self, Ctx* cx,
                                 const ClickEvent*, int64_t ix) {
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

void SettingsState::OnDropdownPick(SettingsState* self, Ctx* cx,
                                   const ClickEvent*, int64_t packed) {
    SettingBinding* f = FieldAt(self, packed / kDropdownOptionsMax);
    if (!f || f->kind != SettingFieldKind::Dropdown) {
        return;
    }
    if (SearchableListState* st = f->list.Get(cx)) {
        SearchableListSelectOnly(st, (int)(packed % kDropdownOptionsMax));
    }
    Notify(cx);
}

void SettingsState::OnFieldReset(SettingsState* self, Ctx* cx,
                                 const ClickEvent*, int64_t ix) {
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

// SettingItem::reset: the on_reset an Element field or item gave, or the
// typed field's default_value. A field with neither is left as it is.
static void ResetBinding(SettingsState* self, Ctx* cx, const ClickEvent* ev,
                         int64_t ix) {
    SettingBinding* f = FieldAt(self, ix);
    if (!f) {
        return;
    }
    if (f->onReset.IsValid()) {
        ListenerCall(cx->app, cx->win, f->onReset, ev);
        Notify(cx);
        return;
    }
    if (f->hasDefault) {
        SettingsState::OnFieldReset(self, cx, ev, ix);
    }
}

void SettingsState::OnResetPage(SettingsState* self, Ctx* cx,
                                const ClickEvent* ev, int64_t) {
    if (!self) {
        return;
    }
    for (int i = 0; i < self->fields.len; i++) {
        ResetBinding(self, cx, ev, (int64_t)i);
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

Settings* Settings::GroupVariant(GroupBoxVariant variant) {
    if (pages.len == 0) {
        return this;
    }
    SettingPage& p = pages[pages.len - 1];
    if (p.groups.len > 0) {
        p.groups[p.groups.len - 1].variant = variant;
        p.groups[p.groups.len - 1].hasVariant = true;
    }
    return this;
}

Settings* Settings::GroupFooter(El* footer) {
    if (pages.len == 0) {
        return this;
    }
    SettingPage& p = pages[pages.len - 1];
    if (p.groups.len > 0) {
        p.groups[p.groups.len - 1].footer = footer;
    }
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

Settings* Settings::ElementItem(El* content) {
    Item({}, {}, content);
    SettingItem* it = LastItem(this);
    if (it) {
        it->isElement = true;
    }
    return this;
}

Settings* Settings::DescriptionEl(El* e) {
    SettingItem* it = LastItem(this);
    if (it) {
        it->descriptionEl = e;
    }
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

Settings* Settings::PageDefaultOpen(bool v) {
    if (pages.len > 0) {
        pages[pages.len - 1].defaultOpen = v;
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
Settings* Settings::WithGroupVariant(GroupBoxVariant v) {
    groupVariant = v;
    return this;
}

Settings* Settings::Bordered(bool v) {
    groupVariant = v ? GroupBoxVariant::Outline : GroupBoxVariant::Normal;
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
        out.resettable = it.onReset.IsValid() && pageResettable;
        out.onReset = it.onReset;
        // An Element field resets only through its on_reset, which Reset
        // All finds by the binding like any other field's.
        if (st && it.onReset.IsValid()) {
            SettingBinding b;
            b.kind = SettingFieldKind::Element;
            b.onReset = it.onReset;
            VecAppend(st->fields, b);
        }
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
    b.hasDefault = it.hasDefault;
    // SettingField::on_reset wins over default_value, as reset_handler does.
    b.onReset = it.onReset;
    int64_t ix = (int64_t)st->fields.len;
    VecAppend(st->fields, b);

    Listener click = ListenTo(s->state, &SettingsState::OnFieldClick, ix);
    // layout(Axis): a field beside the text is w_32 (number.rs) or w_64
    // (string.rs); one under it fills.
    float w = it.fieldW > 0 ? it.fieldW
              : options.layout == Axis::Horizontal
                  ? Rems(cx, it.field == SettingFieldKind::Input ? 16.f : 8.f)
                  : kFill;
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
            // fields/dropdown.rs: an outline Button with a caret, labelled
            // with the current option, full width when stacked, and a
            // dropdown menu anchored TopRight whose rows tick the current
            // option and choose one on click.
            int current = DropdownIndex(it.list.Get(cx));
            Str label = current >= 0 && current < it.nItems
                            ? it.items[current].title
                            : Str{};
            Button* btn = Button::New(cx, StrL("btn"))
                              ->Label(label)
                              ->DropdownCaret(true)
                              ->Outline()
                              ->Disabled(options.disabled)
                              ->WithSize(options.size);
            El* trigger = btn->IntoEl();
            if (it.fieldW > 0) {
                trigger->W(it.fieldW);
            } else if (options.layout == Axis::Vertical) {
                trigger->W(kFill);
            }
            PopupMenu* menu = PopupMenu::New(cx, StrL("menu"));
            for (int k = 0; k < it.nItems; k++) {
                menu->MenuWithCheck(it.items[k].title, k == current)
                    ->OnClick(ListenTo(s->state, &SettingsState::OnDropdownPick,
                                       ix * kDropdownOptionsMax + k));
            }
            out.el = DropdownMenu::New(cx, StrL("dropdown"))
                         ->Trigger(trigger)
                         ->Menu(menu)
                         ->AnchorRight(true)
                         ->IntoEl();
            if (options.layout == Axis::Vertical) {
                out.el->W(kFill);
            }
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
                      int pageIx, int groupIx, int itemIx, Axis pageLayout,
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
    // A stacked page (the container query's Vertical) stacks every item;
    // otherwise the item's own layout stands.
    Axis layout = pageLayout == Axis::Vertical ? Axis::Vertical : it.layout;
    El* line = Div(a)->Id(id)->W(kFill)->Gap(Rems(cx, 0.75f));
    if (it.disabled) {
        line->Opacity(0.5f);
    }
    RenderOptions options = RenderOptions::New()
                                .WithPageIx(pageIx)
                                .WithGroupIx(groupIx)
                                .WithItemIx(itemIx)
                                .WithSize(s->size)
                                .WithGroupVariant(s->groupVariant)
                                .WithLayout(layout)
                                .WithDisabled(it.disabled);
    // SettingItem::Element: `div().w_full()` around whatever the caller
    // rendered, which takes the row whole.
    if (it.isElement) {
        FieldEl f =
            RenderField(cx, s, it, StrL("field"), pageResettable, options);
        if (f.dirty && f.resettable && f.onReset.IsValid()) {
            *anyDirty = true;
        }
        if (f.el) {
            line->Child(f.el);
        }
        return line;
    }
    if (layout == Axis::Horizontal) {
        line->FlexRow()->ItemsCenter()->JustifyBetween();
    } else {
        line->FlexCol();
    }
    // The label column: flex_1().max_w_3_5() beside the field, w_full over
    // it; Label::new(title).text_sm(), and the description text_sm muted.
    El* text = Div(a)->FlexCol();
    if (layout == Axis::Horizontal) {
        text->Flex1()->MaxWFrac(0.6f);
    } else {
        text->W(kFill);
    }
    text->Child(TextEl(a, it.title)->Font(14)->Fg(th.foreground)->Wrap());
    // `div().size_full().text_sm().text_color(muted_foreground)` around the
    // description, which may be an element (a markdown TextView).
    if (it.descriptionEl) {
        text->Child(Div(a)
                        ->W(kFill)
                        ->Font(14)
                        ->Fg(th.mutedFg)
                        ->Child(it.descriptionEl));
    } else if (it.description.s) {
        text->Child(
            TextEl(a, it.description)->Font(14)->Fg(th.mutedFg)->Wrap());
    }
    line->Child(text);
    // `div().id("field")`: a plain block, so a field stacked under its
    // label is as wide as the item. Rust puts no reset button beside a
    // changed field; the page header's Reset All is the only one.
    El* right = Div(a);
    FieldEl f = RenderField(cx, s, it, StrL("field"), pageResettable, options);
    if (f.dirty && f.resettable && f.onReset.IsValid()) {
        *anyDirty = true;
    }
    if (f.el) {
        right->Child(f.el);
    }
    line->Child(right);
    return line;
}

// The selected page, built into `pane` with the layout the container query
// chose. Built again at prepaint, inside the same pane, when the width
// layout gave the panel puts it on the other side of the line.
static void SettingsBuildPage(Ctx* cx, Settings* s, El* pane, int selected,
                              Str query, Axis pageLayout, int scrollGroup) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    Entity<SettingsState> state = s->state;
    SettingsState* st = state.Get(cx);
    GroupBoxVariant groupVariant = s->groupVariant;
    const SettingPage& p = s->pages[selected];
    // The body first: whether the page offers Reset All is whether
    // anything on it came out dirty, which only the fields know.
    bool anyDirty = false;
    SettingsPageScroll* scroll = ArenaNew<SettingsPageScroll>(a);
    scroll->state = state;
    // `div().px_4().flex_1().w_full()` around the list of groups.
    El* body =
        Div(a)
            ->Id(StrL("page-body"))
            ->FlexCol()
            ->W(kFill)
            ->Flex1()
            ->MinH(0)
            ->PadX(Rems(cx, 1.f))
            ->ClipY()
            ->ScrollY(st ? st->scrollY : 0)
            ->ScrollId((int)IdFoldName(cx->path, fmt("page-%d", selected)))
            ->OnScroll(ListenTo(state, &SettingsState::OnPageScroll));
    int g = -1;
    for (const SettingGroup& grp : p.groups) {
        g++;
        if (!SettingGroupMatches(&grp, query)) {
            continue;
        }
        // group.rs renders a GroupBox; `self.variant.unwrap_or(options.
        // group_variant())`: a group's own variant wins over the
        // settings-level one.
        GroupBoxVariant variant = grp.hasVariant ? grp.variant : groupVariant;
        bool padded = variant != GroupBoxVariant::Normal;
        // The GroupBox root: v_flex w_full, gap_3 around a padded surface
        // and gap_4 around a plain one; the page gives each group `py_4`
        // and the group's own style refines it last.
        El* box = Div(a)
                      ->FlexCol()
                      ->W(kFill)
                      ->Gap(Rems(cx, padded ? 0.75f : 1.f))
                      ->PadY(Rems(cx, 1.f));
        if (grp.title.s) {
            // The title slot: muted, line_height 1.25, holding
            // `v_flex().gap_1()` of the title and a text_sm description.
            El* title = Div(a)
                            ->FlexCol()
                            ->Gap(Rems(cx, 0.25f))
                            ->Fg(th.mutedFg)
                            ->LineHeight(1.25f);
            title->Child(TextEl(a, grp.title)->Wrap());
            if (grp.description.s) {
                title->Child(TextEl(a, grp.description)
                                 ->Font(14)
                                 ->Fg(th.mutedFg)
                                 ->Wrap());
            }
            box->Child(title);
        }
        // The surface: gap_4, rounded, p_4 and a border (Outline) or a
        // fill (Fill), in group_box_foreground.
        El* card = Div(a)
                       ->FlexCol()
                       ->W(kFill)
                       ->Gap(Rems(cx, 1.f))
                       ->Radius(th.radius)
                       ->Fg(th.groupBoxFg);
        if (variant == GroupBoxVariant::Outline) {
            card->Pad(Rems(cx, 1.f))->Border(1, th.border);
        } else if (variant == GroupBoxVariant::Fill) {
            card->Pad(Rems(cx, 1.f))->Bg(th.groupBox);
        }
        int itemIx = -1;
        for (const SettingItem& it : grp.items) {
            itemIx++;
            if (!SettingItemMatches(&it, query)) {
                continue;
            }
            card->Child(RenderItem(
                cx, s, it, StrDup(a, fmt("%d-%d-%d", selected, g, itemIx)),
                selected, g, itemIx, pageLayout, p.resettable, &anyDirty));
        }
        // The surface and the footer share a `v_flex().gap_2()`, so the
        // footer's 8 px is its own and not the root's gap.
        El* slot =
            Div(a)->FlexCol()->W(kFill)->Gap(Rems(cx, 0.5f))->Child(card);
        if (grp.footer) {
            slot->Child(Div(a)->Font(14)->Fg(th.mutedFg)->Child(grp.footer));
        }
        box->Child(slot);
        grp.refiner.Apply(box);
        if (g == scrollGroup) {
            scroll->target = box;
        }
        body->Child(box);
    }

    // page.rs: the header is `v_flex().p_4().gap_3().border_b_1()`, and
    // the title sits in an `h_flex().gap_1()` with whatever `title_suffix`
    // the caller gave beside it.
    El* head = Div(a)
                   ->FlexCol()
                   ->W(kFill)
                   ->Pad(Rems(cx, 1.f))
                   ->Gap(Rems(cx, 0.75f))
                   ->BorderB(1, th.border);
    El* titleRow = Div(a)->FlexRow()->W(kFill)->ItemsCenter()->JustifyBetween();
    El* titleCell = Div(a)->FlexRow()->ItemsCenter()->Gap(Rems(cx, 0.25f));
    // page.rs puts the title in the header with no styling of its own,
    // so it is the page's own text and not a heading.
    titleCell->Child(TextEl(a, p.title)->Fg(th.foreground));
    if (p.titleSuffix) {
        titleCell->Child(p.titleSuffix);
    } else if (p.titleSuffixFn) {
        if (El* suffix = p.titleSuffixFn(p.titleSuffixUser, cx))
            titleCell->Child(suffix);
    }
    titleRow->Child(titleCell);
    // reset_all: the page's own button, there once anything on it has
    // left its default.
    if (anyDirty) {
        titleRow->Child(
            Button::New(cx, StrL("reset-all"))
                ->Icon(IconName::Undo2)
                ->Tooltip(Tr("Settings.Reset All"))
                ->Ghost()
                ->WithSize(UiSize::Small)
                ->OnClick(ListenTo(state, &SettingsState::OnResetPage, 0))
                ->IntoEl());
    }
    head->Child(titleRow);
    if (p.description.s) {
        head->Child(TextEl(a, p.description)->Font(14)->Fg(th.mutedFg)->Wrap());
    }
    if (st && scroll->target) {
        st->pendingScrollGroup = scrollGroup;
    }
    body->customPaint = &SettingsPageScrollPaint;
    body->customUser = scroll;
    pane->Child(head);
    pane->Child(body);
}

El* Settings::IntoEl() {
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
        SelectIndex previous;
        previous.pageIx = st->page;
        previous.groupIx = st->group;
        SelectIndex selectedIx =
            SettingsResolveSelectedIndex(pages, query, previous);
        if (selectedIx.pageIx != previous.pageIx ||
            selectedIx.groupIx != previous.groupIx) {
            st->page = selectedIx.pageIx;
            st->group = selectedIx.groupIx;
            st->deferredScrollGroup = -1;
        }
        VecClear(st->fields);
    }

    // The whole settings pane is one widget, so its name is what scopes the
    // search field, the page rows, the group rows and every item under them —
    // and the states the fields keep, which is what the id stack is for.
    IdScope scope(cx, id);
    // Rust's sidebar panel starts at sidebar_width, but the page panel beside
    // it is size_full with no size of its own: from the second frame both
    // shrink to fit, the sidebar's new width is fed back as its flex_basis,
    // and it settles at the bottom of its size range. The panel opens at that
    // width here, which is what gpui-kit shows; a drag still resizes it.
    float sideWidth = sidebarMinWidth;

    // render_sidebar: a Sidebar the width of its panel, borderless and not
    // collapsible, with the search field as its header and one SidebarMenu
    // of the pages that still have a matching group.
    Sidebar* sidebar = Sidebar::New(cx, StrL("settings-sidebar"))
                           ->Collapsible(false)
                           ->Collapsed(false);
    if (st) {
        El* search =
            Input::New(cx, StrL("search"), &st->search)
                ->Prefix(IconEl(a, IconName::Search))
                ->OnFocus(ListenTo(state, &SettingsState::OnSearchFocus))
                ->IntoEl();
        sidebar->Header(Div(a)->W(kFill)->Child(search));
        if (st->search.focused) {
            cx->win->input = &st->search;
        }
    }
    SidebarMenu* menu = SidebarMenu::New(cx);
    int selected = st ? st->page : 0;
    int selectedGroup = st ? st->group : -1;
    int i = -1;
    for (const SettingPage& p : pages) {
        i++;
        int visibleGroups = MatchingGroupCount(p, query);
        if (visibleGroups == 0) {
            continue;
        }
        // The page row is active when no group is selected, or when the page
        // has only one visible group — the group cannot be a separate row.
        bool pageActive =
            i == selected &&
            (st == nullptr || selectedGroup < 0 || visibleGroups == 1);
        SidebarMenuItem* item =
            SidebarMenuItem::New(cx, p.title)
                ->ClickToOpen(true)
                ->DefaultOpen(p.defaultOpen)
                ->Active(pageActive)
                ->OnClick(
                    ListenTo(state, &SettingsState::OnPageClick, (int64_t)i));
        if (p.icon != IconName::None) {
            item->Icon(p.icon);
        }
        // Each titled group is a row under its page, and jumps to that part
        // of it. Clicks bind the original group index, not the visible
        // position.
        if (visibleGroups > 1) {
            int g = -1;
            for (const SettingGroup& group : p.groups) {
                g++;
                if (!SettingGroupMatches(&group, query) || !group.title.s) {
                    continue;
                }
                item->Child(
                    SidebarMenuItem::New(cx, group.title)
                        ->Active(i == selected && selectedGroup == g)
                        ->OnClick(ListenTo(state, &SettingsState::OnGroupClick,
                                           (int64_t)i * 64 + (int64_t)g)));
            }
        }
        menu->Child(item);
    }
    sidebar->Child(menu);
    // `.w(relative(1.)).border_0()`: the panel is what sizes it.
    El* side = sidebar->IntoEl()->W(kFill);
    side->style.border = 0;
    side->style.borderT = side->style.borderR = 0;
    side->style.borderB = side->style.borderL = 0;

    // The page: its header, then each group as a GroupBox. An empty filter
    // keeps the selection but does not render a stale page.
    El* pane = Div(a)->FlexCol()->SizeFull()->ClipY();
    // container_query: the page is laid out stacked when the panel it is in
    // is at most STACKED_LAYOUT_MAX_WIDTH wide. GPUI builds the page after
    // the panel has its size. This builds it with the width the panel had
    // last frame, and the panel's prepaint builds it again, in the same
    // frame, when layout put the width on the other side of the line.
    Axis pageLayout = Axis::Horizontal;
    if (st && st->containerWidth >= 0 &&
        st->containerWidth <= kStackedLayoutMaxWidth) {
        pageLayout = Axis::Vertical;
    }
    SettingsContainerQuery* query_ = ArenaNew<SettingsContainerQuery>(a);
    query_->state = state;
    query_->layout = pageLayout;
    pane->prePaint = &SettingsContainerPrePaint;
    pane->customUser = query_;
    if (selected >= 0 && selected < pages.len &&
        PageHasMatchingGroup(pages[selected], query)) {
        // The page's list state is its own and starts over whenever the page
        // or the groups the query leaves on it change; the group to scroll
        // to is the one a click deferred, or on such a change the selected
        // one.
        int scrollGroup = -1;
        if (st) {
            uint32_t queryKey = IdFoldName(0, query);
            bool changed =
                st->listPage != selected || st->listQuery != queryKey;
            if (changed) {
                st->listPage = selected;
                st->listQuery = queryKey;
                st->scrollY = 0;
            }
            scrollGroup = st->deferredScrollGroup >= 0
                              ? st->deferredScrollGroup
                              : (changed ? st->group : -1);
            st->deferredScrollGroup = -1;
        }
        query_->settings = this;
        query_->cx = *cx;
        query_->selected = selected;
        query_->query = query;
        query_->scrollGroup = scrollGroup;
        query_->fieldsLen = st ? st->fields.len : 0;
        SettingsBuildPage(cx, this, pane, selected, query, pageLayout,
                          scrollGroup);
    }

    // h_resizable(id): the sidebar's panel at its width, kept inside its
    // range, and the page's panel taking the rest.
    return component::Resizable::New(cx, id)
        ->W(kFill)
        ->H(h)
        ->Panel(side, sideWidth, sidebarMinWidth, sidebarMaxWidth)
        ->Grow(pane)
        ->IntoEl();
}

} // namespace component
} // namespace gpui
