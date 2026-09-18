/* The text and string tokenizers, and the small constructs they run.

   | Rust                                | here                  |
   | ----------------------------------- | --------------------- |
   | `construct/text.rs`                 | Text*                 |
   | `construct/string.rs`               | String*               |
   | `construct/attention.rs`            | Attention*            |
   | `construct/autolink.rs`             | Autolink*             |
   | `construct/character_escape.rs`     | CharacterEscape*      |
   | `construct/character_reference.rs`  | CharacterReference*   |
   | `construct/hard_break_escape.rs`    | HardBreakEscape*      |
   | `construct/label_start_image.rs`    | LabelStartImage*      |
   | `construct/label_start_link.rs`     | LabelStartLink*       |

   Part of the C++ port of markdown-rs 1.0.0 (see src/markdown/readme.md). */

#include "markdown/construct.h"

namespace markdown {

// ─── text.rs ─────────────────────────────────────────────────────────────

// text.rs MARKERS.
static const uint8_t kTextMarkers[16] = {
    '!',  // `label_start_image`
    '$',  // `raw_text` (math (text))
    '&',  // `character_reference`
    '*',  // `attention` (emphasis, strong)
    '<',  // `autolink`, `html_text`
    'H',  // `gfm_autolink_literal` (`protocol` kind)
    'W',  // `gfm_autolink_literal` (`www.` kind)
    '[',  // `label_start_link`
    '\\', // `character_escape`, `hard_break_escape`
    ']',  // `label_end`, `gfm_label_start_footnote`
    '_',  // `attention` (emphasis, strong)
    '`',  // `raw_text` (code (text))
    'h',  // `gfm_autolink_literal` (`protocol` kind)
    'w',  // `gfm_autolink_literal` (`www.` kind)
    '{',  // `mdx_expression_text`
    '~',  // `attention` (gfm strikethrough)
};

State TextStart(Tokenizer* t) {
    t->tokenizeState.markers = kTextMarkers;
    t->tokenizeState.markersLen = 16;
    TokenizerAttempt(t, StateNext(StateName::TextBefore),
                     StateNext(StateName::TextBefore));
    return StateRetry(StateName::GfmTaskListItemCheckStart);
}

State TextBefore(Tokenizer* t) {
    switch (t->current) {
        case -1:
            RegisterResolver(t, ResolveName::Data);
            RegisterResolver(t, ResolveName::Text);
            return StateOk();
        case '!':
            TokenizerAttempt(t, StateNext(StateName::TextBefore),
                             StateNext(StateName::TextBeforeData));
            return StateRetry(StateName::LabelStartImageStart);
        case '$':
        case '`':
            TokenizerAttempt(t, StateNext(StateName::TextBefore),
                             StateNext(StateName::TextBeforeData));
            return StateRetry(StateName::RawTextStart);
        case '&':
            TokenizerAttempt(t, StateNext(StateName::TextBefore),
                             StateNext(StateName::TextBeforeData));
            return StateRetry(StateName::CharacterReferenceStart);
        case '*':
        case '_':
        case '~':
            TokenizerAttempt(t, StateNext(StateName::TextBefore),
                             StateNext(StateName::TextBeforeData));
            return StateRetry(StateName::AttentionStart);
        case '<':
            TokenizerAttempt(t, StateNext(StateName::TextBefore),
                             StateNext(StateName::TextBeforeHtml));
            return StateRetry(StateName::AutolinkStart);
        case 'H':
        case 'h':
            TokenizerAttempt(t, StateNext(StateName::TextBefore),
                             StateNext(StateName::TextBeforeData));
            return StateRetry(StateName::GfmAutolinkLiteralProtocolStart);
        case 'W':
        case 'w':
            TokenizerAttempt(t, StateNext(StateName::TextBefore),
                             StateNext(StateName::TextBeforeData));
            return StateRetry(StateName::GfmAutolinkLiteralWwwStart);
        case '[':
            TokenizerAttempt(t, StateNext(StateName::TextBefore),
                             StateNext(StateName::TextBeforeLabelStartLink));
            return StateRetry(StateName::GfmLabelStartFootnoteStart);
        case '\\':
            TokenizerAttempt(t, StateNext(StateName::TextBefore),
                             StateNext(StateName::TextBeforeHardBreakEscape));
            return StateRetry(StateName::CharacterEscapeStart);
        case ']':
            TokenizerAttempt(t, StateNext(StateName::TextBefore),
                             StateNext(StateName::TextBeforeData));
            return StateRetry(StateName::LabelEndStart);
        default:
            // An MDX expression (and any other unknown character), which is
            // not ported, follows the default data path.
            return StateRetry(StateName::TextBeforeData);
    }
}

State TextBeforeHtml(Tokenizer* t) {
    // Rust tries MDX JSX text after HTML text; that construct is not ported.
    TokenizerAttempt(t, StateNext(StateName::TextBefore),
                     StateNext(StateName::TextBeforeData));
    return StateRetry(StateName::HtmlTextStart);
}

State TextBeforeHardBreakEscape(Tokenizer* t) {
    TokenizerAttempt(t, StateNext(StateName::TextBefore),
                     StateNext(StateName::TextBeforeData));
    return StateRetry(StateName::HardBreakEscapeStart);
}

State TextBeforeLabelStartLink(Tokenizer* t) {
    TokenizerAttempt(t, StateNext(StateName::TextBefore),
                     StateNext(StateName::TextBeforeData));
    return StateRetry(StateName::LabelStartLinkStart);
}

State TextBeforeData(Tokenizer* t) {
    TokenizerAttempt(t, StateNext(StateName::TextBefore), StateNok());
    return StateRetry(StateName::DataStart);
}

bool TextResolve(Tokenizer* t, Subresult*) {
    ResolveWhitespace(t, t->parseState->options->constructs.hardBreakTrailing,
                      true);
    if (t->parseState->options->constructs.gfmAutolinkLiteral) {
        GfmAutolinkLiteralResolve(t);
    }
    EditMapConsume(t->map, t->events);
    return false;
}

// ─── string.rs ───────────────────────────────────────────────────────────

static const uint8_t kStringMarkers[2] = {'&', '\\'};

State StringStart(Tokenizer* t) {
    t->tokenizeState.markers = kStringMarkers;
    t->tokenizeState.markersLen = 2;
    return StateRetry(StateName::StringBefore);
}

State StringBefore(Tokenizer* t) {
    if (t->current < 0) {
        RegisterResolver(t, ResolveName::Data);
        RegisterResolver(t, ResolveName::String);
        return StateOk();
    }
    if (t->current == '&') {
        TokenizerAttempt(t, StateNext(StateName::StringBefore),
                         StateNext(StateName::StringBeforeData));
        return StateRetry(StateName::CharacterReferenceStart);
    }
    if (t->current == '\\') {
        TokenizerAttempt(t, StateNext(StateName::StringBefore),
                         StateNext(StateName::StringBeforeData));
        return StateRetry(StateName::CharacterEscapeStart);
    }
    return StateRetry(StateName::StringBeforeData);
}

State StringBeforeData(Tokenizer* t) {
    TokenizerAttempt(t, StateNext(StateName::StringBefore), StateNok());
    return StateRetry(StateName::DataStart);
}

bool StringResolve(Tokenizer* t, Subresult*) {
    ResolveWhitespace(t, false, false);
    return false;
}

// ─── character_escape.rs ─────────────────────────────────────────────────

State CharacterEscapeStart(Tokenizer* t) {
    if (t->parseState->options->constructs.characterEscape &&
        t->current == '\\') {
        Enter(t, Name::CharacterEscape);
        Enter(t, Name::CharacterEscapeMarker);
        Consume(t);
        Exit(t, Name::CharacterEscapeMarker);
        return StateNext(StateName::CharacterEscapeInside);
    }
    return StateNok();
}

State CharacterEscapeInside(Tokenizer* t) {
    if (t->current >= 0 && IsAsciiPunctuation((uint8_t)t->current)) {
        Enter(t, Name::CharacterEscapeValue);
        Consume(t);
        Exit(t, Name::CharacterEscapeValue);
        Exit(t, Name::CharacterEscape);
        return StateOk();
    }
    return StateNok();
}

// ─── character_reference.rs ──────────────────────────────────────────────

State CharacterReferenceStart(Tokenizer* t) {
    if (t->parseState->options->constructs.characterReference &&
        t->current == '&') {
        Enter(t, Name::CharacterReference);
        Enter(t, Name::CharacterReferenceMarker);
        Consume(t);
        Exit(t, Name::CharacterReferenceMarker);
        return StateNext(StateName::CharacterReferenceOpen);
    }
    return StateNok();
}

State CharacterReferenceOpen(Tokenizer* t) {
    if (t->current == '#') {
        Enter(t, Name::CharacterReferenceMarkerNumeric);
        Consume(t);
        Exit(t, Name::CharacterReferenceMarkerNumeric);
        return StateNext(StateName::CharacterReferenceNumeric);
    }
    t->tokenizeState.marker = '&';
    Enter(t, Name::CharacterReferenceValue);
    return StateRetry(StateName::CharacterReferenceValue);
}

State CharacterReferenceNumeric(Tokenizer* t) {
    if (t->current == 'x' || t->current == 'X') {
        Enter(t, Name::CharacterReferenceMarkerHexadecimal);
        Consume(t);
        Exit(t, Name::CharacterReferenceMarkerHexadecimal);
        Enter(t, Name::CharacterReferenceValue);
        t->tokenizeState.marker = 'x';
        return StateNext(StateName::CharacterReferenceValue);
    }
    Enter(t, Name::CharacterReferenceValue);
    t->tokenizeState.marker = '#';
    return StateRetry(StateName::CharacterReferenceValue);
}

State CharacterReferenceValue(Tokenizer* t) {
    if (t->current == ';' && t->tokenizeState.size > 0) {
        if (t->tokenizeState.marker == '&') {
            Slice slice = SliceFromIndices(
                t->parseState->bytes, t->point.index - t->tokenizeState.size,
                t->point.index);
            if (!DecodeNamed(t->parseState->scratch, slice.bytes).s) {
                t->tokenizeState.marker = 0;
                t->tokenizeState.size = 0;
                return StateNok();
            }
        }
        Exit(t, Name::CharacterReferenceValue);
        Enter(t, Name::CharacterReferenceMarkerSemi);
        Consume(t);
        Exit(t, Name::CharacterReferenceMarkerSemi);
        Exit(t, Name::CharacterReference);
        t->tokenizeState.marker = 0;
        t->tokenizeState.size = 0;
        return StateOk();
    }
    if (t->current >= 0 &&
        t->tokenizeState
                .size < CharacterReferenceValueMax(t->tokenizeState.marker) &&
        CharacterReferenceValueTest(t->tokenizeState.marker, (uint8_t)t
                                                                 ->current)) {
        t->tokenizeState.size += 1;
        Consume(t);
        return StateNext(StateName::CharacterReferenceValue);
    }
    t->tokenizeState.marker = 0;
    t->tokenizeState.size = 0;
    return StateNok();
}

// ─── hard_break_escape.rs ────────────────────────────────────────────────

State HardBreakEscapeStart(Tokenizer* t) {
    if (t->parseState->options->constructs.hardBreakEscape &&
        t->current == '\\') {
        Enter(t, Name::HardBreakEscape);
        Consume(t);
        return StateNext(StateName::HardBreakEscapeAfter);
    }
    return StateNok();
}

State HardBreakEscapeAfter(Tokenizer* t) {
    if (t->current == '\n') {
        Exit(t, Name::HardBreakEscape);
        return StateOk();
    }
    return StateNok();
}

// ─── label_start_image.rs ────────────────────────────────────────────────

State LabelStartImageStart(Tokenizer* t) {
    if (t->parseState->options->constructs.labelStartImage &&
        t->current == '!') {
        Enter(t, Name::LabelImage);
        Enter(t, Name::LabelImageMarker);
        Consume(t);
        Exit(t, Name::LabelImageMarker);
        return StateNext(StateName::LabelStartImageOpen);
    }
    return StateNok();
}

State LabelStartImageOpen(Tokenizer* t) {
    if (t->current == '[') {
        Enter(t, Name::LabelMarker);
        Consume(t);
        Exit(t, Name::LabelMarker);
        Exit(t, Name::LabelImage);
        return StateNext(StateName::LabelStartImageAfter);
    }
    return StateNok();
}

State LabelStartImageAfter(Tokenizer* t) {
    // The `^` of a footnote call wins over an image.
    if (t->parseState->options->constructs.gfmLabelStartFootnote &&
        t->current == '^') {
        return StateNok();
    }
    LabelStartMark start;
    start.kind = LabelKind::Image;
    start.startA = t->events.len - 6;
    start.startB = t->events.len - 1;
    VecAppend(t->tokenizeState.labelStarts, start);
    RegisterResolverBefore(t, ResolveName::Label);
    return StateOk();
}

// ─── label_start_link.rs ─────────────────────────────────────────────────

State LabelStartLinkStart(Tokenizer* t) {
    if (t->parseState->options->constructs.labelStartLink &&
        t->current == '[') {
        int32_t start = t->events.len;
        Enter(t, Name::LabelLink);
        Enter(t, Name::LabelMarker);
        Consume(t);
        Exit(t, Name::LabelMarker);
        Exit(t, Name::LabelLink);
        LabelStartMark mark;
        mark.kind = LabelKind::Link;
        mark.startA = start;
        mark.startB = t->events.len - 1;
        VecAppend(t->tokenizeState.labelStarts, mark);
        RegisterResolverBefore(t, ResolveName::Label);
        return StateOk();
    }
    return StateNok();
}

// ─── autolink.rs ─────────────────────────────────────────────────────────

State AutolinkStart(Tokenizer* t) {
    if (t->parseState->options->constructs.autolink && t->current == '<') {
        Enter(t, Name::Autolink);
        Enter(t, Name::AutolinkMarker);
        Consume(t);
        Exit(t, Name::AutolinkMarker);
        Enter(t, Name::AutolinkProtocol);
        return StateNext(StateName::AutolinkOpen);
    }
    return StateNok();
}

State AutolinkOpen(Tokenizer* t) {
    if (t->current >= 0 && IsAsciiAlpha((uint8_t)t->current)) {
        Consume(t);
        return StateNext(StateName::AutolinkSchemeOrEmailAtext);
    }
    if (t->current == '@') {
        return StateNok();
    }
    return StateRetry(StateName::AutolinkEmailAtext);
}

// `+`, `-`, `.` or an ASCII alphanumeric: what a scheme may hold.
static bool IsSchemeByte(int32_t byte) {
    return byte == '+' || byte == '-' || byte == '.' ||
           (byte >= 0 && IsAsciiAlphanumeric((uint8_t)byte));
}

State AutolinkSchemeOrEmailAtext(Tokenizer* t) {
    if (IsSchemeByte(t->current)) {
        // Count the previous byte, the first of the scheme.
        t->tokenizeState.size = 1;
        return StateRetry(StateName::AutolinkSchemeInsideOrEmailAtext);
    }
    return StateRetry(StateName::AutolinkEmailAtext);
}

State AutolinkSchemeInsideOrEmailAtext(Tokenizer* t) {
    if (t->current == ':') {
        Consume(t);
        t->tokenizeState.size = 0;
        return StateNext(StateName::AutolinkUrlInside);
    }
    if (IsSchemeByte(t->current) && t->tokenizeState
                                            .size < kAutolinkSchemeSizeMax) {
        Consume(t);
        t->tokenizeState.size += 1;
        return StateNext(StateName::AutolinkSchemeInsideOrEmailAtext);
    }
    t->tokenizeState.size = 0;
    return StateRetry(StateName::AutolinkEmailAtext);
}

State AutolinkUrlInside(Tokenizer* t) {
    if (t->current == '>') {
        Exit(t, Name::AutolinkProtocol);
        Enter(t, Name::AutolinkMarker);
        Consume(t);
        Exit(t, Name::AutolinkMarker);
        Exit(t, Name::Autolink);
        return StateOk();
    }
    if (t->current < 0 || t->current <= 0x1f || t->current == ' ' ||
        t->current == '<' || t->current == 0x7f) {
        return StateNok();
    }
    Consume(t);
    return StateNext(StateName::AutolinkUrlInside);
}

State AutolinkEmailAtext(Tokenizer* t) {
    if (t->current == '@') {
        Consume(t);
        return StateNext(StateName::AutolinkEmailAtSignOrDot);
    }
    // `#`..`'`, `*`, `+`, `-`..`9`, `=`, `?`, `A`..`Z`, `^`..`~`.
    int32_t c = t->current;
    bool atext = (c >= '#' && c <= '\'') || c == '*' || c == '+' ||
                 (c >= '-' && c <= '9') || c == '=' || c == '?' ||
                 (c >= 'A' && c <= 'Z') || (c >= '^' && c <= '~');
    if (atext) {
        Consume(t);
        return StateNext(StateName::AutolinkEmailAtext);
    }
    return StateNok();
}

State AutolinkEmailAtSignOrDot(Tokenizer* t) {
    if (t->current >= 0 && IsAsciiAlphanumeric((uint8_t)t->current)) {
        return StateRetry(StateName::AutolinkEmailValue);
    }
    return StateNok();
}

State AutolinkEmailLabel(Tokenizer* t) {
    if (t->current == '.') {
        Consume(t);
        t->tokenizeState.size = 0;
        return StateNext(StateName::AutolinkEmailAtSignOrDot);
    }
    if (t->current == '>') {
        int32_t index = t->events.len;
        Exit(t, Name::AutolinkProtocol);
        // Change the event names.
        t->events[index - 1].name = Name::AutolinkEmail;
        t->events[index].name = Name::AutolinkEmail;
        Enter(t, Name::AutolinkMarker);
        Consume(t);
        Exit(t, Name::AutolinkMarker);
        Exit(t, Name::Autolink);
        t->tokenizeState.size = 0;
        return StateOk();
    }
    return StateRetry(StateName::AutolinkEmailValue);
}

State AutolinkEmailValue(Tokenizer* t) {
    bool value = t->current == '-' ||
                 (t->current >= 0 && IsAsciiAlphanumeric((uint8_t)t->current));
    if (value && t->tokenizeState.size < kAutolinkDomainSizeMax) {
        StateName name = t->current == '-' ? StateName::AutolinkEmailValue
                                           : StateName::AutolinkEmailLabel;
        t->tokenizeState.size += 1;
        Consume(t);
        return StateNext(name);
    }
    t->tokenizeState.size = 0;
    return StateNok();
}

// ─── attention.rs ────────────────────────────────────────────────────────

// attention.rs Sequence. `stack` is the enclosing events' indices, from the
// parse's scratch arena, and is only ever read.
struct Sequence {
    uint8_t marker = 0;
    ArenaVec<int32_t> stack{};
    int32_t index = 0;
    Point startPoint = {};
    Point endPoint = {};
    int32_t size = 0;
    bool open = false;
    bool close = false;
};

State AttentionStart(Tokenizer* t) {
    bool emphasis = t->parseState->options->constructs.attention &&
                    (t->current == '*' || t->current == '_');
    bool strikethrough = t->parseState->options->constructs.gfmStrikethrough &&
                         t->current == '~';
    if (emphasis || strikethrough) {
        t->tokenizeState.marker = (uint8_t)t->current;
        Enter(t, Name::AttentionSequence);
        return StateRetry(StateName::AttentionInside);
    }
    return StateNok();
}

State AttentionInside(Tokenizer* t) {
    if (t->current == (int32_t)t->tokenizeState.marker) {
        Consume(t);
        return StateNext(StateName::AttentionInside);
    }
    Exit(t, Name::AttentionSequence);
    RegisterResolver(t, ResolveName::Attention);
    t->tokenizeState.marker = 0;
    return StateOk();
}

static bool StackEq(const ArenaVec<int32_t>& a, const ArenaVec<int32_t>& b) {
    if (len(a) != len(b)) {
        return false;
    }
    // Two walks in step, which is what the pair of cursors is for: `a[i]`
    // and `b[i]` would each start over from their own first segment.
    ArenaVec<int32_t>::Iter ia = a.begin();
    ArenaVec<int32_t>::Iter ib = b.begin();
    for (; ia != a.end(); ++ia, ++ib) {
        if (*ia != *ib) {
            return false;
        }
    }
    return true;
}

static void GetSequences(Tokenizer* t, Vec<Sequence>& sequences) {
    Arena* a = t->parseState->scratch;
    int32_t index = 0;
    ArenaVec<int32_t> stack{};
    while (index < t->events.len) {
        const Event& enter = t->events[index];
        if (enter.name == Name::AttentionSequence) {
            if (enter.kind == Kind::Enter) {
                const Event& exit = t->events[index + 1];
                uint8_t marker = (uint8_t)t->parseState->bytes
                                     .s[enter.point.index];
                int32_t beforeChar =
                    CharBeforeIndex(t->parseState->bytes, enter.point.index);
                CharKind before = Classify(beforeChar);
                int32_t afterChar =
                    CharAfterIndex(t->parseState->bytes, exit.point.index);
                CharKind after = Classify(afterChar);
                bool gfm = t->parseState->options->constructs.gfmStrikethrough;
                bool open =
                    after == CharKind::Other ||
                    (after == CharKind::Punctuation &&
                     before != CharKind::Other) ||
                    (marker != '~' && (afterChar == '*' || afterChar == '_')) ||
                    (marker != '~' && gfm && afterChar == '~');
                bool close = before == CharKind::Other ||
                             (before == CharKind::Punctuation &&
                              after != CharKind::Other) ||
                             (marker != '~' &&
                              (beforeChar == '*' || beforeChar == '_')) ||
                             (marker != '~' && gfm && beforeChar == '~');

                Sequence sequence;
                sequence.index = index;
                // `stack.clone()`.
                for (int32_t eventIndex : stack) {
                    sequence.stack.Append(a, eventIndex);
                }
                sequence.startPoint = enter.point;
                sequence.endPoint = exit.point;
                sequence.size = exit.point.index - enter.point.index;
                sequence.open =
                    marker == '_'
                        ? (open && (before != CharKind::Other || !close))
                        : open;
                sequence.close =
                    marker == '_'
                        ? (close && (after != CharKind::Other || !open))
                        : close;
                sequence.marker = marker;
                VecAppend(sequences, sequence);
            }
        } else if (enter.kind == Kind::Enter) {
            stack.Append(a, index);
        } else if (len(stack) > 0) {
            stack.Pop();
        }
        index += 1;
    }
}

static void SequencesRemove(Vec<Sequence>& sequences, int32_t index) {
    for (int32_t i = index; i + 1 < len(sequences); i++) {
        sequences[i] = sequences[i + 1];
    }
    sequences.len -= 1;
}

static int32_t MatchSequences(Tokenizer* t, Vec<Sequence>& sequences,
                              int32_t open, int32_t close) {
    int32_t next = close;
    // Number of markers to use from the sequence.
    int32_t take =
        (sequences[open].size > 1 && sequences[close].size > 1) ? 2 : 1;

    // Close gaps between the sequences.
    for (int32_t between = open + 1; between < close; between++) {
        sequences[between].open = false;
    }

    Name groupName = Name::Emphasis;
    Name seqName = Name::EmphasisSequence;
    Name textName = Name::EmphasisText;
    if (sequences[open].marker == '~') {
        groupName = Name::GfmStrikethrough;
        seqName = Name::GfmStrikethroughSequence;
        textName = Name::GfmStrikethroughText;
    } else if (take != 1) {
        groupName = Name::Strong;
        seqName = Name::StrongSequence;
        textName = Name::StrongText;
    }

    int32_t openIndex = sequences[open].index;
    int32_t closeIndex = sequences[close].index;
    Point openExit = sequences[open].endPoint;
    Point closeEnter = sequences[close].startPoint;

    sequences[open].size -= take;
    sequences[close].size -= take;
    sequences[open].endPoint.column -= take;
    sequences[open].endPoint.index -= take;
    sequences[close].startPoint.column += take;
    sequences[close].startPoint.index += take;

    Event before[4];
    before[0].kind = Kind::Enter;
    before[0].name = groupName;
    before[0].point = sequences[open].endPoint;
    before[1].kind = Kind::Enter;
    before[1].name = seqName;
    before[1].point = sequences[open].endPoint;
    before[2].kind = Kind::Exit;
    before[2].name = seqName;
    before[2].point = openExit;
    before[3].kind = Kind::Enter;
    before[3].name = textName;
    before[3].point = openExit;
    EditMapAddBefore(t->map, openIndex + 2, 0, before, 4);

    Event after[4];
    after[0].kind = Kind::Exit;
    after[0].name = textName;
    after[0].point = closeEnter;
    after[1].kind = Kind::Enter;
    after[1].name = seqName;
    after[1].point = closeEnter;
    after[2].kind = Kind::Exit;
    after[2].name = seqName;
    after[2].point = sequences[close].startPoint;
    after[3].kind = Kind::Exit;
    after[3].name = groupName;
    after[3].point = sequences[close].startPoint;
    EditMapAdd(t->map, closeIndex, 0, after, 4);

    // Remove the closing sequence if it is now empty.
    if (sequences[close].size == 0) {
        SequencesRemove(sequences, close);
        EditMapAdd(t->map, closeIndex, 2, nullptr, 0);
    } else {
        t->events[closeIndex].point = sequences[close].startPoint;
    }

    if (sequences[open].size == 0) {
        SequencesRemove(sequences, open);
        EditMapAdd(t->map, openIndex, 2, nullptr, 0);
        next -= 1;
    } else {
        t->events[openIndex + 1].point = sequences[open].endPoint;
    }

    return next;
}

bool AttentionResolve(Tokenizer* t, Subresult*) {
    Vec<Sequence> sequences;
    GetSequences(t, sequences);

    int32_t close = 0;
    while (close < len(sequences)) {
        int32_t nextIndex = close + 1;
        if (sequences[close].close) {
            int32_t open = close;
            while (open > 0) {
                open -= 1;
                if (!sequences[open].open ||
                    sequences[close].marker != sequences[open].marker ||
                    !StackEq(sequences[close].stack, sequences[open].stack)) {
                    continue;
                }
                // The `rule of 3`.
                if ((sequences[open].close || sequences[close].open) &&
                    sequences[close].size % 3 != 0 &&
                    (sequences[open].size + sequences[close].size) % 3 == 0) {
                    continue;
                }
                // Strikethrough is stricter.
                if (sequences[close].marker == '~' &&
                    (sequences[close].size != sequences[open].size ||
                     sequences[close].size > 2 ||
                     (sequences[close].size == 1 &&
                      !t->parseState->options->gfmStrikethroughSingleTilde))) {
                    continue;
                }
                nextIndex = MatchSequences(t, sequences, open, close);
                break;
            }
        }
        close = nextIndex;
    }

    // Mark remaining sequences as data.
    for (int32_t index = 0; index < len(sequences); index++) {
        t->events[sequences[index].index].name = Name::Data;
        t->events[sequences[index].index + 1].name = Name::Data;
    }

    EditMapConsume(t->map, t->events);
    return false;
}

} // namespace markdown
