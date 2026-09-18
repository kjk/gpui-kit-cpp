/* src/tokenizer.rs — the machine that turns bytes into events.

   Part of the C++ port of markdown-rs 1.0.0 (see src/markdown/readme.md). */

#include "markdown/tokenizer.h"

namespace markdown {

// tokenizer.rs ByteAction.
enum class ByteActionKind : uint8_t {
    Normal,
    Ignore,
    Insert,
};

struct ByteAction {
    ByteActionKind kind = ByteActionKind::Normal;
    uint8_t byte = 0;
};

static ByteAction ByteActionAt(Str bytes, const Point& point) {
    uint8_t byte = (uint8_t)bytes.s[point.index];
    if (byte == '\r') {
        // A CR before an LF is not there; a CR alone stands in for one.
        if (point.index < bytes.len - 1 && bytes.s[point.index + 1] == '\n') {
            return ByteAction{ByteActionKind::Ignore, 0};
        }
        return ByteAction{ByteActionKind::Normal, '\n'};
    }
    if (byte == '\t') {
        int32_t remainder = point.column % kTabSize;
        int32_t vs = remainder == 0 ? 0 : kTabSize - remainder;
        if (point.vs == 0) {
            if (vs == 0) {
                return ByteAction{ByteActionKind::Normal, byte};
            }
            return ByteAction{ByteActionKind::Insert, byte};
        }
        if (vs == 0) {
            return ByteAction{ByteActionKind::Normal, ' '};
        }
        return ByteAction{ByteActionKind::Insert, ' '};
    }
    return ByteAction{ByteActionKind::Normal, byte};
}

Point PointShiftTo(const Point& from, Str bytes, int32_t index) {
    Point next = from;
    while (next.index < index) {
        if (bytes.s[next.index] == '\t') {
            int32_t remainder = next.column % kTabSize;
            int32_t vs = remainder == 0 ? 0 : kTabSize - remainder;
            next.index += 1;
            next.column += 1 + vs;
        } else {
            next.index += 1;
            next.column += 1;
        }
    }
    return next;
}

bool IsVoidEvent(Name name) {
    switch (name) {
        case Name::AttentionSequence:
        case Name::AutolinkEmail:
        case Name::AutolinkMarker:
        case Name::AutolinkProtocol:
        case Name::BlankLineEnding:
        case Name::BlockQuoteMarker:
        case Name::ByteOrderMark:
        case Name::CharacterEscapeMarker:
        case Name::CharacterEscapeValue:
        case Name::CharacterReferenceMarker:
        case Name::CharacterReferenceMarkerHexadecimal:
        case Name::CharacterReferenceMarkerNumeric:
        case Name::CharacterReferenceMarkerSemi:
        case Name::CharacterReferenceValue:
        case Name::CodeFencedFenceSequence:
        case Name::CodeFlowChunk:
        case Name::CodeTextData:
        case Name::CodeTextSequence:
        case Name::Data:
        case Name::DefinitionDestinationLiteralMarker:
        case Name::DefinitionLabelMarker:
        case Name::DefinitionMarker:
        case Name::DefinitionTitleMarker:
        case Name::EmphasisSequence:
        case Name::FrontmatterChunk:
        case Name::GfmAutolinkLiteralEmail:
        case Name::GfmAutolinkLiteralProtocol:
        case Name::GfmAutolinkLiteralWww:
        case Name::GfmFootnoteCallMarker:
        case Name::GfmFootnoteDefinitionLabelMarker:
        case Name::GfmFootnoteDefinitionMarker:
        case Name::GfmStrikethroughSequence:
        case Name::GfmTableCellDivider:
        case Name::GfmTableDelimiterMarker:
        case Name::GfmTableDelimiterFiller:
        case Name::GfmTaskListItemMarker:
        case Name::GfmTaskListItemValueChecked:
        case Name::GfmTaskListItemValueUnchecked:
        case Name::FrontmatterSequence:
        case Name::HardBreakEscape:
        case Name::HardBreakTrailing:
        case Name::HeadingAtxSequence:
        case Name::HeadingSetextUnderlineSequence:
        case Name::HtmlFlowData:
        case Name::HtmlTextData:
        case Name::LabelImageMarker:
        case Name::LabelMarker:
        case Name::LineEnding:
        case Name::ListItemMarker:
        case Name::ListItemValue:
        case Name::MathFlowFenceSequence:
        case Name::MathFlowChunk:
        case Name::MathTextData:
        case Name::MathTextSequence:
        case Name::ReferenceMarker:
        case Name::ResourceMarker:
        case Name::ResourceTitleMarker:
        case Name::SpaceOrTab:
        case Name::StrongSequence:
        case Name::ThematicBreakSequence:
            return true;
        default:
            return false;
    }
}

void SubresultAppend(Subresult& dst, Subresult& src) {
    for (int32_t i = 0; i < src.gfmFootnoteDefinitions.len; i++) {
        VecAppend(dst.gfmFootnoteDefinitions, src.gfmFootnoteDefinitions[i]);
    }
    for (int32_t i = 0; i < src.definitions.len; i++) {
        VecAppend(dst.definitions, src.definitions[i]);
    }
    src.gfmFootnoteDefinitions.len = 0;
    src.definitions.len = 0;
}

Tokenizer* TokenizerNew(Point point, ParseState* parseState) {
    Tokenizer* t = new Tokenizer();
    t->firstLine = point.line;
    t->lineStart = point;
    t->point = point;
    t->parseState = parseState;
    t->map.a = parseState->scratch;
    return t;
}

void TokenizerFree(Tokenizer* t) {
    if (!t) {
        return;
    }
    TokenizerFree(t->tokenizeState.documentChild);
    delete t;
}

void RegisterResolver(Tokenizer* t, ResolveName name) {
    for (int32_t i = 0; i < t->resolvers.len; i++) {
        if (t->resolvers[i] == name) {
            return;
        }
    }
    VecAppend(t->resolvers, name);
}

void RegisterResolverBefore(Tokenizer* t, ResolveName name) {
    for (int32_t i = 0; i < t->resolvers.len; i++) {
        if (t->resolvers[i] == name) {
            return;
        }
    }
    VecInsertAt(t->resolvers, 0, name);
}

static void MoveOne(Tokenizer* t);

static void MoveTo(Tokenizer* t, int32_t toIndex, int32_t toVs) {
    while (t->point.index < toIndex ||
           (t->point.index == toIndex && t->point.vs < toVs)) {
        MoveOne(t);
    }
}

static void AccountForPotentialSkip(Tokenizer* t) {
    int32_t at = t->point.line - t->firstLine;
    if (t->point.column == 1 && at != t->columnStart.len) {
        MoveTo(t, t->columnStart[at].index, t->columnStart[at].vs);
    }
}

static void MovePointBack(Tokenizer* t, Point* point) {
    while (point->index > 0) {
        point->index -= 1;
        ByteAction action = ByteActionAt(t->parseState->bytes, *point);
        if (action.kind != ByteActionKind::Ignore) {
            point->index += 1;
            break;
        }
    }
}

void DefineSkip(Tokenizer* t, Point point) {
    MovePointBack(t, &point);
    IndexVs info = {point.index, point.vs};
    int32_t at = point.line - t->firstLine;
    if (at >= t->columnStart.len) {
        VecAppend(t->columnStart, info);
    } else {
        t->columnStart[at] = info;
    }
    AccountForPotentialSkip(t);
}

static void MoveOne(Tokenizer* t) {
    ByteAction action = ByteActionAt(t->parseState->bytes, t->point);
    if (action.kind == ByteActionKind::Ignore) {
        t->point.index += 1;
        return;
    }
    if (action.kind == ByteActionKind::Insert) {
        t->previous = action.byte;
        t->point.column += 1;
        t->point.vs += 1;
        return;
    }
    t->previous = action.byte;
    t->point.vs = 0;
    t->point.index += 1;
    if (action.byte == '\n') {
        t->point.line += 1;
        t->point.column = 1;
        if (t->point.line - t->firstLine + 1 > t->columnStart.len) {
            IndexVs info = {t->point.index, t->point.vs};
            VecAppend(t->columnStart, info);
        }
        t->lineStart = t->point;
        AccountForPotentialSkip(t);
    } else {
        t->point.column += 1;
    }
}

static void Expect(Tokenizer* t, int32_t byte) {
    t->consumed = false;
    t->current = byte;
}

void Consume(Tokenizer* t) {
    MoveOne(t);
    t->previous = t->current;
    t->current = -1;
    t->consumed = true;
}

static void EnterImpl(Tokenizer* t, Name name, bool hasLink, Link link) {
    Point point = t->point;
    MovePointBack(t, &point);
    VecAppend(t->stack, name);
    Event event;
    event.kind = Kind::Enter;
    event.name = name;
    event.point = point;
    event.hasLink = hasLink;
    event.link = link;
    VecAppend(t->events, event);
}

void Enter(Tokenizer* t, Name name) {
    EnterImpl(t, name, false, Link{});
}

void EnterLink(Tokenizer* t, Name name, Link link) {
    EnterImpl(t, name, true, link);
}

void Exit(Tokenizer* t, Name name) {
    t->stack.len -= 1;
    Point point = t->point;
    if (t->previous == '\n') {
        point = t->lineStart;
    } else {
        MovePointBack(t, &point);
    }
    Event event;
    event.kind = Kind::Exit;
    event.name = name;
    event.point = point;
    VecAppend(t->events, event);
}

static Progress Capture(Tokenizer* t) {
    Progress p;
    p.previous = t->previous;
    p.current = t->current;
    p.point = t->point;
    p.eventsLen = t->events.len;
    p.stackLen = t->stack.len;
    return p;
}

static void FreeProgress(Tokenizer* t, const Progress& previous) {
    t->previous = previous.previous;
    t->current = previous.current;
    t->point = previous.point;
    t->events.len = previous.eventsLen;
    t->stack.len = previous.stackLen;
}

void TokenizerCheck(Tokenizer* t, State ok, State nok) {
    Attempt attempt;
    attempt.check = true;
    attempt.hasProgress = true;
    attempt.progress = Capture(t);
    attempt.ok = ok;
    attempt.nok = nok;
    VecAppend(t->attempts, attempt);
}

void TokenizerAttempt(Tokenizer* t, State ok, State nok) {
    Attempt attempt;
    attempt.check = false;
    attempt.hasProgress = nok != StateNok();
    if (attempt.hasProgress) {
        attempt.progress = Capture(t);
    }
    attempt.ok = ok;
    attempt.nok = nok;
    VecAppend(t->attempts, attempt);
}

static State PushImpl(Tokenizer* t, int32_t fromIndex, int32_t fromVs,
                      int32_t toIndex, int32_t toVs, State state, bool flush) {
    MoveTo(t, fromIndex, fromVs);
    for (;;) {
        if (state.kind == State::Kind::Ok || state.kind == State::Kind::Nok) {
            if (t->attempts.len == 0) {
                break;
            }
            Attempt attempt = t->attempts[--t->attempts.len];
            if ((attempt.check || state.kind == State::Kind::Nok) &&
                attempt.hasProgress) {
                FreeProgress(t, attempt.progress);
            }
            t->consumed = true;
            state = state.kind == State::Kind::Ok ? attempt.ok : attempt.nok;
            continue;
        }
        if (state.kind == State::Kind::Next) {
            bool haveAction = false;
            ByteAction action = {};
            if (t->point.index < toIndex ||
                (t->point.index == toIndex && t->point.vs < toVs)) {
                action = ByteActionAt(t->parseState->bytes, t->point);
                haveAction = true;
            } else if (!flush) {
                break;
            }
            if (haveAction && action.kind == ByteActionKind::Ignore) {
                MoveOne(t);
                continue;
            }
            int32_t byte = haveAction ? (int32_t)action.byte : -1;
            StateName name = state.name;
            Expect(t, byte);
            state = Call(t, name);
            continue;
        }
        // Retry: the byte stays where it is.
        state = Call(t, state.name);
    }
    t->consumed = true;
    return state;
}

State Push(Tokenizer* t, int32_t fromIndex, int32_t fromVs, int32_t toIndex,
           int32_t toVs, State state) {
    return PushImpl(t, fromIndex, fromVs, toIndex, toVs, state, false);
}

Subresult Flush(Tokenizer* t, State state, bool resolve) {
    int32_t toIndex = t->point.index;
    int32_t toVs = t->point.vs;
    PushImpl(t, toIndex, toVs, toIndex, toVs, state, true);

    Subresult value;
    value.done = false;
    for (int32_t i = 0; i < t->tokenizeState.gfmFootnoteDefinitions.len; i++) {
        VecAppend(value.gfmFootnoteDefinitions, t->tokenizeState
                                                    .gfmFootnoteDefinitions[i]);
    }
    for (int32_t i = 0; i < t->tokenizeState.definitions.len; i++) {
        VecAppend(value.definitions, t->tokenizeState.definitions[i]);
    }
    t->tokenizeState.gfmFootnoteDefinitions.len = 0;
    t->tokenizeState.definitions.len = 0;

    if (resolve) {
        Vec<ResolveName> resolvers;
        for (int32_t i = 0; i < len(t->resolvers); i++) {
            VecAppend(resolvers, t->resolvers[i]);
        }
        t->resolvers.len = 0;
        for (int32_t index = 0; index < len(resolvers); index++) {
            Subresult result;
            if (ResolveCall(t, resolvers[index], &result)) {
                SubresultAppend(value, result);
            }
        }
        EditMapConsume(t->map, t->events);
    }
    return value;
}

} // namespace markdown
