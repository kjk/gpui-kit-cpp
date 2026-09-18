#include "ui/carousel.h"
#include "base/actions.h"
#include "base/global_state.h"
#include "base/motion.h"
#include "gpui/keymap.h"
#include "ui/i18n.h"

#include <math.h>

namespace gpui {
namespace component {

static const float kPointerAxisLock = 2.f;
static const int kScrollSettleMs = 28;
static const float kItemGap = 16.f;

static float AxisValue(Point p, Axis axis) {
    return axis == Axis::Horizontal ? p.x : p.y;
}
static void SetAxisValue(Point* p, Axis axis, float value) {
    if (axis == Axis::Horizontal)
        p->x = value;
    else
        p->y = value;
}
static float AxisStart(Bounds b, Axis axis) {
    return axis == Axis::Horizontal ? b.x : b.y;
}
static float AxisEnd(Bounds b, Axis axis) {
    return axis == Axis::Horizontal ? b.Right() : b.Bottom();
}
static Point AxisPoint(Axis axis, float value) {
    return axis == Axis::Horizontal ? Point{value, 0} : Point{0, value};
}
static float Clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

CarouselState CarouselState::New(int count) {
    CarouselState state;
    state.itemCount = std::max(0, count);
    state.selectedIndex = state.itemCount > 0 ? 0 : -1;
    return state;
}

CarouselState& CarouselState::WithSelectedIndex(int index) {
    if (itemCount <= 0) {
        selectedIndex = -1;
    } else {
        selectedIndex = std::max(0, std::min(index, itemCount - 1));
    }
    return *this;
}
CarouselState& CarouselState::WithAxis(Axis value) {
    axis = value;
    return *this;
}
CarouselState& CarouselState::WithLooping(bool value) {
    looping = value;
    return *this;
}

static bool GeometryReady(const CarouselState* s) {
    return s->hasViewport && s->items.len == s->itemCount;
}
static bool RuntimeLooping(const CarouselState* s) {
    return s->looping && (!GeometryReady(s) || s->loopLayout.active);
}

static int LogicalNav(const CarouselState* s, int current, bool next) {
    if (next) {
        if (current + 1 < s->itemCount) return current + 1;
        if (RuntimeLooping(s) && s->itemCount > 1) return 0;
        return -1;
    }
    if (current > 0) return current - 1;
    if (RuntimeLooping(s) && s->itemCount > 1) return s->itemCount - 1;
    return -1;
}

static float PrimaryOffset(const CarouselState* s, Point p) {
    return AxisValue(p, s->axis);
}

static bool HasSnap(const CarouselState* s, int index, Point* out);

static int NavigationIndex(const CarouselState* s, int current, bool next) {
    if (RuntimeLooping(s) || !GeometryReady(s)) {
        return LogicalNav(s, current, next);
    }
    Point currentTarget;
    if (!HasSnap(s, current, &currentTarget)) return -1;
    float cur = PrimaryOffset(s, currentTarget);
    if (next) {
        for (int i = current + 1; i < s->itemCount; i++) {
            Point t;
            if (HasSnap(s, i, &t) && PrimaryOffset(s, t) != cur) return i;
        }
        return -1;
    }
    for (int i = current - 1; i >= 0; i--) {
        Point t;
        if (HasSnap(s, i, &t) && PrimaryOffset(s, t) != cur) return i;
    }
    return -1;
}

bool CarouselState::HasPrevious() const {
    return selectedIndex >= 0 &&
           NavigationIndex(this, selectedIndex, false) >= 0;
}
bool CarouselState::HasNext() const {
    return selectedIndex >= 0 &&
           NavigationIndex(this, selectedIndex, true) >= 0;
}

FocusHandle CarouselState::Focus(Ctx* cx) {
    if (!focus.IsValid()) {
        focus = FocusHandleNew(cx);
    }
    return focus;
}
void CarouselState::SuppressFocusRing(bool suppressed) {
    focusRingSuppressed = suppressed;
}
bool CarouselState::IsInteracting() const {
    return pointerGesture.active || scrollGesture.active;
}
bool CarouselState::IsPointerDragLocked() const {
    return pointerGesture.active && pointerGesture.axisLocked;
}
Size CarouselState::FrameSize() const {
    return hasFrame ? Size{frame.w, frame.h} : Size{};
}
float CarouselState::LoopRunway() const {
    return loopLayout.active ? loopLayout.cycleExtent : -1.f;
}
bool CarouselState::IsLoopLayoutTransitioning() const {
    return loopLayoutRemovalPending ||
           (loopLayout.active && !loopLayout.runwayReady);
}

static void ShiftPoint(Point* p, Axis axis, float delta) {
    SetAxisValue(p, axis, AxisValue(*p, axis) + delta);
}

static void ShiftScrollCoordinate(CarouselState* s, float delta) {
    ShiftPoint(&s->offset, s->axis, delta);
    if (s->pointerGesture.active) {
        ShiftPoint(&s->pointerGesture.startOffset, s->axis, delta);
    }
    if (s->scrollGesture.active) {
        ShiftPoint(&s->scrollGesture.startOffset, s->axis, delta);
    }
    if (s->hasLoopMotionTarget) {
        ShiftPoint(&s->loopMotionTarget, s->axis, delta);
    }
}

static float GeometryMaxOffset(const CarouselState* s) {
    if (!s->hasViewport || len(s->items) == 0) return 0;
    float contentStart = AxisStart(s->items[0], s->axis);
    float contentEnd = AxisEnd(s->items[0], s->axis);
    for (int i = 1; i < len(s->items); i++) {
        contentStart = std::min(contentStart, AxisStart(s->items[i], s->axis));
        contentEnd = std::max(contentEnd, AxisEnd(s->items[i], s->axis));
    }
    float extent = s->axis == Axis::Horizontal ? s->viewport.w : s->viewport.h;
    float geometryMax = std::max(contentEnd - contentStart - extent, 0.f);
    if (s->loopLayout.active && s->loopLayout.runwayReady) {
        geometryMax += 2.f * s->loopLayout.runwayExtent;
    }
    return geometryMax;
}

static float MaxSnapOffset(const CarouselState* s) {
    float handleMax = std::max(PrimaryOffset(s, s->maxOffset), 0.f);
    return std::max(handleMax, GeometryMaxOffset(s));
}

static Point SnapOffset(const CarouselState* s, Bounds viewport, Bounds item) {
    Point out = s->offset;
    float target =
        s->axis == Axis::Horizontal ? viewport.x - item.x : viewport.y - item.y;
    if (s->loopLayout.active && s->loopLayout.runwayReady) {
        float contentInset = 0;
        if (s->items.len > 0) {
            contentInset = AxisStart(s->items[0], s->axis) -
                           AxisStart(viewport, s->axis) -
                           s->loopLayout.runwayExtent;
        }
        target += contentInset;
    } else {
        target = Clampf(target, -MaxSnapOffset(s), 0.f);
    }
    SetAxisValue(&out, s->axis, target);
    return out;
}

static bool HasSnap(const CarouselState* s, int index, Point* out) {
    if (!s->hasViewport || index < 0 || index >= s->items.len) return false;
    *out = SnapOffset(s, s->viewport, s->items[index]);
    return true;
}

bool CarouselState::HasSnapTarget(int index) const {
    Point t;
    return HasSnap(this, index, &t);
}
Point CarouselState::SnapTargetFor(int index) const {
    Point t = {};
    HasSnap(this, index, &t);
    return t;
}

static bool AdjacentLoopTarget(const CarouselState* s, int current, int next,
                               Point* out) {
    if (!s->loopLayout.active || !s->loopLayout.runwayReady) return false;
    Point target;
    if (!HasSnap(s, next, &target)) return false;
    float cycle = 0;
    if (current + 1 == s->itemCount && next == 0) {
        cycle = -s->loopLayout.cycleExtent;
    } else if (current == 0 && next + 1 == s->itemCount) {
        cycle = s->loopLayout.cycleExtent;
    } else {
        return false;
    }
    SetAxisValue(&target, s->axis, PrimaryOffset(s, target) + cycle);
    if (out) *out = target;
    return true;
}

static bool IsLoopWrap(const CarouselState* s, int current, int next) {
    return RuntimeLooping(s) && s->itemCount > 1 &&
           ((current == 0 && next + 1 == s->itemCount) ||
            (current + 1 == s->itemCount && next == 0));
}

Point CarouselState::LoopItemOffset(int index) const {
    if (!loopLayout.active) return {};
    if (!loopLayout.runwayReady) {
        return AxisPoint(axis, -loopLayout.runwayExtent);
    }
    if (!hasViewport || index < 0 || index >= items.len) return {};
    float viewStart = AxisStart(viewport, axis);
    float extent = axis == Axis::Horizontal ? viewport.w : viewport.h;
    float viewportCenter =
        viewStart + extent / 2.f - PrimaryOffset(this, offset);
    float itemCenter =
        AxisStart(items[index], axis) +
        (axis == Axis::Horizontal ? items[index].w : items[index].h) / 2.f;
    float cycles = (viewportCenter - itemCenter) / loopLayout.cycleExtent;
    if (cycles > 0)
        cycles = floorf(cycles + 0.5f);
    else
        cycles = ceilf(cycles - 0.5f);
    cycles = Clampf(cycles, -1.f, 1.f);
    return AxisPoint(axis, loopLayout.cycleExtent * cycles);
}

bool CarouselState::HasMotionTargetFor(int index) const {
    if (hasLoopMotionTarget && selectedIndex == index) return true;
    Point t;
    return HasSnap(this, index, &t);
}
Point CarouselState::MotionTargetFor(int index) const {
    if (hasLoopMotionTarget && selectedIndex == index) return loopMotionTarget;
    return SnapTargetFor(index);
}

static bool MeasuredCycle(const CarouselState* s, float* extent, float* gap) {
    if (s->items.len == 0) return false;
    Bounds first = s->items[0];
    Bounds last = s->items[s->items.len - 1];
    *gap = 0;
    if (s->items.len > 1) {
        *gap = std::max(
            AxisStart(s->items[1], s->axis) - AxisEnd(first, s->axis), 0.f);
    }
    *extent = std::max(
        AxisEnd(last, s->axis) - AxisStart(first, s->axis) + *gap, 0.f);
    return true;
}

static void UpdateLoopLayout(CarouselState* s) {
    CarouselLoopLayout previous = s->loopLayout;
    float nextExtent = 0, nextGap = 0;
    bool ok = MeasuredCycle(s, &nextExtent, &nextGap);
    if (ok) {
        float viewExtent =
            s->hasViewport
                ? (s->axis == Axis::Horizontal ? s->viewport.w : s->viewport.h)
                : 0;
        ok = s->looping && s->itemCount > 1 && nextExtent > 0 &&
             s->hasViewport && nextExtent >= viewExtent;
    }
    if (!ok) {
        if (previous.active && previous.runwayReady) {
            ShiftScrollCoordinate(s, previous.runwayExtent);
            s->motionRevision++;
        }
        if (previous.active) {
            s->loopLayoutRemovalPending = s->geometryHasRunway;
        } else if (s->loopLayoutRemovalPending && !s->geometryHasRunway) {
            s->loopLayoutRemovalPending = false;
        }
        s->loopLayout = {};
        s->hasLoopMotionTarget = false;
        return;
    }
    bool sameExtent = previous.active &&
                      fabsf(previous.cycleExtent - nextExtent) <= 0.5f &&
                      fabsf(previous.trackGap - nextGap) <= 0.5f;
    if (!sameExtent) {
        if (previous.active && previous.runwayReady) {
            ShiftScrollCoordinate(s, previous.runwayExtent);
            s->pointerGesture = {};
            s->scrollGesture = {};
            s->hasLoopMotionTarget = false;
            s->motionRevision++;
        }
        s->loopLayoutRemovalPending = false;
        s->loopLayout.cycleExtent = nextExtent;
        s->loopLayout.trackGap = nextGap;
        s->loopLayout.runwayExtent = nextExtent + nextGap;
        s->loopLayout.runwayReady = false;
        s->loopLayout.active = true;
        return;
    }
    if (!previous.active) return;
    if (!previous.runwayReady && s->geometryHasRunway) {
        ShiftScrollCoordinate(s, -previous.runwayExtent);
        previous.runwayReady = true;
        s->loopLayout = previous;
        s->motionRevision++;
    }
}

void CarouselState::SetGeometry(Bounds vp, Bounds fr, const Bounds* itemBounds,
                                int n, bool hasRunway) {
    viewport = vp;
    frame = fr;
    hasViewport = true;
    hasFrame = true;
    VecClear(items);
    for (int i = 0; i < n; i++) VecAppend(items, itemBounds[i]);
    geometryHasRunway = hasRunway;
    UpdateLoopLayout(this);
    maxOffset = AxisPoint(axis, GeometryMaxOffset(this));
}

void CarouselState::SetGeometry(Bounds vp, const Bounds* itemBounds, int n) {
    SetGeometry(vp, vp, itemBounds, n, loopLayout.active);
}

static bool RebasePendingLoopMotion(CarouselState* s) {
    if (!s->hasLoopMotionTarget) return false;
    Point real;
    if (s->selectedIndex < 0 || !HasSnap(s, s->selectedIndex, &real)) {
        s->hasLoopMotionTarget = false;
        return false;
    }
    float delta =
        PrimaryOffset(s, real) - PrimaryOffset(s, s->loopMotionTarget);
    ShiftScrollCoordinate(s, delta);
    s->hasLoopMotionTarget = false;
    s->motionRevision++;
    return true;
}

bool CarouselState::SettleLoopMotion(Point rendered, Point* out, Ctx* cx) {
    if (!hasLoopMotionTarget) return false;
    if (fabsf(PrimaryOffset(this, rendered) -
              PrimaryOffset(this, loopMotionTarget)) > 0.01f) {
        return false;
    }
    if (selectedIndex < 0) return false;
    hasLoopMotionTarget = false;
    Point real;
    if (!HasSnap(this, selectedIndex, &real)) return false;
    offset = real;
    motionRevision++;
    if (cx) Notify(cx);
    if (out) *out = real;
    return true;
}

static void InvalidateScrollSettle(CarouselState* s) {
    s->scrollSettleEpoch++;
}

static void ScheduleTimeout(CarouselState* s, Ctx* cx, int* epoch,
                            void (*fn)(CarouselState*, Ctx*, const TickEvent*,
                                       intptr_t)) {
    if (!cx || !cx->win) return;
    *epoch = *epoch + 1;
    WindowSetTimeout(cx->win, kScrollSettleMs, ListenTo(s->self, fn, *epoch));
}

void CarouselState::OnScrollSettle(CarouselState* self, Ctx* cx,
                                   const TickEvent*, intptr_t epoch) {
    if (self->scrollSettleEpoch == (int)epoch && self->scrollGesture.active) {
        self->FinishScroll(false, cx);
    }
}
void CarouselState::OnIgnoredScrollRecovery(CarouselState* self, Ctx*,
                                            const TickEvent*, intptr_t epoch) {
    if (self->scrollSettleEpoch == (int)epoch) {
        self->ignoreScrollUntilQuiet = false;
    }
}
void CarouselState::OnWheelBurstEnd(CarouselState* self, Ctx*, const TickEvent*,
                                    intptr_t epoch) {
    if (self->wheelBurstEpoch == (int)epoch) {
        self->wheelBurstActive = false;
    }
}

static void CancelInteractions(CarouselState* s, Ctx* cx) {
    bool cancelledScroll = s->scrollGesture.active;
    s->pointerGesture = {};
    s->scrollGesture = {};
    InvalidateScrollSettle(s);
    if (cancelledScroll) {
        s->ignoreScrollUntilQuiet = true;
        ScheduleTimeout(s, cx, &s->scrollSettleEpoch,
                        &CarouselState::OnIgnoredScrollRecovery);
    }
}

static bool SelectIndexWithWrap(CarouselState* s, int index, bool wrapped,
                                Ctx* cx) {
    if (s->itemCount <= 0 || index < 0) return false;
    index = std::min(index, s->itemCount - 1);
    bool rebased = RebasePendingLoopMotion(s);
    bool wasInteracting = s->IsInteracting();
    CancelInteractions(s, cx);
    if (s->selectedIndex == index) {
        if ((wasInteracting || rebased) && cx) Notify(cx);
        return false;
    }
    int current = s->selectedIndex;
    Point loopTarget = {};
    bool hasLoop = wrapped && current >= 0 &&
                   AdjacentLoopTarget(s, current, index, &loopTarget);
    Point real;
    if (hasLoop && HasSnap(s, index, &real) &&
        PrimaryOffset(s, real) != PrimaryOffset(s, loopTarget)) {
        s->loopMotionTarget = loopTarget;
        s->hasLoopMotionTarget = true;
    } else {
        s->hasLoopMotionTarget = false;
    }
    s->selectedIndex = index;
    if (wrapped && !hasLoop) s->motionRevision++;
    if (cx) {
        CarouselEvent event = {index};
        if (s->self.IsValid()) EntityEmit(cx->app, cx->win, s->self, &event);
        Notify(cx);
    }
    return true;
}

void CarouselState::SetSelectedIndex(int index, Ctx* cx) {
    bool rebased = RebasePendingLoopMotion(this);
    int next =
        itemCount <= 0 ? -1 : std::max(0, std::min(index, itemCount - 1));
    bool changed = selectedIndex != next || IsInteracting() || rebased;
    int current = selectedIndex;
    bool wrapped = current >= 0 && next >= 0 && IsLoopWrap(this, current, next);
    Point loopTarget = {};
    bool hasLoop = wrapped && current >= 0 && next >= 0 &&
                   AdjacentLoopTarget(this, current, next, &loopTarget);
    Point real;
    if (hasLoop && HasSnap(this, next, &real) &&
        PrimaryOffset(this, real) != PrimaryOffset(this, loopTarget)) {
        loopMotionTarget = loopTarget;
        hasLoopMotionTarget = true;
    } else {
        hasLoopMotionTarget = false;
    }
    if (wrapped && !hasLoop) motionRevision++;
    selectedIndex = next;
    CancelInteractions(this, cx);
    if (changed && cx) Notify(cx);
}
void CarouselState::SetItemCount(int count, Ctx* cx) {
    count = std::max(0, count);
    if (itemCount == count) return;
    itemCount = count;
    if (count == 0) {
        selectedIndex = -1;
    } else if (selectedIndex < 0) {
        selectedIndex = 0;
    } else if (selectedIndex >= count) {
        selectedIndex = count - 1;
    }
    if (loopLayout.active) {
        offset = {};
        motionRevision++;
    }
    VecClear(items);
    loopLayout = {};
    loopLayoutRemovalPending = geometryHasRunway;
    hasLoopMotionTarget = false;
    CancelInteractions(this, cx);
    if (cx) Notify(cx);
}
void CarouselState::SetAxis(Axis value, Ctx* cx) {
    if (axis == value) return;
    axis = value;
    offset = {};
    loopLayout = {};
    loopLayoutRemovalPending = geometryHasRunway;
    hasLoopMotionTarget = false;
    CancelInteractions(this, cx);
    motionRevision++;
    if (cx) Notify(cx);
}
void CarouselState::SetLooping(bool value, Ctx* cx) {
    if (looping == value) return;
    looping = value;
    UpdateLoopLayout(this);
    hasLoopMotionTarget = false;
    CancelInteractions(this, cx);
    if (cx) Notify(cx);
}
bool CarouselState::SelectIndex(int index, Ctx* cx) {
    if (index < 0 || index >= itemCount) {
        if (IsInteracting()) {
            CancelInteractions(this, cx);
            if (cx) Notify(cx);
        }
        return false;
    }
    bool wrapped = selectedIndex >= 0 && IsLoopWrap(this, selectedIndex, index);
    return SelectIndexWithWrap(this, index, wrapped, cx);
}
bool CarouselState::SelectPrevious(Ctx* cx) {
    if (selectedIndex < 0) return false;
    int next = NavigationIndex(this, selectedIndex, false);
    if (next < 0) {
        if (IsInteracting()) {
            CancelInteractions(this, cx);
            if (cx) Notify(cx);
        }
        return false;
    }
    bool wrapped = IsLoopWrap(this, selectedIndex, next);
    return SelectIndexWithWrap(this, next, wrapped, cx);
}
bool CarouselState::SelectNext(Ctx* cx) {
    if (selectedIndex < 0) return false;
    int next = NavigationIndex(this, selectedIndex, true);
    if (next < 0) {
        if (IsInteracting()) {
            CancelInteractions(this, cx);
            if (cx) Notify(cx);
        }
        return false;
    }
    bool wrapped = IsLoopWrap(this, selectedIndex, next);
    return SelectIndexWithWrap(this, next, wrapped, cx);
}
bool CarouselState::SelectFirst(Ctx* cx) {
    return SelectIndex(0, cx);
}
bool CarouselState::SelectLast(Ctx* cx) {
    if (itemCount <= 0) {
        if (IsInteracting()) {
            CancelInteractions(this, cx);
            if (cx) Notify(cx);
        }
        return false;
    }
    return SelectIndex(itemCount - 1, cx);
}

int CarouselState::NearestIndex(Point at) const {
    if (!hasViewport || items.len == 0) return -1;
    int best = 0;
    float bestDist = 1e9f;
    for (int i = 0; i < items.len; i++) {
        Point target = SnapOffset(this, viewport, items[i]);
        float distance =
            fabsf(PrimaryOffset(this, at) - PrimaryOffset(this, target));
        if (loopLayout.active && loopLayout.runwayReady) {
            float off = PrimaryOffset(this, at);
            float tgt = PrimaryOffset(this, target);
            distance =
                std::min(distance,
                         std::min(fabsf(off - (tgt - loopLayout.cycleExtent)),
                                  fabsf(off - (tgt + loopLayout.cycleExtent))));
        }
        if (distance < bestDist) {
            bestDist = distance;
            best = i;
        }
    }
    return best;
}

static float ClampedOffset(const CarouselState* s, float value) {
    float bound = std::max(MaxSnapOffset(s), 0.f);
    return Clampf(value, -bound, 0.f);
}

bool CarouselState::NormalizeLoopCoordinate() {
    if (!loopLayout.active || !loopLayout.runwayReady ||
        loopLayout.cycleExtent <= 0) {
        return false;
    }
    Point first, last;
    if (!HasSnap(this, 0, &first)) return false;
    int lastIx = itemCount - 1;
    if (lastIx < 0 || !HasSnap(this, lastIx, &last)) return false;
    float firstV = PrimaryOffset(this, first);
    float lastV = PrimaryOffset(this, last);
    float current = PrimaryOffset(this, offset);
    float delta = 0;
    while (current <= firstV - loopLayout.cycleExtent) {
        current += loopLayout.cycleExtent;
        delta += loopLayout.cycleExtent;
    }
    while (current >= lastV + loopLayout.cycleExtent) {
        current -= loopLayout.cycleExtent;
        delta -= loopLayout.cycleExtent;
    }
    if (delta == 0) return false;
    ShiftScrollCoordinate(this, delta);
    return true;
}

static float SnapExtent(const CarouselState* s) {
    if (s->hasViewport) {
        return s->axis == Axis::Horizontal ? s->viewport.w : s->viewport.h;
    }
    if (s->items.len > 0) {
        return s->axis == Axis::Horizontal ? s->items[0].w : s->items[0].h;
    }
    return 1.f;
}

static int LoopBoundaryIndex(const CarouselState* s, int startIndex,
                             float totalDelta) {
    if (startIndex < 0) return -1;
    float threshold = std::max(SnapExtent(s) * 0.25f, 1.f);
    if (fabsf(totalDelta) < threshold) return -1;
    if (startIndex == 0 && totalDelta > 0) return s->itemCount - 1;
    if (startIndex + 1 == s->itemCount && totalDelta < 0) return 0;
    return -1;
}

static bool FinishSnapshot(CarouselState* s, Point startOffset, int startIndex,
                           float totalDelta, Ctx* cx) {
    int selected = -1;
    if (RuntimeLooping(s)) {
        selected = LoopBoundaryIndex(s, startIndex, totalDelta);
        if (selected < 0) selected = s->NearestIndex(s->offset);
    } else {
        selected = s->NearestIndex(s->offset);
    }
    bool changed = false;
    if (selected >= 0 && selected != s->selectedIndex) {
        bool wrapped =
            s->selectedIndex >= 0 && IsLoopWrap(s, s->selectedIndex, selected);
        changed = SelectIndexWithWrap(s, selected, wrapped, cx);
    }
    if (selected < 0) s->offset = startOffset;
    if (!changed && cx) Notify(cx);
    return changed;
}

bool CarouselState::BeginDrag(Point position, Ctx* cx) {
    bool cancelledScroll = scrollGesture.active;
    suppressPointerClick = false;
    if (itemCount < 2) return false;
    RebasePendingLoopMotion(this);
    pointerGesture.startPosition = position;
    pointerGesture.startOffset = offset;
    pointerGesture.startIndex = selectedIndex;
    pointerGesture.totalDelta = 0;
    pointerGesture.axisLocked = false;
    pointerGesture.active = true;
    if (scrollGesture.active) ignoreScrollUntilQuiet = true;
    scrollGesture = {};
    InvalidateScrollSettle(this);
    if (cancelledScroll) {
        ScheduleTimeout(this, cx, &scrollSettleEpoch, &OnIgnoredScrollRecovery);
    }
    if (cx) Notify(cx);
    return true;
}

bool CarouselState::UpdateDrag(Point position, Ctx* cx) {
    if (!pointerGesture.active) return false;
    Point delta = {position.x - pointerGesture.startPosition.x,
                   position.y - pointerGesture.startPosition.y};
    float primary = AxisValue(delta, axis);
    if (!pointerGesture.axisLocked) {
        float cross = axis == Axis::Horizontal ? delta.y : delta.x;
        if (std::max(fabsf(primary), fabsf(cross)) <= kPointerAxisLock) {
            return false;
        }
        if (fabsf(cross) > fabsf(primary)) {
            pointerGesture = {};
            suppressPointerClick = true;
            if (cx) Notify(cx);
            return false;
        }
        pointerGesture.axisLocked = true;
    }
    pointerGesture.totalDelta = primary;
    Point next = pointerGesture.startOffset;
    SetAxisValue(&next, axis,
                 ClampedOffset(this, PrimaryOffset(this, next) + primary));
    Point previous = offset;
    offset = next;
    NormalizeLoopCoordinate();
    if (offset.x != previous.x || offset.y != previous.y) {
        if (cx) Notify(cx);
    }
    return true;
}

bool CarouselState::FinishDrag(Ctx* cx) {
    bool suppressed = suppressPointerClick;
    suppressPointerClick = false;
    if (!pointerGesture.active) {
        if (suppressed && cx) Notify(cx);
        return false;
    }
    CarouselPointerGesture g = pointerGesture;
    pointerGesture = {};
    return FinishSnapshot(this, g.startOffset, g.startIndex, g.totalDelta, cx);
}

bool CarouselState::HandleScrollDelta(Axis value, float delta, TouchPhase phase,
                                      Ctx* cx) {
    if (value != axis || itemCount < 2) return false;
    RebasePendingLoopMotion(this);
    if (ignoreScrollUntilQuiet) {
        if (phase == TouchPhase::Started) {
            ignoreScrollUntilQuiet = false;
            InvalidateScrollSettle(this);
        } else if (phase == TouchPhase::Ended ||
                   phase == TouchPhase::Cancelled) {
            ignoreScrollUntilQuiet = false;
            InvalidateScrollSettle(this);
            return false;
        } else {
            ScheduleTimeout(this, cx, &scrollSettleEpoch,
                            &OnIgnoredScrollRecovery);
            return false;
        }
    }
    if (phase == TouchPhase::Started && scrollGesture.active) {
        FinishScroll(false, cx);
    }
    if (!scrollGesture.active) {
        pointerGesture = {};
        scrollGesture.startOffset = offset;
        scrollGesture.startIndex = selectedIndex;
        scrollGesture.totalDelta = 0;
        scrollGesture.active = true;
    }
    scrollGesture.totalDelta += delta;
    Point previous = offset;
    SetAxisValue(&offset, axis,
                 ClampedOffset(this, PrimaryOffset(this, offset) + delta));
    NormalizeLoopCoordinate();
    bool moved = offset.x != previous.x || offset.y != previous.y;
    if (moved && cx) Notify(cx);
    if (phase == TouchPhase::Started || phase == TouchPhase::Moved) {
        ScheduleTimeout(this, cx, &scrollSettleEpoch, &OnScrollSettle);
    }
    return moved || RuntimeLooping(this);
}

bool CarouselState::FinishScroll(bool cancelled, Ctx* cx) {
    InvalidateScrollSettle(this);
    if (ignoreScrollUntilQuiet) {
        ignoreScrollUntilQuiet = false;
        return false;
    }
    if (!scrollGesture.active) return false;
    CarouselScrollGesture g = scrollGesture;
    scrollGesture = {};
    if (cancelled) {
        offset = g.startOffset;
        if (cx) Notify(cx);
        return false;
    }
    return FinishSnapshot(this, g.startOffset, g.startIndex, g.totalDelta, cx);
}

void CarouselState::DeferScrollToAncestor(Ctx* cx) {
    scrollGesture = {};
    InvalidateScrollSettle(this);
    ignoreScrollUntilQuiet = true;
    ScheduleTimeout(this, cx, &scrollSettleEpoch, &OnIgnoredScrollRecovery);
}

bool CarouselState::HandleWheelStep(Axis value, float delta, Ctx* cx) {
    if (value != axis || delta == 0) return false;
    if (ignoreScrollUntilQuiet) {
        ScheduleTimeout(this, cx, &scrollSettleEpoch, &OnIgnoredScrollRecovery);
        return false;
    }
    if (wheelBurstActive) {
        ScheduleTimeout(this, cx, &wheelBurstEpoch, &OnWheelBurstEnd);
        return true;
    }
    bool stepped = delta > 0 ? SelectPrevious(cx) : SelectNext(cx);
    if (stepped) {
        wheelBurstActive = true;
        ScheduleTimeout(this, cx, &wheelBurstEpoch, &OnWheelBurstEnd);
    } else {
        ignoreScrollUntilQuiet = true;
        ScheduleTimeout(this, cx, &scrollSettleEpoch, &OnIgnoredScrollRecovery);
    }
    return stepped;
}

void CarouselState::IngestPendingGeometry(Ctx* cx) {
    // boundsOut is window space after the track's scroll; snap geometry is
    // kept in the unscrolled content space ScrollHandle uses.
    for (int i = 0; i < len(pendingItems); i++) {
        pendingItems[i].x -= offset.x;
        pendingItems[i].y -= offset.y;
    }
    bool same =
        hasViewport && hasFrame && pendingHasRunway == geometryHasRunway &&
        pendingViewport.x == viewport.x && pendingViewport.y == viewport.y &&
        pendingViewport.w == viewport.w && pendingViewport.h == viewport.h &&
        pendingFrame.x == frame.x && pendingFrame.y == frame.y &&
        pendingFrame.w == frame.w && pendingFrame.h == frame.h &&
        len(pendingItems) == len(items);
    if (same) {
        for (int i = 0; i < len(items); i++) {
            if (pendingItems[i].x != items[i].x ||
                pendingItems[i].y != items[i].y ||
                pendingItems[i].w != items[i].w ||
                pendingItems[i].h != items[i].h) {
                same = false;
                break;
            }
        }
    }
    if (same) return;
    SetGeometry(pendingViewport, pendingFrame,
                len(pendingItems) ? &pendingItems[0] : nullptr,
                len(pendingItems), pendingHasRunway);
    geometryRevision++;
    if (cx) Notify(cx);
}

void CarouselState::OnAction(CarouselState* state, Ctx* cx,
                             const ActionEvent* event) {
    bool handled = false;
    if (event->action == action::SelectFirst())
        handled = state->SelectFirst(cx);
    if (event->action == action::SelectLast()) handled = state->SelectLast(cx);
    if (event->action == action::SelectLeft() &&
        state->axis == Axis::Horizontal)
        handled = state->SelectPrevious(cx);
    if (event->action == action::SelectRight() &&
        state->axis == Axis::Horizontal)
        handled = state->SelectNext(cx);
    if (event->action == action::SelectUp() && state->axis == Axis::Vertical)
        handled = state->SelectPrevious(cx);
    if (event->action == action::SelectDown() && state->axis == Axis::Vertical)
        handled = state->SelectNext(cx);
    if (!handled) const_cast<ActionEvent*>(event)->propagate = true;
}
void CarouselState::OnPrevious(CarouselState* state, Ctx* cx,
                               const ClickEvent*) {
    state->SelectPrevious(cx);
}
void CarouselState::OnNext(CarouselState* state, Ctx* cx, const ClickEvent*) {
    state->SelectNext(cx);
}
void CarouselState::OnSelect(CarouselState* state, Ctx* cx, const ClickEvent*,
                             intptr_t index) {
    state->SelectIndex((int)index, cx);
}

void CarouselState::OnPointerDown(CarouselState* state, Ctx* cx,
                                  const MouseDownEvent* event) {
    if (!event || event->button != MouseButton::Left) return;
    if (state->BeginDrag({event->x, event->y}, cx)) {
        BaseSuppressTextSelection(cx->app);
    }
}
void CarouselState::OnPointerMove(CarouselState* state, Ctx* cx,
                                  const DragMoveEvent* event) {
    if (!event || !event->event.pressed ||
        event->event.pressedButton != MouseButton::Left) {
        return;
    }
    if (state->UpdateDrag({event->event.x, event->event.y}, cx)) {
        WindowStopPropagation(cx);
    }
}
void CarouselState::OnPointerUp(CarouselState* state, Ctx* cx,
                                const MouseUpEvent* event) {
    if (!event || event->button != MouseButton::Left) return;
    bool suppress =
        state->IsPointerDragLocked() || state->ShouldSuppressPointerClick();
    state->FinishDrag(cx);
    if (suppress && cx->win) {
        cx->win->pressPending = false;
    }
}
void CarouselState::OnWheel(CarouselState* state, Ctx* cx,
                            const ScrollWheelEvent* event) {
    if (!event) return;
    Point delta = {event->deltaX, event->deltaY};
    if (event->precise) {
        state->wheelLock.Filter(&delta, event->phase);
    }
    if (delta.x != 0 && delta.y != 0) {
        if (fabsf(delta.x) > fabsf(delta.y))
            delta.y = 0;
        else
            delta.x = 0;
    }
    if (state->axis == Axis::Horizontal)
        delta.y = 0;
    else
        delta.x = 0;
    float primary = state->axis == Axis::Horizontal ? delta.x : delta.y;
    bool consumed = false;
    if (primary == 0) {
        consumed = false;
    } else if (!event->precise) {
        consumed = state->HandleWheelStep(state->axis, primary, cx);
    } else if (event->phase == TouchPhase::Ended ||
               event->phase == TouchPhase::Cancelled) {
        consumed = false;
    } else {
        bool owned = state->HasScrollGesture();
        bool moved =
            state->HandleScrollDelta(state->axis, primary, event->phase, cx);
        if (!moved && !owned) state->DeferScrollToAncestor(cx);
        consumed = moved || owned;
    }
    if (event->precise) {
        if (event->phase == TouchPhase::Ended)
            state->FinishScroll(false, cx);
        else if (event->phase == TouchPhase::Cancelled)
            state->FinishScroll(true, cx);
    }
    if (consumed || (state->axis == Axis::Horizontal && primary != 0)) {
        const_cast<ScrollWheelEvent*>(event)->propagate = false;
    }
}
void CarouselState::OnRootMouseDown(CarouselState* state, Ctx* cx,
                                    const MouseDownEvent*) {
    if (!FocusHandleIsFocused(cx->win, state->Focus(cx))) {
        state->SuppressFocusRing(true);
    }
}

Entity<CarouselState> CarouselStateNew(App* app, int itemCount) {
    Entity<CarouselState> entity = EntityNewState<CarouselState>(app);
    if (CarouselState* state = entity.Get(app)) {
        *state = CarouselState::New(itemCount);
        state->self = entity;
    }
    return entity;
}

void CarouselInitKeys() {
    static uint32_t bound = 0;
    if (bound == KeymapGeneration()) return;
    bound = KeymapGeneration();
    const char* context = "Carousel";
    KeyBinding bindings[] = {
        {"left", action::SelectLeft(), context},
        {"right", action::SelectRight(), context},
        {"up", action::SelectUp(), context},
        {"down", action::SelectDown(), context},
        {"home", action::SelectFirst(), context},
        {"end", action::SelectLast(), context},
    };
    KeymapBind(bindings, (int)(sizeof(bindings) / sizeof(bindings[0])));
}

template <typename T>
static T* CarouselPart(Ctx* cx) {
    T* value = ArenaNew<T>(cx->a);
    value->a = cx->a;
    value->cx = cx;
    return value;
}
static void CarouselChildren(El* root, const ArenaVec<El*>& children) {
    for (El* child : children) root->Child(child);
}

static void CarouselIngestPaint(PaintCtx* ctx, El*, void* user) {
    auto* state = (CarouselState*)user;
    if (!state || !ctx) return;
    Ctx cx = {};
    cx.app = ctx->app;
    cx.win = ctx->window;
    cx.a = ctx->window ? ctx->window->frameArena : nullptr;
    state->IngestPendingGeometry(&cx);
}

Carousel* Carousel::New(Ctx* cx, Str valueId,
                        Entity<CarouselState> valueState) {
    Carousel* value = CarouselPart<Carousel>(cx);
    value->id = valueId;
    value->state = valueState;
    return value;
}
Carousel* Carousel::AccessibilityLabel(Str value) {
    accessibilityLabel = value;
    return this;
}
Carousel* Carousel::FocusRing(bool enabled) {
    focusRingEnabled = enabled;
    return this;
}
Carousel* Carousel::Child(El* child) {
    children.Append(a, child);
    return this;
}
Carousel* Carousel::Refine(const Style& value, uint32_t fields) {
    StyleApplyFields(&style, value, fields);
    styleSet |= fields;
    return this;
}
El* Carousel::IntoEl() {
    CarouselState* snapshot = state.Get(cx);
    FocusHandle focus = snapshot ? snapshot->Focus(cx) : FocusHandle{};
    bool focused = FocusHandleIsFocused(cx->win, focus);
    bool ringSuppressed = snapshot && snapshot->IsFocusRingSuppressed();
    bool focusVisible = focused && !ringSuppressed && focusRingEnabled;
    const Theme& th = ThemeNow(cx->app);
    El* root =
        Div(a)
            ->Id(id)
            ->FlexCol()
            ->Gap(16)
            ->Role(AccessibilityRole::Region)
            ->AriaLabel(accessibilityLabel.s ? accessibilityLabel
                                             : Tr("Carousel.label"))
            ->KeyContext(StrL("Carousel"))
            ->OnMouseDown(ListenTo(state, &CarouselState::OnRootMouseDown),
                          DispatchPhase::Capture)
            ->Refine(style, styleSet);
    if (focus.IsValid()) {
        root->TrackFocus(focus)->TabStop(true);
    }
    Listener actionListener = ListenTo(state, &CarouselState::OnAction);
    root->OnAction(action::SelectLeft(), actionListener)
        ->OnAction(action::SelectRight(), actionListener)
        ->OnAction(action::SelectUp(), actionListener)
        ->OnAction(action::SelectDown(), actionListener)
        ->OnAction(action::SelectFirst(), actionListener)
        ->OnAction(action::SelectLast(), actionListener);
    CarouselChildren(root, children);
    if (focusVisible && snapshot && snapshot->hasFrame) {
        El* ring = Div(a)
                       ->Absolute()
                       ->Left(0)
                       ->Top(0)
                       ->W(snapshot->frame.w)
                       ->H(snapshot->frame.h)
                       ->Border(1, th.transparent)
                       ->Radius(th.radius)
                       ->TabStop(false)
                       ->FocusRing(true);
        if (focus.IsValid()) ring->TrackFocus(focus);
        root->Child(ring);
    }
    return root;
}

CarouselContent* CarouselContent::New(Ctx* cx,
                                      Entity<CarouselState> valueState) {
    CarouselContent* value = CarouselPart<CarouselContent>(cx);
    value->state = valueState;
    return value;
}
CarouselContent* CarouselContent::Child(El* child) {
    children.Append(a, child);
    return this;
}
CarouselContent* CarouselContent::Refine(const Style& value, uint32_t fields) {
    StyleApplyFields(&style, value, fields);
    styleSet |= fields;
    return this;
}
CarouselContent* CarouselContent::TrackStyle(const Style& value,
                                             uint32_t fields) {
    StyleApplyFields(&trackStyle, value, fields);
    trackStyleSet |= fields;
    return this;
}
El* CarouselContent::IntoEl() {
    CarouselState* snapshot = state.Get(cx);
    Axis axis = snapshot ? snapshot->axis : Axis::Horizontal;
    bool interacting = snapshot && snapshot->IsInteracting();
    bool transitioning = snapshot && snapshot->IsLoopLayoutTransitioning();
    float runway = snapshot ? snapshot->LoopRunway() : -1.f;
    bool hasRunway = runway > 0;
    int selected = snapshot ? snapshot->selectedIndex : -1;
    float current = snapshot ? AxisValue(snapshot->offset, axis) : 0;
    float target = current;
    if (snapshot && !transitioning) {
        if (selected >= 0 && snapshot->HasMotionTargetFor(selected)) {
            target = AxisValue(snapshot->MotionTargetFor(selected), axis);
        }
    }
    if (interacting) target = current;
    Spring snap = ThemeNow(cx->app)
                      .motion.springMove.WithEpsilon(0.5f)
                      .WithTravel(!interacting);
    uint32_t motionKey =
        MotionId(StrL("carousel-content"),
                 fmt("offset-%d-%d", snapshot ? snapshot->motionRevision : 0,
                     snapshot ? snapshot->geometryRevision : 0));
    float animated = SpringValue(cx, motionKey, target, snap);
    Point drawn = snapshot ? snapshot->offset : Point{};
    SetAxisValue(&drawn, axis, animated);
    SetAxisValue(&drawn,
                 axis == Axis::Horizontal ? Axis::Vertical : Axis::Horizontal,
                 0);
    if (snapshot && !interacting) {
        Point rebased = {};
        if (snapshot->SettleLoopMotion(drawn, &rebased, cx)) {
            drawn = rebased;
        }
    }
    if (snapshot) snapshot->offset = drawn;

    El* track = Div(a)->Id(StrL("content"))->Flex1()->MinW(0)->MinH(0);
    if (axis == Axis::Vertical) {
        track->FlexCol()->MarginT(-kItemGap);
        track->ScrollY(-drawn.y);
    } else {
        track->FlexRow()->MarginL(-kItemGap);
        track->ScrollX(-drawn.x);
    }
    track->HideScrollbar();
    track->Refine(trackStyle, trackStyleSet);
    if (hasRunway) {
        El* lead = Div(a)->Shrink0();
        if (axis == Axis::Vertical)
            lead->H(runway);
        else
            lead->W(runway);
        track->Child(lead);
    }
    CarouselChildren(track, children);
    if (hasRunway) {
        El* trail = Div(a)->Shrink0();
        if (axis == Axis::Vertical)
            trail->H(runway);
        else
            trail->W(runway);
        track->Child(trail);
    }

    // Pointer and wheel sit on the clipped frame rather than a sibling mask:
    // capture-phase mouse down plus ancestor OnDragMove cover a press that
    // landed on a child, matching CarouselScrollMask's viewport surface.
    El* frame =
        Div(a)
            ->W(kFill)
            ->MinW(0)
            ->ClipX()
            ->ClipY()
            ->OnMouseDown(ListenTo(state, &CarouselState::OnPointerDown),
                          DispatchPhase::Capture)
            ->OnDragMove(ListenTo(state, &CarouselState::OnPointerMove))
            ->OnMouseUp(ListenTo(state, &CarouselState::OnPointerUp),
                        DispatchPhase::Capture)
            ->OnScrollWheel(ListenTo(state, &CarouselState::OnWheel))
            ->Refine(style, styleSet)
            ->Child(track);
    if (axis == Axis::Vertical)
        frame->FlexCol();
    else
        frame->FlexRow();
    if (snapshot) {
        snapshot->pendingHasRunway = hasRunway;
        frame->boundsOut = &snapshot->pendingFrame;
        track->boundsOut = &snapshot->pendingViewport;
        VecClear(snapshot->pendingItems);
        El* child = track->first;
        if (hasRunway && child) child = child->next;
        for (; child; child = child->next) {
            if (hasRunway && !child->next) break;
            Bounds slot = {};
            VecAppend(snapshot->pendingItems, slot);
        }
        int logical = 0;
        child = track->first;
        if (hasRunway && child) child = child->next;
        for (; child; child = child->next) {
            if (hasRunway && !child->next) break;
            if (logical < len(snapshot->pendingItems)) {
                child->boundsOut = &snapshot->pendingItems[logical];
            }
            logical++;
        }
        El* ingest = Div(a)->W(0)->H(0)->Absolute();
        ingest->customPaint = &CarouselIngestPaint;
        ingest->customUser = snapshot;
        frame->Child(ingest);
    }
    if (snapshot && snapshot->itemCount == 0) {
        frame->Opacity(0);
    }
    return frame;
}

CarouselItem* CarouselItem::New(Ctx* cx, Str valueId, int valueIndex,
                                Entity<CarouselState> valueState) {
    CarouselItem* value = CarouselPart<CarouselItem>(cx);
    value->id = valueId;
    value->index = valueIndex;
    value->state = valueState;
    return value;
}
CarouselItem* CarouselItem::AccessibilityLabel(Str value) {
    accessibilityLabel = value;
    return this;
}
CarouselItem* CarouselItem::Child(El* child) {
    children.Append(a, child);
    return this;
}
CarouselItem* CarouselItem::Refine(const Style& value, uint32_t fields) {
    StyleApplyFields(&style, value, fields);
    styleSet |= fields;
    return this;
}
El* CarouselItem::IntoEl() {
    CarouselState* snapshot = state.Get(cx);
    int count = snapshot ? snapshot->itemCount : 0;
    Axis axis = snapshot ? snapshot->axis : Axis::Horizontal;
    Str label = accessibilityLabel.s ? accessibilityLabel
                                     : fmt("Slide %d of %d", index + 1, count);
    Point loopOff = snapshot ? snapshot->LoopItemOffset(index) : Point{};
    El* root = Div(a)
                   ->Id(id)
                   ->Role(AccessibilityRole::Group)
                   ->AriaLabel(label)
                   ->AriaPositionInSet(index + 1)
                   ->AriaSizeOfSet(count)
                   ->MinW(0)
                   ->MinH(0)
                   ->FlexNone()
                   ->PaintOffset(loopOff.x, loopOff.y)
                   ->Refine(style, styleSet);
    if (axis == Axis::Vertical)
        root->H(kFill)->PadT(kItemGap);
    else
        root->W(kFill)->PadL(kItemGap);
    CarouselChildren(root, children);
    return root;
}

CarouselControl* CarouselControl::WithSize(UiSize value) {
    size = value;
    return this;
}
CarouselControl* CarouselControl::AccessibilityLabel(Str value) {
    accessibilityLabel = value;
    return this;
}
CarouselControl* CarouselControl::Child(El* child) {
    children.Append(a, child);
    return this;
}
CarouselControl* CarouselControl::Refine(const Style& value, uint32_t fields) {
    StyleApplyFields(&style, value, fields);
    styleSet |= fields;
    return this;
}
El* CarouselControl::IntoEl() {
    CarouselState* snapshot = state.Get(cx);
    bool vertical = snapshot && snapshot->axis == Axis::Vertical;
    bool disabled =
        !snapshot || (next ? !snapshot->HasNext() : !snapshot->HasPrevious());
    IconName icon =
        vertical ? (next ? IconName::ChevronDown : IconName::ChevronUp)
                 : (next ? IconName::ChevronRight : IconName::ChevronLeft);
    Str label = accessibilityLabel.s
                    ? accessibilityLabel
                    : (next ? Tr("Carousel.next") : Tr("Carousel.previous"));
    Button* button = Button::New(cx, next ? StrL("carousel-next")
                                          : StrL("carousel-previous"))
                         ->Outline()
                         ->WithSize(size)
                         ->Rounded(1000.f)
                         ->Disabled(disabled)
                         ->AccessibilityLabel(label)
                         ->Tooltip(label);
    if (len(children) == 0)
        button->Icon(icon);
    else
        for (El* child : children) button->Child(child);
    if (!disabled)
        button->OnClick(next ? ListenTo(state, &CarouselState::OnNext)
                             : ListenTo(state, &CarouselState::OnPrevious));
    return button->IntoEl()->Refine(style, styleSet);
}
CarouselPrevious* CarouselPrevious::New(Ctx* cx, Entity<CarouselState> state) {
    CarouselPrevious* value = CarouselPart<CarouselPrevious>(cx);
    value->state = state;
    value->next = false;
    return value;
}
CarouselNext* CarouselNext::New(Ctx* cx, Entity<CarouselState> state) {
    CarouselNext* value = CarouselPart<CarouselNext>(cx);
    value->state = state;
    value->next = true;
    return value;
}

CarouselPagination* CarouselPagination::New(Ctx* cx) {
    CarouselPagination* value = ArenaNew<CarouselPagination>(cx->a);
    value->a = cx->a;
    return value;
}
CarouselPagination* CarouselPagination::AccessibilityLabel(Str value) {
    accessibilityLabel = value;
    return this;
}
CarouselPagination* CarouselPagination::Child(El* child) {
    children.Append(a, child);
    return this;
}
CarouselPagination* CarouselPagination::Refine(const Style& value,
                                               uint32_t fields) {
    StyleApplyFields(&style, value, fields);
    styleSet |= fields;
    return this;
}
El* CarouselPagination::IntoEl() {
    El* root = Div(a)
                   ->Role(AccessibilityRole::Group)
                   ->AriaLabel(accessibilityLabel)
                   ->FlexRow()
                   ->ItemsCenter()
                   ->JustifyCenter()
                   ->Gap(8)
                   ->Refine(style, styleSet);
    CarouselChildren(root, children);
    return root;
}

CarouselPaginationItem* CarouselPaginationItem::New(
    Ctx* cx, Str valueId, int valueIndex, Entity<CarouselState> valueState) {
    CarouselPaginationItem* value = CarouselPart<CarouselPaginationItem>(cx);
    value->id = valueId;
    value->index = valueIndex;
    value->state = valueState;
    return value;
}
CarouselPaginationItem* CarouselPaginationItem::WithSize(UiSize value) {
    size = value;
    return this;
}
CarouselPaginationItem* CarouselPaginationItem::AccessibilityLabel(Str value) {
    accessibilityLabel = value;
    return this;
}
CarouselPaginationItem* CarouselPaginationItem::Child(El* child) {
    children.Append(a, child);
    return this;
}
CarouselPaginationItem* CarouselPaginationItem::Refine(const Style& value,
                                                       uint32_t fields) {
    StyleApplyFields(&style, value, fields);
    styleSet |= fields;
    return this;
}
El* CarouselPaginationItem::IntoEl() {
    CarouselState* snapshot = state.Get(cx);
    bool disabled = !snapshot || index < 0 || index >= snapshot->itemCount;
    bool selected = snapshot && snapshot->selectedIndex == index;
    Str label = accessibilityLabel.s ? accessibilityLabel
                                     : fmt("Go to slide %d", index + 1);
    Button* button = Button::New(cx, id)
                         ->Compact()
                         ->WithSize(size)
                         ->Selected(selected)
                         ->Disabled(disabled)
                         ->AccessibilityLabel(label);
    for (El* child : children) button->Child(child);
    if (!disabled)
        button->OnClick(
            ListenTo(state, &CarouselState::OnSelect, (intptr_t)index));
    return button->IntoEl()->Refine(style, styleSet);
}

} // namespace component
} // namespace gpui
