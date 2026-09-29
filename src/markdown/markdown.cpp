/* src/lib.rs + src/configuration.rs — the public entry point.

   Part of the C++ port of markdown-rs 1.0.0 (see src/markdown/readme.md). */

#include "markdown/construct.h"

namespace markdown {

Constructs Constructs::Gfm() {
    Constructs constructs;
    constructs.gfmAutolinkLiteral = true;
    constructs.gfmFootnoteDefinition = true;
    constructs.gfmLabelStartFootnote = true;
    constructs.gfmStrikethrough = true;
    constructs.gfmTable = true;
    constructs.gfmTaskListItem = true;
    return constructs;
}

ParseOptions ParseOptions::Gfm() {
    ParseOptions options;
    options.constructs = Constructs::Gfm();
    return options;
}

Node* ToMdast(Arena* a, Str source, const ParseOptions& options) {
    return ToMdast(a, source, options, nullptr);
}

Node* ToMdast(Arena* a, Str source, const ParseOptions& options,
              NodePositions* positions) {
    ParseState parseState;
    parseState.a = a;
    // The parse's own working memory, thrown away whole below, so none of it
    // is left in the caller's arena.
    parseState.scratch = base::ArenaNew();
    parseState.options = &options;
    parseState.bytes = source;

    Vec<Event> events = Parse(&parseState);
    Node* tree = ToMdastCompile(events, &parseState, positions);

    base::ArenaDelete(parseState.scratch);
    if (positions && positions->spans.len > 1) {
        // Recorded in the order the nodes were pushed; sorted by node so
        // NodePosition is a binary search rather than a walk per lookup.
        qsort(positions->spans.els, (size_t)positions->spans.len,
              sizeof(NodeSpan), [](const void* l, const void* r) -> int {
                  uintptr_t a = (uintptr_t)((const NodeSpan*)l)->node;
                  uintptr_t b = (uintptr_t)((const NodeSpan*)r)->node;
                  return a < b ? -1 : (a > b ? 1 : 0);
              });
    }
    return tree;
}

bool NodePosition(const NodePositions* positions, const Node* n, int32_t* start,
                  int32_t* end) {
    if (!positions || !n) {
        return false;
    }
    int32_t lo = 0;
    int32_t hi = positions->spans.len;
    while (lo < hi) {
        int32_t mid = lo + (hi - lo) / 2;
        const NodeSpan& span = positions->spans[mid];
        if ((uintptr_t)span.node < (uintptr_t)n) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    if (lo >= positions->spans.len || positions->spans[lo].node != n) {
        return false;
    }
    *start = positions->spans[lo].start;
    *end = positions->spans[lo].end;
    return true;
}

} // namespace markdown
