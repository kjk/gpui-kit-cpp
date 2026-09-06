/* crates/story/examples/editor.rs — a file tree beside the code editor, with
   the editor's own switches under it.

   The tree walks the working directory and opens a file into the editor,
   which scans it as whatever its extension names. The walk is filtered
   through `autocorrect::Ignorer` (src/autocorrect/), which reads .gitignore
   and .autocorrectignore the way Rust's walk does, plus the same standalone
   `.git` name check.

   Over it is the bar `crates/story/src/title_bar.rs` draws: Rust's editor
   window is a StoryRoot, whose AppTitleBar carries the app menu bar on the
   left and the appearance menu, the GitHub link and the notification bell on
   the right. This example is a window of its own, so it draws that bar
   itself.

   Under it is the status bar, with the caret's one-based `line:column (byte)`
   on the right opening the Go to line dialog. All eight switches are here:
   line numbers, soft wrap, show whitespaces, indent guides, folding, read
   only, scroll beyond last line, and cursor surrounding lines.

   The diagnostics are the crate's: the open document is linted with the
   ported `autocorrect` crate (src/autocorrect/), exactly as upstream lints
   it, and every finding is published twice the way Rust's ExampleLspStore
   publishes it — as a diagnostic drawn with the wavy underline `element.rs`
   draws, and as a quickfix code action ("Change to '…'") beside
   TextConvertor's rewrites.

   The completion menu is the store's other half that is here: the items are
   upstream's own `fixtures/completion_items.json`, filtered by the word being
   typed, with the selected item's documentation rendered as markdown beside
   the list, and the hover popover, which answers about the word the pointer
   rests on out of the same items, and the code action menu, which is
   TextConvertor's five ways to rewrite a selection. The colours the document
   names are the last of it: upstream hands the text to the `color-lsp` crate,
   and this scans for the two spellings a source file has — a hex literal and
   an `rgb(...)` call — and paints each behind the text that names it. */

#include "gpui.h"

// The autocorrect port is not part of the gpui amalgam: it is this example's
// linter, so the standard build compiles it as its own amalgamated pair
// under extras/ and links it only here (and into the tests); the non-amalgam
// build is the one consumer of the raw src/autocorrect sources. Both spell
// the include the same way — cmd/build.ts points -I at <amalgam>/extras in
// the standard build and at src/ in the non-amalgam one.
#include "autocorrect/autocorrect.h"

using namespace gpui;

// The walk asks the `autocorrect` ignorer, which reads .gitignore and
// .autocorrectignore from the working directory, and skips `.git` by name on
// top of it — build_file_items in the Rust example. A leading dot is not
// itself a reason to skip: `.github` and `.cache` stay unless an ignore file
// names them.
static bool SkipEntry(const autocorrect::Ignorer* ig, Str relPath, Str name) {
    if (name.len == 0) {
        return true;
    }
    if (StrEq(name, StrL(".git"))) {
        return true;
    }
    return autocorrect::IgnorerIsIgnored(ig, relPath);
}

static const int kMaxDiagnostics = 512;

// One lint finding's quickfix: replace `range` with `newText`. The title the
// menu shows is derived from it — `Change to '…'`, the way Rust builds the
// CodeAction beside each diagnostic.
struct LintFix {
    Selection range = {};
    Str newText = {};
};

struct EditorApp {
    Entity<TreeState> tree = {};
    InputState editor;
    InputState goToLine;
    Subscription treeSub = {};
    bool seeded = false;
    bool dialogOpen = false;
    int lastSelected = -1;

    // The switches the status bar carries.
    bool lineNumbers = true;
    bool softWrap = false;
    bool showWhitespaces = false;
    bool indentGuides = true;
    bool folding = true;
    bool readOnly = false;
    // -1 is None ("default"); cycle_rows walks 0, 3, 8, then back.
    int scrollBeyondLastLine = -1;
    int cursorSurroundingLines = -1;

    // The two the appearance menu keeps for itself: Rust's AppState carries
    // these, since neither is a theme setting.
    bool fpsMonitor = false;
    bool appMenuBar = true;

    // The file the editor holds, and what the tree said last.
    char openPath[1024] = {};
    // The extension `LanguageFor` copied out of the path. Must be owned: the
    // path handed to `OpenFile` need not survive this call, and
    // `Highlighter::Language` must keep seeing "md" next frame.
    char language[32] = {};

    // The autocorrect lint's diagnostics, rebuilt when the document
    // changes, and the quickfix each carries — Rust keeps the same pair in
    // ExampleLspStore as `diagnostics` and `code_actions`.
    Diagnostic* diagnostics = nullptr;
    LintFix* fixes = nullptr;
    int nDiagnostics = 0;
    int lintedLen = -1;

    ~EditorApp() {
        for (int i = 0; i < nDiagnostics; i++) {
            StrFree(diagnostics[i].message);
            StrFree(fixes[i].newText);
        }
        Free(nullptr, diagnostics);
        Free(nullptr, fixes);
    }
    static El* Render(EditorApp* self, Ctx* cx);
};

// ─── the tree ─────────────────────────────────────────────────────────────

static void SortDir(DirEntry* e, int n) {
    // Folders first, then by name — an insertion sort over one directory.
    for (int i = 1; i < n; i++) {
        DirEntry key = e[i];
        int j = i - 1;
        while (j >= 0) {
            bool after = e[j].isDir != key.isDir
                             ? (!e[j].isDir && key.isDir)
                             : StrCmp(Str(e[j].name), Str(key.name)) > 0;
            if (!after) {
                break;
            }
            e[j + 1] = e[j];
            j--;
        }
        e[j + 1] = key;
    }
}

static void LoadDir(TreeState* s, const autocorrect::Ignorer* ig, Str path,
                    int parent, int depth) {
    // One listing per level, on the heap: a static array here would be the
    // same array the level above is still walking.
    const int kMaxEntries = 512;
    DirEntry* found = AllocArray<DirEntry>(kMaxEntries);
    if (!found) {
        return;
    }
    TempStr pathZ = StrDupTemp(path);
    int got = PlatListDir(pathZ.s, found, kMaxEntries);
    SortDir(found, got);
    for (int i = 0; i < got; i++) {
        TempStr child = fmt("%s/%s", path, Str(found[i].name));
        if (child.len >= 1024) {
            continue;
        }
        // The ignorer is asked with the path relative to the walked root,
        // which is what `child` is: the walk starts at ".".
        if (SkipEntry(ig, child, Str(found[i].name))) {
            continue;
        }
        // TreeAddItem copies both strings; the state owns and frees them.
        int ix = TreeAddItem(s, child, Str(found[i].name), parent);
        if (ix < 0) {
            break;
        }
        s->items[ix].folder = found[i].isDir;
        if (found[i].isDir && depth > 0) {
            LoadDir(s, ig, child, ix, depth - 1);
        }
    }
    Free(nullptr, found);
}

// The language a file's extension names, which is what the editor scans it as.
// Writes the last extension (after `/` or `\`) into `out`.
static void LanguageFor(Str path, char* out, int cap) {
    if (!out || cap <= 0) {
        return;
    }
    out[0] = 0;
    if (!path) {
        return;
    }
    int dot = -1;
    for (int i = 0; i < path.len; i++) {
        if (path.s[i] == '.') {
            dot = i + 1;
        } else if (path.s[i] == '/' || path.s[i] == '\\') {
            dot = -1;
        }
    }
    if (dot >= 0) {
        int n = std::min(path.len - dot, cap - 1);
        memcpy(out, path.s + dot, (size_t)n);
        out[n] = 0;
    }
}

// ─── the autocorrect lint ─────────────────────────────────────────────────
//
// lint_document in the Rust example: the document goes through
// autocorrect::lint_for as the language the highlighter names, and each
// LineResult becomes a diagnostic plus a quickfix. The severity mapping is
// Rust's, copied exactly: Error → Warning, Warning → Hint, Pass → Info.
//
// Rust background_spawns the lint; this runs it on the UI thread, measured
// first: ~30 ms per MB of markdown and ~16 ms per MB of C, so the 4 MB
// OpenFile cap keeps the worst case around a tenth of a second and a
// typical document under a millisecond. If a document ever wants more, the
// ExecSpawn + WindowPost pair is the seam to move it to.

// The canonical language name lint_for dispatches on — Rust passes
// `self.language.name()` ("rust", "markdown"), never the extension, and an
// extension the highlighter does not know becomes Plain, whose name is
// "text" (linted as markdown). SyntaxLangFor is that same table here.
static Str LintLanguageName(const char* ext) {
    Str name = component::SyntaxLangName(component::SyntaxLangFor(Str(ext)));
    return name.len > 0 ? name : StrL("text");
}

// `n` chars forward from byte `at`, staying inside the text — the crate
// counts columns and lengths in chars, the document is bytes.
static int AdvanceChars(Str text, int at, int n) {
    uint32_t cp = 0;
    while (n > 0 && at < text.len) {
        at += Utf8At(text, at, &cp);
        n--;
    }
    return at;
}

static void Lint(EditorApp* self) {
    for (int i = 0; i < self->nDiagnostics; i++) {
        StrFree(self->diagnostics[i].message);
        StrFree(self->fixes[i].newText);
    }
    self->nDiagnostics = 0;
    Str text = InputValue(&self->editor);
    self->lintedLen = text.len;

    Arena* a = ArenaNew();
    autocorrect::LintResult result =
        autocorrect::LintFor(a, text, LintLanguageName(self->language));
    for (int i = 0; i < result.nLines && self->nDiagnostics < kMaxDiagnostics;
         i++) {
        const autocorrect::LineResult& item = result.lines[i];
        if (!self->diagnostics) {
            self->diagnostics = AllocArray<Diagnostic>(kMaxDiagnostics);
            self->fixes = AllocArray<LintFix>(kMaxDiagnostics);
            if (!self->diagnostics || !self->fixes) {
                break;
            }
        }
        // 1-based (line, col) to byte offsets; the end is the start plus
        // item.old counted in chars, which is Rust's
        // `col + item.old.chars().count()`.
        int row = item.line - 1;
        int lineStart = RopeLineStartOffset(text, row);
        int lineEnd = RopeLineEndOffset(text, row);
        int start = AdvanceChars(Str(text.s + lineStart, lineEnd - lineStart),
                                 0, item.col - 1) +
                    lineStart;
        int oldChars = 0;
        for (int at = 0; at < item.old.len;) {
            uint32_t cp = 0;
            at += Utf8At(item.old, at, &cp);
            oldChars++;
        }
        int end = AdvanceChars(text, start, oldChars);
        int ix = self->nDiagnostics++;
        Diagnostic& d = self->diagnostics[ix];
        d = Diagnostic{};
        d.range.start = start;
        d.range.end = end;
        switch (item.severity) {
            case autocorrect::Severity::Error:
                d.severity = DiagnosticSeverity::Warning;
                break;
            case autocorrect::Severity::Warning:
                d.severity = DiagnosticSeverity::Hint;
                break;
            case autocorrect::Severity::Pass:
            default:
                d.severity = DiagnosticSeverity::Info;
                break;
        }
        d.message = StrDup(fmt("AutoCorrect: %s", item.neu));
        self->fixes[ix].range = d.range;
        self->fixes[ix].newText = StrDup(item.neu);
    }
    ArenaDelete(a);
}

// The second code action provider — Rust registers the LspStore beside
// TextConvertor. A request whose range sits inside a lint finding gets that
// finding's one edit.
static int AutocorrectQuickfixes(void* data, Arena* a, Str, Selection sel,
                                 CodeActionItem* out, int cap) {
    EditorApp* self = (EditorApp*)data;
    int n = 0;
    for (int i = 0; i < self->nDiagnostics; i++) {
        Selection range = self->fixes[i].range;
        if (sel.start < range.start || sel.end > range.end) {
            continue;
        }
        if (n < cap && out) {
            out[n].title =
                StrDup(a, fmt("Change to '%s'", self->fixes[i].newText));
            out[n].range = range;
            out[n].newText = StrDup(a, self->fixes[i].newText);
        }
        n++;
    }
    return n;
}

static void OpenFile(EditorApp* self, Str path) {
    if (!path) {
        return;
    }
    TempStr pathZ = StrDupTemp(path);
    FILE* f = fopen(pathZ.s, "rb");
    if (!f) {
        return;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    // A file this example will not open in one gulp is one the editor should
    // not be asked to hold either.
    const long kMax = 4 * 1024 * 1024;
    if (size <= 0 || size > kMax) {
        fclose(f);
        return;
    }
    char* buf = (char*)Alloc(nullptr, (int)size + 1);
    if (!buf) {
        fclose(f);
        return;
    }
    size_t got = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[got] = 0;
    InputSetValue(&self->editor, Str(buf, (int)got));
    Free(nullptr, buf);
    int pathLen = std::min(path.len, (int)sizeof(self->openPath) - 1);
    memcpy(self->openPath, path.s, (size_t)pathLen);
    self->openPath[pathLen] = 0;
    LanguageFor(path, self->language, (int)sizeof(self->language));
    Lint(self);
}

static void OnTreeEvent(EditorApp*, Ctx* cx, const TreeEvent*) {
    Notify(cx);
}

// ─── the completion provider ──────────────────────────────────────────────
//
// `ExampleLspStore`'s own: the items are upstream's
// `fixtures/completion_items.json`, read once, and what the menu shows is the
// ones whose label starts with the word being typed. Rust's provider answers
// a Task and filters with a fuzzy matcher; there is nothing to await here and
// no matcher, so it is a prefix and the answer is immediate.

static const int kMaxItems = 256;
static CompletionItem gItems[kMaxItems];
static int gNItems = 0;
static Arena* gItemArena = nullptr;

// CompletionProvider::resolve_completions — completionItem/resolve. The menu
// asks about the item the selection is on; this looks its documentation up in
// the same table the items came from.
// `additionalTextEdits`: what else accepting an item writes. A name that
// needs an import is the case the protocol has in mind, so `unwrap` brings
// its own line in at the top of the document — the caret's insert and this
// go in together, as one undo step.
static const TextEditItem kUseImport = {{0, 0}, StrL("use std::result;\n")};

static void LoadCompletionItems() {
    if (gNItems > 0) {
        return;
    }
    TempStr json = AssetsLoadTextTemp(StrL("completion_items.json"));
    if (json.len <= 0) {
        return;
    }
    gItemArena = ArenaNew();
    JsonValue* root = JsonParse(gItemArena, json);
    if (!root) {
        return;
    }
    for (const JsonValue* v = root->first; v && gNItems < kMaxItems;
         v = v->next) {
        CompletionItem& item = gItems[gNItems];
        item.label = JsonString(JsonGet(v, "label"));
        item.detail = JsonString(JsonGet(v, "detail"));
        item.documentation = JsonString(JsonGet(v, "documentation"));
        if (item.label.len == 0) {
            continue;
        }
        gNItems++;
    }
}

// HoverProvider: the word under the pointer, looked up in the same items —
// its documentation, or the sentence Rust shows when the item has none.
static Str HoverAt(void*, Str text, int offset) {
    LoadCompletionItems();
    int a = offset, b = offset;
    if (!TextWordRangeAt(text, offset, &a, &b) || a >= b) {
        return Str{};
    }
    Str word(text.s + a, b - a);
    for (int i = 0; i < gNItems; i++) {
        const CompletionItem& item = gItems[i];
        if (!StrEq(item.label, word)) {
            continue;
        }
        return item.documentation.len > 0 ? item.documentation
                                          : StrL("No documentation available.");
    }
    return Str{};
}

static int CompleteFrom(void*, Str, int, Str query, CompletionItem* out,
                        int cap) {
    LoadCompletionItems();
    int n = 0;
    for (int i = 0; i < gNItems; i++) {
        const CompletionItem& item = gItems[i];
        if (query.len > item.label.len) {
            continue;
        }
        if (query.len > 0 && !StrEq(Str(item.label.s, query.len), query)) {
            continue;
        }
        if (n < cap && out) {
            out[n] = item;
            // The items go out *thin*, which is what a server does with a
            // thousand of them: the documentation is left for `resolve` to
            // fill in when one is looked at.
            out[n].documentation = Str{};
            // One item brings an import with it, which is what
            // `additionalTextEdits` is for.
            if (StrEq(item.label, StrL("unwrap"))) {
                out[n].additionalEdits = &kUseImport;
                out[n].nAdditionalEdits = 1;
            }
        }
        n++;
    }
    return n;
}

static Str ResolveCompletion(void*, Arena* a, const CompletionItem* item) {
    (void)a;
    LoadCompletionItems();
    for (int i = 0; i < gNItems; i++) {
        if (StrEq(gItems[i].label, item->label)) {
            return gItems[i].documentation;
        }
    }
    return Str{};
}

// CompletionProvider::is_completion_trigger. The rule underneath the provider
// is a word character or `.`; a C++ document wants `:` as well, since a
// member of a namespace is reached through one.
static CompletionTrigger CompletionTriggerAt(void*, Str, int, Str typed) {
    if (typed.len == 0) {
        return CompletionTrigger::Close;
    }
    char c = typed.s[0];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '_') {
        return CompletionTrigger::Continue;
    }
    if (c == '.' || c == ':') {
        return CompletionTrigger::Open;
    }
    return CompletionTrigger::Close;
}

// ─── the code action provider ─────────────────────────────────────────────
//
// TextConvertor's, which is the five ways it offers to rewrite whatever is
// selected. Rust wraps each in a WorkspaceEdit against a document URI; a
// field is the one document here, so an action is the range and the text.

static Str CaseMapped(Arena* a, Str src, int which) {
    char* out = (char*)Alloc(a, src.len * 2 + 1);
    int n = 0;
    bool startOfWord = true;
    for (int i = 0; i < src.len; i++) {
        char c = src.s[i];
        bool upper = c >= 'A' && c <= 'Z';
        bool lower = c >= 'a' && c <= 'z';
        char up = lower ? (char)(c - 'a' + 'A') : c;
        char down = upper ? (char)(c - 'A' + 'a') : c;
        switch (which) {
            case 0: // Convert to Uppercase
                out[n++] = up;
                break;
            case 1: // Convert to Lowercase
                out[n++] = down;
                break;
            case 2: // Titleize: every word's first letter
                out[n++] = startOfWord ? up : c;
                break;
            case 3: // Capitalize: the first letter of the whole run
                out[n++] = i == 0 ? up : c;
                break;
            default: // snake_case: an underscore in front of every capital
                if (upper && i != 0) {
                    out[n++] = '_';
                }
                out[n++] = down;
                break;
        }
        startOfWord = c == ' ' || c == '\t' || c == '\n';
    }
    return Str(out, n);
}

// ─── the inline completion provider ───────────────────────────────────────
//
// The ghost text in front of the caret. A real one asks a suggestion engine;
// this one answers for two openings a C++ file has plenty of — `for (` and
// `if (` — so the debounce, the drawing and Tab can all be seen working.
static Str InlineCompletionAt(void*, Arena* a, Str text, int offset) {
    if (offset <= 0 || offset > text.len) {
        return Str{};
    }
    // What was typed up to the caret, back to the start of the line.
    int lineStart = offset;
    while (lineStart > 0 && text.s[lineStart - 1] != '\n') {
        lineStart--;
    }
    Str line(text.s + lineStart, offset - lineStart);
    auto endsWith = [](Str s, const char* suffix) {
        int n = (int)strlen(suffix);
        return s.len >= n && StrEq(Str(s.s + s.len - n, n), Str(suffix, n));
    };
    if (endsWith(line, "for (")) {
        return StrDup(a, StrL("int i = 0; i < n; i++) {\n}"));
    }
    if (endsWith(line, "if (")) {
        return StrDup(a, StrL("!s) {\n    return;\n}"));
    }
    return Str{};
}

// ─── the range semantic tokens provider ───────────────────────────────────
//
// `MarkerHighlighter` from the Rust markdown example: every TODO / FIXME /
// XXX / HACK / NOTE in the document gets a token type of its own, so each
// renders in a different colour of the theme. The scan is synchronous and the
// answer delta-encoded exactly as a language server sends it — which is the
// point of the exercise, since the decoding is the editor's.
struct Marker {
    Str word;
    const char* type;
};

static const Marker kSemanticMarkers[] = {
    {StrL("TODO"), "keyword"}, {StrL("FIXME"), "string"},
    {StrL("XXX"), "number"},   {StrL("HACK"), "function"},
    {StrL("NOTE"), "type"},
};
static const int kNMarkers =
    (int)(sizeof(kSemanticMarkers) / sizeof(kSemanticMarkers[0]));

// The legend the `tokenType` of each answer indexes into.
static const Str kMarkerLegend[kNMarkers] = {
    StrL("keyword"),  StrL("string"), StrL("number"),
    StrL("function"), StrL("type"),
};

struct MarkerHit {
    int line;
    int col;
    int len;
    int type;
};

static int SemanticTokensFor(void*, Str text, Selection range,
                             SemanticToken* out, int cap) {
    if (!text.s) {
        return 0;
    }
    Vec<MarkerHit> hits;
    // Walk the document first, rather than one token type at a time. The
    // result is already in the document order LSP delta encoding requires,
    // and Rust's Vec has no counterpart to the port's former 512-hit array.
    for (int i = range.start; i < range.end; i++) {
        for (int t = 0; t < kNMarkers; t++) {
            Str word = kSemanticMarkers[t].word;
            // Most bytes cannot begin a marker. Avoid five strlen/StrEq
            // calls per byte on every edit (the typing profile's hotspot).
            if (text.s[i] != word.s[0]) {
                continue;
            }
            if (i + word.len > range.end) {
                continue;
            }
            if (!StrEq(Str(text.s + i, word.len), word)) {
                continue;
            }
            RopePoint p = RopeOffsetToPoint(text, i);
            VecAppend(hits, MarkerHit{p.row, p.column, word.len, t});
            i += word.len - 1;
            break;
        }
    }
    int prevLine = 0, prevCol = 0;
    for (int i = 0; i < hits.len; i++) {
        int deltaLine = hits[i].line - prevLine;
        if (i < cap && out) {
            out[i].deltaLine = (uint32_t)deltaLine;
            out[i]
                .deltaStart = (uint32_t)(deltaLine == 0 ? hits[i].col - prevCol
                                                        : hits[i].col);
            out[i].length = (uint32_t)hits[i].len;
            out[i].tokenType = (uint32_t)hits[i].type;
            out[i].tokenModifiers = 0;
        }
        prevLine = hits[i].line;
        prevCol = hits[i].col;
    }
    return hits.len;
}

// ─── the definition provider ──────────────────────────────────────────────
//
// `ExampleLspStore`'s own: `Duration` is defined in this document, and the
// std type names have a page on doc.rust-lang.org. Rust answers a Task of
// `LocationLink`s; the same two answers are written straight out here.
struct DocLink {
    const char* name;
    const char* path;
};

static const DocLink kRustDocs[] = {
    {"HashMap", "collections/hash_map/struct.HashMap"},
    {"HashSet", "collections/hash_set/struct.HashSet"},
    {"Arc", "sync/struct.Arc"},
    {"RwLock", "sync/struct.RwLock"},
    {"Duration", "time/struct.Duration"},
};

static int DefinitionsAt(void*, Arena* a, Str text, int offset,
                         DefinitionLink* out, int cap) {
    int wa = offset, wb = offset;
    if (!TextWordRangeAt(text, offset, &wa, &wb) || wa >= wb) {
        return 0;
    }
    Str word(text.s + wa, wb - wa);
    // The one symbol this document defines: the first `Duration` in it, which
    // is where the word is declared.
    if (StrEqI(word, "Duration")) {
        int at = -1;
        for (int i = 0; i + word.len <= text.len; i++) {
            if (StrEq(Str(text.s + i, word.len), word) && i != wa) {
                at = i;
                break;
            }
        }
        if (at >= 0) {
            if (cap > 0 && out) {
                out[0].origin = {wa, wb};
                out[0].uri = Str{};
                out[0].target = {at, at + word.len};
            }
            return 1;
        }
    }
    for (const DocLink& d : kRustDocs) {
        if (!base::StrEqI(word, d.name)) {
            continue;
        }
        if (cap > 0 && out) {
            out[0].origin = {wa, wb};
            out[0].uri = StrDup(
                a, fmt("https://doc.rust-lang.org/std/%s.html", Str(d.path)));
            out[0].target = {};
        }
        return 1;
    }
    return 0;
}

static int CodeActionsFor(void*, Arena* a, Str text, Selection sel,
                          CodeActionItem* out, int cap) {
    if (sel.IsEmpty() || sel.end > text.len) {
        return 0;
    }
    static const char* kTitles[] = {
        "Convert to Uppercase", "Convert to Lowercase",  "Titleize",
        "Capitalize",           "Convert to snake_case",
    };
    Str selected(text.s + sel.start, sel.end - sel.start);
    int n = 0;
    for (int i = 0; i < (int)(sizeof(kTitles) / sizeof(kTitles[0])); i++) {
        if (n < cap && out) {
            out[n].title = Str(kTitles[i]);
            out[n].range = sel;
            out[n].newText = CaseMapped(a, selected, i);
        }
        n++;
    }
    // One action that is more than one edit, which is what a WorkspaceEdit
    // carries and what a rename or an extract is made of: the two ends of the
    // selection are written separately, last one first so the first does not
    // move the second.
    if (n < cap && out) {
        auto* edits = (TextEditItem*)Alloc(a, (int)sizeof(TextEditItem) * 2);
        if (edits) {
            edits[0].range = Selection{sel.end, sel.end};
            edits[0].newText = StrL(")");
            edits[1].range = Selection{sel.start, sel.start};
            edits[1].newText = StrL("(");
            out[n].title = StrL("Wrap in Parentheses");
            out[n].edits = edits;
            out[n].nEdits = 2;
        }
    }
    n++;
    return n;
}

// ─── the document colour provider ─────────────────────────────────────────
//
// ExampleLspStore's, which hands the document to the `color-lsp` crate and
// answers what it found. There is no such crate here, so this is the same
// scan over the two spellings that turn up in the source a code editor is
// pointed at: `#rgb` / `#rgba` / `#rrggbb` / `#rrggbbaa`, and `rgb(...)` /
// `rgba(...)` with a number per channel. What comes back is painted behind
// the text that names it, which is what element.rs does with a colour.

static int HexDigit(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

// A hex colour at `at`, or 0 if what stands there is not one.
static int HexColorAt(Str text, int at, Rgba* out) {
    int n = 0;
    while (at + 1 + n < text.len && n < 9 &&
           HexDigit(text.s[at + 1 + n]) >= 0) {
        n++;
    }
    if (n != 3 && n != 4 && n != 6 && n != 8) {
        return 0;
    }
    int v[8] = {};
    for (int i = 0; i < n; i++) {
        v[i] = HexDigit(text.s[at + 1 + i]);
    }
    uint8_t c[4] = {0, 0, 0, 255};
    if (n <= 4) {
        // The short spelling doubles each digit: #1af is #11aaff.
        for (int i = 0; i < n; i++) {
            c[i] = (uint8_t)(v[i] * 17);
        }
    } else {
        for (int i = 0; i < n / 2; i++) {
            c[i] = (uint8_t)(v[i * 2] * 16 + v[i * 2 + 1]);
        }
    }
    *out = Rgba{c[0], c[1], c[2], c[3]};
    return n + 1;
}

// `rgb(1, 2, 3)` or `rgba(1, 2, 3, 0.5)`, in as many spellings as a scan this
// small can take: the numbers, in order, and whatever separates them.
static int RgbColorAt(Str text, int at, Rgba* out) {
    bool hasAlpha = at + 4 < text.len && text.s[at + 3] == 'a';
    int i = at + (hasAlpha ? 4 : 3);
    if (i >= text.len || text.s[i] != '(') {
        return 0;
    }
    i++;
    float ch[4] = {0, 0, 0, 1};
    int got = 0;
    while (i < text.len && got < 4) {
        while (i < text.len && (text.s[i] == ' ' || text.s[i] == ',')) {
            i++;
        }
        if (i >= text.len || text.s[i] == ')') {
            break;
        }
        int digits = 0;
        float value = 0;
        while (i < text.len && text.s[i] >= '0' && text.s[i] <= '9') {
            value = value * 10 + (float)(text.s[i] - '0');
            i++;
            digits++;
        }
        if (i < text.len && text.s[i] == '.') {
            i++;
            float scale = 0.1f;
            while (i < text.len && text.s[i] >= '0' && text.s[i] <= '9') {
                value += (float)(text.s[i] - '0') * scale;
                scale *= 0.1f;
                i++;
                digits++;
            }
        }
        if (digits == 0) {
            return 0;
        }
        ch[got++] = value;
    }
    if (i >= text.len || text.s[i] != ')' || got < 3) {
        return 0;
    }
    auto clamp = [](float v) {
        return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
    };
    *out = Rgba{clamp(ch[0]), clamp(ch[1]), clamp(ch[2]),
                clamp(ch[3] <= 1.f ? ch[3] * 255.f : ch[3])};
    return i + 1 - at;
}

static bool WordCharAt(Str text, int at) {
    if (at < 0 || at >= text.len) {
        return false;
    }
    char c = text.s[at];
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

static int DocumentColorsIn(void*, Str text, DocumentColor* out, int cap) {
    int n = 0;
    for (int i = 0; i < text.len; i++) {
        Rgba color = {};
        int len = 0;
        if (text.s[i] == '#') {
            len = HexColorAt(text, i, &color);
        } else if ((text.s[i] == 'r' && i + 3 < text.len &&
                    StrEq(Str(text.s + i, 3), StrL("rgb"))) &&
                   !WordCharAt(text, i - 1)) {
            len = RgbColorAt(text, i, &color);
        }
        if (len <= 0) {
            continue;
        }
        if (n < cap && out) {
            out[n].range = Selection{i, i + len};
            out[n].color = color;
        }
        n++;
        i += len - 1;
    }
    return n;
}

// ─── the status bar's switches ────────────────────────────────────────────

enum {
    kToggleLineNumbers = 0,
    kToggleSoftWrap,
    kToggleWhitespaces,
    kToggleIndentGuides,
    kToggleFolding,
    kToggleReadOnly
};

enum {
    kCycleScrollBeyond = 0,
    kCycleCursorSurrounding
};

static int CycleRows(int v) {
    if (v < 0) {
        return 0;
    }
    if (v == 0) {
        return 3;
    }
    if (v == 3) {
        return 8;
    }
    return -1;
}

static Str RowsLabel(Arena* a, int v) {
    if (v < 0) {
        return StrL("default");
    }
    return StrDup(a, fmt("%d", v));
}

static void OnToggle(EditorApp* self, Ctx* cx, const ClickEvent*,
                     intptr_t which) {
    switch (which) {
        case kToggleLineNumbers:
            self->lineNumbers = !self->lineNumbers;
            self->editor.mode.lineNumber = self->lineNumbers;
            break;
        case kToggleSoftWrap:
            self->softWrap = !self->softWrap;
            self->editor.softWrap = self->softWrap;
            break;
        case kToggleWhitespaces:
            self->showWhitespaces = !self->showWhitespaces;
            self->editor.showWhitespaces = self->showWhitespaces;
            break;
        case kToggleIndentGuides:
            self->indentGuides = !self->indentGuides;
            break;
        case kToggleFolding:
            self->folding = !self->folding;
            self->editor.mode.folding = self->folding;
            break;
        default:
            self->readOnly = !self->readOnly;
            self->editor.readonly = self->readOnly;
            break;
    }
    Notify(cx);
}

static void OnCycleRows(EditorApp* self, Ctx* cx, const ClickEvent*,
                        intptr_t which) {
    if (which == kCycleScrollBeyond) {
        self->scrollBeyondLastLine = CycleRows(self->scrollBeyondLastLine);
        self->editor.scrollBeyondLastLine = self->scrollBeyondLastLine;
    } else {
        self->cursorSurroundingLines = CycleRows(self->cursorSurroundingLines);
        self->editor.cursorSurroundingLines = self->cursorSurroundingLines;
    }
    Notify(cx);
}

static void OpenGoTo(EditorApp* self, Ctx* cx, const ClickEvent*) {
    RopePoint at = InputCursorPosition(&self->editor);
    InputSetPlaceholder(&self->goToLine,
                        StrDup(fmt("%d:%d", at.row, at.column)));
    InputSetValue(&self->goToLine, Str{});
    self->goToLine.focused = true;
    self->dialogOpen = true;
    Notify(cx);
}

static void CloseGoTo(EditorApp* self, Ctx* cx, const ClickEvent*) {
    self->dialogOpen = false;
    self->goToLine.focused = false;
    self->editor.focused = true;
    Notify(cx);
}

static void ConfirmGoTo(EditorApp* self, Ctx* cx, const ClickEvent* ev) {
    Str query = InputValue(&self->goToLine);
    int line = 0;
    int column = 1;
    int at = 0;
    bool any = false;
    while (at < query.len && query.s[at] >= '0' && query.s[at] <= '9') {
        line = line * 10 + (query.s[at] - '0');
        at++;
        any = true;
    }
    if (any && at < query.len && query.s[at] == ':') {
        at++;
        column = 0;
        while (at < query.len && query.s[at] >= '0' && query.s[at] <= '9') {
            column = column * 10 + (query.s[at] - '0');
            at++;
        }
    }
    if (any) {
        Str text = InputValue(&self->editor);
        int row = line > 0 ? line - 1 : 0;
        int col = column > 0 ? column - 1 : 0;
        int offset = RopeLineStartOffset(text, row) + col;
        InputMoveTo(&self->editor, cx,
                    RopeClipOffset(text, offset, Bias::Left));
    }
    CloseGoTo(self, cx, ev);
}

// A switch in the status bar. Rust says what is on with a check in front of
// the label rather than with a pressed look: `.when(on, |this|
// this.icon(IconName::Check))`.
static El* ToggleButton(Ctx* cx, Str id, Str label, bool on, Listener toggle,
                        intptr_t which) {
    component::Button* b = component::Button::New(cx, id)
                               ->Ghost()
                               ->WithSize(UiSize::XSmall)
                               ->Label(label);
    if (on) {
        b->Icon(IconName::Check);
    }
    return b->OnClick(ListenerArg(toggle, which))->IntoEl();
}

// editor.rs's file-tree row: ListItem with py_0p5, px_2, pl(16*depth+8),
// gap_2. ListItem itself paints text_base (16), so a row is that line box
// plus 2px of pad each side — closer together than the story tree's 34px
// py_1 / text_base rows. The wrapper also has p_1 and the sidebar fill.
static const float kFileTreeRowH = 28;

static El* FileTreeRow(void*, Ctx* cx, int, const TreeEntry& entry,
                       TreeEntryState entryState) {
    const Theme& th = ThemeNow(cx->app);
    const TreeItem* it = entry.item;
    if (!it) {
        return nullptr;
    }
    Arena* a = cx->a;
    Rgba fg = it->disabled ? th.mutedFg : th.sidebarFg;
    El* row = Div(a)
                  ->FlexRow()
                  ->W(kFill)
                  ->H(kFileTreeRowH)
                  ->PadR(8)
                  ->PadL(8.f + (float)it->depth * 16.f)
                  ->Gap(8)
                  ->ItemsCenter()
                  ->Radius(th.radius);
    if (!it->disabled) {
        row->HoverBg(th.tokens.muted);
    }
    if (entryState.IsSelected()) {
        row->Bg(th.tokens.accent);
    } else if (entryState.IsRightClicked()) {
        row->Bg(BackgroundOpacity(th.tokens.accent, 0.5f));
    }
    IconName ic = !it->folder    ? IconName::File
                  : it->expanded ? IconName::FolderOpen
                                 : IconName::Folder;
    row->Child(IconEl(a, ic, 16)->Fg(fg));
    row->Child(TextEl(a, it->label)->Font(16)->Fg(fg));
    return row;
}

// ─── the title bar ────────────────────────────────────────────────────────
//
// crates/story/src/title_bar.rs, and crates/story/src/app_menus.rs under it.
// Rust's editor window is opened by `create_new_window_with_size`, which
// wraps the view in a StoryRoot whose AppTitleBar draws the app menu bar on
// the left and the tools on the right. This example is a window of its own
// rather than a page of the gallery, so it draws the same bar itself.
//
// One menu Rust builds is not here: `Language` wants rust_i18n and a locale
// table, and this example has no labels to translate. Rust's `Go` menu names
// the gallery's command palette and theme picker; the editor's own way of
// going somewhere is the Go to line dialog, so that is the row under it.

#define EDITOR_ACTION(fn, spelled)                    \
    static uint32_t fn() {                            \
        static uint32_t id = ActionOf(StrL(spelled)); \
        return id;                                    \
    }

EDITOR_ACTION(ActAbout, "editor::About")
EDITOR_ACTION(ActOpen, "editor::Open")
EDITOR_ACTION(ActQuit, "editor::Quit")
EDITOR_ACTION(ActGoToLine, "editor::GoToLine")
EDITOR_ACTION(ActDocumentation, "editor::Documentation")
EDITOR_ACTION(ActOpenWebsite, "editor::OpenWebsite")
// The payload rides on the action, the way `SwitchThemeMode(mode)`,
// `SelectFont(px)` and `SelectRadius(px)` carry theirs in Rust.
EDITOR_ACTION(ActSwitchThemeMode, "editor::SwitchThemeMode")
EDITOR_ACTION(ActSelectFont, "editor::SelectFont")
EDITOR_ACTION(ActSelectRadius, "editor::SelectRadius")
EDITOR_ACTION(ActSelectScrollbarMode, "editor::SelectScrollbarMode")
EDITOR_ACTION(ActToggleListActiveHighlight, "editor::ToggleListActiveHighlight")
EDITOR_ACTION(ActToggleFpsMonitor, "editor::ToggleFpsMonitor")
EDITOR_ACTION(ActToggleAppMenuBar, "editor::ToggleAppMenuBar")

// `MenuItem::action("About", About)`. The gallery answers this with a dialog
// of its own; this example already owns one dialog, and says it in the place
// a window says everything else.
static void OnAboutAction(EditorApp*, Ctx* cx, const ActionEvent*) {
    WindowPushNotification(cx, component::NotificationType::Info,
                           StrL("Editor example — the C++ port of "
                                "crates/story/examples/editor.rs"));
}

// `on_action_open`: the desktop's own dialog, and what it answers is read
// into the editor.
static void OnOpenAction(EditorApp* self, Ctx* cx, const ActionEvent*) {
    PathPrompt prompt;
    prompt.files = true;
    prompt.directories = true;
    prompt.title = StrL("Select a source file");
    TempStr path = PromptForPathTemp(cx->win, prompt);
    if (!path) {
        return;
    }
    OpenFile(self, path);
    Notify(cx);
}

static void OnQuitAction(EditorApp*, Ctx* cx, const ActionEvent*) {
    AppQuitAll(cx->app);
}

static void OnGoToLineAction(EditorApp* self, Ctx* cx, const ActionEvent*) {
    OpenGoTo(self, cx, nullptr);
}

static void OnDocumentationAction(EditorApp*, Ctx*, const ActionEvent*) {
    OpenUrl(StrL("https://github.com/longbridge/gpui-kit"));
}

static void OnOpenWebsiteAction(EditorApp*, Ctx*, const ActionEvent*) {
    OpenUrl(StrL("https://github.com/longbridge/gpui-kit"));
}

static void OnGithubClick(EditorApp*, Ctx*, const ClickEvent*) {
    OpenUrl(StrL("https://github.com/longbridge/gpui-kit"));
}

static void OnSwitchThemeModeAction(EditorApp*, Ctx* cx,
                                    const ActionEvent* ev) {
    ThemeSet(cx->app, ev->arg == 0 ? ThemeMode::Light : ThemeMode::Dark);
    Notify(cx);
}

static void OnSelectFontAction(EditorApp*, Ctx* cx, const ActionEvent* ev) {
    ThemeSetFontSize(cx->app, (float)ev->arg);
    Notify(cx);
}

static void OnSelectRadiusAction(EditorApp*, Ctx* cx, const ActionEvent* ev) {
    ThemeSetRadius(cx->app, (float)ev->arg);
    Notify(cx);
}

static void OnSelectScrollbarModeAction(EditorApp*, Ctx* cx,
                                        const ActionEvent* ev) {
    ScrollbarModeSet(cx->app, (ScrollbarMode)(int)ev->arg);
    Notify(cx);
}

static void OnToggleListActiveHighlightAction(EditorApp*, Ctx* cx,
                                              const ActionEvent*) {
    ListSettings s = ListSettingsNow(cx->app);
    s.activeHighlight = !s.activeHighlight;
    ListSettingsSet(cx->app, s);
    Notify(cx);
}

static void OnToggleFpsMonitorAction(EditorApp* self, Ctx* cx,
                                     const ActionEvent*) {
    self->fpsMonitor = !self->fpsMonitor;
    Notify(cx);
}

static void OnToggleAppMenuBarAction(EditorApp* self, Ctx* cx,
                                     const ActionEvent*) {
    self->appMenuBar = !self->appMenuBar;
    Notify(cx);
}

// Every handler above, hung off the root so a row chosen in either bar finds
// one. Rust registers these with `cx.on_action` on the app rather than on an
// element, which is the same reach.
static El* EditorBindMenuActions(El* root, Ctx* cx) {
    return root->OnAction(ActAbout(), Listen(cx, &OnAboutAction))
        ->OnAction(ActOpen(), Listen(cx, &OnOpenAction))
        ->OnAction(ActQuit(), Listen(cx, &OnQuitAction))
        ->OnAction(ActGoToLine(), Listen(cx, &OnGoToLineAction))
        ->OnAction(ActDocumentation(), Listen(cx, &OnDocumentationAction))
        ->OnAction(ActOpenWebsite(), Listen(cx, &OnOpenWebsiteAction))
        ->OnAction(ActSwitchThemeMode(), Listen(cx, &OnSwitchThemeModeAction))
        ->OnAction(ActSelectFont(), Listen(cx, &OnSelectFontAction))
        ->OnAction(ActSelectRadius(), Listen(cx, &OnSelectRadiusAction))
        ->OnAction(ActSelectScrollbarMode(),
                   Listen(cx, &OnSelectScrollbarModeAction))
        ->OnAction(ActToggleListActiveHighlight(),
                   Listen(cx, &OnToggleListActiveHighlightAction))
        ->OnAction(ActToggleFpsMonitor(), Listen(cx, &OnToggleFpsMonitorAction))
        ->OnAction(ActToggleAppMenuBar(),
                   Listen(cx, &OnToggleAppMenuBarAction));
}

// cx.bind_keys: a menu row shows the chord bound to its action and nothing
// else, so the rows that want one have one bound to them.
static void EditorInitKeys() {
    static uint32_t bound = 0;
    if (bound == KeymapGeneration()) {
        return;
    }
    bound = KeymapGeneration();
    KeyBinding bindings[] = {
        {"secondary-o", ActOpen(), nullptr},
        {"secondary-g", ActGoToLine(), nullptr},
#if GPUI_OS_MAC
        {"cmd-q", ActQuit(), nullptr},
#else
        {"alt-f4", ActQuit(), nullptr},
#endif
    };
    KeymapBind(bindings, (int)(sizeof(bindings) / sizeof(bindings[0])));
}

static const int kEditorMenus = 4;

static MenuRow* EditorRows(Ctx* cx, int n) {
    // Zeroed is what every MenuRow field defaults to, which is what makes a
    // row that sets nothing a row that does nothing.
    return (MenuRow*)cx->a
        ->Push((uint64_t)n * sizeof(MenuRow), alignof(MenuRow), true);
}

// build_menus(title, cx), with the window's own name on the first one.
static int EditorBuildMenus(Ctx* cx, MenuDef* out, int cap) {
    if (cap < kEditorMenus) {
        return 0;
    }
    bool dark = ThemeGet(cx->app) == ThemeMode::Dark;
    MenuRow* appearance = EditorRows(cx, 2);
    appearance[0].label = StrL("Light");
    appearance[0].action = ActSwitchThemeMode();
    appearance[0].arg = 0;
    appearance[0].checked = !dark;
    appearance[1].label = StrL("Dark");
    appearance[1].action = ActSwitchThemeMode();
    appearance[1].arg = 1;
    appearance[1].checked = dark;

    MenuRow* appRows = EditorRows(cx, 7);
    appRows[0].label = StrL("About");
    appRows[0].action = ActAbout();
    appRows[1].separator = true;
    appRows[2].label = StrL("Open...");
    appRows[2].action = ActOpen();
    appRows[3].separator = true;
    appRows[4].label = StrL("Appearance");
    appRows[4].submenu = appearance;
    appRows[4].submenuN = 2;
    appRows[5].separator = true;
    appRows[6].label = StrL("Quit");
    appRows[6].action = ActQuit();
    out[0].name = StrL("Editor");
    out[0].items = appRows;
    out[0].n = 7;

    // Every row of the Edit menu names one of the input's actions and carries
    // no handler of its own: choosing it dispatches the action to whatever
    // field has the keyboard, which is the same handler the chord reaches,
    // and the shortcut beside it is looked up in the keymap rather than typed
    // here.
    struct EditRow {
        const char* label;
        uint32_t action;
    };
    const EditRow kEdit[] = {
        {"Undo", input::Undo()},
        {"Redo", input::Redo()},
        {nullptr, 0},
        {"Cut", input::Cut()},
        {"Copy", input::Copy()},
        {"Paste", input::Paste()},
        {nullptr, 0},
        {"Delete", input::Delete()},
        {"Delete Previous Word", input::DeleteToPreviousWordStart()},
        {"Delete Next Word", input::DeleteToNextWordEnd()},
        {nullptr, 0},
        {"Find", input::Search()},
        {nullptr, 0},
        {"Select All", input::SelectAll()},
    };
    const int nEdit = (int)(sizeof(kEdit) / sizeof(kEdit[0]));
    MenuRow* editRows = EditorRows(cx, nEdit);
    for (int i = 0; i < nEdit; i++) {
        if (!kEdit[i].label) {
            editRows[i].separator = true;
            continue;
        }
        editRows[i].label = Str(kEdit[i].label);
        editRows[i].action = kEdit[i].action;
    }
    out[1].name = StrL("Edit");
    out[1].items = editRows;
    out[1].n = nEdit;

    MenuRow* goRows = EditorRows(cx, 1);
    goRows[0].label = StrL("Go to Line/Column...");
    goRows[0].action = ActGoToLine();
    out[2].name = StrL("Go");
    out[2].items = goRows;
    out[2].n = 1;

    MenuRow* helpRows = EditorRows(cx, 3);
    helpRows[0].label = StrL("Documentation");
    helpRows[0].action = ActDocumentation();
    helpRows[0].disabled = true;
    helpRows[1].separator = true;
    helpRows[2].label = StrL("Open Website");
    helpRows[2].action = ActOpenWebsite();
    out[3].name = StrL("Help");
    out[3].items = helpRows;
    out[3].n = 3;
    return kEditorMenus;
}

static component::PopupMenu* EditorPopupMenu(Ctx* cx, Str id,
                                             const MenuRow* rows, int n) {
    component::PopupMenu* menu = component::PopupMenu::New(cx, id);
    for (int i = 0; i < n; i++) {
        const MenuRow& r = rows[i];
        if (r.separator || r.label.len <= 0) {
            menu->Separator();
            continue;
        }
        if (r.submenu && r.submenuN > 0) {
            Str subId = StrDup(cx->a, fmt("%s-%d", id, i));
            menu->Submenu(r.label,
                          EditorPopupMenu(cx, subId, r.submenu, r.submenuN));
            menu->Disabled(r.disabled);
            continue;
        }
        menu->MenuWithAction(r.label, r.action, r.arg);
        menu->Checked(r.checked);
        menu->Disabled(r.disabled);
    }
    return menu;
}

static El* EditorTitleMenuItem(Ctx* cx, Str label, bool semibold) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    El* text = TextEl(a, label)->Font(14)->Fg(th.foreground);
    if (semibold) {
        text->Semibold();
    }
    Str clickId = StrDup(a, fmt("editor-title-%s", label));
    return Div(a)
        ->H(kFill)
        ->PadX(8)
        ->ItemsCenter()
        ->Radius(th.radius)
        ->Click(HashClickId(clickId))
        ->HoverBg(th.tokens.muted)
        ->Child(text);
}

// AppMenuBar: the menus drawn into the title bar.
static El* EditorMenuBar(Ctx* cx, const MenuDef* menus, int n) {
    Arena* a = cx->a;
    El* bar = Div(a)->FlexRow()->H(kFill)->ItemsCenter();
    for (int i = 0; i < n; i++) {
        Str menuId = StrDup(a, fmt("editor-menu-%d", i));
        component::PopupMenu* menu =
            EditorPopupMenu(cx, menuId, menus[i].items, menus[i].n)->MinW(220);
        if (i == 1) {
            // The field's own key context, which is where the Edit menu's
            // actions are bound and so where their chords are found.
            menu->ActionContext("Input");
        }
        // The application's own menu is the one named for it, in the heavier
        // weight that makes it read as a title rather than as the first of
        // four.
        Str triggerId = StrDup(a, fmt("editor-menu-trigger-%d", i));
        bar->Child(component::DropdownMenu::New(cx, triggerId)
                       ->Trigger(EditorTitleMenuItem(cx, menus[i].name, i == 0))
                       ->Menu(menu)
                       ->IntoEl());
    }
    return bar;
}

// AppTitleBar's FontSizeSelector, which is the Appearance menu behind the
// Settings2 button. Every row names one of the actions above and carries the
// value it sets, which is what Rust's `SelectFont(18)` is; the table says
// which action a kind of row names, whether it is ticked, and what it reads
// back to say so.
enum class ApKind : uint8_t {
    Label,
    Sep,
    Font,
    Radius,
    Scroll,
    ListHighlight,
    Fps,
    MenuBar
};

struct ApRow {
    ApKind kind;
    const char* label;
    // The font size or radius in DIPs, or the scrollbar mode; unused by the
    // three toggles, which read what they toggle.
    float value;
};

static const ApRow kAppearance[] = {
    {ApKind::Label, "Font Size", 0},
    {ApKind::Font, "Large", 18},
    {ApKind::Font, "Medium (default)", 16},
    {ApKind::Font, "Small", 14},
    {ApKind::Sep, nullptr, 0},
    {ApKind::Label, "Border Radius", 0},
    {ApKind::Radius, "8px", 8},
    {ApKind::Radius, "6px (default)", 6},
    {ApKind::Radius, "4px", 4},
    {ApKind::Radius, "0px", 0},
    {ApKind::Sep, nullptr, 0},
    {ApKind::Label, "Scrollbar", 0},
    {ApKind::Scroll, "Scrolling to show", (float)ScrollbarMode::Scrolling},
    {ApKind::Scroll, "Hover to show", (float)ScrollbarMode::Hover},
    {ApKind::Scroll, "Always show", (float)ScrollbarMode::Always},
    {ApKind::Sep, nullptr, 0},
    {ApKind::ListHighlight, "List Active Highlight", 0},
    {ApKind::Fps, "FPS Monitor", 0},
    // ToggleAppMenuBar: on a Mac the menus are already in the system bar, and
    // this is what puts the component itself on screen beside them. Turning
    // it off gives the freed up left side back to the window's name.
    {ApKind::MenuBar, "App Menu Bar", 0},
};

static const int kAppearanceRows = (int)(sizeof(kAppearance) / sizeof(ApRow));

// menu_with_check: which row is the one in force.
static bool ApChecked(const EditorApp* self, Ctx* cx, const ApRow& r) {
    switch (r.kind) {
        case ApKind::Font:
            return ThemeFontSize(cx->app) == r.value;
        case ApKind::Radius:
            return ThemeNow(cx->app).radius == r.value;
        case ApKind::Scroll:
            return ScrollbarModeNow(cx->app) == (ScrollbarMode)(int)r.value;
        case ApKind::ListHighlight:
            return ListSettingsNow(cx->app).activeHighlight;
        case ApKind::Fps:
            return self->fpsMonitor;
        case ApKind::MenuBar:
            return self->appMenuBar;
        default:
            return false;
    }
}

// Which action a row of the table dispatches. The three that carry a value
// hand it over as the action's payload — `SelectFont(18)` — and the toggles
// carry nothing, since what they flip is what they read.
static uint32_t ApAction(ApKind kind) {
    switch (kind) {
        case ApKind::Font:
            return ActSelectFont();
        case ApKind::Radius:
            return ActSelectRadius();
        case ApKind::Scroll:
            return ActSelectScrollbarMode();
        case ApKind::ListHighlight:
            return ActToggleListActiveHighlight();
        case ApKind::Fps:
            return ActToggleFpsMonitor();
        case ApKind::MenuBar:
            return ActToggleAppMenuBar();
        default:
            return 0;
    }
}

static El* EditorAppearanceMenu(EditorApp* self, Ctx* cx) {
    component::PopupMenu* menu =
        component::PopupMenu::New(cx, StrL("editor-appearance-menu"));
    for (int i = 0; i < kAppearanceRows; i++) {
        const ApRow& r = kAppearance[i];
        switch (r.kind) {
            case ApKind::Label:
                menu->Label(Str(r.label));
                break;
            case ApKind::Sep:
                menu->Separator();
                break;
            default:
                menu->MenuWithAction(Str(r.label), ApAction(r.kind),
                                     (intptr_t)r.value);
                menu->Checked(ApChecked(self, cx, r));
                break;
        }
    }
    // check_side(Right): the tick sits on the far edge, so the labels start
    // flush.
    menu->CheckSide(Side::Right);
    return component::DropdownMenu::New(cx, StrL("editor-appearance"))
        ->Trigger(component::Button::New(cx, StrL("editor-title-settings"))
                      ->Icon(IconName::Settings2)
                      ->Ghost()
                      ->Compact()
                      ->WithSize(UiSize::Small)
                      ->Tooltip(StrL("Appearance"))
                      ->IntoEl()
                      ->Cursor(CursorKind::Pointer))
        ->Menu(menu)
        // Anchor::TopRight: the menu's right edge lines up with the button's,
        // which is what keeps it on screen at the corner of the window.
        ->AnchorRight()
        ->IntoEl();
}

// The three tools at the right of the title bar. They are ghost buttons, and
// over a title bar there is nothing else to say an icon is a control rather
// than an ornament, so they ask for the hand themselves.
static El* EditorTitleBar(EditorApp* self, Ctx* cx, const MenuDef* defs,
                          int nDefs) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    El* menus = Div(a)->FlexRow()->H(kFill)->ItemsCenter()->Shrink0();
    if (self->appMenuBar) {
        menus->Child(EditorMenuBar(cx, defs, nDefs));
    } else {
        // The system menu bar owns the menus, so the freed up left side names
        // the window the way a Mac application does.
        menus->Child(Div(a)->PadX(8)->Child(
            TextEl(a, StrL("Editor"))->Font(14)->Fg(th.foreground)->Medium()));
    }
    El* tools =
        Div(a)
            ->FlexRow()
            ->H(kFill)
            ->ItemsCenter()
            ->Shrink0()
            ->PadX(8)
            ->Gap(2)
            ->Child(EditorAppearanceMenu(self, cx))
            ->Child(component::Button::New(cx, StrL("editor-title-github"))
                        ->Icon(IconName::Github)
                        ->Ghost()
                        ->Compact()
                        ->WithSize(UiSize::Small)
                        ->Tooltip(StrL("GitHub"))
                        ->OnClick(Listen(cx, &OnGithubClick))
                        ->IntoEl()
                        ->Cursor(CursorKind::Pointer))
            // Badge::count: how many notifications are up, capped at 99. The
            // bell itself has nothing to do in Rust either — the count is the
            // whole of it.
            ->Child(component::Badge::New(cx)
                        ->Count(WindowNotificationCount(cx))
                        ->Max(99)
                        ->Child(component::Button::New(
                                    cx, StrL("editor-title-bell"))
                                    ->Icon(IconName::Bell)
                                    ->Ghost()
                                    ->Compact()
                                    ->WithSize(UiSize::Small)
                                    ->Tooltip(StrL("Notifications"))
                                    ->IntoEl()
                                    ->Cursor(CursorKind::Pointer))
                        ->IntoEl());
    return component::TitleBar::New(cx)->Child(menus)->Child(tools)->IntoEl();
}

El* EditorApp::Render(EditorApp* self, Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    if (!self->seeded) {
        self->seeded = true;
        self->tree = EntityNewState<TreeState>(cx->app);
        if (TreeState* s = self->tree.Get(cx)) {
            s->rowH = kFileTreeRowH;
            // Ignorer::new("./"), for the duration of the walk — it loads
            // .gitignore and .autocorrectignore from the working directory.
            autocorrect::Ignorer ig;
            autocorrect::IgnorerInit(&ig, StrL("."));
            LoadDir(s, &ig, StrL("."), -1, 2);
            autocorrect::IgnorerFree(&ig);
            TreeRebuild(s);
            self->treeSub = Subscribe(cx, self->tree, &OnTreeEvent);
        }
    }
    // The document is linted when it changes; Rust's store does it on every
    // InputEvent and publishes what it found.
    if (self->lintedLen != InputValue(&self->editor).len) {
        Lint(self);
    }
    cx->win->input = self->dialogOpen ? &self->goToLine : &self->editor;
    // InputState defaults wrap on; the bar's switch is off. Copy every
    // frame so the highlighter's ScrollX / virtualization path matches
    // the labels, not only after a toggle.
    self->editor.mode.lineNumber = self->lineNumbers;
    self->editor.softWrap = self->softWrap;
    self->editor.showWhitespaces = self->showWhitespaces;
    self->editor.mode.folding = self->folding;
    self->editor.readonly = self->readOnly;
    self->editor.scrollBeyondLastLine = self->scrollBeyondLastLine;
    self->editor.cursorSurroundingLines = self->cursorSurroundingLines;

    EditorInitKeys();
    // build_menus() once: the OS menu bar is installed from it, the title bar
    // draws it, and the root answers for every action either of them
    // dispatches.
    MenuDef defs[kEditorMenus] = {};
    int nDefs = EditorBuildMenus(cx, defs, kEditorMenus);
    AppSetMenus(cx->app, defs, nDefs);

    WinSize win = WindowSize(cx->win);
    // The status bar under it, and the title bar over it where the window
    // owns one; what is left is the tree and the editor.
    float chrome =
        30 + (cx->win->opts.clientTitleBar ? component::kTitleBarHeight : 0);
    float bodyH = win.dipH - chrome;

    // The tree reports a selection rather than a click; a row that names a
    // file and is not the one already open is the one to read in.
    if (TreeState* s = self->tree.Get(cx)) {
        if (s->selected != self->lastSelected) {
            self->lastSelected = s->selected;
            const TreeItem* item = TreeEntryItem(s, s->selected);
            if (item && !item->folder && item->id.s) {
                OpenFile(self, item->id);
            }
        }
    }
    // p_1 on the tree wrapper, subtracted from the list height so the
    // virtualized rows still fill the pane.
    float treeH = bodyH > 8 ? bodyH - 8 : bodyH;
    El* tree = TreeList::New(cx, StrL("files"), self->tree, treeH, &FileTreeRow,
                             nullptr)
                   ->Bg(th.sidebar);
    El* left = Div(a)
                   ->FlexCol()
                   ->SizeFull()
                   ->ClipX()
                   ->ClipY()
                   ->Pad(4)
                   ->Bg(th.sidebar)
                   ->Child(tree);

    component::Highlighter* ed =
        component::Highlighter::New(cx, StrL("editor"), &self->editor);
    ed->H(bodyH)->ActiveLine();
    if (self->language[0]) {
        ed->Language(Str(self->language));
    }
    if (self->indentGuides) {
        ed->IndentGuides();
    }
    if (self->folding) {
        ed->Folding();
    }
    ed->Diagnostics(self->diagnostics, self->nDiagnostics);
    El* right = Div(a)->FlexCol()->SizeFull()->MinW(0)->Child(ed->IntoEl());

    // h_resizable("editor-container"): a 240px file pane the user can drag,
    // and a flex editor that takes the rest. The group's own state is keyed
    // off the id, so a drag survives the frame.
    El* body = component::Resizable::New(cx, StrL("editor-container"))
                   ->H(kFill)
                   ->Panel(left, 240)
                   ->Grow(right)
                   ->IntoEl();
    body = Div(a)->FlexCol()->W(kFill)->Flex1()->MinW(0)->MinH(0)->Child(body);

    Listener toggle = Listen(cx, &OnToggle);
    Listener cycle = Listen(cx, &OnCycleRows);
    component::StatusBar* bar = component::StatusBar::New(cx);
    bar->Left(ToggleButton(cx, StrL("line-number"), StrL("Line Number"),
                           self->lineNumbers, toggle, kToggleLineNumbers));
    bar->Left(ToggleButton(cx, StrL("soft-wrap"), StrL("Soft Wrap"),
                           self->softWrap, toggle, kToggleSoftWrap));
    bar->Left(ToggleButton(cx, StrL("show-whitespace"),
                           StrL("Show Whitespaces"), self->showWhitespaces,
                           toggle, kToggleWhitespaces));
    bar->Left(ToggleButton(cx, StrL("indent-guides"), StrL("Indent Guides"),
                           self->indentGuides, toggle, kToggleIndentGuides));
    bar->Left(ToggleButton(cx, StrL("folding"), StrL("Folding"), self->folding,
                           toggle, kToggleFolding));
    bar->Left(ToggleButton(cx, StrL("readonly"), StrL("Read only"),
                           self->readOnly, toggle, kToggleReadOnly));
    bar->Left(
        component::Button::New(cx, StrL("scroll-beyond-last-line"))
            ->Ghost()
            ->WithSize(UiSize::XSmall)
            ->Label(StrDup(a, fmt("Scroll Beyond: %s",
                                  RowsLabel(a, self->scrollBeyondLastLine))))
            ->OnClick(ListenerArg(cycle, kCycleScrollBeyond))
            ->IntoEl());
    bar->Left(
        component::Button::New(cx, StrL("cursor-surrounding-lines"))
            ->Ghost()
            ->WithSize(UiSize::XSmall)
            ->Label(StrDup(a, fmt("Cursor Surrounding: %s",
                                  RowsLabel(a, self->cursorSurroundingLines))))
            ->OnClick(ListenerArg(cycle, kCycleCursorSurrounding))
            ->IntoEl());
    // render_go_to_line_button: the point one-based the way an editor counts
    // it, and the byte the caret stands on beside it.
    RopePoint at = InputCursorPosition(&self->editor);
    bar->Right(
        component::Button::New(cx, StrL("line-column"))
            ->Ghost()
            ->WithSize(UiSize::XSmall)
            ->Label(StrDup(a, fmt("%d:%d (%d byte)", at.row + 1, at.column + 1,
                                  InputCursor(&self->editor))))
            ->Tooltip(StrL("Go to Line/Column"))
            ->OnClick(Listen(cx, &OpenGoTo))
            ->IntoEl());

    El* root = Div(a)->FlexCol()->SizeFull()->Bg(th.tokens.background);
    EditorBindMenuActions(root, cx);
    if (cx->win->opts.clientTitleBar) {
        root->Child(EditorTitleBar(self, cx, defs, nDefs));
    }
    root->Child(body)->Child(bar->IntoEl()->Shrink0());
    if (self->dialogOpen) {
        root->Child(component::Dialog::New(cx)
                        ->Open(true)
                        ->Title(StrL("Go to line"))
                        ->Body(component::Input::New(cx, StrL("go-to-line"),
                                                     &self->goToLine)
                                   ->IntoEl())
                        ->Confirm()
                        ->OnClose(Listen(cx, &CloseGoTo))
                        ->OnCancel(Listen(cx, &CloseGoTo))
                        ->OnOk(Listen(cx, &ConfirmGoTo))
                        ->IntoEl(win));
    }
    // ToggleFpsMonitor: the HUD places itself in the top right corner of
    // whatever it is put in, so what it is put in is a strip that starts
    // under the title bar -- `div().absolute().top(TITLE_BAR_HEIGHT).left_0()
    // .right_0()` in StoryRoot::render. Without it the HUD is laid over the
    // caption's own buttons. A window with no title bar of its own, like the
    // fps_monitor example, hands it the whole window and it sits at the top.
    if (self->fpsMonitor) {
        root->Child(Div(a)
                        ->Absolute()
                        ->Top(component::kTitleBarHeight)
                        ->Left(0)
                        ->Right(0)
                        ->Child(FpsMonitorEl(cx)));
    }
    // Bordered only where the window is client-decorated; a system frame
    // draws its own.
    return component::Root::New(cx)
        ->Bordered(cx->win->opts.clientTitleBar)
        ->Child(root)
        ->IntoEl();
}

int GpuiMain(int argc, char** argv) {
    (void)argc;
    (void)argv;
    App* app = AppNew();
    component::Init(app);
    AssetsClear();
    AssetsAddDefaultRoots(StrL("editor"));
    Entity<EditorApp> view = EntityNew<EditorApp>(app);
    EditorApp* self = view.Get(app);
    self->editor.kind = InputKind::Editor;
    self->editor.mode.kind = LayoutModeKind::CodeEditor;
    self->editor.mode.tabSize = 4;
    self->editor.mode.lineNumber = true;
    self->editor.mode.folding = true;
    self->editor.softWrap = false;
    InputSetPlaceholder(&self->editor, StrL("Enter your code here..."));
    // The completion provider, which is what makes the menu open as a word is
    // typed and on `.`.
    self->editor.completionProvider = &CompleteFrom;
    // The two halves of the completion surface beside it: when the menu
    // opens, and where an item's documentation comes from once it is looked
    // at.
    self->editor.completionTrigger = &CompletionTriggerAt;
    self->editor.completionResolve = &ResolveCompletion;
    // And the hover provider, which answers about the word the pointer rests
    // on out of the same items.
    self->editor.hoverProvider = &HoverAt;
    // And the code actions, which ctrl-. offers over a selection. The lint's
    // quickfixes register as a second provider, the way Rust's
    // code_action_providers holds the LspStore beside TextConvertor.
    self->editor.codeActionProvider = &CodeActionsFor;
    InputAddCodeActionProvider(&self->editor, &AutocorrectQuickfixes, self,
                               nullptr);
    // And the colours the document names, painted where they are named.
    self->editor.documentColorProvider = &DocumentColorsIn;
    // And where a symbol is defined: ctrl-hover underlines one it can reach,
    // ctrl-click follows it. `Duration` is defined in this document; the
    // other four std names open their page on doc.rust-lang.org.
    self->editor.definitionProvider = &DefinitionsAt;
    // And what a language server would say about the document beyond what the
    // scanner can see — here the five markers, each in its own colour, over
    // the highlighting rather than instead of it.
    // And the ghost text in front of the caret, which Tab accepts.
    self->editor.inlineCompletionProvider = &InlineCompletionAt;
    self->editor.semanticTokensProvider = &SemanticTokensFor;
    self->editor.semanticLegend = kMarkerLegend;
    self->editor.nSemanticLegend = kNMarkers;
    // default_value(include_str!("./fixtures/test.rs")): the document the
    // example opens with, vendored beside the completion items out of the
    // same upstream fixtures directory. It is the one that exercises the
    // colour provider -- its last line names four -- and it is what makes
    // `bun cmd/run.ts -compare editor` put the same text in both windows.
    TempStr fixture = AssetsLoadTextTemp(StrL("test.rs"));
    if (fixture.len > 0) {
        InputSetValue(&self->editor, Str(fixture.s, fixture.len));
    }
    StrCopyZ(self->language, (int)sizeof(self->language), "rs");
    Lint(self);
    self->editor.focused = true;
    // TitleBar::window_options(): the example owns its title bar, so the
    // window is opened without the system caption on the platforms whose
    // windows can be.
    WinOpts opts;
    opts.clientTitleBar = true;
    Window* win = WindowOpenView(app, StrL("Editor"), 1200, 750, view.id, opts);
    (void)win;
    int rc = AppRun(app);
    AppFree(app);
    return rc;
}
