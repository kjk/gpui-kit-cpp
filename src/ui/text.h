#ifndef GPUI_SRC_UI_TEXT_H_
#define GPUI_SRC_UI_TEXT_H_
/* The themed façade over Base's rich text — crates/ui/src/text.
 *
 * The renderer, the parser, the inline layout and the selection all live in
 * `src/base/text.h` now; `crates/ui/src/text/mod.rs` keeps a compatibility
 * façade and so does this. Every `component::TextView`, `component::MdNode`
 * and `component::HtmlParse` in this tree still names the same thing it did,
 * because the names below are the Base ones re-exported.
 *
 * What stayed on this side is what Rust kept: the theme adapter that derives
 * a Base `TextViewStyle` from the component `Theme` (`base_text_view_style`),
 * and syntax highlighting — `ui/syntax.h` is a scanner rather than the
 * tree-sitter parse Rust runs, and either way it is a themed-layer concern
 * that Base only reaches through the `code_block_highlighter` callback.
 *
 * `install_text_view_defaults` is called from `ThemeSet` / `ThemeSyncBase`,
 * so a themed application's TextViews pick up the palette and the
 * highlighter without naming either. */

#include "base/text.h"
#include "base/text_format.h"
#include "ui/sizing.h"
#include "ui/syntax.h"

namespace gpui {

namespace component {

// `pub use gpui_base::text::{..}` — compat.rs plus the Base re-exports. The
// C++ façade collapses Rust's second, component-level TextViewStyle into the
// Base one: this tree's style never carried a HighlightTheme (ui/syntax.h
// keys its colours off ThemeMode), so there was no second representation to
// fold, and `resolve_component_style` has nothing left to resolve.
using Span = gpui::Span;
using LinkMark = gpui::LinkMark;
using TextMark = gpui::TextMark;
using ImageNode = gpui::ImageNode;
using MarkdownParseContext = gpui::MarkdownParseContext;
using MarkdownNode = gpui::MarkdownNode;
using MarkdownBlockParserFn = gpui::MarkdownBlockParserFn;
using MarkdownBlockRenderFn = gpui::MarkdownBlockRenderFn;
using MarkdownPlugin = gpui::MarkdownPlugin;
using MarkdownBlockParser = gpui::MarkdownBlockParser;
using MarkdownBlockRenderer = gpui::MarkdownBlockRenderer;
using MarkdownExtensions = gpui::MarkdownExtensions;
using TextViewPlugin = gpui::TextViewPlugin;
using TextViewSetupFn = gpui::TextViewSetupFn;
using MdRun = gpui::MdRun;
using MdKind = gpui::MdKind;
using MdNode = gpui::MdNode;
using MdPlugin = gpui::MdPlugin;
using MdPluginNode = gpui::MdPluginNode;
using MdPluginParseFn = gpui::MdPluginParseFn;
using MdPluginRenderFn = gpui::MdPluginRenderFn;
using CodeBlock = gpui::CodeBlock;
using CodeHighlight = gpui::CodeHighlight;
using CodeBlockHighlighterFn = gpui::CodeBlockHighlighterFn;
using CodeBlockActionsFn = gpui::CodeBlockActionsFn;
using TableActionsFn = gpui::TableActionsFn;
using TableData = gpui::TableData;
using HeadingFontSizeFn = gpui::HeadingFontSizeFn;
using TextViewStyle = gpui::TextViewStyle;
using TextViewDefaults = gpui::TextViewDefaults;
using TextViewFormat = gpui::TextViewFormat;
using TextViewState = gpui::TextViewState;
using TextViewLayoutState = gpui::TextViewLayoutState;
using TextView = gpui::TextView;
using Text = gpui::Text;
using Minifier = gpui::Minifier;
using HtmlInlineTag = gpui::HtmlInlineTag;

using gpui::HtmlAttrValue;
using gpui::HtmlMinify;
using gpui::HtmlParse;
using gpui::HtmlParseInlineTag;
using gpui::HtmlParseInto;
using gpui::MdDecodeEntity;
using gpui::MdParse;
using gpui::MdTableToMarkdown;
using gpui::TextViewInitKeys;

// text/mod.rs `markdown(source)` / `html(source)`.
inline TextView* Markdown(Ctx* cx, Str source) {
    return gpui::MarkdownView(cx, source);
}
inline TextView* Html(Ctx* cx, Str source) {
    return gpui::HtmlView(cx, source);
}

// `base_text_view_style`: the component palette as a Base rich-text style.
// The radius the themed table and code block are drawn with rides along as a
// style refinement, which is where Base looks for it.
TextViewStyle UiTextViewStyle(const Theme& theme);

// `component_code_block_highlighter`: ui/syntax.h scanning a fenced block and
// answering the coloured stretches, which is the one thing Base cannot do for
// itself.
void UiCodeBlockHighlighter(void* data, const CodeBlock* block, Arena* a,
                            ArenaVec<CodeHighlight>* out);

// `install_text_view_defaults`: called whenever the theme is set or synced,
// so every TextView starts from the themed style and the themed highlighter.
void TextViewInstallDefaults(App* app);

// text/frontmatter.rs. The parser deliberately accepts only the same small,
// unambiguous YAML mapping subset as upstream; unsupported YAML remains a
// normal fenced-looking YAML code block instead of being misrepresented.
struct FrontmatterPlugin {
    static MarkdownPlugin New();
};

} // namespace component
} // namespace gpui
#endif // GPUI_SRC_UI_TEXT_H_
