/* Parser throughput and retained DOM memory for a large HTML document.

   html5ever carries tokenizer microbenchmarks but no stable large document
   fixture. This generated document is deterministic, self-contained and
   deliberately exercises nested elements, attributes, character references,
   tables and the tree builder. Source construction is outside the timed
   region; each sample parses into a freshly reset arena. */

#include "Bench.h"

#include "html5ever/html5ever.h"

#include <stdio.h>

static constexpr int kHtmlBytes = 1024 * 1024;

static Str BuildLargeHtml(Arena* a) {
    StrBuilder out(a);
    out.Reserve(kHtmlBytes + 4096);
    out
        .Append(StrL("<!doctype html><html><head><title>Large document</title>"
                     "</head><body><main id=content class=article>"));
    int section = 0;
    while (out.len < kHtmlBytes) {
        TempStr n = fmt("%d", section++);
        out.Append(StrL("<section data-section=\""));
        out.Append(n);
        out.Append(
            StrL("\"><h2>Parser &amp; memory benchmark</h2>"
                 "<p class=lead>This is a <strong>large</strong> HTML "
                 "document with <a href=\"/docs?q=one&amp;x=two\">links</a>, "
                 "entities such as &copy; and &#x41;, and enough repeated "
                 "structure to measure tree allocation.</p>"
                 "<ul><li data-kind=first>alpha</li><li>beta <em>with "
                 "emphasis</em></li><li>gamma<br>delta</li></ul>"
                 "<table><thead><tr><th>Name</th><th>Value</th></tr></thead>"
                 "<tbody><tr><td>one</td><td>123</td></tr>"
                 "<tr><td>two</td><td>456</td></tr></tbody></table>"
                 "<!-- retained comment --></section>"));
    }
    out.Append(StrL("</main></body></html>"));
    return out.TakeStr();
}

struct HtmlCase {
    Arena* out = nullptr;
    Str source = {};
};

static void HtmlSetup(HtmlCase* c) {
    c->out->Reset();
}

static void HtmlParseRun(HtmlCase* c) {
    html5ever::Node* doc = html5ever::ParseDocument(c->out, c->source);
    BenchKeep(doc);
}

void BenchHtml5ever() {
    const char* group = "html5ever/parse";
    const char* name = "large document";
    if (!BenchWanted(group, name)) return;

    Arena* sourceArena = ArenaNew();
    Arena* out = ArenaNew();
    HtmlCase c;
    c.out = out;
    c.source = BuildLargeHtml(sourceArena);

    BenchCase(group, name, "bytes", len(c.source), MkFunc0(HtmlSetup, &c),
              MkFunc0(HtmlParseRun, &c));
    HtmlSetup(&c);
    HtmlParseRun(&c);
    BenchMem(group, name, len(c.source), ArenaUsed(out));
    printf("  struct sizes: Attribute %zu B, Node %zu B, Token %zu B\n",
           sizeof(html5ever::Attribute), sizeof(html5ever::Node),
           sizeof(html5ever::Token));

    ArenaDelete(out);
    ArenaDelete(sourceArena);
}
