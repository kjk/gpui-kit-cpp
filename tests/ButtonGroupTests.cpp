/* crates/ui/src/button/button_group.rs: selection callback behavior. */

#include "Test.h"

struct ButtonGroupHarness {
    int calls = 0;
    int count = 0;
    int selected[80] = {};

    static El* Render(ButtonGroupHarness*, Ctx* cx) { return Div(cx->a); }

    static void OnChange(ButtonGroupHarness* self, Ctx*,
                         const component::ButtonGroupEvent* ev) {
        self->calls++;
        self->count = std::min(ev->count, 80);
        for (int i = 0; i < self->count; i++) {
            self->selected[i] = ev->selected[i];
        }
    }
};

static bool SameButtonColor(Rgba a, Rgba b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

static int ButtonChildCount(const El* root) {
    int count = 0;
    for (const El* child = root ? root->first : nullptr; child;
         child = child->next) {
        count++;
    }
    return count;
}

static const AccessibilityNode* ButtonAt(const Vec<AccessibilityNode>& nodes,
                                         int wanted) {
    for (int i = 0; i < len(nodes); i++) {
        if (nodes[i].info.role != AccessibilityRole::Button) {
            continue;
        }
        if (wanted-- == 0) {
            return &nodes[i];
        }
    }
    return nullptr;
}

static void BaseButtonCentersOrdinaryChildGeometry() {
    App app;
    Window* win = new Window();
    win->app = &app;
    Arena* arena = ArenaNew();
    Ctx cx{&app, win, arena, {}};

    El* child = Div(arena)->W(48)->H(12);
    El* button =
        Button::New(&cx, StrL("alignment-button"))->W(120)->H(40)->Child(child);
    utassert(button->style.display == Display::Flex);
    utassert(button->style.align == FlexAlign::Center);
    utassert(button->style.justify == Justify::Center);
    utassertnear(button->style.lineHeight, 1.f);

    win->paint.app = &app;
    win->paint.window = win;
    const RuntimeStyle& th = RuntimeStyleNow(&app);
    LayoutEl(&win->paint, button, 0, 0, 120, 40, th.fontSize, th.foreground);
    utassertnear(child->Bounds().CenterX(), button->Bounds().CenterX());
    utassertnear(child->Bounds().CenterY(), button->Bounds().CenterY());

    WindowKeyedFree(win);
    delete win;
    ArenaDelete(arena);
    EntityDropAll(&app);
}

static void BaseTabAndToggleCenterOrdinaryChildGeometry() {
    App app;
    Window* win = new Window();
    win->app = &app;
    Arena* arena = ArenaNew();
    Ctx cx{&app, win, arena, {}};
    win->paint.app = &app;
    win->paint.window = win;
    const RuntimeStyle& th = RuntimeStyleNow(&app);

    El* tabChild = Div(arena)->W(48)->H(12);
    El* tab =
        Tab::New(&cx, StrL("alignment-tab"))->W(120)->H(40)->Child(tabChild);
    utassert(tab->style.display == Display::Flex);
    utassert(tab->style.align == FlexAlign::Center);
    utassert(tab->style.justify == Justify::Center);
    utassertnear(tab->style.lineHeight, 1.f);
    LayoutEl(&win->paint, tab, 0, 0, 120, 40, th.fontSize, th.foreground);
    utassertnear(tabChild->Bounds().CenterX(), tab->Bounds().CenterX());
    utassertnear(tabChild->Bounds().CenterY(), tab->Bounds().CenterY());

    El* toggleChild = Div(arena)->W(48)->H(12);
    El* toggle = Toggle::New(&cx, StrL("alignment-toggle"))
                     ->W(120)
                     ->H(40)
                     ->Child(toggleChild);
    utassert(toggle->style.display == Display::Flex);
    utassert(toggle->style.align == FlexAlign::Center);
    utassert(toggle->style.justify == Justify::Center);
    utassertnear(toggle->style.lineHeight, 1.f);
    LayoutEl(&win->paint, toggle, 0, 0, 120, 40, th.fontSize, th.foreground);
    utassertnear(toggleChild->Bounds().CenterX(), toggle->Bounds().CenterX());
    utassertnear(toggleChild->Bounds().CenterY(), toggle->Bounds().CenterY());

    WindowKeyedFree(win);
    delete win;
    ArenaDelete(arena);
    EntityDropAll(&app);
}

static void SelectionEventsAreOrderedAndNotWordSized() {
    App app;
    component::Init(&app);
    Window* win = new Window();
    win->app = &app;
    Arena* arena = ArenaNew();
    Entity<ButtonGroupHarness> harness = EntityNew<ButtonGroupHarness>(&app);
    Ctx cx{&app, win, arena, harness.id};

    component::ButtonGroup* group =
        component::ButtonGroup::New(&cx, StrL("wide-group"))
            ->Multiple(true)
            ->OnClick(Listen(&cx, &ButtonGroupHarness::OnChange));
    for (int i = 0; i < 70; i++) {
        Str id = StrDup(arena, fmt("button-%d", i));
        group->Child(component::Button::New(&cx, id)->Label(id)->Selected(
            i == 1 || i == 65));
    }
    El* root = group->IntoEl();
    IdsCollect(root);
    AccessibilityCollect(root, &win->accessibility);

    const AccessibilityNode* button69 = ButtonAt(win->accessibility, 69);
    utassert(button69 != nullptr);
    if (button69) {
        utassert(WindowAccessibilityPerform(win, button69->id,
                                            AccessibilityAction::Default));
    }
    ButtonGroupHarness* state = harness.Get(&app);
    utassert(state && state->calls == 1);
    utassert(state && state->count == 3);
    if (state && state->count == 3) {
        utassert(state->selected[0] == 1);
        utassert(state->selected[1] == 65);
        utassert(state->selected[2] == 69);
    }

    VecReset(win->accessibility);
    WindowKeyedFree(win);
    delete win;
    ArenaDelete(arena);
    EntityDropAll(&app);
}

static void SourceButtonVariantsRoundingAndIconsRemainConcrete() {
    App app;
    component::Init(&app);
    Window* win = new Window();
    win->app = &app;
    Arena* arena = ArenaNew();
    Ctx cx{&app, win, arena, {}};
    const Theme& theme = ThemeNow(&app);

    component::ButtonCustomVariant base =
        component::ButtonCustomVariant::New(&app);
    component::ButtonCustomVariant custom =
        base.Color(theme.magenta)
            .Foreground(theme.magenta)
            .Hover(RgbaOpacity(theme.magenta, 0.1f))
            .Active(RgbaOpacity(theme.magenta, 0.2f))
            .Shadow();
    utassert(base.color.a == 0 && !base.shadow);
    utassert(custom.shadow &&
             SameButtonColor(custom.foreground, theme.magenta));

    component::Button* button =
        component::Button::New(&cx, StrL("custom-button"))
            ->Custom(custom)
            ->Rounded(component::ButtonRounded::Large)
            ->Label(StrL("Custom"));
    El* rendered = button->IntoEl();
    utassert(button->variant == component::ButtonVariant::Custom);
    utassertnear(rendered->style.corners.tl, theme.radius * 2.f);
    utassert(
        SameButtonColor(rendered->style.bg.color,
                        RgbaMixOklab(theme.magenta, Rgba8(0, 0, 0, 0), 0.2f)));
    utassert(rendered->style.shadowCount == 1);
    utassertnear(rendered->style.shadows[0].y, 1.f);
    utassertnear(rendered->style.shadows[0].blur, 2.f);
    utassert(rendered->style.shadows[0].color.a == 13);
    component::ButtonVariants::Primary(button);
    utassert(button->variant == component::ButtonVariant::Primary &&
             !button->hasCustom);
    utassert(component::ButtonVariantIsGhost(component::ButtonVariant::Ghost));
    utassert(component::ButtonVariantIsLink(component::ButtonVariant::Link));
    utassert(component::ButtonVariantIsText(component::ButtonVariant::Text));

    component::Button* grouped[2] = {
        component::Button::New(&cx, StrL("grouped-one")),
        component::Button::New(&cx, StrL("grouped-two")),
    };
    component::ButtonGroup* sourceGroup =
        component::ButtonGroup::New(&cx, StrL("source-group"))
            ->Children(grouped, 2)
            ->Layout(Axis::Vertical)
            ->Custom(custom);
    utassert(sourceGroup->children.len == 2 && sourceGroup->vertical);
    utassert(sourceGroup->variant == component::ButtonVariant::Custom);
    component::DropdownButton* dropdown =
        component::DropdownButton::New(&cx, StrL("source-dropdown"))
            ->Success()
            ->Custom(custom);
    utassert(dropdown->variant == component::ButtonVariant::Custom);

    El* extra = Div(arena)->W(7)->H(7);
    component::ButtonIcon* icon = component::ButtonIcon::New(
        &cx, component::Icon::New(&cx, IconName::Check)->Color(theme.green));
    El* content = component::Button::New(&cx, StrL("icon-and-child"))
                      ->Icon(icon)
                      ->Label(StrL("Both"))
                      ->Extra(extra)
                      ->IntoEl();
    utassert(ButtonChildCount(content) == 3);
    utassert(content->first && content->first->next &&
             content->first->next->next == extra);
    utassert(icon->variant == component::ButtonIconVariant::Icon &&
             !icon->IsSpinner() && !icon->IsProgress());

    El* textLoading = component::Button::New(&cx, StrL("text-loading"))
                          ->Label(StrL("Waiting"))
                          ->Loading(true)
                          ->IntoEl();
    utassert(ButtonChildCount(textLoading) == 1);
    component::ButtonIcon* spinner =
        component::ButtonIcon::New(&cx, component::Spinner::New(&cx));
    component::ButtonIcon* progress = component::ButtonIcon::New(
        &cx, component::ProgressCircle::New(&cx)->Value(75));
    utassert(spinner->IsSpinner() && !spinner->IsProgress());
    utassert(progress->IsProgress() && !progress->IsSpinner());
    utassert(spinner->Loading(true)->WithSize(UiSize::Small)->IntoEl());
    utassert(progress->Loading(true)->Size(18)->IntoEl());

    // button_icon.rs: test_custom_data_icon_converts_through_component_slots.
    // An `Icon::Data` goes through the button's icon slot, its loading icon
    // and a menu row's slot with its bytes intact, and renders them as SVG
    // source.
    static const char kSvg[] =
        "<svg viewBox=\"0 0 24 24\"><circle cx=\"11\" cy=\"11\" "
        "r=\"8\"/></svg>";
    component::ButtonIcon* dataIcon =
        component::ButtonIcon::New(&cx, component::Icon::Empty(&cx)
                                            ->Data(Str(kSvg)))
            ->Loading(true)
            ->LoadingIcon(component::Icon::Empty(&cx)->Data(Str(kSvg)));
    utassert(dataIcon->variant == component::ButtonIconVariant::Icon);
    utassert(dataIcon->icon && dataIcon->icon
                                       ->source == component::IconSource::Data);
    utassert(dataIcon->loading);
    utassert(dataIcon->loadingIcon &&
             dataIcon->loadingIcon->source == component::IconSource::Data);
    El* turning = dataIcon->IntoEl();
    El* turningIcon = turning;
    while (turningIcon && !turningIcon->iconSvg.s) {
        turningIcon = turningIcon->first;
    }
    utassert(turningIcon && StrEq(turningIcon->iconSvg, Str(kSvg)));
    El* still = component::ButtonIcon::New(&cx, component::Icon::Empty(&cx)
                                                    ->Data(Str(kSvg)))
                    ->IntoEl();
    utassert(StrEq(still->iconSvg, Str(kSvg)));
    component::PopupMenu* menu =
        component::PopupMenu::New(&cx, StrL("data-icons"))
            ->Menu(StrL("Search"), component::Icon::Empty(&cx)
                                       ->Data(Str(kSvg)));
    utassert(menu->items.len == 1 && StrEq(menu->items[0].iconSvg, Str(kSvg)));

    // tooltip_placement (699936e4): a preferred side rides with the tip so
    // the overlay can honour it; omitting it keeps automatic positioning, and
    // setting it without tooltip content does nothing.
    El* placed = component::Button::New(&cx, StrL("placed"))
                     ->Label(StrL("Search"))
                     ->Tooltip(StrL("Find a document"))
                     ->TooltipPlacement(Placement::Left)
                     ->IntoEl();
    utassert(StrEq(placed->style.tooltip, StrL("Find a document")));
    utassert(placed->style.tooltipPlacement == (int8_t)Placement::Left);
    El* automatic = component::Button::New(&cx, StrL("auto"))
                        ->Tooltip(StrL("Anywhere"))
                        ->IntoEl();
    utassert(automatic->style.tooltipPlacement == -1);
    El* noTip = component::Button::New(&cx, StrL("no-tip"))
                    ->TooltipPlacement(Placement::Right)
                    ->IntoEl();
    utassert(!noTip->style.tooltip.s && noTip->style.tooltipPlacement == -1);

    WindowKeyedFree(win);
    delete win;
    ArenaDelete(arena);
    EntityDropAll(&app);
    AppGlobalClear(&app);
}

static void GhostButtonsUseAccentHoverAndButtonActivePress() {
    App app;
    component::Init(&app);
    ThemeSet(&app, ThemeMode::Light);
    Window* win = new Window();
    win->app = &app;
    Arena* arena = ArenaNew();
    Ctx cx{&app, win, arena, {}};

    const Theme& light = ThemeNow(&app);
    El* ghost = component::Button::New(&cx, StrL("light-ghost"))
                    ->Ghost()
                    ->Label(StrL("Ghost"))
                    ->IntoEl();
    utassert(
        ghost->style.hasHoverBg && ghost->style.hasActiveBg &&
        SameButtonColor(ghost->style.hoverBg.color, light.tokens.accent.color));
    utassert(ghost->style.hasHoverFg &&
             SameButtonColor(ghost->style.hoverFg, light.accentFg));
    utassert(SameButtonColor(ghost->style.activeBg.color,
                             light.tokens.buttonActive.color));

    El* selected = component::Button::New(&cx, StrL("selected-ghost"))
                       ->Ghost()
                       ->Selected(true)
                       ->IntoEl();
    utassert(SameButtonColor(selected->style.bg.color,
                             light.tokens.secondaryActive.color));

    ThemeSet(&app, ThemeMode::Dark);
    const Theme& dark = ThemeNow(&app);
    El* darkGhost =
        component::Button::New(&cx, StrL("dark-ghost"))->Ghost()->IntoEl();
    Background darkHover = BackgroundOpacity(dark.tokens.accent, 0.5f);
    utassert(SameButtonColor(darkGhost->style.hoverBg.color, darkHover.color));

    WindowKeyedFree(win);
    delete win;
    ArenaDelete(arena);
    EntityDropAll(&app);
    AppGlobalClear(&app);
}

struct ToggleGroupHarness {
    int calls = 0;
    int count = 0;
    bool checked[8] = {};

    static El* Render(ToggleGroupHarness*, Ctx* cx) { return Div(cx->a); }
    static void OnChange(ToggleGroupHarness* self, Ctx*,
                         const component::ToggleGroupEvent* event) {
        self->calls++;
        self->count = std::min(event->count, 8);
        for (int i = 0; i < self->count; i++) {
            self->checked[i] = event->checked[i];
        }
    }
};

static void SourceToggleAndSegmentedGroupKeepStateAndGeometry() {
    App app;
    component::Init(&app);
    Window* win = new Window();
    win->app = &app;
    Arena* arena = ArenaNew();
    Entity<ToggleGroupHarness> harness = EntityNew<ToggleGroupHarness>(&app);
    Ctx cx{&app, win, arena, harness.id};
    const Theme& theme = ThemeNow(&app);

    component::Toggle* checked = component::Toggle::New(&cx, StrL("checked"))
                                     ->Label(StrL("Bold"))
                                     ->Icon(IconName::Check)
                                     ->Checked(true)
                                     ->Outline()
                                     ->WithSize(UiSize::Small);
    El* checkedEl = checked->IntoEl();
    utassert(checked->variant == component::ToggleVariant::Outline);
    utassertnear(checkedEl->style.height, 24.f);
    utassertnear(checkedEl->style.minW, 24.f);
    utassert(SameButtonColor(checkedEl->StyleStates()->refine.bg.color,
                             theme.tokens.accent.color));
    utassert(ButtonChildCount(checkedEl) == 2);

    component::ToggleGroup* group =
        component::ToggleGroup::New(&cx, StrL("source-toggle-group"))
            ->Segmented()
            ->Outline()
            ->WithSize(UiSize::Medium)
            ->OnClick(Listen(&cx, &ToggleGroupHarness::OnChange))
            ->Child(component::Toggle::New(&cx, StrL("left"))->Checked(true))
            ->Child(component::Toggle::New(&cx, StrL("right")));
    El* root = group->IntoEl();
    utassert(ButtonChildCount(root) == 1);
    El* row = root->first;
    utassert(ButtonChildCount(row) == 2);
    utassertnear(row->first->style.corners.tr, 0.f);
    utassertnear(row->first->next->style.corners.tl, 0.f);
    utassertnear(row->first->next->style.borderL, 0.f);

    IdsCollect(root);
    AccessibilityCollect(root, &win->accessibility);
    const AccessibilityNode* second = ButtonAt(win->accessibility, 1);
    utassert(second != nullptr);
    if (second) {
        utassert(WindowAccessibilityPerform(win, second->id,
                                            AccessibilityAction::Default));
    }
    ToggleGroupHarness* state = harness.Get(&app);
    utassert(state && state->calls == 1 && state->count == 2);
    utassert(state && state->checked[0] && state->checked[1]);

    VecReset(win->accessibility);
    WindowKeyedFree(win);
    delete win;
    ArenaDelete(arena);
    EntityDropAll(&app);
    AppGlobalClear(&app);
}

static void ButtonGroupsAssignSourceCornersWithoutAWrapperClip() {
    App app;
    component::Init(&app);
    Window* win = new Window();
    win->app = &app;
    Arena* arena = ArenaNew();
    Ctx cx{&app, win, arena, {}};
    El* root = component::ButtonGroup::New(&cx, StrL("corners"))
                   ->Outline()
                   ->Child(component::Button::New(&cx, StrL("one"))
                               ->Label(StrL("One")))
                   ->Child(component::Button::New(&cx, StrL("two"))
                               ->Label(StrL("Two")))
                   ->IntoEl();
    utassert(!root->style.hasCorners);
    utassert(ButtonChildCount(root) == 2);
    utassertnear(root->first->style.corners.tr, 0.f);
    utassertnear(root->first->style.corners.tl, ThemeNow(&app).radius);
    utassertnear(root->first->next->style.corners.tl, 0.f);
    utassertnear(root->first->next->style.corners.tr, ThemeNow(&app).radius);

    WindowKeyedFree(win);
    delete win;
    ArenaDelete(arena);
    EntityDropAll(&app);
    AppGlobalClear(&app);
}

// dropdown_button.rs: the action half takes the left corners and all four
// edges, the caret the right corners and every edge but the left. button.rs
// gives the edges a width only on the Default variant or an outlined one, so
// a primary split has no border at all — no seam between its halves.
static void ASplitButtonBordersOnlyTheVariantsRustBorders() {
    App app;
    component::Init(&app);
    Window* win = new Window();
    win->app = &app;
    Arena* arena = ArenaNew();
    Ctx cx{&app, win, arena, {}};
    float radius = ThemeNow(&app).radius;
    auto split = [&](bool primary, bool outline) {
        component::DropdownButton* d =
            component::DropdownButton::New(&cx, StrL("split"))
                ->Button_(component::Button::New(&cx, StrL("action"))
                              ->Label(StrL("Actions")))
                ->Menu(component::PopupMenu::New(&cx, StrL("split-menu")));
        if (primary) {
            d->WithVariant(component::ButtonVariant::Primary);
        }
        if (outline) {
            d->Outline();
        }
        return d->IntoEl();
    };
    auto edges = [](const El* e) {
        return (e->style.borderT > 0) + (e->style.borderR > 0) +
               (e->style.borderB > 0) + (e->style.borderL > 0) +
               (e->style.border > 0);
    };
    El* row = split(true, false);
    utassert(ButtonChildCount(row) == 2);
    const El* action = row->first;
    // The caret sits inside the dropdown menu's trigger wrapper.
    const El* caret = row->first->next;
    while (caret && caret->first && !caret->style.hasCorners) {
        caret = caret->first;
    }
    utassert(action && edges(action) == 0);
    utassert(caret && edges(caret) == 0);
    utassertnear(action->style.corners.tl, radius);
    utassertnear(action->style.corners.bl, radius);
    utassertnear(action->style.corners.tr, 0.f);
    utassert(caret && caret->style.corners.tr == radius &&
             caret->style.corners.br == radius && caret->style.corners.tl == 0);

    for (int outline = 0; outline < 2; outline++) {
        row = split(outline == 1, outline == 1);
        action = row->first;
        caret = row->first->next;
        while (caret && caret->first && !caret->style.hasCorners) {
            caret = caret->first;
        }
        utassert(action && edges(action) == 4 && action->style.borderL > 0);
        utassert(caret && edges(caret) == 3 && caret->style.borderL == 0);
    }
    // A plain primary button has no border either; Default does.
    utassert(edges(component::Button::New(&cx, StrL("p"))
                       ->WithVariant(component::ButtonVariant::Primary)
                       ->IntoEl()) == 0);
    utassert(edges(component::Button::New(&cx, StrL("d"))->IntoEl()) > 0);

    WindowKeyedFree(win);
    delete win;
    ArenaDelete(arena);
    EntityDropAll(&app);
    AppGlobalClear(&app);
}

// Per-side borders on a rounded box follow its corners, as GPUI's quad
// draws them: a box bordered on three sides keeps a round corner on the
// fourth side rather than having a straight line painted across it.
static void PerSideBordersFollowRoundedCorners() {
#if !GPUI_OS_WASM
    App* app = AppNew();
    if (!app) {
        return;
    }
    Arena* arena = ArenaNew();
    PaintCtx paint = {};
    paint.pa = app->paint;
    paint.app = app;
    paint.opacity = 1;
    if (PaintTargetBeginOffscreen(&paint, 64, 32)) {
        Rgba red = Rgba8(255, 0, 0, 255);
        El* box = Div(arena)->W(40)->H(24)->Radius(8);
        box->BorderT(1, red)->BorderB(1, red)->BorderL(1, red);
        LayoutEl(nullptr, box, 0, 0, 64, 32, 14, Rgba{});
        PaintEl(&paint, box);
        uint8_t* px = (uint8_t*)Alloc(arena, 64 * 32 * 4);
        utassert(PaintTargetEndOffscreen(&paint, px));
        auto alpha = [&](int x, int y) { return px[(y * 64 + x) * 4 + 3]; };
        auto redAt = [&](int x, int y) {
            const uint8_t* p = px + (y * 64 + x) * 4;
            return p[3] > 200 && p[2] > 200 && p[1] < 60;
        };
        // Outside each rounded corner: nothing, including the top-right one
        // whose side has no border.
        utassert(alpha(0, 0) == 0);
        utassert(alpha(39, 0) == 0);
        utassert(alpha(39, 23) == 0);
        // The bordered sides are drawn along their straight runs.
        utassert(redAt(0, 12));
        utassert(redAt(20, 0));
        utassert(redAt(20, 23));
        // And the side with no border is not.
        utassert(!redAt(39, 12));
    }
    TextMeasClear(&paint);
    ArenaDelete(arena);
    AppFree(app);
#endif
}

// Window::paint_quad snaps each edge of an element's quad to a device
// pixel, so two boxes that meet at a fractional x share one pixel boundary.
// Unsnapped, both antialiased the pixel they split and the background showed
// through it as a seam — the line between a primary split button's halves.
static void AdjacentBoxesMeetWithoutASeam() {
#if !GPUI_OS_WASM
    App* app = AppNew();
    if (!app) {
        return;
    }
    Arena* arena = ArenaNew();
    PaintCtx paint = {};
    paint.pa = app->paint;
    paint.app = app;
    paint.opacity = 1;
    if (PaintTargetBeginOffscreen(&paint, 64, 16)) {
        Rgba black = Rgba8(0, 0, 0, 255);
        El* row = Div(arena)->FlexRow()->PadX(3.3f)->H(16);
        row->Child(Div(arena)->W(20.4f)->H(16)->Bg(black)->Radius(4));
        row->Child(Div(arena)->W(20.f)->H(16)->Bg(black)->Radius(4));
        LayoutEl(nullptr, row, 0, 0, 64, 16, 14, Rgba{});
        PaintEl(&paint, row);
        uint8_t* px = (uint8_t*)Alloc(arena, 64 * 16 * 4);
        utassert(PaintTargetEndOffscreen(&paint, px));
        // The halves meet at x = 23.7: every pixel across the join, on the
        // middle row, is fully covered.
        for (int x = 21; x <= 26; x++) {
            utassert(px[(8 * 64 + x) * 4 + 3] == 255);
        }
        // The outer edges land on whole pixels too: 3.3 rounds to 3.
        utassert(px[(8 * 64 + 2) * 4 + 3] == 0);
        utassert(px[(8 * 64 + 3) * 4 + 3] == 255);
    }
    TextMeasClear(&paint);
    ArenaDelete(arena);
    AppFree(app);
#endif
}

// styled.rs focus_style with FocusLine::Inside(color): with the outer ring
// off, a focused borderless element draws its 1px line FOCUS_LINE_GAP inside
// its edge in the colour it was given; a filled button gives its normal
// foreground at FOCUS_LINE_OPACITY.
static void AnInsideFocusLineTakesTheColourItWasGiven() {
#if !GPUI_OS_WASM
    App* app = AppNew();
    if (!app) {
        return;
    }
    RuntimeStyle style = RuntimeStyleNow(app);
    style.focusRing = false;
    RuntimeStyleInstall(app, style);
    Arena* arena = ArenaNew();
    PaintCtx paint = {};
    paint.pa = app->paint;
    paint.app = app;
    paint.opacity = 1;
    paint.focusId = 7;
    if (PaintTargetBeginOffscreen(&paint, 64, 32)) {
        Rgba red = Rgba8(255, 0, 0, 255);
        El* box = Div(arena)
                      ->W(40)
                      ->H(24)
                      ->FocusId(7)
                      ->FocusRing(true)
                      ->FocusLineStyle(FocusLine::Inside, red);
        LayoutEl(nullptr, box, 0, 0, 64, 32, 14, Rgba{});
        PaintEl(&paint, box);
        uint8_t* px = (uint8_t*)Alloc(arena, 64 * 32 * 4);
        utassert(PaintTargetEndOffscreen(&paint, px));
        auto redAt = [&](int x, int y) {
            const uint8_t* p = px + (y * 64 + x) * 4;
            return p[3] > 100 && p[2] > 150 && p[1] < 80 && p[0] < 80;
        };
        // The line runs 2px in from the edge, along the middle of each side,
        // and not on the edge itself.
        utassert(redAt(20, 2));
        utassert(redAt(2, 12));
        utassert(px[(12 * 64 + 0) * 4 + 3] == 0);
    }
    TextMeasClear(&paint);
    ArenaDelete(arena);
    AppFree(app);
#endif
}

// button.rs: a filled button's focus line is Inside(normal fg at
// FOCUS_LINE_OPACITY), and a ghost's sits on its edge with no colour of its
// own.
static void AFilledButtonGivesItsFocusLineItsForeground() {
    App app;
    component::Init(&app);
    Arena* arena = ArenaNew();
    Ctx cx{&app, nullptr, arena, {}};
    const Theme& th = ThemeNow(&app);
    El* primary = component::Button::New(&cx, StrL("primary"))
                      ->Primary()
                      ->Label(StrL("Go"))
                      ->IntoEl();
    utassert((FocusLine)primary->style.focusLine == FocusLine::Inside);
    utassert(primary->style.hasFocusLineColor);
    Rgba want = RgbaOpacity(th.buttonPrimaryFg, component::kFocusLineOpacity);
    utassert(primary->style.focusLineColor.r == want.r &&
             primary->style.focusLineColor.g == want.g &&
             primary->style.focusLineColor.b == want.b &&
             primary->style.focusLineColor.a == want.a);
    El* ghost = component::Button::New(&cx, StrL("ghost"))
                    ->Ghost()
                    ->Label(StrL("Go"))
                    ->IntoEl();
    utassert((FocusLine)ghost->style.focusLine == FocusLine::Edge);
    ArenaDelete(arena);
    AppGlobalClear(&app);
}

// style.rs Style::paint under debug_assertions: a debug_below box and an
// unmarked child painted inside it are both outlined in red, 1px inside
// their bounds, and nothing outside the outline is painted.
static void DebugBelowOutlinesEveryElementUnderIt() {
#if !GPUI_OS_WASM && !defined(NDEBUG)
    App* app = AppNew();
    if (!app) {
        return;
    }
    Arena* arena = ArenaNew();
    PaintCtx paint = {};
    paint.pa = app->paint;
    paint.app = app;
    paint.opacity = 1;
    if (PaintTargetBeginOffscreen(&paint, 64, 32)) {
        El* child = Div(arena)->W(20)->H(10);
        El* root = Div(arena)->W(48)->H(24)->Pad(4)->Child(child);
        root->DebugBelow();
        LayoutEl(nullptr, root, 0, 0, 64, 32, 14, Rgba{});
        PaintEl(&paint, root);
        uint8_t* px = (uint8_t*)Alloc(arena, 64 * 32 * 4);
        utassert(PaintTargetEndOffscreen(&paint, px));
        auto redAt = [&](int x, int y) {
            const uint8_t* p = px + (y * 64 + x) * 4;
            return p[3] > 200 && p[2] > 200 && p[1] < 60 && p[0] < 60;
        };
        utassert(redAt(0, 12) && redAt(47, 12));
        utassert(redAt(4, 8) && redAt(14, 4));
        utassert(px[(18 * 64 + 30) * 4 + 3] == 0);
        utassert(px[(28 * 64 + 60) * 4 + 3] == 0);
        utassert(paint.debugBelow == 0);
    }
    TextMeasClear(&paint);
    ArenaDelete(arena);
    AppFree(app);
#endif
}

static void ClipboardButtonsAcceptTheSharedSizeContract() {
    App app;
    component::Init(&app);
    Arena* arena = ArenaNew();
    Ctx cx{&app, nullptr, arena, {}};
    component::Clipboard* clipboard =
        component::Clipboard::New(&cx, StrL("copy"));
    utassert(clipboard->size == UiSize::XSmall);
    utassert(clipboard->WithSize(UiSize::Medium)->size == UiSize::Medium);
    ArenaDelete(arena);
    AppGlobalClear(&app);
}

static void AnOpenTriggerIsStoredApartFromASelectedOne() {
    App app;
    component::Init(&app);
    Arena* arena = ArenaNew();
    Ctx cx{&app, nullptr, arena, {}};
    component::Button* opened = component::Button::New(&cx, StrL("trigger"))
                                    ->Open(true);
    utassert(opened->open);
    utassert(!opened->selected);
    utassert(opened->ShowsSelectedStyle());

    component::Button* selected = component::Button::New(&cx, StrL("trigger"))
                                      ->Selected(true);
    utassert(selected->selected);
    utassert(!selected->open);
    utassert(selected->ShowsSelectedStyle());

    utassert(!component::Button::New(&cx, StrL("trigger"))
                  ->ShowsSelectedStyle());

    El* openEl = component::Button::New(&cx, StrL("open-ghost"))
                     ->Ghost()
                     ->Open(true)
                     ->IntoEl();
    El* selectedEl = component::Button::New(&cx, StrL("selected-ghost"))
                         ->Ghost()
                         ->Selected(true)
                         ->IntoEl();
    utassert(
        SameButtonColor(openEl->style.bg.color, selectedEl->style.bg.color));
    ArenaDelete(arena);
    AppGlobalClear(&app);
}

void TestButtonGroup() {
    TestSuite("button_group");
    BaseButtonCentersOrdinaryChildGeometry();
    BaseTabAndToggleCenterOrdinaryChildGeometry();
    SelectionEventsAreOrderedAndNotWordSized();
    SourceButtonVariantsRoundingAndIconsRemainConcrete();
    GhostButtonsUseAccentHoverAndButtonActivePress();
    SourceToggleAndSegmentedGroupKeepStateAndGeometry();
    ButtonGroupsAssignSourceCornersWithoutAWrapperClip();
    ASplitButtonBordersOnlyTheVariantsRustBorders();
    PerSideBordersFollowRoundedCorners();
    AdjacentBoxesMeetWithoutASeam();
    AnInsideFocusLineTakesTheColourItWasGiven();
    AFilledButtonGivesItsFocusLineItsForeground();
    DebugBelowOutlinesEveryElementUnderIt();
    ClipboardButtonsAcceptTheSharedSizeContract();
    AnOpenTriggerIsStoredApartFromASelectedOne();
}
