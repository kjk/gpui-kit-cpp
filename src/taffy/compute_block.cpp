/* The CSS block layout algorithm, for a block container holding only
 * block-level boxes — taffy/src/compute/block.rs — together with the float
 * positioning context it uses, taffy/src/compute/float.rs.
 *
 * The two live in one file here because `BlockContext` is the only thing that
 * ever touches a `FloatContext`, and nothing outside block layout names
 * either of them.
 */

#include "taffy/compute.h"
#include "taffy/sizing_keyword.h"

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

// Percentage widths and margins that sum to the container can exceed it by
// a fraction of a pixel. A fit check that does not absorb that wraps a box
// that should sit on the same line.
static constexpr float kFitTolerance = 0.001f;

static bool FloatFitsHorizontally(float width, FloatDirection direction,
                                  float bfcWidth, const float floatInsets[2],
                                  const float cbInsets[2]) {
    int lead = (int)direction;
    int trail = 1 - lead;
    float xInset = F32Max(floatInsets[lead], cbInsets[lead]);
    bool fitsOpposite =
        floatInsets[trail] == 0.0f ||
        xInset + width <= bfcWidth - floatInsets[trail] + kFitTolerance;
    bool fitsContaining =
        floatInsets[lead] == 0.0f ||
        xInset + width <= bfcWidth - cbInsets[trail] + kFitTolerance;
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
    // Running sums of adjacent uncleared floats on the left and the right.
    float sideSums[2] = {};
    float contribution = 0.0f;
    float widest = 0.0f;

    void AddFloat(float width, FloatDirection direction, Clear clear) {
        switch (availableWidth.kind) {
            case AvailableSpace::Kind::Definite:
            case AvailableSpace::Kind::MaxContent:
                // A float that clears a side is placed below the floats on
                // that side, so its width does not accumulate with theirs.
                if (clear == Clear::Left || clear == Clear::Both) {
                    sideSums[0] = 0.0f;
                }
                if (clear == Clear::Right || clear == Clear::Both) {
                    sideSums[1] = 0.0f;
                }
                sideSums[(int)direction] += width;
                contribution = F32Max(contribution, sideSums[0] + sideSums[1]);
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
    // The height that floats take up in this element. Negative infinity when
    // the subtree contains no floats, so a child with none does not lift a
    // negative contribution back to zero.
    float floatContentContribution = -INFINITY;
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
    // Raw width/height, so a sizing keyword can be told from auto. In the
    // block axis only stretch differs from auto; the intrinsic keywords are
    // the content size an auto height already produces.
    SizeDim sizeStyle;
    SizeFOpt minSize = SizeFOptNone();
    SizeFOpt maxSize = SizeFOptNone();

    PointOverflow overflow;
    Contain contain;
    float scrollbarWidth = 0.0f;

    Position position = Position::Static;
    // Used to resolve self-start/self-end of an out-of-flow child.
    Direction direction = Direction::Ltr;
    OptAlignSelf alignSelf;
    OptAlignSelf justifySelf;
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

    // Pending layout, held back from the tree so the post-loop align-content
    // pass can shift location.y first. Floats are held the same way.
    bool hasFinalLayout = false;
    Layout finalLayout;
    // Candidates bubbled from this in-flow child, indexes into the pass pool.
    int oofStart = 0;
    int oofCount = 0;
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
        // Percentage padding and border resolve against the containing
        // block's width on every side, including when its height is auto.
        RectF padding = cs.padding.ResolveOrZero(nodeInnerSize.w, calc);
        RectF border = cs.border.ResolveOrZero(nodeInnerSize.w, calc);
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
        item.direction = cs.direction;
        item.alignSelf = cs.alignSelf;
        item.justifySelf = cs.justifySelf;
        item.overflow = cs.overflow;
        item.contain = cs.contain;
        item.scrollbarWidth = cs.scrollbarWidth;

        bool isNotFloated = cs.floatMode == Float::None;
        bool isScrollContainer = IsScrollContainer(cs.overflow.x) ||
                                 IsScrollContainer(cs.overflow.y);
        // Layout and paint containment establish an independent formatting
        // context, so the child is not in its parent's BFC.
        item.isInSameBfc =
            cs.IsBlock() && !cs.itemIsTable && !IsOutOfFlow(cs.position) &&
            isNotFloated && !isScrollContainer &&
            !cs.contain.EstablishesIndependentFormattingContext();

        item.sizeStyle = cs.size;
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

// In the block axis, min-content, max-content and fit-content are the content
// size an auto height already resolves to. Only stretch is a definite height.
Optf ResolveStretchHeight(Dimension heightStyle, Optf containerInnerHeight,
                          float itemYMarginSum, CalcResolver calc) {
    SizingKeywordResolution resolved = ResolveSizingKeyword(
        heightStyle, MaybeSub(containerInnerHeight, Some(itemYMarginSum)),
        containerInnerHeight, calc);
    if (resolved.isSome && resolved.kind == SizingKeywordKind::Exact) {
        return Some(resolved.exact);
    }
    return None();
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
        if (IsOutOfFlow(item.position)) {
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
            SizingKeywordResolution keyword = ResolveSizingKeyword(
                item.sizeStyle.width, None(), None(), calc);
            if (keyword.isSome && keyword.kind == SizingKeywordKind::Exact) {
                childAvail.width = AvailableSpace::Definite(keyword.exact);
            } else if (keyword.isSome) {
                childAvail.width = keyword.measure;
            } else {
                childAvail.width = MaybeSub(childAvail.width, itemXMarginSum);
            }
            // The same collapsibility as the child's final layout, so the
            // cache mode stays constant for the pass.
            width = tree->MeasureChildSize(
                item.nodeId, knownDimensions, SizeFOptNone(), childAvail,
                SizingMode::InherentSize, AbsoluteAxis::Horizontal,
                item.isInSameBfc ? LineBool::True() : LineBool::False());
        }
        width = F32Max(width, item.paddingBorderSum.w) + itemXMarginSum;

        if (IsFloated(item.floatMode)) {
            floatContribution
                .AddFloat(width, FloatDir(item.floatMode).val, item.clear);
            continue;
        }
        maxChildWidth = F32Max(maxChildWidth, width);
    }

    return F32Max(maxChildWidth, floatContribution.Result());
}

// ─── perform_final_layout_on_in_flow_children ────────────────────────────

struct InFlowResult {
    RectF inflowOverflowRect;
    float intrinsicOuterHeight = 0.0f;
    CollapsibleMarginSet firstChildTopMarginSet;
    CollapsibleMarginSet lastChildBottomMarginSet;
    Optf firstBaseline = None();
};

InFlowResult PerformFinalLayoutOnInFlowChildren(
    TaffyTree* tree, RunMode runMode, Vec<BlockItem>* items,
    Vec<OofCandidate>* oofPool, float containerOuterWidth,
    Optf containerPercentageResolutionHeight, RectF contentBoxInset,
    RectF resolvedContentBoxInset, RectF resolvedBorder, TextAlign textAlign,
    Direction direction, LineBool ownMarginsCollapseWithChildren,
    bool isScrollContainer, BlockContext* blockCtx) {
    CalcResolver calc = tree->calc;
    // Padding or border wider than the box floors the content width at zero.
    // A negative width would be passed down as a known size and available
    // space.
    float containerInnerWidth =
        F32Max(0.0f, containerOuterWidth - resolvedContentBoxInset
                                               .HorizontalAxisSum());
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
        if (IsOutOfFlow(item.position)) {
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

            float availableWidth =
                F32Max(0.0f, containerInnerWidth - itemNonAutoXMarginSum);
            Optf itemKnownWidth = None();
            AvailableSpace itemAvailableWidth =
                AvailableSpace::Definite(availableWidth);
            SizingKeywordResolution keywordWidth =
                ResolveSizingKeyword(item.sizeStyle.width, Some(availableWidth),
                                     Some(containerInnerWidth), calc);
            if (keywordWidth.isSome &&
                keywordWidth.kind == SizingKeywordKind::Measure) {
                itemAvailableWidth = keywordWidth.measure;
            } else if (keywordWidth.isSome) {
                itemKnownWidth = Some(keywordWidth.exact);
                itemAvailableWidth =
                    AvailableSpace::Definite(keywordWidth.exact);
            }
            Optf itemKnownHeight = ResolveStretchHeight(
                item.sizeStyle.height, percentageResolutionHeight,
                itemNonAutoMargin.VerticalAxisSum(), calc);
            LayoutOutput itemLayout = tree->PerformChildLayout(
                item.nodeId, {itemKnownWidth, itemKnownHeight}, parentSize,
                {itemAvailableWidth, AvailableSpace::MaxContent()},
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

            // Deferred to the post-loop pass in ComputeInner, the same way
            // in-flow items are, so align-content can shift location.y
            // before the layout is committed.
            item.hasFinalLayout = true;
            item.finalLayout.order = item.order;
            item.finalLayout.size = itemLayout.size;
            item.finalLayout
                .scrollableOverflowRect = itemLayout.scrollableOverflowRect;
            item.finalLayout.scrollbarSize = scrollbarSize;
            item.finalLayout.location = location;
            item.finalLayout.padding = item.padding;
            item.finalLayout.border = item.border;
            item.finalLayout.margin = itemNonAutoMargin;
            OofCandRange range;
            OofAppendRange(oofPool, &range, itemLayout.oofCandidates);
            item.oofStart = range.start;
            item.oofCount = range.count;

            res.inflowOverflowRect = res.inflowOverflowRect.Union(
                ComputeScrollableOverflowContribution(
                    {IsRtl(direction) ? containerOuterWidth -
                                            (location.x + itemLayout.size.w) -
                                            resolvedBorder.right
                                      : location.x - resolvedBorder.left,
                     location.y - resolvedBorder.top},
                    itemLayout.size, itemLayout.scrollableOverflowRect,
                    item.overflow, item.contain, isScrollContainer));
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
            stretchWidth =
                F32Max(0.0f, containerInnerWidth - itemNonAutoXMarginSum);
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
                                 F32Max(F32Max(slot.stretchWidth, minAutoWidth),
                                        0.0f)),
                        item.minSize.w, item.maxSize.w);
                    if (width <= slot.borderWidth + kFitTolerance) {
                        break;
                    }
                    after = slot.segmentId;
                }
                itemPushedBelowFloat = slot.y > minY;
                hasActiveFloats = slot.segmentId >= 0;
                itemAvoidsFloats = true;
                stretchWidth =
                    F32Max(F32Max(slot.stretchWidth, minAutoWidth), 0.0f);
                floatAvoidingPosition = {slot.x, slot.y};
                floatAvoidingWidth = slot.borderWidth;
            } else {
                stretchWidth =
                    F32Max(0.0f, containerInnerWidth - itemNonAutoXMarginSum);
                floatAvoidingPosition = {resolvedContentBoxInset.left, minY};
                floatAvoidingWidth = containerInnerWidth;
            }
        }

        SizeFOpt knownDimensions = SizeFOptNone();
        if (!item.isTable && !item.isReplaced) {
            Optf keywordWidth = None();
            SizingKeywordResolution keyword =
                ResolveSizingKeyword(item.sizeStyle.width, Some(stretchWidth),
                                     Some(containerInnerWidth), calc);
            if (keyword.isSome && keyword.kind == SizingKeywordKind::Exact) {
                keywordWidth = Some(keyword.exact);
            } else if (keyword.isSome) {
                keywordWidth = Some(tree->MeasureChildSize(
                    item.nodeId, SizeFOptNone(), parentSize,
                    {keyword.measure, AvailableSpace::MaxContent()},
                    SizingMode::InherentSize, AbsoluteAxis::Horizontal,
                    item.isInSameBfc ? LineBool::True() : LineBool::False()));
            }
            Optf keywordHeight = ResolveStretchHeight(
                item.sizeStyle.height, percentageResolutionHeight,
                itemNonAutoMargin.VerticalAxisSum(), calc);
            SizeFOpt sized = item.size;
            sized.w = Some(
                MaybeClamp(UnwrapOr(Or(sized.w, keywordWidth), stretchWidth),
                           item.minSize.w, item.maxSize.w));
            sized.h = Or(sized.h, keywordHeight);
            knownDimensions = MaybeClamp(sized, item.minSize, item.maxSize);
        }

        LayoutInput inputs;
        inputs.runMode = runMode;
        inputs.sizingMode = SizingMode::InherentSize;
        inputs.axis = RequestedAxis::Both;
        inputs.knownDimensions = knownDimensions;
        // Stretch gives this child a definite size in both axes.
        inputs.knownDimensionsAreDefiniteW = true;
        inputs.knownDimensionsAreDefiniteH = true;
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

        PointF insetOffset = {0.0f, 0.0f};
        if (item.position == Position::Relative) {
            RectFOpt inset = item.inset.MaybeResolveZip(
                {Some(containerInnerWidth), percentageResolutionHeight}, calc);
            Optf negRight = inset.right;
            if (IsSome(negRight)) {
                negRight = -negRight;
            }
            Optf negBottom = inset.bottom;
            if (IsSome(negBottom)) {
                negBottom = -negBottom;
            }
            insetOffset = {IsRtl(direction)
                               ? UnwrapOr(Or(negRight, inset.left), 0.0f)
                               : UnwrapOr(Or(inset.left, negRight), 0.0f),
                           UnwrapOr(Or(inset.top, negBottom), 0.0f)};
        }

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

        // The first in-flow child that has a baseline supplies the
        // container's. A scroll container's baseline is its content at the
        // initial scroll position, clamped to the border box. With none of
        // its own, it synthesizes one from the bottom edge.
        // https://github.com/w3c/csswg-drafts/issues/7660
        if (!IsSome(res.firstBaseline)) {
            Optf childBaseline = itemLayout.baselines.first;
            if (IsScrollContainer(item.overflow.y)) {
                float raw = UnwrapOr(childBaseline, itemLayout.size.h);
                childBaseline =
                    Some(F32Max(0.0f, F32Min(raw, itemLayout.size.h)));
            }
            if (IsSome(childBaseline)) {
                res.firstBaseline = Some(location.y + childBaseline);
            }
        }

        // Held back so the align-content pass can shift location.y before the
        // layout is committed to the tree.
        item.hasFinalLayout = true;
        item.finalLayout.order = item.order;
        item.finalLayout.size = itemLayout.size;
        item.finalLayout.scrollableOverflowRect = itemLayout
                                                      .scrollableOverflowRect;
        item.finalLayout.scrollbarSize = scrollbarSize;
        item.finalLayout.location = location;
        item.finalLayout.padding = item.padding;
        item.finalLayout.border = item.border;
        // Used auto margins include the float-avoidance offset. The
        // alignment margins above, which positioned the box, do not.
        if (itemAvoidsFloats) {
            if (!IsSome(itemMargin.left)) {
                resolvedMargin.left +=
                    floatAvoidingPosition.x - resolvedContentBoxInset.left;
            }
            if (!IsSome(itemMargin.right)) {
                resolvedMargin.right +=
                    containerOuterWidth - resolvedContentBoxInset.right -
                    floatAvoidingPosition.x - floatAvoidingWidth;
            }
        }
        item.finalLayout.margin = resolvedMargin;
        OofCandRange range;
        OofAppendRange(oofPool, &range, itemLayout.oofCandidates);
        item.oofStart = range.start;
        item.oofCount = range.count;

        res.inflowOverflowRect =
            res.inflowOverflowRect.Union(ComputeScrollableOverflowContribution(
                {IsRtl(direction)
                     ? containerOuterWidth - (location.x + finalSize.w) -
                           resolvedBorder.right
                     : location.x - resolvedBorder.left,
                 location.y - resolvedBorder.top},
                finalSize, itemLayout.scrollableOverflowRect, item.overflow,
                item.contain, isScrollContainer));

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
    // A known height imposed by a parent is a percentage basis only when
    // that parent says the height is definite. Width always is.
    SizeFOpt percentageBasisDimensions = knownDimensions;
    if (!inputs.knownDimensionsAreDefiniteH) {
        percentageBasisDimensions.h = None();
    }
    SizeFOpt containerContentBoxSize =
        MaybeSub(percentageBasisDimensions, contentBoxInset.SumAxes());

    bool isScrollContainer = IsScrollContainer(style.overflow.x) ||
                             IsScrollContainer(style.overflow.y);
    // css-align-3 §5.1.1: a non-normal align-content makes a block container
    // establish an independent formatting context. Layout and paint
    // containment do too.
    bool establishesNewBfc = isScrollContainer || style.alignContent.IsSome() ||
                             style.contain
                                 .EstablishesIndependentFormattingContext();

    LineBool ownMarginsCollapseWithChildren = {
        verticalMarginsAreCollapsible.start && !establishesNewBfc &&
            !IsOutOfFlow(style.position) && padding.top == 0.0f &&
            border.top == 0.0f,
        verticalMarginsAreCollapsible.end && !establishesNewBfc &&
            !IsOutOfFlow(style.position) && padding.bottom == 0.0f &&
            border.bottom == 0.0f && !IsSome(size.h)};
    bool hasStylesPreventingBeingCollapsedThrough =
        !style.IsBlock() || blockCtx->IsBfcRoot() || establishesNewBfc ||
        IsOutOfFlow(style.position) || padding.top > 0.0f ||
        padding.bottom > 0.0f || border.top > 0.0f || border.bottom > 0.0f;

    TextAlign textAlign = style.textAlign;
    OptAlignContent alignContent = style.alignContent;
    OptAlignItems justifyItems = style.justifyItems;

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

    // A min-height on its own is not a percentage basis. An indefinite
    // height stays indefinite even when min-height is set. Under
    // ContentSize the box's own height and min-height are ignored, so they
    // are not a percentage basis for children either: a content-size
    // suggestion must not be influenced by the specified height.
    Optf ownHeightBasis = inputs.sizingMode == SizingMode::InherentSize
                              ? MaybeMax(size.h, minSize.h)
                              : None();
    Optf containerPercentageResolutionHeight =
        Or(percentageBasisDimensions.h, ownHeightBasis);

    // 3. Final item layout.
    // Percentage padding and borders resolve against the containing block's
    // width, not this box's own width. They differ when the box does not
    // stretch. An unknown parent size falls back to this box's width, which
    // is the root of the tree.
    float percentageResolutionWidth =
        UnwrapOr(parentSize.w, containerOuterWidth);
    RectF resolvedPadding =
        rawPadding.ResolveOrZero(Some(percentageResolutionWidth), calc);
    RectF resolvedBorder =
        rawBorder.ResolveOrZero(Some(percentageResolutionWidth), calc);
    RectF resolvedContentBoxInset =
        resolvedPadding + resolvedBorder + scrollbarGutter;

    Vec<OofCandidate> oofPool;
    InFlowResult inFlow = PerformFinalLayoutOnInFlowChildren(
        tree, runMode, &items, &oofPool, containerOuterWidth,
        containerPercentageResolutionHeight, contentBoxInset,
        resolvedContentBoxInset, resolvedBorder, textAlign, direction,
        ownMarginsCollapseWithChildren, isScrollContainer, blockCtx);
    RectF inflowOverflowRect = inFlow.inflowOverflowRect;
    float intrinsicOuterHeight = inFlow.intrinsicOuterHeight;

    // A root BFC contains its floats. A non-normal align-content does too.
    if (blockCtx->IsBfcRoot() || establishesNewBfc) {
        intrinsicOuterHeight = F32Max(
            intrinsicOuterHeight, blockCtx->FloatedContentHeightContribution());
    }

    float containerOuterHeight = MaybeMax(
        UnwrapOr(knownDimensions.h,
                 MaybeClamp(intrinsicOuterHeight, minSize.h, maxSize.h)),
        Some(paddingBorderSize.h));
    SizeF finalOuterSize = {containerOuterWidth, containerOuterHeight};

    // CSS2 §8.3.1: the bottom margin of a block with height: auto collapses
    // with its last in-flow child's bottom margin only if min-height is less
    // than the used height. When min-height determines the used height, the
    // last child's bottom margin stays inside the box. max-height does not.
    bool heightConstrainedByMinHeight = IsSome(minSize.h) && minSize.h > 0.0f &&
                                        minSize.h >= containerOuterHeight;
    bool ownBottomMarginCollapsesWithChildren =
        ownMarginsCollapseWithChildren.end && !heightConstrainedByMinHeight;

    // Apply align-content to the deferred children, floats included. Block
    // layout treats the whole stack as one alignment subject, so the
    // distribution keywords take the single-subject fallback. Passing
    // numItems = 1 does that. The group then shifts by one offset, with
    // no inter-item gap.
    if (alignContent.IsSome()) {
        float containerInnerHeight =
            containerOuterHeight - resolvedContentBoxInset.VerticalAxisSum();
        float inflowContentHeight =
            intrinsicOuterHeight - resolvedContentBoxInset.VerticalAxisSum();
        float freeSpace = containerInnerHeight - inflowContentHeight;
        // The offset is applied even when every child is out of flow: their
        // static positions move with the in-flow content.
        AlignContentKeyword keyword =
            ApplyAlignmentFallback(freeSpace, 1, alignContent.val);
        float groupOffset =
            ComputeAlignmentOffset(freeSpace, 1, 0.0f, keyword, false, true);
        if (IsSome(inFlow.firstBaseline)) {
            inFlow.firstBaseline += groupOffset;
        }
        bool anyInFlow = false;
        for (int i = 0; i < len(items); i++) {
            if (items[i].hasFinalLayout) {
                items[i].finalLayout.location.y += groupOffset;
                anyInFlow = true;
            }
            if (IsOutOfFlow(items[i].position)) {
                items[i].staticPosition.y += groupOffset;
            }
        }
        if (anyInFlow) {
            inflowOverflowRect = RectF::Zero();
            for (int i = 0; i < len(items); i++) {
                if (!items[i].hasFinalLayout) {
                    continue;
                }
                const Layout& l = items[i].finalLayout;
                inflowOverflowRect = inflowOverflowRect.Union(
                    ComputeScrollableOverflowContribution(
                        {IsRtl(direction)
                             ? containerOuterWidth - (l.location.x + l.size.w) -
                                   resolvedBorder.right
                             : l.location.x - resolvedBorder.left,
                         l.location.y - resolvedBorder.top},
                        l.size, l.scrollableOverflowRect, items[i].overflow,
                        items[i].contain, isScrollContainer));
            }
        }
    }

    bool allInFlowChildrenCanBeCollapsedThrough = true;
    for (int i = 0; i < len(items); i++) {
        if (IsFloated(items[i].floatMode)) {
            continue;
        }
        if (!IsOutOfFlow(items[i].position) && !items[i]
                                                    .canBeCollapsedThrough) {
            allInFlowChildrenCanBeCollapsedThrough = false;
            break;
        }
    }
    // CSS2 8.3.1: top and bottom margins collapse through a box only when
    // its used height is zero. height, min-height and max-height are judged
    // by the height they resolve to, not by the specified value being Some.
    bool canBeCollapsedThrough = !hasStylesPreventingBeingCollapsedThrough &&
                                 containerOuterHeight == 0.0f &&
                                 allInFlowChildrenCanBeCollapsedThrough;

    LayoutOutput output;
    output.size = finalOuterSize;
    output.baselines = BaselinesFromFirst(inFlow.firstBaseline);
    output.topMargin =
        ownMarginsCollapseWithChildren.start
            ? inFlow.firstChildTopMarginSet
            : CollapsibleMarginSet::FromMargin(UnwrapOr(
                  rawMargin.MaybeResolve(parentSize.w, calc).top, 0.0f));
    output.bottomMargin =
        ownBottomMarginCollapsesWithChildren
            ? inFlow.lastChildBottomMarginSet
            : CollapsibleMarginSet::FromMargin(UnwrapOr(
                  rawMargin.MaybeResolve(parentSize.w, calc).bottom, 0.0f));
    output.marginsCanCollapseThrough = canBeCollapsedThrough;

    // The margin-collapsing outputs matter to the parent's intrinsic height,
    // so they are returned even when only the size was asked for.
    if (runMode == RunMode::ComputeSize) {
        return output;
    }

    // Commit the deferred child layouts.
    for (int i = 0; i < len(items); i++) {
        if (items[i].hasFinalLayout) {
            tree->SetUnroundedLayout(items[i].nodeId, items[i].finalLayout);
        }
    }

    // 4. Out-of-flow candidates, in document order. The positioning pass
    // lays out the ones this block claims. Direct children use a degenerate
    // static position; bubbled candidates are translated by the in-flow
    // child's location.
    OofCandidates candidates;
    for (int i = 0; i < len(items); i++) {
        BlockItem& item = items[i];
        if (IsOutOfFlow(item.position)) {
            OofCandidate candidate;
            candidate.node = item.nodeId;
            candidate.order = item.order;
            candidate.position = item.position;
            // The static-position rectangle spans the content box on the
            // inline axis and is a zero-height line at the child's
            // hypothetical block position. justify-self: auto takes the
            // container's justify-items. align-self is not filled from
            // align-items.
            float inlineStart = resolvedContentBoxInset.left;
            float inlineEnd = containerOuterWidth - resolvedContentBoxInset
                                                        .right;
            OptAlignItems itemJustify = item.justifySelf.Or(justifyItems);
            if (itemJustify.IsSome()) {
                itemJustify.val = ResolveSelfRelative(
                    itemJustify.val, item.direction, direction, true);
            }
            OptAlignItems itemAlign = item.alignSelf;
            if (itemAlign.IsSome()) {
                itemAlign.val = ResolveSelfRelative(
                    itemAlign.val, item.direction, direction, false);
            }
            candidate.staticX = AxisStaticPosition::FromAlignment(
                itemJustify, inlineStart, inlineEnd, IsRtl(direction));
            candidate.staticY = AxisStaticPosition::FromAlignment(
                itemAlign, item.staticPosition.y, item.staticPosition.y, false);
            candidates.Push(candidate);
        } else if (item.oofCount > 0) {
            OofCandRange range = {item.oofStart, item.oofCount};
            if (item.hasFinalLayout) {
                OofTranslateRange(&oofPool, range, item.finalLayout.location);
            }
            OofAppendPoolRange(&candidates, oofPool, range);
        }
    }
    RectF absolutePositionInset = resolvedBorder + scrollbarGutter;
    output.oofCandidates = candidates;
    output.hasOofPositioningArea = true;
    output.oofPositioningArea.size = finalOuterSize - absolutePositionInset
                                                          .SumAxes();
    output.oofPositioningArea
        .offset = {absolutePositionInset.left, absolutePositionInset.top};

    // End padding is part of a scroll container's overflow. Other boxes
    // do not extend their overflow rect by their own padding. Claimed
    // out-of-flow boxes are unioned by the positioning pass.
    if (isScrollContainer) {
        inflowOverflowRect.right +=
            IsRtl(direction) ? resolvedPadding.left : resolvedPadding.right;
        inflowOverflowRect.bottom += resolvedPadding.bottom;
    }
    output.scrollableOverflowRect = inflowOverflowRect;

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
    // css-align-3 §5.1.1: a non-normal align-content makes a block container
    // establish an independent formatting context. Layout and paint
    // containment do too.
    Contain contain = style.contain;
    bool establishesNewBfc = isScrollContainer || style.alignContent.IsSome() ||
                             contain.EstablishesIndependentFormattingContext();
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
    // establishes its own (a scroll container, a non-normal align-content,
    // or layout/paint containment).
    LayoutOutput out;
    if (blockCtx && !establishesNewBfc) {
        out = ComputeInner(tree, nodeId, next, blockCtx);
    } else {
        BlockFormattingContext rootBfc;
        BlockContext rootCtx;
        rootCtx.bfc = &rootBfc;
        rootCtx.isRoot = true;
        out = ComputeInner(tree, nodeId, next, &rootCtx);
        VecReset(rootBfc.floatContext.leftFloats);
        VecReset(rootBfc.floatContext.rightFloats);
        VecReset(rootBfc.floatContext.segments);
    }
    // Layout containment suppresses the box's baseline. Paint does not.
    if (contain.SuppressesBaseline()) {
        out.baselines = BaselinesFromFirst(None());
    }
    return out;
}

} // namespace taffy
