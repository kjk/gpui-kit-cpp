#include "base/text_format.h"

#include "html5ever/html5ever.h"

namespace gpui {

using namespace base;

static bool HtmlSpace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

Minifier& Minifier::OmitDoctype(bool value) {
    omitDoctype = value;
    return *this;
}

Minifier& Minifier::CollapseWhitespace(bool value) {
    collapseWhitespace = value;
    return *this;
}

Minifier& Minifier::PreserveComments(bool value) {
    preserveComments = value;
    return *this;
}

Str Minifier::WriteCollapseWhitespace(Arena* a, Str source) {
    if (!a || len(source) <= 0) return {};
    char* out = (char*)Alloc(a, len(source) + 1);
    if (!out) return {};
    int n = 0;
    bool whitespace = precedingWhitespace;
    for (int i = 0; i < len(source); i++) {
        char c = source.s[i];
        if (HtmlSpace(c)) {
            if (!whitespace) out[n++] = ' ';
            whitespace = true;
        } else {
            out[n++] = c;
            whitespace = false;
        }
    }
    precedingWhitespace = whitespace;
    out[n] = 0;
    return Str(out, n);
}

// crates/base/src/text/format/html5minify. Minification stays a source pass:
// serializing a DOM would insert html/head/body and normalize malformed input.
Str Minifier::Minify(Arena* a, Str source) {
    if (!a || len(source) <= 0) return {};
    char* out = (char*)Alloc(a, len(source) + 1);
    if (!out) return {};
    int n = 0;
    int at = 0;
    bool whitespace = precedingWhitespace;
    Str raw = {};
    while (at < len(source)) {
        if (StrStartsWithI(Str(source.s + at, len(source) - at), "<!--")) {
            int end = at + 4;
            while (end + 2 < len(source) &&
                   !(source.s[end] == '-' && source.s[end + 1] == '-' &&
                     source.s[end + 2] == '>')) {
                end++;
            }
            end = end + 2 < len(source) ? end + 3 : len(source);
            if (preserveComments) {
                memcpy(out + n, source.s + at, (size_t)(end - at));
                n += end - at;
                whitespace = false;
            }
            at = end;
            continue;
        }
        if (omitDoctype &&
            StrStartsWithI(Str(source.s + at, len(source) - at), "<!doctype")) {
            while (at < len(source) && source.s[at] != '>') at++;
            if (at < len(source)) at++;
            continue;
        }
        if (source.s[at] == '<') {
            int end = at + 1;
            char quote = 0;
            while (end < len(source)) {
                char c = source.s[end];
                if (quote) {
                    if (c == quote) quote = 0;
                } else if (c == '\'' || c == '"') {
                    quote = c;
                } else if (c == '>') {
                    end++;
                    break;
                }
                end++;
            }
            int nameAt = at + 1;
            bool close = nameAt < len(source) && source.s[nameAt] == '/';
            if (close) nameAt++;
            int nameEnd = nameAt;
            while (nameEnd < len(source) &&
                   ((source.s[nameEnd] >= 'a' && source.s[nameEnd] <= 'z') ||
                    (source.s[nameEnd] >= 'A' && source.s[nameEnd] <= 'Z'))) {
                nameEnd++;
            }
            Str name(source.s + nameAt, nameEnd - nameAt);
            if (!close && (StrEqI(name, "pre") || StrEqI(name, "textarea") ||
                           StrEqI(name, "script") || StrEqI(name, "style"))) {
                raw = name;
            } else if (close && raw.s && StrEqI(name, raw)) {
                raw = {};
            }
            memcpy(out + n, source.s + at, (size_t)(end - at));
            n += end - at;
            whitespace = false;
            at = end;
            continue;
        }
        char c = source.s[at++];
        if (collapseWhitespace && !raw.s && HtmlSpace(c)) {
            if (!whitespace) out[n++] = ' ';
            whitespace = true;
        } else {
            out[n++] = c;
            whitespace = false;
        }
    }
    precedingWhitespace = whitespace;
    out[n] = 0;
    return Str(out, n);
}

Str HtmlMinify(Arena* a, Str source) {
    Minifier minifier;
    minifier.OmitDoctype();
    return minifier.Minify(a, source);
}

static const html5ever::Node* FirstElement(Arena* a,
                                           const html5ever::Node* node) {
    for (const html5ever::Node* at = html5ever::NodeFirst(a, node); at;
         at = html5ever::NodeNext(a, at)) {
        if (at->kind == html5ever::NodeKind::Element) return at;
        if (const html5ever::Node* child = FirstElement(a, at)) return child;
    }
    return nullptr;
}

Str HtmlAttrValue(Arena* a, Str attrs, const char* name) {
    if (!a || !attrs.s || !name) return {};
    StrBuilder source(a);
    source.Append(StrL("<x "));
    source.Append(attrs);
    source.AppendChar('>');
    Str html = source.TakeStr();
    html5ever::Node* doc = html5ever::ParseFragment(a, html);
    return html5ever::AttrValue(a, FirstElement(a, doc), Str((char*)name));
}

static uint8_t InlineMark(Str name) {
    static const char names[] =
        "b\0strong\0i\0em\0cite\0var\0code\0kbd\0samp\0tt\0u\0ins\0s\0del\0"
        "strike\0mark\0";
    static const uint8_t marks[] = {
        MdBold, MdBold, MdItalic, MdItalic,    MdItalic,    MdItalic,
        MdCode, MdCode, MdCode,   MdCode,      MdUnderline, MdUnderline,
        MdDel,  MdDel,  MdDel,    MdHighlight,
    };
    int ix = SeqStrIndexIS(names, name);
    return ix < 0 ? 0 : marks[ix];
}

static float LengthValue(Str value) {
    value = StrTrimAscii(value);
    float result = 0;
    bool any = false;
    int at = 0;
    while (at < len(value) && value.s[at] >= '0' && value.s[at] <= '9') {
        result = result * 10 + (float)(value.s[at++] - '0');
        any = true;
    }
    if (at < len(value) && value.s[at] == '.') {
        at++;
        float scale = 0.1f;
        while (at < len(value) && value.s[at] >= '0' && value.s[at] <= '9') {
            result += (float)(value.s[at++] - '0') * scale;
            scale *= 0.1f;
            any = true;
        }
    }
    if (!any || (at < len(value) && value.s[at] == '%')) return 0;
    return result;
}

static Str StyleValue(Str style, const char* name) {
    int nameLen = (int)strlen(name);
    for (int i = 0; i + nameLen <= len(style); i++) {
        if (!StrEqI(Str(style.s + i, nameLen), Str(name, nameLen))) continue;
        int at = i + nameLen;
        while (at < len(style) && HtmlSpace(style.s[at])) at++;
        if (at >= len(style) || style.s[at++] != ':') continue;
        while (at < len(style) && HtmlSpace(style.s[at])) at++;
        int end = at;
        while (end < len(style) && style.s[end] != ';') end++;
        return StrTrimAscii(Str(style.s + at, end - at));
    }
    return {};
}

static float ElementLength(Arena* a, const html5ever::Node* node,
                           const char* name) {
    Str value = html5ever::AttrValue(a, node, Str((char*)name));
    if (!value.s) {
        value = StyleValue(html5ever::AttrValue(a, node, StrL("style")), name);
    }
    return LengthValue(value);
}

HtmlInlineTag HtmlParseInlineTag(Arena* a, Str tag) {
    HtmlInlineTag result;
    Str trimmed = StrTrimAscii(tag);
    if (len(trimmed) < 3 || trimmed.s[0] != '<') return result;
    int at = 1;
    if (trimmed.s[at] == '/') {
        result.close = true;
        at++;
    }
    int start = at;
    while (at < len(trimmed) &&
           ((trimmed.s[at] >= 'a' && trimmed.s[at] <= 'z') ||
            (trimmed.s[at] >= 'A' && trimmed.s[at] <= 'Z') ||
            (trimmed.s[at] >= '0' && trimmed.s[at] <= '9'))) {
        at++;
    }
    Str name(trimmed.s + start, at - start);
    if (StrEqI(name, "br")) {
        result.known = !result.close;
        result.isBreak = result.known;
        return result;
    }
    if (StrEqI(name, "a")) {
        result.known = true;
        result.mark = MdLink;
    } else if (StrEqI(name, "img")) {
        result.known = !result.close;
        result.isImage = result.known;
    } else {
        result.mark = InlineMark(name);
        result.known = result.mark != 0;
    }
    if (result.close || !result.known) return result;
    html5ever::Node* doc = html5ever::ParseFragment(a, tag);
    const html5ever::Node* element = FirstElement(a, doc);
    if (!element) return result;
    if (result.isImage) {
        result.alt = html5ever::AttrValue(a, element, StrL("alt"));
        result.src = html5ever::AttrValue(a, element, StrL("src"));
        result.width = ElementLength(a, element, "width");
        result.height = ElementLength(a, element, "height");
    } else if (result.mark == MdLink) {
        result.href = html5ever::AttrValue(a, element, StrL("href"));
    }
    return result;
}

static const char kBlockNames[] =
    "p\0dt\0dd\0summary\0figcaption\0blockquote\0ul\0ol\0li\0pre\0table\0"
    "tr\0td\0th\0div\0section\0article\0main\0header\0footer\0aside\0nav\0"
    "figure\0details\0form\0fieldset\0address\0dl\0body\0html\0center\0";
static const MdKind kBlockKinds[] = {
    MdKind::Paragraph, MdKind::Paragraph, MdKind::Paragraph, MdKind::Paragraph,
    MdKind::Paragraph, MdKind::Quote,     MdKind::List,      MdKind::List,
    MdKind::Item,      MdKind::Code,      MdKind::Table,     MdKind::Row,
    MdKind::Cell,      MdKind::Cell,      MdKind::Group,     MdKind::Group,
    MdKind::Group,     MdKind::Group,     MdKind::Group,     MdKind::Group,
    MdKind::Group,     MdKind::Group,     MdKind::Group,     MdKind::Group,
    MdKind::Group,     MdKind::Group,     MdKind::Group,     MdKind::Group,
    MdKind::Group,     MdKind::Group,     MdKind::Group,
};

static bool BlockKind(Arena* a, const html5ever::Node* node, MdKind* kind,
                      uint8_t* level) {
    *level = 0;
    Str name = html5ever::NodeName(a, node);
    if (len(name) == 2 && name.s[0] == 'h' && name.s[1] >= '1' &&
        name.s[1] <= '6') {
        *kind = MdKind::Heading;
        *level = (uint8_t)(name.s[1] - '0');
        return true;
    }
    int ix = SeqStrIndexIS(kBlockNames, name);
    if (ix < 0) return false;
    *kind = kBlockKinds[ix];
    return true;
}

struct Project {
    Arena* a = nullptr;
    MdNode* cur = nullptr;
    MdNode* para = nullptr;
    uint8_t marks = 0;
    Str href = {};
    bool raw = false;
    bool tableHead = false;
};

static MdNode* NewMd(Project* p, MdKind kind) {
    MdNode* node = ArenaNew<MdNode>(p->a);
    node->kind = kind;
    node->parent = p->cur;
    if (p->cur->last)
        p->cur->last->next = node;
    else
        p->cur->first = node;
    p->cur->last = node;
    return node;
}

static MdNode* TextTarget(Project* p) {
    MdKind kind = p->cur->kind;
    if (kind == MdKind::Paragraph || kind == MdKind::Heading ||
        kind == MdKind::Cell || kind == MdKind::Code || kind == MdKind::Item) {
        return p->cur;
    }
    if (!p->para) p->para = NewMd(p, MdKind::Paragraph);
    return p->para;
}

static bool TargetEmpty(Project* p) {
    MdKind kind = p->cur->kind;
    if (kind == MdKind::Paragraph || kind == MdKind::Heading ||
        kind == MdKind::Cell || kind == MdKind::Code || kind == MdKind::Item) {
        return p->cur->runFirst == nullptr;
    }
    return !p->para || !p->para->runFirst;
}

static void AddRun(Project* p, Str text) {
    if (len(text) <= 0) return;
    MdNode* target = TextTarget(p);
    MdRun* run = ArenaNew<MdRun>(p->a);
    run->text = text;
    run->marks = p->marks;
    run->href = p->href;
    if (target->runLast)
        target->runLast->next = run;
    else
        target->runFirst = run;
    target->runLast = run;
}

static void AddText(Project* p, Str text) {
    if (len(text) <= 0) return;
    if (p->raw) {
        if (!p->cur->runFirst && text.s[0] == '\n') {
            text = Str(text.s + 1, len(text) - 1);
        }
        AddRun(p, text);
        return;
    }
    char* out = (char*)Alloc(p->a, len(text) + 1);
    int n = 0;
    bool space = false;
    for (int i = 0; i < len(text); i++) {
        if (HtmlSpace(text.s[i])) {
            if (!space) out[n++] = ' ';
            space = true;
        } else {
            out[n++] = text.s[i];
            space = false;
        }
    }
    bool empty = TargetEmpty(p);
    int start = empty && n > 0 && out[0] == ' ' ? 1 : 0;
    if (n - start == 1 && out[start] == ' ' && empty) return;
    out[n] = 0;
    AddRun(p, Str(out + start, n - start));
}

static void AddImage(Project* p, const html5ever::Node* node) {
    Str src = html5ever::AttrValue(p->a, node, StrL("src"));
    if (len(src) <= 0) return;
    MdNode* target = TextTarget(p);
    MdRun* run = ArenaNew<MdRun>(p->a);
    run->imgSrc = src;
    run->text = html5ever::AttrValue(p->a, node, StrL("alt"));
    run->imgW = ElementLength(p->a, node, "width");
    run->imgH = ElementLength(p->a, node, "height");
    run->marks = p->marks;
    run->href = p->href;
    if (target->runLast)
        target->runLast->next = run;
    else
        target->runFirst = run;
    target->runLast = run;
}

static uint8_t AlignValue(Str value) {
    value = StrTrimAscii(value);
    if (StrStartsWithI(value, "center")) return MdAlignCenter;
    if (StrStartsWithI(value, "right")) return MdAlignRight;
    if (StrStartsWithI(value, "left")) return MdAlignLeft;
    return MdAlignDefault;
}

static uint8_t CellAlign(Arena* a, const html5ever::Node* node) {
    Str value = html5ever::AttrValue(a, node, StrL("align"));
    if (!value.s) {
        value = StyleValue(html5ever::AttrValue(a, node, StrL("style")),
                           "text-align");
    }
    return AlignValue(value);
}

static int ListStart(Arena* a, const html5ever::Node* node) {
    Str value = html5ever::AttrValue(a, node, StrL("start"));
    if (!value.s || len(value) <= 0) return 1;
    int result = 0;
    for (int i = 0; i < len(value); i++) {
        if (value.s[i] < '0' || value.s[i] > '9') return 1;
        result = result * 10 + value.s[i] - '0';
    }
    return result;
}

static void ProjectNode(Project* p, const html5ever::Node* source);

static void ProjectChildren(Project* p, const html5ever::Node* source) {
    for (const html5ever::Node* child = html5ever::NodeFirst(p->a, source);
         child; child = html5ever::NodeNext(p->a, child)) {
        ProjectNode(p, child);
    }
}

static void ProjectElement(Project* p, const html5ever::Node* source) {
    Str name = html5ever::NodeName(p->a, source);
    if (StrEqI(name, "head") || StrEqI(name, "title") ||
        StrEqI(name, "script") || StrEqI(name, "style")) {
        return;
    }
    if (source->implicit && (StrEqI(name, "html") || StrEqI(name, "body") ||
                             StrEqI(name, "head") || StrEqI(name, "tbody"))) {
        ProjectChildren(p, source);
        return;
    }
    if (StrEqI(name, "br")) {
        AddRun(p, StrL("\n"));
        return;
    }
    if (StrEqI(name, "hr")) {
        p->para = nullptr;
        NewMd(p, MdKind::Rule);
        return;
    }
    if (StrEqI(name, "img")) {
        AddImage(p, source);
        return;
    }
    if (StrEqI(name, "thead") || StrEqI(name, "tbody") ||
        StrEqI(name, "tfoot")) {
        bool oldHead = p->tableHead;
        p->tableHead = StrEqI(name, "thead");
        ProjectChildren(p, source);
        p->tableHead = oldHead;
        return;
    }
    MdKind kind = MdKind::Group;
    uint8_t level = 0;
    if (BlockKind(p->a, source, &kind, &level)) {
        p->para = nullptr;
        MdNode* parent = p->cur;
        MdNode* node = NewMd(p, kind);
        node->level = level;
        if (kind == MdKind::List) {
            node->ordered = StrEqI(name, "ol");
            node->start = ListStart(p->a, source);
        } else if (kind == MdKind::Row) {
            const html5ever::Node* sourceParent =
                html5ever::NodeParent(p->a, source);
            node->head =
                p->tableHead ||
                (sourceParent &&
                 StrEqI(html5ever::NodeName(p->a, sourceParent), "thead"));
        } else if (kind == MdKind::Cell) {
            node->align = CellAlign(p->a, source);
            if (StrEqI(name, "th") && node->parent) node->parent->head = true;
        }
        bool oldRaw = p->raw;
        p->raw = kind == MdKind::Code;
        p->cur = node;
        const html5ever::Node* first = html5ever::NodeFirst(p->a, source);
        if (kind == MdKind::Code && first &&
            first->kind == html5ever::NodeKind::Element &&
            StrEqI(html5ever::NodeName(p->a, first), "code")) {
            Str cls = html5ever::AttrValue(p->a, first, StrL("class"));
            if (StrStartsWith(cls, "language-")) {
                node->lang = Str(cls.s + 9, len(cls) - 9);
            }
        }
        ProjectChildren(p, source);
        p->cur = parent;
        p->para = nullptr;
        p->raw = oldRaw;
        return;
    }
    uint8_t oldMarks = p->marks;
    Str oldHref = p->href;
    uint8_t mark = InlineMark(name);
    if (StrEqI(name, "a")) {
        p->marks = (uint8_t)(p->marks | MdLink);
        p->href = html5ever::AttrValue(p->a, source, StrL("href"));
    } else {
        p->marks = (uint8_t)(p->marks | mark);
    }
    ProjectChildren(p, source);
    p->marks = oldMarks;
    p->href = oldHref;
}

static void ProjectNode(Project* p, const html5ever::Node* source) {
    if (!source) return;
    if (source->kind == html5ever::NodeKind::Text) {
        AddText(p, html5ever::NodeData(p->a, source));
    } else if (source->kind == html5ever::NodeKind::Element) {
        ProjectElement(p, source);
    } else if (source->kind == html5ever::NodeKind::Document) {
        ProjectChildren(p, source);
    }
}

void HtmlParseInto(Arena* a, MdNode* parent, Str source) {
    if (!a || !parent || !source.s || len(source) <= 0) return;
    html5ever::ParseOptions options;
    options.dropDoctype = true;
    html5ever::Node* dom =
        html5ever::ParseFragment(a, source, StrL("body"), options);
    Project project;
    project.a = a;
    project.cur = parent;
    ProjectNode(&project, dom);
}

MdNode* HtmlParse(Arena* a, Str source) {
    MdNode* doc = ArenaNew<MdNode>(a);
    doc->kind = MdKind::Doc;
    if (!source.s || len(source) <= 0) return doc;
    html5ever::ParseOptions options;
    options.dropDoctype = true;
    html5ever::Node* dom = html5ever::ParseDocument(a, source, options);
    Project project;
    project.a = a;
    project.cur = doc;
    ProjectNode(&project, dom);
    return doc;
}

} // namespace gpui
