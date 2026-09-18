/* The TextView page — crates/base/examples/showcase/components/text_view.rs
 *
 * Base rich text with no themed layer above it: the page derives a
 * TextViewStyle from the example palette and hands it to the view, which is
 * the whole point of the move — a gpui-base application renders Markdown
 * without depending on gpui-kit. */

#include "Showcase.h"
#include "palette.h"
#include "gpui.h"

using namespace gpui;

// Rust includes the story's own fixture. The same document is an asset here,
// with a small sample behind it so the page still reads if the asset roots do
// not carry it.
static const char* kFallbackMarkdown =
    "# Hello, **World**!\n"
    "\n"
    "This is the first paragraph, with **bold**, _italic_ and ~~struck~~ "
    "text, `code`, and a [link](https://github.com/longbridge/gpui-kit) "
    "in it.\n"
    "\n"
    "## Lists\n"
    "\n"
    "- A bullet\n"
    "- Another one\n"
    "  - Nested under it\n"
    "- [x] A finished task\n"
    "- [ ] One still open\n"
    "\n"
    "## A table\n"
    "\n"
    "| Name | Role | Since |\n"
    "| :--- | :---: | ---: |\n"
    "| Alice | Design | 2019 |\n"
    "| Bob | Engineering | 2021 |\n"
    "\n"
    "## Code\n"
    "\n"
    "```cpp\n"
    "int main() {\n"
    "    return 0;\n"
    "}\n"
    "```\n"
    "\n"
    "> A blockquote, greyed and ruled down its left side.\n"
    "\n"
    "---\n"
    "\n"
    "The document scrolls inside a viewport of its own, and a drag selects "
    "across every block in it.\n";

// text_view.rs text_view_style: the palette's roles as a Base rich-text
// style. `selection` is left at the Base default, which is the readable wash
// the token carries.
static TextViewStyle ShowcaseTextViewStyle(ExamplePalette palette) {
    bool isDark = palette.canvas == ExamplePalette::ForDark(true).canvas;
    TextViewStyle style = TextViewStyle::Default();
    gpui::Style code = {};
    code.bg = Background(RgbaHex(palette.elevated));
    style.WithForeground(RgbaHex(palette.foreground))
        .WithMutedForeground(RgbaHex(palette.mutedForeground))
        .WithLink(RgbaHex(palette.Resolve(0x007fff)))
        .WithCodeBackground(RgbaHex(palette.elevated))
        .WithBorder(RgbaHex(palette.border))
        .WithInlineCode(code, StyleFieldBg)
        .WithDark(isDark);
    return style;
}

El* ShowcaseTextView(ShowcaseApp* app, Ctx* cx) {
    Arena* a = cx->a;
    if (!app->textView.IsValid()) {
        TempStr md = AssetsLoadTextTemp(StrL("test.md"));
        Str source =
            md.s && len(md) > 0 ? Str(md.s, len(md)) : Str(kFallbackMarkdown);
        app->textView = TextViewState::Markdown(cx->app, source);
    }
    ExamplePalette palette = PaletteActive();
    TextViewStyle style = ShowcaseTextViewStyle(palette);

    // `div().w_full().h(px(560.)).max_h_full()` around a document that
    // scrolls inside it, so the viewport itself never moves.
    El* document =
        TextView::New(cx, app->textView)->Style(style)->Scrollable()->IntoEl();
    return Div(a)
        ->FlexCol()
        ->W(kFill)
        ->H(560)
        ->MaxH(kFill)
        ->Fg(RgbaHex(palette.foreground))
        ->Child(
            Div(a)->FlexCol()->SizeFull()->MinH(0)->ClipY()->PadX(16)->Child(
                document));
}

SHOWCASE_PAGE(CompTextView, ShowcaseTextView);
