#ifndef GPUI_GPUI_PAINT_BACKEND_H_
#define GPUI_GPUI_PAINT_BACKEND_H_
/* What the paint_*.cpp backends share behind Paint.h: the generation counter
   their resources are stamped with, the end-of-frame scene replay, and the
   bookkeeping of a RenderImage that holds decoded frames.

   None of it is Paint.h API. A mobile host adapter supplies Paint.h itself and
   may use these or not; defining a Paint.h function here would collide with
   its own. */

#include "gpui/paint.h"

namespace gpui {

// A process-unique, never-zero stamp for a paint resource (image, text
// layout, target), so a cache keyed on it cannot confuse a freed resource with
// a new one allocated at the same address.
uint64_t PaintResourceGenerationNew();

// Close the scene recording and draw what it collected: a pure scroll by
// shifting pixels when the backend can, otherwise the damaged area. Returns
// false when the frame was unchanged. `replayUnchanged` draws the whole scene
// anyway on an unchanged frame, for a target whose previous contents are not
// kept (an AppKit view may already have cleared it, and an identical frame
// left unpainted would flash).
bool PaintSceneFinish(PaintCtx* ctx, bool replayUnchanged);

// Copy h rows of w BGRA pixels between buffers whose rows are dstStride and
// srcStride bytes apart, leaving any row padding of the destination alone.
void PaintCopyBgraRows(uint8_t* dst, size_t dstStride, const uint8_t* src,
                       size_t srcStride, int w, int h);

// The grayscale copy of h rows of w premultiplied BGRA pixels an image draws
// with when asked for grayscale: Rec. 709 luma in 8.8 fixed point, alpha
// kept. dst may be src.
void PaintGrayBgraRows(uint8_t* dst, size_t dstStride, const uint8_t* src,
                       size_t srcStride, int w, int h);

// The part of a RenderImage every frame-holding backend keeps the same way: a
// refcount (Rust's Arc<RenderImage>), a generation, a load status and the
// decoded frames. A backend writes `struct RenderImage : RenderImageFrames<
// MyFrame> {};`; a Frame has `w`, `h` and `durationMs`.
template <typename Frame>
struct RenderImageFrames {
    int refs = 1;
    uint64_t generation = 0;
    RenderImageStatus status = RenderImageStatus::Ready;
    Vec<Frame> frames;
};

// A frame index a caller may hand in, made one that exists: anything out of
// range is the first frame. -1 when the image has no frames.
template <typename Img>
int RenderImageFrameIndex(const Img* img, int frameIndex) {
    if (!img || img->frames.len <= 0) {
        return -1;
    }
    if (frameIndex < 0 || frameIndex >= img->frames.len) {
        return 0;
    }
    return frameIndex;
}

template <typename Img>
Img* RenderImageAlloc(RenderImageStatus status) {
    auto* img = new Img();
    img->generation = PaintResourceGenerationNew();
    img->status = status;
    return img;
}

// A Ready image holding one frame.
template <typename Img, typename Frame>
Img* RenderImageWithFrame(const Frame& frame) {
    Img* img = RenderImageAlloc<Img>(RenderImageStatus::Ready);
    VecAppend(img->frames, frame);
    return img;
}

template <typename Img, typename Frame>
void RenderImageFramesFree(Img* img, void (*frameFree)(Frame*)) {
    for (int i = 0; i < img->frames.len; i++) {
        frameFree(&img->frames[i]);
    }
    VecReset(img->frames);
}

// RenderImageComplete: a Loading handle takes over the frames of what was
// decoded for it (or turns Failed when nothing was), and the decoded image is
// released.
template <typename Img, typename Frame>
void RenderImageAdoptFrames(Img* img, Img* decoded, void (*frameFree)(Frame*)) {
    if (!img) {
        if (decoded) {
            RenderImageRelease(decoded);
        }
        return;
    }
    if (decoded && decoded->frames.len > 0) {
        RenderImageFramesFree(img, frameFree);
        img->frames.els = decoded->frames.els;
        img->frames.len = decoded->frames.len;
        img->frames.cap = decoded->frames.cap;
        decoded->frames.els = nullptr;
        decoded->frames.len = 0;
        decoded->frames.cap = 0;
        img->status = RenderImageStatus::Ready;
    } else {
        img->status = RenderImageStatus::Failed;
    }
    if (decoded) {
        RenderImageRelease(decoded);
    }
}

// RenderImageRelease's count: true when this was the last reference and the
// caller frees the image.
template <typename Img>
bool RenderImageUnref(Img* img) {
    return img && --img->refs == 0;
}

template <typename Img>
Size RenderImageFrameSizePx(const Img* img, int frameIndex) {
    int i = RenderImageFrameIndex(img, frameIndex);
    if (i < 0) {
        return {};
    }
    return {(float)img->frames[i].w, (float)img->frames[i].h};
}

template <typename Img>
int RenderImageFrameDuration(const Img* img, int frameIndex) {
    int i = RenderImageFrameIndex(img, frameIndex);
    return i < 0 ? 0 : img->frames[i].durationMs;
}

} // namespace gpui
#endif // GPUI_GPUI_PAINT_BACKEND_H_
