/* Structural and layout coverage for crates/ui/src/form/{form,field}.rs. */

#include "Test.h"

using namespace gpui::component;

static void FieldBuilderAndStandaloneFieldKeepSourceState() {
    Arena* arena = ArenaNew();
    El* control = Div(arena);
    El* label = TextEl(arena, StrL("Custom"));
    gpui::component::Field fld = field(control)
                                     .Label(label)
                                     .Description(StrL("Help"))
                                     .Required()
                                     .Visible(false)
                                     .LabelIndent(false)
                                     .Align(FieldAlign::End)
                                     .ColSpan(2)
                                     .ColStart(1)
                                     .ColEnd(3);
    utassert(fld.control == control);
    utassert(fld.label.kind == FieldBuilderKind::Element);
    utassert(fld.label.element == label);
    utassert(fld.description.kind == FieldBuilderKind::String);
    utassert(base::StrEq(fld.description.string, StrL("Help")));
    utassert(fld.required && !fld.visible && !fld.labelIndent);
    utassert(fld.align == FieldAlign::End);
    utassert(fld.colSpan == 2 && fld.colStart == 1 && fld.colEnd == 3);
    ArenaDelete(arena);
}

static void FormAxesUseSourceSpacingAndLabelWidths() {
    App app;
    component::Init(&app);
    Window* win = new Window();
    Arena* arena = ArenaNew();
    win->app = &app;
    Ctx cx = {&app, win, arena, {}};

    Form* horizontal = h_form(&cx);
    horizontal->Child(field(Div(arena)->H(20)).Label(StrL("Name")));
    El* hRoot = horizontal->IntoEl();
    utassert(horizontal->horizontal);
    utassertnear(hRoot->style.gapX, 24.f);
    utassertnear(hRoot->style.gapY, 8.f);
    El* hField = hRoot->first;
    El* hHead = hField ? hField->first : nullptr;
    El* hLabel = hHead ? hHead->first : nullptr;
    utassert(hLabel && hLabel != hHead->last);
    utassertnear(hLabel->style.width, 140.f);
    utassertnear(hLabel->style.flexShrink, 0.f);

    Form* vertical = v_form(&cx)->WithSize(UiSize::Small);
    vertical->Child(field(Div(arena)->H(20)).Label(StrL("Name")));
    El* vRoot = vertical->IntoEl();
    utassert(!vertical->horizontal);
    utassertnear(vRoot->style.gapX, 18.f);
    utassertnear(vRoot->style.gapY, 6.f);
    El* vLabel = vRoot->first->first->first;
    utassert(vLabel && vLabel->style.width == kAuto);

    Form* large = v_form(&cx)->WithSize(UiSize::Large);
    large->Child(field(Div(arena)));
    El* largeRoot = large->IntoEl();
    utassertnear(largeRoot->style.gapX, 36.f);
    utassertnear(largeRoot->style.gapY, 12.f);

    delete win;
    ArenaDelete(arena);
    AppGlobalClear(&app);
}

static void HorizontalLabelIndentExistsWithoutLabel() {
    App app;
    component::Init(&app);
    Window* win = new Window();
    Arena* arena = ArenaNew();
    win->app = &app;
    Ctx cx = {&app, win, arena, {}};

    Form* indented = h_form(&cx);
    indented->Child(field(Div(arena)->H(20)));
    El* head = indented->IntoEl()->first->first;
    utassert(head && head->first && head->last);
    utassert(head->first != head->last);
    utassertnear(head->first->style.width, 140.f);

    Form* flush = h_form(&cx);
    flush->Child(field(Div(arena)->H(20)).LabelIndent(false));
    head = flush->IntoEl()->first->first;
    utassert(head && head->first == head->last);

    delete win;
    ArenaDelete(arena);
    AppGlobalClear(&app);
}

static void FormConventionsExposeLabelLayoutAndFooter() {
    App app;
    component::Init(&app);
    Window* win = new Window();
    Arena* arena = ArenaNew();
    win->app = &app;
    Ctx cx = {&app, win, arena, {}};

    El* actions = Div(arena)->H(20);
    Form* form = Form::New(&cx)
                     ->LabelLayout(Axis::Horizontal)
                     ->Columns(2)
                     ->Footer(actions);
    form->Child(field(Div(arena)).Label(StrL("One")));
    El* root = form->IntoEl();
    El* footer = root ? root->last : nullptr;
    utassert(form->horizontal && form->columns == 2);
    utassert(form->footer == actions);
    utassert(footer && footer->first == actions);
    utassert(footer && footer->style.width == kFill && footer->style.minW == 0);

    delete win;
    ArenaDelete(arena);
    AppGlobalClear(&app);
}

// form/tests.rs form_applies_styled_refinements (#3091): a caller's
// padding and row gap reach the form. Rust's Form records Styled
// refinements and now applies them to its root; here the form hands back
// that root, and styling it is the refinement.
static void FormAppliesStyledRefinements() {
    App app;
    component::Init(&app);
    Window* win = new Window();
    Arena* arena = ArenaNew();
    win->app = &app;
    Ctx cx = {&app, win, arena, {}};

    El* controls[2][2] = {};
    for (int styled = 0; styled < 2; styled++) {
        controls[styled][0] = Div(arena)->W(kFill)->H(20);
        controls[styled][1] = Div(arena)->W(kFill)->H(20);
        Form* form = v_form(&cx);
        form->Child(field(controls[styled][0]))
            ->Child(field(controls[styled][1]));
        El* root = form->IntoEl();
        if (styled) {
            root->Pad(20)->GapY(30);
        }
        El* page = Div(arena)->W(400)->Child(root);
        LayoutEl(nullptr, page, 0, 0, 400, 400, 14, Rgba{});
    }
    El** plain = controls[0];
    El** styled = controls[1];
    // Padding applied: top and left shifted by 20px.
    utassertnear(styled[0]->x - plain[0]->x, 20.f);
    utassertnear(styled[0]->y - plain[0]->y, 20.f);
    // The gap between fields grows by 30 - 8, the default.
    float plainGap = plain[1]->y - (plain[0]->y + plain[0]->h);
    float styledGap = styled[1]->y - (styled[0]->y + styled[0]->h);
    utassertnear(styledGap - plainGap, 22.f);

    delete win;
    ArenaDelete(arena);
    AppGlobalClear(&app);
}

static bool Holds(const El* root, const El* target) {
    if (root == target) return true;
    for (const El* c = root ? root->first : nullptr; c; c = c->next) {
        if (Holds(c, target)) return true;
    }
    return false;
}

// hidden_fields_are_not_rendered (#3269): a field with visible(false) is not
// in the form at all, and leaves no grid row behind.
static void HiddenFieldsAreNotRendered() {
    App app;
    component::Init(&app);
    Window* win = new Window();
    Arena* arena = ArenaNew();
    win->app = &app;
    Ctx cx = {&app, win, arena, {}};

    El* controls[2][3] = {};
    El* roots[2] = {};
    for (int hide = 0; hide < 2; hide++) {
        Form* form = v_form(&cx);
        for (int ix = 0; ix < 3; ix++) {
            controls[hide][ix] = Div(arena)->W(kFill)->H(20);
            form->Child(field(controls[hide][ix]).Visible(!(hide && ix == 1)));
        }
        roots[hide] = form->IntoEl();
        El* page = Div(arena)->W(400)->Child(roots[hide]);
        LayoutEl(nullptr, page, 0, 0, 400, 400, 14, Rgba{});
    }
    utassert(Holds(roots[0], controls[0][1]));
    utassert(!Holds(roots[1], controls[1][1]));
    utassert(Holds(roots[1], controls[1][2]));
    utassertnear(controls[1][2]->y, controls[0][1]->y);

    delete win;
    ArenaDelete(arena);
    AppGlobalClear(&app);
}

// field.rs: Field is an element of its own. Outside a Form it renders with
// FieldProps::default() — vertical, Medium (a 4 px gap, halved between the
// parts), a text_sm label — which is the field a one-field v_form draws.
static void AFieldRendersOutsideAForm() {
    App app;
    component::Init(&app);
    Window* win = new Window();
    Arena* arena = ArenaNew();
    win->app = &app;
    Ctx cx = {&app, win, arena, {}};

    El* loneControl = Div(arena)->W(kFill)->H(20);
    El* lone = field(loneControl)
                   .Label(StrL("Name"))
                   .Description(StrL("Help"))
                   .Required()
                   .IntoEl(&cx);
    El* formControl = Div(arena)->W(kFill)->H(20);
    El* form = v_form(&cx)
                   ->Child(field(formControl)
                               .Label(StrL("Name"))
                               .Description(StrL("Help"))
                               .Required())
                   ->IntoEl();
    utassert(lone && lone->style.display != Display::None);
    utassertnear(lone->style.gapY, 2.f);
    El* head = lone->first;
    El* label = head ? head->first : nullptr;
    El* text = label ? label->first : nullptr;
    utassert(head && head->style.gapY == 2.f);
    utassert(label && label->style.width == kAuto);
    utassert(text && text->style.fontSize == 14.f);
    // The asterisk follows the label.
    utassert(text && text->next && text->next->kind == ElKind::Text);
    utassert(Holds(lone, loneControl));
    El* desc = head ? head->next : nullptr;
    utassert(desc && desc->first && desc->first->style.fontSize == 12.f);

    El* page = Div(arena)->W(400)->FlexCol()->Child(lone)->Child(form);
    LayoutEl(nullptr, page, 0, 0, 400, 400, 14, Rgba{});
    // Same place inside its own box as inside the form's.
    utassertnear(loneControl->y - lone->y, formControl->y - form->y);
    utassertnear(lone->h, form->h);

    // visible(false) on its own is display: none, Rust's `.hidden()`.
    El* hidden = field(Div(arena)).Visible(false).IntoEl(&cx);
    utassert(hidden->style.display == Display::None);

    delete win;
    ArenaDelete(arena);
    AppGlobalClear(&app);
}

void TestForm() {
    TestSuite("form");
    FieldBuilderAndStandaloneFieldKeepSourceState();
    FormAxesUseSourceSpacingAndLabelWidths();
    HorizontalLabelIndentExistsWithoutLabel();
    FormConventionsExposeLabelLayoutAndFooter();
    FormAppliesStyledRefinements();
    HiddenFieldsAreNotRendered();
    AFieldRendersOutsideAForm();
}
