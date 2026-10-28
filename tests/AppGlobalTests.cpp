/* App::global<T> ownership and isolation. */

#include "Test.h"

struct TestGlobalValue {
    static int destroyed;
    int value = 0;

    ~TestGlobalValue() { destroyed++; }
};

int TestGlobalValue::destroyed = 0;

struct OtherGlobalValue {
    int value = 0;
};

static void GlobalsBelongToOneApp() {
    App first;
    App second;
    TestGlobalValue* a = AppGlobalEnsure<TestGlobalValue>(&first);
    TestGlobalValue* b = AppGlobalEnsure<TestGlobalValue>(&second);
    utassert(a && b && a != b);
    a->value = 7;
    b->value = 11;
    utassert(AppGlobalGet<TestGlobalValue>(&first)->value == 7);
    utassert(AppGlobalGet<TestGlobalValue>(&second)->value == 11);
    utassert(AppGlobalEnsure<TestGlobalValue>(&first) == a);

    OtherGlobalValue* other = AppGlobalEnsure<OtherGlobalValue>(&first);
    other->value = 13;
    utassert(AppGlobalGet<OtherGlobalValue>(&first)->value == 13);
    utassert(AppGlobalGet<OtherGlobalValue>(&second) == nullptr);

    AppGlobalClear(&first);
    AppGlobalClear(&second);
}

static void BaseInitializationIsIdempotentAndIsolated() {
    App first;
    App second;
    BaseInit(&first);
    BaseInit(&first);
    BaseInit(&second);
    utassert(BaseThemeGlobal(&first) != nullptr);
    utassert(BaseThemeGlobal(&second) != nullptr);
    utassert(BaseThemeGlobal(&first) != BaseThemeGlobal(&second));
    utassert(BaseThemeGlobal(&first)
                 ->scrollbar.mode == ScrollbarMode::Scrolling);
    utassert(BaseThemeGlobal(&first)->scrollbar.motion.enter == 0);

    BaseSuppressTextSelection(&first);
    utassert(BaseIsTextSelectionSuppressed(&first));
    utassert(!BaseIsTextSelectionSuppressed(&second));
    BaseResetTextSelectionSuppression(&first);
    utassert(!BaseIsTextSelectionSuppressed(&first));
    GlobalState* sourceName = BaseGlobalStateOf(&first);
    utassert(sourceName != nullptr);

    AppGlobalClear(&first);
    AppGlobalClear(&second);
}

static void BaseGlobalRetainsTheApplicationMenus() {
    App app;
    char menuName[] = "File";
    char rowName[] = "Open";
    MenuRow row = {};
    row.label = Str(rowName);
    row.action = ActionOf(StrL("test::Open"));
    MenuDef menu = {};
    menu.name = Str(menuName);
    menu.items = &row;
    menu.n = 1;

    BaseSetAppMenus(&app, &menu, 1);
    menuName[0] = 'X';
    rowName[0] = 'Y';
    int count = 0;
    const MenuDef* retained = BaseAppMenus(&app, &count);
    utassert(count == 1 && retained);
    utassert(StrEqI(retained[0].name, "File"));
    utassert(retained[0].n == 1);
    utassert(StrEqI(retained[0].items[0].label, "Open"));
    uint32_t action = 0;
    utassert(AppMenuRowForId(&app, 1, &action, nullptr));
    utassert(action == row.action);

    BaseSetAppMenus(&app, nullptr, 0);
    utassert(BaseAppMenus(&app, &count) == nullptr && count == 0);
    AppMenuClear(&app);
    AppGlobalClear(&app);
}

static void UiInitializationIsIdempotentAndIsolated() {
    App first;
    App second;
    component::UiGlobalStateInit(&first);
    component::UiGlobalStateInit(&first);
    component::UiGlobalStateInit(&second);
    // ui/global_state.rs preserves this early legacy initialization point;
    // BaseInit repeats it later after Root initialization.
    utassert(AppGlobalGet<BaseGlobalState>(&first) != nullptr);
    utassert(BaseGlobalStateOf(&second) != nullptr);
    utassert(component::UiSelectionNextDocumentOrder(&first) == 1);
    utassert(component::UiSelectionNextDocumentOrder(&first) == 2);
    utassert(component::UiSelectionNextDocumentOrder(&second) == 1);
    component::UiSelectionFrameBegin(&first);
    utassert(component::UiSelectionNextDocumentOrder(&first) == 1);

    component::UiGlobalStateOf(&first)->selectionDocumentOrder = UINT64_MAX;
    utassert(component::UiSelectionNextDocumentOrder(&first) == UINT64_MAX);
    utassert(component::UiSelectionNextDocumentOrder(&first) == 0);

    EntityId view = {7, 3};
    EntityId nested = {8, 4};
    component::UiTextViewStatePush(&first, view);
    utassert(component::UiTextViewStateCurrent(&first) == view);
    component::UiTextViewStatePush(&first, nested);
    utassert(component::UiTextViewStateCurrent(&first) == nested);
    utassert(!component::UiTextViewStateCurrent(&second).IsValid());
    component::UiTextViewStatePop(&first);
    utassert(component::UiTextViewStateCurrent(&first) == view);
    component::UiTextViewStatePop(&first);
    component::UiTextViewStatePop(&first);
    utassert(!component::UiTextViewStateCurrent(&first).IsValid());

    AppGlobalClear(&first);
    AppGlobalClear(&second);
}

static void ReplacingAndRemovingReleaseOwnership() {
    App app;
    TestGlobalValue::destroyed = 0;
    TestGlobalValue* first = new TestGlobalValue();
    AppGlobalSetRaw(&app, AppGlobalKey<TestGlobalValue>(), first,
                    &AppGlobalDelete<TestGlobalValue>);
    TestGlobalValue* second = new TestGlobalValue();
    AppGlobalSetRaw(&app, AppGlobalKey<TestGlobalValue>(), second,
                    &AppGlobalDelete<TestGlobalValue>);
    utassert(TestGlobalValue::destroyed == 1);
    utassert(AppGlobalRemove<TestGlobalValue>(&app));
    utassert(TestGlobalValue::destroyed == 2);
    utassert(!AppGlobalRemove<TestGlobalValue>(&app));
    utassert(AppGlobalGet<TestGlobalValue>(&app) == nullptr);
    AppGlobalClear(&app);
}

static Bounds CellAt(float x) {
    return Bounds{x, 0, 100, 30};
}

static void ADragOutsideACellIsUnchanged() {
    Point anchor = {5, 5};
    Point cursor = {250, 80};
    Point outA;
    Point outC;
    utassert(ConfineToCell(false, {}, {}, true, CellAt(0), anchor, cursor,
                           &outA, &outC));
    utassert(outA.x == anchor.x && outA.y == anchor.y && outC.x == cursor.x &&
             outC.y == cursor.y);
    Point oldAnchor = {50, 10};
    utassert(ConfineToCell(true, oldAnchor, CellAt(0), false, {}, anchor,
                           cursor, &outA, &outC));
    utassert(outA.x == anchor.x && outC.x == cursor.x && outC.y == cursor.y);
}

static void ADragFromACellSelectsNothingOutsideIt() {
    Point anchor = {10, 10};
    Point cursor = {150, 20};
    Point outA;
    Point outC;
    utassert(!ConfineToCell(true, anchor, CellAt(0), true, CellAt(100), anchor,
                            cursor, &outA, &outC));
    utassert(!ConfineToCell(true, anchor, CellAt(0), false, {}, anchor, cursor,
                            &outA, &outC));
}

static void ADragFromACellIsClampedToIt() {
    Point anchor = {10, 10};
    Point outA;
    Point outC;
    utassert(ConfineToCell(true, anchor, CellAt(0), true, CellAt(0), anchor,
                           {250, 80}, &outA, &outC));
    utassert(outA.x == 10 && outA.y == 10 && outC.x == 100 && outC.y == 30);
    utassert(ConfineToCell(true, anchor, CellAt(0), true, CellAt(0), anchor,
                           {-20, -5}, &outA, &outC));
    utassert(outC.x == 0 && outC.y == 0);
    Point inside = {60, 12};
    utassert(ConfineToCell(true, anchor, CellAt(0), true, CellAt(0), anchor,
                           inside, &outA, &outC));
    utassert(outC.x == inside.x && outC.y == inside.y);
}

void TestAppGlobals() {
    TestSuite("app globals");
    GlobalsBelongToOneApp();
    ReplacingAndRemovingReleaseOwnership();
    BaseInitializationIsIdempotentAndIsolated();
    BaseGlobalRetainsTheApplicationMenus();
    UiInitializationIsIdempotentAndIsolated();
    ADragOutsideACellIsUnchanged();
    ADragFromACellSelectsNothingOutsideIt();
    ADragFromACellIsClampedToIt();
}
