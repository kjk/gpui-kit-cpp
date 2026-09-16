/* Entity store, contexts and listeners — GPUI's App/Context in C++. */

#include "gpui/gpui.h"
#include "gpui/platform.h"
#include "gpui/keymap.h"
#include "sys/executor.h"

namespace gpui {

void* AppGlobalGetRaw(const App* app, const void* key) {
    if (!app || !key) {
        return nullptr;
    }
    for (int i = 0; i < app->globals.len; i++) {
        const AppGlobalSlot& slot = app->globals[i];
        if (slot.key == key) {
            return slot.value;
        }
    }
    return nullptr;
}

void AppGlobalSetRaw(App* app, const void* key, void* value,
                     AppGlobalFreeFn freeValue) {
    if (!app || !key) {
        if (value && freeValue) {
            freeValue(value);
        }
        return;
    }
    for (int i = 0; i < app->globals.len; i++) {
        AppGlobalSlot& slot = app->globals[i];
        if (slot.key != key) {
            continue;
        }
        if (slot.value && slot.freeValue) {
            slot.freeValue(slot.value);
        }
        slot.value = value;
        slot.freeValue = freeValue;
        return;
    }
    VecAppend(app->globals, AppGlobalSlot{key, value, freeValue});
}

bool AppGlobalRemoveRaw(App* app, const void* key) {
    if (!app || !key) {
        return false;
    }
    for (int i = 0; i < app->globals.len; i++) {
        AppGlobalSlot& slot = app->globals[i];
        if (slot.key != key) {
            continue;
        }
        if (slot.value && slot.freeValue) {
            slot.freeValue(slot.value);
        }
        for (int j = i; j < app->globals.len - 1; j++) {
            app->globals[j] = app->globals[j + 1];
        }
        app->globals.len--;
        if (app->globals.els) {
            app->globals[app->globals.len] = {};
        }
        return true;
    }
    return false;
}

void AppGlobalClear(App* app) {
    if (!app) {
        return;
    }
    for (int i = app->globals.len - 1; i >= 0; i--) {
        AppGlobalSlot& slot = app->globals[i];
        if (slot.value && slot.freeValue) {
            slot.freeValue(slot.value);
        }
    }
    VecReset(app->globals);
}

// Slot 0 is never handed out so a zeroed EntityId reads as null.
EntityId EntityNewRaw(App* app, void* ptr, RenderFn render, DropFn drop) {
    EntityId id;
    if (!app || !ptr) {
        return id;
    }
    int32_t ix;
    if (app->freeSlots.len > 0) {
        ix = app->freeSlots[app->freeSlots.len - 1];
        app->freeSlots.len--;
    } else {
        EntitySlot fresh = {};
        VecAppend(app->entities, fresh);
        ix = (int32_t)(app->entities.len - 1);
    }
    EntitySlot& s = app->entities[ix];
    s.ptr = ptr;
    s.render = render;
    s.drop = drop;
    if (s.gen == 0) {
        s.gen = 1;
    }
    id.index = ix;
    id.gen = s.gen;
    return id;
}

void* EntityGet(App* app, EntityId id) {
    if (!app || !id.IsValid() || id.index >= app->entities.len) {
        return nullptr;
    }
    EntitySlot& s = app->entities[id.index];
    if (s.gen != id.gen || !s.ptr) {
        return nullptr;
    }
    return s.ptr;
}

void EntityDrop(App* app, EntityId id) {
    if (!app || !id.IsValid() || id.index >= app->entities.len) {
        return;
    }
    EntitySlot& s = app->entities[id.index];
    if (s.gen != id.gen || !s.ptr) {
        return;
    }
    if (s.drop) {
        s.drop(s.ptr);
    }
    s.ptr = nullptr;
    s.render = nullptr;
    s.drop = nullptr;
    // Bump the generation so every outstanding handle goes stale.
    s.gen++;
    if (s.gen == 0) {
        s.gen = 1;
    }
    VecAppend(app->freeSlots, id.index);
}

void EntityDropAll(App* app) {
    if (!app) {
        return;
    }
    for (int i = 0; i < app->entities.len; i++) {
        EntitySlot& s = app->entities[i];
        if (s.ptr && s.drop) {
            s.drop(s.ptr);
        }
        s.ptr = nullptr;
        s.render = nullptr;
        s.drop = nullptr;
    }
    VecReset(app->entities);
    VecReset(app->freeSlots);
}

El* EntityRender(App* app, Window* win, Arena* a, EntityId id) {
    if (!app || !id.IsValid() || id.index >= app->entities.len) {
        return nullptr;
    }
    EntitySlot& s = app->entities[id.index];
    if (s.gen != id.gen || !s.ptr || !s.render) {
        return nullptr;
    }
    Ctx cx;
    cx.app = app;
    cx.win = win;
    cx.a = a;
    cx.self = id;
    // What the window has on it, for Notify to aim at. GPUI records the same
    // set while it renders, as `Window::dirty_views`.
    if (win) {
        VecAppend(win->rendered, id);
    }
    return s.render(s.ptr, &cx);
}

static void InvalidateForNotify(Window* win) {
    if (win && !win->active && win->animFrame) {
        // GPUI's inactive-frame throttle applies when a next-frame callback
        // is pending. Keep the notification for that already-scheduled frame
        // instead of turning an async update into a second frame between
        // animation deadlines. Direct input still uses AppInvalidate, so an
        // inactive window under the pointer continues to scroll immediately.
        win->invalidations++;
        return;
    }
    AppInvalidate(win);
}

void NotifyApp(App* app) {
    if (!app) {
        return;
    }
    for (int i = 0; i < app->windows.len; i++) {
        InvalidateForNotify(app->windows[i]);
    }
}

// Every observer of `observed`, oldest first, over a copy of the handles:
// a handler may observe or unobserve, and the list moving under the walk
// would otherwise skip or repeat one. The same shape as EntityEmit's.
static void RunObservers(App* app, Window* win, EntityId observed) {
    if (!app || app->observers.len <= 0) {
        return;
    }
    Subscription ids[64];
    int nIds = 0;
    for (int i = 0; i < app->observers.len && nIds < 64; i++) {
        if (app->observers[i].emitter == observed) {
            ids[nIds++].id = app->observers[i].id;
        }
    }
    for (int k = 0; k < nIds; k++) {
        for (int i = 0; i < app->observers.len; i++) {
            if (app->observers[i].id != ids[k].id) {
                continue;
            }
            Listener l = app->observers[i].handler;
            ListenerCall(app, win, l, &observed);
            break;
        }
    }
}

void NotifyEntity(App* app, EntityId id, Window* from) {
    if (!app) {
        return;
    }
    RunObservers(app, from, id);
    // The windows that have this entity on them. GPUI marks a window dirty
    // only when the entity that notified is one of the views it rendered.
    bool any = false;
    if (id.IsValid()) {
        for (int i = 0; i < app->windows.len; i++) {
            Window* w = app->windows[i];
            for (int j = 0; j < w->rendered.len; j++) {
                if (w->rendered[j] == id) {
                    InvalidateForNotify(w);
                    any = true;
                    break;
                }
            }
        }
    }
    if (any) {
        return;
    }
    // Nothing has rendered it: a state entity that is not a view, or a view
    // whose first frame has not been built yet. Both still have to reach the
    // screen, so this is where the old shotgun stays.
    if (from) {
        InvalidateForNotify(from);
        return;
    }
    NotifyApp(app);
}

void Notify(Ctx* cx) {
    if (!cx) {
        return;
    }
    NotifyEntity(cx->app, cx->self, cx->win);
}

void ListenerCall(App* app, Window* win, const Listener& l, const void* ev) {
    if (!l.IsValid()) {
        return;
    }
    void* self = EntityGet(app, l.view);
    if (!self) {
        // The view went away between paint and dispatch; GPUI drops it too.
        return;
    }
    Ctx cx;
    cx.app = app;
    cx.win = win;
    cx.a = win ? win->frameArena : nullptr;
    cx.self = l.view;
    if (l.HasArg()) {
        ((ListenerArgFn)l.Fn())(self, &cx, ev, l.arg);
    } else {
        ((ListenerFn)l.Fn())(self, &cx, ev);
    }
}

void WindowOnKey(Window* win, Listener l) {
    if (win) {
        win->onKey = l;
    }
}

void WindowOnScrollWheel(Window* win, Listener l) {
    if (win) {
        win->onScrollWheel = l;
    }
}

WinSize WindowSize(Window* win) {
    WinSize ws = {};
    if (!win) {
        return ws;
    }
    ws.dipW = win->paint.viewW;
    ws.dipH = win->paint.viewH;
    ws.pxW = DipToPx(&win->paint, ws.dipW);
    ws.pxH = DipToPx(&win->paint, ws.dipH);
    return ws;
}

const DragPayload* WindowActiveDrag(Ctx* cx) {
    if (!cx || !cx->win || !cx->win->activeDrag.IsValid()) {
        return nullptr;
    }
    return &cx->win->activeDrag;
}

bool WindowIsActive(Ctx* cx) {
    return (cx && cx->win) ? cx->win->active : true;
}

bool WindowIsTouchPress(Ctx* cx) {
    return cx && cx->win && cx->win->touchPress;
}

void WindowSetActive(Window* win, bool active) {
    if (!win || win->active == active) {
        return;
    }
    win->active = active;
    if (!active) {
        // Window::deactivate. Everything the keyboard was part-way through is
        // dropped, because its other half is going somewhere else: the rest
        // of a multi-stroke binding will be typed into whatever took the
        // focus, the character a taken keystroke was going to arrive as never
        // will, and the key held down over a focused element gets no release
        // here — so none of the three may be waiting when the window comes
        // back.
        KeymapClearPending();
        win->eatChar = false;
        win->keyPressPending = false;
    }
    win->lastDrawTime = 0;
    AppInvalidate(win);
}

int WindowDragOverId(Ctx* cx) {
    return (cx && cx->win) ? cx->win->dragOverId : 0;
}

Point WindowDragOffset(Ctx* cx) {
    if (!cx || !cx->win) {
        return {};
    }
    return {cx->win->dragOffX, cx->win->dragOffY};
}

void WindowToggleInspector(Window* win) {
    if (!win) {
        return;
    }
    win->inspector.on = !win->inspector.on;
    // Toggling it on starts in picking mode, which is what the magnifier is
    // already pressed for in Rust.
    win->inspector.picking = win->inspector.on;
    if (!win->inspector.on) {
        win->inspector.hasPick = false;
    }
    AppInvalidate(win);
}

void WindowInspectorPick(Window* win, bool picking) {
    if (!win) {
        return;
    }
    win->inspector.picking = picking;
    AppInvalidate(win);
}

const InspectorState* WindowInspector(Ctx* cx) {
    return (cx && cx->win) ? &cx->win->inspector : nullptr;
}

void WindowOnUnhandledClick(Window* win, Listener l) {
    if (win) {
        win->onClick = l;
    }
}

void WindowOnMouseDown(Window* win, Listener l) {
    if (win) {
        win->onMouseDown = l;
    }
}

void WindowOnMouseUp(Window* win, Listener l) {
    if (win) {
        win->onMouseUp = l;
    }
}

void WindowOnMouseMove(Window* win, Listener l) {
    if (win) {
        win->onMouseMove = l;
    }
}

void WindowOnMouseExit(Window* win, Listener l) {
    if (win) {
        win->onMouseExit = l;
    }
}

// What WindowPost hands the main-thread queue. One allocation per post,
// freed by the run below whether the listener got to fire or not.
struct PostedTask {
    App* app = nullptr;
    Window* win = nullptr;
    Listener l = {};
    const void* ev = nullptr;
};

// The window is looked up by pointer rather than trusted: a post made from a
// worker can outlive the window it was made against, and there is no
// generation on a Window the way there is on an entity.
static bool WindowIsLive(App* app, Window* win) {
    if (!app || !win) {
        return false;
    }
    for (int i = 0; i < app->windows.len; i++) {
        if (app->windows[i] == win) {
            return true;
        }
    }
    return false;
}

static void RunPostedTask(PostedTask* t) {
    if (WindowIsLive(t->app, t->win)) {
        // ListenerCall drops it again if the entity itself is gone.
        ListenerCall(t->app, t->win, t->l, t->ev);
    }
    Free(nullptr, t);
}

void WindowPost(Window* win, Listener l, const void* ev) {
    if (!win || !win->app || !l.IsValid()) {
        return;
    }
    auto* t = AllocArray<PostedTask>(1);
    if (!t) {
        return;
    }
    t->app = win->app;
    t->win = win;
    t->l = l;
    t->ev = ev;
    ExecPost(MkFunc0(RunPostedTask, t));
}

static int WindowArmTimer(Window* win, int ms, Listener l, bool repeat) {
    if (!win || ms <= 0 || !l.IsValid()) {
        return 0;
    }
    TimerSub t;
    t.id = win->nextTimerId++;
    t.ms = ms;
    t.dueAt = TimeNow() + (double)ms / 1000.0;
    t.repeat = repeat;
    t.l = l;
    VecAppend(win->timers, t);
    PlatSetTimer(win, WindowTimerMs(win));
    return t.id;
}

int WindowSetInterval(Window* win, int ms, Listener l) {
    return WindowArmTimer(win, ms, l, true);
}

int WindowSetTimeout(Window* win, int ms, Listener l) {
    return WindowArmTimer(win, ms, l, false);
}

void WindowCancelTimer(Window* win, int id) {
    if (!win || id <= 0) {
        return;
    }
    for (int i = 0; i < win->timers.len; i++) {
        if (win->timers[i].id != id) {
            continue;
        }
        for (int j = i + 1; j < win->timers.len; j++) {
            win->timers[j - 1] = win->timers[j];
        }
        win->timers.len--;
        break;
    }
    PlatSetTimer(win, WindowTimerMs(win));
}

void* WindowKeyedState(Window* win, uint32_t key, void* fresh, DropFn drop) {
    if (!win || !fresh || !drop) {
        if (fresh && drop) {
            drop(fresh);
        }
        return nullptr;
    }
    for (int i = 0; i < win->keyed.len; i++) {
        if (win->keyed[i].key == key) {
            drop(fresh);
            return win->keyed[i].ptr;
        }
    }
    KeyedSlot s = {};
    s.key = key;
    s.ptr = fresh;
    s.drop = drop;
    if (!VecAppend(win->keyed, s)) {
        drop(fresh);
        return nullptr;
    }
    return fresh;
}

// window.use_keyed_state, when the state has to be an entity so timers and
// listeners can be bound to it. `fresh` is a new T the caller allocated; it is
// adopted on the first call for this key and deleted on every later one, which
// is how a C++ caller says Rust's `|_, cx| HoverCardState::new(..)` without a
// closure to defer it.
EntityId WindowKeyedEntity(Window* win, App* app, uint32_t key, void* fresh,
                           DropFn drop) {
    if (!win || !app) {
        if (fresh && drop) {
            drop(fresh);
        }
        return {};
    }
    for (int i = 0; i < win->keyed.len; i++) {
        if (win->keyed[i].key != key) {
            continue;
        }
        // Still ours only while the entity is alive; a recycled slot starts
        // over, the way a dropped keyed state does.
        if (EntityGet(app, win->keyed[i].entity)) {
            if (fresh && drop) {
                drop(fresh);
            }
            return win->keyed[i].entity;
        }
        win->keyed[i].entity = EntityNewRaw(app, fresh, nullptr, drop);
        return win->keyed[i].entity;
    }
    KeyedSlot s = {};
    s.key = key;
    s.entity = EntityNewRaw(app, fresh, nullptr, drop);
    VecAppend(win->keyed, s);
    return s.entity;
}

void* WindowMotionState(Window* win, uint32_t key, int size) {
    if (!win || size <= 0) {
        return nullptr;
    }
    for (int i = 0; i < win->motionSlots.len; i++) {
        if (win->motionSlots[i].key == key) {
            win->motionSlots[i].frame = win->frameSeq;
            return win->motionSlots[i].ptr;
        }
    }
    MotionSlotRec s = {};
    s.key = key;
    s.frame = win->frameSeq;
    s.ptr = AllocZero(1, size);
    VecAppend(win->motionSlots, s);
    return s.ptr;
}

void WindowMotionSweep(Window* win) {
    if (!win) {
        return;
    }
    int keep = 0;
    for (int i = 0; i < win->motionSlots.len; i++) {
        if (win->motionSlots[i].frame == win->frameSeq) {
            win->motionSlots[keep++] = win->motionSlots[i];
            continue;
        }
        Free(nullptr, win->motionSlots[i].ptr);
    }
    win->motionSlots.len = keep;
}

void WindowMotionFree(Window* win) {
    if (!win) {
        return;
    }
    for (int i = 0; i < win->motionSlots.len; i++) {
        Free(nullptr, win->motionSlots[i].ptr);
    }
    VecReset(win->motionSlots);
}

void WindowKeyedFree(Window* win) {
    if (!win) {
        return;
    }
    for (int i = 0; i < win->keyed.len; i++) {
        if (win->keyed[i].ptr) {
            if (win->keyed[i].drop) {
                win->keyed[i].drop(win->keyed[i].ptr);
            } else {
                // Entity-valued slots only carry their handle in `entity`;
                // pointer slots always have a drop function.
                Free(nullptr, win->keyed[i].ptr);
            }
        }
    }
    VecReset(win->keyed);
}

// ─── EventEmitter ─────────────────────────────────────────────────────────

// Vec is POD storage with no removal of its own; a subscription list is
// short and ordered, so the tail slides down.
static void SubRemoveAt(App* app, int ix) {
    for (int k = ix; k + 1 < app->subs.len; k++) {
        app->subs[k] = app->subs[k + 1];
    }
    app->subs.len--;
}

static void ObserverRemoveAt(App* app, int ix) {
    for (int k = ix; k + 1 < app->observers.len; k++) {
        app->observers[k] = app->observers[k + 1];
    }
    app->observers.len--;
}

Subscription EntityObserveRaw(App* app, EntityId observed, Listener handler) {
    Subscription sub;
    if (!app || !observed.IsValid() || !handler.IsValid()) {
        return sub;
    }
    EntitySub s;
    s.id = app->nextSubId++;
    s.emitter = observed;
    s.handler = handler;
    VecAppend(app->observers, s);
    sub.id = s.id;
    return sub;
}

void EntityUnobserve(App* app, Subscription sub) {
    if (!app || !sub.IsValid()) {
        return;
    }
    for (int i = 0; i < app->observers.len; i++) {
        if (app->observers[i].id == sub.id) {
            ObserverRemoveAt(app, i);
            return;
        }
    }
}

// Stale ones swept first: an observer whose entity has gone is not one.
int EntityObserverCount(App* app, EntityId observed) {
    if (!app) {
        return 0;
    }
    int n = 0;
    for (int i = app->observers.len - 1; i >= 0; i--) {
        if (!EntityGet(app, app->observers[i].handler.view)) {
            ObserverRemoveAt(app, i);
            continue;
        }
        if (app->observers[i].emitter == observed) {
            n++;
        }
    }
    return n;
}

Subscription EntitySubscribeRaw(App* app, EntityId emitter,
                                const void* eventType, Listener handler) {
    Subscription sub;
    if (!app || !emitter.IsValid() || !eventType || !handler.IsValid()) {
        return sub;
    }
    EntitySub s;
    s.id = app->nextSubId++;
    s.emitter = emitter;
    s.eventType = eventType;
    s.handler = handler;
    VecAppend(app->subs, s);
    sub.id = s.id;
    return sub;
}

void EntityUnsubscribe(App* app, Subscription sub) {
    if (!app || !sub.IsValid()) {
        return;
    }
    for (int i = 0; i < app->subs.len; i++) {
        if (app->subs[i].id == sub.id) {
            SubRemoveAt(app, i);
            return;
        }
    }
}

// A subscription is dead once either end of it is: Rust drops the whole list
// with the emitter, and a Subscription with the subscriber.
static void SweepSubs(App* app) {
    for (int i = app->subs.len - 1; i >= 0; i--) {
        const EntitySub& s = app->subs[i];
        if (!EntityGet(app, s.emitter) || !EntityGet(app, s.handler.view)) {
            SubRemoveAt(app, i);
        }
    }
}

void EntityEmitRaw(App* app, Window* win, EntityId emitter,
                   const void* eventType, const void* ev) {
    if (!app || !emitter.IsValid() || !eventType) {
        return;
    }
    SweepSubs(app);
    // Oldest first, and over a copy of the handles: a handler is allowed to
    // subscribe or unsubscribe, and the list moving under the walk would
    // otherwise skip or repeat one.
    int n = app->subs.len;
    if (n <= 0) {
        return;
    }
    Subscription ids[64];
    int nIds = 0;
    for (int i = 0; i < n && nIds < 64; i++) {
        if (app->subs[i].emitter == emitter &&
            app->subs[i].eventType == eventType) {
            ids[nIds++].id = app->subs[i].id;
        }
    }
    for (int k = 0; k < nIds; k++) {
        for (int i = 0; i < app->subs.len; i++) {
            if (app->subs[i].id != ids[k].id) {
                continue;
            }
            Listener l = app->subs[i].handler;
            ListenerCall(app, win, l, ev);
            break;
        }
    }
}

int EntitySubscriberCount(App* app, EntityId emitter) {
    if (!app || !emitter.IsValid()) {
        return 0;
    }
    SweepSubs(app);
    int n = 0;
    for (int i = 0; i < app->subs.len; i++) {
        if (app->subs[i].emitter == emitter) {
            n++;
        }
    }
    return n;
}

} // namespace gpui
