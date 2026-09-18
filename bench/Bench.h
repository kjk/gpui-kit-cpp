/* The benchmark harness, and the tree builders the layout benchmarks share.

   Most of what runs here is a port of the benchmarks in taffy's `benches/`
   directory, one file per Rust one. That directory is a crate of its own —
   `taffy_benchmarks` — and is not part of the published crate, so it comes
   from the git checkout .claude/skills/update-port/crates.md describes.
   `MarkdownBench.cpp` and `Html5everBench.cpp` are not ports: their crates
   carry no comparable benchmarks to translate, so those cases are ours and
   the files say what each measures.

   The Rust runs on criterion, which warms up, estimates a sample count and
   reports a confidence interval. This runs a fixed number of samples and
   reports the median and the minimum, which is what a layout number is read
   for: the median says what a frame costs and the minimum says what it costs
   with the noise taken out.

   `benches/mixed.rs` is not ported. It measures text leaves through `parley`,
   a Rust text stack we have no equivalent of; the shape it lays out —
   alternating flex and grid containers — would need our own text measure
   substituted, which makes it a different benchmark rather than a port of
   that one. Worth doing on its own, as `LayoutMeasure` is what every real
   window spends its layout time in. */

#ifndef GPUI_BENCH_H_
#define GPUI_BENCH_H_

#include "gpui.h"

using namespace gpui;

// ─── the harness ─────────────────────────────────────────────────────────

// Samples per benchmark. Criterion picks this per group; here it is one
// number for the whole run, settable with -n=<count>.
extern int gBenchSamples;
// The crate's `small` and `large` cargo features, which gate the smallest and
// largest node counts in `flexbox.rs`.
extern bool gBenchSmall;
extern bool gBenchLarge;
// Only benchmarks whose group or name contains this run. Null means all.
extern Str gBenchFilter;

/* Runs one benchmark and prints its row.

   `setup` builds a fresh tree and is not timed; `run` is what the clock sees.
   That is criterion's `iter_batched(setup, routine, SmallInput)`, which the
   Rust uses everywhere except `tree_creation.rs`, where building the tree is
   the thing being measured and `setup` does nothing.

   `param` is the number the row is indexed by — the node count, the track
   count, the depth — and `unit` names it. */
void BenchCase(const char* group, const char* name, const char* unit,
               int64_t param, Func0 setup, Func0 run);

// The memory a case's output cost, printed in the same shape as its timing
// row. `param` is the case's own number — the bytes of source — so the ratio
// says how much arena a byte of document turns into.
void BenchMem(const char* group, const char* name, int64_t param,
              uint64_t bytes);
// The same line under another label, for a case with more than one number
// worth reporting: the markdown parse reports the tree it produced and the
// scratch it produced it with, and the second is where every ArenaVec lives.
void BenchMemAs(const char* group, const char* name, const char* what,
                int64_t param, uint64_t bytes);

// True if a case with this group and name would run. A benchmark whose setup
// is expensive to even reach can ask first.
bool BenchWanted(const char* group, const char* name);

// Rust's `std::hint::black_box`: keeps the compiler from concluding that the
// work had no effect and removing it.
void BenchKeep(const void* p);

// ─── benches/src/lib.rs ──────────────────────────────────────────────────

/* The random source.

   Rust's benches seed `ChaCha8Rng` with `STANDARD_RNG_SEED` and draw styles
   through `rand` 0.9's `random_range`. This is that generator and those
   samplers, so a C++ tree is the same tree the Rust bench builds — not only
   the same shape. `Seed` is `SeedableRng::seed_from_u64`; `Range` is the
   one-shot f32 path (inclusive and exclusive share it); `RangeInt` is the
   one-shot inclusive integer path (`1..=4`). */
struct BenchRng {
    uint32_t key[8] = {};
    uint64_t blockPos = 0;
    uint32_t buf[64] = {};
    int index = 64;

    void Seed(uint64_t seed);
    uint32_t NextU32();
    // [0, 1), rand's `StandardUniform` for f32.
    float NextFloat();
    // Rust's `random_range(lo..hi)` / `random_range(lo..=hi)` for f32.
    float Range(float lo, float hi);
    // Rust's `random_range(lo..=hi)` for i32 / usize when high fits in u32.
    int RangeInt(int lo, int hi);
};

// Aborts if this RNG does not match rand_chacha 0.9 + rand 0.9 on seed 12345.
void BenchRngCheck();

extern const uint64_t kStandardRngSeed;

/* How a benchmark makes the styles for the tree it builds.

   Rust's `GenStyle` trait, with `FixedStyleGenerator` and the per-benchmark
   generators as implementations. A function pointer and a `void*` are the
   same thing without the vtable; `root` may be null, which is Rust's default
   method returning `Style::default()`. */
using BenchStyleFn = taffy::Style (*)(BenchRng* rng, void* ud);

struct StyleGen {
    BenchStyleFn leaf = nullptr;
    BenchStyleFn container = nullptr;
    BenchStyleFn root = nullptr;
    void* ud = nullptr;
};

/* Rust's `BuildTree` / `BuildTreeExt`.

   Those are traits so that the same shapes can be built for taffy, taffy 0.3
   and Yoga and timed against each other. There is one tree here, so they
   collapse into it. */
struct TreeBuilder {
    taffy::TaffyTree tree;
    BenchRng rng;
    StyleGen gen;
    taffy::NodeId root;

    // `capacity` is the tree's initial slot count — Rust's
    // `TaffyTree::with_capacity`, whose default is `TaffyTree::new`.
    void Init(const StyleGen& gen, int capacity = 16);
    void Free();

    taffy::NodeId CreateLeafNode();
    taffy::NodeId CreateContainerNode(const taffy::NodeId* children, int n);
    void SetRootChildren(const taffy::NodeId* children, int n);
    int TotalNodeCount() const { return tree.TotalNodeCount(); }

    // A tree `branchingFactor` wide at every level, deep enough to hold
    // `maxNodes`. Appends the nodes it made to `out`.
    void BuildDeepTree(uint32_t maxNodes, uint32_t branchingFactor,
                       Vec<taffy::NodeId>* out);
    void BuildDeepHierarchy(uint32_t nodeCount, uint32_t branchingFactor);
    // Many children, shallow: containers of 1-4 leaves under the root.
    void BuildFlatHierarchy(uint32_t targetNodeCount);
    // One container per level, each with `nodesPerLevel - 1` leaves beside it.
    void BuildSuperDeepHierarchy(uint32_t depth, uint32_t nodesPerLevel);

    void ComputeLayout(taffy::Optf availableWidth, taffy::Optf availableHeight);
};

// The benchmark files. The parser benchmarks are ours and say why in their
// own headers.
void BenchFlexbox();
void BenchGrid();
void BenchTreeCreation();
void BenchMarkdown();
// bench/Html5everBench.cpp -- a large generated HTML document.
void BenchHtml5ever();
// bench/MotionBench.cpp — the motion core's steady sampling paths.
void BenchMotion();

#endif // GPUI_BENCH_H_
