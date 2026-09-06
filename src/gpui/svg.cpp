#include "gpui/svg.h"
#include "gpui/asset_icons.h"
#include "gpui/assets.h"
#include "gpui/drawops.h"
#include "gpui/paint.h"

#include <math.h>

namespace gpui {

// Reading an `.svg` and drawing one are two different jobs now. This file does
// the first: a Lucide-style file - viewBox, path/rect/polyline/line/circle/
// polygon - becomes the byte stream `drawops.h` describes, and
// `ExecuteDrawOps` is what paints it. `assets/icons` never comes through here
// at runtime: `cmd/svg-to-bytecode.ts` ran this same conversion ahead of time
// and its output is `asset_icons.cpp`, so the two have to keep agreeing.

enum SvgCmd : uint8_t {
    kMove = 0,
    kLine = 1,
    kCubic = 2,
    kClose = 3
};

struct SvgOp {
    uint8_t cmd = kMove;
    float x = 0, y = 0;
    float x1 = 0, y1 = 0;
    float x2 = 0, y2 = 0;
};

// One drawn element of the file - a <path>, a <rect>, a <circle> - and the run
// of ops it contributed. A Lucide icon says nothing about colour and every
// shape takes the caller's; a picture with colours of its own names them per
// shape, which is what a two-tone logo is.
struct SvgShape {
    int start = 0;
    int count = 0;
    bool hasFill = false;
    Rgba fill = {};
    // A shape may name the colour it is drawn *with* as well as the one it is
    // filled with, and the two are not the same question.
    bool hasStroke = false;
    Rgba stroke = {};
    // A <text> or <tspan> run rather than a path. It contributes no ops, so
    // `count` is zero and this is everything it draws. The string is a slice
    // of the file, which outlives the one conversion this struct exists for.
    bool isText = false;
    Str text = {};
    float tx = 0, ty = 0;
    float fontSize = 0;
    float textLength = 0;
    uint32_t textFlags = 0;
};

// A gradient the file declared, as the one colour this reader keeps of it:
// its first stop. There is no gradient fill in `drawops.h` -- a shape is one
// colour -- and the first stop is the same reduction `try_parse_theme_color`
// makes of a `linear-gradient(..)` in a theme file. A GitHub Actions badge
// paints both of its plates this way and would otherwise have no colour at
// all.
struct SvgGradient {
    Str id = {};
    Rgba color = {};
    bool hasColor = false;
};

// The file, read but not yet encoded. It lives for the length of one
// conversion; the bytes it turns into are what is kept.
struct SvgIcon {
    float vbX = 0, vbY = 0, vbW = 24, vbH = 24;
    float strokeW = 2;
    // What the root said to do with the one path. SVG's own defaults, which
    // are what GPUI's renderer applies: fill black, stroke none. So an icon
    // that names neither -- github.svg is the one here -- is filled and not
    // stroked, a Lucide icon says fill="none" stroke="currentColor" and is
    // stroked and not filled, and a solid variant (star-fill) says
    // currentColor for both and is both.
    bool filled = true;
    bool stroked = false;
    // Whether any shape named a colour. False is every Lucide icon, and the
    // whole file is then one path in the caller's colour, as it always was.
    bool hasOwnColors = false;
    // Whether any shape is a run of text. A file with one is a picture rather
    // than an icon whatever else it says, since the single-path encoding has
    // nowhere to put a string.
    bool hasText = false;
    Vec<SvgOp> ops;
    Vec<SvgShape> shapes;
    Vec<SvgGradient> gradients;
};

static void AddOp(SvgIcon* ic, SvgOp op) {
    VecAppend(ic->ops, op);
}

static void AddMove(SvgIcon* ic, float x, float y) {
    SvgOp o;
    o.cmd = kMove;
    o.x = x;
    o.y = y;
    AddOp(ic, o);
}
static void AddLine(SvgIcon* ic, float x, float y) {
    SvgOp o;
    o.cmd = kLine;
    o.x = x;
    o.y = y;
    AddOp(ic, o);
}
static void AddCubic(SvgIcon* ic, float x1, float y1, float x2, float y2,
                     float x, float y) {
    SvgOp o;
    o.cmd = kCubic;
    o.x1 = x1;
    o.y1 = y1;
    o.x2 = x2;
    o.y2 = y2;
    o.x = x;
    o.y = y;
    AddOp(ic, o);
}
static void AddClose(SvgIcon* ic) {
    SvgOp o;
    o.cmd = kClose;
    AddOp(ic, o);
}

// Four cubics, one per quadrant. AddRoundRect below cannot stand in for this:
// it takes one corner radius for both axes, so an <ellipse> drawn with it
// comes out a stadium. <circle> still goes through AddRoundRect, where it has
// always gone and where its bytes are what the compiled icon table holds.
static void AddEllipse(SvgIcon* ic, float cx, float cy, float rx, float ry) {
    if (rx <= 0 || ry <= 0) {
        return;
    }
    float kx = rx * 0.55228475f;
    float ky = ry * 0.55228475f;
    AddMove(ic, cx + rx, cy);
    AddCubic(ic, cx + rx, cy + ky, cx + kx, cy + ry, cx, cy + ry);
    AddCubic(ic, cx - kx, cy + ry, cx - rx, cy + ky, cx - rx, cy);
    AddCubic(ic, cx - rx, cy - ky, cx - kx, cy - ry, cx, cy - ry);
    AddCubic(ic, cx + kx, cy - ry, cx + rx, cy - ky, cx + rx, cy);
    AddClose(ic);
}

static void AddRoundRect(SvgIcon* ic, float x, float y, float w, float h,
                         float rx) {
    if (rx < 0) {
        rx = 0;
    }
    if (rx > w * 0.5f) {
        rx = w * 0.5f;
    }
    if (rx > h * 0.5f) {
        rx = h * 0.5f;
    }
    if (rx <= 0.01f) {
        AddMove(ic, x, y);
        AddLine(ic, x + w, y);
        AddLine(ic, x + w, y + h);
        AddLine(ic, x, y + h);
        AddClose(ic);
        return;
    }
    // Cubic kappa for quarter circle
    float k = rx * 0.55228475f;
    float x1 = x + rx, x2 = x + w - rx;
    float y1 = y + rx, y2 = y + h - rx;
    AddMove(ic, x1, y);
    AddLine(ic, x2, y);
    AddCubic(ic, x2 + k, y, x + w, y1 - k, x + w, y1);
    AddLine(ic, x + w, y2);
    AddCubic(ic, x + w, y2 + k, x2 + k, y + h, x2, y + h);
    AddLine(ic, x1, y + h);
    AddCubic(ic, x1 - k, y + h, x, y2 + k, x, y2);
    AddLine(ic, x, y1);
    AddCubic(ic, x, y1 - k, x1 - k, y, x1, y);
    AddClose(ic);
}

// ─── path d parser ────────────────────────────────────────────────────────

struct PathScan {
    const char* p;
    const char* end;
};

static void SkipWs(PathScan* s) {
    while (s->p < s->end && (*s->p == ' ' || *s->p == '\t' || *s->p == '\n' ||
                             *s->p == '\r' || *s->p == ',')) {
        s->p++;
    }
}

static bool ParseNum(PathScan* s, float* out) {
    SkipWs(s);
    if (s->p >= s->end) {
        return false;
    }
    char* endp = nullptr;
    float v = strtof(s->p, &endp);
    if (endp == s->p) {
        return false;
    }
    *out = v;
    s->p = endp;
    return true;
}

static float Angle(float ux, float uy, float vx, float vy) {
    float dot = ux * vx + uy * vy;
    float nu = sqrtf(ux * ux + uy * uy);
    float nv = sqrtf(vx * vx + vy * vy);
    float c = (nu > 0 && nv > 0) ? dot / (nu * nv) : 1;
    if (c < -1) {
        c = -1;
    }
    if (c > 1) {
        c = 1;
    }
    float a = acosf(c);
    if (ux * vy - uy * vx < 0) {
        a = -a;
    }
    return a;
}

static void AddArc(SvgIcon* ic, float x1, float y1, float rx, float ry,
                   float phiDeg, bool large, bool sweep, float x2, float y2) {
    rx = fabsf(rx);
    ry = fabsf(ry);
    if (rx < 1e-6f || ry < 1e-6f) {
        AddLine(ic, x2, y2);
        return;
    }
    float phi = phiDeg * kPi / 180.f;
    float cosP = cosf(phi);
    float sinP = sinf(phi);
    float dx = (x1 - x2) * 0.5f;
    float dy = (y1 - y2) * 0.5f;
    float x1p = cosP * dx + sinP * dy;
    float y1p = -sinP * dx + cosP * dy;
    float rx2 = rx * rx, ry2 = ry * ry;
    float x1p2 = x1p * x1p, y1p2 = y1p * y1p;
    float lam = x1p2 / rx2 + y1p2 / ry2;
    if (lam > 1) {
        float sc = sqrtf(lam);
        rx *= sc;
        ry *= sc;
        rx2 = rx * rx;
        ry2 = ry * ry;
    }
    float num = rx2 * ry2 - rx2 * y1p2 - ry2 * x1p2;
    float den = rx2 * y1p2 + ry2 * x1p2;
    float csq = (den > 0) ? num / den : 0;
    if (csq < 0) {
        csq = 0;
    }
    float c = sqrtf(csq);
    if (large == sweep) {
        c = -c;
    }
    float cxp = c * rx * y1p / ry;
    float cyp = c * -ry * x1p / rx;
    float cx = cosP * cxp - sinP * cyp + (x1 + x2) * 0.5f;
    float cy = sinP * cxp + cosP * cyp + (y1 + y2) * 0.5f;
    float theta1 = Angle(1, 0, (x1p - cxp) / rx, (y1p - cyp) / ry);
    float dtheta = Angle((x1p - cxp) / rx, (y1p - cyp) / ry, (-x1p - cxp) / rx,
                         (-y1p - cyp) / ry);
    if (!sweep && dtheta > 0) {
        dtheta -= 2 * kPi;
    }
    if (sweep && dtheta < 0) {
        dtheta += 2 * kPi;
    }
    int segs = (int)ceilf(fabsf(dtheta) / (kPi * 0.5f + 1e-6f));
    if (segs < 1) {
        segs = 1;
    }
    if (segs > 8) {
        segs = 8;
    }
    float dt = dtheta / (float)segs;
    for (int i = 0; i < segs; i++) {
        float t0 = theta1 + dt * (float)i;
        float t1 = t0 + dt;
        float e0x = rx * cosf(t0), e0y = ry * sinf(t0);
        float e1x = rx * cosf(t1), e1y = ry * sinf(t1);
        float q = tanf(dt * 0.5f);
        float alpha = sinf(dt) * (sqrtf(4 + 3 * q * q) - 1) / 3.f;
        float d0x = -rx * sinf(t0), d0y = ry * cosf(t0);
        float d1x = -rx * sinf(t1), d1y = ry * cosf(t1);
        float p0x = cx + cosP * e0x - sinP * e0y;
        float p0y = cy + sinP * e0x + cosP * e0y;
        (void)p0x;
        (void)p0y;
        float p1x = cx + cosP * e1x - sinP * e1y;
        float p1y = cy + sinP * e1x + cosP * e1y;
        float c1x =
            cx + cosP * (e0x + alpha * d0x) - sinP * (e0y + alpha * d0y);
        float c1y =
            cy + sinP * (e0x + alpha * d0x) + cosP * (e0y + alpha * d0y);
        float c2x =
            cx + cosP * (e1x - alpha * d1x) - sinP * (e1y - alpha * d1y);
        float c2y =
            cy + sinP * (e1x - alpha * d1x) + cosP * (e1y - alpha * d1y);
        AddCubic(ic, c1x, c1y, c2x, c2y, p1x, p1y);
    }
}

static void ParsePathD(SvgIcon* ic, Str d) {
    if (!d.s || d.len <= 0) {
        return;
    }
    PathScan s{d.s, d.s + d.len};
    char cmd = 0;
    float cx = 0, cy = 0, sx = 0, sy = 0;
    float pcx = 0, pcy = 0; // previous cubic control (for S)
    bool hasPrevC = false;
    while (s.p < s.end) {
        SkipWs(&s);
        if (s.p >= s.end) {
            break;
        }
        char c = *s.p;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
            cmd = c;
            s.p++;
        } else if (!cmd) {
            s.p++;
            continue;
        }
        bool rel = cmd >= 'a';
        char op = rel ? (char)(cmd - 32) : cmd;
        if (op == 'Z') {
            AddClose(ic);
            cx = sx;
            cy = sy;
            hasPrevC = false;
            continue;
        }
        if (op == 'M') {
            float x, y;
            if (!ParseNum(&s, &x) || !ParseNum(&s, &y)) {
                break;
            }
            if (rel) {
                x += cx;
                y += cy;
            }
            AddMove(ic, x, y);
            cx = sx = x;
            cy = sy = y;
            hasPrevC = false;
            // extra pairs are implicit L/l
            cmd = rel ? 'l' : 'L';
            continue;
        }
        if (op == 'L') {
            float x, y;
            if (!ParseNum(&s, &x) || !ParseNum(&s, &y)) {
                break;
            }
            if (rel) {
                x += cx;
                y += cy;
            }
            AddLine(ic, x, y);
            cx = x;
            cy = y;
            hasPrevC = false;
            continue;
        }
        if (op == 'H') {
            float x;
            if (!ParseNum(&s, &x)) {
                break;
            }
            if (rel) {
                x += cx;
            }
            AddLine(ic, x, cy);
            cx = x;
            hasPrevC = false;
            continue;
        }
        if (op == 'V') {
            float y;
            if (!ParseNum(&s, &y)) {
                break;
            }
            if (rel) {
                y += cy;
            }
            AddLine(ic, cx, y);
            cy = y;
            hasPrevC = false;
            continue;
        }
        if (op == 'C') {
            float x1, y1, x2, y2, x, y;
            if (!ParseNum(&s, &x1) || !ParseNum(&s, &y1) ||
                !ParseNum(&s, &x2) || !ParseNum(&s, &y2) || !ParseNum(&s, &x) ||
                !ParseNum(&s, &y)) {
                break;
            }
            if (rel) {
                x1 += cx;
                y1 += cy;
                x2 += cx;
                y2 += cy;
                x += cx;
                y += cy;
            }
            AddCubic(ic, x1, y1, x2, y2, x, y);
            pcx = x2;
            pcy = y2;
            hasPrevC = true;
            cx = x;
            cy = y;
            continue;
        }
        if (op == 'S') {
            float x2, y2, x, y;
            if (!ParseNum(&s, &x2) || !ParseNum(&s, &y2) || !ParseNum(&s, &x) ||
                !ParseNum(&s, &y)) {
                break;
            }
            if (rel) {
                x2 += cx;
                y2 += cy;
                x += cx;
                y += cy;
            }
            float x1 = hasPrevC ? (2 * cx - pcx) : cx;
            float y1 = hasPrevC ? (2 * cy - pcy) : cy;
            AddCubic(ic, x1, y1, x2, y2, x, y);
            pcx = x2;
            pcy = y2;
            hasPrevC = true;
            cx = x;
            cy = y;
            continue;
        }
        if (op == 'Q') {
            float x1, y1, x, y;
            if (!ParseNum(&s, &x1) || !ParseNum(&s, &y1) || !ParseNum(&s, &x) ||
                !ParseNum(&s, &y)) {
                break;
            }
            if (rel) {
                x1 += cx;
                y1 += cy;
                x += cx;
                y += cy;
            }
            // elevate quad to cubic
            float c1x = cx + 2.f / 3.f * (x1 - cx);
            float c1y = cy + 2.f / 3.f * (y1 - cy);
            float c2x = x + 2.f / 3.f * (x1 - x);
            float c2y = y + 2.f / 3.f * (y1 - y);
            AddCubic(ic, c1x, c1y, c2x, c2y, x, y);
            pcx = x1;
            pcy = y1;
            hasPrevC = true;
            cx = x;
            cy = y;
            continue;
        }
        if (op == 'T') {
            float x, y;
            if (!ParseNum(&s, &x) || !ParseNum(&s, &y)) {
                break;
            }
            if (rel) {
                x += cx;
                y += cy;
            }
            float x1 = hasPrevC ? (2 * cx - pcx) : cx;
            float y1 = hasPrevC ? (2 * cy - pcy) : cy;
            float c1x = cx + 2.f / 3.f * (x1 - cx);
            float c1y = cy + 2.f / 3.f * (y1 - cy);
            float c2x = x + 2.f / 3.f * (x1 - x);
            float c2y = y + 2.f / 3.f * (y1 - y);
            AddCubic(ic, c1x, c1y, c2x, c2y, x, y);
            pcx = x1;
            pcy = y1;
            hasPrevC = true;
            cx = x;
            cy = y;
            continue;
        }
        if (op == 'A') {
            float rx, ry, rot, x, y;
            float fA, fS;
            if (!ParseNum(&s, &rx) || !ParseNum(&s, &ry) ||
                !ParseNum(&s, &rot) || !ParseNum(&s, &fA) ||
                !ParseNum(&s, &fS) || !ParseNum(&s, &x) || !ParseNum(&s, &y)) {
                break;
            }
            if (rel) {
                x += cx;
                y += cy;
            }
            AddArc(ic, cx, cy, rx, ry, rot, fA != 0, fS != 0, x, y);
            cx = x;
            cy = y;
            hasPrevC = false;
            continue;
        }
        // unknown command
        s.p++;
    }
}

static void ParsePolyline(SvgIcon* ic, Str pts, bool close) {
    PathScan s{pts.s, pts.s + pts.len};
    bool first = true;
    float x, y;
    while (ParseNum(&s, &x) && ParseNum(&s, &y)) {
        if (first) {
            AddMove(ic, x, y);
            first = false;
        } else {
            AddLine(ic, x, y);
        }
    }
    if (close && !first) {
        AddClose(ic);
    }
}

// ─── tiny SVG tag scanner ─────────────────────────────────────────────────

static bool IsIdentChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '-' || c == '_';
}

// `name="value"` inside one tag's text. Matched only on a name boundary, so
// "fill" does not match "fill-rule". The temporary copy has no fixed bound: a
// window-chrome icon traced by a design tool can have a `d` attribute longer
// than two thousand characters.
static TempStr GetAttrTemp(Str tag, const char* name) {
    int nlen = (int)strlen(name);
    const char* p = tag.s;
    const char* end = tag.s + tag.len;
    while (p + nlen + 2 < end) {
        bool bound = (p == tag.s) || !IsIdentChar(p[-1]);
        if (bound && base::StrStartsWithI(Str(p, (int)(end - p)), name) &&
            p[nlen] == '=') {
            p += nlen + 1;
            char q = 0;
            if (*p == '"' || *p == '\'') {
                q = *p++;
            }
            const char* vs = p;
            while (p < end && *p != q && *p != '>') {
                p++;
            }
            return StrDupTemp(Str(vs, (int)(p - vs)));
        }
        p++;
    }
    return {};
}

static float AttrF(Str tag, const char* name, float def) {
    TempStr value = GetAttrTemp(tag, name);
    if (!value) {
        return def;
    }
    return StrToFloatUnchecked(value);
}

// `fill="#rrggbb"` on a shape. "none" and "currentColor" both leave the shape
// in the caller's colour, which is what every Lucide icon says.
static bool ParseSvgColor(Str s, Rgba* out) {
    if (!s.s || s.len < 4 || s.s[0] != '#') {
        return false;
    }
    int n = s.len - 1;
    if (n != 3 && n != 6) {
        return false;
    }
    int v[6] = {};
    for (int i = 0; i < n; i++) {
        char c = s.s[i + 1];
        if (c >= '0' && c <= '9') {
            v[i] = c - '0';
        } else if (c >= 'a' && c <= 'f') {
            v[i] = c - 'a' + 10;
        } else if (c >= 'A' && c <= 'F') {
            v[i] = c - 'A' + 10;
        } else {
            return false;
        }
    }
    if (n == 3) {
        *out = Rgba{(uint8_t)(v[0] * 17), (uint8_t)(v[1] * 17),
                    (uint8_t)(v[2] * 17), 255};
    } else {
        *out = Rgba{(uint8_t)(v[0] * 16 + v[1]), (uint8_t)(v[2] * 16 + v[3]),
                    (uint8_t)(v[4] * 16 + v[5]), 255};
    }
    return true;
}

// `fill` / `stroke` / `stop-color`, which may be a colour or a reference to
// something the file declared. Only a gradient is followed, and only as far
// as its first stop; url() to anything else -- a pattern, a filter output --
// is no colour, and the shape falls back to the caller's the way a shape that
// named nothing does.
static bool ParseSvgPaint(const SvgIcon* ic, Str v, Rgba* out) {
    if (ParseSvgColor(v, out)) {
        return true;
    }
    if (!ic || v.len < 6 || !base::StrStartsWithI(v, "url(")) {
        return false;
    }
    const char* p = v.s + 4;
    const char* end = v.s + v.len;
    if (p < end && *p == '#') {
        p++;
    }
    const char* idStart = p;
    while (p < end && *p != ')') {
        p++;
    }
    Str id(idStart, (int)(p - idStart));
    for (int i = 0; i < ic->gradients.len; i++) {
        const SvgGradient& g = ic->gradients[i];
        if (g.hasColor && id.len > 0 && StrEq(g.id, id)) {
            *out = g.color;
            return true;
        }
    }
    return false;
}

// ─── transform= ───────────────────────────────────────────────────────────
//
// The 2x3 affine SVG writes as `matrix(a b c d e f)`: x' = a*x + c*y + e and
// y' = b*x + d*y + f. Everything else — translate, scale, rotate — is one of
// these, and a list of them is their product, left to right.
//
// A shape's points are transformed as the shape is finished rather than as
// the backend draws it, so the byte stream stays a flat list of coordinates
// and `ExecuteDrawOps` never has to know a matrix exists.

struct SvgMatrix {
    float a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;

    bool IsIdentity() const {
        return a == 1 && b == 0 && c == 0 && d == 1 && e == 0 && f == 0;
    }
};

// `m` applied after `n` — the order a nested <g> composes in, the outer one
// last.
static SvgMatrix MatMul(const SvgMatrix& m, const SvgMatrix& n) {
    SvgMatrix r;
    r.a = m.a * n.a + m.c * n.b;
    r.b = m.b * n.a + m.d * n.b;
    r.c = m.a * n.c + m.c * n.d;
    r.d = m.b * n.c + m.d * n.d;
    r.e = m.a * n.e + m.c * n.f + m.e;
    r.f = m.b * n.e + m.d * n.f + m.f;
    return r;
}

static void MatApply(const SvgMatrix& m, float* x, float* y) {
    float px = *x;
    float py = *y;
    *x = m.a * px + m.c * py + m.e;
    *y = m.b * px + m.d * py + m.f;
}

// "translate(120 0) scale(.1)" and the rest of the list, in order. A function
// this does not know is skipped rather than guessed at — skewX and skewY are
// the two it does not know.
static SvgMatrix ParseTransform(Str s) {
    SvgMatrix out;
    if (!s.s || s.len <= 0) {
        return out;
    }
    const char* p = s.s;
    const char* end = s.s + s.len;
    while (p < end) {
        while (p < end && (*p == ' ' || *p == ',' || *p == '\t' || *p == '\n' ||
                           *p == '\r')) {
            p++;
        }
        const char* name = p;
        while (p < end && *p != '(') {
            p++;
        }
        if (p >= end) {
            break;
        }
        int nameLen = (int)(p - name);
        p++; // past (
        const char* argStart = p;
        while (p < end && *p != ')') {
            p++;
        }
        PathScan sc{argStart, p};
        if (p < end) {
            p++; // past )
        }
        float v[6] = {};
        int n = 0;
        while (n < 6 && ParseNum(&sc, &v[n])) {
            n++;
        }
        SvgMatrix m;
        Str fn(name, nameLen);
        if (StrEqI(fn, "translate") && n >= 1) {
            m.e = v[0];
            m.f = n >= 2 ? v[1] : 0;
        } else if (StrEqI(fn, "scale") && n >= 1) {
            m.a = v[0];
            m.d = n >= 2 ? v[1] : v[0];
        } else if (StrEqI(fn, "rotate") && n >= 1) {
            float rad = v[0] * 3.14159265358979f / 180.f;
            float cs = cosf(rad);
            float sn = sinf(rad);
            m.a = cs;
            m.b = sn;
            m.c = -sn;
            m.d = cs;
            if (n >= 3) {
                // About a point rather than the origin: move it there, turn,
                // move it back.
                SvgMatrix to;
                to.e = v[1];
                to.f = v[2];
                SvgMatrix back;
                back.e = -v[1];
                back.f = -v[2];
                m = MatMul(to, MatMul(m, back));
            }
        } else if (StrEqI(fn, "matrix") && n >= 6) {
            m.a = v[0];
            m.b = v[1];
            m.c = v[2];
            m.d = v[3];
            m.e = v[4];
            m.f = v[5];
        }
        out = MatMul(out, m);
    }
    return out;
}

// One drawn element is done: where it landed, what it added, and the colours
// it named. Every element gets one, `<line>` and `<polyline>` included — a
// shape that is not on the list is a shape the per-colour encoding would drop.
static void EndShape(SvgIcon* ic, int start, Str tag, const SvgMatrix& m) {
    if (ic->ops.len <= start) {
        return;
    }
    // The groups this shape sits in, and its own transform after them.
    TempStr own = GetAttrTemp(tag, "transform");
    SvgMatrix full = m;
    if (own) {
        full = MatMul(m, ParseTransform(own));
    }
    if (!full.IsIdentity()) {
        for (int i = start; i < ic->ops.len; i++) {
            SvgOp& o = ic->ops[i];
            if (o.cmd == kClose) {
                continue; // carries no point
            }
            MatApply(full, &o.x, &o.y);
            if (o.cmd == kCubic) {
                MatApply(full, &o.x1, &o.y1);
                MatApply(full, &o.x2, &o.y2);
            }
        }
    }
    SvgShape sh;
    sh.start = start;
    sh.count = ic->ops.len - start;
    TempStr fill = GetAttrTemp(tag, "fill");
    if (fill && ParseSvgPaint(ic, fill, &sh.fill)) {
        sh.hasFill = true;
        ic->hasOwnColors = true;
    }
    TempStr stroke = GetAttrTemp(tag, "stroke");
    if (stroke && ParseSvgPaint(ic, stroke, &sh.stroke)) {
        sh.hasStroke = true;
        ic->hasOwnColors = true;
    }
    VecAppend(ic->shapes, sh);
}

// Shapes inside one of these are a definition, not a drawing: a clip path, a
// gradient stop, a filter's input, the contents of <defs>. A file that names
// one and never uses it — a shields.io badge names two — would otherwise
// paint it, which is how a 20-pixel badge grew a stripe down the page.
// A <g> and not a <glyph>: the name has to end where the tag's whitespace or
// its close begins, the same rule IsHiddenContainer applies.
static bool IsGroupTag(const char* name, const char* end) {
    if (end <= name || (name[0] != 'g' && name[0] != 'G')) {
        return false;
    }
    char after = name + 1 < end ? name[1] : ' ';
    return after == ' ' || after == '>' || after == '/' || after == '\t' ||
           after == '\n' || after == '\r';
}

static bool IsHiddenContainer(const char* name, const char* end) {
    static const char* kNames[] = {
        "defs",   "clipPath", "mask",           "filter",        "pattern",
        "symbol", "marker",   "linearGradient", "radialGradient"};
    for (const char* n : kNames) {
        int len = (int)strlen(n);
        if (!base::StrStartsWithI(Str(name, (int)(end - name)), n)) {
            continue;
        }
        // "clipPath" must not match "clipPathUnits": the name ends where
        // the tag's whitespace or its close begins.
        char after = name + len < end ? name[len] : ' ';
        if (after == ' ' || after == '>' || after == '/' || after == '\t' ||
            after == '\n' || after == '\r') {
            return true;
        }
    }
    return false;
}

// The name of a tag, matched whole: "text" must not also match "textPath".
static bool IsTagNamed(const char* name, const char* end, const char* lit) {
    int len = (int)strlen(lit);
    if (!base::StrStartsWithI(Str(name, (int)(end - name)), lit)) {
        return false;
    }
    char after = name + len < end ? name[len] : ' ';
    return after == ' ' || after == '>' || after == '/' || after == '\t' ||
           after == '\n' || after == '\r';
}

// What a container hands down to what is inside it: the transform, and the
// presentation a <text> inherits. Fill is here for the sake of text and text
// only -- a shape still reads its own, for the reason IsGroupTag's note
// gives -- and `hidden` is the one thing a group can say that stops a run
// being drawn at all.
struct SvgCtx {
    SvgMatrix m;
    float fontSize = 0; // 0: nothing named one, so nothing is drawn
    float x = 0, y = 0;
    float textLength = 0;
    uint32_t anchor = kTextAnchorStart;
    bool bold = false;
    bool hasFill = false;
    Rgba fill = {};
    // filter="url(#blur)": a shields.io badge draws its label three times,
    // twice as a blurred drop shadow underneath. There is no blur here, so
    // the shadows would come out as two hard copies of the word offset by a
    // pixel. The filtered ones are dropped and the plain one is the label.
    bool filtered = false;
};

// The tag's own attributes over what the container around it said. Every one
// of them inherits in SVG, so an absent attribute leaves the outer value in
// place.
static SvgCtx RefineCtx(const SvgIcon* ic, const SvgCtx& outer, Str tag) {
    SvgCtx cur = outer;
    TempStr tr = GetAttrTemp(tag, "transform");
    if (tr) {
        cur.m = MatMul(cur.m, ParseTransform(tr));
    }
    TempStr value = GetAttrTemp(tag, "font-size");
    if (value) {
        float v = StrToFloatUnchecked(value);
        if (v > 0) {
            cur.fontSize = v;
        }
    }
    value = GetAttrTemp(tag, "font-weight");
    if (value) {
        cur.bold = StrEqI(value, "bold") || StrToIntUnchecked(value) >= 600;
    }
    value = GetAttrTemp(tag, "text-anchor");
    if (value) {
        cur.anchor = StrEqI(value, "middle") ? kTextAnchorMiddle
                     : StrEqI(value, "end")  ? kTextAnchorEnd
                                             : kTextAnchorStart;
    }
    value = GetAttrTemp(tag, "fill");
    if (value) {
        Rgba c;
        if (ParseSvgPaint(ic, value, &c)) {
            cur.hasFill = true;
            cur.fill = c;
        }
    }
    value = GetAttrTemp(tag, "fill-opacity");
    if (value) {
        float o = StrToFloatUnchecked(value);
        if (o < 0) {
            o = 0;
        }
        if (o > 1) {
            o = 1;
        }
        cur.fill.a = (uint8_t)lroundf(o * 255.f);
        cur.hasFill = cur.hasFill || o < 1.f;
    }
    tr = GetAttrTemp(tag, "filter");
    if (tr) {
        cur.filtered = true;
    }
    // x/y do not inherit the way the rest do -- a tspan without them
    // continues where the last run left off -- but every file this reads
    // names both on whichever element carries the words.
    value = GetAttrTemp(tag, "x");
    if (value) {
        cur.x = StrToFloatUnchecked(value);
    }
    value = GetAttrTemp(tag, "y");
    if (value) {
        cur.y = StrToFloatUnchecked(value);
    }
    value = GetAttrTemp(tag, "textLength");
    if (value) {
        cur.textLength = StrToFloatUnchecked(value);
    }
    return cur;
}

// The characters between this tag and the next one, as one run. Whitespace
// either side is dropped, so a <text> that only wraps a <tspan> adds nothing.
static void AddTextRun(SvgIcon* ic, const SvgCtx& cur, const char* p,
                       const char* end) {
    const char* q = p;
    while (q < end && *q != '<') {
        q++;
    }
    const char* a = p;
    const char* b = q;
    while (a < b && (*a == ' ' || *a == '\t' || *a == '\n' || *a == '\r')) {
        a++;
    }
    while (b > a &&
           (b[-1] == ' ' || b[-1] == '\t' || b[-1] == '\n' || b[-1] == '\r')) {
        b--;
    }
    if (b <= a || cur.fontSize <= 0 || cur.filtered) {
        return;
    }
    // The transform the containers add up to, applied to the anchor point and
    // to the size: `scale(.1)` on a shields.io badge is what turns font-size
    // 110 into eleven pixels.
    SvgShape sh;
    sh.isText = true;
    sh.text = Str(a, (int)(b - a));
    sh.tx = cur.x;
    sh.ty = cur.y;
    MatApply(cur.m, &sh.tx, &sh.ty);
    float det = cur.m.a * cur.m.d - cur.m.b * cur.m.c;
    float scale = sqrtf(det < 0 ? -det : det);
    if (scale <= 0) {
        scale = 1;
    }
    sh.fontSize = cur.fontSize * scale;
    sh.textLength = cur.textLength * scale;
    sh.textFlags = cur.anchor | (cur.bold ? (uint32_t)kTextBold : 0u);
    sh.hasFill = cur.hasFill;
    sh.fill = cur.fill;
    VecAppend(ic->shapes, sh);
    ic->hasText = true;
    // A string cannot be drawn by the single-path encoding, so the file goes
    // down the per-shape route whether or not anything named a colour.
    ic->hasOwnColors = true;
}

static void ParseSvg(Str xml, SvgIcon* ic) {
    VecReset(ic->ops);
    VecReset(ic->shapes);
    // Nine Lucide icons in ten come out under 32 ops and 16 shapes
    // (`bun cmd/vec-log.ts tests`), so both lists are one allocation.
    VecReserve(ic->ops, 32);
    VecReserve(ic->shapes, 16);
    ic->vbX = 0;
    ic->vbY = 0;
    ic->vbW = 24;
    ic->vbH = 24;
    ic->strokeW = 2;
    // The same defaults the struct declares -- SVG's own: fill black, stroke
    // none. ParseSvg is reached with an icon that has been used before, so
    // every field it reads has to be put back here as well as declared there.
    ic->filled = true;
    ic->stroked = false;
    ic->hasOwnColors = false;
    ic->hasText = false;
    VecReset(ic->gradients);
    if (!xml.s || xml.len <= 0) {
        return;
    }
    const char* p = xml.s;
    const char* end = xml.s + xml.len;
    // How deep inside a <defs> / <clipPath> / <mask> / ... we are. Nothing is
    // drawn while this is above zero.
    int hidden = 0;
    // What the containers in force say, innermost last. Deeper than this and
    // the file is doing something no picture does, so the extra groups draw
    // untransformed rather than not at all.
    constexpr int kMaxGroupDepth = 16;
    SvgCtx gstack[kMaxGroupDepth];
    int gdepth = 0;
    const SvgCtx kRootCtx;
    // The gradient being read, while inside one. Its <stop> children are the
    // only thing a hidden container contributes to the drawing.
    int gradIx = -1;
    while (p < end) {
        if (*p != '<') {
            p++;
            continue;
        }
        p++;
        if (p < end && *p == '/') {
            const char* name = p + 1;
            while (p < end && *p != '>') {
                p++;
            }
            if (hidden > 0 && IsHiddenContainer(name, p)) {
                hidden--;
                if (IsTagNamed(name, p, "linearGradient") ||
                    IsTagNamed(name, p, "radialGradient")) {
                    gradIx = -1;
                }
            } else if (hidden == 0 && gdepth > 0 &&
                       (IsGroupTag(name, p) || IsTagNamed(name, p, "text") ||
                        IsTagNamed(name, p, "tspan"))) {
                gdepth--;
            }
            if (p < end) {
                p++;
            }
            continue;
        }
        if (p < end && *p == '!') {
            // comment / doctype
            while (p + 2 < end &&
                   !(p[0] == '-' && p[1] == '-' && p[2] == '>')) {
                p++;
            }
            p += 3;
            continue;
        }
        const char* tagStart = p;
        while (p < end && *p != '>') {
            p++;
        }
        if (p >= end) {
            break;
        }
        Str tag(tagStart, (int)(p - tagStart));
        bool selfClosing = tag.len > 0 && tag.s[tag.len - 1] == '/';
        p++; // skip >

        if (IsHiddenContainer(tagStart, tagStart + tag.len)) {
            if (!selfClosing) {
                // Wherever it is declared, including inside the <defs> that
                // is the usual place for it.
                if (IsTagNamed(tagStart, tagStart + tag.len,
                               "linearGradient") ||
                    IsTagNamed(tagStart, tagStart + tag.len,
                               "radialGradient")) {
                    SvgGradient g;
                    g.id = GetAttrTemp(tag, "id");
                    if (g.id) {
                        VecAppend(ic->gradients, g);
                        gradIx = ic->gradients.len - 1;
                    }
                }
                hidden++;
            }
            continue;
        }
        if (hidden > 0) {
            // <stop stop-color=..>: the first one is the colour the gradient
            // reduces to, and the rest of what is in here still draws nothing.
            if (gradIx >= 0 && gradIx < ic->gradients.len &&
                !ic->gradients[gradIx].hasColor &&
                IsTagNamed(tagStart, tagStart + tag.len, "stop")) {
                TempStr stop = GetAttrTemp(tag, "stop-color");
                Rgba c;
                if (stop && ParseSvgColor(stop, &c)) {
                    // stop-opacity is what makes a shields.io badge's sheen a
                    // sheen: the gradient laid over the whole plate is #bbb at
                    // a tenth, and reading the colour without the opacity
                    // washes the plate out to grey.
                    stop = GetAttrTemp(tag, "stop-opacity");
                    if (stop) {
                        float o = StrToFloatUnchecked(stop);
                        if (o < 0) {
                            o = 0;
                        }
                        if (o > 1) {
                            o = 1;
                        }
                        c.a = (uint8_t)lroundf(o * 255.f);
                    }
                    ic->gradients[gradIx].color = c;
                    ic->gradients[gradIx].hasColor = true;
                }
            }
            continue;
        }

        // <g transform=".."> — the one container that still says something
        // about what is drawn inside it. Its fill and stroke are deliberately
        // *not* inherited: every window-chrome icon under assets/icons wraps
        // its path in <g fill="#000000">, and honouring that would make the
        // file a picture with a colour of its own and pin the title bar's
        // buttons black instead of letting the theme colour them.
        const SvgCtx& outer = gdepth > 0 && gdepth <= kMaxGroupDepth
                                  ? gstack[gdepth - 1]
                                  : kRootCtx;
        if (IsGroupTag(tagStart, tagStart + tag.len)) {
            if (!selfClosing) {
                SvgCtx cur = RefineCtx(ic, outer, tag);
                if (gdepth < kMaxGroupDepth) {
                    gstack[gdepth] = cur;
                }
                gdepth++;
            }
            continue;
        }
        // <text> and <tspan> are containers as well: the presentation the
        // outer one names is what the inner one draws with, which is how a
        // GitHub badge writes `<text fill=..><tspan x=.. y=..>CI</tspan>`.
        // The characters that follow the tag, up to the next one, are the run
        // -- an element that holds only another element contributes nothing
        // itself, and the tspan inside it does the drawing.
        bool isText = IsTagNamed(tagStart, tagStart + tag.len, "text");
        bool isTspan = IsTagNamed(tagStart, tagStart + tag.len, "tspan");
        if (isText || isTspan) {
            SvgCtx cur = RefineCtx(ic, outer, tag);
            AddTextRun(ic, cur, p, end);
            if (!selfClosing) {
                if (gdepth < kMaxGroupDepth) {
                    gstack[gdepth] = cur;
                }
                gdepth++;
            }
            continue;
        }
        // What the groups around this shape add up to.
        const SvgMatrix& gm = outer.m;

        if (base::StrStartsWithI(tag, "svg")) {
            TempStr viewBox = GetAttrTemp(tag, "viewBox");
            if (viewBox) {
                PathScan s{viewBox.s, viewBox.s + viewBox.len};
                float a = 0, b = 0, c = 24, d = 24;
                ParseNum(&s, &a);
                ParseNum(&s, &b);
                ParseNum(&s, &c);
                ParseNum(&s, &d);
                ic->vbX = a;
                ic->vbY = b;
                ic->vbW = c > 0 ? c : 24;
                ic->vbH = d > 0 ? d : 24;
            } else {
                // No viewBox: the coordinates are the viewport's, which
                // width and height give. Every lucide icon has one and never
                // reaches this; a badge has neither and drew at 24x24, which
                // scaled an 86-wide file by three and a half.
                float w = AttrF(tag, "width", 0);
                float h = AttrF(tag, "height", 0);
                if (w > 0 && h > 0) {
                    ic->vbW = w;
                    ic->vbH = h;
                }
            }
            float sw = AttrF(tag, "stroke-width", 0);
            if (sw > 0) {
                ic->strokeW = sw;
            }
            TempStr fill = GetAttrTemp(tag, "fill");
            if (fill) {
                ic->filled = !StrEqI(fill, "none");
            }
            TempStr stroke = GetAttrTemp(tag, "stroke");
            if (stroke) {
                ic->stroked = !StrEqI(stroke, "none");
            }
            continue;
        }
        if (base::StrStartsWithI(tag, "path")) {
            TempStr d = GetAttrTemp(tag, "d");
            int start = ic->ops.len;
            if (d) {
                ParsePathD(ic, d);
            }
            EndShape(ic, start, tag, gm);
            continue;
        }
        if (base::StrStartsWithI(tag, "rect")) {
            int start = ic->ops.len;
            float x = AttrF(tag, "x", 0);
            float y = AttrF(tag, "y", 0);
            float w = AttrF(tag, "width", 0);
            float h = AttrF(tag, "height", 0);
            float rx = AttrF(tag, "rx", 0);
            AddRoundRect(ic, x, y, w, h, rx);
            EndShape(ic, start, tag, gm);
            continue;
        }
        if (base::StrStartsWithI(tag, "polyline")) {
            TempStr pts = GetAttrTemp(tag, "points");
            int start = ic->ops.len;
            if (pts) {
                ParsePolyline(ic, pts, false);
            }
            EndShape(ic, start, tag, gm);
            continue;
        }
        if (base::StrStartsWithI(tag, "polygon")) {
            TempStr pts = GetAttrTemp(tag, "points");
            int start = ic->ops.len;
            if (pts) {
                ParsePolyline(ic, pts, true);
            }
            EndShape(ic, start, tag, gm);
            continue;
        }
        if (base::StrStartsWithI(tag, "line")) {
            int start = ic->ops.len;
            float x1 = AttrF(tag, "x1", 0);
            float y1 = AttrF(tag, "y1", 0);
            float x2 = AttrF(tag, "x2", 0);
            float y2 = AttrF(tag, "y2", 0);
            AddMove(ic, x1, y1);
            AddLine(ic, x2, y2);
            EndShape(ic, start, tag, gm);
            continue;
        }
        if (base::StrStartsWithI(tag, "circle")) {
            int start = ic->ops.len;
            float cx = AttrF(tag, "cx", 0);
            float cy = AttrF(tag, "cy", 0);
            float r = AttrF(tag, "r", 0);
            AddRoundRect(ic, cx - r, cy - r, r * 2, r * 2, r);
            EndShape(ic, start, tag, gm);
            continue;
        }
        // No icon under assets/icons has one; a picture from anywhere else
        // may, and it used to be dropped without a word.
        if (base::StrStartsWithI(tag, "ellipse")) {
            int start = ic->ops.len;
            AddEllipse(ic, AttrF(tag, "cx", 0), AttrF(tag, "cy", 0),
                       AttrF(tag, "rx", 0), AttrF(tag, "ry", 0));
            EndShape(ic, start, tag, gm);
            continue;
        }
    }
}

// ─── the file, as bytecode ────────────────────────────────

static void EmitOps(DrawOpsBuilder* b, const SvgIcon* ic, int from, int to) {
    for (int i = from; i < to; i++) {
        const SvgOp& o = ic->ops[i];
        if (o.cmd == kMove) {
            b->MoveTo(o.x, o.y);
        } else if (o.cmd == kLine) {
            b->LineTo(o.x, o.y);
        } else if (o.cmd == kCubic) {
            b->CubicTo(o.x1, o.y1, o.x2, o.y2, o.x, o.y);
        } else if (o.cmd == kClose) {
            b->ClosePath();
        }
    }
}

// The rule `cmd/svg-to-bytecode.ts` implements too. A plain Lucide icon is one
// path in the caller's colour - filled first if the root said
// fill="currentColor", then stroked. A file whose shapes name their own
// colours is a picture, not an icon: each shape is painted on its own, so it
// keeps the colour it asked for and the ones that named none still take the
// caller's.
// Which of the three path ops says "fill this", "stroke this", or both. A
// path that is neither filled nor stroked would draw nothing, and an icon
// that says so is a mistake rather than an instruction, so it is stroked.
static DrawOp PathOp(bool filled, bool stroked) {
    if (filled && stroked) {
        return kOpFillStrokePath;
    }
    return filled ? kOpFillPath : kOpStrokePath;
}

static void EncodeIcon(const SvgIcon* ic, DrawOpsBuilder* b) {
    b->ViewBox(ic->vbX, ic->vbY, ic->vbW, ic->vbH);
    b->StrokeWidth(ic->strokeW > 0 ? ic->strokeW : 2.f);
    if (!ic->hasOwnColors) {
        EmitOps(b, ic, 0, ic->ops.len);
        b->Op(PathOp(ic->filled, ic->stroked));
        b->End();
        return;
    }
    for (int i = 0; i < ic->shapes.len; i++) {
        const SvgShape& sh = ic->shapes[i];
        if (sh.isText) {
            if (sh.hasFill) {
                b->Color(sh.fill);
            }
            b->Text(sh.tx, sh.ty, sh.fontSize, sh.textLength, sh.textFlags,
                    sh.text);
            if (sh.hasFill) {
                b->ColorReset();
            }
            continue;
        }
        if (sh.hasFill) {
            b->Color(sh.fill);
            EmitOps(b, ic, sh.start, sh.start + sh.count);
            b->Op(kOpFillPath);
            b->ColorReset();
        }
        // A shape may be filled in one colour and drawn in another, and one
        // op carries one colour, so that is two passes over the same points.
        // kOpFillStrokePath is the single-colour case and cannot say this.
        if (sh.hasStroke) {
            b->Color(sh.stroke);
            EmitOps(b, ic, sh.start, sh.start + sh.count);
            b->Op(kOpStrokePath);
            b->ColorReset();
        }
        if (sh.hasFill || sh.hasStroke) {
            continue;
        }
        // Named no colour of its own: the caller's, the way every shape in a
        // plain icon is drawn.
        EmitOps(b, ic, sh.start, sh.start + sh.count);
        b->Op(PathOp(ic->filled, ic->stroked));
    }
    b->End();
}

bool SvgToDrawOps(Str xml, DrawOpsBuilder* out) {
    if (!out) {
        return false;
    }
    SvgIcon ic;
    ParseSvg(xml, &ic);
    if (ic.ops.len <= 0 && !ic.hasText) {
        return false;
    }
    EncodeIcon(&ic, out);
    return true;
}

// ─── what an asset path draws ──────────────────────────────

// The generated table, looked up. The names are a SeqStrings run rather than
// a pointer per icon, so this is a scan of about nine hundred bytes instead
// of a binary search — which costs nothing that matters, because a path is
// looked up once and `SvgDrawOpsFor` remembers the answer.
const uint8_t* AssetIconFind(Str name, int* lenOut) {
    *lenOut = 0;
    int ix = SeqStrIndex(kAssetIconNames, name);
    if (ix < 0 || ix >= kAssetIconsCount) {
        return nullptr;
    }
    const AssetIcon& e = kAssetIcons[ix];
    *lenOut = e.len;
    return kAssetIconsData + e.offset;
}

const uint8_t* AssetIconForPath(Str assetPath, int* lenOut) {
    *lenOut = 0;
    const char* kDir = "icons/";
    const int kDirLen = 6;
    const int kExtLen = 4; // ".svg"
    if (assetPath.len <= kDirLen + kExtLen) {
        return nullptr;
    }
    if (!base::StrStartsWithI(assetPath, kDir)) {
        return nullptr;
    }
    Str base(assetPath.s + kDirLen, assetPath.len - kDirLen - kExtLen);
    if (!base::StrEqI(Str(base.s + base.len, kExtLen), ".svg")) {
        return nullptr;
    }
    return AssetIconFind(base, lenOut);
}

// What an asset path resolved to, looked up once and then remembered. An
// application's own `.svg` is read and converted here; a path that no root
// supplies falls through to the compiled table, and that answer is cached
// too — otherwise every icon would stat the filesystem once a frame.
//
// Big enough to hold the whole icon set and then some. It wraps rather than
// grows, and a wrap frees whatever the slot was holding.
static const int kMaxCache = 128;

struct OpsCache {
    char path[128] = {};
    const uint8_t* data = nullptr;
    int len = 0;
    // False when `data` points into kAssetIconsData, which is not ours to
    // free.
    bool owned = false;
};

static OpsCache gCache[kMaxCache];
static int gCacheN = 0;

struct XmlOpsCache;
static void XmlCacheClear();

void SvgCacheClear() {
    for (int i = 0; i < kMaxCache; i++) {
        if (gCache[i].owned) {
            Free(nullptr, (void*)gCache[i].data);
        }
        gCache[i] = {};
    }
    gCacheN = 0;
    XmlCacheClear();
}

// The slot `assetPath` will live in, emptied and named. The caller has
// already bounded the path, so this always has one to give.
static OpsCache* CacheSlotFor(Str assetPath) {
    if (gCacheN >= kMaxCache) {
        gCacheN = 0; // simple wrap
    }
    OpsCache* e = &gCache[gCacheN++];
    if (e->owned) {
        Free(nullptr, (void*)e->data);
    }
    e->data = nullptr;
    e->len = 0;
    e->owned = false;
    memcpy(e->path, assetPath.s, (size_t)assetPath.len);
    e->path[assetPath.len] = 0;
    return e;
}

const uint8_t* SvgDrawOpsFor(Str assetPath, int* lenOut) {
    *lenOut = 0;
    if (!assetPath.s || assetPath.len <= 0 || assetPath.len > 127) {
        return nullptr;
    }
    for (int i = 0; i < gCacheN; i++) {
        if (gCache[i].data && base::StrEqI(assetPath, gCache[i].path)) {
            *lenOut = gCache[i].len;
            return gCache[i].data;
        }
    }
    // The asset roots first: an application that ships "icons/inbox.svg" of
    // its own means that one, the way Rust's AssetSource does. Only when no
    // root has the file does the compiled-in table answer — which is every
    // lucide icon in a binary shipped without its assets folder beside it.
    TempStr xml = AssetsLoadTextTemp(assetPath);
    if (xml.s) {
        DrawOpsBuilder b;
        if (SvgToDrawOps(xml, &b)) {
            uint8_t* buf = AllocArray<uint8_t>(b.data.len);
            if (!buf) {
                return nullptr;
            }
            memcpy(buf, b.data.els, (size_t)b.data.len);
            OpsCache* e = CacheSlotFor(assetPath);
            e->data = buf;
            e->len = b.data.len;
            e->owned = true;
            *lenOut = e->len;
            return e->data;
        }
    }
    int len = 0;
    const uint8_t* built = AssetIconForPath(assetPath, &len);
    if (!built) {
        return nullptr;
    }
    // Remembered as well, so a name the table answers is not a directory
    // walk every time it is drawn.
    OpsCache* e = CacheSlotFor(assetPath);
    e->data = built;
    e->len = len;
    *lenOut = len;
    return built;
}

bool SvgViewBox(Str assetPath, Size* out) {
    int len = 0;
    const uint8_t* ops = SvgDrawOpsFor(assetPath, &len);
    if (!ops || !out) {
        return false;
    }
    return DrawOpsViewBox(ops, len, out);
}

bool SvgDrawOps(PaintCtx* ctx, const uint8_t* ops, int len, float x, float y,
                float w, float h, Rgba color, float turns, bool grayscale) {
    if (!ctx || !ctx->rt || w <= 0 || h <= 0 || !ops || len <= 0) {
        return false;
    }
    DrawOpsTarget t;
    t.x = x;
    t.y = y;
    t.w = w;
    t.h = h;
    t.color = color;
    t.turns = turns;
    t.grayscale = grayscale;
    return ExecuteDrawOps(ctx, ops, len, t);
}

bool SvgDraw(PaintCtx* ctx, Str assetPath, float x, float y, float size,
             Rgba color, float turns) {
    int len = 0;
    const uint8_t* ops = SvgDrawOpsFor(assetPath, &len);
    return SvgDrawOps(ctx, ops, len, x, y, size, size, color, turns);
}

// The source-backed half of the cache: `Icon::data` icons, kept by a hash of
// their bytes rather than by name. Smaller than the path cache — an
// application embeds a handful of icons, not a set — and it wraps the same
// way. A hash and the length together stand for the bytes; two different
// icons colliding on both is not a case worth the copy that ruling it out
// would take.
static const int kMaxXmlCache = 32;

struct XmlOpsCache {
    uint64_t hash = 0;
    int xmlLen = 0;
    uint8_t* data = nullptr;
    int len = 0;
};

static XmlOpsCache gXmlCache[kMaxXmlCache];
static int gXmlCacheN = 0;

static uint64_t XmlHash(Str xml) {
    // FNV-1a over the source.
    uint64_t h = 1469598103934665603ull;
    for (int i = 0; i < xml.len; i++) {
        h ^= (uint8_t)xml.s[i];
        h *= 1099511628211ull;
    }
    return h;
}

const uint8_t* SvgDrawOpsForXml(Str xml, int* lenOut) {
    *lenOut = 0;
    if (!xml.s || xml.len <= 0) {
        return nullptr;
    }
    uint64_t hash = XmlHash(xml);
    for (int i = 0; i < kMaxXmlCache; i++) {
        if (gXmlCache[i].data && gXmlCache[i].hash == hash &&
            gXmlCache[i].xmlLen == xml.len) {
            *lenOut = gXmlCache[i].len;
            return gXmlCache[i].data;
        }
    }
    DrawOpsBuilder b;
    if (!SvgToDrawOps(xml, &b)) {
        return nullptr;
    }
    uint8_t* buf = AllocArray<uint8_t>(b.data.len);
    if (!buf) {
        return nullptr;
    }
    memcpy(buf, b.data.els, (size_t)b.data.len);
    if (gXmlCacheN >= kMaxXmlCache) {
        gXmlCacheN = 0; // simple wrap
    }
    XmlOpsCache* e = &gXmlCache[gXmlCacheN++];
    if (e->data) {
        Free(nullptr, e->data);
    }
    e->hash = hash;
    e->xmlLen = xml.len;
    e->data = buf;
    e->len = b.data.len;
    *lenOut = e->len;
    return e->data;
}

bool SvgDrawXml(PaintCtx* ctx, Str xml, float x, float y, float size,
                Rgba color, float turns) {
    int len = 0;
    const uint8_t* ops = SvgDrawOpsForXml(xml, &len);
    return SvgDrawOps(ctx, ops, len, x, y, size, size, color, turns);
}

static void XmlCacheClear() {
    for (int i = 0; i < kMaxXmlCache; i++) {
        if (gXmlCache[i].data) {
            Free(nullptr, gXmlCache[i].data);
        }
        gXmlCache[i] = {};
    }
    gXmlCacheN = 0;
}

Str IconNamePath(IconName name) {
    switch (name) {
        case IconName::ALargeSmall:
            return StrL("icons/a-large-small.svg");
        case IconName::ArrowLeft:
            return StrL("icons/arrow-left.svg");
        case IconName::ArrowRight:
            return StrL("icons/arrow-right.svg");
        case IconName::ArrowUp:
            return StrL("icons/arrow-up.svg");
        case IconName::ArrowDown:
            return StrL("icons/arrow-down.svg");
        case IconName::Asterisk:
            return StrL("icons/asterisk.svg");
        case IconName::Bell:
            return StrL("icons/bell.svg");
        case IconName::Building2:
            return StrL("icons/building-2.svg");
        case IconName::Eye:
            return StrL("icons/eye.svg");
        case IconName::EyeOff:
            return StrL("icons/eye-off.svg");
        case IconName::Heart:
            return StrL("icons/heart.svg");
        case IconName::HeartOff:
            return StrL("icons/heart-off.svg");
        case IconName::Maximize:
            return StrL("icons/maximize.svg");
        case IconName::Minimize:
            return StrL("icons/minimize.svg");
        case IconName::Star:
            return StrL("icons/star.svg");
        case IconName::StarFill:
            return StrL("icons/star-fill.svg");
        case IconName::Sun:
            return StrL("icons/sun.svg");
        case IconName::Moon:
            return StrL("icons/moon.svg");
        case IconName::Play:
            return StrL("icons/play.svg");
        case IconName::Map:
            return StrL("icons/map.svg");
        case IconName::Globe:
            return StrL("icons/globe.svg");
        case IconName::Github:
            return StrL("icons/github.svg");
        case IconName::ExternalLink:
            return StrL("icons/external-link.svg");
        case IconName::Inbox:
            return StrL("icons/inbox.svg");
        case IconName::Bot:
            return StrL("icons/bot.svg");
        case IconName::Cpu:
            return StrL("icons/cpu.svg");
        case IconName::MemoryStick:
            return StrL("icons/memory-stick.svg");
        case IconName::HardDrive:
            return StrL("icons/hard-drive.svg");
        case IconName::Battery:
            return StrL("icons/battery.svg");
        case IconName::BatteryCharging:
            return StrL("icons/battery-charging.svg");
        case IconName::BatteryMedium:
            return StrL("icons/battery-medium.svg");
        case IconName::BatteryFull:
            return StrL("icons/battery-full.svg");
        case IconName::BatteryLow:
            return StrL("icons/battery-low.svg");
        case IconName::BatteryWarning:
            return StrL("icons/battery-warning.svg");
        case IconName::WindowMinimize:
            return StrL("icons/window-minimize.svg");
        case IconName::WindowMaximize:
            return StrL("icons/window-maximize.svg");
        case IconName::WindowRestore:
            return StrL("icons/window-restore.svg");
        case IconName::WindowClose:
            return StrL("icons/window-close.svg");
        case IconName::LayoutDashboard:
            return StrL("icons/layout-dashboard.svg");
        case IconName::Calendar:
            return StrL("icons/calendar.svg");
        case IconName::Folder:
            return StrL("icons/folder.svg");
        case IconName::FolderClosed:
            return StrL("icons/folder-closed.svg");
        case IconName::Settings:
            return StrL("icons/settings.svg");
        case IconName::GalleryVerticalEnd:
            return StrL("icons/gallery-vertical-end.svg");
        case IconName::CircleUser:
            return StrL("icons/circle-user.svg");
        case IconName::User:
            return StrL("icons/user.svg");
        case IconName::PanelLeft:
            return StrL("icons/panel-left.svg");
        case IconName::PanelLeftOpen:
            return StrL("icons/panel-left-open.svg");
        case IconName::PanelLeftClose:
            return StrL("icons/panel-left-close.svg");
        case IconName::PanelRight:
            return StrL("icons/panel-right.svg");
        case IconName::PanelRightOpen:
            return StrL("icons/panel-right-open.svg");
        case IconName::PanelBottom:
            return StrL("icons/panel-bottom.svg");
        case IconName::PanelBottomOpen:
            return StrL("icons/panel-bottom-open.svg");
        case IconName::PanelRightClose:
            return StrL("icons/panel-right-close.svg");
        case IconName::Info:
            return StrL("icons/info.svg");
        case IconName::Inspector:
            return StrL("icons/inspector.svg");
        case IconName::Close:
            return StrL("icons/close.svg");
        case IconName::X:
            return StrL("icons/x.svg");
        case IconName::CircleCheck:
            return StrL("icons/circle-check.svg");
        case IconName::TriangleAlert:
            return StrL("icons/triangle-alert.svg");
        case IconName::CircleX:
            return StrL("icons/circle-x.svg");
        case IconName::Loader:
            return StrL("icons/loader.svg");
        case IconName::LoaderCircle:
            return StrL("icons/loader-circle.svg");
        case IconName::Ellipsis:
            return StrL("icons/ellipsis.svg");
        case IconName::EllipsisVertical:
            return StrL("icons/ellipsis-vertical.svg");
        case IconName::ChevronsUpDown:
            return StrL("icons/chevrons-up-down.svg");
        case IconName::SquareTerminal:
            return StrL("icons/square-terminal.svg");
        case IconName::BookOpen:
            return StrL("icons/book-open.svg");
        case IconName::Settings2:
            return StrL("icons/settings-2.svg");
        case IconName::Frame:
            return StrL("icons/frame.svg");
        case IconName::ChartPie:
            return StrL("icons/chart-pie.svg");
        case IconName::Palette:
            return StrL("icons/palette.svg");
        case IconName::File:
            return StrL("icons/file.svg");
        case IconName::FileText:
            return StrL("icons/file-text.svg");
        case IconName::RotateCw:
            return StrL("icons/rotate-cw.svg");
        case IconName::FolderOpen:
            return StrL("icons/folder-open.svg");
        case IconName::ChevronDown:
            return StrL("icons/chevron-down.svg");
        case IconName::ChevronLeft:
            return StrL("icons/chevron-left.svg");
        case IconName::ChevronRight:
            return StrL("icons/chevron-right.svg");
        case IconName::CaseSensitive:
            return StrL("icons/case-sensitive.svg");
        case IconName::Replace:
            return StrL("icons/replace.svg");
        case IconName::ChevronUp:
            return StrL("icons/chevron-up.svg");
        case IconName::Check:
            return StrL("icons/check.svg");
        case IconName::Search:
            return StrL("icons/search.svg");
        case IconName::Minus:
            return StrL("icons/minus.svg");
        case IconName::Plus:
            return StrL("icons/plus.svg");
        case IconName::Copy:
            return StrL("icons/copy.svg");
        case IconName::Dash:
            return StrL("icons/dash.svg");
        case IconName::Delete:
            return StrL("icons/delete.svg");
        case IconName::Menu:
            return StrL("icons/menu.svg");
        case IconName::Network:
            return StrL("icons/network.svg");
        case IconName::Pause:
            return StrL("icons/pause.svg");
        case IconName::Redo:
            return StrL("icons/redo.svg");
        case IconName::Redo2:
            return StrL("icons/redo-2.svg");
        case IconName::ResizeCorner:
            return StrL("icons/resize-corner.svg");
        case IconName::SortAscending:
            return StrL("icons/sort-ascending.svg");
        case IconName::SortDescending:
            return StrL("icons/sort-descending.svg");
        case IconName::StarOff:
            return StrL("icons/star-off.svg");
        case IconName::ThumbsDown:
            return StrL("icons/thumbs-down.svg");
        case IconName::ThumbsUp:
            return StrL("icons/thumbs-up.svg");
        case IconName::Undo:
            return StrL("icons/undo.svg");
        case IconName::Undo2:
            return StrL("icons/undo-2.svg");
        default:
            return {};
    }
}

bool SvgRasterize(PaintApp* pa, Str assetPath, int px, Rgba color,
                  uint8_t* outBgra) {
    if (!pa || px <= 0 || !outBgra) {
        return false;
    }
    PaintCtx ctx = {};
    ctx.pa = pa;
    ctx.dpi = 96;
    ctx.viewW = (float)px;
    ctx.viewH = (float)px;
    if (!PaintTargetBeginOffscreen(&ctx, px, px)) {
        return false;
    }
    bool drew = SvgDraw(&ctx, assetPath, 0, 0, (float)px, color);
    bool ok = PaintTargetEndOffscreen(&ctx, outBgra);
    return drew && ok;
}

bool SvgRasterizeXml(PaintApp* pa, Str xml, int px, Rgba color,
                     uint8_t* outBgra) {
    if (!pa || px <= 0 || !outBgra) {
        return false;
    }
    PaintCtx ctx = {};
    ctx.pa = pa;
    ctx.dpi = 96;
    ctx.viewW = (float)px;
    ctx.viewH = (float)px;
    if (!PaintTargetBeginOffscreen(&ctx, px, px)) {
        return false;
    }
    bool drew = SvgDrawXml(&ctx, xml, 0, 0, (float)px, color);
    bool ok = PaintTargetEndOffscreen(&ctx, outBgra);
    return drew && ok;
}

} // namespace gpui
