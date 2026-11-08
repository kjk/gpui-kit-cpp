/* The platform entry point consumes runtime-owned arguments before GpuiMain.
 * Keep that filtering separate from application arguments: story, shell and
 * user programs all parse argv themselves. */

#include "Test.h"

void TestRuntimeArgs() {
    TestSuite("runtime args");
    char app[] = "tests";
    char first[] = "first";
    char scene[] = "__scene=on";
    char reuse[] = "__layout_reuse=off";
    char second[] = "second";
    char* argv[] = {app, first, scene, reuse, second, nullptr};

    int argc = GpuiTakeRuntimeArgs(5, argv);
    utassert(argc == 3);
    utassert(base::StrEq(Str(argv[0]), StrL("tests")));
    utassert(base::StrEq(Str(argv[1]), StrL("first")));
    utassert(base::StrEq(Str(argv[2]), StrL("second")));
    utassert(argv[3] == nullptr);
    utassert(SceneOn());
    utassert(!LayoutReuseOn());

    // Later layout-reuse tests need the cache on; argv is how this process
    // set it, so argv is how it is put back.
    char on[] = "__layout_reuse=on";
    char* restore[] = {app, on, nullptr};
    GpuiTakeRuntimeArgs(2, restore);
    utassert(LayoutReuseOn());
}
