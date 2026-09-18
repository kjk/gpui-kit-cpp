/* The CSS block layout algorithm, for a block container holding only
 * block-level boxes — taffy/src/compute/block.rs — together with the float
 * positioning context it uses, taffy/src/compute/float.rs.
 *
 * The two live in one file here because `BlockContext` is the only thing that
 * ever touches a `FloatContext`, and nothing outside block layout names
 * either of them.
 */

#include "taffy/compute.h"

namespace taffy {

// ─── float placement — taffy/src/compute/float.rs ────────────────────────
//
// This group is not in the anonymous namespace the rest of the file uses.
// `BlockFormattingContext` embeds a `FloatContext` by value and is reached
// through `BlockContext`, which `taffy_tree.h` names in the signature of
// `ComputeBlockChildLayout` — so it has external linkage, and a struct with
// external linkage may not have a field whose type is internal to one
// translation unit (gcc's -Wsubobject-linkage).
//
// The rules that govern floats are in CSS 2.2 §9.5:
// https://www.w3.org/TR/CSS22/visuren.html#floats

// An empty "slot" that avoids floats, for non-floated content to lay out in.
struct ContentSlot {
    // -1 when the slot is not inside any segment.
    int segmentId = -1;
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

// A slot for a box that establishes its own formatting context. Its border
// box may not overlap floats, while its margins are still resolved against
// the containing block edges and may overlap them. Rust's `BfcSlot`.
struct BfcSlot {
    int segmentId = -1;
    float x = 0.0f;
    float y = 0.0f;
    float borderWidth = 0.0f;
    float stretchWidth = 0.0f;
};

struct PlacedFloatedBox {
    float width = 0.0f;
    float height = 0.0f;
    // Distance from the edge the box is floated towards — from the left for
    // left floats, from the right for right floats.
    float xInset = 0.0f;
    // Distance from the top edge of the container.
    float y = 0.0f;
};

static bool FloatFitsHorizontally(float width, FloatDirection direction,
                                  float bfcWidth, const float floatInsets[2],
                                  const float cbInsets[2]) {
    int lead = (int)direction;
    int trail = 1 - lead;
    float xInset = F32Max(floatInsets[lead], cbInsets[lead]);
    bool fitsOpposite = floatInsets[trail] == 0.0f ||
                        xInset + width <= bfcWidth - floatInsets[trail];
    bool fitsContaining = floatInsets[lead] == 0.0f ||
                          xInset + width <= bfcWidth - cbInsets[trail];
    return fitsOpposite && fitsContaining;
}

// A non-overlapping horizontal band of the block formatting context, with the
// same available width for its whole height.
struct Segment {
    float yStart = 0.0f;
    float yEnd = 0.0f;
    // Left inset in slot 0, right inset in slot 1.
    float insets[2] = {0.0f, 0.0f};
    // A containing-block inset may be non-zero without a float occupying it.
    bool hasFloat[2] = {false, false};

    // Whether the segment can fit the box in the horizontal axis.
    bool FitsFloatWidth(SizeF floatedBox, FloatDirection direction,
                        float bfcWidth, const float cbInsets[2]) const {
        return FloatFitsHorizontally(floatedBox.w, direction, bfcWidth, insets,
                                     cbInsets);
    }
    bool Contains(float y) const { return y >= yStart && y < yEnd; }
};

// Helper for placing one floated box: given a pinned starting y, works out
// whether any x position has room for the box across its whole height.
struct FloatFitter {
    float bfcWidth = 0.0f;
    double slotHeight = 0.0;
    float floatInsets[2] = {0.0f, 0.0f};
    float cbInsets[2] = {0.0f, 0.0f};

    FloatFitter(float bfcWidth_, float slotHeight_, const float in[2])
        : bfcWidth(bfcWidth_), slotHeight((double)slotHeight_) {
        cbInsets[0] = in[0];
        cbInsets[1] = in[1];
    }

    // Union another segment's insets, which is a max on each side.
    void UnionInsets(const float other[2]) {
        floatInsets[0] = F32Max(floatInsets[0], other[0]);
        floatInsets[1] = F32Max(floatInsets[1], other[1]);
    }
    float PlacedInset(FloatDirection direction) const {
        int lead = (int)direction;
        return F32Max(floatInsets[lead], cbInsets[lead]);
    }
    bool FitsHorizontally(float width, FloatDirection direction) const {
        return FloatFitsHorizontally(width, direction, bfcWidth, floatInsets,
                                     cbInsets);
    }
    void AddHeight(float height) { slotHeight += (double)height; }
    bool FitsVertically(float height) const {
        return slotHeight >= (double)height;
    }
};

struct IndexRange {
    int start = 0;
    int end = 0;
};

// A context for placing floated boxes.
struct FloatContext {
    // The available space constraint of the root block formatting context
    // this manages floats for.
    float availableWidth = 0.0f;
    bool hasFloats = false;
    Vec<PlacedFloatedBox> leftFloats;
    Vec<PlacedFloatedBox> rightFloats;
    // Non-overlapping horizontal bands of the context.
    Vec<Segment> segments;
    // A closed-open range saying which segments the last placed float went
    // into, per side.
    IndexRange lastPlacedFloats[2];
    // Lowest bottom and latest top, including zero-sized floats that occupy
    // no segment.
    Optf clearBottoms[2] = {None(), None()};
    Optf floatCeiling = None();

    float LastSegmentEnd() const {
        return len(segments) > 0 ? segments[len(segments) - 1].yEnd : 0.0f;
    }

    bool HasActiveFloats(float minY) const {
        return hasFloats && LastSegmentEnd() > minY;
    }

    void SetWidth(float w) { availableWidth = w; }

    // Split a segment in two so a new float can start and end on exact
    // segment boundaries.
    void SubdivideSegment(int idx, float divideAtY) {
        Segment newSegment;
        newSegment.insets[0] = segments[idx].insets[0];
        newSegment.insets[1] = segments[idx].insets[1];
        newSegment.hasFloat[0] = segments[idx].hasFloat[0];
        newSegment.hasFloat[1] = segments[idx].hasFloat[1];
        newSegment.yStart = divideAtY;
        newSegment.yEnd = segments[idx].yEnd;
        segments[idx].yEnd = divideAtY;
        VecInsertAt(segments, idx + 1, newSegment);
    }

    void UpdateLastPlacedFloat(FloatDirection direction, IndexRange placement) {
        int slot = (int)direction;
        IndexRange& r = lastPlacedFloats[slot];
        r.start = r.start > placement.start ? r.start : placement.start;
        r.end = r.end > placement.end ? r.end : placement.end;
    }

    PlacedFloatedBox PlaceFloatedBoxInner(SizeF floatedBox, float minY,
                                          const float containingBlockInsets[2],
                                          FloatDirection direction,
                                          Clear clear);

    PointF PlaceFloatedBox(SizeF floatedBox, float minY,
                           const float containingBlockInsets[2],
                           FloatDirection direction, Clear clear) {
        hasFloats = true;
        PlacedFloatedBox placed = PlaceFloatedBoxInner(
            floatedBox, minY, containingBlockInsets, direction, clear);
        int slot = (int)direction;
        float bottom = placed.y + placed.height;
        clearBottoms[slot] =
            Some(IsSome(clearBottoms[slot]) ? F32Max(clearBottoms[slot], bottom)
                                            : bottom);
        floatCeiling = Some(
            IsSome(floatCeiling) ? F32Max(floatCeiling, placed.y) : placed.y);
        float xInset = placed.xInset;
        float y = placed.y;
        if (direction == FloatDirection::Left) {
            VecAppend(leftFloats, placed);
            return {xInset, y};
        }
        VecAppend(rightFloats, placed);
        return {availableWidth - xInset - floatedBox.w, y};
    }

    // The end segment of the last float on the side(s) `clear` names, or -1.
    int ClearedSegment(Clear clear) const {
        int left = lastPlacedFloats[0].end;
        int right = lastPlacedFloats[1].end;
        switch (clear) {
            case Clear::Left:
                return left > 0 ? left : -1;
            case Clear::Right:
                return right > 0 ? right : -1;
            case Clear::Both: {
                return left > 0 || right > 0 ? (left > right ? left : right)
                                             : -1;
            }
            default:
                return -1;
        }
    }

    // The bottom of the lowest float the clear property is concerned with.
    Optf ClearedThreshold(Clear clear) const {
        switch (clear) {
            case Clear::Left:
                return clearBottoms[0];
            case Clear::Right:
                return clearBottoms[1];
            case Clear::Both:
                if (IsSome(clearBottoms[0]) && IsSome(clearBottoms[1])) {
                    return Some(F32Max(clearBottoms[0], clearBottoms[1]));
                }
                return IsSome(clearBottoms[0]) ? clearBottoms[0]
                                               : clearBottoms[1];
            default:
                return None();
        }
    }

    ContentSlot FindContentSlot(float minY,
                                const float containingBlockInsets[2],
                                Clear clear, int after) const;
    BfcSlot FindBfcSlot(float minY, const float containingBlockInsets[2],
                        const float margins[2], Direction direction,
                        Clear clear, int after) const;
};

PlacedFloatedBox FloatContext::PlaceFloatedBoxInner(
    SizeF floatedBox, float minY, const float containingBlockInsets[2],
    FloatDirection direction, Clear clear) {
    int slot = (int)direction;

    minY = F32Max(minY, UnwrapOr(floatCeiling, -INFINITY));
    minY = F32Max(minY, UnwrapOr(ClearedThreshold(clear), -INFINITY));

    // CSS2 float rule 5: a float may not be above any earlier float.
    int floatStart = lastPlacedFloats[0].start > lastPlacedFloats[1].start
                         ? lastPlacedFloats[0].start
                         : lastPlacedFloats[1].start;
    int hwm = 0;
    switch (clear) {
        case Clear::Left: {
            int b = lastPlacedFloats[0].end + 1;
            hwm = floatStart > b ? floatStart : b;
            break;
        }
        case Clear::Right: {
            int b = lastPlacedFloats[1].end + 1;
            hwm = floatStart > b ? floatStart : b;
            break;
        }
        case Clear::Both: {
            int l = lastPlacedFloats[0].end;
            int r = lastPlacedFloats[1].end;
            hwm = (l > r ? l : r) + 1;
            break;
        }
        default:
            hwm = floatStart;
            break;
    }

    // The float goes in a segment at or below minY.
    int startIdx = segments.len;
    for (int i = hwm; i < segments.len; i++) {
        if (segments[i].yEnd > minY) {
            startIdx = i;
            break;
        }
    }
    float startY = minY;
    int endIdx = startIdx;

    bool haveStart = false;
    bool haveEnd = false;
    int foundStart = 0;
    int foundEnd = 0;
    float placedInset = containingBlockInsets[slot];

    // Find a position with room for the float.
    while (true) {
        // No existing segment can take the float, so it goes into a new one
        // below all of them; that always has room.
        if (startIdx >= segments.len) {
            haveStart = false;
            haveEnd = false;
            placedInset = containingBlockInsets[slot];
            break;
        }

        const Segment& startSegment = segments[startIdx];
        if (!startSegment.FitsFloatWidth(floatedBox, direction, availableWidth,
                                         containingBlockInsets)) {
            startIdx++;
            if (endIdx < startIdx) {
                endIdx = startIdx;
            }
            continue;
        }

        startY = F32Max(startY, startSegment.yStart);
        float availableHeight = startSegment.yEnd - startY;
        FloatFitter fitter(availableWidth, availableHeight,
                           containingBlockInsets);
        fitter.UnionInsets(startSegment.insets);

        // With the start segment pinned, walk down to find the end segment:
        // the range must be tall enough for the float and wide enough all the
        // way down.
        bool restartOuter = false;
        while (true) {
            if (endIdx >= segments.len) {
                haveStart = true;
                foundStart = startIdx;
                haveEnd = false;
                placedInset = fitter.PlacedInset(direction);
                break;
            }
            const Segment& endSegment = segments[endIdx];
            fitter.UnionInsets(endSegment.insets);
            if (!fitter.FitsHorizontally(floatedBox.w, direction)) {
                startIdx++;
                if (endIdx < startIdx) {
                    endIdx = startIdx;
                }
                restartOuter = true;
                break;
            }
            if (endIdx != startIdx) {
                fitter.AddHeight(endSegment.yEnd - endSegment.yStart);
            }
            if (!fitter.FitsVertically(floatedBox.h)) {
                endIdx++;
                continue;
            }
            haveStart = true;
            foundStart = startIdx;
            haveEnd = true;
            foundEnd = endIdx;
            placedInset = fitter.PlacedInset(direction);
            break;
        }
        if (restartOuter) {
            continue;
        }
        break;
    }

    PlacedFloatedBox out;
    out.width = floatedBox.w;
    out.height = floatedBox.h;
    out.y = startY;
    out.xInset = placedInset;

    // A zero-width float still obstructs an independent formatting context.
    if (floatedBox.h == 0.0f) {
        return out;
    }

    // Placed after every existing segment.
    if (!haveStart) {
        float lastYEnd = LastSegmentEnd();
        if (startY > lastYEnd) {
            Segment gap;
            gap.yStart = lastYEnd;
            gap.yEnd = startY;
            VecAppend(segments, gap);
        }
        float newStartY = F32Max(lastYEnd, startY);
        Segment seg;
        seg.yStart = newStartY;
        seg.yEnd = newStartY + floatedBox.h;
        seg.insets[0] = containingBlockInsets[0];
        seg.insets[1] = containingBlockInsets[1];
        seg.insets[slot] += floatedBox.w;
        seg.hasFloat[slot] = true;
        VecAppend(segments, seg);

        int si = segments.len - 1;
        UpdateLastPlacedFloat(direction, {si, si + 1});

        out.y = newStartY;
        out.xInset = containingBlockInsets[slot];
        return out;
    }

    int si = foundStart;
    // If the float does not start exactly on the segment boundary, split the
    // segment there and place it in the second half.
    if (startY != segments[si].yStart) {
        SubdivideSegment(si, startY);
        si++;
        if (haveEnd) {
            foundEnd++;
        }
    }

    int ei;
    if (!haveEnd) {
        float lastYEnd = LastSegmentEnd();
        if (minY > lastYEnd) {
            Segment gap;
            gap.yStart = lastYEnd;
            gap.yEnd = minY;
            VecAppend(segments, gap);
        }
        ei = segments.len - 1;
    } else {
        ei = foundEnd;
        float endY = startY + floatedBox.h;
        while (ei > si && endY <= segments[ei].yStart) {
            ei--;
        }
        if (segments[ei].yStart < endY && endY < segments[ei].yEnd) {
            SubdivideSegment(ei, endY);
        }
    }

    float placedInsetPlusWidth = placedInset + floatedBox.w;
    for (int i = si; i <= ei && i < segments.len; i++) {
        segments[i].insets[slot] = placedInsetPlusWidth;
        segments[i].hasFloat[slot] = true;
    }

    UpdateLastPlacedFloat(direction, {si, ei + 1});
    return out;
}

ContentSlot FloatContext::FindContentSlot(float minY,
                                          const float containingBlockInsets[2],
                                          Clear clear, int after) const {
    ContentSlot fallback;
    fallback.segmentId = -1;
    fallback.x = containingBlockInsets[0];
    fallback.y = minY;
    fallback.width =
        availableWidth - containingBlockInsets[0] - containingBlockInsets[1];
    fallback.height = INFINITY;

    if (!HasActiveFloats(minY)) {
        return fallback;
    }

    minY = F32Max(minY, UnwrapOr(ClearedThreshold(clear), -INFINITY));

    int atLeast = after >= 0 ? after + 1 : 0;
    int cleared = ClearedSegment(clear);
    int hwm = cleared >= 0 ? cleared + 1 : 0;
    if (atLeast > hwm) {
        hwm = atLeast;
    }

    int startIdx = segments.len;
    for (int i = hwm; i < segments.len; i++) {
        if (segments[i].yEnd > minY) {
            startIdx = i;
            break;
        }
    }
    if (startIdx >= segments.len) {
        return fallback;
    }

    const Segment& segment = segments[startIdx];
    float insetLeft = F32Max(segment.insets[0], containingBlockInsets[0]);
    float insetRight = F32Max(segment.insets[1], containingBlockInsets[1]);
    ContentSlot slot;
    slot.segmentId = startIdx;
    slot.x = insetLeft;
    slot.y = F32Max(segment.yStart, minY);
    slot.width = availableWidth - insetLeft - insetRight;
    slot.height = INFINITY;
    return slot;
}

BfcSlot FloatContext::FindBfcSlot(float minY,
                                  const float containingBlockInsets[2],
                                  const float margins[2], Direction direction,
                                  Clear clear, int after) const {
    float marginInsets[2] = {containingBlockInsets[0] + margins[0],
                             containingBlockInsets[1] + margins[1]};
    float noFloatWidth = availableWidth - marginInsets[0] - marginInsets[1];
    BfcSlot fallback;
    fallback.x = marginInsets[0];
    fallback.y = minY;
    fallback.borderWidth = noFloatWidth;
    fallback.stretchWidth = noFloatWidth;
    if (!HasActiveFloats(minY)) {
        return fallback;
    }

    minY = F32Max(minY, UnwrapOr(ClearedThreshold(clear), -INFINITY));
    int atLeast = after >= 0 ? after + 1 : 0;
    int cleared = ClearedSegment(clear);
    int hwm = cleared >= 0 ? cleared + 1 : 0;
    if (atLeast > hwm) {
        hwm = atLeast;
    }
    int startIdx = segments.len;
    for (int i = hwm; i < segments.len; i++) {
        if (segments[i].yEnd > minY) {
            startIdx = i;
            break;
        }
    }
    if (startIdx >= segments.len) {
        fallback.y = F32Max(LastSegmentEnd(), minY);
        return fallback;
    }

    const Segment& segment = segments[startIdx];
    int lead = direction == Direction::Ltr ? 0 : 1;
    int trail = 1 - lead;
    bool hasLeadFloat = segment.hasFloat[lead];
    bool hasTrailFloat = segment.hasFloat[trail];
    float fitInsets[2] = {};
    float stretchInsets[2] = {};
    fitInsets[lead] = hasLeadFloat
                          ? F32Max(segment.insets[lead], marginInsets[lead])
                          : marginInsets[lead];
    stretchInsets[lead] = fitInsets[lead];
    fitInsets[trail] =
        hasTrailFloat
            ? F32Max(segment.insets[trail], containingBlockInsets[trail])
            : F32Min(marginInsets[trail], containingBlockInsets[trail]);
    stretchInsets[trail] =
        hasTrailFloat ? F32Max(segment.insets[trail], marginInsets[trail])
                      : marginInsets[trail];

    BfcSlot slot;
    slot.segmentId = startIdx;
    slot.x = fitInsets[0];
    slot.y = F32Max(segment.yStart, minY);
    slot.borderWidth = availableWidth - fitInsets[0] - fitInsets[1];
    slot.stretchWidth = availableWidth - stretchInsets[0] - stretchInsets[1];
    return slot;
}

// The intrinsic width contribution of a set of floats.
struct FloatIntrinsicWidthCalculator {
    AvailableSpace availableWidth;
    float contribution = 0.0f;
    float widest = 0.0f;

    void AddFloat(float width) {
        switch (availableWidth.kind) {
            case AvailableSpace::Kind::Definite:
            case AvailableSpace::Kind::MaxContent:
                contribution += width;
                break;
            case AvailableSpace::Kind::MinContent:
                contribution = F32Max(contribution, width);
                break;
        }
        widest = F32Max(widest, width);
    }
    float Result() const {
        if (availableWidth.kind == AvailableSpace::Kind::Definite) {
            return F32Max(F32Min(contribution, availableWidth.value), widest);
        }
        return contribution;
    }
};

// ─── the block formatting context ────────────────────────────────────────

// Context for positioning block and float boxes within a block formatting
// context. Rust's `BlockFormattingContext`.
struct BlockFormattingContext {
    FloatContext floatContext;
};

// Context for one block within a block formatting context: a pointer to the
// shared BFC plus this block's own offsets. Rust's `BlockContext<'bfc>`.
struct BlockContext {
    BlockFormattingContext* bfc = nullptr;
    // The y offset of this block's border-top, relative to the border-top of
    // the BFC root.
    float yOffset = 0.0f;
    // The x insets of the border box in from each side, relative to the BFC
    // root.
    float insets[2] = {0.0f, 0.0f};
    // The x insets of the content box.
    float contentBoxInsets[2] = {0.0f, 0.0f};
    // The height that floats take up in this element.
    float floatContentContribution = 0.0f;
    bool isRoot = false;
    bool adjoiningFloats[2] = {false, false};
    bool topAdjoiningFloats[2] = {false, false};
    bool hasTopAdjoiningFloats = false;

    // A sub-context for a child block node.
    BlockContext SubContext(float additionalYOffset,
                            const float childInsets[2]) {
        BlockContext out;
        out.bfc = bfc;
        out.yOffset = yOffset + additionalYOffset;
        out.insets[0] = insets[0] + childInsets[0];
        out.insets[1] = insets[1] + childInsets[1];
        out.contentBoxInsets[0] = out.insets[0];
        out.contentBoxInsets[1] = out.insets[1];
        out.isRoot = false;
        out.adjoiningFloats[0] = adjoiningFloats[0];
        out.adjoiningFloats[1] = adjoiningFloats[1];
        return out;
    }

    bool IsBfcRoot() const { return isRoot; }

    // The width of the whole BFC, used to resolve positions relative to its
    // right edge (right-floated boxes). Sub-blocks use SubContext instead.
    void SetWidth(float availableWidth) {
        bfc->floatContext.SetWidth(availableWidth);
    }
    // The x-axis content-box insets: the difference between the border box
    // and the content box (padding + border + scrollbar gutter).
    void ApplyContentBoxInset(const float contentBoxXInsets[2]) {
        contentBoxInsets[0] = insets[0] + contentBoxXInsets[0];
        contentBoxInsets[1] = insets[1] + contentBoxXInsets[1];
    }
    bool HasFloats() const { return bfc->floatContext.hasFloats; }
    bool HasActiveFloats(float minY) const {
        return bfc->floatContext.HasActiveFloats(minY + yOffset);
    }
    PointF PlaceFloatedBox(SizeF floatedBox, float minY,
                           FloatDirection direction, Clear clear,
                           bool adjoinsUnresolvedStrut) {
        if (adjoinsUnresolvedStrut) {
            adjoiningFloats[(int)direction] = true;
        }
        PointF pos = bfc->floatContext.PlaceFloatedBox(
            floatedBox, minY + yOffset, contentBoxInsets, direction, clear);
        pos.y -= yOffset;
        pos.x -= insets[0];
        floatContentContribution =
            F32Max(floatContentContribution, pos.y + floatedBox.h);
        return pos;
    }
    ContentSlot FindContentSlot(float minY, Clear clear, int after) const {
        ContentSlot slot = bfc->floatContext.FindContentSlot(
            minY + yOffset, contentBoxInsets, clear, after);
        slot.y -= yOffset;
        slot.x -= insets[0];
        return slot;
    }
    BfcSlot FindBfcSlot(float minY, const float margins[2], Direction direction,
                        Clear clear, int after) const {
        BfcSlot slot = bfc->floatContext.FindBfcSlot(
            minY + yOffset, contentBoxInsets, margins, direction, clear, after);
        slot.y -= yOffset;
        slot.x -= insets[0];
        return slot;
    }
    Optf ClearedThreshold(Clear clear) const {
        Optf t = bfc->floatContext.ClearedThreshold(clear);
        if (IsSome(t)) {
            return Some(t - yOffset);
        }
        return None();
    }
    bool HasAdjoiningFloat(Clear clear) const {
        switch (clear) {
            case Clear::Left:
                return adjoiningFloats[0];
            case Clear::Right:
                return adjoiningFloats[1];
            case Clear::Both:
                return adjoiningFloats[0] || adjoiningFloats[1];
            default:
                return false;
        }
    }
    void MergeAdjoiningFloats(const bool flags[2]) {
        adjoiningFloats[0] = adjoiningFloats[0] || flags[0];
        adjoiningFloats[1] = adjoiningFloats[1] || flags[1];
    }
    void CommitStrut() {
        if (!hasTopAdjoiningFloats) {
            topAdjoiningFloats[0] = adjoiningFloats[0];
            topAdjoiningFloats[1] = adjoiningFloats[1];
            hasTopAdjoiningFloats = true;
        }
        adjoiningFloats[0] = false;
        adjoiningFloats[1] = false;
    }
    void GetTopAdjoiningFloats(bool out[2]) const {
        out[0] =
            hasTopAdjoiningFloats ? topAdjoiningFloats[0] : adjoiningFloats[0];
        out[1] =
            hasTopAdjoiningFloats ? topAdjoiningFloats[1] : adjoiningFloats[1];
    }
    void AddChildFloatedContentHeightContribution(float childContribution) {
        floatContentContribution =
            F32Max(floatContentContribution, childContribution);
    }
    float FloatedContentHeightContribution() const {
        return floatContentContribution;
    }
};

namespace {

// ─── block items ─────────────────────────────────────────────────────────

// Per-child data accumulated over the course of the algorithm.
struct BlockItem {
    NodeId nodeId;
    // The index of the item among the children, which controls placement
    // order.
    uint32_t order = 0;

    // Tables do not get stretch sizing applied to them.
    bool isTable = false;
    // Replaced elements resolve auto width from their intrinsic size.
    bool isReplaced = false;
    // Whether the child is a non-independent block or inline node.
    bool isInSameBfc = false;

    Float floatMode = Float::None;
    Clear clear = Clear::None;

    SizeFOpt size = SizeFOptNone();
    SizeFOpt minSize = SizeFOptNone();
    SizeFOpt maxSize = SizeFOptNone();

    PointOverflow overflow;
    float scrollbarWidth = 0.0f;

    Position position = Position::Relative;
    RectLpa inset;
    RectLpa margin;
    RectF padding;
    RectF border;
    SizeF paddingBorderSum;

    SizeF computedSize;
    // The position taking padding, border, margins and scrollbar gutters into
    // account, but not inset.
    PointF staticPosition;
    bool canBeCollapsedThrough = false;

    // Pending layout for in-flow non-floated items, held back from the tree so
    // the post-loop align-content pass can shift location.y first.
    bool hasFinalLayout = false;
    Layout finalLayout;
};

// ─── generate_item_list ──────────────────────────────────────────────────

void GenerateItemList(TaffyTree* tree, NodeId node, SizeFOpt nodeInnerSize,
                      Vec<BlockItem>* items) {
    CalcResolver calc = tree->calc;
    int n = tree->ChildCount(node);
    uint32_t order = 0;
    for (int i = 0; i < n; i++) {
        NodeId childNodeId = tree->GetChildId(node, i);
        const Style& cs = tree->GetStyle(childNodeId);
        if (cs.BoxGenMode() == BoxGenerationMode::None) {
            continue;
        }

        Optf aspectRatio = cs.aspectRatio;
        RectF padding = cs.padding.ResolveOrZero(nodeInnerSize, calc);
        RectF border = cs.border.ResolveOrZero(nodeInnerSize, calc);
        SizeF pbSum = (padding + border).SumAxes();
        SizeF boxSizingAdjustment =
            cs.boxSizing == BoxSizing::ContentBox ? pbSum : SizeF::Zero();

        BlockItem item;
        item.nodeId = childNodeId;
        item.order = order++;
        item.isTable = cs.itemIsTable;
        item.isReplaced = cs.IsCompressibleReplaced();
        item.floatMode = cs.floatMode;
        item.clear = cs.clear;
        item.position = cs.position;
        item.overflow = cs.overflow;
        item.scrollbarWidth = cs.scrollbarWidth;

        bool isNotFloated = cs.floatMode == Float::None;
        bool isScrollContainer = IsScrollContainer(cs.overflow.x) ||
                                 IsScrollContainer(cs.overflow.y);
        item.isInSameBfc = cs.IsBlock() && !cs.itemIsTable &&
                           cs.position != Position::Absolute && isNotFloated &&
                           !isScrollContainer;

        item.size = MaybeAdd(
            MaybeApplyAspectRatio(cs.size.MaybeResolve(nodeInnerSize, calc),
                                  aspectRatio),
            boxSizingAdjustment);
        item.minSize = MaybeAdd(
            MaybeApplyAspectRatio(cs.minSize.MaybeResolve(nodeInnerSize, calc),
                                  aspectRatio),
            boxSizingAdjustment);
        item.maxSize = MaybeAdd(
            MaybeApplyAspectRatio(cs.maxSize.MaybeResolve(nodeInnerSize, calc),
                                  aspectRatio),
            boxSizingAdjustment);
        item.inset = cs.inset;
        item.margin = cs.margin;
        item.padding = padding;
        item.border = border;
        item.paddingBorderSum = pbSum;
        VecAppend(*items, item);
    }
}

// ─── determine_content_based_container_width ─────────────────────────────

float DetermineContentBasedContainerWidth(TaffyTree* tree,
                                          const Vec<BlockItem>& items,
                                          AvailableSpace availableWidth) {
    CalcResolver calc = tree->calc;
    SizeAvail availableSpace = {availableWidth, AvailableSpace::MinContent()};

    float maxChildWidth = 0.0f;
    FloatIntrinsicWidthCalculator floatContribution;
    floatContribution.availableWidth = availableWidth;

    for (int i = 0; i < len(items); i++) {
        const BlockItem& item = items[i];
        if (item.position == Position::Absolute) {
            continue;
        }
        SizeFOpt knownDimensions =
            MaybeClamp(item.size, item.minSize, item.maxSize);
        float itemXMarginSum =
            item.margin.ResolveOrZero(availableSpace.width.IntoOption(), calc)
                .HorizontalAxisSum();
        float width;
        if (IsSome(knownDimensions.w)) {
            width = knownDimensions.w;
        } else {
            SizeAvail childAvail = availableSpace;
            childAvail.width = MaybeSub(childAvail.width, itemXMarginSum);
            width = tree->MeasureChildSize(
                item.nodeId, knownDimensions, SizeFOptNone(), childAvail,
                SizingMode::InherentSize, AbsoluteAxis::Horizontal,
                LineBool::True());
        }
        width = F32Max(width, item.paddingBorderSum.w) + itemXMarginSum;

        if (IsFloated(item.floatMode)) {
            floatContribution.AddFloat(width);
            continue;
        }
        maxChildWidth = F32Max(maxChildWidth, width);
    }

    return F32Max(maxChildWidth, floatContribution.Result());
}

// ─── perform_final_layout_on_in_flow_children ────────────────────────────

struct InFlowResult {
    SizeF inflowContentSize;
    float intrinsicOuterHeight = 0.0f;
    CollapsibleMarginSet firstChildTopMarginSet;
    CollapsibleMarginSet lastChildBottomMarginSet;
    Optf firstBaseline = None();
};

InFlowResult PerformFinalLayoutOnInFlowChildren(
    TaffyTree* tree, RunMode runMode, Vec<BlockItem>* items,
    float containerOuterWidth, Optf containerPercentageResolutionHeight,
    RectF contentBoxInset, RectF resolvedContentBoxInset, RectF resolvedBorder,
    TextAlign textAlign, Direction direction,
    LineBool ownMarginsCollapseWithChildren, BlockContext* blockCtx) {
    CalcResolver calc = tree->calc;
    float containerInnerWidth = containerOuterWidth - resolvedContentBoxInset
                                                          .HorizontalAxisSum();
    Optf percentageResolutionHeight =
        MaybeSub(containerPercentageResolutionHeight, resolvedContentBoxInset
                                                          .VerticalAxisSum());
    SizeFOpt parentSize = {Some(containerInnerWidth),
                           percentageResolutionHeight};
    // Vertical available space in block flow is indefinite, NOT a min-content
    // constraint: MaxContent is taffy's spelling of "indefinite". MinContent
    // here would make every descendant grid believe it was being sized under
    // a min-content constraint, where the maximize-tracks step has no free
    // space, and auto rows holding only scroll containers would collapse.
    SizeAvail availableSpace = {AvailableSpace::Definite(containerInnerWidth),
                                AvailableSpace::MaxContent()};

    // TODO(taffy): handle nested blocks with different widths.
    if (blockCtx->IsBfcRoot()) {
        blockCtx->SetWidth(containerOuterWidth);
        float xInsets[2] = {resolvedContentBoxInset.left,
                            resolvedContentBoxInset.right};
        blockCtx->ApplyContentBoxInset(xInsets);
    }

    if (!ownMarginsCollapseWithChildren.start) {
        blockCtx->CommitStrut();
    }

    InFlowResult res;
    float committedYOffset = resolvedContentBoxInset.top;
    float yOffsetForAbsolute = resolvedContentBoxInset.top;
    CollapsibleMarginSet activeCollapsibleMarginSet;
    bool isCollapsingWithFirstMarginSet = true;
    bool activeMarginSetHasClearance = false;
    bool hasActiveFloats = blockCtx->HasActiveFloats(committedYOffset);

    for (int itemIdx = 0; itemIdx < items->len; itemIdx++) {
        BlockItem& item = (*items)[itemIdx];
        if (item.position == Position::Absolute) {
            float x = direction == Direction::Ltr
                          ? resolvedContentBoxInset.left
                          : containerOuterWidth - resolvedContentBoxInset.right;
            item.staticPosition = {x, yOffsetForAbsolute};
            continue;
        }

        RectFOpt itemMargin =
            item.margin.MaybeResolve(Some(containerInnerWidth), calc);
        RectF itemNonAutoMargin = {
            UnwrapOr(itemMargin.left, 0.0f), UnwrapOr(itemMargin.right, 0.0f),
            UnwrapOr(itemMargin.top, 0.0f), UnwrapOr(itemMargin.bottom, 0.0f)};
        float itemNonAutoXMarginSum = itemNonAutoMargin.HorizontalAxisSum();

        SizeF scrollbarSize = {
            item.overflow.y == Overflow::Scroll ? item.scrollbarWidth : 0.0f,
            item.overflow.x == Overflow::Scroll ? item.scrollbarWidth : 0.0f};

        // Floated boxes.
        OptFloatDirection floatDirection = FloatDir(item.floatMode);
        if (floatDirection.IsSome()) {
            hasActiveFloats = true;

            float availableWidth = containerInnerWidth - itemNonAutoXMarginSum;
            LayoutOutput itemLayout = tree->PerformChildLayout(
                item.nodeId, SizeFOptNone(), parentSize,
                {AvailableSpace::Definite(availableWidth),
                 AvailableSpace::MaxContent()},
                SizingMode::InherentSize, LineBool::False());
            SizeF marginBox = itemLayout.size + itemNonAutoMargin.SumAxes();

            bool adjoinsUnresolvedStrut = isCollapsingWithFirstMarginSet &&
                                          ownMarginsCollapseWithChildren.start;
            float yOffsetForFloat =
                adjoinsUnresolvedStrut
                    ? committedYOffset
                    : committedYOffset + activeCollapsibleMarginSet.Resolve();
            PointF location = blockCtx->PlaceFloatedBox(
                marginBox, yOffsetForFloat, floatDirection.val, item.clear,
                adjoinsUnresolvedStrut);

            // Turn the margin-box location float placement returned into a
            // border-box location for the output Layout.
            location.y += itemNonAutoMargin.top;
            location.x += itemNonAutoMargin.left;

            Layout layout;
            layout.order = item.order;
            layout.size = itemLayout.size;
            layout.contentSize = itemLayout.contentSize;
            layout.scrollbarSize = scrollbarSize;
            layout.location = location;
            layout.padding = item.padding;
            layout.border = item.border;
            layout.margin = itemNonAutoMargin;
            tree->SetUnroundedLayout(item.nodeId, layout);

            res.inflowContentSize = Max(
                res.inflowContentSize,
                ComputeContentSizeContribution(
                    {IsRtl(direction) ? containerOuterWidth -
                                            (location.x + itemLayout.size.w) -
                                            resolvedBorder.right
                                      : location.x - resolvedBorder.left,
                     location.y - resolvedBorder.top},
                    itemLayout.size, itemLayout.contentSize, item.overflow));
            continue;
        }

        // Non-floated boxes.
        float yMarginOffset = 0.0f;
        float stretchWidth;
        PointF floatAvoidingPosition;
        float floatAvoidingWidth;
        bool itemAvoidsFloats = false;
        bool itemPushedBelowFloat = false;

        if (item.isInSameBfc) {
            stretchWidth = containerInnerWidth - itemNonAutoXMarginSum;
            floatAvoidingPosition = {0.0f, 0.0f};
            floatAvoidingWidth = 0.0f;
        } else {
            if (!isCollapsingWithFirstMarginSet ||
                !ownMarginsCollapseWithChildren.start) {
                yMarginOffset = activeCollapsibleMarginSet
                                    .CollapseWithMargin(itemNonAutoMargin.top)
                                    .Resolve();
            }
            float minY = committedYOffset + yMarginOffset;
            if (hasActiveFloats || blockCtx->HasActiveFloats(minY)) {
                float xMargins[2] = {itemNonAutoMargin.left, itemNonAutoMargin
                                                                 .right};
                float minAutoWidth = -itemNonAutoXMarginSum;
                int after = -1;
                BfcSlot slot;
                while (true) {
                    slot = blockCtx->FindBfcSlot(minY, xMargins, direction,
                                                 item.clear, after);
                    if (slot.segmentId < 0) {
                        break;
                    }
                    float width = MaybeClamp(
                        UnwrapOr(item.size.w,
                                 F32Max(slot.stretchWidth, minAutoWidth)),
                        item.minSize.w, item.maxSize.w);
                    if (width <= slot.borderWidth + 0.001f) {
                        break;
                    }
                    after = slot.segmentId;
                }
                itemPushedBelowFloat = slot.y > minY;
                hasActiveFloats = slot.segmentId >= 0;
                itemAvoidsFloats = true;
                stretchWidth = F32Max(slot.stretchWidth, minAutoWidth);
                floatAvoidingPosition = {slot.x, slot.y};
                floatAvoidingWidth = slot.borderWidth;
            } else {
                stretchWidth = containerInnerWidth - itemNonAutoXMarginSum;
                floatAvoidingPosition = {resolvedContentBoxInset.left, minY};
                floatAvoidingWidth = containerInnerWidth;
            }
        }

        SizeFOpt knownDimensions = SizeFOptNone();
        if (!item.isTable && !item.isReplaced) {
            SizeFOpt sized = item.size;
            sized.w = Some(MaybeClamp(UnwrapOr(sized.w, stretchWidth),
                                      item.minSize.w, item.maxSize.w));
            knownDimensions = MaybeClamp(sized, item.minSize, item.maxSize);
        }

        LayoutInput inputs;
        inputs.runMode = runMode;
        inputs.sizingMode = SizingMode::InherentSize;
        inputs.axis = RequestedAxis::Both;
        inputs.knownDimensions = knownDimensions;
        inputs.parentSize = parentSize;
        inputs.availableSpace = availableSpace;
        inputs.availableSpace.width = AvailableSpace::Definite(stretchWidth);
        inputs.verticalMarginsAreCollapsible =
            item.isInSameBfc ? LineBool::True() : LineBool::False();

        Optf clearThreshold = blockCtx->ClearedThreshold(item.clear);
        float clearPos = UnwrapOr(clearThreshold, -INFINITY);

        LayoutOutput itemLayout;
        if (item.isInSameBfc) {
            // A same-BFC child always has a defined width, from stretch
            // sizing.
            float width = UnwrapOr(knownDimensions.w, stretchWidth);

            // TODO(taffy): account for auto margins.
            float insetLeft = itemNonAutoMargin.left + contentBoxInset.left;
            float insetRight = containerOuterWidth - width - insetLeft;
            float insets[2] = {insetLeft, insetRight};

            BlockContext childBlockCtx = blockCtx->SubContext(
                F32Max(yOffsetForAbsolute + itemNonAutoMargin.top, clearPos),
                insets);
            itemLayout = tree->ComputeBlockChildLayout(item.nodeId, inputs,
                                                       &childBlockCtx);
            blockCtx->AddChildFloatedContentHeightContribution(
                yOffsetForAbsolute + childBlockCtx
                                         .FloatedContentHeightContribution());
            bool childFlags[2];
            childBlockCtx.GetTopAdjoiningFloats(childFlags);
            blockCtx->MergeAdjoiningFloats(childFlags);
        } else {
            itemLayout = tree->ComputeChildLayout(item.nodeId, inputs);
        }
        SizeF finalSize = itemLayout.size;

        CollapsibleMarginSet topMarginSet =
            itemLayout.topMargin
                .CollapseWithMargin(UnwrapOr(itemMargin.top, 0.0f));
        CollapsibleMarginSet bottomMarginSet =
            itemLayout.bottomMargin
                .CollapseWithMargin(UnwrapOr(itemMargin.bottom, 0.0f));

        // Expand auto margins to fill the available space. Vertical auto
        // margins on a relatively positioned block item simply resolve to 0.
        // https://www.w3.org/TR/CSS21/visudet.html#abs-non-replaced-width
        float freeXSpace = F32Max(0.0f, stretchWidth - finalSize.w);
        int autoMarginCount = (IsSome(itemMargin.left) ? 0 : 1) +
                              (IsSome(itemMargin.right) ? 0 : 1);
        float xAxisAutoMarginSize =
            autoMarginCount > 0 ? freeXSpace / (float)autoMarginCount : 0.0f;
        RectF resolvedMargin = {UnwrapOr(itemMargin.left, xAxisAutoMarginSize),
                                UnwrapOr(itemMargin.right, xAxisAutoMarginSize),
                                topMarginSet.Resolve(),
                                bottomMarginSet.Resolve()};

        RectFOpt inset = item.inset.MaybeResolveZip(
            {Some(containerInnerWidth), Some(0.0f)}, calc);
        Optf negRight = inset.right;
        if (IsSome(negRight)) {
            negRight = -negRight;
        }
        Optf negBottom = inset.bottom;
        if (IsSome(negBottom)) {
            negBottom = -negBottom;
        }
        PointF insetOffset = {IsRtl(direction)
                                  ? UnwrapOr(Or(negRight, inset.left), 0.0f)
                                  : UnwrapOr(Or(inset.left, negRight), 0.0f),
                              UnwrapOr(Or(inset.top, negBottom), 0.0f)};

        if (item.isInSameBfc && (!isCollapsingWithFirstMarginSet ||
                                 !ownMarginsCollapseWithChildren.start)) {
            yMarginOffset = activeCollapsibleMarginSet
                                .CollapseWithSet(topMarginSet)
                                .Resolve();
        }

        bool hasClearance = false;
        if (item.isInSameBfc && IsSome(clearThreshold)) {
            float hypotheticalY =
                committedYOffset + activeCollapsibleMarginSet
                                       .CollapseWithSet(topMarginSet)
                                       .Resolve();
            bool forcedClearance = blockCtx->HasAdjoiningFloat(item.clear);
            if (forcedClearance || hypotheticalY < clearThreshold) {
                hasClearance = true;
                float escapedMargin = isCollapsingWithFirstMarginSet &&
                                              ownMarginsCollapseWithChildren
                                                  .start
                                          ? activeCollapsibleMarginSet.Resolve()
                                          : 0.0f;
                yMarginOffset =
                    clearThreshold - committedYOffset - escapedMargin;
            }
        }

        item.computedSize = itemLayout.size;
        item.canBeCollapsedThrough =
            itemLayout.marginsCanCollapseThrough && !hasClearance;
        if (item.isInSameBfc) {
            float unclearedY = committedYOffset + activeCollapsibleMarginSet
                                                      .Resolve();
            item.staticPosition = {direction == Direction::Ltr
                                       ? resolvedContentBoxInset.left
                                       : containerOuterWidth -
                                             resolvedContentBoxInset.right -
                                             finalSize.w,
                                   F32Max(unclearedY, clearPos)};
        } else {
            // TODO(taffy): handle inset and margins.
            item.staticPosition = {direction == Direction::Ltr
                                       ? floatAvoidingPosition.x
                                       : floatAvoidingPosition.x +
                                             floatAvoidingWidth - finalSize.w,
                                   floatAvoidingPosition.y};
        }

        PointF location;
        if (item.isInSameBfc) {
            location = {direction == Direction::Ltr
                            ? resolvedContentBoxInset.left + insetOffset.x +
                                  resolvedMargin.left
                            : containerOuterWidth -
                                  resolvedContentBoxInset.right - finalSize.w -
                                  resolvedMargin.right + insetOffset.x,
                        committedYOffset + yMarginOffset + insetOffset.y};
        } else {
            float extraLeft = itemAvoidsFloats
                                  ? resolvedMargin.left - itemNonAutoMargin.left
                                  : resolvedMargin.left;
            float extraRight = itemAvoidsFloats ? resolvedMargin.right -
                                                      itemNonAutoMargin.right
                                                : resolvedMargin.right;
            location = {
                direction == Direction::Ltr
                    ? floatAvoidingPosition.x + extraLeft + insetOffset.x
                    : floatAvoidingPosition.x + floatAvoidingWidth -
                          finalSize.w - extraRight + insetOffset.x,
                floatAvoidingPosition.y + insetOffset.y};
        }

        // Legacy text-align on the block container shifts the item.
        float itemOuterWidth = itemLayout.size.w + resolvedMargin
                                                       .HorizontalAxisSum();
        if (itemOuterWidth < containerInnerWidth) {
            float free = containerInnerWidth - itemOuterWidth;
            switch (textAlign) {
                case TextAlign::LegacyLeft:
                    if (IsRtl(direction)) {
                        location.x -= free;
                    }
                    break;
                case TextAlign::LegacyRight:
                    if (!IsRtl(direction)) {
                        location.x += free;
                    }
                    break;
                case TextAlign::LegacyCenter:
                    location.x += IsRtl(direction) ? -free / 2.0f : free / 2.0f;
                    break;
                default:
                    break;
            }
        }

        if (!IsSome(res.firstBaseline) && IsSome(itemLayout.firstBaselines.y)) {
            res.firstBaseline = Some(location.y + itemLayout.firstBaselines.y);
        }

        // Held back so the align-content pass can shift location.y before the
        // layout is committed to the tree.
        item.hasFinalLayout = true;
        item.finalLayout.order = item.order;
        item.finalLayout.size = itemLayout.size;
        item.finalLayout.contentSize = itemLayout.contentSize;
        item.finalLayout.scrollbarSize = scrollbarSize;
        item.finalLayout.location = location;
        item.finalLayout.padding = item.padding;
        item.finalLayout.border = item.border;
        item.finalLayout.margin = resolvedMargin;

        res.inflowContentSize =
            Max(res.inflowContentSize,
                ComputeContentSizeContribution(
                    {IsRtl(direction)
                         ? containerOuterWidth - (location.x + finalSize.w) -
                               resolvedBorder.right
                         : location.x - resolvedBorder.left,
                     location.y - resolvedBorder.top},
                    finalSize, itemLayout.contentSize, item.overflow));

        if (isCollapsingWithFirstMarginSet && itemPushedBelowFloat) {
            isCollapsingWithFirstMarginSet = false;
        }
        if (isCollapsingWithFirstMarginSet && hasClearance) {
            isCollapsingWithFirstMarginSet = false;
        } else if (isCollapsingWithFirstMarginSet) {
            if (item.canBeCollapsedThrough) {
                res.firstChildTopMarginSet =
                    res.firstChildTopMarginSet.CollapseWithSet(topMarginSet)
                        .CollapseWithSet(bottomMarginSet);
            } else {
                res.firstChildTopMarginSet = res.firstChildTopMarginSet
                                                 .CollapseWithSet(topMarginSet);
                isCollapsingWithFirstMarginSet = false;
            }
        }

        if (item.canBeCollapsedThrough) {
            activeCollapsibleMarginSet = activeCollapsibleMarginSet
                                             .CollapseWithSet(topMarginSet)
                                             .CollapseWithSet(bottomMarginSet);
            yOffsetForAbsolute =
                committedYOffset + itemLayout.size.h + yMarginOffset;
        } else {
            committedYOffset = location.y - insetOffset.y + itemLayout.size.h;
            if (hasClearance && itemLayout.marginsCanCollapseThrough) {
                committedYOffset -= topMarginSet.Resolve();
                activeCollapsibleMarginSet =
                    topMarginSet.CollapseWithSet(bottomMarginSet);
                activeMarginSetHasClearance = true;
            } else {
                activeCollapsibleMarginSet = bottomMarginSet;
                activeMarginSetHasClearance = false;
            }
            yOffsetForAbsolute = committedYOffset + activeCollapsibleMarginSet
                                                        .Resolve();
            blockCtx->CommitStrut();
        }
    }

    res.lastChildBottomMarginSet = activeMarginSetHasClearance
                                       ? CollapsibleMarginSet{}
                                       : activeCollapsibleMarginSet;
    float bottomYMarginOffset = activeMarginSetHasClearance
                                    ? activeCollapsibleMarginSet.Resolve()
                                : ownMarginsCollapseWithChildren.end
                                    ? 0.0f
                                    : res.lastChildBottomMarginSet.Resolve();
    committedYOffset += resolvedContentBoxInset.bottom + bottomYMarginOffset;
    res.intrinsicOuterHeight = F32Max(0.0f, committedYOffset);
    return res;
}

// ─── perform_absolute_layout_on_absolute_children ────────────────────────

SizeF PerformAbsoluteLayoutOnAbsoluteChildren(TaffyTree* tree,
                                              const Vec<BlockItem>& items,
                                              SizeF areaSize, PointF areaOffset,
                                              Direction direction) {
    CalcResolver calc = tree->calc;
    float areaWidth = areaSize.w;
    float areaHeight = areaSize.h;
    SizeF absoluteContentSize = SizeF::Zero();

    for (int i = 0; i < len(items); i++) {
        const BlockItem& item = items[i];
        if (item.position != Position::Absolute) {
            continue;
        }
        const Style& cs = tree->GetStyle(item.nodeId);
        if (cs.BoxGenMode() == BoxGenerationMode::None ||
            cs.position != Position::Absolute) {
            continue;
        }

        Optf aspectRatio = cs.aspectRatio;
        RectFOpt margin = cs.margin.MaybeResolve(Some(areaWidth), calc);
        RectF padding = cs.padding.ResolveOrZero(Some(areaWidth), calc);
        RectF border = cs.border.ResolveOrZero(Some(areaWidth), calc);
        SizeF paddingBorderSum = (padding + border).SumAxes();
        SizeF boxSizingAdjustment = cs.boxSizing == BoxSizing::ContentBox
                                        ? paddingBorderSum
                                        : SizeF::Zero();

        RectFOpt inset = cs.inset.MaybeResolveZip(AsOptional(areaSize), calc);
        Optf left = inset.left;
        Optf right = inset.right;
        Optf top = inset.top;
        Optf bottom = inset.bottom;

        SizeFOpt styleSize = MaybeAdd(
            MaybeApplyAspectRatio(
                cs.size.MaybeResolve(AsOptional(areaSize), calc), aspectRatio),
            boxSizingAdjustment);
        SizeFOpt minSize = MaybeMax(
            Or(MaybeAdd(MaybeApplyAspectRatio(
                            cs.minSize.MaybeResolve(AsOptional(areaSize), calc),
                            aspectRatio),
                        boxSizingAdjustment),
               AsOptional(paddingBorderSum)),
            paddingBorderSum);
        SizeFOpt maxSize =
            MaybeAdd(MaybeApplyAspectRatio(
                         cs.maxSize.MaybeResolve(AsOptional(areaSize), calc),
                         aspectRatio),
                     boxSizingAdjustment);
        SizeFOpt knownDimensions = MaybeClamp(styleSize, minSize, maxSize);

        if (!IsSome(knownDimensions.w) && IsSome(left) && IsSome(right)) {
            float newWidthRaw =
                MaybeSub(MaybeSub(areaWidth, margin.left), margin.right) -
                left - right;
            knownDimensions.w = Some(F32Max(newWidthRaw, 0.0f));
            knownDimensions =
                MaybeClamp(MaybeApplyAspectRatio(knownDimensions, aspectRatio),
                           minSize, maxSize);
        }
        if (!IsSome(knownDimensions.h) && IsSome(top) && IsSome(bottom)) {
            float newHeightRaw =
                MaybeSub(MaybeSub(areaHeight, margin.top), margin.bottom) -
                top - bottom;
            knownDimensions.h = Some(F32Max(newHeightRaw, 0.0f));
            knownDimensions =
                MaybeClamp(MaybeApplyAspectRatio(knownDimensions, aspectRatio),
                           minSize, maxSize);
        }

        SizeAvail childAvail = {AvailableSpace::Definite(MaybeClamp(
                                    areaWidth, minSize.w, maxSize.w)),
                                AvailableSpace::Definite(MaybeClamp(
                                    areaHeight, minSize.h, maxSize.h))};

        SizeF measuredSize = tree->MeasureChildSizeBoth(
            item.nodeId, knownDimensions, AsOptional(areaSize), childAvail,
            SizingMode::ContentSize, LineBool::False());
        SizeF finalSize = MaybeClamp(UnwrapOr(knownDimensions, measuredSize),
                                     minSize, maxSize);

        LayoutOutput layoutOutput = tree->PerformChildLayout(
            item.nodeId, AsOptional(finalSize), AsOptional(areaSize),
            childAvail, SizingMode::ContentSize, LineBool::False());

        RectF nonAutoMargin = {
            IsSome(left) ? UnwrapOr(margin.left, 0.0f) : 0.0f,
            IsSome(right) ? UnwrapOr(margin.right, 0.0f) : 0.0f,
            IsSome(top) ? UnwrapOr(margin.top, 0.0f) : 0.0f,
            IsSome(bottom) ? UnwrapOr(margin.bottom, 0.0f) : 0.0f};

        // Auto margins on an absolutely positioned element in a block
        // container only resolve if the matching inset is set; otherwise they
        // resolve to 0.
        // https://www.w3.org/TR/CSS21/visudet.html#abs-non-replaced-width
        PointF absoluteAutoMarginSpace = {
            IsSome(right) ? areaSize.w - right - UnwrapOr(left, 0.0f)
                          : finalSize.w,
            IsSome(bottom) ? areaSize.h - bottom - UnwrapOr(top, 0.0f)
                           : finalSize.h};
        SizeF freeSpace = {absoluteAutoMarginSpace.x - finalSize.w -
                               nonAutoMargin.HorizontalAxisSum(),
                           absoluteAutoMarginSpace.y - finalSize.h -
                               nonAutoMargin.VerticalAxisSum()};

        int autoW =
            (IsSome(margin.left) ? 0 : 1) + (IsSome(margin.right) ? 0 : 1);
        int autoH =
            (IsSome(margin.top) ? 0 : 1) + (IsSome(margin.bottom) ? 0 : 1);
        SizeF autoMarginSize;
        if (autoW == 2 &&
            (!IsSome(styleSize.w) || styleSize.w >= freeSpace.w)) {
            autoMarginSize.w = 0.0f;
        } else if (autoW > 0) {
            autoMarginSize.w = freeSpace.w / (float)autoW;
        }
        if (autoH == 2 &&
            (!IsSome(styleSize.h) || styleSize.h >= freeSpace.h)) {
            autoMarginSize.h = 0.0f;
        } else if (autoH > 0) {
            autoMarginSize.h = freeSpace.h / (float)autoH;
        }
        RectF autoMargin = {IsSome(margin.left) ? 0.0f : autoMarginSize.w,
                            IsSome(margin.right) ? 0.0f : autoMarginSize.w,
                            IsSome(margin.top) ? 0.0f : autoMarginSize.h,
                            IsSome(margin.bottom) ? 0.0f : autoMarginSize.h};
        RectF resolvedMargin = {UnwrapOr(margin.left, autoMargin.left),
                                UnwrapOr(margin.right, autoMargin.right),
                                UnwrapOr(margin.top, autoMargin.top),
                                UnwrapOr(margin.bottom, autoMargin.bottom)};

        float xOffset;
        if (IsSome(left) && IsSome(right)) {
            xOffset = IsRtl(direction) ? areaSize.w - finalSize.w - right -
                                             resolvedMargin.right
                                       : left + resolvedMargin.left;
        } else if (IsSome(left)) {
            xOffset = left + resolvedMargin.left;
        } else if (IsSome(right)) {
            xOffset = areaSize.w - finalSize.w - right - resolvedMargin.right;
        } else {
            xOffset = IsRtl(direction) ? item.staticPosition.x - finalSize.w -
                                             resolvedMargin.right - areaOffset.x
                                       : item.staticPosition.x +
                                             resolvedMargin.left - areaOffset.x;
        }

        float yLocation;
        if (IsSome(top)) {
            yLocation = top + resolvedMargin.top + areaOffset.y;
        } else if (IsSome(bottom)) {
            yLocation = areaSize.h - finalSize.h - bottom -
                        resolvedMargin.bottom + areaOffset.y;
        } else {
            yLocation = item.staticPosition.y + resolvedMargin.top;
        }
        PointF location = {xOffset + areaOffset.x, yLocation};

        // The axes are switched: a scrollbar takes space in the axis opposite
        // the one it scrolls.
        SizeF scrollbarSize = {
            item.overflow.y == Overflow::Scroll ? item.scrollbarWidth : 0.0f,
            item.overflow.x == Overflow::Scroll ? item.scrollbarWidth : 0.0f};

        Layout layout;
        layout.order = item.order;
        layout.size = finalSize;
        layout.contentSize = layoutOutput.contentSize;
        layout.scrollbarSize = scrollbarSize;
        layout.location = location;
        layout.padding = padding;
        layout.border = border;
        layout.margin = resolvedMargin;
        tree->SetUnroundedLayout(item.nodeId, layout);

        PointF relativeLocation = {location.x - areaOffset.x,
                                   location.y - areaOffset.y};
        absoluteContentSize = Max(absoluteContentSize,
                                  ComputeContentSizeContribution(
                                      relativeLocation, finalSize,
                                      layoutOutput.contentSize, item.overflow));
    }

    return absoluteContentSize;
}

// ─── compute_inner ───────────────────────────────────────────────────────

LayoutOutput ComputeInner(TaffyTree* tree, NodeId nodeId,
                          const LayoutInput& inputs, BlockContext* blockCtx) {
    CalcResolver calc = tree->calc;
    SizeFOpt knownDimensionsIn = inputs.knownDimensions;
    SizeFOpt parentSize = inputs.parentSize;
    SizeAvail availableSpace = inputs.availableSpace;
    RunMode runMode = inputs.runMode;
    LineBool verticalMarginsAreCollapsible = inputs
                                                 .verticalMarginsAreCollapsible;

    const Style& style = tree->GetStyle(nodeId);
    RectLp rawPadding = style.padding;
    RectLp rawBorder = style.border;
    RectLpa rawMargin = style.margin;
    Optf aspectRatio = style.aspectRatio;
    RectF padding = rawPadding.ResolveOrZero(parentSize.w, calc);
    RectF border = rawBorder.ResolveOrZero(parentSize.w, calc);
    Direction direction = style.direction;

    // A node that scrolls vertically needs *horizontal* space reserved for
    // its scrollbar, hence the transposed axes.
    PointOverflow t = style.overflow.Transpose();
    PointF offsets = {t.x == Overflow::Scroll ? style.scrollbarWidth : 0.0f,
                      t.y == Overflow::Scroll ? style.scrollbarWidth : 0.0f};
    RectF scrollbarGutter = direction == Direction::Ltr
                                ? RectF{0.0f, offsets.x, 0.0f, offsets.y}
                                : RectF{offsets.x, 0.0f, 0.0f, offsets.y};
    RectF paddingBorder = padding + border;
    SizeF paddingBorderSize = paddingBorder.SumAxes();
    RectF contentBoxInset = paddingBorder + scrollbarGutter;

    float xInsets[2] = {contentBoxInset.left, contentBoxInset.right};
    blockCtx->ApplyContentBoxInset(xInsets);

    SizeF boxSizingAdjustment = style.boxSizing == BoxSizing::ContentBox
                                    ? paddingBorderSize
                                    : SizeF::Zero();
    SizeFOpt size =
        MaybeAdd(MaybeApplyAspectRatio(
                     style.size.MaybeResolve(parentSize, calc), aspectRatio),
                 boxSizingAdjustment);
    SizeFOpt minSize =
        MaybeAdd(MaybeApplyAspectRatio(
                     style.minSize.MaybeResolve(parentSize, calc), aspectRatio),
                 boxSizingAdjustment);
    SizeFOpt maxSize =
        MaybeAdd(MaybeApplyAspectRatio(
                     style.maxSize.MaybeResolve(parentSize, calc), aspectRatio),
                 boxSizingAdjustment);

    // css-sizing-4: a definite size in one axis transfers through
    // `aspect-ratio` to make the other definite. Deriving it from
    // knownDimensions self-gates the transfer — a block parent fills an axis
    // only when that is a real constraint (the stretched width at final
    // layout) and leaves it None while probing intrinsic sizes, so measure
    // passes stay content-based. Only a newly-filled axis is adopted and
    // clamped; an incoming known size is left as the parent resolved it,
    // since re-clamping would undo padding/border overrides.
    SizeFOpt derived =
        MaybeClamp(MaybeApplyAspectRatio(knownDimensionsIn, aspectRatio),
                   minSize, maxSize);
    SizeFOpt knownDimensions = {Or(knownDimensionsIn.w, derived.w),
                                Or(knownDimensionsIn.h, derived.h)};
    SizeFOpt containerContentBoxSize =
        MaybeSub(knownDimensions, contentBoxInset.SumAxes());

    bool isScrollContainer = IsScrollContainer(style.overflow.x) ||
                             IsScrollContainer(style.overflow.y);

    LineBool ownMarginsCollapseWithChildren = {
        verticalMarginsAreCollapsible.start && !isScrollContainer &&
            style.position == Position::Relative && padding.top == 0.0f &&
            border.top == 0.0f,
        verticalMarginsAreCollapsible.end && !isScrollContainer &&
            style.position == Position::Relative && padding.bottom == 0.0f &&
            border.bottom == 0.0f && !IsSome(size.h)};
    bool hasStylesPreventingBeingCollapsedThrough =
        !style.IsBlock() || blockCtx->IsBfcRoot() || isScrollContainer ||
        style.position == Position::Absolute || padding.top > 0.0f ||
        padding.bottom > 0.0f || border.top > 0.0f || border.bottom > 0.0f ||
        (IsSome(size.h) && size.h > 0.0f) ||
        (IsSome(minSize.h) && minSize.h > 0.0f);

    TextAlign textAlign = style.textAlign;
    OptAlignContent alignContent = style.alignContent;

    // 1. Generate items.
    Vec<BlockItem> items;
    GenerateItemList(tree, nodeId, containerContentBoxSize, &items);

    // 2. Compute the container width.
    float containerOuterWidth;
    if (IsSome(knownDimensions.w)) {
        containerOuterWidth = knownDimensions.w;
    } else {
        AvailableSpace availableWidth =
            MaybeSub(availableSpace.width, contentBoxInset.HorizontalAxisSum());
        float intrinsicWidth =
            DetermineContentBasedContainerWidth(tree, items, availableWidth) +
            contentBoxInset.HorizontalAxisSum();
        containerOuterWidth =
            MaybeMax(MaybeClamp(intrinsicWidth, minSize.w, maxSize.w),
                     Some(paddingBorderSize.w));
    }

    if (runMode == RunMode::ComputeSize && IsSome(knownDimensions.h)) {
        return LayoutOutput::FromOuterSize(
            {containerOuterWidth, knownDimensions.h});
    }
    if (runMode == RunMode::ComputeSize &&
        inputs.axis == RequestedAxis::Horizontal) {
        return LayoutOutput::FromOuterSize({containerOuterWidth, 0.0f});
    }

    Optf containerPercentageResolutionHeight =
        Or(Or(knownDimensions.h, MaybeMax(size.h, minSize.h)), minSize.h);

    // 3. Final item layout.
    RectF resolvedPadding = rawPadding
                                .ResolveOrZero(Some(containerOuterWidth), calc);
    RectF resolvedBorder = rawBorder
                               .ResolveOrZero(Some(containerOuterWidth), calc);
    RectF resolvedContentBoxInset =
        resolvedPadding + resolvedBorder + scrollbarGutter;

    InFlowResult inFlow = PerformFinalLayoutOnInFlowChildren(
        tree, runMode, &items, containerOuterWidth,
        containerPercentageResolutionHeight, contentBoxInset,
        resolvedContentBoxInset, resolvedBorder, textAlign, direction,
        ownMarginsCollapseWithChildren, blockCtx);
    SizeF inflowContentSize = inFlow.inflowContentSize;
    float intrinsicOuterHeight = inFlow.intrinsicOuterHeight;

    // A root BFC contains its floats.
    if (blockCtx->IsBfcRoot() || isScrollContainer) {
        intrinsicOuterHeight = F32Max(
            intrinsicOuterHeight, blockCtx->FloatedContentHeightContribution());
    }

    float containerOuterHeight = MaybeMax(
        UnwrapOr(knownDimensions.h,
                 MaybeClamp(intrinsicOuterHeight, minSize.h, maxSize.h)),
        Some(paddingBorderSize.h));
    SizeF finalOuterSize = {containerOuterWidth, containerOuterHeight};

    // Apply align-content to the in-flow non-floated items. Block layout
    // treats the whole stack of in-flow children as one alignment subject, so
    // the distribution keywords must take the single-subject fallback
    // unconditionally — which is what passing numItems = 1 does. The group
    // then shifts by one offset, with no inter-item gap.
    if (alignContent.IsSome()) {
        float containerInnerHeight =
            containerOuterHeight - resolvedContentBoxInset.VerticalAxisSum();
        float inflowContentHeight =
            intrinsicOuterHeight - resolvedContentBoxInset.VerticalAxisSum();
        float freeSpace = containerInnerHeight - inflowContentHeight;
        bool anyInFlow = false;
        for (int i = 0; i < len(items); i++) {
            if (items[i].hasFinalLayout) {
                anyInFlow = true;
                break;
            }
        }
        if (anyInFlow) {
            AlignContentKeyword keyword =
                ApplyAlignmentFallback(freeSpace, 1, alignContent.val);
            float groupOffset = ComputeAlignmentOffset(freeSpace, 1, 0.0f,
                                                       keyword, false, true);
            if (IsSome(inFlow.firstBaseline)) {
                inFlow.firstBaseline += groupOffset;
            }
            for (int i = 0; i < len(items); i++) {
                if (items[i].hasFinalLayout) {
                    items[i].finalLayout.location.y += groupOffset;
                }
            }
            inflowContentSize = SizeF::Zero();
            for (int i = 0; i < len(items); i++) {
                if (!items[i].hasFinalLayout) {
                    continue;
                }
                const Layout& l = items[i].finalLayout;
                inflowContentSize = Max(
                    inflowContentSize,
                    ComputeContentSizeContribution(
                        {IsRtl(direction)
                             ? containerOuterWidth - (l.location.x + l.size.w) -
                                   resolvedBorder.right
                             : l.location.x - resolvedBorder.left,
                         l.location.y - resolvedBorder.top},
                        l.size, l.contentSize, items[i].overflow));
            }
        }
    }

    bool allInFlowChildrenCanBeCollapsedThrough = true;
    for (int i = 0; i < len(items); i++) {
        if (IsFloated(items[i].floatMode)) {
            continue;
        }
        if (items[i].position != Position::Absolute &&
            !items[i].canBeCollapsedThrough) {
            allInFlowChildrenCanBeCollapsedThrough = false;
            break;
        }
    }
    bool canBeCollapsedThrough = !hasStylesPreventingBeingCollapsedThrough &&
                                 allInFlowChildrenCanBeCollapsedThrough;

    LayoutOutput output;
    output.size = finalOuterSize;
    output.firstBaselines.y = inFlow.firstBaseline;
    output.topMargin =
        ownMarginsCollapseWithChildren.start
            ? inFlow.firstChildTopMarginSet
            : CollapsibleMarginSet::FromMargin(UnwrapOr(
                  rawMargin.MaybeResolve(parentSize.w, calc).top, 0.0f));
    output.bottomMargin =
        ownMarginsCollapseWithChildren.end
            ? inFlow.lastChildBottomMarginSet
            : CollapsibleMarginSet::FromMargin(UnwrapOr(
                  rawMargin.MaybeResolve(parentSize.w, calc).bottom, 0.0f));
    output.marginsCanCollapseThrough = canBeCollapsedThrough;

    // The margin-collapsing outputs matter to the parent's intrinsic height,
    // so they are returned even when only the size was asked for.
    if (runMode == RunMode::ComputeSize) {
        return output;
    }

    // Commit the deferred in-flow layouts. Floated items already wrote theirs.
    for (int i = 0; i < len(items); i++) {
        if (items[i].hasFinalLayout) {
            tree->SetUnroundedLayout(items[i].nodeId, items[i].finalLayout);
        }
    }

    // 4. Absolutely positioned children.
    RectF absolutePositionInset = resolvedBorder + scrollbarGutter;
    SizeF absolutePositionArea = finalOuterSize - absolutePositionInset
                                                      .SumAxes();
    PointF absolutePositionOffset = {absolutePositionInset.left,
                                     absolutePositionInset.top};
    SizeF absoluteContentSize = PerformAbsoluteLayoutOnAbsoluteChildren(
        tree, items, absolutePositionArea, absolutePositionOffset, direction);

    inflowContentSize
        .w += IsRtl(direction) ? resolvedPadding.left : resolvedPadding.right;
    inflowContentSize.h += resolvedPadding.bottom;
    output.contentSize = Max(inflowContentSize, absoluteContentSize);

    // 5. Hidden children.
    int len = tree->ChildCount(nodeId);
    for (int order = 0; order < len; order++) {
        NodeId child = tree->GetChildId(nodeId, order);
        if (tree->GetStyle(child).BoxGenMode() == BoxGenerationMode::None) {
            tree->SetUnroundedLayout(child, Layout::WithOrder((uint32_t)order));
            tree->PerformChildLayout(
                child, SizeFOptNone(), SizeFOptNone(), SizeAvail::MaxContent(),
                SizingMode::InherentSize, LineBool::False());
        }
    }

    return output;
}

} // namespace

// ─── compute_block_layout ────────────────────────────────────────────────

LayoutOutput ComputeBlockLayout(TaffyTree* tree, NodeId nodeId,
                                const LayoutInput& inputs,
                                BlockContext* blockCtx) {
    CalcResolver calc = tree->calc;
    SizeFOpt knownDimensions = inputs.knownDimensions;
    SizeFOpt parentSize = inputs.parentSize;
    RunMode runMode = inputs.runMode;
    const Style& style = tree->GetStyle(nodeId);

    bool isScrollContainer = IsScrollContainer(style.overflow.x) ||
                             IsScrollContainer(style.overflow.y);
    Optf aspectRatio = style.aspectRatio;
    RectF padding = style.padding.ResolveOrZero(parentSize.w, calc);
    RectF border = style.border.ResolveOrZero(parentSize.w, calc);
    SizeF paddingBorderSize = (padding + border).SumAxes();
    SizeF boxSizingAdjustment = style.boxSizing == BoxSizing::ContentBox
                                    ? paddingBorderSize
                                    : SizeF::Zero();

    SizeFOpt minSize =
        MaybeAdd(MaybeApplyAspectRatio(
                     style.minSize.MaybeResolve(parentSize, calc), aspectRatio),
                 boxSizingAdjustment);
    SizeFOpt maxSize =
        MaybeAdd(MaybeApplyAspectRatio(
                     style.maxSize.MaybeResolve(parentSize, calc), aspectRatio),
                 boxSizingAdjustment);
    SizeFOpt clampedStyleSize = SizeFOptNone();
    if (inputs.sizingMode == SizingMode::InherentSize) {
        clampedStyleSize = MaybeClamp(
            MaybeAdd(
                MaybeApplyAspectRatio(style.size.MaybeResolve(parentSize, calc),
                                      aspectRatio),
                boxSizingAdjustment),
            minSize, maxSize);
    }

    // If both min and max are set in an axis and max <= min, that determines
    // the size in that axis.
    SizeFOpt minMaxDefiniteSize = SizeFOptNone();
    if (IsSome(minSize.w) && IsSome(maxSize.w) && maxSize.w <= minSize.w) {
        minMaxDefiniteSize.w = minSize.w;
    }
    if (IsSome(minSize.h) && IsSome(maxSize.h) && maxSize.h <= minSize.h) {
        minMaxDefiniteSize.h = minSize.h;
    }

    SizeFOpt styledBasedKnownDimensions =
        MaybeMax(Or(Or(knownDimensions, minMaxDefiniteSize), clampedStyleSize),
                 paddingBorderSize);

    if (runMode == RunMode::ComputeSize) {
        if (BothAxisDefined(styledBasedKnownDimensions)) {
            return LayoutOutput::FromOuterSize(
                {styledBasedKnownDimensions.w, styledBasedKnownDimensions.h});
        }
        if (inputs.axis == RequestedAxis::Horizontal &&
            IsSome(styledBasedKnownDimensions.w)) {
            return LayoutOutput::FromOuterSize(
                {styledBasedKnownDimensions.w, 0.0f});
        }
    }

    LayoutInput next = inputs;
    next.knownDimensions = styledBasedKnownDimensions;

    // Use the block formatting context that was passed down, unless this node
    // is a scroll container, in which case it starts one of its own.
    if (blockCtx && !isScrollContainer) {
        return ComputeInner(tree, nodeId, next, blockCtx);
    }
    BlockFormattingContext rootBfc;
    BlockContext rootCtx;
    rootCtx.bfc = &rootBfc;
    rootCtx.isRoot = true;
    LayoutOutput out = ComputeInner(tree, nodeId, next, &rootCtx);
    VecReset(rootBfc.floatContext.leftFloats);
    VecReset(rootBfc.floatContext.rightFloats);
    VecReset(rootBfc.floatContext.segments);
    return out;
}

} // namespace taffy
