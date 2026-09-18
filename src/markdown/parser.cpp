/* Driving the tokenizers: the whole parse, the passes over the chunks one
   tokenizer leaves for the next, and the resolver dispatch.

   | Rust                  | here                    |
   | --------------------- | ----------------------- |
   | `src/parser.rs`       | Parse                   |
   | `src/subtokenize.rs`  | Subtokenize, DivideEvents |
   | `src/resolve.rs`      | ResolveCall             |

   Part of the C++ port of markdown-rs 1.0.0 (see src/markdown/readme.md). */

#include "markdown/construct.h"

namespace markdown {

// ─── resolve.rs ──────────────────────────────────────────────────────────

bool ResolveCall(Tokenizer* t, ResolveName name, Subresult* out) {
    switch (name) {
        case ResolveName::Label:
            return LabelEndResolve(t, out);
        case ResolveName::Attention:
            return AttentionResolve(t, out);
        case ResolveName::GfmTable:
            return GfmTableResolve(t, out);
        case ResolveName::HeadingAtx:
            return HeadingAtxResolve(t, out);
        case ResolveName::HeadingSetext:
            return HeadingSetextResolve(t, out);
        case ResolveName::ListItem:
            return ListItemResolve(t, out);
        case ResolveName::Content:
            return ContentResolve(t, out);
        case ResolveName::Data:
            return DataResolve(t, out);
        case ResolveName::String:
            return StringResolve(t, out);
        case ResolveName::Text:
            return TextResolve(t, out);
    }
    return false;
}

// ─── subtokenize.rs ──────────────────────────────────────────────────────

void SubtokenizeLinkTo(Vec<Event>& events, int32_t previous, int32_t next) {
    events[previous].link.next = next;
    events[next].link.previous = previous;
}

void SubtokenizeLink(Vec<Event>& events, int32_t index) {
    SubtokenizeLinkTo(events, index - 2, index);
}

// One of `divide_events`'s slices: where in the parent the chunk goes, and
// where in the child's events it starts.
struct DivideSlice {
    int32_t linkIndex;
    int32_t sliceStart;
};

void DivideEvents(EditMap& map, const Vec<Event>& events, int32_t linkIndex,
                  Vec<Event>& childEvents, int32_t* accA, int32_t* accB) {
    int32_t childIndex = 0;
    Vec<DivideSlice> slices;
    int32_t sliceStart = 0;
    int32_t oldPrev = -1;
    int32_t childLen = len(childEvents);

    while (childIndex < childLen) {
        const Point& current = childEvents[childIndex].point;
        const Point& end = events[linkIndex + 1].point;

        // Find the first event that starts after the end of this chunk.
        if (current.index > end.index ||
            (current.index == end.index && current.vs > end.vs)) {
            DivideSlice slice = {linkIndex, sliceStart};
            VecAppend(slices, slice);
            sliceStart = childIndex;
            linkIndex = events[linkIndex].link.next;
        }

        // Fix sublinks.
        if (childEvents[childIndex].hasLink && childEvents[childIndex]
                                                       .link.previous != -1) {
            Event& prevEvent = childEvents[oldPrev];
            int32_t newLink = len(slices) == 0
                                  ? oldPrev + linkIndex + 2
                                  : oldPrev + linkIndex - (len(slices) - 1) * 2;
            prevEvent.link.next = newLink + *accB - *accA;
        }

        // Correct the next links.
        if (childEvents[childIndex].hasLink && childEvents[childIndex]
                                                       .link.next != -1) {
            int32_t next = childEvents[childIndex].link.next;
            oldPrev = childEvents[next].link.previous;
            if (childEvents[next].link.previous != -1) {
                childEvents[next].link.previous =
                    childEvents[next].link.previous + linkIndex -
                    (len(slices) * 2) + *accB - *accA;
            }
        }

        childIndex += 1;
    }

    if (len(childEvents) > 0) {
        DivideSlice slice = {linkIndex, sliceStart};
        VecAppend(slices, slice);
    }

    // Splice the child events into the parent, back to front so the indices
    // stay right.
    int32_t index = len(slices);
    while (index > 0) {
        index -= 1;
        int32_t from = slices[index].sliceStart;
        EditMapAdd(map, slices[index].linkIndex, 2, childEvents.els + from,
                   len(childEvents) - from);
        childEvents.len = from;
    }

    *accA = *accA + len(slices) * 2;
    *accB = *accB + childLen;
}

Subresult Subtokenize(Vec<Event>& events, ParseState* parseState,
                      bool hasFilter, ContentKind filter) {
    EditMap map;
    map.a = parseState->scratch;
    int32_t index = 0;
    Subresult value;
    value.done = true;
    int32_t accA = 0;
    int32_t accB = 0;

    while (index < len(events)) {
        if (events[index].hasLink && events[index].link.previous == -1 &&
            (!hasFilter || events[index].link.content == filter)) {
            const Link& link = events[index].link;
            int32_t linkIndex = index;
            Tokenizer* tokenizer =
                TokenizerNew(events[index].point, parseState);

            StateName startName = StateName::TextStart;
            if (link.content == ContentKind::Content) {
                startName = StateName::ContentDefinitionBefore;
            } else if (link.content == ContentKind::String) {
                startName = StateName::StringStart;
            }
            State state = StateNext(startName);

            // A GFM task list item check can only occur at the start of the
            // first paragraph of a list item.
            if (parseState->options->constructs.gfmTaskListItem && index > 2 &&
                events[index - 1].kind == Kind::Enter &&
                events[index - 1].name == Name::Paragraph) {
                Name names[4] = {Name::BlankLineEnding, Name::Definition,
                                 Name::LineEnding, Name::SpaceOrTab};
                int32_t before = SkipOptBack(events, index - 2, names, 4);
                if (events[before].kind == Kind::Exit &&
                    events[before].name == Name::ListItemPrefix) {
                    tokenizer->tokenizeState
                        .documentAtFirstParagraphOfListItem = true;
                }
            }

            // Feed the tokenizer each chunk of this link.
            while (linkIndex != -1) {
                const Event& enter = events[linkIndex];
                const Link& linkCurr = enter.link;
                if (linkCurr.previous != -1) {
                    DefineSkip(tokenizer, enter.point);
                }
                const Point& end = events[linkIndex + 1].point;
                state = Push(tokenizer, enter.point.index, enter.point.vs,
                             end.index, end.vs, state);
                linkIndex = linkCurr.next;
            }

            Subresult result = Flush(tokenizer, state, true);
            SubresultAppend(value, result);
            value.done = false;
            DivideEvents(map, events, index, tokenizer->events, &accA, &accB);
            TokenizerFree(tokenizer);
        }
        index += 1;
    }

    EditMapConsume(map, events);
    return value;
}

// ─── parser.rs ───────────────────────────────────────────────────────────

Vec<Event> Parse(ParseState* parseState) {
    Point start;
    start.line = 1;
    start.column = 1;
    start.index = 0;
    start.vs = 0;

    Tokenizer* tokenizer = TokenizerNew(start, parseState);
    // A document's event list ends up at about one event per two bytes of
    // source across every shape `bun cmd/bench.ts markdown` parses, and this
    // is the one tokenizer whose span is the whole document — a subtokenizer
    // covers a fraction of it and must not reserve for the lot. Without this
    // a 64 KB document walks 4, 8, 16 … 32768 and memcpys about 2 MB getting
    // there. A floor and not a cap: a document with more events than that
    // still grows the ordinary way.
    VecReserve(tokenizer->events, parseState->bytes.len / 2);
    State state = Push(tokenizer, 0, 0, parseState->bytes.len, 0,
                       StateNext(StateName::DocumentStart));
    Subresult result = Flush(tokenizer, state, true);

    Vec<Event> events;
    events.els = tokenizer->events.els;
    events.len = len(tokenizer->events);
    events.cap = tokenizer->events.cap;
    tokenizer->events.els = nullptr;
    tokenizer->events.len = 0;
    tokenizer->events.cap = 0;
    TokenizerFree(tokenizer);

    for (;;) {
        for (int32_t i = 0; i < result.gfmFootnoteDefinitions.len; i++) {
            VecAppend(parseState->gfmFootnoteDefinitions,
                      result.gfmFootnoteDefinitions[i]);
        }
        for (int32_t i = 0; i < result.definitions.len; i++) {
            VecAppend(parseState->definitions, result.definitions[i]);
        }
        result.gfmFootnoteDefinitions.len = 0;
        result.definitions.len = 0;
        if (result.done) {
            return events;
        }
        result = Subtokenize(events, parseState, false, ContentKind::Flow);
    }
}

} // namespace markdown
