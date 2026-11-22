# markdown-mini

`markdown-mini` implements the public API in `src/markdown/markdown.h` and
produces the same `markdown::Node` mdast. Select it when building with:

```text
bun cmd/build.ts -markdown=mini -rel story
```

`-markdown=full` is the default. Selection happens while `gpui.cpp` is
amalgamated, so only one parser reaches the compiler and linker.

The mini parser supports paragraphs, ATX and setext headings, bold, italic,
inline and fenced/indented code, ordered and unordered lists, blockquotes,
inline links and images, thematic breaks, backslash escapes, hard breaks,
numeric entities, and the small named-entity set `amp`, `lt`, `gt`, `quot`,
`apos`, and `nbsp` (including the HTML uppercase aliases where they exist).

For size, it omits GFM tables, task lists, footnotes, raw HTML, autolinks,
reference-style links, math/frontmatter, strikethrough, and the full HTML5
named-entity database. Unsupported syntax remains ordinary readable text.

It also keeps no source positions (`NodePosition` answers false). What the
TextView builds from them is therefore absent under `-markdown=mini`: block
surfaces (`> [!name]`), inline plugins, stream fades, and source ranges for
selection.

The test suite runs against both parsers (CI's clang lanes run
`bun cmd/test.ts -markdown=mini -html=mini`). A test of something this
contract leaves out runs only when `TestMarkdownIsFull()` says so.

## The standalone extras/ pair

Besides being compiled into the gpui amalgam, this library is also
amalgamated on its own by `cmd/update-dist.ts` into an `extras/` pair (one
header + one source, base inlined, the implementation included) for using
it without gpui. `readme-dist.md` documents the pairs; never link one
beside `gpui.cpp`, which already contains this code.
