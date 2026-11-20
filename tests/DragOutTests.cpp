/* Drag out of the window and ExternalText drops, which have no Rust twin.
   The platform windows run the OS drag loop and the drop target; this drives
   the shared half: the payload copy, the formats offered, the preview size,
   the mouse up the drag ate, and the ExternalText drag a drop target hears.
   On Windows it also reads the data object DoDragDrop would get. */

#include "Test.h"

#if GPUI_OS_WINDOWS
#include <shlobj.h>
#endif

#include <cstring>

struct DragOutView {
    int ends = 0;
    DragOutResult last = DragOutResult::Canceled;
    int mouseUps = 0;
    int textDrops = 0;
    int pathDrops = 0;
    char text[64] = {};
    char url[64] = {};

    static void OnEnd(DragOutView* self, Ctx*, const DragOutEndEvent* ev) {
        self->ends++;
        self->last = ev->result;
    }

    static void OnUp(DragOutView* self, Ctx*, const MouseUpEvent*) {
        self->mouseUps++;
    }

    static void Copy(char* dst, int room, Str s) {
        int n = std::min(len(s), room - 1);
        if (n > 0) {
            memcpy(dst, s.s, (size_t)n);
        }
        dst[n > 0 ? n : 0] = 0;
    }

    static void OnText(DragOutView* self, Ctx*, const DropEvent* ev) {
        self->textDrops++;
        utassert(base::StrEq(ev->drag.kind, StrL("ExternalText")));
        Copy(self->text, (int)sizeof(self->text), ev->externalText);
        Copy(self->url, (int)sizeof(self->url), ev->externalUrl);
    }

    static void OnPaths(DragOutView* self, Ctx*, const DropEvent*) {
        self->pathDrops++;
    }

    // The left half takes text, the right half files.
    static El* Render(DragOutView*, Ctx* cx) {
        return Div(cx->a)
            ->SizeFull()
            ->FlexRow()
            ->OnMouseUp(Listen(cx, &DragOutView::OnUp))
            ->Child(Div(cx->a)->W(200)->H(300)->Click(1)->OnDrop(
                StrL("ExternalText"), Listen(cx, &DragOutView::OnText)))
            ->Child(Div(cx->a)->W(200)->H(300)->Click(2)->OnDrop(
                StrL("ExternalPaths"), Listen(cx, &DragOutView::OnPaths)));
    }
};

static void TestDragOutShared() {
    DragOutData d;
    DragOutFormat f[3];
    utassert(DragOutFormats(d, f) == 0);
    d.text = StrL("hello");
    d.paths = StrL("C:\\a.txt");
    static const uint8_t kPng[] = {0x89, 'P', 'N', 'G'};
    d.imageBytes = kPng;
    d.imageBytesLen = (int)sizeof(kPng);
    utassert(DragOutFormats(d, f) == 3);
    utassert(f[0] == DragOutFormat::Text);
    utassert(f[1] == DragOutFormat::Paths);
    utassert(f[2] == DragOutFormat::ImageFile);
    utassert(base::StrEq(DragOutImageName(d), StrL("image.png")));
    d.imageName = StrL("page 3.png");
    utassert(base::StrEq(DragOutImageName(d), StrL("page 3.png")));

    // no preview asked for, or one too small to see
    DragOutPreview p = DragOutPreviewSize(d, 800, 400, 1);
    utassert(p.w == 0 && p.h == 0);
    d.previewMaxEdge = 10;
    utassert(DragOutPreviewSize(d, 800, 400, 1).w == 0);

    // scaled down to the longest edge, in pixels at the window's scale
    d.previewMaxEdge = 220;
    p = DragOutPreviewSize(d, 800, 400, 2);
    utassert(p.w == 440 && p.h == 220);
    utassert(p.hotX == 220 && p.hotY == 110);
    // a small image stays as it is; the hot spot follows the fractions
    d.previewHotX = 0;
    d.previewHotY = 1;
    p = DragOutPreviewSize(d, 100, 50, 1);
    utassert(p.w == 100 && p.h == 50);
    utassert(p.hotX == 0 && p.hotY == 49);

    // over white, framed
    uint8_t px[3 * 3 * 4] = {};
    // the middle pixel: half-transparent premultiplied red (BGRA)
    uint8_t* mid = px + (1 * 3 + 1) * 4;
    mid[2] = 100;
    mid[3] = 128;
    DragOutPreviewFrame(px, 3, 3, 3 * 4);
    utassert(px[0] == 60 && px[1] == 60 && px[2] == 60 && px[3] == 255);
    utassert(mid[0] == 127 && mid[1] == 127 && mid[2] == 227);
    utassert(mid[3] == 255);
}

static void TestDragOutWindow() {
    App* app = TestAppNew();
    auto view = EntityNew<DragOutView>(app);
    Window* win = TestWindowOpen(app, view, 400, 300, 1);
    DragOutView* self = view.Get(app);

    utassert(AppCanDragOut(app));
    // nothing to drag
    DragOutData empty;
    utassert(
        !WindowStartDragOut(win, empty, ListenTo(view, &DragOutView::OnEnd)));
    utassert(self->ends == 1 && self->last == DragOutResult::Unsupported);
    utassert(!TestPendingDragOut(nullptr));

    // a press that moved starts one; the payload is the window's copy
    PlatformInput down =
        InputMouseDown(MouseButton::Left, 50, 50, {}, 1, false);
    WindowDispatchInput(win, &down);
    utassert(win->mouseDown);
    char text[] = "selected";
    DragOutData d;
    d.text = Str(text);
    utassert(WindowStartDragOut(win, d, ListenTo(view, &DragOutView::OnEnd)));
    text[0] = 'X';
    DragOutData got;
    utassert(TestPendingDragOut(&got));
    utassert(base::StrEq(got.text, StrL("selected")));
    // one at a time
    utassert(!WindowStartDragOut(win, d, ListenTo(view, &DragOutView::OnEnd)));
    utassert(self->ends == 2 && self->last == DragOutResult::Unsupported);

    // the drop target took the release; the window ends the press itself
    utassert(TestFinishDragOut(DragOutResult::Copied));
    utassert(self->ends == 3 && self->last == DragOutResult::Copied);
    utassert(!win->mouseDown);
    utassert(self->mouseUps == 1);
    utassert(!TestPendingDragOut(nullptr));
    utassert(!TestFinishDragOut(DragOutResult::Copied));

    // text and a link dragged in: only the element taking ExternalText hears it
    PlatformInput in =
        InputTextDrop(FileDropPhase::Entered, 20, 20, StrL("a picture"),
                      StrL("https://x.org/a.png"));
    WindowDispatchInput(win, &in);
    utassert(win->dragOverId == 1);
    in = InputTextDrop(FileDropPhase::Pending, 300, 20, {}, {});
    WindowDispatchInput(win, &in);
    utassert(win->dragOverId == 0);
    in = InputTextDrop(FileDropPhase::Submit, 300, 20, {}, {});
    WindowDispatchInput(win, &in);
    utassert(self->textDrops == 0 && self->pathDrops == 0);

    in = InputTextDrop(FileDropPhase::Entered, 20, 20, StrL("a picture"),
                       StrL("https://x.org/a.png"));
    WindowDispatchInput(win, &in);
    in = InputTextDrop(FileDropPhase::Submit, 30, 30, {}, {});
    WindowDispatchInput(win, &in);
    utassert(self->textDrops == 1);
    utassert(strcmp(self->text, "a picture") == 0);
    utassert(strcmp(self->url, "https://x.org/a.png") == 0);
    utassert(!win->fileDrop);
    utassert(len(win->fileDropText) == 0 && len(win->fileDropUrl) == 0);

    // files with text beside them are still a file drag
    in = InputFileDrop(FileDropPhase::Entered, 300, 20, StrL("one.txt"));
    in.fileDrop.text = StrL("one.txt");
    WindowDispatchInput(win, &in);
    utassert(base::StrEq(win->activeDrag.kind, StrL("ExternalPaths")));
    in = InputFileDrop(FileDropPhase::Submit, 300, 20, {});
    WindowDispatchInput(win, &in);
    utassert(self->pathDrops == 1 && self->textDrops == 1);

    // a drag still out when the test app goes ends without its window
    WindowDispatchInput(win, &down);
    utassert(WindowStartDragOut(win, d, ListenTo(view, &DragOutView::OnEnd)));
    TestAppFree(app);
}

#if GPUI_OS_WINDOWS
static Str ReadGlobalWide(Arena* a, HGLOBAL h) {
    auto* w = (const WCHAR*)GlobalLock(h);
    int n =
        WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    char* buf = (char*)Alloc(a, n);
    WideCharToMultiByte(CP_UTF8, 0, w, -1, buf, n, nullptr, nullptr);
    GlobalUnlock(h);
    return Str(buf, n - 1);
}

static void TestDragOutDataObject() {
    Arena* a = ArenaNew();
    static const uint8_t kPng[] = {0x89, 'P', 'N', 'G', 1, 2, 3};
    DragOutData d;
    d.text = StrL("za\xC5\xBC\xC3\xB3\xC5\x82\xC4\x87");
    d.paths = StrL("C:\\one.txt\nC:\\dir\\two.pdf\n");
    d.imageBytes = kPng;
    d.imageBytesLen = (int)sizeof(kPng);
    IDataObject* obj = DragOutDataObjectNew(d);
    utassert(obj != nullptr);

    CLIPFORMAT cfDesc =
        (CLIPFORMAT)RegisterClipboardFormatW(CFSTR_FILEDESCRIPTORW);
    CLIPFORMAT cfContents =
        (CLIPFORMAT)RegisterClipboardFormatW(CFSTR_FILECONTENTS);
    CLIPFORMAT cfEffect =
        (CLIPFORMAT)RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT);

    // what EnumFormatEtc lists, in order
    IEnumFORMATETC* e = nullptr;
    utassert(SUCCEEDED(obj->EnumFormatEtc(DATADIR_GET, &e)));
    FORMATETC fmts[8] = {};
    ULONG n = 0;
    e->Next(8, fmts, &n);
    e->Release();
    utassert(n == 5);
    utassert(fmts[0].cfFormat == CF_UNICODETEXT);
    utassert(fmts[1].cfFormat == CF_HDROP);
    utassert(fmts[2].cfFormat == cfDesc);
    utassert(fmts[3].cfFormat == cfContents && fmts[3].lindex == 0);
    utassert(fmts[4].cfFormat == cfEffect);

    FORMATETC f = {CF_UNICODETEXT, nullptr, DVASPECT_CONTENT, -1,
                   TYMED_HGLOBAL};
    utassert(obj->QueryGetData(&f) == S_OK);
    STGMEDIUM m = {};
    utassert(SUCCEEDED(obj->GetData(&f, &m)));
    utassert(base::StrEq(ReadGlobalWide(a, m.hGlobal),
                         StrL("za\xC5\xBC\xC3\xB3\xC5\x82\xC4\x87")));
    ReleaseStgMedium(&m);
    f.cfFormat = CF_TEXT;
    utassert(obj->QueryGetData(&f) != S_OK);

    f.cfFormat = CF_HDROP;
    utassert(SUCCEEDED(obj->GetData(&f, &m)));
    HDROP drop = (HDROP)m.hGlobal;
    utassert(DragQueryFileW(drop, 0xffffffffu, nullptr, 0) == 2);
    WCHAR path[MAX_PATH] = {};
    DragQueryFileW(drop, 1, path, MAX_PATH);
    utassert(wcscmp(path, L"C:\\dir\\two.pdf") == 0);
    ReleaseStgMedium(&m);

    f.cfFormat = cfDesc;
    utassert(SUCCEEDED(obj->GetData(&f, &m)));
    auto* fgd = (FILEGROUPDESCRIPTORW*)GlobalLock(m.hGlobal);
    utassert(fgd->cItems == 1);
    utassert(wcscmp(fgd->fgd[0].cFileName, L"image.png") == 0);
    utassert(fgd->fgd[0].nFileSizeLow == sizeof(kPng));
    GlobalUnlock(m.hGlobal);
    ReleaseStgMedium(&m);

    // the file's bytes, as an HGLOBAL or a stream, only for item 0
    f.cfFormat = cfContents;
    f.lindex = 1;
    utassert(obj->QueryGetData(&f) != S_OK);
    f.lindex = 0;
    utassert(SUCCEEDED(obj->GetData(&f, &m)));
    utassert(GlobalSize(m.hGlobal) == sizeof(kPng));
    utassert(memcmp(GlobalLock(m.hGlobal), kPng, sizeof(kPng)) == 0);
    GlobalUnlock(m.hGlobal);
    ReleaseStgMedium(&m);
    f.tymed = TYMED_ISTREAM;
    utassert(SUCCEEDED(obj->GetData(&f, &m)) && m.tymed == TYMED_ISTREAM);
    uint8_t buf[16] = {};
    ULONG read = 0;
    m.pstm->Read(buf, sizeof(buf), &read);
    utassert(read == sizeof(kPng) && memcmp(buf, kPng, sizeof(kPng)) == 0);
    ReleaseStgMedium(&m);

    f = {cfEffect, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    utassert(SUCCEEDED(obj->GetData(&f, &m)));
    utassert(*(DWORD*)GlobalLock(m.hGlobal) == DROPEFFECT_COPY);
    GlobalUnlock(m.hGlobal);
    ReleaseStgMedium(&m);
    obj->Release();

    // text alone offers only text
    DragOutData t;
    t.text = StrL("x");
    obj = DragOutDataObjectNew(t);
    f = {CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    utassert(obj->QueryGetData(&f) != S_OK);
    f.cfFormat = cfEffect;
    utassert(obj->QueryGetData(&f) != S_OK);
    obj->Release();
    utassert(DragOutDataObjectNew(DragOutData{}) == nullptr);
    ArenaDelete(a);
}
#endif

void TestDragOut() {
    TestSuite("drag out");
    TestDragOutShared();
    TestDragOutWindow();
#if GPUI_OS_WINDOWS
    TestDragOutDataObject();
#endif
}
