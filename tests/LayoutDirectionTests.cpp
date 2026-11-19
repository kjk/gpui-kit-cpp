/* crates/base/src/layout_direction.rs */

#include "Test.h"

static void DirectionFollowsThePrimaryLanguage() {
    for (const char* rtl :
         {"ar", "ar-EG", "he_IL", "fa", "ur-PK", "HE", "yi.UTF-8"}) {
        utassert(LayoutDirectionFromLocale(Str(rtl)) == LayoutDirection::Rtl);
    }
    for (const char* ltr : {"en", "en-US", "zh-CN", "", "C", "arn"}) {
        utassert(LayoutDirectionFromLocale(Str(ltr)) == LayoutDirection::Ltr);
    }
}

static void LogicalEdgesAndRowsFollowTheAppDirection() {
    App* app = TestAppNew();
    BaseInit(app);
    App independent;
    utassert(!LayoutIsRtl(app));
    Arena* arena = ArenaNew();
    for (bool rtl : {false, true}) {
        auto direction = rtl ? LayoutDirection::Rtl : LayoutDirection::Ltr;
        LayoutDirectionSetGlobal(app, direction);
        LayoutDirectionInit(app);
        utassert(LayoutIsRtl(app) == rtl && !LayoutIsRtl(&independent));
        utassert(LogicalAnchor(Anchor::TopLeft, app) ==
                 (rtl ? Anchor::TopRight : Anchor::TopLeft));
        utassert(LogicalAnchor(Anchor::BottomRight, app) ==
                 (rtl ? Anchor::BottomLeft : Anchor::BottomRight));
        utassert(LogicalAnchor(Anchor::TopCenter, app) == Anchor::TopCenter);
        utassert(DockPhysicalPlacement(DockPlacement::Left, app) ==
                 (rtl ? DockPlacement::Right : DockPlacement::Left));
        utassert(DockPhysicalPlacement(DockPlacement::Bottom, app) ==
                 DockPlacement::Bottom);
        using component::SheetPlacement;
        utassert(
            component::SheetPhysicalPlacement(SheetPlacement::Right, app) ==
            (rtl ? SheetPlacement::Left : SheetPlacement::Right));
        utassert(component::SheetPhysicalPlacement(SheetPlacement::Top, app) ==
                 SheetPlacement::Top);
        El* box = Div(arena)
                      ->PadS(4, app)
                      ->MarginE(8, app)
                      ->BorderS(1, Rgba{}, app)
                      ->Radius(4)
                      ->RadiusS(2, app)
                      ->TextStart(app);
        utassert(box->style.pad.left == (rtl ? 0 : 4));
        utassert(box->style.pad.right == (rtl ? 4 : 0));
        utassert(box->style.margin.left == (rtl ? 8 : 0));
        utassert(box->style.margin.right == (rtl ? 0 : 8));
        utassert(box->style.borderL == (rtl ? 0 : 1));
        utassert(box->style.borderR == (rtl ? 1 : 0));
        utassert(box->style.corners.tl == (rtl ? 4 : 2));
        utassert(box->style.corners.tr == (rtl ? 2 : 4));

        Ctx cx = {app, nullptr, arena};
        InputState input;
        El* logical = component::Input::New(&cx, StrL("logical-text"), &input)
                          ->IntoEl();
        utassert((logical->first->style.justify == Justify::End) == rtl);
        El* physical = component::Input::New(&cx, StrL("physical-text"), &input)
                           ->Align(component::InputAlign::Left)
                           ->IntoEl();
        utassert(physical->first->style.justify != Justify::End);
        El* popup = Positioner::Side(&cx, {40, 40, 60, 20})
                        ->Placement(Placement::Top)
                        ->Align(Align::Start)
                        ->IntoEl();
        utassert(popup->style.positionerAlign ==
                 (uint8_t)(rtl ? Align::End : Align::Start));

        El* child = Div(arena)->W(10)->H(10);
        El* row = Div(arena)
                      ->W(100)
                      ->H(20)
                      ->FlexRowDir(app)
                      ->JustifyFlexStart()
                      ->Child(child);
        PaintCtx paint = {};
        LayoutEl(&paint, row, 0, 0, 100, 20, 14, Rgba{});
        utassertnear(child->x, rtl ? 90.f : 0.f);
        row->JustifyFlexEnd();
        LayoutEl(&paint, row, 0, 0, 100, 20, 14, Rgba{});
        utassertnear(child->x, rtl ? 0.f : 90.f);
    }
    ArenaDelete(arena);
    TestAppFree(app);
}

static void InputArrowsMirrorExceptInSourceCode() {
    App* app = TestAppNew();
    LayoutDirectionSetGlobal(app, LayoutDirection::Rtl);
    InputState input;
    Str text = StrL("مرحبا عالم");
    InputSetValue(&input, text);
    auto place = [&](int offset) {
        InputSetSelectedRangeWithDirection(&input, app, nullptr, offset, offset,
                                           false);
    };
    auto act = [&](InputAction action) {
        utassert(InputPerform(&input, app, nullptr, action, false));
        return InputCursor(&input);
    };
    place(4);
    utassert(act(InputAction::MoveRight) == 2);
    utassert(act(InputAction::MoveLeft) == 4);
    utassert(act(InputAction::SelectRight) == 2);
    utassert(input.selectedRange.start == 2 && input.selectedRange.end == 4);
    place(4);
    utassert(act(InputAction::SelectLeft) == 6);
    utassert(input.selectedRange.start == 4 && input.selectedRange.end == 6);
    place(len(text));
    utassert(act(InputAction::MoveToNextWord) == 11);
    place(0);
    utassert(act(InputAction::MoveToPreviousWord) == 10);
    place(4);
    input.mode.kind = LayoutModeKind::CodeEditor;
    utassert(act(InputAction::MoveRight) == 6);
    TestAppFree(app);
}

static void HorizontalSliderMeasuresFromTheRight() {
    SliderState slider = SliderStateNew(0, 100, SliderSingle(50), 1);
    SliderSetBounds(&slider, {0, 0, 100, 20});
    utassert(SliderUpdateByPosition(&slider, Axis::Horizontal, {20, 10}, false,
                                    true));
    utassertnear(slider.value.Start(), 80.f);
    utassert(
        SliderUpdateByPosition(&slider, Axis::Vertical, {20, 5}, false, true));
    utassertnear(slider.value.Start(), 75.f);
}

struct RtlTabBounds {
    Bounds first;
    Bounds second;
    Bounds prefix;
    Bounds suffix;
    bool showSuffix = false;

    static El* Render(RtlTabBounds* self, Ctx* cx) {
        auto* bar = component::TabBar::New(cx, StrL("rtl-tab-test"))->W(400);
        bar->Child(
            component::Tab::New(cx)
                ->Label(StrL("A"))
                ->Prefix(Div(cx->a)->W(16)->H(16)->BoundsOut(&self->prefix))
                ->Suffix(Div(cx->a)->W(16)->H(16)->BoundsOut(&self->suffix))
                ->Child(Div(cx->a)->W(12)->H(16)->BoundsOut(&self->first)));
        bar->Child(component::Tab::New(cx)->Label(StrL("B"))->Child(
            Div(cx->a)->W(12)->H(16)->BoundsOut(&self->second)));
        if (self->showSuffix) {
            bar->Suffix(Div(cx->a)->W(16)->H(16));
        }
        return bar->IntoEl();
    }
};

static void TabStripAndSlotsStartAtTheRight() {
    for (bool suffix : {false, true}) {
        App* app = TestAppNew();
        component::Init(app);
        LayoutDirectionSetGlobal(app, LayoutDirection::Rtl);
        auto view = EntityNew<RtlTabBounds>(app);
        RtlTabBounds* state = view.Get(app);
        state->showSuffix = suffix;
        Window* win = TestWindowOpen(app, view, 400, 60);
        TestDraw(win);
        utassert(state->prefix.Right() > 360.f);
        utassert(state->second.Right() < state->first.x);
        utassert(state->suffix.Right() < state->prefix.x);
        TestAppFree(app);
    }
}

void TestLayoutDirection() {
    TestSuite("layout direction");
    DirectionFollowsThePrimaryLanguage();
    LogicalEdgesAndRowsFollowTheAppDirection();
    InputArrowsMirrorExceptInSourceCode();
    HorizontalSliderMeasuresFromTheRight();
    TabStripAndSlotsStartAtTheRight();
}
