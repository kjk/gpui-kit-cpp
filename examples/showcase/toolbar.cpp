#include "Showcase.h"
#include "gpui.h"

using namespace gpui;

// crates/base/examples/showcase/components/toolbar.rs: two labelled groups of
// commands, separators, and a trailing search field in one Toolbar. Left and
// Right move the focus along it; a command reports itself underneath.

static const char* const kToolbarCommands[] = {"New", "Save", "Undo", "Redo"};

static void OnToolbarCommand(ShowcaseApp* app, Ctx* cx, const ClickEvent*,
                             intptr_t ix) {
    app->toolbarAction = (int)ix;
    Notify(cx);
}

static void OnToolbarSearch(ShowcaseApp* app, Ctx* cx, const ClickEvent*) {
    app->toolbarSearch.focused = true;
    Notify(cx);
}

static El* ToolbarCommand(ShowcaseApp*, Ctx* cx, const char* id, int ix) {
    Arena* a = cx->a;
    Str label = Str(kToolbarCommands[ix]);
    return ScButton(cx, Str(id))
        ->OnClick(Listen(cx, &OnToolbarCommand, (intptr_t)ix))
        ->H(28)
        ->PadX(8)
        ->ItemsCenter()
        ->Border(1, ScBorder())
        ->HoverBg(ExampleRgb(0xf5f5f5))
        ->AriaLabel(label)
        ->Child(TextEl(a, label)->Font(12)->Fg(ScInk()));
}

static El* ToolbarRule(Ctx* cx) {
    // div().w_px().h_5().bg(..)
    return Div(cx->a)->W(1)->H(20)->Bg(ScBorder());
}

El* ShowcaseToolbar(ShowcaseApp* app, Ctx* cx) {
    Arena* a = cx->a;
    if (!app->toolbarSeeded) {
        app->toolbarSeeded = true;
        InputSetPlaceholder(&app->toolbarSearch, StrL("Search"));
    }
    Toolbar* bar = Toolbar::New(cx, StrL("example-toolbar"));
    bar->root->Pad(8)->FlexRow()->ItemsCenter()->Gap(4)->Border(1, ScBorder());
    ToolbarGroup* document = ToolbarGroup::New(cx, StrL("document-commands"))
                                 ->Label(StrL("Document"));
    document->root->Gap(4);
    document->Child(ToolbarCommand(app, cx, "toolbar-new", 0))
        ->Child(ToolbarCommand(app, cx, "toolbar-save", 1));
    ToolbarGroup* history = ToolbarGroup::New(cx, StrL("history-commands"))
                                ->Label(StrL("History"));
    history->root->Gap(4);
    history->Child(ToolbarCommand(app, cx, "toolbar-undo", 2))
        ->Child(ToolbarCommand(app, cx, "toolbar-redo", 3));
    bar->Child(document->IntoEl())
        ->Child(ToolbarRule(cx))
        ->Child(history->IntoEl())
        ->Child(ToolbarRule(cx))
        ->Child(
            InputBase::New(cx, StrL("toolbar-search"), true)
                ->OnClick(Listen(cx, &OnToolbarSearch))
                ->W(160)
                ->H(28)
                ->PadX(8)
                ->ItemsCenter()
                ->Border(1, app->toolbarSearch.focused ? ScInk() : ScBorder())
                ->Child(Input::New(cx, &app->toolbarSearch)));
    Str action =
        app->toolbarAction < 0
            ? StrL("No command yet")
            : StrDup(a, fmt("%s command",
                            Str(kToolbarCommands[app->toolbarAction])));
    return Div(a)
        ->FlexCol()
        ->ItemsCenter()
        ->Gap(8)
        ->Child(bar->IntoEl())
        ->Child(TextEl(a, action)->Font(12)->Fg(ScMutedC()));
}

SHOWCASE_PAGE(CompToolbar, ShowcaseToolbar);
