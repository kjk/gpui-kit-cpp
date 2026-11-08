/* src/construct/ — every state function, and the resolvers that run after.

   Part of the C++ port of markdown-rs 1.0.0 (see src/markdown/readme.md).

   One function per StateName, named exactly as the state is: Rust's
   `construct::attention::start` is `AttentionStart` here, so the dispatcher
   in state.cpp is a switch of identical pairs and a diff against the crate
   applies by name. The seven construct_*.cpp files each hold a handful of
   the crate's construct modules; the readme has the map. */

#ifndef GPUI_MARKDOWN_CONSTRUCT_H_
#define GPUI_MARKDOWN_CONSTRUCT_H_

#include "markdown/tokenizer.h"

namespace markdown {

// The label and title partials take their token names in token1..token3,
// and destination also in token4, token5 and its paren balance in sizeB.
// Every construct that called one puts them back when it returns; Rust
// writes the assignments out at each site, these are them once.
inline void ResetLabelTokens(Tokenizer* t) {
    t->tokenizeState.token1 = Name::Data;
    t->tokenizeState.token2 = Name::Data;
    t->tokenizeState.token3 = Name::Data;
}
inline void ResetDestinationTokens(Tokenizer* t) {
    ResetLabelTokens(t);
    t->tokenizeState.token4 = Name::Data;
    t->tokenizeState.token5 = Name::Data;
    t->tokenizeState.sizeB = 0;
}

// ─── the state functions ─────────────────────────────────────────────────

State AttentionStart(Tokenizer* t);
State AttentionInside(Tokenizer* t);
State AutolinkStart(Tokenizer* t);
State AutolinkOpen(Tokenizer* t);
State AutolinkSchemeOrEmailAtext(Tokenizer* t);
State AutolinkSchemeInsideOrEmailAtext(Tokenizer* t);
State AutolinkUrlInside(Tokenizer* t);
State AutolinkEmailAtSignOrDot(Tokenizer* t);
State AutolinkEmailAtext(Tokenizer* t);
State AutolinkEmailValue(Tokenizer* t);
State AutolinkEmailLabel(Tokenizer* t);
State BlankLineStart(Tokenizer* t);
State BlankLineAfter(Tokenizer* t);
State BlockQuoteStart(Tokenizer* t);
State BlockQuoteContStart(Tokenizer* t);
State BlockQuoteContBefore(Tokenizer* t);
State BlockQuoteContAfter(Tokenizer* t);
State BomStart(Tokenizer* t);
State BomInside(Tokenizer* t);
State CharacterEscapeStart(Tokenizer* t);
State CharacterEscapeInside(Tokenizer* t);
State CharacterReferenceStart(Tokenizer* t);
State CharacterReferenceOpen(Tokenizer* t);
State CharacterReferenceNumeric(Tokenizer* t);
State CharacterReferenceValue(Tokenizer* t);
State CodeIndentedStart(Tokenizer* t);
State CodeIndentedAtBreak(Tokenizer* t);
State CodeIndentedAfter(Tokenizer* t);
State CodeIndentedFurtherStart(Tokenizer* t);
State CodeIndentedInside(Tokenizer* t);
State CodeIndentedFurtherBegin(Tokenizer* t);
State CodeIndentedFurtherAfter(Tokenizer* t);
State ContentChunkStart(Tokenizer* t);
State ContentChunkInside(Tokenizer* t);
State ContentDefinitionBefore(Tokenizer* t);
State ContentDefinitionAfter(Tokenizer* t);
State DataStart(Tokenizer* t);
State DataInside(Tokenizer* t);
State DataAtBreak(Tokenizer* t);
State DefinitionStart(Tokenizer* t);
State DefinitionBefore(Tokenizer* t);
State DefinitionLabelAfter(Tokenizer* t);
State DefinitionLabelNok(Tokenizer* t);
State DefinitionMarkerAfter(Tokenizer* t);
State DefinitionDestinationBefore(Tokenizer* t);
State DefinitionDestinationAfter(Tokenizer* t);
State DefinitionDestinationMissing(Tokenizer* t);
State DefinitionTitleBefore(Tokenizer* t);
State DefinitionAfter(Tokenizer* t);
State DefinitionAfterWhitespace(Tokenizer* t);
State DefinitionTitleBeforeMarker(Tokenizer* t);
State DefinitionTitleAfter(Tokenizer* t);
State DefinitionTitleAfterOptionalWhitespace(Tokenizer* t);
State DestinationStart(Tokenizer* t);
State DestinationEnclosedBefore(Tokenizer* t);
State DestinationEnclosed(Tokenizer* t);
State DestinationEnclosedEscape(Tokenizer* t);
State DestinationRaw(Tokenizer* t);
State DestinationRawEscape(Tokenizer* t);
State DocumentStart(Tokenizer* t);
State DocumentBeforeFrontmatter(Tokenizer* t);
State DocumentContainerExistingBefore(Tokenizer* t);
State DocumentContainerExistingAfter(Tokenizer* t);
State DocumentContainerNewBefore(Tokenizer* t);
State DocumentContainerNewBeforeNotBlockQuote(Tokenizer* t);
State DocumentContainerNewBeforeNotList(Tokenizer* t);
State DocumentContainerNewBeforeNotGfmFootnoteDefinition(Tokenizer* t);
State DocumentContainerNewAfter(Tokenizer* t);
State DocumentContainersAfter(Tokenizer* t);
State DocumentFlowInside(Tokenizer* t);
State DocumentFlowEnd(Tokenizer* t);
State FlowStart(Tokenizer* t);
State FlowBeforeGfmTable(Tokenizer* t);
State FlowBeforeCodeIndented(Tokenizer* t);
State FlowBeforeRaw(Tokenizer* t);
State FlowBeforeHtml(Tokenizer* t);
State FlowBeforeHeadingAtx(Tokenizer* t);
State FlowBeforeHeadingSetext(Tokenizer* t);
State FlowBeforeThematicBreak(Tokenizer* t);
State FlowAfter(Tokenizer* t);
State FlowBlankLineBefore(Tokenizer* t);
State FlowBlankLineAfter(Tokenizer* t);
State FlowBeforeContent(Tokenizer* t);
State FrontmatterStart(Tokenizer* t);
State FrontmatterOpenSequence(Tokenizer* t);
State FrontmatterOpenAfter(Tokenizer* t);
State FrontmatterAfter(Tokenizer* t);
State FrontmatterContentStart(Tokenizer* t);
State FrontmatterContentInside(Tokenizer* t);
State FrontmatterContentEnd(Tokenizer* t);
State FrontmatterCloseStart(Tokenizer* t);
State FrontmatterCloseSequence(Tokenizer* t);
State FrontmatterCloseAfter(Tokenizer* t);
State GfmAutolinkLiteralProtocolStart(Tokenizer* t);
State GfmAutolinkLiteralProtocolAfter(Tokenizer* t);
State GfmAutolinkLiteralProtocolPrefixInside(Tokenizer* t);
State GfmAutolinkLiteralProtocolSlashesInside(Tokenizer* t);
State GfmAutolinkLiteralWwwStart(Tokenizer* t);
State GfmAutolinkLiteralWwwAfter(Tokenizer* t);
State GfmAutolinkLiteralWwwPrefixInside(Tokenizer* t);
State GfmAutolinkLiteralWwwPrefixAfter(Tokenizer* t);
State GfmAutolinkLiteralDomainInside(Tokenizer* t);
State GfmAutolinkLiteralDomainAtPunctuation(Tokenizer* t);
State GfmAutolinkLiteralDomainAfter(Tokenizer* t);
State GfmAutolinkLiteralPathInside(Tokenizer* t);
State GfmAutolinkLiteralPathAtPunctuation(Tokenizer* t);
State GfmAutolinkLiteralPathAfter(Tokenizer* t);
State GfmAutolinkLiteralTrail(Tokenizer* t);
State GfmAutolinkLiteralTrailCharRefInside(Tokenizer* t);
State GfmAutolinkLiteralTrailCharRefStart(Tokenizer* t);
State GfmAutolinkLiteralTrailBracketAfter(Tokenizer* t);
State GfmFootnoteDefinitionStart(Tokenizer* t);
State GfmFootnoteDefinitionLabelBefore(Tokenizer* t);
State GfmFootnoteDefinitionLabelAtMarker(Tokenizer* t);
State GfmFootnoteDefinitionLabelInside(Tokenizer* t);
State GfmFootnoteDefinitionLabelEscape(Tokenizer* t);
State GfmFootnoteDefinitionLabelAfter(Tokenizer* t);
State GfmFootnoteDefinitionWhitespaceAfter(Tokenizer* t);
State GfmFootnoteDefinitionContStart(Tokenizer* t);
State GfmFootnoteDefinitionContBlank(Tokenizer* t);
State GfmFootnoteDefinitionContFilled(Tokenizer* t);
State GfmLabelStartFootnoteStart(Tokenizer* t);
State GfmLabelStartFootnoteOpen(Tokenizer* t);
State GfmTaskListItemCheckStart(Tokenizer* t);
State GfmTaskListItemCheckInside(Tokenizer* t);
State GfmTaskListItemCheckClose(Tokenizer* t);
State GfmTaskListItemCheckAfter(Tokenizer* t);
State GfmTaskListItemCheckAfterSpaceOrTab(Tokenizer* t);
State GfmTableStart(Tokenizer* t);
State GfmTableHeadRowBefore(Tokenizer* t);
State GfmTableHeadRowStart(Tokenizer* t);
State GfmTableHeadRowBreak(Tokenizer* t);
State GfmTableHeadRowData(Tokenizer* t);
State GfmTableHeadRowEscape(Tokenizer* t);
State GfmTableHeadDelimiterStart(Tokenizer* t);
State GfmTableHeadDelimiterBefore(Tokenizer* t);
State GfmTableHeadDelimiterCellBefore(Tokenizer* t);
State GfmTableHeadDelimiterValueBefore(Tokenizer* t);
State GfmTableHeadDelimiterLeftAlignmentAfter(Tokenizer* t);
State GfmTableHeadDelimiterFiller(Tokenizer* t);
State GfmTableHeadDelimiterRightAlignmentAfter(Tokenizer* t);
State GfmTableHeadDelimiterCellAfter(Tokenizer* t);
State GfmTableHeadDelimiterNok(Tokenizer* t);
State GfmTableBodyRowStart(Tokenizer* t);
State GfmTableBodyRowBreak(Tokenizer* t);
State GfmTableBodyRowData(Tokenizer* t);
State GfmTableBodyRowEscape(Tokenizer* t);
State HardBreakEscapeStart(Tokenizer* t);
State HardBreakEscapeAfter(Tokenizer* t);
State HeadingAtxStart(Tokenizer* t);
State HeadingAtxBefore(Tokenizer* t);
State HeadingAtxSequenceOpen(Tokenizer* t);
State HeadingAtxAtBreak(Tokenizer* t);
State HeadingAtxSequenceFurther(Tokenizer* t);
State HeadingAtxData(Tokenizer* t);
State HeadingSetextStart(Tokenizer* t);
State HeadingSetextBefore(Tokenizer* t);
State HeadingSetextInside(Tokenizer* t);
State HeadingSetextAfter(Tokenizer* t);
State HtmlFlowStart(Tokenizer* t);
State HtmlFlowBefore(Tokenizer* t);
State HtmlFlowOpen(Tokenizer* t);
State HtmlFlowDeclarationOpen(Tokenizer* t);
State HtmlFlowCommentOpenInside(Tokenizer* t);
State HtmlFlowCdataOpenInside(Tokenizer* t);
State HtmlFlowTagCloseStart(Tokenizer* t);
State HtmlFlowTagName(Tokenizer* t);
State HtmlFlowBasicSelfClosing(Tokenizer* t);
State HtmlFlowCompleteClosingTagAfter(Tokenizer* t);
State HtmlFlowCompleteEnd(Tokenizer* t);
State HtmlFlowCompleteAttributeNameBefore(Tokenizer* t);
State HtmlFlowCompleteAttributeName(Tokenizer* t);
State HtmlFlowCompleteAttributeNameAfter(Tokenizer* t);
State HtmlFlowCompleteAttributeValueBefore(Tokenizer* t);
State HtmlFlowCompleteAttributeValueQuoted(Tokenizer* t);
State HtmlFlowCompleteAttributeValueQuotedAfter(Tokenizer* t);
State HtmlFlowCompleteAttributeValueUnquoted(Tokenizer* t);
State HtmlFlowCompleteAfter(Tokenizer* t);
State HtmlFlowBlankLineBefore(Tokenizer* t);
State HtmlFlowContinuation(Tokenizer* t);
State HtmlFlowContinuationDeclarationInside(Tokenizer* t);
State HtmlFlowContinuationAfter(Tokenizer* t);
State HtmlFlowContinuationStart(Tokenizer* t);
State HtmlFlowContinuationBefore(Tokenizer* t);
State HtmlFlowContinuationCommentInside(Tokenizer* t);
State HtmlFlowContinuationRawTagOpen(Tokenizer* t);
State HtmlFlowContinuationRawEndTag(Tokenizer* t);
State HtmlFlowContinuationClose(Tokenizer* t);
State HtmlFlowContinuationCdataInside(Tokenizer* t);
State HtmlFlowContinuationStartNonLazy(Tokenizer* t);
State HtmlTextStart(Tokenizer* t);
State HtmlTextOpen(Tokenizer* t);
State HtmlTextDeclarationOpen(Tokenizer* t);
State HtmlTextTagCloseStart(Tokenizer* t);
State HtmlTextTagClose(Tokenizer* t);
State HtmlTextTagCloseBetween(Tokenizer* t);
State HtmlTextTagOpen(Tokenizer* t);
State HtmlTextTagOpenBetween(Tokenizer* t);
State HtmlTextTagOpenAttributeName(Tokenizer* t);
State HtmlTextTagOpenAttributeNameAfter(Tokenizer* t);
State HtmlTextTagOpenAttributeValueBefore(Tokenizer* t);
State HtmlTextTagOpenAttributeValueQuoted(Tokenizer* t);
State HtmlTextTagOpenAttributeValueQuotedAfter(Tokenizer* t);
State HtmlTextTagOpenAttributeValueUnquoted(Tokenizer* t);
State HtmlTextCdata(Tokenizer* t);
State HtmlTextCdataOpenInside(Tokenizer* t);
State HtmlTextCdataClose(Tokenizer* t);
State HtmlTextCdataEnd(Tokenizer* t);
State HtmlTextCommentOpenInside(Tokenizer* t);
State HtmlTextComment(Tokenizer* t);
State HtmlTextCommentClose(Tokenizer* t);
State HtmlTextCommentEnd(Tokenizer* t);
State HtmlTextDeclaration(Tokenizer* t);
State HtmlTextEnd(Tokenizer* t);
State HtmlTextInstruction(Tokenizer* t);
State HtmlTextInstructionClose(Tokenizer* t);
State HtmlTextLineEndingBefore(Tokenizer* t);
State HtmlTextLineEndingAfter(Tokenizer* t);
State HtmlTextLineEndingAfterPrefix(Tokenizer* t);
State LabelStart(Tokenizer* t);
State LabelAtBreak(Tokenizer* t);
State LabelEolAfter(Tokenizer* t);
State LabelEscape(Tokenizer* t);
State LabelInside(Tokenizer* t);
State LabelNok(Tokenizer* t);
State LabelEndStart(Tokenizer* t);
State LabelEndAfter(Tokenizer* t);
State LabelEndResourceStart(Tokenizer* t);
State LabelEndResourceBefore(Tokenizer* t);
State LabelEndResourceOpen(Tokenizer* t);
State LabelEndResourceDestinationAfter(Tokenizer* t);
State LabelEndResourceDestinationMissing(Tokenizer* t);
State LabelEndResourceBetween(Tokenizer* t);
State LabelEndResourceTitleAfter(Tokenizer* t);
State LabelEndResourceEnd(Tokenizer* t);
State LabelEndOk(Tokenizer* t);
State LabelEndNok(Tokenizer* t);
State LabelEndReferenceFull(Tokenizer* t);
State LabelEndReferenceFullAfter(Tokenizer* t);
State LabelEndReferenceFullMissing(Tokenizer* t);
State LabelEndReferenceNotFull(Tokenizer* t);
State LabelEndReferenceCollapsed(Tokenizer* t);
State LabelEndReferenceCollapsedOpen(Tokenizer* t);
State LabelStartImageStart(Tokenizer* t);
State LabelStartImageOpen(Tokenizer* t);
State LabelStartImageAfter(Tokenizer* t);
State LabelStartLinkStart(Tokenizer* t);
State ListItemStart(Tokenizer* t);
State ListItemBefore(Tokenizer* t);
State ListItemBeforeOrdered(Tokenizer* t);
State ListItemBeforeUnordered(Tokenizer* t);
State ListItemValue(Tokenizer* t);
State ListItemMarker(Tokenizer* t);
State ListItemMarkerAfter(Tokenizer* t);
State ListItemAfter(Tokenizer* t);
State ListItemMarkerAfterFilled(Tokenizer* t);
State ListItemWhitespace(Tokenizer* t);
State ListItemPrefixOther(Tokenizer* t);
State ListItemWhitespaceAfter(Tokenizer* t);
State ListItemContStart(Tokenizer* t);
State ListItemContBlank(Tokenizer* t);
State ListItemContFilled(Tokenizer* t);
State NonLazyContinuationStart(Tokenizer* t);
State NonLazyContinuationAfter(Tokenizer* t);
State ParagraphStart(Tokenizer* t);
State ParagraphLineStart(Tokenizer* t);
State ParagraphInside(Tokenizer* t);
State RawFlowStart(Tokenizer* t);
State RawFlowBeforeSequenceOpen(Tokenizer* t);
State RawFlowSequenceOpen(Tokenizer* t);
State RawFlowInfoBefore(Tokenizer* t);
State RawFlowInfo(Tokenizer* t);
State RawFlowMetaBefore(Tokenizer* t);
State RawFlowMeta(Tokenizer* t);
State RawFlowAtNonLazyBreak(Tokenizer* t);
State RawFlowCloseStart(Tokenizer* t);
State RawFlowBeforeSequenceClose(Tokenizer* t);
State RawFlowSequenceClose(Tokenizer* t);
State RawFlowAfterSequenceClose(Tokenizer* t);
State RawFlowContentBefore(Tokenizer* t);
State RawFlowContentStart(Tokenizer* t);
State RawFlowBeforeContentChunk(Tokenizer* t);
State RawFlowContentChunk(Tokenizer* t);
State RawFlowAfter(Tokenizer* t);
State RawTextStart(Tokenizer* t);
State RawTextSequenceOpen(Tokenizer* t);
State RawTextBetween(Tokenizer* t);
State RawTextData(Tokenizer* t);
State RawTextSequenceClose(Tokenizer* t);
State SpaceOrTabStart(Tokenizer* t);
State SpaceOrTabInside(Tokenizer* t);
State SpaceOrTabAfter(Tokenizer* t);
State SpaceOrTabEolStart(Tokenizer* t);
State SpaceOrTabEolAfterFirst(Tokenizer* t);
State SpaceOrTabEolAfterEol(Tokenizer* t);
State SpaceOrTabEolAtEol(Tokenizer* t);
State SpaceOrTabEolAfterMore(Tokenizer* t);
State StringStart(Tokenizer* t);
State StringBefore(Tokenizer* t);
State StringBeforeData(Tokenizer* t);
State TextStart(Tokenizer* t);
State TextBefore(Tokenizer* t);
State TextBeforeHtml(Tokenizer* t);
State TextBeforeHardBreakEscape(Tokenizer* t);
State TextBeforeLabelStartLink(Tokenizer* t);
State TextBeforeData(Tokenizer* t);
State ThematicBreakStart(Tokenizer* t);
State ThematicBreakBefore(Tokenizer* t);
State ThematicBreakSequence(Tokenizer* t);
State ThematicBreakAtBreak(Tokenizer* t);
State TitleStart(Tokenizer* t);
State TitleBegin(Tokenizer* t);
State TitleAfterEol(Tokenizer* t);
State TitleAtBreak(Tokenizer* t);
State TitleEscape(Tokenizer* t);
State TitleInside(Tokenizer* t);
State TitleNok(Tokenizer* t);

// ─── partial_space_or_tab.rs ─────────────────────────────────────────────

// Its Options, and `usize::MAX` as the max.
constexpr int32_t kSizeMax = 0x7fffffff;

struct SpaceOrTabOptions {
    int32_t min = 0;
    int32_t max = 0;
    Name kind = Name::SpaceOrTab;
    bool connect = false;
    ContentKind content = ContentKind::Flow;
    bool contentSome = false;
};

StateName SpaceOrTab(Tokenizer* t);
StateName SpaceOrTabMinMax(Tokenizer* t, int32_t min, int32_t max);
StateName SpaceOrTabWithOptions(Tokenizer* t, const SpaceOrTabOptions& options);

// ─── partial_space_or_tab_eol.rs ─────────────────────────────────────────

struct SpaceOrTabEolOptions {
    bool connect = false;
    ContentKind content = ContentKind::Flow;
    bool contentSome = false;
};

StateName SpaceOrTabEol(Tokenizer* t);
StateName SpaceOrTabEolWithOptions(Tokenizer* t,
                                   const SpaceOrTabEolOptions& options);

// ─── partial_whitespace.rs ───────────────────────────────────────────────

void ResolveWhitespace(Tokenizer* t, bool hardBreak, bool trimWhole);

// ─── gfm_autolink_literal.rs ─────────────────────────────────────────────

// Not a registered resolver: text.rs calls it from its own.
void GfmAutolinkLiteralResolve(Tokenizer* t);

// ─── the resolvers ───────────────────────────────────────────────────────
//
// Rust returns `Option<Subresult>`; only content's ever has one, so these
// return whether they filled `out`.

bool LabelEndResolve(Tokenizer* t, Subresult* out);
bool AttentionResolve(Tokenizer* t, Subresult* out);
bool GfmTableResolve(Tokenizer* t, Subresult* out);
bool HeadingAtxResolve(Tokenizer* t, Subresult* out);
bool HeadingSetextResolve(Tokenizer* t, Subresult* out);
bool ListItemResolve(Tokenizer* t, Subresult* out);
bool ContentResolve(Tokenizer* t, Subresult* out);
bool DataResolve(Tokenizer* t, Subresult* out);
bool StringResolve(Tokenizer* t, Subresult* out);
bool TextResolve(Tokenizer* t, Subresult* out);

} // namespace markdown

#endif // GPUI_MARKDOWN_CONSTRUCT_H_
