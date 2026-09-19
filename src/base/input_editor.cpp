/* crates/base/src/input/editor data and projection facades. */

#include "base/input_editor.h"

namespace gpui {

static const SyntaxContext kDefaultNotIn[] = {SyntaxContext::String,
                                              SyntaxContext::Comment};
static const BracketPair kDefaultBrackets[] = {
    {StrL("("), StrL(")")},
    {StrL("["), StrL("]")},
    {StrL("{"), StrL("}")},
};
static const AutoClosingPair kDefaultClosingPairs[] = {
    {StrL("("), StrL(")"), kDefaultNotIn, 2},
    {StrL("["), StrL("]"), kDefaultNotIn, 2},
    {StrL("{"), StrL("}"), kDefaultNotIn, 2},
    {StrL("\""), StrL("\""), kDefaultNotIn, 2},
    {StrL("'"), StrL("'"), kDefaultNotIn, 2},
};

LanguageConfig LanguageConfig::Default() {
    LanguageConfig out;
    out.brackets = kDefaultBrackets;
    out.nBrackets = dimof(kDefaultBrackets);
    out.autoClosingPairs = kDefaultClosingPairs;
    out.nAutoClosingPairs = dimof(kDefaultClosingPairs);
    out.hasAutoClosingPairs = true;
    out.autoCloseBefore = StrL(";:.,=}])>");
    return out;
}

static bool IndentClassMatch(Str cls, uint32_t c) {
    for (int i = 0; i < len(cls);) {
        if (cls.s[i] == '\\' && i + 1 < len(cls)) {
            i++;
        }
        uint32_t got = (uint8_t)cls.s[i];
        if (got == c) {
            return true;
        }
        i++;
    }
    return false;
}

static bool IndentAtom(Str pat, int* pi, Str text, int* ti);

// Walk one atom in the pattern without looking at the text, so a following
// quantifier can be seen even when the atom matches zero times.
static bool IndentSkipAtom(Str pat, int* pi) {
    if (*pi >= len(pat)) {
        return false;
    }
    char c = pat.s[*pi];
    if (c == '^' || c == '$' || c == '*' || c == '+' || c == '?') {
        return false;
    }
    if (c == '.') {
        (*pi)++;
        return true;
    }
    if (c == '\\' && *pi + 1 < len(pat)) {
        *pi += 2;
        return true;
    }
    if (c == '[') {
        int end = *pi + 1;
        while (end < len(pat) && pat.s[end] != ']') {
            if (pat.s[end] == '\\' && end + 1 < len(pat)) {
                end += 2;
            } else {
                end++;
            }
        }
        if (end >= len(pat)) {
            return false;
        }
        *pi = end + 1;
        return true;
    }
    (*pi)++;
    return true;
}

static bool IndentHere(Str pat, int pi, Str text, int ti) {
    for (;;) {
        if (pi >= len(pat)) {
            return ti >= len(text);
        }
        if (pat.s[pi] == '$') {
            return ti >= len(text) && IndentHere(pat, pi + 1, text, ti);
        }
        if (pat.s[pi] == '^') {
            if (ti != 0) {
                return false;
            }
            pi++;
            continue;
        }
        int atomPi = pi;
        if (!IndentSkipAtom(pat, &pi)) {
            return false;
        }
        char quant = 0;
        if (pi < len(pat) &&
            (pat.s[pi] == '*' || pat.s[pi] == '+' || pat.s[pi] == '?')) {
            quant = pat.s[pi++];
        }
        int afterAtom = pi;
        if (!quant) {
            int tryPi = atomPi;
            if (!IndentAtom(pat, &tryPi, text, &ti)) {
                return false;
            }
            continue;
        }
        int minN = quant == '+' ? 1 : 0;
        int maxN = quant == '?' ? 1 : 1024;
        int n = 0;
        int t = ti;
        while (n < maxN) {
            int tryT = t;
            int tryP = atomPi;
            if (!IndentAtom(pat, &tryP, text, &tryT)) {
                break;
            }
            t = tryT;
            n++;
        }
        while (n >= minN) {
            if (IndentHere(pat, afterAtom, text, t)) {
                return true;
            }
            if (n == minN) {
                break;
            }
            n--;
            t = ti;
            for (int k = 0; k < n; k++) {
                int tryP = atomPi;
                if (!IndentAtom(pat, &tryP, text, &t)) {
                    return false;
                }
            }
        }
        return false;
    }
}

static bool IndentEscape(char e, uint32_t c) {
    switch (e) {
        case 's':
            return c == ' ' || c == '\t' || c == '\n' || c == '\r';
        case 'S':
            return !(c == ' ' || c == '\t' || c == '\n' || c == '\r');
        case 'd':
            return c >= '0' && c <= '9';
        case 'w':
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                   (c >= '0' && c <= '9') || c == '_';
        default:
            return (uint32_t)(uint8_t)e == c;
    }
}

static bool IndentAtom(Str pat, int* pi, Str text, int* ti) {
    if (*pi >= len(pat)) {
        return false;
    }
    char c = pat.s[*pi];
    if (c == '^' || c == '$' || c == '*' || c == '+' || c == '?') {
        return false;
    }
    if (c == '.') {
        (*pi)++;
        if (*ti >= len(text)) {
            return false;
        }
        (*ti)++;
        return true;
    }
    if (c == '\\' && *pi + 1 < len(pat)) {
        (*pi) += 2;
        if (*ti >= len(text)) {
            return false;
        }
        bool ok = IndentEscape(pat.s[*pi - 1], (uint8_t)text.s[*ti]);
        if (ok) {
            (*ti)++;
        }
        return ok;
    }
    if (c == '[') {
        int start = *pi + 1;
        int end = start;
        while (end < len(pat) && pat.s[end] != ']') {
            if (pat.s[end] == '\\' && end + 1 < len(pat)) {
                end += 2;
            } else {
                end++;
            }
        }
        if (end >= len(pat)) {
            return false;
        }
        *pi = end + 1;
        if (*ti >= len(text)) {
            return false;
        }
        bool ok = IndentClassMatch(Str(pat.s + start, end - start),
                                   (uint8_t)text.s[*ti]);
        if (ok) {
            (*ti)++;
        }
        return ok;
    }
    (*pi)++;
    if (*ti >= len(text) || text.s[*ti] != c) {
        return false;
    }
    (*ti)++;
    return true;
}

bool IndentPatternMatch(Str pattern, Str text) {
    if (!pattern.s) {
        return false;
    }
    // Rust Regex::is_match is unanchored unless the pattern starts with `^`.
    if (len(pattern) > 0 && pattern.s[0] == '^') {
        return IndentHere(pattern, 0, text, 0);
    }
    for (int i = 0; i <= len(text); i++) {
        if (IndentHere(pattern, 0, text, i)) {
            return true;
        }
    }
    return false;
}

IndentationRules IndentationRules::FromPatterns(Str increase, Str decrease) {
    IndentationRules out;
    out.increasePattern = increase;
    out.decreasePattern = decrease;
    return out;
}

struct RegisteredLanguageConfig {
    Str name = {};
    LanguageConfig config = {};
};

struct LanguageSettings {
    Arena* arena = nullptr;
    LanguageProvider provider = {};
    ArenaVec<RegisteredLanguageConfig> configs;

    LanguageSettings() { arena = ArenaNew(); }
    ~LanguageSettings() { ArenaDelete(arena); }
};

static Str LanguageCanonical(const LanguageSettings* settings, Arena* a,
                             Str name) {
    if (settings->provider.languageName) {
        return settings->provider
            .languageName(settings->provider.data, a, name);
    }
    char* copy = (char*)Alloc(a, len(name) + 1);
    if (!copy) {
        return {};
    }
    for (int i = 0; i < len(name); i++) {
        char c = name.s[i];
        copy[i] = c >= 'A' && c <= 'Z' ? (char)(c + ('a' - 'A')) : c;
    }
    copy[len(name)] = 0;
    return Str(copy, len(name));
}

static LanguageConfig LanguageConfigCopy(Arena* a,
                                         const LanguageConfig& source) {
    LanguageConfig out = source;
    out.autoCloseBefore = StrDup(a, source.autoCloseBefore);
    out.indentation
        .increasePattern = StrDup(a, source.indentation.increasePattern);
    out.indentation
        .decreasePattern = StrDup(a, source.indentation.decreasePattern);
    if (source.nBrackets > 0 && source.brackets) {
        BracketPair* pairs =
            (BracketPair*)Alloc(a, (int)sizeof(BracketPair) * source.nBrackets);
        out.brackets = pairs;
        for (int i = 0; i < source.nBrackets; i++) {
            pairs[i] = {StrDup(a, source.brackets[i].open),
                        StrDup(a, source.brackets[i].close)};
        }
    }
    if (source.nAutoClosingPairs > 0 && source.autoClosingPairs) {
        AutoClosingPair* pairs = (AutoClosingPair*)Alloc(
            a, (int)sizeof(AutoClosingPair) * source.nAutoClosingPairs);
        out.autoClosingPairs = pairs;
        for (int i = 0; i < source.nAutoClosingPairs; i++) {
            pairs[i] = source.autoClosingPairs[i];
            pairs[i].open = StrDup(a, source.autoClosingPairs[i].open);
            pairs[i].close = StrDup(a, source.autoClosingPairs[i].close);
            if (pairs[i].nNotIn > 0 && pairs[i].notIn) {
                auto* contexts = (SyntaxContext*)Alloc(
                    a, (int)sizeof(SyntaxContext) * pairs[i].nNotIn);
                memcpy(contexts, pairs[i].notIn,
                       (size_t)sizeof(SyntaxContext) * pairs[i].nNotIn);
                pairs[i].notIn = contexts;
            }
        }
    }
    return out;
}

void InputSetLanguageProvider(App* app, const LanguageProvider& provider) {
    if (app) {
        AppGlobalEnsure<LanguageSettings>(app)->provider = provider;
    }
}

void InputSetLanguageConfig(App* app, Str language,
                            const LanguageConfig& config) {
    if (!app || !language) {
        return;
    }
    LanguageSettings* settings = AppGlobalEnsure<LanguageSettings>(app);
    Str canonical = LanguageCanonical(settings, settings->arena, language);
    for (int i = settings->configs.len - 1; i >= 0; i--) {
        if (StrEq(settings->configs[i].name, canonical)) {
            settings->configs[i]
                .config = LanguageConfigCopy(settings->arena, config);
            return;
        }
    }
    settings->configs
        .Append(settings->arena, {StrDup(settings->arena, canonical),
                                  LanguageConfigCopy(settings->arena, config)});
}

LanguageConfig InputLanguageConfig(App* app, Str language) {
    if (!app) {
        return LanguageConfig::Default();
    }
    LanguageSettings* settings = AppGlobalEnsure<LanguageSettings>(app);
    Arena* temp = GetTempArena();
    Str canonical = LanguageCanonical(settings, temp, language);
    for (int i = settings->configs.len - 1; i >= 0; i--) {
        Str registered =
            LanguageCanonical(settings, temp, settings->configs[i].name);
        if (StrEq(registered, canonical)) {
            return settings->configs[i].config;
        }
    }
    LanguageConfig out;
    if (settings->provider.config &&
        settings->provider.config(settings->provider.data, canonical, &out)) {
        return out;
    }
    return LanguageConfig::Default();
}

SyntaxContextProvider InputSyntaxContextProvider(App* app, Str language) {
    SyntaxContextProvider out;
    if (!app) {
        return out;
    }
    LanguageSettings* settings = AppGlobalEnsure<LanguageSettings>(app);
    Str canonical = LanguageCanonical(settings, GetTempArena(), language);
    if (settings->provider.syntaxContextProvider) {
        settings->provider
            .syntaxContextProvider(settings->provider.data, canonical, &out);
    }
    return out;
}

Str TabSize::ToString(Arena* a) const {
    if (hardTabs) {
        return StrDup(a, StrL("\t"));
    }
    int n = std::max(0, tabSize);
    char* s = (char*)Alloc(a, n + 1);
    if (!s) {
        return {};
    }
    memset(s, ' ', (size_t)n);
    s[n] = 0;
    return Str(s, n);
}

int TabSize::IndentCount(Str line) const {
    int tab = std::max(1, tabSize);
    int count = 0;
    for (int i = 0; i < len(line);) {
        uint32_t c = 0;
        int n = Utf8At(line, i, &c);
        if (n <= 0) {
            break;
        }
        if (c == '\t') {
            count += tab;
        } else if (c == ' ') {
            count++;
        } else {
            break;
        }
        i += n;
    }
    return count;
}

TextDecoration TextDecoration::New(Selection range, const TextSpan& style) {
    TextDecoration result;
    result.range = range;
    result.style = style;
    result.style.lo = range.start;
    result.style.hi = range.end;
    return result;
}

struct DecorationCollectionEntry {
    uint64_t id = 0;
    Vec<TextDecoration> decorations;
};

struct DecorationCollectionsState {
    int refs = 1;
    bool ownerAlive = true;
    InputState* input = nullptr;
    uint64_t nextId = 1;
    Vec<DecorationCollectionEntry*> entries;
};

static void DecorationsRetain(DecorationCollectionsState* state) {
    if (state) {
        state->refs++;
    }
}

static void DecorationsRelease(DecorationCollectionsState* state) {
    if (!state || --state->refs > 0) {
        return;
    }
    for (int i = 0; i < state->entries.len; i++) {
        delete state->entries[i];
    }
    delete state;
}

static DecorationCollectionEntry* DecorationEntry(
    DecorationCollectionsState* state, uint64_t id) {
    if (!state || !state->ownerAlive) {
        return nullptr;
    }
    for (int i = 0; i < state->entries.len; i++) {
        DecorationCollectionEntry* entry = state->entries[i];
        if (entry && entry->id == id) {
            return entry;
        }
    }
    return nullptr;
}

static bool NormalizeDecoration(Str text, const TextDecoration& in,
                                TextDecoration* out) {
    int start = RopeClipOffset(text, in.range.start, Bias::Left);
    int end = RopeClipOffset(text, in.range.end, Bias::Right);
    if (end <= start) {
        return false;
    }
    *out = in;
    out->range = {start, end};
    out->style.lo = start;
    out->style.hi = end;
    return true;
}

static Str DecorationsText(const DecorationCollectionsState* state) {
    return state && state->input ? InputValue(state->input) : Str{};
}

TextDecorationCollection::TextDecorationCollection(
    const TextDecorationCollection& other)
    : state(other.state), id(other.id) {
    DecorationsRetain(state);
}

TextDecorationCollection& TextDecorationCollection::operator=(
    const TextDecorationCollection& other) {
    if (this == &other) {
        return *this;
    }
    DecorationsRetain(other.state);
    DecorationsRelease(state);
    state = other.state;
    id = other.id;
    return *this;
}

TextDecorationCollection::~TextDecorationCollection() {
    DecorationsRelease(state);
}

bool TextDecorationCollection::Set(const TextDecoration* decorations, int n) {
    DecorationCollectionEntry* entry = DecorationEntry(state, id);
    if (!entry) {
        return false;
    }
    VecClear(entry->decorations);
    return Append(decorations, n);
}

bool TextDecorationCollection::Append(const TextDecoration* decorations,
                                      int n) {
    DecorationCollectionEntry* entry = DecorationEntry(state, id);
    if (!entry) {
        return false;
    }
    Str text = DecorationsText(state);
    for (int i = 0; decorations && i < n; i++) {
        TextDecoration normalized;
        if (NormalizeDecoration(text, decorations[i], &normalized)) {
            VecAppend(entry->decorations, normalized);
        }
    }
    return true;
}

void TextDecorationCollection::Clear() {
    Set(nullptr, 0);
}

int TextDecorationCollection::GetRanges(Selection* out, int cap) const {
    DecorationCollectionEntry* entry = DecorationEntry(state, id);
    if (!entry) {
        return 0;
    }
    for (int i = 0; out && i < entry->decorations.len && i < cap; i++) {
        out[i] = entry->decorations[i].range;
    }
    return entry->decorations.len;
}

bool TextDecorationCollection::IsValid() const {
    return DecorationEntry(state, id) != nullptr;
}

DecorationCollections::DecorationCollections(InputState* input) {
    state = new DecorationCollectionsState();
    state->input = input;
}

DecorationCollections::~DecorationCollections() {
    if (state) {
        state->ownerAlive = false;
        state->input = nullptr;
    }
    DecorationsRelease(state);
}

TextDecorationCollection DecorationCollections::Create(
    const TextDecoration* decorations, int n) {
    TextDecorationCollection result;
    if (!state || !state->ownerAlive) {
        return result;
    }
    DecorationCollectionEntry* entry = new DecorationCollectionEntry();
    entry->id = state->nextId++;
    VecAppend(state->entries, entry);
    result.state = state;
    result.id = entry->id;
    DecorationsRetain(state);
    result.Append(decorations, n);
    return result;
}

static Selection AdjustDecorationRange(Selection range, Selection edit,
                                       int insertedLen) {
    int removedLen = std::max(0, edit.end - edit.start);
    int delta = insertedLen - removedLen;
    auto shift = [delta](int offset) { return std::max(0, offset + delta); };
    if (edit.start == edit.end) {
        int start = range.start < edit.start ? range.start : shift(range.start);
        int end = range.end <= edit.start ? range.end : shift(range.end);
        return {start, end};
    }
    int insertedEnd = edit.start + insertedLen;
    int start = range.start <= edit.start ? range.start
                : range.start >= edit.end ? shift(range.start)
                                          : edit.start;
    int end = range.end <= edit.start ? range.end
              : range.end >= edit.end ? shift(range.end)
                                      : insertedEnd;
    return {start, end};
}

void DecorationCollections::AdjustForEdit(Selection editedRange,
                                          int insertedLen) {
    if (!state || !state->ownerAlive) {
        return;
    }
    for (int i = 0; i < state->entries.len; i++) {
        Vec<TextDecoration>& ds = state->entries[i]->decorations;
        int write = 0;
        for (int j = 0; j < len(ds); j++) {
            TextDecoration d = ds[j];
            d.range = AdjustDecorationRange(d.range, editedRange, insertedLen);
            if (!d.range.IsEmpty()) {
                d.style.lo = d.range.start;
                d.style.hi = d.range.end;
                ds[write++] = d;
            }
        }
        ds.len = write;
    }
}

void DecorationCollections::Clear() {
    if (!state || !state->ownerAlive) {
        return;
    }
    for (int i = 0; i < state->entries.len; i++) {
        VecClear(state->entries[i]->decorations);
    }
}

static bool SelectionOverlaps(Selection a, Selection b) {
    return a.start < b.end && b.start < a.end;
}

int DecorationCollections::BuildSpans(TextSpan* out, int cap) const {
    if (!state || !state->ownerAlive) {
        return 0;
    }
    Vec<TextSpan> accepted;
    for (int i = 0; i < state->entries.len; i++) {
        const Vec<TextDecoration>& ds = state->entries[i]->decorations;
        for (int j = 0; j < len(ds); j++) {
            Vec<Selection> pieces;
            VecAppend(pieces, ds[j].range);
            for (int k = 0; k < len(accepted) && len(pieces) > 0; k++) {
                Selection occupied = {accepted[k].lo, accepted[k].hi};
                for (int p = len(pieces) - 1; p >= 0; p--) {
                    Selection piece = pieces[p];
                    if (!SelectionOverlaps(piece, occupied)) {
                        continue;
                    }
                    pieces[p] = pieces[len(pieces) - 1];
                    pieces.len--;
                    if (piece.start < occupied.start) {
                        VecAppend(
                            pieces,
                            {piece.start, std::min(piece.end, occupied.start)});
                    }
                    if (piece.end > occupied.end) {
                        VecAppend(pieces, {std::max(piece.start, occupied.end),
                                           piece.end});
                    }
                }
            }
            for (int p = 0; p < len(pieces); p++) {
                TextSpan span = ds[j].style;
                span.lo = pieces[p].start;
                span.hi = pieces[p].end;
                VecAppend(accepted, span);
            }
        }
    }
    if (len(accepted) > 1) {
        std::sort(accepted.els, accepted.els + len(accepted),
                  [](const TextSpan& a, const TextSpan& b) {
                      return a.lo < b.lo || (a.lo == b.lo && a.hi < b.hi);
                  });
    }
    for (int i = 0; out && i < len(accepted) && i < cap; i++) {
        out[i] = accepted[i];
    }
    return len(accepted);
}

static DiagnosticRelatedInformation* CloneRelated(
    Arena* a, const DiagnosticRelatedInformation* src, int n) {
    if (!src || n <= 0) {
        return nullptr;
    }
    auto* dst = (DiagnosticRelatedInformation*)Alloc(
        a, (int)(sizeof(DiagnosticRelatedInformation) * (size_t)n));
    for (int i = 0; i < n; i++) {
        dst[i] = src[i];
        dst[i].uri = StrDup(a, src[i].uri);
        dst[i].message = StrDup(a, src[i].message);
    }
    return dst;
}

static Diagnostic CloneDiagnostic(Arena* a, const Diagnostic& src) {
    Diagnostic result = src;
    result.message = StrDup(a, src.message);
    result.source = StrDup(a, src.source);
    result.code = StrDup(a, src.code);
    result.codeDescriptionUri = StrDup(a, src.codeDescriptionUri);
    result.data = StrDup(a, src.data);
    result.relatedInformation =
        CloneRelated(a, src.relatedInformation, src.nRelatedInformation);
    if (src.tags && src.nTags > 0) {
        auto* tags = (DiagnosticTag*)Alloc(
            a, (int)(sizeof(DiagnosticTag) * (size_t)src.nTags));
        memcpy(tags, src.tags, sizeof(DiagnosticTag) * (size_t)src.nTags);
        result.tags = tags;
    }
    return result;
}

DiagnosticSet::DiagnosticSet(Str value) {
    Reset(value);
}

DiagnosticSet::~DiagnosticSet() {
    ArenaDelete(arena);
}

void DiagnosticSet::Reset(Str value) {
    VecClear(diagnostics);
    ArenaDelete(arena);
    arena = ArenaNew();
    text = StrDup(arena, value);
}

void DiagnosticSet::Push(const Diagnostic& diagnostic) {
    DiagnosticEntry entry;
    entry.range
        .start = RopeClipOffset(text, diagnostic.range.start, Bias::Left);
    entry.range.end = RopeClipOffset(text, diagnostic.range.end, Bias::Right);
    if (entry.range.end < entry.range.start) {
        entry.range.end = entry.range.start;
    }
    entry.diagnostic = CloneDiagnostic(arena, diagnostic);
    entry.diagnostic.range = entry.range;
    int at = 0;
    while (at < diagnostics.len && diagnostics[at].range.start <= entry.range
                                                                      .start) {
        at++;
    }
    VecInsertAt(diagnostics, at, entry);
}

void DiagnosticSet::Extend(const Diagnostic* values, int n) {
    for (int i = 0; values && i < n; i++) {
        Push(values[i]);
    }
}

void DiagnosticSet::Clear() {
    // Keep the arena because callers may still hold entries returned by the
    // last range query until their operation ends. Reset releases it when the
    // document itself changes.
    VecClear(diagnostics);
}

DiagnosticSummary DiagnosticSet::Summary() const {
    DiagnosticSummary result;
    result.count = diagnostics.len;
    if (diagnostics.len > 0) {
        result.start = diagnostics[0].range.start;
        result.end = diagnostics[diagnostics.len - 1].range.end;
    }
    return result;
}

int DiagnosticSet::Range(Selection range, const DiagnosticEntry** out,
                         int cap) const {
    int total = 0;
    for (int i = 0; i < diagnostics.len; i++) {
        const DiagnosticEntry& entry = diagnostics[i];
        if (entry.range.start >= range.end) {
            break;
        }
        if (entry.range.end > range.start) {
            if (out && total < cap) {
                out[total] = &entry;
            }
            total++;
        }
    }
    return total;
}

const DiagnosticEntry* DiagnosticSet::ForOffset(int offset) const {
    const DiagnosticEntry* result = nullptr;
    return Range({offset, offset + 1}, &result, 1) > 0 ? result : nullptr;
}

const DiagnosticEntry* DiagnosticSet::At(int index) const {
    return index >= 0 && index < diagnostics.len ? &diagnostics[index]
                                                 : nullptr;
}

DisplayMap::DisplayMap(int columns) : wrapColumns(std::max(0, columns)) {
    SetText({});
}

DisplayMap::~DisplayMap() {
    StrFree(text);
}

void DisplayMap::SetText(Str value) {
    Str copy = StrDup(value);
    StrFree(text);
    text = copy;
    Rebuild();
}

void DisplayMap::OnTextChanged(Str value) {
    SetText(value);
}

void DisplayMap::SetWrapColumns(int columns) {
    columns = std::max(0, columns);
    if (wrapColumns != columns) {
        wrapColumns = columns;
        Rebuild();
    }
}

void DisplayMap::SetWrappingIndent(WrappingIndent indent) {
    if (wrappingIndent != indent) {
        wrappingIndent = indent;
        Rebuild();
    }
}

void DisplayMap::SetTabSize(TabSize value) {
    value.tabSize = std::max(1, value.tabSize);
    if (tab.tabSize != value.tabSize || tab.hardTabs != value.hardTabs) {
        tab = value;
        Rebuild();
    }
}

BufferPoint DisplayMap::ClipBufferPoint(BufferPoint point) const {
    int lines = BufferLineCount();
    point.line = std::max(0, std::min(point.line, lines - 1));
    Str line = RopeSliceLine(text, point.line);
    point.col = RopeClipOffset(line, point.col, Bias::Left);
    return point;
}

DisplayPoint DisplayMap::BufferPosToDisplayPos(BufferPoint point) const {
    point = ClipBufferPoint(point);
    int firstForLine = -1;
    for (int i = 0; i < rows.len; i++) {
        const DisplayMapRow& row = rows[i];
        if (row.bufferLine != point.line) {
            continue;
        }
        if (firstForLine < 0) {
            firstForLine = i;
        }
        if (point.col < row.endCol ||
            (point.col == row.endCol &&
             (i + 1 == rows.len || rows[i + 1].bufferLine != point.line))) {
            return {i, point.col - row.startCol};
        }
    }
    if (firstForLine >= 0) {
        return {firstForLine, 0};
    }
    int nearestLine = FoldMapNearestVisibleLine(&foldMap, point.line);
    for (int i = 0; i < rows.len; i++) {
        if (rows[i].bufferLine == nearestLine) {
            return {i, 0};
        }
    }
    return {};
}

BufferPoint DisplayMap::DisplayPosToBufferPos(DisplayPoint point) const {
    if (rows.len == 0) {
        return {};
    }
    int rowIx = std::max(0, std::min(point.row, rows.len - 1));
    const DisplayMapRow& row = rows[rowIx];
    int width = std::max(0, row.endCol - row.startCol);
    return {row.bufferLine,
            row.startCol + std::max(0, std::min(point.col, width))};
}

static int DisplayColumnAt(Str text, int end, int tabSize) {
    int column = 0;
    int tab = std::max(1, tabSize);
    for (int at = 0; at < len(text) && at < end;) {
        uint32_t rune = 0;
        int n = Utf8At(text, at, &rune);
        if (n <= 0) {
            break;
        }
        if (rune == '\t') {
            column += tab - column % tab;
        } else {
            column++;
        }
        at += n;
    }
    return column;
}

static int DisplayAdvanceColumns(Str text, int start, int columns,
                                 int tabSize) {
    int at = start;
    int used = 0;
    int tab = std::max(1, tabSize);
    while (at < len(text)) {
        uint32_t rune = 0;
        int n = Utf8At(text, at, &rune);
        if (n <= 0) {
            break;
        }
        int width = rune == '\t' ? tab - used % tab : 1;
        if (used > 0 && used + width > columns) {
            break;
        }
        used += width;
        at += n;
        if (used >= columns) {
            break;
        }
    }
    // A wrap narrower than one glyph still has to make progress.
    if (at == start && at < len(text)) {
        uint32_t rune = 0;
        int n = Utf8At(text, at, &rune);
        at += std::max(1, n);
    }
    return std::min(at, len(text));
}

static int DisplayWrappedLineCount(Str value, int wrapColumns,
                                   WrappingIndent indent, int tabSize) {
    if (wrapColumns <= 0 || len(value) == 0) {
        return 1;
    }
    int leadingEnd = 0;
    while (leadingEnd < len(value) &&
           (value.s[leadingEnd] == ' ' || value.s[leadingEnd] == '\t')) {
        leadingEnd++;
    }
    int leading = indent == WrappingIndent::Same
                      ? DisplayColumnAt(value, leadingEnd, tabSize)
                      : 0;
    int continuation = std::max(1, wrapColumns - leading);
    int count = 0;
    int start = 0;
    while (start < len(value)) {
        int columns = count == 0 ? wrapColumns : continuation;
        start = DisplayAdvanceColumns(value, start, columns, tabSize);
        count++;
    }
    return std::max(1, count);
}

int DisplayMap::WrapRowCount() const {
    int count = 0;
    int lines = BufferLineCount();
    for (int line = 0; line < lines; line++) {
        count += DisplayWrappedLineCount(RopeSliceLine(text, line), wrapColumns,
                                         wrappingIndent, tab.tabSize);
    }
    return count;
}

int DisplayMap::BufferLineCount() const {
    return RopeLinesLen(text);
}

int DisplayMap::DisplayRowToBufferLine(int row) const {
    return row >= 0 && row < rows.len ? rows[row].bufferLine : 0;
}

Selection DisplayMap::BufferLineToDisplayRowRange(int line) const {
    int start = -1;
    int end = -1;
    for (int i = 0; i < rows.len; i++) {
        if (rows[i].bufferLine == line) {
            if (start < 0) {
                start = i;
            }
            end = i + 1;
        }
    }
    return start < 0 ? Selection{-1, -1} : Selection{start, end};
}

bool DisplayMap::IsBufferLineHidden(int line) const {
    return BufferLineToDisplayRowRange(line).start < 0;
}

int DisplayMap::BufferLineToDisplayRow(int line) const {
    Selection range = BufferLineToDisplayRowRange(line);
    if (range.start >= 0) {
        return range.start;
    }
    return BufferPosToDisplayPos({line, 0}).row;
}

void DisplayMap::SetFoldCandidates(const FoldRange* ranges, int n) {
    FoldMapSetCandidates(&foldMap, ranges, n);
    Rebuild();
}

void DisplayMap::SetFolded(int startLine, bool folded) {
    FoldMapSetFolded(&foldMap, startLine, folded);
    Rebuild();
}

void DisplayMap::ToggleFold(int startLine) {
    FoldMapToggle(&foldMap, startLine);
    Rebuild();
}

bool DisplayMap::IsFoldedAt(int startLine) const {
    return FoldMapIsFolded(&foldMap, startLine);
}

bool DisplayMap::IsFoldCandidate(int startLine) const {
    return FoldMapIsCandidate(&foldMap, startLine);
}

void DisplayMap::ClearFolds() {
    FoldMapClearFolds(&foldMap);
    Rebuild();
}

void DisplayMap::AdjustFoldsForEdit(Str oldText, Selection editedRange,
                                    Str inserted) {
    RopePoint start = RopeOffsetToPoint(oldText, editedRange.start);
    RopePoint end = RopeOffsetToPoint(oldText, editedRange.end);
    int newLines = 0;
    for (int i = 0; i < len(inserted); i++) {
        newLines += inserted.s[i] == '\n';
    }
    FoldMapAdjustForEdit(&foldMap, start.row, end.row,
                         newLines - (end.row - start.row));
}

void DisplayMap::Rebuild() {
    VecClear(rows);
    int lineCount = BufferLineCount();
    FoldMapRebuild(&foldMap, lineCount);
    for (int line = 0; line < lineCount; line++) {
        if (FoldMapLineHidden(&foldMap, line)) {
            continue;
        }
        Str value = RopeSliceLine(text, line);
        if (wrapColumns <= 0 || len(value) == 0) {
            VecAppend(rows, {line, 0, len(value)});
            continue;
        }
        int leadingEnd = 0;
        while (leadingEnd < len(value) &&
               (value.s[leadingEnd] == ' ' || value.s[leadingEnd] == '\t')) {
            leadingEnd++;
        }
        int leading = wrappingIndent == WrappingIndent::Same
                          ? DisplayColumnAt(value, leadingEnd, tab.tabSize)
                          : 0;
        int continuation = std::max(1, wrapColumns - leading);
        int start = 0;
        int row = 0;
        while (start < len(value)) {
            int columns = row == 0 ? wrapColumns : continuation;
            int end = DisplayAdvanceColumns(value, start, columns, tab.tabSize);
            VecAppend(rows, {line, start, end});
            start = end;
            row++;
        }
    }
}

bool HighlightStyleResolver::Style(Str name, TextSpan* out) const {
    return style && style(data, name, out);
}

Str InputHighlighter::Language() const {
    return language ? language(data) : Str{};
}

void InputHighlighter::Update(const InputEdit* edit, Str text,
                              bool folding) const {
    if (update) {
        update(data, edit, text, folding);
    }
}

int InputHighlighter::Styles(Selection range,
                             const HighlightStyleResolver* resolver, Arena* a,
                             TextSpan** out) const {
    return styles ? styles(data, range, resolver, a, out) : 0;
}

int InputHighlighter::FoldRanges(Str text, Selection changedRange, Arena* a,
                                 FoldRange** out) const {
    return foldRanges ? foldRanges(data, text, changedRange, a, out) : 0;
}

bool InputHighlighterFactory::Create(Str language,
                                     InputHighlighter* out) const {
    return create && create(data, language, out);
}

El* FoldIconRenderer::Render(Ctx* cx, int line, bool folded) const {
    return render ? render(data, cx, line, folded) : nullptr;
}

} // namespace gpui
