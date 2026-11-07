#ifndef GPUI_HTML5EVER_HTML5EVER_H_
#define GPUI_HTML5EVER_HTML5EVER_H_
/* A C++ port of html5ever 0.27's public parsing model.

   The Rust crate is generic over a TreeSink. This port keeps the useful half
   of that seam: Tokenize sends POD tokens to a callback, while ParseDocument
   and ParseFragment build an arena-owned DOM. Strings and nodes have the
   arena's lifetime. Their stored strings and links are arena offsets; use the
   accessors below with that arena when walking the public model. */

#include "base.h"

namespace html5ever {

using base::Arena;
using base::ArenaPtr;
using base::ArenaPtrGet;
using base::ArenaStr;
using base::ArenaStrGet;
using base::ArenaVec;
using base::Str;

enum class Namespace : uint8_t {
    Html,
    MathMl,
    Svg,
};

enum class NodeKind : uint8_t {
    Document,
    Doctype,
    Text,
    Comment,
    Element,
};

struct Attribute {
    ArenaStr name = {};
    ArenaStr value = {};
    Namespace ns = Namespace::Html;
    ArenaPtr<Attribute> next = {};
};

struct Node {
    NodeKind kind = NodeKind::Document;
    Namespace ns = Namespace::Html;
    ArenaStr name = {};
    // Text/comment contents, or a doctype's public identifier.
    ArenaStr data = {};
    // A doctype's system identifier.
    ArenaStr systemId = {};
    ArenaPtr<Attribute> attrs = {};
    ArenaPtr<Node> parent = {};
    ArenaPtr<Node> first = {};
    ArenaPtr<Node> last = {};
    ArenaPtr<Node> next = {};
    // Parser-created html/head/body/tbody nodes are distinguishable from
    // source elements. Consumers which project rather than serialize a DOM
    // can omit those wrappers without losing an explicit source wrapper.
    bool implicit = false;
};

enum class TokenKind : uint8_t {
    Eof,
    ParseError,
    Character,
    NullCharacter,
    Comment,
    Doctype,
    StartTag,
    EndTag,
};

struct Token {
    int line = 1;
    ArenaStr name = {};
    ArenaStr data = {};
    ArenaStr systemId = {};
    ArenaPtr<Attribute> attrs = {};
    TokenKind kind = TokenKind::Eof;
    bool selfClosing = false;
    bool forceQuirks = false;
};

using TokenSink = void (*)(void* user, const Token* token);

struct TokenizerOptions {
    bool exactErrors = false;
    bool discardBom = true;
};

struct ParseOptions {
    TokenizerOptions tokenizer = {};
    bool exactErrors = false;
    bool scriptingEnabled = true;
    bool iframeSrcdoc = false;
    bool dropDoctype = false;
};

struct SerializeOptions {
    bool includeNode = false;
    bool createMissingParent = true;
};

void Tokenize(Arena* a, Str source, TokenSink sink, void* user = nullptr,
              TokenizerOptions options = {});
Node* ParseDocument(Arena* a, Str source, ParseOptions options = {});
Node* ParseFragment(Arena* a, Str source, Str context = Str{},
                    ParseOptions options = {});
Str Serialize(Arena* a, const Node* node, SerializeOptions options = {});

// Incremental tendril feed. Process appends a chunk and tokenizes every
// complete token; Finish emits EOF and returns the document. When
// scriptingEnabled, the parser pauses after a </script> so the caller can
// run the script (or not) and ResumeAfterCurrentScript continues.
struct Parser {
    Arena* a = nullptr;
    ParseOptions options = {};
    void* impl = nullptr;
};

Parser* ParserNew(Arena* a, ParseOptions options = {});
Parser* ParserNewFragment(Arena* a, Str context, ParseOptions options = {});
void ParserProcess(Parser* parser, Str chunk);
bool ParserIsPaused(const Parser* parser);
void ParserResumeAfterCurrentScript(Parser* parser);
Node* ParserFinish(Parser* parser);

inline Str AttributeName(Arena* a, const Attribute* attr) {
    return attr ? ArenaStrGet(a, attr->name) : Str{};
}
inline Str AttributeValue(Arena* a, const Attribute* attr) {
    return attr ? ArenaStrGet(a, attr->value) : Str{};
}
inline Attribute* AttributeNext(Arena* a, Attribute* attr) {
    return attr ? ArenaPtrGet(a, attr->next) : nullptr;
}
inline const Attribute* AttributeNext(Arena* a, const Attribute* attr) {
    return attr ? ArenaPtrGet(a, attr->next) : nullptr;
}

inline Str NodeName(Arena* a, const Node* node) {
    return node ? ArenaStrGet(a, node->name) : Str{};
}
inline Str NodeData(Arena* a, const Node* node) {
    return node ? ArenaStrGet(a, node->data) : Str{};
}
inline Str NodeSystemId(Arena* a, const Node* node) {
    return node ? ArenaStrGet(a, node->systemId) : Str{};
}
inline Attribute* NodeAttrs(Arena* a, Node* node) {
    return node ? ArenaPtrGet(a, node->attrs) : nullptr;
}
inline const Attribute* NodeAttrs(Arena* a, const Node* node) {
    return node ? ArenaPtrGet(a, node->attrs) : nullptr;
}
#define GPUI_HTML5EVER_NODE_LINK(Name, field)                   \
    inline Node* Node##Name(Arena* a, Node* node) {             \
        return node ? ArenaPtrGet(a, node->field) : nullptr;    \
    }                                                           \
    inline const Node* Node##Name(Arena* a, const Node* node) { \
        return node ? ArenaPtrGet(a, node->field) : nullptr;    \
    }
GPUI_HTML5EVER_NODE_LINK(Parent, parent)
GPUI_HTML5EVER_NODE_LINK(First, first)
GPUI_HTML5EVER_NODE_LINK(Last, last)
GPUI_HTML5EVER_NODE_LINK(Next, next)
#undef GPUI_HTML5EVER_NODE_LINK

inline Str TokenName(Arena* a, const Token* token) {
    return token ? ArenaStrGet(a, token->name) : Str{};
}
inline Str TokenData(Arena* a, const Token* token) {
    return token ? ArenaStrGet(a, token->data) : Str{};
}
inline Str TokenSystemId(Arena* a, const Token* token) {
    return token ? ArenaStrGet(a, token->systemId) : Str{};
}
inline const Attribute* TokenAttrs(Arena* a, const Token* token) {
    return token ? ArenaPtrGet(a, token->attrs) : nullptr;
}

// The node's attribute called `name` (ASCII case-insensitive), or null. Here
// rather than in a .cpp so html5ever-mini, which implements this header too,
// shares them.
inline const Attribute* Attr(Arena* a, const Node* node, Str name) {
    for (const Attribute* attr = NodeAttrs(a, node); attr;
         attr = AttributeNext(a, attr)) {
        if (base::StrEqI(AttributeName(a, attr), name)) return attr;
    }
    return nullptr;
}
inline Str AttrValue(Arena* a, const Node* node, Str name) {
    return AttributeValue(a, Attr(a, node, name));
}

// The rest of what this crate and html5ever-mini, which implements this
// header too, would otherwise each write out: the tokenizer's input, tree
// building's node constructor and append, the numeric character reference's
// digits, and the serializer's tags.

// What Tokenize is handed and its place in the source: the head of either
// tokenizer's state.
struct TokenizerInput {
    Arena* a = nullptr;
    Str source = {};
    int at = 0;
    int line = 1;
    TokenSink sink = nullptr;
    void* user = nullptr;
    TokenizerOptions options = {};

    void Start(Arena* arena, Str text, TokenSink to, void* data,
               TokenizerOptions opts) {
        a = arena;
        source = text;
        sink = to;
        user = data;
        options = opts;
    }
    void Emit(const Token& token) const {
        if (sink) sink(user, &token);
    }
};

inline Node* NodeNew(Arena* a, NodeKind kind, Str name = {}) {
    Node* node = base::ArenaNew<Node>(a);
    node->kind = kind;
    node->name = base::ArenaStrDup(a, name);
    return node;
}

// `child` becomes the last child of `parent`.
inline void NodeAppend(Arena* a, Node* parent, Node* child) {
    child->parent = base::ArenaPtrOf(a, parent);
    child->next = {};
    Node* last = NodeLast(a, parent);
    if (last)
        last->next = base::ArenaPtrOf(a, child);
    else
        parent->first = base::ArenaPtrOf(a, child);
    parent->last = base::ArenaPtrOf(a, child);
}

// The digits of a numeric character reference, `at` just past "&#": an `x`
// or `X` first makes it hex. Sets the radix and where the digits start and
// returns where they end, which is the start when there are none.
inline int NumericCharRefDigits(Str s, int at, int* radix, int* digits) {
    *radix = 10;
    if (at < len(s) && (s.s[at] == 'x' || s.s[at] == 'X')) {
        *radix = 16;
        at++;
    }
    *digits = at;
    while (at < len(s)) {
        int d = base::HexDigitValue(s.s[at]);
        if (d < 0 || d >= *radix) break;
        at++;
    }
    return at;
}

// `<name attr="value" ...>`, the attribute values escaped.
inline void SerializeStartTag(Arena* a, base::StrBuilder& out,
                              const Node* node) {
    out.AppendChar('<');
    out.Append(NodeName(a, node));
    for (const Attribute* at = NodeAttrs(a, node); at;
         at = AttributeNext(a, at)) {
        out.AppendChar(' ');
        out.Append(AttributeName(a, at));
        out.Append(StrL("=\""));
        base::AppendHtmlEscaped(out, AttributeValue(a, at), false, true);
        out.AppendChar('"');
    }
    out.AppendChar('>');
}
inline void SerializeEndTag(Arena* a, base::StrBuilder& out, const Node* node) {
    out.Append(StrL("</"));
    out.Append(NodeName(a, node));
    out.AppendChar('>');
}
inline void SerializeComment(Arena* a, base::StrBuilder& out,
                             const Node* node) {
    out.Append(StrL("<!--"));
    out.Append(NodeData(a, node));
    out.Append(StrL("-->"));
}

} // namespace html5ever
#endif // GPUI_HTML5EVER_HTML5EVER_H_
