#include "base/scroll_bounce.h"
#include "gpui/platform.h"

#include <math.h>

namespace gpui {

float ScrollBouncePhysics::Offset() const {
    if (!dragging) return position;
    float d = extent > 1 ? extent : 1;
    float tracked = motion.tracking;
    return position * tracked / (1.f + tracked * fabsf(position) / d);
}

void ScrollBouncePhysics::Begin(float viewportExtent) {
    float offset = Offset();
    if (offset == 0) extent = viewportExtent > 1 ? viewportExtent : 1;
    float denominator =
        motion.tracking * std::max(0.01f, 1.f - fabsf(offset) / extent);
    position = offset / denominator;
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

bool ScrollBouncePhysics::Step(float seconds) {
    if (dragging || position == 0) return false;
    if (motion.responseMs <= 0) {
        position = 0;
        velocity = 0;
        return false;
    }
    const float tau = 6.2831853071795864769f;
    float omega = tau / (motion.responseMs / 1000.f);
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

ScrollBounce* ScrollBounce::New(Ctx* cx, Str id, El* child) {
    ScrollBounce* out = ArenaNew<ScrollBounce>(cx->a);
    out->cx = cx;
    out->id = id;
    out->child = child;
    out->enabled = GPUI_OS_IOS || GPUI_OS_ANDROID;
    return out;
}

ScrollBounce* ScrollBounce::Enabled(bool value) {
    enabled = value;
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

struct ScrollBounceState {
    ScrollBouncePhysics physics;
    OngoingScroll wheelLock;
    double sampledAt = 0;
    Listener onScroll = {};

    static void OnWheel(ScrollBounceState* self, Ctx* cx,
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
        bool ended = ev->phase == TouchPhase::Ended ||
                     ev->phase == TouchPhase::Cancelled;
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
        if (self->physics.suppressMomentum) {
            const_cast<ScrollWheelEvent*>(ev)->propagate = false;
            return;
        }
        bool changed = false;
        bool scrolled = false;
        if (self->physics.Offset() != 0) {
            float remainder = self->physics.Pull(delta.y);
            if (remainder != 0 &&
                WindowScrollApply(win, ev->x, ev->y, 0, remainder)) {
                scrolled = true;
            }
            if (ended) {
                self->physics.Release();
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
                if (!self->physics.dragging) {
                    self->physics.Begin(viewH);
                }
                self->physics.Pull(delta.y);
                if (ended) {
                    self->physics.Release();
                }
                changed = true;
                const_cast<ScrollWheelEvent*>(ev)->propagate = false;
            }
        }
        if (ended && !changed) {
            self->physics.Release();
        }
        self->sampledAt = TimeNow();
        if (changed && cx->win) {
            AppInvalidate(cx->win);
        }
        if (scrolled && self->onScroll.IsValid()) {
            ListenerCall(cx->app, cx->win, self->onScroll, ev);
        }
    }
};

El* ScrollBounce::IntoEl() {
    if (!child) {
        return Div(cx->a)->PathClick(id);
    }
    if (!enabled || MotionReduced()) {
        return Div(cx->a)->PathClick(id)->Child(child);
    }
    Entity<ScrollBounceState> st =
        ElementStateEntity<ScrollBounceState>(cx, id, StrL("ScrollBounce"));
    ScrollBounceState* s = st.Get(cx);
    float offset = 0;
    if (s) {
        s->physics.motion = motion;
        s->onScroll = onScroll;
        double now = TimeNow();
        float dt = s->sampledAt > 0 ? (float)(now - s->sampledAt) : 0;
        s->sampledAt = now;
        if (s->physics.Step(dt) && cx->win) {
            AppRequestAnim(cx->win, true);
        }
        offset = s->physics.Offset();
    }
    El* wrap = Div(cx->a)->PathClick(id)->ClipY()->PaintOffset(0, offset);
    if (s) {
        wrap->OnScrollWheel(ListenTo(st, &ScrollBounceState::OnWheel));
    }
    return wrap->Child(child);
}

} // namespace gpui
