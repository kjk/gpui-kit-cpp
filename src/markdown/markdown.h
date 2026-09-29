/* src/lib.rs + src/configuration.rs — the public API of the `markdown` crate.

   `src/markdown/` is a C++ port of markdown-rs 1.0.0, the CommonMark + GFM
   parser gpui-kit parses every TextView with. See
   src/markdown/readme.md for what is ported and what is not, and
   cmd/versions.ts (`markdown`) for the pinned version.

       Arena* a = ArenaNew();
       markdown::Node* tree =
           markdown::ToMdast(a, source, markdown::ParseOptions::Gfm());

   Rust's `to_mdast` returns a `Result`, because MDX has syntax errors and
   markdown does not. The MDX constructs are not ported, so this cannot fail
   and returns the tree. */

#ifndef GPUI_MARKDOWN_MARKDOWN_H_
#define GPUI_MARKDOWN_MARKDOWN_H_

#include "markdown/mdast.h"

namespace markdown {

// configuration.rs Constructs. The mdx_* fields are gone with the MDX
// constructs.
struct Constructs {
    bool attention = true;
    bool autolink = true;
    bool blockQuote = true;
    bool characterEscape = true;
    bool characterReference = true;
    bool codeIndented = true;
    bool codeFenced = true;
    bool codeText = true;
    bool definition = true;
    bool frontmatter = false;
    bool gfmAutolinkLiteral = false;
    bool gfmFootnoteDefinition = false;
    bool gfmLabelStartFootnote = false;
    bool gfmStrikethrough = false;
    bool gfmTable = false;
    bool gfmTaskListItem = false;
    bool hardBreakEscape = true;
    bool hardBreakTrailing = true;
    bool headingAtx = true;
    bool headingSetext = true;
    bool htmlFlow = true;
    bool htmlText = true;
    bool labelStartImage = true;
    bool labelStartLink = true;
    bool labelEnd = true;
    bool listItem = true;
    bool mathFlow = false;
    bool mathText = false;
    bool thematicBreak = true;

    // Constructs::default() is the member initialisers above.
    static Constructs Gfm();
};

// configuration.rs ParseOptions.
struct ParseOptions {
    Constructs constructs = {};
    bool gfmStrikethroughSingleTilde = true;
    bool mathTextSingleDollar = true;

    static ParseOptions Gfm();
};

// lib.rs to_mdast. Every node of the tree, and every string in it, is
// allocated from `a`; `source` is only read.
Node* ToMdast(Arena* a, Str source, const ParseOptions& options);

// unist Position, as its two byte offsets, for a caller that wants them. A
// Node does not carry its position (mdast.h says why); this is the side
// table `to_mdast` fills when it is handed one, set where the crate sets
// `position`: the start at `tail_push`, the end at `tail_pop`, a hard break
// stretched over the line ending after it, and a task item's text moved past
// the whitespace after its checkbox. gpui-kit's markdown.rs reads these to
// map a rendered selection back to its source.
struct NodeSpan {
    const Node* node = nullptr;
    int32_t start = 0;
    int32_t end = 0;
};

struct NodePositions {
    base::Vec<NodeSpan> spans;
};

Node* ToMdast(Arena* a, Str source, const ParseOptions& options,
              NodePositions* positions);

// The span `to_mdast` recorded for `n`. False for a node it made no record
// of: one the compile did not push, or every node when no table was kept.
bool NodePosition(const NodePositions* positions, const Node* n, int32_t* start,
                  int32_t* end);

// lib.rs decode_named / decode_numeric, exposed the way the crate exposes
// them. `&amp;` -> `&`. Returns a null `Str` when the name is not one of the
// 2125 the HTML5 table holds.
Str DecodeNamed(Arena* a, Str name);
// `radix` is 10 or 16. Never fails: an invalid code point decodes to U+FFFD.
Str DecodeNumeric(Arena* a, Str value, int radix);

} // namespace markdown

#endif // GPUI_MARKDOWN_MARKDOWN_H_
