/* The test runner. Builds like an example — it implements GpuiMain and links
   the same amalgam — but the Windows link gives it a console subsystem, so
   printf reaches the terminal that started it.

   Every test here is a port of one in .work/gpui-component at the SHA in
   cmd/versions.ts. Each file says which Rust module it came from. */

#include "Test.h"

#include <math.h>
#include <stdio.h>

int gTestChecks = 0;
int gTestFailures = 0;
const char* gTestSuite = "";

void TestSuite(const char* name) {
    gTestSuite = name;
}

void TestFailed(const char* cond, const char* file, int line) {
    gTestFailures++;
    printf("FAIL %s: %s(%d): %s\n", gTestSuite, file, line, cond);
}

bool TestNear(float a, float b) {
    return fabsf(a - b) < 0.001f;
}

int GpuiMain(int argc, char** argv) {
    TestSuite("runtime argv");
    utassert(argc >= 1 && argv && argv[argc] == nullptr);
    for (int i = 0; i < argc; i++) {
        Str arg(argv[i]);
        utassert(!base::StrStartsWith(arg, "__paint=") &&
                 !base::StrStartsWith(arg, "__msaa=") &&
                 !base::StrStartsWith(arg, "__scene=") &&
                 !base::StrStartsWith(arg, "__layout_reuse="));
    }

    // No test reaches the network. A suite that did would fail on a machine
    // without one and be slow on a machine with one, so the client is off for
    // the whole run and the fetch table starts nothing.
    HttpSetEnabled(false);

    TestBackground();
    TestIndexPath();
    TestAutoScroll();
    TestThemeRegistry();
    TestPositioner();
    TestScale();
    TestFrameSampler();
    TestTitleBar();
    TestTextBoundary();
    TestTextView();
    TestSyntax();
    TestSlider();
    TestPagination();
    TestNumberInput();
    TestOtpInput();
    TestSelect();
    TestDialog();
    TestSheet();
    TestMotion();
    TestScrollbar();
    TestScrollBounce();
    TestTouchSelection();
    TestThemeSettings();
    TestResizable();
    TestTree();
    TestNavStack();
    TestCalendar();
    TestCarousel();
    TestColorPicker();
    TestToast();
    TestVirtualList();
    TestTask();
    TestTaffy();
    TestMarkdown();
    TestHtml5ever();
    TestGpuiBlockLayout();
    TestMinSize();
    TestDatePicker();
    TestPopup();
    TestTextSelection();
    TestRope();
    TestMaskPattern();
    TestUndoManager();
    TestInputState();
    TestInputGroup();
    TestSearchMatcher();
    TestFoldMap();
    TestList();
    TestPopupMenu();
    TestDataTable();
    TestDock();
    TestTab();
    TestSetting();
    TestCommand();
    TestNotification();
    TestSearchableList();
    TestSidebar();
    TestWindowBorder();
    TestAttachment();
    TestAvatar();
    TestBubble();
    TestMarker();
    TestMessage();
    TestMessageScroller();
    TestShimmer();
    TestKbd();
    TestNativeMenu();
    TestAppMenu();
    TestI18n();
    TestLayoutReuse();
    TestTiles();
    TestRoot();
    TestSankey();
    TestJson();
    TestInspector();
    TestThemeColor();
    TestThemeFont();
    TestColor();
    TestWryUri();
    TestAutocorrect();
    TestObservers();
    TestStyleEq();
    TestScrollbarMotion();
    TestAnchorFlip();
    TestDockState();
    TestFocusTrap();
    TestKeymap();
    TestEventEmitter();
    TestListSettings();
    TestStateStyle();
    TestClick();
    TestVec();
    TestElementId();
    TestHoverCard();
    TestArena();
    TestArenaVec();
    TestDrawOps();
    TestHttp();
    TestArenaStr();
    TestStr();
    TestFmt();
    TestGeometry();
    TestExecutor();
    TestAppGlobals();
    TestBuilderCapacity();
    TestHistory();
    TestAccessibility();
    TestButtonGroup();
    TestIcon();
    TestKeyedState();
    TestWindowExt();
    TestDescriptionList();
    TestLabel();
    TestGroupBox();
    TestSizing();
    TestChart();
    TestForm();
    TestQuickJs();
    TestShellCore();
    TestShellDependencies();
    TestShellDock();
    TestScene();
    // Last because it deliberately changes the process-wide paint options.
    TestRuntimeArgs();

    if (gTestFailures == 0) {
        printf("ok: %d checks\n", gTestChecks);
        return 0;
    }
    printf("FAILED: %d of %d checks\n", gTestFailures, gTestChecks);
    return 1;
}
