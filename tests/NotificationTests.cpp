/* Ported from crates/ui/src/notification.rs, the system-delivery half.
 *
 * Where a notification goes, the tag the OS notification is posted under, and
 * the registry that turns a response — which arrives as a tag and nothing
 * else — back into a window, a list and an on_click. The platform half is not
 * exercised here: with no icon in the notification area a post goes nowhere
 * and a retraction is a no-op, which is exactly what every platform but
 * Windows does anyway. */

#include "Test.h"

using namespace gpui;
using namespace gpui::component;

static App gNotificationApp;
static Window gNotificationWinA;
static Window gNotificationWinB;
static Window* const kWinA = &gNotificationWinA;
static Window* const kWinB = &gNotificationWinB;

static void ADeliverySaysWhichHalvesRun() {
    utassert(NotificationDeliveryIncludesInApp(NotificationDelivery::InApp));
    utassert(!NotificationDeliveryIncludesSystem(NotificationDelivery::InApp));
    utassert(!NotificationDeliveryIncludesInApp(NotificationDelivery::System));
    utassert(NotificationDeliveryIncludesSystem(NotificationDelivery::System));
    utassert(NotificationDeliveryIncludesInApp(
        NotificationDelivery::InAppAndSystem));
    utassert(NotificationDeliveryIncludesSystem(
        NotificationDelivery::InAppAndSystem));
}

static void ATagIsNamespacedAndCarriesTheId() {
    TempStr tag = NotificationSystemTagTemp(7);
    utassert(len(tag) > 0);
    int id = 0;
    utassert(NotificationTagId(tag, &id) && id == 7);

    // The same id gives the same tag, which is what makes a second push
    // replace the first in the notification center.
    TempStr again = NotificationSystemTagTemp(7);
    utassert(StrEq(again, tag));

    // Two ids never collide.
    TempStr next = NotificationSystemTagTemp(8);
    utassert(!StrEq(next, tag));

    // A tag the application posted itself is not ours to answer.
    utassert(!NotificationTagId(StrL("com.example.app/own-tag"), &id));
    utassert(!NotificationTagId(StrL("gpui-component/notification/"), &id));
    utassert(!NotificationTagId(StrL("gpui-component/notification/abc"), &id));
}

static void TheRegistryKeepsOneEntryPerIdAndWindow() {
    NotificationSystemDismissAll(kWinA);
    NotificationSystemDismissAll(kWinB);
    utassert(NotificationSystemCount() == 0);

    NotificationSystemEntry e;
    e.id = 1;
    e.win = kWinA;
    NotificationSystemInsert(e);
    e.id = 2;
    NotificationSystemInsert(e);
    utassert(NotificationSystemCount() == 2);

    // A repeat push with the same id replaces its entry rather than stacking
    // a second one.
    NotificationSystemInsert(e);
    utassert(NotificationSystemCount() == 2);
    utassert(NotificationSystemFind(2, kWinA) != nullptr);
    // And that entry belongs to the window it was posted from.
    utassert(NotificationSystemFind(2, kWinB) == nullptr);

    NotificationSystemDismissAll(kWinA);
    utassert(NotificationSystemCount() == 0);
}

static void ADismissLeavesAnotherWindowsNotificationAlone() {
    NotificationSystemDismissAll(kWinA);
    NotificationSystemDismissAll(kWinB);
    NotificationSystemEntry a;
    a.id = 5;
    a.win = kWinA;
    NotificationSystemInsert(a);
    NotificationSystemEntry b;
    b.id = 5;
    b.win = kWinB;
    NotificationSystemInsert(b);
    // The same id from a second window took the tag over; the first window's
    // entry went with it, as Rust's insert drops the entry the tag had.
    utassert(NotificationSystemCount() == 1);
    utassert(NotificationSystemFind(5, kWinB) != nullptr);

    // A dismiss from the window that no longer owns the tag does nothing.
    NotificationSystemDismiss(5, kWinA);
    utassert(NotificationSystemFind(5, kWinB) != nullptr);
    NotificationSystemDismiss(5, kWinB);
    utassert(NotificationSystemCount() == 0);
}

static void TheOldestEntriesArePrunedPastTheCap() {
    NotificationSystemDismissAll(kWinA);
    for (int i = 1; i <= kNotificationSystemMax + 5; i++) {
        NotificationSystemEntry e;
        e.id = i;
        e.win = kWinA;
        NotificationSystemInsert(e);
    }
    utassert(NotificationSystemCount() == kNotificationSystemMax);
    // The first five went; the last one is still there.
    utassert(NotificationSystemFind(1, kWinA) == nullptr);
    utassert(NotificationSystemFind(5, kWinA) == nullptr);
    utassert(NotificationSystemFind(6, kWinA) != nullptr);
    utassert(NotificationSystemFind(kNotificationSystemMax + 5, kWinA) !=
             nullptr);
    NotificationSystemDismissAll(kWinA);
    utassert(NotificationSystemCount() == 0);
}

static void AResponseToAForeignTagIsIgnored() {
    NotificationSystemDismissAll(kWinA);
    NotificationSystemEntry e;
    e.id = 3;
    e.win = kWinA;
    NotificationSystemInsert(e);
    // Applications may post their own system notifications; a response for
    // one of those is not ours to dispatch, and takes nothing off the
    // registry.
    NotificationSystemResponse(StrL("com.example.app/own-tag"));
    utassert(NotificationSystemFind(3, kWinA) != nullptr);
    NotificationSystemDismissAll(kWinA);
}

static void RegistriesAreIsolatedByApp() {
    App other = {};
    Window otherWin;
    otherWin.app = &other;
    NotificationSystemEntry e;
    e.id = 71;
    e.win = &otherWin;
    NotificationSystemInsert(e);
    utassert(NotificationSystemCount(&other) == 1);
    utassert(NotificationSystemCount(&gNotificationApp) == 0);
    NotificationSystemDismissAll(&otherWin);
    AppGlobalClear(&other);
}

// The in-app half, which a null Ctx leaves on its own.
static NotificationItem Item(int id, const char* message) {
    NotificationItem it = Notification::New();
    it.id = id;
    it.Message(Str(message));
    return it;
}

static void SettingsAndBuilderMatchThePublicSourceShape() {
    NotificationSettings settings;
    utassert(settings.placement == Anchor::TopRight);
    utassertnear(settings.margins.top, 50.f);
    utassertnear(settings.margins.right, 16.f);
    utassertnear(settings.margins.bottom, 16.f);
    utassertnear(settings.margins.left, 16.f);
    utassert(settings.maxItems == 10);
    utassertnear(settings.width, 382.f);
    utassert(settings.delivery == NotificationDelivery::InApp);

    Style refine = {};
    refine.width = 410.f;
    Notification n = Notification::Success(StrL("saved"));
    n.Title(StrL("Done"))
        .Placement(Anchor::BottomLeft)
        .System()
        .Autohide(false)
        .Icon(IconName::Bell)
        .Refine(refine, StyleFieldWidth);
    utassert(n.hasType && n.type == NotificationType::Success);
    utassert(base::StrEq(n.title, StrL("Done")));
    utassert(base::StrEq(n.message, StrL("saved")));
    utassert(n.hasPlacement && n.placement == Anchor::BottomLeft);
    utassert(n.hasDelivery && n.delivery == NotificationDelivery::System);
    utassert(!n.autohide);
    utassert(n.hasIcon && n.icon == IconName::Bell);
    utassert(n.styleSet == StyleFieldWidth && n.style.width == 410.f);

    Notification plain = Notification::New();
    plain.Message(StrL("plain"));
    utassert(!plain.hasType);
    utassert(!plain.hasPlacement);
    utassert(!plain.hasDelivery);
    utassert(plain.autohide);

    EntityId action = {7, 2};
    plain.Action(action);
    utassert(plain.action == action && !plain.autohide);
}

static void PushOwnsItsTextAndHonorsBuilderAutohide() {
    NotificationListState s;
    char text[] = "borrowed";
    Notification n = Notification::Info(Str(text));
    n.Autohide(false);
    int id = NotificationPush(&s, nullptr, n);
    text[0] = 'X';
    utassert(id > 0 && s.items.len == 1);
    utassert(base::StrEq(s.items[0].message, StrL("borrowed")));
    utassert(s.items[0].ownsText);
    utassert(!s.stack.entries[0].hasTimeout);
}

namespace {
struct CloseRecorder {
    int closed = 0;
    int clicked = 0;
    static void OnClose(CloseRecorder* self, Ctx*, const ClickEvent*) {
        self->closed++;
    }
    static void OnClick(CloseRecorder* self, Ctx*, const ClickEvent*) {
        self->clicked++;
    }
};
} // namespace

static ToastStatus StatusOf(const NotificationListState& s, int id);

static void CloseAndBodyClickFollowTheSourceLifecycle() {
    App app;
    Window* win = new Window();
    win->app = &app;
    Entity<CloseRecorder> recorder = EntityNewState<CloseRecorder>(&app);
    Ctx cx = {&app, win, nullptr, recorder.id};
    NotificationListState s;

    Notification passive = Notification::Info(StrL("passive"));
    passive.Autohide(false).OnClose(Listen(&cx, &CloseRecorder::OnClose));
    int passiveId = NotificationPush(&s, &cx, passive);
    ClickEvent left = {};
    NotificationListState::OnItemClick(&s, &cx, &left, passiveId);
    utassert(StatusOf(s, passiveId) != ToastStatus::Ending);

    ClickEvent middle = {};
    middle.button = MouseButton::Middle;
    NotificationListState::OnItemClick(&s, &cx, &middle, passiveId);
    utassert(StatusOf(s, passiveId) == ToastStatus::Ending);
    NotificationAdvance(&s, &cx, kToastExitMs);
    utassert(recorder.Get(&app)->closed == 1);

    Notification active = Notification::Info(StrL("active"));
    active.Autohide(false)
        .OnClick(Listen(&cx, &CloseRecorder::OnClick))
        .OnClose(Listen(&cx, &CloseRecorder::OnClose));
    int activeId = NotificationPush(&s, &cx, active);
    NotificationListState::OnItemClick(&s, &cx, &left, activeId);
    utassert(recorder.Get(&app)->clicked == 1);
    utassert(StatusOf(s, activeId) == ToastStatus::Ending);
    NotificationAdvance(&s, &cx, kToastExitMs);
    utassert(recorder.Get(&app)->closed == 2);

    delete win;
    EntityDropAll(&app);
}

static void PerNotificationPlacementsBuildIndependentStableStacks() {
    App app;
    Window* win = new Window();
    win->app = &app;
    win->paint.viewW = 800;
    win->paint.viewH = 600;
    Arena* a = ArenaNew();
    Theme themed = ThemeLight();
    themed.notification.width = 411.f;
    themed.notification.margins.top = 73.f;
    ThemeInstall(&app, ThemeMode::Light, themed);
    Entity<NotificationListState> state =
        EntityNewState<NotificationListState>(&app);
    Ctx cx = {&app, win, a, state.id};
    NotificationListState* s = state.Get(&app);
    s->useThemeSettings = true;
    Notification top = Notification::Info(StrL("default"));
    top.Autohide(false);
    Notification bottom = Notification::Info(StrL("bottom"));
    bottom.Placement(Anchor::BottomLeft).Autohide(false);
    NotificationPush(s, &cx, top);
    NotificationPush(s, &cx, bottom);

    El* root = NotificationList::New(&cx, state)->IntoEl();
    utassert(root->first && root->first->next && !root->first->next->next);
    utassertnear(root->first->style.width, 411.f);
    utassertnear(root->first->style.absTop, 73.f);
    utassert(root->first->next->style.absTop != root->first->style.absTop);
    utassert(s->stackFocus[(int)Anchor::TopRight].IsValid());
    utassert(s->stackFocus[(int)Anchor::BottomLeft].IsValid());

    HoverEvent hover = {};
    hover.hovered = true;
    NotificationListState::OnHover(s, &cx, &hover,
                                   (intptr_t)Anchor::BottomLeft);
    utassert(s->stackHovered[(int)Anchor::BottomLeft]);
    utassert(!s->stackHovered[(int)Anchor::TopRight]);
    utassert(s->IsExpanded());

    WindowMotionFree(win);
    delete win;
    ArenaDelete(a);
    EntityDropAll(&app);
}

// notification.rs after f001d800: an inactive window no longer pauses the
// countdown. Activation says nothing about visibility — a toast raised on a
// side-by-side or second-monitor window would otherwise stay up until the
// window was clicked — and there is no occlusion state to ask instead. Only
// hover or focus, which hold the stack expanded, pause it.
static void AnInactiveWindowDoesNotPauseAutohide() {
    App app;
    Window* win = new Window();
    win->app = &app;
    Ctx cx = {&app, win, nullptr, {}};
    NotificationListState s;
    win->active = false;
    int id =
        NotificationPush(&s, &cx, Notification::Info(StrL("background")), 100);
    NotificationAdvance(&s, &cx, kToastTransitionMs);
    utassert(StatusOf(s, id) == ToastStatus::Present);

    // The window is still inactive, and the toast retires anyway.
    NotificationAdvance(&s, &cx, 100);
    utassert(StatusOf(s, id) == ToastStatus::Ending);

    delete win;
}

static void AnExpandedStackPausesOnlyTheTimeout() {
    App app;
    Window* win = new Window();
    win->app = &app;
    Ctx cx = {&app, win, nullptr, {}};
    NotificationListState s;
    int id = NotificationPush(&s, &cx, Notification::Info(StrL("pause")), 100);
    NotificationAdvance(&s, &cx, kToastTransitionMs);
    utassert(StatusOf(s, id) == ToastStatus::Present);

    // Hover holds the stack open, and the countdown with it.
    s.stackHovered[(int)Anchor::TopRight] = true;
    NotificationAdvance(&s, &cx, 1000);
    utassert(StatusOf(s, id) == ToastStatus::Present);
    // Focus does the same.
    s.stackHovered[(int)Anchor::TopRight] = false;
    s.stackFocused[(int)Anchor::TopRight] = true;
    NotificationAdvance(&s, &cx, 1000);
    utassert(StatusOf(s, id) == ToastStatus::Present);
    s.stackFocused[(int)Anchor::TopRight] = false;
    NotificationAdvance(&s, &cx, 100);
    utassert(StatusOf(s, id) == ToastStatus::Ending);

    delete win;
}

// 52693f2e: the lifecycle clock runs only while something is mounted. A list
// that has never shown anything arms no timer, `push` starts it, and the
// tick that empties the manager stops it.
static void TheLifecycleClockRunsOnlyWhileANotificationIsMounted() {
    App app;
    Window* win = new Window();
    win->app = &app;
    Entity<NotificationListState> entity =
        EntityNewState<NotificationListState>(&app);
    NotificationListState* s = entity.Get(&app);
    utassert(s != nullptr);
    if (!s) {
        delete win;
        return;
    }
    s->self = entity.id;
    Ctx cx = {&app, win, nullptr, entity.id};

    // A list that has never shown anything arms no timer.
    utassert(!s->isAdvancing);

    int id = NotificationPush(s, &cx, Notification::Info(StrL("tick")), 100);
    utassert(s->isAdvancing);
    utassert(s->advanceTimer != 0);

    // Autohide expiry plus the exit transition stops the clock with it. The
    // tick is what ends the loop, so it runs through OnTick rather than
    // NotificationAdvance directly.
    for (int i = 0; i < 100 && s->isAdvancing; i++) {
        NotificationListState::OnTick(s, &cx, nullptr);
    }
    utassert(NotificationIndexOf(s, id) < 0);
    utassert(!s->isAdvancing);
    utassert(s->advanceTimer == 0);

    // A later notification restarts it.
    NotificationPush(s, &cx, Notification::Info(StrL("again")), 100);
    utassert(s->isAdvancing);

    EntityDropAll(&app);
    delete win;
}

static void SystemOnlyDeliveryShowsNoCard() {
    NotificationListState s;
    NotificationItem it = Item(0, "in-app");
    utassert(NotificationPush(&s, nullptr, it, 1000) > 0);
    utassert(s.items.len == 1);

    // The delivery that has no in-app half pushes nothing onto the stack —
    // and still answers with the id, which is what a later dismiss names.
    it = Item(0, "system only");
    it.hasDelivery = true;
    it.delivery = NotificationDelivery::System;
    int id = NotificationPush(&s, nullptr, it, 1000);
    utassert(id > 0);
    utassert(s.items.len == 1);

    // The list's own delivery is what an item with none of its own takes.
    s.delivery = NotificationDelivery::System;
    utassert(NotificationPush(&s, nullptr, Item(0, "by default"), 1000) > 0);
    utassert(s.items.len == 1);

    // And an item that overrides it the other way is still shown.
    it = Item(0, "override");
    it.hasDelivery = true;
    it.delivery = NotificationDelivery::InAppAndSystem;
    utassert(NotificationPush(&s, nullptr, it, 1000) > 0);
    utassert(s.items.len == 2);
}

static void AutohideExpiryDoesNotRetractTheSystemHalf() {
    NotificationSystemDismissAll(kWinA);
    NotificationSystemEntry e;
    e.id = 11;
    e.win = kWinA;
    NotificationSystemInsert(e);

    NotificationListState s;
    NotificationItem it = Item(11, "both");
    NotificationPush(&s, nullptr, it, 100);
    // Long enough to animate in, count down and animate out.
    for (int i = 0; i < 20; i++) {
        NotificationAdvance(&s, 100);
    }
    utassert(s.items.len == 0);
    // Gone from the screen, kept in the notification center: only an explicit
    // dismissal retracts one.
    utassert(NotificationSystemFind(11, kWinA) != nullptr);
    NotificationSystemDismissAll(kWinA);
}

static void MaxItemsLimitsVisibilityWithoutEvictingMountedToasts() {
    NotificationListState s;
    s.maxItems = 1;
    NotificationPush(&s, nullptr, Item(1, "one"), 0);
    NotificationPush(&s, nullptr, Item(2, "two"), 0);
    NotificationPush(&s, nullptr, Item(3, "three"), 0);
    // ToastManager::visible(1) chooses what renders. All three remain mounted
    // and addressable, which is how an ending older toast can finish exiting.
    utassert(s.items.len == 3);
    utassert(s.stack.entries.len == 3);
    NotificationDismiss(&s, nullptr, 1);
    utassert(s.stack.entries[0].status == ToastStatus::Ending);
    utassert(s.items.len == 3);
}

namespace {
struct BuildNotice {};
struct DeployNotice {};
} // namespace

static ToastStatus StatusOf(const NotificationListState& s, int id) {
    for (int i = 0; i < s.stack.entries.len; i++) {
        if (s.stack.entries[i].id == id) {
            return s.stack.entries[i].status;
        }
    }
    return ToastStatus::Ending;
}

static void TypedIdentityReplacesAndRemovesLikeRust() {
    NotificationListState s;
    NotificationItem first = Item(0, "build one");
    first.Id1<BuildNotice>(1);
    int firstId = NotificationPush(&s, nullptr, first, 0);

    NotificationItem replacement = Item(0, "build one again");
    replacement.Id1<BuildNotice>(1);
    int replacementId = NotificationPush(&s, nullptr, replacement, 0);
    utassert(replacementId == firstId);
    utassert(s.items.len == 1);

    NotificationItem second = Item(0, "build two");
    second.Id1<BuildNotice>(2);
    int secondId = NotificationPush(&s, nullptr, second, 0);
    NotificationItem broad = Item(0, "all builds");
    broad.Id<BuildNotice>();
    int broadId = NotificationPush(&s, nullptr, broad, 0);
    NotificationItem other = Item(0, "deploy");
    other.Id<DeployNotice>();
    int otherId = NotificationPush(&s, nullptr, other, 0);
    utassert(s.items.len == 4);
    utassert(NotificationTypeOf<BuildNotice>() !=
             NotificationTypeOf<DeployNotice>());

    NotificationDismissByTypeKey(&s, nullptr, NotificationTypeOf<BuildNotice>(),
                                 2);
    utassert(StatusOf(s, secondId) == ToastStatus::Ending);
    utassert(StatusOf(s, firstId) != ToastStatus::Ending);
    utassert(StatusOf(s, broadId) != ToastStatus::Ending);

    NotificationDismissByType(&s, nullptr, NotificationTypeOf<BuildNotice>());
    utassert(StatusOf(s, firstId) == ToastStatus::Ending);
    utassert(StatusOf(s, broadId) == ToastStatus::Ending);
    utassert(StatusOf(s, otherId) != ToastStatus::Ending);
}

static void SystemRegistryBroadRemovalIncludesEveryKey() {
    NotificationSystemDismissAll(kWinA);
    NotificationSystemEntry one;
    one.id = 81;
    one.win = kWinA;
    one.identityType = NotificationTypeOf<BuildNotice>();
    one.identityHasKey = true;
    one.identityKey = 1;
    NotificationSystemInsert(one);
    NotificationSystemEntry two = one;
    two.id = 82;
    two.identityKey = 2;
    NotificationSystemInsert(two);
    NotificationSystemEntry other = one;
    other.id = 83;
    other.identityType = NotificationTypeOf<DeployNotice>();
    NotificationSystemInsert(other);

    NotificationSystemDismissByTypeKey(NotificationTypeOf<BuildNotice>(), 1,
                                       kWinA);
    utassert(NotificationSystemFind(81, kWinA) == nullptr);
    utassert(NotificationSystemFind(82, kWinA) != nullptr);
    NotificationSystemDismissByType(NotificationTypeOf<BuildNotice>(), kWinA);
    utassert(NotificationSystemFind(82, kWinA) == nullptr);
    utassert(NotificationSystemFind(83, kWinA) != nullptr);
    NotificationSystemDismissAll(kWinA);
}

void TestNotification() {
    TestSuite("notification");
    gNotificationWinA.app = &gNotificationApp;
    gNotificationWinB.app = &gNotificationApp;
    NotificationInitSystem(&gNotificationApp);
    ADeliverySaysWhichHalvesRun();
    ATagIsNamespacedAndCarriesTheId();
    TheRegistryKeepsOneEntryPerIdAndWindow();
    ADismissLeavesAnotherWindowsNotificationAlone();
    TheOldestEntriesArePrunedPastTheCap();
    AResponseToAForeignTagIsIgnored();
    RegistriesAreIsolatedByApp();
    SettingsAndBuilderMatchThePublicSourceShape();
    PushOwnsItsTextAndHonorsBuilderAutohide();
    CloseAndBodyClickFollowTheSourceLifecycle();
    PerNotificationPlacementsBuildIndependentStableStacks();
    AnInactiveWindowDoesNotPauseAutohide();
    AnExpandedStackPausesOnlyTheTimeout();
    TheLifecycleClockRunsOnlyWhileANotificationIsMounted();
    SystemOnlyDeliveryShowsNoCard();
    AutohideExpiryDoesNotRetractTheSystemHalf();
    MaxItemsLimitsVisibilityWithoutEvictingMountedToasts();
    TypedIdentityReplacesAndRemovesLikeRust();
    SystemRegistryBroadRemovalIncludesEveryKey();
    AppGlobalClear(&gNotificationApp);
}
