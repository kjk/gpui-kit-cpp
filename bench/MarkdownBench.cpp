/* Parsing a large markdown document — src/markdown, the ported `markdown`
   crate.

   Not a port: markdown-rs has no benchmark suite of its own to translate, so
   these are ours, and the documents are generated here rather than read off
   disk. That is deliberate. `assets/story/README.md` is upstream's README,
   copied in at the pinned SHA, so a number measured against it stops being
   comparable the moment the pin moves; a document built from a seed does not.

   Four shapes, because the parser's cost is not evenly spread:

     prose     paragraphs with emphasis, code spans and links — the common
               case, and the one that runs the text tokenizer, the attention
               resolver and the label resolver hardest.
     nested    block quotes and lists inside each other, with lazy
               continuation lines — the document tokenizer's container
               machinery, which is where an attempt costs the most: every
               line is tried against every open container before the flow
               reader sees it.
     table     GFM tables — the table resolver, which rewrites the event
               list, and one text subtokenizer per cell.
     entities  character references — `DecodeNamed` against the 2125-entry
               table, once per `&…;`.

   Each shape runs at one size by default; -small and -large add the others.
   The last group splits the work in two, so a change can be attributed:
   `tokenize` is `Parse` alone (bytes to events, which is nearly all of it)
   and `to_mdast` is `ToMdastCompile` over events that are already there.

   One number to expect: a table costs about one and a half times what prose
   does per byte, and the ratio holds as the document grows. It did not use to.
   markdown-rs and this port shared an edit map whose `add` scanned the entries
   it already held for one at the same index and whose `consume` sorted them by
   insertion sort — both linear per entry, and a table adds one entry per cell,
   so a document of tables was quadratic in its cell count twice over. Reading
   64 KB of tables cost 65 ms against markdown-rs's 80 ms; it is 14 ms here
   now, and the 256 KB shape went from 1.0 s to 61 ms. The events produced are
   the same; see `EditMap` in src/markdown/util.h. */

// The tokenize/to_mdast split below measures through the crate's internals
// (ParseState, the event stream), which the amalgam hides from ordinary
// consumers.
#define GPUI_INCLUDE_PRIVATE_API 1
#include "Bench.h"

// `gpui::CharKind` and `markdown::CharKind` both exist, so this file names
// the namespace at each use rather than opening it.

// ─── the documents ───────────────────────────────────────────────────────

// A grown buffer of source text. The document is built once per case and
// parsed `gBenchSamples` times, so this is not on the clock.
struct Doc {
    Arena* a = nullptr;
    char* buf = nullptr;
    int32_t len = 0;
    int32_t cap = 0;

    void Init(Arena* arena, int32_t capacity) {
        a = arena;
        cap = capacity;
        buf = (char*)Alloc(a, cap);
        len = 0;
    }

    void Add(Str s) {
        if (len + len(s) > cap) {
            return;
        }
        memcpy(buf + len, s.s, (size_t)len(s));
        len += len(s);
    }

    void Add(const char* s) { Add(Str(s)); }

    void AddInt(int v) { Add(fmt("%d", v)); }

    bool Full(int32_t target) const { return len >= target; }
    Str AsStr() const { return Str(buf, len); }
};

// Words to build sentences out of. Short and long ones both, since a run of
// data ends at the next marker byte and the marker density is what the text
// tokenizer feels.
static const char* kWords[16] = {"the",     "layout",   "engine",   "reads",
                                 "a",       "document", "and",      "renders",
                                 "it",      "into",     "elements", "that",
                                 "measure", "their",    "own",      "text"};

static void AddWords(Doc* d, BenchRng* rng, int count) {
    for (int i = 0; i < count; i++) {
        if (i > 0) {
            d->Add(" ");
        }
        d->Add(kWords[rng->NextU32() & 15]);
    }
}

// Paragraphs, headings and the odd list, with a mark on roughly every fifth
// word: what a page of documentation looks like.
static void BuildProse(Doc* d, int32_t target) {
    BenchRng rng;
    rng.Seed(kStandardRngSeed);
    int paragraph = 0;
    while (!d->Full(target)) {
        if (paragraph % 10 == 0) {
            d->Add("## Section ");
            d->AddInt(paragraph / 10);
            d->Add("\n\n");
        }
        int sentences = rng.RangeInt(2, 5);
        for (int s = 0; s < sentences; s++) {
            AddWords(d, &rng, rng.RangeInt(3, 8));
            switch (rng.NextU32() % 5) {
                case 0:
                    d->Add(" **");
                    AddWords(d, &rng, 2);
                    d->Add("**");
                    break;
                case 1:
                    d->Add(" *");
                    AddWords(d, &rng, 1);
                    d->Add("*");
                    break;
                case 2:
                    d->Add(" `");
                    AddWords(d, &rng, 1);
                    d->Add("`");
                    break;
                case 3:
                    d->Add(" [");
                    AddWords(d, &rng, 2);
                    d->Add("](/some/path)");
                    break;
                default:
                    break;
            }
            d->Add(". ");
        }
        d->Add("\n\n");
        if (paragraph % 7 == 3) {
            for (int item = 0; item < 4; item++) {
                d->Add("- ");
                AddWords(d, &rng, rng.RangeInt(3, 7));
                d->Add("\n");
            }
            d->Add("\n");
        }
        paragraph++;
    }
}

// Quotes and lists inside each other. Every block ends with a line that
// carries none of the prefixes, which is the lazy continuation the document
// tokenizer has to decide about a line at a time.
static void BuildNested(Doc* d, int32_t target) {
    BenchRng rng;
    rng.Seed(kStandardRngSeed);
    while (!d->Full(target)) {
        int depth = rng.RangeInt(1, 4);
        for (int line = 0; line < 4; line++) {
            for (int i = 0; i < depth; i++) {
                d->Add("> ");
            }
            if (line == 0) {
                d->Add("- ");
            } else if (line == 1) {
                d->Add("  - ");
            } else {
                d->Add("  ");
            }
            AddWords(d, &rng, rng.RangeInt(4, 9));
            d->Add("\n");
        }
        // No prefix: this line continues the paragraph above it lazily.
        AddWords(d, &rng, rng.RangeInt(4, 9));
        d->Add("\n\n");
    }
}

// Tables of five columns, one alignment per column, eight rows each.
static void BuildTable(Doc* d, int32_t target) {
    BenchRng rng;
    rng.Seed(kStandardRngSeed);
    const char* delimiters[5] = {":---", ":---:", "---:", "---", ":---:"};
    while (!d->Full(target)) {
        d->Add("| head | of | five | short | columns |\n|");
        for (int col = 0; col < 5; col++) {
            d->Add(delimiters[col]);
            d->Add("|");
        }
        d->Add("\n");
        for (int row = 0; row < 8; row++) {
            d->Add("|");
            for (int col = 0; col < 5; col++) {
                d->Add(" ");
                AddWords(d, &rng, rng.RangeInt(1, 3));
                d->Add(" |");
            }
            d->Add("\n");
        }
        d->Add("\n");
    }
}

// Prose where every fifth token is a character reference, named or numeric.
static void BuildEntities(Doc* d, int32_t target) {
    BenchRng rng;
    rng.Seed(kStandardRngSeed);
    const char* refs[6] = {"&amp;", "&copy;", "&nbsp;",
                           "&#65;", "&#x41;", "&hellip;"};
    while (!d->Full(target)) {
        for (int word = 0; word < 40; word++) {
            if (word % 5 == 4) {
                d->Add(refs[rng.NextU32() % 6]);
            } else {
                d->Add(kWords[rng.NextU32() & 15]);
            }
            d->Add(" ");
        }
        d->Add("\n\n");
    }
}

// ─── the cases ───────────────────────────────────────────────────────────

using BuildFn = void (*)(Doc* d, int32_t target);

// What a sample parses, and where it parses into. The setup empties the
// output arena so ten samples of a megabyte do not hold ten trees.
struct MdCase {
    Arena* out = nullptr;
    Str source = {};
    markdown::ParseOptions options = markdown::ParseOptions::Gfm();
};

static void MdSetup(MdCase* c) {
    c->out->Reset();
}

static void MdParseRun(MdCase* c) {
    markdown::Node* tree = markdown::ToMdast(c->out, c->source, c->options);
    BenchKeep(tree);
}

// The three sizes, in bytes. The middle one always runs, which is what the
// taffy benchmarks do with their node counts.
struct MdSize {
    int32_t bytes;
    bool smallOnly;
    bool largeOnly;
};

static const MdSize kSizes[3] = {
    {16 * 1024, true, false},
    {64 * 1024, false, false},
    {1024 * 1024, false, true},
};

/* `largeBytes` overrides the -large size for one shape. Every shape takes the
   same three sizes now; the hook stays because the reason it was added is the
   kind of thing that comes back. Tables used to run at a quarter of what the
   others do: the edit map was quadratic in its entry count and a table adds
   one entry per cell, so a megabyte of them was 22 seconds a sample. See
   `EditMap` in src/markdown/util.h for what that cost was and what replaced
   it. */
static void RunShapeSized(const char* name, BuildFn build, int32_t largeBytes) {
    const char* group = "markdown/parse";
    for (int i = 0; i < 3; i++) {
        if (kSizes[i].smallOnly && !gBenchSmall) {
            continue;
        }
        if (kSizes[i].largeOnly && !gBenchLarge) {
            continue;
        }
        int32_t bytes = kSizes[i].largeOnly ? largeBytes : kSizes[i].bytes;
        if (!BenchWanted(group, name)) {
            continue;
        }

        // The source and the tree live in arenas of their own: the source is
        // built once, the tree is thrown away between samples.
        Arena* src = ArenaNew();
        Arena* out = ArenaNew();
        Doc doc;
        // Slack for the block a generator is in the middle of when it fills.
        doc.Init(src, bytes + 4096);
        build(&doc, bytes);

        MdCase c;
        c.out = out;
        c.source = doc.AsStr();
        BenchCase(group, name, "bytes", doc.len, MkFunc0(MdSetup, &c),
                  MkFunc0(MdParseRun, &c));
        // What the tree cost to hold, which is the other half of the number
        // above: one more parse into a freshly reset arena, and how far the
        // arena's position moved. Reported per byte of source as well, since
        // that is what makes two document shapes comparable.
        MdSetup(&c);
        MdParseRun(&c);
        BenchMem(group, name, doc.len, ArenaUsed(c.out));

        // And what building it cost, which is the arena `ToMdast` makes and
        // throws away: every ArenaVec the tokenizer and the resolvers fill
        // lives there, so it is the number a change to ArenaVec moves. Parsed
        // the long way round because ToMdast owns its scratch and this needs
        // to see it.
        {
            Arena* scratch = ArenaNew();
            markdown::ParseState st;
            st.a = c.out;
            st.scratch = scratch;
            st.options = &c.options;
            st.bytes = c.source;
            c.out->Reset();
            Vec<markdown::Event> events = markdown::Parse(&st);
            markdown::Node* tree = markdown::ToMdastCompile(events, &st);
            BenchKeep(tree);
            BenchMemAs(group, name, "scratch", doc.len, ArenaUsed(scratch));
            ArenaDelete(scratch);
        }

        ArenaDelete(out);
        ArenaDelete(src);
    }
}

static void RunShape(const char* name, BuildFn build) {
    RunShapeSized(name, build, kSizes[2].bytes);
}

// ─── the two halves ──────────────────────────────────────────────────────

// `Parse` alone: source bytes to the event list, no tree.
struct TokenizeCase {
    Arena* out = nullptr;
    Arena* scratch = nullptr;
    Str source = {};
    markdown::ParseOptions options = markdown::ParseOptions::Gfm();
};

static void TokenizeSetup(TokenizeCase* c) {
    c->out->Reset();
    c->scratch->Reset();
}

static void TokenizeRun(TokenizeCase* c) {
    markdown::ParseState state;
    state.a = c->out;
    state.scratch = c->scratch;
    state.options = &c->options;
    state.bytes = c->source;
    Vec<markdown::Event> events = markdown::Parse(&state);
    BenchKeep(events.els);
}

// `ToMdastCompile` alone, over an event list parsed in the setup.
struct CompileCase {
    Arena* out = nullptr;
    Arena* scratch = nullptr;
    Str source = {};
    markdown::ParseOptions options = markdown::ParseOptions::Gfm();
    markdown::ParseState state;
    Vec<markdown::Event> events;
};

static void CompileSetup(CompileCase* c) {
    c->out->Reset();
    c->scratch->Reset();
    c->state.a = c->out;
    c->state.scratch = c->scratch;
    c->state.options = &c->options;
    c->state.bytes = c->source;
    c->state.definitions.len = 0;
    c->state.gfmFootnoteDefinitions.len = 0;
    c->events = markdown::Parse(&c->state);
}

static void CompileRun(CompileCase* c) {
    markdown::Node* tree = markdown::ToMdastCompile(c->events, &c->state);
    BenchKeep(tree);
}

// Both halves of the same document, so their two numbers add up to the one
// `markdown/parse` reports for `prose`.
static void RunPhases() {
    const char* group = "markdown/phases";
    int32_t bytes = 64 * 1024;
    Arena* src = ArenaNew();
    Doc doc;
    doc.Init(src, bytes + 4096);
    BuildProse(&doc, bytes);

    if (BenchWanted(group, "tokenize")) {
        Arena* out = ArenaNew();
        Arena* scratch = ArenaNew();
        TokenizeCase c;
        c.out = out;
        c.scratch = scratch;
        c.source = doc.AsStr();
        BenchCase(group, "tokenize", "bytes", doc.len,
                  MkFunc0(TokenizeSetup, &c), MkFunc0(TokenizeRun, &c));
        ArenaDelete(scratch);
        ArenaDelete(out);
    }

    if (BenchWanted(group, "to_mdast")) {
        Arena* out = ArenaNew();
        Arena* scratch = ArenaNew();
        CompileCase c;
        c.out = out;
        c.scratch = scratch;
        c.source = doc.AsStr();
        BenchCase(group, "to_mdast", "bytes", doc.len,
                  MkFunc0(CompileSetup, &c), MkFunc0(CompileRun, &c));
        ArenaDelete(scratch);
        ArenaDelete(out);
    }

    ArenaDelete(src);
}

void BenchMarkdown() {
    RunShape("prose", BuildProse);
    RunShape("nested quotes and lists", BuildNested);
    RunShape("gfm tables", BuildTable);
    RunShape("character references", BuildEntities);
    RunPhases();
}
