#include "ui/i18n.h"
#include "ui/select.h"
#include "ui/button.h"

namespace gpui {

namespace component {

Caret Caret::New(UiSize size) {
    Caret out;
    out.size = size;
    return out;
}

Caret Caret::TextColor(Rgba value) const {
    Caret out = *this;
    out.color = value;
    out.hasColor = true;
    return out;
}

float Caret::IconSize() const {
    if (size == UiSize::XSmall) {
        return 12;
    }
    if (size == UiSize::Small) {
        return 14;
    }
    return 16;
}

El* Caret::IntoEl(Arena* a) const {
    El* out = IconEl(a, IconName::ChevronDown, IconSize());
    if (hasColor) {
        out->Fg(color);
    }
    return out;
}

static int SelectFlatIndex(const SearchableListState* s, IndexPath path) {
    if (!s || !s->items || path.row < 0 || path.section < 0) {
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

static IndexPath SelectPath(const SearchableListState* s, int flat) {
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

Entity<SearchableListState> SelectListEntity(Entity<SelectState> state) {
    Entity<SearchableListState> out;
    out.id = state.id;
    return out;
}

Entity<SelectState> SelectState::New(App* app) {
    Entity<SelectState> out = EntityNewState<SelectState>(app);
    SelectState* self = out.Get(app);
    if (self) {
        self->self = out;
        self->activeQuery = &self->queryInput;
        self->state.onChange = ListenTo(out, &SelectState::OnListChange);
    }
    return out;
}

void SelectState::Searchable(bool value) {
    searchable = value;
}

void SelectState::SetItems(const SearchableItem* items, int nItems) {
    SearchableListSearch(&state, items, nItems, Str{});
}

void SelectState::SetSelectedIndex(const IndexPath* selected, Ctx* cx) {
    SetSelectedIndex(selected ? SelectFlatIndex(&state, *selected) : -1, cx);
}

void SelectState::SetSelectedIndex(int flatIndex, Ctx* cx) {
    if (flatIndex < 0 || flatIndex >= state.nItems) {
        flatIndex = -1;
    }
    SearchableListSelectOnly(&state, flatIndex);
    state.list.selected = -1;
    for (int i = 0; i < state.matches.len; i++) {
        if (state.matches[i] == flatIndex) {
            state.list.selected = i;
            break;
        }
    }
    if (cx) {
        Notify(cx);
    }
}

void SelectState::SetSelectedValue(Str value, Ctx* cx) {
    // Rust clears the active search before asking the delegate for a value's
    // full-list position. The query text lives in InputState here, while the
    // state owns the same full match snapshot.
    if (activeQuery) {
        InputSetValue(activeQuery, Str{});
    }
    SearchableListSearch(&state, state.items, state.nItems, Str{});
    int found = -1;
    for (int i = 0; i < state.nItems; i++) {
        if (base::StrEq(state.items[i].value, value)) {
            found = i;
            break;
        }
    }
    SetSelectedIndex(found, cx);
}

bool SelectState::SelectedIndex(IndexPath* out) const {
    if (state.selected.len <= 0) {
        return false;
    }
    IndexPath path = SelectPath(&state, state.selected[0]);
    if (path.row < 0) {
        return false;
    }
    if (out) {
        *out = path;
    }
    return true;
}

Str SelectState::SelectedValue() const {
    if (state.selected.len <= 0 || !state.items) {
        return {};
    }
    int ix = state.selected[0];
    return ix >= 0 && ix < state.nItems ? state.items[ix].value : Str{};
}

void SelectState::Focus(Window* win) const {
    if (win && state.triggerFocus.IsValid()) {
        FocusHandleFocus(win, state.triggerFocus);
    }
}

void SelectState::SetOpen(bool open, Ctx* cx) {
    if (state.open == open) {
        return;
    }
    SelectToggleOpen(&state, cx);
}

void SelectState::ToggleMenu(Ctx* cx) {
    SelectToggleOpen(&state, cx);
}

void SelectState::Clean(Ctx* cx) {
    SetSelectedIndex(-1, cx);
    SelectEvent ev;
    EntityEmit(cx->app, cx->win, self, &ev);
}

void SelectState::OnListChange(SelectState* self, Ctx* cx,
                               const ListEvent* event) {
    if (!self || !event || event->kind != ListEventKind::Confirm) {
        return;
    }
    SelectEvent ev;
    int ix = event->index;
    if (ix >= 0 && ix < self->state.nItems) {
        ev.hasValue = true;
        ev.index = SelectPath(&self->state, ix);
        ev.value = self->state.items[ix].value;
    }
    self->Focus(cx->win);
    EntityEmit(cx->app, cx->win, self->self, &ev);
}

void SelectState::OnMouseDownOut(SelectState* self, Ctx* cx,
                                 const MouseDownEvent*) {
    if (self && self->state.open) {
        self->SetOpen(false, cx);
        self->Focus(cx->win);
    }
}

Select* Select::New(Ctx* cx, Str id, Entity<SearchableListState> state) {
    Arena* a = cx->a;
    Select* s = ArenaNew<Select>(a);
    s->a = a;
    s->cx = cx;
    s->id = id;
    s->state = state;
    return s;
}

Select* Select::New(Ctx* cx, Str id, Entity<SelectState> state) {
    Select* out = Select::New(cx, id, SelectListEntity(state));
    out->selectState = state;
    SelectState* self = state.Get(cx);
    if (self) {
        self->self = state;
        self->state.onChange = ListenTo(state, &SelectState::OnListChange);
    }
    return out;
}
Select* Select::Items(const SearchableItem* it, int n) {
    items = it;
    nItems = n;
    return this;
}
Select* Select::Sections(const Str* titles, int n) {
    sections = titles;
    nSections = n;
    return this;
}
Select* Select::Placeholder(Str s) {
    placeholder = s;
    return this;
}
Select* Select::AccessibilityLabel(Str s) {
    accessibilityLabel = s;
    return this;
}
Select* Select::TitlePrefix(Str s) {
    titlePrefix = s;
    return this;
}
Select* Select::Empty(Str s) {
    empty = s;
    return this;
}
Select* Select::Empty(El* element) {
    emptyEl = element;
    return this;
}
Select* Select::W(float v) {
    width = v;
    return this;
}
Select* Select::MenuWidth(float v) {
    menuWidth = v;
    return this;
}
Select* Select::MenuMaxH(float v) {
    menuMaxH = v;
    return this;
}
Select* Select::WithSize(UiSize s) {
    size = s;
    return this;
}
Select* Select::Icon(IconName i) {
    icon = i;
    return this;
}
Select* Select::CheckIcon(IconName n) {
    checkIcon = n;
    return this;
}
Select* Select::Disabled(bool v) {
    disabled = v;
    return this;
}
Select* Select::Cleanable(bool v) {
    cleanable = v;
    return this;
}
Select* Select::Appearance(bool v) {
    appearance = v;
    return this;
}
Select* Select::FocusRing(bool v) {
    focusRing = v;
    return this;
}
Select* Select::Searchable(InputState* q, Listener onFocus) {
    query = q;
    onQueryFocus = onFocus;
    return this;
}
Select* Select::Multiple(bool v) {
    SearchableListState* s = state.Get(cx);
    if (s) {
        s->mode = v ? SearchableListMode::Multi : SearchableListMode::Single;
        s->closeOnSelect = !v;
    }
    return this;
}
Select* Select::OnToggle(Listener fn) {
    onToggle = fn;
    return this;
}
Select* Select::OnClear(Listener fn) {
    onClear = fn;
    return this;
}
Select* Select::OnMouseDownOut(Listener fn) {
    onMouseDownOut = fn;
    return this;
}
Select* Select::TriggerBoundsOut(Bounds* bounds) {
    triggerBoundsOut = bounds;
    return this;
}
Select* Select::TriggerRefine(const Style& style, uint32_t fields) {
    triggerStyle = style;
    triggerStyleSet = fields;
    return this;
}

Str SelectTriggerTitle(const SearchableListState* s, Str placeholder,
                       Str titlePrefix, Arena* a) {
    Str none = placeholder.s ? placeholder : Tr("Select.placeholder");
    if (!s || s->selected.len == 0 || !s->items) {
        return none;
    }
    if (s->selected.len > 1) {
        // Rust shows the picked items as tags; the trigger says how many when
        // there is no room for that.
        return StrDup(a, fmt("%d selected", s->selected.len));
    }
    int ix = s->selected[0];
    if (ix < 0 || ix >= s->nItems) {
        return none;
    }
    Str title =
        s->items[ix].display.s ? s->items[ix].display : s->items[ix].title;
    if (titlePrefix.s) {
        return StrDup(a, fmt("%s%s", titlePrefix, title));
    }
    return title;
}

void SelectToggleOpen(SearchableListState* s, Ctx* cx) {
    if (!s) {
        return;
    }
    s->open = !s->open;
    // The focus goes into the list that came up and comes back to the trigger
    // when it goes away — `content_focus_handle.focus(..)` on the way in and
    // `previous.focus(..)` on the way out, which is Rust's toggle. Focus that
    // has moved somewhere else on purpose is left alone.
    if (s->open) {
        s->previousFocus = WindowFocused(cx->win);
        FocusHandleFocus(cx->win, s->contentFocus);
    } else {
        if (s->previousFocus.IsValid() &&
            FocusHandleContainsFocused(cx->win, s->contentFocus)) {
            if (!FocusHandleRestore(cx->win, s->previousFocus)) {
                FocusHandleRestore(cx->win, s->triggerFocus);
            }
        }
        s->previousFocus = {};
    }
    // Opening starts the keyboard on whatever is already picked, so the first
    // arrow steps from there rather than from the top.
    s->list.selected = -1;
    if (s->open && s->selected.len > 0) {
        for (int m = 0; m < s->matches.len; m++) {
            if (s->matches[m] == s->selected[0]) {
                s->list.selected = m;
                break;
            }
        }
    }
    Notify(cx);
}

void SelectToggleOpen(SelectState* s, Ctx* cx) {
    if (s) {
        s->ToggleMenu(cx);
    }
}

void SelectClear(SearchableListState* s, Ctx* cx) {
    if (!s) {
        return;
    }
    VecClear(s->selected);
    Notify(cx);
}

void SelectClear(SelectState* s, Ctx* cx) {
    if (s) {
        s->Clean(cx);
    }
}

Select* Select::Trigger(El* e) {
    trigger = e;
    return this;
}
Select* Select::Footer(El* e) {
    footer = e;
    return this;
}
Select* Select::Delegate(const SearchableListDelegate& value) {
    delegate = value;
    hasDelegate = true;
    return this;
}

El* Select::IntoEl() {
    // The whole control is the select's, so its name goes on the stack of ids
    // around what it builds — the open transition below, and the popover's
    // state under it.
    IdScope scope(cx, id);
    const Theme& th = ThemeNow(cx->app);
    SearchableListState* s = state.Get(cx);
    if (SelectState* owner = selectState.Get(cx)) {
        if (query) {
            owner->activeQuery = query;
            owner->searchable = true;
        } else if (owner->searchable) {
            query = &owner->queryInput;
            owner->activeQuery = query;
        }
        owner->icon = icon;
        owner->titlePrefix = titlePrefix;
        owner->focusRingEnabled = focusRing;
        owner->state.items = items;
        owner->state.nItems = nItems;
    }
    // input_size / input_text_size, by size.
    float h = 32, padX = 10, font = 14;
    if (size == UiSize::Large) {
        h = 44;
        padX = 12;
        font = 16;
    } else if (size == UiSize::Small) {
        h = 24;
        padX = 8;
    } else if (size == UiSize::XSmall) {
        h = 20;
        padX = 4;
        font = 12;
    }
    bool open = s && s->open && !disabled;
    bool hasValue = s && s->selected.len > 0;
    Str title = SelectTriggerTitle(s, placeholder, titlePrefix, a);
    // The select's own name, so the parts inside it are scoped by it rather
    // than spelling it out. BindClick below names it again for the enabled
    // case; a disabled select is named all the same, since its children still
    // need something to fold against.
    El* box = Div(a)
                  ->Id(id)
                  ->FlexRow()
                  ->W(width)
                  ->H(h)
                  ->PadX(padX)
                  ->Gap(4)
                  ->ItemsCenter()
                  ->JustifyBetween();
    if (triggerBoundsOut) {
        box->BoundsOut(triggerBoundsOut);
    }
    if (triggerStyleSet) {
        box->Refine(triggerStyle, triggerStyleSet);
    }
    if (appearance) {
        box->Radius(th.radius)
            ->Bg(disabled ? th.muted : th.inputBg)
            ->Border(1, open ? th.ring : th.inputBorder);
        // select.rs: a disabled trigger is the whole control at half
        // strength, over and above the muted surface it already takes.
        if (disabled) {
            box->Opacity(0.5f);
        }
    }
    Rgba fg = disabled ? th.mutedFg : th.foreground;
    if (this->trigger) {
        // render_trigger: the caller's element is the whole of the trigger's
        // content, caret included, so nothing else goes in beside it.
        box->Child(this->trigger->W(kFill)->MinW(0));
    } else {
        box->Child(
            TextEl(a, title)->Font(font)->Fg(hasValue ? fg : th.mutedFg));
        if (cleanable && hasValue && !disabled) {
            // `SelectState::clean` calls cx.stop_propagation() first: the ×
            // sits inside the trigger, which is listening for the same click,
            // and a clear that also opened the list would be no clear at all.
            box->Child(Button::New(cx, StrL("clean"))
                           ->Text()
                           ->WithSize(UiSize::XSmall)
                           ->Icon(IconName::X)
                           ->OnClick(onClear)
                           ->IntoEl()
                           ->StopClick());
        } else if (icon != IconName::None) {
            // A custom icon replaces the caret, at xsmall.
            box->Child(IconEl(a, icon, 12)->Fg(th.mutedFg));
        } else {
            box->Child(Caret::New(size).TextColor(th.mutedFg).IntoEl(a));
        }
    }
    if (!disabled && !open) {
        BindClick(box, id, onToggle);
        box->FocusRing(focusRing);
    }
    // The two handles the toggle above moves focus between. Asked for once
    // and kept on the state, the way Rust's `focus_handle` and
    // `content_focus_handle` are, rather than derived from the element's name
    // each frame — which is what let the trigger's focus id and its *click*
    // id be the same number by accident.
    if (s) {
        if (!s->triggerFocus.IsValid()) {
            s->triggerFocus = FocusHandleNew(cx);
        }
        if (!s->contentFocus.IsValid()) {
            s->contentFocus = FocusHandleNew(cx);
        }
        if (!disabled) {
            box->TrackFocus(s->triggerFocus);
        }
    }

    El* menu = nullptr;
    if (open) {
        // The list is the whole dropdown: the query, the sections, the checks
        // and the empty state are all its own.
        SearchableList* list =
            SearchableList::New(cx, StrL("list"), state, query)
                ->InSelect(true)
                ->Items(items, nItems)
                ->W(menuWidth > 0 ? menuWidth : (width > 0 ? width + 2 : 242))
                ->CheckIcon(checkIcon)
                ->WithSize(size);
        if (sections) {
            list->Sections(sections, nSections);
        }
        if (query) {
            list->OnQueryFocus(onQueryFocus);
        }
        if (menuMaxH > 0) {
            list->MaxH(menuMaxH);
        }
        if (footer) {
            list->Footer(footer);
        }
        if (hasDelegate) {
            list->Delegate(delegate);
        }
        if (emptyEl) {
            list->Empty(emptyEl);
        } else if (empty.s) {
            list->Empty(
                Div(a)->H(96)->W(kFill)->ItemsCenter()->JustifyCenter()->Child(
                    TextEl(a, empty)->Font(font)->Fg(th.mutedFg)));
        }
        // `popover_style` on the panel itself, and the shared open motion
        // over it: the dropdown fades up while sliding the last 8 px out of
        // the trigger's edge.
        menu = PopoverSurface(cx, list->IntoEl());
        menu = DropdownOpen(cx, menu, MotionName(cx, StrL("open")));
        if (onMouseDownOut.IsValid()) {
            menu->OnMouseDownOut(onMouseDownOut);
        } else if (selectState.IsValid()) {
            menu->OnMouseDownOut(
                ListenTo(selectState, &SelectState::OnMouseDownOut));
        }
    } else if (s) {
        // A closed list still has to know its items and what the query left,
        // so the trigger can name the selection and the keys can move it.
        if (hasDelegate) {
            s->delegate = delegate;
            s->hasDelegate = true;
        }
        SearchableListSearch(s, items, nItems,
                             query ? InputValue(query) : Str{});
    }
    // accessibility_value: the committed selection with its prefix, or the
    // placeholder when nothing is selected — the same words the trigger
    // shows, and not the search query, so filtering the list leaves it be.
    Str accessibilityValue = SelectTriggerTitle(s, placeholder, titlePrefix, a);
    El* root = gpui::Select::New(cx, id, open, disabled, accessibilityLabel,
                                 onToggle, accessibilityValue)
                   ->W(width)
                   ->Child(box);
    // `("select-popup", cx.entity_id())`: the open list is deferred out of
    // the tree, so Rust qualifies its name with an identity rather than
    // leaning on the stack. The picker's id is what stands for the entity
    // here, so the spelled-out name is the faithful one.
    El* wrap = Popup::New(cx, StrDup(a, fmt("%s-popup", id)), root)
                   ->Content(DropdownPlaceContent(menu))
                   ->IntoEl();
    // The five bindings, on the element that holds both the trigger and the
    // popup: the trigger is focusable and so is the query field inside the
    // list, and the context is above whichever of them has the focus.
    if (!disabled) {
        SelectBindKeys(cx, wrap, state);
    }
    return wrap;
}

} // namespace component
} // namespace gpui
