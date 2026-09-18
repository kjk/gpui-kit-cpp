/* The constructs that are pieces of other constructs, plus the two smallest
   whole ones.

   | Rust                                    | here                    |
   | --------------------------------------- | ----------------------- |
   | `construct/partial_space_or_tab.rs`     | SpaceOrTab*             |
   | `construct/partial_space_or_tab_eol.rs` | SpaceOrTabEol*          |
   | `construct/partial_data.rs`             | Data*                   |
   | `construct/partial_destination.rs`      | Destination*            |
   | `construct/partial_label.rs`            | Label{Start,AtBreak,…}  |
   | `construct/partial_title.rs`            | Title*                  |
   | `construct/partial_whitespace.rs`       | ResolveWhitespace       |
   | `construct/partial_bom.rs`              | Bom*                    |
   | `construct/partial_non_lazy_continuation.rs` | NonLazyContinuation* |
   | `construct/blank_line.rs`               | BlankLine*              |

   Part of the C++ port of markdown-rs 1.0.0 (see src/markdown/readme.md). */

#include "markdown/construct.h"

namespace markdown {

// ─── partial_space_or_tab.rs ─────────────────────────────────────────────

StateName SpaceOrTabWithOptions(Tokenizer* t,
                                const SpaceOrTabOptions& options) {
    t->tokenizeState.spaceOrTabConnect = options.connect;
    t->tokenizeState.spaceOrTabContent = options.content;
    t->tokenizeState.spaceOrTabContentSome = options.contentSome;
    t->tokenizeState.spaceOrTabMin = options.min;
    t->tokenizeState.spaceOrTabMax = options.max;
    t->tokenizeState.spaceOrTabToken = options.kind;
    return StateName::SpaceOrTabStart;
}

StateName SpaceOrTabMinMax(Tokenizer* t, int32_t min, int32_t max) {
    SpaceOrTabOptions options;
    options.kind = Name::SpaceOrTab;
    options.min = min;
    options.max = max;
    return SpaceOrTabWithOptions(t, options);
}

StateName SpaceOrTab(Tokenizer* t) {
    return SpaceOrTabMinMax(t, 1, kSizeMax);
}

State SpaceOrTabStart(Tokenizer* t) {
    if (t->tokenizeState.spaceOrTabMax > 0 &&
        (t->current == '\t' || t->current == ' ')) {
        if (t->tokenizeState.spaceOrTabContentSome) {
            Link link;
            link.content = t->tokenizeState.spaceOrTabContent;
            EnterLink(t, t->tokenizeState.spaceOrTabToken, link);
        } else {
            Enter(t, t->tokenizeState.spaceOrTabToken);
        }
        if (t->tokenizeState.spaceOrTabConnect) {
            SubtokenizeLink(t->events, t->events.len - 1);
        } else {
            t->tokenizeState.spaceOrTabConnect = true;
        }
        return StateRetry(StateName::SpaceOrTabInside);
    }
    return StateRetry(StateName::SpaceOrTabAfter);
}

State SpaceOrTabInside(Tokenizer* t) {
    if ((t->current == '\t' || t->current == ' ') &&
        t->tokenizeState.spaceOrTabSize < t->tokenizeState.spaceOrTabMax) {
        Consume(t);
        t->tokenizeState.spaceOrTabSize += 1;
        return StateNext(StateName::SpaceOrTabInside);
    }
    Exit(t, t->tokenizeState.spaceOrTabToken);
    return StateRetry(StateName::SpaceOrTabAfter);
}

State SpaceOrTabAfter(Tokenizer* t) {
    State state = t->tokenizeState.spaceOrTabSize >= t->tokenizeState
                                                         .spaceOrTabMin
                      ? StateOk()
                      : StateNok();
    t->tokenizeState.spaceOrTabConnect = false;
    t->tokenizeState.spaceOrTabContentSome = false;
    t->tokenizeState.spaceOrTabSize = 0;
    t->tokenizeState.spaceOrTabMax = 0;
    t->tokenizeState.spaceOrTabMin = 0;
    t->tokenizeState.spaceOrTabToken = Name::SpaceOrTab;
    return state;
}

// ─── partial_space_or_tab_eol.rs ─────────────────────────────────────────

StateName SpaceOrTabEolWithOptions(Tokenizer* t,
                                   const SpaceOrTabEolOptions& options) {
    t->tokenizeState.spaceOrTabEolContent = options.content;
    t->tokenizeState.spaceOrTabEolContentSome = options.contentSome;
    t->tokenizeState.spaceOrTabEolConnect = options.connect;
    return StateName::SpaceOrTabEolStart;
}

StateName SpaceOrTabEol(Tokenizer* t) {
    SpaceOrTabEolOptions options;
    return SpaceOrTabEolWithOptions(t, options);
}

State SpaceOrTabEolStart(Tokenizer* t) {
    if (t->current == '\t' || t->current == ' ') {
        TokenizerAttempt(t, StateNext(StateName::SpaceOrTabEolAfterFirst),
                         StateNext(StateName::SpaceOrTabEolAtEol));
        SpaceOrTabOptions options;
        options.kind = Name::SpaceOrTab;
        options.min = 1;
        options.max = kSizeMax;
        options.content = t->tokenizeState.spaceOrTabEolContent;
        options.contentSome = t->tokenizeState.spaceOrTabEolContentSome;
        options.connect = t->tokenizeState.spaceOrTabEolConnect;
        return StateRetry(SpaceOrTabWithOptions(t, options));
    }
    return StateRetry(StateName::SpaceOrTabEolAtEol);
}

State SpaceOrTabEolAfterFirst(Tokenizer* t) {
    t->tokenizeState.spaceOrTabEolOk = true;
    return StateRetry(StateName::SpaceOrTabEolAtEol);
}

State SpaceOrTabEolAtEol(Tokenizer* t) {
    if (t->current == '\n') {
        if (t->tokenizeState.spaceOrTabEolContentSome) {
            Link link;
            link.content = t->tokenizeState.spaceOrTabEolContent;
            EnterLink(t, Name::LineEnding, link);
        } else {
            Enter(t, Name::LineEnding);
        }
        if (t->tokenizeState.spaceOrTabEolConnect) {
            SubtokenizeLink(t->events, t->events.len - 1);
        } else if (t->tokenizeState.spaceOrTabEolContentSome) {
            t->tokenizeState.spaceOrTabEolConnect = true;
        }
        Consume(t);
        Exit(t, Name::LineEnding);
        return StateNext(StateName::SpaceOrTabEolAfterEol);
    }
    bool ok = t->tokenizeState.spaceOrTabEolOk;
    t->tokenizeState.spaceOrTabEolContentSome = false;
    t->tokenizeState.spaceOrTabEolConnect = false;
    t->tokenizeState.spaceOrTabEolOk = false;
    return ok ? StateOk() : StateNok();
}

State SpaceOrTabEolAfterEol(Tokenizer* t) {
    if (t->current == '\t' || t->current == ' ') {
        TokenizerAttempt(t, StateNext(StateName::SpaceOrTabEolAfterMore),
                         StateNok());
        SpaceOrTabOptions options;
        options.kind = Name::SpaceOrTab;
        options.min = 1;
        options.max = kSizeMax;
        options.content = t->tokenizeState.spaceOrTabEolContent;
        options.contentSome = t->tokenizeState.spaceOrTabEolContentSome;
        options.connect = t->tokenizeState.spaceOrTabEolConnect;
        return StateRetry(SpaceOrTabWithOptions(t, options));
    }
    return StateRetry(StateName::SpaceOrTabEolAfterMore);
}

State SpaceOrTabEolAfterMore(Tokenizer* t) {
    t->tokenizeState.spaceOrTabEolContentSome = false;
    t->tokenizeState.spaceOrTabEolConnect = false;
    t->tokenizeState.spaceOrTabEolOk = false;
    return StateOk();
}

// ─── partial_data.rs ─────────────────────────────────────────────────────

static bool MarkersContain(Tokenizer* t, int32_t byte) {
    if (byte < 0) {
        return false;
    }
    for (int32_t i = 0; i < t->tokenizeState.markersLen; i++) {
        if (t->tokenizeState.markers[i] == (uint8_t)byte) {
            return true;
        }
    }
    return false;
}

State DataStart(Tokenizer* t) {
    if (t->current >= 0 && MarkersContain(t, t->current)) {
        Enter(t, Name::Data);
        Consume(t);
        return StateNext(StateName::DataInside);
    }
    return StateRetry(StateName::DataAtBreak);
}

State DataAtBreak(Tokenizer* t) {
    if (t->current >= 0 && !MarkersContain(t, t->current)) {
        if (t->current == '\n') {
            Enter(t, Name::LineEnding);
            Consume(t);
            Exit(t, Name::LineEnding);
            return StateNext(StateName::DataAtBreak);
        }
        Enter(t, Name::Data);
        return StateRetry(StateName::DataInside);
    }
    return StateOk();
}

State DataInside(Tokenizer* t) {
    if (t->current >= 0 && t->current != '\n' &&
        !MarkersContain(t, t->current)) {
        Consume(t);
        return StateNext(StateName::DataInside);
    }
    Exit(t, Name::Data);
    return StateRetry(StateName::DataAtBreak);
}

bool DataResolve(Tokenizer* t, Subresult*) {
    int32_t index = 0;
    while (index < t->events.len) {
        const Event& event = t->events[index];
        if (event.kind == Kind::Enter && event.name == Name::Data) {
            index += 1;
            int32_t exitIndex = index;
            while (exitIndex + 1 < t->events.len &&
                   t->events[exitIndex + 1].name == Name::Data) {
                exitIndex += 2;
            }
            if (exitIndex > index) {
                EditMapAdd(t->map, index, exitIndex - index, nullptr, 0);
                t->events[index].point = t->events[exitIndex].point;
                index = exitIndex;
            }
        }
        index += 1;
    }
    EditMapConsume(t->map, t->events);
    return false;
}

// ─── partial_destination.rs ──────────────────────────────────────────────

State DestinationStart(Tokenizer* t) {
    if (t->current == '<') {
        Enter(t, t->tokenizeState.token1);
        Enter(t, t->tokenizeState.token2);
        Enter(t, t->tokenizeState.token3);
        Consume(t);
        Exit(t, t->tokenizeState.token3);
        return StateNext(StateName::DestinationEnclosedBefore);
    }
    if (t->current < 0 || (t->current >= 0x01 && t->current <= 0x1f) ||
        t->current == ' ' || t->current == ')' || t->current == 0x7f) {
        return StateNok();
    }
    Enter(t, t->tokenizeState.token1);
    Enter(t, t->tokenizeState.token4);
    Enter(t, t->tokenizeState.token5);
    Link link;
    link.content = ContentKind::String;
    EnterLink(t, Name::Data, link);
    return StateRetry(StateName::DestinationRaw);
}

State DestinationEnclosedBefore(Tokenizer* t) {
    if (t->current == '>') {
        Enter(t, t->tokenizeState.token3);
        Consume(t);
        Exit(t, t->tokenizeState.token3);
        Exit(t, t->tokenizeState.token2);
        Exit(t, t->tokenizeState.token1);
        return StateOk();
    }
    Enter(t, t->tokenizeState.token5);
    Link link;
    link.content = ContentKind::String;
    EnterLink(t, Name::Data, link);
    return StateRetry(StateName::DestinationEnclosed);
}

State DestinationEnclosed(Tokenizer* t) {
    if (t->current < 0 || t->current == '\n' || t->current == '<') {
        return StateNok();
    }
    if (t->current == '>') {
        Exit(t, Name::Data);
        Exit(t, t->tokenizeState.token5);
        return StateRetry(StateName::DestinationEnclosedBefore);
    }
    if (t->current == '\\') {
        Consume(t);
        return StateNext(StateName::DestinationEnclosedEscape);
    }
    Consume(t);
    return StateNext(StateName::DestinationEnclosed);
}

State DestinationEnclosedEscape(Tokenizer* t) {
    if (t->current == '<' || t->current == '>' || t->current == '\\') {
        Consume(t);
        return StateNext(StateName::DestinationEnclosed);
    }
    return StateRetry(StateName::DestinationEnclosed);
}

State DestinationRaw(Tokenizer* t) {
    if (t->tokenizeState.size == 0 &&
        (t->current < 0 || t->current == '\t' || t->current == '\n' ||
         t->current == ' ' || t->current == ')')) {
        Exit(t, Name::Data);
        Exit(t, t->tokenizeState.token5);
        Exit(t, t->tokenizeState.token4);
        Exit(t, t->tokenizeState.token1);
        t->tokenizeState.size = 0;
        return StateOk();
    }
    if (t->tokenizeState.size < t->tokenizeState.sizeB && t->current == '(') {
        Consume(t);
        t->tokenizeState.size += 1;
        return StateNext(StateName::DestinationRaw);
    }
    if (t->current == ')') {
        Consume(t);
        t->tokenizeState.size -= 1;
        return StateNext(StateName::DestinationRaw);
    }
    // ASCII control, space or an unbalanced `(`.
    if (t->current < 0 || (t->current >= 0x01 && t->current <= 0x1f) ||
        t->current == ' ' || t->current == '(' || t->current == 0x7f) {
        t->tokenizeState.size = 0;
        return StateNok();
    }
    if (t->current == '\\') {
        Consume(t);
        return StateNext(StateName::DestinationRawEscape);
    }
    Consume(t);
    return StateNext(StateName::DestinationRaw);
}

State DestinationRawEscape(Tokenizer* t) {
    if (t->current == '(' || t->current == ')' || t->current == '\\') {
        Consume(t);
        return StateNext(StateName::DestinationRaw);
    }
    return StateRetry(StateName::DestinationRaw);
}

// ─── partial_label.rs ────────────────────────────────────────────────────

State LabelStart(Tokenizer* t) {
    Enter(t, t->tokenizeState.token1);
    Enter(t, t->tokenizeState.token2);
    Consume(t);
    Exit(t, t->tokenizeState.token2);
    Enter(t, t->tokenizeState.token3);
    return StateNext(StateName::LabelAtBreak);
}

State LabelAtBreak(Tokenizer* t) {
    if (t->tokenizeState.size > kLinkReferenceSizeMax || t->current < 0 ||
        t->current == '[' || (t->current == ']' && !t->tokenizeState.seen)) {
        return StateRetry(StateName::LabelNok);
    }
    if (t->current == '\n') {
        TokenizerAttempt(t, StateNext(StateName::LabelEolAfter),
                         StateNext(StateName::LabelNok));
        SpaceOrTabEolOptions options;
        options.content = ContentKind::String;
        options.contentSome = true;
        options.connect = t->tokenizeState.connect;
        return StateRetry(SpaceOrTabEolWithOptions(t, options));
    }
    if (t->current == ']') {
        Exit(t, t->tokenizeState.token3);
        Enter(t, t->tokenizeState.token2);
        Consume(t);
        Exit(t, t->tokenizeState.token2);
        Exit(t, t->tokenizeState.token1);
        t->tokenizeState.connect = false;
        t->tokenizeState.seen = false;
        t->tokenizeState.size = 0;
        return StateOk();
    }
    Link link;
    link.content = ContentKind::String;
    EnterLink(t, Name::Data, link);
    if (t->tokenizeState.connect) {
        SubtokenizeLink(t->events, t->events.len - 1);
    } else {
        t->tokenizeState.connect = true;
    }
    return StateRetry(StateName::LabelInside);
}

State LabelEolAfter(Tokenizer* t) {
    t->tokenizeState.connect = true;
    return StateRetry(StateName::LabelAtBreak);
}

State LabelNok(Tokenizer* t) {
    t->tokenizeState.connect = false;
    t->tokenizeState.seen = false;
    t->tokenizeState.size = 0;
    return StateNok();
}

State LabelInside(Tokenizer* t) {
    if (t->current < 0 || t->current == '\n' || t->current == '[' ||
        t->current == ']') {
        Exit(t, Name::Data);
        return StateRetry(StateName::LabelAtBreak);
    }
    if (t->tokenizeState.size > kLinkReferenceSizeMax) {
        Exit(t, Name::Data);
        return StateRetry(StateName::LabelAtBreak);
    }
    int32_t byte = t->current;
    Consume(t);
    t->tokenizeState.size += 1;
    if (!t->tokenizeState.seen && byte != '\t' && byte != ' ') {
        t->tokenizeState.seen = true;
    }
    return StateNext(byte == '\\' ? StateName::LabelEscape
                                  : StateName::LabelInside);
}

State LabelEscape(Tokenizer* t) {
    if (t->current == '[' || t->current == '\\' || t->current == ']') {
        Consume(t);
        t->tokenizeState.size += 1;
        return StateNext(StateName::LabelInside);
    }
    return StateRetry(StateName::LabelInside);
}

// ─── partial_title.rs ────────────────────────────────────────────────────

State TitleStart(Tokenizer* t) {
    if (t->current == '"' || t->current == '\'' || t->current == '(') {
        uint8_t marker = (uint8_t)t->current;
        t->tokenizeState.marker = marker == '(' ? (uint8_t)')' : marker;
        Enter(t, t->tokenizeState.token1);
        Enter(t, t->tokenizeState.token2);
        Consume(t);
        Exit(t, t->tokenizeState.token2);
        return StateNext(StateName::TitleBegin);
    }
    return StateNok();
}

State TitleBegin(Tokenizer* t) {
    if (t->current == (int32_t)t->tokenizeState.marker) {
        Enter(t, t->tokenizeState.token2);
        Consume(t);
        Exit(t, t->tokenizeState.token2);
        Exit(t, t->tokenizeState.token1);
        t->tokenizeState.marker = 0;
        t->tokenizeState.connect = false;
        return StateOk();
    }
    Enter(t, t->tokenizeState.token3);
    return StateRetry(StateName::TitleAtBreak);
}

State TitleAtBreak(Tokenizer* t) {
    if (t->current < 0) {
        return StateRetry(StateName::TitleNok);
    }
    if (t->current == (int32_t)t->tokenizeState.marker) {
        Exit(t, t->tokenizeState.token3);
        return StateRetry(StateName::TitleBegin);
    }
    if (t->current == '\n') {
        TokenizerAttempt(t, StateNext(StateName::TitleAfterEol),
                         StateNext(StateName::TitleNok));
        SpaceOrTabEolOptions options;
        options.content = ContentKind::String;
        options.contentSome = true;
        options.connect = t->tokenizeState.connect;
        return StateRetry(SpaceOrTabEolWithOptions(t, options));
    }
    Link link;
    link.content = ContentKind::String;
    EnterLink(t, Name::Data, link);
    if (t->tokenizeState.connect) {
        SubtokenizeLink(t->events, t->events.len - 1);
    } else {
        t->tokenizeState.connect = true;
    }
    return StateRetry(StateName::TitleInside);
}

State TitleAfterEol(Tokenizer* t) {
    t->tokenizeState.connect = true;
    return StateRetry(StateName::TitleAtBreak);
}

State TitleNok(Tokenizer* t) {
    t->tokenizeState.marker = 0;
    t->tokenizeState.connect = false;
    return StateNok();
}

State TitleInside(Tokenizer* t) {
    if (t->current == (int32_t)t->tokenizeState.marker || t->current < 0 ||
        t->current == '\n') {
        Exit(t, Name::Data);
        return StateRetry(StateName::TitleAtBreak);
    }
    StateName name =
        t->current == '\\' ? StateName::TitleEscape : StateName::TitleInside;
    Consume(t);
    return StateNext(name);
}

State TitleEscape(Tokenizer* t) {
    if (t->current == '"' || t->current == '\'' || t->current == ')' ||
        t->current == '\\') {
        Consume(t);
        return StateNext(StateName::TitleInside);
    }
    return StateRetry(StateName::TitleInside);
}

// ─── partial_whitespace.rs ───────────────────────────────────────────────

static void TrimData(Tokenizer* t, int32_t exitIndex, bool trimStart,
                     bool trimEnd, bool hardBreak) {
    Position position = PositionFromExitEvent(t->events, exitIndex);
    Slice slice = SliceFromPosition(t->parseState->bytes, position);

    if (trimEnd) {
        int32_t index = slice.bytes.len;
        bool spacesOnly = slice.after == 0;
        while (index > 0) {
            char byte = slice.bytes.s[index - 1];
            if (byte == ' ') {
                // Still only spaces.
            } else if (byte == '\t') {
                spacesOnly = false;
            } else {
                break;
            }
            index -= 1;
        }
        int32_t diff = slice.bytes.len - index;
        Name name =
            (hardBreak && spacesOnly && diff >= kHardBreakPrefixSizeMin &&
             exitIndex + 1 < t->events.len)
                ? Name::HardBreakTrailing
                : Name::SpaceOrTab;
        if (index == 0) {
            t->events[exitIndex - 1].name = name;
            t->events[exitIndex].name = name;
            return;
        }
        if (diff > 0 || slice.after > 0) {
            Point exitPoint = t->events[exitIndex].point;
            Point enterPoint = exitPoint;
            enterPoint.index -= diff;
            enterPoint.column -= diff;
            enterPoint.vs = 0;
            Event add[2];
            add[0].kind = Kind::Enter;
            add[0].name = name;
            add[0].point = enterPoint;
            add[1].kind = Kind::Exit;
            add[1].name = name;
            add[1].point = exitPoint;
            EditMapAdd(t->map, exitIndex + 1, 0, add, 2);
            t->events[exitIndex].point = enterPoint;
            slice.bytes.len = index;
        }
    }

    if (trimStart) {
        int32_t index = 0;
        while (index < slice.bytes.len) {
            char byte = slice.bytes.s[index];
            if (byte == ' ' || byte == '\t') {
                index += 1;
            } else {
                break;
            }
        }
        if (index == slice.bytes.len) {
            t->events[exitIndex - 1].name = Name::SpaceOrTab;
            t->events[exitIndex].name = Name::SpaceOrTab;
            return;
        }
        if (index > 0 || slice.before > 0) {
            Point enterPoint = t->events[exitIndex - 1].point;
            Point exitPoint = enterPoint;
            exitPoint.index += index;
            exitPoint.column += index;
            exitPoint.vs = 0;
            Event add[2];
            add[0].kind = Kind::Enter;
            add[0].name = Name::SpaceOrTab;
            add[0].point = enterPoint;
            add[1].kind = Kind::Exit;
            add[1].name = Name::SpaceOrTab;
            add[1].point = exitPoint;
            EditMapAdd(t->map, exitIndex - 1, 0, add, 2);
            t->events[exitIndex - 1].point = exitPoint;
        }
    }
}

void ResolveWhitespace(Tokenizer* t, bool hardBreak, bool trimWhole) {
    int32_t index = 0;
    while (index < t->events.len) {
        const Event& event = t->events[index];
        if (event.kind == Kind::Exit && event.name == Name::Data) {
            bool trimStart =
                (trimWhole && index == 1) ||
                (index > 1 && t->events[index - 2].name == Name::LineEnding);
            bool trimEnd = (trimWhole && index == t->events.len - 1) ||
                           (index + 1 < t->events.len &&
                            t->events[index + 1].name == Name::LineEnding);
            TrimData(t, index, trimStart, trimEnd, hardBreak);
        }
        index += 1;
    }
    EditMapConsume(t->map, t->events);
}

// ─── partial_bom.rs ──────────────────────────────────────────────────────

static const uint8_t kBom[3] = {0xef, 0xbb, 0xbf};

State BomStart(Tokenizer* t) {
    if (t->current == (int32_t)kBom[0]) {
        Enter(t, Name::ByteOrderMark);
        return StateRetry(StateName::BomInside);
    }
    return StateNok();
}

State BomInside(Tokenizer* t) {
    if (t->current == (int32_t)kBom[t->tokenizeState.size]) {
        t->tokenizeState.size += 1;
        Consume(t);
        if (t->tokenizeState.size == 3) {
            Exit(t, Name::ByteOrderMark);
            t->tokenizeState.size = 0;
            return StateOk();
        }
        return StateNext(StateName::BomInside);
    }
    t->tokenizeState.size = 0;
    return StateNok();
}

// ─── partial_non_lazy_continuation.rs ────────────────────────────────────

State NonLazyContinuationStart(Tokenizer* t) {
    if (t->current == '\n') {
        Enter(t, Name::LineEnding);
        Consume(t);
        Exit(t, Name::LineEnding);
        return StateNext(StateName::NonLazyContinuationAfter);
    }
    return StateNok();
}

State NonLazyContinuationAfter(Tokenizer* t) {
    return t->lazy ? StateNok() : StateOk();
}

// ─── blank_line.rs ───────────────────────────────────────────────────────

State BlankLineStart(Tokenizer* t) {
    if (t->current == '\t' || t->current == ' ') {
        TokenizerAttempt(t, StateNext(StateName::BlankLineAfter), StateNok());
        return StateRetry(SpaceOrTab(t));
    }
    return StateRetry(StateName::BlankLineAfter);
}

State BlankLineAfter(Tokenizer* t) {
    if (t->current < 0 || t->current == '\n') {
        return StateOk();
    }
    return StateNok();
}

} // namespace markdown
