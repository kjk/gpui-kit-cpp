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

void TestForm() {
    TestSuite("form");
    FieldBuilderAndStandaloneFieldKeepSourceState();
    FormAxesUseSourceSpacingAndLabelWidths();
    HorizontalLabelIndentExistsWithoutLabel();
    FormConventionsExposeLabelLayoutAndFooter();
}
