/* A typed representation of the CSS style properties layout reads —
 * taffy/src/style/{compact_length,dimension,available_space,alignment,block,
 * flex,float,grid,mod}.rs, folded into one header the way `mod style` folds
 * them into one module.
 *
 * Deviations from the Rust:
 *   - Rust's `CoreStyle` / `FlexboxContainerStyle` / `GridItemStyle` traits let
 *     a caller supply its own style type. There is one style type here, so the
 *     traits are gone and the compute pass reads `Style` fields directly.
 *   - The generic containers are de-templatized: `Size<Dimension>` is
 *     `SizeDim`, `Rect<LengthPercentage>` is `RectLp`, `Line<GridPlacement>`
 *     is `LinePlacement`, and so on. Each carries only the operations taffy
 *     performs on it. The float ones live in geometry.h.
 *   - `Option<AlignItems>` and friends are one concrete struct each.
 *   - `CustomIdent` is a `Str` rather than a type parameter, and the grid
 *     track lists are arena-backed `Slice<T>` rather than `Vec<T>`. A `Style`
 *     therefore owns nothing and copies as bytes; whoever built it owns the
 *     arena its grid data came from. Rust gets `Clone`/`Drop` for free and can
 *     afford `Vec`s here; hard rule 4 says a graph of owned heap inside a
 *     value type is the wrong shape for this tree.
 *   - The `Safe*` / `START` associated constants are aggregate initialisers:
 *     `AlignItems{AlignItemsKeyword::Start}` is Rust's `AlignItems::START`.
 */

#ifndef GPUI_TAFFY_STYLE_H_
#define GPUI_TAFFY_STYLE_H_

#include "taffy/geometry.h"

namespace taffy {

// ─── CompactLength ───────────────────────────────────────────────────────
//
// A length as a compact 64-bit tagged word. The low 8 bits are the tag; a
// numeric variant keeps its f32 in the high 32 bits, and a calc() variant is
// a pointer whose low 3 bits (its tag) are zero. Rust stores this in a tagged
// `*const ()` and gets the same layout on a 64-bit target.

struct CompactLength {
    uint64_t bits = 0;

    // The tag indicating a calc() value.
    static constexpr uint64_t kCalcTag = 0;
    static constexpr uint64_t kLengthTag = 0x01;
    static constexpr uint64_t kPercentTag = 0x02;
    static constexpr uint64_t kAutoTag = 0x03;
    static constexpr uint64_t kFrTag = 0x04;
    static constexpr uint64_t kMinContentTag = 0x07;
    static constexpr uint64_t kMaxContentTag = 0x0f;
    static constexpr uint64_t kFitContentPxTag = 0x17;
    static constexpr uint64_t kFitContentPercentTag = 0x1f;

    static constexpr uint64_t kTagMask = 0xff;
    static constexpr uint64_t kCalcTagMask = 0x07;

    uint64_t Tag() const { return bits & kTagMask; }
    // Inline, and so is FromVal: style.cpp's resolve helpers call this once
    // per dimension per node per layout pass, and it is one bit-cast.
    float Value() const {
        return __builtin_bit_cast(float, (uint32_t)(bits >> 32));
    }
    const void* CalcValue() const { return (const void*)(uintptr_t)bits; }

    bool IsCalc() const { return (bits & kCalcTagMask) == 0; }
    bool IsZero() const { return bits == FromVal(0.0f, kLengthTag).bits; }
    bool IsLengthOrPercentage() const {
        uint64_t t = Tag();
        return t == kLengthTag || t == kPercentTag;
    }
    bool IsAuto() const { return Tag() == kAutoTag; }
    bool IsMinContent() const { return Tag() == kMinContentTag; }
    bool IsMaxContent() const { return Tag() == kMaxContentTag; }
    bool IsFitContent() const {
        uint64_t t = Tag();
        return t == kFitContentPxTag || t == kFitContentPercentTag;
    }
    bool IsMaxOrFitContent() const {
        uint64_t t = Tag();
        return t == kMaxContentTag || t == kFitContentPxTag ||
               t == kFitContentPercentTag;
    }
    // "In all cases, treat auto and fit-content() as max-content, except where
    // specified otherwise for fit-content()."
    bool IsMaxContentAlike() const {
        uint64_t t = Tag();
        return t == kAutoTag || t == kMaxContentTag || t == kFitContentPxTag ||
               t == kFitContentPercentTag;
    }
    bool IsMinOrMaxContent() const {
        uint64_t t = Tag();
        return t == kMinContentTag || t == kMaxContentTag;
    }
    bool IsIntrinsic() const {
        uint64_t t = Tag();
        return t == kAutoTag || t == kMinContentTag || t == kMaxContentTag ||
               t == kFitContentPxTag || t == kFitContentPercentTag;
    }
    bool IsFr() const { return Tag() == kFrTag; }
    // Whether the value depends on the size of the parent node.
    bool UsesPercentage() const {
        uint64_t t = Tag();
        return t == kPercentTag || t == kFitContentPercentTag || IsCalc();
    }

    static CompactLength FromVal(float v, uint64_t tag) {
        return CompactLength{((uint64_t)__builtin_bit_cast(uint32_t, v) << 32) |
                             tag};
    }
    static CompactLength FromTag(uint64_t tag) { return CompactLength{tag}; }
    static CompactLength FromPtr(const void* p) {
        return CompactLength{(uint64_t)(uintptr_t)p};
    }

    static CompactLength Length(float v) { return FromVal(v, kLengthTag); }
    // Percentages are f32 in [0.0, 1.0], not [0.0, 100.0].
    static CompactLength Percent(float v) { return FromVal(v, kPercentTag); }
    static CompactLength Auto() { return FromTag(kAutoTag); }
    static CompactLength Fr(float v) { return FromVal(v, kFrTag); }
    static CompactLength MinContent() { return FromTag(kMinContentTag); }
    static CompactLength MaxContent() { return FromTag(kMaxContentTag); }
    static CompactLength FitContentPx(float limit) {
        return FromVal(limit, kFitContentPxTag);
    }
    static CompactLength FitContentPercent(float limit) {
        return FromVal(limit, kFitContentPercentTag);
    }
    static CompactLength Zero() { return Length(0.0f); }
    // The low 3 bits are the tag and must be zero; callers align their calc
    // nodes. Rust asserts the same thing.
    static CompactLength Calc(const void* p) { return FromPtr(p); }
};

inline bool operator==(CompactLength a, CompactLength b) {
    return a.bits == b.bits;
}

inline bool operator!=(CompactLength a, CompactLength b) {
    return a.bits != b.bits;
}

// Resolving a calc() handle is the embedder's job; the tree hands one of these
// down, which is Rust's `impl Fn(*const (), f32) -> f32`.
using CalcResolverFn = float (*)(const void* handle, float parentSize,
                                 void* ctx);

struct CalcResolver {
    CalcResolverFn fn = nullptr;
    void* ctx = nullptr;

    float Resolve(const void* handle, float parentSize) const {
        return fn ? fn(handle, parentSize, ctx) : 0.0f;
    }
};

// Resolve percentage values against parentSize. Non-percentage (and non-calc)
// values return None.
inline Optf ResolvedPercentageSize(CompactLength cl, float parentSize,
                                   CalcResolver calc) {
    if (cl.Tag() == CompactLength::kPercentTag) {
        return Some(cl.Value() * parentSize);
    }
    if (cl.IsCalc()) {
        return Some(calc.Resolve(cl.CalcValue(), parentSize));
    }
    return None();
}

// ─── Dimension / LengthPercentage / LengthPercentageAuto ─────────────────
//
// Three newtypes over CompactLength that differ only in which tags are valid.

struct LengthPercentage {
    CompactLength raw = CompactLength::Zero();

    static LengthPercentage Length(float v) {
        return {CompactLength::Length(v)};
    }
    static LengthPercentage Percent(float v) {
        return {CompactLength::Percent(v)};
    }
    static LengthPercentage Calc(const void* p) {
        return {CompactLength::Calc(p)};
    }
    static LengthPercentage Zero() { return {CompactLength::Zero()}; }
};

struct LengthPercentageAuto {
    CompactLength raw = CompactLength::Zero();

    static LengthPercentageAuto Length(float v) {
        return {CompactLength::Length(v)};
    }
    static LengthPercentageAuto Percent(float v) {
        return {CompactLength::Percent(v)};
    }
    static LengthPercentageAuto Auto() { return {CompactLength::Auto()}; }
    static LengthPercentageAuto Calc(const void* p) {
        return {CompactLength::Calc(p)};
    }
    static LengthPercentageAuto Zero() { return {CompactLength::Zero()}; }
    static LengthPercentageAuto From(LengthPercentage lp) { return {lp.raw}; }

    bool IsAuto() const { return raw.IsAuto(); }
    // Rust's `LengthPercentageAuto::resolve_to_option`: Some for a length,
    // Some for a percentage against `context`, None for auto.
    Optf ResolveToOption(float context, CalcResolver calc) const;
    // The same against a context that may itself be unknown, which is Rust's
    // `MaybeResolve<Option<f32>>`: a percentage against None stays None.
    Optf MaybeResolve(Optf context, CalcResolver calc) const;
};

struct Dimension {
    CompactLength raw = CompactLength::Zero();

    static Dimension Length(float v) { return {CompactLength::Length(v)}; }
    static Dimension Percent(float v) { return {CompactLength::Percent(v)}; }
    static Dimension Auto() { return {CompactLength::Auto()}; }
    static Dimension Calc(const void* p) { return {CompactLength::Calc(p)}; }
    static Dimension Zero() { return {CompactLength::Zero()}; }
    static Dimension From(LengthPercentage lp) { return {lp.raw}; }
    static Dimension From(LengthPercentageAuto lp) { return {lp.raw}; }

    bool IsAuto() const { return raw.IsAuto(); }
    uint64_t Tag() const { return raw.Tag(); }
    float Value() const { return raw.Value(); }
    // Rust's `MaybeResolve<Option<f32>>` for a single dimension.
    Optf MaybeResolve(Optf context, CalcResolver calc) const;
    // Some(len) only for the Length variant.
    Optf IntoOption() const {
        return raw.Tag() == CompactLength::kLengthTag ? Some(raw.Value())
                                                      : None();
    }
};

inline bool operator==(LengthPercentage a, LengthPercentage b) {
    return a.raw == b.raw;
}
inline bool operator!=(LengthPercentage a, LengthPercentage b) {
    return a.raw != b.raw;
}
inline bool operator==(LengthPercentageAuto a, LengthPercentageAuto b) {
    return a.raw == b.raw;
}
inline bool operator!=(LengthPercentageAuto a, LengthPercentageAuto b) {
    return a.raw != b.raw;
}
inline bool operator==(Dimension a, Dimension b) {
    return a.raw == b.raw;
}
inline bool operator!=(Dimension a, Dimension b) {
    return a.raw != b.raw;
}

// ─── AvailableSpace ──────────────────────────────────────────────────────

// The amount of space available to a node in a given axis.
// https://www.w3.org/TR/css-sizing-3/#available
struct AvailableSpace {
    enum class Kind : uint8_t {
        Definite,
        MinContent,
        MaxContent
    };

    Kind kind = Kind::MaxContent;
    float value = 0.0f;

    static AvailableSpace Definite(float v) { return {Kind::Definite, v}; }
    static AvailableSpace MinContent() { return {Kind::MinContent, 0.0f}; }
    static AvailableSpace MaxContent() { return {Kind::MaxContent, 0.0f}; }
    static AvailableSpace Zero() { return Definite(0.0f); }
    // Rust's `From<Option<f32>>`: None becomes MaxContent.
    static AvailableSpace From(Optf v) {
        return IsSome(v) ? Definite(v) : MaxContent();
    }

    bool IsDefinite() const { return kind == Kind::Definite; }
    Optf IntoOption() const {
        return kind == Kind::Definite ? Some(value) : None();
    }
    float UnwrapOr(float def) const {
        return kind == Kind::Definite ? value : def;
    }
    // A None here means the caller asked for a value it had already
    // established was definite; that is a layout bug, not input.
    float Unwrap() const { return value; }
    AvailableSpace Or(AvailableSpace def) const {
        return kind == Kind::Definite ? *this : def;
    }
    AvailableSpace MaybeSet(Optf v) const {
        return IsSome(v) ? Definite(v) : *this;
    }
    float ComputeFreeSpace(float usedSpace) const {
        switch (kind) {
            case Kind::MaxContent:
                return INFINITY;
            case Kind::MinContent:
                return 0.0f;
            default:
                return value - usedSpace;
        }
    }
    // Definite values within f32::EPSILON of each other count as equal.
    bool IsRoughlyEqual(AvailableSpace other) const {
        if (kind != other.kind) {
            return false;
        }
        if (kind != Kind::Definite) {
            return true;
        }
        return fabsf(value - other.value) < FLT_EPSILON;
    }
};

inline bool operator==(AvailableSpace a, AvailableSpace b) {
    return a.kind == b.kind &&
           (a.kind != AvailableSpace::Kind::Definite || a.value == b.value);
}

inline bool operator!=(AvailableSpace a, AvailableSpace b) {
    return !(a == b);
}

// ─── alignment ───────────────────────────────────────────────────────────

// The position-keyword half of AlignItems (and its aliases AlignSelf,
// JustifyItems, JustifySelf).
enum class AlignItemsKeyword : uint8_t {
    Start,
    End,
    FlexStart,
    FlexEnd,
    SelfStart,
    SelfEnd,
    Center,
    Baseline,
    Stretch
};

// The position-keyword half of AlignContent (and its alias JustifyContent).
enum class AlignContentKeyword : uint8_t {
    Start,
    End,
    FlexStart,
    FlexEnd,
    Center,
    Stretch,
    SpaceBetween,
    SpaceEvenly,
    SpaceAround
};

// Start<->End and FlexStart<->FlexEnd for RTL. Stretch maps to End to keep
// the layout algorithms' historical handling; Center and the distribution
// keywords are direction-symmetric and unaffected.
constexpr AlignContentKeyword Reversed(AlignContentKeyword k) {
    switch (k) {
        case AlignContentKeyword::Start:
            return AlignContentKeyword::End;
        case AlignContentKeyword::End:
            return AlignContentKeyword::Start;
        case AlignContentKeyword::FlexStart:
            return AlignContentKeyword::FlexEnd;
        case AlignContentKeyword::FlexEnd:
            return AlignContentKeyword::FlexStart;
        case AlignContentKeyword::Stretch:
            return AlignContentKeyword::End;
        default:
            return k;
    }
}

// The overflow-position modifier per CSS Box Alignment §4.3. `Safe` falls
// back to the start edge when the subject would overflow the container.
enum class AlignmentSafety : uint8_t {
    Unsafe,
    Safe
};

// Rust's `AlignItems::START` is `AlignItems{AlignItemsKeyword::Start}` here;
// its `SAFE_START` is `{AlignItemsKeyword::Start, AlignmentSafety::Safe}`.
struct AlignItems {
    AlignItemsKeyword keyword = AlignItemsKeyword::Start;
    AlignmentSafety safety = AlignmentSafety::Unsafe;

    constexpr bool IsSafe() const { return safety == AlignmentSafety::Safe; }
    constexpr AlignItemsKeyword Keyword() const { return keyword; }
};

struct AlignContent {
    AlignContentKeyword keyword = AlignContentKeyword::Start;
    AlignmentSafety safety = AlignmentSafety::Unsafe;

    constexpr bool IsSafe() const { return safety == AlignmentSafety::Safe; }
    constexpr AlignContentKeyword Keyword() const { return keyword; }
};

constexpr bool operator==(AlignItems a, AlignItems b) {
    return a.keyword == b.keyword && a.safety == b.safety;
}
constexpr bool operator!=(AlignItems a, AlignItems b) {
    return !(a == b);
}
constexpr bool operator==(AlignContent a, AlignContent b) {
    return a.keyword == b.keyword && a.safety == b.safety;
}
constexpr bool operator!=(AlignContent a, AlignContent b) {
    return !(a == b);
}

using JustifyItems = AlignItems;
using AlignSelf = AlignItems;
using JustifySelf = AlignItems;
using JustifyContent = AlignContent;

// Rust's `Option<AlignItems>` / `Option<AlignContent>`.
struct OptAlignItems {
    AlignItems val;
    bool has = false;

    constexpr OptAlignItems() = default;
    constexpr explicit OptAlignItems(AlignItems v) : val(v), has(true) {}

    constexpr bool IsSome() const { return has; }
    constexpr AlignItems UnwrapOr(AlignItems alt) const {
        return has ? val : alt;
    }
    constexpr OptAlignItems Or(OptAlignItems alt) const {
        return has ? *this : alt;
    }
};

using OptAlignSelf = OptAlignItems;

struct OptAlignContent {
    AlignContent val;
    bool has = false;

    constexpr OptAlignContent() = default;
    constexpr explicit OptAlignContent(AlignContent v) : val(v), has(true) {}

    constexpr bool IsSome() const { return has; }
    constexpr AlignContent UnwrapOr(AlignContent alt) const {
        return has ? val : alt;
    }
};

using OptJustifyContent = OptAlignContent;

constexpr bool operator==(OptAlignItems a, OptAlignItems b) {
    return a.has == b.has && (!a.has || a.val == b.val);
}
constexpr bool operator!=(OptAlignItems a, OptAlignItems b) {
    return !(a == b);
}
constexpr bool operator==(OptAlignContent a, OptAlignContent b) {
    return a.has == b.has && (!a.has || a.val == b.val);
}
constexpr bool operator!=(OptAlignContent a, OptAlignContent b) {
    return !(a == b);
}

// ─── display / position / overflow ───────────────────────────────────────

// Sets the layout used for the children of this node.
enum class Display : uint8_t {
    Block,
    FlowRoot,
    Flex,
    Grid,
    None
};

// An abstracted `display` where anything other than "none" is "normal".
enum class BoxGenerationMode : uint8_t {
    Normal,
    None
};

// The positioning strategy for this item. Relative is the default here, in
// contrast to CSS.
enum class Position : uint8_t {
    Relative,
    Absolute
};

// Whether size styles apply to the content box or the border box.
enum class BoxSizing : uint8_t {
    BorderBox,
    ContentBox
};

// How children overflowing their container affect layout. Taffy implements
// the layout-related effects only, not the painting ones.
enum class Overflow : uint8_t {
    Visible,
    Clip,
    Hidden,
    Scroll
};

constexpr bool IsScrollContainer(Overflow o) {
    return o == Overflow::Hidden || o == Overflow::Scroll;
}

// Some(0.0) when the overflow mode forces the automatic minimum size of a
// flex/grid item to 0, else None.
constexpr Optf MaybeIntoAutomaticMinSize(Overflow o) {
    return IsScrollContainer(o) ? Some(0.0f) : None();
}

// The direction of text, table and grid columns, and horizontal overflow.
enum class Direction : uint8_t {
    Ltr,
    Rtl
};

constexpr bool IsRtl(Direction d) {
    return d == Direction::Rtl;
}

// Resolve the item's writing-mode-relative self-start/self-end keyword into
// the container-relative start/end used by the layout algorithms. Taffy only
// supports horizontal-tb, so only an inline axis can flip with direction.
constexpr AlignItems ResolveSelfRelative(AlignItems value,
                                         Direction itemDirection,
                                         Direction containerDirection,
                                         bool axisIsInline) {
    bool flip = axisIsInline && itemDirection != containerDirection;
    if (value.keyword == AlignItemsKeyword::SelfStart) {
        value
            .keyword = flip ? AlignItemsKeyword::End : AlignItemsKeyword::Start;
    } else if (value.keyword == AlignItemsKeyword::SelfEnd) {
        value
            .keyword = flip ? AlignItemsKeyword::Start : AlignItemsKeyword::End;
    }
    return value;
}

// Used by block layout for the legacy behaviour of `<center>` and
// `<div align="left | right | center">`.
enum class TextAlign : uint8_t {
    Auto,
    LegacyLeft,
    LegacyRight,
    LegacyCenter
};

// Controls whether flex items are forced onto one line or can wrap.
enum class FlexWrap : uint8_t {
    NoWrap,
    Wrap,
    WrapReverse
};

// FlexDirection lives in geometry.h — the containers there key their
// main/cross helpers off it.

// ─── float ───────────────────────────────────────────────────────────────

// Floats a box to the left or right. Only applies to children of a block
// layout.
enum class Float : uint8_t {
    Left,
    Right,
    None
};

// A box that is definitely floated, and which way.
enum class FloatDirection : uint8_t {
    Left = 0,
    Right = 1
};

constexpr bool IsFloated(Float f) {
    return f == Float::Left || f == Float::Right;
}

struct OptFloatDirection {
    FloatDirection val = FloatDirection::Left;
    bool has = false;

    constexpr OptFloatDirection() = default;
    constexpr explicit OptFloatDirection(FloatDirection v)
        : val(v), has(true) {}

    constexpr bool IsSome() const { return has; }
};

constexpr OptFloatDirection FloatDir(Float f) {
    switch (f) {
        case Float::Left:
            return OptFloatDirection(FloatDirection::Left);
        case Float::Right:
            return OptFloatDirection(FloatDirection::Right);
        default:
            return OptFloatDirection();
    }
}

// Gives a box "clearance", moving it below floated boxes that precede it.
enum class Clear : uint8_t {
    Left,
    Right,
    Both,
    None
};

// ─── style-typed containers ──────────────────────────────────────────────

// Rust's `Size<Dimension>`.
struct SizeDim {
    Dimension width = Dimension::Auto();
    Dimension height = Dimension::Auto();

    static SizeDim Auto() { return {}; }
    static SizeDim FromLengths(float w, float h) {
        return {Dimension::Length(w), Dimension::Length(h)};
    }
    static SizeDim FromPercent(float w, float h) {
        return {Dimension::Percent(w), Dimension::Percent(h)};
    }

    Dimension GetAbs(AbsoluteAxis a) const {
        return a == AbsoluteAxis::Horizontal ? width : height;
    }
    Dimension Get(AbstractAxis a) const {
        return a == AbstractAxis::Inline ? width : height;
    }
    Dimension Main(FlexDirection d) const { return IsRow(d) ? width : height; }
    Dimension Cross(FlexDirection d) const { return IsRow(d) ? height : width; }
    // Rust's `MaybeResolve<Size<Option<f32>>>`.
    SizeFOpt MaybeResolve(SizeFOpt context, CalcResolver calc) const;
    SizeF ResolveOrZero(SizeFOpt context, CalcResolver calc) const;
};

// Rust's `Size<LengthPercentage>` (the gap).
struct SizeLp {
    LengthPercentage width = LengthPercentage::Zero();
    LengthPercentage height = LengthPercentage::Zero();

    static SizeLp Zero() { return {}; }

    LengthPercentage GetAbs(AbsoluteAxis a) const {
        return a == AbsoluteAxis::Horizontal ? width : height;
    }
    LengthPercentage Get(AbstractAxis a) const {
        return a == AbstractAxis::Inline ? width : height;
    }
    LengthPercentage Main(FlexDirection d) const {
        return IsRow(d) ? width : height;
    }
    LengthPercentage Cross(FlexDirection d) const {
        return IsRow(d) ? height : width;
    }
    SizeF ResolveOrZero(SizeFOpt context, CalcResolver calc) const;
    SizeF ResolveOrZero(Optf context, CalcResolver calc) const;
};

// Rust's `Size<AvailableSpace>`.
struct SizeAvail {
    AvailableSpace width = AvailableSpace::MaxContent();
    AvailableSpace height = AvailableSpace::MaxContent();

    static SizeAvail MaxContent() {
        return {AvailableSpace::MaxContent(), AvailableSpace::MaxContent()};
    }
    static SizeAvail MinContent() {
        return {AvailableSpace::MinContent(), AvailableSpace::MinContent()};
    }
    static SizeAvail Definite(SizeF s) {
        return {AvailableSpace::Definite(s.w), AvailableSpace::Definite(s.h)};
    }
    static SizeAvail From(SizeFOpt s) {
        return {AvailableSpace::From(s.w), AvailableSpace::From(s.h)};
    }

    AvailableSpace GetAbs(AbsoluteAxis a) const {
        return a == AbsoluteAxis::Horizontal ? width : height;
    }
    AvailableSpace Get(AbstractAxis a) const {
        return a == AbstractAxis::Inline ? width : height;
    }
    void Set(AbstractAxis a, AvailableSpace v) {
        if (a == AbstractAxis::Inline) {
            width = v;
        } else {
            height = v;
        }
    }
    AvailableSpace Main(FlexDirection d) const {
        return IsRow(d) ? width : height;
    }
    AvailableSpace Cross(FlexDirection d) const {
        return IsRow(d) ? height : width;
    }
    void SetMain(FlexDirection d, AvailableSpace v) {
        if (IsRow(d)) {
            width = v;
        } else {
            height = v;
        }
    }
    void SetCross(FlexDirection d, AvailableSpace v) {
        if (IsRow(d)) {
            height = v;
        } else {
            width = v;
        }
    }
    SizeFOpt IntoOptions() const {
        return {width.IntoOption(), height.IntoOption()};
    }
    SizeAvail MaybeSet(SizeFOpt v) const {
        return {width.MaybeSet(v.w), height.MaybeSet(v.h)};
    }
};

inline bool operator==(SizeAvail a, SizeAvail b) {
    return a.width == b.width && a.height == b.height;
}

inline bool operator!=(SizeAvail a, SizeAvail b) {
    return !(a == b);
}

// Rust's `Rect<LengthPercentage>` (padding, border).
struct RectLp {
    LengthPercentage left = LengthPercentage::Zero();
    LengthPercentage right = LengthPercentage::Zero();
    LengthPercentage top = LengthPercentage::Zero();
    LengthPercentage bottom = LengthPercentage::Zero();

    static RectLp Zero() { return {}; }

    RectF ResolveOrZero(SizeFOpt context, CalcResolver calc) const;
    RectF ResolveOrZero(Optf context, CalcResolver calc) const;
};

// Rust's `Rect<LengthPercentageAuto>` (margin, inset).
struct RectLpa {
    LengthPercentageAuto left = LengthPercentageAuto::Zero();
    LengthPercentageAuto right = LengthPercentageAuto::Zero();
    LengthPercentageAuto top = LengthPercentageAuto::Zero();
    LengthPercentageAuto bottom = LengthPercentageAuto::Zero();

    static RectLpa Zero() { return {}; }
    static RectLpa Auto() {
        return {LengthPercentageAuto::Auto(), LengthPercentageAuto::Auto(),
                LengthPercentageAuto::Auto(), LengthPercentageAuto::Auto()};
    }

    LengthPercentageAuto MainStart(FlexDirection d) const {
        return IsRow(d) ? left : top;
    }
    LengthPercentageAuto MainEnd(FlexDirection d) const {
        return IsRow(d) ? right : bottom;
    }
    LengthPercentageAuto CrossStart(FlexDirection d) const {
        return IsRow(d) ? top : left;
    }
    LengthPercentageAuto CrossEnd(FlexDirection d) const {
        return IsRow(d) ? bottom : right;
    }
    RectF ResolveOrZero(SizeFOpt context, CalcResolver calc) const;
    RectF ResolveOrZero(Optf context, CalcResolver calc) const;
    // Rust's `Rect::<LengthPercentageAuto>::maybe_resolve`, which keeps auto
    // as None instead of collapsing it to zero.
    RectFOpt MaybeResolve(Optf context, CalcResolver calc) const;
    // The same, but resolving left/right against the width and top/bottom
    // against the height. Rust spells it `zip_size`.
    RectFOpt MaybeResolveZip(SizeFOpt context, CalcResolver calc) const;
};

// Rust's `Point<Overflow>`.
struct PointOverflow {
    Overflow x = Overflow::Visible;
    Overflow y = Overflow::Visible;

    Overflow Get(AbstractAxis a) const {
        return a == AbstractAxis::Inline ? x : y;
    }
    Overflow Main(FlexDirection d) const { return IsRow(d) ? x : y; }
    Overflow Cross(FlexDirection d) const { return IsRow(d) ? y : x; }
    PointOverflow Transpose() const { return {y, x}; }
};

inline bool operator==(SizeDim a, SizeDim b) {
    return a.width == b.width && a.height == b.height;
}
inline bool operator!=(SizeDim a, SizeDim b) {
    return !(a == b);
}
inline bool operator==(SizeLp a, SizeLp b) {
    return a.width == b.width && a.height == b.height;
}
inline bool operator!=(SizeLp a, SizeLp b) {
    return !(a == b);
}
inline bool operator==(RectLp a, RectLp b) {
    return a.left == b.left && a.right == b.right && a.top == b.top &&
           a.bottom == b.bottom;
}
inline bool operator!=(RectLp a, RectLp b) {
    return !(a == b);
}
inline bool operator==(RectLpa a, RectLpa b) {
    return a.left == b.left && a.right == b.right && a.top == b.top &&
           a.bottom == b.bottom;
}
inline bool operator!=(RectLpa a, RectLpa b) {
    return !(a == b);
}
inline bool operator==(PointOverflow a, PointOverflow b) {
    return a.x == b.x && a.y == b.y;
}
inline bool operator!=(PointOverflow a, PointOverflow b) {
    return !(a == b);
}

// ─── grid coordinates ────────────────────────────────────────────────────

// A grid line position in "CSS Grid Line" coordinates: the line at the left
// (or top) edge of the explicit grid is 1, the right (or bottom) edge is -1,
// and 0 is not a valid index.
struct GridLine {
    int16_t v = 0;

    int16_t AsI16() const { return v; }
};

// A grid line position in "OriginZero" coordinates: the left (or top) edge of
// the explicit grid is 0, counting up to the right/down and negative to the
// left/up.
struct OriginZeroLine {
    int16_t v = 0;
};

constexpr bool operator==(GridLine a, GridLine b) {
    return a.v == b.v;
}
constexpr bool operator!=(GridLine a, GridLine b) {
    return a.v != b.v;
}
constexpr bool operator==(OriginZeroLine a, OriginZeroLine b) {
    return a.v == b.v;
}
constexpr bool operator!=(OriginZeroLine a, OriginZeroLine b) {
    return a.v != b.v;
}
constexpr bool operator<(OriginZeroLine a, OriginZeroLine b) {
    return a.v < b.v;
}
constexpr bool operator>(OriginZeroLine a, OriginZeroLine b) {
    return a.v > b.v;
}
constexpr bool operator<=(OriginZeroLine a, OriginZeroLine b) {
    return a.v <= b.v;
}
constexpr bool operator>=(OriginZeroLine a, OriginZeroLine b) {
    return a.v >= b.v;
}
constexpr OriginZeroLine operator+(OriginZeroLine a, OriginZeroLine b) {
    return {(int16_t)(a.v + b.v)};
}
constexpr OriginZeroLine operator-(OriginZeroLine a, OriginZeroLine b) {
    return {(int16_t)(a.v - b.v)};
}
constexpr OriginZeroLine operator+(OriginZeroLine a, uint16_t b) {
    return {(int16_t)(a.v + (int16_t)b)};
}
constexpr OriginZeroLine operator-(OriginZeroLine a, uint16_t b) {
    return {(int16_t)(a.v - (int16_t)b)};
}

struct OptOriginZeroLine {
    OriginZeroLine val;
    bool has = false;

    constexpr OptOriginZeroLine() = default;
    constexpr explicit OptOriginZeroLine(OriginZeroLine v)
        : val(v), has(true) {}

    constexpr bool IsSome() const { return has; }
};

constexpr uint16_t kMaxGridTracks = 10000;
constexpr int16_t kMinOzLine = -(int16_t)kMaxGridTracks;
constexpr int16_t kMaxOzLine = (int16_t)kMaxGridTracks;

// Convert into OriginZero coordinates. Line zero is invalid and every caller
// filters it out first.
OriginZeroLine IntoOriginZeroLine(GridLine line, uint16_t explicitTrackCount);

// The minimum number of negative implicit tracks there must be if a grid item
// starts at this line.
constexpr uint16_t ImpliedNegativeImplicitTracks(OriginZeroLine l) {
    return l.v < 0 ? (uint16_t)(-(int32_t)l.v) : (uint16_t)0;
}

// The minimum number of positive implicit tracks there must be if a grid item
// ends at this line.
constexpr uint16_t ImpliedPositiveImplicitTracks(OriginZeroLine l,
                                                 uint16_t explicitTrackCount) {
    return l.v > (int16_t)explicitTrackCount
               ? (uint16_t)((uint16_t)l.v - explicitTrackCount)
               : (uint16_t)0;
}

// Rust's `Line<OriginZeroLine>`.
struct LineOzl {
    OriginZeroLine start;
    OriginZeroLine end;

    // The number of tracks between the start and end lines.
    uint16_t Span() const {
        int32_t d = (int32_t)end.v - (int32_t)start.v;
        return d > 0 ? (uint16_t)d : (uint16_t)0;
    }
};

// Rust's `Line<Option<OriginZeroLine>>`, for absolutely positioned items.
struct LineOptOzl {
    OptOriginZeroLine start;
    OptOriginZeroLine end;
};

// ─── grid placement ──────────────────────────────────────────────────────

enum class GridAutoFlow : uint8_t {
    Row,
    Column,
    RowDense,
    ColumnDense
};

constexpr bool IsDense(GridAutoFlow f) {
    return f == GridAutoFlow::RowDense || f == GridAutoFlow::ColumnDense;
}

constexpr AbsoluteAxis PrimaryAxis(GridAutoFlow f) {
    return (f == GridAutoFlow::Row || f == GridAutoFlow::RowDense)
               ? AbsoluteAxis::Horizontal
               : AbsoluteAxis::Vertical;
}

enum class GridPlacementKind : uint8_t {
    Auto,
    Line,
    Span,
    NamedLine,
    NamedSpan
};

// Rust's public `GridPlacement<S>`. `name` points into the arena the style
// was built from.
struct GridPlacement {
    GridPlacementKind kind = GridPlacementKind::Auto;
    int16_t line = 0;  // Line / NamedLine
    uint16_t span = 0; // Span / NamedSpan
    Str name;          // NamedLine / NamedSpan

    static GridPlacement Auto() { return {}; }
    static GridPlacement FromLineIndex(int16_t index) {
        GridPlacement p;
        p.kind = GridPlacementKind::Line;
        p.line = index;
        return p;
    }
    static GridPlacement FromSpan(uint16_t span) {
        GridPlacement p;
        p.kind = GridPlacementKind::Span;
        p.span = span;
        return p;
    }
    static GridPlacement FromNamedLine(Str name, int16_t index) {
        GridPlacement p;
        p.kind = GridPlacementKind::NamedLine;
        p.name = name;
        p.line = index;
        return p;
    }
    static GridPlacement FromNamedSpan(Str name, uint16_t span) {
        GridPlacement p;
        p.kind = GridPlacementKind::NamedSpan;
        p.name = name;
        p.span = span;
        return p;
    }
};

// Rust's two internal placement enums — `NonNamedGridPlacement` (CSS grid line
// coordinates, names already resolved) and `OriginZeroGridPlacement` — are one
// struct here. Which coordinate system `line` is in depends on which stage
// produced it, exactly as the Rust type alias says.
struct PlainPlacement {
    GridPlacementKind kind = GridPlacementKind::Auto;
    int16_t line = 0;
    uint16_t span = 0;

    static PlainPlacement Auto() { return {}; }
    static PlainPlacement AtLine(int16_t l) {
        return {GridPlacementKind::Line, l, 0};
    }
    static PlainPlacement Spanning(uint16_t s) {
        return {GridPlacementKind::Span, 0, s};
    }

    bool IsAuto() const { return kind == GridPlacementKind::Auto; }
    bool IsLine() const { return kind == GridPlacementKind::Line; }
    bool IsSpan() const { return kind == GridPlacementKind::Span; }
    OriginZeroLine Ozl() const { return OriginZeroLine{line}; }
};

constexpr bool operator==(PlainPlacement a, PlainPlacement b) {
    return a.kind == b.kind && a.line == b.line && a.span == b.span;
}
constexpr bool operator!=(PlainPlacement a, PlainPlacement b) {
    return !(a == b);
}

// Rust's `Line<GridPlacement<S>>`.
struct LinePlacement {
    GridPlacement start;
    GridPlacement end;

    // Definite if at least one end is a non-zero line index or a named line
    // (line 0 is invalid in GridLine coordinates and falls back to auto).
    bool IsDefinite() const;
    // Rust's `into_origin_zero_ignoring_named`.
    struct LinePlain IntoOriginZeroIgnoringNamed(
        uint16_t explicitTrackCount) const;
};

// Rust's `Line<NonNamedGridPlacement>` and `Line<OriginZeroGridPlacement>`.
struct LinePlain {
    PlainPlacement start;
    PlainPlacement end;

    // In OriginZero coordinates, where every line index is valid: definite if
    // either end names a line. Rust's
    // `Line<OriginZeroGridPlacement>::is_definite`.
    bool IsDefinite() const;
    // In CSS grid line coordinates, where 0 is not a valid index and falls
    // back to auto. Rust's `Line<NonNamedGridPlacement>::is_definite`.
    bool IsDefiniteGridLine() const;
    // For a placement in CSS grid line coordinates: normalize to OriginZero.
    LinePlain IntoOriginZero(uint16_t explicitTrackCount) const;
    // The span of an indefinite placement. Calling this on a definite one is
    // a bug; Rust panics, we return 1.
    uint16_t IndefiniteSpan() const;
    // If at least one end is a line, the other resolves from it alone.
    LineOzl ResolveDefiniteGridLines() const;
    // For absolutely positioned items: a None end means that side is bounded
    // by the grid container's border box.
    LineOptOzl ResolveAbsolutelyPositionedGridTracks() const;
    // Neither end is a line, so a definite start has to come from outside.
    LineOzl ResolveIndefiniteGridTracks(OriginZeroLine start) const;
};

// ─── track sizing ────────────────────────────────────────────────────────

// Specifies the maximum size of a grid track.
struct MaxTrackSizingFunction {
    CompactLength raw = CompactLength::Auto();

    static MaxTrackSizingFunction Length(float v) {
        return {CompactLength::Length(v)};
    }
    static MaxTrackSizingFunction Percent(float v) {
        return {CompactLength::Percent(v)};
    }
    static MaxTrackSizingFunction Auto() { return {CompactLength::Auto()}; }
    static MaxTrackSizingFunction MinContent() {
        return {CompactLength::MinContent()};
    }
    static MaxTrackSizingFunction MaxContent() {
        return {CompactLength::MaxContent()};
    }
    static MaxTrackSizingFunction FitContentPx(float v) {
        return {CompactLength::FitContentPx(v)};
    }
    static MaxTrackSizingFunction FitContentPercent(float v) {
        return {CompactLength::FitContentPercent(v)};
    }
    static MaxTrackSizingFunction FitContent(LengthPercentage lp) {
        return lp.raw.Tag() == CompactLength::kPercentTag
                   ? FitContentPercent(lp.raw.Value())
                   : FitContentPx(lp.raw.Value());
    }
    static MaxTrackSizingFunction Fr(float v) { return {CompactLength::Fr(v)}; }
    static MaxTrackSizingFunction Zero() { return {CompactLength::Zero()}; }
    static MaxTrackSizingFunction From(LengthPercentage v) { return {v.raw}; }
    static MaxTrackSizingFunction From(LengthPercentageAuto v) {
        return {v.raw};
    }
    static MaxTrackSizingFunction From(Dimension v) { return {v.raw}; }

    bool IsIntrinsic() const { return raw.IsIntrinsic(); }
    bool IsMaxContentAlike() const { return raw.IsMaxContentAlike(); }
    bool IsFr() const { return raw.IsFr(); }
    bool IsAuto() const { return raw.IsAuto(); }
    bool IsMinContent() const { return raw.IsMinContent(); }
    bool IsMaxContent() const { return raw.IsMaxContent(); }
    bool IsFitContent() const { return raw.IsFitContent(); }
    bool IsMaxOrFitContent() const { return raw.IsMaxOrFitContent(); }
    bool UsesPercentage() const { return raw.UsesPercentage(); }

    bool HasDefiniteValue(Optf parentSize) const;
    Optf DefiniteValue(Optf parentSize, CalcResolver calc) const;
    // A fixed track size, a percentage against definite space, or the argument
    // of a fit-content(). Everything else is None.
    Optf DefiniteLimit(Optf parentSize, CalcResolver calc) const;
    Optf ResolvedPercentageSize(float parentSize, CalcResolver calc) const {
        return taffy::ResolvedPercentageSize(raw, parentSize, calc);
    }
};

// Specifies the minimum size of a grid track.
struct MinTrackSizingFunction {
    CompactLength raw = CompactLength::Auto();

    static MinTrackSizingFunction Length(float v) {
        return {CompactLength::Length(v)};
    }
    static MinTrackSizingFunction Percent(float v) {
        return {CompactLength::Percent(v)};
    }
    static MinTrackSizingFunction Auto() { return {CompactLength::Auto()}; }
    static MinTrackSizingFunction MinContent() {
        return {CompactLength::MinContent()};
    }
    static MinTrackSizingFunction MaxContent() {
        return {CompactLength::MaxContent()};
    }
    static MinTrackSizingFunction Zero() { return {CompactLength::Zero()}; }
    static MinTrackSizingFunction From(LengthPercentage v) { return {v.raw}; }
    static MinTrackSizingFunction From(LengthPercentageAuto v) {
        return {v.raw};
    }
    static MinTrackSizingFunction From(Dimension v) { return {v.raw}; }
    // Rust's `From<MaxTrackSizingFunction>`: fr and fit-content() both become
    // auto on the min side.
    static MinTrackSizingFunction From(MaxTrackSizingFunction v) {
        if (v.raw.IsFr() || v.raw.IsFitContent()) {
            return Auto();
        }
        return {v.raw};
    }

    bool IsAuto() const { return raw.IsAuto(); }
    bool IsMinContent() const { return raw.IsMinContent(); }
    bool IsMaxContent() const { return raw.IsMaxContent(); }
    bool IsMinOrMaxContent() const { return raw.IsMinOrMaxContent(); }
    bool UsesPercentage() const {
        return raw.Tag() == CompactLength::kPercentTag || raw.IsCalc();
    }

    Optf DefiniteValue(Optf parentSize, CalcResolver calc) const;
    Optf ResolvedPercentageSize(float parentSize, CalcResolver calc) const {
        return taffy::ResolvedPercentageSize(raw, parentSize, calc);
    }
};

// The sizing function for a grid track: a min half and a max half. Rust
// spells it `MinMax<MinTrackSizingFunction, MaxTrackSizingFunction>`.
struct TrackSizingFunction {
    MinTrackSizingFunction min = MinTrackSizingFunction::Auto();
    MaxTrackSizingFunction max = MaxTrackSizingFunction::Auto();

    static TrackSizingFunction Auto() { return {}; }
    static TrackSizingFunction MinContent() {
        return {MinTrackSizingFunction::MinContent(),
                MaxTrackSizingFunction::MinContent()};
    }
    static TrackSizingFunction MaxContent() {
        return {MinTrackSizingFunction::MaxContent(),
                MaxTrackSizingFunction::MaxContent()};
    }
    static TrackSizingFunction Zero() {
        return {MinTrackSizingFunction::Zero(), MaxTrackSizingFunction::Zero()};
    }
    static TrackSizingFunction Length(float v) {
        return {MinTrackSizingFunction::Length(v),
                MaxTrackSizingFunction::Length(v)};
    }
    static TrackSizingFunction Percent(float v) {
        return {MinTrackSizingFunction::Percent(v),
                MaxTrackSizingFunction::Percent(v)};
    }
    static TrackSizingFunction Fr(float v) {
        return {MinTrackSizingFunction::Auto(), MaxTrackSizingFunction::Fr(v)};
    }
    static TrackSizingFunction FitContent(LengthPercentage lp) {
        return {MinTrackSizingFunction::Auto(),
                MaxTrackSizingFunction::FitContent(lp)};
    }
    static TrackSizingFunction MinMax(MinTrackSizingFunction mn,
                                      MaxTrackSizingFunction mx) {
        return {mn, mx};
    }
    static TrackSizingFunction From(LengthPercentage v) {
        return {MinTrackSizingFunction::From(v),
                MaxTrackSizingFunction::From(v)};
    }

    MinTrackSizingFunction MinSizingFunction() const { return min; }
    MaxTrackSizingFunction MaxSizingFunction() const { return max; }
    // At least one of the two halves is a length or a percentage.
    bool HasFixedComponent() const {
        return min.raw.IsLengthOrPercentage() || max.raw.IsLengthOrPercentage();
    }
};

// ─── grid templates ──────────────────────────────────────────────────────

// The first argument to a repeat(): how many times to repeat.
struct RepetitionCount {
    enum class Kind : uint8_t {
        AutoFill,
        AutoFit,
        Count
    };
    Kind kind = Kind::Count;
    uint16_t count = 1;

    static RepetitionCount AutoFill() { return {Kind::AutoFill, 0}; }
    static RepetitionCount AutoFit() { return {Kind::AutoFit, 0}; }
    static RepetitionCount Exactly(uint16_t n) { return {Kind::Count, n}; }

    bool IsAuto() const {
        return kind == Kind::AutoFill || kind == Kind::AutoFit;
    }
};

constexpr bool operator==(RepetitionCount a, RepetitionCount b) {
    return a.kind == b.kind &&
           (a.kind != RepetitionCount::Kind::Count || a.count == b.count);
}

// The set of names attached to one grid line.
struct LineNameSet {
    Slice<Str> names;
};

// A typed `repeat(..)` in a grid-template-* value.
struct GridTemplateRepetition {
    RepetitionCount count;
    Slice<TrackSizingFunction> tracks;
    Slice<LineNameSet> lineNames;

    uint16_t TrackCount() const { return (uint16_t)tracks.len; }
};

// An element of a grid-template-columns / grid-template-rows definition:
// either a single track sizing function or a repeat().
struct GridTemplateComponent {
    bool isRepeat = false;
    TrackSizingFunction single;
    GridTemplateRepetition repeat;

    static GridTemplateComponent Single(TrackSizingFunction t) {
        GridTemplateComponent c;
        c.single = t;
        return c;
    }
    static GridTemplateComponent Repeat(GridTemplateRepetition r) {
        GridTemplateComponent c;
        c.isRepeat = true;
        c.repeat = r;
        return c;
    }

    bool IsAutoRepetition() const { return isRepeat && repeat.count.IsAuto(); }
};

// A rectangular named grid area, in grid coordinates.
struct GridTemplateArea {
    Str name;
    uint16_t rowStart = 0;
    uint16_t rowEnd = 0;
    uint16_t columnStart = 0;
    uint16_t columnEnd = 0;
};

// The named areas plus the full template dimensions. Unnamed `.` cells can
// make the template larger than the extents of all named areas, so 0.13 no
// longer derives these counts from the area rectangles.
struct GridTemplateAreas {
    Slice<GridTemplateArea> areas;
    uint16_t rowCount = 0;
    uint16_t columnCount = 0;
};

// Which axis a named grid area edge belongs to, and which end of it.
enum class GridAreaAxis : uint8_t {
    Row,
    Column
};

enum class GridAreaEnd : uint8_t {
    Start,
    End
};

// ─── Style ───────────────────────────────────────────────────────────────

// The CSS style information for a single node. Field for field the Rust
// `Style`, minus the `dummy: PhantomData<S>` that only exists there to keep
// the grid feature optional.
struct Style {
    Display display = Display::Flex;
    // Whether a child is display:table. Affects children of block layouts.
    bool itemIsTable = false;
    // Is it a replaced element, like an image or a form field?
    bool itemIsReplaced = false;
    BoxSizing boxSizing = BoxSizing::BorderBox;
    Direction direction = Direction::Ltr;

    // Overflow
    PointOverflow overflow;
    float scrollbarWidth = 0.0f;

    // Float
    Float floatMode = Float::None;
    Clear clear = Clear::None;

    // Position
    Position position = Position::Relative;
    RectLpa inset = RectLpa::Auto();

    // Size
    SizeDim size;
    SizeDim minSize;
    SizeDim maxSize;
    Optf aspectRatio = None();

    // Spacing
    RectLpa margin;
    RectLp padding;
    RectLp border;

    // Alignment
    OptAlignItems alignItems;
    OptAlignSelf alignSelf;
    OptAlignItems justifyItems;
    OptAlignSelf justifySelf;
    OptAlignContent alignContent;
    OptJustifyContent justifyContent;
    SizeLp gap;

    // Block container
    TextAlign textAlign = TextAlign::Auto;

    // Flexbox container
    FlexDirection flexDirection = FlexDirection::Row;
    FlexWrap flexWrap = FlexWrap::NoWrap;

    // Flexbox item
    Dimension flexBasis = Dimension::Auto();
    float flexGrow = 0.0f;
    float flexShrink = 1.0f;

    // Grid container
    Slice<GridTemplateComponent> gridTemplateRows;
    Slice<GridTemplateComponent> gridTemplateColumns;
    Slice<TrackSizingFunction> gridAutoRows;
    Slice<TrackSizingFunction> gridAutoColumns;
    GridAutoFlow gridAutoFlow = GridAutoFlow::Row;

    // Grid container, named
    GridTemplateAreas gridTemplateAreas;
    Slice<LineNameSet> gridTemplateColumnNames;
    Slice<LineNameSet> gridTemplateRowNames;

    // Grid child
    LinePlacement gridRow;
    LinePlacement gridColumn;

    BoxGenerationMode BoxGenMode() const {
        return display == Display::None ? BoxGenerationMode::None
                                        : BoxGenerationMode::Normal;
    }
    bool IsBlock() const { return display == Display::Block; }
    bool IsCompressibleReplaced() const { return itemIsReplaced; }
};

// Rust derives `PartialEq` on `Style` and on everything it holds; this is
// that derive, and the ones its fields needed. What asks: the layout cache in
// `src/gpui` keeps taffy's tree between frames and has to know whether an
// element's style is the one its node already carries — a style that compares
// equal is one taffy is not told about, and a node it is not told about keeps
// the cache it filled last frame.
//
// Every field of `Style` is compared. A field added to `Style` has to be
// added here too, or a change to it will not reach layout.
bool operator==(const Style& a, const Style& b);

inline bool operator!=(const Style& a, const Style& b) {
    return !(a == b);
}

// ─── the equality derives the fields needed ──────────────────────────────
//
// `Optf` is Rust's `Option<f32>` with a NaN standing for None, so it is
// compared by its bits: `NaN == NaN` is false, and two Nones are equal.

inline bool SameOptf(Optf a, Optf b) {
    uint32_t ab = 0;
    uint32_t bb = 0;
    memcpy(&ab, &a, sizeof(ab));
    memcpy(&bb, &b, sizeof(bb));
    return ab == bb;
}

// A float compared by bits for the same reason, and because two styles that
// differ only in a NaN are not styles taffy would lay out alike.
inline bool SameFloatBits(float a, float b) {
    uint32_t ab = 0;
    uint32_t bb = 0;
    memcpy(&ab, &a, sizeof(ab));
    memcpy(&bb, &b, sizeof(bb));
    return ab == bb;
}

inline bool operator==(MinTrackSizingFunction a, MinTrackSizingFunction b) {
    return a.raw == b.raw;
}

inline bool operator==(MaxTrackSizingFunction a, MaxTrackSizingFunction b) {
    return a.raw == b.raw;
}

inline bool operator==(TrackSizingFunction a, TrackSizingFunction b) {
    return a.min == b.min && a.max == b.max;
}

inline bool operator==(const GridPlacement& a, const GridPlacement& b) {
    return a.kind == b.kind && a.line == b.line && a.span == b.span &&
           base::StrEq(a.name, b.name);
}

inline bool operator==(const LinePlacement& a, const LinePlacement& b) {
    return a.start == b.start && a.end == b.end;
}

inline bool operator==(const GridTemplateArea& a, const GridTemplateArea& b) {
    return base::StrEq(a.name, b.name) && a.rowStart == b.rowStart &&
           a.rowEnd == b.rowEnd && a.columnStart == b.columnStart &&
           a.columnEnd == b.columnEnd;
}

// A Vec in Rust, so its contents are what is compared and not where they sit.
template <typename T>
inline bool SameSlice(Slice<T> a, Slice<T> b) {
    if (a.len != b.len) {
        return false;
    }
    for (int i = 0; i < a.len; i++) {
        if (!(a.els[i] == b.els[i])) {
            return false;
        }
    }
    return true;
}

inline bool SameNames(Slice<base::Str> a, Slice<base::Str> b) {
    if (a.len != b.len) {
        return false;
    }
    for (int i = 0; i < a.len; i++) {
        if (!base::StrEq(a.els[i], b.els[i])) {
            return false;
        }
    }
    return true;
}

inline bool operator==(const LineNameSet& a, const LineNameSet& b) {
    return SameNames(a.names, b.names);
}

inline bool operator==(const GridTemplateRepetition& a,
                       const GridTemplateRepetition& b) {
    return a.count == b.count && SameSlice(a.tracks, b.tracks) &&
           SameSlice(a.lineNames, b.lineNames);
}

inline bool operator==(const GridTemplateComponent& a,
                       const GridTemplateComponent& b) {
    if (a.isRepeat != b.isRepeat) {
        return false;
    }
    return a.isRepeat ? a.repeat == b.repeat : a.single == b.single;
}

} // namespace taffy

#endif // GPUI_TAFFY_STYLE_H_
