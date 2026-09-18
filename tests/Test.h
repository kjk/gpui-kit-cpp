/* The whole test framework: utassert(cond), a pass counter, and a failure
   report. Ported tests live next to it, one file per Rust module.

   A test is a plain function. It is not registered anywhere; TestsMain() in
   tests.cpp calls it. That is enough for a tree this size and keeps the
   harness to the few lines below. */

#include "gpui.h"

using namespace gpui;

// Counted by every utassert, whether it passed or not.
extern int gTestChecks;
extern int gTestFailures;
// The suite currently running, so a failure line says where it came from.
extern const char* gTestSuite;

void TestFailed(const char* cond, const char* file, int line);
void TestSuite(const char* name);

#define utassert(cond)                             \
    do {                                           \
        gTestChecks++;                             \
        if (!(cond)) {                             \
            TestFailed(#cond, __FILE__, __LINE__); \
        }                                          \
    } while (0)

// Floats come out of layout arithmetic, so most comparisons are approximate.
// The tolerance is a thousandth of a pixel: tight enough that a wrong formula
// fails, loose enough that the order of two additions does not.
bool TestNear(float a, float b);

#define utassertnear(a, b) utassert(TestNear((a), (b)))

void TestBackground();
void TestIndexPath();
void TestAutoScroll();
void TestThemeRegistry();
void TestPositioner();
void TestScale();
void TestFrameSampler();
void TestTitleBar();
void TestTextBoundary();
void TestTextView();
void TestSyntax();
void TestSlider();
void TestPagination();
void TestNumberInput();
void TestOtpInput();
void TestSelect();
void TestDialog();
void TestSheet();
void TestMotion();
void TestScrollbar();
void TestScrollBounce();
void TestTouchSelection();
void TestThemeSettings();
void TestResizable();
void TestTree();
void TestNavStack();
void TestCalendar();
void TestColorPicker();
void TestToast();
void TestVirtualList();
void TestTask();
void TestTaffy();
void TestMarkdown();
void TestHtml5ever();
void TestGpuiBlockLayout();
void TestMinSize();
void TestDatePicker();
void TestPopup();
void TestTextSelection();
void TestRope();
void TestMaskPattern();
void TestUndoManager();
void TestInputState();
void TestInputGroup();
void TestSearchMatcher();
void TestFoldMap();
void TestList();
void TestPopupMenu();
void TestDataTable();
void TestDock();
void TestTab();
void TestSetting();
void TestCommand();
void TestNotification();
void TestSearchableList();
void TestSidebar();
void TestWindowBorder();
void TestAttachment();
void TestAvatar();
void TestBubble();
void TestMarker();
void TestMessage();
void TestMessageScroller();
void TestShimmer();
void TestKbd();
void TestNativeMenu();
void TestAppMenu();
void TestI18n();
void TestLayoutReuse();
void TestTiles();
void TestRoot();
void TestSankey();
void TestJson();
void TestInspector();
void TestThemeColor();
void TestThemeFont();
void TestColor();
void TestWryUri();
void TestAutocorrect();
void TestObservers();
void TestStyleEq();
void TestScrollbarMotion();
void TestAnchorFlip();
void TestDockState();
void TestFocusTrap();
void TestKeymap();
void TestEventEmitter();
void TestListSettings();
void TestStateStyle();
void TestClick();
void TestVec();
void TestElementId();
void TestHoverCard();
void TestArena();
void TestArenaVec();
void TestDrawOps();
void TestHttp();
void TestArenaStr();
void TestFmt();
void TestStr();
void TestGeometry();
void TestExecutor();
void TestAppGlobals();
void TestBuilderCapacity();
void TestHistory();
void TestAccessibility();
void TestButtonGroup();
void TestIcon();
void TestKeyedState();
void TestWindowExt();
void TestDescriptionList();
void TestLabel();
void TestGroupBox();
void TestSizing();
void TestChart();
void TestForm();
void TestQuickJs();
void TestShellCore();
void TestShellDependencies();
void TestShellDock();
void TestScene();
void TestRuntimeArgs();
