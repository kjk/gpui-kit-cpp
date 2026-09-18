/* The three tokenizers a document is read through, and the paragraph that is
   what is left when nothing else matches.

   | Rust                        | here          |
   | --------------------------- | ------------- |
   | `construct/document.rs`     | Document*     |
   | `construct/flow.rs`         | Flow*         |
   | `construct/content.rs`      | Content*      |
   | `construct/paragraph.rs`    | Paragraph*    |

   Part of the C++ port of markdown-rs 1.0.0 (see src/markdown/readme.md). */

#include "markdown/construct.h"

namespace markdown {

// ─── document.rs ─────────────────────────────────────────────────────────

// document.rs Phase.
enum class Phase : uint8_t {
    After,
    Prefix,
    Eof,
};

static void ExitContainers(Tokenizer* t, Phase phase);
static void DocumentResolve(Tokenizer* t);

State DocumentStart(Tokenizer* t) {
    t->tokenizeState.documentChild = TokenizerNew(t->point, t->parseState);
    TokenizerAttempt(t, StateNext(StateName::DocumentBeforeFrontmatter),
                     StateNext(StateName::DocumentBeforeFrontmatter));
    return StateRetry(StateName::BomStart);
}

State DocumentBeforeFrontmatter(Tokenizer* t) {
    TokenizerAttempt(t, StateNext(StateName::DocumentContainerNewBefore),
                     StateNext(StateName::DocumentContainerNewBefore));
    return StateRetry(StateName::FrontmatterStart);
}

State DocumentContainerExistingBefore(Tokenizer* t) {
    // If there are more containers, check whether the next one continues.
    if (t->tokenizeState.documentContinued < t->tokenizeState
                                                 .documentContainerStack.len) {
        const ContainerState& container =
            t->tokenizeState
                .documentContainerStack[t->tokenizeState.documentContinued];
        StateName name = StateName::BlockQuoteContStart;
        if (container.kind == Container::GfmFootnoteDefinition) {
            name = StateName::GfmFootnoteDefinitionContStart;
        } else if (container.kind == Container::ListItem) {
            name = StateName::ListItemContStart;
        }
        TokenizerAttempt(t,
                         StateNext(StateName::DocumentContainerExistingAfter),
                         StateNext(StateName::DocumentContainerNewBefore));
        return StateRetry(name);
    }
    // Otherwise, check new containers.
    return StateRetry(StateName::DocumentContainerNewBefore);
}

State DocumentContainerExistingAfter(Tokenizer* t) {
    t->tokenizeState.documentContinued += 1;
    return StateRetry(StateName::DocumentContainerExistingBefore);
}

State DocumentContainerNewBefore(Tokenizer* t) {
    // If we have completely continued, restore the flow's past `interrupt`
    // status.
    if (t->tokenizeState.documentContinued == t->tokenizeState
                                                  .documentContainerStack.len) {
        Tokenizer* child = t->tokenizeState.documentChild;
        t->interrupt = child->interrupt;
        // …and if we're in a concrete construct, new containers can't start.
        if (child->concrete) {
            return StateRetry(StateName::DocumentContainersAfter);
        }
    }

    // Check for a new container. Block quote?
    int32_t tail = t->tokenizeState.documentContainerStack.len;
    ContainerState fresh;
    fresh.kind = Container::BlockQuote;
    VecAppend(t->tokenizeState.documentContainerStack, fresh);
    ContainerState swap =
        t->tokenizeState
            .documentContainerStack[t->tokenizeState.documentContinued];
    t->tokenizeState
        .documentContainerStack[t->tokenizeState.documentContinued] =
        t->tokenizeState.documentContainerStack[tail];
    t->tokenizeState.documentContainerStack[tail] = swap;

    TokenizerAttempt(
        t, StateNext(StateName::DocumentContainerNewAfter),
        StateNext(StateName::DocumentContainerNewBeforeNotBlockQuote));
    return StateRetry(StateName::BlockQuoteStart);
}

State DocumentContainerNewBeforeNotBlockQuote(Tokenizer* t) {
    ContainerState fresh;
    fresh.kind = Container::ListItem;
    t->tokenizeState
        .documentContainerStack[t->tokenizeState.documentContinued] = fresh;
    TokenizerAttempt(t, StateNext(StateName::DocumentContainerNewAfter),
                     StateNext(StateName::DocumentContainerNewBeforeNotList));
    return StateRetry(StateName::ListItemStart);
}

State DocumentContainerNewBeforeNotList(Tokenizer* t) {
    ContainerState fresh;
    fresh.kind = Container::GfmFootnoteDefinition;
    t->tokenizeState
        .documentContainerStack[t->tokenizeState.documentContinued] = fresh;
    TokenizerAttempt(
        t, StateNext(StateName::DocumentContainerNewAfter),
        StateNext(
            StateName::DocumentContainerNewBeforeNotGfmFootnoteDefinition));
    return StateRetry(StateName::GfmFootnoteDefinitionStart);
}

// `Vec::swap_remove`: the last element takes the place of the removed one.
static ContainerState SwapRemove(Vec<ContainerState>& stack, int32_t index) {
    ContainerState out = stack[index];
    stack[index] = stack[len(stack) - 1];
    stack.len -= 1;
    return out;
}

State DocumentContainerNewBeforeNotGfmFootnoteDefinition(Tokenizer* t) {
    SwapRemove(t->tokenizeState.documentContainerStack, t->tokenizeState
                                                            .documentContinued);
    return StateRetry(StateName::DocumentContainersAfter);
}

State DocumentContainerNewAfter(Tokenizer* t) {
    ContainerState container =
        SwapRemove(t->tokenizeState.documentContainerStack,
                   t->tokenizeState.documentContinued);

    // Remove from the event stack. We'll properly add exits at the end.
    if (t->tokenizeState.documentContinued != t->tokenizeState
                                                  .documentContainerStack.len) {
        ExitContainers(t, Phase::Prefix);
    }

    t->tokenizeState.documentChild->pierce = true;
    VecAppend(t->tokenizeState.documentContainerStack, container);
    t->tokenizeState.documentContinued += 1;
    t->interrupt = false;
    return StateRetry(StateName::DocumentContainerNewBefore);
}

State DocumentContainersAfter(Tokenizer* t) {
    Tokenizer* child = t->tokenizeState.documentChild;
    child->lazy = t->tokenizeState.documentContinued !=
                  t->tokenizeState.documentContainerStack.len;
    DefineSkip(child, t->point);

    if (t->current < 0) {
        return StateRetry(StateName::DocumentFlowEnd);
    }
    int32_t current = t->events.len;
    int32_t previous = t->tokenizeState.documentDataIndex;
    if (previous != -1) {
        t->events[previous].link.next = current;
    }
    t->tokenizeState.documentDataIndex = current;
    Link link;
    link.previous = previous;
    link.content = ContentKind::Flow;
    EnterLink(t, Name::Data, link);
    return StateRetry(StateName::DocumentFlowInside);
}

State DocumentFlowInside(Tokenizer* t) {
    if (t->current < 0) {
        Exit(t, Name::Data);
        return StateRetry(StateName::DocumentFlowEnd);
    }
    if (t->current == '\n') {
        Consume(t);
        Exit(t, Name::Data);
        return StateNext(StateName::DocumentFlowEnd);
    }
    Consume(t);
    return StateNext(StateName::DocumentFlowInside);
}

State DocumentFlowEnd(Tokenizer* t) {
    Tokenizer* child = t->tokenizeState.documentChild;
    State state = t->tokenizeState.documentChildStateSome
                      ? t->tokenizeState.documentChildState
                      : StateNext(StateName::FlowStart);
    t->tokenizeState.documentChildStateSome = false;

    ArenaVec<Event> emptyExits{};
    VecAppend(t->tokenizeState.documentExits, emptyExits);

    state = Push(child, child->point.index, child->point.vs, t->point.index,
                 t->point.vs, state);
    t->tokenizeState.documentChildState = state;
    t->tokenizeState.documentChildStateSome = true;

    // If we’re in a lazy line, and the previous (lazy or not) line is
    // something that can be lazily continued, then we can also continue here.
    bool documentLazyContinuationCurrent = false;
    int32_t stackIndex = len(child->stack);
    while (!documentLazyContinuationCurrent && stackIndex > 0) {
        stackIndex -= 1;
        Name name = child->stack[stackIndex];
        if (name == Name::Content || name == Name::GfmTableHead) {
            documentLazyContinuationCurrent = true;
        }
    }
    if (!documentLazyContinuationCurrent && child->events.len > 0) {
        Name lineEnding = Name::LineEnding;
        int32_t before =
            SkipOptBack(child->events, child->events.len - 1, &lineEnding, 1);
        Name name = child->events[before].name;
        if (name == Name::Content || name == Name::HeadingSetextUnderline) {
            documentLazyContinuationCurrent = true;
        }
    }

    child->pierce = false;

    if (child->lazy && t->tokenizeState.documentLazyAcceptingBefore &&
        documentLazyContinuationCurrent) {
        t->tokenizeState.documentContinued = t->tokenizeState
                                                 .documentContainerStack.len;
    }

    if (t->tokenizeState.documentContinued != t->tokenizeState
                                                  .documentContainerStack.len) {
        ExitContainers(t, Phase::After);
    }

    if (t->current < 0) {
        t->tokenizeState.documentContinued = 0;
        ExitContainers(t, Phase::Eof);
        DocumentResolve(t);
        return StateOk();
    }

    t->tokenizeState.documentContinued = 0;
    t->tokenizeState
        .documentLazyAcceptingBefore = documentLazyContinuationCurrent;
    t->interrupt = false;
    return StateRetry(StateName::DocumentContainerExistingBefore);
}

static void ExitContainers(Tokenizer* t, Phase phase) {
    // `Vec::split_off`: what is past `document_continued`.
    Vec<ContainerState> stackClose;
    for (int32_t i = t->tokenizeState.documentContinued;
         i < t->tokenizeState.documentContainerStack.len; i++) {
        VecAppend(stackClose, t->tokenizeState.documentContainerStack[i]);
    }
    t->tokenizeState.documentContainerStack.len = t->tokenizeState
                                                      .documentContinued;

    Tokenizer* child = t->tokenizeState.documentChild;

    // Flush the flow reader.
    if (phase != Phase::After) {
        State state = t->tokenizeState.documentChildStateSome
                          ? t->tokenizeState.documentChildState
                          : StateNext(StateName::FlowStart);
        t->tokenizeState.documentChildStateSome = false;
        Flush(child, state, false);
    }

    if (len(stackClose) > 0) {
        int32_t index = t->tokenizeState.documentExits.len -
                        (phase == Phase::After ? 2 : 1);
        ArenaVec<Event> exits{};
        while (len(stackClose) > 0) {
            ContainerState container = stackClose[--stackClose.len];
            Name name = Name::BlockQuote;
            if (container.kind == Container::GfmFootnoteDefinition) {
                name = Name::GfmFootnoteDefinition;
            } else if (container.kind == Container::ListItem) {
                name = Name::ListItem;
            }
            Event event;
            event.kind = Kind::Exit;
            event.name = name;
            event.point = t->point;
            exits.Append(t->parseState->scratch, event);

            int32_t stackIndex = len(t->stack);
            while (stackIndex > 0) {
                stackIndex -= 1;
                if (t->stack[stackIndex] == name) {
                    for (int32_t i = stackIndex; i + 1 < len(t->stack); i++) {
                        t->stack[i] = t->stack[i + 1];
                    }
                    t->stack.len -= 1;
                    break;
                }
            }
        }
        t->tokenizeState.documentExits[index] = exits;
    }

    child->interrupt = false;
}

// Inject everything together.
static void DocumentResolve(Tokenizer* t) {
    Tokenizer* child = t->tokenizeState.documentChild;

    // First, add the exits to the child tokenizer.
    int32_t childIndex = 0;
    int32_t line = 0;
    while (childIndex < child->events.len) {
        if (child->events[childIndex].kind == Kind::Exit &&
            (child->events[childIndex].name == Name::LineEnding ||
             child->events[childIndex].name == Name::BlankLineEnding)) {
            int32_t injectIndex = childIndex - 1;
            Point point = child->events[injectIndex].point;
            // Move past the exits that follow.
            while (childIndex + 1 < child->events.len &&
                   child->events[childIndex + 1].kind == Kind::Exit) {
                childIndex += 1;
                point = child->events[childIndex].point;
                injectIndex = childIndex + 1;
            }
            if (line < t->tokenizeState.documentExits.len) {
                ArenaVec<Event> exits = t->tokenizeState.documentExits[line];
                if (len(exits) > 0) {
                    t->tokenizeState.documentExits[line] = ArenaVec<Event>{};
                    for (Event& exit : exits) {
                        exit.point = point;
                    }
                    EditMapAdd(child->map, injectIndex, 0,
                               exits.Flatten(t->parseState->scratch),
                               len(exits));
                }
            }
            line += 1;
        }
        childIndex += 1;
    }
    EditMapConsume(child->map, child->events);

    // Now, add the child events into the parent, at the flow data.
    Name data = Name::Data;
    int32_t flowIndex = SkipTo(t->events, 0, &data, 1);
    while (flowIndex < t->events.len &&
           (!t->events[flowIndex].hasLink ||
            t->events[flowIndex].link.content != ContentKind::Flow)) {
        flowIndex = SkipTo(t->events, flowIndex + 1, &data, 1);
    }
    int32_t accA = 0;
    int32_t accB = 0;
    DivideEvents(t->map, t->events, flowIndex, child->events, &accA, &accB);
    EditMapConsume(t->map, t->events);

    // Add the last exits, which are past everything.
    if (line < t->tokenizeState.documentExits.len) {
        ArenaVec<Event> exits = t->tokenizeState.documentExits[line];
        if (len(exits) > 0) {
            t->tokenizeState.documentExits[line] = ArenaVec<Event>{};
            for (Event& exit : exits) {
                exit.point = t->point;
                VecAppend(t->events, exit);
            }
        }
    }

    for (int32_t i = 0; i < child->resolvers.len; i++) {
        VecAppend(t->resolvers, child->resolvers[i]);
    }
    child->resolvers.len = 0;
    for (int32_t i = 0; i < child->tokenizeState.definitions.len; i++) {
        VecAppend(t->tokenizeState.definitions, child->tokenizeState
                                                    .definitions[i]);
    }
    child->tokenizeState.definitions.len = 0;
}

// ─── flow.rs ─────────────────────────────────────────────────────────────

State FlowStart(Tokenizer* t) {
    switch (t->current) {
        case '#':
            TokenizerAttempt(t, StateNext(StateName::FlowAfter),
                             StateNext(StateName::FlowBeforeContent));
            return StateRetry(StateName::HeadingAtxStart);
        // Note: `$` is only used in math (not enabled by default).
        case '$':
        case '`':
        case '~':
            TokenizerAttempt(t, StateNext(StateName::FlowAfter),
                             StateNext(StateName::FlowBeforeContent));
            return StateRetry(StateName::RawFlowStart);
        case '*':
        case '_':
            TokenizerAttempt(t, StateNext(StateName::FlowAfter),
                             StateNext(StateName::FlowBeforeContent));
            return StateRetry(StateName::ThematicBreakStart);
        case '<':
            // Rust tries MDX JSX after HTML here; that construct is not
            // ported, so the fallback is the one that came after it.
            TokenizerAttempt(t, StateNext(StateName::FlowAfter),
                             StateNext(StateName::FlowBeforeHeadingAtx));
            return StateRetry(StateName::HtmlFlowStart);
        case 'e':
        case 'i':
        case '{':
            // `e`/`i` open MDX ESM (`export`, `import`) and `{` an MDX
            // expression. Neither is ported, so both always fail — but their
            // failure goes straight to content, jumping over the whole chain
            // below, and a line starting with one of these three bytes is
            // therefore never a GFM table's header row. That is the crate's
            // behaviour whether or not MDX is on, so it is kept.
            return StateRetry(StateName::FlowBeforeContent);
        default:
            return StateRetry(StateName::FlowBlankLineBefore);
    }
}

State FlowBlankLineBefore(Tokenizer* t) {
    TokenizerAttempt(t, StateNext(StateName::FlowBlankLineAfter),
                     StateNext(StateName::FlowBeforeCodeIndented));
    return StateRetry(StateName::BlankLineStart);
}

State FlowBeforeCodeIndented(Tokenizer* t) {
    TokenizerAttempt(t, StateNext(StateName::FlowAfter),
                     StateNext(StateName::FlowBeforeRaw));
    return StateRetry(StateName::CodeIndentedStart);
}

State FlowBeforeRaw(Tokenizer* t) {
    TokenizerAttempt(t, StateNext(StateName::FlowAfter),
                     StateNext(StateName::FlowBeforeHtml));
    return StateRetry(StateName::RawFlowStart);
}

State FlowBeforeHtml(Tokenizer* t) {
    TokenizerAttempt(t, StateNext(StateName::FlowAfter),
                     StateNext(StateName::FlowBeforeHeadingAtx));
    return StateRetry(StateName::HtmlFlowStart);
}

State FlowBeforeHeadingAtx(Tokenizer* t) {
    TokenizerAttempt(t, StateNext(StateName::FlowAfter),
                     StateNext(StateName::FlowBeforeHeadingSetext));
    return StateRetry(StateName::HeadingAtxStart);
}

State FlowBeforeHeadingSetext(Tokenizer* t) {
    TokenizerAttempt(t, StateNext(StateName::FlowAfter),
                     StateNext(StateName::FlowBeforeThematicBreak));
    return StateRetry(StateName::HeadingSetextStart);
}

State FlowBeforeThematicBreak(Tokenizer* t) {
    TokenizerAttempt(t, StateNext(StateName::FlowAfter),
                     StateNext(StateName::FlowBeforeGfmTable));
    return StateRetry(StateName::ThematicBreakStart);
}

State FlowBeforeGfmTable(Tokenizer* t) {
    TokenizerAttempt(t, StateNext(StateName::FlowAfter),
                     StateNext(StateName::FlowBeforeContent));
    return StateRetry(StateName::GfmTableStart);
}

State FlowBeforeContent(Tokenizer* t) {
    TokenizerAttempt(t, StateNext(StateName::FlowAfter), StateNok());
    return StateRetry(StateName::ContentChunkStart);
}

State FlowBlankLineAfter(Tokenizer* t) {
    if (t->current < 0) {
        return StateOk();
    }
    Enter(t, Name::BlankLineEnding);
    Consume(t);
    Exit(t, Name::BlankLineEnding);
    // Feel free to interrupt.
    t->interrupt = false;
    return StateNext(StateName::FlowStart);
}

State FlowAfter(Tokenizer* t) {
    if (t->current < 0) {
        return StateOk();
    }
    Enter(t, Name::LineEnding);
    Consume(t);
    Exit(t, Name::LineEnding);
    return StateNext(StateName::FlowStart);
}

// ─── content.rs ──────────────────────────────────────────────────────────

State ContentChunkStart(Tokenizer* t) {
    Link link;
    link.content = ContentKind::Content;
    EnterLink(t, Name::Content, link);
    return StateRetry(StateName::ContentChunkInside);
}

State ContentChunkInside(Tokenizer* t) {
    if (t->current < 0 || t->current == '\n') {
        Exit(t, Name::Content);
        RegisterResolverBefore(t, ResolveName::Content);
        // You’d be interrupting.
        t->interrupt = true;
        return StateOk();
    }
    Consume(t);
    return StateNext(StateName::ContentChunkInside);
}

State ContentDefinitionBefore(Tokenizer* t) {
    TokenizerAttempt(t, StateNext(StateName::ContentDefinitionAfter),
                     StateNext(StateName::ParagraphStart));
    return StateRetry(StateName::DefinitionStart);
}

State ContentDefinitionAfter(Tokenizer* t) {
    if (t->current < 0) {
        return StateOk();
    }
    Enter(t, Name::LineEnding);
    Consume(t);
    Exit(t, Name::LineEnding);
    return StateNext(StateName::ContentDefinitionBefore);
}

bool ContentResolve(Tokenizer* t, Subresult* out) {
    int32_t index = 0;
    while (index < t->events.len) {
        const Event& event = t->events[index];
        if (event.kind == Kind::Enter && event.name == Name::Content) {
            int32_t exitIndex = index + 1;
            for (;;) {
                int32_t enterIndex = exitIndex + 1;
                if (enterIndex == t->events.len ||
                    t->events[enterIndex].name != Name::LineEnding) {
                    break;
                }
                // Skip past line ending.
                enterIndex += 2;
                // Skip past prefix.
                while (enterIndex < t->events.len) {
                    Name name = t->events[enterIndex].name;
                    if (name != Name::SpaceOrTab &&
                        name != Name::BlockQuotePrefix &&
                        name != Name::BlockQuoteMarker) {
                        break;
                    }
                    enterIndex += 1;
                }
                if (enterIndex == t->events.len ||
                    t->events[enterIndex].name != Name::Content) {
                    break;
                }
                // Set point at end of prefix.
                t->events[exitIndex].point = t->events[exitIndex + 2].point;
                // Remove the line ending and the content enter.
                EditMapAdd(t->map, exitIndex + 1, 2, nullptr, 0);
                // Link the two content chunks.
                t->events[exitIndex - 1].link.next = enterIndex;
                t->events[enterIndex].link.previous = exitIndex - 1;
                exitIndex = enterIndex + 1;
            }
            index = exitIndex;
        }
        index += 1;
    }
    EditMapConsume(t->map, t->events);
    *out = Subtokenize(t->events, t->parseState, true, ContentKind::Content);
    return true;
}

// ─── paragraph.rs ────────────────────────────────────────────────────────

State ParagraphStart(Tokenizer* t) {
    Enter(t, Name::Paragraph);
    return StateRetry(StateName::ParagraphLineStart);
}

State ParagraphLineStart(Tokenizer* t) {
    Link link;
    link.content = ContentKind::Text;
    EnterLink(t, Name::Data, link);
    if (t->tokenizeState.connect) {
        SubtokenizeLink(t->events, t->events.len - 1);
    } else {
        t->tokenizeState.connect = true;
    }
    return StateRetry(StateName::ParagraphInside);
}

State ParagraphInside(Tokenizer* t) {
    if (t->current < 0) {
        t->tokenizeState.connect = false;
        Exit(t, Name::Data);
        Exit(t, Name::Paragraph);
        return StateOk();
    }
    if (t->current == '\n') {
        Consume(t);
        Exit(t, Name::Data);
        return StateNext(StateName::ParagraphLineStart);
    }
    Consume(t);
    return StateNext(StateName::ParagraphInside);
}

} // namespace markdown
