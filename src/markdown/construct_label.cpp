/* The end of a label — `]` — which is where a link, an image or a footnote
   call is decided, and where they are injected into the events.

   | Rust                       | here       |
   | -------------------------- | ---------- |
   | `construct/label_end.rs`   | LabelEnd*  |

   Part of the C++ port of markdown-rs 1.0.0 (see src/markdown/readme.md). */

#include "markdown/construct.h"

namespace markdown {

// `Vec<String>::contains`.
static bool DefinitionsContain(const Vec<Str>& definitions, Str id) {
    for (int32_t i = 0; i < len(definitions); i++) {
        if (base::StrEq(definitions[i], id)) {
            return true;
        }
    }
    return false;
}

State LabelEndStart(Tokenizer* t) {
    if (t->current == ']' && t->parseState->options->constructs.labelEnd &&
        t->tokenizeState.labelStarts.len > 0) {
        const LabelStartMark& labelStart =
            t->tokenizeState.labelStarts[t->tokenizeState.labelStarts.len - 1];
        t->tokenizeState.end = t->events.len;
        // Mark as balanced if the info is inactive.
        if (labelStart.inactive) {
            return StateRetry(StateName::LabelEndNok);
        }
        Enter(t, Name::LabelEnd);
        Enter(t, Name::LabelMarker);
        Consume(t);
        Exit(t, Name::LabelMarker);
        Exit(t, Name::LabelEnd);
        return StateNext(StateName::LabelEndAfter);
    }
    return StateNok();
}

State LabelEndAfter(Tokenizer* t) {
    int32_t startIndex = t->tokenizeState.labelStarts.len - 1;
    const LabelStartMark& start = t->tokenizeState.labelStarts[startIndex];
    int32_t from = t->events[start.startB].point.index;
    int32_t to = t->events[t->tokenizeState.end].point.index;
    Arena* a = t->parseState->scratch;
    Slice slice = SliceFromIndices(t->parseState->bytes, from, to);
    Str id = NormalizeIdentifier(a, slice.bytes);

    if (start.kind == LabelKind::GfmFootnote) {
        if (DefinitionsContain(t->parseState->gfmFootnoteDefinitions, id)) {
            return StateRetry(StateName::LabelEndOk);
        }
        // The footnote call is not defined: it is a link with a `^` in it.
        t->tokenizeState.labelStarts[startIndex]
            .kind = LabelKind::GfmUndefinedFootnote;
        char* caret = (char*)base::Alloc(a, id.len + 2);
        caret[0] = '^';
        if (id.len > 0) {
            memcpy(caret + 1, id.s, (size_t)id.len);
        }
        caret[id.len + 1] = 0;
        id = Str(caret, id.len + 1);
    }

    bool defined = DefinitionsContain(t->parseState->definitions, id);

    if (t->current == '(') {
        TokenizerAttempt(t, StateNext(StateName::LabelEndOk),
                         StateNext(defined ? StateName::LabelEndOk
                                           : StateName::LabelEndNok));
        return StateRetry(StateName::LabelEndResourceStart);
    }
    if (t->current == '[') {
        TokenizerAttempt(t, StateNext(StateName::LabelEndOk),
                         StateNext(defined ? StateName::LabelEndReferenceNotFull
                                           : StateName::LabelEndNok));
        return StateRetry(StateName::LabelEndReferenceFull);
    }
    return StateRetry(defined ? StateName::LabelEndOk : StateName::LabelEndNok);
}

State LabelEndReferenceNotFull(Tokenizer* t) {
    TokenizerAttempt(t, StateNext(StateName::LabelEndOk),
                     StateNext(StateName::LabelEndNok));
    return StateRetry(StateName::LabelEndReferenceCollapsed);
}

State LabelEndOk(Tokenizer* t) {
    // Remove the start.
    LabelStartMark labelStart =
        t->tokenizeState.labelStarts[--t->tokenizeState.labelStarts.len];

    // Deactivate the other label starts: a link may not contain a link.
    if (labelStart.kind != LabelKind::Image) {
        for (int32_t index = 0; index < t->tokenizeState.labelStarts.len;
             index++) {
            if (t->tokenizeState.labelStarts[index].kind != LabelKind::Image) {
                t->tokenizeState.labelStarts[index].inactive = true;
            }
        }
    }

    Label label;
    label.kind = labelStart.kind;
    label.startA = labelStart.startA;
    label.startB = labelStart.startB;
    label.endA = t->tokenizeState.end;
    label.endB = t->events.len - 1;
    VecAppend(t->tokenizeState.labels, label);
    t->tokenizeState.end = 0;
    RegisterResolverBefore(t, ResolveName::Label);
    return StateOk();
}

State LabelEndNok(Tokenizer* t) {
    LabelStartMark start = t->tokenizeState
                               .labelStarts[--t->tokenizeState.labelStarts.len];
    VecAppend(t->tokenizeState.labelStartsLoose, start);
    t->tokenizeState.end = 0;
    return StateNok();
}

State LabelEndResourceStart(Tokenizer* t) {
    Enter(t, Name::Resource);
    Enter(t, Name::ResourceMarker);
    Consume(t);
    Exit(t, Name::ResourceMarker);
    return StateNext(StateName::LabelEndResourceBefore);
}

State LabelEndResourceBefore(Tokenizer* t) {
    if (t->current == '\t' || t->current == '\n' || t->current == ' ') {
        TokenizerAttempt(t, StateNext(StateName::LabelEndResourceOpen),
                         StateNext(StateName::LabelEndResourceOpen));
        return StateRetry(SpaceOrTabEol(t));
    }
    return StateRetry(StateName::LabelEndResourceOpen);
}

State LabelEndResourceOpen(Tokenizer* t) {
    if (t->current == ')') {
        return StateRetry(StateName::LabelEndResourceEnd);
    }
    t->tokenizeState.token1 = Name::ResourceDestination;
    t->tokenizeState.token2 = Name::ResourceDestinationLiteral;
    t->tokenizeState.token3 = Name::ResourceDestinationLiteralMarker;
    t->tokenizeState.token4 = Name::ResourceDestinationRaw;
    t->tokenizeState.token5 = Name::ResourceDestinationString;
    t->tokenizeState.sizeB = kResourceDestinationBalanceMax;
    TokenizerAttempt(t, StateNext(StateName::LabelEndResourceDestinationAfter),
                     StateNext(StateName::LabelEndResourceDestinationMissing));
    return StateRetry(StateName::DestinationStart);
}

State LabelEndResourceDestinationAfter(Tokenizer* t) {
    t->tokenizeState.token1 = Name::Data;
    t->tokenizeState.token2 = Name::Data;
    t->tokenizeState.token3 = Name::Data;
    t->tokenizeState.token4 = Name::Data;
    t->tokenizeState.token5 = Name::Data;
    t->tokenizeState.sizeB = 0;
    if (t->current == '\t' || t->current == '\n' || t->current == ' ') {
        TokenizerAttempt(t, StateNext(StateName::LabelEndResourceBetween),
                         StateNext(StateName::LabelEndResourceEnd));
        return StateRetry(SpaceOrTabEol(t));
    }
    return StateRetry(StateName::LabelEndResourceEnd);
}

State LabelEndResourceDestinationMissing(Tokenizer* t) {
    t->tokenizeState.token1 = Name::Data;
    t->tokenizeState.token2 = Name::Data;
    t->tokenizeState.token3 = Name::Data;
    t->tokenizeState.token4 = Name::Data;
    t->tokenizeState.token5 = Name::Data;
    t->tokenizeState.sizeB = 0;
    return StateNok();
}

State LabelEndResourceBetween(Tokenizer* t) {
    if (t->current == '"' || t->current == '\'' || t->current == '(') {
        t->tokenizeState.token1 = Name::ResourceTitle;
        t->tokenizeState.token2 = Name::ResourceTitleMarker;
        t->tokenizeState.token3 = Name::ResourceTitleString;
        TokenizerAttempt(t, StateNext(StateName::LabelEndResourceTitleAfter),
                         StateNok());
        return StateRetry(StateName::TitleStart);
    }
    return StateRetry(StateName::LabelEndResourceEnd);
}

State LabelEndResourceTitleAfter(Tokenizer* t) {
    t->tokenizeState.token1 = Name::Data;
    t->tokenizeState.token2 = Name::Data;
    t->tokenizeState.token3 = Name::Data;
    if (t->current == '\t' || t->current == '\n' || t->current == ' ') {
        TokenizerAttempt(t, StateNext(StateName::LabelEndResourceEnd),
                         StateNext(StateName::LabelEndResourceEnd));
        return StateRetry(SpaceOrTabEol(t));
    }
    return StateRetry(StateName::LabelEndResourceEnd);
}

State LabelEndResourceEnd(Tokenizer* t) {
    if (t->current == ')') {
        Enter(t, Name::ResourceMarker);
        Consume(t);
        Exit(t, Name::ResourceMarker);
        Exit(t, Name::Resource);
        return StateOk();
    }
    return StateNok();
}

State LabelEndReferenceFull(Tokenizer* t) {
    t->tokenizeState.token1 = Name::Reference;
    t->tokenizeState.token2 = Name::ReferenceMarker;
    t->tokenizeState.token3 = Name::ReferenceString;
    TokenizerAttempt(t, StateNext(StateName::LabelEndReferenceFullAfter),
                     StateNext(StateName::LabelEndReferenceFullMissing));
    return StateRetry(StateName::LabelStart);
}

State LabelEndReferenceFullAfter(Tokenizer* t) {
    t->tokenizeState.token1 = Name::Data;
    t->tokenizeState.token2 = Name::Data;
    t->tokenizeState.token3 = Name::Data;
    Name referenceString = Name::ReferenceString;
    int32_t at = SkipToBack(t->events, t->events.len - 1, &referenceString, 1);
    Position position = PositionFromExitEvent(t->events, at);
    Slice slice = SliceFromPosition(t->parseState->bytes, position);
    Str id = NormalizeIdentifier(t->parseState->scratch, slice.bytes);
    if (DefinitionsContain(t->parseState->definitions, id)) {
        return StateOk();
    }
    return StateNok();
}

State LabelEndReferenceFullMissing(Tokenizer* t) {
    t->tokenizeState.token1 = Name::Data;
    t->tokenizeState.token2 = Name::Data;
    t->tokenizeState.token3 = Name::Data;
    return StateNok();
}

State LabelEndReferenceCollapsed(Tokenizer* t) {
    Enter(t, Name::Reference);
    Enter(t, Name::ReferenceMarker);
    Consume(t);
    Exit(t, Name::ReferenceMarker);
    return StateNext(StateName::LabelEndReferenceCollapsedOpen);
}

State LabelEndReferenceCollapsedOpen(Tokenizer* t) {
    if (t->current == ']') {
        Enter(t, Name::ReferenceMarker);
        Consume(t);
        Exit(t, Name::ReferenceMarker);
        Exit(t, Name::Reference);
        return StateOk();
    }
    return StateNok();
}

static void InjectLabels(Tokenizer* t, const Vec<Label>& labels) {
    for (int32_t index = 0; index < len(labels); index++) {
        const Label& label = labels[index];
        Name groupName = Name::Link;
        if (label.kind == LabelKind::GfmFootnote) {
            groupName = Name::GfmFootnoteCall;
        } else if (label.kind == LabelKind::Image) {
            groupName = Name::Image;
        }

        // An undefined footnote call is a link whose text starts with `^`.
        Event caret[2];
        int32_t caretLen = 0;
        if (label.kind == LabelKind::GfmUndefinedFootnote) {
            caret[0].kind = Kind::Enter;
            caret[0].name = Name::Data;
            caret[0].point = t->events[label.startB - 2].point;
            caret[1].kind = Kind::Exit;
            caret[1].name = Name::Data;
            caret[1].point = t->events[label.startB - 1].point;
            caretLen = 2;
            t->events[label.startA].name = Name::LabelLink;
            t->events[label.startB].name = Name::LabelLink;
            t->events[label.startB].point = caret[0].point;
            EditMapAdd(t->map, label.startB - 2, 2, nullptr, 0);
        }

        Event open[2];
        open[0].kind = Kind::Enter;
        open[0].name = groupName;
        open[0].point = t->events[label.startA].point;
        open[1].kind = Kind::Enter;
        open[1].name = Name::Label;
        open[1].point = t->events[label.startA].point;
        EditMapAdd(t->map, label.startA, 0, open, 2);

        // The label text is only there when it is not empty.
        if (label.startB != label.endA || caretLen > 0) {
            Event textEnter;
            textEnter.kind = Kind::Enter;
            textEnter.name = Name::LabelText;
            textEnter.point = t->events[label.startB].point;
            EditMapAddBefore(t->map, label.startB + 1, 0, &textEnter, 1);
            Event textExit;
            textExit.kind = Kind::Exit;
            textExit.name = Name::LabelText;
            textExit.point = t->events[label.endA].point;
            EditMapAdd(t->map, label.endA, 0, &textExit, 1);
        }

        if (caretLen > 0) {
            EditMapAdd(t->map, label.startB + 1, 0, caret, caretLen);
        }

        Event labelExit;
        labelExit.kind = Kind::Exit;
        labelExit.name = Name::Label;
        labelExit.point = t->events[label.endA + 3].point;
        EditMapAdd(t->map, label.endA + 4, 0, &labelExit, 1);

        Event groupExit;
        groupExit.kind = Kind::Exit;
        groupExit.name = groupName;
        groupExit.point = t->events[label.endB].point;
        EditMapAdd(t->map, label.endB + 1, 0, &groupExit, 1);
    }
}

static void MarkAsData(Tokenizer* t, const Vec<LabelStartMark>& events) {
    for (int32_t index = 0; index < len(events); index++) {
        int32_t dataEnterIndex = events[index].startA;
        int32_t dataExitIndex = events[index].startB;
        Event add[2];
        add[0].kind = Kind::Enter;
        add[0].name = Name::Data;
        add[0].point = t->events[dataEnterIndex].point;
        add[1].kind = Kind::Exit;
        add[1].name = Name::Data;
        add[1].point = t->events[dataExitIndex].point;
        EditMapAdd(t->map, dataEnterIndex, dataExitIndex - dataEnterIndex + 1,
                   add, 2);
    }
}

bool LabelEndResolve(Tokenizer* t, Subresult*) {
    // Inject the labels.
    Vec<Label> labels;
    for (int32_t i = 0; i < len(t->tokenizeState.labels); i++) {
        VecAppend(labels, t->tokenizeState.labels[i]);
    }
    t->tokenizeState.labels.len = 0;
    InjectLabels(t, labels);

    // Turn the leftover starts into data.
    Vec<LabelStartMark> starts;
    for (int32_t i = 0; i < t->tokenizeState.labelStarts.len; i++) {
        VecAppend(starts, t->tokenizeState.labelStarts[i]);
    }
    t->tokenizeState.labelStarts.len = 0;
    MarkAsData(t, starts);

    starts.len = 0;
    for (int32_t i = 0; i < t->tokenizeState.labelStartsLoose.len; i++) {
        VecAppend(starts, t->tokenizeState.labelStartsLoose[i]);
    }
    t->tokenizeState.labelStartsLoose.len = 0;
    MarkAsData(t, starts);

    EditMapConsume(t->map, t->events);
    return false;
}

} // namespace markdown
