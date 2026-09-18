#include "base/sankey.h"

#include <math.h>

namespace gpui {

// STAGGER_RATIO: how far a run of equal single-node columns is nudged off the
// centre line, as a fraction of the node's height, so the ribbon between two
// of them curves instead of running flat.
static const float kSankeyStaggerRatio = 0.15f;

int SankeyLayerCount(const SankeyGraph* g) {
    int n = 0;
    for (int i = 0; i < g->nodes.len; i++) {
        int layer = g->nodes[i].layer + 1;
        if (layer > n) {
            n = layer;
        }
    }
    return n;
}

static double ScaleValue(SankeyValueScale scale, double v) {
    if (scale == SankeyValueScale::Linear) {
        return v;
    }
    // Guard against tiny negatives from bad data.
    return sqrt(v > 0 ? v : 0);
}

// One node's slice of a link list, sorted by the y0 of the node at the other
// end, with the link index breaking a tie — which makes the order total, so
// the answer does not depend on the sort.
static void SortLinkSlice(SankeyGraph* g, int* items, int n, bool byTarget) {
    for (int i = 1; i < n; i++) {
        int v = items[i];
        float vy = byTarget ? g->nodes[g->links[v].target].y0
                            : g->nodes[g->links[v].source].y0;
        int j = i - 1;
        while (j >= 0) {
            int u = items[j];
            float uy = byTarget ? g->nodes[g->links[u].target].y0
                                : g->nodes[g->links[u].source].y0;
            if (uy < vy || (uy == vy && u < v)) {
                break;
            }
            items[j + 1] = items[j];
            j--;
        }
        items[j + 1] = v;
    }
}

static void SortSourceLinks(SankeyGraph* g, int index) {
    const SankeyNodeLayout& node = g->nodes[index];
    SortLinkSlice(g, &g->srcLinks[node.srcStart], node.srcCount, true);
}

static void SortTargetLinks(SankeyGraph* g, int index) {
    const SankeyNodeLayout& node = g->nodes[index];
    SortLinkSlice(g, &g->tgtLinks[node.tgtStart], node.tgtCount, false);
}

// reorderNodeLinks: after a node moved, the neighbours on the other end of
// each of its links have to be sorted again.
static void ReorderNodeLinks(SankeyGraph* g, int index) {
    for (int i = 0; i < g->nodes[index].tgtCount; i++) {
        int link = g->tgtLinks[g->nodes[index].tgtStart + i];
        SortSourceLinks(g, g->links[link].source);
    }
    for (int i = 0; i < g->nodes[index].srcCount; i++) {
        int link = g->srcLinks[g->nodes[index].srcStart + i];
        SortTargetLinks(g, g->links[link].target);
    }
}

// The link lists, as one array each with a slice per node.
static void ComputeNodeLinks(SankeyGraph* g) {
    int n = g->nodes.len;
    int m = g->links.len;
    VecReset(g->srcLinks);
    VecReset(g->tgtLinks);
    if (m > 0) {
        VecAppendBlanks(g->srcLinks, m);
        VecAppendBlanks(g->tgtLinks, m);
    }
    // Count, then the running start of each node's slice, then fill.
    for (int i = 0; i < n; i++) {
        g->nodes[i].srcCount = 0;
        g->nodes[i].tgtCount = 0;
    }
    for (int i = 0; i < m; i++) {
        g->nodes[g->links[i].source].srcCount++;
        g->nodes[g->links[i].target].tgtCount++;
    }
    int srcAt = 0;
    int tgtAt = 0;
    for (int i = 0; i < n; i++) {
        g->nodes[i].srcStart = srcAt;
        srcAt += g->nodes[i].srcCount;
        g->nodes[i].tgtStart = tgtAt;
        tgtAt += g->nodes[i].tgtCount;
    }
    // The counts are used again as the fill cursor, then put back.
    for (int i = 0; i < n; i++) {
        g->nodes[i].srcCount = 0;
        g->nodes[i].tgtCount = 0;
    }
    for (int i = 0; i < m; i++) {
        SankeyNodeLayout& s = g->nodes[g->links[i].source];
        g->srcLinks[s.srcStart + s.srcCount] = i;
        s.srcCount++;
        SankeyNodeLayout& t = g->nodes[g->links[i].target];
        g->tgtLinks[t.tgtStart + t.tgtCount] = i;
        t.tgtCount++;
    }
}

// A node's throughput: the larger of what comes in and what goes out.
static void ComputeNodeValues(SankeyGraph* g) {
    for (int i = 0; i < g->nodes.len; i++) {
        double outgoing = 0;
        for (int k = 0; k < g->nodes[i].srcCount; k++) {
            outgoing += g->links[g->srcLinks[g->nodes[i].srcStart + k]].value;
        }
        double incoming = 0;
        for (int k = 0; k < g->nodes[i].tgtCount; k++) {
            incoming += g->links[g->tgtLinks[g->nodes[i].tgtStart + k]].value;
        }
        g->nodes[i].value = outgoing > incoming ? outgoing : incoming;
    }
}

// The longest path from any source (depth) and to any sink (height), out of
// one topological ordering. A node the ordering never reached has an incoming
// link that never resolved, which is a cycle.
static SankeyError ComputeNodeRanks(SankeyGraph* g) {
    int n = g->nodes.len;
    Vec<int> incoming;
    Vec<int> order;
    Vec<int> depths;
    Vec<int> heights;
    if (n > 0) {
        VecAppendBlanks(incoming, n);
        VecAppendBlanks(depths, n);
        VecAppendBlanks(heights, n);
    }
    for (int i = 0; i < n; i++) {
        incoming[i] = g->nodes[i].tgtCount;
        depths[i] = 0;
        heights[i] = 0;
        if (incoming[i] == 0) {
            VecAppend(order, i);
        }
    }

    int visited = 0;
    while (visited < len(order)) {
        int index = order[visited];
        visited++;
        int depth = depths[index] + 1;
        for (int k = 0; k < g->nodes[index].srcCount; k++) {
            int link = g->srcLinks[g->nodes[index].srcStart + k];
            int target = g->links[link].target;
            if (depths[target] < depth) {
                depths[target] = depth;
            }
            incoming[target]--;
            if (incoming[target] == 0) {
                VecAppend(order, target);
            }
        }
    }
    if (len(order) != n) {
        return SankeyError::CircularLink;
    }

    // Walking the order backwards means a target's height is already final
    // when its sources are reached.
    for (int i = len(order) - 1; i >= 0; i--) {
        int index = order[i];
        for (int k = 0; k < g->nodes[index].srcCount; k++) {
            int link = g->srcLinks[g->nodes[index].srcStart + k];
            int h = heights[g->links[link].target] + 1;
            if (heights[index] < h) {
                heights[index] = h;
            }
        }
    }
    for (int i = 0; i < n; i++) {
        g->nodes[i].depth = depths[i];
        g->nodes[i].height = heights[i];
    }
    return SankeyError::None;
}

static int AlignLayer(const Sankey* s, const SankeyGraph* g, int index, int n) {
    const SankeyNodeLayout& node = g->nodes[index];
    switch (s->align) {
        case SankeyAlign::Left:
            return node.depth;
        case SankeyAlign::Right:
            return n - 1 - node.height;
        case SankeyAlign::Justify:
            // A node with nothing leaving it belongs in the last column.
            return node.srcCount == 0 ? n - 1 : node.depth;
        case SankeyAlign::Center:
        default:
            if (node.tgtCount != 0) {
                return node.depth;
            }
            if (node.srcCount != 0) {
                // One column before the earliest node it feeds.
                int least = 1;
                bool any = false;
                for (int k = 0; k < node.srcCount; k++) {
                    int link = g->srcLinks[node.srcStart + k];
                    int d = g->nodes[g->links[link].target].depth;
                    if (!any || d < least) {
                        least = d;
                        any = true;
                    }
                }
                return least > 0 ? least - 1 : 0;
            }
            return 0;
    }
}

static void ComputeNodeLayers(const Sankey* s, SankeyGraph* g) {
    int n = 0;
    for (int i = 0; i < g->nodes.len; i++) {
        if (g->nodes[i].depth + 1 > n) {
            n = g->nodes[i].depth + 1;
        }
    }
    if (n <= 0) {
        return;
    }
    float kx = n > 1 ? (s->x1 - s->x0 - s->nodeWidth) / (float)(n - 1) : 0.f;
    for (int i = 0; i < g->nodes.len; i++) {
        int layer = AlignLayer(s, g, i, n);
        if (layer > n - 1) {
            layer = n - 1;
        }
        if (layer < 0) {
            layer = 0;
        }
        SankeyNodeLayout& node = g->nodes[i];
        node.layer = layer;
        node.x0 = s->x0 + (float)layer * kx;
        node.x1 = node.x0 + s->nodeWidth;
    }
}

// Every node, and the ends of every link, moved by its layer's offset. Both
// the centring and the stagger come down to this.
static void ApplyLayerOffsets(SankeyGraph* g, const float* offsets) {
    for (int i = 0; i < g->nodes.len; i++) {
        float dy = offsets[g->nodes[i].layer];
        g->nodes[i].y0 += dy;
        g->nodes[i].y1 += dy;
    }
    for (int i = 0; i < g->links.len; i++) {
        g->links[i].y0 += offsets[g->nodes[g->links[i].source].layer];
        g->links[i].y1 += offsets[g->nodes[g->links[i].target].layer];
    }
}

// Push the nodes of a column down, which is d3's
// resolveCollisionsTopToBottom.
static void PushDown(SankeyGraph* g, const int* column, int n, float y,
                     float alpha, float py) {
    for (int i = 0; i < n; i++) {
        SankeyNodeLayout& node = g->nodes[column[i]];
        float dy = (y - node.y0) * alpha;
        if (dy > 1e-6f) {
            node.y0 += dy;
            node.y1 += dy;
        }
        y = node.y1 + py;
    }
}

// And up, which is resolveCollisionsBottomToTop.
static void PushUp(SankeyGraph* g, const int* column, int n, float y,
                   float alpha, float py) {
    for (int i = n - 1; i >= 0; i--) {
        SankeyNodeLayout& node = g->nodes[column[i]];
        float dy = (node.y1 - y) * alpha;
        if (dy > 1e-6f) {
            node.y0 -= dy;
            node.y1 -= dy;
        }
        y = node.y0 - py;
    }
}

// The y0 the target would want so its ribbon from `source` lines up with the
// slot that ribbon takes in the source's outgoing stack — d3's targetTop.
static float TargetTop(const SankeyGraph* g, int source, int target, float py) {
    const SankeyNodeLayout& sn = g->nodes[source];
    int spread = sn.srcCount > 0 ? sn.srcCount - 1 : 0;
    float y = sn.y0 - (float)spread * py / 2.f;
    for (int k = 0; k < sn.srcCount; k++) {
        const SankeyLinkLayout& link = g->links[g->srcLinks[sn.srcStart + k]];
        if (link.target == target) {
            break;
        }
        y += link.width + py;
    }
    const SankeyNodeLayout& tn = g->nodes[target];
    for (int k = 0; k < tn.tgtCount; k++) {
        const SankeyLinkLayout& link = g->links[g->tgtLinks[tn.tgtStart + k]];
        if (link.source == source) {
            break;
        }
        y -= link.width;
    }
    return y;
}

// The same the other way round — d3's sourceTop.
static float SourceTop(const SankeyGraph* g, int source, int target, float py) {
    const SankeyNodeLayout& tn = g->nodes[target];
    int spread = tn.tgtCount > 0 ? tn.tgtCount - 1 : 0;
    float y = tn.y0 - (float)spread * py / 2.f;
    for (int k = 0; k < tn.tgtCount; k++) {
        const SankeyLinkLayout& link = g->links[g->tgtLinks[tn.tgtStart + k]];
        if (link.source == source) {
            break;
        }
        y += link.width + py;
    }
    const SankeyNodeLayout& sn = g->nodes[source];
    for (int k = 0; k < sn.srcCount; k++) {
        const SankeyLinkLayout& link = g->links[g->srcLinks[sn.srcStart + k]];
        if (link.target == target) {
            break;
        }
        y -= link.width;
    }
    return y;
}

// Each link's centre at both ends, and its width there: the links on one side
// of a node share that node's height in proportion to their values, so both
// sides are covered even where the graph does not balance.
static void ComputeLinkBreadths(SankeyGraph* g) {
    for (int i = 0; i < g->nodes.len; i++) {
        const SankeyNodeLayout& node = g->nodes[i];
        float nodeY0 = node.y0;
        float nodeHeight = node.y1 - node.y0;

        double outgoing = 0;
        for (int k = 0; k < node.srcCount; k++) {
            outgoing += g->links[g->srcLinks[node.srcStart + k]].value;
        }
        float y0 = nodeY0;
        for (int k = 0; k < node.srcCount; k++) {
            SankeyLinkLayout& link = g->links[g->srcLinks[node.srcStart + k]];
            float width = outgoing > 0
                              ? (float)(link.value / outgoing) * nodeHeight
                              : 0.f;
            link.sourceWidth = width;
            link.y0 = y0 + width / 2.f;
            y0 += width;
        }

        double incoming = 0;
        for (int k = 0; k < node.tgtCount; k++) {
            incoming += g->links[g->tgtLinks[node.tgtStart + k]].value;
        }
        float y1 = nodeY0;
        for (int k = 0; k < node.tgtCount; k++) {
            SankeyLinkLayout& link = g->links[g->tgtLinks[node.tgtStart + k]];
            float width = incoming > 0
                              ? (float)(link.value / incoming) * nodeHeight
                              : 0.f;
            link.targetWidth = width;
            link.y1 = y1 + width / 2.f;
            y1 += width;
        }
    }
}

// A column's nodes, ordered by where they now sit.
static void SortColumn(const SankeyGraph* g, int* column, int n) {
    for (int i = 1; i < n; i++) {
        int v = column[i];
        float vy = g->nodes[v].y0;
        int j = i - 1;
        while (j >= 0 && g->nodes[column[j]].y0 > vy) {
            column[j + 1] = column[j];
            j--;
        }
        column[j + 1] = v;
    }
}

// d3's middle-out collision resolution: push the column away from its middle
// node, then clamp it against both edges of the extent.
static void ResolveCollisions(const Sankey* s, SankeyGraph* g, int* column,
                              int n, float beta, float py) {
    if (n <= 0) {
        return;
    }
    int i = n >> 1;
    float subjectY0 = g->nodes[column[i]].y0;
    float subjectY1 = g->nodes[column[i]].y1;
    PushUp(g, column, i, subjectY0 - py, beta, py);
    PushDown(g, column + i + 1, n - i - 1, subjectY1 + py, beta, py);
    PushUp(g, column, n, s->y1, beta, py);
    PushDown(g, column, n, s->y0, beta, py);
}

static void InitializeNodeBreadths(const Sankey* s, SankeyGraph* g,
                                   const int* colItems, const int* colStart,
                                   const int* colCount, int layers, float py) {
    // The scale between a flow value and pixels. d3 lets an over-crowded
    // column produce a negative one; clamped to zero here so a height never
    // comes out inverted.
    float ky = 0;
    bool any = false;
    for (int l = 0; l < layers; l++) {
        double sum = 0;
        for (int i = 0; i < colCount[l]; i++) {
            sum += g->nodes[colItems[colStart[l] + i]].value;
        }
        if (sum > 0) {
            float k =
                (s->y1 - s->y0 - (float)(colCount[l] - 1) * py) / (float)sum;
            if (!any || k < ky) {
                ky = k;
                any = true;
            }
        }
    }
    if (!any || !(ky > 0)) {
        ky = 0;
    }

    for (int l = 0; l < layers; l++) {
        float y = s->y0;
        for (int i = 0; i < colCount[l]; i++) {
            SankeyNodeLayout& node = g->nodes[colItems[colStart[l] + i]];
            float h = (float)node.value * ky;
            node.y0 = y;
            node.y1 = y + h;
            y = node.y1 + py;
        }
        // The vertical space left over, shared out evenly. d3 keeps this
        // signed, so an over-crowded column shifts back up.
        float leftover = (s->y1 - y + py) / (float)(colCount[l] + 1);
        for (int i = 0; i < colCount[l]; i++) {
            SankeyNodeLayout& node = g->nodes[colItems[colStart[l] + i]];
            float dy = leftover * (float)(i + 1);
            node.y0 += dy;
            node.y1 += dy;
        }
    }

    for (int i = 0; i < g->links.len; i++) {
        g->links[i].width = (float)g->links[i].value * ky;
    }
    for (int l = 0; l < layers; l++) {
        for (int i = 0; i < colCount[l]; i++) {
            int index = colItems[colStart[l] + i];
            SortSourceLinks(g, index);
            SortTargetLinks(g, index);
        }
    }
}

// Every node placed by its incoming links.
static void RelaxLeftToRight(const Sankey* s, SankeyGraph* g, int* colItems,
                             const int* colStart, const int* colCount,
                             int layers, float alpha, float beta, float py) {
    for (int l = 1; l < layers; l++) {
        for (int i = 0; i < colCount[l]; i++) {
            int target = colItems[colStart[l] + i];
            float y = 0;
            float w = 0;
            for (int k = 0; k < g->nodes[target].tgtCount; k++) {
                int li = g->tgtLinks[g->nodes[target].tgtStart + k];
                int source = g->links[li].source;
                float v =
                    (float)g->links[li].value *
                    (float)(g->nodes[target].layer - g->nodes[source].layer);
                y += TargetTop(g, source, target, py) * v;
                w += v;
            }
            if (w <= 0) {
                continue;
            }
            float dy = (y / w - g->nodes[target].y0) * alpha;
            g->nodes[target].y0 += dy;
            g->nodes[target].y1 += dy;
            ReorderNodeLinks(g, target);
        }
        SortColumn(g, colItems + colStart[l], colCount[l]);
        ResolveCollisions(s, g, colItems + colStart[l], colCount[l], beta, py);
    }
}

// And every node placed by its outgoing links.
static void RelaxRightToLeft(const Sankey* s, SankeyGraph* g, int* colItems,
                             const int* colStart, const int* colCount,
                             int layers, float alpha, float beta, float py) {
    for (int l = layers - 2; l >= 0; l--) {
        for (int i = 0; i < colCount[l]; i++) {
            int source = colItems[colStart[l] + i];
            float y = 0;
            float w = 0;
            for (int k = 0; k < g->nodes[source].srcCount; k++) {
                int li = g->srcLinks[g->nodes[source].srcStart + k];
                int target = g->links[li].target;
                float v =
                    (float)g->links[li].value *
                    (float)(g->nodes[target].layer - g->nodes[source].layer);
                y += SourceTop(g, source, target, py) * v;
                w += v;
            }
            if (w <= 0) {
                continue;
            }
            float dy = (y / w - g->nodes[source].y0) * alpha;
            g->nodes[source].y0 += dy;
            g->nodes[source].y1 += dy;
            ReorderNodeLinks(g, source);
        }
        SortColumn(g, colItems + colStart[l], colCount[l]);
        ResolveCollisions(s, g, colItems + colStart[l], colCount[l], beta, py);
    }
}

// Each column's stack centred in the extent. A crowded column forces a small
// scale, so the sparse ones do not fill the height and the relaxation leaves
// them sitting at their flows' weighted centre — which puts the trunk high
// with empty space under it. d3 does not correct this; translating each
// column keeps the arrangement the relaxation found and balances the diagram.
static void CenterColumns(const Sankey* s, SankeyGraph* g) {
    int layers = SankeyLayerCount(g);
    if (layers == 0) {
        return;
    }
    Vec<float> lo;
    Vec<float> hi;
    Vec<float> offsets;
    VecAppendBlanks(lo, layers);
    VecAppendBlanks(hi, layers);
    VecAppendBlanks(offsets, layers);
    for (int l = 0; l < layers; l++) {
        lo[l] = 0;
        hi[l] = 0;
        offsets[l] = 0;
    }
    Vec<bool> seen;
    VecAppendBlanks(seen, layers);
    for (int l = 0; l < layers; l++) {
        seen[l] = false;
    }
    for (int i = 0; i < g->nodes.len; i++) {
        int l = g->nodes[i].layer;
        if (!seen[l]) {
            seen[l] = true;
            lo[l] = g->nodes[i].y0;
            hi[l] = g->nodes[i].y1;
            continue;
        }
        if (g->nodes[i].y0 < lo[l]) {
            lo[l] = g->nodes[i].y0;
        }
        if (g->nodes[i].y1 > hi[l]) {
            hi[l] = g->nodes[i].y1;
        }
    }
    for (int l = 0; l < layers; l++) {
        offsets[l] = seen[l] && hi[l] > lo[l]
                         ? (s->y0 + s->y1 - lo[l] - hi[l]) / 2.f
                         : 0.f;
    }
    ApplyLayerOffsets(g, offsets.els);
}

// Runs of adjacent single-node columns of the same height nudged off the
// centre line. Centring lines such columns up exactly, which leaves the
// ribbon between them a flat rectangle; a small alternating stagger turns it
// into a gentle S. Only the flat single-node case is touched — anything else
// already curves.
static void StaggerFlatColumns(const Sankey* s, SankeyGraph* g) {
    int layers = SankeyLayerCount(g);
    if (layers < 2) {
        return;
    }
    Vec<int> count;
    Vec<int> single;
    Vec<float> heights;
    Vec<float> offsets;
    VecAppendBlanks(count, layers);
    VecAppendBlanks(single, layers);
    VecAppendBlanks(heights, layers);
    VecAppendBlanks(offsets, layers);
    for (int l = 0; l < layers; l++) {
        count[l] = 0;
        single[l] = -1;
        heights[l] = 0;
        offsets[l] = 0;
    }
    for (int i = 0; i < g->nodes.len; i++) {
        count[g->nodes[i].layer]++;
        single[g->nodes[i].layer] = g->nodes[i].index;
    }
    for (int l = 0; l < layers; l++) {
        if (count[l] == 1 && single[l] >= 0) {
            heights[l] = g->nodes[single[l]].y1 - g->nodes[single[l]].y0;
        }
    }

    // The odd column of each flat run is offset and the even ones stay on the
    // centre line, so consecutive ribbons bend down and then back up.
    int run = 0;
    for (int l = 1; l < layers; l++) {
        float d = heights[l] - heights[l - 1];
        bool flat =
            count[l] == 1 && count[l - 1] == 1 && (d < 0 ? -d : d) < 1e-3f;
        if (!flat) {
            run = 0;
            continue;
        }
        run++;
        if (run % 2 == 1) {
            // Bounded by the slack, so a column that fills the height cannot
            // move outside the extent.
            float slack = s->y1 - s->y0 - heights[l];
            if (slack < 0) {
                slack = 0;
            }
            float want = heights[l] * kSankeyStaggerRatio;
            offsets[l] = want < slack / 2.f ? want : slack / 2.f;
        }
    }
    ApplyLayerOffsets(g, offsets.els);
}

static void ComputeNodeBreadths(const Sankey* s, SankeyGraph* g, int* colItems,
                                const int* colStart, const int* colCount,
                                int layers) {
    int longest = 0;
    for (int l = 0; l < layers; l++) {
        if (colCount[l] > longest) {
            longest = colCount[l];
        }
    }
    float py = s->nodePadding;
    if (longest > 1) {
        float fit = (s->y1 - s->y0) / (float)(longest - 1);
        if (fit < py) {
            py = fit;
        }
    }

    InitializeNodeBreadths(s, g, colItems, colStart, colCount, layers, py);

    for (int i = 0; i < s->iterations; i++) {
        float alpha = powf(0.99f, (float)i);
        float beta = 1.f - alpha;
        float share = (float)(i + 1) / (float)s->iterations;
        if (share > beta) {
            beta = share;
        }
        RelaxRightToLeft(s, g, colItems, colStart, colCount, layers, alpha,
                         beta, py);
        RelaxLeftToRight(s, g, colItems, colStart, colCount, layers, alpha,
                         beta, py);
    }
}

SankeyError SankeyTopology(const Sankey* s, int nodeCount,
                           const SankeyLink* links, int nLinks,
                           SankeyGraph* out) {
    out->Reset();
    out->errNode = 0;
    for (int i = 0; i < nLinks; i++) {
        if (links[i].source >= nodeCount || links[i].source < 0) {
            out->errNode = links[i].source;
            return SankeyError::MissingNode;
        }
        if (links[i].target >= nodeCount || links[i].target < 0) {
            out->errNode = links[i].target;
            return SankeyError::MissingNode;
        }
    }

    if (nodeCount > 0) {
        VecAppendBlanks(out->nodes, nodeCount);
        for (int i = 0; i < nodeCount; i++) {
            out->nodes[i] = SankeyNodeLayout{};
            out->nodes[i].index = i;
        }
    }
    if (nLinks > 0) {
        VecAppendBlanks(out->links, nLinks);
        for (int i = 0; i < nLinks; i++) {
            SankeyLinkLayout& l = out->links[i];
            l = SankeyLinkLayout{};
            l.index = i;
            l.source = links[i].source;
            l.target = links[i].target;
            // The layout works in scaled value space throughout: everything
            // downstream of this is additive, so the nodes stay exactly
            // covered by their ribbons whatever the scale.
            l.value = ScaleValue(s->valueScale, links[i].value);
        }
    }
    if (nodeCount == 0) {
        return SankeyError::None;
    }

    ComputeNodeLinks(out);
    ComputeNodeValues(out);
    SankeyError err = ComputeNodeRanks(out);
    if (err != SankeyError::None) {
        return err;
    }
    ComputeNodeLayers(s, out);
    return SankeyError::None;
}

void SankeyLayoutFrom(const Sankey* s, SankeyGraph* g) {
    if (g->nodes.len == 0) {
        return;
    }
    ComputeNodeLayers(s, g);

    int layers = SankeyLayerCount(g);
    Vec<int> colStart;
    Vec<int> colCount;
    Vec<int> colItems;
    VecAppendBlanks(colStart, layers);
    VecAppendBlanks(colCount, layers);
    VecAppendBlanks(colItems, g->nodes.len);
    for (int l = 0; l < layers; l++) {
        colStart[l] = 0;
        colCount[l] = 0;
    }
    for (int i = 0; i < g->nodes.len; i++) {
        colCount[g->nodes[i].layer]++;
    }
    int at = 0;
    for (int l = 0; l < layers; l++) {
        colStart[l] = at;
        at += colCount[l];
        colCount[l] = 0;
    }
    for (int i = 0; i < g->nodes.len; i++) {
        int l = g->nodes[i].layer;
        colItems[colStart[l] + colCount[l]] = i;
        colCount[l]++;
    }

    ComputeNodeBreadths(s, g, colItems.els, colStart.els, colCount.els, layers);
    ComputeLinkBreadths(g);
    CenterColumns(s, g);
    StaggerFlatColumns(s, g);
}

SankeyError SankeyLayout(const Sankey* s, int nodeCount,
                         const SankeyLink* links, int nLinks,
                         SankeyGraph* out) {
    SankeyError err = SankeyTopology(s, nodeCount, links, nLinks, out);
    if (err != SankeyError::None) {
        return err;
    }
    SankeyLayoutFrom(s, out);
    return SankeyError::None;
}

} // namespace gpui
