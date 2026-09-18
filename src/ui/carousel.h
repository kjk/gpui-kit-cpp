#ifndef GPUI_UI_CAROUSEL_H_
#define GPUI_UI_CAROUSEL_H_
/* Carousel — crates/component/src/carousel. */

#include "ui/button.h"

namespace gpui {
namespace component {

struct CarouselEvent {
    int index = 0;
};

struct CarouselPointerGesture {
    Point startPosition = {};
    Point startOffset = {};
    int startIndex = -1;
    float totalDelta = 0;
    bool axisLocked = false;
    bool active = false;
};

struct CarouselScrollGesture {
    Point startOffset = {};
    int startIndex = -1;
    float totalDelta = 0;
    bool active = false;
};

struct CarouselLoopLayout {
    float cycleExtent = 0;
    float trackGap = 0;
    float runwayExtent = 0;
    bool runwayReady = false;
    bool active = false;
};

struct CarouselState {
    Entity<CarouselState> self = {};
    int itemCount = 0;
    int selectedIndex = -1;
    Axis axis = Axis::Horizontal;
    bool looping = false;
    Point offset = {};
    Point maxOffset = {};
    Bounds viewport = {};
    Bounds frame = {};
    Vec<Bounds> items;
    bool hasViewport = false;
    bool hasFrame = false;
    CarouselPointerGesture pointerGesture = {};
    CarouselScrollGesture scrollGesture = {};
    bool ignoreScrollUntilQuiet = false;
    int scrollSettleEpoch = 0;
    bool suppressPointerClick = false;
    int motionRevision = 0;
    int geometryRevision = 0;
    CarouselLoopLayout loopLayout = {};
    bool geometryHasRunway = false;
    bool loopLayoutRemovalPending = false;
    Point loopMotionTarget = {};
    bool hasLoopMotionTarget = false;
    bool wheelBurstActive = false;
    int wheelBurstEpoch = 0;
    FocusHandle focus = {};
    bool focusRingSuppressed = false;
    OngoingScroll wheelLock = {};
    Vec<Bounds> pendingItems;
    Bounds pendingViewport = {};
    Bounds pendingFrame = {};
    bool pendingHasRunway = false;

    static CarouselState New(int itemCount);
    CarouselState& WithSelectedIndex(int index);
    CarouselState& WithAxis(Axis value);
    CarouselState& WithLooping(bool value = true);
    int ItemCount() const { return itemCount; }
    int SelectedIndex() const { return selectedIndex; }
    Axis GetAxis() const { return axis; }
    bool IsLooping() const { return looping; }
    bool HasPrevious() const;
    bool HasNext() const;
    void SetSelectedIndex(int index, Ctx* cx);
    void SetItemCount(int count, Ctx* cx);
    void SetAxis(Axis value, Ctx* cx);
    void SetLooping(bool value, Ctx* cx);
    bool SelectIndex(int index, Ctx* cx);
    bool SelectPrevious(Ctx* cx);
    bool SelectNext(Ctx* cx);
    bool SelectFirst(Ctx* cx);
    bool SelectLast(Ctx* cx);
    FocusHandle Focus(Ctx* cx);
    void SuppressFocusRing(bool suppressed);
    bool IsFocusRingSuppressed() const { return focusRingSuppressed; }
    bool IsInteracting() const;
    bool HasScrollGesture() const { return scrollGesture.active; }
    bool IsPointerDragLocked() const;
    bool ShouldSuppressPointerClick() const { return suppressPointerClick; }
    int MotionRevision() const { return motionRevision; }
    Size FrameSize() const;
    void SetGeometry(Bounds viewport, Bounds frame, const Bounds* itemBounds,
                     int n, bool hasRunway);
    void SetGeometry(Bounds viewport, const Bounds* itemBounds, int n);
    float LoopRunway() const;
    bool IsLoopLayoutTransitioning() const;
    Point LoopItemOffset(int index) const;
    Point MotionTargetFor(int index) const;
    bool HasMotionTargetFor(int index) const;
    bool SettleLoopMotion(Point rendered, Point* out, Ctx* cx);
    bool HasSnapTarget(int index) const;
    Point SnapTargetFor(int index) const;
    int NearestIndex(Point offset) const;
    bool NormalizeLoopCoordinate();
    bool BeginDrag(Point position, Ctx* cx);
    bool UpdateDrag(Point position, Ctx* cx);
    bool FinishDrag(Ctx* cx);
    bool HandleScrollDelta(Axis axis, float delta, TouchPhase phase, Ctx* cx);
    bool FinishScroll(bool cancelled, Ctx* cx);
    void DeferScrollToAncestor(Ctx* cx);
    bool HandleWheelStep(Axis axis, float delta, Ctx* cx);
    Point Offset() const { return offset; }
    void SetOffset(Point value) { offset = value; }
    void IngestPendingGeometry(Ctx* cx);

    static void OnAction(CarouselState* self, Ctx* cx,
                         const ActionEvent* event);
    static void OnPrevious(CarouselState* self, Ctx* cx,
                           const ClickEvent* event);
    static void OnNext(CarouselState* self, Ctx* cx, const ClickEvent* event);
    static void OnSelect(CarouselState* self, Ctx* cx, const ClickEvent* event,
                         intptr_t index);
    static void OnPointerDown(CarouselState* self, Ctx* cx,
                              const MouseDownEvent* event);
    static void OnPointerMove(CarouselState* self, Ctx* cx,
                              const DragMoveEvent* event);
    static void OnPointerUp(CarouselState* self, Ctx* cx,
                            const MouseUpEvent* event);
    static void OnWheel(CarouselState* self, Ctx* cx,
                        const ScrollWheelEvent* event);
    static void OnScrollSettle(CarouselState* self, Ctx* cx,
                               const TickEvent* event, intptr_t epoch);
    static void OnIgnoredScrollRecovery(CarouselState* self, Ctx* cx,
                                        const TickEvent* event, intptr_t epoch);
    static void OnWheelBurstEnd(CarouselState* self, Ctx* cx,
                                const TickEvent* event, intptr_t epoch);
    static void OnRootMouseDown(CarouselState* self, Ctx* cx,
                                const MouseDownEvent* event);
};

Entity<CarouselState> CarouselStateNew(App* app, int itemCount);
void CarouselInitKeys();

struct Carousel {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Str id = {};
    Entity<CarouselState> state = {};
    Str accessibilityLabel = StrL("Carousel");
    bool focusRingEnabled = true;
    ArenaVec<El*> children;
    Style style = {};
    uint32_t styleSet = 0;

    static Carousel* New(Ctx* cx, Str id, Entity<CarouselState> state);
    Carousel* AccessibilityLabel(Str value);
    Carousel* FocusRing(bool enabled);
    Carousel* Child(El* child);
    Carousel* Refine(const Style& value, uint32_t fields);
    El* IntoEl();
};

struct CarouselContent {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Entity<CarouselState> state = {};
    ArenaVec<El*> children;
    Style style = {};
    uint32_t styleSet = 0;
    Style trackStyle = {};
    uint32_t trackStyleSet = 0;

    static CarouselContent* New(Ctx* cx, Entity<CarouselState> state);
    CarouselContent* Child(El* child);
    CarouselContent* Refine(const Style& value, uint32_t fields);
    CarouselContent* TrackStyle(const Style& value, uint32_t fields);
    El* IntoEl();
};

struct CarouselItem {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Str id = {};
    int index = 0;
    Entity<CarouselState> state = {};
    Str accessibilityLabel = {};
    ArenaVec<El*> children;
    Style style = {};
    uint32_t styleSet = 0;

    static CarouselItem* New(Ctx* cx, Str id, int index,
                             Entity<CarouselState> state);
    CarouselItem* AccessibilityLabel(Str value);
    CarouselItem* Child(El* child);
    CarouselItem* Refine(const Style& value, uint32_t fields);
    El* IntoEl();
};

struct CarouselControl {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Entity<CarouselState> state = {};
    UiSize size = UiSize::Medium;
    Str accessibilityLabel = {};
    ArenaVec<El*> children;
    Style style = {};
    uint32_t styleSet = 0;
    bool next = false;

    CarouselControl* WithSize(UiSize value);
    CarouselControl* AccessibilityLabel(Str value);
    CarouselControl* Child(El* child);
    CarouselControl* Refine(const Style& value, uint32_t fields);
    El* IntoEl();
};

struct CarouselPrevious : CarouselControl {
    static CarouselPrevious* New(Ctx* cx, Entity<CarouselState> state);
};

struct CarouselNext : CarouselControl {
    static CarouselNext* New(Ctx* cx, Entity<CarouselState> state);
};

struct CarouselPagination {
    Arena* a = nullptr;
    Str accessibilityLabel = StrL("Carousel pagination");
    ArenaVec<El*> children;
    Style style = {};
    uint32_t styleSet = 0;

    static CarouselPagination* New(Ctx* cx);
    CarouselPagination* AccessibilityLabel(Str value);
    CarouselPagination* Child(El* child);
    CarouselPagination* Refine(const Style& value, uint32_t fields);
    El* IntoEl();
};

struct CarouselPaginationItem {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Str id = {};
    int index = 0;
    Entity<CarouselState> state = {};
    UiSize size = UiSize::XSmall;
    Str accessibilityLabel = {};
    ArenaVec<El*> children;
    Style style = {};
    uint32_t styleSet = 0;

    static CarouselPaginationItem* New(Ctx* cx, Str id, int index,
                                       Entity<CarouselState> state);
    CarouselPaginationItem* WithSize(UiSize value);
    CarouselPaginationItem* AccessibilityLabel(Str value);
    CarouselPaginationItem* Child(El* child);
    CarouselPaginationItem* Refine(const Style& value, uint32_t fields);
    El* IntoEl();
};

} // namespace component

template <>
struct EventEmitter<component::CarouselState, component::CarouselEvent> {};

} // namespace gpui
#endif // GPUI_UI_CAROUSEL_H_
