/* The halves of the paint backends that do not depend on the OS; see
   paint_backend.h. */

#include "gpui/paint_backend.h"
#include "gpui/scene.h"

namespace gpui {

static uint64_t gNextPaintResourceGeneration = 1;

uint64_t PaintResourceGenerationNew() {
    uint64_t id = gNextPaintResourceGeneration++;
    if (id == 0) {
        id = gNextPaintResourceGeneration++;
    }
    return id;
}

bool PaintSceneFinish(PaintCtx* ctx, bool replayUnchanged) {
    if (!SceneOn() || !scene::Recording()) {
        return true;
    }
    Bounds damage = {};
    bool draw = scene::FrameEnd(ctx, &damage);
    if (draw) {
        if (!scene::ReplayScroll(ctx)) {
            scene::Replay(ctx, &damage);
        }
    } else if (replayUnchanged) {
        scene::Replay(ctx, nullptr);
    }
    return draw;
}

void PaintCopyBgraRows(uint8_t* dst, size_t dstStride, const uint8_t* src,
                       size_t srcStride, int w, int h) {
    for (int y = 0; y < h; y++) {
        memcpy(dst + (size_t)y * dstStride, src + (size_t)y * srcStride,
               (size_t)w * 4);
    }
}

void PaintGrayBgraRows(uint8_t* dst, size_t dstStride, const uint8_t* src,
                       size_t srcStride, int w, int h) {
    for (int y = 0; y < h; y++) {
        const uint8_t* s = src + (size_t)y * srcStride;
        uint8_t* d = dst + (size_t)y * dstStride;
        for (int x = 0; x < w; x++, s += 4, d += 4) {
            uint8_t v = (uint8_t)(((uint32_t)s[2] * 54 + (uint32_t)s[1] * 183 +
                                   (uint32_t)s[0] * 19) >>
                                  8);
            uint8_t a = s[3];
            d[0] = v;
            d[1] = v;
            d[2] = v;
            d[3] = a;
        }
    }
}

} // namespace gpui
