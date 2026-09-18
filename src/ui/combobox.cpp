#include "ui/i18n.h"
#include "ui/combobox.h"

namespace gpui {

namespace component {

static bool SameSelection(const Vec<int>& a, const Vec<int>& b) {
    if (len(a) != len(b)) {
        return false;
    }
    for (int i = 0; i < len(a); i++) {
        if (a[i] != b[i]) {
            return false;
        }
    }
    return true;
}

int ComboboxTriggerContext::SelectionCount() const {
    return state ? state->selected.len : 0;
}

int ComboboxTriggerContext::SelectionIndex(int at) const {
    return state && at >= 0 && at < state->selected.len ? state->selected[at]
                                                        : -1;
}

const SearchableListItem* ComboboxTriggerContext::SelectionItem(int at) const {
    int ix = SelectionIndex(at);
    return state && state->items && ix >= 0 && ix < state->nItems
               ? &state->items[ix]
               : nullptr;
}

Str ComboboxTriggerContext::Placeholder() const {
    return hasPlaceholder ? placeholder : Str{};
}

Entity<SearchableListState> ComboboxListEntity(Entity<ComboboxState> state) {
    Entity<SearchableListState> out;
    out.id = state.id;
    return out;
}

Entity<ComboboxState> ComboboxState::New(App* app) {
    Entity<ComboboxState> out = EntityNewState<ComboboxState>(app);
    ComboboxState* self = out.Get(app);
    if (self) {
        self->self = out;
        self->state.onChange = ListenTo(out, &ComboboxState::OnListChange);
    }
    return out;
}

ComboboxState* ComboboxState::Multiple(bool value) {
    multiple = value;
    state.mode = value ? SearchableListMode::Multi : SearchableListMode::Single;
    state.closeOnSelect = !value;
    return this;
}

ComboboxState* ComboboxState::Searchable(bool value) {
    searchable = value;
    return this;
}

void ComboboxState::SetItems(const SearchableListItem* items, int nItems) {
    SearchableListSearch(&state, items, nItems, Str{});
}

void ComboboxState::SetDelegate(const SearchableListDelegate& value) {
    state.delegate = value;
    state.hasDelegate = true;
}

void ComboboxState::SelectedValues(Vec<Str>* out) const {
    state.SelectedValues(out);
}

Str ComboboxState::SelectedValue() const {
    if (!state.items || state.selected.len == 0) {
        return {};
    }
    int ix = state.selected[0];
    return ix >= 0 && ix < state.nItems ? state.items[ix].value : Str{};
}

void ComboboxState::SyncSnapshot() {
    VecClear(selectionSnapshot);
    for (int i = 0; i < state.selected.len; i++) {
        VecAppend(selectionSnapshot, state.selected[i]);
    }
}

void ComboboxState::SetSelectedValues(const Str* values, int nValues, Ctx* cx) {
    InputSetValue(&queryInput, Str{});
    SearchableListSearch(&state, state.items, state.nItems, Str{});
    VecClear(state.selected);
    for (int v = 0; v < nValues; v++) {
        int found = -1;
        IndexPath path;
        if (state.hasDelegate && state.delegate.Position(values[v], &path)) {
            int row = 0;
            for (int i = 0; i < state.nItems; i++) {
                if (state.items[i].section != path.section) {
                    continue;
                }
                if (row++ == path.row) {
                    found = i;
                    break;
                }
            }
        } else {
            for (int i = 0; i < state.nItems; i++) {
                if (base::StrEq(state.items[i].value, values[v])) {
                    found = i;
                    break;
                }
            }
        }
        if (found < 0) {
            continue;
        }
        bool seen = false;
        for (int i = 0; i < state.selected.len; i++) {
            seen = seen || state.selected[i] == found;
        }
        if (!seen) {
            VecAppend(state.selected, found);
        }
    }
    SyncSnapshot();
    if (cx) {
        Notify(cx);
    }
}

void ComboboxState::SetSelectedIndices(const IndexPath* indices, int n,
                                       Ctx* cx) {
    state.SetSelectedIndices(indices, n);
    SyncSnapshot();
    if (cx) {
        Notify(cx);
    }
}

bool ComboboxState::AddSelectedIndex(IndexPath index, Ctx* cx) {
    bool added = state.AddSelectedIndex(index);
    if (added) {
        SyncSnapshot();
        if (cx) {
            Notify(cx);
        }
    }
    return added;
}

bool ComboboxState::RemoveSelectedIndex(IndexPath index, Ctx* cx) {
    bool removed = state.RemoveSelectedIndex(index);
    if (removed) {
        SyncSnapshot();
        if (cx) {
            Notify(cx);
        }
    }
    return removed;
}

void ComboboxState::Emit(Ctx* cx, ComboboxEventKind kind) {
    if (!cx || !cx->app) {
        return;
    }
    Vec<Str> values;
    SelectedValues(&values);
    ComboboxEvent event = {kind, values.els, len(values)};
    EntityEmit(cx->app, cx->win, self, &event);
    VecReset(values);
}

void ComboboxState::ClearSelection(Ctx* cx) {
    VecClear(state.selected);
    SyncSnapshot();
    Emit(cx, ComboboxEventKind::Change);
    if (cx) {
        Notify(cx);
    }
}

void ComboboxState::Focus(Window* win) const {
    if (win && state.triggerFocus.IsValid()) {
        FocusHandleFocus(win, state.triggerFocus);
    }
}

const FocusHandle* ComboboxState::FocusHandleNow() const {
    return state.open ? &state.contentFocus : &state.triggerFocus;
}

void ComboboxState::SetQuery(Str query, Ctx* cx) {
    InputSetValue(&queryInput, query);
    SearchableListSearch(&state, state.items, state.nItems, query);
    if (cx) {
        Notify(cx);
    }
}

void ComboboxState::SetOpen(bool open, Ctx* cx) {
    if (state.open == open) {
        if (!open && queryInput.focused && cx) {
            InputBlur(&queryInput, cx->app, cx->win);
        }
        return;
    }
    SelectToggleOpen(&state, cx);
    if (open && searchable && cx) {
        InputFocus(&queryInput, cx->app, cx->win);
    } else if (!open && queryInput.focused && cx) {
        InputBlur(&queryInput, cx->app, cx->win);
    }
}

void ComboboxState::OnListChange(ComboboxState* self, Ctx* cx,
                                 const ListEvent* event) {
    if (!self || !event || event->kind != ListEventKind::Confirm) {
        return;
    }
    bool changed =
        !SameSelection(self->selectionSnapshot, self->state.selected);
    self->SyncSnapshot();
    if (changed) {
        self->Emit(cx, ComboboxEventKind::Change);
    }
    if (changed && !self->multiple) {
        if (self->state.hasDelegate) {
            int flat = event->index;
            int section = flat >= 0 && flat < self->state.nItems
                              ? self->state.items[flat].section
                              : 0;
            int row = 0;
            for (int i = 0; i < flat; i++) {
                row += self->state.items[i].section == section ? 1 : 0;
            }
            self->state.delegate.OnConfirm(
                &self->state, IndexPathNew(row).Section(section), false);
        }
        self->Emit(cx, ComboboxEventKind::Confirm);
        self->SetOpen(false, cx);
        self->Focus(cx->win);
    } else if (!changed && !self->multiple && !self->state.open) {
        // SearchableList's standalone single-select path closes eagerly.
        // Combobox only closes after the delegate actually changes the
        // selection, so restore the open list after a veto or same-value pick.
        self->state.open = true;
        FocusHandleFocus(cx->win, self->state.contentFocus);
        Notify(cx);
    }
}

void ComboboxState::OnToggle(ComboboxState* self, Ctx* cx, const ClickEvent*) {
    if (!self) {
        return;
    }
    bool wasOpen = self->state.open;
    self->SetOpen(!wasOpen, cx);
    if (wasOpen) {
        self->Emit(cx, ComboboxEventKind::Confirm);
        self->Focus(cx->win);
    }
}

void ComboboxState::OnClear(ComboboxState* self, Ctx* cx, const ClickEvent*) {
    if (self) {
        self->ClearSelection(cx);
    }
}

void ComboboxState::OnMouseDownOut(ComboboxState* self, Ctx* cx,
                                   const MouseDownEvent* event) {
    if (!self || !self->state.open ||
        (event && self->bounds.Contains({event->x, event->y}))) {
        return;
    }
    self->Emit(cx, ComboboxEventKind::Confirm);
    self->SetOpen(false, cx);
    self->Focus(cx->win);
}

Combobox* Combobox::New(Ctx* cx, Str id, Entity<SearchableListState> state,
                        InputState* query) {
    Arena* a = cx->a;
    Combobox* c = ArenaNew<Combobox>(a);
    c->a = a;
    c->cx = cx;
    c->id = id;
    c->state = state;
    c->query = query;
    return c;
}
Combobox* Combobox::New(Ctx* cx, Str id, Entity<ComboboxState> state) {
    ComboboxState* owner = state.Get(cx);
    Combobox* out = Combobox::New(cx, id, ComboboxListEntity(state), nullptr);
    out->comboboxState = state;
    if (owner) {
        owner->self = state;
        owner->searchable = true;
        owner->state.onChange = ListenTo(state, &ComboboxState::OnListChange);
    }
    return out;
}
Combobox* Combobox::Items(const SearchableItem* it, int n) {
    items = it;
    nItems = n;
    return this;
}
Combobox* Combobox::Sections(const Str* titles, int n) {
    sections = titles;
    nSections = n;
    return this;
}
Combobox* Combobox::Placeholder(Str s) {
    placeholder = s;
    return this;
}
Combobox* Combobox::SearchPlaceholder(Str s) {
    searchPlaceholder = s;
    return this;
}
Combobox* Combobox::Empty(Str s) {
    empty = s;
    return this;
}
Combobox* Combobox::Icon(IconName n) {
    icon = n;
    return this;
}
Combobox* Combobox::CheckIcon(IconName n) {
    checkIcon = n;
    return this;
}
Combobox* Combobox::W(float v) {
    width = v;
    return this;
}
Combobox* Combobox::MenuWidth(float v) {
    menuWidth = v;
    return this;
}
Combobox* Combobox::MenuMaxH(float v) {
    menuMaxH = v;
    return this;
}
Combobox* Combobox::WithSize(UiSize value) {
    size = value;
    return this;
}
Combobox* Combobox::Disabled(bool v) {
    disabled = v;
    return this;
}
Combobox* Combobox::Cleanable(bool v) {
    cleanable = v;
    return this;
}
Combobox* Combobox::Appearance(bool v) {
    appearance = v;
    return this;
}
Combobox* Combobox::FocusRing(bool v) {
    focusRing = v;
    return this;
}
Combobox* Combobox::Multiple(bool v) {
    if (ComboboxState* owner = comboboxState.Get(cx)) {
        owner->Multiple(v);
        return this;
    }
    SearchableListState* s = state.Get(cx);
    if (s) {
        s->mode = v ? SearchableListMode::Multi : SearchableListMode::Single;
        s->closeOnSelect = !v;
    }
    return this;
}
Combobox* Combobox::OnQueryFocus(Listener fn) {
    onQueryFocus = fn;
    return this;
}
Combobox* Combobox::OnToggle(Listener fn) {
    onToggle = fn;
    return this;
}
Combobox* Combobox::OnClear(Listener fn) {
    onClear = fn;
    return this;
}

Combobox* Combobox::Trigger(El* e) {
    trigger = e;
    return this;
}
Combobox* Combobox::RenderTrigger(
    void* value, El* (*fn)(Ctx* cx, void* data,
                           const ComboboxTriggerContext* triggerContext)) {
    triggerData = value;
    renderTrigger = fn;
    return this;
}
Combobox* Combobox::Footer(El* e) {
    footer = e;
    return this;
}
Combobox* Combobox::RenderFooter(void* value, El* (*fn)(Ctx* cx, void* data)) {
    footerData = value;
    renderFooter = fn;
    return this;
}
Combobox* Combobox::RenderEmpty(void* value, El* (*fn)(Ctx* cx, void* data)) {
    emptyData = value;
    renderEmpty = fn;
    return this;
}
Combobox* Combobox::Delegate(const SearchableListDelegate& value) {
    delegate = value;
    hasDelegate = true;
    return this;
}
Combobox* Combobox::Refine(const Style& value, uint32_t fields) {
    style = value;
    styleSet = fields;
    return this;
}
Combobox* Combobox::MaxSelected(int n) {
    if (SearchableListState* s = state.Get(cx)) {
        s->maxSelected = n;
    }
    return this;
}

El* Combobox::IntoEl() {
    // A ComboBox is a Select that is always searchable, so it is one: the
    // trigger, the list and the popup are all the Select's, and the query
    // field is what tells them apart.
    // t!("ComboBox.search_placeholder") where the caller named none, which
    // is where Rust's own default comes from too.
    ComboboxState* owner = comboboxState.Get(cx);
    if (owner) {
        query = owner->searchable ? &owner->queryInput : nullptr;
    }
    if (query) {
        InputSetPlaceholder(query, searchPlaceholder.s
                                       ? searchPlaceholder
                                       : Tr("ComboBox.search_placeholder"));
    }
    if (owner) {
        owner->SetItems(items, nItems);
        owner->Multiple(owner->multiple);
        owner->triggerIcon = icon;
        owner->checkIcon = checkIcon;
        owner->focusRingEnabled = focusRing;
        owner->state.suppressDelegateConfirm = true;
        if (hasDelegate) {
            owner->SetDelegate(delegate);
        }
        if (renderTrigger) {
            ComboboxTriggerContext triggerContext = {
                &owner->state,     placeholder, placeholder.s != nullptr,
                owner->state.open, disabled,    size};
            trigger = renderTrigger(cx, triggerData, &triggerContext);
        }
        if (renderFooter) {
            footer = renderFooter(cx, footerData);
        }
    }
    Listener toggle = onToggle;
    Listener clear = onClear;
    if (owner) {
        if (!toggle.IsValid()) {
            toggle = ListenTo(comboboxState, &ComboboxState::OnToggle);
        }
        if (!clear.IsValid()) {
            clear = ListenTo(comboboxState, &ComboboxState::OnClear);
        }
    }
    Select* sel = Select::New(cx, id, state)
                      ->Items(items, nItems)
                      ->Placeholder(placeholder)
                      ->Empty(empty.s ? empty : Tr("ComboBox.empty"))
                      ->W(width)
                      ->MenuWidth(menuWidth)
                      ->WithSize(size)
                      ->Icon(icon)
                      ->CheckIcon(checkIcon)
                      ->Disabled(disabled)
                      ->Cleanable(cleanable)
                      ->Appearance(appearance)
                      ->FocusRing(focusRing)
                      ->Searchable(query, onQueryFocus)
                      ->OnToggle(toggle)
                      ->OnClear(clear);
    if (styleSet) {
        sel->TriggerRefine(style, styleSet);
    }
    if (renderEmpty) {
        sel->Empty(renderEmpty(cx, emptyData));
    }
    if (owner) {
        sel->OnMouseDownOut(
               ListenTo(comboboxState, &ComboboxState::OnMouseDownOut))
            ->TriggerBoundsOut(&owner->bounds);
    }
    if (hasDelegate) {
        sel->Delegate(delegate);
    }
    if (sections) {
        sel->Sections(sections, nSections);
    }
    if (menuMaxH > 0) {
        sel->MenuMaxH(menuMaxH);
    }
    if (trigger) {
        sel->Trigger(trigger);
    }
    if (footer) {
        sel->Footer(footer);
    }
    return sel->IntoEl();
}

} // namespace component
} // namespace gpui
