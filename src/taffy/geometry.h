/* Geometric primitives useful for layout — taffy/src/geometry.rs
 *
 * Part of the C++ port of taffy 0.15.0 (see src/taffy/readme.md).
 *
 * Deviations from the Rust, both forced by this tree's rules:
 *   - Rust's `Point<T>`, `Size<T>`, `Rect<T>` and `Line<T>` are generic over
 *     the element type. Here each instantiation taffy actually uses is its own
 *     struct: the float ones (`SizeF`, `PointF`, `RectF`, `LineF`) live here,
 *     and the style-typed ones (`SizeDim`, `SizeAvail`, `RectLp`, …) live in
 *     style.h next to the types they hold. Each carries only the operations
 *     taffy performs on it, which is why they are not all alike.
 *   - `Option<f32>` is `Optf`, an alias for `float` with one reserved NaN
 *     bit pattern standing for `None` — see the block below. The optional
 *     enums taffy carries get one concrete struct each, also in style.h.
 */

#ifndef GPUI_TAFFY_GEOMETRY_H_
#define GPUI_TAFFY_GEOMETRY_H_

#include "base.h"

#include <cmath>
#include <cfloat>

namespace taffy {

using base::Arena;
using base::PointF;
using base::RectF;
using base::SizeF;
using base::Str;
using base::Vec;

// ─── Option<f32> ─────────────────────────────────────────────────────────

// Rust's `Option<f32>`, as a bare `float`.
//
// The obvious port is a `{ float, bool }` pair, and that is what this was:
// eight bytes for four bits of information, doubling every `Size`, `Rect` and
// `Line` of them that layout carries. Instead `None` is one reserved bit
// pattern — the NaN tagging V8 uses to fit a pointer beside a double, minus
// the pointer. A float has 2^23 quiet NaNs and layout has a use for none of
// them, so one is spent on `None` and every other bit pattern, NaN included,
// is `Some`.
//
// Reserving a single pattern rather than "any NaN" is deliberate: taffy does
// arithmetic that can produce a NaN of its own (`INFINITY - INFINITY` in
// `MaybeSub`, a zero aspect ratio), `F32Min` / `F32Max` have a rule for one,
// and turning that into a `None` would change what layout computes.
//
// The type is an alias, not a wrapper, so `Optf` and `float` are the same
// type and a plain float is simply an `Optf` that is always `Some`. That is
// what collapses the three overload families in taffy_math.h — `Optf op Optf`,
// `Optf op float`, `float op Optf` — into one function each, and it is why
// there is no distinct-typedef version of this: the whole point of the tag is
// that the two are interchangeable. The name stays because it says which
// values may be `None`.
using Optf = float;

// A quiet NaN (exponent all ones, mantissa MSB set) with a payload nothing
// else produces: x86 makes 0xffc00000 for an invalid operation, and NaN
// propagation copies an operand's payload rather than inventing this one.
inline constexpr uint32_t kOptfNoneBits = 0x7fc0beefu;

constexpr Optf None() {
    return __builtin_bit_cast(float, kOptfNoneBits);
}

// Rust spells this `Some(x)`. A no-op; it is here so a call site can say
// which of the two it means.
constexpr Optf Some(float v) {
    return v;
}

constexpr bool IsNone(Optf v) {
    return __builtin_bit_cast(uint32_t, v) == kOptfNoneBits;
}

constexpr bool IsSome(Optf v) {
    return __builtin_bit_cast(uint32_t, v) != kOptfNoneBits;
}

// Rust's `Option::unwrap`. A None here is a layout bug, not input, so this is
// the same no-op cast `Some` is.
constexpr float Unwrap(Optf v) {
    return v;
}

constexpr float UnwrapOr(Optf v, float alt) {
    return IsSome(v) ? v : alt;
}

constexpr Optf Or(Optf v, Optf alt) {
    return IsSome(v) ? v : alt;
}

// `==` on two Optf is float comparison, and None is a NaN, so None == None
// would be false. Rust's `Option<f32>: PartialEq` says the two Nones are
// equal, and this is that.
constexpr bool OptfEq(Optf a, Optf b) {
    return IsNone(a) ? IsNone(b) : (a == b);
}

constexpr bool OptfNe(Optf a, Optf b) {
    return !OptfEq(a, b);
}

// ─── f32 helpers — taffy/src/util/sys.rs ─────────────────────────────────
//
// Rust's `f32::max` / `f32::min` return the non-NaN operand when one side is
// NaN; `std::max` does not, and taffy leans on that.

//
// The NaN test is the bit pattern rather than `std::isnan`: MSVC compiles
// that one to a call into the CRT's `_fdclass`, and these two run several
// times per node per layout pass.
inline bool F32IsNan(float v) {
    return (__builtin_bit_cast(uint32_t, v) & 0x7fffffffu) > 0x7f800000u;
}

inline float F32Max(float a, float b) {
    if (F32IsNan(a)) {
        return b;
    }
    if (F32IsNan(b)) {
        return a;
    }
    return a > b ? a : b;
}

inline float F32Min(float a, float b) {
    if (F32IsNan(a)) {
        return b;
    }
    if (F32IsNan(b)) {
        return a;
    }
    return a < b ? a : b;
}

// Rust spells this `(value + 0.5).floor()` — round half up, not half away
// from zero.
inline float F32Round(float v) {
    return floorf(v + 0.5f);
}

// ─── axes ────────────────────────────────────────────────────────────────

// The simple absolute horizontal and vertical axis.
enum class AbsoluteAxis : uint8_t {
    Horizontal,
    Vertical
};

constexpr AbsoluteAxis OtherAxis(AbsoluteAxis axis) {
    return axis == AbsoluteAxis::Horizontal ? AbsoluteAxis::Vertical
                                            : AbsoluteAxis::Horizontal;
}

// The CSS abstract axis.
// https://www.w3.org/TR/css-writing-modes-3/#abstract-axes
enum class AbstractAxis : uint8_t {
    Inline,
    Block
};

constexpr AbstractAxis Other(AbstractAxis axis) {
    return axis == AbstractAxis::Inline ? AbstractAxis::Block
                                        : AbstractAxis::Inline;
}

// Convert an AbstractAxis into an AbsoluteAxis, naively assuming the Inline
// axis is Horizontal. Always true until taffy implements writing_mode.
constexpr AbsoluteAxis AsAbsNaive(AbstractAxis axis) {
    return axis == AbstractAxis::Inline ? AbsoluteAxis::Horizontal
                                        : AbsoluteAxis::Vertical;
}

// FlexDirection belongs to the style layer, but every container below keys
// its main/cross helpers off it, so it is declared here.
enum class FlexDirection : uint8_t {
    Row,
    Column,
    RowReverse,
    ColumnReverse
};

constexpr bool IsRow(FlexDirection d) {
    return d == FlexDirection::Row || d == FlexDirection::RowReverse;
}

constexpr bool IsColumn(FlexDirection d) {
    return d == FlexDirection::Column || d == FlexDirection::ColumnReverse;
}

constexpr bool IsReverse(FlexDirection d) {
    return d == FlexDirection::RowReverse || d == FlexDirection::ColumnReverse;
}

constexpr AbsoluteAxis MainAxis(FlexDirection d) {
    return IsRow(d) ? AbsoluteAxis::Horizontal : AbsoluteAxis::Vertical;
}

constexpr AbsoluteAxis CrossAxis(FlexDirection d) {
    return IsRow(d) ? AbsoluteAxis::Vertical : AbsoluteAxis::Horizontal;
}

// ─── SizeF / PointF / RectF ──────────────────────────────────────────────
//
// The three float shapes live in base.h, because gpui has the same three and
// there is no reason for two of each. What is here is everything taffy does
// with them that the base has no business knowing: a flex direction, a
// writing-mode axis, an aspect ratio. Rust has them as inherent methods on
// `Size<f32>` and friends; free functions are how a type you do not own grows
// an operation.

constexpr float GetAbs(SizeF s, AbsoluteAxis a) {
    return a == AbsoluteAxis::Horizontal ? s.w : s.h;
}
constexpr float Get(SizeF s, AbstractAxis a) {
    return a == AbstractAxis::Inline ? s.w : s.h;
}
constexpr void Set(SizeF* s, AbstractAxis a, float v) {
    if (a == AbstractAxis::Inline) {
        s->w = v;
    } else {
        s->h = v;
    }
}
constexpr float Main(SizeF s, FlexDirection d) {
    return IsRow(d) ? s.w : s.h;
}
constexpr float Cross(SizeF s, FlexDirection d) {
    return IsRow(d) ? s.h : s.w;
}
constexpr void SetMain(SizeF* s, FlexDirection d, float v) {
    if (IsRow(d)) {
        s->w = v;
    } else {
        s->h = v;
    }
}
constexpr void SetCross(SizeF* s, FlexDirection d, float v) {
    if (IsRow(d)) {
        s->h = v;
    } else {
        s->w = v;
    }
}
constexpr SizeF WithMain(SizeF s, FlexDirection d, float v) {
    SetMain(&s, d, v);
    return s;
}
constexpr SizeF WithCross(SizeF s, FlexDirection d, float v) {
    SetCross(&s, d, v);
    return s;
}
// Both components clamped against another size.
inline SizeF Max(SizeF a, SizeF b) {
    return {F32Max(a.w, b.w), F32Max(a.h, b.h)};
}
inline SizeF Min(SizeF a, SizeF b) {
    return {F32Min(a.w, b.w), F32Min(a.h, b.h)};
}
constexpr bool HasNonZeroArea(SizeF s) {
    return s.w > 0.0f && s.h > 0.0f;
}

constexpr float Get(PointF p, AbstractAxis a) {
    return a == AbstractAxis::Inline ? p.x : p.y;
}
constexpr void Set(PointF* p, AbstractAxis a, float v) {
    if (a == AbstractAxis::Inline) {
        p->x = v;
    } else {
        p->y = v;
    }
}
constexpr float Main(PointF p, FlexDirection d) {
    return IsRow(d) ? p.x : p.y;
}
constexpr float Cross(PointF p, FlexDirection d) {
    return IsRow(d) ? p.y : p.x;
}
constexpr PointF Transpose(PointF p) {
    return {p.y, p.x};
}
constexpr SizeF IntoSize(PointF p) {
    return {p.x, p.y};
}

constexpr float GridAxisSum(RectF r, AbsoluteAxis a) {
    return a == AbsoluteAxis::Horizontal ? r.left + r.right : r.top + r.bottom;
}
constexpr float MainAxisSum(RectF r, FlexDirection d) {
    return IsRow(d) ? r.left + r.right : r.top + r.bottom;
}
constexpr float CrossAxisSum(RectF r, FlexDirection d) {
    return IsRow(d) ? r.top + r.bottom : r.left + r.right;
}
constexpr float MainStart(RectF r, FlexDirection d) {
    return IsRow(d) ? r.left : r.top;
}
constexpr float MainEnd(RectF r, FlexDirection d) {
    return IsRow(d) ? r.right : r.bottom;
}
constexpr float CrossStart(RectF r, FlexDirection d) {
    return IsRow(d) ? r.top : r.left;
}
constexpr float CrossEnd(RectF r, FlexDirection d) {
    return IsRow(d) ? r.bottom : r.right;
}

// ─── SizeFOpt / PointFOpt / RectFOpt ─────────────────────────────────────
//
// Rust's `Size<Option<f32>>`, `Point<Option<f32>>` and `Rect<Option<f32>>`.
// An `Optf` is a float (see above), so these are the float shapes themselves
// — the same eight and sixteen bytes, and no conversion between the definite
// and the optional view of one. The aliases stay because a signature saying
// `SizeFOpt` says its components may be `None`, which `SizeF` does not; the
// two default differently, so a value that starts out unknown has to be
// spelled `SizeFOptNone()` rather than left to `{}`.

using SizeFOpt = SizeF;
using PointFOpt = PointF;
using RectFOpt = RectF;

constexpr SizeFOpt SizeFOptNone() {
    return {None(), None()};
}

constexpr PointFOpt PointFOptNone() {
    return {None(), None()};
}

constexpr RectFOpt RectFOptNone() {
    return {None(), None(), None(), None()};
}

// Rust's `Size::<Option<f32>>::from_cross`.
constexpr SizeFOpt SizeFOptFromCross(FlexDirection d, Optf v) {
    return IsRow(d) ? SizeFOpt{None(), v} : SizeFOpt{v, None()};
}

constexpr SizeF UnwrapOr(SizeFOpt s, SizeF alt) {
    return {UnwrapOr(s.w, alt.w), UnwrapOr(s.h, alt.h)};
}

constexpr SizeFOpt Or(SizeFOpt s, SizeFOpt alt) {
    return {Or(s.w, alt.w), Or(s.h, alt.h)};
}

constexpr bool BothAxisDefined(SizeFOpt s) {
    return IsSome(s.w) && IsSome(s.h);
}

// If one axis is Some and the other None, fill the None one in from the
// ratio. Anything else is returned unchanged.
constexpr SizeFOpt MaybeApplyAspectRatio(SizeFOpt s, Optf aspectRatio) {
    if (IsNone(aspectRatio)) {
        return s;
    }
    if (IsSome(s.w) && IsNone(s.h)) {
        return {s.w, s.w / aspectRatio};
    }
    if (IsNone(s.w) && IsSome(s.h)) {
        return {s.h * aspectRatio, s.h};
    }
    return s;
}

// `==` on two SizeFOpt is float comparison and None is a NaN, so the two
// Nones would not compare equal. This is Rust's `PartialEq`, component-wise.
constexpr bool SizeFOptEq(SizeFOpt a, SizeFOpt b) {
    return OptfEq(a.w, b.w) && OptfEq(a.h, b.h);
}

// ─── LineF / LineBool ────────────────────────────────────────────────────

// An abstract "line": anything with a start and an end.
struct LineF {
    float start = 0.0f;
    float end = 0.0f;

    constexpr float Sum() const { return start + end; }
};

struct LineBool {
    bool start = false;
    bool end = false;

    static constexpr LineBool True() { return {true, true}; }
    static constexpr LineBool False() { return {false, false}; }
};

// ─── slices ──────────────────────────────────────────────────────────────
//
// The one template left. Rust's grid styles hold `Vec<T>`s; a `Vec<T>` here
// owns heap and runs a destructor, which is the wrong shape for a value type
// that is copied as bytes (hard rule 4). A `Slice<T>` is a view of memory the
// caller's arena owns, so a `Style` owns nothing.

template <typename T>
struct Slice {
    T* els = nullptr;
    int len = 0;

    T& operator[](int i) const { return els[i]; }
    bool IsEmpty() const { return len == 0; }
    T* begin() const { return els; }
    T* end() const { return els + len; }
};

// `n` uninitialised (zeroed) elements from `a`.
template <typename T>
Slice<T> SliceNew(Arena* a, int n) {
    if (n <= 0) {
        return {};
    }
    return {(T*)a->Alloc((int)sizeof(T) * n), n};
}

// A copy of `n` elements from `src` into `a`.
template <typename T>
Slice<T> SliceDup(Arena* a, const T* src, int n) {
    Slice<T> out = SliceNew<T>(a, n);
    if (out.els && src) {
        memcpy((void*)out.els, (const void*)src, sizeof(T) * (size_t)n);
    }
    return out;
}

// One element, which is what most `grid-auto-*` lists are.
template <typename T>
Slice<T> SliceOne(Arena* a, const T& v) {
    Slice<T> out = SliceNew<T>(a, 1);
    if (out.els) {
        out.els[0] = v;
    }
    return out;
}

} // namespace taffy

#endif // GPUI_TAFFY_GEOMETRY_H_
