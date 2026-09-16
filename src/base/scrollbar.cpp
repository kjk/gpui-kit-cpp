#include "base/scrollbar.h"
#include "base/theme.h"

namespace gpui {

static Background ResolveTrackBackground(const ScrollbarTrackStyle& state,
                                         const ScrollbarTrackStyle& local,
                                         const ScrollbarTrackStyle& globalState,
                                         const ScrollbarTrackStyle& global) {
    if (state.hasBackground) return state.background;
    if (local.hasBackground) return local.background;
    if (globalState.hasBackground) return globalState.background;
    return global.hasBackground ? global.background : Background{};
}

static Rgba ResolveTrackBorder(const ScrollbarTrackStyle& state,
                               const ScrollbarTrackStyle& local,
                               const ScrollbarTrackStyle& globalState,
                               const ScrollbarTrackStyle& global) {
    if (state.hasBorder) return state.border;
    if (local.hasBorder) return local.border;
    if (globalState.hasBorder) return globalState.border;
    return global.hasBorder ? global.border : Rgba{};
}

struct ResolvedScrollbarThumb {
    Background background = {};
    float width = 0;
    float inset = 0;
    float radius = 0;
    float minLength = 0;
};

static ResolvedScrollbarThumb ResolveThumb(
    const ScrollbarThumbStyle& state, const ScrollbarThumbStyle& local,
    const ScrollbarThumbStyle& globalState, const ScrollbarThumbStyle& global,
    Background defaultBackground, float defaultWidth, float defaultInset,
    float defaultRadius) {
    ResolvedScrollbarThumb out;
    out.background =
        state.hasBackground
            ? state.background
            : (local.hasBackground
                   ? local.background
                   : (globalState.hasBackground
                          ? globalState.background
                          : (global.hasBackground ? global.background
                                                  : defaultBackground)));
    out.width =
        state.hasWidth
            ? state.width
            : (local.hasWidth
                   ? local.width
                   : (globalState.hasWidth
                          ? globalState.width
                          : (global.hasWidth ? global.width : defaultWidth)));
    out.inset =
        state.hasInset
            ? state.inset
            : (local.hasInset
                   ? local.inset
                   : (globalState.hasInset
                          ? globalState.inset
                          : (global.hasInset ? global.inset : defaultInset)));
    out.radius =
        state.hasRadius
            ? state.radius
            : (local.hasRadius ? local.radius
                               : (globalState.hasRadius
                                      ? globalState.radius
                                      : (global.hasRadius ? global.radius
                                                          : defaultRadius)));
    out.minLength =
        state.hasMinLength
            ? state.minLength
            : (local.hasMinLength
                   ? local.minLength
                   : (globalState.hasMinLength
                          ? globalState.minLength
                          : (global.hasMinLength ? global.minLength : 48.f)));
    return out;
}

static void ResolveScrollbarStyles(El* box, const ScrollbarStyles& local,
                                   const ScrollbarStyles& global,
                                   Rgba foreground) {
    box->scrollThemeSet = true;
    box->scrollTrack = ResolveTrackBackground(local.track, local.track,
                                              global.track, global.track);
    box->scrollTrackHover = ResolveTrackBackground(
        local.trackHover, local.track, global.trackHover, global.track);
    box->scrollTrackActive = ResolveTrackBackground(
        local.trackActive, local.track, global.trackActive, global.track);
    box->scrollTrackBorder = ResolveTrackBorder(local.track, local.track,
                                                global.track, global.track);
    box->scrollTrackHoverBorder = ResolveTrackBorder(
        local.trackHover, local.track, global.trackHover, global.track);
    box->scrollTrackActiveBorder = ResolveTrackBorder(
        local.trackActive, local.track, global.trackActive, global.track);
    box->scrollTrackWidth =
        local.track.hasWidth
            ? local.track.width
            : (global.track.hasWidth ? global.track.width : 16.f);

    Background normalDefault(RgbaOpacity(foreground, 0.35f));
    Background activeDefault(RgbaOpacity(foreground, 0.55f));
    ResolvedScrollbarThumb normal =
        ResolveThumb(local.thumb, local.thumb, global.thumb, global.thumb,
                     normalDefault, 6, 4, 0);
    ResolvedScrollbarThumb hover =
        ResolveThumb(local.thumbHover, local.thumb, global.thumbHover,
                     global.thumb, activeDefault, 8, 4, 0);
    ResolvedScrollbarThumb active =
        ResolveThumb(local.thumbActive, local.thumb, global.thumbActive,
                     global.thumb, activeDefault, 8, 4, 0);
    box->scrollThumb = normal.background;
    box->scrollThumbHover = hover.background;
    box->scrollThumbActive = active.background;
    box->scrollThumbWidth = normal.width;
    box->scrollThumbHoverWidth = hover.width;
    box->scrollThumbActiveWidth = active.width;
    box->scrollThumbInset = normal.inset;
    box->scrollThumbHoverInset = hover.inset;
    box->scrollThumbActiveInset = active.inset;
    box->scrollThumbRadius = normal.radius;
    box->scrollThumbHoverRadius = hover.radius;
    box->scrollThumbActiveRadius = active.radius;
    box->scrollThumbMinLength = normal.minLength;
    box->scrollThumbHoverMinLength = hover.minLength;
    box->scrollThumbActiveMinLength = active.minLength;
}

static El* ApplyScrollbarTheme(Ctx* cx, El* box) {
    BaseTheme theme = base_theme::Theme::Global(cx->app);
    box->scrollMotion = theme.scrollbar.motion;
    ResolveScrollbarStyles(box, ScrollbarStyles{}, theme.scrollbar.styles,
                           theme.tokens.colors.foreground);
    return box;
}

// Rust's floor on the thumb: below this there is nothing left to aim at.
static const float kMinThumb = 48.f;

static float ClampF(float v, float lo, float hi) {
    if (v < lo) {
        return lo;
    }
    return v > hi ? hi : v;
}

ScrollbarThumbGeometry ScrollbarGeometry(float origin, float container,
                                         float content, float marginEnd,
                                         float inset, float minLength) {
    ScrollbarThumbGeometry out;
    out.origin = origin;
    float track = container - marginEnd;
    if (track < 0) track = 0;
    float logical = content > 0 ? container / content * container : 0;
    if (logical < minLength) logical = minLength;
    if (logical > track) logical = track;
    float maxInset = logical * .5f;
    out.inset = ClampF(inset, 0, maxInset);
    out.length = logical - out.inset * 2.f;
    out.travel = track - logical;
    out.extent = content - container;
    return out;
}

float ScrollbarThumbGeometry::Start(float offset) const {
    float pct = extent > 0 ? ClampF(offset / extent, 0, 1) : 0;
    return origin + inset + pct * travel;
}

float ScrollbarThumbGeometry::DragOffset(float position, float grab,
                                         float current) const {
    if (travel <= 0 || extent <= 0) return current;
    float pct = (position - origin - inset - grab) / travel;
    return ClampF(pct, 0, 1) * extent;
}

float ScrollbarThumbSize(float track, float container, float content,
                         float minLength) {
    if (content <= 0 || track <= 0) {
        return 0;
    }
    float thumb = track * (container / content);
    if (thumb < minLength) {
        thumb = minLength;
    }
    return thumb > track ? track : thumb;
}

float ScrollbarThumbSize(float track, float container, float content) {
    return ScrollbarThumbSize(track, container, content, kMinThumb);
}

// The offset a percentage along the track comes to. Rust clamps into
// `safe_range`, which is the whole scrollable distance.
static float OffsetForPct(float pct, float container, float content) {
    float max = content - container;
    if (max <= 0) {
        return 0;
    }
    return ClampF(pct, 0.f, 1.f) * max;
}

float ScrollbarThumbPos(float track, float thumb, float offset, float container,
                        float content, float marginEnd) {
    float max = content - container;
    float travel = track - marginEnd - thumb;
    if (max <= 0 || travel <= 0) {
        return 0;
    }
    return ClampF(offset / max, 0.f, 1.f) * travel;
}

float ScrollbarThumbPos(float track, float thumb, float offset, float container,
                        float content) {
    return ScrollbarThumbPos(track, thumb, offset, container, content, 0);
}

AxisPrepaintState ScrollbarPrepaintAxis(Axis axis, Bounds track, float offset,
                                        float containerSize, float contentSize,
                                        const ScrollbarThumbStyle& style) {
    AxisPrepaintState out;
    out.axis = axis;
    out.barHitbox = track;
    out.bounds = track;
    out.scrollSize = contentSize;
    out.containerSize = containerSize;
    out.trackWidth = axis == Axis::Vertical ? track.w : track.h;

    float trackLength = axis == Axis::Vertical ? track.h : track.w;
    if (trackLength <= 0 || containerSize <= 0 ||
        contentSize <= containerSize) {
        return out;
    }

    float minLength = style.hasMinLength ? style.minLength : kMinThumb;
    float inset = style.hasInset ? style.inset : 4.f;
    ScrollbarThumbGeometry geometry =
        ScrollbarGeometry(axis == Axis::Vertical ? track.y : track.x,
                          containerSize, contentSize, 0, inset, minLength);
    float thumbStart = geometry.Start(offset);
    float thumbLength = geometry.length;
    float thumbWidth = style.hasWidth ? style.width : 6.f;

    if (axis == Axis::Vertical) {
        out.thumbBounds = {track.x + track.w - inset - out.trackWidth,
                           thumbStart, out.trackWidth, thumbLength};
        out.thumbFillBounds = {track.x + track.w - inset - thumbWidth,
                               thumbStart, thumbWidth, thumbLength};
    } else {
        out.thumbBounds = {thumbStart,
                           track.y + track.h - inset - out.trackWidth,
                           thumbLength, out.trackWidth};
        out.thumbFillBounds = {thumbStart,
                               track.y + track.h - inset - thumbWidth,
                               thumbLength, thumbWidth};
    }

    out.thumbBg = style.hasBackground ? style.background : Background{};
    out.thumbSize = thumbLength;
    float radius = style.hasRadius ? style.radius : 0.f;
    float maxRadius = out.thumbFillBounds.w < out.thumbFillBounds.h
                          ? out.thumbFillBounds.w * .5f
                          : out.thumbFillBounds.h * .5f;
    out.radius = ClampF(radius, 0.f, maxRadius);
    out.visibilityOpacity = 1.f;
    out.visibilityPosition = 1.f;
    out.visibilityRequested = true;
    return out;
}

float ScrollbarOffsetForTrackPress(float pos, float trackOrigin, float track,
                                   float thumb, float container,
                                   float content) {
    float span = track - thumb;
    if (span <= 0) {
        return 0;
    }
    // The thumb's centre goes to the press, so half its length comes off.
    float pct = (pos - thumb * 0.5f - trackOrigin) / span;
    return OffsetForPct(pct, container, content);
}

float ScrollbarOffsetForDrag(float pos, float grab, float trackOrigin,
                             float track, float thumb, float container,
                             float content, float marginEnd) {
    float span = track - thumb - marginEnd;
    if (span <= 0) {
        return 0;
    }
    float pct = (pos - grab - trackOrigin) / span;
    return OffsetForPct(pct, container, content);
}

float ScrollbarOffsetForDrag(float pos, float grab, float trackOrigin,
                             float track, float thumb, float container,
                             float content) {
    return ScrollbarOffsetForDrag(pos, grab, trackOrigin, track, thumb,
                                  container, content, 0);
}

El* Scrollbar::New(Ctx* cx) {
    Arena* a = cx->a;
    const BaseTheme* theme = BaseThemeGlobal(cx->app);
    El* box = Div(a);
    if (theme) {
        box->ScrollMode(theme->scrollbar.mode);
    }
    return ApplyScrollbarTheme(cx, box);
}

El* Scrollbar::New(Ctx* cx, Str id, float scrollY, float scrollX,
                   Listener onScroll, ScrollAxis axis) {
    const BaseTheme* theme = BaseThemeGlobal(cx->app);
    ScrollbarMode mode =
        theme ? theme->scrollbar.mode : ScrollbarMode::Scrolling;
    return New(cx, id, scrollY, scrollX, onScroll, axis, mode);
}

El* Scrollbar::New(Ctx* cx, Str id, float scrollY, float scrollX,
                   Listener onScroll, ScrollAxis axis, ScrollbarMode mode) {
    return Apply(cx, Div(cx->a), id, scrollY, scrollX, onScroll, axis, mode);
}

El* Scrollbar::Apply(Ctx* cx, El* element, Str id, float scrollY, float scrollX,
                     Listener onScroll, ScrollAxis axis) {
    const BaseTheme* theme = BaseThemeGlobal(cx->app);
    ScrollbarMode mode =
        theme ? theme->scrollbar.mode : ScrollbarMode::Scrolling;
    return Apply(cx, element, id, scrollY, scrollX, onScroll, axis, mode);
}

El* Scrollbar::Apply(Ctx* cx, El* element, Str id, float scrollY, float scrollX,
                     Listener onScroll, ScrollAxis axis, ScrollbarMode mode) {
    El* box = ApplyScrollbarTheme(cx, element ? element : Div(cx->a))
                  ->ScrollMode(mode);
    // Each axis is asked for on its own: a box that only scrolls down still
    // clips what runs off its side.
    box->ClipY();
    if (axis == ScrollAxis::Vertical || axis == ScrollAxis::Both) {
        box->ScrollY(scrollY);
    }
    if (axis == ScrollAxis::Horizontal || axis == ScrollAxis::Both) {
        box->ScrollX(scrollX);
    } else {
        box->ClipX();
    }
    // `div().id(root_id)` over `div().id((id, "area")).track_scroll(..)`:
    // the port's clip and scroll area are one box, so the name goes on it and
    // the scroll handle's identity is that box's place in the tree. The
    // listener is the other half -- without it the box is only a clip and the
    // wheel falls through to whatever is behind it.
    box->Id(id.s ? id : StrL("scrollbar"))->ScrollFromPath();
    if (onScroll.IsValid()) {
        box->OnScroll(onScroll);
    }
    return box;
}

El* Scrollbar::ApplyStyles(Ctx* cx, El* element,
                           const ScrollbarStyles& styles) {
    El* box = element ? element : Div(cx->a);
    BaseTheme theme = base_theme::Theme::Global(cx->app);
    ResolveScrollbarStyles(box, styles, theme.scrollbar.styles,
                           theme.tokens.colors.foreground);
    box->scrollMotion = theme.scrollbar.motion;
    return box;
}

El* Scrollbar::Vertical(Ctx* cx, Str id, float scrollY, Listener onScroll) {
    return New(cx, id, scrollY, 0, onScroll, ScrollAxis::Vertical);
}

El* Scrollbar::Vertical(Ctx* cx, Str id, float scrollY, Listener onScroll,
                        ScrollbarMode mode) {
    return New(cx, id, scrollY, 0, onScroll, ScrollAxis::Vertical, mode);
}
} // namespace gpui
