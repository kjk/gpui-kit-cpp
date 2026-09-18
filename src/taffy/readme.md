# src/taffy — the taffy layout crate, ported to C++

This is a port of [taffy](https://github.com/DioxusLabs/taffy) **0.13.0**, the
layout crate Zed's GPUI uses and therefore the one gpui-kit's layout is
defined by. Everything in `src/gpui` that has a box lays it out through here.

The pinned version is the one `gpui-kit`'s `Cargo.lock` resolves for
`gpui`. The pin lives in
[`cmd/run.ts`](../../cmd/run.ts) (`taffy`) alongside the
gpui-kit and Zed GPUI pins, and moves when they do — see
[`/update-port`](../../.claude/skills/update-port/crates.md).

## Where the Rust went

| Rust                                | C++                     |
| ----------------------------------- | ----------------------- |
| `src/geometry.rs`                   | `geometry.h`            |
| `src/util/sys.rs` (the f32 helpers) | `geometry.h`            |
| `src/util/math.rs`                  | `taffy_math.h`          |
| `src/util/resolve.rs`               | `style.h` / `style.cpp` |
| `src/style/*.rs`                    | `style.h` / `style.cpp` |
| `src/tree/{node,layout,cache}.rs`   | `tree.h` / `tree.cpp`   |
| `src/tree/{traits,taffy_tree}.rs`   | `taffy_tree.h` / `.cpp` |
| `src/compute/{mod,leaf,common}.rs`  | `compute.h` / `compute.cpp` |
| `src/compute/flexbox.rs`            | `compute_flexbox.cpp`   |
| `src/compute/{block,float}.rs`      | `compute_block.cpp`     |
| `src/compute/grid/**`               | `compute_grid.cpp`      |

Everything is in `namespace taffy` — not `gpui` — because it is a port of a
crate of its own rather than part of gpui-kit. `gpui::Style` and
`taffy::Style` both exist and mean different things; so do `Overflow`,
`Position` and `Display`.

The dependency goes one way and stops early: this directory includes
`base.h` and its own headers, and nothing else in the tree. `Str`, `Vec`,
`Arena` and `Alloc` are `base::`, which is the namespace the SumatraPDF base
lives in for exactly this reason; no gpui header is included here and no
`gpui::` name appears in the code. `cmd/update-dist.ts` fails the build if that
stops being true, because the amalgam compiles all of `src/` as one
translation unit and would not otherwise notice. Anything this port comes to
need from the tree belongs in `base`, or it does not belong to this port.

## What is ported

The default feature set of the crate as gpui builds it: `std`, `taffy_tree`,
`flexbox`, `grid`, `block_layout`, `float_layout`, `calc`, `content_size`.

Not ported, and not planned:

- `parse` — the CSS parser, which needs `cssparser`. Styles here are built by
  the caller, never parsed.
- `serde` — nothing serialises a style.
- `detailed_layout_info` — an accessor for the computed grid track sizes.
  Nothing in this tree reads it.

## The PartialEq derive

Rust derives `PartialEq` on `Style` and on the types it holds; that derive is
ported (`operator==` in `style.h` / `style.cpp`) because `src/gpui`'s layout
cache asks whether an element's style is the one its node already carries. Two
notes on it, both in comments where they apply: `Optf` is Rust's
`Option<f32>` with a NaN standing for None and is compared by bits, since
`NaN == NaN` would report two Nones unequal; and every field of `Style` is
compared by hand, so a field added to `Style` has to be added there too or a
change to it will not reach layout.

## Deliberate differences

Each of these is also stated in a comment at the place it applies.

- **No traits.** Rust's `LayoutPartialTree`, `CacheTree`,
  `LayoutFlexboxContainer`, `GridContainerStyle` and the rest exist so a caller
  can bring its own tree and style types. There is one of each here, so the
  compute pass takes a `TaffyTree*` and reads `Style` fields directly. The
  method names are the trait method names.
- **No templates on the containers.** `Size<T>`, `Point<T>`, `Rect<T>` and
  `Line<T>` become one struct per element type the algorithms actually carry:
  `SizeF`, `SizeDim`, `SizeAvail`, `SizeLp`, `RectF`, `RectLp`, `RectLpa`,
  `PointF`, `PointOverflow`, `LineF`, `LineBool`, `LineOzl`, `LinePlacement`,
  `LinePlain`. The optional enums get one struct each. The only template left
  is `Slice<T>`.
- **`Option<f32>` is a NaN-tagged `float`.** `Optf` is `using Optf = float`
  with one reserved quiet-NaN bit pattern (`0x7fc0beef`) standing for `None`;
  `Some`, `None`, `IsSome`, `UnwrapOr` and `Or` are free functions over it.
  `SizeFOpt`, `PointFOpt` and `RectFOpt` are therefore aliases of `SizeF`,
  `PointF` and `RectF` rather than types of their own, which halves every
  optional size, point and rect layout carries and drops the conversions
  between the definite and optional views of one. Reserving a single pattern
  rather than "any NaN" keeps the NaNs taffy's own arithmetic can produce
  meaning what they meant. It is also what collapses math.rs's `MaybeMath`
  overloads: `Optf op Optf`, `Optf op float` and `float op Optf` are one
  function now, because a plain float is an `Optf` that is always `Some`. A
  value that starts out unknown has to say so — `SizeFOptNone()`, not `{}` —
  since the alias defaults to zero, and two of them compare with `OptfEq` /
  `SizeFOptEq`, because `None == None` is a NaN comparison. Measured on
  `bench/`, this takes 23-33% off the flexbox benchmarks and 17-19% off the
  grid ones.
- **`Style` owns nothing.** Its grid track lists are arena-backed `Slice<T>`
  rather than `Vec<T>`, and a custom ident is a `Str` pointing into that arena.
  A `Style` therefore copies as bytes; whoever built it owns the arena.
- **No `Result`.** The three calls that can be handed an out-of-range child
  index return `bool`; everything else takes a live node by contract, which is
  what Rust's `.expect()`-ing callers already assume.
- **`TaffyView` is gone.** It exists in Rust only to keep the measure closure
  out of the tree's borrow. The measure function is a field on the tree here,
  set for the duration of a `ComputeLayoutWithMeasure` call.
- **`NodeContext` is a `void*`**, handed back to the measure function
  untouched, rather than a type parameter.
- **Grid occupancy is flat storage.** Rust 0.13 changed its
  `CellOccupancyMatrix` to sparse per-track interval vectors. This port keeps
  the POD byte matrix, but derives the occupied interval at each collision and
  makes the same jump over it during auto-placement. The layout behavior is
  the same; the tradeoff is simpler ownership and indexing here versus the
  Rust representation's lower memory use for very sparse implicit grids.

## Tests

`tests/TaffyTests.cpp` ports every `#[cfg(test)]` module in the crate that
pins behaviour rather than Rust specifics — `util/math.rs`, `util/resolve.rs`,
`style/alignment.rs`, `style/flex.rs`, `style/mod.rs`, `compute/mod.rs`,
`tree/taffy_tree.rs`, and the grid's `explicit_grid.rs`, `implicit_grid.rs` and
`placement.rs` — plus end-to-end checks of flexbox, block and grid layout.

The crate's larger generated suite lives in its `tests/` directory, which is
not part of the published crate (its `Cargo.toml` `include` covers only `src/`
and `examples/`), so it takes the git checkout of taffy
(`.claude/skills/update-port/crates.md`).

Three grid internals are reached through the seams `GridExplicitSizeForTest`,
`GridChildMinMaxSpanForTest`, `GridSizeEstimateForTest`,
`GridInitTracksForTest` and `GridPlaceForTest` in `compute.h`. They exist for
the tests and have no other caller.

## Benchmarks

`bench/` ports the crate's own benchmarks — `benches/benches/flexbox.rs`,
`grid.rs` and `tree_creation.rs`, and the tree builders in `benches/src/`.
Run them with `bun cmd/bench.ts`. They come from the same checkout as the
generated tests, for the same reason.

Not ported: `benches/benches/mixed.rs`, which measures text leaves through
`parley`. Its shape — flex and grid containers alternating, with real text at
the leaves — is worth having, since measured leaves are where a real window
spends its layout time, but with our own text measure substituted it would be
a new benchmark rather than a port of that one.

The random source is Rust's: `ChaCha8Rng` seeded 12345 and `rand` 0.9's
`random_range`, so a C++ tree is the same tree the Rust bench builds. `bench/`
pins the first draws against that crate.

## Refreshing the port

The `/update-port` skill moves this crate when a gpui-kit checkin's
`Cargo.lock` does — [`crates.md`](../../.claude/skills/update-port/crates.md).
The table above is the map it applies the diff through.

## The standalone extras/ pair

Besides being compiled into the gpui amalgam, this library is also
amalgamated on its own by `cmd/update-dist.ts` into an `extras/` pair (one
header + one source, base inlined, the implementation included) for using
it without gpui. `readme-dist.md` documents the pairs; never link one
beside `gpui.cpp`, which already contains this code.
