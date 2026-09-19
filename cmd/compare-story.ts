// Side-by-side story compare: Rust original (left) vs C++ port (right).
//
//   bun cmd/compare-story.ts
//   bun cmd/compare-story.ts introduction
//   bun cmd/compare-story.ts -rel accordion
//   bun cmd/compare-story.ts -nobuild introduction
//
// Rust takes the left half of the work area, ours the right, both 80% of the
// work area tall — same placement as `bun cmd/run.ts -compare`, so the two
// shots are the same size and directly comparable.
//
// Screenshots: out/compare-story/<slug>-rust.png and <slug>-cpp.png

import { existsSync, mkdirSync, statSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import {
  bringToTopAndRedraw,
  captureWindowToPng,
  getWindowRect,
  getWorkArea,
  moveWindow,
  killAndWait,
  placeOnWorkAreaHalf,
  setCursorPos,
  setForegroundWindow,
  setProcessDpiAware,
  sleep,
  waitForPidWindow,
  workAreaHalfRect,
} from "./winapi.ts";
import { ensureRustTree, rustTreeDir } from "./run.ts";

const root = resolve(dirname(Bun.main), "..");
process.chdir(root);

const slugs = [
  "introduction",
  "accordion",
  "alert",
  "alert-dialog",
  "attachment",
  "avatar",
  "badge",
  "breadcrumb",
  "bubble",
  "button",
  "calendar",
  "carousel",
  "chart",
  "checkbox",
  "clipboard",
  "collapsible",
  "color-picker",
  "combobox",
  "command",
  "data-table",
  "date-picker",
  "description-list",
  "dialog",
  "dock",
  "dropdown-button",
  "editor",
  "empty",
  "form",
  "group-box",
  "hover-card",
  "icon",
  "image",
  "input",
  "input-group",
  "kbd",
  "label",
  "list",
  "marker",
  "menu",
  "message",
  "message-scroller",
  "native-menu",
  "notification",
  "number-input",
  "otp-input",
  "pagination",
  "popover",
  "progress",
  "radio",
  "rating",
  "resizable",
  "scrollbar",
  "select",
  "separator",
  "settings",
  "shell",
  "sheet",
  "shimmer",
  "sidebar",
  "skeleton",
  "slider",
  "spinner",
  "status-bar",
  "stepper",
  "switch",
  "table",
  "tabs",
  "tag",
  "textarea",
  "theme-colors",
  "toggle",
  "tooltip",
  "tree",
  "virtual-list",
] as const;

function die(msg?: string): never {
  if (msg) {
    console.error(msg);
  }
  console.error("Usage: bun cmd/compare-story.ts [-rel|-dbg] [-nobuild] [slug]");
  process.exit(1);
}

function parseArgs(argv: string[]): { debug: boolean; nobuild: boolean; pages: string[] } {
  let debug = false;
  let nobuild = false;
  const pages: string[] = [];
  for (const raw of argv) {
    if (raw === "-rel") {
      debug = false;
      continue;
    }
    if (raw === "-dbg") {
      debug = true;
      continue;
    }
    if (raw === "-nobuild") {
      nobuild = true;
      continue;
    }
    if (raw.startsWith("-")) {
      die(`Unknown flag: ${raw}`);
    }
    pages.push(raw.toLowerCase());
  }
  return { debug, nobuild, pages: pages.length ? pages : ["introduction"] };
}

function rustDir(): string {
  return rustTreeDir(root);
}

function rustExe(debug: boolean): string {
  return join(rustDir(), "target", debug ? "debug" : "release", "gpui-component-story.exe");
}

// Rust Gallery::set_active_story matches Story::title(), not the kebab slug.
// "alert-dialog" does not contain-match "AlertDialog" and blanks the page.
function rustStoryArg(slug: string): string {
  if (slug === "theme-colors") {
    return "Theme Colors";
  }
  if (slug === "introduction") {
    return "Introduction";
  }
  return slug
    .split("-")
    .map((part) => (part ? part[0].toUpperCase() + part.slice(1) : part))
    .join("");
}

function cppExe(debug: boolean): string {
  return join(root, "out", debug ? "dbg" : "rel", "story.exe");
}

function run(cmd: string[], cwd: string): number {
  const r = Bun.spawnSync(cmd, { cwd, stdout: "inherit", stderr: "inherit" });
  return r.exitCode ?? 1;
}

// Same placement as `bun cmd/run.ts -compare`: rust on the left half, ours on
// the right, both 80% of the work area tall. Keeps the two shots comparable.
//
// Asked for twice, and checked: an app that sizes its own window while the
// move is in flight ends up where it put itself, and a pair of shots taken at
// two different window sizes is not a comparison at all — every row of the
// page is somewhere else and the diff is 100%.
async function placePair(rustHwnd: number, cppHwnd: number) {
  for (const [hwnd, side] of [
    [rustHwnd, "left"],
    [cppHwnd, "right"],
  ] as const) {
    const want = workAreaHalfRect(side);
    for (let i = 0; i < 5; i++) {
      placeOnWorkAreaHalf(hwnd, side);
      const got = getWindowRect(hwnd);
      if (
        got.left === want.x &&
        got.top === want.y &&
        got.right - got.left === want.w &&
        got.bottom - got.top === want.h
      ) {
        break;
      }
      await sleep(120);
    }
    // The rect being right is not the same as the app having laid out at it:
    // a window moved while its first frame was still being built keeps the
    // size it opened with, and photographs as the left 960 pixels of a wider
    // page. One more resize, a pixel short and then back, with time either
    // side for the frame it asks for.
    await sleep(200);
    moveWindow(hwnd, want.x, want.y, want.w, want.h - 1);
    await sleep(200);
    moveWindow(hwnd, want.x, want.y, want.w, want.h);
    await sleep(200);
  }
}

// PrintWindow reads what DWM is holding, and for a window that has not
// painted — or one behind another — that is sometimes a blank surface. A
// blank PNG compresses to almost nothing, so the file size gives it away.
async function captureSettled(hwnd: number, path: string) {
  for (let attempt = 0; ; attempt++) {
    captureWindowToPng(hwnd, path);
    if (attempt >= 4 || statSync(path).size > 20000) {
      return;
    }
    setForegroundWindow(hwnd);
    bringToTopAndRedraw(hwnd);
    await sleep(300);
  }
}

const { debug, nobuild, pages } = parseArgs(Bun.argv.slice(2));
setProcessDpiAware();

let rustRoot: string;
try {
  rustRoot = ensureRustTree(root);
} catch (e) {
  die(e instanceof Error ? e.message : String(e));
}

if (!nobuild) {
  if (run(["bun", "cmd/build.ts", debug ? "-dbg" : "-rel", "story"], root) !== 0) {
    process.exit(1);
  }
}

if (!existsSync(cppExe(debug))) {
  die(`Missing ${cppExe(debug)}`);
}
if (!existsSync(rustExe(debug))) {
  die(`Missing ${rustExe(debug)}. Build with: cargo build -p gpui-component-story`);
}

const outDir = join(root, "out", "compare-story");
mkdirSync(outDir, { recursive: true });

type Pair = {
  rustProc: Bun.Subprocess;
  cppProc: Bun.Subprocess;
  rustHwnd: number;
  cppHwnd: number;
};

// Both apps take the page on the command line, so every page costs a launch,
// and the Rust window takes ~300ms to appear -- a quarter of what a page needs
// end to end. Start the next pair while this one is still being photographed.
// An occluded window captures exactly the same picture as a visible one (the
// shot comes from what DWM holds, not from the screen), so the new pair
// landing on top of the pair being shot does not matter.
async function launch(slug: string): Promise<Pair> {
  const rustProc = Bun.spawn([rustExe(debug), rustStoryArg(slug)], {
    cwd: rustDir(),
    stdout: "ignore",
    stderr: "ignore",
  });
  // Ours can open at the rect placePair would move it to; the Rust app has no
  // such flag and gets moved as before.
  const half = workAreaHalfRect("right");
  const cppProc = Bun.spawn([cppExe(debug), `-gpui-window=${half.x},${half.y},${half.w},${half.h}`, slug], {
    cwd: join(root, "out", debug ? "dbg" : "rel"),
    stdout: "ignore",
    stderr: "ignore",
  });
  const rustHwnd = await waitForPidWindow(rustProc.pid ?? 0, 30000);
  const cppHwnd = await waitForPidWindow(cppProc.pid ?? 0, 15000);
  return { rustProc, cppProc, rustHwnd, cppHwnd };
}

async function close(pair: Pair | null) {
  if (pair) {
    await Promise.all([killAndWait(pair.rustProc), killAndWait(pair.cppProc)]);
  }
}

for (const slug of pages) {
  if (!(slugs as readonly string[]).includes(slug)) {
    die(`Unknown story slug: ${slug}`);
  }
}

// One pair at a time. Starting the next pair while this one is being
// photographed used to be free — an occluded window was said to photograph
// the same as a visible one — and it is not: the pair coming up steals the
// front, and a window moved while another is being created comes back at the
// size it opened with. Both show up as a shot that is nothing like the page.
for (let i = 0; i < pages.length; i++) {
  const slug = pages[i]!;
  console.log(`
=== ${slug} ===`);
  const pair = await launch(slug);
  const { rustHwnd, cppHwnd } = pair;
  if (!rustHwnd || !cppHwnd) {
    await close(pair);
    die(`window did not appear (rust=${!!rustHwnd} cpp=${!!cppHwnd})`);
  }
  await placePair(rustHwnd, cppHwnd);
  const wa = getWorkArea();
  setCursorPos(wa.left + 4, wa.top + 4);
  setForegroundWindow(rustHwnd);
  setForegroundWindow(cppHwnd);
  await sleep(500);
  // Virtual rows need their first measured-height feedback frame before a
  // screenshot can say whether they overlap.
  if (slug === "message-scroller") await sleep(1200);
  const rustPng = join(outDir, `${slug}-rust.png`);
  const cppPng = join(outDir, `${slug}-cpp.png`);
  await captureSettled(rustHwnd, rustPng);
  await captureSettled(cppHwnd, cppPng);
  console.log(`  ${rustPng}`);
  console.log(`  ${cppPng}`);
  await close(pair);
}
