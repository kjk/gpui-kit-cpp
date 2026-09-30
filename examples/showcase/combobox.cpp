#include "Showcase.h"
#include "gpui.h"

using namespace gpui;

static const char* kFwCombo[] = {"GPUI", "React", "SwiftUI", "Vue"};

static bool Matches(Str label, Str query) {
    return !query || StrContainsI(label, query);
}

static void ToggleCombo(ShowcaseApp* app, Ctx* cx, const ClickEvent*) {
    app->comboboxOpen = !app->comboboxOpen;
    app->comboQuery.focused = app->comboboxOpen;
    app->input.focused = false;
    Notify(cx);
}

static void FocusComboQuery(ShowcaseApp* app, Ctx* cx, const ClickEvent*) {
    app->comboQuery.focused = true;
    app->input.focused = false;
    Notify(cx);
}

static void PickCombo(ShowcaseApp* app, Ctx* cx, const ClickEvent*,
                      intptr_t ix) {
    StrCopyZ(app->comboboxSel, (int)sizeof(app->comboboxSel), kFwCombo[ix]);
    app->comboboxOpen = false;
    app->comboQuery.focused = false;
    Notify(cx);
}

El* ShowcaseCombobox(ShowcaseApp* app, Ctx* cx) {
    Arena* a = cx->a;
    El* trigger = Div(a)
                      ->Id(StrL("combobox-trigger"))
                      ->W(224)
                      ->H(28)
                      ->PadX(8)
                      ->ItemsCenter()
                      ->JustifyBetween()
                      ->Border(1, ExampleRgb(0xd4d4d4))
                      ->Bg(ExampleRgb(0xffffff))
                      ->OnClick(Listen(cx, &ToggleCombo))
                      ->FocusId(HashClickId(StrL("combobox-trigger")))
                      ->HoverBg(ExampleRgb(0xf5f5f5))
                      ->Child(TextEl(a, Str(app->comboboxSel))
                                  ->Font(12)
                                  ->Fg(ExampleRgb(0x171717)))
                      ->Child(ScChevron(cx, false, ExampleRgb(0x737373)));
    El* pop = nullptr;
    if (app->comboboxOpen) {
        pop = Div(a)
                  ->FlexCol()
                  ->W(224)
                  ->Border(1, ExampleRgb(0xd4d4d4))
                  ->Bg(ExampleRgb(0xffffff));
        pop->Child(InputBase::New(cx, StrL("combobox-search"), true)
                       ->OnClick(Listen(cx, &FocusComboQuery))
                       ->FocusId(0)
                       ->W(kFill)
                       ->H(28)
                       ->PadX(8)
                       ->BorderB(1, ExampleRgb(0xe5e5e5))
                       ->Child(Input::New(cx, &app->comboQuery,
                                          ShowcaseEditorStyle())));
        El* list = Div(a)->FlexCol()->W(kFill)->Pad(4);
        for (int i = 0; i < 4; i++) {
            if (!Matches(Str(kFwCombo[i]), Str(InputCStr(&app->comboQuery)))) {
                continue;
            }
            list->Child(Div(a)
                            ->W(kFill)
                            ->H(28)
                            ->PadX(8)
                            ->ItemsCenter()
                            ->HoverBg(ExampleRgb(0xf5f5f5))
                            ->OnClick(Listen(cx, &PickCombo, i))
                            ->Child(TextEl(a, Str(kFwCombo[i]))
                                        ->Font(12)
                                        ->Fg(ExampleRgb(0x171717))));
        }
        pop->Child(list);
    }
    El* combo =
        Combobox::New(cx, StrL("example-combobox"))->W(224)->Child(trigger);
    return Popup::New(cx, StrL("example-combobox-popup"), combo)
        ->Content(pop)
        ->IntoEl();
}

SHOWCASE_PAGE(CompCombobox, ShowcaseCombobox);
