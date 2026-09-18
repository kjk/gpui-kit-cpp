/* Accessibility semantics ported from the role/state/action assertions in
 * crates/base and crates/ui. Most tests pin the portable tree independently
 * of an OS; Windows also probes the actual UI Automation COM projection. */

#include "Test.h"

#if GPUI_OS_WINDOWS
namespace gpui {
// Internal conformance probe implemented beside the UIA provider, not part of
// the published header surface.
bool AccessibilityWinSmokeTest(Window* win, uint32_t nodeId);
} // namespace gpui
#endif

struct AccessibilityFrame {
    App app;
    Window* win = nullptr;
    Arena* arena = nullptr;
    Ctx cx = {};
};

static AccessibilityFrame NewAccessibilityFrame() {
    AccessibilityFrame f;
    f.win = new Window();
    f.arena = ArenaNew();
    f.win->app = &f.app;
    f.cx.app = &f.app;
    f.cx.win = f.win;
    f.cx.a = f.arena;
    return f;
}

static void FreeAccessibilityFrame(AccessibilityFrame* f) {
    VecReset(f->win->accessibility);
    WindowKeyedFree(f->win);
    delete f->win;
    f->win = nullptr;
    ArenaDelete(f->arena);
    f->arena = nullptr;
    EntityDropAll(&f->app);
}

static const AccessibilityNode* RoleNode(const Vec<AccessibilityNode>& nodes,
                                         AccessibilityRole role,
                                         int occurrence = 0) {
    for (int i = 0; i < len(nodes); i++) {
        if (nodes[i].info.role == role && occurrence-- == 0) {
            return &nodes[i];
        }
    }
    return nullptr;
}

static void Increment(int* value) {
    (*value)++;
}

static void TheTreeSkipsVisualBoxesButKeepsSemanticParents() {
    AccessibilityFrame f = NewAccessibilityFrame();
    int clicks = 0;
    El* dialog = Div(f.arena)->Role(AccessibilityRole::Dialog);
    El* visual = Div(f.arena)->Pad(8);
    El* button = Button::New(&f.cx, StrL("save"))
                     ->OnClick(MkFunc0(Increment, &clicks))
                     ->Child(TextEl(f.arena, StrL("Save")))
                     ->Child(TextEl(f.arena, StrL("changes")));
    visual->Child(button);
    dialog->Child(visual);

    IdsCollect(dialog);
    AccessibilityCollect(dialog, &f.win->accessibility);
    utassert(f.win->accessibility.len == 2);
    const AccessibilityNode* dialogNode =
        RoleNode(f.win->accessibility, AccessibilityRole::Dialog);
    const AccessibilityNode* buttonNode =
        RoleNode(f.win->accessibility, AccessibilityRole::Button);
    utassert(dialogNode && buttonNode);
    if (dialogNode && buttonNode) {
        utassert(dialogNode->parent == -1);
        utassert(buttonNode->parent == 0);
        utassert(base::StrEq(buttonNode->info.label, StrL("Save changes")));
        utassert(buttonNode->actions & AccessibilityActionDefault);
        utassert(buttonNode->actions & AccessibilityActionFocus);
        utassert(WindowAccessibilityNode(f.win, buttonNode->id) == buttonNode);
        utassert(WindowAccessibilityPerform(f.win, buttonNode->id,
                                            AccessibilityAction::Default));
        utassert(clicks == 1);
        utassert(WindowAccessibilityPerform(f.win, buttonNode->id,
                                            AccessibilityAction::Focus));
        utassert(f.win->focusId == buttonNode->focusId);
    }
    FreeAccessibilityFrame(&f);
}

static void StyledButtonsCanReplaceTheirVisibleAccessibleName() {
    AccessibilityFrame f = NewAccessibilityFrame();
    component::Init(&f.app);
    El* button = component::Button::New(&f.cx, StrL("row-action"))
                     ->Label(StrL("Save"))
                     ->AccessibilityLabel(StrL("Save the current document"))
                     ->IntoEl();
    AccessibilityCollect(button, &f.win->accessibility);
    const AccessibilityNode* node =
        RoleNode(f.win->accessibility, AccessibilityRole::Button);
    utassert(node &&
             base::StrEq(node->info.label, StrL("Save the current document")));
    FreeAccessibilityFrame(&f);
}

// checkbox.rs / color_picker.rs / radio.rs / switch.rs, mod tests:
// an_explicit_accessibility_label_replaces_the_visible_one. The explicit name
// wins over the visible label, and what is drawn does not change with it.
static void AnExplicitAccessibilityLabelReplacesTheVisibleOne() {
    AccessibilityFrame f = NewAccessibilityFrame();
    component::Init(&f.app);

    component::Checkbox* plainBox =
        component::Checkbox::New(&f.cx, StrL("remember"))
            ->Label(StrL("Remember me"));
    utassert(!plainBox->accessibilityLabel.s);
    component::Checkbox* namedBox =
        component::Checkbox::New(&f.cx, StrL("remember"))
            ->Label(StrL("Remember me"))
            ->AccessibilityLabel(StrL("Remember this account"));
    // ...and must not change what is drawn.
    utassert(base::StrEq(namedBox->label, StrL("Remember me")));
    AccessibilityCollect(namedBox->IntoEl(), &f.win->accessibility);
    const AccessibilityNode* box =
        RoleNode(f.win->accessibility, AccessibilityRole::CheckBox);
    utassert(box &&
             base::StrEq(box->info.label, StrL("Remember this account")));

    component::Radio* namedRadio =
        component::Radio::New(&f.cx, StrL("auto"))
            ->Label(StrL("Automatic"))
            ->AccessibilityLabel(StrL("Choose automatic mode"));
    utassert(base::StrEq(namedRadio->label, StrL("Automatic")));
    AccessibilityCollect(namedRadio->IntoEl(), &f.win->accessibility);
    const AccessibilityNode* radio =
        RoleNode(f.win->accessibility, AccessibilityRole::RadioButton);
    utassert(radio &&
             base::StrEq(radio->info.label, StrL("Choose automatic mode")));

    component::Switch* namedSwitch =
        component::Switch::New(&f.cx, StrL("wifi"))
            ->Label(StrL("Wi-Fi"))
            ->AccessibilityLabel(StrL("Toggle Wi-Fi"));
    utassert(base::StrEq(namedSwitch->label, StrL("Wi-Fi")));
    AccessibilityCollect(namedSwitch->IntoEl(), &f.win->accessibility);
    const AccessibilityNode* sw =
        RoleNode(f.win->accessibility, AccessibilityRole::Switch);
    utassert(sw && base::StrEq(sw->info.label, StrL("Toggle Wi-Fi")));

    component::ColorPicker* namedPicker =
        component::ColorPicker::New(&f.cx, StrL("picker"))
            ->Label(StrL("Color"))
            ->AccessibilityLabel(StrL("Text color"));
    utassert(base::StrEq(namedPicker->label, StrL("Color")));
    El* pickerEl = namedPicker->IntoEl();
    IdsCollect(pickerEl);
    AccessibilityCollect(pickerEl, &f.win->accessibility);
    const AccessibilityNode* picker =
        RoleNode(f.win->accessibility, AccessibilityRole::Button);
    utassert(picker && base::StrEq(picker->info.label, StrL("Text color")));

    FreeAccessibilityFrame(&f);
}

// progress.rs / progress_circle.rs / table.rs, mod tests:
// stores_an_explicit_accessibility_label. These four name explicitly only —
// a progress value, a caption, a placeholder or a selected value describes
// the current value rather than the control, so none is inferred as a name.
static void ExplicitOnlyNamesReachTheAnnouncedControl() {
    AccessibilityFrame f = NewAccessibilityFrame();
    component::Init(&f.app);

    utassert(!component::Progress::New(&f.cx)->accessibilityLabel.s);
    El* bar = component::Progress::New(&f.cx)
                  ->Id(StrL("upload"))
                  ->Value(40)
                  ->AccessibilityLabel(StrL("Upload progress"))
                  ->IntoEl();
    AccessibilityCollect(bar, &f.win->accessibility);
    const AccessibilityNode* barNode =
        RoleNode(f.win->accessibility, AccessibilityRole::ProgressIndicator);
    utassert(barNode &&
             base::StrEq(barNode->info.label, StrL("Upload progress")));

    utassert(!component::ProgressCircle::New(&f.cx)->accessibilityLabel.s);
    El* circle = component::ProgressCircle::New(&f.cx)
                     ->Id(StrL("upload-circle"))
                     ->Value(40)
                     ->AccessibilityLabel(StrL("Upload progress"))
                     ->IntoEl();
    AccessibilityCollect(circle, &f.win->accessibility);
    // Each collection replaces what the last one gathered, so this is the
    // circle's own node rather than the bar's.
    const AccessibilityNode* circleNode =
        RoleNode(f.win->accessibility, AccessibilityRole::ProgressIndicator);
    utassert(circleNode &&
             base::StrEq(circleNode->info.label, StrL("Upload progress")));

    utassert(!component::Table::New(&f.cx, StrL("t"))->accessibilityLabel.s);
    El* table = component::Table::New(&f.cx, StrL("invoices"))
                    ->AccessibilityLabel(StrL("Recent invoices"))
                    ->IntoEl();
    AccessibilityCollect(table, &f.win->accessibility);
    const AccessibilityNode* tableNode =
        RoleNode(f.win->accessibility, AccessibilityRole::Table);
    utassert(tableNode &&
             base::StrEq(tableNode->info.label, StrL("Recent invoices")));

    FreeAccessibilityFrame(&f);
}

static void ExplicitAriaFieldsSurviveCollection() {
    AccessibilityFrame f = NewAccessibilityFrame();
    El* node = Div(f.arena)
                   ->Role(AccessibilityRole::Heading)
                   ->AccessibilityId(StrL("heading.main"))
                   ->AriaLabel(StrL("Explicit"))
                   ->AriaValue(StrL("value"))
                   ->AriaPlaceholder(StrL("placeholder"))
                   ->AriaToggled(AccessibilityToggled::Mixed)
                   ->AriaSelected(true)
                   ->AriaExpanded(false)
                   ->AriaNumericValue(4)
                   ->AriaMinNumericValue(1)
                   ->AriaMaxNumericValue(9)
                   ->AriaNumericValueStep(2)
                   ->AriaOrientation(AccessibilityOrientation::Vertical)
                   ->AriaPositionInSet(3)
                   ->AriaSizeOfSet(7)
                   ->AriaRowCount(11)
                   ->AriaColumnCount(5)
                   ->AriaRowIndex(6)
                   ->AriaColumnIndex(4)
                   ->AriaLevel(2)
                   ->AriaDisabled(true)
                   ->Child(TextEl(f.arena, StrL("Derived")));
    AccessibilityCollect(node, &f.win->accessibility);
    utassert(f.win->accessibility.len == 1);
    if (f.win->accessibility.len == 1) {
        const AccessibilityInfo& a = f.win->accessibility[0].info;
        utassert(base::StrEq(a.authorId, StrL("heading.main")));
        utassert(base::StrEq(a.label, StrL("Explicit")));
        utassert(base::StrEq(a.value, StrL("value")));
        utassert(base::StrEq(a.placeholder, StrL("placeholder")));
        utassert(a.toggled == AccessibilityToggled::Mixed);
        utassert(a.hasSelected && a.selected);
        utassert(a.hasExpanded && !a.expanded);
        utassert(a.hasNumericValue && TestNear(a.numericValue, 4));
        utassert(a.hasMinNumericValue && TestNear(a.minNumericValue, 1));
        utassert(a.hasMaxNumericValue && TestNear(a.maxNumericValue, 9));
        utassert(a.hasNumericValueStep && TestNear(a.numericValueStep, 2));
        utassert(a.orientation == AccessibilityOrientation::Vertical);
        utassert(a.positionInSet == 3 && a.sizeOfSet == 7);
        utassert(a.hasPositionInSet && a.hasSizeOfSet);
        utassert(a.rowCount == 11 && a.columnCount == 5);
        utassert(a.hasRowCount && a.hasColumnCount);
        utassert(a.rowIndex == 6 && a.columnIndex == 4);
        utassert(a.hasRowIndex && a.hasColumnIndex);
        utassert(a.level == 2 && a.hasLevel && a.disabled);
        utassert(f.win->accessibility[0].actions == AccessibilityActionNone);
    }
    FreeAccessibilityFrame(&f);
}

struct SemanticActionView {
    int increments = 0;
    int decrements = 0;
    int defaults = 0;

    static El* Render(SemanticActionView*, Ctx* cx) { return Div(cx->a); }

    static void Increment(SemanticActionView* self, Ctx*, const ClickEvent*) {
        self->increments++;
    }
    static void Decrement(SemanticActionView* self, Ctx*, const ClickEvent*) {
        self->decrements++;
    }
    static void Default(SemanticActionView* self, Ctx*, const ClickEvent*) {
        self->defaults++;
    }
};

static void ExplicitSemanticListenersAreInvoked() {
    AccessibilityFrame f = NewAccessibilityFrame();
    Entity<SemanticActionView> view = EntityNew<SemanticActionView>(&f.app);
    f.cx.self = view.id;
    El* node = Div(f.arena)
                   ->Role(AccessibilityRole::SpinButton)
                   ->OnAccessibilityDefault(
                       Listen(&f.cx, &SemanticActionView::Default))
                   ->OnAccessibilityIncrement(
                       Listen(&f.cx, &SemanticActionView::Increment))
                   ->OnAccessibilityDecrement(
                       Listen(&f.cx, &SemanticActionView::Decrement));
    AccessibilityCollect(node, &f.win->accessibility);
    const AccessibilityNode* semantic =
        RoleNode(f.win->accessibility, AccessibilityRole::SpinButton);
    utassert(semantic && (semantic->actions & AccessibilityActionDefault) &&
             (semantic->actions & AccessibilityActionIncrement) &&
             (semantic->actions & AccessibilityActionDecrement));
    if (semantic) {
        uint32_t id = semantic->id;
        utassert(WindowAccessibilityPerform(f.win, id,
                                            AccessibilityAction::Default));
        utassert(WindowAccessibilityPerform(f.win, id,
                                            AccessibilityAction::Increment));
        utassert(WindowAccessibilityPerform(f.win, id,
                                            AccessibilityAction::Decrement));
    }
    SemanticActionView* state = view.Get(&f.app);
    utassert(state && state->defaults == 1 && state->increments == 1 &&
             state->decrements == 1);
    FreeAccessibilityFrame(&f);
}

static void ConditionalAndCompositeRolesMatchUpstream() {
    AccessibilityFrame f = NewAccessibilityFrame();
    El* colorPicker =
        ColorPicker::New(&f.cx, StrL("picker"), true, false, StrL("Color"))
            ->Child(Div(f.arena)->PathId(StrL("trigger")));
    El* unnamedHeader =
        AccordionHeader::New(&f.cx, AccordionTrigger::New(&f.cx, StrL("u")));
    El* namedHeader = AccordionHeader::New(
        &f.cx, AccordionTrigger::New(&f.cx, StrL("n")), StrL("heading"), 4);
    El* root = Div(f.arena)
                   ->Child(unnamedHeader)
                   ->Child(namedHeader)
                   ->Child(AccordionPanel::New(&f.cx))
                   ->Child(AccordionPanel::New(&f.cx, StrL("region")))
                   ->Child(colorPicker)
                   ->Child(ColorSwatch::New(&f.cx, StrL("swatch"), {}, {},
                                            0x12ab34, true))
                   ->Child(DatePicker::New(&f.cx, StrL("date"), false, true));
    IdsCollect(root);
    AccessibilityCollect(root, &f.win->accessibility);
    utassert(RoleNode(f.win->accessibility, AccessibilityRole::Heading) &&
             RoleNode(f.win->accessibility, AccessibilityRole::Heading)
                     ->info.level == 4);
    utassert(RoleNode(f.win->accessibility, AccessibilityRole::Region));
    const AccessibilityNode* picker =
        RoleNode(f.win->accessibility, AccessibilityRole::Button, 2);
    utassert(picker && picker->info.hasExpanded && picker->info.expanded &&
             base::StrEq(picker->info.label, StrL("Color")) &&
             (picker->actions & AccessibilityActionFocus));
    const AccessibilityNode* swatch =
        RoleNode(f.win->accessibility, AccessibilityRole::RadioButton);
    utassert(swatch && swatch->info.hasSelected && swatch->info.selected &&
             base::StrEq(swatch->info.label,
                         ColorPickerHexString(f.arena, 0x12ab34)));
    const AccessibilityNode* date =
        RoleNode(f.win->accessibility, AccessibilityRole::ComboBox);
    utassert(date && date->info.hasExpanded && date->info.expanded);
    // The unnamed header and panel add neither role.
    utassert(f.win->accessibility.len == 7);
    FreeAccessibilityFrame(&f);
}

static void InputContentTypesAndSecretsProjectSafely() {
    AccessibilityFrame f = NewAccessibilityFrame();
    component::Init(&f.app);
    InputState emailState;
    InputSetValue(&emailState, StrL("ada@example.test"));
    El* email = component::Input::New(&f.cx, StrL("email"), &emailState)
                    ->ContentType(component::InputContentType::EmailAddress)
                    ->AccessibilityId(StrL("account.email"))
                    ->AriaLabel(StrL("Email address"))
                    ->IntoEl();
    AccessibilityCollect(email, &f.win->accessibility);
    const AccessibilityNode* node =
        RoleNode(f.win->accessibility, AccessibilityRole::EmailInput);
    utassert(node && base::StrEq(node->info.authorId, StrL("account.email")) &&
             base::StrEq(node->info.label, StrL("Email address")) &&
             base::StrEq(node->info.value, StrL("ada@example.test")) &&
             (node->actions & AccessibilityActionSetValue));

    InputState passwordState;
    InputSetValue(&passwordState, StrL("never expose this"));
    El* password =
        component::Input::New(&f.cx, StrL("password"), &passwordState)
            ->ContentType(component::InputContentType::Password)
            ->Readonly()
            ->IntoEl();
    AccessibilityCollect(password, &f.win->accessibility);
    node = RoleNode(f.win->accessibility, AccessibilityRole::PasswordInput);
    utassert(node && !node->info.value.s &&
             !(node->actions & AccessibilityActionSetValue));
    FreeAccessibilityFrame(&f);
}

static void BaseControlsProjectTheirControlledState() {
    AccessibilityFrame f = NewAccessibilityFrame();
    El* root = Div(f.arena)
                   ->Child(Checkbox::New(
                       &f.cx, StrL("check"), CheckboxState::Indeterminate,
                       false, {}, nullptr, nullptr, StrL("Remember choice")))
                   ->Child(Radio::New(&f.cx, StrL("radio"), true))
                   ->Child(Switch::New(&f.cx, StrL("switch"), true, false, {},
                                       nullptr, nullptr, StrL("Airplane mode"),
                                       3, false, FocusHandle{-88}))
                   ->Child(Toggle::New(&f.cx, StrL("toggle"), true))
                   ->Child(Progress::New(&f.cx, StrL("done"), 120, false,
                                         StrL("Downloading release")))
                   ->Child(Progress::New(&f.cx, StrL("busy"), 40, true))
                   ->Child(Tab::New(&f.cx, StrL("account"), false, {}, true,
                                    StrL("Account"), 2, 5));
    AccessibilityCollect(root, &f.win->accessibility);

    const AccessibilityNode* check =
        RoleNode(f.win->accessibility, AccessibilityRole::CheckBox);
    const AccessibilityNode* radio =
        RoleNode(f.win->accessibility, AccessibilityRole::RadioButton);
    const AccessibilityNode* sw =
        RoleNode(f.win->accessibility, AccessibilityRole::Switch);
    const AccessibilityNode* toggle =
        RoleNode(f.win->accessibility, AccessibilityRole::Button);
    const AccessibilityNode* done =
        RoleNode(f.win->accessibility, AccessibilityRole::ProgressIndicator);
    const AccessibilityNode* busy =
        RoleNode(f.win->accessibility, AccessibilityRole::ProgressIndicator, 1);
    const AccessibilityNode* tab =
        RoleNode(f.win->accessibility, AccessibilityRole::Tab);
    utassert(check && check->info.toggled == AccessibilityToggled::Mixed);
    utassert(check && base::StrEq(check->info.label, StrL("Remember choice")));
    utassert(radio && radio->info.hasSelected && radio->info.selected);
    utassert(sw && sw->info.toggled == AccessibilityToggled::True);
    utassert(sw && base::StrEq(sw->info.label, StrL("Airplane mode")));
    utassert(sw && sw->focusId == -88);
    utassert(toggle && toggle->info.toggled == AccessibilityToggled::True);
    utassert(done && done->info.hasNumericValue &&
             TestNear(done->info.numericValue, 100));
    utassert(done &&
             base::StrEq(done->info.label, StrL("Downloading release")));
    utassert(busy && !busy->info.hasNumericValue);
    utassert(tab && tab->info.hasSelected && tab->info.selected);
    if (tab) {
        utassert(base::StrEq(tab->info.label, StrL("Account")));
        utassert(tab->info.positionInSet == 2 && tab->info.sizeOfSet == 5);
    }
    FreeAccessibilityFrame(&f);
}

static void TablesKeepCountsAndOneBasedIndices() {
    AccessibilityFrame f = NewAccessibilityFrame();
    El* table = Table::New(&f.cx, StrL("table"), 12, 4, StrL("Open positions"));
    El* body = TableBody::New(&f.cx, StrL("body"));
    El* row = TableRow::New(&f.cx, StrL("row"), 3);
    row->Child(TableCell::New(&f.cx, StrL("cell"), 2)
                   ->Child(TextEl(f.arena, StrL("Ada"))));
    body->Child(row);
    table->Child(body);
    table->Child(TableHeader::New(&f.cx, StrL("header"))
                     ->Child(TableHead::New(&f.cx, StrL("name"), 2)
                                 ->AriaLabel(StrL("Name"))));
    AccessibilityCollect(table, &f.win->accessibility);

    const AccessibilityNode* tableNode =
        RoleNode(f.win->accessibility, AccessibilityRole::Table);
    const AccessibilityNode* bodyNode =
        RoleNode(f.win->accessibility, AccessibilityRole::RowGroup);
    const AccessibilityNode* rowNode =
        RoleNode(f.win->accessibility, AccessibilityRole::Row);
    const AccessibilityNode* cellNode =
        RoleNode(f.win->accessibility, AccessibilityRole::Cell);
    utassert(tableNode && tableNode->info.rowCount == 12 &&
             tableNode->info.columnCount == 4);
    utassert(tableNode &&
             base::StrEq(tableNode->info.label, StrL("Open positions")));
    utassert(bodyNode && bodyNode->parent == 0);
    utassert(rowNode && rowNode->parent == 1 && rowNode->info.rowIndex == 3);
    utassert(cellNode && cellNode->parent == 2 &&
             cellNode->info.columnIndex == 2);
    utassert(cellNode && base::StrEq(cellNode->info.label, StrL("Ada")));
#if GPUI_OS_WINDOWS
    if (tableNode && cellNode) {
        utassert(AccessibilityWinSmokeTest(f.win, tableNode->id));
        utassert(AccessibilityWinSmokeTest(f.win, cellNode->id));
    }
#endif
    FreeAccessibilityFrame(&f);
}

static void SliderActionsUseTheSlidersOwnStep() {
    AccessibilityFrame f = NewAccessibilityFrame();
    SliderState state = SliderStateNew(0, 10, SliderSingle(5), 2);
    El* slider = Slider::New(&f.cx, &state, Axis::Vertical);
    AccessibilityCollect(slider, &f.win->accessibility);
    const AccessibilityNode* node =
        RoleNode(f.win->accessibility, AccessibilityRole::Slider);
    utassert(node &&
             node->info.orientation == AccessibilityOrientation::Vertical);
    utassert(node && node->info.hasNumericValue &&
             TestNear(node->info.numericValue, 5));
    utassert(node && (node->actions & AccessibilityActionIncrement));
    utassert(node && (node->actions & AccessibilityActionDecrement));
    if (node) {
        uint32_t id = node->id;
#if GPUI_OS_WINDOWS
        utassert(AccessibilityWinSmokeTest(f.win, id));
#endif
        utassert(WindowAccessibilityPerform(f.win, id,
                                            AccessibilityAction::Increment));
        utassertnear(state.value.End(), 7);
        utassert(WindowAccessibilityPerform(f.win, id,
                                            AccessibilityAction::Decrement));
        utassertnear(state.value.End(), 5);
        utassert(WindowAccessibilitySetNumericValue(f.win, id, 8.6f));
        utassertnear(state.value.End(), 8);
        utassert(WindowAccessibilitySetNumericValue(f.win, id, 100));
        utassertnear(state.value.End(), 10);
    }
    FreeAccessibilityFrame(&f);
}

static void EditableTextOffersSetValueAndReadOnlyTextDoesNot() {
    AccessibilityFrame f = NewAccessibilityFrame();
    InputState editable;
    InputSetValue(&editable, StrL("old"));
    El* input = InputBase::New(&f.cx, StrL("input"), true)
                    ->BindInput(&editable)
                    ->AriaValue(InputValue(&editable));
    AccessibilityCollect(input, &f.win->accessibility);
    const AccessibilityNode* node =
        RoleNode(f.win->accessibility, AccessibilityRole::TextInput);
    utassert(node && (node->actions & AccessibilityActionSetValue));
    utassert(node && base::StrEq(node->info.value, StrL("old")));
    if (node) {
#if GPUI_OS_WINDOWS
        utassert(AccessibilityWinSmokeTest(f.win, node->id));
#endif
        utassert(WindowAccessibilityPerform(
            f.win, node->id, AccessibilityAction::SetValue, StrL("new value")));
        utassert(base::StrEq(InputValue(&editable), StrL("new value")));
    }

    InputState readOnly;
    readOnly.readonly = true;
    El* locked = InputBase::New(&f.cx, StrL("locked"), true)
                     ->BindInput(&readOnly);
    AccessibilityCollect(locked, &f.win->accessibility);
    node = RoleNode(f.win->accessibility, AccessibilityRole::TextInput);
    utassert(node && !(node->actions & AccessibilityActionSetValue));
    FreeAccessibilityFrame(&f);
}

static void SelectionContainersExposeTheirSelectedItems() {
    AccessibilityFrame f = NewAccessibilityFrame();
    El* list = Div(f.arena)
                   ->Role(AccessibilityRole::ListBox)
                   ->Child(Div(f.arena)
                               ->Role(AccessibilityRole::ListBoxOption)
                               ->AriaLabel(StrL("One"))
                               ->AriaSelected(false))
                   ->Child(Div(f.arena)
                               ->Role(AccessibilityRole::ListBoxOption)
                               ->AriaLabel(StrL("Two"))
                               ->AriaSelected(true));
    AccessibilityCollect(list, &f.win->accessibility);
    const AccessibilityNode* node =
        RoleNode(f.win->accessibility, AccessibilityRole::ListBox);
    utassert(node);
#if GPUI_OS_WINDOWS
    if (node) {
        utassert(AccessibilityWinSmokeTest(f.win, node->id));
    }
#endif
    FreeAccessibilityFrame(&f);
}

void TestAccessibility() {
    TestSuite("accessibility");
    TheTreeSkipsVisualBoxesButKeepsSemanticParents();
    StyledButtonsCanReplaceTheirVisibleAccessibleName();
    AnExplicitAccessibilityLabelReplacesTheVisibleOne();
    ExplicitOnlyNamesReachTheAnnouncedControl();
    ExplicitAriaFieldsSurviveCollection();
    BaseControlsProjectTheirControlledState();
    TablesKeepCountsAndOneBasedIndices();
    SliderActionsUseTheSlidersOwnStep();
    EditableTextOffersSetValueAndReadOnlyTextDoesNot();
    SelectionContainersExposeTheirSelectedItems();
    ExplicitSemanticListenersAreInvoked();
    ConditionalAndCompositeRolesMatchUpstream();
    InputContentTypesAndSecretsProjectSafely();
}
