/* Canvas2D backend for Paint.h.

   The browser's 2D context is y-down with clockwise-increasing angles, the
   same convention the element tree uses, so nothing here flips coordinates.

   Everything below the C API is JavaScript, reached through EM_JS. The state
   the two halves share — the contexts, the paths, the images, the shaped
   lines — lives on `globalThis.__gpui` and is handed back to C++ as integer
   ids, which is the same arrangement the other three backends have with their
   opaque pointers.

   Two things this cannot do that the hosted backends can, both because the
   platform underneath is asynchronous where Paint.h is not:

   - `RenderImageDecode` answers before the picture has been decoded. The
     browser will not decode one synchronously, so the RenderImage is loading
     until its DOM Image fires load or error; either result wakes the window.
     gpui/image.h keeps the handle but returns it to layout and paint only once
     it is ready. SVG never goes through here — src/gpui/svg.h turns it into
     draw ops, which is most of what this tree draws.
   - Text is shaped by `measureText`, so wrapping is the greedy word-then-
     character break done here rather than a real line breaker. It agrees with
     Pango and DirectWrite on everything Latin. */

#include "gpui/paint.h"

#include <emscripten/emscripten.h>
#include <math.h>

namespace gpui {

static uint64_t gNextPaintResourceGeneration = 1;

static uint64_t PaintResourceGenerationNew() {
    uint64_t id = gNextPaintResourceGeneration++;
    if (id == 0) {
        id = gNextPaintResourceGeneration++;
    }
    return id;
}

// ─── the JavaScript half ──────────────────────────────────────────────────
//
// One state object, built once. Every other EM_JS body starts by picking it
// up, so nothing here depends on the order the browser gets to them in.

// clang-format off
EM_JS(void, GpJsInit, (), {
    if (globalThis.__gpui) {
        return;
    }
    const G = {
        // The <canvas> the window draws into, its context, and the device
        // pixel ratio its backing store is sized for.
        canvas: null,
        ctx: null,
        dpr: 1,
        // The context drawing is going to right now: the window's, or an
        // offscreen one while a bitmap is being rasterized.
        cur: null,
        off: null,
        offCtx: null,
        // Handle tables. Slot 0 is never used, so 0 is "no handle" in C++
        // the way a null pointer is for the other backends.
        paths: [null],
        pathFree: [],
        images: [null],
        imageFree: [],
        texts: [null],
        textFree: [],
        // "rgba(...)" strings, keyed by the packed colour. A frame paints a
        // few dozen distinct colours and tens of thousands of primitives.
        css: new Map(),
        // Font metrics, keyed by the canvas font string.
        metrics: new Map(),
        // A scratch context for measuring text with no target bound.
        meas: null,
        dec: new TextDecoder("utf-8")
    };
    G.alloc = function(table, free, obj) {
        if (free.length > 0) {
            const id = free.pop();
            table[id] = obj;
            return id;
        }
        table.push(obj);
        return table.length - 1;
    };
    G.release = function(table, free, id) {
        if (id > 0 && id < table.length && table[id]) {
            table[id] = null;
            free.push(id);
        }
    };
    // 0xRRGGBBAA to a CSS colour.
    G.color = function(c) {
        c = c >>> 0;
        let s = G.css.get(c);
        if (s === undefined) {
            const a = (c & 255) / 255;
            s = "rgba(" + ((c >>> 24) & 255) + "," + ((c >>> 16) & 255) + "," +
                ((c >>> 8) & 255) + "," + a + ")";
            G.css.set(c, s);
        }
        return s;
    };
    G.str = function(ptr, len) {
        if (!ptr || len <= 0) {
            return "";
        }
        return G.dec.decode(HEAPU8.subarray(ptr, ptr + len));
    };
    // The UTF-8 length of a JavaScript string, which is the unit every offset
    // crossing this boundary is measured in: Str carries UTF-8 bytes.
    G.u8len = function(s) {
        let n = 0;
        for (let i = 0; i < s.length; i++) {
            const c = s.charCodeAt(i);
            if (c < 0x80) {
                n += 1;
            } else if (c < 0x800) {
                n += 2;
            } else if (c >= 0xd800 && c < 0xdc00) {
                n += 4;
                i++;
            } else {
                n += 3;
            }
        }
        return n;
    };
    // How many UTF-16 units of `s` make up its first `bytes` UTF-8 ones.
    G.u16at = function(s, bytes) {
        let n = 0;
        for (let i = 0; i < s.length; i++) {
            if (n >= bytes) {
                return i;
            }
            const c = s.charCodeAt(i);
            if (c < 0x80) {
                n += 1;
            } else if (c < 0x800) {
                n += 2;
            } else if (c >= 0xd800 && c < 0xdc00) {
                n += 4;
                i++;
            } else {
                n += 3;
            }
        }
        return s.length;
    };
    // A paragraph as the units a greedy wrap may break between: each is one
    // run of non-space plus the spaces that follow it, so concatenating them
    // gives the paragraph back and a break never loses a space.
    G.words = function(para) {
        const out = [];
        let i = 0;
        while (i < para.length) {
            let j = i;
            while (j < para.length && para.charCodeAt(j) > 32) {
                j++;
            }
            while (j < para.length && para.charCodeAt(j) <= 32) {
                j++;
            }
            out.push(para.slice(i, j));
            i = j;
        }
        return out.length > 0 ? out : [para];
    };
    G.measurer = function() {
        if (!G.meas) {
            if (typeof document !== "undefined") {
                const c = document.createElement("canvas");
                c.width = 8;
                c.height = 8;
                G.meas = c.getContext("2d");
            } else {
                // The wasm tests run under Node, where there is no DOM. They
                // still exercise wrapping and hit testing, so give them a
                // stable Canvas2D-shaped measurer. A browser always takes the
                // real canvas branch above; this never affects rendered text.
                G.meas = {
                    font: "400 16px sans-serif",
                    measureText: function(s) {
                        const font = this.font || "400 16px sans-serif";
                        const marker = font.indexOf("px");
                        let start = marker;
                        while (start > 0) {
                            const code = font.charCodeAt(start - 1);
                            if ((code < 48 || code > 57) && code !== 46) {
                                break;
                            }
                            start--;
                        }
                        const parsed = marker > start
                            ? Number(font.slice(start, marker)) : 16;
                        const px = parsed > 0 ? parsed : 16;
                        let units = 0;
                        for (let i = 0; i < s.length; i++) {
                            const code = s.charCodeAt(i);
                            if (code === 9) {
                                units += 4;
                            } else if (code === 32) {
                                units += 0.33;
                            } else if (code >= 0xd800 && code < 0xdc00) {
                                units += 1;
                                i++;
                            } else if (code < 0x80) {
                                units += 0.6;
                            } else {
                                units += 1;
                            }
                        }
                        return {
                            width: units * px,
                            fontBoundingBoxAscent: px * 0.8,
                            fontBoundingBoxDescent: px * 0.2
                        };
                    }
                };
            }
        }
        return G.cur || G.meas;
    };
    // The font's own line box: how far the glyphs reach above and below the
    // baseline. Every backend here centres that inside GPUI's phi-tall box.
    G.fontMetrics = function(font, px) {
        let m = G.metrics.get(font);
        if (m === undefined) {
            const c = G.measurer();
            const prev = c.font;
            c.font = font;
            const t = c.measureText("Hgjpq");
            let asc = t.fontBoundingBoxAscent;
            let desc = t.fontBoundingBoxDescent;
            if (!(asc > 0)) {
                // Older Safari has only the ink box. The 0.8 / 0.2 split is
                // what a text engine reports for the families below.
                asc = px * 0.8;
                desc = px * 0.2;
            }
            m = {asc: asc, desc: desc};
            G.metrics.set(font, m);
            c.font = prev;
        }
        return m;
    };
    globalThis.__gpui = G;
});

// Bind the window's canvas as the target and open a batch. `cssW` / `cssH`
// are DIPs; the backing store is that times the device pixel ratio and the
// context is scaled to match, so everything above this line keeps writing
// DIPs and the glyphs still come out at the display's own resolution.
EM_JS(int, GpJsTargetBegin, (int cssW, int cssH), {
    const G = globalThis.__gpui;
    if (!G || !G.canvas || cssW <= 0 || cssH <= 0) {
        return 0;
    }
    const dpr = globalThis.devicePixelRatio || 1;
    const pxW = Math.max(1, Math.round(cssW * dpr));
    const pxH = Math.max(1, Math.round(cssH * dpr));
    if (G.canvas.width !== pxW || G.canvas.height !== pxH) {
        G.canvas.width = pxW;
        G.canvas.height = pxH;
    }
    G.dpr = dpr;
    G.cur = G.ctx;
    G.cur.setTransform(dpr, 0, 0, dpr, 0, 0);
    G.cur.save();
    return 1;
});

EM_JS(int, GpJsTargetBeginOffscreen, (int pxW, int pxH), {
    const G = globalThis.__gpui;
    if (!G || pxW <= 0 || pxH <= 0) {
        return 0;
    }
    if (!G.off) {
        G.off = document.createElement("canvas");
    }
    G.off.width = pxW;
    G.off.height = pxH;
    G.offCtx = G.off.getContext("2d", {willReadFrequently: true});
    if (!G.offCtx) {
        return 0;
    }
    G.offCtx.setTransform(1, 0, 0, 1, 0, 0);
    G.offCtx.clearRect(0, 0, pxW, pxH);
    G.cur = G.offCtx;
    G.cur.save();
    return 1;
});

// Paint.h asks for premultiplied BGRA, top-down; getImageData answers
// straight RGBA, top-down.
EM_JS(int, GpJsTargetEndOffscreen, (uint8_t* out), {
    const G = globalThis.__gpui;
    if (!G || !G.offCtx) {
        return 0;
    }
    if (out) {
        const w = G.off.width, h = G.off.height;
        const d = G.offCtx.getImageData(0, 0, w, h).data;
        const n = w * h;
        for (let i = 0; i < n; i++) {
            const s = i * 4;
            const a = d[s + 3];
            HEAPU8[out + s + 0] = (d[s + 2] * a + 127) / 255 | 0;
            HEAPU8[out + s + 1] = (d[s + 1] * a + 127) / 255 | 0;
            HEAPU8[out + s + 2] = (d[s + 0] * a + 127) / 255 | 0;
            HEAPU8[out + s + 3] = a;
        }
    }
    G.cur.restore();
    G.cur = null;
    return 1;
});

EM_JS(void, GpJsTargetEnd, (), {
    const G = globalThis.__gpui;
    if (G && G.cur) {
        G.cur.restore();
        G.cur = null;
    }
});

// ─── canvas ───────────────────────────────────────────────────────────────

EM_JS(void, GpJsClear, (int color), {
    const G = globalThis.__gpui;
    const c = G && G.cur;
    if (!c) {
        return;
    }
    // Under the transform in force one unit is a DIP, so the whole surface is
    // reached by undoing it rather than by dividing the backing store out.
    c.save();
    c.setTransform(1, 0, 0, 1, 0, 0);
    c.clearRect(0, 0, c.canvas.width, c.canvas.height);
    c.fillStyle = G.color(color);
    c.fillRect(0, 0, c.canvas.width, c.canvas.height);
    c.restore();
});

EM_JS(void, GpJsFillRect,
      (float x, float y, float w, float h, int color), {
    const c = globalThis.__gpui.cur;
    if (!c) {
        return;
    }
    c.fillStyle = globalThis.__gpui.color(color);
    c.fillRect(x, y, w, h);
});

// The rounded rectangle every fill, stroke and clip here is built from.
// `roundRect` has been in every engine since 2023; the arcTo fallback keeps
// an older one drawing rather than throwing.
EM_JS(void, GpJsRoundPath, (float x, float y, float w, float h, float r), {
    const c = globalThis.__gpui.cur;
    const rmax = Math.min(w, h) * 0.5;
    if (r > rmax) {
        r = rmax;
    }
    c.beginPath();
    if (r <= 0) {
        c.rect(x, y, w, h);
    } else if (c.roundRect) {
        c.roundRect(x, y, w, h, r);
    } else {
        c.moveTo(x + r, y);
        c.arcTo(x + w, y, x + w, y + h, r);
        c.arcTo(x + w, y + h, x, y + h, r);
        c.arcTo(x, y + h, x, y, r);
        c.arcTo(x, y, x + w, y, r);
        c.closePath();
    }
});

EM_JS(void, GpJsFill, (int color), {
    const c = globalThis.__gpui.cur;
    c.fillStyle = globalThis.__gpui.color(color);
    c.fill();
});

// `dash` is a two-element {on, off} pattern in stroke widths, the way D2D
// measures it; canvas measures in user units, so it is multiplied out here.
EM_JS(void, GpJsStroke,
      (float stroke, int color, float dashOn, float dashOff, int roundCaps), {
    const c = globalThis.__gpui.cur;
    c.strokeStyle = globalThis.__gpui.color(color);
    c.lineWidth = stroke;
    c.lineCap = roundCaps ? "round" : "butt";
    c.lineJoin = roundCaps ? "round" : "miter";
    if (dashOn > 0 || dashOff > 0) {
        c.setLineDash([dashOn * stroke, dashOff * stroke]);
    }
    c.stroke();
    if (dashOn > 0 || dashOff > 0) {
        c.setLineDash([]);
    }
    c.lineCap = "butt";
    c.lineJoin = "miter";
});

EM_JS(void, GpJsLine,
      (float x1, float y1, float x2, float y2), {
    const c = globalThis.__gpui.cur;
    c.beginPath();
    c.moveTo(x1, y1);
    c.lineTo(x2, y2);
});

EM_JS(void, GpJsEllipsePath,
      (float cx, float cy, float rx, float ry), {
    const c = globalThis.__gpui.cur;
    c.beginPath();
    c.ellipse(cx, cy, rx, ry, 0, 0, Math.PI * 2);
});

EM_JS(void, GpJsPushClip, (float x, float y, float w, float h), {
    const c = globalThis.__gpui.cur;
    if (!c) {
        return;
    }
    c.save();
    c.beginPath();
    c.rect(x, y, w, h);
    c.clip();
});

EM_JS(void, GpJsPopClip, (), {
    const c = globalThis.__gpui.cur;
    if (c) {
        c.restore();
    }
});

// ─── paths ────────────────────────────────────────────────────────────────
//
// The ops are recorded in C++ and handed over in one buffer, rather than one
// call per segment: a chart's area is a few hundred segments and a frame has
// several of them.

EM_JS(int, GpJsPathBuild, (const float* ops, int n, int winding), {
    const G = globalThis.__gpui;
    const p = new Path2D();
    let open = false;
    for (let i = 0; i < n; i++) {
        const b = (ops >> 2) + i * 8;
        const cmd = HEAPF32[b];
        const a0 = HEAPF32[b + 1], a1 = HEAPF32[b + 2], a2 = HEAPF32[b + 3];
        const a3 = HEAPF32[b + 4], a4 = HEAPF32[b + 5], a5 = HEAPF32[b + 6];
        const a6 = HEAPF32[b + 7];
        if (cmd === 0) {
            p.moveTo(a0, a1);
            open = true;
        } else if (cmd === 1) {
            p.lineTo(a0, a1);
        } else if (cmd === 2) {
            p.bezierCurveTo(a0, a1, a2, a3, a4, a5);
        } else if (cmd === 3) {
            // arc draws a line from the current point to the arc's start,
            // the way cairo_arc does, so an arc opens a figure too.
            if (!open) {
                p.moveTo(a0 + a2 * Math.cos(a3), a1 + a2 * Math.sin(a3));
                open = true;
            }
            p.arc(a0, a1, a2, a3, a4, a6 === 0);
        } else if (cmd === 4) {
            p.closePath();
            open = false;
        }
    }
    return G.alloc(G.paths, G.pathFree, {p: p, winding: winding !== 0});
});

EM_JS(void, GpJsPathFill, (int id, int color, float dx, float dy), {
    const G = globalThis.__gpui;
    const c = G.cur, e = G.paths[id];
    if (!c || !e) {
        return;
    }
    c.save();
    c.translate(dx, dy);
    c.fillStyle = G.color(color);
    c.fill(e.p, e.winding ? "nonzero" : "evenodd");
    c.restore();
});

EM_JS(void, GpJsPathFillGradient,
      (int id, float x0, float y0, float x1, float y1, int from, int to,
       float dx, float dy), {
    const G = globalThis.__gpui;
    const c = G.cur, e = G.paths[id];
    if (!c || !e) {
        return;
    }
    c.save();
    c.translate(dx, dy);
    const g = c.createLinearGradient(x0, y0, x1, y1);
    g.addColorStop(0, G.color(from));
    g.addColorStop(1, G.color(to));
    c.fillStyle = g;
    c.fill(e.p, e.winding ? "nonzero" : "evenodd");
    c.restore();
});

EM_JS(void, GpJsPathStroke,
      (int id, float stroke, int color, int roundCaps, float dx, float dy), {
    const G = globalThis.__gpui;
    const c = G.cur, e = G.paths[id];
    if (!c || !e) {
        return;
    }
    c.save();
    c.translate(dx, dy);
    c.strokeStyle = G.color(color);
    c.lineWidth = stroke;
    c.lineCap = roundCaps ? "round" : "butt";
    c.lineJoin = roundCaps ? "round" : "miter";
    c.stroke(e.p);
    c.lineCap = "butt";
    c.lineJoin = "miter";
    c.restore();
});

EM_JS(void, GpJsPathFree, (int id), {
    const G = globalThis.__gpui;
    G.release(G.paths, G.pathFree, id);
});

// ─── images ───────────────────────────────────────────────────────────────

EM_JS(int, GpJsImageDecode, (const uint8_t* bytes, int len), {
    const G = globalThis.__gpui;
    // The bytes have to be copied: HEAPU8 is a view on memory that moves when
    // the heap grows, and the decode outlives this call.
    const copy = new Uint8Array(HEAPU8.subarray(bytes, bytes + len));
    const url = URL.createObjectURL(new Blob([copy]));
    let animated = false;
    if (len >= 13 && copy[0] == 71 && copy[1] == 73 && copy[2] == 70) {
        let at = 13;
        const packed = copy[10];
        if (packed & 128) {
            at += 3 * (1 << ((packed & 7) + 1));
        }
        let frames = 0;
        while (at < len && frames < 2) {
            const kind = copy[at++];
            if (kind == 59) {
                break;
            }
            if (kind == 33) {
                at++;
            } else if (kind == 44) {
                frames++;
                if (at + 9 > len) {
                    break;
                }
                const imagePacked = copy[at + 8];
                at += 9;
                if (imagePacked & 128) {
                    at += 3 * (1 << ((imagePacked & 7) + 1));
                }
                at++;
            } else {
                break;
            }
            while (at < len) {
                const block = copy[at++];
                if (!block) {
                    break;
                }
                at += block;
            }
        }
        animated = frames > 1;
    }
    if (!animated && len >= 16 && copy[0] == 82 && copy[1] == 73 &&
        copy[2] == 70 && copy[3] == 70) {
        for (let i = 12; i + 4 <= len; i++) {
            if (copy[i] == 65 && copy[i + 1] == 78 && copy[i + 2] == 73 &&
                copy[i + 3] == 77) {
                animated = true;
                break;
            }
        }
    }
    const rec = {
        img: new Image(),
        w: 0,
        h: 0,
        status: 0,
        url: url,
        animated: animated
    };
    rec.img.onload = function() {
        rec.w = rec.img.naturalWidth;
        rec.h = rec.img.naturalHeight;
        rec.status = 1;
        URL.revokeObjectURL(url);
        rec.url = null;
        // The frame that asked for this measured it at nothing. Draw another.
        if (typeof _gpui_wasm_wake === "function") {
            _gpui_wasm_wake();
        }
    };
    rec.img.onerror = function() {
        rec.status = 2;
        rec.img = null;
        URL.revokeObjectURL(url);
        rec.url = null;
        if (typeof _gpui_wasm_wake === "function") {
            _gpui_wasm_wake();
        }
    };
    rec.img.src = url;
    return G.alloc(G.images, G.imageFree, rec);
});

EM_JS(int, GpJsImageW, (int id), {
    const e = globalThis.__gpui.images[id];
    return e ? e.w : 0;
});

EM_JS(int, GpJsImageH, (int id), {
    const e = globalThis.__gpui.images[id];
    return e ? e.h : 0;
});

EM_JS(int, GpJsImageStatus, (int id), {
    const e = globalThis.__gpui.images[id];
    return e ? e.status : 2;
});

EM_JS(int, GpJsImageFrameCount, (int id), {
    const e = globalThis.__gpui.images[id];
    return e && e.animated ? 2 : (e ? 1 : 0);
});

EM_JS(void, GpJsImageDraw,
      (int id, float x, float y, float w, float h, float imageX, float imageY,
       float imageW, float imageH, float alpha, float r, int grayscale), {
    const G = globalThis.__gpui;
    const c = G.cur, e = G.images[id];
    if (!c || !e || e.w <= 0 || e.h <= 0) {
        return;
    }
    c.save();
    c.globalAlpha = alpha;
    if (grayscale) {
        c.filter = "grayscale(1)";
    }
    if (r > 0) {
        c.beginPath();
        c.moveTo(x + r, y);
        c.arcTo(x + w, y, x + w, y + h, r);
        c.arcTo(x + w, y + h, x, y + h, r);
        c.arcTo(x, y + h, x, y, r);
        c.arcTo(x, y, x + w, y, r);
        c.closePath();
        c.clip();
    } else {
        c.beginPath();
        c.rect(x, y, w, h);
        c.clip();
    }
    c.drawImage(e.img, imageX, imageY, imageW, imageH);
    c.restore();
});

EM_JS(void, GpJsImageFree, (int id), {
    const G = globalThis.__gpui;
    const e = G.images[id];
    if (e && e.img) {
        e.img.onload = null;
        e.img.onerror = null;
    }
    if (e && e.url) {
        URL.revokeObjectURL(e.url);
    }
    G.release(G.images, G.imageFree, id);
});

// ─── shaped text ──────────────────────────────────────────────────────────
//
// A layout is the text broken into lines, each with the UTF-8 offset it
// starts at, plus the font it was measured with. Every offset that crosses
// this boundary is a UTF-8 byte count, because that is what Str carries.

EM_JS(int, GpJsTextNew,
      (const uint8_t* ptr, int len, float fontSize, float maxW, int wrap,
       int weightBits, float lineH, float* outSize), {
    const G = globalThis.__gpui;
    const text = G.str(ptr, len);
    if (text.length === 0) {
        return 0;
    }
    const mono = (weightBits & 16) !== 0;
    const italic = (weightBits & 64) !== 0;
    const underline = (weightBits & 32) !== 0;
    const strike = (weightBits & 128) !== 0;
    let w = 400;
    const wb = weightBits & 15;
    if (wb > 0) {
        w = wb * 100;
    } else if (fontSize >= 18) {
        // The 20 px and 24 px DirectWrite formats are created semibold and a
        // run that asks for no weight inherits that. The other backends match
        // it here; so does this one.
        w = 600;
    }
    // Unquoted family names on purpose: CSS takes a multi-word family as a
    // run of identifiers, and a quote of either kind inside this literal
    // would not survive the preprocessor that turns this body into a string.
    const family = mono
        ? "ui-monospace, SFMono-Regular, Menlo, Consolas, Liberation Mono, monospace"
        : "system-ui, -apple-system, Segoe UI, Roboto, Helvetica Neue, Arial, sans-serif";
    const font = (italic ? "italic " : "") + w + " " + fontSize + "px " + family;

    const c = G.measurer();
    const prevFont = c.font;
    c.font = font;
    const width = function(s) { return c.measureText(s).width; };

    // Greedy word wrap, falling back to breaking inside a word that will not
    // fit on a line of its own. Hard newlines break first, always.
    //
    // `starts` is the UTF-8 offset each line begins at, in the string that
    // came in. A wrap consumes nothing, so the lines of one paragraph run
    // straight on from each other; the newline between two paragraphs is the
    // one byte that belongs to neither.
    const lines = [];
    const starts = [];
    // Whether the line was ended by a wrap rather than by the text running
    // out. It decides what the line is as wide as: a space a wrap left at the
    // end of a line is not drawn and does not count, but a space the text
    // really ends with does — an inline run measured on its own is how a
    // paragraph made of several of them keeps the gaps between its words.
    const wrapped = [];
    const paras = text.split("\n");
    let at = 0;
    const emit = function(line, byWrap) {
        lines.push(line);
        starts.push(at);
        wrapped.push(byWrap);
        at += G.u8len(line);
    };
    for (let pi = 0; pi < paras.length; pi++) {
        const para = paras[pi];
        if (!wrap || maxW <= 0 || width(para) <= maxW) {
            emit(para, false);
        } else {
            let line = "";
            // Keep the space with the word before it, so a break never loses
            // one and the offsets stay a partition of the source.
            const words = G.words(para);
            for (let i = 0; i < words.length; i++) {
                let word = words[i];
                if (line !== "" && width(line + word) > maxW) {
                    emit(line, true);
                    line = "";
                }
                while (width(word) > maxW && word.length > 1) {
                    let cut = 1;
                    while (cut < word.length &&
                           width(word.slice(0, cut + 1)) <= maxW) {
                        cut++;
                    }
                    emit(line + word.slice(0, cut), true);
                    line = "";
                    word = word.slice(cut);
                }
                line += word;
            }
            emit(line, false);
        }
        if (pi + 1 < paras.length) {
            at += 1; // the newline that split them
        }
    }

    let maxLine = 0;
    for (let i = 0; i < lines.length; i++) {
        const lw = width(wrapped[i] ? lines[i].trimEnd() : lines[i]);
        if (lw > maxLine) {
            maxLine = lw;
        }
    }

    const fm = G.fontMetrics(font, fontSize);
    const natural = fm.asc + fm.desc;
    const box = fontSize * (lineH > 0 ? lineH : 1.618034);
    const rec = {
        lines: lines, starts: starts, wrapped: wrapped, font: font,
        px: fontSize, box: box, natural: natural, asc: fm.asc,
        underline: underline, strike: strike,
        wrapW: (wrap && maxW > 0) ? maxW : maxLine
    };
    c.font = prevFont;
    if (outSize) {
        HEAPF32[(outSize >> 2)] = maxLine;
        HEAPF32[(outSize >> 2) + 1] = box * lines.length;
    }
    return G.alloc(G.texts, G.textFree, rec);
});

// Where the glyphs sit inside GPUI's phi-tall line box: half the slack, top
// and bottom, which is what every other backend does with it.
EM_JS(double, GpJsTextBaseline, (int id), {
    const e = globalThis.__gpui.texts[id];
    if (!e) {
        return 0;
    }
    return e.asc + (e.box - e.natural) * 0.5;
});

EM_JS(void, GpJsTextDraw, (int id, float x, float y, int color, int clip), {
    const G = globalThis.__gpui;
    const c = G.cur, e = G.texts[id];
    if (!c || !e) {
        return;
    }
    if (clip) {
        c.save();
        c.beginPath();
        c.rect(x, y, e.wrapW, e.box * e.lines.length);
        c.clip();
    }
    const pad = (e.box - e.natural) * 0.5;
    c.font = e.font;
    c.textBaseline = "alphabetic";
    c.textAlign = "left";
    c.fillStyle = G.color(color);
    for (let i = 0; i < e.lines.length; i++) {
        const base = y + pad + i * e.box + e.asc;
        c.fillText(e.lines[i], x, base);
        if (e.underline || e.strike) {
            const lw = Math.max(1, Math.round(e.px / 14));
            const w = c.measureText(
                e.wrapped[i] ? e.lines[i].trimEnd() : e.lines[i]).width;
            if (e.underline) {
                c.fillRect(x, Math.round(base + lw * 2), w, lw);
            }
            if (e.strike) {
                // Just under a third of the ascent above the baseline, which
                // is where Core Text's own strikethrough would sit.
                c.fillRect(x, Math.round(base - e.asc * 0.3), w, lw);
            }
        }
    }
    if (clip) {
        c.restore();
    }
});

EM_JS(int, GpJsTextHit, (int id, float relX, float relY), {
    const G = globalThis.__gpui;
    const e = G.texts[id];
    if (!e) {
        return 0;
    }
    const pad = (e.box - e.natural) * 0.5;
    let li = Math.floor((relY - pad) / e.box);
    if (li < 0) {
        li = 0;
    }
    if (li >= e.lines.length) {
        li = e.lines.length - 1;
    }
    const line = e.lines[li];
    const c = G.measurer();
    const prev = c.font;
    c.font = e.font;
    // The nearest gap between characters, which is where a caret goes.
    let best = 0;
    let bestD = Infinity;
    for (let i = 0; i <= line.length; i++) {
        const w = c.measureText(line.slice(0, i)).width;
        const d = Math.abs(w - relX);
        if (d < bestD) {
            bestD = d;
            best = i;
        }
    }
    c.font = prev;
    return e.starts[li] + G.u8len(line.slice(0, best));
});

// The rectangles covering UTF-8 range [a, b), one per line it crosses.
EM_JS(int, GpJsTextRangeRects,
      (int id, int a, int b, float* out, int max), {
    const G = globalThis.__gpui;
    const e = G.texts[id];
    if (!e) {
        return 0;
    }
    const c = G.measurer();
    const prev = c.font;
    c.font = e.font;
    const pad = (e.box - e.natural) * 0.5;
    let n = 0;
    for (let i = 0; i < e.lines.length && n < max; i++) {
        const line = e.lines[i];
        const start = e.starts[i];
        const end = start + G.u8len(line);
        const lo = a > start ? a : start;
        const hi = b < end ? b : end;
        if (lo >= hi) {
            continue;
        }
        const x0 = c.measureText(line.slice(0, G.u16at(line, lo - start))).width;
        const x1 = c.measureText(line.slice(0, G.u16at(line, hi - start))).width;
        const o = (out >> 2) + n * 4;
        HEAPF32[o] = x0;
        HEAPF32[o + 1] = pad + i * e.box;
        HEAPF32[o + 2] = x1 - x0;
        HEAPF32[o + 3] = e.natural;
        n++;
    }
    c.font = prev;
    return n;
});

EM_JS(void, GpJsTextFree, (int id), {
    const G = globalThis.__gpui;
    G.release(G.texts, G.textFree, id);
});
// clang-format on

// ─── the C++ half ─────────────────────────────────────────────────────────

// There is nothing process-wide to own: the browser holds the fonts and the
// contexts. PaintApp exists so the signatures match the other three.
struct PaintApp {
    int unused = 0;
};

// Nor is there a per-target object. Which context is bound is JavaScript's
// business; this only says that one is.
struct PaintTarget {
    bool offscreen = false;
};

static uint32_t Packed(PaintCtx* ctx, Rgba c) {
    c = PaintFade(ctx, c);
    return ((uint32_t)c.r << 24) | ((uint32_t)c.g << 16) |
           ((uint32_t)c.b << 8) | (uint32_t)c.a;
}

// ─── lifecycle ────────────────────────────────────────────────────────────

PaintApp* PaintAppNew() {
    GpJsInit();
    return new PaintApp();
}

void PaintAppFree(PaintApp* pa) {
    delete pa;
}

void PaintTargetFree(PaintCtx* ctx) {
    if (!ctx || !ctx->rt) {
        return;
    }
    delete ctx->rt;
    ctx->rt = nullptr;
}

bool PaintTargetBegin(PaintCtx* ctx, void* native, int pxW, int pxH) {
    (void)native;
    if (!ctx || !ctx->pa) {
        return false;
    }
    PaintTargetFree(ctx);
    if (!GpJsTargetBegin(pxW, pxH)) {
        return false;
    }
    ctx->rt = new PaintTarget();
    return true;
}

bool PaintTargetBeginOffscreen(PaintCtx* ctx, int pxW, int pxH) {
    if (!ctx || !ctx->pa) {
        return false;
    }
    PaintTargetFree(ctx);
    if (!GpJsTargetBeginOffscreen(pxW, pxH)) {
        return false;
    }
    ctx->rt = new PaintTarget();
    ctx->rt->offscreen = true;
    return true;
}

bool PaintTargetEndOffscreen(PaintCtx* ctx, uint8_t* outBgra) {
    if (!ctx || !ctx->rt || !ctx->rt->offscreen) {
        return false;
    }
    bool ok = GpJsTargetEndOffscreen(outBgra) != 0;
    PaintTargetFree(ctx);
    return ok;
}

bool PaintTargetEnd(PaintCtx* ctx) {
    if (!ctx || !ctx->rt) {
        return false;
    }
    GpJsTargetEnd();
    PaintTargetFree(ctx);
    return true;
}

// ─── canvas ───────────────────────────────────────────────────────────────

void CanvasClear(PaintCtx* ctx, Rgba c) {
    if (!ctx || !ctx->rt) {
        return;
    }
    GpJsClear((int)Packed(ctx, c));
}

void CanvasFillRect(PaintCtx* ctx, float x, float y, float w, float h, Rgba c) {
    if (!ctx || !ctx->rt || w <= 0 || h <= 0 || c.a == 0) {
        return;
    }
    GpJsFillRect(x, y, w, h, (int)Packed(ctx, c));
}

void CanvasFillRound(PaintCtx* ctx, float x, float y, float w, float h, float r,
                     Rgba c) {
    if (!ctx || !ctx->rt || w <= 0 || h <= 0 || c.a == 0) {
        return;
    }
    GpJsRoundPath(x, y, w, h, r);
    GpJsFill((int)Packed(ctx, c));
}

void CanvasStrokeRound(PaintCtx* ctx, float x, float y, float w, float h,
                       float r, float stroke, Rgba c, const float* dash) {
    if (!ctx || !ctx->rt || stroke <= 0 || w <= 0 || h <= 0) {
        return;
    }
    // Inset by half the stroke: canvas, like D2D, centers it on the path.
    GpJsRoundPath(x + stroke * 0.5f, y + stroke * 0.5f, w - stroke, h - stroke,
                  r);
    GpJsStroke(stroke, (int)Packed(ctx, c), dash ? dash[0] : 0,
               dash ? dash[1] : 0, 0);
}

void CanvasLine(PaintCtx* ctx, float x1, float y1, float x2, float y2,
                float stroke, Rgba c, const float* dash) {
    if (!ctx || !ctx->rt) {
        return;
    }
    GpJsLine(x1, y1, x2, y2);
    GpJsStroke(stroke, (int)Packed(ctx, c), dash ? dash[0] : 0,
               dash ? dash[1] : 0, 0);
}

void CanvasEllipse(PaintCtx* ctx, float cx, float cy, float rx, float ry,
                   float stroke, Rgba c) {
    if (!ctx || !ctx->rt || rx <= 0 || ry <= 0) {
        return;
    }
    GpJsEllipsePath(cx, cy, rx, ry);
    if (stroke > 0) {
        GpJsStroke(stroke, (int)Packed(ctx, c), 0, 0, 0);
    } else {
        GpJsFill((int)Packed(ctx, c));
    }
}

void CanvasPushClip(PaintCtx* ctx, float x, float y, float w, float h) {
    if (ctx && ctx->rt) {
        GpJsPushClip(x, y, w, h);
    }
}

void CanvasPopClip(PaintCtx* ctx) {
    if (ctx && ctx->rt) {
        GpJsPopClip();
    }
}

// ─── paths ────────────────────────────────────────────────────────────────
//
// Recorded here and built into one Path2D on first use, so a path that is
// filled and then stroked crosses the boundary once. Eight floats an op:
// the command, six arguments, and the arc's direction.

enum PathCmd {
    kPathMove = 0,
    kPathLine = 1,
    kPathCubic = 2,
    kPathArc = 3,
    kPathClose = 4
};

struct Path {
    Vec<float> ops;
    bool winding = true;
    bool fig = false;
    // The Path2D built from `ops`, or 0 while there is none.
    int js = 0;
};

static void Push(Path* p, float cmd, float a = 0, float b = 0, float c = 0,
                 float d = 0, float e = 0, float f = 0, float g = 0) {
    if (!p) {
        return;
    }
    const float vals[8] = {cmd, a, b, c, d, e, f, g};
    for (int i = 0; i < 8; i++) {
        VecAppend(p->ops, vals[i]);
    }
    // A path that is edited after it was drawn has to be rebuilt.
    if (p->js) {
        GpJsPathFree(p->js);
        p->js = 0;
    }
}

Path* PathNew(PaintCtx* ctx, bool winding) {
    if (!ctx) {
        return nullptr;
    }
    auto* p = new Path();
    p->winding = winding;
    return p;
}

void PathFree(Path* p) {
    if (!p) {
        return;
    }
    if (p->js) {
        GpJsPathFree(p->js);
    }
    delete p;
}

void PathMoveTo(Path* p, float x, float y) {
    if (!p) {
        return;
    }
    Push(p, kPathMove, x, y);
    p->fig = true;
}

void PathLineTo(Path* p, float x, float y) {
    if (!p) {
        return;
    }
    if (!p->fig) {
        PathMoveTo(p, x, y);
        return;
    }
    Push(p, kPathLine, x, y);
}

void PathCubicTo(Path* p, float x1, float y1, float x2, float y2, float x,
                 float y) {
    if (!p) {
        return;
    }
    if (!p->fig) {
        PathMoveTo(p, x, y);
        return;
    }
    Push(p, kPathCubic, x1, y1, x2, y2, x, y);
}

void PathArcTo(Path* p, float cx, float cy, float r, float a0, float a1,
               bool clockwise) {
    if (!p) {
        return;
    }
    Push(p, kPathArc, cx, cy, r, a0, a1, 0, clockwise ? 1.f : 0.f);
    p->fig = true;
}

void PathClose(Path* p) {
    if (!p || !p->fig) {
        return;
    }
    Push(p, kPathClose);
    p->fig = false;
}

static int JsPath(Path* p) {
    if (!p || p->ops.len == 0) {
        return 0;
    }
    if (!p->js) {
        p->js = GpJsPathBuild(p->ops.els, p->ops.len / 8, p->winding ? 1 : 0);
    }
    return p->js;
}

// Nothing to cache: this backend hands the path to Canvas2D, which owns
// whatever it wants to keep about it.
void PathRealize(PaintCtx* ctx, Path* p) {
    (void)ctx;
    (void)p;
}

void PathFill(PaintCtx* ctx, Path* p, Rgba c, float dx, float dy) {
    int id = JsPath(p);
    if (!id || !ctx || !ctx->rt) {
        return;
    }
    GpJsPathFill(id, (int)Packed(ctx, c), dx, dy);
}

void PathFillGradientV(PaintCtx* ctx, Path* p, float y0, float y1, Rgba top,
                       Rgba bot) {
    PathFillGradient(ctx, p, 0, y0, 0, y1, top, bot);
}

void PathFillGradient(PaintCtx* ctx, Path* p, float x0, float y0, float x1,
                      float y1, Rgba from, Rgba to, float dx, float dy) {
    int id = JsPath(p);
    if (!id || !ctx || !ctx->rt) {
        return;
    }
    GpJsPathFillGradient(id, x0, y0, x1, y1, (int)Packed(ctx, from),
                         (int)Packed(ctx, to), dx, dy);
}

void PathStroke(PaintCtx* ctx, Path* p, float stroke, Rgba c, bool roundCaps,
                float dx, float dy) {
    int id = JsPath(p);
    if (!id || !ctx || !ctx->rt) {
        return;
    }
    GpJsPathStroke(id, stroke, (int)Packed(ctx, c), roundCaps ? 1 : 0, dx, dy);
}

// ─── images ───────────────────────────────────────────────────────────────

struct RenderImage {
    int refs = 1;
    uint64_t generation = 0;
    int js = 0;
};

RenderImage* RenderImageDecode(PaintApp* pa, const uint8_t* bytes, int len) {
    (void)pa;
    if (!bytes || len <= 0) {
        return nullptr;
    }
    int id = GpJsImageDecode(bytes, len);
    if (!id) {
        return nullptr;
    }
    auto* img = new RenderImage();
    img->generation = PaintResourceGenerationNew();
    img->js = id;
    return img;
}

void RenderImageRetain(RenderImage* img) {
    if (img) {
        img->refs++;
    }
}

void RenderImageRelease(RenderImage* img) {
    if (!img || --img->refs != 0) {
        return;
    }
    if (img->js) {
        GpJsImageFree(img->js);
    }
    delete img;
}

uint64_t RenderImageGeneration(const RenderImage* img) {
    return img ? img->generation : 0;
}

RenderImageStatus RenderImageStatusGet(const RenderImage* img) {
    if (!img || !img->js) {
        return RenderImageStatus::Failed;
    }
    int status = GpJsImageStatus(img->js);
    return status == 0   ? RenderImageStatus::Loading
           : status == 1 ? RenderImageStatus::Ready
                         : RenderImageStatus::Failed;
}

// Zero until the browser has decoded it. The caller lays the picture out at
// nothing for a frame and draws again when GpJsImageDecode's onload wakes the
// window.
Size RenderImageSizePx(const RenderImage* img, int frameIndex) {
    (void)frameIndex;
    if (!img || !img->js) {
        return {};
    }
    return {(float)GpJsImageW(img->js), (float)GpJsImageH(img->js)};
}

int RenderImageFrameCount(const RenderImage* img) {
    return img && img->js ? GpJsImageFrameCount(img->js) : 0;
}

int RenderImageFrameDurationMs(const RenderImage* img, int frameIndex) {
    (void)img;
    (void)frameIndex;
    // The browser advances the underlying HTMLImageElement from the file's
    // own delays. This interval only keeps Canvas2D repainting while it does.
    return 16;
}

void RenderImageDraw(PaintCtx* ctx, RenderImage* img, Bounds bounds,
                     Bounds imageBounds, int frameIndex, float radius,
                     bool grayscale) {
    (void)frameIndex;
    if (!ctx || !ctx->rt || !img || !img->js || bounds.w <= 0 ||
        bounds.h <= 0 || imageBounds.w <= 0 || imageBounds.h <= 0) {
        return;
    }
    float a = ctx->opacity < 0 ? 0 : (ctx->opacity > 1 ? 1 : ctx->opacity);
    float half = (bounds.w < bounds.h ? bounds.w : bounds.h) * 0.5f;
    float r = radius > half ? half : (radius > 0 ? radius : 0.f);
    GpJsImageDraw(img->js, bounds.x, bounds.y, bounds.w, bounds.h,
                  imageBounds.x, imageBounds.y, imageBounds.w, imageBounds.h, a,
                  r, grayscale ? 1 : 0);
}

// ─── shaped text ──────────────────────────────────────────────────────────

struct TextLayout {
    uint64_t generation = 0;
    int js = 0;
    int refs = 1;
    // What TextLayoutNew reported, kept so TextLayoutSize can answer without
    // measuring again.
    Size size = {};
};

TextLayout* TextLayoutNew(PaintCtx* ctx, Str s, float fontSize, float maxW,
                          bool wrap, uint8_t weight, float lineH,
                          Size* outSize) {
    if (!ctx || !ctx->pa || !s.s || s.len <= 0) {
        return nullptr;
    }
    if (fontSize <= 0) {
        fontSize = 16.f;
    }
    float size[2] = {0, 0};
    int id = GpJsTextNew((const uint8_t*)s.s, s.len, fontSize, maxW,
                         wrap ? 1 : 0, weight, lineH, size);
    if (!id) {
        return nullptr;
    }
    if (outSize) {
        outSize->w = size[0];
        outSize->h = size[1];
    }
    auto* tl = new TextLayout();
    tl->generation = PaintResourceGenerationNew();
    tl->js = id;
    tl->size = Size{size[0], size[1]};
    return tl;
}

Size TextLayoutSize(TextLayout* tl) {
    return tl ? tl->size : Size{0, 0};
}

void TextLayoutAddRef(TextLayout* tl) {
    if (tl) {
        tl->refs++;
    }
}

void TextLayoutRelease(TextLayout* tl) {
    if (!tl) {
        return;
    }
    if (--tl->refs > 0) {
        return;
    }
    if (tl->js) {
        GpJsTextFree(tl->js);
    }
    delete tl;
}

uint64_t TextLayoutGeneration(const TextLayout* tl) {
    return tl ? tl->generation : 0;
}

bool PaintTextLayoutSpans(PaintCtx*, TextLayout*, Str, float, float, Rgba,
                          const TextSpan*, int) {
    return false;
}

void TextLayoutDraw(PaintCtx* ctx, TextLayout* tl, float x, float y, Rgba c,
                    bool clip, float clipW) {
    // No ellipsis here yet: Canvas2D has no trimming, so it would have to be
    // measured and appended by hand. A truncated run is cut at the box edge.
    (void)clipW;
    if (!ctx || !ctx->rt || !tl || !tl->js) {
        return;
    }
    GpJsTextDraw(tl->js, x, y, (int)Packed(ctx, c), clip ? 1 : 0);
}

int TextLayoutHitPoint(TextLayout* tl, Str s, float relX, float relY) {
    if (!tl || !tl->js) {
        return 0;
    }
    int at = GpJsTextHit(tl->js, relX, relY);
    if (at < 0) {
        at = 0;
    }
    if (at > s.len) {
        at = s.len;
    }
    return at;
}

float TextLayoutBaseline(TextLayout* tl) {
    return tl && tl->js ? (float)GpJsTextBaseline(tl->js) : 0.f;
}

int TextLayoutRangeRects(TextLayout* tl, Str s, int u8a, int u8b, Bounds* out,
                         int max) {
    (void)s;
    if (!tl || !tl->js || !out || max <= 0 || u8a >= u8b) {
        return 0;
    }
    // Bounds is four floats in x, y, w, h order, which is what the JavaScript
    // writes; anything else here would need a copy.
    static_assert(sizeof(Bounds) == 4 * sizeof(float),
                  "Bounds must be four floats for GpJsTextRangeRects");
    return GpJsTextRangeRects(tl->js, u8a, u8b, (float*)out, max);
}

} // namespace gpui
