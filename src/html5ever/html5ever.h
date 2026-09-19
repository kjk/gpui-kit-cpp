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

const Attribute* Attr(Arena* a, const Node* node, Str name);
Str AttrValue(Arena* a, const Node* node, Str name);

} // namespace html5ever
#endif // GPUI_HTML5EVER_HTML5EVER_H_
