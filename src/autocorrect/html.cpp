/* grammar/html.pest + code/html.rs — the HTML grammar as a scan.

   Part of the C++ port of the `autocorrect` crate 2.14.2 (see
   src/autocorrect/readme.md).

   What the crate corrects in HTML is the text nodes and the comments;
   <script> bodies re-dispatch as javascript, <style> bodies as css,
   `<code>` elements, `<% server %>` blocks, doctypes, tags and their
   attributes pass through, and <title>/<textarea> raw text is left alone
   (their pest rule is not one format_pair corrects). The grammar's
   recursive element structure only decides which bytes are tags and which
   are text, so this walks flat: a tag is skipped quote-aware, everything
   between tags is a text node. */

#include "autocorrect/internal.h"

namespace autocorrect {

static bool HtmlLitI(Str s, int i, const char* lit) {
    for (int k = 0; lit[k]; k++) {
        if (i + k >= len(s)) {
            return false;
        }
        char c = s.s[i + k];
        if (c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 'a');
        }
        if (c != lit[k]) {
            return false;
        }
    }
    return true;
}

static int HtmlLitLen(const char* lit) {
    int n = 0;
    while (lit[n]) {
        n++;
    }
    return n;
}

// A tag from '<' to its '>', honouring quoted attribute values. -1 when the
// tag never closes.
static int MatchTag(Str s, int i) {
    if (i >= len(s) || s.s[i] != '<') {
        return -1;
    }
    char quote = 0;
    for (int at = i + 1; at < len(s); at++) {
        char c = s.s[at];
        if (quote) {
            if (c == quote) {
                quote = 0;
            }
            continue;
        }
        if (c == '"' || c == '\'') {
            quote = c;
            continue;
        }
        if (c == '>') {
            return at + 1 - i;
        }
    }
    return -1;
}

// `</name … >` (case-blind), returning its length at i, or -1.
static int MatchCloseTag(Str s, int i, const char* name) {
    if (!HtmlLitI(s, i, "</")) {
        return -1;
    }
    int at = i + 2;
    if (!HtmlLitI(s, at, name)) {
        return -1;
    }
    at += HtmlLitLen(name);
    while (at < len(s) && (s.s[at] == ' ' || s.s[at] == '\t' ||
                           s.s[at] == '\n' || s.s[at] == '\r')) {
        at++;
    }
    if (at >= len(s) || s.s[at] != '>') {
        return -1;
    }
    return at + 1 - i;
}

// Find `</name>` from `from`; answers the offset or -1, and the length.
static int FindCloseTag(Str s, int from, const char* name, int* len) {
    for (int at = from; at < s.len; at++) {
        int n = MatchCloseTag(s, at, name);
        if (n > 0) {
            *len = n;
            return at;
        }
    }
    return -1;
}

// An opening `<name` whose name ends there (`<style>` yes, `<styles>` no).
static bool AtOpenTag(Str s, int i, const char* name) {
    if (i >= len(s) || s.s[i] != '<' || !HtmlLitI(s, i + 1, name)) {
        return false;
    }
    int after = i + 1 + HtmlLitLen(name);
    if (after >= len(s)) {
        return false;
    }
    char c = s.s[after];
    return c == '>' || c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
           c == '/';
}

void ScanHtml(Results* res, Str raw) {
    int ignoreStart = 0;
    int i = 0;
    auto flush = [&](int upTo) {
        if (upTo > ignoreStart) {
            EmitIgnore(res, Str(raw.s + ignoreStart, upTo - ignoreStart));
        }
    };
    while (i < len(raw)) {
        if (raw.s[i] != '<') {
            // text = (!("<" | comment start) ~ ANY)+ — a text node.
            int start = i;
            while (i < len(raw) && raw.s[i] != '<') {
                i++;
            }
            flush(start);
            EmitText(res, StrL("text"), Str(raw.s + start, i - start));
            ignoreStart = i;
            continue;
        }
        // `<!-- … -->`
        if (HtmlLitI(raw, i, "<!--")) {
            int end = -1;
            for (int at = i + 4; at + 3 <= len(raw); at++) {
                if (raw.s[at] == '-' && raw.s[at + 1] == '-' &&
                    raw.s[at + 2] == '>') {
                    end = at + 3;
                    break;
                }
            }
            if (end > 0) {
                flush(i);
                EmitText(res, StrL("comment"), Str(raw.s + i, end - i));
                ignoreStart = end;
                i = end;
                continue;
            }
            i++;
            continue;
        }
        // `<% server %>`
        if (HtmlLitI(raw, i, "<%")) {
            int end = -1;
            for (int at = i + 2; at + 2 <= len(raw); at++) {
                if (raw.s[at] == '%' && raw.s[at + 1] == '>') {
                    end = at + 2;
                    break;
                }
            }
            if (end > 0) {
                i = end;
                continue;
            }
            i++;
            continue;
        }
        // `<code> … </code>` passes through whole.
        if (HtmlLitI(raw, i, "<code>")) {
            int closeLen = 0;
            int close = FindCloseTag(raw, i + 6, "code", &closeLen);
            if (close > 0) {
                i = close + closeLen;
                continue;
            }
        }
        // <script> / <style>: the body is its own language. <title> /
        // <textarea>: raw text, not corrected.
        struct RawEl {
            const char* name;
            const char* rule; // null: body ignored
        };
        static const RawEl kRawEls[] = {
            {"script", "inline_javascript"},
            {"style", "inline_style"},
            {"title", nullptr},
            {"textarea", nullptr},
        };
        bool handled = false;
        for (const RawEl& el : kRawEls) {
            if (!AtOpenTag(raw, i, el.name)) {
                continue;
            }
            int tag = MatchTag(raw, i);
            if (tag < 0) {
                break;
            }
            int closeLen = 0;
            int close = FindCloseTag(raw, i + tag, el.name, &closeLen);
            if (close < 0) {
                break;
            }
            if (el.rule) {
                flush(i + tag);
                EmitInlineScript(res, Str(el.rule),
                                 Str(raw.s + i + tag, close - (i + tag)));
                ignoreStart = close;
            }
            i = close + closeLen;
            handled = true;
            break;
        }
        if (handled) {
            continue;
        }
        // Any other tag (start, end, self-closing, doctype, <? … ?>).
        int tag = MatchTag(raw, i);
        if (tag > 0) {
            i += tag;
            continue;
        }
        // A '<' that never closes — pest's `other`, one char at a time.
        i++;
    }
    flush(len(raw));
}

} // namespace autocorrect
