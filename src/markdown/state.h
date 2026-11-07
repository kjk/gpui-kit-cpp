/* src/state.rs — the name of every state function, and what one returns.

   Part of the C++ port of markdown-rs 1.0.0 (see src/markdown/readme.md).

   The crate is a state machine whose states are plain functions taking the
   tokenizer and returning where to go next. Rust names them in an enum and
   dispatches through `state::call`, because a `StateName` is stored in
   attempts and compared; that is what this is, minus the MDX states, whose
   constructs are not ported. Each name is the name of the function that runs
   it, in CamelCase, so `construct::attention::start` is `AttentionStart`.

   `State::Error` is gone with MDX: markdown has no syntax errors, so nothing
   left can fail. */

#ifndef GPUI_MARKDOWN_STATE_H_
#define GPUI_MARKDOWN_STATE_H_

#include "base.h"

namespace markdown {

struct Tokenizer;

// Every state, in the order StateName numbers them and kStateFns (state.cpp)
// dispatches them: one list, so the two cannot drift apart. Each name is
// both a StateName value and the construct function that runs it.
#define GPUI_MARKDOWN_STATES(X)                           \
    X(AttentionStart)                                     \
    X(AttentionInside)                                    \
    X(AutolinkStart)                                      \
    X(AutolinkOpen)                                       \
    X(AutolinkSchemeOrEmailAtext)                         \
    X(AutolinkSchemeInsideOrEmailAtext)                   \
    X(AutolinkUrlInside)                                  \
    X(AutolinkEmailAtSignOrDot)                           \
    X(AutolinkEmailAtext)                                 \
    X(AutolinkEmailValue)                                 \
    X(AutolinkEmailLabel)                                 \
    X(BlankLineStart)                                     \
    X(BlankLineAfter)                                     \
    X(BlockQuoteStart)                                    \
    X(BlockQuoteContStart)                                \
    X(BlockQuoteContBefore)                               \
    X(BlockQuoteContAfter)                                \
    X(BomStart)                                           \
    X(BomInside)                                          \
    X(CharacterEscapeStart)                               \
    X(CharacterEscapeInside)                              \
    X(CharacterReferenceStart)                            \
    X(CharacterReferenceOpen)                             \
    X(CharacterReferenceNumeric)                          \
    X(CharacterReferenceValue)                            \
    X(CodeIndentedStart)                                  \
    X(CodeIndentedAtBreak)                                \
    X(CodeIndentedAfter)                                  \
    X(CodeIndentedFurtherStart)                           \
    X(CodeIndentedInside)                                 \
    X(CodeIndentedFurtherBegin)                           \
    X(CodeIndentedFurtherAfter)                           \
    X(ContentChunkStart)                                  \
    X(ContentChunkInside)                                 \
    X(ContentDefinitionBefore)                            \
    X(ContentDefinitionAfter)                             \
    X(DataStart)                                          \
    X(DataInside)                                         \
    X(DataAtBreak)                                        \
    X(DefinitionStart)                                    \
    X(DefinitionBefore)                                   \
    X(DefinitionLabelAfter)                               \
    X(DefinitionLabelNok)                                 \
    X(DefinitionMarkerAfter)                              \
    X(DefinitionDestinationBefore)                        \
    X(DefinitionDestinationAfter)                         \
    X(DefinitionDestinationMissing)                       \
    X(DefinitionTitleBefore)                              \
    X(DefinitionAfter)                                    \
    X(DefinitionAfterWhitespace)                          \
    X(DefinitionTitleBeforeMarker)                        \
    X(DefinitionTitleAfter)                               \
    X(DefinitionTitleAfterOptionalWhitespace)             \
    X(DestinationStart)                                   \
    X(DestinationEnclosedBefore)                          \
    X(DestinationEnclosed)                                \
    X(DestinationEnclosedEscape)                          \
    X(DestinationRaw)                                     \
    X(DestinationRawEscape)                               \
    X(DocumentStart)                                      \
    X(DocumentBeforeFrontmatter)                          \
    X(DocumentContainerExistingBefore)                    \
    X(DocumentContainerExistingAfter)                     \
    X(DocumentContainerNewBefore)                         \
    X(DocumentContainerNewBeforeNotBlockQuote)            \
    X(DocumentContainerNewBeforeNotList)                  \
    X(DocumentContainerNewBeforeNotGfmFootnoteDefinition) \
    X(DocumentContainerNewAfter)                          \
    X(DocumentContainersAfter)                            \
    X(DocumentFlowInside)                                 \
    X(DocumentFlowEnd)                                    \
    X(FlowStart)                                          \
    X(FlowBeforeGfmTable)                                 \
    X(FlowBeforeCodeIndented)                             \
    X(FlowBeforeRaw)                                      \
    X(FlowBeforeHtml)                                     \
    X(FlowBeforeHeadingAtx)                               \
    X(FlowBeforeHeadingSetext)                            \
    X(FlowBeforeThematicBreak)                            \
    X(FlowAfter)                                          \
    X(FlowBlankLineBefore)                                \
    X(FlowBlankLineAfter)                                 \
    X(FlowBeforeContent)                                  \
    X(FrontmatterStart)                                   \
    X(FrontmatterOpenSequence)                            \
    X(FrontmatterOpenAfter)                               \
    X(FrontmatterAfter)                                   \
    X(FrontmatterContentStart)                            \
    X(FrontmatterContentInside)                           \
    X(FrontmatterContentEnd)                              \
    X(FrontmatterCloseStart)                              \
    X(FrontmatterCloseSequence)                           \
    X(FrontmatterCloseAfter)                              \
    X(GfmAutolinkLiteralProtocolStart)                    \
    X(GfmAutolinkLiteralProtocolAfter)                    \
    X(GfmAutolinkLiteralProtocolPrefixInside)             \
    X(GfmAutolinkLiteralProtocolSlashesInside)            \
    X(GfmAutolinkLiteralWwwStart)                         \
    X(GfmAutolinkLiteralWwwAfter)                         \
    X(GfmAutolinkLiteralWwwPrefixInside)                  \
    X(GfmAutolinkLiteralWwwPrefixAfter)                   \
    X(GfmAutolinkLiteralDomainInside)                     \
    X(GfmAutolinkLiteralDomainAtPunctuation)              \
    X(GfmAutolinkLiteralDomainAfter)                      \
    X(GfmAutolinkLiteralPathInside)                       \
    X(GfmAutolinkLiteralPathAtPunctuation)                \
    X(GfmAutolinkLiteralPathAfter)                        \
    X(GfmAutolinkLiteralTrail)                            \
    X(GfmAutolinkLiteralTrailCharRefInside)               \
    X(GfmAutolinkLiteralTrailCharRefStart)                \
    X(GfmAutolinkLiteralTrailBracketAfter)                \
    X(GfmFootnoteDefinitionStart)                         \
    X(GfmFootnoteDefinitionLabelBefore)                   \
    X(GfmFootnoteDefinitionLabelAtMarker)                 \
    X(GfmFootnoteDefinitionLabelInside)                   \
    X(GfmFootnoteDefinitionLabelEscape)                   \
    X(GfmFootnoteDefinitionLabelAfter)                    \
    X(GfmFootnoteDefinitionWhitespaceAfter)               \
    X(GfmFootnoteDefinitionContStart)                     \
    X(GfmFootnoteDefinitionContBlank)                     \
    X(GfmFootnoteDefinitionContFilled)                    \
    X(GfmLabelStartFootnoteStart)                         \
    X(GfmLabelStartFootnoteOpen)                          \
    X(GfmTaskListItemCheckStart)                          \
    X(GfmTaskListItemCheckInside)                         \
    X(GfmTaskListItemCheckClose)                          \
    X(GfmTaskListItemCheckAfter)                          \
    X(GfmTaskListItemCheckAfterSpaceOrTab)                \
    X(GfmTableStart)                                      \
    X(GfmTableHeadRowBefore)                              \
    X(GfmTableHeadRowStart)                               \
    X(GfmTableHeadRowBreak)                               \
    X(GfmTableHeadRowData)                                \
    X(GfmTableHeadRowEscape)                              \
    X(GfmTableHeadDelimiterStart)                         \
    X(GfmTableHeadDelimiterBefore)                        \
    X(GfmTableHeadDelimiterCellBefore)                    \
    X(GfmTableHeadDelimiterValueBefore)                   \
    X(GfmTableHeadDelimiterLeftAlignmentAfter)            \
    X(GfmTableHeadDelimiterFiller)                        \
    X(GfmTableHeadDelimiterRightAlignmentAfter)           \
    X(GfmTableHeadDelimiterCellAfter)                     \
    X(GfmTableHeadDelimiterNok)                           \
    X(GfmTableBodyRowStart)                               \
    X(GfmTableBodyRowBreak)                               \
    X(GfmTableBodyRowData)                                \
    X(GfmTableBodyRowEscape)                              \
    X(HardBreakEscapeStart)                               \
    X(HardBreakEscapeAfter)                               \
    X(HeadingAtxStart)                                    \
    X(HeadingAtxBefore)                                   \
    X(HeadingAtxSequenceOpen)                             \
    X(HeadingAtxAtBreak)                                  \
    X(HeadingAtxSequenceFurther)                          \
    X(HeadingAtxData)                                     \
    X(HeadingSetextStart)                                 \
    X(HeadingSetextBefore)                                \
    X(HeadingSetextInside)                                \
    X(HeadingSetextAfter)                                 \
    X(HtmlFlowStart)                                      \
    X(HtmlFlowBefore)                                     \
    X(HtmlFlowOpen)                                       \
    X(HtmlFlowDeclarationOpen)                            \
    X(HtmlFlowCommentOpenInside)                          \
    X(HtmlFlowCdataOpenInside)                            \
    X(HtmlFlowTagCloseStart)                              \
    X(HtmlFlowTagName)                                    \
    X(HtmlFlowBasicSelfClosing)                           \
    X(HtmlFlowCompleteClosingTagAfter)                    \
    X(HtmlFlowCompleteEnd)                                \
    X(HtmlFlowCompleteAttributeNameBefore)                \
    X(HtmlFlowCompleteAttributeName)                      \
    X(HtmlFlowCompleteAttributeNameAfter)                 \
    X(HtmlFlowCompleteAttributeValueBefore)               \
    X(HtmlFlowCompleteAttributeValueQuoted)               \
    X(HtmlFlowCompleteAttributeValueQuotedAfter)          \
    X(HtmlFlowCompleteAttributeValueUnquoted)             \
    X(HtmlFlowCompleteAfter)                              \
    X(HtmlFlowBlankLineBefore)                            \
    X(HtmlFlowContinuation)                               \
    X(HtmlFlowContinuationDeclarationInside)              \
    X(HtmlFlowContinuationAfter)                          \
    X(HtmlFlowContinuationStart)                          \
    X(HtmlFlowContinuationBefore)                         \
    X(HtmlFlowContinuationCommentInside)                  \
    X(HtmlFlowContinuationRawTagOpen)                     \
    X(HtmlFlowContinuationRawEndTag)                      \
    X(HtmlFlowContinuationClose)                          \
    X(HtmlFlowContinuationCdataInside)                    \
    X(HtmlFlowContinuationStartNonLazy)                   \
    X(HtmlTextStart)                                      \
    X(HtmlTextOpen)                                       \
    X(HtmlTextDeclarationOpen)                            \
    X(HtmlTextTagCloseStart)                              \
    X(HtmlTextTagClose)                                   \
    X(HtmlTextTagCloseBetween)                            \
    X(HtmlTextTagOpen)                                    \
    X(HtmlTextTagOpenBetween)                             \
    X(HtmlTextTagOpenAttributeName)                       \
    X(HtmlTextTagOpenAttributeNameAfter)                  \
    X(HtmlTextTagOpenAttributeValueBefore)                \
    X(HtmlTextTagOpenAttributeValueQuoted)                \
    X(HtmlTextTagOpenAttributeValueQuotedAfter)           \
    X(HtmlTextTagOpenAttributeValueUnquoted)              \
    X(HtmlTextCdata)                                      \
    X(HtmlTextCdataOpenInside)                            \
    X(HtmlTextCdataClose)                                 \
    X(HtmlTextCdataEnd)                                   \
    X(HtmlTextCommentOpenInside)                          \
    X(HtmlTextComment)                                    \
    X(HtmlTextCommentClose)                               \
    X(HtmlTextCommentEnd)                                 \
    X(HtmlTextDeclaration)                                \
    X(HtmlTextEnd)                                        \
    X(HtmlTextInstruction)                                \
    X(HtmlTextInstructionClose)                           \
    X(HtmlTextLineEndingBefore)                           \
    X(HtmlTextLineEndingAfter)                            \
    X(HtmlTextLineEndingAfterPrefix)                      \
    X(LabelStart)                                         \
    X(LabelAtBreak)                                       \
    X(LabelEolAfter)                                      \
    X(LabelEscape)                                        \
    X(LabelInside)                                        \
    X(LabelNok)                                           \
    X(LabelEndStart)                                      \
    X(LabelEndAfter)                                      \
    X(LabelEndResourceStart)                              \
    X(LabelEndResourceBefore)                             \
    X(LabelEndResourceOpen)                               \
    X(LabelEndResourceDestinationAfter)                   \
    X(LabelEndResourceDestinationMissing)                 \
    X(LabelEndResourceBetween)                            \
    X(LabelEndResourceTitleAfter)                         \
    X(LabelEndResourceEnd)                                \
    X(LabelEndOk)                                         \
    X(LabelEndNok)                                        \
    X(LabelEndReferenceFull)                              \
    X(LabelEndReferenceFullAfter)                         \
    X(LabelEndReferenceFullMissing)                       \
    X(LabelEndReferenceNotFull)                           \
    X(LabelEndReferenceCollapsed)                         \
    X(LabelEndReferenceCollapsedOpen)                     \
    X(LabelStartImageStart)                               \
    X(LabelStartImageOpen)                                \
    X(LabelStartImageAfter)                               \
    X(LabelStartLinkStart)                                \
    X(ListItemStart)                                      \
    X(ListItemBefore)                                     \
    X(ListItemBeforeOrdered)                              \
    X(ListItemBeforeUnordered)                            \
    X(ListItemValue)                                      \
    X(ListItemMarker)                                     \
    X(ListItemMarkerAfter)                                \
    X(ListItemAfter)                                      \
    X(ListItemMarkerAfterFilled)                          \
    X(ListItemWhitespace)                                 \
    X(ListItemPrefixOther)                                \
    X(ListItemWhitespaceAfter)                            \
    X(ListItemContStart)                                  \
    X(ListItemContBlank)                                  \
    X(ListItemContFilled)                                 \
    X(NonLazyContinuationStart)                           \
    X(NonLazyContinuationAfter)                           \
    X(ParagraphStart)                                     \
    X(ParagraphLineStart)                                 \
    X(ParagraphInside)                                    \
    X(RawFlowStart)                                       \
    X(RawFlowBeforeSequenceOpen)                          \
    X(RawFlowSequenceOpen)                                \
    X(RawFlowInfoBefore)                                  \
    X(RawFlowInfo)                                        \
    X(RawFlowMetaBefore)                                  \
    X(RawFlowMeta)                                        \
    X(RawFlowAtNonLazyBreak)                              \
    X(RawFlowCloseStart)                                  \
    X(RawFlowBeforeSequenceClose)                         \
    X(RawFlowSequenceClose)                               \
    X(RawFlowAfterSequenceClose)                          \
    X(RawFlowContentBefore)                               \
    X(RawFlowContentStart)                                \
    X(RawFlowBeforeContentChunk)                          \
    X(RawFlowContentChunk)                                \
    X(RawFlowAfter)                                       \
    X(RawTextStart)                                       \
    X(RawTextSequenceOpen)                                \
    X(RawTextBetween)                                     \
    X(RawTextData)                                        \
    X(RawTextSequenceClose)                               \
    X(SpaceOrTabStart)                                    \
    X(SpaceOrTabInside)                                   \
    X(SpaceOrTabAfter)                                    \
    X(SpaceOrTabEolStart)                                 \
    X(SpaceOrTabEolAfterFirst)                            \
    X(SpaceOrTabEolAfterEol)                              \
    X(SpaceOrTabEolAtEol)                                 \
    X(SpaceOrTabEolAfterMore)                             \
    X(StringStart)                                        \
    X(StringBefore)                                       \
    X(StringBeforeData)                                   \
    X(TextStart)                                          \
    X(TextBefore)                                         \
    X(TextBeforeHtml)                                     \
    X(TextBeforeHardBreakEscape)                          \
    X(TextBeforeLabelStartLink)                           \
    X(TextBeforeData)                                     \
    X(ThematicBreakStart)                                 \
    X(ThematicBreakBefore)                                \
    X(ThematicBreakSequence)                              \
    X(ThematicBreakAtBreak)                               \
    X(TitleStart)                                         \
    X(TitleBegin)                                         \
    X(TitleAfterEol)                                      \
    X(TitleAtBreak)                                       \
    X(TitleEscape)                                        \
    X(TitleInside)                                        \
    X(TitleNok)

enum class StateName : uint16_t {
#define GPUI_MARKDOWN_STATE_NAME(name) name,
    GPUI_MARKDOWN_STATES(GPUI_MARKDOWN_STATE_NAME)
#undef GPUI_MARKDOWN_STATE_NAME
    Count,
};

// state.rs State, minus `Error`. `Next` feeds the next byte to the named
// state; `Retry` calls it with the byte still unconsumed.
struct State {
    enum class Kind : uint8_t {
        Ok,
        Nok,
        Next,
        Retry,
    };

    Kind kind = Kind::Ok;
    StateName name = StateName::AttentionStart;

    bool operator==(const State& other) const {
        if (kind != other.kind) {
            return false;
        }
        if (kind == Kind::Next || kind == Kind::Retry) {
            return name == other.name;
        }
        return true;
    }
    bool operator!=(const State& other) const { return !(*this == other); }
};

inline State StateNext(StateName name) {
    return State{State::Kind::Next, name};
}
inline State StateRetry(StateName name) {
    return State{State::Kind::Retry, name};
}
inline State StateOk() {
    return State{State::Kind::Ok, StateName::AttentionStart};
}
inline State StateNok() {
    return State{State::Kind::Nok, StateName::AttentionStart};
}

// state.rs `call`: run the state of that name.
State Call(Tokenizer* tokenizer, StateName name);

} // namespace markdown

#endif // GPUI_MARKDOWN_STATE_H_
