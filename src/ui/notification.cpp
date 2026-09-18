#include "ui/notification.h"
#include "base/motion.h"
#include "sys/notify.h"
#include "ui/alert.h"
#include "ui/title_bar.h"

namespace gpui {

namespace component {

Notification Notification::New() {
    return {};
}

Notification Notification::Info(Str value) {
    Notification n;
    n.Message(value).WithType(NotificationType::Info);
    return n;
}

Notification Notification::Success(Str value) {
    Notification n;
    n.Message(value).WithType(NotificationType::Success);
    return n;
}

Notification Notification::Warning(Str value) {
    Notification n;
    n.Message(value).WithType(NotificationType::Warning);
    return n;
}

Notification Notification::Error(Str value) {
    Notification n;
    n.Message(value).WithType(NotificationType::Error);
    return n;
}

Notification& Notification::Message(Str value) {
    message = value;
    return *this;
}
Notification& Notification::Title(Str value) {
    title = value;
    return *this;
}
Notification& Notification::WithType(NotificationType value) {
    hasType = true;
    type = value;
    return *this;
}
Notification& Notification::Icon(IconName value) {
    hasIcon = true;
    icon = value;
    return *this;
}
Notification& Notification::Placement(Anchor value) {
    hasPlacement = true;
    placement = value;
    return *this;
}
Notification& Notification::Delivery(NotificationDelivery value) {
    hasDelivery = true;
    delivery = value;
    return *this;
}
Notification& Notification::System() {
    return Delivery(NotificationDelivery::System);
}
Notification& Notification::InAppAndSystem() {
    return Delivery(NotificationDelivery::InAppAndSystem);
}
Notification& Notification::Autohide(bool value) {
    autohide = value;
    return *this;
}
Notification& Notification::Action(EntityId value) {
    action = value;
    autohide = false;
    return *this;
}
Notification& Notification::Content(EntityId value) {
    content = value;
    return *this;
}
Notification& Notification::OnClick(Listener value) {
    onClick = value;
    return *this;
}
Notification& Notification::OnClose(Listener value) {
    onClose = value;
    return *this;
}
Notification& Notification::Refine(const Style& value, uint32_t fields) {
    style = value;
    styleSet = fields;
    return *this;
}

bool NotificationDeliveryIncludesInApp(NotificationDelivery d) {
    return d == NotificationDelivery::InApp ||
           d == NotificationDelivery::InAppAndSystem;
}

bool NotificationDeliveryIncludesSystem(NotificationDelivery d) {
    return d == NotificationDelivery::System ||
           d == NotificationDelivery::InAppAndSystem;
}

// --- the system half ------------------------------------------------------

// SYSTEM_TAG_PREFIX. A response to a tag without it belongs to whoever posted
// it, and is not ours to retract or dispatch.
static const char kSystemTagPrefix[] = "gpui-component/notification/";

TempStr NotificationSystemTagTemp(int id) {
    return fmt("%s%d", Str(kSystemTagPrefix), id);
}

bool NotificationTagId(Str tag, int* outId) {
    int prefixLen = (int)sizeof(kSystemTagPrefix) - 1;
    if (!tag.s || tag.len <= prefixLen) {
        return false;
    }
    if (!StrEq(Str(tag.s, prefixLen), Str(kSystemTagPrefix, prefixLen))) {
        return false;
    }
    int id = 0;
    for (int i = prefixLen; i < tag.len; i++) {
        char c = tag.s[i];
        if (c < '0' || c > '9') {
            return false;
        }
        id = id * 10 + (c - '0');
    }
    if (id <= 0) {
        return false;
    }
    if (outId) {
        *outId = id;
    }
    return true;
}

static App* gSysApp = nullptr;
static bool gSysInited = false;

struct NotificationSystemState {
    App* app = nullptr;
    Vec<NotificationSystemEntry> entries;

    ~NotificationSystemState() {
        VecReset(entries);
        if (gSysApp == app) {
            gSysApp = nullptr;
        }
    }
};

static NotificationSystemState* SysState(App* app) {
    NotificationSystemState* state =
        AppGlobalEnsure<NotificationSystemState>(app);
    if (state && !state->app) {
        state->app = app;
    }
    return state;
}

static NotificationSystemState* SysState(Window* win) {
    return SysState(win ? win->app : gSysApp);
}

static void SysEntryRemoveAt(NotificationSystemState* state, int ix) {
    for (int i = ix; i < state->entries.len - 1; i++) {
        state->entries[i] = state->entries[i + 1];
    }
    state->entries.len--;
}

// The platform's response, on the main thread. Only the tag comes back, so
// everything else is what the registry kept.
static void OnSysNotifyResponse(Str tag) {
    NotificationSystemResponse(tag);
}

void NotificationInitSystem(App* app) {
    if (!app) {
        return;
    }
    (void)SysState(app);
    gSysApp = app;
    if (!gSysInited) {
        gSysInited = true;
        SysNotifyOnResponse(MkFunc1Void(OnSysNotifyResponse));
        // Whatever the platform is holding — on Windows a notification area
        // icon — is given back when the process goes.
        AppOnShutdown(SysNotifyShutdown);
    }
}

void NotificationSystemInsert(const NotificationSystemEntry& e) {
    NotificationSystemState* state = SysState(e.win);
    if (!state) {
        return;
    }
    for (int i = 0; i < state->entries.len; i++) {
        if (state->entries[i].id == e.id) {
            SysEntryRemoveAt(state, i);
            break;
        }
    }
    if (state->entries.len >= kNotificationSystemMax) {
        SysEntryRemoveAt(state, 0);
    }
    VecAppend(state->entries, e);
}

const NotificationSystemEntry* NotificationSystemFind(int id, Window* win) {
    NotificationSystemState* state = SysState(win);
    if (!state) {
        return nullptr;
    }
    for (int i = 0; i < state->entries.len; i++) {
        if (state->entries[i].id == id && state->entries[i].win == win) {
            return &state->entries[i];
        }
    }
    return nullptr;
}

int NotificationSystemCount() {
    return NotificationSystemCount(gSysApp);
}

int NotificationSystemCount(const App* app) {
    NotificationSystemState* state = AppGlobalGet<NotificationSystemState>(app);
    return state ? state->entries.len : 0;
}

static void SysDismissTag(int id) {
    SysNotifyDismiss(NotificationSystemTagTemp(id));
}

static bool SystemIdentityMatches(const NotificationSystemEntry& e,
                                  NotificationTypeId type, bool hasKey,
                                  uint32_t key) {
    return e.identityType == type &&
           (!hasKey || (e.identityHasKey && e.identityKey == key));
}

static int NotificationSystemIdentityId(const NotificationItem& item,
                                        Window* win) {
    if (!item.identityType) {
        return 0;
    }
    NotificationSystemState* state = SysState(win);
    if (!state) {
        return 0;
    }
    for (int i = 0; i < state->entries.len; i++) {
        const NotificationSystemEntry& e = state->entries[i];
        if (e.win == win && e.identityType == item.identityType &&
            e.identityHasKey == item.identityHasKey &&
            (!e.identityHasKey || e.identityKey == item.identityKey)) {
            return e.id;
        }
    }
    return 0;
}

void NotificationSystemDismiss(int id, Window* win) {
    NotificationSystemState* state = SysState(win);
    if (!state) {
        return;
    }
    for (int i = 0; i < state->entries.len; i++) {
        if (state->entries[i].id == id && state->entries[i].win == win) {
            SysEntryRemoveAt(state, i);
            SysDismissTag(id);
            return;
        }
    }
}

static void NotificationSystemDismissIdentity(NotificationTypeId type,
                                              bool hasKey, uint32_t key,
                                              Window* win) {
    NotificationSystemState* state = SysState(win);
    if (!state || !type) {
        return;
    }
    for (int i = state->entries.len - 1; i >= 0; i--) {
        if (state->entries[i].win != win ||
            !SystemIdentityMatches(state->entries[i], type, hasKey, key)) {
            continue;
        }
        int id = state->entries[i].id;
        SysEntryRemoveAt(state, i);
        SysDismissTag(id);
    }
}

void NotificationSystemDismissByType(NotificationTypeId type, Window* win) {
    NotificationSystemDismissIdentity(type, false, 0, win);
}

void NotificationSystemDismissByTypeKey(NotificationTypeId type, uint32_t key,
                                        Window* win) {
    NotificationSystemDismissIdentity(type, true, key, win);
}

void NotificationSystemDismissAll(Window* win) {
    NotificationSystemState* state = SysState(win);
    if (!state) {
        return;
    }
    for (int i = state->entries.len - 1; i >= 0; i--) {
        if (state->entries[i].win == win) {
            int id = state->entries[i].id;
            SysEntryRemoveAt(state, i);
            SysDismissTag(id);
        }
    }
}

void NotificationSystemResponse(Str tag) {
    int id = 0;
    if (!NotificationTagId(tag, &id)) {
        return;
    }
    // Platforms usually take a clicked notification away themselves; the ones
    // that keep it are why this is asked for at all.
    SysNotifyDismiss(tag);
    NotificationSystemState* state = SysState(gSysApp);
    const NotificationSystemEntry* found = nullptr;
    if (state) {
        for (int i = 0; i < state->entries.len; i++) {
            if (state->entries[i].id == id) {
                found = &state->entries[i];
                break;
            }
        }
    }
    // The platform can deliver a response for a notification posted before
    // the process restarted, and entries past the cap are pruned. Rust brings
    // the application forward even then; there is no window named here to
    // bring forward, so nothing happens.
    if (!found) {
        return;
    }
    Window* win = found->win;
    EntityId list = found->list;
    AppActivate(win);
    if (!list.IsValid()) {
        // Nothing to dispatch to: the list never rendered, so its handle was
        // never stamped. The window is still brought forward.
        return;
    }
    // Off the platform's own event and onto the next turn of the loop, which
    // is where an entity may be touched. The post is dropped if the window or
    // the list has gone by then.
    Entity<NotificationListState> e;
    e.id = list;
    WindowPost(win, ListenTo(e, &NotificationListState::OnSystemResponse,
                             (intptr_t)id));
}

void NotificationListState::OnSystemResponse(NotificationListState* self,
                                             Ctx* cx, const ClickEvent*,
                                             intptr_t idArg) {
    int id = (int)idArg;
    Listener onClick = {};
    NotificationSystemState* state = SysState(cx->app);
    if (state) {
        for (int i = 0; i < state->entries.len; i++) {
            if (state->entries[i].id == id && state->entries[i]
                                                      .win == cx->win) {
                onClick = state->entries[i].onClick;
                SysEntryRemoveAt(state, i);
                break;
            }
        }
    }
    // A no-op when the card already closed or was never created, which is
    // what system-only delivery leaves.
    NotificationDismiss(self, cx, id);
    if (onClick.IsValid()) {
        ClickEvent ev = {};
        ListenerCall(cx->app, cx->win, onClick, &ev);
    }
    Notify(cx);
}

int NotificationIndexOf(const NotificationListState* s, int id) {
    for (int i = 0; i < s->items.len; i++) {
        if (s->items[i].id == id) {
            return i;
        }
    }
    return -1;
}

bool NotificationIdentitySame(const Notification& a, const Notification& b) {
    if (a.identityType || b.identityType) {
        return a.identityType != 0 && a.identityType == b.identityType &&
               a.identityHasKey == b.identityHasKey &&
               (!a.identityHasKey || a.identityKey == b.identityKey);
    }
    return a.id != 0 && a.id == b.id;
}

static int NotificationIndexOfIdentity(const NotificationListState* s,
                                       const Notification& item) {
    for (int i = 0; i < s->items.len; i++) {
        if (NotificationIdentitySame(s->items[i], item)) {
            return i;
        }
    }
    return -1;
}

static void NotificationFreeOwned(Notification* item) {
    if (!item || !item->ownsText) {
        return;
    }
    StrFree(item->title);
    StrFree(item->message);
    item->title = {};
    item->message = {};
    item->ownsText = false;
}

static Notification NotificationOwnedCopy(const Notification& item) {
    Notification copy = item;
    copy.title = StrDup(item.title);
    copy.message = StrDup(item.message);
    copy.ownsText = true;
    return copy;
}

NotificationListState::~NotificationListState() {
    NotificationStopAdvancing(this);
    for (int i = 0; i < items.len; i++) {
        NotificationFreeOwned(&items[i]);
    }
    VecReset(items);
}

bool NotificationListState::IsExpanded() const {
    for (int i = 0; i < 8; i++) {
        if (stackHovered[i] || stackFocused[i]) {
            return true;
        }
    }
    return false;
}

static void NotificationRemoveAt(NotificationListState* s, int ix) {
    ToastRemove(&s->stack, s->items[ix].id);
    NotificationFreeOwned(&s->items[ix]);
    for (int i = ix; i < s->items.len - 1; i++) {
        s->items[i] = s->items[i + 1];
    }
    if (s->items.len > 0) {
        s->items[s->items.len - 1] = Notification{};
    }
    s->items.len--;
}

// push_system: what the OS notification center is given, and the registry
// entry its response comes back through.
static void NotificationPushSystem(NotificationListState* s, Ctx* cx,
                                   const Notification& item) {
    Str title = item.title;
    Str body = item.message;
    if (title.len == 0) {
        // A message with no title becomes the system notification's title,
        // and one with neither — a content-only notification — has nothing
        // textual to show.
        if (body.len == 0) {
            return;
        }
        title = body;
        body = Str{};
    }
    NotificationInitSystem(cx->app);
    TempStr tag = NotificationSystemTagTemp(item.id);
    // Rust registers and then posts; the order is the other way here because
    // a platform that cannot post — every one but Windows so far — would
    // otherwise leave an entry waiting for a response that cannot come.
    if (!SysNotifyShow(tag, title, body)) {
        return;
    }
    NotificationSystemEntry e;
    e.id = item.id;
    e.identityType = item.identityType;
    e.identityKey = item.identityKey;
    e.identityHasKey = item.identityHasKey;
    e.list = s->self;
    e.win = cx->win;
    e.onClick = item.onClick;
    NotificationSystemInsert(e);
}

int NotificationPush(NotificationListState* s, Ctx* cx, Notification item,
                     int timeoutMs) {
    // A push with an id already in the list replaces that one: Rust keys its
    // notifications by NotificationId, so the same one never stacks twice.
    int at = NotificationIndexOfIdentity(s, item);
    if (at >= 0) {
        if (item.id == 0) {
            item.id = s->items[at].id;
        }
        NotificationRemoveAt(s, at);
    }
    NotificationDelivery defaultDelivery = s->delivery;
    if (cx && s->useThemeSettings) {
        defaultDelivery = ThemeNow(cx->app).notification.delivery;
    }
    NotificationDelivery delivery =
        item.hasDelivery ? item.delivery : defaultDelivery;
    if (item.id == 0 && cx && NotificationDeliveryIncludesSystem(delivery)) {
        item.id = NotificationSystemIdentityId(item, cx->win);
    }
    if (item.id == 0) {
        item.id = s->nextId++;
    }
    // The id has to be settled before this: it is what the system tag names,
    // and what makes a second push with the same id replace the first there
    // as well.
    if (cx && NotificationDeliveryIncludesSystem(delivery)) {
        NotificationPushSystem(s, cx, item);
    }
    if (!NotificationDeliveryIncludesInApp(delivery)) {
        return item.id;
    }
    // ToastManager keeps every mounted toast. `max_items` is applied by
    // visible() while rendering; ending entries remain mounted and visible
    // until their exit completes.
    Notification owned = NotificationOwnedCopy(item);
    if (!VecAppend(s->items, owned)) {
        NotificationFreeOwned(&owned);
        return item.id;
    }
    if (timeoutMs < 0) {
        timeoutMs = item.autohide ? 5000 : 0;
    }
    if (!ToastPush(&s->stack, item.id, timeoutMs)) {
        NotificationFreeOwned(&s->items[s->items.len - 1]);
        s->items.len--;
        return item.id;
    }
    NotificationStartAdvancing(s, cx);
    return item.id;
}

void NotificationDismiss(NotificationListState* s, Ctx* cx, int id) {
    // Unconditional, as Rust's close() is: a system-only notification has no
    // card to animate out and must still be retracted.
    if (cx) {
        NotificationSystemDismiss(id, cx->win);
    }
    for (int i = 0; i < s->stack.entries.len; i++) {
        if (s->stack.entries[i].id == id) {
            // The card animates out first; advance drops it when it is done.
            s->stack.entries[i].status = ToastStatus::Ending;
            s->stack.entries[i].elapsedMs = 0;
            return;
        }
    }
}

static void NotificationDismissIdentity(NotificationListState* s, Ctx* cx,
                                        NotificationTypeId type, bool hasKey,
                                        uint32_t key) {
    if (!s || !type) {
        return;
    }
    if (cx) {
        if (hasKey) {
            NotificationSystemDismissByTypeKey(type, key, cx->win);
        } else {
            NotificationSystemDismissByType(type, cx->win);
        }
    }
    for (int i = 0; i < s->items.len; i++) {
        const NotificationItem& item = s->items[i];
        bool matches =
            item.identityType == type &&
            (!hasKey || (item.identityHasKey && item.identityKey == key));
        if (!matches) {
            continue;
        }
        for (int j = 0; j < s->stack.entries.len; j++) {
            if (s->stack.entries[j].id == item.id) {
                s->stack.entries[j].status = ToastStatus::Ending;
                s->stack.entries[j].elapsedMs = 0;
                break;
            }
        }
    }
}

void NotificationDismissByType(NotificationListState* s, Ctx* cx,
                               NotificationTypeId type) {
    NotificationDismissIdentity(s, cx, type, false, 0);
}

void NotificationDismissByTypeKey(NotificationListState* s, Ctx* cx,
                                  NotificationTypeId type, uint32_t key) {
    NotificationDismissIdentity(s, cx, type, true, key);
}

void NotificationClear(NotificationListState* s, Ctx* cx) {
    if (cx) {
        NotificationSystemDismissAll(cx->win);
    }
    for (int i = 0; i < s->items.len; i++) {
        NotificationDismiss(s, cx, s->items[i].id);
    }
}

void NotificationStartAdvancing(NotificationListState* s, Ctx* cx) {
    if (s->isAdvancing || !cx || !cx->win || !s->self.IsValid()) {
        return;
    }
    s->isAdvancing = true;
    s->advanceWin = cx->win;
    Entity<NotificationListState> self = {s->self};
    s->advanceTimer =
        WindowSetInterval(cx->win, kNotificationTickMs,
                          ListenTo(self, &NotificationListState::OnTick));
}

void NotificationStopAdvancing(NotificationListState* s) {
    if (!s->isAdvancing) {
        return;
    }
    if (s->advanceWin && s->advanceTimer) {
        WindowCancelTimer(s->advanceWin, s->advanceTimer);
    }
    s->isAdvancing = false;
    s->advanceTimer = 0;
    s->advanceWin = nullptr;
}

static bool NotificationAdvanceImpl(NotificationListState* s, Ctx* cx,
                                    int deltaMs) {
    // Only stack expansion — hover or focus — holds the countdown. Window
    // activation says nothing about visibility: a toast on a side-by-side or
    // second-monitor window would otherwise stay up until the window was
    // clicked, and there is no occlusion state to ask instead. A message
    // that must not be missed already asks for no autohide.
    bool paused = s->IsExpanded();
    bool changed = ToastStackAdvance(&s->stack, deltaMs, paused);
    // ToastAdvance reports phase boundaries and removals. Notification also
    // paints progress within Starting/Ending, so those ticks repaint too.
    bool animating = false;
    for (int i = 0; i < s->stack.entries.len; i++) {
        animating = animating || s->stack.entries[i]
                                         .status != ToastStatus::Present;
    }
    // Whatever the stack dropped goes from the list with it.
    for (int i = s->items.len - 1; i >= 0; i--) {
        bool alive = false;
        for (int j = 0; j < s->stack.entries.len; j++) {
            if (s->stack.entries[j].id == s->items[i].id) {
                alive = true;
                break;
            }
        }
        if (!alive) {
            Listener onClose = s->items[i].onClose;
            NotificationRemoveAt(s, i);
            if (cx && onClose.IsValid()) {
                ListenerCall(cx->app, cx->win, onClose, nullptr);
            }
        }
    }
    return changed || animating;
}

bool NotificationAdvance(NotificationListState* s, int deltaMs) {
    return NotificationAdvanceImpl(s, nullptr, deltaMs);
}

bool NotificationAdvance(NotificationListState* s, Ctx* cx, int deltaMs) {
    return NotificationAdvanceImpl(s, cx, deltaMs);
}

void NotificationListState::OnCloseClick(NotificationListState* self, Ctx* cx,
                                         const ClickEvent*, intptr_t id) {
    NotificationDismiss(self, cx, (int)id);
    Notify(cx);
}

void NotificationListState::OnItemClick(NotificationListState* self, Ctx* cx,
                                        const ClickEvent* ev, intptr_t id) {
    int at = NotificationIndexOf(self, (int)id);
    if (at < 0 || !ev) {
        return;
    }
    bool middle = ev->button == MouseButton::Middle;
    Listener onClick = {};
    if (!middle) {
        onClick = self->items[at].onClick;
    }
    // Rust makes ordinary body clicks interactive only when on_click exists;
    // the auxiliary middle-click dismissal is always installed.
    if (!middle && !onClick.IsValid()) {
        return;
    }
    NotificationDismiss(self, cx, (int)id);
    if (onClick.IsValid()) {
        ListenerCall(cx->app, cx->win, onClick, ev);
    }
    Notify(cx);
}

void NotificationListState::OnHover(NotificationListState* self, Ctx* cx,
                                    const HoverEvent* ev, intptr_t anchor) {
    // is_expanded: the pointer over the stack opens it out, and holds every
    // timeout while it is there.
    int ix = (int)anchor;
    if (ix < 0 || ix >= 8 || self->stackHovered[ix] == ev->hovered) {
        return;
    }
    self->stackHovered[ix] = ev->hovered;
    Notify(cx);
}

void NotificationListState::OnTick(NotificationListState* self, Ctx* cx,
                                   const TickEvent*) {
    bool changed = NotificationAdvance(self, cx, kNotificationTickMs);
    // The loop ends itself once the manager is empty, and `push` is the only
    // insertion point, so the clock only ever stops at an instant when
    // nothing is mounted.
    if (self->stack.entries.len == 0) {
        NotificationStopAdvancing(self);
    }
    if (changed) {
        Notify(cx);
    }
}

NotificationList* NotificationList::New(Ctx* cx,
                                        Entity<NotificationListState> state) {
    Arena* a = cx->a;
    NotificationList* l = ArenaNew<NotificationList>(a);
    l->a = a;
    l->cx = cx;
    l->state = state;
    return l;
}

El* NotificationList::IntoEl() {
    NotificationListState* s = state.Get(cx);
    if (s) {
        // cx.entity(): what a system notification's response is dispatched
        // back to.
        s->self = state.id;
    }
    if (!s || s->items.len == 0) {
        return Div(a);
    }

    NotificationSettings settings;
    if (s->useThemeSettings) {
        settings = ThemeNow(cx->app).notification;
    } else {
        settings.placement = s->placement;
        settings.margins = s->margins;
        settings.width = s->width;
        settings.maxItems = s->maxItems;
        settings.delivery = s->delivery;
    }

    // ToastManager::visible(max_items): newest active entries plus every
    // ending entry, kept in display order.
    int activeCount = 0;
    for (int i = 0; i < s->stack.entries.len; i++) {
        if (s->stack.entries[i].status != ToastStatus::Ending) {
            activeCount++;
        }
    }
    int maxItems = settings.maxItems > 0 ? settings.maxItems : 0;
    int firstActive = activeCount > maxItems ? activeCount - maxItems : 0;
    int activeIx = 0;
    ArenaVec<int> shown;
    for (int i = 0; i < s->stack.entries.len; i++) {
        bool ending = s->stack.entries[i].status == ToastStatus::Ending;
        bool visible = ending || activeIx >= firstActive;
        if (!ending) {
            activeIx++;
        }
        if (visible) {
            shown.Append(a, i);
        }
    }
    if (len(shown) == 0) {
        return Div(a);
    }

    // Group after applying the global visibility limit. A notification-level
    // placement creates a separate stable stack, exactly as grouped() does.
    ArenaVec<int> groups[8]{};
    int groupOrder[8] = {};
    int groupCount = 0;
    bool present[8] = {};
    for (int i = 0; i < len(shown); i++) {
        const Notification& item = s->items[shown[i]];
        Anchor anchor = item.hasPlacement ? item.placement : settings.placement;
        int aix = (int)anchor;
        if (aix < 0 || aix >= 8) {
            continue;
        }
        if (!present[aix]) {
            present[aix] = true;
            groupOrder[groupCount++] = aix;
        }
        groups[aix].Append(a, shown[i]);
    }
    for (int i = 0; i < 8; i++) {
        if (!present[i]) {
            s->stackHovered[i] = false;
            s->stackFocused[i] = false;
        }
    }

    const Theme& th = ThemeNow(cx->app);
    WinSize win = WindowSize(cx->win);
    El* root = Div(a)->SizeFull();
    Spring geometry = SpringNew((float)kToastTransitionMs);
    geometry.epsilon = 0.1f;
    Spring fade = SpringNew((float)kToastTransitionMs);

    for (int g = 0; g < groupCount; g++) {
        int aix = groupOrder[g];
        Anchor anchor = (Anchor)aix;
        ArenaVec<int>& group = groups[aix];
        bool bottom = anchor == Anchor::BottomLeft ||
                      anchor == Anchor::BottomCenter ||
                      anchor == Anchor::BottomRight;
        if (!s->stackFocus[aix].IsValid()) {
            s->stackFocus[aix] = FocusHandleNew(cx);
        }
        s->stackFocused[aix] =
            FocusHandleContainsFocused(cx->win, s->stackFocus[aix]);
        bool expanded = s->stackHovered[aix] || s->stackFocused[aix];

        float* heights = (float*)Alloc(a, (int)sizeof(float) * len(group));
        float* collapsedOff = (float*)Alloc(a, (int)sizeof(float) * len(group));
        float* expandedOff = (float*)Alloc(a, (int)sizeof(float) * len(group));
        for (int i = 0; i < len(group); i++) {
            const Notification& item = s->items[group[i]];
            heights[i] = item.measured.h > 0 ? item.measured.h : s->itemH;
        }
        float expandedH = 0;
        float collapsedH = ToastStackGeometry(
            heights, len(group), kToastCollapsedPeek, kToastExpandedGap, bottom,
            collapsedOff, expandedOff, &expandedH);
        Str anchorKey = StrDup(a, fmt("%d", aix));
        float stackH = SpringValue(
            cx, MotionId(StrL("notification-stack-height"), anchorKey),
            expanded ? expandedH : collapsedH, geometry);

        Str stackId = StrDup(a, fmt("notification-list-%d", aix));
        El* layer = Div(a)
                        ->Absolute()
                        ->Fixed()
                        ->W(settings.width)
                        ->H(stackH)
                        ->MaxH(win.dipH)
                        ->Id(stackId)
                        ->Click(HashClickId(stackId))
                        ->TrackFocus(s->stackFocus[aix])
                        ->TabStop(true)
                        ->OnHover(ListenTo(
                            state, &NotificationListState::OnHover, aix));
        bool right = anchor == Anchor::TopRight ||
                     anchor == Anchor::RightCenter ||
                     anchor == Anchor::BottomRight;
        bool center =
            anchor == Anchor::TopCenter || anchor == Anchor::BottomCenter;
        float left = right ? win.dipW - settings.width - settings.margins.right
                           : (center ? (win.dipW - settings.width) * 0.5f
                                     : settings.margins.left);
        float top = bottom ? win.dipH - stackH - settings.margins.bottom
                           : settings.margins.top;
        if (anchor == Anchor::LeftCenter || anchor == Anchor::RightCenter) {
            top = (win.dipH - stackH) * 0.5f;
        }
        layer->Left(left)->Top(top);

        for (int i = 0; i < len(group); i++) {
            int itemIx = group[i];
            Notification& item = s->items[itemIx];
            const ToastEntry& entry = s->stack.entries[itemIx];
            int rank = len(group) - 1 - i;
            // "visibility": collapsed_visible is how many of them show at
            // all when the stack is closed, and the ones past it fade rather
            // than vanishing. A card that has finished fading is left out —
            // Rust keeps it in the tree at zero opacity, where it would still
            // be in the way of the pointer.
            Str key = StrDup(a, fmt("%d", item.id));
            float visible = SpringValue(
                cx, MotionId(StrL("toast-visibility"), key),
                (expanded || rank < kToastCollapsedVisible) ? 1.f : 0.f, fade);
            if (visible <= 0.01f) {
                continue;
            }
            // "offset" and "inset": where this card sits, and how much
            // narrower it is than the front one. Both are the card's own
            // transitions, keyed on its id, so a stack that opens moves each
            // of them from wherever it had got to.
            float off = SpringValue(cx, MotionId(StrL("toast-offset"), key),
                                    expanded ? expandedOff[i] : collapsedOff[i],
                                    geometry);
            float shrink = SpringValue(
                cx, MotionId(StrL("toast-inset"), key),
                expanded ? 0.f
                         : settings.width * kToastCollapsedScaleStep *
                               (float)(rank < kToastCollapsedVisible
                                           ? rank
                                           : kToastCollapsedVisible - 1),
                geometry);

            float transitionOpacity = 1.f;
            float transitionY = 0.f;
            if (entry.status == ToastStatus::Starting) {
                float delta =
                    (float)entry.elapsedMs / (float)kToastTransitionMs;
                if (delta > 1.f) delta = 1.f;
                transitionOpacity = delta;
                transitionY = (bottom ? 1.f : -1.f) * 96.f * (1.f - delta);
            } else if (entry.status == ToastStatus::Ending) {
                float delta = (float)entry.elapsedMs / (float)kToastExitMs;
                if (delta > 1.f) delta = 1.f;
                transitionOpacity = 1.f - delta;
                transitionY = (bottom ? 1.f : -1.f) * 96.f * delta;
            }

            Listener close = ListenTo(
                state, &NotificationListState::OnCloseClick, (intptr_t)item.id);
            Listener click = ListenTo(
                state, &NotificationListState::OnItemClick, (intptr_t)item.id);
            El* card = gpui::Toast::New(cx, StrL("notification"))
                           ->TransitionStatus(entry.status)
                           ->IntoEl()
                           ->FlexRow()
                           ->Group()
                           ->W(kFill)
                           ->Gap(12)
                           ->PadY(14)
                           ->PadX(16)
                           ->Border(1, th.border)
                           ->Bg(th.tokens.popover)
                           ->Radius(th.radiusLg)
                           ->Refine(item.style, item.styleSet)
                           ->OnClick(click)
                           ->BoundsOut(&item.measured);

            IconName iconName = item.hasIcon ? item.icon : IconName::None;
            Rgba iconFg = th.foreground;
            if (item.hasType) {
                switch (item.type) {
                    case NotificationType::Info:
                        iconName = IconName::Info;
                        iconFg = th.info;
                        break;
                    case NotificationType::Success:
                        iconName = IconName::CircleCheck;
                        iconFg = th.success;
                        break;
                    case NotificationType::Warning:
                        iconName = IconName::TriangleAlert;
                        iconFg = th.warning;
                        break;
                    case NotificationType::Error:
                        iconName = IconName::CircleX;
                        iconFg = th.danger;
                        break;
                }
            }
            bool hasIcon = iconName != IconName::None;
            if (hasIcon) {
                card->Child(Div(a)->Absolute()->Top(18)->Left(16)->Child(
                    IconEl(a, iconName, 16)->Fg(iconFg)));
            }
            El* body = Div(a)->FlexCol()->Flex1()->ClipX()->ClipY();
            if (hasIcon) {
                body->PadL(24);
            }
            if (item.title.len > 0) {
                body->Child(TextEl(a, item.title)
                                ->Font(14)
                                ->Semibold()
                                ->Fg(th.foreground)
                                ->Wrap()
                                ->W(kFill));
            }
            if (item.message.len > 0) {
                body->Child(TextEl(a, item.message)
                                ->Font(14)
                                ->Fg(th.foreground)
                                ->Wrap()
                                ->W(kFill));
            }
            if (item.content.IsValid()) {
                El* content = EntityRender(cx->app, cx->win, a, item.content);
                if (content) {
                    body->Child(content);
                }
            }
            card->Child(body);
            if (item.action.IsValid()) {
                El* action = EntityRender(cx->app, cx->win, a, item.action);
                if (action) {
                    // The source's generated small action has mr_3p5 so it
                    // stays clear of the hover-only close control.
                    card->Child(Div(a)->PadR(14)->Child(action));
                }
            }
            El* closeButton = component::Button::New(cx, StrL("close"))
                                  ->Ghost()
                                  ->WithSize(UiSize::XSmall)
                                  ->Icon(IconName::X)
                                  ->OnClick(close)
                                  ->IntoEl()
                                  ->StopClick();
            card->Child(Div(a)
                            ->Absolute()
                            ->Top(4)
                            ->Right(4)
                            ->GroupHoverVisible()
                            ->Child(closeButton));
            layer->Child(Div(a)
                             ->Absolute()
                             ->Top(off + transitionY)
                             ->Left(shrink * 0.5f)
                             ->W(settings.width - shrink)
                             ->Opacity(visible * transitionOpacity)
                             ->Child(card));
        }
        root->Child(layer);
    }
    return root;
}

} // namespace component
} // namespace gpui
