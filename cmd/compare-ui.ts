// Drive the same input at the Rust story app and at ours, and photograph both
// after each step. cmd/compare-story.ts answers "does the page look the same
// when it opens"; this one answers "does it behave the same once you touch it",
// which is where the differences that a static sweep cannot see live: a menu
// that opens with different rows, a dropdown that does not close, a list that
// scrolls a different distance, a field that takes a keystroke differently.
//
//   bun cmd/compare-ui.ts menu click:120,200 shot:open
//   bun cmd/compare-ui.ts -nobuild select click:200,180 shot:open key:40 key:13 shot:picked
//
// A caveat on `hover:`. A hover state needs the real pointer inside the
// window, and a session that refuses SetCursorPos — locked, or a CI agent —
// will not put it there: every synthetic WM_MOUSEMOVE is answered with a
// WM_MOUSELEAVE. The app is launched with GPUI_HOVER_HOLD=1, which makes it
// ignore the leave, and the steps below keep re-sending the move; even so,
// what PrintWindow reads back for a GPU-composited window can be the frame
// before the hover on such a session. Hover comparisons are reliable on an
// interactive desktop and only there. Clicks, keys and the wheel are not
// affected — they change state rather than a transient.
//
// Steps, applied left to right, to each app in turn (so a hover step can own
// the one real cursor the desktop has):
//
//   click:X,Y            left press + release at client X,Y
//   rclick:X,Y           the same with the secondary button
//   press:X,Y            the press alone; rpress: for the secondary button
//   release:X,Y          the release alone; rrelease: likewise. A menu that
//                        opens on the press and closes on the release wants
//                        the two apart, with a shot: in between
//   hover:X,Y            park the pointer there
//   move:X,Y             a WM_MOUSEMOVE without moving the cursor
//   drag:X1,Y1,X2,Y2     press, eight moves, release
//   wheel:N[@X,Y]        N notches (negative scrolls down) at the window
//                        centre, or at X,Y when it is given
//   key:VK[+mods]        keydown+keyup; mods are ctrl/shift/alt
//   type:TEXT            WM_CHAR per character
//   wait:MS              sit still
//   shot:NAME            capture both windows as <slug>-NN-NAME-{rust,cpp}.png
//
// A step list with no shot: gets one at the end anyway, so the common case is
// just the clicks. Shots land in out/compare-ui/, or the directory named by
// -out. The parity suite uses the latter to keep each case self-contained.
//
// Both windows are the same size (the halves cmd/compare-story.ts uses), so a
// client coordinate means the same thing in both -- which is the whole point:
// the same number drives both apps, and a click that lands on a different
// widget on one side is itself the finding.

import { existsSync, mkdirSync, statSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import {
  captureWindowToPng,
  captureWindowSurfaceToPng,
  clickClient,
  clientToScreen,
  getClientRect,
  getWorkArea,
  hoverClient,
  killAndWait,
  packCoords,
  placeOnWorkAreaHalf,
  rightClickClient,
  sendMessage,
  setCursorPos,
  setForegroundWindow,
  setProcessDpiAware,
  showWindow,
  bringToTopAndRedraw,
  SW_RESTORE,
  sleep,
  waitForForeground,
  waitForPidWindow,
  workAreaHalfRect,
} from "./winapi.ts";
import { ensureRustTree, rustTreeDir } from "./run.ts";

const root = resolve(dirname(Bun.main), "..");
process.chdir(root);

const WM_KEYDOWN = 0x0100;
const WM_KEYUP = 0x0101;
const WM_CHAR = 0x0102;
const WM_MOUSEMOVE = 0x0200;
const WM_LBUTTONDOWN = 0x0201;
const WM_LBUTTONUP = 0x0202;
const WM_RBUTTONDOWN = 0x0204;
const WM_RBUTTONUP = 0x0205;
const WM_MOUSEWHEEL = 0x020a;
const WM_PAINT = 0x000f;

const VK_CONTROL = 0x11;
const VK_SHIFT = 0x10;
const VK_MENU = 0x12;

type Step =
  | { kind: "click"; x: number; y: number; right?: boolean }
  | { kind: "press"; x: number; y: number; right?: boolean }
  | { kind: "release"; x: number; y: number; right?: boolean }
  | { kind: "hover"; x: number; y: number }
  | { kind: "move"; x: number; y: number }
  | { kind: "drag"; x1: number; y1: number; x2: number; y2: number }
  | { kind: "wheel"; n: number; x?: number; y?: number }
  | { kind: "key"; vk: number; ctrl: boolean; shift: boolean; alt: boolean }
  | { kind: "type"; text: string }
  | { kind: "wait"; ms: number }
  | { kind: "shot"; name: string };

function die(msg?: string): never {
  if (msg) {
    console.error(msg);
  }
  console.error("Usage: bun cmd/compare-ui.ts [-dbg] [-nobuild] [-out=DIR] <slug> [step ...]");
  process.exit(1);
}

function nums(s: string): number[] {
  return s.split(",").map((v) => Number(v.trim()));
}

function parseStep(raw: string): Step {
  const at = raw.indexOf(":");
  const kind = at < 0 ? raw : raw.slice(0, at);
  const arg = at < 0 ? "" : raw.slice(at + 1);
  switch (kind) {
    case "click":
    case "rclick": {
      const [x, y] = nums(arg);
      return { kind: "click", x: x ?? 0, y: y ?? 0, right: kind === "rclick" };
    }
    case "press":
    case "rpress": {
      const [x, y] = nums(arg);
      return { kind: "press", x: x ?? 0, y: y ?? 0, right: kind === "rpress" };
    }
    case "release":
    case "rrelease": {
      const [x, y] = nums(arg);
      return { kind: "release", x: x ?? 0, y: y ?? 0, right: kind === "rrelease" };
    }
    case "hover":
    case "move": {
      const [x, y] = nums(arg);
      return { kind, x: x ?? 0, y: y ?? 0 };
    }
    case "drag": {
      const [x1, y1, x2, y2] = nums(arg);
      return { kind, x1: x1 ?? 0, y1: y1 ?? 0, x2: x2 ?? 0, y2: y2 ?? 0 };
    }
    case "wheel": {
      const cut = arg.indexOf("@");
      if (cut < 0) {
        return { kind, n: Number(arg) || 0 };
      }
      const [x, y] = nums(arg.slice(cut + 1));
      return { kind, n: Number(arg.slice(0, cut)) || 0, x: x ?? 0, y: y ?? 0 };
    }
    case "key": {
      const parts = arg.split("+");
      const vk = Number(parts[0]);
      const mods = parts.slice(1).map((m) => m.toLowerCase());
      return {
        kind,
        vk: Number.isFinite(vk) ? vk : 0,
        ctrl: mods.includes("ctrl"),
        shift: mods.includes("shift"),
        alt: mods.includes("alt"),
      };
    }
    case "type":
      return { kind, text: arg };
    case "wait":
      return { kind, ms: Number(arg) || 0 };
    case "shot":
      return { kind, name: arg || "shot" };
    default:
      return die(`Unknown step: ${raw}`);
  }
}

// Where a `hover:` step left the pointer. SetCursorPos is refused on a
// locked or non-interactive desktop, so the real pointer may be nowhere near
// the window and Windows answers our synthetic move with a WM_MOUSELEAVE that
// takes the hover straight back off. Re-sending the move while we wait, and
// again just before the shutter, is what keeps a hover state up long enough
// to be photographed. The Rust app happens not to need it; ours does, and a
// tool that only works on one of the two is no use for comparing them.
let lastHover: { x: number; y: number } | null = null;

async function holdHover(hwnd: number, ms: number): Promise<void> {
  if (!lastHover) {
    await sleep(ms);
    return;
  }
  // A tight cadence, not a lazy one: each synthetic move re-arms
  // TrackMouseEvent, Windows answers it with a WM_MOUSELEAVE because the real
  // pointer is elsewhere, and the window repaints without the hover. Moving
  // again every few milliseconds keeps the hovered frame the one on screen
  // most of the time, which is what the shutter needs.
  const step = 8;
  for (let left = ms; left > 0; left -= step) {
    sendMessage(hwnd, WM_MOUSEMOVE, 0, packCoords(lastHover.x, lastHover.y));
    await sleep(Math.min(step, left));
  }
}

async function applyStep(hwnd: number, s: Step, shot: (name: string) => Promise<void>): Promise<void> {
  switch (s.kind) {
    case "click":
      lastHover = null;
      await (s.right ? rightClickClient(hwnd, s.x, s.y, 250) : clickClient(hwnd, s.x, s.y, 250));
      return;
    case "press":
      lastHover = null;
      sendMessage(hwnd, s.right ? WM_RBUTTONDOWN : WM_LBUTTONDOWN, s.right ? 2 : 1, packCoords(s.x, s.y));
      await sleep(200);
      return;
    case "release":
      sendMessage(hwnd, s.right ? WM_RBUTTONUP : WM_LBUTTONUP, 0, packCoords(s.x, s.y));
      await sleep(200);
      return;
    case "move":
      sendMessage(hwnd, WM_MOUSEMOVE, 0, packCoords(s.x, s.y));
      await sleep(150);
      return;
    case "hover": {
      lastHover = { x: s.x, y: s.y };
      await hoverClient(hwnd, s.x, s.y, 0);
      await holdHover(hwnd, 300);
      return;
    }
    case "drag": {
      sendMessage(hwnd, WM_LBUTTONDOWN, 1, packCoords(s.x1, s.y1));
      await sleep(60);
      for (let i = 1; i <= 8; i++) {
        const x = Math.round(s.x1 + ((s.x2 - s.x1) * i) / 8);
        const y = Math.round(s.y1 + ((s.y2 - s.y1) * i) / 8);
        sendMessage(hwnd, WM_MOUSEMOVE, 1, packCoords(x, y));
        await sleep(20);
      }
      sendMessage(hwnd, WM_LBUTTONUP, 0, packCoords(s.x2, s.y2));
      await sleep(200);
      return;
    }
    case "wheel": {
      const r = getClientRect(hwnd);
      const cx = s.x ?? Math.floor(r.right / 2);
      const cy = s.y ?? Math.floor(r.bottom / 2);
      const pt = clientToScreen(hwnd, cx, cy);
      const step = s.n > 0 ? 120 : -120;
      for (let i = 0; i < Math.abs(s.n); i++) {
        sendMessage(hwnd, WM_MOUSEWHEEL, (step << 16) >>> 0, packCoords(pt.x, pt.y));
        await sleep(40);
      }
      await sleep(250);
      return;
    }
    case "key": {
      const down: number[] = [];
      if (s.ctrl) down.push(VK_CONTROL);
      if (s.shift) down.push(VK_SHIFT);
      if (s.alt) down.push(VK_MENU);
      for (const m of down) {
        sendMessage(hwnd, WM_KEYDOWN, m, 0);
      }
      sendMessage(hwnd, WM_KEYDOWN, s.vk, 0);
      await sleep(40);
      sendMessage(hwnd, WM_KEYUP, s.vk, 0xc0000001);
      for (const m of down.reverse()) {
        sendMessage(hwnd, WM_KEYUP, m, 0xc0000001);
      }
      await sleep(150);
      return;
    }
    case "type":
      for (const chr of s.text) {
        sendMessage(hwnd, WM_CHAR, chr.codePointAt(0) ?? 0, 1);
        await sleep(50);
      }
      await sleep(120);
      return;
    case "wait":
      await holdHover(hwnd, s.ms);
      return;
    case "shot":
      if (lastHover) {
        // Move, paint, and shoot with nothing in between: a sent message is
        // delivered ahead of a posted one, so the frame the paint draws still
        // has the hover on it, where a sleep here would let the leave through
        // first.
        sendMessage(hwnd, WM_MOUSEMOVE, 0, packCoords(lastHover.x, lastHover.y));
        sendMessage(hwnd, WM_PAINT, 0, 0);
        await sleep(200);
      }
      await shot(s.name);
      return;
  }
}

// Rust Gallery::set_active_story matches Story::title(), not the kebab slug.
function rustStoryArg(slug: string): string {
  if (slug === "theme-colors") return "Theme Colors";
  if (slug === "introduction") return "Introduction";
  return slug
    .split("-")
    .map((p) => (p ? p[0]!.toUpperCase() + p.slice(1) : p))
    .join("");
}

const argv = Bun.argv.slice(2);
let debug = false;
let nobuild = false;
let outDir = join(root, "out", "compare-ui");
const rest: string[] = [];
for (const a of argv) {
  if (a === "-dbg") debug = true;
  else if (a === "-rel") debug = false;
  else if (a === "-nobuild") nobuild = true;
  else if (a.startsWith("-out=")) outDir = resolve(root, a.slice(5));
  else if (a.startsWith("-")) die(`Unknown flag: ${a}`);
  else rest.push(a);
}
const slug = rest[0];
if (!slug) die();
const steps = rest.slice(1).map(parseStep);
if (!steps.some((s) => s.kind === "shot")) {
  steps.push({ kind: "shot", name: "end" });
}

setProcessDpiAware();
let rustRoot: string;
try {
  rustRoot = ensureRustTree(root);
} catch (e) {
  die(e instanceof Error ? e.message : String(e));
}
void rustRoot;

if (!nobuild) {
  const b = Bun.spawnSync(["bun", "cmd/build.ts", debug ? "-dbg" : "-rel", "story"], {
    cwd: root,
    stdout: "inherit",
    stderr: "inherit",
  });
  if ((b.exitCode ?? 1) !== 0) process.exit(1);
}

const rustExe = join(rustTreeDir(root), "target", debug ? "debug" : "release", "gpui-component-story.exe");
const cppExe = join(root, "out", debug ? "dbg" : "rel", "story.exe");
if (!existsSync(rustExe)) die(`Missing ${rustExe}. Build with: cargo build -p gpui-component-story`);
if (!existsSync(cppExe)) die(`Missing ${cppExe}`);

mkdirSync(outDir, { recursive: true });

const half = workAreaHalfRect("right");
const rustProc = Bun.spawn([rustExe, rustStoryArg(slug!)], {
  cwd: rustTreeDir(root),
  stdout: "ignore",
  stderr: "ignore",
});
const cppProc = Bun.spawn([cppExe, `-gpui-window=${half.x},${half.y},${half.w},${half.h}`, slug!], {
  cwd: join(root, "out", debug ? "dbg" : "rel"),
  stdout: "ignore",
  stderr: "ignore",
  // See the note on `half`: on a session that refuses SetCursorPos the real
  // pointer cannot be put inside the window, so every synthetic move is
  // answered with a WM_MOUSELEAVE and no hover survives. This tells the app
  // to ignore the leave.
  env: { ...process.env, GPUI_HOVER_HOLD: "1" },
});
const rustHwnd = await waitForPidWindow(rustProc.pid ?? 0, 30000);
const cppHwnd = await waitForPidWindow(cppProc.pid ?? 0, 15000);
if (!rustHwnd || !cppHwnd) {
  await Promise.all([killAndWait(rustProc), killAndWait(cppProc)]);
  die(`window did not appear (rust=${!!rustHwnd} cpp=${!!cppHwnd})`);
}
// Halves, not one on top of the other: a fully occluded window composited by
// the GPU photographs black, whatever DWM is holding for it.
placeOnWorkAreaHalf(rustHwnd, "left");
placeOnWorkAreaHalf(cppHwnd, "right");
await sleep(600);

// One side at a time: a hover step needs the one cursor the desktop has, and
// an app that only repaints when it is active would otherwise be photographed
// mid-nap.
async function drive(hwnd: number, tag: "rust" | "cpp"): Promise<void> {
  lastHover = null;
  setForegroundWindow(hwnd);
  bringToTopAndRedraw(hwnd);
  await waitForForeground(hwnd, 3000);
  await sleep(500);
  // Wait until the window has actually drawn something. A window that has
  // been shown but has not painted — or one DWM is still holding a blank
  // surface for — photographs as an empty rectangle, and every shot in the
  // run after it is wrong in the same way. A blank PNG compresses to almost
  // nothing, so the size of a throwaway capture is the test.
  const probe = join(outDir, `.probe-${tag}.png`);
  for (let i = 0; i < 12; i++) {
    captureWindowToPng(hwnd, probe);
    if (statSync(probe).size > 20000) {
      break;
    }
    bringToTopAndRedraw(hwnd);
    await sleep(300);
  }
  let n = 0;
  // PrintWindow reads what DWM is holding for the window, and for a window
  // that is behind another the answer is sometimes an all-black surface. A
  // black PNG compresses to almost nothing, so the file size is the cheapest
  // tell there is: bring the window forward and shoot again.
  const shot = async (name: string) => {
    n++;
    const p = join(outDir, `${slug}-${String(n).padStart(2, "0")}-${name}-${tag}.png`);
    for (let attempt = 0; ; attempt++) {
      captureWindowToPng(hwnd, p);
      if (attempt >= 6 || statSync(p).size > 20000) {
        break;
      }
      showWindow(hwnd, SW_RESTORE);
      setForegroundWindow(hwnd);
      bringToTopAndRedraw(hwnd);
      await sleep(400);
    }
    // PrintWindow can return a black GPU surface even while the window is
    // visible. In that case read the compositor's visible client pixels.
    if (statSync(p).size <= 20000) {
      setForegroundWindow(hwnd);
      bringToTopAndRedraw(hwnd);
      await sleep(250);
      captureWindowSurfaceToPng(hwnd, p);
    }
    console.log(`  ${p}`);
  };
  for (const s of steps) {
    await applyStep(hwnd, s, shot);
  }
}

console.log(`
=== ${slug} ===`);
const wa = getWorkArea();
setCursorPos(wa.left + 4, wa.top + 4);
await drive(rustHwnd, "rust");
await drive(cppHwnd, "cpp");
await Promise.all([killAndWait(rustProc), killAndWait(cppProc)]);
