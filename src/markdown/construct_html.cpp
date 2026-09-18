/* Raw HTML, as a block and inside a paragraph.

   | Rust                       | here        |
   | -------------------------- | ----------- |
   | `construct/html_flow.rs`   | HtmlFlow*   |
   | `construct/html_text.rs`   | HtmlText*   |

   Part of the C++ port of markdown-rs 1.0.0 (see src/markdown/readme.md). */

#include "markdown/construct.h"

namespace markdown {

// ─── html_flow.rs ────────────────────────────────────────────────────────

// The kinds of HTML (flow), as `tokenize_state.marker`.
static const uint8_t kHtmlRaw = 1;
static const uint8_t kHtmlComment = 2;
static const uint8_t kHtmlInstruction = 3;
static const uint8_t kHtmlDeclaration = 4;
static const uint8_t kHtmlCdata = 5;
static const uint8_t kHtmlBasic = 6;
static const uint8_t kHtmlComplete = 7;

// `HTML_RAW_NAMES.contains(&name)` / `HTML_BLOCK_NAMES.contains(&name)`,
// against a name that is not lowercased first.
// The name lists are SeqStrings runs. Tag names are ASCII by CommonMark;
// base's case-insensitive slice comparison avoids a folded copy.
static bool NamesContainI(SeqStrings names, Str name) {
    for (Str item = SeqStrFirst(names); len(item) > 0;
         item = SeqStrNext(item)) {
        if (base::StrEqI(item, name)) {
            return true;
        }
    }
    return false;
}

State HtmlFlowStart(Tokenizer* t) {
    if (!t->parseState->options->constructs.htmlFlow) {
        return StateNok();
    }
    Enter(t, Name::HtmlFlow);
    if (t->current == '\t' || t->current == ' ') {
        TokenizerAttempt(t, StateNext(StateName::HtmlFlowBefore), StateNok());
        SpaceOrTabOptions options;
        options.kind = Name::HtmlFlowData;
        options.min = 0;
        options.max = t->parseState->options->constructs.codeIndented
                          ? kTabSize - 1
                          : kSizeMax;
        return StateRetry(SpaceOrTabWithOptions(t, options));
    }
    return StateRetry(StateName::HtmlFlowBefore);
}

State HtmlFlowBefore(Tokenizer* t) {
    if (t->current == '<') {
        Enter(t, Name::HtmlFlowData);
        Consume(t);
        return StateNext(StateName::HtmlFlowOpen);
    }
    return StateNok();
}

State HtmlFlowOpen(Tokenizer* t) {
    if (t->current == '!') {
        Consume(t);
        return StateNext(StateName::HtmlFlowDeclarationOpen);
    }
    if (t->current == '/') {
        Consume(t);
        t->tokenizeState.seen = true;
        t->tokenizeState.start = t->point.index;
        return StateNext(StateName::HtmlFlowTagCloseStart);
    }
    if (t->current == '?') {
        Consume(t);
        t->tokenizeState.marker = kHtmlInstruction;
        // Do not form containers.
        t->concrete = true;
        return StateNext(StateName::HtmlFlowContinuationDeclarationInside);
    }
    if (t->current >= 0 && IsAsciiAlpha((uint8_t)t->current)) {
        t->tokenizeState.start = t->point.index;
        return StateRetry(StateName::HtmlFlowTagName);
    }
    return StateNok();
}

State HtmlFlowDeclarationOpen(Tokenizer* t) {
    if (t->current == '-') {
        Consume(t);
        t->tokenizeState.marker = kHtmlComment;
        return StateNext(StateName::HtmlFlowCommentOpenInside);
    }
    if (t->current >= 0 && IsAsciiAlpha((uint8_t)t->current)) {
        Consume(t);
        t->tokenizeState.marker = kHtmlDeclaration;
        t->concrete = true;
        return StateNext(StateName::HtmlFlowContinuationDeclarationInside);
    }
    if (t->current == '[') {
        Consume(t);
        t->tokenizeState.marker = kHtmlCdata;
        return StateNext(StateName::HtmlFlowCdataOpenInside);
    }
    return StateNok();
}

State HtmlFlowCommentOpenInside(Tokenizer* t) {
    if (t->current == '-') {
        Consume(t);
        t->concrete = true;
        return StateNext(StateName::HtmlFlowContinuationDeclarationInside);
    }
    t->tokenizeState.marker = 0;
    return StateNok();
}

State HtmlFlowCdataOpenInside(Tokenizer* t) {
    if (t->current == (int32_t)(uint8_t)kHtmlCdataPrefix
                          .s[t->tokenizeState.size]) {
        Consume(t);
        t->tokenizeState.size += 1;
        if (t->tokenizeState.size == kHtmlCdataPrefix.len) {
            t->tokenizeState.size = 0;
            t->concrete = true;
            return StateNext(StateName::HtmlFlowContinuation);
        }
        return StateNext(StateName::HtmlFlowCdataOpenInside);
    }
    t->tokenizeState.marker = 0;
    t->tokenizeState.size = 0;
    return StateNok();
}

State HtmlFlowTagCloseStart(Tokenizer* t) {
    if (t->current >= 0 && IsAsciiAlpha((uint8_t)t->current)) {
        Consume(t);
        return StateNext(StateName::HtmlFlowTagName);
    }
    t->tokenizeState.seen = false;
    t->tokenizeState.start = 0;
    return StateNok();
}

State HtmlFlowTagName(Tokenizer* t) {
    if (t->current < 0 || t->current == '\t' || t->current == '\n' ||
        t->current == ' ' || t->current == '/' || t->current == '>') {
        bool closingTag = t->tokenizeState.seen;
        bool slash = t->current == '/';
        Slice slice = SliceFromIndices(t->parseState->bytes,
                                       t->tokenizeState.start, t->point.index);
        Str name = base::StrTrimAscii(slice.bytes);
        t->tokenizeState.seen = false;
        t->tokenizeState.start = 0;

        if (!slash && !closingTag && NamesContainI(kHtmlRawNames, name)) {
            t->tokenizeState.marker = kHtmlRaw;
            t->concrete = true;
            return StateRetry(StateName::HtmlFlowContinuation);
        }
        if (NamesContainI(kHtmlBlockNames, name)) {
            t->tokenizeState.marker = kHtmlBasic;
            if (slash) {
                Consume(t);
                return StateNext(StateName::HtmlFlowBasicSelfClosing);
            }
            t->concrete = true;
            return StateRetry(StateName::HtmlFlowContinuation);
        }
        t->tokenizeState.marker = kHtmlComplete;
        // Do not support complete HTML when interrupting.
        if (t->interrupt && !t->lazy) {
            t->tokenizeState.marker = 0;
            return StateNok();
        }
        if (closingTag) {
            return StateRetry(StateName::HtmlFlowCompleteClosingTagAfter);
        }
        return StateRetry(StateName::HtmlFlowCompleteAttributeNameBefore);
    }
    if (t->current == '-' ||
        (t->current >= 0 && IsAsciiAlphanumeric((uint8_t)t->current))) {
        Consume(t);
        return StateNext(StateName::HtmlFlowTagName);
    }
    t->tokenizeState.seen = false;
    return StateNok();
}

State HtmlFlowBasicSelfClosing(Tokenizer* t) {
    if (t->current == '>') {
        Consume(t);
        t->concrete = true;
        return StateNext(StateName::HtmlFlowContinuation);
    }
    t->tokenizeState.marker = 0;
    return StateNok();
}

State HtmlFlowCompleteClosingTagAfter(Tokenizer* t) {
    if (t->current == '\t' || t->current == ' ') {
        Consume(t);
        return StateNext(StateName::HtmlFlowCompleteClosingTagAfter);
    }
    return StateRetry(StateName::HtmlFlowCompleteEnd);
}

State HtmlFlowCompleteAttributeNameBefore(Tokenizer* t) {
    if (t->current == '\t' || t->current == ' ') {
        Consume(t);
        return StateNext(StateName::HtmlFlowCompleteAttributeNameBefore);
    }
    if (t->current == '/') {
        Consume(t);
        return StateNext(StateName::HtmlFlowCompleteEnd);
    }
    if (t->current == ':' || t->current == '_' ||
        (t->current >= 0 && (IsAsciiDigit((uint8_t)t->current) ||
                             IsAsciiAlpha((uint8_t)t->current)))) {
        Consume(t);
        return StateNext(StateName::HtmlFlowCompleteAttributeName);
    }
    return StateRetry(StateName::HtmlFlowCompleteEnd);
}

State HtmlFlowCompleteAttributeName(Tokenizer* t) {
    if (t->current == '-' || t->current == '.' || t->current == ':' ||
        t->current == '_' ||
        (t->current >= 0 && IsAsciiAlphanumeric((uint8_t)t->current))) {
        Consume(t);
        return StateNext(StateName::HtmlFlowCompleteAttributeName);
    }
    return StateRetry(StateName::HtmlFlowCompleteAttributeNameAfter);
}

State HtmlFlowCompleteAttributeNameAfter(Tokenizer* t) {
    if (t->current == '\t' || t->current == ' ') {
        Consume(t);
        return StateNext(StateName::HtmlFlowCompleteAttributeNameAfter);
    }
    if (t->current == '=') {
        Consume(t);
        return StateNext(StateName::HtmlFlowCompleteAttributeValueBefore);
    }
    return StateRetry(StateName::HtmlFlowCompleteAttributeNameBefore);
}

State HtmlFlowCompleteAttributeValueBefore(Tokenizer* t) {
    if (t->current < 0 || t->current == '<' || t->current == '=' ||
        t->current == '>' || t->current == '`') {
        t->tokenizeState.marker = 0;
        return StateNok();
    }
    if (t->current == '\t' || t->current == ' ') {
        Consume(t);
        return StateNext(StateName::HtmlFlowCompleteAttributeValueBefore);
    }
    if (t->current == '"' || t->current == '\'') {
        t->tokenizeState.markerB = (uint8_t)t->current;
        Consume(t);
        return StateNext(StateName::HtmlFlowCompleteAttributeValueQuoted);
    }
    return StateRetry(StateName::HtmlFlowCompleteAttributeValueUnquoted);
}

State HtmlFlowCompleteAttributeValueQuoted(Tokenizer* t) {
    if (t->current == (int32_t)t->tokenizeState.markerB) {
        Consume(t);
        t->tokenizeState.markerB = 0;
        return StateNext(StateName::HtmlFlowCompleteAttributeValueQuotedAfter);
    }
    if (t->current < 0 || t->current == '\n') {
        t->tokenizeState.marker = 0;
        t->tokenizeState.markerB = 0;
        return StateNok();
    }
    Consume(t);
    return StateNext(StateName::HtmlFlowCompleteAttributeValueQuoted);
}

State HtmlFlowCompleteAttributeValueUnquoted(Tokenizer* t) {
    if (t->current < 0 || t->current == '\t' || t->current == '\n' ||
        t->current == ' ' || t->current == '"' || t->current == '\'' ||
        t->current == '/' || t->current == '<' || t->current == '=' ||
        t->current == '>' || t->current == '`') {
        return StateRetry(StateName::HtmlFlowCompleteAttributeNameAfter);
    }
    Consume(t);
    return StateNext(StateName::HtmlFlowCompleteAttributeValueUnquoted);
}

State HtmlFlowCompleteAttributeValueQuotedAfter(Tokenizer* t) {
    if (t->current == '\t' || t->current == ' ' || t->current == '/' ||
        t->current == '>') {
        return StateRetry(StateName::HtmlFlowCompleteAttributeNameBefore);
    }
    t->tokenizeState.marker = 0;
    return StateNok();
}

State HtmlFlowCompleteEnd(Tokenizer* t) {
    if (t->current == '>') {
        Consume(t);
        return StateNext(StateName::HtmlFlowCompleteAfter);
    }
    t->tokenizeState.marker = 0;
    return StateNok();
}

State HtmlFlowCompleteAfter(Tokenizer* t) {
    if (t->current < 0 || t->current == '\n') {
        // Do not form containers.
        t->concrete = true;
        return StateRetry(StateName::HtmlFlowContinuation);
    }
    if (t->current == '\t' || t->current == ' ') {
        Consume(t);
        return StateNext(StateName::HtmlFlowCompleteAfter);
    }
    t->tokenizeState.marker = 0;
    return StateNok();
}

State HtmlFlowContinuation(Tokenizer* t) {
    uint8_t marker = t->tokenizeState.marker;
    if (marker == kHtmlComment && t->current == '-') {
        Consume(t);
        return StateNext(StateName::HtmlFlowContinuationCommentInside);
    }
    if (marker == kHtmlRaw && t->current == '<') {
        Consume(t);
        return StateNext(StateName::HtmlFlowContinuationRawTagOpen);
    }
    if (marker == kHtmlDeclaration && t->current == '>') {
        Consume(t);
        return StateNext(StateName::HtmlFlowContinuationClose);
    }
    if (marker == kHtmlInstruction && t->current == '?') {
        Consume(t);
        return StateNext(StateName::HtmlFlowContinuationDeclarationInside);
    }
    if (marker == kHtmlCdata && t->current == ']') {
        Consume(t);
        return StateNext(StateName::HtmlFlowContinuationCdataInside);
    }
    if ((marker == kHtmlBasic || marker == kHtmlComplete) &&
        t->current == '\n') {
        Exit(t, Name::HtmlFlowData);
        TokenizerCheck(t, StateNext(StateName::HtmlFlowContinuationAfter),
                       StateNext(StateName::HtmlFlowContinuationStart));
        return StateRetry(StateName::HtmlFlowBlankLineBefore);
    }
    if (t->current < 0 || t->current == '\n') {
        Exit(t, Name::HtmlFlowData);
        return StateRetry(StateName::HtmlFlowContinuationStart);
    }
    Consume(t);
    return StateNext(StateName::HtmlFlowContinuation);
}

State HtmlFlowContinuationStart(Tokenizer* t) {
    TokenizerCheck(t, StateNext(StateName::HtmlFlowContinuationStartNonLazy),
                   StateNext(StateName::HtmlFlowContinuationAfter));
    return StateRetry(StateName::NonLazyContinuationStart);
}

State HtmlFlowContinuationStartNonLazy(Tokenizer* t) {
    Enter(t, Name::LineEnding);
    Consume(t);
    Exit(t, Name::LineEnding);
    return StateNext(StateName::HtmlFlowContinuationBefore);
}

State HtmlFlowContinuationBefore(Tokenizer* t) {
    if (t->current < 0 || t->current == '\n') {
        return StateRetry(StateName::HtmlFlowContinuationStart);
    }
    Enter(t, Name::HtmlFlowData);
    return StateRetry(StateName::HtmlFlowContinuation);
}

State HtmlFlowContinuationCommentInside(Tokenizer* t) {
    if (t->current == '-') {
        Consume(t);
        return StateNext(StateName::HtmlFlowContinuationDeclarationInside);
    }
    return StateRetry(StateName::HtmlFlowContinuation);
}

State HtmlFlowContinuationRawTagOpen(Tokenizer* t) {
    if (t->current == '/') {
        Consume(t);
        t->tokenizeState.start = t->point.index;
        return StateNext(StateName::HtmlFlowContinuationRawEndTag);
    }
    return StateRetry(StateName::HtmlFlowContinuation);
}

State HtmlFlowContinuationRawEndTag(Tokenizer* t) {
    if (t->current == '>') {
        Slice slice = SliceFromIndices(t->parseState->bytes,
                                       t->tokenizeState.start, t->point.index);
        t->tokenizeState.start = 0;
        if (NamesContainI(kHtmlRawNames, slice.bytes)) {
            Consume(t);
            return StateNext(StateName::HtmlFlowContinuationClose);
        }
        return StateRetry(StateName::HtmlFlowContinuation);
    }
    if (t->current >= 0 && IsAsciiAlpha((uint8_t)t->current) &&
        t->point.index - t->tokenizeState.start < kHtmlRawSizeMax) {
        Consume(t);
        return StateNext(StateName::HtmlFlowContinuationRawEndTag);
    }
    t->tokenizeState.start = 0;
    return StateRetry(StateName::HtmlFlowContinuation);
}

State HtmlFlowContinuationCdataInside(Tokenizer* t) {
    if (t->current == ']') {
        Consume(t);
        return StateNext(StateName::HtmlFlowContinuationDeclarationInside);
    }
    return StateRetry(StateName::HtmlFlowContinuation);
}

State HtmlFlowContinuationDeclarationInside(Tokenizer* t) {
    if (t->tokenizeState.marker == kHtmlComment && t->current == '-') {
        Consume(t);
        return StateNext(StateName::HtmlFlowContinuationDeclarationInside);
    }
    if (t->current == '>') {
        Consume(t);
        return StateNext(StateName::HtmlFlowContinuationClose);
    }
    return StateRetry(StateName::HtmlFlowContinuation);
}

State HtmlFlowContinuationClose(Tokenizer* t) {
    if (t->current < 0 || t->current == '\n') {
        Exit(t, Name::HtmlFlowData);
        return StateRetry(StateName::HtmlFlowContinuationAfter);
    }
    Consume(t);
    return StateNext(StateName::HtmlFlowContinuationClose);
}

State HtmlFlowContinuationAfter(Tokenizer* t) {
    Exit(t, Name::HtmlFlow);
    t->tokenizeState.marker = 0;
    t->interrupt = false;
    // No longer concrete.
    t->concrete = false;
    return StateOk();
}

State HtmlFlowBlankLineBefore(Tokenizer* t) {
    Enter(t, Name::LineEnding);
    Consume(t);
    Exit(t, Name::LineEnding);
    return StateNext(StateName::BlankLineStart);
}

// ─── html_text.rs ────────────────────────────────────────────────────────

State HtmlTextStart(Tokenizer* t) {
    if (t->current == '<' && t->parseState->options->constructs.htmlText) {
        Enter(t, Name::HtmlText);
        Enter(t, Name::HtmlTextData);
        Consume(t);
        return StateNext(StateName::HtmlTextOpen);
    }
    return StateNok();
}

State HtmlTextOpen(Tokenizer* t) {
    if (t->current == '!') {
        Consume(t);
        return StateNext(StateName::HtmlTextDeclarationOpen);
    }
    if (t->current == '/') {
        Consume(t);
        return StateNext(StateName::HtmlTextTagCloseStart);
    }
    if (t->current == '?') {
        Consume(t);
        return StateNext(StateName::HtmlTextInstruction);
    }
    if (t->current >= 0 && IsAsciiAlpha((uint8_t)t->current)) {
        Consume(t);
        return StateNext(StateName::HtmlTextTagOpen);
    }
    return StateNok();
}

State HtmlTextDeclarationOpen(Tokenizer* t) {
    if (t->current == '-') {
        Consume(t);
        return StateNext(StateName::HtmlTextCommentOpenInside);
    }
    if (t->current >= 0 && IsAsciiAlpha((uint8_t)t->current)) {
        Consume(t);
        return StateNext(StateName::HtmlTextDeclaration);
    }
    if (t->current == '[') {
        Consume(t);
        return StateNext(StateName::HtmlTextCdataOpenInside);
    }
    return StateNok();
}

State HtmlTextCommentOpenInside(Tokenizer* t) {
    if (t->current == '-') {
        Consume(t);
        return StateNext(StateName::HtmlTextCommentEnd);
    }
    return StateNok();
}

State HtmlTextComment(Tokenizer* t) {
    if (t->current < 0) {
        return StateNok();
    }
    if (t->current == '\n') {
        TokenizerAttempt(t, StateNext(StateName::HtmlTextComment), StateNok());
        return StateRetry(StateName::HtmlTextLineEndingBefore);
    }
    if (t->current == '-') {
        Consume(t);
        return StateNext(StateName::HtmlTextCommentClose);
    }
    Consume(t);
    return StateNext(StateName::HtmlTextComment);
}

State HtmlTextCommentClose(Tokenizer* t) {
    if (t->current == '-') {
        Consume(t);
        return StateNext(StateName::HtmlTextCommentEnd);
    }
    return StateRetry(StateName::HtmlTextComment);
}

State HtmlTextCommentEnd(Tokenizer* t) {
    if (t->current == '>') {
        return StateRetry(StateName::HtmlTextEnd);
    }
    if (t->current == '-') {
        return StateRetry(StateName::HtmlTextCommentClose);
    }
    return StateRetry(StateName::HtmlTextComment);
}

State HtmlTextCdataOpenInside(Tokenizer* t) {
    if (t->current == (int32_t)(uint8_t)kHtmlCdataPrefix
                          .s[t->tokenizeState.size]) {
        t->tokenizeState.size += 1;
        Consume(t);
        if (t->tokenizeState.size == kHtmlCdataPrefix.len) {
            t->tokenizeState.size = 0;
            return StateNext(StateName::HtmlTextCdata);
        }
        return StateNext(StateName::HtmlTextCdataOpenInside);
    }
    return StateNok();
}

State HtmlTextCdata(Tokenizer* t) {
    if (t->current < 0) {
        return StateNok();
    }
    if (t->current == '\n') {
        TokenizerAttempt(t, StateNext(StateName::HtmlTextCdata), StateNok());
        return StateRetry(StateName::HtmlTextLineEndingBefore);
    }
    if (t->current == ']') {
        Consume(t);
        return StateNext(StateName::HtmlTextCdataClose);
    }
    Consume(t);
    return StateNext(StateName::HtmlTextCdata);
}

State HtmlTextCdataClose(Tokenizer* t) {
    if (t->current == ']') {
        Consume(t);
        return StateNext(StateName::HtmlTextCdataEnd);
    }
    return StateRetry(StateName::HtmlTextCdata);
}

State HtmlTextCdataEnd(Tokenizer* t) {
    if (t->current == '>') {
        return StateRetry(StateName::HtmlTextEnd);
    }
    if (t->current == ']') {
        return StateRetry(StateName::HtmlTextCdataClose);
    }
    return StateRetry(StateName::HtmlTextCdata);
}

State HtmlTextDeclaration(Tokenizer* t) {
    if (t->current < 0 || t->current == '>') {
        return StateRetry(StateName::HtmlTextEnd);
    }
    if (t->current == '\n') {
        TokenizerAttempt(t, StateNext(StateName::HtmlTextDeclaration),
                         StateNok());
        return StateRetry(StateName::HtmlTextLineEndingBefore);
    }
    Consume(t);
    return StateNext(StateName::HtmlTextDeclaration);
}

State HtmlTextInstruction(Tokenizer* t) {
    if (t->current < 0) {
        return StateNok();
    }
    if (t->current == '\n') {
        TokenizerAttempt(t, StateNext(StateName::HtmlTextInstruction),
                         StateNok());
        return StateRetry(StateName::HtmlTextLineEndingBefore);
    }
    if (t->current == '?') {
        Consume(t);
        return StateNext(StateName::HtmlTextInstructionClose);
    }
    Consume(t);
    return StateNext(StateName::HtmlTextInstruction);
}

State HtmlTextInstructionClose(Tokenizer* t) {
    if (t->current == '>') {
        return StateRetry(StateName::HtmlTextEnd);
    }
    return StateRetry(StateName::HtmlTextInstruction);
}

State HtmlTextTagCloseStart(Tokenizer* t) {
    if (t->current >= 0 && IsAsciiAlpha((uint8_t)t->current)) {
        Consume(t);
        return StateNext(StateName::HtmlTextTagClose);
    }
    return StateNok();
}

State HtmlTextTagClose(Tokenizer* t) {
    if (t->current == '-' ||
        (t->current >= 0 && IsAsciiAlphanumeric((uint8_t)t->current))) {
        Consume(t);
        return StateNext(StateName::HtmlTextTagClose);
    }
    return StateRetry(StateName::HtmlTextTagCloseBetween);
}

State HtmlTextTagCloseBetween(Tokenizer* t) {
    if (t->current == '\n') {
        TokenizerAttempt(t, StateNext(StateName::HtmlTextTagCloseBetween),
                         StateNok());
        return StateRetry(StateName::HtmlTextLineEndingBefore);
    }
    if (t->current == '\t' || t->current == ' ') {
        Consume(t);
        return StateNext(StateName::HtmlTextTagCloseBetween);
    }
    return StateRetry(StateName::HtmlTextEnd);
}

State HtmlTextTagOpen(Tokenizer* t) {
    if (t->current == '-' ||
        (t->current >= 0 && IsAsciiAlphanumeric((uint8_t)t->current))) {
        Consume(t);
        return StateNext(StateName::HtmlTextTagOpen);
    }
    if (t->current == '\t' || t->current == '\n' || t->current == ' ' ||
        t->current == '/' || t->current == '>') {
        return StateRetry(StateName::HtmlTextTagOpenBetween);
    }
    return StateNok();
}

State HtmlTextTagOpenBetween(Tokenizer* t) {
    if (t->current == '\n') {
        TokenizerAttempt(t, StateNext(StateName::HtmlTextTagOpenBetween),
                         StateNok());
        return StateRetry(StateName::HtmlTextLineEndingBefore);
    }
    if (t->current == '\t' || t->current == ' ') {
        Consume(t);
        return StateNext(StateName::HtmlTextTagOpenBetween);
    }
    if (t->current == '/') {
        Consume(t);
        return StateNext(StateName::HtmlTextEnd);
    }
    if (t->current == ':' || t->current == '_' ||
        (t->current >= 0 && IsAsciiAlpha((uint8_t)t->current))) {
        Consume(t);
        return StateNext(StateName::HtmlTextTagOpenAttributeName);
    }
    return StateRetry(StateName::HtmlTextEnd);
}

State HtmlTextTagOpenAttributeName(Tokenizer* t) {
    if (t->current == '-' || t->current == '.' || t->current == ':' ||
        t->current == '_' ||
        (t->current >= 0 && IsAsciiAlphanumeric((uint8_t)t->current))) {
        Consume(t);
        return StateNext(StateName::HtmlTextTagOpenAttributeName);
    }
    return StateRetry(StateName::HtmlTextTagOpenAttributeNameAfter);
}

State HtmlTextTagOpenAttributeNameAfter(Tokenizer* t) {
    if (t->current == '\n') {
        TokenizerAttempt(
            t, StateNext(StateName::HtmlTextTagOpenAttributeNameAfter),
            StateNok());
        return StateRetry(StateName::HtmlTextLineEndingBefore);
    }
    if (t->current == '\t' || t->current == ' ') {
        Consume(t);
        return StateNext(StateName::HtmlTextTagOpenAttributeNameAfter);
    }
    if (t->current == '=') {
        Consume(t);
        return StateNext(StateName::HtmlTextTagOpenAttributeValueBefore);
    }
    return StateRetry(StateName::HtmlTextTagOpenBetween);
}

State HtmlTextTagOpenAttributeValueBefore(Tokenizer* t) {
    if (t->current < 0 || t->current == '<' || t->current == '=' ||
        t->current == '>' || t->current == '`') {
        return StateNok();
    }
    if (t->current == '\n') {
        TokenizerAttempt(
            t, StateNext(StateName::HtmlTextTagOpenAttributeValueBefore),
            StateNok());
        return StateRetry(StateName::HtmlTextLineEndingBefore);
    }
    if (t->current == '\t' || t->current == ' ') {
        Consume(t);
        return StateNext(StateName::HtmlTextTagOpenAttributeValueBefore);
    }
    if (t->current == '"' || t->current == '\'') {
        t->tokenizeState.marker = (uint8_t)t->current;
        Consume(t);
        return StateNext(StateName::HtmlTextTagOpenAttributeValueQuoted);
    }
    Consume(t);
    return StateNext(StateName::HtmlTextTagOpenAttributeValueUnquoted);
}

State HtmlTextTagOpenAttributeValueQuoted(Tokenizer* t) {
    if (t->current == (int32_t)t->tokenizeState.marker) {
        t->tokenizeState.marker = 0;
        Consume(t);
        return StateNext(StateName::HtmlTextTagOpenAttributeValueQuotedAfter);
    }
    if (t->current < 0) {
        t->tokenizeState.marker = 0;
        return StateNok();
    }
    if (t->current == '\n') {
        TokenizerAttempt(
            t, StateNext(StateName::HtmlTextTagOpenAttributeValueQuoted),
            StateNok());
        return StateRetry(StateName::HtmlTextLineEndingBefore);
    }
    Consume(t);
    return StateNext(StateName::HtmlTextTagOpenAttributeValueQuoted);
}

State HtmlTextTagOpenAttributeValueUnquoted(Tokenizer* t) {
    if (t->current < 0 || t->current == '"' || t->current == '\'' ||
        t->current == '<' || t->current == '=' || t->current == '`') {
        return StateNok();
    }
    if (t->current == '\t' || t->current == '\n' || t->current == ' ' ||
        t->current == '/' || t->current == '>') {
        return StateRetry(StateName::HtmlTextTagOpenBetween);
    }
    Consume(t);
    return StateNext(StateName::HtmlTextTagOpenAttributeValueUnquoted);
}

State HtmlTextTagOpenAttributeValueQuotedAfter(Tokenizer* t) {
    if (t->current == '\t' || t->current == '\n' || t->current == ' ' ||
        t->current == '/' || t->current == '>') {
        return StateRetry(StateName::HtmlTextTagOpenBetween);
    }
    return StateNok();
}

State HtmlTextEnd(Tokenizer* t) {
    if (t->current == '>') {
        Consume(t);
        Exit(t, Name::HtmlTextData);
        Exit(t, Name::HtmlText);
        return StateOk();
    }
    return StateNok();
}

State HtmlTextLineEndingBefore(Tokenizer* t) {
    Exit(t, Name::HtmlTextData);
    Enter(t, Name::LineEnding);
    Consume(t);
    Exit(t, Name::LineEnding);
    return StateNext(StateName::HtmlTextLineEndingAfter);
}

State HtmlTextLineEndingAfter(Tokenizer* t) {
    if (t->current == '\t' || t->current == ' ') {
        TokenizerAttempt(t, StateNext(StateName::HtmlTextLineEndingAfterPrefix),
                         StateNok());
        return StateRetry(SpaceOrTab(t));
    }
    return StateRetry(StateName::HtmlTextLineEndingAfterPrefix);
}

State HtmlTextLineEndingAfterPrefix(Tokenizer* t) {
    Enter(t, Name::HtmlTextData);
    return StateOk();
}

} // namespace markdown
