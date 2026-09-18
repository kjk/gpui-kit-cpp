/* grammar/markdown.pest + code/markdown.rs — the Markdown grammar, as a
   recursive-descent parser over the same PEG rules. Also serves "text"
   (lint_for treats plain text as markdown).

   Part of the C++ port of the `autocorrect` crate 2.14.2 (see
   src/autocorrect/readme.md).

   What gets corrected is what the crate's format_pair corrects: `text`
   runs, `link_string`, `mark_string`, html `inner_text`, `<!-- comments -->`
   and front-matter values; fenced code blocks re-dispatch on their language
   (EmitCodeblock); link hrefs, wikilinks, inline code, tags and fences pass
   through. A block that contains CJK disables the halfwidth-punctuation
   rule for its children, the crate's Markdown hotfix.

   The parse builds a small pair tree first (arena nodes, byte spans), then
   walks it in document order feeding Results; the gaps between child spans
   are the literals pest consumed without making pairs, emitted as ignores
   so every byte passes the cursor exactly once. */

#include "autocorrect/internal.h"

namespace autocorrect {

namespace {

enum class MdRule : uint8_t {
    Container, // recurse children; a leaf Container is all gap
    Block,     // the CJK halfwidth hotfix wrapper
    Text,
    String,
    LinkString,
    MarkString,
    InnerText,
    Comment,
    Codeblock,
};

struct MdNode {
    MdRule rule = MdRule::Container;
    int start = 0;
    int end = 0;
    MdNode* firstChild = nullptr;
    MdNode* lastChild = nullptr;
    MdNode* next = nullptr;
    // Codeblock only.
    int langStart = 0;
    int langEnd = 0;
    int codeStart = 0;
    int codeEnd = 0;
};

struct MdParser {
    Str s = {};
    base::Arena* a = nullptr;
    int depth = 0;
};

const int kMaxDepth = 200;

MdNode* NewNode(MdParser* p, MdRule rule, int start) {
    MdNode* n = base::ArenaNew<MdNode>(p->a);
    n->rule = rule;
    n->start = start;
    return n;
}

void AddChild(MdNode* parent, MdNode* child) {
    if (!parent->firstChild) {
        parent->firstChild = child;
    } else {
        parent->lastChild->next = child;
    }
    parent->lastChild = child;
}

bool AtLit(Str s, int i, const char* lit) {
    for (int k = 0; lit[k]; k++) {
        if (i + k >= len(s) || s.s[i + k] != lit[k]) {
            return false;
        }
    }
    return true;
}

// newline = "\n" | "\r\n"
int MatchNewline(Str s, int i) {
    if (i < len(s) && s.s[i] == '\n') {
        return 1;
    }
    if (i + 1 < len(s) && s.s[i] == '\r' && s.s[i + 1] == '\n') {
        return 2;
    }
    return -1;
}

bool IsIdentifierCh(char c) {
    return c == '_' || c == '-' || c == '.' || (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

bool MdIsAsciiAlnumCh(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9');
}

MdNode* ParseInline(MdParser* p, int* pos);

// Whether an inline construct matches at `i` — the `!(inline)` used inside
// string and mark_string. The nodes an attempt builds are arena garbage.
bool InlineStartsAt(MdParser* p, int i) {
    char c = i < len(p->s) ? p->s.s[i] : 0;
    if (c != '[' && c != '!' && c != '`' && c != '*' && c != '~' && c != '"') {
        return false;
    }
    int at = i;
    return ParseInline(p, &at) != nullptr;
}

// string = @{ (!(newline | inline) ~ ANY)+ } → the matched end, or start.
int ScanString(MdParser* p, int i) {
    Str s = p->s;
    int at = i;
    while (at < len(s)) {
        char c = s.s[at];
        if (c == '\n' ||
            (c == '\r' && at + 1 < len(s) && s.s[at + 1] == '\n')) {
            break;
        }
        if ((c == '[' || c == '!' || c == '`' || c == '*' || c == '~' ||
             c == '"') &&
            InlineStartsAt(p, at)) {
            break;
        }
        at++;
    }
    return at;
}

// ─── inline constructs ────────────────────────────────────────────────────

// wikilinks = "[[" ~ (!("]]") ~ ANY)* ~ "]]"
MdNode* ParseWikilinks(MdParser* p, int* pos) {
    Str s = p->s;
    int i = *pos;
    if (!AtLit(s, i, "[[")) {
        return nullptr;
    }
    for (int at = i + 2; at + 1 < len(s); at++) {
        if (s.s[at] == ']' && s.s[at + 1] == ']') {
            MdNode* n = NewNode(p, MdRule::Container, i);
            n->end = at + 2;
            *pos = n->end;
            return n;
        }
    }
    return nullptr;
}

// paren = { "(" ~ inner_paren ~ paren* ~ inner_paren* ~ ")" | "(" ~ ")" }
// inner_paren = (!(newline | "(" | ")") ~ ANY)+
int MatchParen(Str s, int i) {
    if (i >= len(s) || s.s[i] != '(') {
        return -1;
    }
    int at = i + 1;
    if (at < len(s) && s.s[at] == ')') {
        return at + 1 - i;
    }
    auto inner = [&s](int from) {
        int j = from;
        while (j < len(s) && s.s[j] != '\n' && s.s[j] != '(' && s.s[j] != ')' &&
               !(s.s[j] == '\r' && j + 1 < len(s) && s.s[j + 1] == '\n')) {
            j++;
        }
        return j;
    };
    int j = inner(at);
    if (j == at) {
        return -1;
    }
    at = j;
    for (;;) {
        int sub = MatchParen(s, at);
        if (sub < 0) {
            break;
        }
        at += sub;
    }
    at = inner(at);
    if (at >= len(s) || s.s[at] != ')') {
        return -1;
    }
    return at + 1 - i;
}

MdNode* ParseMark(MdParser* p, int* pos);

// link = ${ link_string_wrap ~ href? }
// link_string_wrap = { "[" ~ (mark* ~ link_string ~ mark*) ~ "]" }
// link_string = @{ (!("]") ~ ANY)* }
MdNode* ParseLink(MdParser* p, int* pos) {
    Str s = p->s;
    int i = *pos;
    if (i >= len(s) || s.s[i] != '[') {
        return nullptr;
    }
    MdNode* n = NewNode(p, MdRule::Container, i);
    int at = i + 1;
    for (;;) {
        int save = at;
        MdNode* mark = ParseMark(p, &at);
        if (!mark) {
            at = save;
            break;
        }
        AddChild(n, mark);
    }
    int stringStart = at;
    while (at < len(s) && s.s[at] != ']') {
        at++;
    }
    if (at >= len(s)) {
        return nullptr;
    }
    MdNode* ls = NewNode(p, MdRule::LinkString, stringStart);
    ls->end = at;
    AddChild(n, ls);
    at++; // ']'
    int href = MatchParen(s, at);
    if (href > 0) {
        at += href;
    }
    n->end = at;
    *pos = at;
    return n;
}

// code = ${ "`" ~ (!(newline | "`") ~ ANY)* ~ "`" }
MdNode* ParseCodeInline(MdParser* p, int* pos) {
    Str s = p->s;
    int i = *pos;
    if (i >= len(s) || s.s[i] != '`') {
        return nullptr;
    }
    int at = i + 1;
    while (at < len(s) && s.s[at] != '`' && s.s[at] != '\n' &&
           !(s.s[at] == '\r' && at + 1 < len(s) && s.s[at + 1] == '\n')) {
        at++;
    }
    if (at >= len(s) || s.s[at] != '`') {
        return nullptr;
    }
    MdNode* n = NewNode(p, MdRule::Container, i);
    n->end = at + 1;
    *pos = n->end;
    return n;
}

// mark = ${ code | PUSH(open_mark) ~ (mark | mark_string) ~ POP }
// open_mark = "***" | "**" | "*" | "~~" | "\""
MdNode* ParseMark(MdParser* p, int* pos) {
    if (p->depth >= kMaxDepth) {
        return nullptr;
    }
    Str s = p->s;
    int i = *pos;
    MdNode* code = ParseCodeInline(p, pos);
    if (code) {
        return code;
    }
    static const char* const kMarks[] = {"***", "**", "*", "~~", "\""};
    for (const char* open : kMarks) {
        if (!AtLit(s, i, open)) {
            continue;
        }
        int openLen = 0;
        while (open[openLen]) {
            openLen++;
        }
        int at = i + openLen;
        MdNode* n = NewNode(p, MdRule::Container, i);
        p->depth++;
        int save = at;
        MdNode* nested = ParseMark(p, &at);
        p->depth--;
        if (nested) {
            AddChild(n, nested);
        } else {
            at = save;
            // mark_string = { (!(PEEK | inline) ~ ANY)* }
            int stringStart = at;
            while (at < len(s) && !AtLit(s, at, open)) {
                char c = s.s[at];
                if ((c == '[' || c == '!' || c == '`' || c == '*' || c == '~' ||
                     c == '"')) {
                    p->depth++;
                    bool isInline = InlineStartsAt(p, at);
                    p->depth--;
                    if (isInline) {
                        break;
                    }
                }
                at++;
            }
            MdNode* ms = NewNode(p, MdRule::MarkString, stringStart);
            ms->end = at;
            AddChild(n, ms);
        }
        if (!AtLit(s, at, open)) {
            // close_mark = POP failed. PEG choice does not reopen a shorter
            // open_mark once one alternative matched, so mark fails whole.
            return nullptr;
        }
        n->end = at + openLen;
        *pos = n->end;
        return n;
    }
    return nullptr;
}

// img = ${ "!" ~ link }
MdNode* ParseImg(MdParser* p, int* pos) {
    Str s = p->s;
    int i = *pos;
    if (i >= len(s) || s.s[i] != '!') {
        return nullptr;
    }
    int at = i + 1;
    MdNode* link = ParseLink(p, &at);
    if (!link) {
        return nullptr;
    }
    MdNode* n = NewNode(p, MdRule::Container, i);
    n->end = at;
    AddChild(n, link);
    *pos = at;
    return n;
}

// inline = ${ wikilinks | img | link | code | mark }
MdNode* ParseInline(MdParser* p, int* pos) {
    if (p->depth >= kMaxDepth) {
        return nullptr;
    }
    p->depth++;
    MdNode* n = ParseWikilinks(p, pos);
    if (!n) {
        n = ParseImg(p, pos);
    }
    if (!n) {
        n = ParseLink(p, pos);
    }
    if (!n) {
        n = ParseCodeInline(p, pos);
    }
    if (!n) {
        n = ParseMark(p, pos);
    }
    p->depth--;
    return n;
}

// ─── comment / html ───────────────────────────────────────────────────────

// comment = ${ "<!--" ~ (!("-->") ~ ANY)* ~ "-->" }
MdNode* ParseComment(MdParser* p, int* pos) {
    Str s = p->s;
    int i = *pos;
    if (!AtLit(s, i, "<!--")) {
        return nullptr;
    }
    for (int at = i + 4; at + 3 <= len(s); at++) {
        if (AtLit(s, at, "-->")) {
            MdNode* n = NewNode(p, MdRule::Comment, i);
            n->end = at + 3;
            *pos = n->end;
            return n;
        }
    }
    return nullptr;
}

// tag_self = "<" ~ … ~ "/>"; tag_start stops at the first '/' or '>';
// tag_end = "</" ~ (!">" ~ ANY)* ~ ">".
int MatchTagSelf(Str s, int i) {
    if (i >= len(s) || s.s[i] != '<') {
        return -1;
    }
    int at = i + 1;
    while (at < len(s)) {
        if (s.s[at] == '>') {
            return -1;
        }
        if (s.s[at] == '/' && at + 1 < len(s) && s.s[at + 1] == '>') {
            return at + 2 - i;
        }
        at++;
    }
    return -1;
}

int MatchTagStart(Str s, int i) {
    if (i >= len(s) || s.s[i] != '<') {
        return -1;
    }
    int at = i + 1;
    while (at < len(s)) {
        if (s.s[at] == '/') {
            return -1;
        }
        if (s.s[at] == '>') {
            return at + 1 - i;
        }
        at++;
    }
    return -1;
}

int MatchTagEnd(Str s, int i) {
    if (!AtLit(s, i, "</")) {
        return -1;
    }
    for (int at = i + 2; at < len(s); at++) {
        if (s.s[at] == '>') {
            return at + 1 - i;
        }
    }
    return -1;
}

// html = { tag_self | tag_start ~ ws* ~ inner_html* ~ ws* ~ tag_end }
MdNode* ParseHtml(MdParser* p, int* pos) {
    if (p->depth >= kMaxDepth) {
        return nullptr;
    }
    Str s = p->s;
    int i = *pos;
    int n = MatchTagSelf(s, i);
    if (n > 0) {
        MdNode* node = NewNode(p, MdRule::Container, i);
        node->end = i + n;
        *pos = node->end;
        return node;
    }
    n = MatchTagStart(s, i);
    if (n < 0) {
        return nullptr;
    }
    MdNode* node = NewNode(p, MdRule::Container, i);
    int at = i + n;
    // ws* — spaces and newlines right after the start tag are pest's ws
    // pairs, not inner_text.
    while (at < len(s) && (s.s[at] == ' ' || MatchNewline(s, at) > 0)) {
        at += s.s[at] == ' ' ? 1 : MatchNewline(s, at);
    }
    // inner_html* = (html | inner_text)*
    for (;;) {
        p->depth++;
        int save = at;
        MdNode* sub = ParseHtml(p, &at);
        p->depth--;
        if (sub) {
            AddChild(node, sub);
            continue;
        }
        at = save;
        // inner_text = { (!("<" | ">") ~ ANY)+ }
        int textStart = at;
        while (at < len(s) && s.s[at] != '<' && s.s[at] != '>') {
            at++;
        }
        if (at == textStart) {
            break;
        }
        MdNode* text = NewNode(p, MdRule::InnerText, textStart);
        text->end = at;
        AddChild(node, text);
    }
    int end = MatchTagEnd(s, at);
    if (end < 0) {
        return nullptr;
    }
    node->end = at + end;
    *pos = node->end;
    return node;
}

// ─── meta info / meta tags ────────────────────────────────────────────────

// meta_wrap = "-"{3,}
int MatchMetaWrap(Str s, int i) {
    int at = i;
    while (at < len(s) && s.s[at] == '-') {
        at++;
    }
    return at - i >= 3 ? at - i : -1;
}

// meta_key = (!(":" | newline) ~ identifier)* ~ ":" ~ " "*
int MatchMetaKey(Str s, int i) {
    int at = i;
    while (at < len(s) && s.s[at] != ':' && IsIdentifierCh(s.s[at])) {
        at++;
    }
    if (at >= len(s) || s.s[at] != ':') {
        return -1;
    }
    at++;
    while (at < len(s) && s.s[at] == ' ') {
        at++;
    }
    return at - i;
}

// meta_info = ${ meta_wrap ~ newline ~ meta_pair* ~ meta_wrap ~ newline* }
// meta_pair = ${ meta_key ~ string ~ newline }
MdNode* ParseMetaInfo(MdParser* p, int* pos) {
    Str s = p->s;
    int i = *pos;
    int wrap = MatchMetaWrap(s, i);
    if (wrap < 0) {
        return nullptr;
    }
    int at = i + wrap;
    int nl = MatchNewline(s, at);
    if (nl < 0) {
        return nullptr;
    }
    at += nl;
    MdNode* node = NewNode(p, MdRule::Container, i);
    for (;;) {
        int save = at;
        int key = MatchMetaKey(s, at);
        if (key < 0) {
            break;
        }
        int valueStart = at + key;
        int valueEnd = ScanString(p, valueStart);
        if (valueEnd == valueStart) {
            at = save;
            break;
        }
        int pairNl = MatchNewline(s, valueEnd);
        if (pairNl < 0) {
            at = save;
            break;
        }
        MdNode* value = NewNode(p, MdRule::String, valueStart);
        value->end = valueEnd;
        AddChild(node, value);
        at = valueEnd + pairNl;
    }
    wrap = MatchMetaWrap(s, at);
    if (wrap < 0) {
        return nullptr;
    }
    at += wrap;
    for (;;) {
        int n = MatchNewline(s, at);
        if (n < 0) {
            break;
        }
        at += n;
    }
    node->end = at;
    *pos = at;
    return node;
}

// meta_tags = @{ meta_key ~ (meta_tags_item ~ " "* "," " "*)+ ~
//                meta_tags_item ~ newline } — the whole line is ignored.
bool IsMetaTagsItemCh(Str s, int* i) {
    int at = *i;
    if (at >= len(s)) {
        return false;
    }
    char c = s.s[at];
    if (c == ',' || c == '\n' || (c == '\r' && MatchNewline(s, at) > 0)) {
        return false;
    }
    if (c == ' ' || MdIsAsciiAlnumCh(c)) {
        *i = at + 1;
        return true;
    }
    int next = at;
    uint32_t cp = Utf8Next(s, &next);
    if (IsCjk(cp)) {
        *i = next;
        return true;
    }
    return false;
}

int MatchMetaTags(Str s, int i) {
    int key = MatchMetaKey(s, i);
    if (key < 0) {
        return -1;
    }
    int at = i + key;
    int commas = 0;
    for (;;) {
        while (IsMetaTagsItemCh(s, &at)) {
        }
        int save = at;
        while (at < len(s) && s.s[at] == ' ') {
            at++;
        }
        if (at < len(s) && s.s[at] == ',') {
            at++;
            while (at < len(s) && s.s[at] == ' ') {
                at++;
            }
            commas++;
            continue;
        }
        at = save;
        break;
    }
    if (commas == 0) {
        return -1;
    }
    int nl = MatchNewline(s, at);
    if (nl < 0) {
        return -1;
    }
    return at + nl - i;
}

// ─── blocks ───────────────────────────────────────────────────────────────

// hr = "--" ~ "-"+
int MatchHr(Str s, int i) {
    int at = i;
    while (at < len(s) && s.s[at] == '-') {
        at++;
    }
    return at - i >= 3 ? at - i : -1;
}

// codeblock = ${ "```" ~ codeblock_lang ~ codeblock_code ~ "```"
//              | indent_code+ }
// indent_code = @{ (" "{4,} | "\t") ~ (!"\n" ~ ANY)* ~ newline }
int MatchIndent(Str s, int i) {
    if (i < len(s) && s.s[i] == '\t') {
        return 1;
    }
    int at = i;
    while (at < len(s) && s.s[at] == ' ') {
        at++;
    }
    return at - i >= 4 ? at - i : -1;
}

MdNode* ParseCodeblock(MdParser* p, int* pos) {
    Str s = p->s;
    int i = *pos;
    if (AtLit(s, i, "```")) {
        int at = i + 3;
        int langStart = at;
        while (at < len(s) && IsIdentifierCh(s.s[at])) {
            at++;
        }
        int langEnd = at;
        int codeStart = at;
        while (at < len(s) && !AtLit(s, at, "```")) {
            at++;
        }
        if (at >= len(s)) {
            return nullptr;
        }
        MdNode* n = NewNode(p, MdRule::Codeblock, i);
        n->langStart = langStart;
        n->langEnd = langEnd;
        n->codeStart = codeStart;
        n->codeEnd = at;
        n->end = at + 3;
        *pos = n->end;
        return n;
    }
    int at = i;
    int lines = 0;
    for (;;) {
        int save = at;
        int indent = MatchIndent(s, at);
        if (indent < 0) {
            break;
        }
        int j = at + indent;
        while (j < len(s) && s.s[j] != '\n' &&
               !(s.s[j] == '\r' && j + 1 < len(s) && s.s[j + 1] == '\n')) {
            j++;
        }
        int nl = MatchNewline(s, j);
        if (nl < 0) {
            at = save;
            break;
        }
        at = j + nl;
        lines++;
    }
    if (lines == 0) {
        return nullptr;
    }
    MdNode* n = NewNode(p, MdRule::Codeblock, i);
    n->end = at;
    // lang and code stay empty: the crate cannot dispatch an indented block.
    n->langStart = n->langEnd = n->codeStart = n->codeEnd = i;
    *pos = at;
    return n;
}

// (inline | string)+ into `parent`; answers how many matched.
int ParseInlineOrStringSeq(MdParser* p, int* pos, MdNode* parent) {
    int matched = 0;
    for (;;) {
        int save = *pos;
        MdNode* inl = ParseInline(p, pos);
        if (inl) {
            AddChild(parent, inl);
            matched++;
            continue;
        }
        *pos = save;
        int end = ScanString(p, *pos);
        if (end == *pos) {
            break;
        }
        MdNode* str = NewNode(p, MdRule::String, *pos);
        str->end = end;
        AddChild(parent, str);
        *pos = end;
        matched++;
    }
    return matched;
}

// list_prefix = space* ~ ("*" | "-" | DIGIT ~ "." | "[" ~ (" "|"x"|"X") ~ "]")
//               ~ " "*
int MatchListPrefix(Str s, int i) {
    int at = i;
    while (at < len(s) && s.s[at] == ' ') {
        at++;
    }
    if (at >= len(s)) {
        return -1;
    }
    char c = s.s[at];
    if (c == '*' || c == '-') {
        at++;
    } else if (c >= '0' && c <= '9' && at + 1 < len(s) && s.s[at + 1] == '.') {
        at += 2;
    } else if (c == '[' && at + 2 < len(s) &&
               (s.s[at + 1] == ' ' || s.s[at + 1] == 'x' ||
                s.s[at + 1] == 'X') &&
               s.s[at + 2] == ']') {
        at += 3;
    } else {
        return -1;
    }
    while (at < len(s) && s.s[at] == ' ') {
        at++;
    }
    return at - i;
}

int MatchNewlinePlus(Str s, int i) {
    int at = i;
    int count = 0;
    for (;;) {
        int n = MatchNewline(s, at);
        if (n < 0) {
            break;
        }
        at += n;
        count++;
    }
    return count > 0 ? at - i : -1;
}

// list_item = ${ list_prefix ~ (inline | string)+ ~ newline+ }
MdNode* ParseListItem(MdParser* p, int* pos) {
    Str s = p->s;
    int i = *pos;
    int prefix = MatchListPrefix(s, i);
    if (prefix < 0) {
        return nullptr;
    }
    MdNode* n = NewNode(p, MdRule::Container, i);
    int at = i + prefix;
    if (ParseInlineOrStringSeq(p, &at, n) == 0) {
        return nullptr;
    }
    int nl = MatchNewlinePlus(s, at);
    if (nl < 0) {
        return nullptr;
    }
    n->end = at + nl;
    *pos = n->end;
    return n;
}

// list_paragraph = { indent ~ (inline | string)* ~ newline+ }
MdNode* ParseListParagraph(MdParser* p, int* pos) {
    Str s = p->s;
    int i = *pos;
    int indent = MatchIndent(s, i);
    if (indent < 0) {
        return nullptr;
    }
    MdNode* n = NewNode(p, MdRule::Container, i);
    int at = i + indent;
    ParseInlineOrStringSeq(p, &at, n);
    int nl = MatchNewlinePlus(s, at);
    if (nl < 0) {
        return nullptr;
    }
    n->end = at + nl;
    *pos = n->end;
    return n;
}

// list = { list_item ~ (list_item | list_paragraph)* }
MdNode* ParseList(MdParser* p, int* pos) {
    int i = *pos;
    int at = i;
    MdNode* first = ParseListItem(p, &at);
    if (!first) {
        return nullptr;
    }
    MdNode* n = NewNode(p, MdRule::Container, i);
    AddChild(n, first);
    for (;;) {
        int save = at;
        MdNode* item = ParseListItem(p, &at);
        if (!item) {
            at = save;
            item = ParseListParagraph(p, &at);
        }
        if (!item) {
            at = save;
            break;
        }
        AddChild(n, item);
    }
    n->end = at;
    *pos = at;
    return n;
}

// block_item = { block_prefix ~ space* ~ (inline | string)+ }
// block_prefix = "######" … "#" | ">"
MdNode* ParseBlockItem(MdParser* p, int* pos) {
    Str s = p->s;
    int i = *pos;
    int at = i;
    if (at < len(s) && s.s[at] == '>') {
        at++;
    } else {
        int hashes = 0;
        while (at < len(s) && s.s[at] == '#' && hashes < 6) {
            at++;
            hashes++;
        }
        if (hashes == 0) {
            return nullptr;
        }
    }
    while (at < len(s) && s.s[at] == ' ') {
        at++;
    }
    MdNode* n = NewNode(p, MdRule::Container, i);
    if (ParseInlineOrStringSeq(p, &at, n) == 0) {
        return nullptr;
    }
    n->end = at;
    *pos = at;
    return n;
}

// text = @{ string ~ (newline ~ string)* } — one atomic pair.
int MatchText(MdParser* p, int i) {
    int at = ScanString(p, i);
    if (at == i) {
        return -1;
    }
    for (;;) {
        int nl = MatchNewline(p->s, at);
        if (nl < 0) {
            break;
        }
        int next = ScanString(p, at + nl);
        if (next == at + nl) {
            break;
        }
        at = next;
    }
    return at - i;
}

// paragraph = { (inline | text)+ }
MdNode* ParseParagraph(MdParser* p, int* pos) {
    int i = *pos;
    MdNode* n = NewNode(p, MdRule::Container, i);
    int matched = 0;
    for (;;) {
        int save = *pos;
        MdNode* inl = ParseInline(p, pos);
        if (inl) {
            AddChild(n, inl);
            matched++;
            continue;
        }
        *pos = save;
        int text = MatchText(p, *pos);
        if (text < 0) {
            break;
        }
        MdNode* t = NewNode(p, MdRule::Text, *pos);
        t->end = *pos + text;
        AddChild(n, t);
        *pos = t->end;
        matched++;
    }
    if (matched == 0) {
        return nullptr;
    }
    n->end = *pos;
    return n;
}

// block = ${ meta_tags | hr | list | codeblock | block_item | paragraph }
MdNode* ParseBlock(MdParser* p, int* pos) {
    Str s = p->s;
    int i = *pos;
    MdNode* block = NewNode(p, MdRule::Block, i);
    int tags = MatchMetaTags(s, i);
    if (tags > 0) {
        block->end = i + tags;
        *pos = block->end;
        return block;
    }
    int hr = MatchHr(s, i);
    if (hr > 0) {
        block->end = i + hr;
        *pos = block->end;
        return block;
    }
    int at = i;
    MdNode* sub = ParseList(p, &at);
    if (!sub) {
        at = i;
        sub = ParseCodeblock(p, &at);
    }
    if (!sub) {
        at = i;
        sub = ParseBlockItem(p, &at);
    }
    if (!sub) {
        at = i;
        sub = ParseParagraph(p, &at);
    }
    if (!sub) {
        return nullptr;
    }
    AddChild(block, sub);
    block->end = at;
    *pos = at;
    return block;
}

// td_tag = @{ space* ~ "|" ~ space* }
int MatchTdTag(Str s, int i) {
    int at = i;
    while (at < len(s) && s.s[at] == ' ') {
        at++;
    }
    if (at >= len(s) || s.s[at] != '|') {
        return -1;
    }
    at++;
    while (at < len(s) && s.s[at] == ' ') {
        at++;
    }
    return at - i;
}

// ─── the walk ─────────────────────────────────────────────────────────────

void WalkNode(Results* res, Str raw, const MdNode* n);

void WalkChildren(Results* res, Str raw, const MdNode* n) {
    int at = n->start;
    for (const MdNode* child = n->firstChild; child; child = child->next) {
        if (child->start > at) {
            EmitIgnore(res, Str(raw.s + at, child->start - at));
        }
        WalkNode(res, raw, child);
        at = child->end;
    }
    if (n->end > at) {
        EmitIgnore(res, Str(raw.s + at, n->end - at));
    }
}

void WalkNode(Results* res, Str raw, const MdNode* n) {
    Str span(raw.s + n->start, n->end - n->start);
    switch (n->rule) {
        case MdRule::Text:
            EmitText(res, StrL("text"), span);
            return;
        case MdRule::String:
            EmitText(res, StrL("string"), span);
            return;
        case MdRule::LinkString:
            EmitText(res, StrL("link_string"), span);
            return;
        case MdRule::MarkString:
            EmitText(res, StrL("mark_string"), span);
            return;
        case MdRule::InnerText:
            EmitText(res, StrL("inner_text"), span);
            return;
        case MdRule::Comment:
            EmitText(res, StrL("comment"), span);
            return;
        case MdRule::Codeblock: {
            Str lang(raw.s + n->langStart, n->langEnd - n->langStart);
            Str code(raw.s + n->codeStart, n->codeEnd - n->codeStart);
            EmitCodeblock(res, span, lang, code);
            return;
        }
        case MdRule::Block: {
            // The crate's hotfix: a block with CJK content disables the
            // halfwidth-punctuation rule for everything inside it.
            bool cjk = HasCjk(span);
            Toggle saved = {};
            if (cjk) {
                saved = ResultsPushCodeblockToggle(res);
            }
            WalkChildren(res, raw, n);
            if (cjk && saved.kind != ToggleKind::None) {
                res->toggle = saved;
            }
            return;
        }
        case MdRule::Container:
        default:
            if (!n->firstChild) {
                EmitIgnore(res, span);
                return;
            }
            WalkChildren(res, raw, n);
            return;
    }
}

} // namespace

void ScanMarkdown(Results* res, Str raw) {
    MdParser p;
    p.s = raw;
    p.a = res->a;
    MdNode* root = NewNode(&p, MdRule::Container, 0);
    root->end = len(raw);
    int pos = 0;
    while (pos < len(raw)) {
        // line = expr | newline; expr = comment | html | meta_info | block |
        // inline | td_tag.
        int save = pos;
        MdNode* n = ParseComment(&p, &pos);
        if (!n) {
            pos = save;
            n = ParseHtml(&p, &pos);
        }
        if (!n) {
            pos = save;
            n = ParseMetaInfo(&p, &pos);
        }
        if (!n) {
            pos = save;
            n = ParseBlock(&p, &pos);
        }
        if (!n) {
            pos = save;
            n = ParseInline(&p, &pos);
        }
        if (!n) {
            pos = save;
            int td = MatchTdTag(raw, pos);
            if (td > 0) {
                pos += td;
                continue; // an ignored leaf; the gap fill covers it
            }
            int nl = MatchNewline(raw, pos);
            if (nl > 0) {
                pos += nl;
                continue;
            }
            // Cannot happen with this grammar (text absorbs any char), but
            // never loop forever on the unexpected.
            pos++;
            continue;
        }
        AddChild(root, n);
    }
    WalkNode(res, raw, root);
}

} // namespace autocorrect
