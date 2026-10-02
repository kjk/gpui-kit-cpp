#include "base/toast.h"

namespace gpui {

ToastMotion ToastMotion::Sonner() {
    return {};
}

ToastOptions ToastOptions::Timeout(int milliseconds) {
    ToastOptions out;
    out.hasTimeout = true;
    out.timeoutMs = milliseconds > 0 ? milliseconds : 0;
    return out;
}

ToastOptions ToastOptions::Persistent() {
    return {};
}

bool ToastPush(ToastStackState* s, int id, int timeoutMs) {
    ToastEntry e;
    e.id = id;
    e.status = ToastStatus::Starting;
    e.hasTimeout = timeoutMs > 0;
    e.timeoutRemainingMs = timeoutMs;
    e.elapsedMs = 0;
    return VecAppend(s->entries, e);
}

static void ToastEraseAt(ToastStackState* s, int i) {
    for (int j = i; j < s->entries.len - 1; j++) {
        s->entries[j] = s->entries[j + 1];
    }
    s->entries.len--;
}

bool ToastRemove(ToastStackState* s, int id) {
    for (int i = 0; i < s->entries.len; i++) {
        if (s->entries[i].id == id) {
            ToastEraseAt(s, i);
            return true;
        }
    }
    return false;
}

bool ToastStackAdvance(ToastStackState* s, int deltaMs, bool paused) {
    bool changed = false;
    for (int i = 0; i < s->entries.len; i++) {
        ToastEntry* e = &s->entries[i];
        switch (e->status) {
            case ToastStatus::Starting:
                e->elapsedMs += deltaMs;
                if (e->elapsedMs >= s->transitionMs) {
                    e->status = ToastStatus::Present;
                    e->elapsedMs = 0;
                    changed = true;
                }
                break;
            case ToastStatus::Present:
                // Only this arm is guarded by `paused`, so a pointer resting
                // on the stack stops the countdown and nothing else.
                if (paused || !e->hasTimeout) {
                    break;
                }
                e->timeoutRemainingMs -= deltaMs;
                if (e->timeoutRemainingMs <= 0) {
                    e->timeoutRemainingMs = 0;
                    e->status = ToastStatus::Ending;
                    e->elapsedMs = 0;
                    changed = true;
                }
                break;
            case ToastStatus::Ending:
                e->elapsedMs += deltaMs;
                break;
        }
    }
    int i = 0;
    while (i < s->entries.len) {
        if (s->entries[i].status == ToastStatus::Ending &&
            s->entries[i].elapsedMs >= s->exitMs) {
            ToastEraseAt(s, i);
            changed = true;
        } else {
            i++;
        }
    }
    return changed;
}

float ToastStackGeometry(const float* heights, int n, float peek, float gap,
                         bool anchoredBottom, float* collapsedOffsets,
                         float* expandedOffsets, float* expandedHeight) {
    if (n <= 0) {
        if (expandedHeight) {
            *expandedHeight = 0;
        }
        return 0;
    }
    float expanded = 0;
    for (int i = 0; i < n; i++) {
        expanded += heights[i];
    }
    expanded += gap * (float)(n - 1);

    // Closed, the stack is as tall as the front toast plus a sliver of each
    // one behind it — or as tall as a taller one behind, whichever wins.
    float collapsed = heights[n - 1] + peek * (float)(n - 1);
    for (int i = 0; i < n; i++) {
        int rank = n - 1 - i;
        float h = heights[i] + peek * (float)rank;
        if (h > collapsed) {
            collapsed = h;
        }
    }

    for (int i = 0; i < n; i++) {
        int rank = n - 1 - i;
        float newer = 0;
        for (int j = i + 1; j < n; j++) {
            newer += heights[j];
        }
        float e = anchoredBottom
                      ? expanded - newer - gap * (float)rank - heights[i]
                      : newer + gap * (float)rank;
        float c = anchoredBottom ? collapsed - heights[i] - peek * (float)rank
                                 : peek * (float)rank;
        if (expandedOffsets) {
            expandedOffsets[i] = e;
        }
        if (collapsedOffsets) {
            collapsedOffsets[i] = c;
        }
    }
    if (expandedHeight) {
        *expandedHeight = expanded;
    }
    return collapsed;
}

static bool ToastStackBottom(Anchor anchor) {
    return anchor == Anchor::BottomLeft || anchor == Anchor::BottomCenter ||
           anchor == Anchor::BottomRight;
}

static ToastMeasurement* ToastStackMeasurement(ToastStackState* state,
                                               uint32_t id, bool create) {
    for (int i = 0; i < state->heights.len; i++) {
        if (state->heights[i].id == id) {
            return &state->heights[i];
        }
    }
    if (!create) {
        return nullptr;
    }
    ToastMeasurement measurement;
    measurement.id = id;
    if (!VecAppend(state->heights, measurement)) {
        return nullptr;
    }
    return &state->heights[state->heights.len - 1];
}

ToastStack* ToastStack::New(Ctx* context, Str value,
                            ToastStackState* stackState) {
    ToastStack* out = ArenaNew<ToastStack>(context->a);
    out->arena = context->a;
    out->cx = context;
    out->id = value;
    out->state = stackState;
    out->motion = ToastMotion::Sonner();
    return out;
}

ToastStack* ToastStack::Item(Str value, El* child) {
    if (!child) {
        return this;
    }
    ToastStackItem item;
    item.id = value;
    item.key = (uint32_t)HashClickId(value);
    item.child = child;
    children.Append(arena, item);
    return this;
}

ToastStack* ToastStack::Child(El* child) {
    Str childId = ElementIdNamed(arena, StrL("toast-stack-child"),
                                 (uint64_t)children.len);
    return Item(childId, child);
}

ToastStack* ToastStack::Motion(ToastMotion value) {
    motion = value;
    return this;
}

ToastStack* ToastStack::Placement(Anchor value) {
    placement = value;
    return this;
}

ToastStack* ToastStack::Focus(FocusHandle value) {
    focus = value;
    hasFocus = value.IsValid();
    return this;
}

ToastStack* ToastStack::Refine(const Style& value, uint32_t fields) {
    StyleApplyFields(&style, value, fields);
    styleFields |= fields;
    return this;
}

static bool ToastStackHasItem(const ToastStack* stack, uint32_t id) {
    for (int i = 0; i < stack->children.len; i++) {
        if (stack->children[i].key == id) {
            return true;
        }
    }
    return false;
}

El* ToastStack::IntoEl() {
    if (!state) {
        return Div(arena);
    }
    // Measurements are retained by stable item id, and disappear with their
    // item. Bounds are one frame old, like Rust's prepaint callback state.
    for (int i = state->heights.len - 1; i >= 0; i--) {
        if (ToastStackHasItem(this, state->heights[i].id)) {
            continue;
        }
        for (int j = i; j < state->heights.len - 1; j++) {
            state->heights[j] = state->heights[j + 1];
        }
        state->heights.len--;
    }
    if (state->bounds.w > 0 && cx->win) {
        state->hovered = state->bounds
                             .Contains({cx->win->mouseX, cx->win->mouseY});
    }
    state->focused = hasFocus && FocusHandleContainsFocused(cx->win, focus);
    bool expanded = state->IsExpanded();
    bool bottom = ToastStackBottom(placement);
    int count = children.len;
    float* heights =
        count ? (float*)Alloc(arena, (int)sizeof(float) * count) : nullptr;
    float* collapsed =
        count ? (float*)Alloc(arena, (int)sizeof(float) * count) : nullptr;
    float* expandedOffsets =
        count ? (float*)Alloc(arena, (int)sizeof(float) * count) : nullptr;
    for (int i = 0; i < count; i++) {
        ToastMeasurement* measured =
            ToastStackMeasurement(state, children[i].key, true);
        heights[i] = measured && measured->bounds.h > 0 ? measured->bounds.h
                     : children[i].child->style.height > 0
                         ? children[i].child->style.height
                         : 0;
    }
    float expandedHeight = 0;
    float collapsedHeight = ToastStackGeometry(
        heights, count, motion.collapsedPeek, motion.expandedGap, bottom,
        collapsed, expandedOffsets, &expandedHeight);
    Spring geometry = SpringNew((float)motion.durationMs);
    geometry.epsilon = 0.1f;
    Spring fade = SpringNew((float)motion.durationMs);
    float stackHeight =
        SpringValue(cx, MotionId(id, StrL("height")),
                    expanded ? expandedHeight : collapsedHeight, geometry);

    El* root = Div(arena)
                   ->Id(id)
                   ->H(stackHeight)
                   ->BoundsOut(&state->bounds)
                   ->Refine(style, styleFields);
    if (hasFocus) {
        root->TrackFocus(focus);
    }
    float stackWidth = state->bounds.w;
    int visibleLayers =
        motion.collapsedVisible > 0 ? motion.collapsedVisible : 1;
    for (int i = 0; i < count; i++) {
        const ToastStackItem& item = children[i];
        int rank = count - 1 - i;
        Str key = StrDup(arena, fmt("%u", item.key));
        float offset =
            SpringValue(cx, MotionId(key, StrL("offset")),
                        expanded ? expandedOffsets[i] : collapsed[i], geometry);
        int visibleRank = rank < visibleLayers ? rank : visibleLayers - 1;
        float targetInset = expanded ? 0.f
                                     : stackWidth * motion.collapsedScaleStep *
                                           (float)visibleRank / 2.f;
        float inset = SpringValue(cx, MotionId(key, StrL("inset")), targetInset,
                                  geometry);
        float opacity =
            SpringValue(cx, MotionId(key, StrL("visibility")),
                        (expanded || rank < visibleLayers) ? 1.f : 0.f, fade);
        if (opacity <= 0.001f) {
            continue;
        }
        ToastMeasurement* measured =
            ToastStackMeasurement(state, item.key, true);
        El* layer = Div(arena)
                        ->Id(item.id)
                        ->Absolute()
                        ->Top(offset)
                        ->Left(inset)
                        ->Right(inset)
                        ->Opacity(opacity)
                        ->Child(item.child);
        if (measured) {
            layer->BoundsOut(&measured->bounds);
        }
        root->Child(layer);
    }
    return root;
}

Toast* Toast::New(Ctx* cx, Str id) {
    Toast* out = ArenaNew<Toast>(cx->a);
    out->root = Div(cx->a)->Id(id)->Role(AccessibilityRole::Alert);
    return out;
}

Toast* Toast::TransitionStatus(ToastTransitionStatus value) {
    transitionStatus = value;
    return this;
}

ToastTransitionStatus Toast::Status() const {
    return transitionStatus;
}

Toast* Toast::Child(El* child) {
    root->Child(child);
    return this;
}

Toast* Toast::Refine(const Style& value, uint32_t fields) {
    root->Refine(value, fields);
    return this;
}

El* Toast::IntoEl() {
    return root;
}
} // namespace gpui
