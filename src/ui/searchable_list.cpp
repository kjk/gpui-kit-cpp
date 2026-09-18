#include "ui/searchable_list.h"
#include "base/actions.h"
#include "base/list.h"
#include "base/select.h"
#include "ui/select.h"
#include "ui/input.h"

namespace gpui {

namespace component {

static int SearchableFlatIndex(const SearchableListState* state,
                               IndexPath path);

SearchableListDelegate SearchableListDelegate::Items(
    const SearchableListItem* values, int count) {
    SearchableListDelegate out;
    out.items = values;
    out.nItems = count;
    return out;
}

SearchableListDelegate SearchableListDelegate::Groups(
    SearchableGroup* const* values, int count) {
    SearchableListDelegate out;
    out.groups = values;
    out.nGroups = count;
    return out;
}

int SearchableListDelegate::SectionsCount(const App* app) const {
    if (sectionsCount) {
        return sectionsCount(user, app);
    }
    return groups ? nGroups : 1;
}

Str SearchableListDelegate::SectionTitle(int section) const {
    if (sectionTitle) {
        return sectionTitle(user, section);
    }
    return groups && section >= 0 && section < nGroups && groups[section]
               ? groups[section]->title
               : Str{};
}

int SearchableListDelegate::ItemsCount(int section) const {
    if (itemsCount) {
        return itemsCount(user, section);
    }
    if (!groups) {
        return section == 0 ? nItems : 0;
    }
    return section >= 0 && section < nGroups && groups[section]
               ? groups[section]->items.len
               : 0;
}

const SearchableListItem* SearchableListDelegate::Item(IndexPath path) const {
    if (item) {
        return item(user, path);
    }
    if (!groups) {
        return path.section == 0 && path.row >= 0 && path.row < nItems
                   ? &items[path.row]
                   : nullptr;
    }
    if (path.section < 0 || path.section >= nGroups || !groups[path.section]) {
        return nullptr;
    }
    const SearchableGroup* group = groups[path.section];
    return path.row >= 0 && path.row < group->items.len
               ? &group->items[path.row]
               : nullptr;
}

bool SearchableListDelegate::Position(Str value, IndexPath* out) const {
    if (position) {
        return position(user, value, out);
    }
    for (int section = 0; section < SectionsCount(); section++) {
        for (int row = 0; row < ItemsCount(section); row++) {
            const SearchableListItem* foundItem =
                Item(IndexPathNew(row).Section(section));
            if (foundItem && base::StrEq(foundItem->value, value)) {
                if (out) {
                    *out = IndexPathNew(row).Section(section);
                }
                return true;
            }
        }
    }
    return false;
}

bool SearchableListDelegate::Matches(const SearchableListItem* value,
                                     Str query) const {
    return matches ? matches(user, value, query)
                   : SearchableItemMatches(value, query);
}

El* SearchableListDelegate::RenderItem(Ctx* cx, IndexPath path,
                                       const SearchableListItem* value,
                                       bool checked) const {
    return renderItem ? renderItem(user, cx, path, value, checked) : nullptr;
}

El* SearchableListDelegate::RenderSectionHeader(Ctx* cx, int section) const {
    return renderSectionHeader ? renderSectionHeader(user, cx, section)
                               : nullptr;
}

bool SearchableListDelegate::IsItemEnabled(IndexPath path,
                                           const SearchableListItem* value,
                                           const App* app) const {
    return isItemEnabled ? isItemEnabled(user, path, value, app)
                         : value && !value->disabled && !value->pinned;
}

bool SearchableListDelegate::IsItemChecked(IndexPath path,
                                           const SearchableListItem* value,
                                           const SearchableListState* state,
                                           const App* app) const {
    if (isItemChecked) {
        return isItemChecked(user, path, value, state, app);
    }
    return value && state &&
           SearchableListIsChecked(state, state->items, state->nItems,
                                   SearchableFlatIndex(state, path));
}

void SearchableListDelegate::OnWillChange(SearchableListState* state,
                                          const SearchableListChange* changes,
                                          int n) const {
    if (onWillChange) {
        onWillChange(user, state, changes, n);
        return;
    }
    SearchableListApply(state, state->items, state->nItems, changes, n);
}

void SearchableListDelegate::OnConfirm(const SearchableListState* state,
                                       IndexPath path, bool secondary) const {
    if (onConfirm) {
        onConfirm(user, state, path, secondary);
    }
}

SearchableGroup* SearchableGroup::New(Str value) {
    SearchableGroup* out = new SearchableGroup();
    out->title = value;
    return out;
}

SearchableGroup* SearchableGroup::Item(const SearchableListItem& value) {
    VecAppend(items, value);
    return this;
}

SearchableGroup* SearchableGroup::Items(const SearchableListItem* values,
                                        int count) {
    for (int i = 0; i < count; i++) {
        VecAppend(items, values[i]);
    }
    return this;
}

bool SearchableGroup::Matches(Str query) const {
    if (StrContainsI(title, query)) {
        return true;
    }
    for (int i = 0; i < items.len; i++) {
        if (SearchableItemMatches(&items[i], query)) {
            return true;
        }
    }
    return false;
}

SearchableVec* SearchableVec::New(const SearchableListItem* values, int count) {
    SearchableVec* out = new SearchableVec();
    for (int i = 0; i < count; i++) {
        VecAppend(out->items, values[i]);
        VecAppend(out->matchedItems, values[i]);
    }
    return out;
}

SearchableVec* SearchableVec::Push(const SearchableListItem& value) {
    VecAppend(items, value);
    VecAppend(matchedItems, value);
    return this;
}

void SearchableVec::PerformSearch(Str query) {
    VecClear(matchedItems);
    for (int i = 0; i < items.len; i++) {
        if (SearchableItemMatches(&items[i], query)) {
            VecAppend(matchedItems, items[i]);
        }
    }
}

int SearchableVec::ItemsCount(int section) const {
    return section == 0 ? matchedItems.len : 0;
}

const SearchableListItem* SearchableVec::Item(IndexPath path) const {
    return path.section == 0 && path.row >= 0 && path.row < matchedItems.len
               ? &matchedItems[path.row]
               : nullptr;
}

bool SearchableVec::Position(Str value, IndexPath* out) const {
    for (int i = 0; i < matchedItems.len; i++) {
        if (base::StrEq(matchedItems[i].value, value)) {
            if (out) {
                *out = IndexPathNew(i);
            }
            return true;
        }
    }
    return false;
}

SearchableListItemElement* SearchableListItemElement::New(Ctx* cx,
                                                          size_t index) {
    SearchableListItemElement* out = ArenaNew<SearchableListItemElement>(cx->a);
    out->cx = cx;
    out->index = index;
    return out;
}

SearchableListItemElement* SearchableListItemElement::Checked(bool value) {
    checked = value;
    return this;
}

SearchableListItemElement* SearchableListItemElement::CheckIcon(
    IconName value) {
    checkIcon = value;
    return this;
}

SearchableListItemElement* SearchableListItemElement::Disabled(bool value) {
    disabled = value;
    return this;
}

SearchableListItemElement* SearchableListItemElement::Selected(bool value) {
    selected = value;
    return this;
}

bool SearchableListItemElement::IsSelected() const {
    return selected;
}

SearchableListItemElement* SearchableListItemElement::WithSize(UiSize value) {
    size = value;
    return this;
}

SearchableListItemElement* SearchableListItemElement::Child(El* child) {
    if (child) {
        children.Append(cx->a, child);
    }
    return this;
}

SearchableListItemElement* SearchableListItemElement::Refine(const Style& value,
                                                             uint32_t fields) {
    style = value;
    styleSet = fields;
    return this;
}

El* SearchableListItemElement::IntoEl() {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    El* row =
        Div(a)
            ->PathId(StrDup(a, fmt("searchable-list-item-%d", (int)index)))
            ->FlexRow()
            ->Gap(4)
            ->PadY(4)
            ->PadX(8)
            ->Radius(th.radius)
            ->Fg(disabled ? th.mutedFg : th.foreground)
            ->ItemsCenter()
            ->JustifyBetween();
    UiListSize(row, size);
    if (!disabled && !selected) {
        row->HoverBg(BackgroundOpacity(th.tokens.accent, 0.7f));
    }
    if (selected) {
        row->Bg(th.tokens.accent);
    }
    if (styleSet) {
        row->Refine(style, styleSet);
    }
    El* left = Div(a)->FlexRow()->W(kFill)->Gap(4)->ItemsCenter();
    for (int i = 0; i < children.len; i++) {
        left->Child(children[i]);
    }
    El* inner = Div(a)
                    ->FlexRow()
                    ->W(kFill)
                    ->Gap(4)
                    ->ItemsCenter()
                    ->JustifyBetween()
                    ->Child(left);
    El* check = IconEl(a, checkIcon, UiIconPx(UiSize::XSmall))
                    ->Fg(th.foreground);
    if (!checked) {
        check->Opacity(0);
    }
    inner->Child(check);
    row->Child(inner);
    return row;
}

static int SearchableFlatIndex(const SearchableListState* s, IndexPath path) {
    if (!s || !s->items || path.section < 0 || path.row < 0) {
        return -1;
    }
    int row = 0;
    for (int i = 0; i < s->nItems; i++) {
        if (s->items[i].section != path.section) {
            continue;
        }
        if (row == path.row) {
            return i;
        }
        row++;
    }
    return -1;
}

static IndexPath SearchablePath(const SearchableListState* s, int flat) {
    if (!s || !s->items || flat < 0 || flat >= s->nItems) {
        return IndexPathNew(-1);
    }
    int section = s->items[flat].section;
    int row = 0;
    for (int i = 0; i < flat; i++) {
        if (s->items[i].section == section) {
            row++;
        }
    }
    return IndexPathNew(row).Section(section);
}

void SearchableListState::SelectedValues(Vec<Str>* out) const {
    if (!out) {
        return;
    }
    VecClear(*out);
    for (int i = 0; i < selected.len; i++) {
        int ix = selected[i];
        if (items && ix >= 0 && ix < nItems) {
            VecAppend(*out, items[ix].value);
        }
    }
}

bool SearchableListState::AddSelectedIndex(IndexPath index) {
    int flat = SearchableFlatIndex(this, index);
    if (flat < 0) {
        return false;
    }
    for (int i = 0; i < selected.len; i++) {
        if (selected[i] == flat) {
            return false;
        }
    }
    VecAppend(selected, flat);
    return true;
}

bool SearchableListState::RemoveSelectedIndex(IndexPath index) {
    int flat = SearchableFlatIndex(this, index);
    for (int i = 0; i < selected.len; i++) {
        if (selected[i] != flat) {
            continue;
        }
        for (int j = i; j < selected.len - 1; j++) {
            selected[j] = selected[j + 1];
        }
        selected.len--;
        return true;
    }
    return false;
}

void SearchableListState::SetSelectedIndices(const IndexPath* indices, int n) {
    VecClear(selected);
    for (int i = 0; i < n; i++) {
        AddSelectedIndex(indices[i]);
    }
}

bool SearchableItemMatches(const SearchableItem* it, Str query) {
    if (len(query) <= 0) {
        return true;
    }
    return StrContainsI(it->title, query);
}

void SearchableListSearch(SearchableListState* s, const SearchableItem* items,
                          int nItems, Str query) {
    // The list keeps what it was given, so a click later on knows what it is
    // changing.
    s->items = items;
    s->nItems = nItems;
    VecClear(s->matches);
    for (int i = 0; i < nItems; i++) {
        if (s->hasDelegate ? s->delegate.Matches(&items[i], query)
                           : SearchableItemMatches(&items[i], query)) {
            VecAppend(s->matches, i);
        }
    }
    s->list.count = s->matches.len;
}

void SearchableListSelectOnly(SearchableListState* s, int index) {
    if (!s) {
        return;
    }
    VecClear(s->selected);
    if (index >= 0) {
        VecAppend(s->selected, index);
    }
}

static int SelectionIndexOfValue(const SearchableListState* s,
                                 const SearchableItem* items, int nItems,
                                 Str value) {
    for (int i = 0; i < s->selected.len; i++) {
        int ix = s->selected[i];
        if (ix >= 0 && ix < nItems && base::StrEq(items[ix].value, value)) {
            return i;
        }
    }
    return -1;
}

bool SearchableListIsChecked(const SearchableListState* s,
                             const SearchableItem* items, int nItems,
                             int index) {
    if (index < 0 || index >= nItems) {
        return false;
    }
    if (items[index].pinned) {
        return true;
    }
    if (s->hasDelegate && s->delegate.isItemChecked) {
        return s->delegate
            .IsItemChecked(SearchablePath(s, index), &items[index], s, nullptr);
    }
    return SelectionIndexOfValue(s, items, nItems, items[index].value) >= 0;
}

bool SearchableListIsEnabled(const SearchableListState* s,
                             const SearchableItem* items, int nItems,
                             int index) {
    if (index < 0 || index >= nItems) {
        return false;
    }
    if (s->hasDelegate && s->delegate.isItemEnabled &&
        !s->delegate
             .IsItemEnabled(SearchablePath(s, index), &items[index], nullptr)) {
        return false;
    }
    if (items[index].disabled || items[index].pinned) {
        return false;
    }
    if (s->maxSelected > 0 && s->selected.len >= s->maxSelected) {
        return SelectionIndexOfValue(s, items, nItems, items[index].value) >= 0;
    }
    return true;
}

void SearchableListChangesFor(const SearchableListState* s,
                              const SearchableItem* items, int nItems,
                              int index, Vec<SearchableListChange>* out) {
    VecClear(*out);
    if (s->mode == SearchableListMode::Single) {
        // The single-select strategy: everything that was selected comes out,
        // and the one that was clicked goes in.
        for (int i = 0; i < s->selected.len; i++) {
            VecAppend(*out,
                      {SearchableListChangeKind::Deselect, s->selected[i]});
        }
        VecAppend(*out, {SearchableListChangeKind::Select, index});
        return;
    }
    // Multi toggles the row that was clicked and leaves the rest alone. What
    // it is toggling is the item's value, which is what the check beside it
    // goes by.
    bool selected = SearchableListIsChecked(s, items, nItems, index);
    VecAppend(*out, {selected ? SearchableListChangeKind::Deselect
                              : SearchableListChangeKind::Select,
                     index});
}

static void SelectionRemoveAt(SearchableListState* s, int at) {
    for (int i = at; i < s->selected.len - 1; i++) {
        s->selected[i] = s->selected[i + 1];
    }
    s->selected.len--;
}

void SearchableListApply(SearchableListState* s, const SearchableItem* items,
                         int nItems, const SearchableListChange* changes,
                         int n) {
    for (int c = 0; c < n; c++) {
        const SearchableListChange& ch = changes[c];
        if (ch.index < 0 || ch.index >= nItems) {
            continue;
        }
        Str value = items[ch.index].value;
        int at = SelectionIndexOfValue(s, items, nItems, value);
        if (ch.kind == SearchableListChangeKind::Select) {
            // on_will_change: a Select that would take the selection past its
            // limit is dropped, rather than pushing something else out.
            if (s->maxSelected > 0 && s->selected.len >= s->maxSelected) {
                continue;
            }
            // A value already in the selection is not added twice.
            if (at < 0) {
                VecAppend(s->selected, ch.index);
            }
            continue;
        }
        if (at >= 0) {
            SelectionRemoveAt(s, at);
            continue;
        }
        // Nothing carried that value, so the index itself is what goes.
        for (int i = 0; i < s->selected.len; i++) {
            if (s->selected[i] == ch.index) {
                SelectionRemoveAt(s, i);
                break;
            }
        }
    }
}

bool SearchableListClick(SearchableListState* s, int index) {
    Vec<SearchableListChange> changes;
    SearchableListChangesFor(s, s->items, s->nItems, index, &changes);
    if (s->hasDelegate) {
        s->delegate.OnWillChange(s, changes.els, len(changes));
    } else {
        SearchableListApply(s, s->items, s->nItems, changes.els, len(changes));
    }
    VecReset(changes);
    return s->mode == SearchableListMode::Single && s->closeOnSelect;
}

void SearchableListState::OnRowClick(SearchableListState* self, Ctx* cx,
                                     const ClickEvent*, intptr_t match) {
    int m = (int)match;
    if (m < 0 || m >= self->matches.len) {
        return;
    }
    self->list.selected = m;
    int index = self->matches[m];
    // The changes the mode came to are applied here, since the list is what
    // holds both the selection and the items. What the caller hears is what
    // was picked, once it has been.
    if (SearchableListClick(self, index)) {
        self->open = false;
        if (self->previousFocus.IsValid() &&
            FocusHandleContainsFocused(cx->win, self->contentFocus)) {
            if (!FocusHandleRestore(cx->win, self->previousFocus)) {
                FocusHandleRestore(cx->win, self->triggerFocus);
            }
        }
        self->previousFocus = {};
    }
    ListEvent ev = {ListEventKind::Confirm, index, false};
    if (self->onChange.IsValid()) {
        ListenerCall(cx->app, cx->win, self->onChange, &ev);
    }
    if (self->hasDelegate && !self->suppressDelegateConfirm) {
        self->delegate.OnConfirm(self, SearchablePath(self, index), false);
    }
    Notify(cx);
}

SearchableList* SearchableList::New(Ctx* cx, Str id,
                                    Entity<SearchableListState> st,
                                    InputState* query) {
    Arena* a = cx->a;
    SearchableList* s = ArenaNew<SearchableList>(a);
    s->a = a;
    s->cx = cx;
    s->id = id;
    s->state = st;
    s->query = query;
    return s;
}
SearchableList* SearchableList::Footer(El* e) {
    footer = e;
    return this;
}
SearchableList* SearchableList::Items(const SearchableItem* it, int n) {
    items = it;
    nItems = n;
    return this;
}
SearchableList* SearchableList::Sections(const Str* titles, int n) {
    sections = titles;
    nSections = n;
    return this;
}
SearchableList* SearchableList::OnQueryFocus(Listener fn) {
    onQueryFocus = fn;
    return this;
}
SearchableList* SearchableList::Empty(El* e) {
    empty = e;
    return this;
}
SearchableList* SearchableList::W(float v) {
    w = v;
    return this;
}
SearchableList* SearchableList::MaxH(float v) {
    maxH = v;
    return this;
}
SearchableList* SearchableList::InSelect(bool v) {
    inSelect = v;
    return this;
}
SearchableList* SearchableList::CheckIcon(IconName n) {
    checkIcon = n;
    return this;
}
SearchableList* SearchableList::WithSize(UiSize value) {
    size = value;
    return this;
}
SearchableList* SearchableList::Delegate(const SearchableListDelegate& value) {
    delegate = value;
    hasDelegate = true;
    return this;
}

El* SearchableList::IntoEl() {
    const Theme& th = ThemeNow(cx->app);
    SearchableListState* s = state.Get(cx);
    El* box = Div(a)
                  ->Id(id)
                  ->Role(AccessibilityRole::ListBox)
                  ->FlexCol()
                  ->W(w)
                  ->Pad(4)
                  ->Gap(2)
                  ->Radius(th.radius)
                  ->Border(1, th.border)
                  ->Bg(th.tokens.background);
    if (!s) {
        return box;
    }
    if (hasDelegate) {
        int sectionCount = delegate.SectionsCount(cx->app);
        int total = 0;
        for (int section = 0; section < sectionCount; section++) {
            total += delegate.ItemsCount(section);
        }
        SearchableListItem* flat =
            total > 0
                ? (SearchableListItem*)Alloc(a, total * (int)sizeof(*flat))
                : nullptr;
        Str* titles = sectionCount > 0
                          ? (Str*)Alloc(a, sectionCount * (int)sizeof(*titles))
                          : nullptr;
        int at = 0;
        for (int section = 0; section < sectionCount; section++) {
            titles[section] = delegate.SectionTitle(section);
            int count = delegate.ItemsCount(section);
            for (int row = 0; row < count; row++) {
                const SearchableListItem* source =
                    delegate.Item(IndexPathNew(row).Section(section));
                if (!source) {
                    continue;
                }
                SearchableListItem copy = *source;
                copy.section = section;
                flat[at++] = copy;
            }
        }
        items = flat;
        nItems = at;
        sections = titles;
        nSections = sectionCount;
        s->delegate = delegate;
        s->hasDelegate = true;
    } else {
        s->hasDelegate = false;
    }
    // perform_search: the query decides which items there are to show at all.
    SearchableListSearch(s, items, nItems, query ? InputValue(query) : Str{});

    if (query) {
        El* row =
            Div(a)->FlexRow()->W(kFill)->H(32)->PadX(4)->Gap(8)->ItemsCenter();
        row->Child(IconEl(a, IconName::Search, 16)->Fg(th.mutedFg));
        row->Child(Div(a)->Flex1()->Child(Input::New(cx, StrL("query"), query)
                                              ->Appearance(false)
                                              ->OnFocus(onQueryFocus)
                                              ->IntoEl()));
        row->BorderB(1, th.border);
        box->Child(row);
    }

    if (s->matches.len == 0) {
        box->Child(empty
                       ? empty
                       : Div(a)
                             ->FlexCol()
                             ->W(kFill)
                             ->PadY(24)
                             ->ItemsCenter()
                             ->JustifyCenter()
                             ->Child(IconEl(a, IconName::Inbox, 32)
                                         ->Fg(RgbaOpacity(th.mutedFg, 0.6f))));
        return box;
    }

    El* rows = Div(a)->FlexCol()->W(kFill)->MaxH(maxH)->ClipY();
    Listener click = ListenTo(state, &SearchableListState::OnRowClick, 0);
    int lastSection = -1;
    for (int m = 0; m < s->matches.len; m++) {
        int ix = s->matches[m];
        const SearchableItem& it = items[ix];
        // render_section_header: a heading whenever the section changes, and
        // only for the sections the query left something in.
        if (sections && it.section != lastSection && it.section < nSections) {
            // render_section_header: py_0p5, px_2, text_sm and muted. The
            // list scrolls rather than squeezing, so neither a heading nor a
            // row is a flex item that can give height back.
            El* custom = hasDelegate
                             ? delegate.RenderSectionHeader(cx, it.section)
                             : nullptr;
            if (custom) {
                rows->Child(custom);
            } else if (sections[it.section].s) {
                rows->Child(
                    Div(a)->W(kFill)->Shrink0()->PadX(8)->PadY(2)->Child(
                        TextEl(a, sections[it.section])
                            ->Font(14)
                            ->Fg(th.mutedFg)));
            }
            lastSection = it.section;
        }
        bool checked = SearchableListIsChecked(s, items, nItems, ix);
        bool enabled = SearchableListIsEnabled(s, items, nItems, ix);
        IndexPath path = SearchablePath(s, ix);
        if (hasDelegate && delegate.isItemChecked) {
            checked = delegate.IsItemChecked(path, &it, s, cx->app);
        }
        if (hasDelegate && delegate.isItemEnabled) {
            enabled = enabled && delegate.IsItemEnabled(path, &it, cx->app);
        }
        // SearchableListItem::render: an icon before the label when the item
        // gave one.
        El* label = Div(a)->FlexRow()->Gap(8)->ItemsCenter()->MinW(0);
        if (it.icon != IconName::None) {
            label->Child(IconEl(a, it.icon, UiIconPx(UiSize::Small))
                             ->Fg(th.mutedFg));
        }
        label->Child(TextEl(a, it.title)
                         ->Fg(enabled || checked ? th.foreground : th.mutedFg));
        El* content = Div(a)
                          ->FlexRow()
                          ->W(kFill)
                          ->Gap(4)
                          ->ItemsCenter()
                          ->JustifyBetween();
        content->Child(label);
        // render_item's badge, as the story's delegate puts its "Featured"
        // pill beside the custom row content.
        if (it.badge.s) {
            content->Child(Div(a)
                               ->PadX(4)
                               ->Radius(th.radius * 0.5f)
                               ->Bg(th.tokens.primary)
                               ->Child(TextEl(a, it.badge)
                                           ->Font(12)
                                           ->Fg(th.primaryFg)
                                           ->LineHeight(1.4f)));
        }
        El* row =
            hasDelegate ? delegate.RenderItem(cx, path, &it, checked) : nullptr;
        if (!row) {
            row = SearchableListItemElement::New(cx, (size_t)m)
                      ->Checked(checked)
                      ->CheckIcon(checkIcon)
                      ->Disabled(!enabled)
                      ->Selected(m == s->list.selected)
                      ->WithSize(size)
                      ->Child(content)
                      ->IntoEl();
        }
        row->Role(AccessibilityRole::ListBoxOption)
            ->AriaLabel(it.title)
            ->AriaSelected(checked)
            ->AriaPositionInSet(m + 1)
            ->AriaSizeOfSet(s->matches.len)
            ->AriaDisabled(!enabled)
            ->Shrink0();
        if (enabled) {
            BindPathClick(row, StrDup(a, fmt("row-%d", ix)),
                          ListenerArg(click, m));
        }
        rows->Child(row);
    }
    box->Child(rows);
    if (footer) {
        // Combobox::footer: an action under the list, ruled off from it.
        box->Child(
            Div(a)->W(kFill)->BorderT(1, th.border)->Pad(4)->Child(footer));
    }
    // The list's own key context, for one that is not inside a select. The
    // rows are focusable, and so is the box, so a chord finds it whether a
    // row was clicked or the list was tabbed to.
    if (!inSelect) {
        ListInitKeys();
        Listener onAction = ListenTo(state, &SearchableListState::OnListAction);
        box->FocusId(HashClickId(id))
            ->FocusRing(false)
            ->FocusOnPress()
            ->KeyContext(ListContext())
            ->OnAction(action::Cancel(), onAction)
            ->OnAction(action::Confirm(), onAction)
            ->OnAction(action::SelectUp(), onAction)
            ->OnAction(action::SelectDown(), onAction);
    } else if (s && s->contentFocus.IsValid()) {
        // `content_focus_handle` is the *list's* handle upstream —
        // `state.list.focus_handle(cx)` — tracked on the list's own element,
        // so the focus a select moves into its dropdown lands on something
        // the frame can name. It named nothing here: the select set
        // `win->focusId` to a handle no element carried, and only the id
        // comparison in WindowFocusWithin made closing look like it worked.
        // With the handle on the box, focus that has gone on into the query
        // field is still the list's, so closing puts it back where it was
        // rather than leaving it on an input that has gone.
        //
        // Not a tab stop: `track_focus` alone only makes an element
        // focusable, and upstream asks for `.tab_stop(true)` on the trigger
        // and nowhere else — so Tab still walks past an open dropdown rather
        // than into it.
        box->TrackFocus(s->contentFocus)->TabStop(false)->FocusRing(false);
    }
    return box;
}

void SearchableListState::OnAction(SearchableListState* self, Ctx* cx,
                                   const ActionEvent* ev) {
    if (!self) {
        return;
    }
    // Once it is open the root has nothing left to do with an arrow, so the
    // list takes it — which is what Rust's content focus handle is for.
    if (self->open && (ev->action == action::SelectUp() ||
                       ev->action == action::SelectDown())) {
        ListPerform(&self->list, cx,
                    ev->action == action::SelectDown() ? ListAction::SelectNext
                                                       : ListAction::SelectPrev,
                    false);
        return;
    }
    switch (SelectActionOf(ev->action, self->open, false)) {
        case SelectAction::Open:
        case SelectAction::Dismiss:
            SelectToggleOpen(self, cx);
            return;
        case SelectAction::Confirm:
            if (self->list.selected >= 0 && self->list.selected < self->matches
                                                                      .len) {
                // The same thing a click on the highlighted row does, down to
                // what the caller hears and whether the list closes behind
                // it — which is close_on_select's to decide, not the key's.
                OnRowClick(self, cx, nullptr, self->list.selected);
                // cx.stop_propagation(): the Enter was the select's, so it
                // must not also reach the focused trigger and reopen what it
                // just closed.
                if (cx->win) {
                    cx->win->eatReturn = true;
                }
            }
            return;
        case SelectAction::None:
            break;
    }
    // Not the select's — a closed one and escape, which is Rust propagating
    // so whatever encloses it can use the key.
    const_cast<ActionEvent*>(ev)->propagate = true;
}

// `.key_context(CONTEXT)` and the handlers under it. A disabled select never
// declares it, which is Rust's every-handler-propagates-when-disabled.
void SelectBindKeys(Ctx* cx, El* root, Entity<SearchableListState> state) {
    if (!cx || !root || !state.IsValid()) {
        return;
    }
    SelectInitKeys();
    Listener onAction = ListenTo(state, &SearchableListState::OnAction);
    root->KeyContext(SelectContext())
        ->OnAction(action::SelectUp(), onAction)
        ->OnAction(action::SelectDown(), onAction)
        ->OnAction(action::Confirm(), onAction)
        ->OnAction(action::Cancel(), onAction);
}

void SearchableListState::OnListAction(SearchableListState* self, Ctx* cx,
                                       const ActionEvent* ev) {
    if (!self) {
        return;
    }
    ListKeyAction act = ListActionOf(ev->action, ev->arg);
    if (act.action == ListAction::None) {
        const_cast<ActionEvent*>(ev)->propagate = true;
        return;
    }
    if (act.action == ListAction::Confirm) {
        if (self->list.selected >= 0 && self->list.selected < self->matches
                                                                  .len) {
            OnRowClick(self, cx, nullptr, self->list.selected);
        }
        return;
    }
    ListPerform(&self->list, cx, act.action, act.secondary);
    Notify(cx);
}

} // namespace component
} // namespace gpui
