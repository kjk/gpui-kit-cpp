#ifndef GPUI_SRC_UI_SYNTAX_H_
#define GPUI_SRC_UI_SYNTAX_H_
/* Code-block highlighting without a grammar — crates/ui/src/highlighter

   Rust parses the fence's language with tree-sitter, runs the language's
   `highlights.scm` over the tree and looks the capture names up in a
   HighlightTheme (registry.rs HIGHLIGHT_NAMES, the colors in
   theme/default-theme.json). There is no tree-sitter here and no room for
   one, so this is a scanner: comments, strings, numbers, keywords and the
   few things position alone can tell — a name before `(` is a call, a name
   before `:` in JSON is a property, a name after `<` is a tag.

   That answers a strict subset of the capture names, and the ones it does
   answer take their colors from the same two theme tables, so a fenced block
   here comes out the color a fenced block does there. What it cannot do is
   what a parse buys: a type is a type by convention (Rust, Go, TypeScript
   capitalize them) rather than by resolution, and a keyword used as a name
   is still painted as a keyword. */

#include "ui/sizing.h"

namespace gpui {

namespace component {

// The capture names a scanner can answer for. Everything else is Text and
// paints in the block's own foreground, which is what the theme leaves
// `punctuation` and `operator` as.
enum class SyntaxTok : uint8_t {
    Text,
    Keyword,
    Type,
    Function,
    Property,
    String,
    Number,
    Boolean,
    Comment,
    Tag,
    Attribute,
    // Markdown: `@title` on a heading, `@text.literal` on a fenced block
    // and `@text.code.span` on an inline span. The scanner has no parse, so
    // the heading is the whole ATX line and a fence's body is one run.
    Title,
    Literal,
};

// An index into the language table, or SyntaxLangNone.
using SyntaxLang = int8_t;
constexpr SyntaxLang SyntaxLangNone = -1;

// The language a fence's info string names: "cpp", "rust", "```js", or the
// `language-cpp` class an HTML <code> carries. Returns SyntaxLangNone when
// it is not one we scan, and the block then renders unhighlighted.
SyntaxLang SyntaxLangFor(Str info);

// The name the table knows the language by ("cpp"), for tests and debugging.
Str SyntaxLangName(SyntaxLang lang);

// A scan in progress. The tokens partition the source: every byte belongs to
// exactly one, whitespace and punctuation included, so a caller can paint
// them one after another and get the text back.
struct SyntaxLexer {
    const void* def = nullptr;
    Str src = {};
    int at = 0;
    SyntaxTok tok = SyntaxTok::Text;
    Str text = {};
    // Markup only: whether the scan is between a `<` and its `>`, where
    // names are attributes rather than text.
    bool inTag = false;
    // Markup only: the next name is the tag's own, not an attribute.
    bool tagName = false;
    // Markdown only: inside a fenced block, where nothing is a mark until the
    // fence that closes it; and "the run just closed a link's [text]", so the
    // (destination) after it reads as one.
    bool inFence = false;
    bool linkDest = false;
    // Rust only: a declaration's name is the next token after fn and trivia.
    bool rustFunctionName = false;
};

void SyntaxLexStart(SyntaxLexer* lx, SyntaxLang lang, Str src);
// The next token, or false at the end of the source.
bool SyntaxLexNext(SyntaxLexer* lx);

// The theme color for a token — theme/default-theme.json's `syntax` block,
// which is the table HighlightTheme::default_light / default_dark read.
// `fallback` is what Text paints with, the block's own foreground.
Rgba SyntaxTokColor(SyntaxTok tok, ThemeMode mode, Rgba fallback);

} // namespace component
} // namespace gpui
#endif // GPUI_SRC_UI_SYNTAX_H_
