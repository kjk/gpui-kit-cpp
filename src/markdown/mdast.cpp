/* src/mdast.rs — the tree's few methods.

   Part of the C++ port of markdown-rs 1.0.0 (see src/markdown/readme.md). */

#include "markdown/constant.h"
#include "markdown/mdast.h"

namespace markdown {

using base::Alloc;

Node* NodeNew(Arena* a, NodeKind kind) {
    // Pushed at the node's own alignment and not at the eight `Alloc` uses.
    // A Node is 16 bytes and needs 4, so the two are the same number here —
    // but they were not at 24, where 4-aligned would have saved nothing and
    // 8-aligned handed out 24 for a 20-byte struct.
    void* mem = a->Push(sizeof(Node), alignof(Node), false);
    if (!mem) {
        return nullptr;
    }
    Node* node = new (mem) Node();
    node->kind = kind;
    return node;
}

bool NodeHasChildren(NodeKind kind) {
    switch (kind) {
        case NodeKind::Root:
        case NodeKind::Paragraph:
        case NodeKind::Heading:
        case NodeKind::Blockquote:
        case NodeKind::List:
        case NodeKind::ListItem:
        case NodeKind::Emphasis:
        case NodeKind::Strong:
        case NodeKind::Link:
        case NodeKind::LinkReference:
        case NodeKind::FootnoteDefinition:
        case NodeKind::Table:
        case NodeKind::TableRow:
        case NodeKind::TableCell:
        case NodeKind::Delete:
            return true;
        default:
            return false;
    }
}

// Whether a node's own value is what it contributes to `NodeToString`, or
// whether its children are what it is made of.
static bool NodeHasOwnValue(const Node* node) {
    switch (node->kind) {
        case NodeKind::Toml:
        case NodeKind::Yaml:
        case NodeKind::InlineCode:
        case NodeKind::InlineMath:
        case NodeKind::Html:
        case NodeKind::Text:
        case NodeKind::Code:
        case NodeKind::Math:
            return true;
        default:
            return false;
    }
}

// The lengths are in the words themselves, so the first pass reads no string
// bytes — only the arena lookups the child offsets need.
static int32_t NodeToStringLen(Arena* a, const Node* node) {
    if (NodeHasChildren(node->kind)) {
        int32_t len = 0;
        for (const Node* child : NodeKids(a, node)) {
            len += NodeToStringLen(a, child);
        }
        return len;
    }
    if (!NodeHasOwnValue(node)) {
        return 0;
    }
    return NodeGetStrLen(a, node, NodeStrKind::Value);
}

static int32_t NodeToStringFill(Arena* a, const Node* node, char* out,
                                int32_t at) {
    if (NodeHasChildren(node->kind)) {
        for (const Node* child : NodeKids(a, node)) {
            at = NodeToStringFill(a, child, out, at);
        }
        return at;
    }
    Str value =
        NodeHasOwnValue(node) ? NodeGetStr(a, node, NodeStrKind::Value) : Str{};
    if (len(value) > 0) {
        memcpy(out + at, value.s, (size_t)len(value));
        at += len(value);
    }
    return at;
}

Str NodeToString(Arena* a, const Node* node) {
    // Two passes rather than a builder: the tree is already there, and this
    // way the result is one allocation of exactly the right size.
    int32_t len = NodeToStringLen(a, node);
    char* out = (char*)Alloc(a, len + 1);
    if (!out) {
        return {};
    }
    int32_t at = NodeToStringFill(a, node, out, 0);
    out[at] = 0;
    return Str(out, at);
}

// ─── a node's strings ────────────────────────────────────────────────────
//
// One record a string, in the arena the node is in:
//
//     [u32 next][u8 kind][varint len][len bytes][NUL]
//
// `next` is read and written through memcpy: the records are byte-aligned,
// because rounding each one up to four would give back what the varint and
// the packing just saved, and a misaligned load is the compiler's business
// to know about rather than ours to risk.

constexpr int32_t kRecNext = 0;
constexpr int32_t kRecKind = 4;
constexpr int32_t kRecLen = 5;

static char* RecAt(Arena* a, ArenaStr off) {
    return off == kArenaStrNone ? nullptr : (char*)base::ArenaAtOffset(a, off);
}

static ArenaStr RecNext(const char* rec) {
    ArenaStr next = kArenaStrNone;
    memcpy(&next, rec + kRecNext, sizeof(next));
    return next;
}

static void RecSetNext(char* rec, ArenaStr next) {
    memcpy(rec + kRecNext, &next, sizeof(next));
}

// The bytes, and where the length that measures them was written.
static Str RecStr(const char* rec, int32_t* headOut = nullptr) {
    uint32_t len = 0;
    int32_t head = kRecLen + base::VarintGet(rec + kRecLen, &len);
    if (headOut) {
        *headOut = head;
    }
    return Str((char*)rec + head, (int32_t)len);
}

// The record of this kind, and the one naming it — which a walk has to keep
// hold of, because taking a record out of the list is the predecessor's
// `next` and there is no way back to it.
static char* FindRec(Arena* a, const Node* n, NodeStrKind k, char** prevOut) {
    char* prev = nullptr;
    for (ArenaStr at = n->firstStr; at != kArenaStrNone;) {
        char* rec = RecAt(a, at);
        if (!rec) {
            break;
        }
        if ((NodeStrKind)(uint8_t)rec[kRecKind] == k) {
            if (prevOut) {
                *prevOut = prev;
            }
            return rec;
        }
        prev = rec;
        at = RecNext(rec);
    }
    if (prevOut) {
        *prevOut = nullptr;
    }
    return nullptr;
}

// A record with room for `len` bytes, written into the list's head. The
// bytes themselves are the caller's to fill.
static char* RecNew(Arena* a, Node* n, NodeStrKind k, uint32_t len,
                    int32_t* headOut) {
    int32_t head = kRecLen + base::VarintSize(len);
    // Byte-aligned, like the strings themselves: rounding a record up to
    // four would give back what the varint saved.
    char* rec = (char*)a->Push((uint64_t)head + len + 1, 1, false);
    if (!rec) {
        return nullptr;
    }
    ArenaStr at = (ArenaStr)base::ArenaOffsetOf(a, rec);
    RecSetNext(rec, n->firstStr);
    rec[kRecKind] = (char)(uint8_t)k;
    base::VarintPut(rec + kRecLen, len);
    rec[head + len] = 0;
    n->firstStr = at;
    *headOut = head;
    return rec;
}

Str NodeGetStr(Arena* a, const Node* n, NodeStrKind k) {
    char* rec = FindRec(a, n, k, nullptr);
    return rec ? RecStr(rec) : Str{};
}

int32_t NodeGetStrLen(Arena* a, const Node* n, NodeStrKind k) {
    char* rec = FindRec(a, n, k, nullptr);
    return rec ? len(RecStr(rec)) : 0;
}

bool NodeHasStr(Arena* a, const Node* n, NodeStrKind k) {
    return FindRec(a, n, k, nullptr) != nullptr;
}

void NodeClearStr(Arena* a, Node* n, NodeStrKind k) {
    char* prev = nullptr;
    char* rec = FindRec(a, n, k, &prev);
    if (!rec) {
        return;
    }
    // The record stays where it is; nothing here frees. The node stops
    // naming it, which is all "unset" ever meant.
    if (prev) {
        RecSetNext(prev, RecNext(rec));
    } else {
        n->firstStr = RecNext(rec);
    }
}

void NodeSetStr(Arena* a, Node* n, NodeStrKind k, Str s) {
    if (!a || !n) {
        return;
    }
    NodeClearStr(a, n, k);
    if (!s.s || len(s) <= 0) {
        return;
    }
    int32_t head = 0;
    char* rec = RecNew(a, n, k, (uint32_t)len(s), &head);
    if (rec) {
        memcpy(rec + head, s.s, (size_t)len(s));
    }
}

void NodeGrowStr(Arena* a, Node* n, NodeStrKind k, Str more) {
    if (!a || !n || !more.s || len(more) <= 0) {
        return;
    }
    char* prev = nullptr;
    char* rec = FindRec(a, n, k, &prev);
    if (!rec) {
        NodeSetStr(a, n, k, more);
        return;
    }

    int32_t head = 0;
    Str had = RecStr(rec, &head);
    uint32_t nlen = (uint32_t)len(had) + (uint32_t)len(more);
    int32_t nhead = kRecLen + base::VarintSize(nlen);

    uint64_t used = base::ArenaUsed(a);
    uint64_t end = (uint64_t)base::ArenaOffsetOf(a, rec) + (uint64_t)head +
                   (uint64_t)len(had) + 1;
    // The newest record in the arena grows where it stands: the terminator's
    // own byte is already ours, so only the difference is asked for. A
    // length that has outgrown its varint asks for that byte as well.
    bool newest = end == used;
    uint64_t want = newest ? (uint64_t)(nhead - head) + (uint64_t)len(more)
                           : (uint64_t)nhead + nlen + 1;
    char* dst = (char*)a->Push(want, 1, false);
    if (!dst) {
        return;
    }
    uint64_t at = base::ArenaOffsetOf(a, dst);

    // A push that chained onto a new block is not contiguous after all.
    if (newest && at == used) {
        if (nhead != head) {
            memmove(rec + nhead, rec + head, (size_t)len(had));
        }
        base::VarintPut(rec + kRecLen, nlen);
        memcpy(rec + nhead + len(had), more.s, (size_t)len(more));
        rec[nhead + nlen] = 0;
        return;
    }

    // Somewhere else: the record is rebuilt at the end and the old one taken
    // out of the list, which is what concatenating always did.
    dst[kRecKind] = (char)(uint8_t)k;
    base::VarintPut(dst + kRecLen, nlen);
    memcpy(dst + nhead, had.s, (size_t)len(had));
    memcpy(dst + nhead + len(had), more.s, (size_t)len(more));
    dst[nhead + nlen] = 0;
    if (prev) {
        RecSetNext(prev, RecNext(rec));
    } else {
        n->firstStr = RecNext(rec);
    }
    RecSetNext(dst, n->firstStr);
    n->firstStr = (ArenaStr)at;
}

// The word `kind` decides, in a record of the list above: varint-encoded,
// because a heading's level and a list's start are one byte of it and a
// table's alignments are an arena offset that is usually three. Zero is
// what a node with no record answers, which is what the field read as
// before it was ever written.

uint32_t NodePerKind(Arena* a, const Node* n) {
    char* rec = FindRec(a, n, NodeStrKind::PerKind, nullptr);
    if (!rec) {
        return 0;
    }
    uint32_t word = 0;
    Str bytes = RecStr(rec);
    base::VarintGet(bytes.s, &word);
    return word;
}

void NodeSetPerKind(Arena* a, Node* n, uint32_t word) {
    base::TempStr buf = base::AllocStrTemp(8);
    buf.len = base::VarintPut(buf.s, word);
    NodeSetStr(a, n, NodeStrKind::PerKind, buf);
}

// ─── a Table's alignments ────────────────────────────────────────────────

// Both halves at once: how many columns, and the bytes their codes live in
// one past the count — which has to be decoded to know where that is, the
// price of not spending four bytes on it.
static uint8_t* AlignAt(Arena* a, ArenaAlign al, int32_t* count) {
    *count = 0;
    if (al == kArenaAlignNone) {
        return nullptr;
    }
    char* p = (char*)base::ArenaAtOffset(a, al);
    if (!p) {
        return nullptr;
    }
    uint32_t n = 0;
    int head = base::VarintGet(p, &n);
    *count = (int32_t)n;
    return (uint8_t*)p + head;
}

ArenaAlign ArenaAlignNew(Arena* a, int32_t count) {
    if (!a || count <= 0) {
        return kArenaAlignNone;
    }
    int32_t head = base::VarintSize((uint32_t)count);
    int32_t bytes = (count + 3) / 4;
    // Byte-aligned, like a string: the block is a count and some bits, and
    // rounding it up to eight would give back what the varint just saved.
    char* mem = (char*)a->Push((uint64_t)head + (uint64_t)bytes, 1, false);
    if (!mem) {
        return kArenaAlignNone;
    }
    memset(mem, 0, (size_t)head + (size_t)bytes);
    base::VarintPut(mem, (uint32_t)count);
    return (ArenaAlign)base::ArenaOffsetOf(a, mem);
}

int32_t ArenaAlignCount(Arena* a, ArenaAlign al) {
    int32_t count = 0;
    AlignAt(a, al, &count);
    return count;
}

AlignKind ArenaAlignAt(Arena* a, ArenaAlign al, int32_t i) {
    int32_t count = 0;
    uint8_t* bits = AlignAt(a, al, &count);
    if (!bits || i < 0 || i >= count) {
        return AlignKind::None;
    }
    return (AlignKind)((bits[i / 4] >> ((i % 4) * 2)) & 3);
}

void ArenaAlignSet(Arena* a, ArenaAlign al, int32_t i, AlignKind k) {
    int32_t count = 0;
    uint8_t* bits = AlignAt(a, al, &count);
    if (!bits || i < 0 || i >= count) {
        return;
    }
    int32_t shift = (i % 4) * 2;
    uint8_t was = (uint8_t)(bits[i / 4] & ~(3 << shift));
    bits[i / 4] = (uint8_t)(was | (((uint8_t)k & 3) << shift));
}

// Walking the source once, by the tokenizer's own rules — see the header for
// which of them matter. The two offsets are visited in the one pass, so a
// position costs a scan of the source up to its end and nothing per node.
UnistPosition GetUnistPosition(Str md, uint32_t start, uint32_t end) {
    UnistPosition out;
    int32_t line = 1;
    int32_t column = 1;
    int32_t at = 0;
    int32_t stop = (int32_t)end;
    if (!md.s) {
        return out;
    }
    if (stop > len(md)) {
        stop = len(md);
    }
    bool haveStart = false;
    while (at <= stop) {
        if (!haveStart && at == (int32_t)start) {
            out.start = UnistPoint{line, column, at};
            haveStart = true;
        }
        if (at == stop) {
            break;
        }
        uint8_t byte = (uint8_t)md.s[at];
        if (byte == '\r' && at + 1 < len(md) && md.s[at + 1] == '\n') {
            // Not a character: the LF behind it is the line ending.
            at += 1;
            continue;
        }
        if (byte == '\n' || byte == '\r') {
            line += 1;
            column = 1;
            at += 1;
            continue;
        }
        if (byte == '\t') {
            int32_t remainder = column % kTabSize;
            column += remainder == 0 ? 1 : 1 + kTabSize - remainder;
            at += 1;
            continue;
        }
        column += 1;
        at += 1;
    }
    if (!haveStart) {
        // Past the end of the source, which a well-formed offset is not.
        out.start = UnistPoint{line, column, at};
    }
    out.end = UnistPoint{line, column, at};
    return out;
}

} // namespace markdown
