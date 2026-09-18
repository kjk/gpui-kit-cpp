#include "html5ever/html5ever.h"

namespace html5ever {

using namespace base;

// html5ever's generated atom sets become compact NUL-separated strings here.
// The hot sets are split by first byte below so their linear walks stay short.
static const char kVoidB[] = "base\0basefont\0bgsound\0br\0";
static const char kVoidI[] = "img\0input\0";
static const char kRawElements[] =
    "iframe\0noembed\0noframes\0script\0style\0xmp\0";
static const char kRcdataElements[] = "textarea\0title\0";
static const char kFormattingB[] = "b\0big\0";
static const char kFormattingS[] = "s\0small\0strike\0strong\0";
static const char kBlockA[] = "address\0article\0aside\0";
static const char kBlockD[] = "details\0dialog\0dir\0div\0dl\0";
static const char kBlockF[] = "fieldset\0figcaption\0figure\0footer\0form\0";
static const char kBlockH[] = "h1\0h2\0h3\0h4\0h5\0h6\0header\0hgroup\0hr\0";
static const char kBlockM[] = "main\0menu\0";
static const char kBlockP[] = "p\0pre\0";
static const char kBlockS[] = "search\0section\0summary\0";
static const char kHeadElements[] =
    "base\0basefont\0bgsound\0link\0meta\0noframes\0script\0style\0template\0"
    "title\0";
static const char kTableParts[] =
    "caption\0col\0colgroup\0tbody\0td\0tfoot\0th\0thead\0tr\0";

static bool IsSpace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

static bool IsAlpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static bool IsDigit(char c) {
    return c >= '0' && c <= '9';
}

static bool IsNameChar(char c) {
    return IsAlpha(c) || IsDigit(c) || c == '-' || c == '_' || c == ':';
}

// Tag names have already been folded to lowercase by ScanName. Branching on
// their first byte keeps the common membership tests to one or two compares.
static bool IsVoid(Str name) {
    if (!name.s || name.len == 0) return false;
    switch (name.s[0]) {
        case 'a':
            return StrEq(name, StrL("area"));
        case 'b':
            return SeqStrContainsI(kVoidB, name);
        case 'c':
            return StrEq(name, StrL("col"));
        case 'e':
            return StrEq(name, StrL("embed"));
        case 'f':
            return StrEq(name, StrL("frame"));
        case 'h':
            return StrEq(name, StrL("hr"));
        case 'i':
            return SeqStrContainsI(kVoidI, name);
        case 'k':
            return StrEq(name, StrL("keygen"));
        case 'l':
            return StrEq(name, StrL("link"));
        case 'm':
            return StrEq(name, StrL("meta"));
        case 'p':
            return StrEq(name, StrL("param"));
        case 's':
            return StrEq(name, StrL("source"));
        case 't':
            return StrEq(name, StrL("track"));
        case 'w':
            return StrEq(name, StrL("wbr"));
        default:
            return false;
    }
}

static bool IsFormatting(Str name) {
    if (!name.s || name.len == 0) return false;
    switch (name.s[0]) {
        case 'a':
            return StrEq(name, StrL("a"));
        case 'b':
            return SeqStrContainsI(kFormattingB, name);
        case 'c':
            return StrEq(name, StrL("code"));
        case 'e':
            return StrEq(name, StrL("em"));
        case 'f':
            return StrEq(name, StrL("font"));
        case 'i':
            return StrEq(name, StrL("i"));
        case 'n':
            return StrEq(name, StrL("nobr"));
        case 's':
            return SeqStrContainsI(kFormattingS, name);
        case 't':
            return StrEq(name, StrL("tt"));
        case 'u':
            return StrEq(name, StrL("u"));
        default:
            return false;
    }
}

static ArenaStr LowerCopy(Arena* a, Str value) {
    ArenaStr result = ArenaStrDup(a, value);
    StrLowerAscii(ArenaStrGet(a, result).s);
    return result;
}

// The frequently occurring named references. Numeric references cover the
// rest of the compact implementation; the full markdown build already owns
// the generated 2,125-name database used by TextView's HTML projection.
// Keeping this tokenizer's standalone table as SeqStrings avoids a pointer
// relocation per spelling and per value.
static const char kEntityNames[] =
    "AMP\0AElig\0COPY\0CounterClockwiseContourIntegral\0GT\0LT\0QUOT\0REG\0"
    "amp\0apos\0cent\0copy\0euro\0gt\0hellip\0laquo\0ldquo\0lsquo\0lt\0"
    "mdash\0middot\0nbsp\0ndash\0pound\0quot\0raquo\0rdquo\0reg\0rsquo\0"
    "trade\0yen\0";
static const char kEntityValues[] =
    "&\0Æ\0©\0∳\0>\0<\0\"\0®\0&\0'\0¢\0©\0€\0>\0…\0«\0“\0‘\0<\0—\0"
    "·\0 \0–\0£\0\"\0»\0”\0®\0’\0™\0¥\0";

static Str NamedEntity(Str name) {
    int ix = SeqStrIndex(kEntityNames, name);
    return ix < 0 ? Str{} : SeqStrByIndex(kEntityValues, ix);
}

static uint32_t NumericEntity(Str value, int radix) {
    uint32_t cp = 0;
    bool any = false;
    for (int i = 0; i < value.len; i++) {
        char c = value.s[i];
        uint32_t digit = 0;
        if (IsDigit(c)) {
            digit = (uint32_t)(c - '0');
        } else if (radix == 16 && c >= 'a' && c <= 'f') {
            digit = (uint32_t)(c - 'a' + 10);
        } else if (radix == 16 && c >= 'A' && c <= 'F') {
            digit = (uint32_t)(c - 'A' + 10);
        } else {
            break;
        }
        any = true;
        if (cp > 0x10ffffu / (uint32_t)radix) return 0xfffd;
        cp = cp * (uint32_t)radix + digit;
    }
    if (!any || cp == 0 || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) {
        return 0xfffd;
    }
    // The HTML numeric-character-reference replacement table.
    static const uint16_t controls[32] = {
        0x20ac, 0x0081, 0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021,
        0x02c6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008d, 0x017d, 0x008f,
        0x0090, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
        0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0x009d, 0x017e, 0x0178,
    };
    if (cp >= 0x80 && cp <= 0x9f) cp = controls[cp - 0x80];
    return cp;
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
    char bytes[4];
    int n = EncodeUtf8(bytes, cp);
    out.Append(Str(bytes, n));
}

static ArenaStr Decode(Arena* a, Str value, bool attribute) {
    bool needsDecode = false;
    for (int i = 0; i < value.len; i++) {
        if (value.s[i] == '&' || value.s[i] == '\r' || value.s[i] == 0) {
            needsDecode = true;
            break;
        }
    }
    if (!needsDecode) return ArenaStrDup(a, value);

    StrBuilder out(a);
    out.Reserve(value.len);
    for (int i = 0; i < value.len;) {
        if (value.s[i] != '&') {
            char c = value.s[i++] == '\r' ? '\n' : value.s[i - 1];
            if (c == '\n' && i < value.len && value.s[i] == '\n' &&
                value.s[i - 1] == '\r') {
                i++;
            }
            if (c == 0) {
                AppendCp(out, 0xfffd);
            } else {
                out.AppendChar(c);
            }
            continue;
        }
        int start = i++;
        if (i < value.len && value.s[i] == '#') {
            i++;
            int radix = 10;
            if (i < value.len && (value.s[i] == 'x' || value.s[i] == 'X')) {
                radix = 16;
                i++;
            }
            int digits = i;
            while (
                i < value.len &&
                (IsDigit(value.s[i]) ||
                 (radix == 16 && ((value.s[i] >= 'a' && value.s[i] <= 'f') ||
                                  (value.s[i] >= 'A' && value.s[i] <= 'F'))))) {
                i++;
            }
            if (digits == i) {
                out.AppendChar('&');
                i = start + 1;
                continue;
            }
            uint32_t cp =
                NumericEntity(Str(value.s + digits, i - digits), radix);
            if (i < value.len && value.s[i] == ';') i++;
            AppendCp(out, cp);
            continue;
        }
        int end = i;
        while (end < value.len && IsAlpha(value.s[end]) && end - i < 31) end++;
        if (end < value.len && IsDigit(value.s[end])) end++;
        int matched = -1;
        Str decoded = {};
        for (int n = end - i; n > 0; n--) {
            decoded = NamedEntity(Str(value.s + i, n));
            if (decoded.s) {
                matched = n;
                break;
            }
        }
        bool semi = matched > 0 && i + matched < value.len &&
                    value.s[i + matched] == ';';
        if (matched < 0 ||
            (attribute && !semi && i + matched < value.len &&
             (IsDigit(value.s[i + matched]) || IsAlpha(value.s[i + matched]) ||
              value.s[i + matched] == '='))) {
            out.AppendChar('&');
            i = start + 1;
            continue;
        }
        out.Append(decoded);
        i += matched + (semi ? 1 : 0);
    }
    return ArenaStrDup(a, out.TakeStr());
}

struct Scanner {
    Arena* a = nullptr;
    Str source = {};
    int at = 0;
    int line = 1;
    TokenSink sink = nullptr;
    void* user = nullptr;
    TokenizerOptions options = {};
    Str rawName = {};
    bool rcdata = false;
};

static void Emit(Scanner* s, const Token& token) {
    if (s->sink) s->sink(s->user, &token);
}

static void Error(Scanner* s, const char* message) {
    if (!s->options.exactErrors) return;
    Token token;
    token.kind = TokenKind::ParseError;
    token.data = ArenaStrDup(s->a, Str((char*)message));
    token.line = s->line;
    Emit(s, token);
}

static void SkipSpace(Scanner* s) {
    while (s->at < s->source.len && IsSpace(s->source.s[s->at])) {
        if (s->source.s[s->at++] == '\n') s->line++;
    }
}

static ArenaStr ScanName(Scanner* s) {
    int start = s->at;
    while (s->at < s->source.len && IsNameChar(s->source.s[s->at])) s->at++;
    return LowerCopy(s->a, Str(s->source.s + start, s->at - start));
}

static Attribute* ScanAttrs(Scanner* s, bool* selfClosing) {
    Attribute* first = nullptr;
    Attribute* last = nullptr;
    *selfClosing = false;
    for (;;) {
        SkipSpace(s);
        if (s->at >= s->source.len) return first;
        char c = s->source.s[s->at];
        if (c == '>') {
            s->at++;
            return first;
        }
        if (c == '/' && s->at + 1 < s->source.len &&
            s->source.s[s->at + 1] == '>') {
            s->at += 2;
            *selfClosing = true;
            return first;
        }
        int nameStart = s->at;
        while (s->at < s->source.len && !IsSpace(s->source.s[s->at]) &&
               s->source.s[s->at] != '=' && s->source.s[s->at] != '>' &&
               s->source.s[s->at] != '/') {
            s->at++;
        }
        if (nameStart == s->at) {
            Error(s, "unexpected byte in tag");
            s->at++;
            continue;
        }
        ArenaStr name =
            LowerCopy(s->a, Str(s->source.s + nameStart, s->at - nameStart));
        SkipSpace(s);
        ArenaStr value = {};
        if (s->at < s->source.len && s->source.s[s->at] == '=') {
            s->at++;
            SkipSpace(s);
            int start = s->at;
            if (s->at < s->source.len &&
                (s->source.s[s->at] == '\'' || s->source.s[s->at] == '"')) {
                char quote = s->source.s[s->at++];
                start = s->at;
                while (s->at < s->source.len && s->source.s[s->at] != quote) {
                    if (s->source.s[s->at++] == '\n') s->line++;
                }
                value =
                    Decode(s->a, Str(s->source.s + start, s->at - start), true);
                if (s->at < s->source.len) s->at++;
            } else {
                while (s->at < s->source.len && !IsSpace(s->source.s[s->at]) &&
                       s->source.s[s->at] != '>') {
                    s->at++;
                }
                value =
                    Decode(s->a, Str(s->source.s + start, s->at - start), true);
            }
        }
        bool duplicate = false;
        for (Attribute* at = first; at; at = AttributeNext(s->a, at)) {
            if (StrEq(AttributeName(s->a, at), ArenaStrGet(s->a, name))) {
                duplicate = true;
            }
        }
        if (duplicate) {
            Error(s, "duplicate attribute");
            continue;
        }
        Attribute* attr = ArenaNew<Attribute>(s->a);
        attr->name = name;
        attr->value = value;
        if (last)
            last->next = ArenaPtrOf(s->a, attr);
        else
            first = attr;
        last = attr;
    }
}

static int FindRawClose(const Scanner* s) {
    for (int i = s->at; i + 2 + s->rawName.len <= s->source.len; i++) {
        if (s->source.s[i] != '<' || s->source.s[i + 1] != '/') continue;
        if (!StrEqI(Str(s->source.s + i + 2, s->rawName.len), s->rawName)) {
            continue;
        }
        int end = i + 2 + s->rawName.len;
        if (end >= s->source.len || IsSpace(s->source.s[end]) ||
            s->source.s[end] == '>') {
            return i;
        }
    }
    return s->source.len;
}

static void TokenizeRun(Scanner* s) {
    if (s->options.discardBom && s->source.len >= 3 &&
        (uint8_t)s->source.s[0] == 0xef && (uint8_t)s->source.s[1] == 0xbb &&
        (uint8_t)s->source.s[2] == 0xbf) {
        s->at = 3;
    }
    while (s->at < s->source.len) {
        if (s->rawName.s) {
            int end = FindRawClose(s);
            if (end > s->at) {
                Token text;
                text.kind = TokenKind::Character;
                Str raw(s->source.s + s->at, end - s->at);
                text.data = s->rcdata ? Decode(s->a, raw, false)
                                      : ArenaStrDup(s->a, raw);
                text.line = s->line;
                for (int i = s->at; i < end; i++) {
                    if (s->source.s[i] == '\n') s->line++;
                }
                s->at = end;
                Emit(s, text);
                continue;
            }
            s->rawName = {};
            s->rcdata = false;
        }
        if (s->source.s[s->at] != '<') {
            int start = s->at;
            while (s->at < s->source.len && s->source.s[s->at] != '<') {
                if (s->source.s[s->at++] == '\n') s->line++;
            }
            Token text;
            text.kind = TokenKind::Character;
            text.data =
                Decode(s->a, Str(s->source.s + start, s->at - start), false);
            text.line = s->line;
            Emit(s, text);
            continue;
        }
        int tokenLine = s->line;
        if (s->at + 3 < s->source.len &&
            StrEq(Str(s->source.s + s->at, 4), StrL("<!--"))) {
            s->at += 4;
            int start = s->at;
            while (s->at + 2 < s->source.len &&
                   !(s->source.s[s->at] == '-' &&
                     s->source.s[s->at + 1] == '-' &&
                     s->source.s[s->at + 2] == '>')) {
                if (s->source.s[s->at++] == '\n') s->line++;
            }
            Token comment;
            comment.kind = TokenKind::Comment;
            comment.data =
                ArenaStrDup(s->a, Str(s->source.s + start, s->at - start));
            comment.line = tokenLine;
            if (s->at + 2 < s->source.len)
                s->at += 3;
            else
                Error(s, "eof in comment");
            Emit(s, comment);
            continue;
        }
        if (s->at + 2 < s->source.len && s->source.s[s->at + 1] == '!') {
            int start = s->at + 2;
            s->at = start;
            while (s->at < s->source.len && s->source.s[s->at] != '>') {
                s->at++;
            }
            Str body = StrTrimAscii(Str(s->source.s + start, s->at - start));
            if (s->at < s->source.len) s->at++;
            Token token;
            token.kind = TokenKind::Doctype;
            token.line = tokenLine;
            if (StrStartsWithI(body, StrL("doctype"))) {
                body = StrTrimAscii(Str(body.s + 7, body.len - 7));
                int n = 0;
                while (n < body.len && !IsSpace(body.s[n])) n++;
                token.name = LowerCopy(s->a, Str(body.s, n));
                token.forceQuirks =
                    !StrEqI(TokenName(s->a, &token), StrL("html"));
            } else {
                token.kind = TokenKind::Comment;
                token.data = ArenaStrDup(s->a, body);
            }
            Emit(s, token);
            continue;
        }
        if (s->at + 1 < s->source.len && s->source.s[s->at + 1] == '/') {
            s->at += 2;
            SkipSpace(s);
            Token token;
            token.kind = TokenKind::EndTag;
            token.name = ScanName(s);
            token.line = tokenLine;
            while (s->at < s->source.len && s->source.s[s->at] != '>') s->at++;
            if (s->at < s->source.len) s->at++;
            Emit(s, token);
            continue;
        }
        if (s->at + 1 < s->source.len && IsAlpha(s->source.s[s->at + 1])) {
            s->at++;
            Token token;
            token.kind = TokenKind::StartTag;
            token.name = ScanName(s);
            token.line = tokenLine;
            token.attrs = ArenaPtrOf(s->a, ScanAttrs(s, &token.selfClosing));
            Emit(s, token);
            Str name = TokenName(s->a, &token);
            if (!token.selfClosing &&
                (SeqStrContainsI(kRawElements, name) ||
                 SeqStrContainsI(kRcdataElements, name))) {
                s->rawName = name;
                s->rcdata = SeqStrContainsI(kRcdataElements, name);
            }
            continue;
        }
        Token text;
        text.kind = TokenKind::Character;
        text.data = ArenaStrDup(s->a, Str(s->source.s + s->at, 1));
        text.line = tokenLine;
        s->at++;
        Emit(s, text);
    }
    Token eof;
    eof.kind = TokenKind::Eof;
    eof.line = s->line;
    Emit(s, eof);
}

void Tokenize(Arena* a, Str source, TokenSink sink, void* user,
              TokenizerOptions options) {
    if (!a || !sink) return;
    Scanner scanner;
    scanner.a = a;
    scanner.source = source;
    scanner.sink = sink;
    scanner.user = user;
    scanner.options = options;
    TokenizeRun(&scanner);
}

static Node* NewNode(Arena* a, NodeKind kind, Str name = {}) {
    Node* node = ArenaNew<Node>(a);
    node->kind = kind;
    node->name = ArenaStrDup(a, name);
    return node;
}

static void Append(Arena* a, Node* parent, Node* child) {
    child->parent = ArenaPtrOf(a, parent);
    child->next = {};
    Node* last = NodeLast(a, parent);
    if (last)
        last->next = ArenaPtrOf(a, child);
    else
        parent->first = ArenaPtrOf(a, child);
    parent->last = ArenaPtrOf(a, child);
}

static void InsertBefore(Arena* a, Node* before, Node* child) {
    Node* parent = NodeParent(a, before);
    if (!parent) return;
    child->parent = ArenaPtrOf(a, parent);
    if (NodeFirst(a, parent) == before) {
        child->next = ArenaPtrOf(a, before);
        parent->first = ArenaPtrOf(a, child);
        return;
    }
    Node* prev = NodeFirst(a, parent);
    while (prev && NodeNext(a, prev) != before) prev = NodeNext(a, prev);
    if (!prev) return;
    prev->next = ArenaPtrOf(a, child);
    child->next = ArenaPtrOf(a, before);
}

static Attribute* CloneAttrs(Arena* a, const Attribute* attrs) {
    Attribute* first = nullptr;
    Attribute* last = nullptr;
    for (; attrs; attrs = AttributeNext(a, attrs)) {
        Attribute* copy = ArenaNew<Attribute>(a);
        *copy = *attrs;
        copy->next = {};
        if (last)
            last->next = ArenaPtrOf(a, copy);
        else
            first = copy;
        last = copy;
    }
    return first;
}

struct Builder {
    Arena* a = nullptr;
    ParseOptions options = {};
    Node* doc = nullptr;
    Node* html = nullptr;
    Node* head = nullptr;
    Node* body = nullptr;
    ArenaVec<Node*> open{};
    bool fragment = false;
    Str context = {};
};

static Node* Current(Builder* b) {
    return b->open.len ? b->open[b->open.len - 1] : b->doc;
}

static int OpenIndex(Builder* b, Str name) {
    for (int i = b->open.len - 1; i >= 0; i--) {
        if (StrEq(NodeName(b->a, b->open[i]), name)) return i;
    }
    return -1;
}

static bool HasOpen(Builder* b, Str name) {
    return OpenIndex(b, name) >= 0;
}

static Node* Element(Builder* b, Str name, const Attribute* attrs,
                     Namespace ns = Namespace::Html) {
    Node* node = NewNode(b->a, NodeKind::Element, name);
    node->attrs = ArenaPtrOf(b->a, CloneAttrs(b->a, attrs));
    node->ns = ns;
    return node;
}

static Node* ElementFromToken(Builder* b, const Token* token,
                              Namespace ns = Namespace::Html) {
    Node* node = ArenaNew<Node>(b->a);
    node->kind = NodeKind::Element;
    node->name = token->name;
    node->attrs = token->attrs;
    node->ns = ns;
    return node;
}

static Node* EnsureWrapper(Builder* b, Node** slot, Str name, Node* parent) {
    if (*slot) return *slot;
    *slot = Element(b, name, nullptr);
    (*slot)->implicit = true;
    Append(b->a, parent, *slot);
    return *slot;
}

static Node* Body(Builder* b) {
    if (b->fragment) return b->doc;
    if (b->body) return b->body;
    EnsureWrapper(b, &b->html, StrL("html"), b->doc);
    EnsureWrapper(b, &b->head, StrL("head"), b->html);
    return EnsureWrapper(b, &b->body, StrL("body"), b->html);
}

static bool AllSpace(Str value) {
    for (int i = 0; i < value.len; i++) {
        if (!IsSpace(value.s[i])) return false;
    }
    return true;
}

static Node* TableInScope(Builder* b) {
    for (int i = b->open.len - 1; i >= 0; i--) {
        if (StrEq(NodeName(b->a, b->open[i]), StrL("table"))) {
            return b->open[i];
        }
    }
    return nullptr;
}

static bool TableAllows(Str parent, Str child) {
    if (StrEq(parent, StrL("table"))) {
        return SeqStrContainsI(kTableParts, child) ||
               StrEq(child, StrL("style")) || StrEq(child, StrL("script")) ||
               StrEq(child, StrL("template"));
    }
    if (StrEq(parent, StrL("tbody")) || StrEq(parent, StrL("thead")) ||
        StrEq(parent, StrL("tfoot"))) {
        return StrEq(child, StrL("tr"));
    }
    if (StrEq(parent, StrL("tr"))) {
        return StrEq(child, StrL("td")) || StrEq(child, StrL("th"));
    }
    return true;
}

static Node* InsertionParent(Builder* b, Str child, bool textIsSpace,
                             Node** tableOut, bool* fosterOut) {
    Node* current = Current(b);
    Node* table = TableInScope(b);
    if (tableOut) *tableOut = table;
    bool foster =
        table && !textIsSpace && !TableAllows(NodeName(b->a, current), child);
    if (fosterOut) *fosterOut = foster;
    if (foster) {
        Node* tableParent = NodeParent(b->a, table);
        return tableParent ? tableParent : current;
    }
    return current == b->doc ? Body(b) : current;
}

static void AppendText(Builder* b, ArenaStr stored) {
    Str data = ArenaStrGet(b->a, stored);
    if (data.len <= 0) return;
    Node* table = nullptr;
    bool foster = false;
    Node* parent = InsertionParent(b, {}, AllSpace(data), &table, &foster);
    if (foster) {
        Node* text = NewNode(b->a, NodeKind::Text);
        text->data = stored;
        InsertBefore(b->a, table, text);
    } else {
        Node* last = NodeLast(b->a, parent);
        if (last && last->kind == NodeKind::Text) {
            last->data = ArenaStrAppend(b->a, last->data, data);
        } else {
            Node* text = NewNode(b->a, NodeKind::Text);
            text->data = stored;
            Append(b->a, parent, text);
        }
    }
}

static bool ClosesP(Str name) {
    if (!name.s || name.len == 0) return false;
    char first = name.s[0];
    if (first >= 'A' && first <= 'Z') first = (char)(first + ('a' - 'A'));
    switch (first) {
        case 'a':
            return SeqStrContainsI(kBlockA, name);
        case 'b':
            return StrEq(name, StrL("blockquote"));
        case 'c':
            return StrEq(name, StrL("center"));
        case 'd':
            return SeqStrContainsI(kBlockD, name);
        case 'f':
            return SeqStrContainsI(kBlockF, name);
        case 'h':
            return SeqStrContainsI(kBlockH, name);
        case 'l':
            return StrEq(name, StrL("listing"));
        case 'm':
            return SeqStrContainsI(kBlockM, name);
        case 'n':
            return StrEq(name, StrL("nav"));
        case 'o':
            return StrEq(name, StrL("ol"));
        case 'p':
            return SeqStrContainsI(kBlockP, name);
        case 's':
            return SeqStrContainsI(kBlockS, name);
        case 't':
            return StrEq(name, StrL("table"));
        case 'u':
            return StrEq(name, StrL("ul"));
        default:
            return false;
    }
}

static void CloseNamed(Builder* b, Str name) {
    int at = OpenIndex(b, name);
    if (at >= 0) b->open.Truncate(at);
}

static void CloseImplied(Builder* b, Str name) {
    if (ClosesP(name)) CloseNamed(b, StrL("p"));
    if (StrEq(name, StrL("li"))) {
        int at = OpenIndex(b, StrL("li"));
        if (at >= 0) b->open.Truncate(at);
    }
    if (StrEq(name, StrL("dt")) || StrEq(name, StrL("dd"))) {
        int dt = OpenIndex(b, StrL("dt"));
        int dd = OpenIndex(b, StrL("dd"));
        int at = dt > dd ? dt : dd;
        if (at >= 0) b->open.Truncate(at);
    }
    if (StrEq(name, StrL("tr"))) {
        int at = OpenIndex(b, StrL("tr"));
        if (at >= 0) b->open.Truncate(at);
    }
    if (StrEq(name, StrL("td")) || StrEq(name, StrL("th"))) {
        int td = OpenIndex(b, StrL("td"));
        int th = OpenIndex(b, StrL("th"));
        int at = td > th ? td : th;
        if (at >= 0) b->open.Truncate(at);
    }
    if (name.len == 2 && name.s[0] == 'h' && name.s[1] >= '1' &&
        name.s[1] <= '6') {
        for (int i = b->open.len - 1; i >= 0; i--) {
            Str n = NodeName(b->a, b->open[i]);
            if (n.len == 2 && n.s[0] == 'h' && n.s[1] >= '1' && n.s[1] <= '6') {
                b->open.Truncate(i);
                break;
            }
        }
    }
}

static void MergeAttrs(Arena* a, Node* node, const Attribute* attrs) {
    for (; attrs; attrs = AttributeNext(a, attrs)) {
        bool exists = false;
        for (Attribute* at = NodeAttrs(a, node); at;
             at = AttributeNext(a, at)) {
            if (StrEq(AttributeName(a, at), AttributeName(a, attrs))) {
                exists = true;
            }
        }
        if (exists) continue;
        Attribute* copy = ArenaNew<Attribute>(a);
        *copy = *attrs;
        copy->next = node->attrs;
        node->attrs = ArenaPtrOf(a, copy);
    }
}

static Node* PushElement(Builder* b, const Token* token,
                         Namespace ns = Namespace::Html) {
    Str name = TokenName(b->a, token);
    Node* table = nullptr;
    bool foster = false;
    Node* parent = InsertionParent(b, name, false, &table, &foster);
    Node* node = ElementFromToken(b, token, ns);
    if (foster) {
        InsertBefore(b->a, table, node);
    } else {
        Append(b->a, parent, node);
    }
    if (!token->selfClosing && !IsVoid(name)) {
        b->open.Append(b->a, node);
    }
    return node;
}

static void StartTag(Builder* b, const Token* token) {
    Str name = TokenName(b->a, token);
    if (!b->fragment && StrEq(name, StrL("html"))) {
        Node* html = EnsureWrapper(b, &b->html, StrL("html"), b->doc);
        html->implicit = false;
        MergeAttrs(b->a, html, TokenAttrs(b->a, token));
        if (b->open.len == 0) b->open.Append(b->a, html);
        return;
    }
    if (!b->fragment && StrEq(name, StrL("head"))) {
        EnsureWrapper(b, &b->html, StrL("html"), b->doc);
        Node* head = EnsureWrapper(b, &b->head, StrL("head"), b->html);
        head->implicit = false;
        MergeAttrs(b->a, head, TokenAttrs(b->a, token));
        if (!HasOpen(b, StrL("head"))) b->open.Append(b->a, head);
        return;
    }
    if (!b->fragment && StrEq(name, StrL("body"))) {
        Body(b)->implicit = false;
        MergeAttrs(b->a, b->body, TokenAttrs(b->a, token));
        while (b->open.len && b->open[b->open.len - 1] != b->html)
            b->open.Pop();
        if (!HasOpen(b, StrL("html"))) b->open.Append(b->a, b->html);
        b->open.Append(b->a, b->body);
        return;
    }
    if (!b->fragment && !b->body && SeqStrContainsI(kHeadElements, name)) {
        EnsureWrapper(b, &b->html, StrL("html"), b->doc);
        Node* head = EnsureWrapper(b, &b->head, StrL("head"), b->html);
        Node* node = ElementFromToken(b, token);
        Append(b->a, head, node);
        if (!token->selfClosing && !IsVoid(name)) {
            b->open.Append(b->a, node);
        }
        return;
    }
    Body(b);
    if (!b->fragment && b->open.len == 0) {
        b->open.Append(b->a, b->html);
        b->open.Append(b->a, b->body);
    } else if (!b->fragment && Current(b) == b->html) {
        b->open.Append(b->a, b->body);
    }

    CloseImplied(b, name);
    if (StrEq(name, StrL("tr")) &&
        StrEq(NodeName(b->a, Current(b)), StrL("table"))) {
        Node* tbody = Element(b, StrL("tbody"), nullptr);
        tbody->implicit = true;
        Append(b->a, Current(b), tbody);
        b->open.Append(b->a, tbody);
    } else if ((StrEq(name, StrL("td")) || StrEq(name, StrL("th"))) &&
               StrEq(NodeName(b->a, Current(b)), StrL("table"))) {
        Node* tbody = Element(b, StrL("tbody"), nullptr);
        tbody->implicit = true;
        Append(b->a, Current(b), tbody);
        b->open.Append(b->a, tbody);
        Node* tr = Element(b, StrL("tr"), nullptr);
        tr->implicit = true;
        Append(b->a, Current(b), tr);
        b->open.Append(b->a, tr);
    } else if ((StrEq(name, StrL("td")) || StrEq(name, StrL("th"))) &&
               (StrEq(NodeName(b->a, Current(b)), StrL("tbody")) ||
                StrEq(NodeName(b->a, Current(b)), StrL("thead")) ||
                StrEq(NodeName(b->a, Current(b)), StrL("tfoot")))) {
        Node* tr = Element(b, StrL("tr"), nullptr);
        tr->implicit = true;
        Append(b->a, Current(b), tr);
        b->open.Append(b->a, tr);
    }
    Namespace ns = Current(b)->ns;
    if (StrEq(name, StrL("svg")))
        ns = Namespace::Svg;
    else if (StrEq(name, StrL("math")))
        ns = Namespace::MathMl;
    PushElement(b, token, ns);
}

static void EndFormatting(Builder* b, Str name) {
    int at = OpenIndex(b, name);
    if (at < 0) return;
    ArenaVec<Node*> reopen;
    for (int i = at + 1; i < b->open.len; i++) {
        if (IsFormatting(NodeName(b->a, b->open[i]))) {
            reopen.Append(b->a, b->open[i]);
        }
    }
    Node* parent = NodeParent(b->a, b->open[at]);
    b->open.Truncate(at);
    for (int i = 0; i < len(reopen); i++) {
        Node* old = reopen[i];
        Node* node =
            Element(b, NodeName(b->a, old), NodeAttrs(b->a, old), old->ns);
        Append(b->a, parent, node);
        b->open.Append(b->a, node);
        parent = node;
    }
}

static void EndTag(Builder* b, const Token* token) {
    Str name = TokenName(b->a, token);
    if (StrEq(name, StrL("head"))) {
        CloseNamed(b, StrL("head"));
        return;
    }
    if (StrEq(name, StrL("body")) || StrEq(name, StrL("html"))) {
        while (b->open.len && b->open[b->open.len - 1] != b->html)
            b->open.Pop();
        return;
    }
    if (IsFormatting(name)) {
        EndFormatting(b, name);
        return;
    }
    int at = OpenIndex(b, name);
    if (at >= 0) b->open.Truncate(at);
}

static void BuildToken(void* user, const Token* token) {
    Builder* b = (Builder*)user;
    switch (token->kind) {
        case TokenKind::Character:
        case TokenKind::NullCharacter:
            AppendText(b, token->data);
            break;
        case TokenKind::Comment: {
            Node* comment = NewNode(b->a, NodeKind::Comment);
            comment->data = token->data;
            Append(b->a, Current(b), comment);
            break;
        }
        case TokenKind::Doctype:
            if (!b->options.dropDoctype && !b->fragment) {
                Node* node =
                    NewNode(b->a, NodeKind::Doctype, TokenName(b->a, token));
                node->data = token->data;
                node->systemId = token->systemId;
                Append(b->a, b->doc, node);
            }
            break;
        case TokenKind::StartTag:
            StartTag(b, token);
            break;
        case TokenKind::EndTag:
            EndTag(b, token);
            break;
        default:
            break;
    }
}

static Node* Parse(Arena* a, Str source, Str context, ParseOptions options,
                   bool fragment) {
    if (!a) return nullptr;
    Builder builder;
    builder.a = a;
    builder.options = options;
    builder.fragment = fragment;
    builder.context = context;
    builder.doc = NewNode(a, NodeKind::Document);
    if (fragment) {
        builder.doc->name =
            context.s ? LowerCopy(a, context) : ArenaStrDup(a, StrL("body"));
        builder.doc->ns = StrEqI(context, StrL("svg"))    ? Namespace::Svg
                          : StrEqI(context, StrL("math")) ? Namespace::MathMl
                                                          : Namespace::Html;
    }
    TokenizerOptions tokenizer = options.tokenizer;
    tokenizer.exactErrors = tokenizer.exactErrors || options.exactErrors;
    if (fragment && (SeqStrContainsI(kRawElements, context) ||
                     SeqStrContainsI(kRcdataElements, context))) {
        Scanner scanner;
        scanner.a = a;
        scanner.source = source;
        scanner.sink = BuildToken;
        scanner.user = &builder;
        scanner.options = tokenizer;
        scanner.rawName = context;
        scanner.rcdata = SeqStrContainsI(kRcdataElements, context);
        TokenizeRun(&scanner);
    } else {
        Tokenize(a, source, BuildToken, &builder, tokenizer);
    }
    if (!fragment) Body(&builder);
    return builder.doc;
}

Node* ParseDocument(Arena* a, Str source, ParseOptions options) {
    return Parse(a, source, {}, options, false);
}

Node* ParseFragment(Arena* a, Str source, Str context, ParseOptions options) {
    return Parse(a, source, context, options, true);
}

const Attribute* Attr(Arena* a, const Node* node, Str name) {
    if (!node) return nullptr;
    for (const Attribute* attr = NodeAttrs(a, node); attr;
         attr = AttributeNext(a, attr)) {
        if (StrEqI(AttributeName(a, attr), name)) return attr;
    }
    return nullptr;
}

Str AttrValue(Arena* a, const Node* node, Str name) {
    return AttributeValue(a, Attr(a, node, name));
}

static void WriteEscaped(StrBuilder& out, Str value, bool attribute) {
    for (int i = 0; i < value.len; i++) {
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

static void WriteNode(Arena* a, StrBuilder& out, const Node* node,
                      bool include) {
    if (!node) return;
    bool element = node->kind == NodeKind::Element;
    if (include) {
        if (node->kind == NodeKind::Text) {
            const Node* parent = NodeParent(a, node);
            if (parent && SeqStrContainsI(kRawElements, NodeName(a, parent)))
                out.Append(NodeData(a, node));
            else
                WriteEscaped(out, NodeData(a, node), false);
        } else if (node->kind == NodeKind::Comment) {
            out.Append(StrL("<!--"));
            out.Append(NodeData(a, node));
            out.Append(StrL("-->"));
        } else if (node->kind == NodeKind::Doctype) {
            out.Append(StrL("<!DOCTYPE "));
            out.Append(NodeName(a, node));
            out.AppendChar('>');
        } else if (element) {
            out.AppendChar('<');
            out.Append(NodeName(a, node));
            for (const Attribute* attr = NodeAttrs(a, node); attr;
                 attr = AttributeNext(a, attr)) {
                out.AppendChar(' ');
                out.Append(AttributeName(a, attr));
                out.Append(StrL("=\""));
                WriteEscaped(out, AttributeValue(a, attr), true);
                out.AppendChar('"');
            }
            out.AppendChar('>');
        }
    }
    if (node->kind != NodeKind::Text && node->kind != NodeKind::Comment &&
        node->kind != NodeKind::Doctype) {
        for (const Node* child = NodeFirst(a, node); child;
             child = NodeNext(a, child)) {
            WriteNode(a, out, child, true);
        }
    }
    if (include && element && !IsVoid(NodeName(a, node))) {
        out.Append(StrL("</"));
        out.Append(NodeName(a, node));
        out.AppendChar('>');
    }
}

Str Serialize(Arena* a, const Node* node, SerializeOptions options) {
    if (!a || !node) return {};
    StrBuilder out(a);
    WriteNode(a, out, node, options.includeNode);
    return out.TakeStr();
}

} // namespace html5ever
