/* Port of taffy's benches/src/lib.rs: the random source and the tree shapes
   the benchmarks are built from.

   Rust states these as the `BuildTree` and `BuildTreeExt` traits so that one
   benchmark can build the same shape for taffy, for taffy 0.3 and for Yoga
   and time all three. There is one tree here, so the traits collapse into
   `TreeBuilder`, and the method names are the trait method names. */

#include "Bench.h"

#include <stdio.h>
#include <stdlib.h>

using namespace taffy;

const uint64_t kStandardRngSeed = 12345;

// ─── BenchRng ────────────────────────────────────────────────────────────

// rand_chacha 0.9's ChaCha8Rng: Bernstein's 64-bit counter layout, 8 rounds
// (4 double-rounds), a 4-block window. rand_core's `seed_from_u64` fills the
// 32-byte key with PCG32; `BlockRng` hands out the window as little-endian
// u32s. See the note in Bench.h.

static uint32_t Rotl32(uint32_t x, int n) {
    return (x << n) | (x >> (32 - n));
}

static void QuarterRound(uint32_t& a, uint32_t& b, uint32_t& c, uint32_t& d) {
    a += b;
    d ^= a;
    d = Rotl32(d, 16);
    c += d;
    b ^= c;
    b = Rotl32(b, 12);
    a += b;
    d ^= a;
    d = Rotl32(d, 8);
    c += d;
    b ^= c;
    b = Rotl32(b, 7);
}

static void ChaChaBlock(uint32_t out[16], const uint32_t in[16]) {
    uint32_t x[16];
    for (int i = 0; i < 16; i++) {
        x[i] = in[i];
    }
    for (int r = 0; r < 4; r++) {
        QuarterRound(x[0], x[4], x[8], x[12]);
        QuarterRound(x[1], x[5], x[9], x[13]);
        QuarterRound(x[2], x[6], x[10], x[14]);
        QuarterRound(x[3], x[7], x[11], x[15]);
        QuarterRound(x[0], x[5], x[10], x[15]);
        QuarterRound(x[1], x[6], x[11], x[12]);
        QuarterRound(x[2], x[7], x[8], x[13]);
        QuarterRound(x[3], x[4], x[9], x[14]);
    }
    for (int i = 0; i < 16; i++) {
        out[i] = x[i] + in[i];
    }
}

void BenchRng::Seed(uint64_t seed) {
    // rand_core::SeedableRng::seed_from_u64: PCG32 into 8 little-endian
    // words. The multiply and increment are the published PCG constants;
    // the output mix is rotate-right of the xorshift.
    uint64_t state = seed;
    for (int i = 0; i < 8; i++) {
        state = state * 6364136223846793005ULL + 11634580027462260723ULL;
        uint32_t xorshifted = (uint32_t)(((state >> 18) ^ state) >> 27);
        uint32_t rot = (uint32_t)(state >> 59);
        key[i] = (xorshifted >> rot) | (xorshifted << ((0u - rot) & 31));
    }
    blockPos = 0;
    index = 64;
}

uint32_t BenchRng::NextU32() {
    if (index >= 64) {
        uint32_t in[16];
        in[0] = 0x61707865u;
        in[1] = 0x3320646eu;
        in[2] = 0x79622d32u;
        in[3] = 0x6b206574u;
        for (int i = 0; i < 8; i++) {
            in[4 + i] = key[i];
        }
        in[14] = 0;
        in[15] = 0;
        for (int b = 0; b < 4; b++) {
            uint64_t pos = blockPos + (uint64_t)b;
            in[12] = (uint32_t)pos;
            in[13] = (uint32_t)(pos >> 32);
            ChaChaBlock(buf + b * 16, in);
        }
        blockPos += 4;
        index = 0;
    }
    return buf[index++];
}

float BenchRng::NextFloat() {
    // rand's StandardUniform for f32: 24 significand bits in [0, 1).
    return (float)(NextU32() >> 8) * (1.0f / 16777216.0f);
}

float BenchRng::Range(float lo, float hi) {
    // UniformFloat::sample_single_inclusive: u32 >> 9 into [1, 2), then
    // scale. Exclusive f32 ranges use the same one-shot path in rand 0.9.
    uint32_t bits = (NextU32() >> 9) | 0x3f800000u;
    float value0_1 = __builtin_bit_cast(float, bits) - 1.0f;
    return value0_1 * (hi - lo) + lo;
}

int BenchRng::RangeInt(int lo, int hi) {
    // UniformInt<u32>::sample_single_inclusive, Canon's method (rand 0.9
    // without the `unbiased` feature). Widening multiply, then a second
    // sample when the low word sits in the overflowing tail.
    uint32_t low = (uint32_t)lo;
    uint32_t high = (uint32_t)hi;
    uint32_t range = high - low + 1;
    if (range == 0) {
        return (int)NextU32();
    }
    uint64_t prod = (uint64_t)NextU32() * (uint64_t)range;
    uint32_t result = (uint32_t)(prod >> 32);
    uint32_t loOrder = (uint32_t)prod;
    if (loOrder > (0u - range)) {
        uint64_t prod2 = (uint64_t)NextU32() * (uint64_t)range;
        uint32_t newHi = (uint32_t)(prod2 >> 32);
        if (loOrder + newHi < loOrder) {
            result += 1;
        }
    }
    return lo + (int)result;
}

void BenchRngCheck() {
    // Goldens from rand_chacha 0.9 + rand 0.9, ChaCha8Rng::seed_from_u64(12345)
    // and from_seed([0; 32]), dumped by
    // .work/taffy/benches/examples/rng_dump.rs.
    static const uint32_t kU32[16] = {
        0xab5953d3u, 0x12de4745u, 0x77f00e64u, 0x0ade5e20u,
        0x68d190adu, 0xca326f89u, 0x38d05dd6u, 0x555b1519u,
        0x6a33652cu, 0x16a5dbbfu, 0xa92d961eu, 0x2cf4528bu,
        0x8c20306au, 0xb6efa69eu, 0x08919e24u, 0x5f44015bu,
    };
    BenchRng rng;
    rng.Seed(12345);
    for (int i = 0; i < 16; i++) {
        uint32_t got = rng.NextU32();
        if (got != kU32[i]) {
            fprintf(stderr, "BenchRng NextU32[%d] = %08x, want %08x\n", i, got,
                    kU32[i]);
            exit(1);
        }
    }

    rng.Seed(12345);
    float f01 = rng.Range(0.0f, 1.0f);
    float f500 = rng.Range(0.0f, 500.0f);
    float f01ex = rng.Range(0.0f, 1.0f);
    int i14 = rng.RangeInt(1, 4);
    int us14 = rng.RangeInt(1, 4);
    if (__builtin_bit_cast(uint32_t, f01) != 0x3f2b5952u ||
        __builtin_bit_cast(uint32_t, f500) != 0x42136883u ||
        __builtin_bit_cast(uint32_t, f01ex) != 0x3eefe01cu || i14 != 1 ||
        us14 != 2) {
        fprintf(stderr, "BenchRng sampling does not match rand 0.9\n");
        exit(1);
    }

    BenchRng zeros;
    static const uint32_t kZero[4] = {0x2fef003eu, 0xd6405f89u, 0xe8b85b7fu,
                                      0xa1a5091fu};
    for (int i = 0; i < 4; i++) {
        uint32_t got = zeros.NextU32();
        if (got != kZero[i]) {
            fprintf(stderr,
                    "BenchRng zero-seed NextU32[%d] = %08x, want %08x\n", i,
                    got, kZero[i]);
            exit(1);
        }
    }
}

// ─── TreeBuilder ─────────────────────────────────────────────────────────

void TreeBuilder::Init(const StyleGen& styleGen, int capacity) {
    tree.Init(capacity);
    rng.Seed(kStandardRngSeed);
    gen = styleGen;
    taffy::Style rootStyle;
    if (gen.root) {
        rootStyle = gen.root(&rng, gen.ud);
    }
    root = tree.NewLeaf(rootStyle);
}

void TreeBuilder::Free() {
    tree.Free();
}

taffy::NodeId TreeBuilder::CreateLeafNode() {
    return tree.NewLeaf(gen.leaf(&rng, gen.ud));
}

taffy::NodeId TreeBuilder::CreateContainerNode(const taffy::NodeId* children,
                                               int n) {
    return tree.NewWithChildren(gen.container(&rng, gen.ud), children, n);
}

void TreeBuilder::SetRootChildren(const taffy::NodeId* children, int n) {
    tree.SetChildren(root, children, n);
}

void TreeBuilder::BuildDeepTree(uint32_t maxNodes, uint32_t branchingFactor,
                                Vec<taffy::NodeId>* out) {
    if (maxNodes <= branchingFactor) {
        for (uint32_t i = 0; i < maxNodes; i++) {
            VecAppend(*out, CreateLeafNode());
        }
        return;
    }

    // Another layer, with the remaining nodes split evenly between the
    // children of this one.
    for (uint32_t i = 0; i < branchingFactor; i++) {
        uint32_t sub = (maxNodes - branchingFactor) / branchingFactor;
        Vec<taffy::NodeId> children;
        BuildDeepTree(sub, branchingFactor, &children);
        VecAppend(*out, CreateContainerNode(children.els, len(children)));
    }
}

void TreeBuilder::BuildDeepHierarchy(uint32_t nodeCount,
                                     uint32_t branchingFactor) {
    Vec<taffy::NodeId> children;
    BuildDeepTree(nodeCount, branchingFactor, &children);
    SetRootChildren(children.els, len(children));
}

void TreeBuilder::BuildFlatHierarchy(uint32_t targetNodeCount) {
    Vec<taffy::NodeId> children;
    while ((uint32_t)TotalNodeCount() < targetNodeCount) {
        int count = rng.RangeInt(1, 4);
        Vec<taffy::NodeId> sub;
        for (int i = 0; i < count; i++) {
            VecAppend(sub, CreateLeafNode());
        }
        VecAppend(children, CreateContainerNode(sub.els, len(sub)));
    }
    SetRootChildren(children.els, len(children));
}

void TreeBuilder::BuildSuperDeepHierarchy(uint32_t depth,
                                          uint32_t nodesPerLevel) {
    Vec<taffy::NodeId> children;
    for (uint32_t i = 0; i < depth; i++) {
        taffy::NodeId nodeWithChildren =
            CreateContainerNode(children.els, len(children));
        children.len = 0;
        VecAppend(children, nodeWithChildren);
        for (uint32_t j = 0; j + 1 < nodesPerLevel; j++) {
            VecAppend(children, CreateLeafNode());
        }
    }
    SetRootChildren(children.els, len(children));
}

void TreeBuilder::ComputeLayout(Optf availableWidth, Optf availableHeight) {
    SizeFOpt avail = SizeFOptNone();
    avail.w = availableWidth;
    avail.h = availableHeight;
    tree.ComputeLayout(root, SizeAvail::From(avail));
}
