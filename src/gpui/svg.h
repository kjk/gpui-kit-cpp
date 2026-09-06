#ifndef GPUI_GPUI_SVG_H_
#define GPUI_GPUI_SVG_H_
#include "gpui/gpui.h"
#include "gpui/drawops.h"

// Lucide-style SVG (viewBox, path/rect/polyline/line/circle/polygon).
// Stroke uses `color`; fill="none" icons are stroked only.
//
// Nothing here rasterizes a path itself: a file is converted once into the
// `drawops.h` byte stream and `ExecuteDrawOps` draws it. The icons under
// `assets/icons` are converted at build time instead — `asset_icons.h` — so
// no `.svg` under that directory is read or parsed while the app runs.

namespace gpui {

// `turns` rotates the icon clockwise about the middle of its box, 1 being a
// whole turn — Transformation::rotate(percentage(..)), applied as the path is
// built rather than by the backend.
// The viewBox of an asset, so a caller that knows one dimension can work out
// the other. False when the file is not there or is not an SVG this reader
// understands.
bool SvgViewBox(Str assetPath, Size* out);

bool SvgDraw(PaintCtx* ctx, Str assetPath, float x, float y, float size,
             Rgba color, float turns = 0);
// The same for SVG source in hand rather than an asset path — `Icon::data`,
// an icon embedded in the binary without registering an asset. Converted
// once and kept by content, the way a path's file is kept by name.
bool SvgDrawXml(PaintCtx* ctx, Str xml, float x, float y, float size,
                Rgba color, float turns = 0);

// The same, for a byte stream already in hand rather than an asset path —
// what a picture fetched over the network resolves to (gpui/image.h) — and
// into a rectangle rather than a square. An icon is square and the two are
// the same for it; a picture is whatever shape its viewBox says, and the box
// an image element was laid out into already has that shape.
bool SvgDrawOps(PaintCtx* ctx, const uint8_t* ops, int len, float x, float y,
                float w, float h, Rgba color, float turns = 0,
                bool grayscale = false);

// The same icon, drawn into a square of pixels instead of onto a window:
// `outBgra` takes px * px * 4 bytes of premultiplied BGRA, top down. What a
// menu the OS draws needs, since it wants a bitmap of the icon rather than
// something that can draw one.
bool SvgRasterize(PaintApp* pa, Str assetPath, int px, Rgba color,
                  uint8_t* outBgra);
bool SvgRasterizeXml(PaintApp* pa, Str xml, int px, Rgba color,
                     uint8_t* outBgra);

// One SVG file, as the byte stream `drawops.h` describes. This is the reader
// `cmd/svg-to-bytecode.ts` mirrors in TypeScript to build `asset_icons.cpp`,
// and the one an application's own `.svg` goes through at load time.
bool SvgToDrawOps(Str xml, DrawOpsBuilder* out);

// The bytecode an asset path draws: the compiled-in table for "icons/*.svg",
// otherwise the file, read and converted once and then kept. Null if there is
// no such asset. The bytes belong to the cache — copy them to keep them.
const uint8_t* SvgDrawOpsFor(Str assetPath, int* lenOut);
// The bytecode SVG source draws, converted once and kept under a hash of the
// bytes — so an icon embedded with `Icon::data` costs a parse the first time
// it is drawn and a lookup after. Null when the source is not an SVG this
// reader understands. The bytes belong to the cache.
const uint8_t* SvgDrawOpsForXml(Str xml, int* lenOut);
// Drop draw operations built from application assets. Compiled-in icon data
// is borrowed and is simply forgotten. AppFree calls this through the image
// cache teardown.
void SvgCacheClear();

// IconName -> "icons/<kebab>.svg" (same mapping as gpui-kit's
// icon_named!).
Str IconNamePath(IconName name);
} // namespace gpui
#endif // GPUI_GPUI_SVG_H_
