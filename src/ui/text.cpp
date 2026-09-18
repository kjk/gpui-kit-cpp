#include "ui/text.h"

#include "ui/description_list.h"

namespace gpui {

namespace component {

struct FrontmatterEntry {
    Str key = {};
    Str value = {};
};

struct FrontmatterData {
    ArenaVec<FrontmatterEntry> entries;
};

enum class FrontmatterScalar : uint8_t {
    Plain,
    Folded,
    Literal,
    Empty,
};

static bool FrontmatterPlainKey(Str key) {
    if (!key) {
        return false;
    }
    for (int i = 0; i < len(key); i++) {
        char c = key.s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_')) {
            return false;
        }
    }
    return true;
}

static bool FrontmatterStarts(Str value, char c) {
    return len(value) > 0 && value.s[0] == c;
}

static bool FrontmatterContains(Str value, Str needle) {
    if (len(needle) <= 0 || len(needle) > len(value)) {
        return false;
    }
    for (int i = 0; i <= len(value) - len(needle); i++) {
        if (memcmp(value.s + i, needle.s, (size_t)len(needle)) == 0) {
            return true;
        }
    }
    return false;
}

static bool FrontmatterUnsupportedPlain(Str value) {
    static const char leading[] = {'\'', '"', '[', ']', '{', '}', '&', '*',
                                   '!',  '|', '>', '#', '%', '@', '`'};
    for (char c : leading) {
        if (FrontmatterStarts(value, c)) {
            return true;
        }
    }
    if (len(value) > 0 && value.s[len(value) - 1] == ':') {
        return true;
    }
    return FrontmatterContains(value, StrL(" #")) ||
           FrontmatterContains(value, StrL("\t#")) ||
           FrontmatterContains(value, StrL(": ")) ||
           FrontmatterContains(value, StrL(":\t")) ||
           FrontmatterContains(value, StrL("- ")) ||
           FrontmatterContains(value, StrL("? "));
}

static Str FrontmatterTrimNewlines(Str value) {
    while (len(value) > 0 && value.s[len(value) - 1] == '\n') {
        value.len--;
    }
    return value;
}

static bool FrontmatterParse(const markdown::Node* node,
                             const MarkdownParseContext* context, void*,
                             MarkdownNode* out) {
    if (!node || !context || !out || node->kind != markdown::NodeKind::Yaml) {
        return false;
    }
    Arena* a = context->arena;
    Str source = context->Value(node, markdown::NodeStrKind::Value);
    FrontmatterData* data = ArenaNew<FrontmatterData>(a);
    Str key = {};
    StrBuilder value(a);
    FrontmatterScalar style = FrontmatterScalar::Empty;
    int indent = -1;
    int scalarLines = 0;
    bool haveEntry = false;

    auto finish = [&]() {
        if (!haveEntry) {
            return;
        }
        Str v = value.TakeStr();
        if (style == FrontmatterScalar::Folded ||
            style == FrontmatterScalar::Literal) {
            v = FrontmatterTrimNewlines(v);
        }
        data->entries.Append(a, {context->Copy(key), context->Copy(v)});
        value.Reset();
        haveEntry = false;
    };

    for (int at = 0; at <= len(source);) {
        int end = at;
        while (end < len(source) && source.s[end] != '\n' &&
               source.s[end] != '\r') {
            end++;
        }
        Str line(source.s + at, end - at);
        while (end < len(source) &&
               (source.s[end] == '\n' || source.s[end] == '\r')) {
            end++;
        }
        at = end;
        Str trimmed = StrTrimAscii(line);
        bool block = style == FrontmatterScalar::Folded ||
                     style == FrontmatterScalar::Literal;
        if (!trimmed) {
            if (haveEntry && block) {
                value.AppendChar('\n');
                scalarLines++;
            }
            if (at >= len(source)) {
                break;
            }
            continue;
        }
        bool topLevel =
            len(line) == 0 || (line.s[0] != ' ' && line.s[0] != '\t');
        if (trimmed.s[0] == '#' && (topLevel || !block)) {
            if (at >= len(source)) {
                break;
            }
            continue;
        }
        if (topLevel) {
            int colon = -1;
            for (int i = 0; i < len(line); i++) {
                if (line.s[i] == ':') {
                    colon = i;
                    break;
                }
            }
            if (colon < 0 ||
                (colon + 1 < len(line) && line.s[colon + 1] != ' ' &&
                 line.s[colon + 1] != '\t')) {
                return false;
            }
            finish();
            key = StrTrimAscii(Str(line.s, colon));
            if (!FrontmatterPlainKey(key)) {
                return false;
            }
            Str raw =
                StrTrimAscii(Str(line.s + colon + 1, len(line) - colon - 1));
            style = FrontmatterScalar::Plain;
            if (!raw) {
                style = FrontmatterScalar::Empty;
            } else if (StrEq(raw, StrL(">-"))) {
                style = FrontmatterScalar::Folded;
            } else if (StrEq(raw, StrL("|-"))) {
                style = FrontmatterScalar::Literal;
            } else {
                if (FrontmatterUnsupportedPlain(raw)) {
                    return false;
                }
                value.Append(raw);
            }
            haveEntry = true;
            indent = -1;
            scalarLines = 0;
        } else {
            if (!haveEntry || !block) {
                return false;
            }
            int spaces = 0;
            while (spaces < len(line) && line.s[spaces] == ' ') {
                spaces++;
            }
            if (spaces == 0) {
                return false;
            }
            if (indent < 0) {
                indent = spaces;
            }
            if (spaces < indent) {
                return false;
            }
            Str continuation(line.s + indent, len(line) - indent);
            if (style == FrontmatterScalar::Folded) {
                if (len(continuation) > 0 &&
                    (continuation.s[0] == ' ' || continuation.s[0] == '\t')) {
                    return false;
                }
                if (len(value) > 0 && value.LastChar() != '\n') {
                    value.AppendChar(' ');
                }
            } else if (scalarLines > 0) {
                value.AppendChar('\n');
            }
            value.Append(continuation);
            scalarLines++;
        }
        if (at >= len(source)) {
            break;
        }
    }
    finish();
    if (data->entries.len == 0) {
        return false;
    }

    StrBuilder text(a);
    for (int i = 0; i < data->entries.len; i++) {
        if (i > 0) {
            text.AppendChar('\n');
        }
        text.Append(data->entries[i].key);
        text.Append(StrL(": "));
        text.Append(data->entries[i].value);
    }
    *out = MarkdownNode::New(StrL("frontmatter"), data)
               .Text(text.TakeStr())
               .Markdown(source);
    return true;
}

static El* FrontmatterRender(Ctx* cx, const MarkdownNode* node, void*) {
    if (!node || !node->data) {
        return nullptr;
    }
    FrontmatterData* data = (FrontmatterData*)node->data;
    DescriptionList* list =
        DescriptionList::Horizontal(cx)->LabelWidth(192)->Columns(1);
    for (int i = 0; i < data->entries.len; i++) {
        list->Item(data->entries[i].key, data->entries[i].value);
    }
    return list->IntoEl();
}

MarkdownPlugin FrontmatterPlugin::New() {
    MarkdownPlugin plugin;
    plugin.name = StrL("frontmatter");
    plugin.parse = &FrontmatterParse;
    plugin.render = &FrontmatterRender;
    plugin.isBlock = true;
    return plugin;
}

TextViewStyle UiTextViewStyle(const Theme& theme) {
    // The colours first — `with_foreground`, `with_link` and the rest — from
    // the themed palette rather than from the Base one, so an application
    // theme reaches rich text.
    TextViewStyle style = TextViewStyle::Default();
    style.WithForeground(theme.foreground)
        .WithMutedForeground(theme.mutedFg)
        .WithLink(theme.link)
        .WithSelection(theme.selection)
        .WithCodeBackground(theme.muted)
        .WithBorder(theme.border)
        .WithDark(theme.mode == ThemeMode::Dark);

    // The radius Base no longer reads off a theme arrives as a refinement on
    // the table and the code block, which is exactly where node.rs looks.
    gpui::Style radius = {};
    radius.radius = theme.radius;
    style.WithTable(radius, StyleFieldRadius);
    style.WithCodeBlock(radius, StyleFieldRadius);

    // The header row keeps the table theme's own pair rather than the code
    // background Base falls back to.
    gpui::Style head = {};
    head.bg = Background(theme.tokens.tableHead);
    head.color = theme.tableHeadFg;
    style.WithTableHead(head, StyleFieldBg | StyleFieldColor);

    gpui::Style inlineCode = {};
    inlineCode.bg = Background(theme.accent);
    style.WithInlineCode(inlineCode, StyleFieldBg);
    return style;
}

void UiCodeBlockHighlighter(void* data, const CodeBlock* block, Arena* a,
                            ArenaVec<CodeHighlight>* out) {
    App* app = (App*)data;
    if (!block || !out || !a || !app) {
        return;
    }
    SyntaxLang lang = SyntaxLangFor(block->Lang());
    if (lang == SyntaxLangNone) {
        return;
    }
    // The colours are the theme's, read live rather than captured, so a
    // theme change repaints the same parsed block in the new palette —
    // `a_new_highlighter_replaces_styles_instead_of_reusing_the_cache`.
    ThemeMode mode = ThemeGet(app);
    Rgba fallback = ThemeNow(app).foreground;
    Str code = block->Code();
    SyntaxLexer lx;
    SyntaxLexStart(&lx, lang, code);
    while (SyntaxLexNext(&lx)) {
        Rgba color = SyntaxTokColor(lx.tok, mode, fallback);
        if (!color.a || len(lx.text) <= 0) {
            continue;
        }
        CodeHighlight span;
        span.start = (int)(lx.text.s - code.s);
        span.end = span.start + len(lx.text);
        span.color = color;
        out->Append(a, span);
    }
}

void TextViewInstallDefaults(App* app) {
    if (!app) {
        return;
    }
    TextViewDefaults::New()
        .WithStyle(UiTextViewStyle(ThemeNow(app)))
        .WithCodeBlockHighlighter(&UiCodeBlockHighlighter, app)
        .Install(app);
}

} // namespace component
} // namespace gpui
