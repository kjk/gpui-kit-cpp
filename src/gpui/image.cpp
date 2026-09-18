#include "gpui/image.h"

#include "gpui/assets.h"
#include "gpui/paint.h"
#include "gpui/svg.h"
#include "sys/http.h"
#include "sys/executor.h"

namespace gpui {

// ─── data: URIs ───────────────────────────────────────────────────────────

static int Base64Value(char c) {
    if (c >= 'A' && c <= 'Z') {
        return c - 'A';
    }
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 26;
    }
    if (c >= '0' && c <= '9') {
        return c - '0' + 52;
    }
    if (c == '+') {
        return 62;
    }
    if (c == '/') {
        return 63;
    }
    return -1;
}

// Decodes into `out`, ignoring whatever is not base64 — a data: URI wrapped
// across lines in a document is still one payload.
static void Base64Decode(Str s, Vec<uint8_t>* out) {
    uint32_t acc = 0;
    int bits = 0;
    for (int i = 0; i < len(s); i++) {
        int v = Base64Value(s.s[i]);
        if (v < 0) {
            continue;
        }
        acc = (acc << 6) | (uint32_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            VecAppend(*out, (uint8_t)((acc >> bits) & 0xff));
        }
    }
}

static int HexValue(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static void PercentDecode(Str s, Vec<uint8_t>* out) {
    for (int i = 0; i < len(s); i++) {
        if (s.s[i] == '%' && i + 2 < len(s)) {
            int hi = HexValue(s.s[i + 1]);
            int lo = HexValue(s.s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                VecAppend(*out, (uint8_t)(hi * 16 + lo));
                i += 2;
                continue;
            }
        }
        VecAppend(*out, (uint8_t)s.s[i]);
    }
}

// "data:image/png;base64,iVBOR..." — the payload after the comma, decoded by
// whichever of the two encodings the header names.
static bool DataUriBytes(Str src, Vec<uint8_t>* out) {
    if (!base::StrStartsWithI(src, "data:")) {
        return false;
    }
    int comma = -1;
    for (int i = 5; i < len(src); i++) {
        if (src.s[i] == ',') {
            comma = i;
            break;
        }
    }
    if (comma < 0) {
        return false;
    }
    Str header(src.s + 5, comma - 5);
    Str payload(src.s + comma + 1, len(src) - comma - 1);
    bool base64 = false;
    for (int i = 0; i + 6 <= len(header); i++) {
        if (StrEq(Str(header.s + i, 6), StrL("base64"))) {
            base64 = true;
            break;
        }
    }
    if (base64) {
        Base64Decode(payload, out);
    } else {
        PercentDecode(payload, out);
    }
    return out->len > 0;
}

bool ImageSrcIsLocal(Str src) {
    if (!src.s || len(src) <= 0) {
        return false;
    }
    if (base::StrStartsWithI(src, "data:")) {
        return true;
    }
    // Anything with a scheme is somewhere else: http, https, ftp, mailto.
    for (int i = 0; i + 2 < len(src); i++) {
        if (src.s[i] == ':' && src.s[i + 1] == '/' && src.s[i + 2] == '/') {
            return false;
        }
    }
    return true;
}

// The asset a src names. A local path is itself; a remote URL is its last path
// segment, looked for in the asset roots and then under the two folders a
// story's own pictures live in. What Rust gets from its asset system fetching
// the URL, an application here gets by shipping the file.
//
// The answer is a walk of the asset roots, and image layout asks it for the
// same handful of srcs on every measure pass of every frame — a picture that
// is a vector has no decoded bitmap to hit the cache below, so this was the
// walk itself, every time. The roots do not change while the app is up, so
// each src is resolved once and the answer kept, the empty one included:
// that is the common answer for a remote URL nothing shipped, and it is the
// one that costs the most to reach.
struct AssetResolveSlot {
    // One StrDup2 block: src.s is the allocation, asset.s is interior.
    Str src = {};
    Str asset = {};
};
constexpr int kAssetResolveSlots = 64;
static AssetResolveSlot gAssetResolve[kAssetResolveSlots];
static int gAssetResolveN = 0;

static void AssetResolveClear() {
    for (int i = 0; i < gAssetResolveN; i++) {
        StrFree(gAssetResolve[i].src);
        gAssetResolve[i] = {};
    }
    gAssetResolveN = 0;
}

static Str ImageAssetResolve(Arena* a, Str src);

Str ImageAssetFor(Arena* a, Str src) {
    if (!src.s || len(src) <= 0 || base::StrStartsWithI(src, "data:")) {
        return {};
    }
    for (int i = 0; i < gAssetResolveN; i++) {
        if (base::StrEq(gAssetResolve[i].src, src)) {
            Str v = gAssetResolve[i].asset;
            return v.s ? StrDup(a, v) : Str{};
        }
    }
    Str got = ImageAssetResolve(a, src);
    // Past the last slot the walk simply happens again; a page with more than
    // sixty-four distinct pictures is not what this is sized for.
    if (gAssetResolveN < kAssetResolveSlots) {
        AssetResolveSlot* sl = &gAssetResolve[gAssetResolveN++];
        StrDup2(src, got, sl->src, sl->asset);
    }
    return got;
}

static Str ImageAssetResolve(Arena* a, Str src) {
    return ImageSrcIsLocal(src) && AssetsExists(src) ? StrDup(a, src) : Str{};
}

// ─── what the bytes are ───────────────────────────────────────────────────

// A picture the icon renderer draws rather than one the platform decodes.
// The server's content-type would say so, but it is not always right and the
// fetch table does not keep it; the first bytes of the file are, and an SVG
// says so within its first line or two.
static bool LooksLikeSvg(const uint8_t* b, int len) {
    int n = len < 512 ? len : 512;
    for (int i = 0; i + 4 <= n; i++) {
        if (b[i] == '<' && b[i + 1] == 's' && b[i + 2] == 'v' &&
            b[i + 3] == 'g') {
            return true;
        }
    }
    return false;
}

// The bytes a src resolves to without going near a decoder: the data: URI,
// the asset, or the fetched body. False while a fetch is still running, which
// is the one answer the caller must not remember.
enum class SrcBytes : uint8_t {
    No,
    Yes,
    Pending
};

static SrcBytes BytesForSrc(Str src, Vec<uint8_t>* owned,
                            const uint8_t** borrowed, int* borrowedLen) {
    *borrowed = nullptr;
    *borrowedLen = 0;
    if (DataUriBytes(src, owned)) {
        return SrcBytes::Yes;
    }
    // Local sources go through the asset system. A URI goes through the HTTP
    // client, matching GPUI's ImageSource::Resource dispatch.
    Str asset = ImageAssetFor(GetTempArena(), src);
    if (asset.s && AssetsLoad(asset, owned) && owned->len > 0) {
        return SrcBytes::Yes;
    }
    VecReset(*owned);
    if (!HttpUrlIsRemote(src)) {
        return SrcBytes::No;
    }
    switch (HttpFetch(src, borrowed, borrowedLen)) {
        case FetchState::Done:
            return *borrowedLen > 0 ? SrcBytes::Yes : SrcBytes::No;
        case FetchState::Pending:
        case FetchState::None:
            // None means the table had no room to start it; asking again next
            // frame is the whole retry policy.
            return SrcBytes::Pending;
        case FetchState::Failed:
            return SrcBytes::No;
    }
    return SrcBytes::No;
}

// ─── the cache ────────────────────────────────────────────────────────────
//
// A document shows the same badge or logo more than once and repaints many
// times a second, so a decode has to happen once. The key is the src as
// written; a failure is remembered too, or a document full of pictures that
// will not decode would retry every one of them every frame. A fetch that has
// not landed is the one thing not remembered — that answer is not final.
//
// GPUI keeps this table on the App (`fetch_asset`) and, when a tree names
// one, on an entity ImageCache. There is no process-wide cap: a slot lives
// until remove/clear/drop. Tests that only have a PaintApp use gFallback.

struct ImageCacheSlot {
    Str src = {};
    RenderImage* img = nullptr;
    // A vector picture instead: the draw-op stream, ours to free.
    uint8_t* ops = nullptr;
    int opsLen = 0;
    double loadingAt = 0;
    bool pending = false;
    bool tried = false;
};

struct EncodedImageSlot {
    uint64_t hash = 0;
    int bytesLen = 0;
    RenderImage* img = nullptr;
    uint8_t* ops = nullptr;
    int opsLen = 0;
    bool tried = false;
};

struct ImageStore {
    Vec<ImageCacheSlot> resources;
    Vec<EncodedImageSlot> encoded;
};

static ImageStore gFallback;

struct ImageClock {
    uint64_t key = 0;
    double at = 0;
    int frameIndex = 0;
};

constexpr int kImageClockSlots = 64;
static ImageClock gLoadingClocks[kImageClockSlots];
static ImageClock gAnimationClocks[kImageClockSlots];

static void ImageSlotFree(ImageCacheSlot* s) {
    if (s->img) {
        RenderImageRelease(s->img);
        s->img = nullptr;
    }
    if (s->ops) {
        Free(nullptr, s->ops);
        s->ops = nullptr;
    }
    s->opsLen = 0;
    s->loadingAt = 0;
    s->pending = false;
    if (s->src.s) {
        StrFree(s->src);
        s->src = {};
    }
    s->tried = false;
}

static void EncodedSlotFree(EncodedImageSlot* s) {
    if (s->img) {
        RenderImageRelease(s->img);
    }
    Free(nullptr, s->ops);
    *s = {};
}

static void ImageStoreClear(ImageStore* s) {
    if (!s) {
        return;
    }
    for (int i = 0; i < s->resources.len; i++) {
        ImageSlotFree(&s->resources[i]);
    }
    VecClear(s->resources);
    for (int i = 0; i < s->encoded.len; i++) {
        EncodedSlotFree(&s->encoded[i]);
    }
    VecClear(s->encoded);
}

ImageStore* ImageStoreNew() {
    return new ImageStore();
}

void ImageStoreFree(ImageStore* s) {
    if (!s) {
        return;
    }
    ImageStoreClear(s);
    VecReset(s->resources);
    VecReset(s->encoded);
    delete s;
}

static ImageStore* StoreOf(App* app) {
    if (app) {
        if (!app->images) {
            app->images = ImageStoreNew();
        }
        return app->images;
    }
    return &gFallback;
}

static ImageCache* EntityCacheOf(const ImageLookup& cx) {
    if (!cx.app) {
        return nullptr;
    }
    EntityId id = cx.cache;
    if (!id.IsValid() && cx.win && cx.win->imageCacheStack.len > 0) {
        id = cx.win->imageCacheStack[cx.win->imageCacheStack.len - 1];
    }
    if (!id.IsValid()) {
        return nullptr;
    }
    return Entity<ImageCache>{id}.Get(cx.app);
}

static Vec<ImageCacheSlot>* ResourceSlots(const ImageLookup& cx) {
    if (ImageCache* cache = EntityCacheOf(cx)) {
        if (!cache->store) {
            cache->store = ImageStoreNew();
        }
        return &cache->store->resources;
    }
    return &StoreOf(cx.app)->resources;
}

ImageCache::ImageCache() {
    store = ImageStoreNew();
}

ImageCache::~ImageCache() {
    ImageStoreFree(store);
    store = nullptr;
}

void ImageCache::Clear() {
    ImageStoreClear(store);
}

void ImageCache::Remove(Str src) {
    if (!store) {
        return;
    }
    for (int i = 0; i < store->resources.len; i++) {
        if (store->resources[i].tried &&
            base::StrEq(store->resources[i].src, src)) {
            ImageSlotFree(&store->resources[i]);
            store->resources[i] = store->resources[store->resources.len - 1];
            store->resources.len--;
            return;
        }
    }
}

int ImageCache::Len() const {
    return store ? store->resources.len : 0;
}

static uint64_t gImageDecodeEpoch = 1;

void ImageCacheClear() {
    gImageDecodeEpoch++;
    ImageStoreClear(&gFallback);
    for (int i = 0; i < kImageClockSlots; i++) {
        gLoadingClocks[i] = {};
        gAnimationClocks[i] = {};
    }
    AssetResolveClear();
    SvgCacheClear();
    HttpFetchClear();
}

void ImageCacheClear(App* app) {
    gImageDecodeEpoch++;
    if (app && app->images) {
        ImageStoreClear(app->images);
    }
}

int ImageCacheResourceCount(App* app) {
    ImageStore* s = StoreOf(app);
    return s ? s->resources.len : 0;
}

int ImageCacheEncodedCount(App* app) {
    ImageStore* s = StoreOf(app);
    return s ? s->encoded.len : 0;
}

static uint64_t ImageBytesHash(const uint8_t* bytes, int len) {
    uint64_t hash = 1469598103934665603ull;
    for (int i = 0; bytes && i < len; i++) {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    return hash ? hash : 1;
}

struct ImageDecodeJob {
    uint8_t* bytes = nullptr;
    int len = 0;
    RenderImage* placeholder = nullptr;
    RenderImage* decoded = nullptr;
    uint64_t epoch = 0;
};

static void ImageDecodeWork(ImageDecodeJob* job) {
    job->decoded = RenderImageDecode(nullptr, job->bytes, job->len);
}

static void ImageDecodeDone(ImageDecodeJob* job) {
    if (job->epoch == gImageDecodeEpoch && job->placeholder) {
        RenderImageComplete(job->placeholder, job->decoded);
        job->decoded = nullptr;
    }
    if (job->decoded) {
        RenderImageRelease(job->decoded);
    }
    if (job->placeholder) {
        RenderImageRelease(job->placeholder);
    }
    Free(nullptr, job->bytes);
    delete job;
}

static bool ImageDecodeAsync(const ImageLookup& cx) {
    return cx.app && cx.app->paint && ExecOnMainThread() && ExecHasThreads();
}

static void DecodeImageBytes(const ImageLookup& cx, const uint8_t* bytes,
                             int len, RenderImage** imgOut, uint8_t** opsOut,
                             int* opsLenOut) {
    *imgOut = nullptr;
    *opsOut = nullptr;
    *opsLenOut = 0;
    if (!bytes || len <= 0) {
        return;
    }
    if (LooksLikeSvg(bytes, len)) {
        DrawOpsBuilder builder;
        if (SvgToDrawOps(Str((char*)bytes, len), &builder) &&
            builder.data.len > 0) {
            *opsOut = AllocArray<uint8_t>(builder.data.len);
            if (*opsOut) {
                memcpy(*opsOut, builder.data.els, (size_t)builder.data.len);
                *opsLenOut = builder.data.len;
            }
        }
    } else if (cx.pa) {
        if (ImageDecodeAsync(cx)) {
            RenderImage* loading = RenderImageNewLoading();
            auto* job = new ImageDecodeJob();
            job->bytes = AllocArray<uint8_t>(len);
            if (!loading || !job->bytes) {
                Free(nullptr, job->bytes);
                delete job;
                if (loading) {
                    RenderImageRelease(loading);
                }
                *imgOut = RenderImageDecode(cx.pa, bytes, len);
                return;
            }
            memcpy(job->bytes, bytes, (size_t)len);
            job->len = len;
            job->placeholder = loading;
            job->epoch = gImageDecodeEpoch;
            RenderImageRetain(loading);
            if (!ExecSpawn(MkFunc0(ImageDecodeWork, job),
                           MkFunc0(ImageDecodeDone, job))) {
                RenderImageRelease(loading);
                RenderImageRelease(loading);
                Free(nullptr, job->bytes);
                delete job;
                *imgOut = RenderImageDecode(cx.pa, bytes, len);
                return;
            }
            *imgOut = loading;
        } else {
            *imgOut = RenderImageDecode(cx.pa, bytes, len);
        }
    }
}

static int ImageSlotFind(Vec<ImageCacheSlot>* slots, Str src) {
    if (!slots) {
        return -1;
    }
    for (int i = 0; i < slots->len; i++) {
        if ((*slots)[i].tried && base::StrEq((*slots)[i].src, src)) {
            return i;
        }
    }
    return -1;
}

// Decodes `src` once and remembers the answer, whichever of the two it is.
// Null while a fetch is still running: nothing is written down then.
static ImageCacheSlot* ImageSlotFor(const ImageLookup& cx, Str src) {
    if (!src.s || len(src) <= 0) {
        return nullptr;
    }
    Vec<ImageCacheSlot>* slots = ResourceSlots(cx);
    int hitIx = ImageSlotFind(slots, src);
    if (hitIx >= 0 && !(*slots)[hitIx].pending) {
        return &(*slots)[hitIx];
    }

    Vec<uint8_t> owned;
    const uint8_t* borrowed = nullptr;
    int borrowedLen = 0;
    SrcBytes got = BytesForSrc(src, &owned, &borrowed, &borrowedLen);
    slots = ResourceSlots(cx);
    hitIx = ImageSlotFind(slots, src);
    if (got == SrcBytes::Pending) {
        if (hitIx < 0) {
            ImageCacheSlot fresh = {};
            fresh.src = StrDup(src);
            fresh.tried = true;
            fresh.pending = true;
            fresh.loadingAt = TimeNow();
            VecAppend(*slots, fresh);
            hitIx = slots->len - 1;
        }
        return &(*slots)[hitIx];
    }
    const uint8_t* bytes = len(owned) > 0 ? owned.els : borrowed;
    int n = len(owned) > 0 ? len(owned) : borrowedLen;

    RenderImage* img = nullptr;
    uint8_t* ops = nullptr;
    int opsLen = 0;
    if (got == SrcBytes::Yes && bytes && n > 0) {
        if (!cx.pa && !LooksLikeSvg(bytes, n)) {
            // ImageVectorForSrc probes a one-dimension image before layout so
            // an SVG can supply its aspect ratio. A bitmap is not a failed
            // vector decode: leave it uncached so ImageForSrc can hand the
            // same bytes to the platform decoder later in this frame. Caching
            // the empty answer here made every remote <img width="..."> stay
            // blank after its fetch completed.
            VecReset(owned);
            return nullptr;
        }
        DecodeImageBytes(cx, bytes, n, &img, &ops, &opsLen);
    }

    slots = ResourceSlots(cx);
    hitIx = ImageSlotFind(slots, src);
    if (hitIx < 0) {
        ImageCacheSlot fresh = {};
        fresh.src = StrDup(src);
        fresh.tried = true;
        VecAppend(*slots, fresh);
        hitIx = slots->len - 1;
    }
    ImageCacheSlot* slot = &(*slots)[hitIx];
    slot->img = img;
    slot->ops = ops;
    slot->opsLen = opsLen;
    slot->pending = false;
    if (img && RenderImageStatusGet(img) == RenderImageStatus::Loading &&
        slot->loadingAt == 0) {
        slot->loadingAt = TimeNow();
    }
    if (HttpUrlIsRemote(src)) {
        HttpFetchDrop(src);
    }
    return slot;
}

static EncodedImageSlot* EncodedSlotFor(const ImageLookup& cx,
                                        const ImageSource& source) {
    if (!source.bytes || source.bytesLen <= 0) {
        return nullptr;
    }
    ImageStore* store = StoreOf(cx.app);
    uint64_t hash = ImageBytesHash(source.bytes, source.bytesLen);
    for (int i = 0; i < store->encoded.len; i++) {
        EncodedImageSlot* slot = &store->encoded[i];
        if (slot->tried && slot->hash == hash &&
            slot->bytesLen == source.bytesLen) {
            return slot;
        }
    }
    if (!cx.pa && !LooksLikeSvg(source.bytes, source.bytesLen)) {
        return nullptr;
    }
    EncodedImageSlot fresh = {};
    fresh.hash = hash;
    fresh.bytesLen = source.bytesLen;
    fresh.tried = true;
    DecodeImageBytes(cx, source.bytes, source.bytesLen, &fresh.img, &fresh.ops,
                     &fresh.opsLen);
    VecAppend(store->encoded, fresh);
    return &store->encoded[store->encoded.len - 1];
}

static uint64_t ImageSourceKey(const ImageSource& source) {
    uint64_t key = ((uint64_t)source.kind + 1) * 0x9e3779b97f4a7c15ull;
    switch (source.kind) {
        case ImageSourceKind::Resource:
            key ^= ImageBytesHash((const uint8_t*)source.resource.s,
                                  source.resource.len);
            break;
        case ImageSourceKind::Render:
            key ^= RenderImageGeneration(source.render);
            break;
        case ImageSourceKind::Image:
            key ^= ImageBytesHash(source.bytes, source.bytesLen);
            break;
        case ImageSourceKind::Custom:
            key ^= (uint64_t)(uintptr_t)source.loader;
            key ^= (uint64_t)(uintptr_t)source.user * 0xc2b2ae3d27d4eb4full;
            break;
    }
    return key ? key : 1;
}

static ImageClock* ClockFor(ImageClock* clocks, uint64_t key) {
    int empty = -1;
    for (int i = 0; i < kImageClockSlots; i++) {
        if (clocks[i].key == key) {
            return &clocks[i];
        }
        if (!clocks[i].key && empty < 0) {
            empty = i;
        }
    }
    int at = empty >= 0 ? empty : (int)(key % kImageClockSlots);
    clocks[at] = {};
    clocks[at].key = key;
    return &clocks[at];
}

ImageLoadState ImageSrcState(const ImageLookup& cx, Str src,
                             double* loadingSeconds) {
    if (loadingSeconds) {
        *loadingSeconds = 0;
    }
    if (!src.s || len(src) <= 0) {
        return ImageLoadState::Failed;
    }
    ImageCacheSlot* s = ImageSlotFor(cx, src);
    if (!s || s->pending) {
        if (s && loadingSeconds && s->loadingAt > 0) {
            *loadingSeconds = TimeNow() - s->loadingAt;
        }
        return ImageLoadState::Loading;
    }
    if (s->ops) {
        return ImageLoadState::Ready;
    }
    if (!s->img) {
        return ImageLoadState::Failed;
    }
    RenderImageStatus status = RenderImageStatusGet(s->img);
    if (status == RenderImageStatus::Loading) {
        if (loadingSeconds && s->loadingAt > 0) {
            *loadingSeconds = TimeNow() - s->loadingAt;
        }
        return ImageLoadState::Loading;
    }
    if (status == RenderImageStatus::Failed) {
        RenderImageRelease(s->img);
        s->img = nullptr;
        return ImageLoadState::Failed;
    }
    s->loadingAt = 0;
    return ImageLoadState::Ready;
}

ImageLoadState ImageSrcState(PaintApp* pa, Str src, double* loadingSeconds) {
    return ImageSrcState(ImageLookup::Of(pa), src, loadingSeconds);
}

ImageLoadState ImageSourceState(const ImageLookup& cx,
                                const ImageSource& source,
                                double* loadingSeconds) {
    if (loadingSeconds) {
        *loadingSeconds = 0;
    }
    ImageLoadState state = ImageLoadState::Failed;
    switch (source.kind) {
        case ImageSourceKind::Resource:
            state = ImageSrcState(cx, source.resource, nullptr);
            break;
        case ImageSourceKind::Render:
            if (source.render) {
                RenderImageStatus status = RenderImageStatusGet(source.render);
                state = status == RenderImageStatus::Ready
                            ? ImageLoadState::Ready
                            : (status == RenderImageStatus::Loading
                                   ? ImageLoadState::Loading
                                   : ImageLoadState::Failed);
            }
            break;
        case ImageSourceKind::Image: {
            EncodedImageSlot* slot = EncodedSlotFor(cx, source);
            if (slot && slot->ops) {
                state = ImageLoadState::Ready;
            } else if (slot && slot->img) {
                RenderImageStatus status = RenderImageStatusGet(slot->img);
                state = status == RenderImageStatus::Ready
                            ? ImageLoadState::Ready
                            : (status == RenderImageStatus::Loading
                                   ? ImageLoadState::Loading
                                   : ImageLoadState::Failed);
            }
            break;
        }
        case ImageSourceKind::Custom: {
            RenderImage* image = nullptr;
            if (source.loader) {
                state = source.loader(cx.pa, source.user, &image);
                if (state == ImageLoadState::Ready && !image) {
                    state = ImageLoadState::Failed;
                }
            }
            break;
        }
    }
    uint64_t key = ImageSourceKey(source);
    ImageClock* clock = ClockFor(gLoadingClocks, key);
    if (state == ImageLoadState::Loading) {
        double now = TimeNow();
        if (clock->at <= 0) {
            clock->at = now;
        }
        if (loadingSeconds) {
            *loadingSeconds = now - clock->at;
        }
    } else {
        *clock = {};
    }
    return state;
}

ImageLoadState ImageSourceState(PaintApp* pa, const ImageSource& source,
                                double* loadingSeconds) {
    return ImageSourceState(ImageLookup::Of(pa), source, loadingSeconds);
}

int ImageFrameIndex(RenderImage* image, bool reducedMotion,
                    bool* wantsAnimation) {
    if (wantsAnimation) {
        *wantsAnimation = false;
    }
    int count = RenderImageFrameCount(image);
    if (count <= 1) {
        return 0;
    }
    ImageClock* clock =
        ClockFor(gAnimationClocks, RenderImageGeneration(image));
    if (clock->frameIndex < 0 || clock->frameIndex >= count) {
        clock->frameIndex = 0;
    }
    if (reducedMotion) {
        clock->at = 0;
        return clock->frameIndex;
    }
    if (wantsAnimation) {
        *wantsAnimation = true;
    }
    double now = TimeNow();
    if (clock->at <= 0) {
        clock->at = now;
        return clock->frameIndex;
    }
    double elapsed = now - clock->at;
    for (int advances = 0; advances < count * 2; advances++) {
        int delay = RenderImageFrameDurationMs(image, clock->frameIndex);
        double seconds = (delay > 0 ? delay : 100) / 1000.0;
        if (elapsed < seconds) {
            break;
        }
        elapsed -= seconds;
        clock->at += seconds;
        clock->frameIndex = (clock->frameIndex + 1) % count;
    }
    return clock->frameIndex;
}

RenderImage* ImageForSrc(const ImageLookup& cx, Str src) {
    if (!cx.pa) {
        return nullptr;
    }
    ImageCacheSlot* s = ImageSlotFor(cx, src);
    if (!s || !s->img) {
        return nullptr;
    }
    RenderImageStatus status = RenderImageStatusGet(s->img);
    if (status == RenderImageStatus::Failed) {
        RenderImageRelease(s->img);
        s->img = nullptr;
        return nullptr;
    }
    return status == RenderImageStatus::Ready ? s->img : nullptr;
}

RenderImage* ImageForSrc(PaintApp* pa, Str src) {
    return ImageForSrc(ImageLookup::Of(pa), src);
}

RenderImage* ImageForSource(const ImageLookup& cx, const ImageSource& source) {
    if (!cx.pa) {
        return nullptr;
    }
    if (source.kind == ImageSourceKind::Resource) {
        return ImageForSrc(cx, source.resource);
    }
    RenderImage* image = nullptr;
    if (source.kind == ImageSourceKind::Render) {
        image = source.render;
    } else if (source.kind == ImageSourceKind::Image) {
        EncodedImageSlot* slot = EncodedSlotFor(cx, source);
        image = slot ? slot->img : nullptr;
    } else if (source.kind == ImageSourceKind::Custom && source.loader) {
        if (source
                .loader(cx.pa, source.user, &image) != ImageLoadState::Ready) {
            image = nullptr;
        }
    }
    return image && RenderImageStatusGet(image) == RenderImageStatus::Ready
               ? image
               : nullptr;
}

RenderImage* ImageForSource(PaintApp* pa, const ImageSource& source) {
    return ImageForSource(ImageLookup::Of(pa), source);
}

const uint8_t* ImageVectorForSrc(const ImageLookup& cx, Str src, int* lenOut) {
    if (lenOut) {
        *lenOut = 0;
    }
    // A local `.svg` already has a home: svg.cpp's own cache, which is where
    // every icon in the tree comes from and which knows the compiled-in
    // table. Only a src that is not an asset needs the slot above.
    Str asset = ImageAssetFor(GetTempArena(), src);
    if (asset.s && len(asset) > 4 &&
        StrEqI(Str(asset.s + len(asset) - 4, 4), ".svg")) {
        return SvgDrawOpsFor(asset, lenOut);
    }
    ImageCacheSlot* s = ImageSlotFor(cx, src);
    if (!s || !s->ops) {
        return nullptr;
    }
    if (lenOut) {
        *lenOut = s->opsLen;
    }
    return s->ops;
}

const uint8_t* ImageVectorForSrc(Str src, int* lenOut) {
    return ImageVectorForSrc(ImageLookup{}, src, lenOut);
}

const uint8_t* ImageVectorForSource(const ImageLookup& cx,
                                    const ImageSource& source, int* lenOut) {
    if (lenOut) {
        *lenOut = 0;
    }
    if (source.kind == ImageSourceKind::Resource) {
        return ImageVectorForSrc(cx, source.resource, lenOut);
    }
    if (source.kind != ImageSourceKind::Image) {
        return nullptr;
    }
    EncodedImageSlot* slot = EncodedSlotFor(cx, source);
    if (!slot || !slot->ops) {
        return nullptr;
    }
    if (lenOut) {
        *lenOut = slot->opsLen;
    }
    return slot->ops;
}

const uint8_t* ImageVectorForSource(PaintApp* pa, const ImageSource& source,
                                    int* lenOut) {
    return ImageVectorForSource(ImageLookup::Of(pa), source, lenOut);
}

} // namespace gpui
