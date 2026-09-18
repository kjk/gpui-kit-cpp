/* src/ignorer.rs — the Ignorer the editor's file tree walks with. The crate
   builds it from the `ignore` crate's gitignore half; this is that half,
   written out: gitignore pattern syntax (comments, `!` negation, trailing
   `/` for directories, leading `/` and inner `/` anchoring, `*`, `?`,
   `[...]` and `**`), later patterns overriding earlier ones, and
   matched_path_or_any_parents checking the path and then each parent
   directory.

   Part of the C++ port of the `autocorrect` crate 2.14.2 (see
   src/autocorrect/readme.md). Only the two files the crate loads are
   loaded: workDir/.autocorrectignore then workDir/.gitignore — nested
   .gitignore files are not what this API reads. */

#include "autocorrect/internal.h"

#include <stdio.h>
#include <string.h>

namespace autocorrect {

struct IgnorePattern {
    Str glob = {}; // heap-owned, normalized (no trailing '/', no leading '/')
    bool negated = false;
    bool dirOnly = false;
    bool anchored = false;
};

// ─── glob matching ────────────────────────────────────────────────────────

// Gitignore glob over a '/'-separated relative path. `*` and `?` do not
// cross '/', `**` does, `[...]` is a character class. Bytes, not codepoints:
// gitignore patterns in this tree are ASCII, and UTF-8 comparison is exact
// either way.
static bool GlobMatch(const char* p, const char* pe, const char* t,
                      const char* te) {
    while (p < pe) {
        char c = *p;
        if (c == '*') {
            bool doubleStar = p + 1 < pe && p[1] == '*';
            if (doubleStar) {
                const char* rest = p + 2;
                // `**/` may match zero components.
                if (rest < pe && *rest == '/') {
                    if (GlobMatch(rest + 1, pe, t, te)) {
                        return true;
                    }
                }
                for (const char* at = t; at <= te; at++) {
                    if (GlobMatch(rest, pe, at, te)) {
                        return true;
                    }
                }
                return false;
            }
            const char* rest = p + 1;
            for (const char* at = t; at <= te; at++) {
                if (GlobMatch(rest, pe, at, te)) {
                    return true;
                }
                if (at < te && *at == '/') {
                    break;
                }
            }
            return false;
        }
        if (t >= te) {
            return false;
        }
        if (c == '?') {
            if (*t == '/') {
                return false;
            }
            p++;
            t++;
            continue;
        }
        if (c == '[') {
            const char* cls = p + 1;
            bool negate = cls < pe && (*cls == '!' || *cls == '^');
            if (negate) {
                cls++;
            }
            bool hit = false;
            const char* k = cls;
            while (k < pe && *k != ']') {
                if (k + 2 < pe && k[1] == '-' && k[2] != ']') {
                    if (*t >= *k && *t <= k[2]) {
                        hit = true;
                    }
                    k += 3;
                } else {
                    if (*t == *k) {
                        hit = true;
                    }
                    k++;
                }
            }
            if (k >= pe) {
                // No closing ']': treat '[' literally.
                if (*t != '[') {
                    return false;
                }
                p++;
                t++;
                continue;
            }
            if (hit == negate || *t == '/') {
                return false;
            }
            p = k + 1;
            t++;
            continue;
        }
        if (c != *t) {
            return false;
        }
        p++;
        t++;
    }
    return t == te;
}

static bool PatternMatches(const IgnorePattern& pat, Str path, bool isDir) {
    if (pat.dirOnly && !isDir) {
        return false;
    }
    const char* pe = pat.glob.s + pat.glob.len;
    const char* te = path.s + path.len;
    if (pat.anchored) {
        return GlobMatch(pat.glob.s, pe, path.s, te);
    }
    // An unanchored pattern gets the implicit `**/`: match at the path
    // itself and at every component start.
    if (GlobMatch(pat.glob.s, pe, path.s, te)) {
        return true;
    }
    for (const char* at = path.s; at < te; at++) {
        if (*at == '/' && GlobMatch(pat.glob.s, pe, at + 1, te)) {
            return true;
        }
    }
    return false;
}

// Gitignore::matched: the last matching pattern decides. 0 none, 1 ignore,
// -1 whitelist.
static int Matched(const Ignorer* ig, Str path, bool isDir) {
    for (int i = ig->nPatterns - 1; i >= 0; i--) {
        const IgnorePattern& pat = ig->patterns[i];
        if (PatternMatches(pat, path, isDir)) {
            return pat.negated ? -1 : 1;
        }
    }
    return 0;
}

// matched_path_or_any_parents: the path itself, then each parent directory,
// nearest first.
static int MatchedOrParents(const Ignorer* ig, Str path, bool isDir) {
    int m = Matched(ig, path, isDir);
    if (m != 0) {
        return m;
    }
    int end = path.len;
    for (;;) {
        while (end > 0 && path.s[end - 1] != '/') {
            end--;
        }
        if (end == 0) {
            return 0;
        }
        end--; // drop the '/'
        m = Matched(ig, Str(path.s, end), true);
        if (m != 0) {
            return m;
        }
    }
}

// ─── loading ──────────────────────────────────────────────────────────────

static void AddPatternsFromFile(base::Vec<IgnorePattern>& out, Str workDir,
                                Str name) {
    int dirLen = workDir.len;
    while (dirLen > 0 &&
           (workDir.s[dirLen - 1] == '/' || workDir.s[dirLen - 1] == '\\')) {
        dirLen--;
    }
    base::TempStr path = base::fmt("%s/%s", Str(workDir.s, dirLen), name);
    if (path.len >= 1024) {
        return;
    }
    FILE* f = fopen(path.s, "rb");
    if (!f) {
        return;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    const long kMax = 1024 * 1024;
    if (size <= 0 || size > kMax) {
        fclose(f);
        return;
    }
    char* buf = (char*)base::Alloc(nullptr, (int)size);
    if (!buf) {
        fclose(f);
        return;
    }
    size_t got = fread(buf, 1, (size_t)size, f);
    fclose(f);
    Str text(buf, (int)got);
    int lineStart = 0;
    for (int i = 0; i <= text.len; i++) {
        if (i < text.len && text.s[i] != '\n') {
            continue;
        }
        int end = i;
        while (end > lineStart &&
               (text.s[end - 1] == '\r' || text.s[end - 1] == ' ' ||
                text.s[end - 1] == '\t')) {
            end--;
        }
        Str line(text.s + lineStart, end - lineStart);
        lineStart = i + 1;
        if (line.len == 0 || line.s[0] == '#') {
            continue;
        }
        IgnorePattern pat;
        if (line.s[0] == '!') {
            pat.negated = true;
            line = Str(line.s + 1, line.len - 1);
        }
        if (line.len > 0 && line.s[line.len - 1] == '/') {
            pat.dirOnly = true;
            line = Str(line.s, line.len - 1);
        }
        // A leading '/' or a '/' anywhere inside anchors the pattern to the
        // root; only a bare name floats to any depth.
        if (line.len > 0 && line.s[0] == '/') {
            pat.anchored = true;
            line = Str(line.s + 1, line.len - 1);
        }
        if (line.len == 0) {
            continue;
        }
        for (int k = 0; !pat.anchored && k < line.len; k++) {
            if (line.s[k] == '/') {
                pat.anchored = true;
            }
        }
        // "**/…" carries its own anchoring; keep it as written.
        pat.glob = base::StrDup(line);
        base::VecAppend(out, pat);
    }
    base::Free(nullptr, buf);
}

void IgnorerInit(Ignorer* ig, Str workDir) {
    ig->patterns = nullptr;
    ig->nPatterns = 0;
    base::Vec<IgnorePattern> patterns;
    // .autocorrectignore first, .gitignore second: later files win, like
    // the crate's GitignoreBuilder add order.
    AddPatternsFromFile(patterns, workDir, StrL(".autocorrectignore"));
    AddPatternsFromFile(patterns, workDir, StrL(".gitignore"));
    if (len(patterns) == 0) {
        return;
    }
    ig->patterns = (IgnorePattern*)base::Alloc(
        nullptr, len(patterns) * (int)sizeof(IgnorePattern));
    if (!ig->patterns) {
        for (int i = 0; i < len(patterns); i++) {
            base::StrFree(patterns[i].glob);
        }
        return;
    }
    memcpy(ig->patterns, patterns.els,
           (size_t)len(patterns) * sizeof(IgnorePattern));
    ig->nPatterns = len(patterns);
}

bool IgnorerIsIgnored(const Ignorer* ig, Str relativePath) {
    if (!ig || ig->nPatterns == 0 || relativePath.len == 0) {
        return false;
    }
    // Normalize: backslashes to slashes, a leading "./" dropped.
    base::TempStr buf = base::AllocStrTemp(std::min(relativePath.len, 1024));
    int n = 0;
    int start = 0;
    if (relativePath.len >= 2 && relativePath.s[0] == '.' &&
        (relativePath.s[1] == '/' || relativePath.s[1] == '\\')) {
        start = 2;
    }
    for (int i = start; i < relativePath.len && n < buf.len; i++) {
        char c = relativePath.s[i];
        buf.s[n++] = c == '\\' ? '/' : c;
    }
    Str path(buf.s, n);
    // is_ignored: matched with is_dir false, then true — either ignores.
    return MatchedOrParents(ig, path, false) == 1 ||
           MatchedOrParents(ig, path, true) == 1;
}

void IgnorerFree(Ignorer* ig) {
    if (!ig) {
        return;
    }
    for (int i = 0; i < ig->nPatterns; i++) {
        base::StrFree(ig->patterns[i].glob);
    }
    base::Free(nullptr, ig->patterns);
    ig->patterns = nullptr;
    ig->nPatterns = 0;
}

} // namespace autocorrect
