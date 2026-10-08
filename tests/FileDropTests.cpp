/* OS file drop. gpui turns FileDropEvent into an ExternalPaths drag, and
   on_drop of that type receives the paths. The platform windows feed the
   same event; this drives the shared half. */

#include "Test.h"

#include <cstring>

struct FileDropView {
    int drops = 0;
    char got[128] = {};

    static void OnFiles(FileDropView* self, Ctx*, const DropEvent* ev) {
        self->drops++;
        int n = len(ev->externalPaths);
        if (n >= (int)sizeof(self->got)) {
            n = (int)sizeof(self->got) - 1;
        }
        if (n > 0 && ev->externalPaths.s) {
            memcpy(self->got, ev->externalPaths.s, (size_t)n);
        }
        self->got[n] = 0;
        utassert(base::StrEq(ev->drag.kind, StrL("ExternalPaths")));
    }

    static El* Render(FileDropView*, Ctx* cx) {
        return Div(cx->a)->SizeFull()->Click(1)->OnDrop(
            StrL("ExternalPaths"), Listen(cx, &FileDropView::OnFiles));
    }
};

static void Offer(Window* win, FileDropPhase phase, float x, float y,
                  Str paths) {
    PlatformInput in = InputFileDrop(phase, x, y, paths);
    WindowDispatchInput(win, &in);
}

void TestFileDrop() {
    TestSuite("file drop");

    Arena* scratch = ArenaNew();
    Str paths =
        FileUriListToPaths(scratch, StrL("file:///tmp/a.txt\r\n"
                                         "# comment\r\n"
                                         "http://example.com/nope\r\n"
                                         "file://localhost/tmp/b%20c.txt\n"));
    utassert(base::StrEq(paths, StrL("/tmp/a.txt\n/tmp/b c.txt")));
    ArenaDelete(scratch);

    App* app = TestAppNew();
    auto view = EntityNew<FileDropView>(app);
    Window* win = TestWindowOpen(app, view, 400, 300, 1);
    FileDropView* self = view.Get(app);

    Offer(win, FileDropPhase::Entered, 20, 20, StrL("one.txt\ntwo.txt"));
    utassert(win->dragOverId == 1);
    utassert(self->drops == 0);
    Offer(win, FileDropPhase::Pending, 30, 24, {});
    utassert(win->dragOverId == 1);
    Offer(win, FileDropPhase::Submit, 30, 24, {});
    utassert(self->drops == 1);
    utassert(strcmp(self->got, "one.txt\ntwo.txt") == 0);
    utassert(!win->fileDrop);
    utassert(!win->activeDrag.IsValid());

    Offer(win, FileDropPhase::Entered, 8, 8, StrL("gone.txt"));
    Offer(win, FileDropPhase::Exited, 8, 8, {});
    Offer(win, FileDropPhase::Submit, 8, 8, {});
    utassert(self->drops == 1);

    Offer(win, FileDropPhase::Entered, 2000, 2000, StrL("miss.txt"));
    Offer(win, FileDropPhase::Submit, 2000, 2000, {});
    utassert(self->drops == 1);

    TestAppFree(app);
}
