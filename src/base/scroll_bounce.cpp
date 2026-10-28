#include "base/scroll_bounce.h"
#include "base/virtual_list.h"
#include "gpui/platform.h"

#include <math.h>

namespace gpui {

// A pause without packets that ends a phaseless gesture. Momentum arrives
// once a frame, so the same gap ScrollBounce already uses for suppression.
const double kOverscrollGestureGap = 0.25;
// Fraction of the text's speed the edge carries. Measured on NSScrollView,
// and WebKit's rubber-band amplitude.
const float kOverscrollCarry = 0.31f;
// A finger that sends nothing for this long before lifting has stopped.
const double kOverscrollStill = 0.05;
// The interval assumed for the first packet of a stream.
const float kOverscrollFrame = 1.f / 60.f;
// How far back the stream's speed is read. Packets carry no timestamp, so a
// frame that takes long hands over several at once.
const double kOverscrollSpeedSpan = 0.05;
// The most a packet's unused part can be and still be rounding. Far from
// its start an f32 offset cannot hold every fraction, so text that used a
// whole packet can report a hair more or less, which is not an edge.
const float kOverscrollRounding = 0.01f;

static const float kBounceTau = 6.2831853071795864769f;

static float BounceOmega(const ScrollBounceMotion& motion) {
    if (motion.responseMs <= 0) return 0;
    return kBounceTau / (motion.responseMs / 1000.f);
}

// The largest outward speed, at most `speed`, whose peak stays within `max`.
// The edge follows (position + speed * t) * e^(-wt).
static float LimitThrow(float position, float speed, float omega, float max) {
    auto peak = [&](float v) {
        if (v <= omega * position) return position;
        return v / omega * expf(omega * position / v - 1.f);
    };
    if (peak(speed) <= max) return speed;
    float low = 0;
    float high = speed;
    for (int i = 0; i < 24; i++) {
        float mid = (low + high) * .5f;
        if (peak(mid) <= max) {
            low = mid;
        } else {
            high = mid;
        }
    }
    return low;
}

ScrollBounceMotion ScrollBounceMotion::Default() {
    ScrollBounceMotion out;
    if (base::PlatScrollBounceTouch()) {
        // UIScrollView feel. These are not UIKit constants.
        out.tracking = 0.55f;
        out.responseMs = 524.f;
        out.maxStretch = 0;
        out.hasMaxStretch = false;
    }
    return out;
}

float ScrollBouncePhysics::Offset() const {
    if (!dragging) return position;
    float d = extent > 1.f ? extent : 1.f;
    float tracked = motion.tracking;
    float stretch = position * tracked / (1.f + tracked * fabsf(position) / d);
    if (!motion.hasMaxStretch) return stretch;
    float max = motion.maxStretch;
    if (!(max > 0)) return stretch;
    return max * tanhf(stretch / max);
}

void ScrollBouncePhysics::Begin(float viewportExtent) {
    float offset = Offset();
    // A displaced edge keeps the extent it was stretched under. Re-reading a
    // viewport that shrank mid-return could not place the finger on it.
    if (offset == 0 || extent <= 0) {
        extent = viewportExtent > 1.f ? viewportExtent : 1.f;
    }
    float stretch = offset;
    if (motion.hasMaxStretch && motion.maxStretch > 0) {
        float max = motion.maxStretch;
        float t = offset / max;
        if (t > 0.999f) t = 0.999f;
        if (t < -0.999f) t = -0.999f;
        stretch = max * atanhf(t);
    }
    float denom =
        motion.tracking * std::max(0.01f, 1.f - fabsf(stretch) / extent);
    position = stretch / denom;
    velocity = 0;
    dragging = true;
    suppressMomentum = false;
}

float ScrollBouncePhysics::Pull(float delta) {
    float previous = position;
    float next = previous + delta;
    if (previous != 0 && ((previous < 0) != (next < 0))) {
        position = 0;
        return next;
    }
    position = next;
    return 0;
}

void ScrollBouncePhysics::Release() {
    position = Offset();
    dragging = false;
    if (position != 0) suppressMomentum = true;
}

void ScrollBouncePhysics::LetGo(float vel) {
    position = Offset();
    dragging = false;
    float omega = BounceOmega(motion);
    if (omega <= 0) {
        position = 0;
        velocity = 0;
        return;
    }
    // Velocity back toward the content is dropped. The path never crosses
    // the boundary.
    float outward = position * vel >= 0 ? vel : 0;
    if (motion.hasMaxStretch && motion.maxStretch > 0) {
        float sign = outward < 0 ? -1.f : 1.f;
        outward = sign * LimitThrow(fabsf(position), fabsf(outward), omega,
                                    motion.maxStretch);
    }
    velocity = outward - omega * position;
}

void ScrollBouncePhysics::Reset() {
    ScrollBounceMotion keep = motion;
    position = 0;
    velocity = 0;
    dragging = false;
    suppressMomentum = false;
    extent = 0;
    motion = keep;
}

bool ScrollBouncePhysics::Step(float seconds) {
    if (dragging || (position == 0 && velocity == 0)) return false;
    float omega = BounceOmega(motion);
    if (omega <= 0) {
        position = 0;
        velocity = 0;
        return false;
    }
    float decay = expf(-omega * std::max(0.f, seconds));
    float c = velocity + omega * position;
    position = (position + c * seconds) * decay;
    velocity = (velocity - omega * c * seconds) * decay;
    if (fabsf(position) < .1f && fabsf(velocity) < 1.f) {
        position = 0;
        velocity = 0;
        return false;
    }
    return true;
}

void ScrollOverscroll::SetMotion(ScrollBounceMotion value) {
    physics.motion = value;
}

ScrollOverscroll::~ScrollOverscroll() {
    VecReset(moveDy);
    VecReset(moveAt);
}

static void OverscrollStartGesture(ScrollOverscroll* self) {
    self->spent = false;
    self->velocity = 0;
    VecClear(self->moveDy);
    VecClear(self->moveAt);
}

static void OverscrollSample(ScrollOverscroll* self, float dy, double now) {
    if (dy == 0) return;
    int n = len(self->moveDy);
    if (n > 0 && self->moveDy[n - 1] * dy < 0) {
        VecClear(self->moveDy);
        VecClear(self->moveAt);
    }
    VecAppend(self->moveDy, dy);
    VecAppend(self->moveAt, now);
    int start = 0;
    for (int i = len(self->moveAt) - 1; i >= 0; i--) {
        if (now - self->moveAt[i] > kOverscrollSpeedSpan) {
            start = i;
            break;
        }
    }
    while (start > 0 && len(self->moveAt) > 0) {
        VecRemoveAt(self->moveAt, 0);
        VecRemoveAt(self->moveDy, 0);
        start--;
    }
    if (len(self->moveAt) <= 0) return;
    float seconds = (float)(now - self->moveAt[0]);
    float sum = 0;
    if (seconds > 0 && len(self->moveDy) > 1) {
        for (int i = 1; i < len(self->moveDy); i++) sum += self->moveDy[i];
        self->velocity = sum / seconds;
    } else {
        for (int i = 0; i < len(self->moveDy); i++) sum += self->moveDy[i];
        self->velocity = sum / kOverscrollFrame;
    }
    self->movedAt = now;
    self->hasMoved = true;
}

bool ScrollOverscroll::Packet(float dy, TouchPhase phase, float extent,
                              double now, float (*scroll)(void*, float),
                              void* user) {
    float before = Offset();
    bool thrown = false;
    bool inGesture = false;
    if (hasLastPacket && now - lastPacketAt < kOverscrollGestureGap) {
        inGesture = true;
    }
    lastPacketAt = now;
    hasLastPacket = true;
    if (!inGesture && !physics.IsDragging()) {
        OverscrollStartGesture(this);
        hasMoved = false;
    }
    OverscrollSample(this, dy, now);
    if (phase == TouchPhase::Started) {
        physics.Begin(extent);
        spent = false;
        resting = dy == 0;
    } else if (dy != 0) {
        resting = false;
    } else if (resting && physics.IsDragging()) {
        // A touch that lifts without moving: GPUI reports the cancel as a
        // move of nothing. Let go of the edge where it was held.
        resting = false;
        physics.LetGo(0);
        spent = Offset() != 0;
        if (spent) {
            sampledAt = now;
            hasSampled = true;
        }
        return physics.IsMoving();
    }
    if (physics.IsDragging()) {
        float rest = Offset() != 0 ? physics.Pull(dy) : dy;
        if (rest != 0 && scroll) {
            float unused = rest - scroll(user, rest);
            if (fabsf(unused) > kOverscrollRounding) physics.Pull(unused);
        }
        if (phase == TouchPhase::Ended || phase == TouchPhase::Cancelled) {
            bool moving = hasMoved && now - movedAt < kOverscrollStill;
            // Only an edge held past its end carries the finger's speed.
            // Let go within the text, the momentum that follows scrolls
            // it, and throws the edge only if it runs into it.
            float speed = moving && Offset() != 0 ? velocity : 0;
            physics.LetGo(speed * kOverscrollCarry);
            thrown = true;
            spent = Offset() != 0;
        }
    } else if (spent) {
        // Momentum after the edge answered scrolls the text back and does
        // not throw the edge again.
        if (scroll) scroll(user, dy);
    } else if (scroll) {
        float unused = dy - scroll(user, dy);
        if (fabsf(unused) > kOverscrollRounding) {
            physics.LetGo(velocity * kOverscrollCarry);
            thrown = true;
            spent = true;
        }
    }
    bool changed = (thrown && physics.IsMoving()) || Offset() != before;
    if (thrown || (changed && !hasSampled)) {
        sampledAt = now;
        hasSampled = true;
    }
    return changed;
}

bool ScrollOverscroll::Step(double now) {
    float elapsed = 0;
    if (hasSampled) elapsed = (float)(now - sampledAt);
    sampledAt = now;
    hasSampled = true;
    bool moving = physics.Step(elapsed);
    if (!moving) hasSampled = false;
    return moving;
}

void ScrollOverscroll::Reset() {
    ScrollBounceMotion keep = physics.motion;
    physics.Reset();
    physics.motion = keep;
    sampledAt = 0;
    lastPacketAt = 0;
    movedAt = 0;
    velocity = 0;
    VecClear(moveDy);
    VecClear(moveAt);
    hasSampled = false;
    hasLastPacket = false;
    hasMoved = false;
    resting = false;
    spent = false;
}

static Bounds PositiveViewport(void* user) {
    PositiveScroll* s = (PositiveScroll*)user;
    float h = s && s->viewport ? *s->viewport : 0;
    return Bounds{0, 0, 0, h};
}

static Point PositiveOffset(void* user) {
    PositiveScroll* s = (PositiveScroll*)user;
    float y = s && s->offset ? *s->offset : 0;
    return Point{0, -y};
}

static void PositiveSetOffset(void* user, Point value) {
    PositiveScroll* s = (PositiveScroll*)user;
    if (!s || !s->offset) return;
    float max = 0;
    if (s->content && s->viewport) {
        max = *s->content - *s->viewport;
        if (max < 0) max = 0;
    }
    float y = -value.y;
    if (y < 0) y = 0;
    if (y > max) y = max;
    *s->offset = y;
}

static Size PositiveContent(void* user) {
    PositiveScroll* s = (PositiveScroll*)user;
    float h = s && s->content ? *s->content : 0;
    return Size{0, h};
}

ScrollbarHandle PositiveScrollHandle(PositiveScroll* slot) {
    ScrollbarHandle out;
    if (!slot) return out;
    out.user = slot;
    out.viewportBounds = PositiveViewport;
    out.offset = PositiveOffset;
    out.setOffset = PositiveSetOffset;
    out.contentSize = PositiveContent;
    return out;
}

static Bounds VirtualViewport(void* user) {
    auto* h = (VirtualListScrollHandle*)user;
    return Bounds{0, 0, 0, h ? h->viewport : 0};
}

static Point VirtualOffset(void* user) {
    auto* h = (VirtualListScrollHandle*)user;
    return Point{0, h ? -h->offset : 0};
}

static void VirtualSetOffset(void* user, Point value) {
    auto* h = (VirtualListScrollHandle*)user;
    if (!h) return;
    float max = h->contentSize - h->viewport;
    if (max < 0) max = 0;
    float y = -value.y;
    if (y < 0) y = 0;
    if (y > max) y = max;
    h->offset = y;
    h->pending = false;
}

static Size VirtualContent(void* user) {
    auto* h = (VirtualListScrollHandle*)user;
    return Size{0, h ? h->contentSize : 0};
}

ScrollbarHandle VirtualListScrollbar(VirtualListScrollHandle* handle) {
    ScrollbarHandle out;
    if (!handle) return out;
    out.user = handle;
    out.viewportBounds = VirtualViewport;
    out.offset = VirtualOffset;
    out.setOffset = VirtualSetOffset;
    out.contentSize = VirtualContent;
    return out;
}

bool ScrollBounce::EnabledByDefault() {
    return base::PlatScrollBounce();
}

ScrollBounce* ScrollBounce::New(Ctx* cx, Str id, El* child) {
    ScrollBounce* out = ArenaNew<ScrollBounce>(cx->a);
    out->cx = cx;
    out->id = id;
    out->child = child;
    out->enabled = base::PlatScrollBounce();
    return out;
}

ScrollBounce* ScrollBounce::New(Ctx* cx, Str id, ScrollbarHandle handle,
                                El* child) {
    ScrollBounce* out = New(cx, id, child);
    out->handle = handle;
    out->scrollWheel = true;
    return out;
}

ScrollBounce* ScrollBounce::Enabled(bool value) {
    enabled = value;
    return this;
}

ScrollBounce* ScrollBounce::ScrollWheel(bool value) {
    scrollWheel = value;
    return this;
}

ScrollBounce* ScrollBounce::Motion(ScrollBounceMotion value) {
    motion = value;
    return this;
}

ScrollBounce* ScrollBounce::OnScroll(Listener listener) {
    onScroll = listener;
    return this;
}

ScrollBounce* ScrollBounce::Measure(PositiveScroll* slot) {
    laidOut = slot;
    return this;
}

void ScrollBounceSuppression::Release(ScrollBouncePhysics* physics,
                                      bool fromRest) {
    physics->Release();
    float offset = physics->Offset();
    direction = fromRest && physics->suppressMomentum
                    ? (offset > 0 ? 1.f : (offset < 0 ? -1.f : 0.f))
                    : 0.f;
}

bool ScrollBounceSuppression::Lifts(double now, float deltaY) {
    bool paused = lastWheelAt >= 0 && now - lastWheelAt >= kMomentumGapSeconds;
    lastWheelAt = now;
    bool reversed =
        direction != 0 && deltaY != 0 && (deltaY > 0 ? 1.f : -1.f) != direction;
    return paused || reversed;
}

bool ScrollBounceCatch::Observe(TouchPhase phase, float deltaY) {
    if (phase == TouchPhase::Started) {
        tracking = deltaY == 0;
        distance = 0;
    } else if (tracking) {
        distance += fabsf(deltaY);
        if (distance > kCatchDragSlop) {
            tracking = false;
        }
    }
    if (phase != TouchPhase::Ended && phase != TouchPhase::Cancelled) {
        return false;
    }
    bool shortCatch = tracking;
    tracking = false;
    return shortCatch && phase == TouchPhase::Ended;
}

static float HandleMax(const ScrollbarHandle& handle) {
    if (!handle.IsValid()) return 0;
    float max = handle.ContentSize().h - handle.ViewportBounds().h;
    return max > 0 ? max : 0;
}

struct OwnedScroll {
    ScrollbarHandle handle;
    float max = 0;
};

static float OwnedScrollBy(void* user, float dy) {
    OwnedScroll* a = (OwnedScroll*)user;
    Point before = a->handle.Offset();
    float y = before.y + dy;
    if (y < -a->max) y = -a->max;
    if (y > 0) y = 0;
    Point next = before;
    next.y = y;
    a->handle.SetOffset(next);
    return next.y - before.y;
}

struct ScrollBounceState {
    ScrollBouncePhysics physics;
    ScrollOverscroll overscroll;
    ScrollBounceCatch shortDrag;
    ScrollBounceSuppression suppression;
    OngoingScroll wheelLock;
    double sampledAt = 0;
    float paintOffset = 0;
    Listener onScroll = {};
    ScrollbarHandle handle = {};
    PositiveScroll* laidOut = nullptr;
    Window* win = nullptr;
    bool scrollWheel = true;
    bool enabled = false;
    // The wrapped scroll box, once layout has named it. A wheel over a
    // deeper scroller belongs to that scroller.
    int scrollId = 0;

    static void OnWheel(ScrollBounceState* self, Ctx* cx,
                        const ScrollWheelEvent* ev);
    static void OnOwnedWheel(ScrollBounceState* self, Ctx* cx,
                             const ScrollWheelEvent* ev);
};

static void ApplyContentShift(El* scrollBox, float offset) {
    if (!scrollBox || offset == 0) return;
    // Shift the rows, not the scroll box. The scrollbar is painted from the
    // box's own bounds, so it stays put while the content stretches.
    for (El* c = scrollBox->first; c; c = c->next) {
        c->PaintOffset(c->paintDx, c->paintDy + offset);
    }
}

static void BounceAfterChild(PaintCtx*, El* wrap, void* user) {
    ScrollBounceState* s = (ScrollBounceState*)user;
    if (!s) return;
    El* box = wrap ? wrap->first : nullptr;
    if (box && box->scrollId) s->scrollId = box->scrollId;
    if (s->laidOut && box) {
        if (s->laidOut->viewport && box->h > 0) {
            *s->laidOut->viewport = box->h;
        }
        if (s->laidOut->measureContent && s->laidOut->content) {
            *s->laidOut->content = box->contentH;
        }
    }
    if (!s->scrollWheel) {
        ApplyContentShift(box, s->paintOffset);
        return;
    }
    if (HandleMax(s->handle) <= 0) {
        bool displaced = s->overscroll.Offset() != 0;
        s->overscroll.Reset();
        s->paintOffset = 0;
        if (displaced && s->win) AppRequestAnim(s->win, true);
    }
    ApplyContentShift(box, s->paintOffset);
}

bool ScrollBounceOwnedWheel(ScrollOverscroll* over, OngoingScroll* lock,
                            ScrollbarHandle handle, bool enabled,
                            const ScrollWheelEvent* ev, Ctx* cx,
                            Listener onScroll) {
    if (!over || !ev || !cx) return false;
    float max = HandleMax(handle);
    if (max <= 0) {
        over->Reset();
        return false;
    }
    Point delta = {ev->deltaX, ev->deltaY};
    if (lock) lock->Filter(&delta, ev->phase);
    if (fabsf(delta.x) > fabsf(delta.y)) {
        over->Reset();
        return false;
    }
    float before = handle.Offset().y;
    bool changed = false;
    if (!enabled || MotionReduced() || !ev->precise) {
        bool displaced = over->Offset() != 0;
        over->Reset();
        Point next = handle.Offset();
        float y = before + delta.y;
        if (y < -max) y = -max;
        if (y > 0) y = 0;
        next.y = y;
        handle.SetOffset(next);
        changed = displaced;
    } else {
        float extent = handle.ViewportBounds().h;
        if (extent < 1.f) extent = 1.f;
        OwnedScroll apply;
        apply.handle = handle;
        apply.max = max;
        changed = over->Packet(delta.y, ev->phase, extent, TimeNow(),
                               OwnedScrollBy, &apply);
    }
    bool scrolled = handle.Offset().y != before;
    const_cast<ScrollWheelEvent*>(ev)->propagate = false;
    if ((changed || scrolled) && cx->win) AppInvalidate(cx->win);
    if (scrolled && onScroll.IsValid()) {
        ScrollEvent se;
        se.offsetY = -handle.Offset().y;
        se.offsetX = handle.Offset().x;
        ListenerCall(cx->app, cx->win, onScroll, &se);
    }
    return true;
}

float ScrollBounceOwnedOffset(ScrollOverscroll* over, Window* win, bool enabled,
                              float maxExtent) {
    if (!over) return 0;
    if (!enabled || MotionReduced() || maxExtent <= 0) {
        bool was = over->Offset() != 0;
        over->Reset();
        if (was && win) AppRequestAnim(win, true);
        return 0;
    }
    if (over->Step(TimeNow()) && win) AppRequestAnim(win, true);
    return over->Offset();
}

// A scroll rect under the pointer, inside this bounce and not the bounce's
// own box. The innermost one keeps the wheel, including at its end, so the
// bounce must leave it for the scroll-rect pass.
static bool DeeperVerticalScroll(Window* win, int selfId, float x, float y) {
    if (!win || selfId == 0) return false;
    const ScrollRect* self = nullptr;
    for (int i = 0; i < win->paint.scrolls.len; i++) {
        if (win->paint.scrolls[i].id == selfId) {
            self = &win->paint.scrolls[i];
            break;
        }
    }
    if (!self) return false;
    for (int i = win->paint.scrolls.len - 1; i >= 0; i--) {
        const ScrollRect& s = win->paint.scrolls[i];
        if (s.id == selfId || !s.bounds.Contains({x, y})) continue;
        bool inside =
            s.bounds.x + 0.5f >= self->bounds.x &&
            s.bounds.y + 0.5f >= self->bounds.y &&
            s.bounds.x + s.bounds.w <= self->bounds.x + self->bounds.w + 0.5f &&
            s.bounds.y + s.bounds.h <= self->bounds.y + self->bounds.h + 0.5f;
        if (!inside) continue;
        if (s.contentH > s.bounds.h + 1.f || (s.maskAxes & 2)) return true;
    }
    return false;
}

void ScrollBounceState::OnOwnedWheel(ScrollBounceState* self, Ctx* cx,
                                     const ScrollWheelEvent* ev) {
    if (!self || !ev) return;
    if (DeeperVerticalScroll(cx ? cx->win : nullptr, self->scrollId, ev->x,
                             ev->y)) {
        return;
    }
    // The hit chain is innermost first, which is the nesting rule: the
    // area under the pointer keeps the gesture, including at its ends.
    ScrollBounceOwnedWheel(&self->overscroll, &self->wheelLock, self->handle,
                           self->enabled, ev, cx, self->onScroll);
}

void ScrollBounceState::OnWheel(ScrollBounceState* self, Ctx* cx,
                                const ScrollWheelEvent* ev) {
    if (!self || !ev || !cx || !cx->win) {
        return;
    }
    Point delta = {ev->deltaX, ev->deltaY};
    if (ev->precise) {
        self->wheelLock.Filter(&delta, ev->phase);
    }
    if (delta.x != 0 && delta.y != 0) {
        if (fabsf(delta.x) > fabsf(delta.y)) {
            return;
        }
        delta.x = 0;
    }
    if (fabsf(delta.x) > fabsf(delta.y)) {
        return;
    }
    bool ended =
        ev->phase == TouchPhase::Ended || ev->phase == TouchPhase::Cancelled;
    Window* win = cx->win;
    ScrollRect* box = nullptr;
    for (int i = win->paint.scrolls.len - 1; i >= 0; i--) {
        ScrollRect& s = win->paint.scrolls[i];
        if (s.onScroll.IsValid() && s.bounds.Contains({ev->x, ev->y}) &&
            s.contentH > s.bounds.h + 1.f) {
            box = &s;
            break;
        }
    }
    float viewH = box ? box->bounds.h : 1.f;
    if (ev->phase == TouchPhase::Started) {
        self->physics.Begin(viewH);
    }
    // A short catch suppresses the momentum after it. The Ended packet
    // itself may still cross an edge, so the suppression is set once this
    // packet is handled; only momentum packets after it are dropped.
    bool suppressShortDragMomentum = self->shortDrag
                                         .Observe(ev->phase, delta.y);
    if (self->suppression.Lifts(TimeNow(), delta.y)) {
        self->physics.suppressMomentum = false;
    }
    if (self->physics.suppressMomentum) {
        const_cast<ScrollWheelEvent*>(ev)->propagate = false;
        return;
    }
    bool changed = false;
    bool scrolled = false;
    if (self->physics.Offset() != 0) {
        // Outside a gesture (a phaseless wheel once suppression lifts) a
        // packet grabs the returning edge and lets it go again, as it
        // would at rest.
        bool fromRest = !self->physics.dragging;
        if (fromRest) {
            self->physics.Begin(viewH);
        }
        float remainder = self->physics.Pull(delta.y);
        if (remainder != 0 &&
            WindowScrollApply(win, ev->x, ev->y, 0, remainder)) {
            scrolled = true;
        }
        if (ended || fromRest) {
            self->suppression.Release(&self->physics, fromRest);
        }
        changed = true;
        const_cast<ScrollWheelEvent*>(ev)->propagate = false;
    } else if (box && ev->precise) {
        float maxOff = box->contentH - box->bounds.h;
        if (maxOff < 0) {
            maxOff = 0;
        }
        bool atTop = box->scrollY <= 0 && delta.y > 0;
        bool atBot = box->scrollY >= maxOff && delta.y < 0;
        if (atTop || atBot) {
            bool dragging = self->physics.dragging;
            if (!dragging) {
                self->physics.Begin(viewH);
            }
            self->physics.Pull(delta.y);
            if (!dragging || ended) {
                self->suppression.Release(&self->physics, !dragging);
            }
            changed = true;
            const_cast<ScrollWheelEvent*>(ev)->propagate = false;
        }
    }
    if (ended && !changed) {
        self->physics.Release();
    }
    if (suppressShortDragMomentum) {
        self->physics.suppressMomentum = true;
        self->suppression.direction = 0;
    }
    self->sampledAt = TimeNow();
    if (changed && cx->win) {
        AppInvalidate(cx->win);
    }
    if (scrolled && self->onScroll.IsValid()) {
        ListenerCall(cx->app, cx->win, self->onScroll, ev);
    }
}

El* ScrollBounce::IntoEl() {
    if (!child) {
        return Div(cx->a)->PathClick(id);
    }
    // Child-owned mode with the bounce off is just the child. The owned
    // wheel still scrolls, and only skips the stretch.
    if (!scrollWheel && (!enabled || MotionReduced())) {
        return Div(cx->a)->PathClick(id)->Child(child);
    }
    Entity<ScrollBounceState> st =
        ElementStateEntity<ScrollBounceState>(cx, id, StrL("ScrollBounce"));
    ScrollBounceState* s = st.Get(cx);
    float offset = 0;
    if (s) {
        s->physics.motion = motion;
        s->overscroll.SetMotion(motion);
        s->onScroll = onScroll;
        s->handle = handle;
        s->laidOut = laidOut;
        s->win = cx->win;
        s->scrollWheel = scrollWheel;
        s->enabled = enabled;
        if (!enabled || MotionReduced()) {
            s->overscroll.Reset();
            s->physics.Reset();
            s->physics.motion = motion;
            s->overscroll.SetMotion(motion);
        }
        double now = TimeNow();
        if (scrollWheel) {
            if (HandleMax(handle) <= 0) s->overscroll.Reset();
            if (s->overscroll.Step(now) && cx->win) {
                AppRequestAnim(cx->win, true);
            }
            offset = enabled && !MotionReduced() ? s->overscroll.Offset() : 0;
        } else {
            float dt = s->sampledAt > 0 ? (float)(now - s->sampledAt) : 0;
            s->sampledAt = now;
            if (s->physics.Step(dt) && cx->win) {
                AppRequestAnim(cx->win, true);
            }
            offset = s->physics.Offset();
        }
        s->paintOffset = offset;
    }
    El* wrap = Div(cx->a)->PathClick(id)->ClipY();
    if (s) {
        wrap->prePaint = BounceAfterChild;
        wrap->customUser = s;
        wrap->OnScrollWheel(ListenTo(st, scrollWheel
                                             ? &ScrollBounceState::OnOwnedWheel
                                             : &ScrollBounceState::OnWheel));
    }
    return wrap->Child(child);
}

} // namespace gpui
