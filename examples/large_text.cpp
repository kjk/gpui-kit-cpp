/* crates/story/examples/large-text.rs — ten thousand paragraphs in one
   textarea, and what the editor costs when the document is that big.

   The document is the same CJK paragraph repeated ten thousand times, which
   is about 2.7 MB and 10,000 lines. What it exercises is the display map: the
   rows the frame can show are the only ones built, the caret's row and column
   come off the rope rather than off a scan of the text, and turning soft wrap
   on re-measures nothing that is not on screen.

   The status row under it is the Rust one: a Soft Wrap toggle on the left,
   and on the right the caret's `line:column (offset c)` as a button that
   opens the Go to line dialog — an Input placeheld with the current position,
   parsed as `line` or `line:column`, both counted from one the way Rust's
   `saturating_sub(1)` does. */

#include "gpui.h"

using namespace gpui;

// The paragraph large-text.rs repeats, and how many times it repeats it.
static const char* kParagraph =
    "这是一个中文演示段落，用于展示更多的 [Markdown GFM] "
    "内容。您可以在此尝试使用使用**粗体**、*斜体*和`代码`等样式。これは日本語の"
    "デモ段落です。Markdown "
    "の多言語サポートを示すためのテキストが含まれています。例えば、、**ボールド"
    "**、_イタリック_、および`コード`のスタイルなどを試すことができます。\n";
static const int kRepeats = 10000;

struct LargeTextApp {
    InputState editor;
    InputState goToLine;
    bool softWrap = false;
    bool dialogOpen = false;

    static El* Render(LargeTextApp* self, Ctx* cx);
};

static void ToggleSoftWrap(LargeTextApp* self, Ctx* cx, const ClickEvent*) {
    self->softWrap = !self->softWrap;
    self->editor.softWrap = self->softWrap;
    Notify(cx);
}

static void OpenGoTo(LargeTextApp* self, Ctx* cx, const ClickEvent*) {
    // The placeholder is the position the caret is on, which is what Rust
    // sets on the state as the dialog opens.
    RopePoint at = InputCursorPosition(&self->editor);
    InputSetPlaceholder(&self->goToLine,
                        StrDup(fmt("%d:%d", at.row, at.column)));
    InputSetValue(&self->goToLine, Str{});
    self->goToLine.focused = true;
    self->dialogOpen = true;
    Notify(cx);
}

static void CloseGoTo(LargeTextApp* self, Ctx* cx, const ClickEvent*) {
    self->dialogOpen = false;
    self->goToLine.focused = false;
    self->editor.focused = true;
    Notify(cx);
}

// `line` or `line:column`, one-based, and anything else leaves the caret.
static void ConfirmGoTo(LargeTextApp* self, Ctx* cx, const ClickEvent* ev) {
    Str query = InputValue(&self->goToLine);
    int line = 0;
    int column = 1;
    int at = 0;
    bool any = false;
    while (at < len(query) && query.s[at] >= '0' && query.s[at] <= '9') {
        line = line * 10 + (query.s[at] - '0');
        at++;
        any = true;
    }
    if (!any) {
        return;
    }
    if (at < len(query) && query.s[at] == ':') {
        at++;
        column = 0;
        while (at < len(query) && query.s[at] >= '0' && query.s[at] <= '9') {
            column = column * 10 + (query.s[at] - '0');
            at++;
        }
    }
    int row = line > 0 ? line - 1 : 0;
    int col = column > 0 ? column - 1 : 0;
    Str text = InputValue(&self->editor);
    int offset = RopeLineStartOffset(text, row) + col;
    InputMoveTo(&self->editor, cx, RopeClipOffset(text, offset, Bias::Left));
    CloseGoTo(self, cx, ev);
}

El* LargeTextApp::Render(LargeTextApp* self, Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    // The dialog's field takes the keyboard while it is up; the document has
    // it the rest of the time.
    cx->win->input = self->dialogOpen ? &self->goToLine : &self->editor;

    WinSize win = WindowSize(cx->win);
    // `Textarea::new(&editor).bordered(false).h(relative(1.))` over a status
    // row of its own height.
    El* body = component::Textarea::New(cx, StrL("source"), &self->editor)
                   ->H(win.dipH - 33)
                   ->SoftWrap(self->softWrap)
                   ->IntoEl();

    RopePoint at = InputCursorPosition(&self->editor);
    int cursor = InputCursor(&self->editor);
    El* status = Div(a)
                     ->FlexRow()
                     ->W(kFill)
                     ->ItemsCenter()
                     ->JustifyBetween()
                     ->PadX(16)
                     ->PadY(6)
                     ->Font(14)
                     ->Fg(th.mutedFg)
                     ->Bg(th.tokens.secondary)
                     ->BorderT(1, th.border);
    status->Child(Div(a)->FlexRow()->Gap(12)->Child(
        component::Button::New(cx, StrL("soft-wrap"))
            ->Ghost()
            ->WithSize(UiSize::XSmall)
            ->Label(StrL("Soft Wrap"))
            ->Selected(self->softWrap)
            ->OnClick(Listen(cx, &ToggleSoftWrap))
            ->IntoEl()));
    status->Child(
        component::Button::New(cx, StrL("line-column"))
            ->Ghost()
            ->WithSize(UiSize::XSmall)
            ->Label(StrDup(a, fmt("%d:%d (%d c)", at.row, at.column, cursor)))
            ->OnClick(Listen(cx, &OpenGoTo))
            ->IntoEl());

    El* root = Div(a)
                   ->FlexCol()
                   ->SizeFull()
                   ->Bg(th.tokens.background)
                   ->Child(body)
                   ->Child(status);
    if (self->dialogOpen) {
        root->Child(component::Dialog::New(cx)
                        ->Open(true)
                        ->Title(StrL("Go to line"))
                        ->Body(component::Input::New(cx, StrL("go-to-line"),
                                                     &self->goToLine)
                                   ->IntoEl())
                        ->Confirm()
                        ->OnClose(Listen(cx, &CloseGoTo))
                        ->OnCancel(Listen(cx, &CloseGoTo))
                        ->OnOk(Listen(cx, &ConfirmGoTo))
                        ->IntoEl(win));
    }
    return root;
}

int GpuiMain(int argc, char** argv) {
    (void)argc;
    (void)argv;
    App* app = AppNew();
    component::Init(app);
    Entity<LargeTextApp> view = EntityNew<LargeTextApp>(app);
    LargeTextApp* self = view.Get(app);
    self->editor.kind = InputKind::Textarea;
    self->editor.mode.kind = LayoutModeKind::PlainText;
    self->editor.mode.tabSize = 4;
    self->editor.softWrap = false;
    InputSetPlaceholder(&self->editor, StrL("Enter your code here..."));
    // "…".repeat(10000): built once, here, rather than in the frame.
    Str one = Str(kParagraph);
    StrBuilder sb;
    for (int i = 0; i < kRepeats; i++) {
        sb.Append(one);
    }
    Str text = sb.TakeStr();
    InputSetValue(&self->editor, text);
    StrFree(text);
    self->editor.focused = true;
    Window* win = WindowOpenView(app, StrL("Large Text Editor"), 1000, 800,
                                 view.id, WinOpts{});
    (void)win;
    int rc = AppRun(app);
    AppFree(app);
    return rc;
}
