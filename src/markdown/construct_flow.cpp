/* The block constructs: what a line can be when it is not a paragraph.

   | Rust                          | here             |
   | ----------------------------- | ---------------- |
   | `construct/block_quote.rs`    | BlockQuote*      |
   | `construct/code_indented.rs`  | CodeIndented*    |
   | `construct/thematic_break.rs` | ThematicBreak*   |
   | `construct/heading_atx.rs`    | HeadingAtx*      |
   | `construct/heading_setext.rs` | HeadingSetext*   |
   | `construct/list_item.rs`      | ListItem*        |
   | `construct/definition.rs`     | Definition*      |
   | `construct/frontmatter.rs`    | Frontmatter*     |

   Part of the C++ port of markdown-rs 1.0.0 (see src/markdown/readme.md). */

#include "markdown/construct.h"

namespace markdown {

// The indent a construct may have before it stops being that construct and
// starts being indented code: `TAB_SIZE - 1`, or no limit at all when
// indented code is off. Written out at each use in Rust.
static int32_t IndentMax(Tokenizer* t) {
    return t->parseState->options->constructs.codeIndented ? kTabSize - 1
                                                           : kSizeMax;
}

// ─── block_quote.rs ──────────────────────────────────────────────────────

State BlockQuoteStart(Tokenizer* t) {
    if (t->parseState->options->constructs.blockQuote) {
        Enter(t, Name::BlockQuote);
        return StateRetry(StateName::BlockQuoteContStart);
    }
    return StateNok();
}

State BlockQuoteContStart(Tokenizer* t) {
    if (t->current == '\t' || t->current == ' ') {
        TokenizerAttempt(t, StateNext(StateName::BlockQuoteContBefore),
                         StateNok());
        return StateRetry(SpaceOrTabMinMax(t, 1, IndentMax(t)));
    }
    return StateRetry(StateName::BlockQuoteContBefore);
}

State BlockQuoteContBefore(Tokenizer* t) {
    if (t->current == '>') {
        Enter(t, Name::BlockQuotePrefix);
        Enter(t, Name::BlockQuoteMarker);
        Consume(t);
        Exit(t, Name::BlockQuoteMarker);
        return StateNext(StateName::BlockQuoteContAfter);
    }
    return StateNok();
}

State BlockQuoteContAfter(Tokenizer* t) {
    if (t->current == '\t' || t->current == ' ') {
        Enter(t, Name::SpaceOrTab);
        Consume(t);
        Exit(t, Name::SpaceOrTab);
    }
    Exit(t, Name::BlockQuotePrefix);
    return StateOk();
}

// ─── code_indented.rs ────────────────────────────────────────────────────

State CodeIndentedStart(Tokenizer* t) {
    // Do not interrupt paragraphs.
    if (!t->interrupt && t->parseState->options->constructs.codeIndented &&
        (t->current == '\t' || t->current == ' ')) {
        Enter(t, Name::CodeIndented);
        TokenizerAttempt(t, StateNext(StateName::CodeIndentedAtBreak),
                         StateNok());
        return StateRetry(SpaceOrTabMinMax(t, kTabSize, kTabSize));
    }
    return StateNok();
}

State CodeIndentedAtBreak(Tokenizer* t) {
    if (t->current < 0) {
        return StateRetry(StateName::CodeIndentedAfter);
    }
    if (t->current == '\n') {
        TokenizerAttempt(t, StateNext(StateName::CodeIndentedAtBreak),
                         StateNext(StateName::CodeIndentedAfter));
        return StateRetry(StateName::CodeIndentedFurtherStart);
    }
    Enter(t, Name::CodeFlowChunk);
    return StateRetry(StateName::CodeIndentedInside);
}

State CodeIndentedInside(Tokenizer* t) {
    if (t->current < 0 || t->current == '\n') {
        Exit(t, Name::CodeFlowChunk);
        return StateRetry(StateName::CodeIndentedAtBreak);
    }
    Consume(t);
    return StateNext(StateName::CodeIndentedInside);
}

State CodeIndentedAfter(Tokenizer* t) {
    Exit(t, Name::CodeIndented);
    // Feel free to interrupt.
    t->interrupt = false;
    return StateOk();
}

State CodeIndentedFurtherStart(Tokenizer* t) {
    if (t->lazy || t->pierce) {
        return StateNok();
    }
    if (t->current == '\n') {
        Enter(t, Name::LineEnding);
        Consume(t);
        Exit(t, Name::LineEnding);
        return StateNext(StateName::CodeIndentedFurtherStart);
    }
    TokenizerAttempt(t, StateOk(),
                     StateNext(StateName::CodeIndentedFurtherBegin));
    return StateRetry(SpaceOrTabMinMax(t, kTabSize, kTabSize));
}

State CodeIndentedFurtherBegin(Tokenizer* t) {
    if (t->current == '\t' || t->current == ' ') {
        TokenizerAttempt(t, StateNext(StateName::CodeIndentedFurtherAfter),
                         StateNok());
        return StateRetry(SpaceOrTab(t));
    }
    return StateNok();
}

State CodeIndentedFurtherAfter(Tokenizer* t) {
    if (t->current == '\n') {
        return StateRetry(StateName::CodeIndentedFurtherStart);
    }
    return StateNok();
}

// ─── thematic_break.rs ───────────────────────────────────────────────────

State ThematicBreakStart(Tokenizer* t) {
    if (!t->parseState->options->constructs.thematicBreak) {
        return StateNok();
    }
    Enter(t, Name::ThematicBreak);
    if (t->current == '\t' || t->current == ' ') {
        TokenizerAttempt(t, StateNext(StateName::ThematicBreakBefore),
                         StateNok());
        return StateRetry(SpaceOrTabMinMax(t, 0, IndentMax(t)));
    }
    return StateRetry(StateName::ThematicBreakBefore);
}

State ThematicBreakBefore(Tokenizer* t) {
    if (t->current == '*' || t->current == '-' || t->current == '_') {
        t->tokenizeState.marker = (uint8_t)t->current;
        return StateRetry(StateName::ThematicBreakAtBreak);
    }
    return StateNok();
}

State ThematicBreakAtBreak(Tokenizer* t) {
    if (t->current == (int32_t)t->tokenizeState.marker) {
        Enter(t, Name::ThematicBreakSequence);
        return StateRetry(StateName::ThematicBreakSequence);
    }
    if (t->tokenizeState.size >= kThematicBreakMarkerCountMin &&
        (t->current < 0 || t->current == '\n')) {
        t->tokenizeState.marker = 0;
        t->tokenizeState.size = 0;
        Exit(t, Name::ThematicBreak);
        // Feel free to interrupt.
        t->interrupt = false;
        return StateOk();
    }
    t->tokenizeState.marker = 0;
    t->tokenizeState.size = 0;
    return StateNok();
}

State ThematicBreakSequence(Tokenizer* t) {
    if (t->current == (int32_t)t->tokenizeState.marker) {
        Consume(t);
        t->tokenizeState.size += 1;
        return StateNext(StateName::ThematicBreakSequence);
    }
    if (t->current == '\t' || t->current == ' ') {
        Exit(t, Name::ThematicBreakSequence);
        TokenizerAttempt(t, StateNext(StateName::ThematicBreakAtBreak),
                         StateNok());
        return StateRetry(SpaceOrTab(t));
    }
    Exit(t, Name::ThematicBreakSequence);
    return StateRetry(StateName::ThematicBreakAtBreak);
}

// ─── heading_atx.rs ──────────────────────────────────────────────────────

State HeadingAtxStart(Tokenizer* t) {
    if (!t->parseState->options->constructs.headingAtx) {
        return StateNok();
    }
    Enter(t, Name::HeadingAtx);
    if (t->current == '\t' || t->current == ' ') {
        TokenizerAttempt(t, StateNext(StateName::HeadingAtxBefore), StateNok());
        return StateRetry(SpaceOrTabMinMax(t, 0, IndentMax(t)));
    }
    return StateRetry(StateName::HeadingAtxBefore);
}

State HeadingAtxBefore(Tokenizer* t) {
    if (t->current == '#') {
        Enter(t, Name::HeadingAtxSequence);
        return StateRetry(StateName::HeadingAtxSequenceOpen);
    }
    return StateNok();
}

State HeadingAtxSequenceOpen(Tokenizer* t) {
    if (t->current == '#' && t->tokenizeState
                                     .size < kHeadingAtxOpeningFenceSizeMax) {
        t->tokenizeState.size += 1;
        Consume(t);
        return StateNext(StateName::HeadingAtxSequenceOpen);
    }
    if (t->current < 0 || t->current == '\t' || t->current == '\n' ||
        t->current == ' ') {
        t->tokenizeState.size = 0;
        Exit(t, Name::HeadingAtxSequence);
        return StateRetry(StateName::HeadingAtxAtBreak);
    }
    t->tokenizeState.size = 0;
    return StateNok();
}

State HeadingAtxAtBreak(Tokenizer* t) {
    if (t->current < 0 || t->current == '\n') {
        Exit(t, Name::HeadingAtx);
        RegisterResolver(t, ResolveName::HeadingAtx);
        // Feel free to interrupt.
        t->interrupt = false;
        return StateOk();
    }
    if (t->current == '\t' || t->current == ' ') {
        TokenizerAttempt(t, StateNext(StateName::HeadingAtxAtBreak),
                         StateNok());
        return StateRetry(SpaceOrTab(t));
    }
    if (t->current == '#') {
        Enter(t, Name::HeadingAtxSequence);
        return StateRetry(StateName::HeadingAtxSequenceFurther);
    }
    Link link;
    link.content = ContentKind::Text;
    EnterLink(t, Name::Data, link);
    return StateRetry(StateName::HeadingAtxData);
}

State HeadingAtxSequenceFurther(Tokenizer* t) {
    if (t->current == '#') {
        Consume(t);
        return StateNext(StateName::HeadingAtxSequenceFurther);
    }
    Exit(t, Name::HeadingAtxSequence);
    return StateRetry(StateName::HeadingAtxAtBreak);
}

State HeadingAtxData(Tokenizer* t) {
    if (t->current < 0 || t->current == '\t' || t->current == '\n' ||
        t->current == ' ') {
        Exit(t, Name::Data);
        return StateRetry(StateName::HeadingAtxAtBreak);
    }
    Consume(t);
    return StateNext(StateName::HeadingAtxData);
}

bool HeadingAtxResolve(Tokenizer* t, Subresult*) {
    int32_t index = 0;
    bool headingInside = false;
    int32_t dataStart = -1;
    int32_t dataEnd = -1;
    while (index < t->events.len) {
        const Event& event = t->events[index];
        if (event.name == Name::HeadingAtx) {
            if (event.kind == Kind::Enter) {
                headingInside = true;
            } else {
                if (dataStart != -1) {
                    int32_t end = dataEnd;
                    Event add;
                    add.kind = Kind::Enter;
                    add.name = Name::HeadingAtxText;
                    add.point = t->events[dataStart].point;
                    EditMapAdd(t->map, dataStart, 0, &add, 1);
                    EditMapAdd(t->map, dataStart + 1, end - dataStart - 1,
                               nullptr, 0);
                    Event addExit;
                    addExit.kind = Kind::Exit;
                    addExit.name = Name::HeadingAtxText;
                    addExit.point = t->events[end].point;
                    EditMapAdd(t->map, end + 1, 0, &addExit, 1);
                }
                headingInside = false;
                dataStart = -1;
                dataEnd = -1;
            }
        } else if (headingInside && event.name == Name::Data) {
            if (event.kind == Kind::Enter) {
                if (dataStart == -1) {
                    dataStart = index;
                }
            } else {
                dataEnd = index;
            }
        }
        index += 1;
    }
    EditMapConsume(t->map, t->events);
    return false;
}

// ─── heading_setext.rs ───────────────────────────────────────────────────

State HeadingSetextStart(Tokenizer* t) {
    if (!t->parseState->options->constructs.headingSetext || t->lazy ||
        t->pierce || t->events.len == 0) {
        return StateNok();
    }
    Name names[2] = {Name::LineEnding, Name::SpaceOrTab};
    int32_t before = SkipOptBack(t->events, t->events.len - 1, names, 2);
    Name name = t->events[before].name;
    if (name != Name::Content && name != Name::HeadingSetextUnderline) {
        return StateNok();
    }
    Enter(t, Name::HeadingSetextUnderline);
    if (t->current == '\t' || t->current == ' ') {
        TokenizerAttempt(t, StateNext(StateName::HeadingSetextBefore),
                         StateNok());
        return StateRetry(SpaceOrTabMinMax(t, 0, IndentMax(t)));
    }
    return StateRetry(StateName::HeadingSetextBefore);
}

State HeadingSetextBefore(Tokenizer* t) {
    if (t->current == '-' || t->current == '=') {
        t->tokenizeState.marker = (uint8_t)t->current;
        Enter(t, Name::HeadingSetextUnderlineSequence);
        return StateRetry(StateName::HeadingSetextInside);
    }
    return StateNok();
}

State HeadingSetextInside(Tokenizer* t) {
    if (t->current == (int32_t)t->tokenizeState.marker) {
        Consume(t);
        return StateNext(StateName::HeadingSetextInside);
    }
    t->tokenizeState.marker = 0;
    Exit(t, Name::HeadingSetextUnderlineSequence);
    if (t->current == '\t' || t->current == ' ') {
        TokenizerAttempt(t, StateNext(StateName::HeadingSetextAfter),
                         StateNok());
        return StateRetry(SpaceOrTab(t));
    }
    return StateRetry(StateName::HeadingSetextAfter);
}

State HeadingSetextAfter(Tokenizer* t) {
    if (t->current < 0 || t->current == '\n') {
        // Feel free to interrupt.
        t->interrupt = false;
        RegisterResolver(t, ResolveName::HeadingSetext);
        Exit(t, Name::HeadingSetextUnderline);
        return StateOk();
    }
    return StateNok();
}

bool HeadingSetextResolve(Tokenizer* t, Subresult*) {
    Name underline = Name::HeadingSetextUnderline;
    int32_t enter = SkipTo(t->events, 0, &underline, 1);
    while (enter < t->events.len) {
        int32_t exit = SkipTo(t->events, enter + 1, &underline, 1);

        Name names[3] = {Name::SpaceOrTab, Name::LineEnding,
                         Name::BlockQuotePrefix};
        int32_t paragraphExitBefore =
            SkipOptBack(t->events, enter - 1, names, 3);

        if (t->events[paragraphExitBefore].name == Name::Paragraph) {
            Name paragraph = Name::Paragraph;
            int32_t paragraphEnter =
                SkipToBack(t->events, paragraphExitBefore - 1, &paragraph, 1);
            // Change the paragraph to setext heading text.
            t->events[paragraphEnter].name = Name::HeadingSetextText;
            t->events[paragraphExitBefore].name = Name::HeadingSetextText;
            // Add the heading around it.
            Event headingEnter = t->events[paragraphEnter];
            headingEnter.name = Name::HeadingSetext;
            EditMapAdd(t->map, paragraphEnter, 0, &headingEnter, 1);
            Event headingExit = t->events[exit];
            headingExit.name = Name::HeadingSetext;
            EditMapAdd(t->map, exit + 1, 0, &headingExit, 1);
        } else if (exit + 3 < t->events.len &&
                   t->events[exit + 1].name == Name::LineEnding &&
                   t->events[exit + 3].name == Name::Paragraph) {
            // There is a following paragraph: turn the underline into its
            // first line.
            t->events[enter].name = Name::Paragraph;
            t->events[exit + 1].name = Name::Data;
            t->events[exit + 2].name = Name::Data;
            t->events[exit + 1].point = t->events[enter].point;
            t->events[exit + 1].hasLink = true;
            t->events[exit + 1].link.previous = -1;
            t->events[exit + 1].link.next = exit + 4;
            t->events[exit + 1].link.content = ContentKind::Text;
            t->events[exit + 4].link.previous = exit + 1;
            EditMapAdd(t->map, enter + 1, exit - enter, nullptr, 0);
            EditMapAdd(t->map, exit + 3, 1, nullptr, 0);
        } else {
            // Nothing follows: the underline is a paragraph of its own.
            t->events[enter].name = Name::Paragraph;
            t->events[exit].name = Name::Paragraph;
            Event add[2];
            add[0].name = Name::Data;
            add[0].kind = Kind::Enter;
            add[0].point = t->events[enter].point;
            add[0].hasLink = true;
            add[0].link.content = ContentKind::Text;
            add[1].name = Name::Data;
            add[1].kind = Kind::Exit;
            add[1].point = t->events[exit].point;
            EditMapAdd(t->map, enter + 1, exit - enter - 1, add, 2);
        }

        enter = SkipTo(t->events, exit + 1, &underline, 1);
    }
    EditMapConsume(t->map, t->events);
    return false;
}

// ─── list_item.rs ────────────────────────────────────────────────────────

State ListItemStart(Tokenizer* t) {
    if (!t->parseState->options->constructs.listItem) {
        return StateNok();
    }
    Enter(t, Name::ListItem);
    if (t->current == '\t' || t->current == ' ') {
        TokenizerAttempt(t, StateNext(StateName::ListItemBefore), StateNok());
        return StateRetry(SpaceOrTabMinMax(t, 0, IndentMax(t)));
    }
    return StateRetry(StateName::ListItemBefore);
}

State ListItemBefore(Tokenizer* t) {
    if (t->current == '*' || t->current == '-') {
        // Unordered: a thematic break wins.
        TokenizerCheck(t, StateNok(),
                       StateNext(StateName::ListItemBeforeUnordered));
        return StateRetry(StateName::ThematicBreakStart);
    }
    if (t->current == '+') {
        return StateRetry(StateName::ListItemBeforeUnordered);
    }
    // Ordered: only `1` may interrupt.
    if (t->current == '1' ||
        (t->current >= '0' && t->current <= '9' && !t->interrupt)) {
        return StateRetry(StateName::ListItemBeforeOrdered);
    }
    return StateNok();
}

State ListItemBeforeUnordered(Tokenizer* t) {
    Enter(t, Name::ListItemPrefix);
    return StateRetry(StateName::ListItemMarker);
}

State ListItemBeforeOrdered(Tokenizer* t) {
    Enter(t, Name::ListItemPrefix);
    Enter(t, Name::ListItemValue);
    return StateRetry(StateName::ListItemValue);
}

State ListItemValue(Tokenizer* t) {
    if ((t->current == '.' || t->current == ')') &&
        (!t->interrupt || t->tokenizeState.size < 2)) {
        Exit(t, Name::ListItemValue);
        return StateRetry(StateName::ListItemMarker);
    }
    if (t->current >= '0' && t->current <= '9' &&
        t->tokenizeState.size + 1 < kListItemValueSizeMax) {
        t->tokenizeState.size += 1;
        Consume(t);
        return StateNext(StateName::ListItemValue);
    }
    t->tokenizeState.size = 0;
    return StateNok();
}

State ListItemMarker(Tokenizer* t) {
    Enter(t, Name::ListItemMarker);
    Consume(t);
    Exit(t, Name::ListItemMarker);
    return StateNext(StateName::ListItemMarkerAfter);
}

State ListItemMarkerAfter(Tokenizer* t) {
    t->tokenizeState.size = 1;
    TokenizerCheck(t, StateNext(StateName::ListItemAfter),
                   StateNext(StateName::ListItemMarkerAfterFilled));
    return StateRetry(StateName::BlankLineStart);
}

State ListItemMarkerAfterFilled(Tokenizer* t) {
    t->tokenizeState.size = 0;
    // Attempt to parse up to the largest allowed indent, `step 2`.
    TokenizerAttempt(t, StateNext(StateName::ListItemAfter),
                     StateNext(StateName::ListItemPrefixOther));
    return StateRetry(StateName::ListItemWhitespace);
}

State ListItemWhitespace(Tokenizer* t) {
    TokenizerAttempt(t, StateNext(StateName::ListItemWhitespaceAfter),
                     StateNok());
    return StateRetry(SpaceOrTabMinMax(t, 1, kTabSize));
}

State ListItemWhitespaceAfter(Tokenizer* t) {
    if (t->current == '\t' || t->current == ' ') {
        return StateNok();
    }
    return StateOk();
}

State ListItemPrefixOther(Tokenizer* t) {
    if (t->current == '\t' || t->current == ' ') {
        Enter(t, Name::SpaceOrTab);
        Consume(t);
        Exit(t, Name::SpaceOrTab);
        return StateNext(StateName::ListItemAfter);
    }
    return StateNok();
}

State ListItemAfter(Tokenizer* t) {
    bool blank = t->tokenizeState.size == 1;
    t->tokenizeState.size = 0;

    if (blank && t->interrupt) {
        return StateNok();
    }

    Name listItem = Name::ListItem;
    int32_t start = SkipToBack(t->events, t->events.len - 1, &listItem, 1);
    Position position;
    position.start = t->events[start].point;
    position.end = t->point;
    int32_t prefix = SliceFromPosition(t->parseState->bytes, position).Len();
    if (blank) {
        prefix += 1;
    }

    ContainerState& container =
        t->tokenizeState
            .documentContainerStack[t->tokenizeState.documentContinued];
    container.blankInitial = blank;
    container.size = prefix;

    Exit(t, Name::ListItemPrefix);
    RegisterResolverBefore(t, ResolveName::ListItem);
    return StateOk();
}

State ListItemContStart(Tokenizer* t) {
    TokenizerCheck(t, StateNext(StateName::ListItemContBlank),
                   StateNext(StateName::ListItemContFilled));
    return StateRetry(StateName::BlankLineStart);
}

State ListItemContBlank(Tokenizer* t) {
    ContainerState& container =
        t->tokenizeState
            .documentContainerStack[t->tokenizeState.documentContinued];
    int32_t size = container.size;
    if (container.blankInitial) {
        return StateNok();
    }
    // Consume, optionally, at most `size`.
    if (t->current == '\t' || t->current == ' ') {
        return StateRetry(SpaceOrTabMinMax(t, 0, size));
    }
    return StateOk();
}

State ListItemContFilled(Tokenizer* t) {
    ContainerState& container =
        t->tokenizeState
            .documentContainerStack[t->tokenizeState.documentContinued];
    int32_t size = container.size;
    container.blankInitial = false;
    // Consume exactly `size`.
    if (t->current == '\t' || t->current == ' ') {
        return StateRetry(SpaceOrTabMinMax(t, size, size));
    }
    return StateNok();
}

// One of Rust's `(u8, usize, usize, usize)` list tuples: marker, balance,
// start and end.
struct ListWip {
    uint8_t marker = 0;
    int32_t balance = 0;
    int32_t start = 0;
    int32_t end = 0;
};

bool ListItemResolve(Tokenizer* t, Subresult*) {
    Vec<ListWip> listsWip;
    Vec<ListWip> lists;
    int32_t index = 0;
    int32_t balance = 0;

    while (index < t->events.len) {
        const Event& event = t->events[index];
        if (event.name == Name::ListItem) {
            if (event.kind == Kind::Enter) {
                Name listItem = Name::ListItem;
                int32_t end = SkipOpt(t->events, index, &listItem, 1) - 1;
                Name listItemMarker = Name::ListItemMarker;
                int32_t markerIndex =
                    SkipTo(t->events, index, &listItemMarker, 1);
                ListWip current;
                current.marker = (uint8_t)t->parseState->bytes
                                     .s[t->events[markerIndex].point.index];
                current.balance = balance;
                current.start = index;
                current.end = end;

                int32_t listIndex = len(listsWip);
                bool matched = false;
                while (listIndex > 0) {
                    listIndex -= 1;
                    const ListWip& previous = listsWip[listIndex];
                    Name names[4] = {Name::SpaceOrTab, Name::LineEnding,
                                     Name::BlankLineEnding,
                                     Name::BlockQuotePrefix};
                    int32_t before =
                        SkipOpt(t->events, previous.end + 1, names, 4);
                    if (previous.marker == current.marker &&
                        previous.balance == current.balance &&
                        before == current.start) {
                        listsWip[listIndex].end = current.end;
                        for (int32_t i = listIndex + 1; i < len(listsWip);
                             i++) {
                            VecAppend(lists, listsWip[i]);
                        }
                        listsWip.len = listIndex + 1;
                        matched = true;
                        break;
                    }
                }

                if (!matched) {
                    int32_t i = len(listsWip);
                    int32_t exit = -1;
                    while (i > 0) {
                        i -= 1;
                        if (current.start > listsWip[i].end) {
                            exit = i;
                        } else {
                            break;
                        }
                    }
                    if (exit != -1) {
                        for (int32_t j = exit; j < len(listsWip); j++) {
                            VecAppend(lists, listsWip[j]);
                        }
                        listsWip.len = exit;
                    }
                    VecAppend(listsWip, current);
                }

                balance += 1;
            } else {
                balance -= 1;
            }
        }
        index += 1;
    }

    for (int32_t i = 0; i < len(listsWip); i++) {
        VecAppend(lists, listsWip[i]);
    }

    for (int32_t i = 0; i < len(lists); i++) {
        const ListWip& listItem = lists[i];
        Event listStart = t->events[listItem.start];
        Event listEnd = t->events[listItem.end];
        Name name = (listItem.marker == '.' || listItem.marker == ')')
                        ? Name::ListOrdered
                        : Name::ListUnordered;
        listStart.name = name;
        listEnd.name = name;
        EditMapAdd(t->map, listItem.start, 0, &listStart, 1);
        EditMapAdd(t->map, listItem.end + 1, 0, &listEnd, 1);
    }

    EditMapConsume(t->map, t->events);
    return false;
}

// ─── definition.rs ───────────────────────────────────────────────────────

State DefinitionStart(Tokenizer* t) {
    if (!t->parseState->options->constructs.definition) {
        return StateNok();
    }
    if (t->interrupt) {
        // Can only interrupt when the thing before is a definition too.
        if (t->events.len == 0) {
            return StateNok();
        }
        Name names[2] = {Name::LineEnding, Name::SpaceOrTab};
        int32_t before = SkipOptBack(t->events, t->events.len - 1, names, 2);
        if (t->events[before].name != Name::Definition) {
            return StateNok();
        }
    }
    Enter(t, Name::Definition);
    if (t->current == '\t' || t->current == ' ') {
        TokenizerAttempt(t, StateNext(StateName::DefinitionBefore), StateNok());
        return StateRetry(SpaceOrTab(t));
    }
    return StateRetry(StateName::DefinitionBefore);
}

State DefinitionBefore(Tokenizer* t) {
    if (t->current == '[') {
        t->tokenizeState.token1 = Name::DefinitionLabel;
        t->tokenizeState.token2 = Name::DefinitionLabelMarker;
        t->tokenizeState.token3 = Name::DefinitionLabelString;
        TokenizerAttempt(t, StateNext(StateName::DefinitionLabelAfter),
                         StateNext(StateName::DefinitionLabelNok));
        return StateRetry(StateName::LabelStart);
    }
    return StateNok();
}

State DefinitionLabelAfter(Tokenizer* t) {
    t->tokenizeState.token1 = Name::Data;
    t->tokenizeState.token2 = Name::Data;
    t->tokenizeState.token3 = Name::Data;
    if (t->current == ':') {
        Name labelString = Name::DefinitionLabelString;
        t->tokenizeState
            .end = SkipToBack(t->events, t->events.len - 1, &labelString, 1);
        Enter(t, Name::DefinitionMarker);
        Consume(t);
        Exit(t, Name::DefinitionMarker);
        return StateNext(StateName::DefinitionMarkerAfter);
    }
    return StateNok();
}

State DefinitionLabelNok(Tokenizer* t) {
    t->tokenizeState.token1 = Name::Data;
    t->tokenizeState.token2 = Name::Data;
    t->tokenizeState.token3 = Name::Data;
    return StateNok();
}

State DefinitionMarkerAfter(Tokenizer* t) {
    TokenizerAttempt(t, StateNext(StateName::DefinitionDestinationBefore),
                     StateNext(StateName::DefinitionDestinationBefore));
    return StateRetry(SpaceOrTabEol(t));
}

State DefinitionDestinationBefore(Tokenizer* t) {
    t->tokenizeState.token1 = Name::DefinitionDestination;
    t->tokenizeState.token2 = Name::DefinitionDestinationLiteral;
    t->tokenizeState.token3 = Name::DefinitionDestinationLiteralMarker;
    t->tokenizeState.token4 = Name::DefinitionDestinationRaw;
    t->tokenizeState.token5 = Name::DefinitionDestinationString;
    t->tokenizeState.sizeB = kSizeMax;
    TokenizerAttempt(t, StateNext(StateName::DefinitionDestinationAfter),
                     StateNext(StateName::DefinitionDestinationMissing));
    return StateRetry(StateName::DestinationStart);
}

State DefinitionDestinationAfter(Tokenizer* t) {
    t->tokenizeState.token1 = Name::Data;
    t->tokenizeState.token2 = Name::Data;
    t->tokenizeState.token3 = Name::Data;
    t->tokenizeState.token4 = Name::Data;
    t->tokenizeState.token5 = Name::Data;
    t->tokenizeState.sizeB = 0;
    TokenizerAttempt(t, StateNext(StateName::DefinitionAfter),
                     StateNext(StateName::DefinitionAfter));
    return StateRetry(StateName::DefinitionTitleBefore);
}

State DefinitionDestinationMissing(Tokenizer* t) {
    t->tokenizeState.token1 = Name::Data;
    t->tokenizeState.token2 = Name::Data;
    t->tokenizeState.token3 = Name::Data;
    t->tokenizeState.token4 = Name::Data;
    t->tokenizeState.token5 = Name::Data;
    t->tokenizeState.sizeB = 0;
    t->tokenizeState.end = 0;
    return StateNok();
}

State DefinitionAfter(Tokenizer* t) {
    if (t->current == '\t' || t->current == ' ') {
        TokenizerAttempt(t, StateNext(StateName::DefinitionAfterWhitespace),
                         StateNok());
        return StateRetry(SpaceOrTab(t));
    }
    return StateRetry(StateName::DefinitionAfterWhitespace);
}

State DefinitionAfterWhitespace(Tokenizer* t) {
    if (t->current < 0 || t->current == '\n') {
        Exit(t, Name::Definition);
        Position position =
            PositionFromExitEvent(t->events, t->tokenizeState.end);
        Slice slice = SliceFromPosition(t->parseState->bytes, position);
        // Rust reads `slice.as_str()`, so the virtual spaces a tab stands in
        // for at either edge are not part of the identifier.
        VecAppend(t->tokenizeState.definitions,
                  NormalizeIdentifier(t->parseState->scratch, slice.bytes));
        t->tokenizeState.end = 0;
        // You’d be interrupting.
        t->interrupt = true;
        return StateOk();
    }
    t->tokenizeState.end = 0;
    return StateNok();
}

State DefinitionTitleBefore(Tokenizer* t) {
    if (t->current == '\t' || t->current == '\n' || t->current == ' ') {
        TokenizerAttempt(t, StateNext(StateName::DefinitionTitleBeforeMarker),
                         StateNok());
        return StateRetry(SpaceOrTabEol(t));
    }
    return StateNok();
}

State DefinitionTitleBeforeMarker(Tokenizer* t) {
    t->tokenizeState.token1 = Name::DefinitionTitle;
    t->tokenizeState.token2 = Name::DefinitionTitleMarker;
    t->tokenizeState.token3 = Name::DefinitionTitleString;
    TokenizerAttempt(t, StateNext(StateName::DefinitionTitleAfter), StateNok());
    return StateRetry(StateName::TitleStart);
}

State DefinitionTitleAfter(Tokenizer* t) {
    t->tokenizeState.token1 = Name::Data;
    t->tokenizeState.token2 = Name::Data;
    t->tokenizeState.token3 = Name::Data;
    if (t->current == '\t' || t->current == ' ') {
        TokenizerAttempt(
            t, StateNext(StateName::DefinitionTitleAfterOptionalWhitespace),
            StateNok());
        return StateRetry(SpaceOrTab(t));
    }
    return StateRetry(StateName::DefinitionTitleAfterOptionalWhitespace);
}

State DefinitionTitleAfterOptionalWhitespace(Tokenizer* t) {
    if (t->current < 0 || t->current == '\n') {
        return StateOk();
    }
    return StateNok();
}

// ─── frontmatter.rs ──────────────────────────────────────────────────────

State FrontmatterStart(Tokenizer* t) {
    if (t->parseState->options->constructs.frontmatter &&
        (t->current == '+' || t->current == '-')) {
        t->tokenizeState.marker = (uint8_t)t->current;
        Enter(t, Name::Frontmatter);
        Enter(t, Name::FrontmatterFence);
        Enter(t, Name::FrontmatterSequence);
        return StateRetry(StateName::FrontmatterOpenSequence);
    }
    return StateNok();
}

State FrontmatterOpenSequence(Tokenizer* t) {
    if (t->current == (int32_t)t->tokenizeState.marker) {
        t->tokenizeState.size += 1;
        Consume(t);
        return StateNext(StateName::FrontmatterOpenSequence);
    }
    if (t->tokenizeState.size == kFrontmatterSequenceSize) {
        t->tokenizeState.size = 0;
        Exit(t, Name::FrontmatterSequence);
        if (t->current == '\t' || t->current == ' ') {
            TokenizerAttempt(t, StateNext(StateName::FrontmatterOpenAfter),
                             StateNok());
            return StateRetry(SpaceOrTab(t));
        }
        return StateRetry(StateName::FrontmatterOpenAfter);
    }
    t->tokenizeState.marker = 0;
    t->tokenizeState.size = 0;
    return StateNok();
}

State FrontmatterOpenAfter(Tokenizer* t) {
    if (t->current == '\n') {
        Exit(t, Name::FrontmatterFence);
        Enter(t, Name::LineEnding);
        Consume(t);
        Exit(t, Name::LineEnding);
        TokenizerAttempt(t, StateNext(StateName::FrontmatterAfter),
                         StateNext(StateName::FrontmatterContentStart));
        return StateNext(StateName::FrontmatterCloseStart);
    }
    t->tokenizeState.marker = 0;
    return StateNok();
}

State FrontmatterCloseStart(Tokenizer* t) {
    if (t->current == (int32_t)t->tokenizeState.marker) {
        Enter(t, Name::FrontmatterFence);
        Enter(t, Name::FrontmatterSequence);
        return StateRetry(StateName::FrontmatterCloseSequence);
    }
    return StateNok();
}

State FrontmatterCloseSequence(Tokenizer* t) {
    if (t->current == (int32_t)t->tokenizeState.marker) {
        t->tokenizeState.size += 1;
        Consume(t);
        return StateNext(StateName::FrontmatterCloseSequence);
    }
    if (t->tokenizeState.size == kFrontmatterSequenceSize) {
        t->tokenizeState.size = 0;
        Exit(t, Name::FrontmatterSequence);
        if (t->current == '\t' || t->current == ' ') {
            TokenizerAttempt(t, StateNext(StateName::FrontmatterCloseAfter),
                             StateNok());
            return StateRetry(SpaceOrTab(t));
        }
        return StateRetry(StateName::FrontmatterCloseAfter);
    }
    t->tokenizeState.size = 0;
    return StateNok();
}

State FrontmatterCloseAfter(Tokenizer* t) {
    if (t->current < 0 || t->current == '\n') {
        Exit(t, Name::FrontmatterFence);
        return StateOk();
    }
    return StateNok();
}

State FrontmatterContentStart(Tokenizer* t) {
    if (t->current < 0 || t->current == '\n') {
        return StateRetry(StateName::FrontmatterContentEnd);
    }
    Enter(t, Name::FrontmatterChunk);
    return StateRetry(StateName::FrontmatterContentInside);
}

State FrontmatterContentInside(Tokenizer* t) {
    if (t->current < 0 || t->current == '\n') {
        Exit(t, Name::FrontmatterChunk);
        return StateRetry(StateName::FrontmatterContentEnd);
    }
    Consume(t);
    return StateNext(StateName::FrontmatterContentInside);
}

State FrontmatterContentEnd(Tokenizer* t) {
    if (t->current < 0) {
        t->tokenizeState.marker = 0;
        return StateNok();
    }
    Enter(t, Name::LineEnding);
    Consume(t);
    Exit(t, Name::LineEnding);
    TokenizerAttempt(t, StateNext(StateName::FrontmatterAfter),
                     StateNext(StateName::FrontmatterContentStart));
    return StateNext(StateName::FrontmatterCloseStart);
}

State FrontmatterAfter(Tokenizer* t) {
    Exit(t, Name::Frontmatter);
    return StateOk();
}

} // namespace markdown
