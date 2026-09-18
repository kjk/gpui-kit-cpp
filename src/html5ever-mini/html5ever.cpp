#include "html5ever-mini/html5ever.h"

namespace html5ever {

using namespace base;

// This is the old dependency-free parser moved out of base/text_format.cpp.
// It intentionally recognizes only reader-mode HTML: tags, attributes,
// comments, a doctype, raw elements and the small entity set below. The full
// implementation is the default; this file exists for size-sensitive builds.
static const char kVoidElements[] =
    "area\0base\0br\0col\0embed\0hr\0img\0input\0link\0meta\0param\0source\0"
    "track\0wbr\0";
static const char kRawElements[] = "script\0style\0";
static const char kEntityNames[] =
    "AMP\0CounterClockwiseContourIntegral\0GT\0LT\0QUOT\0amp\0apos\0gt\0lt\0"
    "nbsp\0quot\0";
static const char kEntityValues[] = "&\0∳\0>\0<\0\"\0&\0'\0>\0<\0 \0\"\0";

static bool Space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

static bool Alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static bool NameChar(char c) {
    return Alpha(c) || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
           c == ':';
}

static ArenaStr Lower(Arena* a, Str value) {
    ArenaStr result = ArenaStrDup(a, value);
    StrLowerAscii(ArenaStrGet(a, result).s);
    return result;
}

static int EncodeUtf8(char* out, uint32_t cp) {
    if (cp <= 0x7f) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp <= 0x7ff) {
        out[0] = (char)(0xc0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3f));
        return 2;
    }
    if (cp <= 0xffff) {
        out[0] = (char)(0xe0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3f));
        out[2] = (char)(0x80 | (cp & 0x3f));
        return 3;
    }
    out[0] = (char)(0xf0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3f));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3f));
    out[3] = (char)(0x80 | (cp & 0x3f));
    return 4;
}

static void AppendCp(StrBuilder& out, uint32_t cp) {
    if (cp == 0 || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) {
        cp = 0xfffd;
    }
    char bytes[4];
    int n = EncodeUtf8(bytes, cp);
    out.Append(Str(bytes, n));
}

static ArenaStr Decode(Arena* a, Str value) {
    bool needsDecode = false;
    for (int i = 0; i < len(value); i++) {
        if (value.s[i] == '&') {
            needsDecode = true;
            break;
        }
    }
    if (!needsDecode) return ArenaStrDup(a, value);

    StrBuilder out(a);
    out.Reserve(len(value));
    for (int i = 0; i < len(value);) {
        if (value.s[i] != '&') {
            out.AppendChar(value.s[i++]);
            continue;
        }
        int start = i++;
        if (i < len(value) && value.s[i] == '#') {
            i++;
            int radix = 10;
            if (i < len(value) && (value.s[i] == 'x' || value.s[i] == 'X')) {
                radix = 16;
                i++;
            }
            int digits = i;
            uint32_t cp = 0;
            while (i < len(value)) {
                char c = value.s[i];
                int d = c >= '0' && c <= '9'                  ? c - '0'
                        : radix == 16 && c >= 'a' && c <= 'f' ? c - 'a' + 10
                        : radix == 16 && c >= 'A' && c <= 'F' ? c - 'A' + 10
                                                              : -1;
                if (d < 0) break;
                cp = cp * (uint32_t)radix + (uint32_t)d;
                i++;
            }
            if (i == digits) {
                i = start + 1;
                out.AppendChar('&');
                continue;
            }
            if (i < len(value) && value.s[i] == ';') i++;
            AppendCp(out, cp);
            continue;
        }
        int end = i;
        while (end < len(value) && Alpha(value.s[end])) end++;
        Str decoded = {};
        int used = 0;
        for (int n = end - i; n > 0; n--) {
            int ix = SeqStrIndex(kEntityNames, Str(value.s + i, n));
            if (ix >= 0) {
                decoded = SeqStrByIndex(kEntityValues, ix);
                used = n;
                break;
            }
        }
        if (!decoded.s) {
            i = start + 1;
            out.AppendChar('&');
            continue;
        }
        out.Append(decoded);
        i += used;
        if (i < len(value) && value.s[i] == ';') i++;
    }
    return ArenaStrDup(a, out.TakeStr());
}

struct Lex {
    Arena* a = nullptr;
    Str source = {};
    int at = 0;
    int line = 1;
    TokenSink sink = nullptr;
    void* user = nullptr;
    TokenizerOptions options = {};
};

static void Emit(Lex* l, const Token& token) {
    if (l->sink) l->sink(l->user, &token);
}

static void SkipSpace(Lex* l) {
    while (l->at < len(l->source) && Space(l->source.s[l->at])) {
        if (l->source.s[l->at++] == '\n') l->line++;
    }
}

static ArenaStr Name(Lex* l) {
    int start = l->at;
    while (l->at < len(l->source) && NameChar(l->source.s[l->at])) l->at++;
    return Lower(l->a, Str(l->source.s + start, l->at - start));
}

static Attribute* Attrs(Lex* l, bool* selfClose) {
    Attribute* first = nullptr;
    Attribute* last = nullptr;
    *selfClose = false;
    for (;;) {
        SkipSpace(l);
        if (l->at >= len(l->source)) return first;
        if (l->source.s[l->at] == '>') {
            l->at++;
            return first;
        }
        if (l->source.s[l->at] == '/' && l->at + 1 < len(l->source) &&
            l->source.s[l->at + 1] == '>') {
            l->at += 2;
            *selfClose = true;
            return first;
        }
        int start = l->at;
        while (l->at < len(l->source) && NameChar(l->source.s[l->at])) l->at++;
        if (start == l->at) {
            l->at++;
            continue;
        }
        ArenaStr name = Lower(l->a, Str(l->source.s + start, l->at - start));
        SkipSpace(l);
        ArenaStr value = {};
        if (l->at < len(l->source) && l->source.s[l->at] == '=') {
            l->at++;
            SkipSpace(l);
            char quote = 0;
            if (l->at < len(l->source) &&
                (l->source.s[l->at] == '\'' || l->source.s[l->at] == '"')) {
                quote = l->source.s[l->at++];
            }
            start = l->at;
            while (l->at < len(l->source) &&
                   (quote ? l->source.s[l->at] != quote
                          : !Space(l->source.s[l->at]) &&
                                l->source.s[l->at] != '>')) {
                l->at++;
            }
            value = Decode(l->a, Str(l->source.s + start, l->at - start));
            if (quote && l->at < len(l->source)) l->at++;
        }
        Attribute* attr = ArenaNew<Attribute>(l->a);
        attr->name = name;
        attr->value = value;
        if (last)
            last->next = ArenaPtrOf(l->a, attr);
        else
            first = attr;
        last = attr;
    }
}

static int RawEnd(Lex* l, Str name) {
    for (int i = l->at; i + len(name) + 2 <= len(l->source); i++) {
        if (l->source.s[i] == '<' && l->source.s[i + 1] == '/' &&
            StrEqI(Str(l->source.s + i + 2, len(name)), name)) {
            return i;
        }
    }
    return len(l->source);
}

void Tokenize(Arena* a, Str source, TokenSink sink, void* user,
              TokenizerOptions options) {
    if (!a || !sink) return;
    Lex l;
    l.a = a;
    l.source = source;
    l.sink = sink;
    l.user = user;
    l.options = options;
    Str rawName = {};
    while (l.at < len(source)) {
        if (rawName.s) {
            int end = RawEnd(&l, rawName);
            Token token;
            token.kind = TokenKind::Character;
            token.data = ArenaStrDup(a, Str(source.s + l.at, end - l.at));
            token.line = l.line;
            l.at = end;
            rawName = {};
            Emit(&l, token);
            continue;
        }
        if (source.s[l.at] != '<') {
            int start = l.at;
            while (l.at < len(source) && source.s[l.at] != '<') l.at++;
            Token token;
            token.kind = TokenKind::Character;
            token.data = Decode(a, Str(source.s + start, l.at - start));
            token.line = l.line;
            Emit(&l, token);
            continue;
        }
        if (l.at + 3 < len(source) &&
            StrEq(Str(source.s + l.at, 4), StrL("<!--"))) {
            l.at += 4;
            int start = l.at;
            while (l.at + 2 < len(source) &&
                   !(source.s[l.at] == '-' && source.s[l.at + 1] == '-' &&
                     source.s[l.at + 2] == '>')) {
                l.at++;
            }
            Token token;
            token.kind = TokenKind::Comment;
            token.data = ArenaStrDup(a, Str(source.s + start, l.at - start));
            if (l.at + 2 < len(source)) l.at += 3;
            Emit(&l, token);
            continue;
        }
        if (l.at + 1 < len(source) && source.s[l.at + 1] == '!') {
            l.at += 2;
            while (l.at < len(source) && source.s[l.at] != '>') l.at++;
            if (l.at < len(source)) l.at++;
            continue;
        }
        Token token;
        token.line = l.line;
        if (l.at + 1 < len(source) && source.s[l.at + 1] == '/') {
            l.at += 2;
            token.kind = TokenKind::EndTag;
            token.name = Name(&l);
            while (l.at < len(source) && source.s[l.at] != '>') l.at++;
            if (l.at < len(source)) l.at++;
        } else if (l.at + 1 < len(source) && Alpha(source.s[l.at + 1])) {
            l.at++;
            token.kind = TokenKind::StartTag;
            token.name = Name(&l);
            token.attrs = ArenaPtrOf(a, Attrs(&l, &token.selfClosing));
            if (!token.selfClosing &&
                SeqStrContainsI(kRawElements, TokenName(a, &token))) {
                rawName = TokenName(a, &token);
            }
        } else {
            token.kind = TokenKind::Character;
            token.data = ArenaStrDup(a, Str(source.s + l.at++, 1));
        }
        Emit(&l, token);
    }
    Token eof;
    eof.kind = TokenKind::Eof;
    eof.line = l.line;
    Emit(&l, eof);
}

static Node* NewNode(Arena* a, NodeKind kind, Str name = {}) {
    Node* node = ArenaNew<Node>(a);
    node->kind = kind;
    node->name = ArenaStrDup(a, name);
    return node;
}

static void Append(Arena* a, Node* parent, Node* child) {
    child->parent = ArenaPtrOf(a, parent);
    Node* last = NodeLast(a, parent);
    if (last)
        last->next = ArenaPtrOf(a, child);
    else
        parent->first = ArenaPtrOf(a, child);
    parent->last = ArenaPtrOf(a, child);
}

struct Build {
    Arena* a = nullptr;
    Node* doc = nullptr;
    ArenaVec<Node*> stack{};
    ParseOptions options = {};
};

static void OnToken(void* user, const Token* token) {
    Build* b = (Build*)user;
    Node* parent = b->stack.len ? b->stack[b->stack.len - 1] : b->doc;
    if (token->kind == TokenKind::Character) {
        if (ArenaStrLen(b->a, token->data) <= 0) return;
        Node* node = NewNode(b->a, NodeKind::Text);
        node->data = token->data;
        Append(b->a, parent, node);
    } else if (token->kind == TokenKind::Comment) {
        Node* node = NewNode(b->a, NodeKind::Comment);
        node->data = token->data;
        Append(b->a, parent, node);
    } else if (token->kind == TokenKind::Doctype && !b->options.dropDoctype) {
        Node* node = NewNode(b->a, NodeKind::Doctype);
        node->name = token->name;
        Append(b->a, parent, node);
    } else if (token->kind == TokenKind::StartTag) {
        Str name = TokenName(b->a, token);
        Node* node = NewNode(b->a, NodeKind::Element);
        node->name = token->name;
        node->attrs = token->attrs;
        Append(b->a, parent, node);
        if (!token->selfClosing && !SeqStrContainsI(kVoidElements, name)) {
            b->stack.Append(b->a, node);
        }
    } else if (token->kind == TokenKind::EndTag) {
        for (int i = b->stack.len - 1; i >= 0; i--) {
            if (StrEqI(NodeName(b->a, b->stack[i]), TokenName(b->a, token))) {
                b->stack.Truncate(i);
                break;
            }
        }
    }
}

static Node* Parse(Arena* a, Str source, ParseOptions options) {
    if (!a) return nullptr;
    Build build;
    build.a = a;
    build.options = options;
    build.doc = NewNode(a, NodeKind::Document);
    Tokenize(a, source, OnToken, &build, options.tokenizer);
    return build.doc;
}

Node* ParseDocument(Arena* a, Str source, ParseOptions options) {
    return Parse(a, source, options);
}

Node* ParseFragment(Arena* a, Str source, Str context, ParseOptions options) {
    (void)context;
    return Parse(a, source, options);
}

const Attribute* Attr(Arena* a, const Node* node, Str name) {
    for (const Attribute* at = NodeAttrs(a, node); at;
         at = AttributeNext(a, at)) {
        if (StrEqI(AttributeName(a, at), name)) return at;
    }
    return nullptr;
}

Str AttrValue(Arena* a, const Node* node, Str name) {
    return AttributeValue(a, Attr(a, node, name));
}

static void WriteEscaped(StrBuilder& out, Str value, bool attribute) {
    for (int i = 0; i < len(value); i++) {
        char c = value.s[i];
        if (c == '&')
            out.Append(StrL("&amp;"));
        else if (c == '<')
            out.Append(StrL("&lt;"));
        else if (c == '>' && !attribute)
            out.Append(StrL("&gt;"));
        else if (c == '"' && attribute)
            out.Append(StrL("&quot;"));
        else
            out.AppendChar(c);
    }
}

static void Write(Arena* a, StrBuilder& out, const Node* node, bool include) {
    bool element = node->kind == NodeKind::Element;
    if (include && node->kind == NodeKind::Text) {
        const Node* parent = NodeParent(a, node);
        if (parent && SeqStrContainsI(kRawElements, NodeName(a, parent)))
            out.Append(NodeData(a, node));
        else
            WriteEscaped(out, NodeData(a, node), false);
    } else if (include && node->kind == NodeKind::Comment) {
        out.Append(StrL("<!--"));
        out.Append(NodeData(a, node));
        out.Append(StrL("-->"));
    } else if (include && element) {
        out.AppendChar('<');
        out.Append(NodeName(a, node));
        for (const Attribute* at = NodeAttrs(a, node); at;
             at = AttributeNext(a, at)) {
            out.AppendChar(' ');
            out.Append(AttributeName(a, at));
            out.Append(StrL("=\""));
            WriteEscaped(out, AttributeValue(a, at), true);
            out.AppendChar('"');
        }
        out.AppendChar('>');
    }
    for (const Node* child = NodeFirst(a, node); child;
         child = NodeNext(a, child)) {
        Write(a, out, child, true);
    }
    if (include && element &&
        !SeqStrContainsI(kVoidElements, NodeName(a, node))) {
        out.Append(StrL("</"));
        out.Append(NodeName(a, node));
        out.AppendChar('>');
    }
}

Str Serialize(Arena* a, const Node* node, SerializeOptions options) {
    if (!a || !node) return {};
    StrBuilder out(a);
    Write(a, out, node, options.includeNode);
    return out.TakeStr();
}

} // namespace html5ever
