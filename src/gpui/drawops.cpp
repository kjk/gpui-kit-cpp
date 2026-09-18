#include "gpui/drawops.h"
#include "gpui/paint.h"

#include <math.h>

namespace gpui {

// ─── reading ──────────────────────────────────────────────────────────────
//
// A cursor over the byte stream. Every read is bounds-checked and a short
// buffer stops the walk rather than running off the end: the bytes may be a
// generated table, but they may also be an `.svg` the application shipped.

struct OpReader {
    const uint8_t* p = nullptr;
    const uint8_t* end = nullptr;
    bool bad = false;

    bool Has(int n) { return !bad && p + n <= end; }

    uint16_t U16() {
        if (!Has(2)) {
            bad = true;
            return kOpEnd;
        }
        uint16_t v;
        memcpy(&v, p, 2);
        p += 2;
        return v;
    }

    float F() {
        if (!Has(4)) {
            bad = true;
            return 0;
        }
        float v;
        memcpy(&v, p, 4);
        p += 4;
        return v;
    }

    uint32_t U32() {
        if (!Has(4)) {
            bad = true;
            return 0;
        }
        uint32_t v;
        memcpy(&v, p, 4);
        p += 4;
        return v;
    }

    // A run of raw bytes, in place: kOpText's string is read where it lies
    // rather than copied, since it is drawn before the walk moves on.
    const uint8_t* Bytes(int n) {
        if (n < 0 || !Has(n)) {
            bad = true;
            return nullptr;
        }
        const uint8_t* v = p;
        p += n;
        return v;
    }
};

// Little-endian on all three targets we build for, and the generator writes
// little-endian bytes; nothing here byte-swaps.
static Rgba ColorFromU32(uint32_t v) {
    return Rgba{(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8),
                (uint8_t)v};
}

static uint32_t ColorToU32(Rgba c) {
    return ((uint32_t)c.r << 24) | ((uint32_t)c.g << 16) |
           ((uint32_t)c.b << 8) | (uint32_t)c.a;
}

static Rgba Grayscale(Rgba c) {
    uint8_t gray = (uint8_t)(((uint32_t)c.r * 54 + (uint32_t)c.g * 183 +
                              (uint32_t)c.b * 19) >>
                             8);
    return {gray, gray, gray, c.a};
}

// ─── the machine ──────────────────────────────────────────────────────────

// Everything the walk carries: where the viewBox lands, the colour and stroke
// in force, and the path being built.
struct DrawOpsExec {
    PaintCtx* ctx = nullptr;
    DrawOpsTarget t;

    // viewBox -> destination box. Applied as the path is built, because a
    // canvas transform would otherwise have to be part of the backend API for
    // the sake of this one caller.
    float vbX = 0, vbY = 0, vbW = 24, vbH = 24;
    float sx = 1, sy = 1;
    // cos/sin of the rotation and the point it turns about.
    float ca = 1, sa = 0, mx = 0, my = 0;

    float strokeW = 2;
    Rgba color = {};

    Path* path = nullptr;

    void Rescale() {
        sx = t.w / (vbW > 0 ? vbW : 24.f);
        sy = t.h / (vbH > 0 ? vbH : 24.f);
    }

    float TX(float u, float v) const {
        float px = t.x + (u - vbX) * sx;
        if (t.turns == 0) {
            return px;
        }
        float py = t.y + (v - vbY) * sy;
        return mx + (px - mx) * ca - (py - my) * sa;
    }

    float TY(float u, float v) const {
        float py = t.y + (v - vbY) * sy;
        if (t.turns == 0) {
            return py;
        }
        float px = t.x + (u - vbX) * sx;
        return my + (px - mx) * sa + (py - my) * ca;
    }

    // The authored stroke width is in viewBox units and scales with the box.
    float Stroke() const { return strokeW * (sx + sy) * 0.5f; }

    Path* Cur() {
        if (!path) {
            path = PathNew(ctx, true);
        }
        return path;
    }

    void MoveTo(float u, float v) {
        Path* p = Cur();
        if (p) {
            PathMoveTo(p, TX(u, v), TY(u, v));
        }
    }
    void LineTo(float u, float v) {
        Path* p = Cur();
        if (p) {
            PathLineTo(p, TX(u, v), TY(u, v));
        }
    }
    void CubicTo(float u1, float v1, float u2, float v2, float u, float v) {
        Path* p = Cur();
        if (p) {
            PathCubicTo(p, TX(u1, v1), TY(u1, v1), TX(u2, v2), TY(u2, v2),
                        TX(u, v), TY(u, v));
        }
    }
    void Close() {
        if (path) {
            PathClose(path);
        }
    }

    // kOpText. The font size is in viewBox units and scales with the box the
    // way a stroke width does; the glyphs themselves are left upright even in
    // a turned drawing, because nothing that names a rotation also names text.
    void Text(Str s, float u, float v, float size, float targetW,
              uint32_t flags) {
        float px = size * (sx + sy) * 0.5f;
        if (!s.s || len(s) <= 0 || px < 1.f) {
            return;
        }
        int weight = (flags & kTextBold) ? kFontWeightBold : kFontWeightNormal;
        Size sz = MeasureText(ctx, s, px, 0, false, weight, 0);
        // textLength asks for the run to be set to exactly that width. There
        // is no horizontal scale in paint.h, so what happens instead is that
        // a run too wide for the width it was drawn to fit is set smaller
        // until it fits, and a narrower one is left where its anchor puts it.
        // A badge is authored against Verdana and this is not Verdana, so the
        // two were never going to agree to the pixel; fitting the plate is
        // what keeps the label inside the plate it belongs to.
        if (targetW > 0 && sz.w > 0) {
            float want = targetW * sx;
            if (want > 0 && sz.w > want) {
                px *= want / sz.w;
                if (px < 1.f) {
                    return;
                }
                sz = MeasureText(ctx, s, px, 0, false, weight, 0);
            }
        }
        float ax = TX(u, v);
        float ay = TY(u, v);
        uint32_t anchor = flags & kTextAnchorMask;
        if (anchor == kTextAnchorMiddle) {
            ax -= sz.w * 0.5f;
        } else if (anchor == kTextAnchorEnd) {
            ax -= sz.w;
        }
        DrawTextBaseline(ctx, s, ax, ay, px, color, weight);
    }

    // Paint what has been built and start the next figure. Round caps and
    // joins, which is what every lucide icon asks for and what the shapes
    // built out of quarter turns want anyway.
    void EndPath(bool fill, bool stroke) {
        if (!path) {
            return;
        }
        if (fill) {
            PathFill(ctx, path, color);
        }
        if (stroke) {
            PathStroke(ctx, path, Stroke(), color, true);
        }
        PathFree(path);
        path = nullptr;
    }
};

// A rounded box as four quarter turns, in viewBox units. A circle is the same
// thing with the radius at half the side, which is how an `<ellipse>` and a
// `<circle>` get here.
static const float kKappa = 0.55228475f;

static void AddRoundRect(DrawOpsExec* e, float x, float y, float w, float h,
                         float rx, float ry) {
    if (rx < 0) {
        rx = 0;
    }
    if (ry < 0) {
        ry = 0;
    }
    if (rx > w * 0.5f) {
        rx = w * 0.5f;
    }
    if (ry > h * 0.5f) {
        ry = h * 0.5f;
    }
    if (rx <= 0.01f || ry <= 0.01f) {
        e->MoveTo(x, y);
        e->LineTo(x + w, y);
        e->LineTo(x + w, y + h);
        e->LineTo(x, y + h);
        e->Close();
        return;
    }
    float kx = rx * kKappa;
    float ky = ry * kKappa;
    float x1 = x + rx, x2 = x + w - rx;
    float y1 = y + ry, y2 = y + h - ry;
    e->MoveTo(x1, y);
    e->LineTo(x2, y);
    e->CubicTo(x2 + kx, y, x + w, y1 - ky, x + w, y1);
    e->LineTo(x + w, y2);
    e->CubicTo(x + w, y2 + ky, x2 + kx, y + h, x2, y + h);
    e->LineTo(x1, y + h);
    e->CubicTo(x1 - kx, y + h, x, y2 + ky, x, y2);
    e->LineTo(x, y1);
    e->CubicTo(x, y1 - ky, x1 - kx, y, x1, y);
    e->Close();
}

// A stretch of a circle, flattened. `PathArcTo` would be the shorter route,
// but it takes a radius and this transform can scale the two axes differently
// and turn the result — the points have to go through TX/TY one at a time.
static void AddArc(DrawOpsExec* e, float cx, float cy, float r, float a0,
                   float a1) {
    const float kStepDeg = 9.f;
    int steps = (int)(fabsf(a1 - a0) / kStepDeg) + 1;
    for (int i = 0; i <= steps; i++) {
        float t = a0 + (a1 - a0) * (float)i / (float)steps;
        float rad = t * kPi / 180.f;
        float u = cx + r * cosf(rad);
        float v = cy + r * sinf(rad);
        if (i == 0) {
            e->MoveTo(u, v);
        } else {
            e->LineTo(u, v);
        }
    }
}

bool ExecuteDrawOps(PaintCtx* ctx, const void* data, int dataLen,
                    const DrawOpsTarget& t) {
    if (!ctx || !ctx->rt || !data || dataLen < 2 || t.w <= 0 || t.h <= 0) {
        return false;
    }
    DrawOpsExec e;
    e.ctx = ctx;
    e.t = t;
    e.color = t.grayscale ? Grayscale(t.color) : t.color;
    e.Rescale();
    float ang = t.turns * 2.f * kPi;
    e.ca = t.turns != 0 ? cosf(ang) : 1.f;
    e.sa = t.turns != 0 ? sinf(ang) : 0.f;
    e.mx = t.x + t.w * 0.5f;
    e.my = t.y + t.h * 0.5f;

    OpReader r{(const uint8_t*)data, (const uint8_t*)data + dataLen, false};
    bool drew = false;
    while (r.Has(2)) {
        uint16_t op = r.U16();
        if (op == kOpEnd) {
            break;
        }
        switch (op) {
            case kOpViewBox: {
                float x = r.F(), y = r.F(), w = r.F(), h = r.F();
                e.vbX = x;
                e.vbY = y;
                e.vbW = w > 0 ? w : 24.f;
                e.vbH = h > 0 ? h : 24.f;
                e.Rescale();
                break;
            }
            case kOpStrokeWidth:
                e.strokeW = r.F();
                break;
            case kOpColor:
                // What the op names is the colour outright. The frame's own
                // opacity is applied under this, by the backend.
                e.color = ColorFromU32(r.U32());
                if (t.grayscale) {
                    e.color = Grayscale(e.color);
                }
                break;
            case kOpColorReset:
                e.color = t.grayscale ? Grayscale(t.color) : t.color;
                break;
            case kOpLine: {
                float x1 = r.F(), y1 = r.F(), x2 = r.F(), y2 = r.F();
                if (r.bad) {
                    break;
                }
                e.EndPath(false, true);
                e.MoveTo(x1, y1);
                e.LineTo(x2, y2);
                e.EndPath(false, true);
                drew = true;
                break;
            }
            case kOpRect:
            case kOpFillRect: {
                float x = r.F(), y = r.F(), w = r.F(), h = r.F(), rad = r.F();
                if (r.bad) {
                    break;
                }
                e.EndPath(false, true);
                AddRoundRect(&e, x, y, w, h, rad, rad);
                e.EndPath(op == kOpFillRect, op == kOpRect);
                drew = true;
                break;
            }
            case kOpEllipse:
            case kOpFillEllipse: {
                float cx = r.F(), cy = r.F(), rx = r.F(), ry = r.F();
                if (r.bad) {
                    break;
                }
                e.EndPath(false, true);
                AddRoundRect(&e, cx - rx, cy - ry, rx * 2, ry * 2, rx, ry);
                e.EndPath(op == kOpFillEllipse, op == kOpEllipse);
                drew = true;
                break;
            }
            case kOpArc: {
                float cx = r.F(), cy = r.F(), rad = r.F(), a0 = r.F(),
                      a1 = r.F();
                if (r.bad) {
                    break;
                }
                e.EndPath(false, true);
                AddArc(&e, cx, cy, rad, a0, a1);
                e.EndPath(false, true);
                drew = true;
                break;
            }
            case kOpMoveTo: {
                float x = r.F(), y = r.F();
                if (!r.bad) {
                    e.MoveTo(x, y);
                }
                break;
            }
            case kOpLineTo: {
                float x = r.F(), y = r.F();
                if (!r.bad) {
                    e.LineTo(x, y);
                }
                break;
            }
            case kOpCubicTo: {
                float x1 = r.F(), y1 = r.F(), x2 = r.F(), y2 = r.F(), x = r.F(),
                      y = r.F();
                if (!r.bad) {
                    e.CubicTo(x1, y1, x2, y2, x, y);
                }
                break;
            }
            case kOpClosePath:
                e.Close();
                break;
            case kOpFillPath:
                e.EndPath(true, false);
                drew = true;
                break;
            case kOpStrokePath:
                e.EndPath(false, true);
                drew = true;
                break;
            case kOpText: {
                float x = r.F(), y = r.F();
                float size = r.F(), textLen = r.F();
                uint32_t flags = r.U32();
                int n = (int)r.U16();
                const uint8_t* bytes = r.Bytes(n);
                if (r.bad || !bytes) {
                    break;
                }
                e.EndPath(false, true);
                e.Text(Str((const char*)bytes, n), x, y, size, textLen, flags);
                drew = true;
                break;
            }
            case kOpFillStrokePath:
                e.EndPath(true, true);
                drew = true;
                break;
            default:
                // An opcode from a later encoder than this reader. There is
                // no length byte to skip it by, so the walk stops rather than
                // reading its arguments as ops.
                r.bad = true;
                break;
        }
        if (r.bad) {
            break;
        }
    }
    // A stream that built a path and never said what to do with it is stroked,
    // which is what an icon whose last op was cut off would have wanted.
    e.EndPath(false, true);
    return drew;
}

bool DrawOpsViewBox(const void* data, int dataLen, Size* out) {
    if (!data || dataLen < 2 || !out) {
        return false;
    }
    OpReader r{(const uint8_t*)data, (const uint8_t*)data + dataLen, false};
    // The viewBox, if it is there, is the first op; a drawing without one is
    // read in the 24x24 lucide box.
    out->w = 24;
    out->h = 24;
    uint16_t op = r.U16();
    if (op == kOpViewBox) {
        r.F();
        r.F();
        float w = r.F();
        float h = r.F();
        if (r.bad) {
            return false;
        }
        out->w = w > 0 ? w : 24.f;
        out->h = h > 0 ? h : 24.f;
    }
    return true;
}

// ─── writing ──────────────────────────────────────────────────────────────

void DrawOpsBuilder::Op(DrawOp op) {
    uint16_t v = (uint16_t)op;
    uint8_t* p = VecAppendBlanks(data, 2);
    if (p) {
        memcpy(p, &v, 2);
    }
}

void DrawOpsBuilder::F2(float a, float b) {
    uint8_t* p = VecAppendBlanks(data, 8);
    if (p) {
        memcpy(p, &a, 4);
        memcpy(p + 4, &b, 4);
    }
}

void DrawOpsBuilder::U32(uint32_t v) {
    uint8_t* p = VecAppendBlanks(data, 4);
    if (p) {
        memcpy(p, &v, 4);
    }
}

void DrawOpsBuilder::ViewBox(float x, float y, float w, float h) {
    // The view box is always the first thing written, so this is where the
    // buffer's size is decided. Half of `assets/icons` encodes to under 224
    // bytes and nine in ten to under 496 (`bun cmd/vec-log.ts tests`), so a
    // whole icon fits in one allocation instead of the seven doublings from
    // eight bytes it took to reach that.
    VecReserve(data, 512);
    Op(kOpViewBox);
    F2(x, y);
    F2(w, h);
}

void DrawOpsBuilder::StrokeWidth(float w) {
    // The one argument in the format that is not half of a pair, so it goes
    // in with its opcode rather than asking for a single-float write.
    uint16_t op = kOpStrokeWidth;
    uint8_t* p = VecAppendBlanks(data, 6);
    if (p) {
        p[0] = (uint8_t)op;
        p[1] = (uint8_t)(op >> 8);
        memcpy(p + 2, &w, 4);
    }
}

void DrawOpsBuilder::Color(Rgba c) {
    Op(kOpColor);
    U32(ColorToU32(c));
}

void DrawOpsBuilder::ColorReset() {
    Op(kOpColorReset);
}

void DrawOpsBuilder::Line(float x1, float y1, float x2, float y2) {
    Op(kOpLine);
    F2(x1, y1);
    F2(x2, y2);
}

void DrawOpsBuilder::MoveTo(float x, float y) {
    Op(kOpMoveTo);
    F2(x, y);
}

void DrawOpsBuilder::LineTo(float x, float y) {
    Op(kOpLineTo);
    F2(x, y);
}

void DrawOpsBuilder::CubicTo(float x1, float y1, float x2, float y2, float x,
                             float y) {
    Op(kOpCubicTo);
    F2(x1, y1);
    F2(x2, y2);
    F2(x, y);
}

void DrawOpsBuilder::ClosePath() {
    Op(kOpClosePath);
}

void DrawOpsBuilder::Text(float x, float y, float size, float textLength,
                          uint32_t flags, Str s) {
    int n = len(s);
    if (!s.s || n <= 0) {
        return;
    }
    if (n > 0xffff) {
        n = 0xffff;
        // Back to a character boundary: a continuation byte is 10xxxxxx.
        while (n > 0 && ((uint8_t)s.s[n] & 0xc0) == 0x80) {
            n--;
        }
    }
    Op(kOpText);
    F2(x, y);
    F2(size, textLength);
    U32(flags);
    uint16_t len = (uint16_t)n;
    uint8_t* p = VecAppendBlanks(data, 2 + n);
    if (p) {
        memcpy(p, &len, 2);
        memcpy(p + 2, s.s, (size_t)n);
    }
}

void DrawOpsBuilder::End() {
    Op(kOpEnd);
}

} // namespace gpui
