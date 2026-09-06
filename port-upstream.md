# Ingesting a later upstream checkin

**Which checkin we port is the pin block at the top of [`cmd/run.ts`](cmd/run.ts)**
— `gpuiComponent.sha`, `zedGpui.sha`, and `taffy` / `markdown` / `html5ever` /
`wry` / `autocorrect` versions. `bun cmd/run.ts -versions` prints them and resets
`.work/gpui-component` to that SHA. Always diff from the pinned SHA, never from
`HEAD`.

## gpui-kit

Trees we translate, and the naming rule: a file under `src/base/` or `src/ui/`
is named after the Rust module it ports, so the map is mechanical both ways. A
Rust *directory* becomes one C++ file however many modules it holds
(`crates/base/src/input/` → `src/base/input.cpp`); `lib.rs` is `lib.h`. Code we
have that the crate has no module for takes the nearest name
(`element_ext.h`, `sizing.h`).

| Rust | C++ |
| --- | --- |
| `crates/base` | `src/base/` |
| `crates/component` | `src/ui/` |
| `crates/story` | `examples/story/` |
| `crates/base/examples/showcase` | `examples/showcase/` |
| `crates/fps` | `src/fps/` |
| `crates/webview` | `src/webview/` |
| `crates/shell` | `src/shell/` (JS engine: `src/quickjs/`) |
| `examples/*` | `examples/*.cpp` |

`README.md` is the one file checked in verbatim rather than translated — the
Introduction page renders it. Re-copy when the pin moves:
`cp .work/gpui-component/README.md assets/story/`.

```
bun cmd/run.ts -versions
cd .work/gpui-component && git fetch origin
git log --oneline <pinned-sha>..origin/main -- crates examples
git diff <pinned-sha> origin/main -- <path>
```

Then `bun cmd/audit-port.ts` (see `port-map.md`) — its content hashes fail on
any added, removed or renamed public declaration, re-export or test, which is
how a pin bump turns into an explicit decision list rather than a silent gap.

## The five ported crates

`src/taffy/`, `src/markdown/`, `src/html5ever/`, `src/wry/` and
`src/autocorrect/` are ports, not references, at the version gpui-kit
resolves. **They move when the gpui-kit pin moves.** After bumping
`gpuiComponent.sha`, check each:

```
grep -A3 'name = "taffy"' .work/gpui-component/Cargo.lock   # also: markdown, html5ever, lb-wry, autocorrect
```

If a version changed, set it in `cmd/run.ts` and diff the crate. Each crate's
`readme.md` has the file-for-file map and the deliberate-omission list — read
that first, since a diff that only touches an omitted area needs no work here.
Every ported function keeps its Rust name in CamelCase (taffy), its `StateName`
spelling (markdown), or its module's (wry, autocorrect), so a diff applies
mechanically.

| Crate | Upstream | How to diff |
| --- | --- | --- |
| taffy | `DioxusLabs/taffy` | git tags: `git -C .work/taffy diff vOLD vNEW -- src` |
| markdown | `wooorm/markdown-rs` | git tags: `git -C .work/markdown-rs diff OLD NEW -- src` |
| html5ever | `servo/html5ever` | crate tarball; compare `src/`, generated tokenizer data and crate features |
| wry | `lb-wry` (longbridge fork) | crate tarball from `static.crates.io` — published from a fork, so no useful tag |
| autocorrect | `huacnlee/autocorrect` | crate tarball; git tags carry the whole workspace. Diff `src/` **and** `grammar/` |

Both taffy and markdown keep their upstream test suites in a `tests/`
directory the *published crate does not carry*, and taffy its `benches/`, so
those need the git clone rather than the tarball. `tests/TaffyTests.cpp`,
`tests/MarkdownTests.cpp`, `tests/AutocorrectTests.cpp` and `bench/` are the
ports; a behaviour-changing bump should show up there first.

Two crate-specific notes a bump never touches: the WebView2 declaration block
in `wry_win.cpp` moves when the *SDK* does, not when wry does; and
`src/markdown-mini/` is ours, not upstream — do not mechanically ingest new
markdown-rs constructs into it (`src/markdown-mini/readme.md`).
The same applies to `src/html5ever-mini/`.

## Zed GPUI — reference only

`Cargo.lock` pins the published `gpui-pre` packages. Their Cargo package
metadata records the Zed revision (`zedGpui` in `cmd/run.ts`). Read that snapshot
when matching runtime behaviour (text measure cache, platform shaping, window);
do **not** treat later Zed `main` as the spec. After a rust build the checkout
is at `%USERPROFILE%\.cargo\git\checkouts\zed-*\<sha prefix>\`. We reimplement a
subset in `src/gpui/`; Blade, the entity/observer graph, cosmic-text and
font-kit are not ported.

## Dependencies we replace rather than port

`sysinfo`, `battery`, `smol`, `reqwest`, ropey, tree-sitter, syntect and resvg
— OS APIs or our own code instead. taffy's `arrayvec`, `grid`,
`slotmap`, `cssparser` — `Vec`, a flat occupancy matrix, our own generational
slots, no CSS parser. markdown's `unicode-id` belongs to MDX, not ported.
wry's `webview2-com` / `-sys` are written out in `wry_win.cpp`, `http` and
`cookie` are a pair of structs, `raw-window-handle` is one `void*`.
`autocorrect-derive` is a dispatch table in `code.cpp`; pest, regex and
`ignore` are hand-written scanners and `ignorer.cpp`.

`src/base.h` / `src/base.cpp` are SumatraPDF, not gpui-kit.
