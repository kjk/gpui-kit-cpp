# The ported crates and the Zed GPUI reference

`src/taffy/`, `src/markdown/`, `src/html5ever/`, `src/wry/` and
`src/autocorrect/` are ports, not references, of exactly the version
gpui-kit resolves. **They move when the gpui-kit pin moves, and only then**:
"latest" for a crate means what gpui-kit's `Cargo.lock` resolves at the
checkin being ported, never a newer release. The SKILL.md loop sends you here
when a checkin's `Cargo.lock` moves one of them, or `gpui-pre`.

## Which version

| Pin in `cmd/run.ts` | `Cargo.lock` name | Asked for by | Upstream |
| --- | --- | --- | --- |
| `taffy` | `taffy` | `gpui-pre` (Zed's gpui) | `DioxusLabs/taffy`, git tags `vX.Y.Z` |
| `markdown` | `markdown` | `crates/base`, `crates/component` | `wooorm/markdown-rs`, git tags `X.Y.Z` |
| `html5ever` | `html5ever` | `crates/base` | `servo/html5ever` — use the crate tarball |
| `wry` | `lb-wry` | `crates/webview`, `examples/webview` | longbridge's fork, published from a fork — tarball only, no useful tag |
| `autocorrect` | `autocorrect` | `crates/story`, `examples/editor` | `huacnlee/autocorrect` — tarball; git tags carry the whole workspace |

```
grep -A3 'name = "taffy"' .work/gpui-component/Cargo.lock   # also markdown, html5ever, lb-wry, autocorrect
```

`Cargo.lock` can carry a crate at two versions (html5ever does: 0.27 for
`gpui-base`, 0.29 for something else). Port the one the consumer asks for —
its `Cargo.toml` line, or the `"html5ever 0.27.0"` spelling in the
consumer's `dependencies` list in `Cargo.lock`.

## Getting the source

Tarball crates — and the check that it is the crate `Cargo.lock` means:

```
curl -sLo .work/crates/NAME-VER.crate https://static.crates.io/crates/NAME/NAME-VER.crate
sha256sum .work/crates/NAME-VER.crate        # must equal Cargo.lock's checksum
tar -xzf .work/crates/NAME-VER.crate -C .work/crates
```

Fetch the old version the same way when `.work/crates/` does not already have
it, and diff the two extracted trees.

taffy and markdown keep their upstream test suites in a `tests/` directory
the *published crate does not carry* (their `Cargo.toml` `include` covers
`src/` and, for taffy, `examples/`), and taffy its `benches/`; those need the
git clone, not the tarball. `.work/taffy` is a clone of taffy (`git -C
.work/taffy fetch --tags origin`); clone markdown-rs to `.work/markdown-rs`
the same way when it is missing.

## Porting a crate move

1. Read the crate's `readme.md` first — `src/<crate>/readme.md` has the
   file-for-file map and the deliberate-omission list. A diff that only
   touches an omitted area needs no work here beyond the version.
2. Walk the change **checkin by checkin** where the crate has a usable git
   history (taffy, markdown-rs):

   ```
   git -C .work/taffy log --reverse --oneline vOLD..vNEW -- src
   git -C .work/markdown-rs log --reverse --oneline OLD..NEW -- src
   ```

   one local commit per upstream commit that changes `src/`:
   `Port taffy <sha8>: <what>` / `Port markdown-rs <sha8>: <what>`. For the
   tarball crates, diff the two versions (html5ever: `src/`, the generated
   tokenizer data and the crate features; autocorrect: `src/` **and**
   `grammar/`) and commit by area: `Port lb-wry 0.53.3..0.54.0 (1/2): <what>`.
3. Every ported function keeps its Rust name in CamelCase (taffy), its
   `StateName` spelling (markdown), or its module's (wry, autocorrect), so a
   diff applies mechanically. The crate stays `namespace <crate>`, includes
   `base.h` and its own headers and nothing else, names no `gpui::` symbol —
   `cmd/update-dist.ts` fails the build otherwise.
4. Port the tests with the code: `tests/TaffyTests.cpp`,
   `tests/MarkdownTests.cpp`, `tests/AutocorrectTests.cpp`,
   `tests/AutocorrectMarkdownFormatTests.cpp`, `bench/`. A behaviour-changing
   bump should show up there first. `bun cmd/test.ts -rel`, and for taffy or
   markdown `bun cmd/bench.ts` from a release build — layout is the one thing
   here with an asymptotic complexity, and a wrong one hides.
5. The last commit of the series moves the pin: `version` and `crateSha256`
   in `cmd/run.ts`, the version named at the top of the crate's `readme.md`,
   and any reader-facing mention (AGENTS.md, `port-status.md`). A new
   deliberate difference goes in the crate readme's list and in a comment
   where it applies.

Then return to SKILL.md and port the gpui-kit checkin itself.

## Per-crate notes

**taffy.** Ported feature set: `std`, `taffy_tree`, `flexbox`, `grid`,
`block_layout`, `float_layout`, `calc`, `content_size`; not `parse`, `serde`,
`detailed_layout_info`. `operator==` on `Style` compares every field by hand
(it is Rust's `PartialEq` derive, which the layout cache relies on), so **a
field added to `Style` must be added to `operator==`** or a change to it will
not reach layout. `Option<f32>` is the NaN-tagged `Optf`; a new optional
value that starts unknown says `SizeFOptNone()`, not `{}`. Grid occupancy is
a flat matrix where 0.13 uses interval vectors — keep it flat.

**markdown.** Two files are transcribed rather than typed:
`constant.cpp` (the tag-name lists and the 2125 character references, as
`SeqStrings` runs plus, for the references, a byte-offset table) from
`src/util/constant.rs`, and `unicode.cpp` (the punctuation ranges) from
`src/util/unicode.rs`. If either Rust table moves, transcribe it again;
`markdown constants` in `tests/` walks both runs against the table. MDX,
`to_html` and `serde` are not ported. `src/markdown-mini/` is ours, not
upstream — do not ingest new markdown-rs constructs into it; keep its smaller
contract (`src/markdown-mini/readme.md`).

**html5ever.** Complete-input parsing into an arena DOM; no tendril feed, no
script pause, reader-set named references. `src/html5ever-mini/` is ours; the
same rule as markdown-mini applies.

**wry.** `src/wry/readme.md` maps `lib.rs`, `webview2/`, `wkwebview/`. The
WebView2 declaration block and the written-out loader in `wry_win.cpp` move
when the *SDK* does — the `webview2-com-sys` version lb-wry pins — not when
wry does. If that pin moves: re-transcribe the changed `ICoreWebView2*`
interfaces (same vtable order, same IIDs), compare every definition and IID
against the new `webview2-com-sys` generated bindings — the readme's
97-definition audit was a one-off, there is no script for it — and update
`TargetCompatibleBrowserVersion` from `CORE_WEBVIEW_TARGET_PRODUCT_VERSION`.
`webview2-com`, `http`, `cookie` and `raw-window-handle` stay replaced.

**autocorrect.** Not part of the gpui amalgam — `extras/autocorrect/` is
compiled only into the targets that use it (`autocorrectTargets` in
`cmd/build.ts`). Regex rules are hand-written scanners and `.pest` grammars
are scanners; a changed regex or grammar is re-transcribed keeping the
crate's own quirks (`src/autocorrect/readme.md`). If the pin moves,
re-extract the two `indoc!` blocks of `test_format_markdown` in
`src/code/markdown.rs` into `tests/AutocorrectMarkdownFormatTests.cpp`
verbatim.

## Dependencies we replace rather than port

A crate move can bring in a new dependency. Nothing on this list is ported;
something new joins it or becomes a port only as a decision the user makes.

`sysinfo`, `battery`, `smol`, `reqwest`, ropey, tree-sitter, syntect and resvg
— OS APIs or our own code instead. taffy's `arrayvec`, `grid`, `slotmap`,
`cssparser` — `Vec`, a flat occupancy matrix, our own generational slots, no
CSS parser. markdown's `unicode-id` belongs to MDX, not ported. wry's
`webview2-com` / `-sys` are written out in `wry_win.cpp`, `http` and `cookie`
are a pair of structs, `raw-window-handle` is one `void*`.
`autocorrect-derive` is a dispatch table in `code.cpp`; pest, regex and
`ignore` are hand-written scanners and `ignorer.cpp`. `src/base.h` /
`src/base.cpp` are SumatraPDF, not gpui-kit.

## Zed GPUI — reference only

gpui-kit's `Cargo.lock` pins the published `gpui-pre` packages; their Cargo
metadata records the Zed revision they snapshot. That snapshot is what
`src/gpui/` is matched against when a question is about runtime behaviour —
text measure cache, platform shaping, the window. **Do not treat later Zed
`main` as the spec.** We reimplement a subset; Blade, the entity/observer
graph, cosmic-text and font-kit are not ported.

When a checkin moves `gpui-pre`:

1. Fetch both `gpui-pre` tarballs into `.work/crates/` as above. The new one's
   `Cargo.toml` has `[package.metadata.gpui-pre] zed-rev`.
2. Update `zedGpui` in `cmd/run.ts`: `sha` = `zed-rev`, `date` and `subject`
   from `gh api repos/zed-industries/zed/commits/<rev> --jq
   '.commit.committer.date, .commit.message'`, the `gpui-pre`,
   `gpui-pre-platform` and `gpui-pre-macros` versions, and `lock`. Update the
   "Upstream package names" bullet in `port-status.md`, which names the
   version and revision.
3. Diff the two extracted trees (`src/`), reading for what `src/gpui/`
   reimplements. Port a behaviour change there only where it shows through
   gpui-kit's widgets; say in the commit what was read and left.
4. If `gpui-pre`'s own `taffy` requirement moved, that is the taffy move
   above.

The commit is named after the gpui-kit checkin that moved it:
`Port upstream 487379c6: advance the GPUI reference to 0.3.5`. After a Rust
build, the matching Zed checkout is also at
`%USERPROFILE%\.cargo\git\checkouts\zed-*\<sha prefix>\` when the lock names
a git source rather than the registry.
