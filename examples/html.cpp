/* crates/story/examples/html.rs — the HTML source on the left and what
   `TextView::html` makes of it on the right.

   The same two panes as the markdown example, over the same resizable split:
   the editor holds the document, the preview reads its text back every frame,
   and a status bar sits under both. The editor is the code editor with the
   HTML scanner behind it; the preview is `src/ui/html.cpp` folding the tags
   into the tree markdown builds, which is where Rust hands the source to
   html5ever and then to the same node renderer.

   Not ported, and named where it is: the **Selection: Plain / Source** toggle
   in the status bar, which would have to map a selection in the rendered
   document back to the source that produced it — the nodes here carry no
   source offsets. */

#include "gpui.h"

using namespace gpui;

struct HtmlApp {
    InputState source;
    float previewScroll = 0;
    char lastLink[512] = {};
    bool seeded = false;

    static El* Render(HtmlApp* self, Ctx* cx);
};

static void OnLink(HtmlApp* self, Ctx* cx, const ClickEvent*, intptr_t href) {
    StrCopyZ(self->lastLink, (int)sizeof(self->lastLink),
             href ? (const char*)href : "");
    Notify(cx);
}

static void OnPreviewScroll(HtmlApp* self, Ctx* cx, const ScrollEvent* ev) {
    self->previewScroll = ev->offsetY;
    Notify(cx);
}

El* HtmlApp::Render(HtmlApp* self, Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    if (!self->seeded) {
        self->seeded = true;
    }
    Str text = InputValue(&self->source);
    cx->win->input = &self->source;

    // `Editor::new(&state).h(relative(1.)).appearance(false)`: the editor
    // fills its panel, less the status bar under both of them.
    float editorH = WindowSize(cx->win).dipH - 26;
    El* left = Div(a)->FlexCol()->SizeFull()->Child(
        component::Highlighter::New(cx, StrL("source"), &self->source)
            ->H(editorH)
            ->Language(StrL("html"))
            ->IntoEl());

    El* preview = component::TextView::NewHtml(cx, text)
                      ->Selectable()
                      ->OnLink(Listen(cx, &OnLink))
                      ->IntoEl();
    El* right =
        Div(a)
            ->FlexCol()
            ->SizeFull()
            ->ClipY()
            ->ScrollY(self->previewScroll)
            ->ScrollId(HashClickId(StrL("preview")))
            ->OnScroll(Listen(cx, &OnPreviewScroll))
            ->Child(Div(a)->FlexCol()->W(kFill)->PadX(20)->Child(preview));

    El* split = component::Resizable::New(cx, StrL("container"))
                    ->H(kFill)
                    ->Panel(left, 520, 200)
                    ->Grow(right, 200)
                    ->IntoEl();

    component::StatusBar* bar = component::StatusBar::New(cx);
    if (self->lastLink[0]) {
        bar->Left(Str(self->lastLink));
    }

    return Div(a)
        ->FlexCol()
        ->SizeFull()
        ->Bg(th.tokens.background)
        ->Child(Div(a)->Flex1()->W(kFill)->ClipY()->Child(split))
        ->Child(bar->IntoEl());
}

int GpuiMain(int argc, char** argv) {
    (void)argc;
    (void)argv;
    App* app = AppNew();
    component::Init(app);
    AssetsClear();
    AssetsAddDefaultRoots(StrL("html"));
    AssetsAddRoot(StrL("assets/html"));
    Entity<HtmlApp> view = EntityNew<HtmlApp>(app);
    HtmlApp* self = view.Get(app);
    self->source.kind = InputKind::Editor;
    self->source.mode.kind = LayoutModeKind::CodeEditor;
    self->source.mode.tabSize = 4;
    self->source.mode.lineNumber = true;
    InputSetPlaceholder(&self->source, StrL("Enter your HTML here..."));
    TempStr html = AssetsLoadTextTemp(StrL("test.html"));
    InputSetValue(&self->source, html);
    self->source.focused = true;
    Window* win = KitOpenWindow(app, StrL("HTML Render (native)"), 1200, 900,
                                view.id, WinOpts{});
    (void)win;
    int rc = AppRun(app);
    AppFree(app);
    return rc;
}
