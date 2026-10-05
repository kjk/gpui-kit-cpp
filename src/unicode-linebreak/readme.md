# unicode-linebreak

C++ port of [unicode-linebreak](https://github.com/axelf4/unicode-linebreak)
0.1.5, the version used by the pinned gpui-kit soft-wrap implementation.
The version and crate archive SHA-256 are in `cmd/run.ts` (`unicodeLinebreak`).
Unicode data is 15.0.0; the crate's SA-to-AL tailoring is preserved.

`BreakProperty`, `LineBreaks` and `SplitAtSafe` port all three public Rust
functions. The iterator borrows a UTF-8 `Str`, returns byte offsets and
distinguishes allowed from mandatory breaks, including end of text.
Copies retain their iteration position. Malformed UTF-8, which Rust's `&str`
cannot contain, consumes each invalid byte as U+FFFD.

`src/base/input_editor.cpp` uses this port for measured soft wrapping, with
the same grapheme-boundary filtering and oversized-grapheme fallback as Rust.
Everything here depends only on `base.h`, in `namespace unicode_linebreak`.
The portable source is compiled into gpui on every target; the independent
`extras/unicode-linebreak` amalgam includes base and must not link beside gpui.

`tables.h` is a mechanical translation of the crate's compressed property
trie, 53-state pair table and safe-pair set. Regenerate using:

```
bun cmd/update-unicode-linebreak.ts <unpacked 0.1.5 crate> [LineBreakTest.txt]
```

Ordinary builds use the checked-in tables and make no network requests.
The optional test file is upstream's Unicode 15.0 conformance corpus;
`tests/UnicodeLinebreakTests.cpp` runs its 6,424 cases, with the same tailored
rule exclusions as upstream, and tests the safe-split API and iterator semantics.
The crate is Apache-2.0 licensed; see LICENSE. Unicode test data retains
its own copyright and terms in the generated fixture.
