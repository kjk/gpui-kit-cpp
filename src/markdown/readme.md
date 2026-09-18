# src/markdown — the `markdown` crate, ported to C++

This is a port of [markdown-rs](https://github.com/wooorm/markdown-rs)
**1.0.0**, the CommonMark + GFM parser `gpui-kit` parses every
`TextView` with (`markdown = { version = "1.0.0", features = ["serde"] }` in
`crates/ui/Cargo.toml`). `component::TextView` calls it and folds the mdast it
returns into its `MdNode` tree, which is what
`crates/ui/src/text/format/markdown.rs` does with `ast_to_node`.

The pin lives in [`cmd/run.ts`](../../cmd/run.ts) (`markdown`)
alongside the gpui-kit, Zed GPUI and taffy pins, and moves when they do
— see [`/update-port`](../../.claude/skills/update-port/crates.md).

This remains the default parser. `-markdown=mini` selects the independent,
size-focused implementation in [`src/markdown-mini`](../markdown-mini) while
keeping this directory's public `markdown.h` and compact mdast representation.

Everything is in `namespace markdown`, not `gpui`, because it is a port of a
crate of its own: `gpui::CharKind` and `markdown::CharKind` both exist and mean
different things, and so do `Name`, `Link`, `Point` and `Node`.

The dependency goes one way and stops early: this directory includes `base.h`
and its own headers, and nothing else in the tree. `Str`, `Arena`, `ArenaVec`
and `Alloc` are `base::`, which is the namespace the SumatraPDF base lives in
for exactly this reason; no gpui header is included here and no `gpui::` name
appears in the code. `cmd/update-dist.ts` fails the build if that stops being
true, because the amalgam compiles all of `src/` as one translation unit and
would not otherwise notice. Anything this port comes to need from the tree
belongs in `base`, or it does not belong to this port.

```cpp
Arena* a = ArenaNew();
markdown::Node* tree =
    markdown::ToMdast(a, source, markdown::ParseOptions::Gfm());
```

## Where the Rust went

| Rust                                     | C++                       |
| ---------------------------------------- | ------------------------- |
| `src/lib.rs`, `src/configuration.rs`     | `markdown.h` / `markdown.cpp` |
| `src/event.rs`                           | `event.h` (+ `IsVoidEvent` in `tokenizer.cpp`) |
| `src/state.rs`                           | `state.h` / `state.cpp`   |
| `src/tokenizer.rs`                       | `tokenizer.h` / `tokenizer.cpp` |
| `src/parser.rs`, `src/subtokenize.rs`, `src/resolve.rs` | `parser.cpp` |
| `src/mdast.rs`                           | `mdast.h` / `mdast.cpp`   |
| `src/to_mdast.rs`                        | `to_mdast.cpp`            |
| `src/util/*.rs`                          | `util.h` / `util.cpp`     |
| `src/util/constant.rs`                   | `constant.h` / `constant.cpp` |
| `src/util/unicode.rs`                    | `unicode.cpp`             |
| `src/construct/*.rs`                     | `construct.h` + the seven `construct_*.cpp` below |

The crate has one file per construct; here they are grouped, and each file's
header names the Rust modules it holds:

| C++                       | Rust construct modules                         |
| ------------------------- | ---------------------------------------------- |
| `construct_document.cpp`  | `document`, `flow`, `content`, `paragraph`     |
| `construct_flow.cpp`      | `block_quote`, `code_indented`, `thematic_break`, `heading_atx`, `heading_setext`, `list_item`, `definition`, `frontmatter` |
| `construct_raw.cpp`       | `raw_flow`, `raw_text`                         |
| `construct_html.cpp`      | `html_flow`, `html_text`                       |
| `construct_text.cpp`      | `text`, `string`, `attention`, `autolink`, `character_escape`, `character_reference`, `hard_break_escape`, `label_start_image`, `label_start_link` |
| `construct_label.cpp`     | `label_end`                                    |
| `construct_gfm.cpp`       | `gfm_table`, `gfm_footnote_definition`, `gfm_label_start_footnote`, `gfm_task_list_item_check`, `gfm_autolink_literal` |
| `construct_partial.cpp`   | `partial_space_or_tab`, `partial_space_or_tab_eol`, `partial_data`, `partial_destination`, `partial_label`, `partial_title`, `partial_whitespace`, `partial_bom`, `partial_non_lazy_continuation`, `blank_line` |

Every state function keeps its Rust name in CamelCase — the crate's
`StateName` enum already spells them that way, so `construct::attention::start`
is `AttentionStart` and the dispatcher in `state.cpp` is a switch of identical
pairs. A diff against the crate therefore applies by name.

## What is ported

CommonMark and GFM: everything `ParseOptions::gfm()` turns on, which is what
`markdown_ext.rs` asks for. The constructs that are off by default are here
too and answer to the same flags — `frontmatter`, `math_flow`, `math_text`.

Not ported, and not planned:

- **MDX** — `mdx_esm`, `mdx_expression_flow`, `mdx_expression_text`,
  `mdx_jsx_flow`, `mdx_jsx_text` and the partials, tree nodes, `Location` and
  `mdx_collect` that serve them. `TextView` never turns it on: the
  `enable_mdx` half of `markdown_ext.rs` is a gpui-kit extension for
  callers that bring their own plugins, and nothing in the story, the showcase
  or the examples does.
  Two consequences worth knowing, both kept faithful:
  - MDX is the only thing in the crate that can *fail*, so `to_mdast` here
    returns the tree rather than a `Result`. `State::Error` and
    `message::Message` are gone with it.
  - A flow line starting with `e`, `i` or `{` still jumps straight to content
    the way it does in the crate, because that is where MDX's failure sends
    it — so a GFM table whose header row starts with one of those three bytes
    is not a table, in the crate and here alike. `FlowStart` says so.
- **`to_html`** — `TextView` renders elements, not HTML. `util/encode.rs`,
  `util/gfm_tagfilter.rs` and `util/sanitize_uri.rs` go with it.
- **`serde`** — nothing serialises a tree.

## Deliberate differences

Each of these is also stated in a comment at the place it applies.

- **One `Node` struct, not thirty.** Rust's `mdast::Node` is an enum of a
  struct per kind; here it is one struct with a `kind` and the union of their
  fields, the way `ui/text.h`'s `MdNode` is. `Option<String>` is a `Str` whose
  `s` is null.
- **`Option<u8>`/`Option<usize>` are `int32_t`**, -1 for `None`: the current
  and previous byte, the link indices, the `document_data_index`.
- **`Vec<String>` is `Vec<Str>`** into the parse's arena. A `ParseState`
  carries two arenas: the caller's, which the tree and its strings come from,
  and a scratch one for the parse's own working memory (the edit maps, the
  definition labels, the attention stacks), thrown away whole at the end so
  none of it is left in the caller's.
- **The tokenizer's `Attempt` and `Check` are free functions** named
  `TokenizerAttempt` / `TokenizerCheck`, since `Attempt` is a struct here.
  For the same reason `tokenizer.rs`'s `LabelStart` struct is
  `LabelStartMark`: the state named `LabelStart` is a function.
- **`normalize_identifier` folds ASCII only.** Rust folds all of Unicode with
  `to_lowercase().to_uppercase()`. A label whose letters are not ASCII still
  matches when its bytes match, which is what real documents rely on; two
  spellings of the same non-ASCII letter do not. Its other quirk *is* kept:
  whitespace inside a value whose first word starts at offset 0 collapses to
  nothing rather than to a space, which is what markdown-rs 1.0.0 does (its
  doc comment claims otherwise) and therefore what a reference has to do to
  find the definitions it finds.
- **The edit map indexes and merge-sorts.** `util/edit_map.rs`'s `add` walks
  the entries it already holds looking for one at the same index, and its
  `consume` sorts them; both are written for a map that stays short. A GFM
  table adds an entry per cell, so a document of tables is quadratic in its
  cell count twice over — 1 MB of them took 22 seconds. Here `add` finds its
  entry through an open-addressed table keyed by `at`, and `consume` sorts
  with a bottom-up merge sort. Same entries, same order (no two entries share
  an `at`, so a stable sort lands where `sort_unstable_by` would), same
  events; `bench/MarkdownBench.cpp` measures it.
- **The character reference table is sorted.** The crate walks its 2125 pairs
  with `find`, so their order is nearly-but-not-quite ascending (`emsp14`
  before `emsp`, `sup3` before `sup`); here they are sorted so `DecodeNamed`
  can binary search. Only those two pairs move.
- **`PUNCTUATION` is ranges.** `util/unicode.rs` lists 9369 code points one by
  one; `unicode.cpp` holds the 349 ranges they collapse into and binary
  searches them — the same answer for every code point, in 8 KB rather than
  37 and without a linear scan.
- **No panics.** The crate `expect()`s its way through `to_mdast`, and there
  are inputs it panics on (`- ~~~~\n1. ~~~ meta` is the shortest one found).
  Nothing here asserts, so those produce a tree rather than a crash — see the
  note under Tests.

## Tests

`tests/MarkdownTests.cpp` ports the crate's `#[cfg(test)]` modules that pin
behaviour rather than Rust specifics — `util/constant.rs`, `util/char.rs` and
`configuration.rs` — and adds an end-to-end check of each construct, reading
the tree the way `TextView` does.

The crate's real suite is the ~8000 CommonMark and GFM cases in its `tests/`
directory, which is not part of the published crate (`Cargo.toml`'s `include`
covers `src/` only), the same gap taffy has. What
was run in its place, once, from a scratch cargo project holding this exact
crate version:

- Both parsers dumped their **event stream** for 3283 documents — every `.md`
  in this tree and in `.work/gpui-component`, an edge-case document, and 3000
  generated from fragments (containers, tabs, CRLF, punctuation soup). All
  3283 matched byte for byte.
- Both dumped their **mdast** for the same corpus. Every document markdown-rs
  can parse produced an identical tree; on 35 of the generated ones
  markdown-rs panicked and this did not.

`bun cmd/test.ts` is the suite that stays.

## Speed

`bun cmd/bench.ts markdown` is the measurement, and
`bench/MarkdownBench.cpp` says what each row is. Four document shapes at
64 KB, plus the tokenize / to_mdast split of one of them; `-small` and
`-large` add a 16 KB and a 1 MB size.

For scale: 1.5 ms for the 13 KB `assets/story/README.md`, against 2.1 ms for
markdown-rs 1.0.0 built `--release` on the same machine, so a page costs what
it costs Rust. That is far more than the 52 µs the vendored md4c took before
this, which is why `ui/text.cpp` keeps its parse cache: a document is parsed
once and the tree is reused until its bytes change.

The one shape that stands out is tables, about seven times prose per byte.
markdown-rs is the same (10.5 ms and 80.4 ms on those two shapes, against
9.4 ms and 66.8 ms here), so it is the edit map both share: `add` scans the
entries it already holds for one at the same index, and a table adds one per
cell.

## Refreshing the port

The `/update-port` skill moves this crate when a gpui-kit checkin's
`Cargo.lock` does — [`crates.md`](../../.claude/skills/update-port/crates.md).
The tables above are the map it applies the diff through.

Two files are generated rather than typed, and their headers say so:
`constant.cpp` (the tag-name lists and the 2125 character references) and
`unicode.cpp` (the punctuation ranges). Both are transcriptions of the crate's
own tables; regenerate them by transcribing `src/util/constant.rs` and
`src/util/unicode.rs` again.

The three lists in `constant.cpp` are `SeqStrings` runs — one NUL-terminated
string after another, the run ended by an empty one — rather than arrays of
`Str` or of `const char*`. The two tag-name lists are scanned end to end
anyway, so they need nothing else; the character references keep a table of
byte offsets beside them so the lookup is still the binary search their sort
is for. Transcribing them again means writing the runs and, for the
references, the offsets into them: `markdown constants` in `tests/` walks both
runs against the table and fails if the two ever disagree.

## The standalone extras/ pair

Besides being compiled into the gpui amalgam, this library is also
amalgamated on its own by `cmd/update-dist.ts` into an `extras/` pair (one
header + one source, base inlined, the implementation included) for using
it without gpui. `readme-dist.md` documents the pairs; never link one
beside `gpui.cpp`, which already contains this code.
