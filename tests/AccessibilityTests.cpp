/* Accessibility semantics ported from the role/state/action assertions in
 * crates/base and crates/ui. Most tests pin the portable tree independently
 * of an OS; Windows also probes the actual UI Automation COM projection. */

#include "Test.h"

#if GPUI_OS_WINDOWS
namespace gpui {
// Internal conformance probe implemented beside the UIA provider, not part of
// the published header surface.
bool AccessibilityWinSmokeTest(Window* win, uint32_t nodeId);
bool AccessibilityWinPartsSmokeTest(Window* win, uint32_t nodeId);
bool AccessibilityWinPropertyChangesSmokeTest();
bool AccessibilityWinTextChangesSmokeTest();
bool AccessibilityWinSelectionEventsSmokeTest();
bool AccessibilityWinDialogEventsSmokeTest();
} // namespace gpui
#endif
#if GPUI_OS_LINUX
namespace gpui {
// Drives the AT-SPI handlers over a socketpair; beside the adapter too.
bool AccessibilityLinuxPartsSmokeTest(Window* win, uint32_t nodeId);
bool AccessibilityLinuxEventSmokeTest();
} // namespace gpui
#endif
#if GPUI_OS_MAC
namespace gpui {
bool AccessibilityMacPartsSmokeTest(Window* win, uint32_t nodeId);
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
    utassert(len(f.win->accessibility) == 2);
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
#if GPUI_OS_WINDOWS
        utassert(AccessibilityWinSmokeTest(f.win, dialogNode->id));
#endif
    }
    FreeAccessibilityFrame(&f);
}

// crates/kit/tests/integration/painted_text.rs:
// labels_expose_their_text_to_assistive_technology.
static void LabelsExposeTheirTextToAssistiveTechnology() {
    AccessibilityFrame f = NewAccessibilityFrame();
    component::Init(&f.app);
    using component::Label;
    El* labels = Div(f.arena)->Id(StrL("labels"));
    auto add = [&](Label* label) {
        El* el = label->IntoEl();
        labels->Child(el);
        return el;
    };
    El* automatic = add(Label::New(&f.cx, StrL("Account details")));
    El* cpu = add(Label::New(&f.cx, StrL("CPU"))
                      ->Secondary(StrL("12%"))
                      ->Id(StrL("cpu")));
    El* secret = add(
        Label::New(&f.cx, StrL("hunter2"))->Masked(true)->Id(StrL("password")));
    El* empty = add(Label::New(&f.cx, StrL(""))->Id(StrL("empty")));
    Str rowNames[] = {StrL("Row 0"), StrL("Row 1")};
    for (int i = 0; i < 2; i++) {
        add(Label::New(&f.cx, rowNames[i])
                ->Id(ElementIdNamed(f.arena, StrL("row"), i)));
    }
    IdsCollect(labels);
    AccessibilityCollect(labels, &f.win->accessibility);
    const auto& nodes = f.win->accessibility;
    utassert(len(nodes) == 4);
    utassert(len(automatic->id) > 0 && StrEq(cpu->id, StrL("cpu")));
    if (len(nodes) == 4) {
        utassert(nodes[0].info.role == AccessibilityRole::Label);
        utassert(StrEq(nodes[0].info.value, StrL("Account details")));
        utassert(nodes[1].info.role == AccessibilityRole::Label);
        utassert(StrEq(nodes[1].info.value, StrL("CPU 12%")));
        utassert(StrEq(nodes[1].info.PlatformName(), StrL("CPU 12%")));
        utassert(nodes[2].id != nodes[3].id);
        utassert(StrEq(nodes[2].info.value, rowNames[0]));
        utassert(StrEq(nodes[3].info.value, rowNames[1]));
#if GPUI_OS_WINDOWS
        utassert(AccessibilityWinSmokeTest(f.win, nodes[1].id));
#endif
    }
    utassert(secret->accessibility.role == AccessibilityRole::None);
    utassert(empty->accessibility.role == AccessibilityRole::None);
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

// crates/kit/tests/components.rs
// clipboard_reports_default_and_explicit_accessibility_names: an icon-only
// Clipboard needs a name without a caller-provided label.
static void ClipboardReportsDefaultAndExplicitAccessibilityNames() {
    AccessibilityFrame f = NewAccessibilityFrame();
    component::Init(&f.app);

    AccessibilityCollect(component::Clipboard::New(&f.cx, StrL("copy-key"))
                             ->Value(StrL("sk-1234"))
                             ->Tooltip(StrL("Copy"))
                             ->AccessibilityLabel(StrL("Copy API key"))
                             ->IntoEl(),
                         &f.win->accessibility);
    const AccessibilityNode* named =
        RoleNode(f.win->accessibility, AccessibilityRole::Button);
    utassert(named && base::StrEq(named->info.label, StrL("Copy API key")));

    AccessibilityCollect(component::Clipboard::New(&f.cx, StrL("copy-plain"))
                             ->Value(StrL("sk-1234"))
                             ->Tooltip(StrL("Copy"))
                             ->IntoEl(),
                         &f.win->accessibility);
    const AccessibilityNode* plain =
        RoleNode(f.win->accessibility, AccessibilityRole::Button);
    utassert(plain && base::StrEq(plain->info.label, StrL("Copy")));
    FreeAccessibilityFrame(&f);
}

// checkbox.rs / color_picker.rs / radio.rs / switch.rs, mod tests:
// an_explicit_accessibility_label_replaces_the_visible_one. The explicit name
// wins over the visible label, and what is drawn does not change with it.
// clipboard.rs test_clipboard_builder: Button derives the name from its
// visible label until an explicit accessible name is supplied.
static void ClipboardUsesItsVisibleLabelAsTheAccessibleName() {
    AccessibilityFrame f = NewAccessibilityFrame();
    component::Init(&f.app);
    Str label = StrL("Copy value"), name = StrL("Copy current value");
    auto* clipboard = component::Clipboard::New(&f.cx, StrL("copy"))
                          ->Value(StrL("Example"))
                          ->Label(label);
    utassert(StrEq(clipboard->label, label));
    utassert(!clipboard->accessibilityLabel.s);
    AccessibilityCollect(clipboard->IntoEl(), &f.win->accessibility);
    auto* button = RoleNode(f.win->accessibility, AccessibilityRole::Button);
    utassert(button && StrEq(button->info.label, label));
    clipboard->AccessibilityLabel(name);
    utassert(StrEq(clipboard->label, label));
    VecClear(f.win->accessibility);
    AccessibilityCollect(clipboard->IntoEl(), &f.win->accessibility);
    button = RoleNode(f.win->accessibility, AccessibilityRole::Button);
    utassert(button && StrEq(button->info.label, name));
    FreeAccessibilityFrame(&f);
}

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
                   ->AriaDescription(StrL("described"))
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
    utassert(len(f.win->accessibility) == 1);
    if (len(f.win->accessibility) == 1) {
        const AccessibilityInfo& a = f.win->accessibility[0].info;
        utassert(base::StrEq(a.authorId, StrL("heading.main")));
        utassert(base::StrEq(a.label, StrL("Explicit")));
        utassert(base::StrEq(a.description, StrL("described")));
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
#if GPUI_OS_WINDOWS
        // UIA reads it as FullDescription, the way accesskit_windows maps a
        // node's description.
        utassert(AccessibilityWinSmokeTest(f.win, f.win->accessibility[0].id));
#endif
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
    utassert(len(f.win->accessibility) == 7);
    FreeAccessibilityFrame(&f);
}

// kit tests/integration/input.rs
// mask_toggle_is_named_for_the_action_it_takes. The eye button is named for
// what the next click does.
static void MaskToggleIsNamedForTheActionItTakes() {
    AccessibilityFrame f = NewAccessibilityFrame();
    component::Init(&f.app);
    InputState state;
    El* masked = component::Input::New(&f.cx, StrL("secret"), &state)
                     ->Masked(true)
                     ->MaskToggle()
                     ->IntoEl();
    IdsCollect(masked);
    AccessibilityCollect(masked, &f.win->accessibility);
    const AccessibilityNode* show = nullptr;
    for (int i = 0; i < len(f.win->accessibility); i++) {
        const AccessibilityNode* node = &f.win->accessibility[i];
        if (node->info.role == AccessibilityRole::Button &&
            base::StrEq(node->info.label, StrL("Show password"))) {
            show = node;
        }
    }
    utassert(show != nullptr);
    VecClear(f.win->accessibility);
    El* revealed = component::Input::New(&f.cx, StrL("secret"), &state)
                       ->Masked(false)
                       ->MaskToggle()
                       ->IntoEl();
    IdsCollect(revealed);
    AccessibilityCollect(revealed, &f.win->accessibility);
    const AccessibilityNode* hide = nullptr;
    for (int i = 0; i < len(f.win->accessibility); i++) {
        const AccessibilityNode* node = &f.win->accessibility[i];
        if (node->info.role == AccessibilityRole::Button &&
            base::StrEq(node->info.label, StrL("Hide password"))) {
            hide = node;
        }
    }
    utassert(hide != nullptr);
    FreeAccessibilityFrame(&f);
}

// kit tests/integration/input.rs masked_input_is_a_password_field. A mask
// with no content type is a password field. A plain field stays a text field.
static void MaskedInputIsAPasswordField() {
    AccessibilityFrame f = NewAccessibilityFrame();
    component::Init(&f.app);
    InputState secret;
    InputState plain;
    El* root = Div(f.arena)
                   ->Child(component::Input::New(&f.cx, StrL("secret"), &secret)
                               ->Masked(true)
                               ->IntoEl())
                   ->Child(component::Input::New(&f.cx, StrL("first"), &plain)
                               ->IntoEl());
    IdsCollect(root);
    AccessibilityCollect(root, &f.win->accessibility);
    utassert(RoleNode(f.win->accessibility, AccessibilityRole::PasswordInput));
    utassert(RoleNode(f.win->accessibility, AccessibilityRole::TextInput));
    FreeAccessibilityFrame(&f);
}

// kit tests/integration/input.rs clear_button_is_named. The clear button is
// icon-only, so its accessible name is the localized Input.Clear string.
static void ClearButtonIsNamed() {
    AccessibilityFrame f = NewAccessibilityFrame();
    component::Init(&f.app);
    InputState state;
    InputSetValue(&state, StrL("query"));
    El* field = component::Input::New(&f.cx, StrL("query"), &state)
                    ->Cleanable()
                    ->W(240)
                    ->IntoEl();
    IdsCollect(field);
    AccessibilityCollect(field, &f.win->accessibility);
    const AccessibilityNode* button = nullptr;
    for (int i = 0; i < len(f.win->accessibility); i++) {
        const AccessibilityNode* node = &f.win->accessibility[i];
        if (node->info.role == AccessibilityRole::Button &&
            base::StrEq(node->info.label, StrL("Clear"))) {
            button = node;
        }
    }
    utassert(button != nullptr);
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
    utassert(node &&
             StrEq(WindowAccessibilityTextValue(*node), "ada@example.test"));

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
    utassert(node && !WindowAccessibilityTextValue(*node).s);
    FreeAccessibilityFrame(&f);
}

struct AccessibleSingleLine {
    InputState input;
    bool masked = false;

    static El* Render(AccessibleSingleLine* self, Ctx* cx) {
        return component::Input::New(cx, StrL("single-line"), &self->input)
            ->Role(AccessibilityRole::TextInput)
            ->Masked(self->masked)
            ->W(360)
            ->IntoEl();
    }
};

static void SingleLineRangesUseTheCurrentFrameGeometry() {
    App* app = TestAppNew();
    component::Init(app);
    auto view = EntityNew<AccessibleSingleLine>(app);
    auto* state = view.Get(app);
    Window* win = TestWindowOpen(app, view, 400, 80);
    TestDraw(win);
    const AccessibilityNode* node =
        RoleNode(win->accessibility, AccessibilityRole::TextInput);
    utassert(node && WindowAccessibilityTextValue(*node).s &&
             len(WindowAccessibilityTextValue(*node)) == 0);

    InputSetValue(&state->input, StrL("Áé CD"));
    TestDraw(win);
    node = RoleNode(win->accessibility, AccessibilityRole::TextInput);
    utassert(node != nullptr);
    if (node) {
        Bounds first = {}, last = {};
        utassert(
            WindowAccessibilityTextRangeBounds(win, *node, {0, 3}, &first));
        utassert(WindowAccessibilityTextRangeBounds(win, *node, {6, 8}, &last));
        utassert(first.w > 0 && first.h > 0 && last.x > first.Right());
#if GPUI_OS_WINDOWS
        utassert(AccessibilityWinSmokeTest(win, node->id));
#endif
        InputSetValue(&state->input, StrL("Éé AB"));
        utassert(StrEq(WindowAccessibilityTextValue(*node), "Éé AB"));
        utassert(StrEq(node->info.value, "Áé CD"));
        utassert(
            !WindowAccessibilityTextRangeBounds(win, *node, {0, 1}, &first));
    }

    state->masked = true;
    TestDraw(win);
    node = RoleNode(win->accessibility, AccessibilityRole::TextInput);
    utassert(node && !WindowAccessibilityTextValue(*node).s);
    TestAppFree(app);
}

// input/base/a11y_text.rs: logical lines include their break, a final break
// leaves a caret line, and the native range owns the original document text.
static void MultilineAccessibilityKeepsLineBreaksAndSelection() {
    Str text = StrL("ab\ncd\r\né\n");
    const Selection lines[] = {{0, 3}, {3, 7}, {7, 10}, {10, 10}};
    for (int i = 0; i < 4; ++i) {
        Selection range = {};
        utassert(AccessibilityTextLineRange(text, i, &range));
        utassert(range.start == lines[i].start && range.end == lines[i].end);
        Selection at = AccessibilityTextLineAt(text, lines[i].start);
        utassert(at.start == range.start && at.end == range.end);
    }
    Selection missing;
    utassert(!AccessibilityTextLineRange(text, 4, &missing));
    Selection empty = AccessibilityTextLineAt(StrL(""), 0);
    utassert(empty.start == 0 && empty.end == 0);
    Selection newline = AccessibilityTextLineAt(text, 6);
    utassert(newline.start == 3 && newline.end == 7);

    AccessibilityFrame f = NewAccessibilityFrame();
    component::Init(&f.app);
    InputState input;
    input.kind = InputKind::Textarea;
    InputSetValue(&input, StrL("one\ntwó\n"));
    InputSetSelectedRangeWithDirection(&input, &f.app, f.win, 1, 6, true);
    El* field = component::Textarea::New(&f.cx, StrL("multiline"), &input)
                    ->Rows(3)
                    ->IntoEl();
    AccessibilityCollect(field, &f.win->accessibility);
    const AccessibilityNode* node =
        RoleNode(f.win->accessibility, AccessibilityRole::MultilineTextInput);
    utassert(node != nullptr);
    if (node) {
        utassert(StrEq(WindowAccessibilityTextValue(*node), "one\ntwó\n"));
        utassert(InputCursor(node->input) == 1);
        utassert(node->input->selectedRange.end == 6);
#if GPUI_OS_WINDOWS
        utassert(AccessibilityWinSmokeTest(f.win, node->id));
#endif
    }
    FreeAccessibilityFrame(&f);
}

// input/base/a11y_text.rs a_large_document_is_navigable_from_start_to_end.
static void ALargeDocumentIsNavigableFromStartToEnd() {
    AccessibilityFrame f = NewAccessibilityFrame();
    component::Init(&f.app);
    InputState input;
    input.kind = InputKind::Textarea;
    StrBuilder text(f.cx.a);
    int middle = 0;
    int last = 0;
    for (int row = 0; row < 20000; row++) {
        if (row == 10000) {
            middle = len(text);
        }
        if (row == 19999) {
            last = len(text);
        }
        text.Append(fmt("line %d\n", row));
    }
    Str source = text.TakeStr();
    InputSetValue(&input, source);
    InputSetSelectedRangeWithDirection(&input, &f.app, f.win, middle, middle,
                                       false);
    El* field = component::Textarea::New(&f.cx, StrL("multiline"), &input)
                    ->Rows(3)
                    ->IntoEl();
    AccessibilityCollect(field, &f.win->accessibility);
    const AccessibilityNode* node =
        RoleNode(f.win->accessibility, AccessibilityRole::MultilineTextInput);
    utassert(node != nullptr);
    if (node) {
        Str value = WindowAccessibilityTextValue(*node);
        utassert(StrEq(value, source));
        utassert(WindowAccessibilityTextCaret(*node) == middle);
        Selection first = AccessibilityTextLineAt(value, 0);
        Selection focus = AccessibilityTextLineAt(value, middle);
        Selection end = AccessibilityTextLineAt(value, last);
        Selection tail = AccessibilityTextLineAt(value, len(value));
        utassert(first.start == 0 && first.end == 7);
        utassert(focus.start == middle && focus.end == middle + 11);
        utassert(end.start == last && end.end == len(value));
        utassert(tail.start == len(value) && tail.end == len(value));
        InputSetValue(&input, StrL("replacement"));
        utassert(StrEq(node->info.value, source));
    }
    FreeAccessibilityFrame(&f);
}

// a11y_text.rs bounded_excerpt_preserves_unicode_and_clips_reversed_selection
// and state.rs accessibility_set_value_cannot_replace_a_bounded_document.
static void ALongDocumentExposesOnlyTheCaretExcerpt() {
    AccessibilityFrame f = NewAccessibilityFrame();
    component::Init(&f.app);
    InputState input;
    input.kind = InputKind::Textarea;
    auto collect = [&]() {
        El* field = component::Textarea::New(&f.cx, StrL("bounded"), &input)
                        ->Rows(3)
                        ->IntoEl();
        AccessibilityCollect(field, &f.win->accessibility);
        return RoleNode(f.win->accessibility,
                        AccessibilityRole::MultilineTextInput);
    };

    InputSetValue(&input, StrL("before"));
    const AccessibilityNode* old = collect();
    utassert(old && (old->actions & AccessibilityActionSetValue));
    StrBuilder document(f.cx.a);
    for (int i = 0; i < 3000; i++) document.Append(StrL("?"));
    document.Append(StrL("here"));
    Str cat = StrL("\xe7\x8c\xab");
    while (len(document) <= kFullAccessibilityTextLimit) {
        document.Append(cat);
    }
    Str source = document.TakeStr();
    InputSetValue(&input, source);
    if (old) {
        utassert(!WindowAccessibilityPerform(f.win, old->id,
                                             AccessibilityAction::SetValue,
                                             StrL("replacement")));
        utassert(StrEq(InputValue(&input), source));
    }
    InputSetSelectedRangeWithDirection(&input, &f.app, f.win, 3000, len(source),
                                       true);
    const AccessibilityNode* node = collect();
    utassert(node && !node->info.value.s &&
             !(node->actions & AccessibilityActionSetValue));
    if (node) {
        Str excerpt = WindowAccessibilityTextValue(*node);
        utassert(len(excerpt) <= 4096 && StrEndsWith(excerpt, cat));
        Selection selection = {};
        utassert(WindowAccessibilityTextSelection(*node, &selection));
        int caret = WindowAccessibilityTextCaret(*node);
        utassert(selection.start == caret && selection.end == len(excerpt));
        utassert(caret >= 0 && caret + 4 <= len(excerpt) &&
                 StrEq(Str(excerpt.s + caret, 4), StrL("here")));
        utassert(WindowAccessibilitySetTextSelection(f.win, *node,
                                                     {caret + 1, caret + 3}));
        utassert(input.selectedRange.start == 3001 && input.selectedRange
                                                              .end == 3003);
    }
    FreeAccessibilityFrame(&f);
}

// crates/base/src/a11y_disabled.rs
// a_disabled_control_marks_its_node_disabled. The wrapper there only sets
// the flag GPUI's element had no setter for. El::AriaDisabled is that
// setter, and a disabled slider already drops Increment with it.
static void ADisabledControlMarksItsNodeDisabled() {
    AccessibilityFrame f = NewAccessibilityFrame();
    component::Init(&f.app);
    SliderState slider = SliderStateNew(0, 10, SliderSingle(4), 1);
    Entity<TimeFieldState> time =
        TimeFieldStateNew(&f.cx, TimePrecision::Minute, HourCycle::H23);

    struct Case {
        AccessibilityRole role;
        bool slider;
        El* el;
    };
    for (int pass = 0; pass < 2; pass++) {
        bool disabled = pass == 1;
        Case cases[] = {
            {AccessibilityRole::Button, false,
             Button::New(&f.cx, StrL("button"), disabled)},
            {AccessibilityRole::Link, false,
             Link::New(&f.cx, StrL("link"), disabled)},
            {AccessibilityRole::Switch, false,
             Switch::New(&f.cx, StrL("switch"), false, disabled)},
            {AccessibilityRole::RadioButton, false,
             Radio::New(&f.cx, StrL("radio"), false, disabled)},
            {AccessibilityRole::Button, false,
             Toggle::New(&f.cx, StrL("toggle"), false, disabled)},
            {AccessibilityRole::CheckBox, false,
             Checkbox::New(&f.cx, StrL("checkbox"), CheckboxState::Unchecked,
                           disabled)},
            {AccessibilityRole::ComboBox, false,
             Select::New(&f.cx, StrL("select"), false, disabled)},
            {AccessibilityRole::Tab, false,
             Tab::New(&f.cx, StrL("tab"), disabled)},
            {AccessibilityRole::ComboBox, false,
             DatePicker::New(&f.cx, StrL("date-picker"), disabled)},
            {AccessibilityRole::TimeInput, false,
             TimeField::New(&f.cx, StrL("time-field"), time)
                 ->Disabled(disabled)
                 ->IntoEl()},
            {AccessibilityRole::TextInput, false,
             InputBase::New(&f.cx, StrL("input"), !disabled)},
            {AccessibilityRole::Slider, true,
             component::Slider::New(&f.cx, StrL("slider"), &slider)
                 ->Disabled(disabled)
                 ->IntoEl()},
        };
        for (int i = 0; i < (int)(sizeof(cases) / sizeof(cases[0])); i++) {
            AccessibilityCollect(cases[i].el, &f.win->accessibility);
            const AccessibilityNode* node =
                RoleNode(f.win->accessibility, cases[i].role);
            utassert(node);
            if (!node) {
                continue;
            }
            utassert((node->info.disabled != 0) == disabled);
            if (cases[i].slider) {
                utassert(((node->actions & AccessibilityActionIncrement) !=
                          0) != disabled);
            }
        }
    }
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
        utassertnear(state.value.End(), 8);
        utassert(WindowAccessibilityPerform(f.win, id,
                                            AccessibilityAction::Decrement));
        utassertnear(state.value.End(), 6);
        utassert(WindowAccessibilitySetNumericValue(f.win, id, 8.6f));
        utassertnear(state.value.End(), 8);
        utassert(WindowAccessibilitySetNumericValue(f.win, id, 100));
        utassertnear(state.value.End(), 10);
    }
    FreeAccessibilityFrame(&f);
}

// crates/kit/tests/integration/slider_steps.rs
// slider_accessibility_labels_follow_the_existing_nodes. The kit query is
// window.find(("slider", entity_id)); here the two nodes are the base
// slider and the component slider, in that order, on one collected tree.
static int SliderRoleCount(const Vec<AccessibilityNode>& nodes) {
    int n = 0;
    for (int i = 0; i < len(nodes); i++) {
        if (nodes[i].info.role == AccessibilityRole::Slider) {
            n++;
        }
    }
    return n;
}

static void ExpectSliderLabel(const AccessibilityNode* node, Str label) {
    utassert(node && node->info.role == AccessibilityRole::Slider);
    if (!node) {
        return;
    }
    if (label) {
        utassert(base::StrEq(node->info.label, label));
    } else {
        utassert(!node->info.label);
    }
}

static void SliderAccessibilityLabelsFollowTheExistingNodes() {
    AccessibilityFrame f = NewAccessibilityFrame();
    component::Init(&f.app);
    SliderState base = SliderStateNew(0, 100, SliderSingle(20), 1);
    SliderState themed = SliderStateNew(0, 100, SliderSingle(40), 1);
    struct LabelPair {
        Str base;
        Str themed;
    };
    LabelPair cases[4] = {
        {},
        {StrL("Corner radius"), StrL("Padding scale")},
        {StrL("圆角"), StrL("内边距")},
        {},
    };
    for (int i = 0; i < 4; i++) {
        El* column = Div(f.arena)->FlexCol()->Pad(8)->W(264);
        column->Child(
            Slider::New(&f.cx, &base, Axis::Horizontal, 0, cases[i].base)
                ->W(kFill)
                ->H(24));
        component::Slider* widget =
            component::Slider::New(&f.cx, StrL("padding-scale"), &themed);
        if (cases[i].themed) {
            widget->AccessibilityLabel(cases[i].themed);
        }
        column->Child(widget->IntoEl());
        AccessibilityCollect(column, &f.win->accessibility);
        utassert(SliderRoleCount(f.win->accessibility) == 2);
        ExpectSliderLabel(
            RoleNode(f.win->accessibility, AccessibilityRole::Slider, 0),
            cases[i].base);
        ExpectSliderLabel(
            RoleNode(f.win->accessibility, AccessibilityRole::Slider, 1),
            cases[i].themed);
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

    // input.rs editable_input_offers_accessibility_write_action (#3246,
    // restored by #3254): the action rechecks the field when it runs, so one
    // made read-only or disabled after its node was collected is neither
    // written nor focused. SetValue keeps a non-ASCII value whole, is one undo
    // step, and writes a masked field too.
    IdsCollect(input);
    AccessibilityCollect(input, &f.win->accessibility);
    node = RoleNode(f.win->accessibility, AccessibilityRole::TextInput);
    utassert(node && (node->actions & AccessibilityActionFocus));
    if (node) {
        uint32_t id = node->id;
        int focusId = node->focusId;
        utassert(
            WindowAccessibilityPerform(f.win, id, AccessibilityAction::Focus));
        utassert(focusId != 0 && f.win->focusId == focusId);
        f.win->focusId = 0;
        utassert(WindowAccessibilityPerform(
            f.win, id, AccessibilityAction::SetValue, StrL("updated🦀")));
        utassert(base::StrEq(InputValue(&editable), StrL("updated🦀")));
        for (bool disabled : {false, true}) {
            editable.disabled = disabled;
            editable.readonly = !disabled;
            utassert(WindowAccessibilityPerform(
                         f.win, id, AccessibilityAction::Focus) == !disabled);
            utassert((f.win->focusId == focusId) == !disabled);
            f.win->focusId = 0;
            utassert(!WindowAccessibilityPerform(
                f.win, id, AccessibilityAction::SetValue, StrL("rejected")));
            utassert(base::StrEq(InputValue(&editable), StrL("updated🦀")));
        }
        editable.disabled = false;
        editable.readonly = false;
        InputPerform(&editable, nullptr, nullptr, InputAction::Undo, false);
        utassert(base::StrEq(InputValue(&editable), StrL("new value")));
        editable.masked = true;
        utassert(WindowAccessibilityPerform(
            f.win, id, AccessibilityAction::SetValue, StrL("updated🦀")));
        utassert(base::StrEq(InputValue(&editable), StrL("updated🦀")));
        editable.masked = false;
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

static const AccessibilityNode* SetValueNode(
    const Vec<AccessibilityNode>& nodes) {
    for (int i = 0; i < len(nodes); i++) {
        if (nodes[i].actions & AccessibilityActionSetValue) return &nodes[i];
    }
    return nullptr;
}

// input.rs accessibility_set_value_preserves_exact_editor_text: SetValue is
// the programmatic replace, not typing, so a Rust editor given "(" keeps
// exactly "(" rather than auto-closing it.
static void SetValuePreservesExactEditorText() {
    AccessibilityFrame f = NewAccessibilityFrame();
    component::Init(&f.app);
    InputState state;
    El* editor = component::Editor::New(&f.cx, StrL("editor"), &state)
                     ->Language(StrL("rust"))
                     ->IntoEl();
    IdsCollect(editor);
    AccessibilityCollect(editor, &f.win->accessibility);
    const AccessibilityNode* node = SetValueNode(f.win->accessibility);
    utassert(node != nullptr);
    if (node) {
        utassert(WindowAccessibilityPerform(
            f.win, node->id, AccessibilityAction::SetValue, StrL("(")));
        utassert(base::StrEq(InputValue(&state), StrL("(")));
    }
    FreeAccessibilityFrame(&f);
    AppGlobalClear(&f.app);
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

// crates/kit/tests/disclosure.rs:
// accordion_preserves_disabled_items_when_the_group_is_enabled. An item's own
// disabled flag holds inside an enabled group, and re-enabling the group after
// disabling it leaves that item disabled. The Rust clicks the triggers; here
// the trigger's projected disabled state and its missing click are the seam.
struct AccordionToggleView {
    static El* Render(AccordionToggleView*, Ctx* cx) { return Div(cx->a); }
    static void Toggle(AccordionToggleView*, Ctx*, const ClickEvent*, int64_t) {
    }
};

static void AccordionPreservesDisabledItemsWhenTheGroupIsEnabled() {
    const bool groupDisabled[] = {false, true, false};
    for (bool group : groupDisabled) {
        AccessibilityFrame f = NewAccessibilityFrame();
        component::Init(&f.app);
        Entity<AccordionToggleView> view =
            EntityNew<AccordionToggleView>(&f.app);
        f.cx.self = view.id;
        El* root = component::Accordion::New(&f.cx, StrL("sections"))
                       ->Disabled(group)
                       ->OnToggle(Listen(&f.cx, &AccordionToggleView::Toggle))
                       ->Item(component::AccordionItem::New(&f.cx)
                                  ->Title(StrL("General")))
                       ->Item(component::AccordionItem::New(&f.cx)
                                  ->Title(StrL("Advanced"))
                                  ->Disabled(true))
                       ->IntoEl();
        AccessibilityCollect(root, &f.win->accessibility);
        const AccessibilityNode* general =
            RoleNode(f.win->accessibility, AccessibilityRole::Button, 0);
        const AccessibilityNode* advanced =
            RoleNode(f.win->accessibility, AccessibilityRole::Button, 1);
        utassert(general && advanced);
        if (general && advanced) {
            utassert(advanced->info.disabled);
            utassert((advanced->actions & AccessibilityActionDefault) == 0);
            utassert((general->info.disabled != 0) == group);
            utassert(((general->actions & AccessibilityActionDefault) != 0) ==
                     !group);
        }
        FreeAccessibilityFrame(&f);
    }
}

static bool TreeHolds(const El* root, const El* want) {
    for (const El* c = root ? root->first : nullptr; c; c = c->next) {
        if (c == want || TreeHolds(c, want)) {
            return true;
        }
    }
    return false;
}

// accordion.rs settled_closed_panel_unmounts_its_content: a closed panel
// whose reveal has settled leaves its content out of the tree, and a closing
// one keeps it until the spring settles. The Rust asks for the content's
// debug bounds; here the content element is looked for in the built tree,
// one 16 ms frame at a time.
static void ASettledClosedPanelUnmountsItsContent() {
    AccessibilityFrame f = NewAccessibilityFrame();
    component::Init(&f.app);
    MotionResetReduceForTest();
    double now = 1000.0;
    auto frame = [&](bool open) {
        f.win->frameNow = now;
        now += 0.016;
        El* content = Div(f.cx.a)->H(60);
        El* root = component::Accordion::New(&f.cx, StrL("accordion-toggle"))
                       ->Item(component::AccordionItem::New(&f.cx)
                                  ->Open(open)
                                  ->Title(StrL("Item"))
                                  ->Child(content))
                       ->Item(component::AccordionItem::New(&f.cx)
                                  ->Title(StrL("Next")))
                       ->IntoEl();
        return TreeHolds(root, content);
    };

    utassert(!frame(false));
    utassert(frame(true));
    for (int i = 0; i < 100; i++) {
        utassert(frame(true));
    }

    // The closing reveal keeps the content until the spring settles.
    int closingFrames = 0;
    while (frame(false)) {
        closingFrames++;
        if (closingFrames >= 100) {
            break;
        }
    }
    utassert(closingFrames < 100); // "closed content stayed mounted"
    utassert(closingFrames > 1);   // "content unmounted before the reveal ran"
    FreeAccessibilityFrame(&f);
}

struct MixedCheckboxView {
    IconName icon = IconName::None;
    int64_t requested = -1;

    static void OnChange(MixedCheckboxView* self, Ctx*, const ClickEvent*,
                         int64_t value) {
        self->requested = value;
    }

    static El* Render(MixedCheckboxView* self, Ctx* cx) {
        El* box = component::Checkbox::New(cx, StrL("mixed"))
                      ->Label(StrL("All files"))
                      ->Checked(true)
                      ->Indeterminate(true)
                      ->OnChange(Listen(cx, &OnChange))
                      ->IntoEl();
        self->icon = box->first && box->first->first ? box->first->first->icon
                                                     : IconName::None;
        return box;
    }
};

static void AMixedCheckboxUsesTheDashAndRequestsChecked() {
    App* app = TestAppNew();
    component::Init(app);
    auto view = EntityNew<MixedCheckboxView>(app);
    Window* win = TestWindowOpen(app, view, 240, 120, 1);
    const auto* node =
        RoleNode(win->accessibility, AccessibilityRole::CheckBox);
    utassert(node && node->info.toggled == AccessibilityToggled::Mixed);
    utassert(view.Get(app)->icon == IconName::Minus);
    if (node) {
        Bounds b = node->bounds;
        TestSimulateClick(win, {b.CenterX(), b.CenterY()});
        utassert(view.Get(app)->requested == 1);
    }
    TestAppFree(app);
}

static void ButtonExposesOptionalDisclosureState() {
    AccessibilityFrame f = NewAccessibilityFrame();
    component::Init(&f.app);
    for (int pass = 0; pass < 3; pass++) {
        auto* button = component::Button::New(&f.cx, StrL("details"))
                           ->Label(StrL("Details"))
                           ->Open(pass != 2);
        if (pass > 0) button->AriaExpanded(pass == 2);
        AccessibilityCollect(button->IntoEl(), &f.win->accessibility);
        const AccessibilityNode* node =
            RoleNode(f.win->accessibility, AccessibilityRole::Button);
        utassert(node && (node->info.hasExpanded != 0) == (pass > 0));
        if (node && pass > 0) {
            utassert((node->info.expanded != 0) == (pass == 2));
        }
    }
    FreeAccessibilityFrame(&f);
}

// text/state.rs accessibility_text_follows_the_rendered_text and the
// a11y_text integration test for the TextView document.
static void TextViewPublishesItsRenderedBodyToAccessibility() {
    struct View {
        Entity<TextViewState> text;
        static El* Render(View* self, Ctx* cx) {
            return Div(cx->a)
                ->FlexCol()
                ->W(300)
                ->Child(TextEl(cx->a, StrL("Outside label"))
                            ->Role(AccessibilityRole::Label))
                ->Child(TextView::New(cx, self->text)->Selectable()->IntoEl());
        }
    };
    App* app = TestAppNew();
    component::Init(app);
    auto view = EntityNew<View>(app);
    View* self = view.Get(app);
    self->text = TextViewState::Markdown(app, StrL("Hello **world**"));
    TextViewState* state = self->text.Get(app);
    Window* win = TestWindowOpen(app, view, 420, 240, 1);
    auto document = [&]() {
        return RoleNode(win->accessibility, AccessibilityRole::Document);
    };
    auto checkText = [&](Str expected) {
        const AccessibilityNode* node = document();
        utassert(node &&
                 StrEq(StrTrim(WindowAccessibilityTextValue(*node)), expected));
    };
    checkText(StrL("Hello world"));
    const AccessibilityNode* run =
        RoleNode(win->accessibility, AccessibilityRole::TextRun);
    utassert(run && StrEq(run->info.value, StrL("Hello world")));
    utassert(run && run->parent >= 0 &&
             &win->accessibility[run->parent] == document());
    utassert(RoleNode(win->accessibility, AccessibilityRole::Label));
    Selection selected;
    const AccessibilityNode* first = document();
    utassert(first && !WindowAccessibilityTextSelection(*first, &selected));
    const Str* cached = state->AccessibilityText(win).lines;
    TestDraw(win);
    utassert(state->AccessibilityText(win).lines == cached);

    state->PushStr(StrL("\n\n- item"), app, win);
    TestRunUntilParked(app);
    TestDraw(win);
    checkText(StrL("Hello world\nitem"));
    state->SetText(StrL("Other"), app, win);
    TestRunUntilParked(app);
    TestDraw(win);
    checkText(StrL("Other"));

    state->SelectAll(win, app);
    TestDraw(win);
    const AccessibilityNode* last = document();
    utassert(last && WindowAccessibilityTextSelection(*last, &selected));
    utassert(last && selected.start == 0 &&
             selected.end == len(WindowAccessibilityTextValue(*last)));
    utassert(last && !(last->actions & AccessibilityActionSetValue));
    TestAppFree(app);
}

// Assistive technology reads the tree between frames, after the app may have
// replaced the strings an element borrowed from its state.
static void TheTreeOutlivesTheStringsItWasBuiltFrom() {
    AccessibilityFrame f = NewAccessibilityFrame();
    component::Init(&f.app);
    InputState state;
    InputSetPlaceholder(&state, StrL("Search"));
    El* field =
        component::Input::New(&f.cx, StrL("query"), &state)->W(240)->IntoEl();
    IdsCollect(field);
    AccessibilityCollect(field, &f.win->accessibility);
    const AccessibilityNode* node =
        RoleNode(f.win->accessibility, AccessibilityRole::TextInput);
    utassert(node != nullptr);
    const char* own = state.placeholder.s;
    utassert(node->info.placeholder.s != own);
    utassert(node->info.label.s != own);
    InputSetPlaceholder(&state, StrL("Find"));
    utassert(base::StrEq(node->info.placeholder, StrL("Search")));
    utassert(base::StrEq(node->info.label, StrL("Search")));
    InputSetPlaceholder(&state, {});
    FreeAccessibilityFrame(&f);
}

// Local, no Rust counterpart: a document source is read only when asked.
struct SourcedDocument {
    Str text;
    int reads = 0;
    int selectionStart = -1;
    int selectionEnd = -1;
};

static Str SourcedDocumentText(void* data) {
    auto* doc = (SourcedDocument*)data;
    doc->reads++;
    return doc->text;
}

static bool SourcedDocumentSelection(void* data, int* start, int* end) {
    auto* doc = (SourcedDocument*)data;
    *start = doc->selectionStart;
    *end = doc->selectionEnd;
    return doc->selectionStart >= 0;
}

static void ADocumentSourceIsReadOnDemand() {
    AccessibilityFrame f = NewAccessibilityFrame();
    SourcedDocument doc;
    doc.text = StrL("Hello world");
    AccessibilityDocumentSource source;
    source.text = &SourcedDocumentText;
    source.selection = &SourcedDocumentSelection;
    source.data = &doc;
    source.selectionRevision = 7;
    El* e = Div(f.arena)
                ->W(100)
                ->H(100)
                ->AriaLabel(StrL("doc.pdf"))
                ->AccessibilityDocument(source);
    AccessibilityCollect(e, &f.win->accessibility);
    const AccessibilityNode* node =
        RoleNode(f.win->accessibility, AccessibilityRole::Document);
    utassert(node && node->document.selectionRevision == 7);
    utassert(node && WindowAccessibilityHasText(*node) && doc.reads == 0);
    utassert(node && StrEq(node->info.label, StrL("doc.pdf")));
    utassert(node && StrEq(WindowAccessibilityTextValue(*node), doc.text));
    utassert(doc.reads == 1);
    Selection selected;
    utassert(node && !WindowAccessibilityTextSelection(*node, &selected));
    doc.selectionStart = 6;
    doc.selectionEnd = 11;
    utassert(node && WindowAccessibilityTextSelection(*node, &selected));
    utassert(selected.start == 6 && selected.end == 11);
    utassert(node && WindowAccessibilityTextCaret(*node) == 11);
    FreeAccessibilityFrame(&f);
}

static void DocumentRevisionsTrackTextAndSelectionWithoutReadingTheSource() {
    struct View {
        SourcedDocument doc;
        uint64_t textRevision = 0;
        uint64_t selectionRevision = 0;
        bool hasSource = true;
        static El* Render(View* self, Ctx* cx) {
            AccessibilityDocumentSource source;
            source.text = self->hasSource ? &SourcedDocumentText : nullptr;
            source.selection = &SourcedDocumentSelection;
            source.data = &self->doc;
            source.textRevision = self->textRevision;
            source.selectionRevision = self->selectionRevision;
            return Div(cx->a)
                ->Role(AccessibilityRole::Document)
                ->AccessibilityDocument(source);
        }
    };
    App* app = TestAppNew();
    auto view = EntityNew<View>(app);
    View* self = view.Get(app);
    Window* win = TestWindowOpen(app, view, 240, 140);
    auto checkFlags = [&](bool text, bool selection) {
        const auto* node =
            RoleNode(win->accessibility, AccessibilityRole::Document);
        utassert(node && node->documentTextChanged == text &&
                 node->documentSelectionChanged == selection);
        utassert(self->doc.reads == 0);
    };
    checkFlags(false, false);
    self->textRevision++;
    TestDraw(win);
    checkFlags(true, false);
    self->selectionRevision++;
    TestDraw(win);
    checkFlags(false, true);
    TestDraw(win);
    checkFlags(false, false);
    self->textRevision++;
    self->selectionRevision++;
    TestDraw(win);
    checkFlags(true, true);
    self->hasSource = false;
    TestDraw(win);
    self->hasSource = true;
    self->textRevision++;
    TestDraw(win);
    checkFlags(false, false);
    TestAppFree(app);
}

// Local, no Rust counterpart: a document's pages are parts enumerated on
// demand, not elements collected each frame.
struct PagedDocument {
    Str pages[3] = {StrL("one"), StrL("two"), StrL("three")};
    Str text = StrL("one\ntwo\nthree");
    int starts[3] = {0, 4, 8};
    int partReads = 0;
};

static Str PagedDocumentText(void* data) {
    return ((PagedDocument*)data)->text;
}

static int PagedDocumentPartCount(void*) {
    return 3;
}

static Str PagedDocumentPartName(void*, int part) {
    static const Str kNames[3] = {StrL("Page 1"), StrL("Page 2"),
                                  StrL("Page 3")};
    return kNames[part];
}

static Str PagedDocumentPartText(void* data, int part) {
    auto* doc = (PagedDocument*)data;
    doc->partReads++;
    return doc->pages[part];
}

static bool PagedDocumentPartBounds(void*, int part, Bounds* out) {
    *out = {10, 10 + 100.f * (float)part, 80, 90};
    return true;
}

static bool PagedDocumentPartRange(void* data, int part, int* start, int* end) {
    auto* doc = (PagedDocument*)data;
    *start = doc->starts[part];
    *end = *start + len(doc->pages[part]);
    return true;
}

static void ADocumentSourceEnumeratesItsPartsOnDemand() {
    AccessibilityFrame f = NewAccessibilityFrame();
    PagedDocument doc;
    AccessibilityDocumentSource source;
    source.text = &PagedDocumentText;
    source.data = &doc;
    source.partCount = &PagedDocumentPartCount;
    source.partName = &PagedDocumentPartName;
    source.partText = &PagedDocumentPartText;
    source.partBounds = &PagedDocumentPartBounds;
    source.partRange = &PagedDocumentPartRange;
    El* overlay =
        Div(f.arena)->Role(AccessibilityRole::Button)->AriaLabel(StrL("x"));
    El* e = Div(f.arena)
                ->AriaLabel(StrL("doc.pdf"))
                ->AccessibilityDocument(source)
                ->Child(overlay);
    AccessibilityCollect(e, &f.win->accessibility);
    utassert(len(f.win->accessibility) == 2);
    AccessibilityNode* node = &f.win->accessibility[0];
    utassert(node->info.role == AccessibilityRole::Document);
    // the parts are not nodes and their text is not read until asked
    utassert(doc.partReads == 0);
    utassert(WindowAccessibilityPartCount(*node) == 3);
    utassert(StrEq(WindowAccessibilityPartName(*node, 1), StrL("Page 2")));
    utassert(!WindowAccessibilityPartName(*node, 3).s);
    utassert(StrEq(WindowAccessibilityPartText(*node, 2), StrL("three")));
    utassert(doc.partReads == 1);
    Bounds b;
    utassert(WindowAccessibilityPartBounds(*node, 1, &b) && b.y == 110);
    Selection range;
    utassert(WindowAccessibilityPartRange(*node, 1, &range));
    utassert(range.start == 4 && range.end == 7);
    // the document covers its pages; the overlay is off them
    node->bounds = {0, 0, 100, 400};
    f.win->accessibility[1].bounds = {90, 390, 5, 5};
#if GPUI_OS_WINDOWS
    utassert(AccessibilityWinPartsSmokeTest(f.win, node->id));
#endif
#if GPUI_OS_LINUX
    utassert(AccessibilityLinuxPartsSmokeTest(f.win, node->id));
#endif
#if GPUI_OS_MAC
    utassert(AccessibilityMacPartsSmokeTest(f.win, node->id));
#endif
    FreeAccessibilityFrame(&f);
}

// crates/kit/tests/integration/live_region.rs
static void LiveRegionAnnouncesChangesAndKeepsTheSameNodeIdentity() {
    struct Host {
        bool show = true;
        bool sibling = false;
        Str id = StrL("status");
        Str text = StrL("Saved 3 files");
        static El* Render(Host* self, Ctx* cx) {
            El* root = Div(cx->a);
            if (self->sibling) {
                root->Child(Div(cx->a)->Role(AccessibilityRole::Heading));
            }
            if (self->show) {
                root->Child(component::LiveRegion::New(cx, self->id, self->text)
                                ->Assertive()
                                ->IntoEl());
            }
            return root;
        }
    };
    App* app = TestAppNew();
    auto view = EntityNew<Host>(app);
    Host* host = view.Get(app);
    Window* win = TestWindowOpen(app, view, 240, 140);
    utassert(len(win->accessibility) == 1);
    const auto status = [&]() -> const AccessibilityNode& {
        return win->accessibility[len(win->accessibility) - 1];
    };
    utassert(status().info.role == AccessibilityRole::Status);
    utassert(status().info.live == AccessibilityLive::Assertive);
    utassert(StrEq(status().info.label, host->text));
    utassert(StrEq(status().info.value, host->text));
    utassert(status().liveChanged && !win->focusId);
    uint32_t path = status().elementPath;
    uint32_t id = status().id;
    host->sibling = true;
    TestDraw(win);
    utassert(len(win->accessibility) == 2 && !status().liveChanged);
    utassert(status().elementPath == path && status().id == id);
    host->text = StrL("Could not save");
    TestDraw(win);
    utassert(status().liveChanged);
    host->text = {};
    TestDraw(win);
    utassert(!status().liveChanged && len(status().info.label) == 0 &&
             len(status().info.value) == 0);
    host->text = StrL("Could not save");
    TestDraw(win);
    utassert(status().liveChanged);
    host->id = StrL("new-status");
    TestDraw(win);
    utassert(status().elementPath != path && status().id != id &&
             status().liveChanged);
    host->show = false;
    TestDraw(win);
    utassert(len(win->accessibility) == 1);
    host->show = true;
    TestDraw(win);
    utassert(status().liveChanged && !win->focusId);
    TestAppFree(app);
}

static void RetainedAccessibilityActionsFollowNamedControls() {
    struct Host {
        bool showTarget = true;
        bool insertSibling = false;
        int activated = 0;

        static void Activate(Host* self, Ctx*, const ClickEvent*,
                             int64_t which) {
            self->activated = (int)which;
        }

        static El* Control(Ctx* cx, Str id, Str label, int which) {
            return Div(cx->a)
                ->PathClick(id)
                ->Role(AccessibilityRole::Button)
                ->OnAccessibilityDefault(Listen(cx, &Activate, which))
                ->Child(TextEl(cx->a, label)->Role(AccessibilityRole::Label));
        }

        static El* Render(Host* self, Ctx* cx) {
            El* root = Div(cx->a)->SizeFull();
            if (self->insertSibling) {
                root->Child(Control(cx, StrL("other"), StrL("Other"), 2));
            }
            if (self->showTarget) {
                root->Child(Control(cx, StrL("target"), StrL("Target"), 1));
            }
            return root;
        }
    };
    App* app = TestAppNew();
    auto entity = EntityNew<Host>(app);
    auto* host = entity.Get(app);
    Window* win = TestWindowOpen(app, entity, 240, 140);
    utassert(len(win->accessibility) == 2);
    uint32_t target = win->accessibility[0].id;
    uint32_t label = win->accessibility[1].id;

    host->insertSibling = true;
    TestDraw(win);
    int index = WindowAccessibilityNodeIndex(win, target);
    const auto* child = WindowAccessibilityNode(win, label);
    utassert(index == 2 && child && child->parent == index);
    utassert(
        WindowAccessibilityPerform(win, target, AccessibilityAction::Default));
    utassert(host->activated == 1);
#if GPUI_OS_WINDOWS
    utassert(AccessibilityWinSmokeTest(win, target));
#endif

    host->showTarget = false;
    TestDraw(win);
    utassert(!WindowAccessibilityNode(win, target));
    utassert(!WindowAccessibilityNode(win, label));
    utassert(
        !WindowAccessibilityPerform(win, target, AccessibilityAction::Default));
    utassert(host->activated == 1);

    host->showTarget = true;
    TestDraw(win);
    utassert(WindowAccessibilityNodeIndex(win, target) == -1);
    utassert(win->accessibility[2].id != target);
    TestAppFree(app);
}

#if GPUI_OS_LINUX
static void GeometryOnlyChangesRefreshLinuxAccessibility() {
    struct Host {
        float top = 12;
        static El* Render(Host* self, Ctx* cx) {
            return Div(cx->a)->SizeFull()->PadT(self->top)->Child(
                Div(cx->a)->W(70)->H(30)->Role(AccessibilityRole::Button));
        }
    };
    App* app = TestAppNew();
    auto view = EntityNew<Host>(app);
    Window* win = TestWindowOpen(app, view, 240, 140);
    uint64_t original = win->accessibilityHash;
    utassert(len(win->accessibility) == 1 && win->accessibility[0]
                                                     .bounds.y == 12);
    view.Get(app)->top = 30;
    TestDraw(win);
    utassert(len(win->accessibility) == 1 && win->accessibility[0]
                                                     .bounds.y == 30);
    uint64_t moved = win->accessibilityHash;
    utassert(original != moved);
    TestDraw(win);
    utassert(win->accessibilityHash == moved);
    TestAppFree(app);
}
#endif

void TestAccessibility() {
    TextViewPublishesItsRenderedBodyToAccessibility();
    ButtonExposesOptionalDisclosureState();
    AMixedCheckboxUsesTheDashAndRequestsChecked();
    TestSuite("accessibility");
#if GPUI_OS_WINDOWS
    utassert(AccessibilityWinPropertyChangesSmokeTest());
    utassert(AccessibilityWinTextChangesSmokeTest());
    utassert(AccessibilityWinSelectionEventsSmokeTest());
    utassert(AccessibilityWinDialogEventsSmokeTest());
#endif
    TheTreeSkipsVisualBoxesButKeepsSemanticParents();
    StyledButtonsCanReplaceTheirVisibleAccessibleName();
    LabelsExposeTheirTextToAssistiveTechnology();
    AnExplicitAccessibilityLabelReplacesTheVisibleOne();
    ClipboardReportsDefaultAndExplicitAccessibilityNames();
    ClipboardUsesItsVisibleLabelAsTheAccessibleName();
    ExplicitOnlyNamesReachTheAnnouncedControl();
    ExplicitAriaFieldsSurviveCollection();
    ADisabledControlMarksItsNodeDisabled();
    BaseControlsProjectTheirControlledState();
    TablesKeepCountsAndOneBasedIndices();
    SliderActionsUseTheSlidersOwnStep();
    SliderAccessibilityLabelsFollowTheExistingNodes();
    EditableTextOffersSetValueAndReadOnlyTextDoesNot();
#if GPUI_OS_LINUX
    utassert(AccessibilityLinuxEventSmokeTest());
    GeometryOnlyChangesRefreshLinuxAccessibility();
#endif
    SetValuePreservesExactEditorText();
    SelectionContainersExposeTheirSelectedItems();
    ExplicitSemanticListenersAreInvoked();
    ConditionalAndCompositeRolesMatchUpstream();
    AccordionPreservesDisabledItemsWhenTheGroupIsEnabled();
    ASettledClosedPanelUnmountsItsContent();
    MaskToggleIsNamedForTheActionItTakes();
    MaskedInputIsAPasswordField();
    ClearButtonIsNamed();
    TheTreeOutlivesTheStringsItWasBuiltFrom();
    InputContentTypesAndSecretsProjectSafely();
    SingleLineRangesUseTheCurrentFrameGeometry();
    MultilineAccessibilityKeepsLineBreaksAndSelection();
    ALargeDocumentIsNavigableFromStartToEnd();
    ALongDocumentExposesOnlyTheCaretExcerpt();
    ADocumentSourceIsReadOnDemand();
    DocumentRevisionsTrackTextAndSelectionWithoutReadingTheSource();
    ADocumentSourceEnumeratesItsPartsOnDemand();
    LiveRegionAnnouncesChangesAndKeepsTheSameNodeIdentity();
    RetainedAccessibilityActionsFollowNamedControls();
}
