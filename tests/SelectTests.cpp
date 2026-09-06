/* Ported from crates/base/src/select.rs.
 *
 * Rust binds up, down, enter, secondary-enter and escape in the select's key
 * context and hangs an on_action off each. Every one of those handlers is a
 * few lines of rules over `open` and `disabled`; this walks the chord in
 * through the keymap and pins what comes out. The focus transfer each handler
 * also does is the pair of handles the state keeps — the trigger's and the
 * list's — and the last case here pins that the second of them names a real
 * element, since a handle that names nothing restores focus by coincidence
 * rather than by containment. */

#include "Test.h"

// The chord, resolved in the select's context, read as what the select does.
static SelectAction ForChord(const char* spec, bool open, bool disabled) {
    SelectInitKeys();
    KeyChord c = {};
    utassert(KeyChordParse(Str(spec), &c));
    uint32_t ctx = KeyContextOf(SelectContext());
    return SelectActionOf(KeymapMatch(c, &ctx, 1).action, open, disabled);
}

static void ArrowsOpenAClosedSelect() {
    utassert(ForChord("down", false, false) == SelectAction::Open);
    utassert(ForChord("up", false, false) == SelectAction::Open);
    // Once open the root is done with them: Rust has focused the content by
    // then, so the list takes the arrow.
    utassert(ForChord("down", true, false) == SelectAction::None);
    utassert(ForChord("up", true, false) == SelectAction::None);
}

static void EnterOpensThenConfirms() {
    // secondary-enter is Confirm { secondary: true } in Rust, which has no
    // payload to carry here — it is its own name and the same answer.
    utassert(ForChord("secondary-enter", true, false) == SelectAction::Confirm);
    utassert(ForChord("enter", false, false) == SelectAction::Open);
    utassert(ForChord("enter", true, false) == SelectAction::Confirm);
}

static void EscapeOnlyCountsWhileOpen() {
    utassert(ForChord("escape", true, false) == SelectAction::Dismiss);
    // Closed, Rust propagates it so whatever encloses the select can use it.
    utassert(ForChord("escape", false, false) == SelectAction::None);
}

static void ADisabledSelectAnswersToNothing() {
    utassert(ForChord("down", false, true) == SelectAction::None);
    utassert(ForChord("up", true, true) == SelectAction::None);
    utassert(ForChord("enter", true, true) == SelectAction::None);
    utassert(ForChord("escape", true, true) == SelectAction::None);
}

static void OtherKeysAreNotTheSelects() {
    utassert(ForChord("tab", true, false) == SelectAction::None);
    utassert(ForChord("space", true, false) == SelectAction::None);
    utassert(ForChord("backspace", false, false) == SelectAction::None);
}

// `content_focus_handle` is `state.list.focus_handle(cx)` upstream, tracked
// on the list's own element. Ours is on the shared state, and the list inside
// a select tracks it: the focus a select moves into its dropdown has to land
// on something the frame can name, or a query field taking focus inside the
// list stops reading as the list's and closing leaves focus on an input that
// has gone. Not a tab stop — upstream asks for `.tab_stop(true)` on the
// trigger and nowhere else, so Tab walks past an open dropdown.
static void TheListInsideASelectIsTheContentHandle() {
    App app;
    Window* win = new Window();
    win->app = &app;
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.app = &app;
    cx.win = win;
    cx.a = a;

    using namespace gpui::component;
    Entity<SearchableListState> state =
        EntityNewState<SearchableListState>(&app);
    SearchableListState* s = state.Get(&app);
    s->contentFocus = FocusHandleNew(&cx);

    SearchableItem items[2] = {};
    items[0].title = StrL("Rust");
    items[0].value = StrL("rust");
    items[1].title = StrL("Go");
    items[1].value = StrL("go");
    SearchableListSearch(s, items, 2, Str{});

    El* box = SearchableList::New(&cx, StrL("list"), state, nullptr)
                  ->InSelect(true)
                  ->Items(items, 2)
                  ->IntoEl();
    utassert(box->style.focusId == s->contentFocus.id);
    utassert(!box->style.tabStop);

    // A list that is not inside a select is its own thing: it keeps the focus
    // id off its name and stays in the tab order, since it is what the reader
    // tabs to.
    El* alone = SearchableList::New(&cx, StrL("list"), state, nullptr)
                    ->Items(items, 2)
                    ->IntoEl();
    utassert(alone->style.focusId == HashClickId(StrL("list")));
    utassert(alone->style.tabStop);

    ArenaDelete(a);
    delete win;
    EntityDropAll(&app);
}

static void CaretKeepsTheSourceSizeScale() {
    using namespace gpui::component;
    utassertnear(Caret::New(UiSize::XSmall).IconSize(), 12.f);
    utassertnear(Caret::New(UiSize::Small).IconSize(), 14.f);
    utassertnear(Caret::New(UiSize::Medium).IconSize(), 16.f);
    utassertnear(Caret::New(UiSize::Large).IconSize(), 16.f);

    Arena* a = ArenaNew();
    Rgba color = Rgba{10, 20, 30, 255};
    El* icon = Caret::New(UiSize::Small).TextColor(color).IntoEl(a);
    utassert(base::StrEq(icon->iconPath, StrL("icons/chevron-down.svg")));
    utassertnear(icon->style.width, 14.f);
    utassert(icon->style.hasColor);
    utassert(icon->style.color.g == 20);
    ArenaDelete(a);
}

struct SelectEventSink {
    int count = 0;
    component::SelectEvent last = {};

    static void OnConfirm(SelectEventSink* self, Ctx*,
                          const component::SelectEvent* event) {
        self->count++;
        self->last = *event;
    }
};

static void SelectStateOwnsCommittedSelectionAndEvents() {
    using namespace gpui::component;
    App app;
    Window* win = new Window();
    win->app = &app;
    Ctx cx = {};
    cx.app = &app;
    cx.win = win;

    Entity<SelectState> state = SelectState::New(&app);
    SelectState* s = state.Get(&app);
    utassert(s != nullptr);
    Entity<SearchableListState> list = SelectListEntity(state);
    utassert(list.Get(&app) == s->List());

    SearchableItem items[] = {
        {StrL("Rust"), StrL("rust"), 0},
        {StrL("C++"), StrL("cpp"), 0},
        {StrL("Swift"), StrL("swift"), 2},
    };
    s->SetItems(items, 3);
    IndexPath swift = IndexPathNew(0).Section(2);
    s->SetSelectedIndex(&swift, &cx);
    IndexPath selected;
    utassert(s->SelectedIndex(&selected));
    utassert(selected == swift);
    utassert(base::StrEq(s->SelectedValue(), StrL("swift")));

    s->SetSelectedValue(StrL("cpp"), &cx);
    utassert(s->SelectedIndex(&selected));
    utassert(selected == IndexPathNew(1));

    s->Searchable(true);
    InputSetValue(&s->queryInput, StrL("Swift"));
    SearchableListSearch(s->List(), items, 3, InputValue(&s->queryInput));
    utassert(s->state.matches.len == 1);
    s->SetSelectedValue(StrL("rust"), &cx);
    utassert(InputValue(&s->queryInput).len == 0);
    utassert(s->state.matches.len == 3);

    Entity<SelectEventSink> sink = EntityNewState<SelectEventSink>(&app);
    SubscribeTo(&app, state, sink, &SelectEventSink::OnConfirm);
    ListEvent confirm = {ListEventKind::Confirm, 1, false};
    SelectState::OnListChange(s, &cx, &confirm);
    SelectEventSink* heard = sink.Get(&app);
    utassert(heard->count == 1);
    utassert(heard->last.hasValue);
    utassert(base::StrEq(heard->last.value, StrL("cpp")));

    s->Clean(&cx);
    utassert(!s->SelectedIndex(nullptr));
    utassert(heard->count == 2);
    utassert(!heard->last.hasValue);

    delete win;
    EntityDropAll(&app);
}

static void SourceSelectBuilderWritesItsOwnState() {
    using namespace gpui::component;
    App app;
    Window* win = new Window();
    win->app = &app;
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.app = &app;
    cx.win = win;
    cx.a = a;

    Entity<SelectState> state = SelectState::New(&app);
    SearchableItem items[] = {{StrL("One"), StrL("one")}};
    component::Select::New(&cx, StrL("source-select"), state)
        ->Items(items, 1)
        ->Icon(IconName::Search)
        ->TitlePrefix(StrL("Value: "))
        ->FocusRing(false)
        ->IntoEl();
    SelectState* s = state.Get(&app);
    utassert(s->state.items == items);
    utassert(s->state.nItems == 1);
    utassert(s->icon == IconName::Search);
    utassert(base::StrEq(s->titlePrefix, StrL("Value: ")));
    utassert(!s->focusRingEnabled);

    ArenaDelete(a);
    delete win;
    EntityDropAll(&app);
}

struct ComboboxEventSink {
    int changes = 0;
    int confirms = 0;
    int lastCount = 0;
    Str first = {};

    static void OnEvent(ComboboxEventSink* self, Ctx*,
                        const component::ComboboxEvent* event) {
        if (event->kind == component::ComboboxEventKind::Change) {
            self->changes++;
        } else {
            self->confirms++;
        }
        self->lastCount = event->nValues;
        self->first = event->nValues > 0 ? event->values[0] : Str{};
    }
};

struct ComboboxRenderProbe {
    int triggers = 0;
    int footers = 0;
    int selected = 0;
    bool open = false;
    bool disabled = false;
    UiSize size = UiSize::Medium;
};

static El* RenderComboboxTrigger(
    Ctx* cx, void* data, const component::ComboboxTriggerContext* trigger) {
    ComboboxRenderProbe* probe = (ComboboxRenderProbe*)data;
    probe->triggers++;
    probe->selected = trigger->SelectionCount();
    probe->open = trigger->IsOpen();
    probe->disabled = trigger->IsDisabled();
    probe->size = trigger->Size();
    const component::SearchableListItem* item = trigger->SelectionItem(0);
    utassert(item && base::StrEq(item->value, StrL("vue")));
    utassert(base::StrEq(trigger->Placeholder(), StrL("Choose")));
    return Div(cx->a);
}

static El* RenderComboboxFooter(Ctx* cx, void* data) {
    ((ComboboxRenderProbe*)data)->footers++;
    return Div(cx->a);
}

static void ComboboxOwnsStateEventsAndTriggerContext() {
    using namespace gpui::component;
    App app;
    Window* win = new Window();
    win->app = &app;
    Arena* a = ArenaNew();
    Entity<ComboboxState> state = ComboboxState::New(&app);
    Ctx cx = {&app, win, a, state.id};
    ComboboxState* s = state.Get(&app);
    utassert(s && ComboboxListEntity(state).Get(&app) == s->List());

    SearchableListItem items[] = {
        {StrL("React"), StrL("react")},
        {StrL("Vue"), StrL("vue")},
        {StrL("Angular"), StrL("angular")},
    };
    s->SetItems(items, 3);
    s->Searchable(true)->Multiple(true);
    s->SetQuery(StrL("React"), &cx);
    Str selected[] = {StrL("vue"), StrL("missing")};
    s->SetSelectedValues(selected, 2, &cx);
    utassert(s->Query().len == 0);
    utassert(s->state.matches.len == 3);
    utassert(s->Selection().len == 1 && s->Selection()[0] == 1);
    utassert(base::StrEq(s->SelectedValue(), StrL("vue")));

    Entity<ComboboxEventSink> sink = EntityNewState<ComboboxEventSink>(&app);
    SubscribeTo(&app, state, sink, &ComboboxEventSink::OnEvent);
    s->SetOpen(true, &cx);
    utassert(s->queryInput.focused && win->input == &s->queryInput);
    SearchableListState::OnRowClick(s->List(), &cx, nullptr, 0);
    ComboboxEventSink* heard = sink.Get(&app);
    utassert(heard->changes == 1 && heard->confirms == 0);
    utassert(heard->lastCount == 2);
    utassert(base::StrEq(heard->first, StrL("vue")));
    utassert(s->state.open);

    MouseDownEvent outside = {};
    outside.x = 400;
    outside.y = 400;
    ComboboxState::OnMouseDownOut(s, &cx, &outside);
    utassert(heard->confirms == 1 && !s->state.open);
    utassert(!s->queryInput.focused);

    // Programmatic replacement updates the committed snapshot without
    // emitting, and a same-value single selection neither emits nor closes.
    IndexPath vue = IndexPathNew(1);
    s->Multiple(false);
    s->SetSelectedIndices(&vue, 1, &cx);
    s->SetOpen(true, &cx);
    int changesBefore = heard->changes;
    int confirmsBefore = heard->confirms;
    SearchableListState::OnRowClick(s->List(), &cx, nullptr, 1);
    utassert(heard->changes == changesBefore);
    utassert(heard->confirms == confirmsBefore);
    utassert(s->state.open);

    ComboboxRenderProbe probe;
    component::Combobox::New(&cx, StrL("source-combobox"), state)
        ->Items(items, 3)
        ->Placeholder(StrL("Choose"))
        ->WithSize(UiSize::Large)
        ->Icon(IconName::Search)
        ->CheckIcon(IconName::CircleCheck)
        ->Appearance(false)
        ->FocusRing(false)
        ->RenderTrigger(&probe, RenderComboboxTrigger)
        ->RenderFooter(&probe, RenderComboboxFooter)
        ->IntoEl();
    utassert(probe.triggers == 1 && probe.footers == 1);
    utassert(probe.selected == 1 && probe.open);
    utassert(!probe.disabled && probe.size == UiSize::Large);
    utassert(s->triggerIcon == IconName::Search);
    utassert(s->checkIcon == IconName::CircleCheck);
    utassert(!s->focusRingEnabled);

    ArenaDelete(a);
    delete win;
    EntityDropAll(&app);
}

static El* FindRole(El* e, AccessibilityRole role) {
    if (!e) return nullptr;
    if (e->accessibility.role == role) return e;
    for (El* child = e->first; child; child = child->next) {
        if (El* found = FindRole(child, role)) return found;
    }
    return nullptr;
}

// select.rs: projects_application_owned_accessible_state. The controlled
// root carries the application's label and committed value, says whether it
// is expanded, and exposes activation itself — platform adapters may flatten
// the trigger child — unless it is disabled.
static void TheRootProjectsApplicationOwnedAccessibleState() {
    App app;
    Window* win = new Window();
    win->app = &app;
    Arena* a = ArenaNew();
    Ctx cx = {&app, win, a, {}};
    // The open-state handler the default action binds to; any live listener
    // will do for the projection.
    Entity<component::SelectState> owner = component::SelectState::New(&app);
    Listener live = ListenTo(owner, &component::SelectState::OnMouseDownOut);
    El* enabled =
        gpui::Select::New(&cx, StrL("enabled"), true, false,
                          StrL("Programming language"), live, StrL("Rust"));
    El* disabled = gpui::Select::New(&cx, StrL("disabled"), false, true, Str{},
                                     live, Str{});
    utassert(StrEq(enabled->accessibility.label, StrL("Programming language")));
    utassert(StrEq(enabled->accessibility.value, StrL("Rust")));
    utassert(enabled->accessibility.hasExpanded && enabled->accessibility
                                                       .expanded);
    utassert(disabled->accessibility.hasExpanded && !disabled->accessibility
                                                         .expanded);
    utassert(enabled->accessibilityDefault.IsValid());
    utassert(!disabled->accessibilityDefault.IsValid());
    ArenaDelete(a);
    delete win;
    EntityDropAll(&app);
}

// component select.rs:
// test_select_accessibility_value_tracks_placeholder_and_selection. The value
// is the committed selection with its prefix, or the placeholder; filtering
// the list does not move it.
static void SelectAccessibilityValueTracksPlaceholderAndSelection() {
    using namespace gpui::component;
    App app;
    Window* win = new Window();
    win->app = &app;
    Arena* a = ArenaNew();
    Ctx cx = {&app, win, a, {}};

    Entity<SelectState> state = SelectState::New(&app);
    SelectState* s = state.Get(&app);
    s->Searchable(true);
    SearchableItem items[] = {{StrL("Rust"), StrL("rust")},
                              {StrL("Go"), StrL("go")}};
    auto render = [&](Str prefix, bool placeholder) {
        component::Select* select =
            component::Select::New(&cx, StrL("language"), state)
                ->Items(items, 2)
                ->AccessibilityLabel(StrL("Programming language"));
        if (placeholder) select->Placeholder(StrL("Choose a language"));
        if (prefix.s) select->TitlePrefix(prefix);
        El* root = FindRole(select->IntoEl(), AccessibilityRole::ComboBox);
        utassert(root != nullptr);
        return root ? root->accessibility.value : Str{};
    };
    utassert(StrEq(render(Str{}, true), StrL("Choose a language")));

    s->SetSelectedValue(StrL("rust"), &cx);
    utassert(StrEq(render(Str{}, true), StrL("Rust")));

    // Filtering changes the available rows, not the committed value.
    SearchableListSearch(&s->state, items, 2, StrL("Go"));
    utassert(s->state.matches.len == 1);
    utassert(StrEq(render(Str{}, true), StrL("Rust")));

    utassert(StrEq(render(StrL("Language: "), true), StrL("Language: Rust")));

    s->SetSelectedIndex(nullptr, &cx);
    utassert(StrEq(render(Str{}, true), StrL("Choose a language")));
    utassert(StrEq(render(Str{}, false), Tr("Select.placeholder")));

    ArenaDelete(a);
    delete win;
    EntityDropAll(&app);
}

void TestSelect() {
    TestSuite("select");
    TheRootProjectsApplicationOwnedAccessibleState();
    SelectAccessibilityValueTracksPlaceholderAndSelection();
    ArrowsOpenAClosedSelect();
    EnterOpensThenConfirms();
    EscapeOnlyCountsWhileOpen();
    ADisabledSelectAnswersToNothing();
    OtherKeysAreNotTheSelects();
    TheListInsideASelectIsTheContentHandle();
    CaretKeepsTheSourceSizeScale();
    SelectStateOwnsCommittedSelectionAndEvents();
    SourceSelectBuilderWritesItsOwnState();
    ComboboxOwnsStateEventsAndTriggerContext();
}
