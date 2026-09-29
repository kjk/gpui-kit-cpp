#ifndef GPUI_UI_NOTIFICATION_H_
#define GPUI_UI_NOTIFICATION_H_
/* Themed notification — crates/ui/src/notification.rs */

#include "ui/sizing.h"

namespace gpui {

namespace component {

// The old port called the source's NotificationType `NotificationKind`.
// Retain that spelling for callers while keeping NotificationType primary.
using NotificationKind = NotificationType;

bool NotificationDeliveryIncludesInApp(NotificationDelivery d);
bool NotificationDeliveryIncludesSystem(NotificationDelivery d);

// DEFAULT_NOTIFICATION_WIDTH.
const float kNotificationWidth = 382;
// NotificationSettings::max_items.
const int kNotificationMaxItems = 10;
// How often the list is advanced. Rust spawns a task that ticks at this rate.
const int kNotificationTickMs = 50;

// std::any::TypeId without RTTI: every template specialization owns one
// writable byte whose address is unique for the life of the process. It is
// identity only and is never dereferenced through this value.
using NotificationTypeId = uintptr_t;
template <typename T>
inline NotificationTypeId NotificationTypeOf() {
    static uint8_t tag = 0;
    return (NotificationTypeId)&tag;
}

// The source-shaped notification value. Its strings are borrowed while the
// builder is in the caller's hand and cloned when NotificationPush retains
// it. Custom content/action are entities so they can be rendered afresh on
// every frame rather than retaining frame-arena El pointers.
struct Notification {
    int id = 0;
    // NotificationId::Id(type) / IdAndElementId(type, key). A zero type uses
    // the older explicit integer id above; generated ids have neither.
    NotificationTypeId identityType = 0;
    uint32_t identityKey = 0;
    bool identityHasKey = false;
    bool hasType = false;
    NotificationType type = NotificationType::Info;
    Str title = {};
    Str message = {};
    bool hasIcon = false;
    IconName icon = IconName::None;
    bool hasPlacement = false;
    Anchor placement = Anchor::TopRight;
    EntityId action = {};
    EntityId content = {};
    Listener onClick = {};
    Listener onClose = {};
    // Notification::delivery is an Option: unset takes the list's own
    // NotificationSettings::delivery, which is what `hasDelivery` says.
    bool hasDelivery = false;
    NotificationDelivery delivery = NotificationDelivery::InApp;
    bool autohide = true;
    Style style = {};
    uint32_t styleSet = 0;
    // The most recent laid-out card height, retained for ToastStack geometry.
    Bounds measured = {};
    bool ownsText = false;

    static Notification New();
    static Notification Info(Str message);
    static Notification Success(Str message);
    static Notification Warning(Str message);
    static Notification Error(Str message);
    Notification& Message(Str value);
    Notification& Title(Str value);
    Notification& WithType(NotificationType value);
    Notification& Icon(IconName value);
    Notification& Placement(Anchor value);
    Notification& Delivery(NotificationDelivery value);
    Notification& System();
    Notification& InAppAndSystem();
    Notification& Autohide(bool value = true);
    Notification& Action(EntityId value);
    Notification& Content(EntityId value);
    Notification& OnClick(Listener value);
    Notification& OnClose(Listener value);
    Notification& Refine(const Style& value, uint32_t fields);

    template <typename T>
    Notification& Id() {
        id = 0;
        identityType = NotificationTypeOf<T>();
        identityKey = 0;
        identityHasKey = false;
        return *this;
    }
    template <typename T>
    Notification& Id1(uint32_t key) {
        id = 0;
        identityType = NotificationTypeOf<T>();
        identityKey = key;
        identityHasKey = true;
        return *this;
    }
    template <typename T>
    Notification& Id1(Str key) {
        return Id1<T>((uint32_t)HashClickId(key));
    }
    template <typename T>
    Notification& Action(Entity<T> value) {
        return Action(value.id);
    }
    template <typename T>
    Notification& Content(Entity<T> value) {
        return Content(value.id);
    }
};

using NotificationItem = Notification;

bool NotificationIdentitySame(const Notification& a, const Notification& b);

// NotificationList: the notifications on screen, when each of them goes away,
// and which corner they stack in. The lifecycle is the toast stack's, so a
// notification is a toast that happens to look like one.
struct NotificationListState {
    ToastStackState stack;
    Vec<Notification> items;
    int nextId = 1;
    // Normal window-owned lists read the active Theme every time. These
    // mirrors keep a list usable in dependency-free state tests and preserve
    // compatibility for code that constructed the state directly.
    bool useThemeSettings = false;
    Anchor placement = Anchor::TopRight;
    Edges margins = Edges::New(16.f, 16.f, 50.f, 16.f);
    float width = kNotificationWidth;
    int maxItems = kNotificationMaxItems;
    // First-frame fallback until BoundsOut has measured the actual card.
    float itemH = 76;
    // How far in the transition each card is, so the tick can be turned into
    // an offset. Written by NotificationAdvance.
    double lastTickAt = 0;
    // NotificationSettings::delivery: where a notification with none of its
    // own goes.
    NotificationDelivery delivery = NotificationDelivery::InApp;
    // Whether the lifecycle clock is running, and the window timer that is
    // it. Rust spawns a detached task from `push` that ends itself once the
    // manager is empty; a window interval is the same clock, armed and
    // cancelled at the same two moments. With nothing mounted there is
    // nothing to advance, so an idle window arms no timer.
    bool isAdvancing = false;
    int advanceTimer = 0;
    // The window the clock is armed on, so it can be cancelled from a tick
    // that has one and from the destructor, which has neither.
    Window* advanceWin = nullptr;
    bool stackHovered[8] = {};
    bool stackFocused[8] = {};
    FocusHandle stackFocus[8] = {};
    // cx.entity().downgrade(): what a system notification's response is
    // dispatched back to, since the response arrives from the platform with
    // no view in hand. Stamped by NotificationList::IntoEl and by whoever
    // makes the entity.
    EntityId self = {};

    ~NotificationListState();
    bool IsExpanded() const;

    static void OnCloseClick(NotificationListState* self, Ctx* cx,
                             const ClickEvent* ev, intptr_t id);
    static void OnItemClick(NotificationListState* self, Ctx* cx,
                            const ClickEvent* ev, intptr_t id);
    static void OnHover(NotificationListState* self, Ctx* cx,
                        const HoverEvent* ev, intptr_t anchor);
    static void OnTick(NotificationListState* self, Ctx* cx,
                       const TickEvent* ev);
    // The user clicked the system notification for `id`: the in-app
    // counterpart closes and on_click fires, on the main thread and after the
    // platform's own event is over.
    static void OnSystemResponse(NotificationListState* self, Ctx* cx,
                                 const ClickEvent* ev, intptr_t id);
};

// push(): add a notification and answer its id. `timeoutMs` of 0 is Rust's
// `autohide(false)` — it stays until it is dismissed. `maxItems` filters the
// rendered set without evicting mounted toasts, and a push with an id already
// in the list replaces that one rather than stacking a second copy, which is
// what Rust's NotificationId does.
//
// `cx` is what the system half needs — the window a response is dispatched
// back to — and a null one is the in-app half on its own, which is what a
// test pushes.
int NotificationPush(NotificationListState* s, Ctx* cx, Notification item,
                     int timeoutMs = -1);
// dismiss(): start the one with this id on its way out, and retract its
// system counterpart. Rust does the second unconditionally: a system-only
// notification has no toast to dismiss and must still be taken back.
void NotificationDismiss(NotificationListState* s, Ctx* cx, int id);
// close_by_type: the broad form matches both Id(type) and every Id1(type,
// key); the keyed form matches exactly one Id1 pair.
void NotificationDismissByType(NotificationListState* s, Ctx* cx,
                               NotificationTypeId type);
void NotificationDismissByTypeKey(NotificationListState* s, Ctx* cx,
                                  NotificationTypeId type, uint32_t key);
// clear().
void NotificationClear(NotificationListState* s, Ctx* cx);
// start_advancing(): tick the toast lifecycle until the last notification is
// unmounted. It advances the transition phases and samples the pause input
// (stack expansion) that reaches the list through no event. Idempotent, and
// `push` is the only thing that calls it.
void NotificationStartAdvancing(NotificationListState* s, Ctx* cx);
// The other end of it: the clock stops itself at an instant when nothing is
// mounted, so a mounted toast always has a running clock.
void NotificationStopAdvancing(NotificationListState* s);
// needs_clock: whether a mounted notification is in transition or counting
// down to autohide. A paused countdown still needs its last sample kept
// current. Persistent notifications at rest arm no timer.
bool NotificationNeedsClock(const NotificationListState* s);
// advance(): move every notification on by `deltaMs`, dropping the ones that
// finished leaving. Answers whether anything changed.
bool NotificationAdvance(NotificationListState* s, int deltaMs);
bool NotificationAdvance(NotificationListState* s, Ctx* cx, int deltaMs);
// Where the item with this id is, or -1.
int NotificationIndexOf(const NotificationListState* s, int id);

// ─── the system half ─────────────────────────────────────────────────────
//
// SystemNotificationRegistry: what a posted system notification needs when
// its response comes back — which window it was posted from, which list to
// close the toast in, and what to fire. It is app-global because the
// response is: the platform hands back a tag and nothing else.

// MAX_SYSTEM_NOTIFICATION_ENTRIES. Past this the stalest are pruned, so a
// long-running application does not accumulate one entry per notification it
// ever posted.
const int kNotificationSystemMax = 100;

struct NotificationSystemEntry {
    int id = 0;
    NotificationTypeId identityType = 0;
    uint32_t identityKey = 0;
    bool identityHasKey = false;
    EntityId list = {};
    Window* win = nullptr;
    Listener onClick = {};
};

// NotificationId::system_tag: SYSTEM_TAG_PREFIX and the notification's id,
// written into `buf`. Namespaced so a response to a notification the
// application posted itself is not ours to answer. Temp-arena backed.
TempStr NotificationSystemTagTemp(int id);
// The id inside one of those tags. False for anybody else's.
bool NotificationTagId(Str tag, int* outId);
// notification::init: install the app-global response handler. Idempotent,
// and called by the first system push.
void NotificationInitSystem(App* app);
// The registry, which the push fills in and a response empties.
void NotificationSystemInsert(const NotificationSystemEntry& e);
// Retract the system notification for `id`, but only if it was posted from
// `win`: another window that pushed the same id owns the tag now, and this
// window's dismiss must not take back its notification.
void NotificationSystemDismiss(int id, Window* win);
void NotificationSystemDismissByType(NotificationTypeId type, Window* win);
void NotificationSystemDismissByTypeKey(NotificationTypeId type, uint32_t key,
                                        Window* win);
// Retract every system notification posted from `win`.
void NotificationSystemDismissAll(Window* win);
const NotificationSystemEntry* NotificationSystemFind(int id, Window* win);
int NotificationSystemCount();
int NotificationSystemCount(const App* app);
// What the platform calls with the tag of the notification the user clicked.
void NotificationSystemResponse(Str tag);

// The floating stack, drawn over the window in the corner its placement
// names — what Root does with the notification list in Rust.
struct NotificationList {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Entity<NotificationListState> state = {};

    static NotificationList* New(Ctx* cx, Entity<NotificationListState> state);
    El* IntoEl();
};

} // namespace component
} // namespace gpui
#endif // GPUI_UI_NOTIFICATION_H_
