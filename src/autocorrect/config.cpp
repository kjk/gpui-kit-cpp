/* config/mod.rs + code/types.rs — the built-in default configuration.

   Part of the C++ port of the `autocorrect` crate 2.14.2 (see
   src/autocorrect/readme.md).

   The crate compiles `.autocorrectrc.default` in and merges a user's
   `.autocorrectrc` over it. Nothing in this tree loads a config file, so the
   defaults are written out here: the rule severities (`spellcheck` and
   `space-dollar` are off by default), the codeblock context (on), no
   textRules, and the fileTypes map match_filename dispatches with. */

#include "autocorrect/internal.h"

namespace autocorrect {

// rule/mod.rs default_rule_names order; the mask bit is the index.
static const char* const kRuleNames[kNRules] = {
    "space-word",         "space-punctuation",        "space-bracket",
    "space-dash",         "space-backticks",          "space-dollar",
    "fullwidth",          "halfwidth-word",           "halfwidth-punctuation",
    "no-space-fullwidth", "no-space-fullwidth-quote", "spellcheck",
};

SeverityMode RuleSeverity(int rule) {
    // .autocorrectrc.default `rules:`: everything 1 (error) except
    // space-dollar 0 and spellcheck 0.
    if (rule == kRuleSpaceDollar || rule == kRuleSpellcheck) {
        return SeverityMode::Off;
    }
    if (rule >= 0 && rule < kNRules) {
        return SeverityMode::Error;
    }
    return SeverityMode::Off;
}

int RuleIdByName(Str name) {
    for (int i = 0; i < kNRules; i++) {
        Str candidate(kRuleNames[i]);
        if (len(name) == len(candidate) && base::StrEqI(name, kRuleNames[i])) {
            return i;
        }
    }
    return -1;
}

// .autocorrectrc.default `fileTypes:` — extension (or bare filename) to the
// grammar that lints it.
struct FileType {
    const char* ext;
    const char* type;
};

static const FileType kFileTypes[] = {
    // HTML
    {"html", "html"},
    {"htm", "html"},
    {"vue", "html"},
    {"ejs", "html"},
    {"html.erb", "html"},
    {"svelte", "html"},
    // YAML
    {"yaml", "yaml"},
    {"yml", "yaml"},
    // Rust
    {"rust", "rust"},
    {"rs", "rust"},
    // SQL
    {"sql", "sql"},
    // Ruby
    {"ruby", "ruby"},
    {"rb", "ruby"},
    {"Gemfile", "ruby"},
    {"Rakefile", "ruby"},
    {"Profile", "ruby"},
    {"gemspec", "ruby"},
    // Crystal
    {"crystal", "ruby"},
    {"cr", "ruby"},
    // Elixir
    {"elixir", "elixir"},
    {"ex", "elixir"},
    {"exs", "elixir"},
    // JavaScript
    {"js", "javascript"},
    {"jsx", "javascript"},
    {"javascript", "javascript"},
    {"ts", "javascript"},
    {"tsx", "javascript"},
    {"typescript", "javascript"},
    {"js.erb", "javascript"},
    // CSS
    {"css", "css"},
    {"scss", "css"},
    {"sass", "css"},
    {"less", "css"},
    // JSON
    {"json", "json"},
    {"json5", "json"},
    // Go
    {"go", "go"},
    // Python
    {"python", "python"},
    {"py", "python"},
    // Objective-C
    {"objective_c", "objective_c"},
    {"objective-c", "objective_c"},
    {"m", "objective_c"},
    {"h", "objective_c"},
    // Strings for Cocoa
    {"strings", "strings"},
    // C#
    {"csharp", "csharp"},
    {"cs", "csharp"},
    // Java
    {"java", "java"},
    {"proto", "java"},
    // Scala
    {"scala", "scala"},
    // Swift
    {"swift", "swift"},
    // Kotlin
    {"kotlin", "kotlin"},
    {"kt", "kotlin"},
    {"gradle", "kotlin"},
    // PHP
    {"php", "php"},
    // Dart
    {"dart", "dart"},
    // Markdown
    {"markdown", "markdown"},
    {"md", "markdown"},
    {"mdx", "markdown"},
    // LaTeX
    {"latex", "latex"},
    {"tex", "latex"},
    // AsciiDoc
    {"asciidoc", "asciidoc"},
    {"adoc", "asciidoc"},
    {"asc", "asciidoc"},
    // Gettext
    {"po", "gettext"},
    {"pot", "gettext"},
    // Conf
    {"properties", "conf"},
    {"conf", "conf"},
    {"ini", "conf"},
    {"cfg", "conf"},
    {"toml", "conf"},
    // C / C++
    {"cc", "c"},
    {"cpp", "c"},
    {"c", "c"},
    // XML
    {"xml", "xml"},
    // Notebook
    {"jupyter", "jupyter"},
    {"ipynb", "jupyter"},
    // Shell
    {"sh", "ruby"},
    {"shell", "ruby"},
    // Text
    {"text", "text"},
    {"plain", "text"},
    {"txt", "text"},
};

static const int kNFileTypes =
    (int)(sizeof(kFileTypes) / sizeof(kFileTypes[0]));

// Config::get_file_type. Extensions are case-sensitive, like the crate's
// HashMap keys ("Gemfile" vs "gemfile").
static Str FileTypeFor(Str ext) {
    for (int i = 0; i < kNFileTypes; i++) {
        if (base::StrEq(ext, Str(kFileTypes[i].ext))) {
            return Str(kFileTypes[i].type);
        }
    }
    return {};
}

bool IsSupportType(Str filenameOrExt) {
    return len(FileTypeFor(filenameOrExt)) > 0;
}

Str GetFileExtension(Arena* a, Str filename) {
    Str name = base::StrTrimAscii(filename);
    if (IsSupportType(name)) {
        return base::StrDup(a, name);
    }
    // Last path segment, split on '/' like the crate.
    for (int i = len(name) - 1; i >= 0; i--) {
        if (name.s[i] == '/') {
            name = Str(name.s + i + 1, len(name) - i - 1);
            break;
        }
    }
    // parts = name.split('.'): ext is the last part; a supported double
    // extension ("html.erb") wins; a name with no dot is its own extension.
    int lastDot = -1;
    int secondLastDot = -1;
    int nDots = 0;
    for (int i = 0; i < len(name); i++) {
        if (name.s[i] == '.') {
            secondLastDot = lastDot;
            lastDot = i;
            nDots++;
        }
    }
    if (nDots == 0) {
        return base::StrDup(a, name);
    }
    Str ext(name.s + lastDot + 1, len(name) - lastDot - 1);
    if (nDots >= 2) {
        Str doubleExt(name.s + secondLastDot + 1,
                      len(name) - secondLastDot - 1);
        if (IsSupportType(doubleExt)) {
            ext = doubleExt;
        }
    }
    return base::StrDup(a, ext);
}

Str MatchFilename(Arena* a, Str filenameOrExt) {
    Str ext = GetFileExtension(a, filenameOrExt);
    Str type = FileTypeFor(ext);
    if (len(type) > 0) {
        return type;
    }
    return base::StrDup(a, filenameOrExt);
}

} // namespace autocorrect
