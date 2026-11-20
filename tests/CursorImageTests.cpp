/* CursorImage and El::CustomCursor: no Rust twin. The element's image travels
   through its hit rect to the window's requested cursor, on a move and on a
   frame drawn under a resting pointer, and a freed image leaves neither. */

#include "Test.h"

struct CursorImageView {
    CursorImage* img = nullptr;

    static El* Render(CursorImageView* self, Ctx* cx) {
        El* inner = Div(cx->a)->W(100)->H(100)->Cursor(CursorKind::Crosshair);
        if (self->img) {
            inner->CustomCursor(self->img);
        }
        return Div(cx->a)->SizeFull()->Child(inner);
    }
};

static const HitRect* CursorHitAt(Window* win, float x, float y) {
    for (int i = len(win->paint.hits) - 1; i >= 0; i--) {
        const HitRect& hr = win->paint.hits[i];
        if (hr.bounds.Contains({x, y})) {
            return &hr;
        }
    }
    return nullptr;
}

void TestCursorImage() {
    TestSuite("cursor image");

    App* app = TestAppNew();
    uint8_t px[2 * 2 * 4] = {};
    px[3] = 0xff;
    utassert(CursorImageNew(app, px, 0, 2, 0, 0) == nullptr);
    utassert(CursorImageNew(app, nullptr, 2, 2, 0, 0) == nullptr);
    CursorImage* img = CursorImageNew(app, px, 2, 2, 1, 1);
    utassert(img != nullptr);

    auto view = EntityNew<CursorImageView>(app);
    CursorImageView* self = view.Get(app);
    self->img = img;
    Window* win = TestWindowOpen(app, view, 400, 300, 1);

    const HitRect* hr = CursorHitAt(win, 50, 50);
    utassert(hr && hr->cursorImage == img &&
             hr->cursor == CursorKind::Crosshair);

    TestSimulateMouseMove(win, {50, 50});
    utassert(win->cursorImage == img);
    utassert(win->cursor == CursorKind::Crosshair);

    TestSimulateMouseMove(win, {300, 200});
    utassert(win->cursorImage == nullptr);
    utassert(win->cursor == CursorKind::Arrow);

    // a frame under a resting pointer applies the element's new choice
    TestSimulateMouseMove(win, {50, 50});
    self->img = nullptr;
    AppInvalidate(win);
    TestDraw(win);
    utassert(win->cursorImage == nullptr);
    utassert(win->cursor == CursorKind::Crosshair);
    self->img = img;
    AppInvalidate(win);
    TestDraw(win);
    utassert(win->cursorImage == img);

    // freeing the image the window shows takes it off the window and its hits
    self->img = nullptr;
    CursorImageFree(img);
    utassert(win->cursorImage == nullptr);
    hr = CursorHitAt(win, 50, 50);
    utassert(hr && hr->cursorImage == nullptr);
    TestSimulateMouseMove(win, {60, 60});
    utassert(win->cursorImage == nullptr);
    utassert(win->cursor == CursorKind::Crosshair);
    CursorImageFree(nullptr);

    TestAppFree(app);
}
