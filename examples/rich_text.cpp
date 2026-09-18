/* component::TextView beyond plain markdown: raw HTML blocks, inline tags,
   links you can click, table column alignment, strikethrough and <mark>.

   The two pages are the same document written twice — once as markdown with
   HTML in it, once as HTML — which is the pair Rust's TextView renders from
   format::markdown and format::html. Clicking a link does not open a browser
   here; the handler puts the href in the status line instead, which is what
   text_view.rs's link_click_handler is for. */

#include "gpui.h"

using namespace gpui;

static const char* kMarkdown =
    "# Rich text\n"
    "\n"
    "Markdown with **bold**, *italic*, `code`, ~~struck out~~ and a\n"
    "[link to the port](https://github.com/kjk/gpui-kit-cpp).\n"
    "\n"
    "Inline HTML is parsed too: <b>bold</b>, <mark>highlighted</mark>,\n"
    "<u>underlined</u> and <a href=\"https://commonmark.org/\">a link</a>.\n"
    "\n"
    "| Left | Center | Right |\n"
    "|:-----|:------:|------:|\n"
    "| one  | two    | 3.00  |\n"
    "| four | five   | 42.50 |\n"
    "\n"
    "An image from the asset roots, inline with the text:\n"
    "![a gradient](gradient.png) — and one fetched over the network,\n"
    "which shows its alt text until it lands and the picture after:\n"
    "![a docs.rs badge](https://docs.rs/gpui-kit/badge.svg). A host\n"
    "that does not resolve keeps its alt text: ![nowhere](https://x/y.png).\n"
    "\n"
    "<div>\n"
    "  <h3>A raw HTML block</h3>\n"
    "  <p>Dropped before this change; now it is parsed into the same tree\n"
    "  the markdown builds, so it renders with the same styles.</p>\n"
    "  <ul><li>a list item</li><li>and <i>another</i></li></ul>\n"
    "</div>\n"
    "\n"
    "```cpp\n"
    "// A fenced block, scanned by ui/syntax.cpp.\n"
    "#include \"gpui.h\"\n"
    "\n"
    "int Add(int a, float b) {\n"
    "    const char* s = \"a string\";\n"
    "    return a + (int)b + 0x2a;  // and a comment\n"
    "}\n"
    "```\n"
    "\n"
    "```python\n"
    "def greet(name: str) -> None:\n"
    "    if name is None:  # nothing to do\n"
    "        return\n"
    "    print(f\"hello {name}\", 42)\n"
    "```\n"
    "\n"
    "> A quote, to end on.\n";

static const char* kHtml =
    "<h1>Rich text</h1>\n"
    "<p>HTML with <b>bold</b>, <i>italic</i>, <code>code</code>,\n"
    "<s>struck out</s> and\n"
    "<a href=\"https://github.com/kjk/gpui-kit-cpp\">a link to the "
    "port</a>.</p>\n"
    "<p><mark>Highlighted</mark> text, <u>underlined</u> text, and an image\n"
    "sized by its tag: <img src=\"gradient.png\" alt=\"[a gradient]\"\n"
    "width=\"60\">.</p>\n"
    "<table>\n"
    "  <thead><tr><th>Left</th><th align=\"center\">Center</th>\n"
    "    <th align=\"right\">Right</th></tr></thead>\n"
    "  <tbody>\n"
    "    <tr><td>one</td><td align=\"center\">two</td>\n"
    "      <td align=\"right\">3.00</td></tr>\n"
    "    <tr><td>four</td><td align=\"center\">five</td>\n"
    "      <td style=\"text-align: right\">42.50</td></tr>\n"
    "  </tbody>\n"
    "</table>\n"
    "<pre><code class=\"language-cpp\">int main() {\n"
    "    return 0;\n"
    "}\n"
    "</code></pre>\n"
    "<ol start=\"3\"><li>third</li><li>fourth</li></ol>\n"
    "<blockquote><p>A quote, to end on.</p></blockquote>\n";

struct RichApp {
    static El* Render(RichApp* self, Ctx* cx);

    bool html = false;
    float scroll = 0;
    // The last link clicked, so the status line can show that the handler ran
    // instead of the desktop opening a browser mid-demo.
    char lastLink[512] = {};
};

static void OnWheel(RichApp* self, Ctx* cx, const ScrollWheelEvent* ev) {
    (void)cx;
    self->scroll -= ev->deltaY;
    if (self->scroll < 0) {
        self->scroll = 0;
    }
}

static void OnToggle(RichApp* self, Ctx* cx, const ClickEvent*) {
    self->html = !self->html;
    self->scroll = 0;
    Notify(cx);
}

// text_view.rs LinkClickHandlerFn: the href arrives as the listener's value.
static void OnLink(RichApp* self, Ctx* cx, const ClickEvent*, intptr_t href) {
    StrCopyZ(self->lastLink, (int)sizeof(self->lastLink),
             href ? (const char*)href : "");
    Notify(cx);
}

El* RichApp::Render(RichApp* self, Ctx* cx) {
    Arena* frame = cx->a;
    const Theme& th = ThemeNow(cx->app);

    El* bar =
        Div(frame)
            ->FlexRow()
            ->Pad(8)
            ->Gap(8)
            ->ItemsCenter()
            ->Child(ButtonEl(frame, 1,
                             self->html ? StrL("Source: HTML")
                                        : StrL("Source: Markdown"))
                        ->OnClick(Listen(cx, &OnToggle)))
            ->Child(TextEl(frame, self->lastLink[0] ? Str(self->lastLink)
                                                    : StrL("click a link"))
                        ->Font(12)
                        ->Fg(th.mutedFg));

    component::TextView* tv =
        self->html ? component::TextView::NewHtml(cx, Str(kHtml))
                   : component::TextView::New(cx, Str(kMarkdown));
    El* doc = tv->Selectable()->OnLink(Listen(cx, &OnLink))->IntoEl();

    El* body = Div(frame)
                   ->Flex1()
                   ->ClipY()
                   ->ScrollY(self->scroll)
                   ->Child(Div(frame)->FlexCol()->Pad(16)->Child(doc));
    return Div(frame)
        ->FlexCol()
        ->SizeFull()
        ->Bg(th.tokens.background)
        ->Child(bar)
        ->Child(Div(frame)->H(1)->Bg(th.border))
        ->Child(body);
}

int GpuiMain(int argc, char** argv) {
    (void)argc;
    (void)argv;
    App* app = AppNew();
    component::Init(app);
    ThemeSet(app, ThemeMode::Dark);
    // Where the images in the two documents are looked up, the same way the
    // icons of any other example are.
    AssetsClear();
    AssetsAddDefaultRoots(StrL("rich_text"));
    AssetsAddRoot(StrL("assets/rich_text"));
    Entity<RichApp> view = EntityNew<RichApp>(app);
    Window* win = WindowOpenView(app, StrL("Rich Text C++"), 820, 760, view.id,
                                 WinOpts{});
    WindowOnScrollWheel(win, ListenTo(view, &OnWheel));
    int rc = AppRun(app);
    AppFree(app);
    return rc;
}
