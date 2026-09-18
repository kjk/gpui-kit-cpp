/* App and entity image caches — gpui-pre src/elements/image_cache.rs and
 * App::fetch_asset. Resources and encoded Images grow without a process-wide
 * 32/16 cap; an entity RetainAllImageCache is a separate table. */

#include "Test.h"

#if !GPUI_OS_WASM
static const char* kPng =
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR4nGP4z8DwHwAFAA"
    "H/iZk9HQAAAABJRU5ErkJggg==";

static const uint8_t kPngBytes[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
    0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
    0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00,
    0x0d, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0xf8, 0xcf, 0xc0, 0xf0,
    0x1f, 0x00, 0x05, 0x00, 0x01, 0xff, 0x89, 0x99, 0x3d, 0x1d, 0x00, 0x00,
    0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};

static Str TaggedPng(int tag) {
    return fmt("data:image/png;tag=%d;base64,%s", tag, Str(kPng));
}

static void ResourcesGrowPastTheOldProcessCap() {
    TestSuite("image cache resources");
    PaintApp* pa = PaintAppNew();
    utassert(pa);
    if (!pa) {
        return;
    }
    ImageCacheClear();
    for (int i = 0; i < 40; i++) {
        utassert(ImageForSrc(pa, TaggedPng(i)));
    }
    utassert(ImageCacheResourceCount(nullptr) >= 40);
    ImageCacheClear();
    utassert(ImageCacheResourceCount(nullptr) == 0);
    PaintAppFree(pa);
}

static void EncodedImagesGrowPastTheOldProcessCap() {
    TestSuite("image cache encoded");
    PaintApp* pa = PaintAppNew();
    utassert(pa);
    if (!pa) {
        return;
    }
    ImageCacheClear();
    uint8_t buf[sizeof(kPngBytes) + 1];
    for (int b = 0; b < (int)sizeof(kPngBytes); b++) {
        buf[b] = kPngBytes[b];
    }
    for (int i = 0; i < 20; i++) {
        buf[sizeof(kPngBytes)] = (uint8_t)i;
        ImageSource source =
            ImageSource::FromImage(buf, (int)sizeof(kPngBytes) + 1);
        ImageForSource(pa, source);
    }
    utassert(ImageCacheEncodedCount(nullptr) >= 20);
    ImageCacheClear();
    PaintAppFree(pa);
}

static void AnAppStoreIsNotTheProcessFallback() {
    TestSuite("image cache app");
    App* app = AppNew();
    utassert(app && app->paint);
    if (!app) {
        return;
    }
    ImageCacheClear();
    ImageCacheClear(app);
    ImageLookup lookup;
    lookup.app = app;
    lookup.pa = app->paint;
    utassert(ImageForSrc(lookup, TaggedPng(1)));
    utassert(ImageCacheResourceCount(app) == 1);
    utassert(ImageCacheResourceCount(nullptr) == 0);
    ImageCacheClear(app);
    utassert(ImageCacheResourceCount(app) == 0);
    AppFree(app);
}

static void AnEntityCacheIsNotTheAppStore() {
    TestSuite("image cache entity");
    App* app = AppNew();
    utassert(app && app->paint);
    if (!app) {
        return;
    }
    Entity<ImageCache> cache = EntityNewState<ImageCache>(app);
    utassert(cache.Get(app));
    ImageLookup appLookup;
    appLookup.app = app;
    appLookup.pa = app->paint;
    utassert(ImageForSrc(appLookup, TaggedPng(2)));
    utassert(ImageCacheResourceCount(app) == 1);

    ImageLookup entityLookup = appLookup;
    entityLookup.cache = cache.id;
    utassert(ImageForSrc(entityLookup, TaggedPng(3)));
    utassert(cache.Get(app)->Len() == 1);
    utassert(ImageCacheResourceCount(app) == 1);

    Window win = {};
    win.app = app;
    VecAppend(win.imageCacheStack, cache.id);
    ImageLookup stacked = appLookup;
    stacked.win = &win;
    utassert(ImageForSrc(stacked, TaggedPng(4)));
    utassert(cache.Get(app)->Len() == 2);
    utassert(ImageCacheResourceCount(app) == 1);
    cache.Get(app)->Remove(TaggedPng(3));
    utassert(cache.Get(app)->Len() == 1);
    VecReset(win.imageCacheStack);
    EntityDrop(app, cache.id);
    AppFree(app);
}
#endif

void TestImageCache() {
#if !GPUI_OS_WASM
    ResourcesGrowPastTheOldProcessCap();
    EncodedImagesGrowPastTheOldProcessCap();
    AnAppStoreIsNotTheProcessFallback();
    AnEntityCacheIsNotTheAppStore();
#endif
}
