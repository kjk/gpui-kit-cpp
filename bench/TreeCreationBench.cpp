/* Port of taffy's benches/benches/tree_creation.rs: building the tree, with
   no layout run at all.

   This is the one benchmark where the tree construction is what is timed, so
   it uses criterion's plain `iter` rather than `iter_batched` and the setup
   here does nothing but drop what the last sample built.

   Rust's two rows are `TaffyTree::new` and `TaffyTree::with_capacity`. The
   C++ tree has one `Init(capacity)` whose default is 16, so the pair is
   `Init()` against `Init(nodeCount)`. */

#include "Bench.h"

using namespace taffy;

struct CreationCase {
    TaffyTree tree;
    BenchRng rng;
    uint32_t nodeCount = 0;
    // Whether the tree is told up front how many nodes are coming.
    bool withCapacity = false;
};

static void CreationSetup(CreationCase* c) {
    c->tree.Free();
}

// Rust's `build_taffy_flat_hierarchy`. The node count it counts up to is its
// own tally, not the tree's, so the root is not included — as in Rust.
static void CreationRun(CreationCase* c) {
    c->tree.Init(c->withCapacity ? (int)c->nodeCount : 16);
    c->rng.Seed(kStandardRngSeed);

    Vec<taffy::NodeId> children;
    uint32_t nodeCount = 0;
    while (nodeCount < c->nodeCount) {
        int subCount = c->rng.RangeInt(1, 4);
        Vec<taffy::NodeId> sub;
        for (int i = 0; i < subCount; i++) {
            VecAppend(sub, c->tree.NewLeaf(taffy::Style{}));
        }
        VecAppend(children,
                  c->tree.NewWithChildren(taffy::Style{}, sub.els, len(sub)));
        nodeCount += 1 + (uint32_t)subCount;
    }

    taffy::NodeId root =
        c->tree.NewWithChildren(taffy::Style{}, children.els, len(children));
    BenchKeep(&c->tree);
    BenchKeep(&root);
}

void BenchTreeCreation() {
    uint32_t counts[3] = {1000, 10000, 100000};
    for (int i = 0; i < 3; i++) {
        for (int withCapacity = 0; withCapacity < 2; withCapacity++) {
            CreationCase c;
            c.nodeCount = counts[i];
            c.withCapacity = withCapacity != 0;
            BenchCase("tree creation",
                      c.withCapacity ? "Init(nodeCount)" : "Init()", "nodes",
                      counts[i], MkFunc0(CreationSetup, &c),
                      MkFunc0(CreationRun, &c));
            c.tree.Free();
        }
    }
}
