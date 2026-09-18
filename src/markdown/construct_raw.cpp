/* Fenced code and math, in flow and in text — one pair of constructs each,
   which is why the crate calls them raw.

   | Rust                       | here        |
   | -------------------------- | ----------- |
   | `construct/raw_flow.rs`    | RawFlow*    |
   | `construct/raw_text.rs`    | RawText*    |

   Part of the C++ port of markdown-rs 1.0.0 (see src/markdown/readme.md). */

#include "markdown/construct.h"

namespace markdown {

// ─── raw_flow.rs ─────────────────────────────────────────────────────────

static void RawFlowClear(Tokenizer* t) {
    t->tokenizeState.marker = 0;
    t->tokenizeState.sizeC = 0;
    t->tokenizeState.size = 0;
    t->tokenizeState.token1 = Name::Data;
    t->tokenizeState.token2 = Name::Data;
    t->tokenizeState.token3 = Name::Data;
    t->tokenizeState.token4 = Name::Data;
    t->tokenizeState.token5 = Name::Data;
    t->tokenizeState.token6 = Name::Data;
}

State RawFlowStart(Tokenizer* t) {
    if (t->parseState->options->constructs.codeFenced ||
        t->parseState->options->constructs.mathFlow) {
        if (t->current == '\t' || t->current == ' ') {
            TokenizerAttempt(t, StateNext(StateName::RawFlowBeforeSequenceOpen),
                             StateNok());
            int32_t max = t->parseState->options->constructs.codeIndented
                              ? kTabSize - 1
                              : kSizeMax;
            return StateRetry(SpaceOrTabMinMax(t, 0, max));
        }
        if (t->current == '$' || t->current == '`' || t->current == '~') {
            return StateRetry(StateName::RawFlowBeforeSequenceOpen);
        }
    }
    return StateNok();
}

State RawFlowBeforeSequenceOpen(Tokenizer* t) {
    int32_t prefix = 0;
    if (t->events.len > 0 && t->events[t->events.len - 1]
                                     .name == Name::SpaceOrTab) {
        Position position = PositionFromExitEvent(t->events, t->events.len - 1);
        prefix = SliceFromPosition(t->parseState->bytes, position).Len();
    }

    bool codeFence = t->parseState->options->constructs.codeFenced &&
                     (t->current == '`' || t->current == '~');
    bool mathFence =
        t->parseState->options->constructs.mathFlow && t->current == '$';
    if (!codeFence && !mathFence) {
        return StateNok();
    }

    t->tokenizeState.marker = (uint8_t)t->current;
    t->tokenizeState.sizeC = prefix;
    if (t->tokenizeState.marker == '$') {
        t->tokenizeState.token1 = Name::MathFlow;
        t->tokenizeState.token2 = Name::MathFlowFence;
        t->tokenizeState.token3 = Name::MathFlowFenceSequence;
        t->tokenizeState.token5 = Name::MathFlowFenceMeta;
        t->tokenizeState.token6 = Name::MathFlowChunk;
    } else {
        t->tokenizeState.token1 = Name::CodeFenced;
        t->tokenizeState.token2 = Name::CodeFencedFence;
        t->tokenizeState.token3 = Name::CodeFencedFenceSequence;
        t->tokenizeState.token4 = Name::CodeFencedFenceInfo;
        t->tokenizeState.token5 = Name::CodeFencedFenceMeta;
        t->tokenizeState.token6 = Name::CodeFlowChunk;
    }
    Enter(t, t->tokenizeState.token1);
    Enter(t, t->tokenizeState.token2);
    Enter(t, t->tokenizeState.token3);
    return StateRetry(StateName::RawFlowSequenceOpen);
}

State RawFlowSequenceOpen(Tokenizer* t) {
    if (t->current == (int32_t)t->tokenizeState.marker) {
        t->tokenizeState.size += 1;
        Consume(t);
        return StateNext(StateName::RawFlowSequenceOpen);
    }
    int32_t min = t->tokenizeState.marker == '$' ? kMathFlowSequenceSizeMin
                                                 : kCodeFencedSequenceSizeMin;
    if (t->tokenizeState.size < min) {
        RawFlowClear(t);
        return StateNok();
    }
    StateName next = t->tokenizeState.marker == '$'
                         ? StateName::RawFlowMetaBefore
                         : StateName::RawFlowInfoBefore;
    if (t->current == '\t' || t->current == ' ') {
        Exit(t, t->tokenizeState.token3);
        TokenizerAttempt(t, StateNext(next), StateNok());
        return StateRetry(SpaceOrTab(t));
    }
    Exit(t, t->tokenizeState.token3);
    return StateRetry(next);
}

State RawFlowInfoBefore(Tokenizer* t) {
    if (t->current < 0 || t->current == '\n') {
        Exit(t, t->tokenizeState.token2);
        // Do not form containers.
        t->concrete = true;
        TokenizerCheck(t, StateNext(StateName::RawFlowAtNonLazyBreak),
                       StateNext(StateName::RawFlowAfter));
        return StateRetry(StateName::NonLazyContinuationStart);
    }
    Enter(t, t->tokenizeState.token4);
    Link link;
    link.content = ContentKind::String;
    EnterLink(t, Name::Data, link);
    return StateRetry(StateName::RawFlowInfo);
}

State RawFlowInfo(Tokenizer* t) {
    if (t->current < 0 || t->current == '\n') {
        Exit(t, Name::Data);
        Exit(t, t->tokenizeState.token4);
        return StateRetry(StateName::RawFlowInfoBefore);
    }
    if (t->current == '\t' || t->current == ' ') {
        Exit(t, Name::Data);
        Exit(t, t->tokenizeState.token4);
        TokenizerAttempt(t, StateNext(StateName::RawFlowMetaBefore),
                         StateNok());
        return StateRetry(SpaceOrTab(t));
    }
    if (t->current == (int32_t)t->tokenizeState.marker &&
        (t->current == '$' || t->current == '`')) {
        t->concrete = false;
        RawFlowClear(t);
        return StateNok();
    }
    Consume(t);
    return StateNext(StateName::RawFlowInfo);
}

State RawFlowMetaBefore(Tokenizer* t) {
    if (t->current < 0 || t->current == '\n') {
        return StateRetry(StateName::RawFlowInfoBefore);
    }
    Enter(t, t->tokenizeState.token5);
    Link link;
    link.content = ContentKind::String;
    EnterLink(t, Name::Data, link);
    return StateRetry(StateName::RawFlowMeta);
}

State RawFlowMeta(Tokenizer* t) {
    if (t->current < 0 || t->current == '\n') {
        Exit(t, Name::Data);
        Exit(t, t->tokenizeState.token5);
        return StateRetry(StateName::RawFlowInfoBefore);
    }
    if (t->current == (int32_t)t->tokenizeState.marker &&
        (t->current == '$' || t->current == '`')) {
        t->concrete = false;
        RawFlowClear(t);
        return StateNok();
    }
    Consume(t);
    return StateNext(StateName::RawFlowMeta);
}

State RawFlowAtNonLazyBreak(Tokenizer* t) {
    TokenizerAttempt(t, StateNext(StateName::RawFlowAfter),
                     StateNext(StateName::RawFlowContentBefore));
    Enter(t, Name::LineEnding);
    Consume(t);
    Exit(t, Name::LineEnding);
    return StateNext(StateName::RawFlowCloseStart);
}

State RawFlowCloseStart(Tokenizer* t) {
    Enter(t, t->tokenizeState.token2);
    if (t->current == '\t' || t->current == ' ') {
        TokenizerAttempt(t, StateNext(StateName::RawFlowBeforeSequenceClose),
                         StateNok());
        int32_t max = t->parseState->options->constructs.codeIndented
                          ? kTabSize - 1
                          : kSizeMax;
        return StateRetry(SpaceOrTabMinMax(t, 0, max));
    }
    return StateRetry(StateName::RawFlowBeforeSequenceClose);
}

State RawFlowBeforeSequenceClose(Tokenizer* t) {
    if (t->current == (int32_t)t->tokenizeState.marker) {
        Enter(t, t->tokenizeState.token3);
        return StateRetry(StateName::RawFlowSequenceClose);
    }
    return StateNok();
}

State RawFlowSequenceClose(Tokenizer* t) {
    if (t->current == (int32_t)t->tokenizeState.marker) {
        t->tokenizeState.sizeB += 1;
        Consume(t);
        return StateNext(StateName::RawFlowSequenceClose);
    }
    if (t->tokenizeState.sizeB >= t->tokenizeState.size) {
        t->tokenizeState.sizeB = 0;
        Exit(t, t->tokenizeState.token3);
        if (t->current == '\t' || t->current == ' ') {
            TokenizerAttempt(t, StateNext(StateName::RawFlowAfterSequenceClose),
                             StateNok());
            return StateRetry(SpaceOrTab(t));
        }
        return StateRetry(StateName::RawFlowAfterSequenceClose);
    }
    t->tokenizeState.sizeB = 0;
    return StateNok();
}

State RawFlowAfterSequenceClose(Tokenizer* t) {
    if (t->current < 0 || t->current == '\n') {
        Exit(t, t->tokenizeState.token2);
        return StateOk();
    }
    return StateNok();
}

State RawFlowContentBefore(Tokenizer* t) {
    Enter(t, Name::LineEnding);
    Consume(t);
    Exit(t, Name::LineEnding);
    return StateNext(StateName::RawFlowContentStart);
}

State RawFlowContentStart(Tokenizer* t) {
    if (t->current == '\t' || t->current == ' ') {
        TokenizerAttempt(t, StateNext(StateName::RawFlowBeforeContentChunk),
                         StateNok());
        return StateRetry(SpaceOrTabMinMax(t, 0, t->tokenizeState.sizeC));
    }
    return StateRetry(StateName::RawFlowBeforeContentChunk);
}

State RawFlowBeforeContentChunk(Tokenizer* t) {
    if (t->current < 0 || t->current == '\n') {
        TokenizerCheck(t, StateNext(StateName::RawFlowAtNonLazyBreak),
                       StateNext(StateName::RawFlowAfter));
        return StateRetry(StateName::NonLazyContinuationStart);
    }
    Enter(t, t->tokenizeState.token6);
    return StateRetry(StateName::RawFlowContentChunk);
}

State RawFlowContentChunk(Tokenizer* t) {
    if (t->current < 0 || t->current == '\n') {
        Exit(t, t->tokenizeState.token6);
        return StateRetry(StateName::RawFlowBeforeContentChunk);
    }
    Consume(t);
    return StateNext(StateName::RawFlowContentChunk);
}

State RawFlowAfter(Tokenizer* t) {
    Exit(t, t->tokenizeState.token1);
    RawFlowClear(t);
    t->interrupt = false;
    // No longer concrete.
    t->concrete = false;
    return StateOk();
}

// ─── raw_text.rs ─────────────────────────────────────────────────────────

static void RawTextClear(Tokenizer* t) {
    t->tokenizeState.marker = 0;
    t->tokenizeState.size = 0;
    t->tokenizeState.token1 = Name::Data;
    t->tokenizeState.token2 = Name::Data;
    t->tokenizeState.token3 = Name::Data;
}

State RawTextStart(Tokenizer* t) {
    bool code =
        t->parseState->options->constructs.codeText && t->current == '`';
    bool math =
        t->parseState->options->constructs.mathText && t->current == '$';
    bool afterEscape = t->events.len > 0 && t->events[t->events.len - 1].name ==
                                                Name::CharacterEscape;
    if ((code || math) && (t->previous != t->current || afterEscape)) {
        uint8_t marker = (uint8_t)t->current;
        if (marker == '`') {
            t->tokenizeState.token1 = Name::CodeText;
            t->tokenizeState.token2 = Name::CodeTextSequence;
            t->tokenizeState.token3 = Name::CodeTextData;
        } else {
            t->tokenizeState.token1 = Name::MathText;
            t->tokenizeState.token2 = Name::MathTextSequence;
            t->tokenizeState.token3 = Name::MathTextData;
        }
        t->tokenizeState.marker = marker;
        Enter(t, t->tokenizeState.token1);
        Enter(t, t->tokenizeState.token2);
        return StateRetry(StateName::RawTextSequenceOpen);
    }
    return StateNok();
}

State RawTextSequenceOpen(Tokenizer* t) {
    if (t->current == (int32_t)t->tokenizeState.marker) {
        t->tokenizeState.size += 1;
        Consume(t);
        return StateNext(StateName::RawTextSequenceOpen);
    }
    if (t->tokenizeState.marker == '$' && t->tokenizeState.size == 1 &&
        !t->parseState->options->mathTextSingleDollar) {
        RawTextClear(t);
        return StateNok();
    }
    Exit(t, t->tokenizeState.token2);
    return StateRetry(StateName::RawTextBetween);
}

State RawTextBetween(Tokenizer* t) {
    if (t->current < 0) {
        RawTextClear(t);
        return StateNok();
    }
    if (t->current == '\n') {
        Enter(t, Name::LineEnding);
        Consume(t);
        Exit(t, Name::LineEnding);
        return StateNext(StateName::RawTextBetween);
    }
    if (t->current == (int32_t)t->tokenizeState.marker) {
        Enter(t, t->tokenizeState.token2);
        return StateRetry(StateName::RawTextSequenceClose);
    }
    Enter(t, t->tokenizeState.token3);
    return StateRetry(StateName::RawTextData);
}

State RawTextData(Tokenizer* t) {
    if (t->current < 0 || t->current == '\n' ||
        t->current == (int32_t)t->tokenizeState.marker) {
        Exit(t, t->tokenizeState.token3);
        return StateRetry(StateName::RawTextBetween);
    }
    Consume(t);
    return StateNext(StateName::RawTextData);
}

State RawTextSequenceClose(Tokenizer* t) {
    if (t->current == (int32_t)t->tokenizeState.marker) {
        t->tokenizeState.sizeB += 1;
        Consume(t);
        return StateNext(StateName::RawTextSequenceClose);
    }
    Exit(t, t->tokenizeState.token2);
    if (t->tokenizeState.size == t->tokenizeState.sizeB) {
        Exit(t, t->tokenizeState.token1);
        t->tokenizeState.sizeB = 0;
        RawTextClear(t);
        return StateOk();
    }
    // More or less accents: mark the sequence as data.
    int32_t len = t->events.len;
    t->events[len - 2].name = t->tokenizeState.token3;
    t->events[len - 1].name = t->tokenizeState.token3;
    t->tokenizeState.sizeB = 0;
    return StateRetry(StateName::RawTextBetween);
}

} // namespace markdown
