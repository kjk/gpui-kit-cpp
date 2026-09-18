# src/autocorrect — the `autocorrect` crate, ported to C++

This is a port of [autocorrect](https://github.com/huacnlee/autocorrect)
**2.14.2**, the CJK copywriting linter/formatter the gpui-kit editor
example lints every open document with (`autocorrect = "2.14.2"` in
`crates/story/Cargo.toml`). `examples/editor.cpp` calls `LintFor` on the
document and maps each `LineResult` to a diagnostic and a quickfix, exactly
as `lint_document` in `crates/story/examples/editor.rs` does, and walks its
file tree through `Ignorer`, which is why `.cache` is in the tree and
`.work/` is not.

The pin lives in [`cmd/run.ts`](../../cmd/run.ts) (`autocorrect`) alongside
the gpui-kit, taffy, markdown, html5ever and wry pins, and moves when
they do —
see [`/update-port`](../../.claude/skills/update-port/crates.md).

Everything is in `namespace autocorrect`, not `gpui`, because it is a port
of a crate of its own. The dependency goes one way and stops early: this
directory includes `base.h` and its own headers, and nothing else in the
tree; `cmd/update-dist.ts` fails the build if that stops being true.

Unlike the other three crate ports, this one is **not part of the gpui
amalgam**: only the editor example (and the tests) use it, so
`cmd/update-dist.ts` amalgamates this directory into its own standalone
`extras/autocorrect/autocorrect.h` + `autocorrect.cpp` pair beside
`quickjs/`, and `cmd/build.ts` compiles and links that pair only into the
targets that ask for it (`autocorrectTargets`). The include spells the same
either way — `#include "autocorrect/autocorrect.h"` — because the standard
build adds `-I <amalgam>/extras` and the non-amalgam build
(`cmd/build-no-amalgam.ts`, which compiles the editor against the raw
sources here) adds `-I src`. The generated pair header
inlines `base.h` — the only thing the port depends on — behind a
`GPUI_BASE_H_` guard shared with `gpui.h`'s own inlined copy, so a
translation unit can include `gpui.h` and
`extras/autocorrect/autocorrect.h` in either order, or the pair alone; the
base *implementation* still comes from `gpui.cpp` at link time. The header
also carries `internal.h` behind `#if GPUI_INCLUDE_PRIVATE_API` (default
0), the same gate the amalgam puts the other crates' private headers
behind — the pair's own `.cpp` and `tests/AutocorrectTests.cpp` define it
to 1 to reach the internals.

```cpp
Arena* a = ArenaNew();
Str out = autocorrect::Format(a, StrL("Hello世界."));   // "Hello 世界。"
autocorrect::LintResult r =
    autocorrect::LintFor(a, source, StrL("markdown"));
```

Everything a call returns is allocated out of the caller's arena.

## Where the Rust went

| Rust                                | C++                              |
| ----------------------------------- | -------------------------------- |
| `src/lib.rs`, `src/result/mod.rs`   | `autocorrect.h`                  |
| `src/format.rs`, `src/rule/mod.rs`, `src/rule/rule.rs` | `rule.cpp` |
| `src/rule/word.rs`, `src/rule/strategery.rs` | `word.cpp`              |
| `src/rule/fullwidth.rs`             | `fullwidth.cpp`                  |
| `src/rule/halfwidth.rs`             | `halfwidth.cpp`                  |
| the regexes' `\p{Han}` etc.         | `unicode.cpp`                    |
| `src/config/mod.rs`, `.autocorrectrc.default`, `src/code/types.rs` | `config.cpp` |
| `src/config/toggle.rs` + `toggle.pest` | `toggle.cpp`                  |
| `src/code/code.rs`, `src/code/mod.rs` | `code.cpp`                     |
| `grammar/markdown.pest`             | `markdown.cpp`                   |
| `grammar/html.pest`                 | `html.cpp`                       |
| the other language grammars         | `source.cpp`                     |
| `src/ignorer.rs`                    | `ignorer.cpp`                    |
| shared internals                    | `internal.h`                     |

The crate's own tests are ported in `tests/AutocorrectTests.cpp`, and its
whole-document Markdown fixture in
`tests/AutocorrectMarkdownFormatTests.cpp`; the second file's two strings
are extracted verbatim from `src/code/markdown.rs` in the pinned crate (a
small script did the extraction; re-extract the two `indoc!` blocks of
`test_format_markdown` if the pin moves).

## No regex crate, no pest

Hard rule 3: the crate's two engines are not vendored.

- Every rule regex in `rule/` is a hand-written scanner in `word.cpp`,
  `fullwidth.cpp` and `halfwidth.cpp`, one function per pattern side, with
  `Strategery::replace_all`'s resume-after-match behaviour kept. The
  transcription keeps the crate's own regex quirks, because they are the
  behaviour: `\p{CJK}` expands to an **ungrouped** alternation, so in
  `\p{CJK}[^%\$\\]` the suffix binds to the Bopomofo alternative only, and
  inside a character class the expansion leaves a literal `|` in the class.
- Every `.pest` grammar is a scanner (or, for Markdown, a small
  recursive-descent parser building a pair tree) that finds the same regions
  the grammar's named pairs cover: comments and strings in source files,
  text/link/mark runs in Markdown, text nodes in HTML. PEG semantics are
  kept where they show — ordered choice does not reopen a committed
  `open_mark`, an unterminated block comment matches nothing, and the pest
  grammars do not honour `\"` escapes inside strings, so neither does this.

Known approximations, deliberate and small:

- The regex crate's `\w` is approximated (`unicode.cpp`): ASCII
  alphanumerics and `_`, Latin-1/Extended, Greek, Cyrillic, fullwidth
  alphanumerics and the five CJK scripts. `\d` is ASCII digits.
- `str::trim` in lint results trims ASCII whitespace, not the full Unicode
  set.
- The JSON and YAML grammars are structural parsers in the crate; here they
  are line/token scans that format the same regions (values and comments,
  not keys) but never produce a parse error, so a malformed document that
  Rust would return unchanged with an error may still be corrected here.
- The JavaScript grammar's HTML mode is matched flat per node rather than
  through pest's full backtracking.

## What is deliberately not ported

- **Spellcheck** (`rule/spellcheck.rs` and its word list). The crate's
  default config registers it **off**; only a user `.autocorrectrc` turns it
  on, and no config loading exists here. So `lint_for("…ios…", "html")`
  answers the spacing-only correction where the crate's *test* config would
  also produce `iOS`. If a compare against Rust ever disagrees on a real
  file because of it, this is the first thing to add.
- **`.autocorrectrc` loading** — the compiled-in default config
  (`config.cpp`) is the behaviour: all rules error except `space-dollar`
  and `spellcheck` (off), the Markdown `codeblock` context on, no
  `textRules`.
- **Six grammars**: latex, asciidoc, gettext, strings, xml, jupyter. Their
  files currently answer the empty result an unknown type gets, where Rust
  would lint them. Add them to `source.cpp` (or a file of their own) when a
  file of that type actually gets opened.
- The CLI, colored diff (`diff.rs`, `owo-colors`), `json`/`rdjson`
  serializers, and `autocorrect-derive` (its two generated functions are
  written out by hand in `code.cpp`'s dispatch).

## The editor seam

`examples/editor.cpp` maps a `LineResult`'s 1-based `(line, col)` — columns
in chars, the way pest counts them — to byte offsets, takes the end as
`col + item.old` counted in chars (Rust's `item.old.chars().count()`),
prefixes messages with `AutoCorrect:` and maps severities Error → Warning,
Warning → Hint, Pass → Info, exactly as the Rust example does. The language
passed in is the highlighter's canonical name (`SyntaxLangName`), never the
raw extension, with `"text"` for an extension the highlighter does not know
— Rust's `Lang::from_str` falls back to `Language::Plain` the same way.
