/* The portable scene recorder's frame comparison. Upstream owns one Scene
 * per Window; these tests pin the equivalent ownership in this tree. */

#include "Test.h"

using namespace gpui;

static bool RecordClear(PaintCtx* paint, Rgba color) {
    paint->viewW = 320;
    paint->viewH = 200;
    scene::FrameBegin(paint);
    scene::RecClear(paint, color);
    Bounds damage = {};
    return scene::FrameEnd(paint, &damage);
}

#if !GPUI_OS_WASM
struct CustomImageSourceTest {
    RenderImage* image = nullptr;
    int calls = 0;
};

static ImageLoadState LoadCustomImage(PaintApp*, void* user,
                                      RenderImage** image) {
    auto* test = (CustomImageSourceTest*)user;
    test->calls++;
    *image = test->image;
    return test->image ? ImageLoadState::Ready : ImageLoadState::Loading;
}
#endif

static void FrameComparisonBelongsToOnePaintContext() {
    TestSuite("scene window ownership");
    PaintCtx first = {};
    PaintCtx second = {};

    Rgba black = Rgba8(0, 0, 0, 255);
    Rgba white = Rgba8(255, 255, 255, 255);
    utassert(RecordClear(&first, black));
    utassert(!RecordClear(&first, black));

    // This is the second window's first frame even though it has the same
    // dimensions and pixels as the first window's retained frame.
    utassert(RecordClear(&second, black));
    utassert(!RecordClear(&second, black));

    utassert(RecordClear(&first, white));
    utassert(!RecordClear(&second, black));
    utassert(scene::Stats(&first).frames == 3);
    utassert(scene::Stats(&second).frames == 3);

    scene::Free(&first);
    scene::Free(&second);
}

static void TextLayoutsHaveStableGenerations() {
    TestSuite("scene resource generations");
    PaintApp* app = PaintAppNew();
    utassert(app);
    if (!app) {
        return;
    }
    PaintCtx paint = {};
    paint.pa = app;
    Size size = {};
    TextLayout* first =
        TextLayoutNew(&paint, StrL("same"), 14, 0, false, 0, 0, &size);
    TextLayout* second =
        TextLayoutNew(&paint, StrL("same"), 14, 0, false, 0, 0, &size);
    utassert(first && second);
    if (first && second) {
        uint64_t firstGeneration = TextLayoutGeneration(first);
        utassert(firstGeneration != 0);
        utassert(TextLayoutGeneration(second) != 0);
        utassert(TextLayoutGeneration(second) != firstGeneration);
        TextLayoutAddRef(first);
        TextLayoutRelease(first);
        utassert(TextLayoutGeneration(first) == firstGeneration);
    }
    TextLayoutRelease(first);
    TextLayoutRelease(second);
    PaintAppFree(app);
}

static bool RecordTriangle(PaintCtx* paint, float x, float y) {
    paint->viewW = 320;
    paint->viewH = 200;
    scene::FrameBegin(paint);
    Path* path = scene::RecPathNew(paint, true);
    scene::RecPathMoveTo(path, x, y);
    scene::RecPathLineTo(path, x + 20, y);
    scene::RecPathLineTo(path, x + 10, y + 20);
    scene::RecPathClose(path);
    scene::RecPathFill(paint, path, Rgba8(0, 0, 0, 255));
    Bounds damage = {};
    return scene::FrameEnd(paint, &damage);
}

static void RecordedTextOwnsItsLayout() {
    TestSuite("scene text ownership");
    PaintApp* app = PaintAppNew();
    utassert(app);
    if (!app) {
        return;
    }
    PaintCtx first = {}, second = {};
    first.pa = second.pa = app;
    Size size = {};
    TextLayout* layout = TextLayoutNew(&first, StrL("retained label"), 14, 0,
                                       false, 0, 0, &size);
    utassert(layout);
    if (layout) {
        uint64_t generation = TextLayoutGeneration(layout);
        Bounds damage = {};
        scene::FrameBegin(&first);
        scene::RecTextDraw(&first, layout, 0, 0, Rgba{}, false, 0);
        scene::FrameEnd(&first, &damage);
        scene::FrameBegin(&second);
        scene::RecTextDraw(&second, layout, 0, 0, Rgba{}, false, 0);
        scene::FrameEnd(&second, &damage);
        TextLayoutRelease(layout); // Both scenes now outlive the caller.
        scene::FrameBegin(&first); // Drops only the first scene's reference.
        scene::FrameEnd(&first, &damage);
        utassert(TextLayoutGeneration(layout) == generation);
        utassertnear(TextLayoutSize(layout).w, size.w);
        utassertnear(TextLayoutSize(layout).h, size.h);
    }
    scene::Free(&first);
    scene::Free(&second);
    PaintAppFree(app);
}

static void PathPlacementRemainsPartOfTheFrameHash() {
    TestSuite("scene translated path placement");
    PaintCtx paint = {};

    utassert(RecordTriangle(&paint, 10, 20));
    // The cache may share these two paths' relative geometry, but the frame
    // diff must still see that the primitive moved.
    utassert(RecordTriangle(&paint, 30, 40));
    utassert(!RecordTriangle(&paint, 30, 40));
    utassert(scene::Stats(&paint).pathPrims == 1);

    scene::Free(&paint);
}

// Rust's Arc<RenderImage> keeps decoded pixels alive independently of the
// loading cache. Exercise that contract across cache eviction and two scenes.
static void RecordedImagesSurviveCacheEviction() {
#if !GPUI_OS_WASM
    TestSuite("scene image ownership");
    // A PaintApp is all the image table needs; AppNew would also want a
    // display, and CI has none.
    PaintApp* app = PaintAppNew();
    utassert(app);
    if (!app) return;
    PaintCtx first = {};
    PaintCtx second = {};
    first.pa = second.pa = app;
    first.viewW = second.viewW = 100;
    first.viewH = second.viewH = 100;
    const char* png =
        "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR4nGP4z8DwHwAFAA"
        "H/iZk9HQAAAABJRU5ErkJggg==";
    RenderImage* image =
        ImageForSrc(app, fmt("data:image/png;base64,%s", Str(png)));
    utassert(image);
    if (image) {
        uint64_t generation = RenderImageGeneration(image);
        scene::FrameBegin(&first);
        scene::RecImageDraw(&first, image, Bounds{0, 0, 1, 1}, 0);
        for (int i = 0; i < 40; i++) {
            RenderImage* next = ImageForSrc(
                app, fmt("data:image/png;tag=%d;base64,%s", i, Str(png)));
            utassert(next);
            if (next)
                scene::RecImageDraw(&first, next, Bounds{(float)i, 0, 1, 1}, 0);
        }
        Bounds damage = {};
        scene::FrameEnd(&first, &damage);
        scene::FrameBegin(&second);
        scene::RecImageDraw(&second, image, Bounds{0, 0, 1, 1}, 0);
        scene::FrameEnd(&second, &damage);
        ImageCacheClear();
        utassert(RenderImageGeneration(image) == generation);
        utassert(RenderImageSizePx(image).w == 1);
        scene::FrameBegin(&first); // Release only this scene's ownership.
        scene::FrameEnd(&first, &damage);
        utassert(RenderImageGeneration(image) == generation);
        utassert(RenderImageSizePx(image).h == 1);
    }
    scene::Free(&first);
    scene::Free(&second);
    ImageCacheClear();
    PaintAppFree(app);
#endif
}

static void ObjectFitMatchesGpuiGeometry() {
    TestSuite("image object fit");
    Bounds box{10, 20, 100, 100};
    Size wide{200, 100};
    Bounds fill = ObjectFitBounds(ObjectFit::Fill, box, wide);
    utassert(fill.x == 10 && fill.y == 20 && fill.w == 100 && fill.h == 100);
    Bounds contain = ObjectFitBounds(ObjectFit::Contain, box, wide);
    utassert(contain.x == 10 && contain.y == 45 && contain.w == 100 &&
             contain.h == 50);
    Bounds cover = ObjectFitBounds(ObjectFit::Cover, box, wide);
    utassert(cover.x == -40 && cover.y == 20 && cover.w == 200 &&
             cover.h == 100);
    Bounds none = ObjectFitBounds(ObjectFit::None, box, wide);
    utassert(none.x == -40 && none.y == 20 && none.w == 200 && none.h == 100);
    Bounds scaledDown =
        ObjectFitBounds(ObjectFit::ScaleDown, box, Size{20, 10});
    utassert(scaledDown.x == 50 && scaledDown.y == 65 && scaledDown.w == 20 &&
             scaledDown.h == 10);
    Bounds large = ObjectFitBounds(ObjectFit::ScaleDown, box, wide);
    utassert(large.x == contain.x && large.y == contain.y &&
             large.w == contain.w && large.h == contain.h);
}

static void FailedImagesLayOutTheirFallback() {
#if !GPUI_OS_WASM
    // The browser decodes a picture on its own time, so a source that will
    // never decode is still Loading when the layout runs — and node, which
    // is where the wasm suite runs, has no Image to hand the bytes to at all.
    TestSuite("image fallback layout");
    App owner;
    PaintApp* app = PaintAppNew();
    utassert(app);
    if (!app) {
        return;
    }
    Arena* arena = ArenaNew();
    PaintCtx paint = {};
    paint.pa = app;
    paint.app = &owner;
    paint.viewW = 100;
    paint.viewH = 100;
    El* fallback = Div(arena)->W(30)->H(40);
    El* image = ImageEl(arena, StrL("data:image/png,not-an-image"))
                    ->WithLoading(Div(arena)->W(10)->H(20))
                    ->WithFallback(fallback);
    El* root = Div(arena)->FlexCol()->ItemsStart()->Child(image);
    LayoutEl(&paint, root, 0, 0, 100, 100, 14, Rgba{});
    utassert(image->imageLoadState == ImageLoadState::Failed);
    utassert(image->imageReplacement == fallback);
    utassert(image->first == fallback && image->last == fallback);
    utassert(image->w == 30 && image->h == 40);
    ArenaDelete(arena);
    ImageCacheClear();
    PaintAppFree(app);
#endif
}

static void ImageSourceVariantsResolveWithoutCopyingOwners() {
#if !GPUI_OS_WASM
    TestSuite("image source variants");
    static const uint8_t png[] = {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
        0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
        0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00,
        0x0d, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0xf8, 0xcf, 0xc0, 0xf0,
        0x1f, 0x00, 0x05, 0x00, 0x01, 0xff, 0x89, 0x99, 0x3d, 0x1d, 0x00, 0x00,
        0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
    PaintApp* app = PaintAppNew();
    utassert(app);
    if (!app) {
        return;
    }
    ImageSource encoded = ImageSource::FromImage(png, (int)sizeof(png));
    RenderImage* decoded = ImageForSource(app, encoded);
    utassert(decoded);
    utassert(ImageForSource(app, encoded) == decoded);
    utassert(ImageSourceState(app, encoded) == ImageLoadState::Ready);

    ImageSource render = ImageSource::FromRender(decoded);
    utassert(ImageForSource(app, render) == decoded);
    CustomImageSourceTest loader{decoded, 0};
    ImageSource custom =
        ImageSource::FromCustom(LoadCustomImage, (void*)&loader);
    utassert(ImageSourceState(app, custom) == ImageLoadState::Ready);
    utassert(ImageForSource(app, custom) == decoded);
    utassert(loader.calls == 2);
    ImageCacheClear();
    PaintAppFree(app);
#endif
}

static void Direct2dImagesSurviveTargetRecreation() {
#if GPUI_OS_WINDOWS
    TestSuite("Direct2D image target recreation");
    App* owner = AppNew();
    PaintApp* app = owner ? owner->paint : nullptr;
    utassert(app);
    if (!app) {
        return;
    }
    const char* png =
        "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR4nGP4z8DwHwAFAA"
        "H/iZk9HQAAAABJRU5ErkJggg==";
    RenderImage* image =
        ImageForSrc(app, fmt("data:image/png;base64,%s", Str(png)));
    utassert(image);
    if (image) {
        uint8_t first[4] = {};
        uint8_t second[4] = {};
        PaintCtx paint = {};
        paint.pa = app;
        paint.opacity = 1;
        utassert(PaintTargetBeginOffscreen(&paint, 1, 1));
        RenderImageDraw(&paint, image, Bounds{0, 0, 1, 1});
        utassert(PaintTargetEndOffscreen(&paint, first));
        utassert(PaintTargetBeginOffscreen(&paint, 1, 1));
        RenderImageDraw(&paint, image, Bounds{0, 0, 1, 1});
        utassert(PaintTargetEndOffscreen(&paint, second));
        utassert(first[2] > 0 && first[3] > 0);
        utassert(memcmp(first, second, sizeof(first)) == 0);
        uint8_t gray[4] = {};
        utassert(PaintTargetBeginOffscreen(&paint, 1, 1));
        RenderImageDraw(&paint, image, Bounds{0, 0, 1, 1}, Bounds{0, 0, 1, 1},
                        0, 0, true);
        utassert(PaintTargetEndOffscreen(&paint, gray));
        utassert(gray[0] == gray[1] && gray[1] == gray[2] && gray[3] > 0);
    }
    AppFree(owner);
#endif
}

static void WindowsDecodePreservesSourceDimensions() {
#if GPUI_OS_WINDOWS
    TestSuite("Windows image source dimensions");
    App* owner = AppNew();
    PaintApp* app = owner ? owner->paint : nullptr;
    utassert(app);
    if (!app) {
        return;
    }
    // A uniform 1921 x 1 PNG. The platform decoder must keep its dimensions;
    // object-fit and the element's bounds decide how it is displayed.
    const char* png =
        "iVBORw0KGgoAAAANSUhEUgAAB4EAAAABCAYAAADQK9gLAAAAIElEQVR42u3DAQkAAAwE"
        "oetf+tdjKFhtqqqqqqqqqv54kiLz0TdbQJkAAAAASUVORK5CYII=";
    RenderImage* image =
        ImageForSrc(app, fmt("data:image/png;base64,%s", Str(png)));
    utassert(image);
    if (image) {
        utassert(RenderImageStatusGet(image) == RenderImageStatus::Ready);
        Size size = RenderImageSizePx(image);
        utassert(size.w == 1921 && size.h == 1);
    }
    AppFree(owner);
#endif
}

static void WindowsDecodesAnimatedGifFrames() {
#if GPUI_OS_WINDOWS
    TestSuite("Windows animated GIF decode");
    App* owner = AppNew();
    PaintApp* app = owner ? owner->paint : nullptr;
    utassert(app);
    if (!app) {
        return;
    }
    // Two 2x1 full frames, red for 100 ms and blue for 200 ms.
    const char* gif =
        "R0lGODlhAgABAIEAAP8AAAAAAAAAAAAAACH/C05FVFNDQVBFMi4wAwEAAAAh+QQA"
        "CgAAACwAAAAAAgABAAAIBQABAAgIACH5BAEUAAEALAAAAAACAAEAgQAA/wAAAAAA"
        "AAAAAAgFAAEACAgAOw==";
    RenderImage* image =
        ImageForSrc(app, fmt("data:image/gif;base64,%s", Str(gif)));
    utassert(image);
    if (image) {
        utassert(RenderImageFrameCount(image) == 2);
        utassert(RenderImageFrameDurationMs(image, 0) == 100);
        utassert(RenderImageFrameDurationMs(image, 1) == 200);
        uint8_t red[8] = {};
        uint8_t blue[8] = {};
        PaintCtx paint = {};
        paint.pa = app;
        paint.opacity = 1;
        utassert(PaintTargetBeginOffscreen(&paint, 2, 1));
        RenderImageDraw(&paint, image, Bounds{0, 0, 2, 1}, Bounds{0, 0, 2, 1},
                        0, 0, false);
        utassert(PaintTargetEndOffscreen(&paint, red));
        utassert(PaintTargetBeginOffscreen(&paint, 2, 1));
        RenderImageDraw(&paint, image, Bounds{0, 0, 2, 1}, Bounds{0, 0, 2, 1},
                        1, 0, false);
        utassert(PaintTargetEndOffscreen(&paint, blue));
        utassert(red[2] > red[0]);
        utassert(blue[0] > blue[2]);
    }
    AppFree(owner);
#endif
}

static void D3d12ImageDescriptorsAreReusable() {
#if GPUI_OS_WINDOWS && WIN_BACKEND_D3D12
    TestSuite("D3D12 image descriptor reuse");
    char d3d12[] = "__paint=d3d12";
    utassert(WinPaintOptionsTakeArg(Str(d3d12)));
    App* owner = AppNew();
    PaintApp* app = owner ? owner->paint : nullptr;
    utassert(app);
    if (!app) {
        return;
    }
    const char* png =
        "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR4nGP4z8DwHwAFAA"
        "H/iZk9HQAAAABJRU5ErkJggg==";
    RenderImage* first = nullptr;
    uint8_t pixels[140 * 4] = {};
    PaintCtx paint = {};
    paint.pa = app;
    paint.opacity = 1;
    utassert(PaintTargetBeginOffscreen(&paint, 140, 1));
    for (int i = 0; i < 140; i++) {
        RenderImage* image = ImageForSrc(
            app, fmt("data:image/png;descriptor=%d;base64,%s", i, Str(png)));
        utassert(image);
        if (!image) {
            continue;
        }
        if (i == 0) {
            first = image;
            RenderImageRetain(first);
        }
        RenderImageDraw(&paint, image, Bounds{(float)i, 0, 1, 1});
    }
    utassert(PaintTargetEndOffscreen(&paint, pixels));
    for (int i = 0; i < 140; i++) {
        utassert(pixels[i * 4 + 2] > 0 && pixels[i * 4 + 3] > 0);
    }
    if (first) {
        uint8_t pixel[4] = {};
        PaintCtx again = {};
        again.pa = app;
        again.opacity = 1;
        utassert(PaintTargetBeginOffscreen(&again, 1, 1));
        RenderImageDraw(&again, first, Bounds{0, 0, 1, 1});
        utassert(PaintTargetEndOffscreen(&again, pixel));
        utassert(pixel[2] > 0 && pixel[3] > 0);
        RenderImageRelease(first);
    }
    AppFree(owner);
    char restore[] = "__paint=d2d";
    WinPaintOptionsTakeArg(Str(restore));
#endif
}

#if GPUI_OS_WINDOWS && (WIN_BACKEND_D3D11 || WIN_BACKEND_D3D12)
static void GpuImageEvictsAtFinalRelease(Str backend) {
    utassert(WinPaintOptionsTakeArg(backend));
    static const uint8_t png[] = {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
        0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
        0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00,
        0x0d, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0xf8, 0xcf, 0xc0, 0xf0,
        0x1f, 0x00, 0x05, 0x00, 0x01, 0xff, 0x89, 0x99, 0x3d, 0x1d, 0x00, 0x00,
        0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
    App* owner = AppNew();
    PaintApp* app = owner ? owner->paint : nullptr;
    utassert(app);
    if (!app) {
        return;
    }
    RenderImage* image = RenderImageDecode(app, png, (int)sizeof(png));
    utassert(image);
    if (image) {
        uint64_t generation = RenderImageGeneration(image);
        uint8_t pixel[4] = {};
        PaintCtx paint = {};
        paint.pa = app;
        paint.opacity = 1;
        utassert(PaintTargetBeginOffscreen(&paint, 1, 1));
        RenderImageDraw(&paint, image, Bounds{0, 0, 1, 1});
        utassert(gpuw::RenderImageCacheCountForTest(generation) == 1);
        RenderImageRelease(image);
        utassert(gpuw::RenderImageCacheCountForTest(generation) == 0);
        // D3D12 still has the texture in its open command list. It is retired
        // at the fence, so submission remains valid after the CPU image dies.
        utassert(PaintTargetEndOffscreen(&paint, pixel));
        utassert(pixel[2] > 0 && pixel[3] > 0);
    }
    AppFree(owner);
}
#endif

static void GpuImagesEvictAtFinalRelease() {
#if GPUI_OS_WINDOWS && (WIN_BACKEND_D3D11 || WIN_BACKEND_D3D12)
    TestSuite("GPU image final release");
#if WIN_BACKEND_D3D11
    char d3d11[] = "__paint=d3d11";
    GpuImageEvictsAtFinalRelease(Str(d3d11));
#endif
#if WIN_BACKEND_D3D12
    char d3d12[] = "__paint=d3d12";
    GpuImageEvictsAtFinalRelease(Str(d3d12));
#endif
    char restore[] = "__paint=d2d";
    WinPaintOptionsTakeArg(Str(restore));
#endif
}

static void ColoredTextSurvivesRecordingAndRestyling() {
#if GPUI_OS_WINDOWS
    TestSuite("scene colored text ownership and restyling");
    PaintCtx paint = {};
    paint.pa = PaintAppNew();
    paint.opacity = 1;
    paint.viewW = 160;
    paint.viewH = 40;
    Str text = StrL("abc \xE4\xB8\xAD def");
    Size size = {};
    TextLayout* layout = TextLayoutNew(&paint, text, 16, 0, false, 0, 0, &size);
    utassert(layout);
    if (!layout) {
        PaintAppFree(paint.pa);
        return;
    }
    Rgba black = Rgba8(0, 0, 0, 255);
    TextSpan spans[2] = {};
    spans[0].lo = 0;
    spans[0].hi = 3;
    spans[0].color = Rgba8(255, 0, 0, 255);
    spans[1].lo = 4;
    spans[1].hi = 7;
    spans[1].color = Rgba8(0, 0, 255, 255);
    uint8_t reference[160 * 40 * 4] = {};
    uint8_t replay[160 * 40 * 4] = {};
    utassert(PaintTargetBeginOffscreen(&paint, 160, 40));
    CanvasClear(&paint, Rgba8(255, 255, 255, 255));
    TextLayoutDrawSpans(&paint, layout, text, 0, 0, black, spans, 2);
    utassert(PaintTargetEndOffscreen(&paint, reference));

    utassert(PaintTargetBeginOffscreen(&paint, 160, 40));
    scene::FrameBegin(&paint);
    scene::RecClear(&paint, Rgba8(255, 255, 255, 255));
    utassert(
        scene::RecTextDrawSpans(&paint, layout, text, 0, 0, black, spans, 2));
    // The frame must own both text and colors, even if its caller reuses
    // the scratch buffer before hashing or replaying.
    spans[0].color = black;
    Bounds damage = {};
    utassert(scene::FrameEnd(&paint, &damage));
    scene::Replay(&paint, nullptr);
    utassert(PaintTargetEndOffscreen(&paint, replay));
    utassert(memcmp(reference, replay, sizeof(reference)) == 0);

    // Effects must not leak into a later plain draw of the same cached
    // layout, even after its original render target was destroyed.
    utassert(PaintTargetBeginOffscreen(&paint, 160, 40));
    CanvasClear(&paint, Rgba8(255, 255, 255, 255));
    TextLayoutDraw(&paint, layout, 0, 0, black, false);
    utassert(PaintTargetEndOffscreen(&paint, reference));
    TextLayout* plain = TextLayoutNew(&paint, text, 16, 0, false, 0, 0, &size);
    utassert(plain);
    utassert(PaintTargetBeginOffscreen(&paint, 160, 40));
    CanvasClear(&paint, Rgba8(255, 255, 255, 255));
    TextLayoutDraw(&paint, plain, 0, 0, black, false);
    utassert(PaintTargetEndOffscreen(&paint, replay));
    utassert(memcmp(reference, replay, sizeof(reference)) == 0);
    TextLayoutRelease(plain);

    auto record = [&]() {
        scene::FrameBegin(&paint);
        scene::RecTextDrawSpans(&paint, layout, text, 0, 0, black, spans, 2);
        return scene::FrameEnd(&paint, &damage);
    };
    utassert(record());
    utassert(!record());
    spans[1].color = black;
    utassert(record());
    utassert(!record());
    spans[1].hi = text.len;
    utassert(record());
    TextLayoutRelease(layout);
    scene::Free(&paint);
    PaintAppFree(paint.pa);
#endif
}

void TestScene() {
    ColoredTextSurvivesRecordingAndRestyling();
    ObjectFitMatchesGpuiGeometry();
    FailedImagesLayOutTheirFallback();
    ImageSourceVariantsResolveWithoutCopyingOwners();
    RecordedImagesSurviveCacheEviction();
    Direct2dImagesSurviveTargetRecreation();
    WindowsDecodePreservesSourceDimensions();
    WindowsDecodesAnimatedGifFrames();
    D3d12ImageDescriptorsAreReusable();
    GpuImagesEvictAtFinalRelease();
    FrameComparisonBelongsToOnePaintContext();
    TextLayoutsHaveStableGenerations();
    RecordedTextOwnsItsLayout();
    PathPlacementRemainsPartOfTheFrameHash();
}
