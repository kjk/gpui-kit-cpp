/* Behavioral projection of crates/ui/src/window_ext.rs. Rust implements an
 * extension trait on Window; C++ exposes the same operations as Window*
 * free functions and retains layer entities in keyed window state. */

#include "Test.h"

using namespace gpui::component;

namespace {
struct ExtLayer {
    static int dropped;
    ~ExtLayer() { dropped++; }
    static El* Render(ExtLayer*, Ctx* cx) { return Div(cx->a); }
};
int ExtLayer::dropped = 0;

struct ExtNotice {};
} // namespace

static ToastStatus NotificationStatus(const NotificationListState* state,
                                      int id) {
    for (int i = 0; state && i < state->stack.entries.len; i++) {
        if (state->stack.entries[i].id == id) {
            return state->stack.entries[i].status;
        }
    }
    return ToastStatus::Ending;
}

static void WindowOwnsDialogAndSheetEntities() {
    App app;
    Window* window = new Window();
    window->app = &app;
    Arena* arena = ArenaNew();
    Ctx cx = {&app, window, arena, {}};
    ExtLayer::dropped = 0;

    Entity<ExtLayer> firstDialog = EntityNew<ExtLayer>(&app);
    Entity<ExtLayer> secondDialog = EntityNew<ExtLayer>(&app);
    WindowOpenDialog(&cx, firstDialog);
    WindowOpenAlertDialog(&cx, secondDialog, false);
    utassert(WindowHasActiveDialog(&cx));
    utassert(WindowDialogCount(&cx) == 2);
    WindowCloseDialog(&cx);
    utassert(WindowDialogCount(&cx) == 1);
    utassert(!secondDialog.Get(&app));
    utassert(ExtLayer::dropped == 1);

    Entity<ExtLayer> firstSheet = EntityNew<ExtLayer>(&app);
    Entity<ExtLayer> secondSheet = EntityNew<ExtLayer>(&app);
    WindowOpenSheetAt(&cx, firstSheet, SheetPlacement::Left, 280);
    utassert(WindowHasActiveSheet(&cx));
    WindowOpenSheet(&cx, secondSheet, 320);
    utassert(!firstSheet.Get(&app));
    utassert(ExtLayer::dropped == 2);

    // The remaining dialog and sheet are Root-owned handles. Window teardown
    // releases both, as dropping Rust's Root does.
    WindowKeyedFree(window);
    utassert(!firstDialog.Get(&app));
    utassert(!secondSheet.Get(&app));
    utassert(ExtLayer::dropped == 4);

    ArenaDelete(arena);
    delete window;
    EntityDropAll(&app);
}

static void TypedRemovalAndForwardingMethodsUseWindowState() {
    App app;
    Window* window = new Window();
    window->app = &app;
    Arena* arena = ArenaNew();
    Ctx cx = {&app, window, arena, {}};

    NotificationItem one;
    one.message = StrL("one");
    one.Id1<ExtNotice>(1);
    int oneId = WindowPushNotification(&cx, one, 0);
    NotificationItem two;
    two.message = StrL("two");
    two.Id1<ExtNotice>(2);
    int twoId = WindowPushNotification(&cx, two, 0);
    NotificationListState* state = WindowNotifications(&cx).Get(&cx);
    utassert(state && WindowNotificationCount(&cx) == 2);

    WindowRemoveNotification1<ExtNotice>(&cx, 1);
    utassert(NotificationStatus(state, oneId) == ToastStatus::Ending);
    utassert(NotificationStatus(state, twoId) != ToastStatus::Ending);
    WindowRemoveNotification<ExtNotice>(&cx);
    utassert(NotificationStatus(state, twoId) == ToastStatus::Ending);

    InputState input;
    window->input = &input;
    input.focused = true;
    input.focusWin = window;
    HitRect inputHit;
    inputHit.input = &input;
    VecAppend(window->paint.hits, inputHit);
    utassert(WindowFocusedInput(&cx) == &input);
    utassert(WindowHasFocusedInput(&cx));
    window->paint.hits.len = 0;
    utassert(!WindowHasFocusedInput(&cx));
    utassert(window->input == nullptr && !input.focused);

    TempStr selected = AllocStrTemp(7);
    selected.s[0] = 0;
    utassert(WindowSelectedText(&cx, selected.s, len(selected) + 1) == 0);
    utassert(!WindowHasTextSelection(&cx));
    WindowClearTextSelection(&cx);
    WindowEndTextSelection(&cx);

    WindowClearNotifications(&cx);
    WindowKeyedFree(window);
    ArenaDelete(arena);
    delete window;
    EntityDropAll(&app);
}

void TestWindowExt() {
    TestSuite("window ext");
    WindowOwnsDialogAndSheetEntities();
    TypedRemovalAndForwardingMethodsUseWindowState();
}
