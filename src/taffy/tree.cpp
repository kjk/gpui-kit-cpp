/* The per-node layout cache — taffy/src/tree/cache.rs */

#include "taffy/tree.h"

namespace taffy {

// f32::INFINITY / f32::NEG_INFINITY as u32 bit patterns.
static constexpr uint32_t kInfinityBits = 0x7f800000u;
static constexpr uint32_t kNegInfinityBits = 0xff800000u;

// The sign bit of each of the two packed f32s, which is where the requested
// axis is stashed.
static constexpr uint64_t kSignBit1 = (uint64_t)1 << 63;
static constexpr uint64_t kSignBit2 = (uint64_t)1 << 31;
static constexpr uint64_t kBothSignBitsMask = kSignBit1 | kSignBit2;
static constexpr uint64_t kNonSignBitsMask = ~kBothSignBitsMask;

// Only the bits that encode the x-axis value, so the y-axis one can be
// ignored when comparing a key.
static constexpr uint64_t kXAxisValueMask = (uint64_t)0xffffffffu << 32;

static uint32_t ToBits(float v) {
    uint32_t out = 0;
    memcpy(&out, &v, sizeof(out));
    return out;
}

static uint32_t OptionCacheKey(Optf v) {
    return IsSome(v) ? ToBits(v) : kInfinityBits;
}

static uint64_t SizeOptionCacheKey(SizeFOpt s) {
    return ((uint64_t)OptionCacheKey(s.w) << 32) |
           (uint64_t)OptionCacheKey(s.h);
}

static uint32_t AvailableSpaceCacheKey(AvailableSpace a) {
    switch (a.kind) {
        case AvailableSpace::Kind::Definite:
            return ToBits(-a.value);
        case AvailableSpace::Kind::MinContent:
            return kNegInfinityBits;
        default:
            return kInfinityBits;
    }
}

// A known dimension wins; when there is none, the available space stands in.
static uint32_t MixedCacheKey(Optf kd, AvailableSpace avs) {
    return IsSome(kd) ? ToBits(kd) : AvailableSpaceCacheKey(avs);
}

static uint64_t SizeMixedCacheKey(SizeFOpt kd, SizeAvail avs) {
    return ((uint64_t)MixedCacheKey(kd.w, avs.width) << 32) |
           (uint64_t)MixedCacheKey(kd.h, avs.height);
}

CacheKey CacheKey::From(const LayoutInput& input) {
    uint64_t extraBits = 0;
    switch (input.axis) {
        case RequestedAxis::Horizontal:
            extraBits = kSignBit1;
            break;
        case RequestedAxis::Vertical:
            extraBits = kSignBit2;
            break;
        default:
            extraBits = kSignBit1 | kSignBit2;
            break;
    }
    CacheKey key;
    key.kdAvailableSpace =
        SizeMixedCacheKey(input.knownDimensions, input.availableSpace);
    key.parentSize =
        (SizeOptionCacheKey(input.parentSize) & kNonSignBitsMask) | extraBits;
    // An axis with no known dimension is always definite. The flag only
    // means something when a parent passed a size.
    key.knownDimensionsAreDefiniteW =
        input.knownDimensionsAreDefiniteW || !IsSome(input.knownDimensions.w);
    key.knownDimensionsAreDefiniteH =
        input.knownDimensionsAreDefiniteH || !IsSome(input.knownDimensions.h);
    return key;
}

uint64_t CacheKey::XAxisParentSize() const {
    return parentSize & (kXAxisValueMask & kNonSignBitsMask);
}

bool CacheKey::SizeIsValidFor(CacheKey other) const {
    uint64_t entryAxis = parentSize & kBothSignBitsMask;
    return entryAxis == kBothSignBitsMask ||
           entryAxis == (other.parentSize & kBothSignBitsMask);
}

bool Cache::Get(const LayoutInput& input, LayoutOutput* out) {
    if (!ModeMatches(input)) {
        return false;
    }
    return GetWithKey(CacheKey::From(input), input.runMode, out);
}

void Cache::Store(const LayoutInput& input, const LayoutOutput& output) {
    StoreWithKey(CacheKey::From(input), input, output);
}

bool Cache::GetWithKey(CacheKey key, RunMode runMode, LayoutOutput* out) {
    switch (runMode) {
        case RunMode::PerformLayout:
            if ((presentMask & kFinalBit) && finalLayoutEntry.key == key) {
                *out = finalLayoutEntry.content;
                return true;
            }
            return false;
        case RunMode::ComputeSize: {
            uint64_t xAxis = key.XAxisParentSize();
            uint16_t mask = (uint16_t)(presentMask >> 1);
            for (int i = 0; mask != 0; i++, mask >>= 1) {
                if ((mask & 1) == 0) {
                    continue;
                }
                const CacheEntry<SizeF>& e = measureEntries[i];
                if (e.key.kdAvailableSpace == key.kdAvailableSpace &&
                    e.key.knownDimensionsAreDefiniteW ==
                        key.knownDimensionsAreDefiniteW &&
                    e.key.knownDimensionsAreDefiniteH ==
                        key.knownDimensionsAreDefiniteH &&
                    e.key.XAxisParentSize() == xAxis &&
                    e.key.SizeIsValidFor(key)) {
                    recentlyUsedEntries |= (uint16_t)(1u << i);
                    *out = LayoutOutput::FromOuterSize(e.content);
                    return true;
                }
            }
            return false;
        }
        default:
            return false;
    }
}

void Cache::StoreWithKey(CacheKey key, const LayoutInput& input,
                         const LayoutOutput& output) {
    // Hidden layout is not a result. It must not evict a real mode.
    if (input.runMode == RunMode::PerformHiddenLayout) {
        return;
    }
    uint8_t nextMode = CacheModeOf(input);
    if (mode != nextMode) {
        Clear();
        mode = nextMode;
    }
    switch (input.runMode) {
        case RunMode::PerformLayout:
            isEmpty = false;
            presentMask |= kFinalBit;
            finalLayoutEntry = {key, output};
            break;
        case RunMode::ComputeSize: {
            // A hit is rebuilt with FromOuterSize, which drops margin-collapse
            // metadata. A result that carries any cannot be cached.
            if (output.marginsCanCollapseThrough ||
                output.topMargin.positive != 0.0f ||
                output.topMargin.negative != 0.0f ||
                output.bottomMargin.positive != 0.0f ||
                output.bottomMargin.negative != 0.0f) {
                return;
            }
            isEmpty = false;
            for (int i = 0; i < kCacheSize; i++) {
                if ((presentMask & MeasureBit(i)) == 0) {
                    continue;
                }
                if (measureEntries[i].key == key) {
                    measureEntries[i].content = output.size;
                    recentlyUsedEntries |= (uint16_t)(1u << i);
                    return;
                }
            }
            while ((recentlyUsedEntries & (uint16_t)(1u << nextMeasureEntry)) !=
                   0) {
                recentlyUsedEntries &= (uint16_t)~(1u << nextMeasureEntry);
                nextMeasureEntry = (uint8_t)(nextMeasureEntry + 1);
                if (nextMeasureEntry == (uint8_t)kCacheSize) {
                    nextMeasureEntry = 0;
                }
            }
            int entryIndex = nextMeasureEntry;
            presentMask |= MeasureBit(entryIndex);
            measureEntries[entryIndex] = {key, output.size};
            recentlyUsedEntries |= (uint16_t)(1u << entryIndex);
            nextMeasureEntry = (uint8_t)(nextMeasureEntry + 1);
            if (nextMeasureEntry == (uint8_t)kCacheSize) {
                nextMeasureEntry = 0;
            }
            break;
        }
        default:
            break;
    }
}

bool Cache::Clear() {
    if (isEmpty) {
        return false;
    }
    isEmpty = true;
    // The contents are left as they are: an entry is only ever read while
    // its bit is set, so dropping the bits is the whole clear.
    presentMask = 0;
    recentlyUsedEntries = 0;
    nextMeasureEntry = 0;
    return true;
}

bool Cache::IsEmpty() const {
    return presentMask == 0;
}

} // namespace taffy
