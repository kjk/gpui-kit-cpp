// Converts `.svg` files into the draw-op byte stream `src/gpui/drawops.h`
// describes, and writes `src/gpui/asset_icons.cpp`: one byte array holding
// every icon end to end, plus the offset and length of each.
//
//   bun cmd/svg-to-bytecode.ts                    # assets/icons -> src/gpui/asset_icons.cpp
//   bun cmd/svg-to-bytecode.ts a.svg b.svg        # those files, to stdout as C
//   bun cmd/svg-to-bytecode.ts -o out.cpp dir/    # a directory of them, to a file
//
// This is the same conversion `SvgToDrawOps` in `src/gpui/svg.cpp` does at
// runtime for a file that is not in the table — the reader below is a port of
// that one, tag scanner, path parser, arcs and all. A change to what either
// emits belongs in both, or an icon will draw differently depending on where
// it came from. Nothing checks that for you.

import { existsSync, readdirSync, readFileSync, writeFileSync, statSync } from "node:fs";
import { basename, join, relative } from "node:path";

const root = join(import.meta.dir, "..");

// ─── the opcodes, from src/gpui/drawops.h ───────────────────────────────────

const OP_END = 0;
const OP_VIEWBOX = 1;
const OP_STROKE_WIDTH = 2;
const OP_COLOR = 3;
const OP_COLOR_RESET = 4;
const OP_MOVE_TO = 11;
const OP_LINE_TO = 12;
const OP_CUBIC_TO = 13;
const OP_CLOSE_PATH = 14;
const OP_FILL_PATH = 15;
const OP_STROKE_PATH = 16;
const OP_FILL_STROKE_PATH = 17;

class Writer {
  bytes: number[] = [];

  op(v: number): void {
    this.bytes.push(v & 0xff, (v >> 8) & 0xff);
  }

  // f32, little-endian, and rounded to f32 first so the bytes are what a C++
  // `float` of the same expression would hold.
  f(v: number): void {
    const buf = new DataView(new ArrayBuffer(4));
    buf.setFloat32(0, Math.fround(v), true);
    for (let i = 0; i < 4; i++) this.bytes.push(buf.getUint8(i));
  }

  u32(v: number): void {
    this.bytes.push((v >>> 24) & 0xff, (v >>> 16) & 0xff, (v >>> 8) & 0xff, v & 0xff);
  }
}

// ─── the reader (a port of src/gpui/svg.cpp) ────────────────────────────────

const MOVE = 0;
const LINE = 1;
const CUBIC = 2;
const CLOSE = 3;

type SvgOp = { cmd: number; x: number; y: number; x1: number; y1: number; x2: number; y2: number };
type SvgShape = { start: number; count: number; fill: number | null; stroke: number | null };

type SvgIcon = {
  vbX: number;
  vbY: number;
  vbW: number;
  vbH: number;
  strokeW: number;
  filled: boolean;
  stroked: boolean;
  hasOwnColors: boolean;
  ops: SvgOp[];
  shapes: SvgShape[];
};

// Which of the three path ops says "fill this", "stroke this", or both. A
// path that is neither filled nor stroked would draw nothing, and an icon
// that says so is a mistake rather than an instruction, so it is stroked.
function pathOp(filled: boolean, stroked: boolean): number {
  if (filled && stroked) return OP_FILL_STROKE_PATH;
  return filled ? OP_FILL_PATH : OP_STROKE_PATH;
}

function newIcon(): SvgIcon {
  return {
    vbX: 0,
    vbY: 0,
    vbW: 24,
    vbH: 24,
    strokeW: 2,
    // SVG's own defaults, which are what GPUI's renderer applies: fill black,
    // stroke none. So an icon that names neither -- github.svg is the one
    // here -- is filled and not stroked, a Lucide icon says fill="none"
    // stroke="currentColor" and is stroked and not filled, and a solid
    // variant (star-fill) says currentColor for both and is both.
    filled: true,
    stroked: false,
    hasOwnColors: false,
    ops: [],
    shapes: [],
  };
}

function addOp(ic: SvgIcon, cmd: number, x = 0, y = 0, x1 = 0, y1 = 0, x2 = 0, y2 = 0): void {
  ic.ops.push({ cmd, x, y, x1, y1, x2, y2 });
}

const addMove = (ic: SvgIcon, x: number, y: number) => addOp(ic, MOVE, x, y);
const addLine = (ic: SvgIcon, x: number, y: number) => addOp(ic, LINE, x, y);
const addClose = (ic: SvgIcon) => addOp(ic, CLOSE);
const addCubic = (ic: SvgIcon, x1: number, y1: number, x2: number, y2: number, x: number, y: number) =>
  addOp(ic, CUBIC, x, y, x1, y1, x2, y2);

// Four cubics, one per quadrant. addRoundRect below cannot stand in for
// this: it takes one corner radius for both axes, so an <ellipse> drawn with
// it comes out a stadium. <circle> still goes through addRoundRect, where its
// bytes are what the compiled icon table holds.
function addEllipse(ic: SvgIcon, cx: number, cy: number, rx: number, ry: number): void {
  if (rx <= 0 || ry <= 0) return;
  const kx = rx * 0.55228475;
  const ky = ry * 0.55228475;
  addMove(ic, cx + rx, cy);
  addCubic(ic, cx + rx, cy + ky, cx + kx, cy + ry, cx, cy + ry);
  addCubic(ic, cx - kx, cy + ry, cx - rx, cy + ky, cx - rx, cy);
  addCubic(ic, cx - rx, cy - ky, cx - kx, cy - ry, cx, cy - ry);
  addCubic(ic, cx + kx, cy - ry, cx + rx, cy - ky, cx + rx, cy);
  addClose(ic);
}

function addRoundRect(ic: SvgIcon, x: number, y: number, w: number, h: number, rx: number): void {
  if (rx < 0) rx = 0;
  if (rx > w * 0.5) rx = w * 0.5;
  if (rx > h * 0.5) rx = h * 0.5;
  if (rx <= 0.01) {
    addMove(ic, x, y);
    addLine(ic, x + w, y);
    addLine(ic, x + w, y + h);
    addLine(ic, x, y + h);
    addClose(ic);
    return;
  }
  // Cubic kappa for quarter circle
  const k = rx * 0.55228475;
  const x1 = x + rx,
    x2 = x + w - rx;
  const y1 = y + rx,
    y2 = y + h - rx;
  addMove(ic, x1, y);
  addLine(ic, x2, y);
  addCubic(ic, x2 + k, y, x + w, y1 - k, x + w, y1);
  addLine(ic, x + w, y2);
  addCubic(ic, x + w, y2 + k, x2 + k, y + h, x2, y + h);
  addLine(ic, x1, y + h);
  addCubic(ic, x1 - k, y + h, x, y2 + k, x, y2);
  addLine(ic, x, y1);
  addCubic(ic, x, y1 - k, x1 - k, y, x1, y);
  addClose(ic);
}

// A cursor over a `d` attribute or a `points` list. `strtof` in C++, a regex
// here; both take a leading sign, an exponent and a bare `.5`.
class Scan {
  s: string;
  i = 0;

  constructor(s: string) {
    this.s = s;
  }

  skipWs(): void {
    while (this.i < this.s.length && " \t\n\r,".includes(this.s[this.i]!)) this.i++;
  }

  num(): number | null {
    this.skipWs();
    const m = /^[+-]?(\d+\.?\d*|\.\d+)([eE][+-]?\d+)?/.exec(this.s.slice(this.i));
    if (!m) return null;
    this.i += m[0].length;
    return parseFloat(m[0]);
  }

  // An arc flag is one character and may run into the next number:
  // "a2 2 0 0022 17" is flags 0 and 0, then 22. ParseFlag in svg.cpp.
  flag(): number | null {
    this.skipWs();
    const ch = this.s[this.i];
    if (ch !== "0" && ch !== "1") return null;
    this.i++;
    return ch === "1" ? 1 : 0;
  }
}

function angleBetween(ux: number, uy: number, vx: number, vy: number): number {
  const dot = ux * vx + uy * vy;
  const nu = Math.sqrt(ux * ux + uy * uy);
  const nv = Math.sqrt(vx * vx + vy * vy);
  let c = nu > 0 && nv > 0 ? dot / (nu * nv) : 1;
  if (c < -1) c = -1;
  if (c > 1) c = 1;
  let a = Math.acos(c);
  if (ux * vy - uy * vx < 0) a = -a;
  return a;
}

// The endpoint parameterisation of an SVG arc, turned into at most eight
// cubics — the same derivation `AddArc` in svg.cpp uses.
function addArc(
  ic: SvgIcon,
  x1: number,
  y1: number,
  rx: number,
  ry: number,
  phiDeg: number,
  large: boolean,
  sweep: boolean,
  x2: number,
  y2: number,
): void {
  rx = Math.abs(rx);
  ry = Math.abs(ry);
  if (rx < 1e-6 || ry < 1e-6) {
    addLine(ic, x2, y2);
    return;
  }
  const phi = (phiDeg * Math.PI) / 180;
  const cosP = Math.cos(phi);
  const sinP = Math.sin(phi);
  const dx = (x1 - x2) * 0.5;
  const dy = (y1 - y2) * 0.5;
  const x1p = cosP * dx + sinP * dy;
  const y1p = -sinP * dx + cosP * dy;
  let rx2 = rx * rx,
    ry2 = ry * ry;
  const x1p2 = x1p * x1p,
    y1p2 = y1p * y1p;
  const lam = x1p2 / rx2 + y1p2 / ry2;
  if (lam > 1) {
    const sc = Math.sqrt(lam);
    rx *= sc;
    ry *= sc;
    rx2 = rx * rx;
    ry2 = ry * ry;
  }
  const num = rx2 * ry2 - rx2 * y1p2 - ry2 * x1p2;
  const den = rx2 * y1p2 + ry2 * x1p2;
  let csq = den > 0 ? num / den : 0;
  if (csq < 0) csq = 0;
  let c = Math.sqrt(csq);
  if (large === sweep) c = -c;
  const cxp = (c * rx * y1p) / ry;
  const cyp = (c * -ry * x1p) / rx;
  const cx = cosP * cxp - sinP * cyp + (x1 + x2) * 0.5;
  const cy = sinP * cxp + cosP * cyp + (y1 + y2) * 0.5;
  const theta1 = angleBetween(1, 0, (x1p - cxp) / rx, (y1p - cyp) / ry);
  let dtheta = angleBetween((x1p - cxp) / rx, (y1p - cyp) / ry, (-x1p - cxp) / rx, (-y1p - cyp) / ry);
  if (!sweep && dtheta > 0) dtheta -= 2 * Math.PI;
  if (sweep && dtheta < 0) dtheta += 2 * Math.PI;
  let segs = Math.ceil(Math.abs(dtheta) / (Math.PI * 0.5 + 1e-6));
  if (segs < 1) segs = 1;
  if (segs > 8) segs = 8;
  const dt = dtheta / segs;
  for (let i = 0; i < segs; i++) {
    const t0 = theta1 + dt * i;
    const t1 = t0 + dt;
    const e0x = rx * Math.cos(t0),
      e0y = ry * Math.sin(t0);
    const e1x = rx * Math.cos(t1),
      e1y = ry * Math.sin(t1);
    const q = Math.tan(dt * 0.5);
    const alpha = (Math.sin(dt) * (Math.sqrt(4 + 3 * q * q) - 1)) / 3;
    const d0x = -rx * Math.sin(t0),
      d0y = ry * Math.cos(t0);
    const d1x = -rx * Math.sin(t1),
      d1y = ry * Math.cos(t1);
    const p1x = cx + cosP * e1x - sinP * e1y;
    const p1y = cy + sinP * e1x + cosP * e1y;
    const c1x = cx + cosP * (e0x + alpha * d0x) - sinP * (e0y + alpha * d0y);
    const c1y = cy + sinP * (e0x + alpha * d0x) + cosP * (e0y + alpha * d0y);
    const c2x = cx + cosP * (e1x - alpha * d1x) - sinP * (e1y - alpha * d1y);
    const c2y = cy + sinP * (e1x - alpha * d1x) + cosP * (e1y - alpha * d1y);
    addCubic(ic, c1x, c1y, c2x, c2y, p1x, p1y);
  }
}

function parsePathD(ic: SvgIcon, d: string): void {
  const s = new Scan(d);
  let cmd = "";
  let cx = 0,
    cy = 0,
    sx = 0,
    sy = 0;
  let pcx = 0,
    pcy = 0; // previous cubic control (for S)
  let hasPrevC = false;
  while (s.i < s.s.length) {
    s.skipWs();
    if (s.i >= s.s.length) break;
    const ch = s.s[s.i]!;
    if (/[A-Za-z]/.test(ch)) {
      cmd = ch;
      s.i++;
    } else if (!cmd) {
      s.i++;
      continue;
    }
    const rel = cmd >= "a";
    const op = rel ? cmd.toUpperCase() : cmd;
    if (op === "Z") {
      addClose(ic);
      cx = sx;
      cy = sy;
      hasPrevC = false;
      continue;
    }
    if (op === "M") {
      let x = s.num(),
        y = s.num();
      if (x === null || y === null) break;
      if (rel) {
        x += cx;
        y += cy;
      }
      addMove(ic, x, y);
      cx = sx = x;
      cy = sy = y;
      hasPrevC = false;
      // extra pairs are implicit L/l
      cmd = rel ? "l" : "L";
      continue;
    }
    if (op === "L") {
      let x = s.num(),
        y = s.num();
      if (x === null || y === null) break;
      if (rel) {
        x += cx;
        y += cy;
      }
      addLine(ic, x, y);
      cx = x;
      cy = y;
      hasPrevC = false;
      continue;
    }
    if (op === "H") {
      let x = s.num();
      if (x === null) break;
      if (rel) x += cx;
      addLine(ic, x, cy);
      cx = x;
      hasPrevC = false;
      continue;
    }
    if (op === "V") {
      let y = s.num();
      if (y === null) break;
      if (rel) y += cy;
      addLine(ic, cx, y);
      cy = y;
      hasPrevC = false;
      continue;
    }
    if (op === "C") {
      let x1 = s.num(),
        y1 = s.num(),
        x2 = s.num(),
        y2 = s.num(),
        x = s.num(),
        y = s.num();
      if (x1 === null || y1 === null || x2 === null || y2 === null || x === null || y === null) break;
      if (rel) {
        x1 += cx;
        y1 += cy;
        x2 += cx;
        y2 += cy;
        x += cx;
        y += cy;
      }
      addCubic(ic, x1, y1, x2, y2, x, y);
      pcx = x2;
      pcy = y2;
      hasPrevC = true;
      cx = x;
      cy = y;
      continue;
    }
    if (op === "S") {
      let x2 = s.num(),
        y2 = s.num(),
        x = s.num(),
        y = s.num();
      if (x2 === null || y2 === null || x === null || y === null) break;
      if (rel) {
        x2 += cx;
        y2 += cy;
        x += cx;
        y += cy;
      }
      const x1 = hasPrevC ? 2 * cx - pcx : cx;
      const y1 = hasPrevC ? 2 * cy - pcy : cy;
      addCubic(ic, x1, y1, x2, y2, x, y);
      pcx = x2;
      pcy = y2;
      hasPrevC = true;
      cx = x;
      cy = y;
      continue;
    }
    if (op === "Q") {
      let x1 = s.num(),
        y1 = s.num(),
        x = s.num(),
        y = s.num();
      if (x1 === null || y1 === null || x === null || y === null) break;
      if (rel) {
        x1 += cx;
        y1 += cy;
        x += cx;
        y += cy;
      }
      // elevate quad to cubic
      addCubic(
        ic,
        cx + (2 / 3) * (x1 - cx),
        cy + (2 / 3) * (y1 - cy),
        x + (2 / 3) * (x1 - x),
        y + (2 / 3) * (y1 - y),
        x,
        y,
      );
      pcx = x1;
      pcy = y1;
      hasPrevC = true;
      cx = x;
      cy = y;
      continue;
    }
    if (op === "T") {
      let x = s.num(),
        y = s.num();
      if (x === null || y === null) break;
      if (rel) {
        x += cx;
        y += cy;
      }
      const x1 = hasPrevC ? 2 * cx - pcx : cx;
      const y1 = hasPrevC ? 2 * cy - pcy : cy;
      addCubic(
        ic,
        cx + (2 / 3) * (x1 - cx),
        cy + (2 / 3) * (y1 - cy),
        x + (2 / 3) * (x1 - x),
        y + (2 / 3) * (y1 - y),
        x,
        y,
      );
      pcx = x1;
      pcy = y1;
      hasPrevC = true;
      cx = x;
      cy = y;
      continue;
    }
    if (op === "A") {
      const rx = s.num(),
        ry = s.num(),
        rot = s.num(),
        fA = s.flag(),
        fS = s.flag();
      let x = s.num(),
        y = s.num();
      if (rx === null || ry === null || rot === null || fA === null || fS === null || x === null || y === null) break;
      if (rel) {
        x += cx;
        y += cy;
      }
      addArc(ic, cx, cy, rx, ry, rot, fA !== 0, fS !== 0, x, y);
      cx = x;
      cy = y;
      hasPrevC = false;
      continue;
    }
    // unknown command
    s.i++;
  }
}

function parsePolyline(ic: SvgIcon, pts: string, close: boolean): void {
  const s = new Scan(pts);
  let first = true;
  for (;;) {
    const x = s.num();
    if (x === null) break;
    const y = s.num();
    if (y === null) break;
    if (first) {
      addMove(ic, x, y);
      first = false;
    } else {
      addLine(ic, x, y);
    }
  }
  if (close && !first) addClose(ic);
}

// `name="value"` inside one tag's text, matched only on a name boundary the
// way `GetAttr` does — "fill" must not match "fill-rule".
function getAttr(tag: string, name: string): string | null {
  const re = new RegExp(`(^|[^A-Za-z0-9_-])${name}=("([^"]*)"|'([^']*)')`, "i");
  const m = re.exec(tag);
  if (!m) return null;
  const v = m[3] !== undefined ? m[3] : m[4];
  return v && v.length > 0 ? v : null;
}

function attrF(tag: string, name: string, def: number): number {
  const v = getAttr(tag, name);
  if (v === null) return def;
  const f = parseFloat(v);
  return Number.isNaN(f) ? 0 : f;
}

// `fill="#rrggbb"` on a shape, as 0xRRGGBBAA. "none" and "currentColor" both
// leave the shape in the caller's colour, which is what every Lucide icon says.
function parseSvgColor(v: string): number | null {
  if (!v.startsWith("#")) return null;
  const h = v.slice(1);
  if (h.length !== 3 && h.length !== 6) return null;
  if (!/^[0-9a-fA-F]+$/.test(h)) return null;
  const p = (i: number) => parseInt(h[i]!, 16);
  const r = h.length === 3 ? p(0) * 17 : p(0) * 16 + p(1);
  const g = h.length === 3 ? p(1) * 17 : p(2) * 16 + p(3);
  const b = h.length === 3 ? p(2) * 17 : p(4) * 16 + p(5);
  return ((r << 24) | (g << 16) | (b << 8) | 0xff) >>> 0;
}

// The 2x3 affine SVG writes as `matrix(a b c d e f)`: x' = a*x + c*y + e and
// y' = b*x + d*y + f. The mirror of SvgMatrix in src/gpui/svg.cpp.
type Mat = { a: number; b: number; c: number; d: number; e: number; f: number };

const identity: Mat = { a: 1, b: 0, c: 0, d: 1, e: 0, f: 0 };

function isIdentity(m: Mat): boolean {
  return m.a === 1 && m.b === 0 && m.c === 0 && m.d === 1 && m.e === 0 && m.f === 0;
}

// `m` applied after `n` — the order a nested <g> composes in.
function matMul(m: Mat, n: Mat): Mat {
  return {
    a: m.a * n.a + m.c * n.b,
    b: m.b * n.a + m.d * n.b,
    c: m.a * n.c + m.c * n.d,
    d: m.b * n.c + m.d * n.d,
    e: m.a * n.e + m.c * n.f + m.e,
    f: m.b * n.e + m.d * n.f + m.f,
  };
}

// "translate(120 0) scale(.1)" and the rest of the list, in order. A function
// this does not know is skipped rather than guessed at.
function parseTransform(s: string): Mat {
  let out = identity;
  let i = 0;
  while (i < s.length) {
    while (i < s.length && " ,\t\n\r".includes(s[i]!)) i++;
    const nameAt = i;
    while (i < s.length && s[i] !== "(") i++;
    if (i >= s.length) break;
    const fn = s.slice(nameAt, i).toLowerCase();
    i++; // past (
    const argAt = i;
    while (i < s.length && s[i] !== ")") i++;
    const sc = new Scan(s.slice(argAt, i));
    if (i < s.length) i++; // past )
    const v: number[] = [];
    for (let k = 0; k < 6; k++) {
      const n = sc.num();
      if (n === null) break;
      v.push(n);
    }
    let m = identity;
    if (fn === "translate" && v.length >= 1) {
      m = { ...identity, e: v[0]!, f: v.length >= 2 ? v[1]! : 0 };
    } else if (fn === "scale" && v.length >= 1) {
      m = { ...identity, a: v[0]!, d: v.length >= 2 ? v[1]! : v[0]! };
    } else if (fn === "rotate" && v.length >= 1) {
      const rad = (v[0]! * Math.PI) / 180;
      const cs = Math.cos(rad);
      const sn = Math.sin(rad);
      m = { a: cs, b: sn, c: -sn, d: cs, e: 0, f: 0 };
      if (v.length >= 3) {
        // About a point rather than the origin.
        const to = { ...identity, e: v[1]!, f: v[2]! };
        const back = { ...identity, e: -v[1]!, f: -v[2]! };
        m = matMul(to, matMul(m, back));
      }
    } else if (fn === "matrix" && v.length >= 6) {
      m = { a: v[0]!, b: v[1]!, c: v[2]!, d: v[3]!, e: v[4]!, f: v[5]! };
    }
    out = matMul(out, m);
  }
  return out;
}

function endShape(ic: SvgIcon, start: number, tag: string, m: Mat): void {
  if (ic.ops.length <= start) return;
  // The groups this shape sits in, and its own transform after them.
  const own = getAttr(tag, "transform");
  const full = own === null ? m : matMul(m, parseTransform(own));
  if (!isIdentity(full)) {
    for (let i = start; i < ic.ops.length; i++) {
      const o = ic.ops[i]!;
      if (o.cmd === CLOSE) continue; // carries no point
      const x = o.x;
      const y = o.y;
      o.x = full.a * x + full.c * y + full.e;
      o.y = full.b * x + full.d * y + full.f;
      if (o.cmd === CUBIC) {
        const x1 = o.x1;
        const y1 = o.y1;
        o.x1 = full.a * x1 + full.c * y1 + full.e;
        o.y1 = full.b * x1 + full.d * y1 + full.f;
        const x2 = o.x2;
        const y2 = o.y2;
        o.x2 = full.a * x2 + full.c * y2 + full.e;
        o.y2 = full.b * x2 + full.d * y2 + full.f;
      }
    }
  }
  const fillAttr = getAttr(tag, "fill");
  const fill = fillAttr === null ? null : parseSvgColor(fillAttr);
  if (fill !== null) ic.hasOwnColors = true;
  const strokeAttr = getAttr(tag, "stroke");
  const stroke = strokeAttr === null ? null : parseSvgColor(strokeAttr);
  if (stroke !== null) ic.hasOwnColors = true;
  ic.shapes.push({ start, count: ic.ops.length - start, fill, stroke });
}

function startsWithI(s: string, at: number, lit: string): boolean {
  return s.slice(at, at + lit.length).toLowerCase() === lit;
}

// Shapes inside one of these are a definition, not a drawing: a clip path, a
// gradient stop, a filter's input, the contents of <defs>. The mirror of
// IsHiddenContainer in src/gpui/svg.cpp.
const hiddenContainers = [
  "defs",
  "clippath",
  "mask",
  "filter",
  "pattern",
  "symbol",
  "marker",
  "lineargradient",
  "radialgradient",
];

// A <g> and not a <glyph>: the name has to end where the tag's whitespace or
// its close begins.
function isGroupTag(s: string, at: number): boolean {
  if (at >= s.length || (s[at] !== "g" && s[at] !== "G")) return false;
  const after = at + 1 < s.length ? s[at + 1]! : " ";
  return " >/\t\n\r".includes(after);
}

function isHiddenContainer(s: string, at: number): boolean {
  for (const n of hiddenContainers) {
    if (!startsWithI(s, at, n)) continue;
    const after = at + n.length < s.length ? s[at + n.length]! : " ";
    // "clipPath" must not match "clipPathUnits".
    if (" >/\t\n\r".includes(after)) return true;
  }
  return false;
}

function parseSvg(xml: string): SvgIcon {
  const ic = newIcon();
  let p = 0;
  const end = xml.length;
  // How deep inside a <defs> / <clipPath> / <mask> / ... we are. Nothing is
  // drawn while this is above zero.
  let hidden = 0;
  // The <g> transforms in force, innermost last.
  const gstack: Mat[] = [];
  while (p < end) {
    if (xml[p] !== "<") {
      p++;
      continue;
    }
    p++;
    if (p < end && xml[p] === "/") {
      const name = p + 1;
      while (p < end && xml[p] !== ">") p++;
      if (hidden > 0 && isHiddenContainer(xml, name)) hidden--;
      else if (hidden === 0 && gstack.length > 0 && isGroupTag(xml, name)) gstack.pop();
      if (p < end) p++;
      continue;
    }
    if (p < end && xml[p] === "!") {
      const close = xml.indexOf("-->", p);
      p = close < 0 ? end : close + 3;
      continue;
    }
    const tagStart = p;
    while (p < end && xml[p] !== ">") p++;
    if (p >= end) break;
    const tag = xml.slice(tagStart, p);
    const selfClosing = tag.endsWith("/");
    p++; // skip >

    if (isHiddenContainer(xml, tagStart)) {
      if (!selfClosing) hidden++;
      continue;
    }
    if (hidden > 0) continue;

    // <g transform=".."> — the one container that still says something about
    // what is drawn inside it. Its fill and stroke are deliberately *not*
    // inherited; src/gpui/svg.cpp says why.
    if (isGroupTag(xml, tagStart)) {
      if (!selfClosing) {
        let cur = gstack.length > 0 ? gstack[gstack.length - 1]! : identity;
        const tr = getAttr(tag, "transform");
        if (tr !== null) cur = matMul(cur, parseTransform(tr));
        gstack.push(cur);
      }
      continue;
    }
    // What the groups around this shape add up to.
    const gm = gstack.length > 0 ? gstack[gstack.length - 1]! : identity;

    if (startsWithI(xml, tagStart, "svg")) {
      const vb = getAttr(tag, "viewBox");
      if (vb) {
        const s = new Scan(vb);
        const a = s.num() ?? 0,
          b = s.num() ?? 0,
          c = s.num() ?? 24,
          d = s.num() ?? 24;
        ic.vbX = a;
        ic.vbY = b;
        ic.vbW = c > 0 ? c : 24;
        ic.vbH = d > 0 ? d : 24;
      } else {
        // No viewBox: the coordinates are the viewport's, which width and
        // height give. Every lucide icon has one and never reaches this.
        const w = attrF(tag, "width", 0);
        const h = attrF(tag, "height", 0);
        if (w > 0 && h > 0) {
          ic.vbW = w;
          ic.vbH = h;
        }
      }
      const sw = attrF(tag, "stroke-width", 0);
      if (sw > 0) ic.strokeW = sw;
      const fill = getAttr(tag, "fill");
      if (fill !== null) ic.filled = fill.toLowerCase() !== "none";
      const rootStroke = getAttr(tag, "stroke");
      if (rootStroke !== null) ic.stroked = rootStroke.toLowerCase() !== "none";
      continue;
    }
    if (startsWithI(xml, tagStart, "path")) {
      const start = ic.ops.length;
      const d = getAttr(tag, "d");
      if (d) parsePathD(ic, d);
      endShape(ic, start, tag, gm);
      continue;
    }
    if (startsWithI(xml, tagStart, "rect")) {
      const start = ic.ops.length;
      addRoundRect(
        ic,
        attrF(tag, "x", 0),
        attrF(tag, "y", 0),
        attrF(tag, "width", 0),
        attrF(tag, "height", 0),
        attrF(tag, "rx", 0),
      );
      endShape(ic, start, tag, gm);
      continue;
    }
    if (startsWithI(xml, tagStart, "polyline")) {
      const start = ic.ops.length;
      const pts = getAttr(tag, "points");
      if (pts) parsePolyline(ic, pts, false);
      endShape(ic, start, tag, gm);
      continue;
    }
    if (startsWithI(xml, tagStart, "polygon")) {
      const start = ic.ops.length;
      const pts = getAttr(tag, "points");
      if (pts) parsePolyline(ic, pts, true);
      endShape(ic, start, tag, gm);
      continue;
    }
    if (startsWithI(xml, tagStart, "line")) {
      const start = ic.ops.length;
      addMove(ic, attrF(tag, "x1", 0), attrF(tag, "y1", 0));
      addLine(ic, attrF(tag, "x2", 0), attrF(tag, "y2", 0));
      endShape(ic, start, tag, gm);
      continue;
    }
    if (startsWithI(xml, tagStart, "circle")) {
      const start = ic.ops.length;
      const cx = attrF(tag, "cx", 0),
        cy = attrF(tag, "cy", 0),
        r = attrF(tag, "r", 0);
      addRoundRect(ic, cx - r, cy - r, r * 2, r * 2, r);
      endShape(ic, start, tag, gm);
      continue;
    }
    // No icon under assets/icons has one; a picture from anywhere else may.
    if (startsWithI(xml, tagStart, "ellipse")) {
      const start = ic.ops.length;
      addEllipse(ic, attrF(tag, "cx", 0), attrF(tag, "cy", 0), attrF(tag, "rx", 0), attrF(tag, "ry", 0));
      endShape(ic, start, tag, gm);
      continue;
    }
  }
  return ic;
}

// ─── the encoder (EncodeIcon in src/gpui/svg.cpp) ───────────────────────────

function emitOps(w: Writer, ic: SvgIcon, from: number, to: number): void {
  for (let i = from; i < to; i++) {
    const o = ic.ops[i]!;
    if (o.cmd === MOVE) {
      w.op(OP_MOVE_TO);
      w.f(o.x);
      w.f(o.y);
    } else if (o.cmd === LINE) {
      w.op(OP_LINE_TO);
      w.f(o.x);
      w.f(o.y);
    } else if (o.cmd === CUBIC) {
      w.op(OP_CUBIC_TO);
      w.f(o.x1);
      w.f(o.y1);
      w.f(o.x2);
      w.f(o.y2);
      w.f(o.x);
      w.f(o.y);
    } else if (o.cmd === CLOSE) {
      w.op(OP_CLOSE_PATH);
    }
  }
}

function encodeIcon(ic: SvgIcon): number[] {
  const w = new Writer();
  w.op(OP_VIEWBOX);
  w.f(ic.vbX);
  w.f(ic.vbY);
  w.f(ic.vbW);
  w.f(ic.vbH);
  w.op(OP_STROKE_WIDTH);
  w.f(ic.strokeW > 0 ? ic.strokeW : 2);
  if (!ic.hasOwnColors) {
    emitOps(w, ic, 0, ic.ops.length);
    w.op(pathOp(ic.filled, ic.stroked));
    w.op(OP_END);
    return w.bytes;
  }
  for (const sh of ic.shapes) {
    if (sh.fill !== null) {
      w.op(OP_COLOR);
      w.u32(sh.fill);
      emitOps(w, ic, sh.start, sh.start + sh.count);
      w.op(OP_FILL_PATH);
      w.op(OP_COLOR_RESET);
    }
    // A shape may be filled in one colour and drawn in another, and one op
    // carries one colour, so that is two passes over the same points.
    if (sh.stroke !== null) {
      w.op(OP_COLOR);
      w.u32(sh.stroke);
      emitOps(w, ic, sh.start, sh.start + sh.count);
      w.op(OP_STROKE_PATH);
      w.op(OP_COLOR_RESET);
    }
    if (sh.fill !== null || sh.stroke !== null) continue;
    // Named no colour of its own: the caller's.
    emitOps(w, ic, sh.start, sh.start + sh.count);
    w.op(pathOp(ic.filled, ic.stroked));
  }
  w.op(OP_END);
  return w.bytes;
}

export function svgToBytecode(xml: string): number[] {
  const ic = parseSvg(xml);
  if (ic.ops.length === 0) return [];
  return encodeIcon(ic);
}

// ─── the generated file ─────────────────────────────────────────────────────

type Entry = { name: string; offset: number; len: number };

function emitCpp(entries: Entry[], data: number[], srcDir: string): string {
  const out: string[] = [];
  out.push("/* Generated by cmd/svg-to-bytecode.ts — do not edit.");
  out.push("");
  out.push(`   Every file in ${srcDir}, converted to the draw-op byte stream`);
  out.push("   src/gpui/drawops.h describes: one array with all of them end to end,");
  out.push("   and the slice of it each icon is. Re-run the generator after adding,");
  out.push("   removing or changing an icon. */");
  out.push("");
  out.push('#include "gpui/asset_icons.h"');
  out.push("");
  out.push("namespace gpui {");
  out.push("");
  out.push("const uint8_t kAssetIconsData[] = {");
  // One icon per run of lines, labelled, so a diff of this file says which
  // icons a re-run changed.
  for (const e of entries) {
    out.push(`    // ${e.name} @ ${e.offset}, ${e.len} bytes`);
    const slice = data.slice(e.offset, e.offset + e.len);
    for (let i = 0; i < slice.length; i += 16) {
      const row = slice.slice(i, i + 16).map((b) => "0x" + b.toString(16).padStart(2, "0"));
      out.push("    " + row.join(", ") + ",");
    }
  }
  out.push("};");
  out.push(`const int kAssetIconsDataLen = ${data.length};`);
  out.push("");
  out.push("// The names, as one SeqStrings run in the same order as the table");
  out.push("// below: the table carries no pointer per icon and the linker no");
  out.push("// relocation per pointer.");
  out.push("const char kAssetIconNames[] =");
  for (let i = 0; i < entries.length; i++) {
    const last = i === entries.length - 1;
    // The literal's own terminator ends the run after the last name.
    out.push('    "' + entries[i]!.name + '\\0"' + (last ? ";" : ""));
  }
  out.push("");
  out.push("// Where each icon sits in kAssetIconsData, in name order.");
  out.push("const AssetIcon kAssetIcons[] = {");
  for (const e of entries) {
    out.push(`    {${e.offset}, ${e.len}}, // ${e.name}`);
  }
  out.push("};");
  out.push(`const int kAssetIconsCount = ${entries.length};`);
  out.push("");
  out.push("} // namespace gpui");
  out.push("");
  return out.join("\n");
}

function svgFilesIn(dir: string): string[] {
  return readdirSync(dir)
    .filter((f) => f.toLowerCase().endsWith(".svg"))
    .sort()
    .map((f) => join(dir, f));
}

function main(): void {
  const argv = process.argv.slice(2);
  let outPath: string | null = null;
  const inputs: string[] = [];
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i]!;
    if (a === "-o") {
      outPath = argv[++i] ?? null;
    } else if (a.startsWith("-o=")) {
      outPath = a.slice(3);
    } else {
      inputs.push(a);
    }
  }

  let files: string[] = [];
  let srcDir = "assets/icons";
  if (inputs.length === 0) {
    files = svgFilesIn(join(root, "assets", "icons"));
    if (outPath === null) outPath = join(root, "src", "gpui", "asset_icons.cpp");
  } else {
    for (const inp of inputs) {
      const abs = join(process.cwd(), inp);
      const path = existsSync(abs) ? abs : inp;
      if (!existsSync(path)) throw new Error(`no such file: ${inp}`);
      if (statSync(path).isDirectory()) {
        files.push(...svgFilesIn(path));
        srcDir = relative(root, path).replaceAll("\\", "/");
      } else {
        files.push(path);
        srcDir = "the files named on the command line";
      }
    }
  }
  if (files.length === 0) throw new Error("nothing to convert");

  // Converted first, then sorted, then laid out: the offsets have to be into
  // the array as it is written, and the table is written in name order so the
  // lookup can binary-search it.
  const converted: { name: string; bytes: number[] }[] = [];
  for (const f of files) {
    const name = basename(f).replace(/\.svg$/i, "");
    const bytes = svgToBytecode(readFileSync(f, "utf8"));
    if (bytes.length === 0) {
      console.warn(`skipped ${f}: nothing to draw`);
      continue;
    }
    converted.push({ name, bytes });
  }
  converted.sort((a, b) => (a.name < b.name ? -1 : a.name > b.name ? 1 : 0));
  const data: number[] = [];
  const entries: Entry[] = [];
  for (const c of converted) {
    entries.push({ name: c.name, offset: data.length, len: c.bytes.length });
    for (const b of c.bytes) data.push(b);
  }

  const text = emitCpp(entries, data, srcDir);
  if (outPath === null) {
    process.stdout.write(text);
    return;
  }
  writeFileSync(outPath, text, "utf8");
  console.log(`wrote ${outPath}: ${entries.length} icons, ${data.length} bytes`);
}

main();
