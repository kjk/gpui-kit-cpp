/* Ported from crates/base/src/state_style.rs, and the priority tests a
 * control keeps for it in crates/base/src/button.rs.
 *
 * A state style is a partial style: it says something about the fields it
 * names and nothing about the rest. resolve_style lays them down in one fixed
 * order — the instance, then the value states, then disabled — so no control
 * gets to decide the order for itself. */

#include "Test.h"

static bool Same(Rgba a, Rgba b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

static const Rgba kInstance = Rgb(0x11, 0x22, 0x33);
static const Rgba kSelected = Rgb(0x44, 0x55, 0x66);
static const Rgba kDisabled = Rgb(0x77, 0x88, 0x99);

// instance_style_is_the_baseline_when_no_state_is_active.
static void TheInstanceStyleIsTheBaseline() {
    StateStyle instance;
    instance.Bg(kInstance);
    StateStyle out = StateStyleResolve(instance, nullptr, 0);
    utassert(out.Has(StateFieldBg));
    utassert(Same(out.style.bg.color, kInstance));
}

// an_active_state_overrides_the_instance_style.
static void AnActiveStateOverridesTheInstance() {
    StateStyle instance;
    instance.Bg(kInstance);
    StateStyle selected;
    selected.Bg(kSelected);
    const StateStyle* states[] = {&selected};
    StateStyle out = StateStyleResolve(instance, states, 1);
    utassert(Same(out.style.bg.color, kSelected));
}

// later_states_override_earlier_states: disabled is last, so it wins.
static void LaterStatesOverrideEarlierOnes() {
    StateStyle instance;
    instance.Bg(kInstance);
    StateStyle selected;
    selected.Bg(kSelected);
    StateStyle disabled;
    disabled.Bg(kDisabled);
    const StateStyle* states[] = {&selected, &disabled};
    StateStyle out = StateStyleResolve(instance, states, 2);
    utassert(Same(out.style.bg.color, kDisabled));
}

// states_only_override_the_fields_they_set.
static void AStateOnlyOverridesWhatItNames() {
    StateStyle instance;
    instance.Bg(kInstance).Radius(8);
    StateStyle disabled;
    disabled.Bg(kDisabled);
    const StateStyle* states[] = {&disabled};
    StateStyle out = StateStyleResolve(instance, states, 1);
    utassert(Same(out.style.bg.color, kDisabled));
    // The radius came from the instance and nothing said otherwise.
    utassert(out.Has(StateFieldRadius));
    utassertnear(out.style.radius, 8.f);
}

// The per-edge builders are refinements too. They used to set the right mask
// but StateStyleRefine failed to copy their values, which made semantic style
// builders silently lose a border edge.
static void EdgeRefinementsCopyEachNamedEdge() {
    StateStyle instance;
    instance.BorderB(3, kInstance);
    StateStyle selected;
    selected.BorderL(2, kSelected);
    const StateStyle* states[] = {&selected};
    StateStyle out = StateStyleResolve(instance, states, 1);
    utassert(out.Has(StateFieldBorderL));
    utassert(out.Has(StateFieldBorderB));
    utassertnear(out.style.borderL, 2.f);
    utassertnear(out.style.borderB, 3.f);
    utassert(Same(out.style.borderColor, kSelected));
}

static void SemanticControlStylesFollowTheSharedPriority() {
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.a = a;

    StateStyle instance;
    instance.Opacity(.9f);
    RadioStyles radioStyles;
    radioStyles.Checked(StateStyle().Opacity(.8f))
        .Disabled(StateStyle().Opacity(.5f));
    El* radio =
        Radio::New(&cx, StrL("radio"), true, true, {}, &radioStyles, &instance);
    utassert(radio->StyleStates()->refineSet & StateFieldOpacity);
    utassertnear(radio->StyleStates()->refine.opacity, .5f);

    ToggleStyles toggleStyles;
    toggleStyles.Pressed(StateStyle().Opacity(.8f))
        .Disabled(StateStyle().Opacity(.5f));
    El* toggle = Toggle::New(&cx, StrL("toggle"), true, false, {},
                             &toggleStyles, &instance);
    utassertnear(toggle->StyleStates()->refine.opacity, .8f);

    CheckboxStyles checkboxStyles;
    checkboxStyles.Checked(StateStyle().Border(1, kSelected))
        .Indeterminate(StateStyle().Opacity(.7f))
        .Disabled(StateStyle().Opacity(.5f));
    El* checkbox = Checkbox::New(&cx, StrL("checkbox"), CheckboxState::Checked,
                                 true, {}, &checkboxStyles, &instance);
    utassertnear(checkbox->StyleStates()->refine.opacity, .5f);
    utassert(checkbox->StyleStates()->refineSet & StateFieldBorder);
    utassert(Same(checkbox->StyleStates()->refine.borderColor, kSelected));

    CheckboxIndicatorStyles indicatorStyles;
    indicatorStyles.Checked(StateStyle().Border(1, kSelected))
        .Indeterminate(StateStyle().Opacity(.7f))
        .Disabled(StateStyle().Border(1, kDisabled));
    El* indicator = CheckboxIndicator::New(&cx, CheckboxState::Checked, true,
                                           &indicatorStyles, &instance);
    utassertnear(indicator->StyleStates()->refine.opacity, .9f);
    utassert(Same(indicator->StyleStates()->refine.borderColor, kDisabled));

    SwitchStyles switchStyles;
    switchStyles.Checked(StateStyle().Opacity(.8f))
        .Disabled(StateStyle().Opacity(.5f));
    El* switchRoot = Switch::New(&cx, StrL("switch"), true, true, {},
                                 &switchStyles, &instance);
    utassertnear(switchRoot->StyleStates()->refine.opacity, .5f);
    utassert(switchRoot->stopMouseDown);

    SwitchTrackStyles trackStyles;
    trackStyles.Checked(StateStyle().Bg(kSelected))
        .Disabled(StateStyle().Bg(kDisabled));
    El* switchTrack = SwitchTrack::New(&cx, StrL("switch-track"), true, true,
                                       &trackStyles, &instance);
    utassert(Same(switchTrack->StyleStates()->refine.bg.color, kDisabled));

    SwitchThumbStyles thumbStyles;
    thumbStyles.Checked(StateStyle().Border(1, kSelected))
        .Disabled(StateStyle().Opacity(.5f));
    El* switchThumb =
        SwitchThumb::New(&cx, true, true, &thumbStyles, &instance);
    utassertnear(switchThumb->StyleStates()->refine.opacity, .5f);
    utassert(Same(switchThumb->StyleStates()->refine.borderColor, kSelected));

    FocusHandle supplied = {-77};
    checkbox =
        Checkbox::New(&cx, StrL("focused-checkbox"), CheckboxState::Unchecked,
                      false, {}, nullptr, nullptr, StrL("Choice"), 4, false,
                      supplied, AccessibilityRole::None);
    utassert(checkbox->style.focusId == supplied.id);
    utassert(checkbox->style.tabIndex == 4 && !checkbox->style.tabStop);
    utassert(checkbox->accessibility.role == AccessibilityRole::None);

    TabStyles tabStyles;
    tabStyles.Selected(StateStyle().Opacity(.8f))
        .Disabled(StateStyle().Opacity(.5f));
    El* tab = Tab::New(&cx, StrL("tab"), true, {}, true, {}, 0, 0, &tabStyles,
                       &instance);
    utassertnear(tab->StyleStates()->refine.opacity, .5f);
    utassert(tab->stopMouseDown);
    utassert(Toggle::New(&cx, StrL("disabled-toggle"), false, true)
                 ->stopMouseDown);
    utassert(!Toggle::New(&cx, StrL("enabled-toggle"))->stopMouseDown);

    ArenaDelete(a);
}

// button.rs disabled_style_applies_only_while_disabled_and_then_wins: a state
// that is not active is left out of the list rather than resolved to nothing.
static void ADisabledStyleOnlyAppliesWhileDisabled() {
    StateStyle instance;
    instance.Bg(kInstance);
    StateStyle disabled;
    disabled.Bg(kDisabled);

    const StateStyle* enabled[] = {nullptr};
    utassert(Same(StateStyleResolve(instance, enabled, 1).style.bg.color,
                  kInstance));

    const StateStyle* off[] = {&disabled};
    utassert(
        Same(StateStyleResolve(instance, off, 1).style.bg.color, kDisabled));

    // And with no instance style of its own, the state is all there is.
    StateStyle bare;
    utassert(Same(StateStyleResolve(bare, off, 1).style.bg.color, kDisabled));
}

// button.rs selected_disabled_and_instance_styles_follow_the_shared_priority.
static void SelectedAndDisabledFollowTheSharedPriority() {
    StateStyle selected;
    selected.Bg(kSelected);
    StateStyle disabled;
    disabled.Bg(kDisabled);
    auto resolve = [&](bool isSelected, bool isDisabled) {
        const StateStyle* states[2] = {isSelected ? &selected : nullptr,
                                       isDisabled ? &disabled : nullptr};
        return StateStyleResolve(StateStyle{}, states, 2);
    };
    utassert(!resolve(false, false).Has(StateFieldBg));
    utassert(Same(resolve(true, false).style.bg.color, kSelected));
    utassert(Same(resolve(true, true).style.bg.color, kDisabled));
    utassert(Same(resolve(false, true).style.bg.color, kDisabled));
}

// refine_style: what the resolved style does to the element it is put on, and
// nothing beyond it. It lands at layout rather than where the call sits, so
// the element carries the refinement until then.
static void AResolvedStyleGoesOntoTheElement() {
    Arena* a = ArenaNew();
    StateStyle s;
    s.Bg(kSelected).Border(2, kDisabled);
    El* e = Div(a)->Radius(4);
    ElRefine(e, s);
    utassert(e->StyleStates()->refineSet == s.set);
    StyleApplyFields(&e->style, e->StyleStates()->refine,
                     e->StyleStates()->refineSet);
    utassert(e->style.hasBg && Same(e->style.bg.color, kSelected));
    utassertnear(e->style.border, 2.f);
    utassert(Same(e->style.borderColor, kDisabled));
    // The radius was never named, so the element keeps its own.
    utassertnear(e->style.radius, 4.f);
    ArenaDelete(a);
}

// The order resolve_style promises: the instance style is underneath, and a
// semantic state wins over it — including the part of the instance style the
// caller chained on *after* the primitive handed the element back, which is
// the only order a builder can offer.
static void AStateWinsOverWhatIsChainedAfterIt() {
    Arena* a = ArenaNew();
    StateStyle s;
    s.Bg(kDisabled);
    El* e = ElRefine(Div(a), s)->Bg(kSelected)->Radius(6);
    utassert(Same(e->style.bg.color, kSelected));
    StyleApplyFields(&e->style, e->StyleStates()->refine,
                     e->StyleStates()->refineSet);
    utassert(Same(e->style.bg.color, kDisabled));
    utassertnear(e->style.radius, 6.f);
    ArenaDelete(a);
}

// The three fills a box can paint, and which one the pointer picks. GPUI
// refines the hovered style and then the active one over the base, so a box
// that is both hovered and held paints its pressed fill.
static void TheHeldBoxPaintsItsPressedFill() {
    utassert(BoxFillFor(true, true, 7, 7, 7) == BoxFill::Active);
    utassert(BoxFillFor(true, true, 7, 0, 7) == BoxFill::Hover);
    utassert(BoxFillFor(true, true, 7, 7, 0) == BoxFill::Active);
    utassert(BoxFillFor(true, true, 7, 0, 0) == BoxFill::Base);
}

// A press that slides off keeps the pressed fill and loses the hover: the
// window holds `activeId` from the press to the release, where `hoverId`
// follows the pointer.
static void APressThatSlidOffStaysPressed() {
    utassert(BoxFillFor(true, true, 7, 7, 8) == BoxFill::Active);
    // And the box it slid onto is neither: nothing else can be held.
    utassert(BoxFillFor(true, true, 8, 7, 8) == BoxFill::Hover);
}

// Both states need a click id of their own, the way the hover always has:
// without one the box matches the 0 that means nothing is hovered or held.
static void ABoxWithNoClickIdIsNeitherHoveredNorHeld() {
    utassert(BoxFillFor(true, true, 0, 0, 0) == BoxFill::Base);
}

// A box that never named a pressed fill falls through to the hover it did
// name, which is every element in the tree that predates `activeBg`.
static void AFillThatWasNeverNamedIsNotPainted() {
    utassert(BoxFillFor(false, true, 7, 7, 7) == BoxFill::Hover);
    utassert(BoxFillFor(false, false, 7, 7, 7) == BoxFill::Base);
    utassert(BoxFillFor(true, false, 7, 7, 7) == BoxFill::Active);
}

// StyleRefinement::refine over the pressed fill, which is what lets a
// control state name one.
static void AStateCanNameThePressedFill() {
    StateStyle instance;
    instance.Bg(kInstance);
    StateStyle over;
    over.ActiveBg(kSelected);
    const StateStyle* states[1] = {&over};
    StateStyle out = StateStyleResolve(instance, states, 1);
    utassert(out.Has(StateFieldActiveBg));
    utassert(Same(out.style.activeBg.color, kSelected));
    // And it says nothing about the fill underneath it.
    utassert(Same(out.style.bg.color, kInstance));
}

// `div().hover(..)` and `div().drag_over::<T>(..)`: refinements that hold only
// while the pointer is over the box, or while a drag of that kind is. GPUI
// resolves both in `compute_style` during prepaint; here that is `PrepareEl`,
// which layout runs, so the test drives layout and reads the style back.
namespace {
struct RefineCase {
    Arena* a = nullptr;
    PaintCtx ctx = {};
    El* root = nullptr;
    El* box = nullptr;

    RefineCase() {
        a = ArenaNew();
        ctx.viewW = 200;
        ctx.viewH = 100;
        root = Div(a)->FlexCol()->W(200)->H(100);
        box = Div(a)->W(100)->H(20)->Click(4242)->Bg(Rgb(1, 2, 3));
        root->Child(box);
    }
    void Run() { LayoutEl(&ctx, root, 0, 0, 200, 100, 14, Rgba{}); }
    ~RefineCase() { ArenaDelete(a); }
};
} // namespace

static void AHoverRefinementHoldsOnlyWhileHovered() {
    {
        RefineCase c;
        c.box->Hover(StateStyle().Bg(Rgb(9, 9, 9)));
        c.ctx.hoverId = 0; // nothing hovered
        c.Run();
        utassert(c.box->style.bg.color.r == 1);
    }
    {
        RefineCase c;
        c.box->Hover(StateStyle().Bg(Rgb(9, 9, 9)));
        c.ctx.hoverId = 4242;
        c.Run();
        utassert(c.box->style.bg.color.r == 9);
    }
    // A box with no click id of its own would match a hoverId of 0, which is
    // what "nothing is hovered" is spelled as, so it is never refined.
    {
        RefineCase c;
        c.box->Click(0)->Hover(StateStyle().Bg(Rgb(9, 9, 9)));
        c.ctx.hoverId = 0;
        c.Run();
        utassert(c.box->style.bg.color.r == 1);
    }
}

static void ADragOverRefinementNeedsTheRightKind() {
    Str panel = StrL("dock-panel");
    Str other = StrL("dock-resize");
    {
        RefineCase c;
        c.box->DragOver(panel, StateStyle().BorderL(2, Rgb(7, 7, 7)));
        c.ctx.dragOverId = 4242;
        c.ctx.dragKind = panel;
        c.Run();
        utassert(c.box->style.borderL == 2);
        utassert(c.box->style.borderColor.r == 7);
    }
    // Over the box, but dragging something it does not take.
    {
        RefineCase c;
        c.box->DragOver(panel, StateStyle().BorderL(2, Rgb(7, 7, 7)));
        c.ctx.dragOverId = 4242;
        c.ctx.dragKind = other;
        c.Run();
        utassert(c.box->style.borderL == 0);
    }
    // The right kind, over something else.
    {
        RefineCase c;
        c.box->DragOver(panel, StateStyle().BorderL(2, Rgb(7, 7, 7)));
        c.ctx.dragOverId = 99;
        c.ctx.dragKind = panel;
        c.Run();
        utassert(c.box->style.borderL == 0);
    }
}

// `.border_l_2()` names the left edge and leaves the other three alone. A
// refinement that copied all four would clear whatever the box already had.
static void AnEdgeRefinementLeavesTheOtherEdgesAlone() {
    RefineCase c;
    c.box->BorderB(3, Rgb(4, 4, 4));
    c.box->DragOver(StrL("k"), StateStyle().BorderL(2, Rgb(7, 7, 7)));
    c.ctx.dragOverId = 4242;
    c.ctx.dragKind = StrL("k");
    c.Run();
    utassert(c.box->style.borderL == 2);
    utassert(c.box->style.borderB == 3);
}

// button.rs: disabled_button_ignores_hover_and_active_styles and
// is_disabled_reports_the_disabled_state.
static void ADisabledButtonIgnoresPointerStyles() {
    App app;
    Arena* arena = ArenaNew();
    Window* win = new Window();
    win->app = &app;
    win->paint.app = &app;
    win->paint.window = win;
    Ctx cx{&app, win, arena, {}};
    Rgba base = Rgb(255, 0, 0), hover = Rgb(0, 255, 0), active = Rgb(0, 0, 255);
    Str group = StrL("group");
    auto color = [&](bool disabled, bool held, bool own) {
        El* button = Button::New(&cx, StrL("probe"), disabled)
                         ->Click(7)
                         ->W(40)
                         ->H(40)
                         ->Fg(base)
                         ->Hover(StateStyle().Fg(hover))
                         ->Active(StateStyle().Fg(active))
                         ->GroupHover(StateStyle().Fg(hover), group)
                         ->GroupActive(StateStyle().Fg(active), group)
                         ->HoverBg(hover)
                         ->ActiveBg(active);
        utassert(Button::IsDisabled(button) == disabled);
        El* box = Div(arena)->Group(group)->W(80)->H(60)->Child(button);
        win->paint.hoverId = own ? 7 : 0;
        win->paint.activeId = own && held ? 7 : 0;
        win->paint.mouseX = 10;
        win->paint.mouseY = 10;
        win->mouseDown = held;
        win->pressedX = 10;
        win->pressedY = 10;
        win->prevNamedGroupCount = 1;
        win->prevNamedGroups[0] = {1, 0, 0, 0, 80, 60};
        LayoutEl(&win->paint, box, 0, 0, 80, 60, 16, base);
        if (disabled) {
            utassert(!button->style.hasHoverBg && !button->style.hasActiveBg);
        }
        return button->style.color;
    };
    utassert(Same(color(true, false, true), base));
    utassert(Same(color(true, true, true), base));
    utassert(Same(color(false, false, true), hover));
    utassert(Same(color(false, true, true), active));
    utassert(Same(color(false, true, false), active));
    utassert(Same(color(true, true, false), base));
    WindowKeyedFree(win);
    delete win;
    ArenaDelete(arena);
}

void TestStateStyle() {
    TestSuite("state_style");
    TheHeldBoxPaintsItsPressedFill();
    APressThatSlidOffStaysPressed();
    ABoxWithNoClickIdIsNeitherHoveredNorHeld();
    AFillThatWasNeverNamedIsNotPainted();
    AStateCanNameThePressedFill();
    TheInstanceStyleIsTheBaseline();
    AnActiveStateOverridesTheInstance();
    LaterStatesOverrideEarlierOnes();
    AStateOnlyOverridesWhatItNames();
    EdgeRefinementsCopyEachNamedEdge();
    SemanticControlStylesFollowTheSharedPriority();
    ADisabledStyleOnlyAppliesWhileDisabled();
    SelectedAndDisabledFollowTheSharedPriority();
    AResolvedStyleGoesOntoTheElement();
    AStateWinsOverWhatIsChainedAfterIt();
    AHoverRefinementHoldsOnlyWhileHovered();
    ADisabledButtonIgnoresPointerStyles();
    ADragOverRefinementNeedsTheRightKind();
    AnEdgeRefinementLeavesTheOtherEdgesAlone();
}
