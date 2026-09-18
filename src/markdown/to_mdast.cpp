/* src/to_mdast.rs — events to a syntax tree.

   Part of the C++ port of markdown-rs 1.0.0 (see src/markdown/readme.md).

   Deviations from the Rust:
     - `Result` is gone. Every error the crate can raise here is about MDX
       tags, and MDX is not ported, so a compile always succeeds. That takes
       `on_mismatch_error` and the event-name check in `tail_pop` with it.
     - `on_exit_media` rewrites the node it finds in place instead of popping
       it and pushing a new one: a `Node` is one struct here, so a link and a
       link reference differ by a field. */

#include "markdown/construct.h"

namespace markdown {

using base::Alloc;

// to_mdast.rs Reference. `reference_kind: Option<ReferenceKind>` is the kind
// plus its flag.
struct Reference {
    ReferenceKind kind = ReferenceKind::Shortcut;
    bool kindSome = true;
    Str identifier = {};
    Str label = {};
};

// One entry of `trees`: the tree being built, the path to the node being
// filled, and the events that opened them.
struct TreeFrame {
    Node* tree = nullptr;
    // The open nodes, root-most first. markdown-rs keeps child *indexes* here
    // and walks down from the tree on every event, because Rust cannot hold a
    // `&mut Node` into a tree it is still building. There is no such rule
    // here, so the stack holds the nodes themselves: an arena allocation does
    // not move, so the pointer stays good for as long as the tree does, and
    // the innermost node is one read off the end of this rather than a walk
    // that indexes `children` once per level.
    ArenaVec<Node*> stack{};
    ArenaVec<int32_t> eventStack{};
};

struct CompileContext {
    Arena* a = nullptr;
    const Vec<Event>* events = nullptr;
    Str bytes = {};
    uint8_t characterReferenceMarker = 0;
    bool gfmTableInside = false;
    bool hardBreakAfter = false;
    bool headingSetextTextAfter = false;
    Vec<Reference> mediaReferenceStack;
    bool rawFlowFenceSeen = false;
    Vec<TreeFrame> trees;
    int32_t index = 0;
};

// `normalize_identifier(..).to_lowercase()`, which is what an identifier on
// the tree is. ASCII only, as in util.h.
static Str IdentifierFrom(Arena* a, Str value) {
    Str id = NormalizeIdentifier(a, value);
    for (int32_t i = 0; i < len(id); i++) {
        if (id.s[i] >= 'A' && id.s[i] <= 'Z') {
            id.s[i] = (char)(id.s[i] + 32);
        }
    }
    return id;
}

// to_mdast.rs `trim_eol`.
static Str TrimEol(Str value, bool atStart, bool atEnd) {
    int32_t start = 0;
    int32_t end = len(value);
    if (atStart && len(value) > 0) {
        if (value.s[0] == '\n') {
            start += 1;
        } else if (value.s[0] == '\r') {
            start += 1;
            if (len(value) > 1 && value.s[1] == '\n') {
                start += 1;
            }
        }
    }
    if (atEnd && end > start) {
        if (value.s[end - 1] == '\n') {
            end -= 1;
            if (end > start && value.s[end - 1] == '\r') {
                end -= 1;
            }
        } else if (value.s[end - 1] == '\r') {
            end -= 1;
        }
    }
    return Str(value.s + start, end - start);
}

// ─── the tree stack ──────────────────────────────────────────────────────

static TreeFrame& TreeTail(CompileContext* c) {
    return c->trees[c->trees.len - 1];
}

// `delve_mut(tree, stack)` in markdown-rs, which walks the whole depth. The
// stack ends at the node it would have arrived at, so both of these are one
// read. An empty stack is the tree itself, which is what a zero-length walk
// came to.
// A node's strings live in a list in the arena, one record each, so storing
// one is the node's own call and not an assignment. `Keep` copies, always:
// the record's header has to sit immediately ahead of the characters, so a
// string already in the arena cannot be pointed at, only copied. What
// NodeToString and IdentifierFrom build ahead of it is a working copy either
// way, so this costs the one copy and nothing that was not already spent.
static void Keep(CompileContext* c, Node* n, NodeStrKind k, Str s) {
    NodeSetStr(c->a, n, k, s);
}

static Str Get(CompileContext* c, const Node* n, NodeStrKind k) {
    return NodeGetStr(c->a, n, k);
}

// A string built a piece at a time. A text node's value arrives one Data
// event at a time and an autolink's URL in two or three parts, and
// concatenating copied the whole of what was there on every one of them —
// so a paragraph of N events left N-1 dead copies in the arena and cost
// O(total^2 / piece) bytes to build. Nothing else allocates between two of
// those events, so the record is still the newest thing in the arena and
// grows in place.
static void Grow(CompileContext* c, Node* n, NodeStrKind k, Str more) {
    NodeGrowStr(c->a, n, k, more);
}

static Node* TailMut(CompileContext* c) {
    TreeFrame& frame = TreeTail(c);
    return frame.stack.len > 0 ? frame.stack[frame.stack.len - 1] : frame.tree;
}

static Node* TailPenultimateMut(CompileContext* c) {
    TreeFrame& frame = TreeTail(c);
    return frame.stack.len > 1 ? frame.stack[frame.stack.len - 2] : frame.tree;
}

static void Buffer(CompileContext* c) {
    TreeFrame frame;
    frame.tree = NodeNew(c->a, NodeKind::Paragraph);
    VecAppend(c->trees, frame);
}

static Node* Resume(CompileContext* c) {
    TreeFrame frame = c->trees[--c->trees.len];
    return frame.tree;
}

static void TailPush(CompileContext* c, Node* child) {
    Node* node = TailMut(c);
    NodeAddChild(c->a, node, child);
    TreeFrame& frame = TreeTail(c);
    frame.stack.Append(c->a, child);
    frame.eventStack.Append(c->a, c->index);
}

// `tail_push` for a node that is already the tail's last child — the text run
// that carries on after a character reference. The caller has the child in
// hand, so nothing has to go looking for it.
static void TailPushAgain(CompileContext* c, Node* child) {
    TreeFrame& frame = TreeTail(c);
    frame.stack.Append(c->a, child);
    frame.eventStack.Append(c->a, c->index);
}

static void TailPop(CompileContext* c) {
    TreeFrame& frame = TreeTail(c);
    frame.stack.Pop();
    frame.eventStack.Pop();
}

// ─── enter ───────────────────────────────────────────────────────────────

static void OnEnterBuffer(CompileContext* c) {
    Buffer(c);
}

static void OnEnterData(CompileContext* c) {
    Node* parent = TailMut(c);
    Node* last = NodeLastChild(c->a, parent);
    if (last && last->kind == NodeKind::Text) {
        TailPushAgain(c, last);
    } else {
        TailPush(c, NodeNew(c->a, NodeKind::Text));
    }
}

static void OnEnterAutolink(CompileContext* c) {
    TailPush(c, NodeNew(c->a, NodeKind::Link));
}

static void OnEnterCodeFenced(CompileContext* c) {
    TailPush(c, NodeNew(c->a, NodeKind::Code));
}

static void OnEnterGfmAutolinkLiteral(CompileContext* c) {
    OnEnterAutolink(c);
    OnEnterData(c);
}

static void OnEnterList(CompileContext* c) {
    Node* node = NodeNew(c->a, NodeKind::List);
    node->Set(NodeOrdered, (*c->events)[c->index].name == Name::ListOrdered);
    node->Set(NodeSpread, ListLoose(*c->events, c->index, false));
    TailPush(c, node);
}

static void OnEnterListItem(CompileContext* c) {
    Node* node = NodeNew(c->a, NodeKind::ListItem);
    node->Set(NodeSpread, ListItemLoose(*c->events, c->index));
    TailPush(c, node);
}

static void OnEnterMedia(CompileContext* c, NodeKind kind) {
    TailPush(c, NodeNew(c->a, kind));
    Reference reference;
    VecAppend(c->mediaReferenceStack, reference);
}

static void Enter(CompileContext* c) {
    switch ((*c->events)[c->index].name) {
        case Name::AutolinkEmail:
        case Name::AutolinkProtocol:
        case Name::CharacterEscapeValue:
        case Name::CharacterReference:
        case Name::CodeFlowChunk:
        case Name::CodeTextData:
        case Name::Data:
        case Name::FrontmatterChunk:
        case Name::HtmlFlowData:
        case Name::HtmlTextData:
        case Name::MathFlowChunk:
        case Name::MathTextData:
            OnEnterData(c);
            break;

        case Name::CodeFencedFenceInfo:
        case Name::CodeFencedFenceMeta:
        case Name::DefinitionDestinationString:
        case Name::DefinitionLabelString:
        case Name::DefinitionTitleString:
        case Name::GfmFootnoteDefinitionLabelString:
        case Name::LabelText:
        case Name::MathFlowFenceMeta:
        case Name::ReferenceString:
        case Name::ResourceDestinationString:
        case Name::ResourceTitleString:
            OnEnterBuffer(c);
            break;

        case Name::Autolink:
            OnEnterAutolink(c);
            break;
        case Name::BlockQuote:
            TailPush(c, NodeNew(c->a, NodeKind::Blockquote));
            break;
        case Name::CodeFenced:
            OnEnterCodeFenced(c);
            break;
        case Name::CodeIndented:
            OnEnterCodeFenced(c);
            OnEnterBuffer(c);
            break;
        case Name::CodeText:
            TailPush(c, NodeNew(c->a, NodeKind::InlineCode));
            Buffer(c);
            break;
        case Name::MathText:
            TailPush(c, NodeNew(c->a, NodeKind::InlineMath));
            Buffer(c);
            break;
        case Name::Definition:
            TailPush(c, NodeNew(c->a, NodeKind::Definition));
            break;
        case Name::Emphasis:
            TailPush(c, NodeNew(c->a, NodeKind::Emphasis));
            break;
        case Name::Frontmatter: {
            int32_t index = (*c->events)[c->index].point.index;
            TailPush(c,
                     NodeNew(c->a, c->bytes.s[index] == '+' ? NodeKind::Toml
                                                            : NodeKind::Yaml));
            Buffer(c);
            break;
        }
        case Name::GfmAutolinkLiteralEmail:
        case Name::GfmAutolinkLiteralMailto:
        case Name::GfmAutolinkLiteralProtocol:
        case Name::GfmAutolinkLiteralWww:
        case Name::GfmAutolinkLiteralXmpp:
            OnEnterGfmAutolinkLiteral(c);
            break;
        case Name::GfmFootnoteCall:
            OnEnterMedia(c, NodeKind::FootnoteReference);
            break;
        case Name::GfmFootnoteDefinition:
            TailPush(c, NodeNew(c->a, NodeKind::FootnoteDefinition));
            break;
        case Name::GfmStrikethrough:
            TailPush(c, NodeNew(c->a, NodeKind::Delete));
            break;
        case Name::GfmTable: {
            Node* node = NodeNew(c->a, NodeKind::Table);
            NodeSetPerKind(c->a, node,
                           GfmTableAlign(*c->events, c->index, c->a));
            TailPush(c, node);
            c->gfmTableInside = true;
            break;
        }
        case Name::GfmTableRow:
            TailPush(c, NodeNew(c->a, NodeKind::TableRow));
            break;
        case Name::GfmTableCell:
            TailPush(c, NodeNew(c->a, NodeKind::TableCell));
            break;
        case Name::HardBreakEscape:
        case Name::HardBreakTrailing:
            TailPush(c, NodeNew(c->a, NodeKind::Break));
            break;
        case Name::HeadingAtx:
        case Name::HeadingSetext:
            // `depth` is set later.
            TailPush(c, NodeNew(c->a, NodeKind::Heading));
            break;
        case Name::HtmlFlow:
        case Name::HtmlText:
            TailPush(c, NodeNew(c->a, NodeKind::Html));
            Buffer(c);
            break;
        case Name::Image:
            OnEnterMedia(c, NodeKind::Image);
            break;
        case Name::Link:
            OnEnterMedia(c, NodeKind::Link);
            break;
        case Name::ListItem:
            OnEnterListItem(c);
            break;
        case Name::ListOrdered:
        case Name::ListUnordered:
            OnEnterList(c);
            break;
        case Name::MathFlow:
            TailPush(c, NodeNew(c->a, NodeKind::Math));
            break;
        case Name::Paragraph:
            TailPush(c, NodeNew(c->a, NodeKind::Paragraph));
            break;
        case Name::Reference:
            c->mediaReferenceStack[c->mediaReferenceStack.len - 1]
                .kind = ReferenceKind::Collapsed;
            c->mediaReferenceStack[c->mediaReferenceStack.len - 1]
                .kindSome = true;
            break;
        case Name::Resource:
            c->mediaReferenceStack[c->mediaReferenceStack.len - 1]
                .kindSome = false;
            break;
        case Name::Strong:
            TailPush(c, NodeNew(c->a, NodeKind::Strong));
            break;
        case Name::ThematicBreak:
            TailPush(c, NodeNew(c->a, NodeKind::ThematicBreak));
            break;
        default:
            break;
    }
}

// ─── exit ────────────────────────────────────────────────────────────────

static Slice ExitSlice(CompileContext* c) {
    Position position = PositionFromExitEvent(*c->events, c->index);
    return SliceFromPosition(c->bytes, position);
}

static void OnExit(CompileContext* c) {
    TailPop(c);
}

static void OnExitData(CompileContext* c) {
    Str value = ExitSlice(c).bytes;
    Node* node = TailMut(c);
    Grow(c, node, NodeStrKind::Value, value);
    OnExit(c);
}

static void OnExitAutolinkProtocol(CompileContext* c) {
    OnExitData(c);
    Str value = ExitSlice(c).bytes;
    Node* link = TailMut(c);
    Grow(c, link, NodeStrKind::Url, value);
}

static void OnExitAutolinkEmail(CompileContext* c) {
    OnExitData(c);
    Str value = ExitSlice(c).bytes;
    Node* link = TailMut(c);
    Grow(c, link, NodeStrKind::Url, StrL("mailto:"));
    Grow(c, link, NodeStrKind::Url, value);
}

static void OnExitCharacterReferenceValue(CompileContext* c) {
    // Decode into the temp arena rather than the node arena: an allocation in
    // the latter would sit between the node's value and the text being
    // appended to it, so the value would stop growing in place.
    base::TempStr value = CharacterReferenceDecodeTemp(
        ExitSlice(c).bytes, c->characterReferenceMarker);
    Node* node = TailMut(c);
    Grow(c, node, NodeStrKind::Value, value);
    c->characterReferenceMarker = 0;
}

static void OnExitRawFlowFence(CompileContext* c) {
    if (!c->rawFlowFenceSeen) {
        Buffer(c);
        c->rawFlowFenceSeen = true;
    }
}

static void OnExitRawFlow(CompileContext* c) {
    Str value = TrimEol(NodeToString(c->a, Resume(c)), true, true);
    Keep(c, TailMut(c), NodeStrKind::Value, value);
    OnExit(c);
    c->rawFlowFenceSeen = false;
}

static void OnExitCodeIndented(CompileContext* c) {
    Str value = TrimEol(NodeToString(c->a, Resume(c)), false, true);
    Keep(c, TailMut(c), NodeStrKind::Value, value);
    OnExit(c);
    c->rawFlowFenceSeen = false;
}

static void OnExitRawText(CompileContext* c) {
    Str value = NodeToString(c->a, Resume(c));

    // In a table cell, `\|` is an escaped pipe.
    if (c->gfmTableInside) {
        int32_t index = 0;
        int32_t n = len(value);
        bool replace = false;
        char* bytes = value.s;
        while (index < n) {
            if (index + 1 < n && bytes[index] == '\\' &&
                bytes[index + 1] == '|') {
                replace = true;
                for (int32_t i = index; i + 1 < n; i++) {
                    bytes[i] = bytes[i + 1];
                }
                n -= 1;
            }
            index += 1;
        }
        if (replace) {
            value.len = n;
            value.s[n] = 0;
        }
    }

    // One space at either end is stripped, unless it is all spaces.
    if (len(value) > 2 && value.s[0] == ' ' && value.s[len(value) - 1] == ' ') {
        bool allSpaces = true;
        for (int32_t i = 0; i < len(value); i++) {
            if (value.s[i] != ' ') {
                allSpaces = false;
                break;
            }
        }
        if (!allSpaces) {
            value = Str(value.s + 1, len(value) - 2);
        }
    }

    Keep(c, TailMut(c), NodeStrKind::Value, value);
    OnExit(c);
}

static void OnExitDefinitionId(CompileContext* c) {
    Str label = NodeToString(c->a, Resume(c));
    Str identifier = IdentifierFrom(c->a, ExitSlice(c).bytes);
    Node* node = TailMut(c);
    Keep(c, node, NodeStrKind::Label, label);
    Keep(c, node, NodeStrKind::Identifier, identifier);
}

static void OnExitGfmAutolinkLiteral(CompileContext* c) {
    OnExitData(c);
    Str value = ExitSlice(c).bytes;
    Name name = (*c->events)[c->index].name;
    Node* link = TailMut(c);
    if (name == Name::GfmAutolinkLiteralEmail) {
        Grow(c, link, NodeStrKind::Url, StrL("mailto:"));
    } else if (name == Name::GfmAutolinkLiteralWww) {
        Grow(c, link, NodeStrKind::Url, StrL("http://"));
    }
    Grow(c, link, NodeStrKind::Url, value);
    OnExit(c);
}

static void OnExitGfmTaskListItemValue(CompileContext* c) {
    bool checked = (*c->events)[c->index]
                       .name == Name::GfmTaskListItemValueChecked;
    Node* ancestor = TailPenultimateMut(c);
    ancestor->Set(NodeChecked, checked);
    ancestor->Set(NodeHasChecked, true);
}

static void OnExitHeadingAtxSequence(CompileContext* c) {
    Node* node = TailMut(c);
    if (NodePerKind(c->a, node) == 0) {
        NodeSetPerKind(c->a, node, (uint32_t)ExitSlice(c).Len());
    }
}

static void OnExitHeadingSetextUnderlineSequence(CompileContext* c) {
    Position position = PositionFromExitEvent(*c->events, c->index);
    uint8_t head = (uint8_t)c->bytes.s[position.start.index];
    NodeSetPerKind(c->a, TailMut(c), head == '-' ? 2 : 1);
}

static void OnExitLabelText(CompileContext* c) {
    Node* fragment = Resume(c);
    Str label = NodeToString(c->a, fragment);
    Str identifier = IdentifierFrom(c->a, ExitSlice(c).bytes);

    Reference& reference =
        c->mediaReferenceStack[c->mediaReferenceStack.len - 1];
    reference.label = label;
    reference.identifier = identifier;

    Node* node = TailMut(c);
    if (node->kind == NodeKind::Link) {
        // The whole ring moves across, which is the one offset naming it.
        node->lastKid = fragment->lastKid;
        fragment->lastKid = ArenaNode{};
    } else if (node->kind == NodeKind::Image) {
        Keep(c, node, NodeStrKind::Alt, label);
    }
}

static void OnExitLineEnding(CompileContext* c) {
    if (c->headingSetextTextAfter) {
        // Ignore.
        return;
    }
    if (c->hardBreakAfter) {
        c->hardBreakAfter = false;
        return;
    }
    NodeKind kind = TailMut(c)->kind;
    if (kind == NodeKind::Emphasis || kind == NodeKind::Heading ||
        kind == NodeKind::Paragraph || kind == NodeKind::Strong ||
        kind == NodeKind::Delete) {
        c->index -= 1;
        OnEnterData(c);
        c->index += 1;
        OnExitData(c);
    }
}

static void OnExitHtml(CompileContext* c) {
    Str value = NodeToString(c->a, Resume(c));
    Keep(c, TailMut(c), NodeStrKind::Value, value);
    OnExit(c);
}

static void OnExitMedia(CompileContext* c) {
    Reference reference = c->mediaReferenceStack[--c->mediaReferenceStack.len];
    OnExit(c);
    if (!reference.kindSome) {
        return;
    }
    Node* parent = TailMut(c);
    Node* node = NodeLastChild(c->a, parent);
    if (node->kind == NodeKind::FootnoteReference) {
        Keep(c, node, NodeStrKind::Identifier, reference.identifier);
        Keep(c, node, NodeStrKind::Label, reference.label);
    } else if (node->kind == NodeKind::Image) {
        node->kind = NodeKind::ImageReference;
        NodeSetRefKind(node, reference.kind);
        Keep(c, node, NodeStrKind::Identifier, reference.identifier);
        Keep(c, node, NodeStrKind::Label, reference.label);
        NodeClearStr(c->a, node, NodeStrKind::Url);
        NodeClearStr(c->a, node, NodeStrKind::Title);
    } else if (node->kind == NodeKind::Link) {
        node->kind = NodeKind::LinkReference;
        NodeSetRefKind(node, reference.kind);
        Keep(c, node, NodeStrKind::Identifier, reference.identifier);
        Keep(c, node, NodeStrKind::Label, reference.label);
        NodeClearStr(c->a, node, NodeStrKind::Url);
        NodeClearStr(c->a, node, NodeStrKind::Title);
    }
}

static void OnExitListItem(CompileContext* c) {
    Node* item = TailMut(c);
    Node* first = NodeFirstChild(c->a, item);
    if (item->Has(NodeHasChecked) && first &&
        first->kind == NodeKind::Paragraph) {
        Node* paragraph = first;
        Node* firstInParagraph = NodeFirstChild(c->a, paragraph);
        if (firstInParagraph && firstInParagraph->kind == NodeKind::Text) {
            Node* text = firstInParagraph;
            Str value = Get(c, text, NodeStrKind::Value);
            int32_t start = 0;
            if (StrStartsWithAny(value, "\t ")) {
                start += 1;
            } else if (StrStartsWithAny(value, "\r\n")) {
                start += 1;
                if (len(value) > 1 && value.s[0] == '\r' &&
                    value.s[1] == '\n') {
                    start += 1;
                }
            }
            if (start == len(value)) {
                // Remove the empty text: the paragraph was only a checkbox.
                // Dropping the first of a ring is the last one's link, or
                // the ring itself when the two are the same node.
                Node* last = NodeLastChild(c->a, paragraph);
                if (last == text) {
                    paragraph->lastKid = ArenaNode{};
                } else {
                    last->sibling = text->sibling;
                }
            } else {
                Keep(c, text, NodeStrKind::Value,
                     Str(value.s + start, len(value) - start));
            }
        }
    }
    OnExit(c);
}

static void OnExitListItemValue(CompileContext* c) {
    Str value = ExitSlice(c).bytes;
    uint32_t start = 0;
    for (int32_t i = 0; i < len(value); i++) {
        start = start * 10 + (uint32_t)(value.s[i] - '0');
    }
    Node* node = TailPenultimateMut(c);
    if (!node->Has(NodeHasStart)) {
        NodeSetPerKind(c->a, node, start);
        node->Set(NodeHasStart, true);
    }
}

static void OnExitReferenceString(CompileContext* c) {
    Str label = NodeToString(c->a, Resume(c));
    Str identifier = IdentifierFrom(c->a, ExitSlice(c).bytes);
    Reference& reference =
        c->mediaReferenceStack[c->mediaReferenceStack.len - 1];
    reference.kind = ReferenceKind::Full;
    reference.kindSome = true;
    reference.label = label;
    reference.identifier = identifier;
}

static void Exit(CompileContext* c) {
    switch ((*c->events)[c->index].name) {
        case Name::Autolink:
        case Name::BlockQuote:
        case Name::CharacterReference:
        case Name::Definition:
        case Name::Emphasis:
        case Name::GfmFootnoteDefinition:
        case Name::GfmStrikethrough:
        case Name::GfmTableRow:
        case Name::GfmTableCell:
        case Name::HeadingAtx:
        case Name::ListOrdered:
        case Name::ListUnordered:
        case Name::Paragraph:
        case Name::Strong:
        case Name::ThematicBreak:
            OnExit(c);
            break;

        case Name::CharacterEscapeValue:
        case Name::CodeFlowChunk:
        case Name::CodeTextData:
        case Name::Data:
        case Name::FrontmatterChunk:
        case Name::HtmlFlowData:
        case Name::HtmlTextData:
        case Name::MathFlowChunk:
        case Name::MathTextData:
            OnExitData(c);
            break;

        case Name::AutolinkProtocol:
            OnExitAutolinkProtocol(c);
            break;
        case Name::AutolinkEmail:
            OnExitAutolinkEmail(c);
            break;
        case Name::CharacterReferenceMarker:
            c->characterReferenceMarker = '&';
            break;
        case Name::CharacterReferenceMarkerNumeric:
            c->characterReferenceMarker = '#';
            break;
        case Name::CharacterReferenceMarkerHexadecimal:
            c->characterReferenceMarker = 'x';
            break;
        case Name::CharacterReferenceValue:
            OnExitCharacterReferenceValue(c);
            break;
        case Name::CodeFencedFenceInfo: {
            // Resume pops the stack TailMut reads, and which of two
            // arguments is evaluated first is the compiler's to
            // choose — so the pop is its own statement.
            Str s = NodeToString(c->a, Resume(c));
            Keep(c, TailMut(c), NodeStrKind::Lang, s);
        } break;
        case Name::CodeFencedFenceMeta:
        case Name::MathFlowFenceMeta: {
            // Resume pops the stack TailMut reads, and which of two
            // arguments is evaluated first is the compiler's to
            // choose — so the pop is its own statement.
            Str s = NodeToString(c->a, Resume(c));
            Keep(c, TailMut(c), NodeStrKind::Meta, s);
        } break;
        case Name::CodeFencedFence:
        case Name::MathFlowFence:
            OnExitRawFlowFence(c);
            break;
        case Name::CodeFenced:
        case Name::MathFlow:
            OnExitRawFlow(c);
            break;
        case Name::CodeIndented:
            OnExitCodeIndented(c);
            break;
        case Name::CodeText:
        case Name::MathText:
            OnExitRawText(c);
            break;
        case Name::DefinitionDestinationString: {
            // Resume pops the stack TailMut reads, and which of two
            // arguments is evaluated first is the compiler's to
            // choose — so the pop is its own statement.
            Str s = NodeToString(c->a, Resume(c));
            Keep(c, TailMut(c), NodeStrKind::Url, s);
        } break;
        case Name::DefinitionLabelString:
        case Name::GfmFootnoteDefinitionLabelString:
            OnExitDefinitionId(c);
            break;
        case Name::DefinitionTitleString: {
            // Resume pops the stack TailMut reads, and which of two
            // arguments is evaluated first is the compiler's to
            // choose — so the pop is its own statement.
            Str s = NodeToString(c->a, Resume(c));
            Keep(c, TailMut(c), NodeStrKind::Title, s);
        } break;
        case Name::Frontmatter: {
            // Resume pops the stack TailMut reads, and which of two
            // arguments is evaluated first is the compiler's to choose —
            // so the pop is its own statement.
            Str s = TrimEol(NodeToString(c->a, Resume(c)), true, true);
            Keep(c, TailMut(c), NodeStrKind::Value, s);
            OnExit(c);
            break;
        }
        case Name::GfmAutolinkLiteralEmail:
        case Name::GfmAutolinkLiteralMailto:
        case Name::GfmAutolinkLiteralProtocol:
        case Name::GfmAutolinkLiteralWww:
        case Name::GfmAutolinkLiteralXmpp:
            OnExitGfmAutolinkLiteral(c);
            break;
        case Name::GfmFootnoteCall:
        case Name::Image:
        case Name::Link:
            OnExitMedia(c);
            break;
        case Name::GfmTable:
            OnExit(c);
            c->gfmTableInside = false;
            break;
        case Name::GfmTaskListItemValueUnchecked:
        case Name::GfmTaskListItemValueChecked:
            OnExitGfmTaskListItemValue(c);
            break;
        case Name::HardBreakEscape:
        case Name::HardBreakTrailing:
            OnExit(c);
            c->hardBreakAfter = true;
            break;
        case Name::HeadingAtxSequence:
            OnExitHeadingAtxSequence(c);
            break;
        case Name::HeadingSetext:
            c->headingSetextTextAfter = false;
            OnExit(c);
            break;
        case Name::HeadingSetextUnderlineSequence:
            OnExitHeadingSetextUnderlineSequence(c);
            break;
        case Name::HeadingSetextText:
            c->headingSetextTextAfter = true;
            break;
        case Name::HtmlFlow:
        case Name::HtmlText:
            OnExitHtml(c);
            break;
        case Name::LabelText:
            OnExitLabelText(c);
            break;
        case Name::LineEnding:
            OnExitLineEnding(c);
            break;
        case Name::ListItem:
            OnExitListItem(c);
            break;
        case Name::ListItemValue:
            OnExitListItemValue(c);
            break;
        case Name::ReferenceString:
            OnExitReferenceString(c);
            break;
        case Name::ResourceDestinationString: {
            // Resume pops the stack TailMut reads, and which of two
            // arguments is evaluated first is the compiler's to
            // choose — so the pop is its own statement.
            Str s = NodeToString(c->a, Resume(c));
            Keep(c, TailMut(c), NodeStrKind::Url, s);
        } break;
        case Name::ResourceTitleString: {
            // Resume pops the stack TailMut reads, and which of two
            // arguments is evaluated first is the compiler's to
            // choose — so the pop is its own statement.
            Str s = NodeToString(c->a, Resume(c));
            Keep(c, TailMut(c), NodeStrKind::Title, s);
        } break;
        default:
            break;
    }
}

Node* ToMdastCompile(const Vec<Event>& events, ParseState* parseState) {
    CompileContext context;
    context.a = parseState->a;
    context.events = &events;
    context.bytes = parseState->bytes;

    TreeFrame frame;
    frame.tree = NodeNew(context.a, NodeKind::Root);
    VecAppend(context.trees, frame);

    int32_t index = 0;
    while (index < len(events)) {
        context.index = index;
        if (events[index].kind == Kind::Enter) {
            Enter(&context);
        } else {
            Exit(&context);
        }
        index += 1;
    }

    return context.trees[0].tree;
}

} // namespace markdown
