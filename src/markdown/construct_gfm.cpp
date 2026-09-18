/* The GFM constructs: tables, footnotes, task list checkboxes and bare URLs.

   | Rust                                     | here                       |
   | ---------------------------------------- | -------------------------- |
   | `construct/gfm_table.rs`                 | GfmTable*                  |
   | `construct/gfm_footnote_definition.rs`   | GfmFootnoteDefinition*     |
   | `construct/gfm_label_start_footnote.rs`  | GfmLabelStartFootnote*     |
   | `construct/gfm_task_list_item_check.rs`  | GfmTaskListItemCheck*      |
   | `construct/gfm_autolink_literal.rs`      | GfmAutolinkLiteral*        |

   Part of the C++ port of markdown-rs 1.0.0 (see src/markdown/readme.md). */

#include "markdown/construct.h"

namespace markdown {

// ─── gfm_label_start_footnote.rs ─────────────────────────────────────────

State GfmLabelStartFootnoteStart(Tokenizer* t) {
    if (t->parseState->options->constructs.gfmLabelStartFootnote &&
        t->current == '[') {
        Enter(t, Name::GfmFootnoteCallLabel);
        Enter(t, Name::LabelMarker);
        Consume(t);
        Exit(t, Name::LabelMarker);
        return StateNext(StateName::GfmLabelStartFootnoteOpen);
    }
    return StateNok();
}

State GfmLabelStartFootnoteOpen(Tokenizer* t) {
    if (t->current == '^') {
        Enter(t, Name::GfmFootnoteCallMarker);
        Consume(t);
        Exit(t, Name::GfmFootnoteCallMarker);
        Exit(t, Name::GfmFootnoteCallLabel);
        LabelStartMark mark;
        mark.kind = LabelKind::GfmFootnote;
        mark.startA = t->events.len - 6;
        mark.startB = t->events.len - 1;
        VecAppend(t->tokenizeState.labelStarts, mark);
        RegisterResolverBefore(t, ResolveName::Label);
        return StateOk();
    }
    return StateNok();
}

// ─── gfm_task_list_item_check.rs ─────────────────────────────────────────

State GfmTaskListItemCheckStart(Tokenizer* t) {
    if (t->parseState->options->constructs.gfmTaskListItem &&
        t->tokenizeState.documentAtFirstParagraphOfListItem &&
        t->current == '[' && t->previous < 0) {
        Enter(t, Name::GfmTaskListItemCheck);
        Enter(t, Name::GfmTaskListItemMarker);
        Consume(t);
        Exit(t, Name::GfmTaskListItemMarker);
        return StateNext(StateName::GfmTaskListItemCheckInside);
    }
    return StateNok();
}

State GfmTaskListItemCheckInside(Tokenizer* t) {
    if (t->current == '\t' || t->current == '\n' || t->current == ' ') {
        Enter(t, Name::GfmTaskListItemValueUnchecked);
        Consume(t);
        Exit(t, Name::GfmTaskListItemValueUnchecked);
        return StateNext(StateName::GfmTaskListItemCheckClose);
    }
    if (t->current == 'X' || t->current == 'x') {
        Enter(t, Name::GfmTaskListItemValueChecked);
        Consume(t);
        Exit(t, Name::GfmTaskListItemValueChecked);
        return StateNext(StateName::GfmTaskListItemCheckClose);
    }
    return StateNok();
}

State GfmTaskListItemCheckClose(Tokenizer* t) {
    if (t->current == ']') {
        Enter(t, Name::GfmTaskListItemMarker);
        Consume(t);
        Exit(t, Name::GfmTaskListItemMarker);
        Exit(t, Name::GfmTaskListItemCheck);
        return StateNext(StateName::GfmTaskListItemCheckAfter);
    }
    return StateNok();
}

State GfmTaskListItemCheckAfter(Tokenizer* t) {
    if (t->current == '\n') {
        return StateOk();
    }
    if (t->current == '\t' || t->current == ' ') {
        // The whitespace is part of the paragraph, so check it and then
        // parse it again as data.
        TokenizerCheck(t, StateOk(), StateNok());
        TokenizerAttempt(
            t, StateNext(StateName::GfmTaskListItemCheckAfterSpaceOrTab),
            StateNok());
        return StateRetry(SpaceOrTab(t));
    }
    return StateNok();
}

State GfmTaskListItemCheckAfterSpaceOrTab(Tokenizer* t) {
    // An item cannot be just a checkbox.
    if (t->current < 0) {
        return StateNok();
    }
    return StateOk();
}

// ─── gfm_footnote_definition.rs ──────────────────────────────────────────

State GfmFootnoteDefinitionStart(Tokenizer* t) {
    if (!t->parseState->options->constructs.gfmFootnoteDefinition) {
        return StateNok();
    }
    Enter(t, Name::GfmFootnoteDefinition);
    if (t->current == '\t' || t->current == ' ') {
        TokenizerAttempt(t,
                         StateNext(StateName::GfmFootnoteDefinitionLabelBefore),
                         StateNok());
        int32_t max = t->parseState->options->constructs.codeIndented
                          ? kTabSize - 1
                          : kSizeMax;
        return StateRetry(SpaceOrTabMinMax(t, 1, max));
    }
    return StateRetry(StateName::GfmFootnoteDefinitionLabelBefore);
}

State GfmFootnoteDefinitionLabelBefore(Tokenizer* t) {
    if (t->current == '[') {
        Enter(t, Name::GfmFootnoteDefinitionPrefix);
        Enter(t, Name::GfmFootnoteDefinitionLabel);
        Enter(t, Name::GfmFootnoteDefinitionLabelMarker);
        Consume(t);
        Exit(t, Name::GfmFootnoteDefinitionLabelMarker);
        return StateNext(StateName::GfmFootnoteDefinitionLabelAtMarker);
    }
    return StateNok();
}

State GfmFootnoteDefinitionLabelAtMarker(Tokenizer* t) {
    if (t->current == '^') {
        Enter(t, Name::GfmFootnoteDefinitionMarker);
        Consume(t);
        Exit(t, Name::GfmFootnoteDefinitionMarker);
        Enter(t, Name::GfmFootnoteDefinitionLabelString);
        Link link;
        link.content = ContentKind::String;
        EnterLink(t, Name::Data, link);
        return StateNext(StateName::GfmFootnoteDefinitionLabelInside);
    }
    return StateNok();
}

State GfmFootnoteDefinitionLabelInside(Tokenizer* t) {
    if (t->tokenizeState.size > kLinkReferenceSizeMax || t->current < 0 ||
        t->current == '\t' || t->current == '\n' || t->current == ' ' ||
        t->current == '[' ||
        (t->current == ']' && t->tokenizeState.size == 0)) {
        t->tokenizeState.size = 0;
        return StateNok();
    }
    if (t->current == ']') {
        t->tokenizeState.size = 0;
        Exit(t, Name::Data);
        Exit(t, Name::GfmFootnoteDefinitionLabelString);
        Enter(t, Name::GfmFootnoteDefinitionLabelMarker);
        Consume(t);
        Exit(t, Name::GfmFootnoteDefinitionLabelMarker);
        Exit(t, Name::GfmFootnoteDefinitionLabel);
        return StateNext(StateName::GfmFootnoteDefinitionLabelAfter);
    }
    StateName next = t->current == '\\'
                         ? StateName::GfmFootnoteDefinitionLabelEscape
                         : StateName::GfmFootnoteDefinitionLabelInside;
    Consume(t);
    t->tokenizeState.size += 1;
    return StateNext(next);
}

State GfmFootnoteDefinitionLabelEscape(Tokenizer* t) {
    if (t->current == '[' || t->current == '\\' || t->current == ']') {
        t->tokenizeState.size += 1;
        Consume(t);
        return StateNext(StateName::GfmFootnoteDefinitionLabelInside);
    }
    return StateRetry(StateName::GfmFootnoteDefinitionLabelInside);
}

State GfmFootnoteDefinitionLabelAfter(Tokenizer* t) {
    if (t->current == ':') {
        Name labelString = Name::GfmFootnoteDefinitionLabelString;
        int32_t end = SkipToBack(t->events, t->events.len - 1, &labelString, 1);
        Position position = PositionFromExitEvent(t->events, end);
        Slice slice = SliceFromPosition(t->parseState->bytes, position);
        VecAppend(t->tokenizeState.gfmFootnoteDefinitions,
                  NormalizeIdentifier(t->parseState->scratch, slice.bytes));
        Enter(t, Name::DefinitionMarker);
        Consume(t);
        Exit(t, Name::DefinitionMarker);
        TokenizerAttempt(
            t, StateNext(StateName::GfmFootnoteDefinitionWhitespaceAfter),
            StateNok());
        return StateNext(SpaceOrTabMinMax(t, 0, kSizeMax));
    }
    return StateNok();
}

State GfmFootnoteDefinitionWhitespaceAfter(Tokenizer* t) {
    Exit(t, Name::GfmFootnoteDefinitionPrefix);
    return StateOk();
}

State GfmFootnoteDefinitionContStart(Tokenizer* t) {
    TokenizerCheck(t, StateNext(StateName::GfmFootnoteDefinitionContBlank),
                   StateNext(StateName::GfmFootnoteDefinitionContFilled));
    return StateRetry(StateName::BlankLineStart);
}

State GfmFootnoteDefinitionContBlank(Tokenizer* t) {
    if (t->current == '\t' || t->current == ' ') {
        return StateRetry(SpaceOrTabMinMax(t, 0, kTabSize));
    }
    return StateOk();
}

State GfmFootnoteDefinitionContFilled(Tokenizer* t) {
    if (t->current == '\t' || t->current == ' ') {
        return StateRetry(SpaceOrTabMinMax(t, kTabSize, kTabSize));
    }
    return StateNok();
}

// ─── gfm_table.rs ────────────────────────────────────────────────────────

State GfmTableStart(Tokenizer* t) {
    if (!t->parseState->options->constructs.gfmTable) {
        return StateNok();
    }
    if (!t->pierce && t->events.len > 0) {
        Name names[2] = {Name::LineEnding, Name::SpaceOrTab};
        int32_t at = SkipOptBack(t->events, t->events.len - 1, names, 2);
        Name name = t->events[at].name;
        if (name == Name::GfmTableHead || name == Name::GfmTableRow) {
            return StateRetry(StateName::GfmTableBodyRowStart);
        }
    }
    return StateRetry(StateName::GfmTableHeadRowBefore);
}

State GfmTableHeadRowBefore(Tokenizer* t) {
    Enter(t, Name::GfmTableHead);
    Enter(t, Name::GfmTableRow);
    if (t->current == '\t' || t->current == ' ') {
        TokenizerAttempt(t, StateNext(StateName::GfmTableHeadRowStart),
                         StateNok());
        int32_t max = t->parseState->options->constructs.codeIndented
                          ? kTabSize - 1
                          : kSizeMax;
        return StateRetry(SpaceOrTabMinMax(t, 0, max));
    }
    return StateRetry(StateName::GfmTableHeadRowStart);
}

State GfmTableHeadRowStart(Tokenizer* t) {
    if (t->current == '\t' || t->current == ' ') {
        return StateNok();
    }
    if (t->current == '|') {
        return StateRetry(StateName::GfmTableHeadRowBreak);
    }
    t->tokenizeState.seen = true;
    t->tokenizeState.sizeB += 1;
    return StateRetry(StateName::GfmTableHeadRowBreak);
}

State GfmTableHeadRowBreak(Tokenizer* t) {
    if (t->current < 0) {
        t->tokenizeState.seen = false;
        t->tokenizeState.size = 0;
        t->tokenizeState.sizeB = 0;
        return StateNok();
    }
    if (t->current == '\n') {
        if (t->tokenizeState.sizeB > 1) {
            t->tokenizeState.sizeB = 0;
            // Feel free to interrupt.
            t->interrupt = true;
            Exit(t, Name::GfmTableRow);
            Enter(t, Name::LineEnding);
            Consume(t);
            Exit(t, Name::LineEnding);
            return StateNext(StateName::GfmTableHeadDelimiterStart);
        }
        t->tokenizeState.seen = false;
        t->tokenizeState.size = 0;
        t->tokenizeState.sizeB = 0;
        return StateNok();
    }
    if (t->current == '\t' || t->current == ' ') {
        TokenizerAttempt(t, StateNext(StateName::GfmTableHeadRowBreak),
                         StateNok());
        return StateRetry(SpaceOrTab(t));
    }
    t->tokenizeState.sizeB += 1;
    if (t->tokenizeState.seen) {
        t->tokenizeState.seen = false;
        t->tokenizeState.size += 1;
    }
    if (t->current == '|') {
        Enter(t, Name::GfmTableCellDivider);
        Consume(t);
        Exit(t, Name::GfmTableCellDivider);
        t->tokenizeState.seen = true;
        return StateNext(StateName::GfmTableHeadRowBreak);
    }
    Enter(t, Name::Data);
    return StateRetry(StateName::GfmTableHeadRowData);
}

State GfmTableHeadRowData(Tokenizer* t) {
    if (t->current < 0 || t->current == '\t' || t->current == '\n' ||
        t->current == ' ' || t->current == '|') {
        Exit(t, Name::Data);
        return StateRetry(StateName::GfmTableHeadRowBreak);
    }
    StateName name = t->current == '\\' ? StateName::GfmTableHeadRowEscape
                                        : StateName::GfmTableHeadRowData;
    Consume(t);
    return StateNext(name);
}

State GfmTableHeadRowEscape(Tokenizer* t) {
    if (t->current == '\\' || t->current == '|') {
        Consume(t);
        return StateNext(StateName::GfmTableHeadRowData);
    }
    return StateRetry(StateName::GfmTableHeadRowData);
}

State GfmTableHeadDelimiterStart(Tokenizer* t) {
    // Reset `interrupt`.
    t->interrupt = false;
    if (t->lazy || t->pierce) {
        t->tokenizeState.size = 0;
        return StateNok();
    }
    Enter(t, Name::GfmTableDelimiterRow);
    t->tokenizeState.seen = false;
    if (t->current == '\t' || t->current == ' ') {
        TokenizerAttempt(t, StateNext(StateName::GfmTableHeadDelimiterBefore),
                         StateNext(StateName::GfmTableHeadDelimiterNok));
        int32_t max = t->parseState->options->constructs.codeIndented
                          ? kTabSize - 1
                          : kSizeMax;
        return StateRetry(SpaceOrTabMinMax(t, 0, max));
    }
    return StateRetry(StateName::GfmTableHeadDelimiterBefore);
}

State GfmTableHeadDelimiterBefore(Tokenizer* t) {
    if (t->current == '-' || t->current == ':') {
        return StateRetry(StateName::GfmTableHeadDelimiterValueBefore);
    }
    if (t->current == '|') {
        t->tokenizeState.seen = true;
        Enter(t, Name::GfmTableCellDivider);
        Consume(t);
        Exit(t, Name::GfmTableCellDivider);
        return StateNext(StateName::GfmTableHeadDelimiterCellBefore);
    }
    return StateRetry(StateName::GfmTableHeadDelimiterNok);
}

State GfmTableHeadDelimiterCellBefore(Tokenizer* t) {
    if (t->current == '\t' || t->current == ' ') {
        TokenizerAttempt(t,
                         StateNext(StateName::GfmTableHeadDelimiterValueBefore),
                         StateNok());
        return StateRetry(SpaceOrTab(t));
    }
    return StateRetry(StateName::GfmTableHeadDelimiterValueBefore);
}

State GfmTableHeadDelimiterValueBefore(Tokenizer* t) {
    if (t->current < 0 || t->current == '\n') {
        return StateRetry(StateName::GfmTableHeadDelimiterCellAfter);
    }
    if (t->current == ':') {
        t->tokenizeState.sizeB += 1;
        t->tokenizeState.seen = true;
        Enter(t, Name::GfmTableDelimiterMarker);
        Consume(t);
        Exit(t, Name::GfmTableDelimiterMarker);
        return StateNext(StateName::GfmTableHeadDelimiterLeftAlignmentAfter);
    }
    if (t->current == '-') {
        t->tokenizeState.sizeB += 1;
        return StateRetry(StateName::GfmTableHeadDelimiterLeftAlignmentAfter);
    }
    return StateRetry(StateName::GfmTableHeadDelimiterNok);
}

State GfmTableHeadDelimiterLeftAlignmentAfter(Tokenizer* t) {
    if (t->current == '-') {
        Enter(t, Name::GfmTableDelimiterFiller);
        return StateRetry(StateName::GfmTableHeadDelimiterFiller);
    }
    return StateRetry(StateName::GfmTableHeadDelimiterNok);
}

State GfmTableHeadDelimiterFiller(Tokenizer* t) {
    if (t->current == '-') {
        Consume(t);
        return StateNext(StateName::GfmTableHeadDelimiterFiller);
    }
    if (t->current == ':') {
        t->tokenizeState.seen = true;
        Exit(t, Name::GfmTableDelimiterFiller);
        Enter(t, Name::GfmTableDelimiterMarker);
        Consume(t);
        Exit(t, Name::GfmTableDelimiterMarker);
        return StateNext(StateName::GfmTableHeadDelimiterRightAlignmentAfter);
    }
    Exit(t, Name::GfmTableDelimiterFiller);
    return StateRetry(StateName::GfmTableHeadDelimiterRightAlignmentAfter);
}

State GfmTableHeadDelimiterRightAlignmentAfter(Tokenizer* t) {
    if (t->current == '\t' || t->current == ' ') {
        TokenizerAttempt(t,
                         StateNext(StateName::GfmTableHeadDelimiterCellAfter),
                         StateNok());
        return StateRetry(SpaceOrTab(t));
    }
    return StateRetry(StateName::GfmTableHeadDelimiterCellAfter);
}

State GfmTableHeadDelimiterCellAfter(Tokenizer* t) {
    if (t->current < 0 || t->current == '\n') {
        // Exit if there was no `-` at all, or if the head and delimiter rows
        // are not the same size.
        if (!t->tokenizeState.seen || t->tokenizeState.size != t->tokenizeState
                                                                   .sizeB) {
            return StateRetry(StateName::GfmTableHeadDelimiterNok);
        }
        t->tokenizeState.seen = false;
        t->tokenizeState.size = 0;
        t->tokenizeState.sizeB = 0;
        Exit(t, Name::GfmTableDelimiterRow);
        Exit(t, Name::GfmTableHead);
        RegisterResolver(t, ResolveName::GfmTable);
        return StateOk();
    }
    if (t->current == '|') {
        return StateRetry(StateName::GfmTableHeadDelimiterBefore);
    }
    return StateRetry(StateName::GfmTableHeadDelimiterNok);
}

State GfmTableHeadDelimiterNok(Tokenizer* t) {
    t->tokenizeState.seen = false;
    t->tokenizeState.size = 0;
    t->tokenizeState.sizeB = 0;
    return StateNok();
}

State GfmTableBodyRowStart(Tokenizer* t) {
    if (t->lazy) {
        return StateNok();
    }
    Enter(t, Name::GfmTableRow);
    if (t->current == '\t' || t->current == ' ') {
        TokenizerAttempt(t, StateNext(StateName::GfmTableBodyRowBreak),
                         StateNok());
        return StateRetry(SpaceOrTabMinMax(t, 0, kSizeMax));
    }
    return StateRetry(StateName::GfmTableBodyRowBreak);
}

State GfmTableBodyRowBreak(Tokenizer* t) {
    if (t->current < 0 || t->current == '\n') {
        Exit(t, Name::GfmTableRow);
        return StateOk();
    }
    if (t->current == '\t' || t->current == ' ') {
        TokenizerAttempt(t, StateNext(StateName::GfmTableBodyRowBreak),
                         StateNok());
        return StateRetry(SpaceOrTab(t));
    }
    if (t->current == '|') {
        Enter(t, Name::GfmTableCellDivider);
        Consume(t);
        Exit(t, Name::GfmTableCellDivider);
        return StateNext(StateName::GfmTableBodyRowBreak);
    }
    Enter(t, Name::Data);
    return StateRetry(StateName::GfmTableBodyRowData);
}

State GfmTableBodyRowData(Tokenizer* t) {
    if (t->current < 0 || t->current == '\t' || t->current == '\n' ||
        t->current == ' ' || t->current == '|') {
        Exit(t, Name::Data);
        return StateRetry(StateName::GfmTableBodyRowBreak);
    }
    StateName name = t->current == '\\' ? StateName::GfmTableBodyRowEscape
                                        : StateName::GfmTableBodyRowData;
    Consume(t);
    return StateNext(name);
}

State GfmTableBodyRowEscape(Tokenizer* t) {
    if (t->current == '\\' || t->current == '|') {
        Consume(t);
        return StateNext(StateName::GfmTableBodyRowData);
    }
    return StateRetry(StateName::GfmTableBodyRowData);
}

// Rust's `(usize, usize, usize, usize)` cell range: where the previous cell
// ends, where this one starts, and where its value starts and ends.
struct CellRange {
    int32_t previousEnd = 0;
    int32_t start = 0;
    int32_t valueStart = 0;
    int32_t valueEnd = 0;
};

static void FlushCell(Tokenizer* t, const CellRange& range, bool inDelimiterRow,
                      int32_t rowEnd) {
    Name groupName =
        inDelimiterRow ? Name::GfmTableDelimiterCell : Name::GfmTableCell;
    Name valueName = inDelimiterRow ? Name::GfmTableDelimiterCellValue
                                    : Name::GfmTableCellText;

    // Insert an exit for the previous cell, if there is one.
    if (range.previousEnd != 0) {
        Event exit;
        exit.kind = Kind::Exit;
        exit.name = groupName;
        exit.point = t->events[range.previousEnd].point;
        EditMapAdd(t->map, range.previousEnd, 0, &exit, 1);
    }

    // Insert an enter for the current cell.
    Event enter;
    enter.kind = Kind::Enter;
    enter.name = groupName;
    enter.point = t->events[range.start].point;
    EditMapAdd(t->map, range.start, 0, &enter, 1);

    // Insert text start at the first data start and end at the last data end,
    // and remove events between.
    if (range.valueStart != 0) {
        Event valueEnter;
        valueEnter.kind = Kind::Enter;
        valueEnter.name = valueName;
        valueEnter.point = t->events[range.valueStart].point;
        EditMapAdd(t->map, range.valueStart, 0, &valueEnter, 1);

        if (!inDelimiterRow) {
            t->events[range.valueStart].hasLink = true;
            t->events[range.valueStart].link.previous = -1;
            t->events[range.valueStart].link.next = -1;
            t->events[range.valueStart].link.content = ContentKind::Text;
            if (range.valueEnd > range.valueStart + 1) {
                int32_t a = range.valueStart + 1;
                int32_t b = range.valueEnd - range.valueStart - 1;
                EditMapAdd(t->map, a, b, nullptr, 0);
            }
        }

        Event valueExit;
        valueExit.kind = Kind::Exit;
        valueExit.name = valueName;
        valueExit.point = t->events[range.valueEnd].point;
        EditMapAdd(t->map, range.valueEnd + 1, 0, &valueExit, 1);
    }

    // Insert an exit for the last cell, if at the row end.
    if (rowEnd != -1) {
        Event exit;
        exit.kind = Kind::Exit;
        exit.name = groupName;
        exit.point = t->events[rowEnd].point;
        EditMapAdd(t->map, rowEnd, 0, &exit, 1);
    }
}

static void FlushTableEnd(Tokenizer* t, int32_t index, bool body) {
    Event exits[2];
    int32_t len = 0;
    if (body) {
        exits[len].kind = Kind::Exit;
        exits[len].name = Name::GfmTableBody;
        exits[len].point = t->events[index].point;
        len++;
    }
    exits[len].kind = Kind::Exit;
    exits[len].name = Name::GfmTable;
    exits[len].point = t->events[index].point;
    len++;
    EditMapAdd(t->map, index + 1, 0, exits, len);
}

// Whether an event is one that carries a cell's value.
static bool IsCellValueName(Name name) {
    return name == Name::Data || name == Name::GfmTableDelimiterMarker ||
           name == Name::GfmTableDelimiterFiller;
}

bool GfmTableResolve(Tokenizer* t, Subresult*) {
    int32_t index = 0;
    bool inFirstCellAwaitingPipe = true;
    bool inRow = false;
    bool inDelimiterRow = false;
    CellRange lastCell = {};
    CellRange cell = {};
    bool afterHeadAwaitingFirstBodyRow = false;
    int32_t lastTableEnd = 0;
    bool lastTableHasBody = false;

    while (index < t->events.len) {
        const Event& event = t->events[index];
        if (event.kind == Kind::Enter) {
            if (event.name == Name::GfmTableHead) {
                afterHeadAwaitingFirstBodyRow = false;
                // Inject the table end of the previous table.
                if (lastTableEnd != 0) {
                    FlushTableEnd(t, lastTableEnd, lastTableHasBody);
                    lastTableHasBody = false;
                    lastTableEnd = 0;
                }
                Event enter;
                enter.kind = Kind::Enter;
                enter.name = Name::GfmTable;
                enter.point = t->events[index].point;
                EditMapAdd(t->map, index, 0, &enter, 1);
            } else if (event.name == Name::GfmTableRow ||
                       event.name == Name::GfmTableDelimiterRow) {
                inDelimiterRow = event.name == Name::GfmTableDelimiterRow;
                inRow = true;
                inFirstCellAwaitingPipe = true;
                lastCell = CellRange{};
                cell = CellRange{};
                cell.start = index + 1;

                if (afterHeadAwaitingFirstBodyRow) {
                    afterHeadAwaitingFirstBodyRow = false;
                    lastTableHasBody = true;
                    Event enter;
                    enter.kind = Kind::Enter;
                    enter.name = Name::GfmTableBody;
                    enter.point = t->events[index].point;
                    EditMapAdd(t->map, index, 0, &enter, 1);
                }
            } else if (inRow && IsCellValueName(event.name)) {
                inFirstCellAwaitingPipe = false;
                if (cell.valueStart == 0) {
                    if (lastCell.start != 0) {
                        cell.previousEnd = cell.start;
                        FlushCell(t, lastCell, inDelimiterRow, -1);
                        lastCell = CellRange{};
                    }
                    cell.valueStart = index;
                }
            } else if (event.name == Name::GfmTableCellDivider) {
                if (inFirstCellAwaitingPipe) {
                    inFirstCellAwaitingPipe = false;
                } else {
                    if (lastCell.start != 0) {
                        cell.previousEnd = cell.start;
                        FlushCell(t, lastCell, inDelimiterRow, -1);
                    }
                    lastCell = cell;
                    cell = CellRange{};
                    cell.previousEnd = lastCell.start;
                    cell.start = index;
                }
            }
        } else if (event.name == Name::GfmTableHead) {
            afterHeadAwaitingFirstBodyRow = true;
            lastTableEnd = index;
        } else if (event.name == Name::GfmTableRow ||
                   event.name == Name::GfmTableDelimiterRow) {
            inRow = false;
            lastTableEnd = index;
            if (lastCell.start != 0) {
                cell.previousEnd = cell.start;
                FlushCell(t, lastCell, inDelimiterRow, index);
            } else if (cell.start != 0) {
                FlushCell(t, cell, inDelimiterRow, index);
            }
        } else if (inRow && IsCellValueName(event.name)) {
            cell.valueEnd = index;
        }
        index += 1;
    }

    if (lastTableEnd != 0) {
        FlushTableEnd(t, lastTableEnd, lastTableHasBody);
    }

    EditMapConsume(t->map, t->events);
    return false;
}

// ─── gfm_autolink_literal.rs ─────────────────────────────────────────────

State GfmAutolinkLiteralProtocolStart(Tokenizer* t) {
    bool alphaBefore = t->previous >= 0 && IsAsciiAlpha((uint8_t)t->previous);
    if (t->parseState->options->constructs.gfmAutolinkLiteral &&
        (t->current == 'H' || t->current == 'h') && !alphaBefore) {
        Enter(t, Name::GfmAutolinkLiteralProtocol);
        TokenizerAttempt(t,
                         StateNext(StateName::GfmAutolinkLiteralProtocolAfter),
                         StateNok());
        TokenizerAttempt(t,
                         StateNext(StateName::GfmAutolinkLiteralDomainInside),
                         StateNok());
        t->tokenizeState.start = t->point.index;
        return StateRetry(StateName::GfmAutolinkLiteralProtocolPrefixInside);
    }
    return StateNok();
}

State GfmAutolinkLiteralProtocolAfter(Tokenizer* t) {
    Exit(t, Name::GfmAutolinkLiteralProtocol);
    return StateOk();
}

State GfmAutolinkLiteralProtocolPrefixInside(Tokenizer* t) {
    if (t->current >= 0 && IsAsciiAlpha((uint8_t)t->current) &&
        t->point.index - t->tokenizeState.start < 5) {
        Consume(t);
        return StateNext(StateName::GfmAutolinkLiteralProtocolPrefixInside);
    }
    if (t->current == ':') {
        Slice slice = SliceFromIndices(t->parseState->bytes,
                                       t->tokenizeState.start, t->point.index);
        t->tokenizeState.start = 0;
        if (base::StrEqI(slice.bytes, "http") ||
            base::StrEqI(slice.bytes, "https")) {
            Consume(t);
            return StateNext(
                StateName::GfmAutolinkLiteralProtocolSlashesInside);
        }
        return StateNok();
    }
    t->tokenizeState.start = 0;
    return StateNok();
}

State GfmAutolinkLiteralProtocolSlashesInside(Tokenizer* t) {
    if (t->current == '/') {
        Consume(t);
        if (t->tokenizeState.size == 0) {
            t->tokenizeState.size += 1;
            return StateNext(
                StateName::GfmAutolinkLiteralProtocolSlashesInside);
        }
        t->tokenizeState.size = 0;
        return StateOk();
    }
    t->tokenizeState.size = 0;
    return StateNok();
}

State GfmAutolinkLiteralWwwStart(Tokenizer* t) {
    int32_t p = t->previous;
    bool okBefore = p < 0 || p == '\t' || p == '\n' || p == ' ' || p == '(' ||
                    p == '*' || p == '_' || p == '[' || p == ']' || p == '~';
    if (t->parseState->options->constructs.gfmAutolinkLiteral &&
        (t->current == 'W' || t->current == 'w') && okBefore) {
        Enter(t, Name::GfmAutolinkLiteralWww);
        TokenizerAttempt(t, StateNext(StateName::GfmAutolinkLiteralWwwAfter),
                         StateNok());
        // Note: the `check` is for the future: the domain is parsed twice.
        TokenizerCheck(t, StateNext(StateName::GfmAutolinkLiteralDomainInside),
                       StateNok());
        return StateRetry(StateName::GfmAutolinkLiteralWwwPrefixInside);
    }
    return StateNok();
}

State GfmAutolinkLiteralWwwAfter(Tokenizer* t) {
    Exit(t, Name::GfmAutolinkLiteralWww);
    return StateOk();
}

State GfmAutolinkLiteralWwwPrefixInside(Tokenizer* t) {
    if (t->current == '.' && t->tokenizeState.size == 3) {
        t->tokenizeState.size = 0;
        Consume(t);
        return StateNext(StateName::GfmAutolinkLiteralWwwPrefixAfter);
    }
    if ((t->current == 'W' || t->current == 'w') && t->tokenizeState.size < 3) {
        t->tokenizeState.size += 1;
        Consume(t);
        return StateNext(StateName::GfmAutolinkLiteralWwwPrefixInside);
    }
    t->tokenizeState.size = 0;
    return StateNok();
}

State GfmAutolinkLiteralWwwPrefixAfter(Tokenizer* t) {
    // If there is *anything*, we can link.
    if (t->current < 0) {
        return StateNok();
    }
    return StateOk();
}

State GfmAutolinkLiteralDomainInside(Tokenizer* t) {
    if (t->current == '.' || t->current == '_') {
        TokenizerCheck(
            t, StateNext(StateName::GfmAutolinkLiteralDomainAfter),
            StateNext(StateName::GfmAutolinkLiteralDomainAtPunctuation));
        return StateRetry(StateName::GfmAutolinkLiteralTrail);
    }
    // GH documents that only alphanumerics work, but they also support `-`,
    // `_`, and the continuation bytes of a UTF-8 character.
    if (t->current == '-' || (t->current >= 0x80 && t->current <= 0xbf)) {
        Consume(t);
        return StateNext(StateName::GfmAutolinkLiteralDomainInside);
    }
    if (KindAfterIndex(t->parseState->bytes, t->point.index) ==
        CharKind::Other) {
        t->tokenizeState.seen = true;
        Consume(t);
        return StateNext(StateName::GfmAutolinkLiteralDomainInside);
    }
    return StateRetry(StateName::GfmAutolinkLiteralDomainAfter);
}

State GfmAutolinkLiteralDomainAtPunctuation(Tokenizer* t) {
    // There is an underscore in the last segment of the domain.
    if (t->current == '_') {
        t->tokenizeState.marker = '_';
    } else {
        // Otherwise, remember the last segment.
        t->tokenizeState.markerB = t->tokenizeState.marker;
        t->tokenizeState.marker = 0;
    }
    Consume(t);
    return StateNext(StateName::GfmAutolinkLiteralDomainInside);
}

State GfmAutolinkLiteralDomainAfter(Tokenizer* t) {
    State result;
    if (t->tokenizeState.markerB == '_' || t->tokenizeState.marker == '_' ||
        !t->tokenizeState.seen) {
        result = StateNok();
    } else {
        result = StateRetry(StateName::GfmAutolinkLiteralPathInside);
    }
    t->tokenizeState.seen = false;
    t->tokenizeState.marker = 0;
    t->tokenizeState.markerB = 0;
    return result;
}

State GfmAutolinkLiteralPathInside(Tokenizer* t) {
    if (t->current >= 0x80 && t->current <= 0xbf) {
        Consume(t);
        return StateNext(StateName::GfmAutolinkLiteralPathInside);
    }
    if (t->current == '(') {
        t->tokenizeState.size += 1;
        Consume(t);
        return StateNext(StateName::GfmAutolinkLiteralPathInside);
    }
    int32_t c = t->current;
    bool trailing = c == '!' || c == '"' || c == '&' || c == '\'' || c == ')' ||
                    c == '*' || c == ',' || c == '.' || c == ':' || c == ';' ||
                    c == '<' || c == '?' || c == ']' || c == '_' || c == '~';
    if (trailing) {
        StateName next = StateName::GfmAutolinkLiteralPathAfter;
        if (c == ')' && t->tokenizeState.sizeB < t->tokenizeState.size) {
            next = StateName::GfmAutolinkLiteralPathAtPunctuation;
        }
        TokenizerCheck(
            t, StateNext(next),
            StateNext(StateName::GfmAutolinkLiteralPathAtPunctuation));
        return StateRetry(StateName::GfmAutolinkLiteralTrail);
    }
    if (t->current < 0 ||
        KindAfterIndex(t->parseState->bytes, t->point.index) ==
            CharKind::Whitespace) {
        return StateRetry(StateName::GfmAutolinkLiteralPathAfter);
    }
    Consume(t);
    return StateNext(StateName::GfmAutolinkLiteralPathInside);
}

State GfmAutolinkLiteralPathAtPunctuation(Tokenizer* t) {
    if (t->current == ')') {
        t->tokenizeState.sizeB += 1;
    }
    Consume(t);
    return StateNext(StateName::GfmAutolinkLiteralPathInside);
}

State GfmAutolinkLiteralPathAfter(Tokenizer* t) {
    t->tokenizeState.size = 0;
    t->tokenizeState.sizeB = 0;
    return StateOk();
}

State GfmAutolinkLiteralTrail(Tokenizer* t) {
    int32_t c = t->current;
    bool trailing = c == '!' || c == '"' || c == '\'' || c == ')' || c == '*' ||
                    c == ',' || c == '.' || c == ':' || c == ';' || c == '?' ||
                    c == '_' || c == '~';
    if (trailing) {
        Consume(t);
        return StateNext(StateName::GfmAutolinkLiteralTrail);
    }
    if (c == '&') {
        Consume(t);
        return StateNext(StateName::GfmAutolinkLiteralTrailCharRefStart);
    }
    if (c == '<') {
        return StateOk();
    }
    if (c == ']') {
        Consume(t);
        return StateNext(StateName::GfmAutolinkLiteralTrailBracketAfter);
    }
    if (KindAfterIndex(t->parseState->bytes, t->point.index) ==
        CharKind::Whitespace) {
        return StateOk();
    }
    return StateNok();
}

State GfmAutolinkLiteralTrailBracketAfter(Tokenizer* t) {
    int32_t c = t->current;
    if (c < 0 || c == '\t' || c == '\n' || c == ' ' || c == '(' || c == '[') {
        return StateOk();
    }
    return StateRetry(StateName::GfmAutolinkLiteralTrail);
}

State GfmAutolinkLiteralTrailCharRefStart(Tokenizer* t) {
    if (t->current >= 0 && IsAsciiAlpha((uint8_t)t->current)) {
        return StateRetry(StateName::GfmAutolinkLiteralTrailCharRefInside);
    }
    return StateNok();
}

State GfmAutolinkLiteralTrailCharRefInside(Tokenizer* t) {
    if (t->current >= 0 && IsAsciiAlpha((uint8_t)t->current)) {
        Consume(t);
        return StateNext(StateName::GfmAutolinkLiteralTrailCharRefInside);
    }
    if (t->current == ';') {
        Consume(t);
        return StateNext(StateName::GfmAutolinkLiteralTrail);
    }
    return StateNok();
}

// Where the `@` of an email may start, or -1 when there is nothing there.
static int32_t PeekBytesAtext(Str bytes, int32_t min, int32_t end) {
    int32_t index = end;
    while (index > min) {
        uint8_t byte = (uint8_t)bytes.s[index - 1];
        bool atext = byte == '+' || byte == '-' || byte == '.' || byte == '_' ||
                     IsAsciiAlphanumeric(byte);
        if (!atext) {
            break;
        }
        index -= 1;
    }
    if (index == end || (index > min && bytes.s[index - 1] == '/')) {
        return -1;
    }
    return index;
}

// The `mailto:` or `xmpp:` before an email, if there is one.
static int32_t PeekProtocol(Str bytes, int32_t min, int32_t end, Name* name) {
    *name = Name::GfmAutolinkLiteralEmail;
    int32_t index = end;
    if (index > min && bytes.s[index - 1] == ':') {
        index -= 1;
        while (index > min &&
               IsAsciiAlphanumeric((uint8_t)bytes.s[index - 1])) {
            index -= 1;
        }
        Slice slice = SliceFromIndices(bytes, index, end - 1);
        if (base::StrEqI(slice.bytes, "xmpp")) {
            *name = Name::GfmAutolinkLiteralXmpp;
            return index;
        }
        if (base::StrEqI(slice.bytes, "mailto")) {
            *name = Name::GfmAutolinkLiteralMailto;
            return index;
        }
    }
    return end;
}

// Where an email's domain ends, or -1 when it is not one.
static int32_t PeekBytesEmailDomain(Str bytes, int32_t start, bool xmpp) {
    int32_t index = start;
    bool dot = false;
    while (index < len(bytes)) {
        uint8_t byte = (uint8_t)bytes.s[index];
        if (byte == '-' || byte == '_' || IsAsciiAlphanumeric(byte) ||
            (byte == '/' && xmpp)) {
            // Fine.
        } else if (byte == '.' && index + 1 < len(bytes) &&
                   IsAsciiAlphanumeric((uint8_t)bytes.s[index + 1])) {
            dot = true;
        } else {
            break;
        }
        index += 1;
    }
    if (index > start && dot) {
        uint8_t last = (uint8_t)bytes.s[index - 1];
        if (last == '.' || IsAsciiAlpha(last)) {
            return index;
        }
    }
    return -1;
}

void GfmAutolinkLiteralResolve(Tokenizer* t) {
    EditMapConsume(t->map, t->events);
    Arena* a = t->parseState->scratch;

    int32_t index = 0;
    int32_t links = 0;
    while (index < t->events.len) {
        const Event& event = t->events[index];
        if (event.kind == Kind::Enter) {
            if (event.name == Name::Link) {
                links += 1;
            }
        } else {
            if (event.name == Name::Data && links == 0) {
                Position position = PositionFromExitEvent(t->events, index);
                Slice slice = SliceFromPosition(t->parseState->bytes, position);
                Str bytes = slice.bytes;
                int32_t byteIndex = 0;
                ArenaVec<Event> replace{};
                Point point = t->events[index - 1].point;
                int32_t startIndex = point.index;
                int32_t min = 0;

                while (byteIndex < len(bytes)) {
                    if (bytes.s[byteIndex] == '@') {
                        int32_t rangeStart = 0;
                        int32_t rangeEnd = 0;
                        Name rangeName = Name::GfmAutolinkLiteralEmail;
                        int32_t start = PeekBytesAtext(bytes, min, byteIndex);
                        if (start != -1) {
                            Name kind;
                            start = PeekProtocol(bytes, min, start, &kind);
                            int32_t end = PeekBytesEmailDomain(
                                bytes, byteIndex + 1,
                                kind == Name::GfmAutolinkLiteralXmpp);
                            if (end != -1) {
                                rangeStart = start;
                                rangeEnd = end;
                                rangeName = kind;
                            }
                        }

                        if (rangeEnd != 0) {
                            byteIndex = rangeEnd;
                            // The data before the email, if any.
                            if (min != rangeStart) {
                                Event enter;
                                enter.kind = Kind::Enter;
                                enter.name = Name::Data;
                                enter.point = point;
                                replace.Append(a, enter);
                                point =
                                    PointShiftTo(point, t->parseState->bytes,
                                                 startIndex + rangeStart);
                                Event exit;
                                exit.kind = Kind::Exit;
                                exit.name = Name::Data;
                                exit.point = point;
                                replace.Append(a, exit);
                            }
                            Event enter;
                            enter.kind = Kind::Enter;
                            enter.name = rangeName;
                            enter.point = point;
                            replace.Append(a, enter);
                            point = PointShiftTo(point, t->parseState->bytes,
                                                 startIndex + rangeEnd);
                            Event exit;
                            exit.kind = Kind::Exit;
                            exit.name = rangeName;
                            exit.point = point;
                            replace.Append(a, exit);
                            min = rangeEnd;
                        }
                    }
                    byteIndex += 1;
                }

                // The data after the last email, if any.
                if (min != 0 && min < len(bytes)) {
                    Event enter;
                    enter.kind = Kind::Enter;
                    enter.name = Name::Data;
                    enter.point = point;
                    replace.Append(a, enter);
                    Event exit;
                    exit.kind = Kind::Exit;
                    exit.name = Name::Data;
                    exit.point = t->events[index].point;
                    replace.Append(a, exit);
                }

                if (len(replace) > 0) {
                    EditMapAdd(t->map, index - 1, 2, replace.Flatten(a),
                               len(replace));
                }
            }
            if (event.name == Name::Link) {
                links -= 1;
            }
        }
        index += 1;
    }
}

} // namespace markdown
