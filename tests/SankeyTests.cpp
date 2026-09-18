/* Ported from crates/ui/src/plot/shape/sankey.rs, whose tests are d3-sankey's
 * behaviour written down: the columns a graph resolves to under each
 * alignment, the heights the flow values buy, the ribbons stacking to cover
 * both sides of every node, the centring and the stagger, and the two ways a
 * graph can be refused. */

#include "Test.h"

#include <math.h>

static const float kSankeyEps = 1e-3f;

static bool Near(float a, float b) {
    float d = a - b;
    return (d < 0 ? -d : d) < kSankeyEps;
}

static SankeyLink L(int source, int target, double value) {
    SankeyLink l;
    l.source = source;
    l.target = target;
    l.value = value;
    return l;
}

// test_sankey_builder: the defaults are d3's.
static void TheGeneratorStartsWithD3sDefaults() {
    Sankey s;
    utassertnear(s.nodeWidth, 24.f);
    utassertnear(s.nodePadding, 8.f);
    utassert(s.align == SankeyAlign::Justify);
    utassert(s.iterations == 6);
    utassert(s.valueScale == SankeyValueScale::Linear);
    utassertnear(s.x0, 0.f);
    utassertnear(s.y0, 0.f);
    utassertnear(s.x1, 1.f);
    utassertnear(s.y1, 1.f);
}

// test_sankey_layout_chain: A -> B -> C, every node carrying the whole flow.
static void AChainFillsEveryColumn() {
    Sankey s;
    s.nodeWidth = 10;
    s.x1 = 100;
    s.y1 = 100;
    SankeyLink links[] = {L(0, 1, 5), L(1, 2, 5)};
    SankeyGraph g;
    utassert(SankeyLayout(&s, 3, links, 2, &g) == SankeyError::None);

    utassert(g.nodes[0].depth == 0 && g.nodes[1].depth == 1 &&
             g.nodes[2].depth == 2);
    utassert(g.nodes[0].height == 2 && g.nodes[1].height == 1 &&
             g.nodes[2].height == 0);
    utassert(g.nodes[0].layer == 0 && g.nodes[1].layer == 1 &&
             g.nodes[2].layer == 2);
    utassert(SankeyLayerCount(&g) == 3);

    utassertnear(g.nodes[0].x0, 0.f);
    utassertnear(g.nodes[1].x0, 45.f);
    utassertnear(g.nodes[2].x0, 90.f);
    for (int i = 0; i < g.nodes.len; i++) {
        utassertnear(g.nodes[i].x1 - g.nodes[i].x0, 10.f);
        utassert(g.nodes[i].value == 5.);
        // Every node carries the whole flow, so each spans the height.
        utassert(Near(g.nodes[i].y1 - g.nodes[i].y0, 100.f));
    }
    for (int i = 0; i < g.links.len; i++) {
        utassert(Near(g.links[i].width, 100.f));
        // The chain balances, so both ends of a ribbon span their nodes.
        utassert(Near(g.links[i].sourceWidth, 100.f));
        utassert(Near(g.links[i].targetWidth, 100.f));
    }

    // The topology alone agrees with the full layout on the fields that do
    // not depend on the extent.
    SankeyGraph topo;
    utassert(SankeyTopology(&s, 3, links, 2, &topo) == SankeyError::None);
    utassert(SankeyLayerCount(&topo) == 3);
    for (int i = 0; i < topo.nodes.len; i++) {
        utassert(topo.nodes[i].depth == g.nodes[i].depth);
        utassert(topo.nodes[i].height == g.nodes[i].height);
        utassert(topo.nodes[i].layer == g.nodes[i].layer);
        utassert(topo.nodes[i].value == g.nodes[i].value);
        utassertnear(topo.nodes[i].x0, g.nodes[i].x0);
    }

    // And a topology taken on the unit extent, completed on the real one —
    // the chart's two-pass flow — lands where a direct layout does.
    Sankey unit;
    unit.nodeWidth = 10;
    SankeyGraph second;
    utassert(SankeyTopology(&unit, 3, links, 2, &second) == SankeyError::None);
    SankeyLayoutFrom(&s, &second);
    for (int i = 0; i < second.nodes.len; i++) {
        utassertnear(second.nodes[i].x0, g.nodes[i].x0);
        utassertnear(second.nodes[i].y0, g.nodes[i].y0);
        utassertnear(second.nodes[i].x1, g.nodes[i].x1);
        utassertnear(second.nodes[i].y1, g.nodes[i].y1);
    }
    for (int i = 0; i < second.links.len; i++) {
        utassertnear(second.links[i].y0, g.links[i].y0);
        utassertnear(second.links[i].y1, g.links[i].y1);
        utassertnear(second.links[i].width, g.links[i].width);
    }
}

// test_sankey_topology_large_chain: fifty thousand nodes in a line, which is
// what the link lists being slices of one array is for.
static void ALongChainStillResolves() {
    const int kCount = 50000;
    Vec<SankeyLink> links;
    for (int i = 0; i < kCount - 1; i++) {
        VecAppend(links, L(i, i + 1, 1));
    }
    Sankey s;
    SankeyGraph g;
    utassert(SankeyTopology(&s, kCount, links.els, len(links), &g) ==
             SankeyError::None);
    utassert(g.nodes[0].height == kCount - 1);
    utassert(g.nodes[kCount - 1].depth == kCount - 1);
}

// test_sankey_alignment: A -> B -> C with a short branch A -> D, which is the
// case the four alignments answer differently.
static void EachAlignmentPutsTheShortBranchSomewhereElse() {
    SankeyLink links[] = {L(0, 1, 1), L(1, 2, 1), L(0, 3, 1)};
    const SankeyAlign kAligns[] = {SankeyAlign::Left, SankeyAlign::Right,
                                   SankeyAlign::Justify, SankeyAlign::Center};
    const int kWant[4][4] = {
        {0, 1, 2, 1}, {0, 1, 2, 2}, {0, 1, 2, 2}, {0, 1, 2, 1}};
    for (int a = 0; a < 4; a++) {
        Sankey s;
        s.align = kAligns[a];
        s.x1 = 100;
        s.y1 = 100;
        SankeyGraph g;
        utassert(SankeyLayout(&s, 4, links, 3, &g) == SankeyError::None);
        for (int i = 0; i < 4; i++) {
            utassert(g.nodes[i].layer == kWant[a][i]);
        }
    }
}

// test_sankey_link_offsets: one source fanning out into two targets.
static void RibbonsStackInsideTheNodeTheyLeave() {
    Sankey s;
    s.nodeWidth = 10;
    s.x1 = 100;
    s.y1 = 100;
    SankeyLink links[] = {L(0, 1, 30), L(0, 2, 10)};
    SankeyGraph g;
    utassert(SankeyLayout(&s, 3, links, 2, &g) == SankeyError::None);

    float sourceHeight = g.nodes[0].y1 - g.nodes[0].y0;
    float total = g.links[0].width + g.links[1].width;
    utassert(Near(total, sourceHeight));
    // The widths follow the values.
    utassert(Near(g.links[0].width / g.links[1].width, 3.f));

    // The outgoing ribbons stack without a gap inside the source.
    const SankeyLinkLayout* first = &g.links[0];
    const SankeyLinkLayout* second = &g.links[1];
    if (second->y0 < first->y0) {
        const SankeyLinkLayout* t = first;
        first = second;
        second = t;
    }
    utassert(Near(first->y0 - first->sourceWidth / 2.f, g.nodes[0].y0));
    utassert(Near(first->y0 + first->sourceWidth / 2.f,
                  second->y0 - second->sourceWidth / 2.f));

    // Each target has one incoming ribbon, filling it.
    for (int i = 0; i < g.links.len; i++) {
        const SankeyNodeLayout& target = g.nodes[g.links[i].target];
        utassert(Near(g.links[i].y1 - g.links[i].targetWidth / 2.f, target.y0));
        utassert(Near(g.links[i].y1 + g.links[i].targetWidth / 2.f, target.y1));
    }
}

// test_sankey_imbalanced_link_widths: A -> B carries 10 and B -> C carries 7,
// so the one ribbon out of B is as tall as B at one end and as tall as C at
// the other.
static void AnImbalancedRibbonWidensAcross() {
    Sankey s;
    s.x1 = 100;
    s.y1 = 100;
    SankeyLink links[] = {L(0, 1, 10), L(1, 2, 7)};
    SankeyGraph g;
    utassert(SankeyLayout(&s, 3, links, 2, &g) == SankeyError::None);

    const SankeyNodeLayout& b = g.nodes[1];
    const SankeyNodeLayout& c = g.nodes[2];
    const SankeyLinkLayout& out = g.links[1];
    utassert(Near(out.sourceWidth, b.y1 - b.y0));
    utassert(Near(out.targetWidth, c.y1 - c.y0));
    utassert(out.sourceWidth > out.targetWidth);
    // Both ends stay centred on the node they meet.
    utassert(Near(out.y0, (b.y0 + b.y1) / 2.f));
    utassert(Near(out.y1, (c.y0 + c.y1) / 2.f));
}

// test_sankey_sqrt_scale_fills_nodes: the compression changes the heights and
// still covers every node.
static void TheSqrtScaleStillFillsEveryNode() {
    Sankey s;
    s.valueScale = SankeyValueScale::Sqrt;
    s.x1 = 100;
    s.y1 = 100;
    SankeyLink links[] = {L(0, 1, 90), L(1, 2, 40), L(1, 3, 50)};
    SankeyGraph g;
    utassert(SankeyLayout(&s, 4, links, 3, &g) == SankeyError::None);

    for (int i = 0; i < g.nodes.len; i++) {
        const SankeyNodeLayout& node = g.nodes[i];
        float h = node.y1 - node.y0;
        float incoming = 0;
        for (int k = 0; k < node.tgtCount; k++) {
            incoming += g.links[g.tgtLinks[node.tgtStart + k]].targetWidth;
        }
        float outgoing = 0;
        for (int k = 0; k < node.srcCount; k++) {
            outgoing += g.links[g.srcLinks[node.srcStart + k]].sourceWidth;
        }
        if (node.tgtCount > 0) {
            utassert(Near(incoming, h));
        }
        if (node.srcCount > 0) {
            utassert(Near(outgoing, h));
        }
    }

    // The two leaves show the compression: their heights are in the ratio of
    // the square roots, not of the values.
    float ratio =
        (g.nodes[3].y1 - g.nodes[3].y0) / (g.nodes[2].y1 - g.nodes[2].y0);
    float want = (float)sqrt(50.0 / 40.0);
    utassert((ratio - want < 0.02f) && (want - ratio < 0.02f));
}

// test_sankey_value_conservation: a node takes the larger of the two sides,
// and nothing leaves the extent.
static void ANodeTakesTheLargerOfItsTwoSides() {
    Sankey s;
    s.x1 = 100;
    s.y1 = 100;
    SankeyLink links[] = {L(0, 1, 10), L(1, 2, 7)};
    SankeyGraph g;
    utassert(SankeyLayout(&s, 3, links, 2, &g) == SankeyError::None);
    utassert(g.nodes[1].value == 10.);
    for (int i = 0; i < g.nodes.len; i++) {
        utassert(g.nodes[i].y0 <= g.nodes[i].y1);
        utassert(g.nodes[i].y0 >= -kSankeyEps);
        utassert(g.nodes[i].y1 <= 100.f + kSankeyEps);
    }
}

// test_sankey_vertical_centering: every column's midpoint is the extent's.
static void EveryColumnIsCentredInTheExtent() {
    Sankey s;
    s.nodePadding = 20;
    s.x0 = 0;
    s.y0 = 10;
    s.x1 = 100;
    s.y1 = 90;
    SankeyLink links[] = {L(0, 2, 40), L(1, 2, 10), L(2, 3, 25), L(2, 4, 25)};
    SankeyGraph g;
    utassert(SankeyLayout(&s, 5, links, 4, &g) == SankeyError::None);

    int layers = SankeyLayerCount(&g);
    for (int l = 0; l < layers; l++) {
        bool seen = false;
        float lo = 0;
        float hi = 0;
        for (int i = 0; i < g.nodes.len; i++) {
            if (g.nodes[i].layer != l) {
                continue;
            }
            if (!seen) {
                seen = true;
                lo = g.nodes[i].y0;
                hi = g.nodes[i].y1;
                continue;
            }
            if (g.nodes[i].y0 < lo) {
                lo = g.nodes[i].y0;
            }
            if (g.nodes[i].y1 > hi) {
                hi = g.nodes[i].y1;
            }
        }
        // The extent is [10, 90], so every column is centred on 50.
        utassert(seen && Near((lo + hi) / 2.f, 50.f));
    }
    for (int i = 0; i < g.nodes.len; i++) {
        utassert(g.nodes[i].y0 >= 10.f - kSankeyEps);
        utassert(g.nodes[i].y1 <= 90.f + kSankeyEps);
    }
}

// test_sankey_stagger_flat_columns: two equal single-node columns feeding a
// fan-out, so the pair is nudged apart and the ribbon between them curves.
static void EqualSingleNodeColumnsAreStaggered() {
    Sankey s;
    s.nodePadding = 20;
    s.x1 = 100;
    s.y1 = 100;
    SankeyLink links[] = {L(0, 1, 100), L(1, 2, 40), L(1, 3, 30), L(1, 4, 20),
                          L(1, 5, 10)};
    SankeyGraph g;
    utassert(SankeyLayout(&s, 6, links, 5, &g) == SankeyError::None);

    float c0 = (g.nodes[0].y0 + g.nodes[0].y1) / 2.f;
    float c1 = (g.nodes[1].y0 + g.nodes[1].y1) / 2.f;
    utassert(!Near(c0, c1));
    for (int i = 0; i < g.nodes.len; i++) {
        utassert(g.nodes[i].y0 >= -kSankeyEps);
        utassert(g.nodes[i].y1 <= 100.f + kSankeyEps);
    }

    // After the stagger's per-layer shift every ribbon end is still attached
    // to its own node — which is what a source/target mix-up in the shift
    // would break.
    for (int i = 0; i < g.nodes.len; i++) {
        const SankeyNodeLayout& node = g.nodes[i];
        float y = node.y0;
        for (int k = 0; k < node.srcCount; k++) {
            const SankeyLinkLayout& link =
                g.links[g.srcLinks[node.srcStart + k]];
            utassert(Near(link.y0, y + link.sourceWidth / 2.f));
            y += link.sourceWidth;
        }
        y = node.y0;
        for (int k = 0; k < node.tgtCount; k++) {
            const SankeyLinkLayout& link =
                g.links[g.tgtLinks[node.tgtStart + k]];
            utassert(Near(link.y1, y + link.targetWidth / 2.f));
            y += link.targetWidth;
        }
    }
}

// test_sankey_circular_link: the two graphs there is no layout for.
static void ACycleAndAMissingNodeAreRefused() {
    Sankey s;
    s.x1 = 100;
    s.y1 = 100;
    SankeyGraph g;

    SankeyLink loop[] = {L(0, 1, 1), L(1, 0, 1)};
    utassert(SankeyLayout(&s, 2, loop, 2, &g) == SankeyError::CircularLink);

    SankeyLink self[] = {L(0, 0, 1)};
    utassert(SankeyLayout(&s, 1, self, 1, &g) == SankeyError::CircularLink);

    SankeyLink missing[] = {L(0, 5, 1)};
    utassert(SankeyLayout(&s, 2, missing, 1, &g) == SankeyError::MissingNode);
    utassert(g.errNode == 5);
}

// test_sankey_degenerate: nothing here may divide by zero.
static void TheDegenerateGraphsStayFinite() {
    Sankey s;
    s.x1 = 100;
    s.y1 = 100;

    // Nothing at all.
    SankeyGraph empty;
    utassert(SankeyLayout(&s, 0, nullptr, 0, &empty) == SankeyError::None);
    utassert(empty.nodes.len == 0 && empty.links.len == 0);
    utassert(SankeyLayerCount(&empty) == 0);

    // A flow of zero must not come out as a height of NaN.
    SankeyLink zero[] = {L(0, 1, 0)};
    SankeyGraph g;
    utassert(SankeyLayout(&s, 2, zero, 1, &g) == SankeyError::None);
    for (int i = 0; i < g.nodes.len; i++) {
        utassert(Near(g.nodes[i].y1 - g.nodes[i].y0, 0.f));
        utassert(g.nodes[i].x0 == g.nodes[i].x0);
        utassert(g.nodes[i].y0 == g.nodes[i].y0);
        utassert(g.nodes[i].y1 == g.nodes[i].y1);
    }

    // Nodes with no links at all collapse into one column.
    SankeyGraph lone;
    utassert(SankeyLayout(&s, 2, nullptr, 0, &lone) == SankeyError::None);
    utassert(SankeyLayerCount(&lone) == 1);
    for (int i = 0; i < lone.nodes.len; i++) {
        utassertnear(lone.nodes[i].x0, 0.f);
        utassert(lone.nodes[i].y0 == lone.nodes[i].y0);
    }
}

void TestSankey() {
    TestSuite("sankey");
    TheGeneratorStartsWithD3sDefaults();
    AChainFillsEveryColumn();
    ALongChainStillResolves();
    EachAlignmentPutsTheShortBranchSomewhereElse();
    RibbonsStackInsideTheNodeTheyLeave();
    AnImbalancedRibbonWidensAcross();
    TheSqrtScaleStillFillsEveryNode();
    ANodeTakesTheLargerOfItsTwoSides();
    EveryColumnIsCentredInTheExtent();
    EqualSingleNodeColumnsAreStaggered();
    ACycleAndAMissingNodeAreRefused();
    TheDegenerateGraphsStayFinite();
}
