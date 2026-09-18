/* src/lib.rs — the public API of the `autocorrect` crate.

   `src/autocorrect/` is a C++ port of autocorrect 2.14.2, the CJK
   copywriting linter/formatter the gpui-kit editor example lints every
   open document with (`autocorrect = "2.14.2"` in `crates/story/Cargo.toml`).
   It adds spaces between CJK (Han / Hangul / Katakana / Hiragana / Bopomofo)
   and halfwidth text, corrects fullwidth/halfwidth punctuation near CJK, and
   for source files runs only over the comments and string literals a
   grammar pulls out. See src/autocorrect/readme.md for what is ported and
   what is not, and cmd/run.ts (`autocorrect`) for the pinned version.

       Arena* a = ArenaNew();
       Str out = autocorrect::Format(a, StrL("Hello世界."));
       // => "Hello 世界。"
       autocorrect::LintResult r =
           autocorrect::LintFor(a, source, StrL("markdown"));

   Everything a call returns is allocated out of the caller's arena; nothing
   here owns memory or needs freeing. */

#ifndef GPUI_AUTOCORRECT_AUTOCORRECT_H_
#define GPUI_AUTOCORRECT_AUTOCORRECT_H_

#include "base.h"

namespace autocorrect {

using base::Arena;
using base::Str;

// result/mod.rs Severity. The values are the crate's serialized ones.
enum class Severity : uint8_t {
    Pass = 0,
    Error = 1,
    Warning = 2
};

// result/mod.rs LineResult: one line the lint would change.
struct LineResult {
    int line = 1; // 1-based, like the crate's
    int col = 1;  // 1-based; counted in chars, the way pest counts columns
    Str neu = {}; // the corrected text (Rust field is `new`)
    Str old = {}; // the original slice, whitespace-trimmed
    Severity severity = Severity::Error;
};

// result/mod.rs LintResult, flattened to POD: the lines live in the arena.
struct LintResult {
    Str filepath = {};
    LineResult* lines = nullptr;
    int nLines = 0;
    Str error = {};

    bool HasError() const { return len(error) > 0; }
};

// result/mod.rs FormatResult. On error `out` is the raw input, like the
// crate's `FormatResult::error`.
struct FormatResult {
    Str out = {};
    Str error = {};

    bool HasError() const { return len(error) > 0; }
};

// format.rs format(): correct plain text, no filetype dispatch.
Str Format(Arena* a, Str text);

// code/code.rs lint_for() / format_for(): dispatch on the filename or
// extension ("markdown", "rust", "md", "index.html", …) and run only over
// the regions that language's grammar corrects.
LintResult LintFor(Arena* a, Str raw, Str filenameOrExt);
FormatResult FormatFor(Arena* a, Str raw, Str filenameOrExt);

// code/types.rs. MatchFilename answers the grammar name the default config
// maps the file to ("app.md" → "markdown"); unknown types answer the input
// unchanged, which is what makes LintFor return an empty result for them.
Str MatchFilename(Arena* a, Str filenameOrExt);
Str GetFileExtension(Arena* a, Str filename);
bool IsSupportType(Str filenameOrExt);

// ignorer.rs Ignorer: the gitignore matcher the editor's file tree walks
// with. Loads `.autocorrectignore` and `.gitignore` from workDir (in that
// order — later files win, like the `ignore` crate's builder).
struct IgnorePattern;
struct Ignorer {
    IgnorePattern* patterns = nullptr;
    int nPatterns = 0;
};

void IgnorerInit(Ignorer* ig, Str workDir);
// `path` is relative to workDir, with either slash. Like the crate's
// `is_ignored`, a path is ignored when it or any parent directory matches,
// whether matched as a file or as a directory.
bool IgnorerIsIgnored(const Ignorer* ig, Str relativePath);
void IgnorerFree(Ignorer* ig);

} // namespace autocorrect
#endif // GPUI_AUTOCORRECT_AUTOCORRECT_H_
