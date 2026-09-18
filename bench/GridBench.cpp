/* Port of taffy's benches/benches/grid.rs: wide, deep and very deep grids
   with randomly chosen track sizing functions.

   These build their trees directly rather than through the `BuildTree` trait,
   the way the Rust does, because a grid container's style is made from its
   track count and does not fit the leaf/container split `TreeBuilder` has.

   A `taffy::Style` holds its track lists as arena-backed slices rather than
   owning `Vec`s (see src/taffy/style.h), so each case owns an arena and drops
   it with the tree. */

#include "Bench.h"

#include <stdio.h>

using namespace taffy;

// Rust's `build_random_leaf`. It takes an rng it never draws from.
static taffy::NodeId BuildRandomLeaf(TaffyTree* tree) {
    taffy::Style s;
    s.size = SizeDim::FromLengths(20.0f, 20.0f);
    return tree->NewLeaf(s);
}

static GridTemplateComponent RandomGridTrack(BenchRng* rng) {
    float sw = rng->Range(0.0f, 1.0f);
    if (sw < 0.1f) {
        return GridTemplateComponent::Single(TrackSizingFunction::Auto());
    }
    if (sw < 0.2f) {
        return GridTemplateComponent::Single(TrackSizingFunction::MinContent());
    }
    if (sw < 0.3f) {
        return GridTemplateComponent::Single(TrackSizingFunction::MaxContent());
    }
    if (sw < 0.5f) {
        return GridTemplateComponent::Single(TrackSizingFunction::Fr(1.0f));
    }
    if (sw < 0.6f) {
        return GridTemplateComponent::Single(
            TrackSizingFunction::MinMax(MinTrackSizingFunction::Length(0.0f),
                                        MaxTrackSizingFunction::Fr(1.0f)));
    }
    if (sw < 0.8f) {
        return GridTemplateComponent::Single(
            TrackSizingFunction::Length(40.0f));
    }
    return GridTemplateComponent::Single(TrackSizingFunction::Percent(0.3f));
}

static Slice<GridTemplateComponent> RandomTracks(Arena* arena, BenchRng* rng,
                                                 int n) {
    Slice<GridTemplateComponent> out =
        SliceNew<GridTemplateComponent>(arena, n);
    for (int i = 0; i < n; i++) {
        out[i] = RandomGridTrack(rng);
    }
    return out;
}

// Rust's `random_nxn_grid_style`.
static taffy::Style RandomNxNGridStyle(Arena* arena, BenchRng* rng,
                                       int trackCount) {
    taffy::Style s;
    s.display = taffy::Display::Grid;
    s.gridTemplateColumns = RandomTracks(arena, rng, trackCount);
    s.gridTemplateRows = RandomTracks(arena, rng, trackCount);
    return s;
}

// ─── the cases ───────────────────────────────────────────────────────────

struct GridCase {
    TaffyTree tree;
    Arena* arena = nullptr;
    BenchRng rng;
    taffy::NodeId root;

    int trackCount = 0;
    int levels = 0;
    // The available space layout runs against.
    SizeAvail avail = SizeAvail::MaxContent();

    void Reset() {
        tree.Free();
        if (arena) {
            ArenaDelete(arena);
        }
        arena = ArenaNew();
        tree.Init();
        rng.Seed(kStandardRngSeed);
    }
};

// Rust's `build_grid_flat_hierarchy`: one grid container holding a leaf per
// cell.
static void FlatSetup(GridCase* c) {
    c->Reset();
    taffy::Style s;
    s.display = taffy::Display::Grid;
    s.gridTemplateColumns = RandomTracks(c->arena, &c->rng, c->trackCount);
    s.gridTemplateRows = RandomTracks(c->arena, &c->rng, c->trackCount);

    int cells = c->trackCount * c->trackCount;
    Vec<taffy::NodeId> children;
    VecReserve(children, cells);
    for (int i = 0; i < cells; i++) {
        VecAppend(children, BuildRandomLeaf(&c->tree));
    }
    c->root = c->tree.NewWithChildren(s, children.els, len(children));
}

// Rust's `build_deep_grid_tree`.
static void BuildDeepGridTree(GridCase* c, int levels,
                              Vec<taffy::NodeId>* out) {
    int childCount = c->trackCount * c->trackCount;
    if (levels == 1) {
        for (int i = 0; i < childCount; i++) {
            VecAppend(*out, BuildRandomLeaf(&c->tree));
        }
        return;
    }
    for (int i = 0; i < childCount; i++) {
        Vec<taffy::NodeId> children;
        BuildDeepGridTree(c, levels - 1, &children);
        taffy::Style s = RandomNxNGridStyle(c->arena, &c->rng, c->trackCount);
        VecAppend(*out, c->tree
                            .NewWithChildren(s, children.els, len(children)));
    }
}

static void DeepSetup(GridCase* c) {
    c->Reset();
    Vec<taffy::NodeId> children;
    BuildDeepGridTree(c, c->levels, &children);
    c->root = c->tree
                  .NewWithChildren(taffy::Style{}, children.els, len(children));
}

static void GridRun(GridCase* c) {
    c->tree.ComputeLayout(c->root, c->avail);
    BenchKeep(&c->tree);
}

static void RunGridCase(const char* group, const char* name, int64_t param,
                        const char* unit, void (*setup)(GridCase*),
                        GridCase* c) {
    BenchCase(group, name, unit, param, MkFunc0(setup, c), MkFunc0(GridRun, c));
    c->tree.Free();
    if (c->arena) {
        ArenaDelete(c->arena);
        c->arena = nullptr;
    }
}

static int64_t IPow(int64_t base, int exp) {
    int64_t out = 1;
    for (int i = 0; i < exp; i++) {
        out *= base;
    }
    return out;
}

void BenchGrid() {
    // grid/wide: one n×n grid with a leaf in every cell.
    int wideTracks[3] = {31, 100, 316};
    for (int i = 0; i < 3; i++) {
        GridCase c;
        c.trackCount = wideTracks[i];
        c.avail = SizeAvail::Definite(SizeF{12000.0f, 12000.0f});
        TempStr name = fmt("%dx%d", wideTracks[i], wideTracks[i]);
        RunGridCase("grid/wide", name.s, (int64_t)wideTracks[i] * wideTracks[i],
                    "cells", FlatSetup, &c);
    }

    // grid/deep: grids of grids. Rust's fourth pair, (3, 5), is commented out
    // there and is left out here.
    int deepTracks[3] = {2, 3, 2};
    int deepLevels[3] = {5, 4, 7};
    for (int i = 0; i < 3; i++) {
        GridCase c;
        c.trackCount = deepTracks[i];
        c.levels = deepLevels[i];
        c.avail = SizeAvail::Definite(SizeF{12000.0f, 12000.0f});
        TempStr name = fmt("%dx%d", deepTracks[i], deepTracks[i]);
        int64_t leaves =
            IPow((int64_t)deepTracks[i] * deepTracks[i], deepLevels[i]);
        RunGridCase("grid/deep", name.s, leaves, "leaves", DeepSetup, &c);
    }

    // grid/superdeep: a 1×1 grid nested into itself, sized by its content.
    int superLevels[2] = {100, 1000};
    for (int i = 0; i < 2; i++) {
        GridCase c;
        c.trackCount = 1;
        c.levels = superLevels[i];
        c.avail = SizeAvail::MaxContent();
        RunGridCase("grid/superdeep", "1x1", superLevels[i], "levels",
                    DeepSetup, &c);
    }
}
