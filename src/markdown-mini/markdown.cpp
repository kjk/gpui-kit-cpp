/* A deliberately small Markdown parser with markdown/markdown.h's API.

   It keeps the common document vocabulary used by compact applications:
   paragraphs, ATX and setext headings, emphasis, strong text, inline and
   fenced code, ordered and unordered lists, blockquotes, links, images,
   thematic breaks, escapes, hard breaks, and numeric/basic named entities.

   It deliberately does not carry the size-heavy long tail of CommonMark and
   GFM: tables, task lists, footnotes, raw HTML, autolinks, reference links,
   math/frontmatter, strikethrough, or the 2,125-name HTML entity database.
   Unsupported syntax remains readable as ordinary text. */

#include "markdown-mini/markdown.h"

namespace markdown {

using base::Alloc;

struct MiniParser {
    Arena* a = nullptr;
    Str source = {};
    const ParseOptions* options = nullptr;
};

struct MiniLine {
    int32_t start = 0;
    int32_t end = 0;
    int32_t next = 0;
};

struct MiniListMarker {
    bool valid = false;
    bool ordered = false;
    int32_t start = 1;
    int32_t indent = 0;
    int32_t content = 0;
};

static bool MiniSpace(char c) {
    return c == ' ' || c == '\t';
}

static bool MiniDigit(char c) {
    return c >= '0' && c <= '9';
}

static bool MiniHex(char c) {
    return MiniDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static bool MiniPunctuation(char c) {
    switch (c) {
        case '!':
        case '"':
        case '#':
        case '$':
        case '%':
        case '&':
        case '\'':
        case '(':
        case ')':
        case '*':
        case '+':
        case ',':
        case '-':
        case '.':
        case '/':
        case ':':
        case ';':
        case '<':
        case '=':
        case '>':
        case '?':
        case '@':
        case '[':
        case '\\':
        case ']':
        case '^':
        case '_':
        case '`':
        case '{':
        case '|':
        case '}':
        case '~':
            return true;
        default:
            return false;
    }
}

static Str MiniSlice(Str s, int32_t start, int32_t end) {
    if (!s.s || start < 0 || end < start || end > len(s)) {
        return {};
    }
    return Str(s.s + start, end - start);
}

static Str MiniTrim(Str s) {
    int32_t start = 0;
    int32_t end = len(s);
    while (start < end && MiniSpace(s.s[start])) {
        start++;
    }
    while (end > start && MiniSpace(s.s[end - 1])) {
        end--;
    }
    return MiniSlice(s, start, end);
}

static Str MiniOwn(Arena* a, Str s) {
    char* out = (char*)Alloc(a, len(s) + 1);
    if (!out) {
        return {};
    }
    if (len(s) > 0) {
        memcpy(out, s.s, (size_t)len(s));
    }
    out[len(s)] = 0;
    return Str(out, len(s));
}

static MiniLine MiniReadLine(Str source, int32_t at) {
    MiniLine line;
    line.start = at;
    while (at < len(source) && source.s[at] != '\n' && source.s[at] != '\r') {
        at++;
    }
    line.end = at;
    if (at < len(source) && source.s[at] == '\r') {
        at++;
        if (at < len(source) && source.s[at] == '\n') {
            at++;
        }
    } else if (at < len(source)) {
        at++;
    }
    line.next = at;
    return line;
}

static Str MiniLineText(Str source, const MiniLine& line) {
    return MiniSlice(source, line.start, line.end);
}

static int32_t MiniIndent(Str line) {
    int32_t n = 0;
    while (n < len(line) && n < 4) {
        if (line.s[n] == ' ') {
            n++;
        } else if (line.s[n] == '\t') {
            return 4;
        } else {
            break;
        }
    }
    return n;
}

static bool MiniBlank(Str line) {
    for (int32_t i = 0; i < len(line); i++) {
        if (!MiniSpace(line.s[i])) {
            return false;
        }
    }
    return true;
}

static Node* MiniNode(MiniParser* p, NodeKind kind, Node* parent) {
    Node* node = NodeNew(p->a, kind);
    if (node && parent) {
        NodeAddChild(p->a, parent, node);
    }
    return node;
}

static void MiniText(MiniParser* p, Node* parent, Str value) {
    if (len(value) <= 0) {
        return;
    }
    Node* text = MiniNode(p, NodeKind::Text, parent);
    if (text) {
        NodeSetStr(p->a, text, NodeStrKind::Value, value);
    }
}

static int32_t MiniFind(Str text, int32_t at, char marker, int32_t count) {
    for (int32_t i = at; i + count <= len(text); i++) {
        if (text.s[i] == '\\') {
            i++;
            continue;
        }
        int32_t n = 0;
        while (i + n < len(text) && text.s[i + n] == marker) {
            n++;
        }
        if (n >= count && i > at && !MiniSpace(text.s[i - 1])) {
            return i;
        }
        i += n > 0 ? n - 1 : 0;
    }
    return -1;
}

static int32_t MiniBracketEnd(Str text, int32_t at) {
    int32_t depth = 1;
    for (int32_t i = at; i < len(text); i++) {
        if (text.s[i] == '\\' && i + 1 < len(text)) {
            i++;
            continue;
        }
        if (text.s[i] == '[') {
            depth++;
        } else if (text.s[i] == ']' && --depth == 0) {
            return i;
        }
    }
    return -1;
}

static int32_t MiniResourceEnd(Str text, int32_t at) {
    int32_t depth = 1;
    char quote = 0;
    for (int32_t i = at; i < len(text); i++) {
        char c = text.s[i];
        if (c == '\\' && i + 1 < len(text)) {
            i++;
            continue;
        }
        if (quote) {
            if (c == quote) {
                quote = 0;
            }
            continue;
        }
        if (c == '"' || c == '\'') {
            quote = c;
        } else if (c == '(') {
            depth++;
        } else if (c == ')' && --depth == 0) {
            return i;
        }
    }
    return -1;
}

static void MiniInline(MiniParser* p, Node* parent, Str text);

static bool MiniLink(MiniParser* p, Node* parent, Str text, int32_t at,
                     int32_t* after) {
    bool image = text.s[at] == '!';
    int32_t open = at + (image ? 1 : 0);
    if (open >= len(text) || text.s[open] != '[') {
        return false;
    }
    if (image && !p->options->constructs.labelStartImage) {
        return false;
    }
    if (!image && !p->options->constructs.labelStartLink) {
        return false;
    }
    int32_t close = MiniBracketEnd(text, open + 1);
    if (close < 0 || close + 1 >= len(text) || text.s[close + 1] != '(') {
        return false;
    }
    int32_t resourceEnd = MiniResourceEnd(text, close + 2);
    if (resourceEnd < 0) {
        return false;
    }

    Str body = MiniTrim(MiniSlice(text, close + 2, resourceEnd));
    Str url = {};
    Str title = {};
    if (len(body) > 1 && body.s[0] == '<') {
        int32_t end = 1;
        while (end < len(body) && body.s[end] != '>') {
            end++;
        }
        if (end >= len(body)) {
            return false;
        }
        url = MiniSlice(body, 1, end);
        body = MiniTrim(MiniSlice(body, end + 1, len(body)));
    } else {
        int32_t end = 0;
        int32_t depth = 0;
        while (end < len(body)) {
            char c = body.s[end];
            if (c == '(') {
                depth++;
            } else if (c == ')' && depth > 0) {
                depth--;
            } else if (MiniSpace(c) && depth == 0) {
                break;
            }
            end++;
        }
        url = MiniSlice(body, 0, end);
        body = MiniTrim(MiniSlice(body, end, len(body)));
    }
    if (len(body) >= 2) {
        char first = body.s[0];
        char last = body.s[len(body) - 1];
        if ((first == '"' && last == '"') || (first == '\'' && last == '\'') ||
            (first == '(' && last == ')')) {
            title = MiniSlice(body, 1, len(body) - 1);
        }
    }

    Str label = MiniSlice(text, open + 1, close);
    Node* node = MiniNode(p, image ? NodeKind::Image : NodeKind::Link, parent);
    if (!node) {
        return false;
    }
    NodeSetStr(p->a, node, NodeStrKind::Url, url);
    NodeSetStr(p->a, node, NodeStrKind::Title, title);
    if (image) {
        NodeSetStr(p->a, node, NodeStrKind::Alt, label);
    } else {
        MiniInline(p, node, label);
    }
    *after = resourceEnd + 1;
    return true;
}

static base::TempStr MiniUtf8Temp(uint32_t cp) {
    base::TempStr out = base::AllocStrTemp(4);
    if (cp < 0x80) {
        out.s[0] = (char)cp;
        out.len = 1;
        return out;
    }
    if (cp < 0x800) {
        out.s[0] = (char)(0xc0 | (cp >> 6));
        out.s[1] = (char)(0x80 | (cp & 0x3f));
        out.len = 2;
        return out;
    }
    if (cp < 0x10000) {
        out.s[0] = (char)(0xe0 | (cp >> 12));
        out.s[1] = (char)(0x80 | ((cp >> 6) & 0x3f));
        out.s[2] = (char)(0x80 | (cp & 0x3f));
        out.len = 3;
        return out;
    }
    out.s[0] = (char)(0xf0 | (cp >> 18));
    out.s[1] = (char)(0x80 | ((cp >> 12) & 0x3f));
    out.s[2] = (char)(0x80 | ((cp >> 6) & 0x3f));
    out.s[3] = (char)(0x80 | (cp & 0x3f));
    return out;
}

static bool MiniEntity(MiniParser* p, Str text, int32_t at, int32_t* after,
                       Str* value) {
    int32_t semi = at + 1;
    while (semi < len(text) && semi - at <= 33 && text.s[semi] != ';' &&
           !MiniSpace(text.s[semi])) {
        semi++;
    }
    if (semi >= len(text) || text.s[semi] != ';' || semi == at + 1) {
        return false;
    }
    Str name = MiniSlice(text, at + 1, semi);
    if (name.s[0] == '#') {
        int32_t start = 1;
        int radix = 10;
        if (start < len(name) &&
            (name.s[start] == 'x' || name.s[start] == 'X')) {
            start++;
            radix = 16;
        }
        if (start == len(name)) {
            return false;
        }
        for (int32_t i = start; i < len(name); i++) {
            if ((radix == 10 && !MiniDigit(name.s[i])) ||
                (radix == 16 && !MiniHex(name.s[i]))) {
                return false;
            }
        }
        *value = DecodeNumeric(p->a, MiniSlice(name, start, len(name)), radix);
    } else {
        *value = DecodeNamed(p->a, name);
    }
    if (!value->s) {
        return false;
    }
    *after = semi + 1;
    return true;
}

static void MiniInline(MiniParser* p, Node* parent, Str text) {
    int32_t plain = 0;
    int32_t at = 0;
    while (at < len(text)) {
        int32_t after = at;
        NodeKind markKind = NodeKind::Text;
        int32_t close = -1;
        char c = text.s[at];

        bool linkStart = c == '[' || (c == '!' && at + 1 < len(text) &&
                                      text.s[at + 1] == '[');
        bool linkEnabled = p->options->constructs.labelEnd &&
                           (c == '!' ? p->options->constructs.labelStartImage
                                     : p->options->constructs.labelStartLink);
        if (linkStart && linkEnabled) {
            int32_t labelOpen = at + (c == '!' ? 1 : 0);
            int32_t labelClose = MiniBracketEnd(text, labelOpen + 1);
            if (labelClose >= 0 && labelClose + 1 < len(text) &&
                text.s[labelClose + 1] == '(') {
                int32_t resourceEnd = MiniResourceEnd(text, labelClose + 2);
                if (resourceEnd >= 0) {
                    if (at > plain) {
                        MiniText(p, parent, MiniSlice(text, plain, at));
                    }
                    if (MiniLink(p, parent, text, at, &after)) {
                        at = after;
                        plain = at;
                        continue;
                    }
                }
            }
        }

        if (p->options->constructs.codeText && c == '`') {
            int32_t count = 1;
            while (at + count < len(text) && text.s[at + count] == '`') {
                count++;
            }
            close = MiniFind(text, at + count, '`', count);
            if (close >= 0) {
                if (at > plain) {
                    MiniText(p, parent, MiniSlice(text, plain, at));
                }
                Node* code = MiniNode(p, NodeKind::InlineCode, parent);
                Str value = MiniSlice(text, at + count, close);
                char* normalized = (char*)Alloc(p->a, len(value) + 1);
                if (normalized) {
                    for (int32_t i = 0; i < len(value); i++) {
                        normalized[i] = value.s[i] == '\n' || value.s[i] == '\r'
                                            ? ' '
                                            : value.s[i];
                    }
                    normalized[len(value)] = 0;
                    NodeSetStr(p->a, code, NodeStrKind::Value,
                               Str(normalized, len(value)));
                }
                at = close + count;
                plain = at;
                continue;
            }
        }

        if (p->options->constructs.attention && (c == '*' || c == '_')) {
            int32_t count = 1;
            while (count < 3 && at + count < len(text) &&
                   text.s[at + count] == c) {
                count++;
            }
            bool intraword = c == '_' && at > 0 && at + count < len(text) &&
                             !MiniSpace(text.s[at - 1]) &&
                             !MiniPunctuation(text.s[at - 1]) &&
                             !MiniSpace(text.s[at + count]) &&
                             !MiniPunctuation(text.s[at + count]);
            if (!intraword && at + count < len(text) &&
                !MiniSpace(text.s[at + count])) {
                close = MiniFind(text, at + count, c, count);
            }
            if (close >= 0) {
                if (at > plain) {
                    MiniText(p, parent, MiniSlice(text, plain, at));
                }
                if (count == 3) {
                    Node* strong = MiniNode(p, NodeKind::Strong, parent);
                    Node* emphasis = MiniNode(p, NodeKind::Emphasis, strong);
                    MiniInline(p, emphasis, MiniSlice(text, at + 3, close));
                } else {
                    markKind =
                        count == 2 ? NodeKind::Strong : NodeKind::Emphasis;
                    Node* marked = MiniNode(p, markKind, parent);
                    MiniInline(p, marked, MiniSlice(text, at + count, close));
                }
                at = close + count;
                plain = at;
                continue;
            }
        }

        if (c == '\\' && at + 1 < len(text)) {
            char next = text.s[at + 1];
            if (p->options->constructs.hardBreakEscape &&
                (next == '\n' || next == '\r')) {
                if (at > plain) {
                    MiniText(p, parent, MiniSlice(text, plain, at));
                }
                MiniNode(p, NodeKind::Break, parent);
                at +=
                    next == '\r' && at + 2 < len(text) && text.s[at + 2] == '\n'
                        ? 3
                        : 2;
                plain = at;
                continue;
            }
            if (p->options->constructs.characterEscape &&
                MiniPunctuation(next)) {
                if (at > plain) {
                    MiniText(p, parent, MiniSlice(text, plain, at));
                }
                MiniText(p, parent, MiniSlice(text, at + 1, at + 2));
                at += 2;
                plain = at;
                continue;
            }
        }

        if (p->options->constructs.characterReference && c == '&') {
            Str value = {};
            if (MiniEntity(p, text, at, &after, &value)) {
                if (at > plain) {
                    MiniText(p, parent, MiniSlice(text, plain, at));
                }
                MiniText(p, parent, value);
                at = after;
                plain = at;
                continue;
            }
        }

        if (c == '\n' || c == '\r') {
            int32_t textEnd = at;
            bool hard = false;
            if (p->options->constructs.hardBreakTrailing) {
                int32_t spaces = 0;
                while (textEnd > plain && text.s[textEnd - 1] == ' ') {
                    textEnd--;
                    spaces++;
                }
                hard = spaces >= 2;
                if (!hard) {
                    textEnd = at;
                }
            }
            if (textEnd > plain) {
                MiniText(p, parent, MiniSlice(text, plain, textEnd));
            }
            if (hard) {
                MiniNode(p, NodeKind::Break, parent);
            } else {
                MiniText(p, parent, StrL(" "));
            }
            at += c == '\r' && at + 1 < len(text) && text.s[at + 1] == '\n' ? 2
                                                                            : 1;
            plain = at;
            continue;
        }
        at++;
    }
    if (len(text) > plain) {
        MiniText(p, parent, MiniSlice(text, plain, len(text)));
    }
}

static bool MiniAtx(Str line, int32_t* level, Str* content) {
    int32_t at = MiniIndent(line);
    if (at > 3) {
        return false;
    }
    int32_t count = 0;
    while (at + count < len(line) && line.s[at + count] == '#' && count < 7) {
        count++;
    }
    if (count < 1 || count > 6 ||
        (at + count < len(line) && !MiniSpace(line.s[at + count]))) {
        return false;
    }
    int32_t start = at + count;
    while (start < len(line) && MiniSpace(line.s[start])) {
        start++;
    }
    int32_t end = len(line);
    while (end > start && MiniSpace(line.s[end - 1])) {
        end--;
    }
    int32_t hashEnd = end;
    while (end > start && line.s[end - 1] == '#') {
        end--;
    }
    if (end == hashEnd || (end > start && !MiniSpace(line.s[end - 1]))) {
        end = hashEnd;
    } else {
        while (end > start && MiniSpace(line.s[end - 1])) {
            end--;
        }
    }
    *level = count;
    *content = MiniSlice(line, start, end);
    return true;
}

static bool MiniSetext(Str line, int32_t* level) {
    Str value = MiniTrim(line);
    if (!StrStartsWithAny(value, "=-")) {
        return false;
    }
    char marker = value.s[0];
    int32_t count = 0;
    for (int32_t i = 0; i < len(value); i++) {
        if (value.s[i] == marker) {
            count++;
        } else if (!MiniSpace(value.s[i])) {
            return false;
        }
    }
    if (count == 0) {
        return false;
    }
    *level = marker == '=' ? 1 : 2;
    return true;
}

static bool MiniThematic(Str line) {
    Str value = MiniTrim(line);
    if (len(value) < 3 || !StrStartsWithAny(value, "*-_")) {
        return false;
    }
    char marker = value.s[0];
    int32_t count = 0;
    for (int32_t i = 0; i < len(value); i++) {
        if (value.s[i] == marker) {
            count++;
        } else if (!MiniSpace(value.s[i])) {
            return false;
        }
    }
    return count >= 3;
}

static bool MiniFence(Str line, char* marker, int32_t* count, Str* info) {
    int32_t at = MiniIndent(line);
    if (at > 3 || at >= len(line) || (line.s[at] != '`' && line.s[at] != '~')) {
        return false;
    }
    char c = line.s[at];
    int32_t n = 0;
    while (at + n < len(line) && line.s[at + n] == c) {
        n++;
    }
    if (n < 3) {
        return false;
    }
    *marker = c;
    *count = n;
    *info = MiniTrim(MiniSlice(line, at + n, len(line)));
    return true;
}

static bool MiniFenceClose(Str line, char marker, int32_t count) {
    int32_t at = MiniIndent(line);
    if (at > 3) {
        return false;
    }
    int32_t n = 0;
    while (at + n < len(line) && line.s[at + n] == marker) {
        n++;
    }
    if (n < count) {
        return false;
    }
    for (int32_t i = at + n; i < len(line); i++) {
        if (!MiniSpace(line.s[i])) {
            return false;
        }
    }
    return true;
}

static MiniListMarker MiniList(Str line) {
    MiniListMarker out;
    int32_t at = MiniIndent(line);
    if (at > 3 || at >= len(line)) {
        return out;
    }
    out.indent = at;
    char c = line.s[at];
    if (c == '-' || c == '+' || c == '*') {
        if (at + 1 < len(line) && !MiniSpace(line.s[at + 1])) {
            return out;
        }
        out.valid = true;
        out.content = at + 1 < len(line) ? at + 2 : len(line);
        while (out.content < len(line) && MiniSpace(line.s[out.content]) &&
               out.content < at + 5) {
            out.content++;
        }
        return out;
    }
    if (!MiniDigit(c)) {
        return out;
    }
    int32_t value = 0;
    int32_t digits = 0;
    while (at + digits < len(line) && MiniDigit(line.s[at + digits]) &&
           digits < 9) {
        value = value * 10 + line.s[at + digits] - '0';
        digits++;
    }
    int32_t mark = at + digits;
    if (digits == 0 || mark >= len(line) ||
        (line.s[mark] != '.' && line.s[mark] != ')') ||
        (mark + 1 < len(line) && !MiniSpace(line.s[mark + 1]))) {
        return out;
    }
    out.valid = true;
    out.ordered = true;
    out.start = value;
    out.content = mark + 1 < len(line) ? mark + 2 : len(line);
    while (out.content < len(line) && MiniSpace(line.s[out.content]) &&
           out.content < mark + 5) {
        out.content++;
    }
    return out;
}

static bool MiniQuote(Str line, int32_t* content) {
    int32_t at = MiniIndent(line);
    if (at > 3 || at >= len(line) || line.s[at] != '>') {
        return false;
    }
    at++;
    if (at < len(line) && line.s[at] == ' ') {
        at++;
    }
    *content = at;
    return true;
}

static Str MiniCopiedLines(MiniParser* p, int32_t start, int32_t end,
                           int32_t stripFirst, int32_t stripRest) {
    char* out = (char*)Alloc(p->a, end - start + 1);
    if (!out) {
        return {};
    }
    int32_t used = 0;
    int32_t at = start;
    bool first = true;
    while (at < end) {
        MiniLine line = MiniReadLine(p->source, at);
        if (line.end > end) {
            line.end = end;
            line.next = end;
        }
        Str value = MiniLineText(p->source, line);
        int32_t cut =
            first ? (stripFirst < len(value) ? stripFirst : len(value)) : 0;
        if (!first) {
            while (cut < len(value) && cut < stripRest &&
                   MiniSpace(value.s[cut])) {
                cut++;
            }
        }
        if (len(value) > cut) {
            memcpy(out + used, value.s + cut, (size_t)(len(value) - cut));
            used += len(value) - cut;
        }
        at = line.next;
        if (at < end) {
            out[used++] = '\n';
        }
        first = false;
    }
    out[used] = 0;
    return Str(out, used);
}

static void MiniBlocks(MiniParser* p, Node* parent, int32_t start, int32_t end);

static int32_t MiniCodeFence(MiniParser* p, Node* parent,
                             const MiniLine& opening, char marker,
                             int32_t count, Str info, int32_t end) {
    int32_t contentStart = opening.next;
    int32_t at = contentStart;
    int32_t contentEnd = end;
    int32_t after = end;
    while (at < end) {
        MiniLine line = MiniReadLine(p->source, at);
        if (MiniFenceClose(MiniLineText(p->source, line), marker, count)) {
            contentEnd = at;
            after = line.next;
            break;
        }
        at = line.next;
    }
    while (contentEnd > contentStart && (p->source.s[contentEnd - 1] == '\n' ||
                                         p->source.s[contentEnd - 1] == '\r')) {
        contentEnd--;
    }
    Node* code = MiniNode(p, NodeKind::Code, parent);
    NodeSetStr(p->a, code, NodeStrKind::Value,
               MiniSlice(p->source, contentStart, contentEnd));
    int32_t split = 0;
    while (split < len(info) && !MiniSpace(info.s[split])) {
        split++;
    }
    NodeSetStr(p->a, code, NodeStrKind::Lang, MiniSlice(info, 0, split));
    NodeSetStr(p->a, code, NodeStrKind::Meta,
               MiniTrim(MiniSlice(info, split, len(info))));
    return after;
}

static int32_t MiniBlockquote(MiniParser* p, Node* parent, int32_t start,
                              int32_t end) {
    int32_t at = start;
    int32_t cap = end - start + 1;
    char* out = (char*)Alloc(p->a, cap);
    if (!out) {
        return end;
    }
    int32_t used = 0;
    while (at < end) {
        MiniLine line = MiniReadLine(p->source, at);
        Str value = MiniLineText(p->source, line);
        int32_t content = 0;
        if (!MiniQuote(value, &content)) {
            if (!MiniBlank(value)) {
                break;
            }
            MiniLine next = MiniReadLine(p->source, line.next);
            int32_t ignored = 0;
            if (line.next >= end ||
                !MiniQuote(MiniLineText(p->source, next), &ignored)) {
                break;
            }
            content = len(value);
        }
        if (used > 0) {
            out[used++] = '\n';
        }
        if (len(value) > content) {
            memcpy(out + used, value.s + content,
                   (size_t)(len(value) - content));
            used += len(value) - content;
        }
        at = line.next;
    }
    out[used] = 0;
    Node* quote = MiniNode(p, NodeKind::Blockquote, parent);
    MiniParser nested = *p;
    nested.source = Str(out, used);
    MiniBlocks(&nested, quote, 0, used);
    return at;
}

static int32_t MiniListBlock(MiniParser* p, Node* parent, int32_t start,
                             int32_t end, MiniListMarker first) {
    Node* list = MiniNode(p, NodeKind::List, parent);
    list->Set(NodeOrdered, first.ordered);
    list->Set(NodeHasStart, first.ordered);
    if (first.ordered) {
        NodeSetPerKind(p->a, list, (uint32_t)first.start);
    }
    int32_t at = start;
    while (at < end) {
        MiniLine opening = MiniReadLine(p->source, at);
        Str openingText = MiniLineText(p->source, opening);
        MiniListMarker marker = MiniList(openingText);
        if (!marker.valid || marker.ordered != first.ordered ||
            marker.indent != first.indent) {
            break;
        }
        int32_t itemStart = at;
        int32_t scan = opening.next;
        int32_t itemEnd = scan;
        bool spread = false;
        bool sawBlank = false;
        while (scan < end) {
            MiniLine line = MiniReadLine(p->source, scan);
            Str value = MiniLineText(p->source, line);
            if (MiniBlank(value)) {
                sawBlank = true;
                itemEnd = line.next;
                scan = line.next;
                continue;
            }
            MiniListMarker next = MiniList(value);
            if (next.valid && next.ordered == first.ordered &&
                next.indent == first.indent) {
                spread = spread || sawBlank;
                break;
            }
            int32_t indent = MiniIndent(value);
            if (indent <= first.indent) {
                break;
            }
            spread = spread || sawBlank;
            itemEnd = line.next;
            scan = line.next;
        }
        Node* item = MiniNode(p, NodeKind::ListItem, list);
        item->Set(NodeSpread, spread);
        list->Set(NodeSpread, list->Has(NodeSpread) || spread);

        Str copied = MiniCopiedLines(p, itemStart, itemEnd, marker.content,
                                     marker.content);
        MiniParser nested = *p;
        nested.source = copied;
        MiniBlocks(&nested, item, 0, len(copied));
        at = scan;
    }
    return at;
}

static bool MiniBlockStart(MiniParser* p, Str line) {
    int32_t level = 0;
    Str content = {};
    char marker = 0;
    int32_t count = 0;
    Str info = {};
    int32_t quote = 0;
    return (p->options->constructs.headingAtx &&
            MiniAtx(line, &level, &content)) ||
           (p->options->constructs.codeFenced &&
            MiniFence(line, &marker, &count, &info)) ||
           (p->options->constructs.blockQuote && MiniQuote(line, &quote)) ||
           (p->options->constructs.thematicBreak && MiniThematic(line)) ||
           (p->options->constructs.listItem && MiniList(line).valid) ||
           (p->options->constructs.codeIndented && MiniIndent(line) >= 4);
}

static void MiniBlocks(MiniParser* p, Node* parent, int32_t start,
                       int32_t end) {
    int32_t at = start;
    while (at < end) {
        MiniLine line = MiniReadLine(p->source, at);
        Str text = MiniLineText(p->source, line);
        if (MiniBlank(text)) {
            at = line.next;
            continue;
        }

        int32_t level = 0;
        Str content = {};
        if (p->options->constructs.headingAtx &&
            MiniAtx(text, &level, &content)) {
            Node* heading = MiniNode(p, NodeKind::Heading, parent);
            NodeSetPerKind(p->a, heading, (uint32_t)level);
            MiniInline(p, heading, content);
            at = line.next;
            continue;
        }

        char fenceMarker = 0;
        int32_t fenceCount = 0;
        Str info = {};
        if (p->options->constructs.codeFenced &&
            MiniFence(text, &fenceMarker, &fenceCount, &info)) {
            at = MiniCodeFence(p, parent, line, fenceMarker, fenceCount, info,
                               end);
            continue;
        }

        int32_t quoteContent = 0;
        if (p->options->constructs.blockQuote &&
            MiniQuote(text, &quoteContent)) {
            at = MiniBlockquote(p, parent, at, end);
            continue;
        }

        if (p->options->constructs.thematicBreak && MiniThematic(text)) {
            MiniNode(p, NodeKind::ThematicBreak, parent);
            at = line.next;
            continue;
        }

        MiniListMarker list = MiniList(text);
        if (p->options->constructs.listItem && list.valid) {
            at = MiniListBlock(p, parent, at, end, list);
            continue;
        }

        if (p->options->constructs.codeIndented && MiniIndent(text) >= 4) {
            int32_t scan = at;
            while (scan < end) {
                MiniLine codeLine = MiniReadLine(p->source, scan);
                Str codeText = MiniLineText(p->source, codeLine);
                if (!MiniBlank(codeText) && MiniIndent(codeText) < 4) {
                    break;
                }
                scan = codeLine.next;
            }
            Str value = MiniCopiedLines(p, at, scan, 4, 4);
            while (len(value) > 0 && value.s[len(value) - 1] == '\n') {
                value.len--;
            }
            Node* code = MiniNode(p, NodeKind::Code, parent);
            NodeSetStr(p->a, code, NodeStrKind::Value, value);
            at = scan;
            continue;
        }

        MiniLine next = MiniReadLine(p->source, line.next);
        if (p->options->constructs.headingSetext && line.next < end &&
            MiniSetext(MiniLineText(p->source, next), &level)) {
            Node* heading = MiniNode(p, NodeKind::Heading, parent);
            NodeSetPerKind(p->a, heading, (uint32_t)level);
            MiniInline(p, heading, MiniTrim(text));
            at = next.next;
            continue;
        }

        int32_t paragraphEnd = line.end;
        int32_t scan = line.next;
        while (scan < end) {
            MiniLine part = MiniReadLine(p->source, scan);
            Str partText = MiniLineText(p->source, part);
            if (MiniBlank(partText) || MiniBlockStart(p, partText)) {
                break;
            }
            paragraphEnd = part.end;
            scan = part.next;
        }
        Node* paragraph = MiniNode(p, NodeKind::Paragraph, parent);
        MiniInline(p, paragraph,
                   MiniSlice(p->source, line.start, paragraphEnd));
        at = scan;
    }
}

Constructs Constructs::Gfm() {
    Constructs constructs;
    // These flags describe the requested dialect and remain API-compatible.
    // The mini implementation intentionally ignores the GFM-only constructs.
    constructs.gfmAutolinkLiteral = true;
    constructs.gfmFootnoteDefinition = true;
    constructs.gfmLabelStartFootnote = true;
    constructs.gfmStrikethrough = true;
    constructs.gfmTable = true;
    constructs.gfmTaskListItem = true;
    return constructs;
}

ParseOptions ParseOptions::Gfm() {
    ParseOptions options;
    options.constructs = Constructs::Gfm();
    return options;
}

Node* ToMdast(Arena* a, Str source, const ParseOptions& options) {
    if (!a) {
        return nullptr;
    }
    MiniParser parser;
    parser.a = a;
    parser.source = source;
    parser.options = &options;
    Node* root = NodeNew(a, NodeKind::Root);
    if (root && source.s && len(source) > 0) {
        MiniBlocks(&parser, root, 0, len(source));
    }
    return root;
}

Str DecodeNamed(Arena* a, Str name) {
    const char* value = nullptr;
    if (base::StrEq(name, StrL("amp")) || base::StrEq(name, StrL("AMP"))) {
        value = "&";
    } else if (base::StrEq(name, StrL("lt")) || base::StrEq(name, StrL("LT"))) {
        value = "<";
    } else if (base::StrEq(name, StrL("gt")) || base::StrEq(name, StrL("GT"))) {
        value = ">";
    } else if (base::StrEq(name, StrL("quot")) ||
               base::StrEq(name, StrL("QUOT"))) {
        value = "\"";
    } else if (base::StrEq(name, StrL("apos"))) {
        value = "'";
    } else if (base::StrEq(name, StrL("nbsp"))) {
        value = "\xc2\xa0";
    }
    return value ? MiniOwn(a, Str((char*)value)) : Str{};
}

Str DecodeNumeric(Arena* a, Str value, int radix) {
    uint32_t cp = 0;
    bool bad = len(value) <= 0 || (radix != 10 && radix != 16);
    for (int32_t i = 0; !bad && i < len(value); i++) {
        uint8_t c = (uint8_t)value.s[i];
        uint32_t digit = 0;
        if (c >= '0' && c <= '9') {
            digit = (uint32_t)(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            digit = (uint32_t)(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            digit = (uint32_t)(c - 'A' + 10);
        } else {
            bad = true;
            break;
        }
        if (digit >= (uint32_t)radix ||
            cp > (0x10ffffu - digit) / (uint32_t)radix) {
            bad = true;
            break;
        }
        cp = cp * (uint32_t)radix + digit;
    }
    bad = bad || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff) ||
          cp <= 0x08 || cp == 0x0b || (cp >= 0x0e && cp <= 0x1f) ||
          (cp >= 0x7f && cp <= 0x9f);
    if (bad) {
        cp = 0xfffd;
    }
    return MiniOwn(a, MiniUtf8Temp(cp));
}

} // namespace markdown
