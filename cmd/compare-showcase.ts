// Side-by-side showcase compare: Rust original (left) vs C++ port (right).
//
//   bun cmd/compare-showcase.ts                 # every page, screenshots
//   bun cmd/compare-showcase.ts accordion
//   bun cmd/compare-showcase.ts -rel accordion
//   bun cmd/compare-showcase.ts -keep accordion  # leave windows open
//
// Rust takes the left half of the work area, ours the right, both 80% of the
// work area tall — same placement as `bun cmd/run.ts -compare`, so the two
// shots are the same size and directly comparable.
//
// Screenshots: out/compare/<slug>-rust.png and <slug>-cpp.png

import { existsSync, mkdirSync, rmSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import {
  captureWindowToPng,
  clickClient,
  getClientRect,
  getWorkArea,
  hoverClient,
  killAndWait,
  placeOnWorkAreaHalf,
  setCursorPos,
  setForegroundWindow,
  setProcessDpiAware,
  sleep,
  waitForPidWindow,
} from "./winapi.ts";
import { ensureRustTree, rustTreeDir } from "./run.ts";

const root = resolve(dirname(Bun.main), "..");
process.chdir(root);

export const slugs = [
  "overview",
  "accordion",
  "alert-dialog",
  "avatar",
  "button",
  "calendar",
  "checkbox",
  "collapsible",
  "color-picker",
  "combobox",
  "date-picker",
  "dialog",
  "dock",
  "editor",
  "hover-card",
  "input",
  "link",
  "number-input",
  "otp-input",
  "pagination",
  "popover",
  "popup",
  "progress",
  "radio",
  "radio-group",
  "resizable",
  "scrollbar",
  "select",
  "sheet",
  "slider",
  "switch",
  "table",
  "tabs",
  "text-selection",
  "textarea",
  "time-field",
  "toast",
  "toggle",
  "toggle-group",
  "tooltip",
  "tree",
  "virtual-list",
] as const;

const usage = `Usage: bun cmd/compare-showcase.ts [-rel|-dbg] [-keep] [-nobuild] [slug|all]

  Opens Rust (left) and C++ (right) showcase windows, screenshots each page
  into out/compare/. Default slug is all.`;

function die(msg?: string): never {
  if (msg) {
    console.error(msg);
  }
  console.error(usage);
  process.exit(1);
}

function parseArgs(argv: string[]): { debug: boolean; keep: boolean; nobuild: boolean; pages: string[] } {
  let debug = false;
  let keep = false;
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
    if (raw === "-keep") {
      keep = true;
      continue;
    }
    if (raw === "-nobuild") {
      nobuild = true;
      continue;
    }
    if (raw.startsWith("-")) {
      die(`Unknown flag: ${raw}`);
    }
    const a = raw.toLowerCase();
    if (a === "all") {
      continue;
    }
    if (!(slugs as readonly string[]).includes(a)) {
      die(`Unknown page: ${raw}`);
    }
    pages.push(a);
  }
  return { debug, keep, nobuild, pages: pages.length ? pages : [...slugs] };
}

function rustDir(): string {
  return rustTreeDir(root);
}

function rustExe(debug: boolean): string {
  const prof = debug ? "debug" : "release";
  return join(rustDir(), "target", prof, "examples", "components.exe");
}

function cppExe(debug: boolean): string {
  return join(root, "out", debug ? "dbg" : "rel", "showcase.exe");
}

function run(cmd: string[], cwd: string): number {
  const r = Bun.spawnSync(cmd, { cwd, stdout: "inherit", stderr: "inherit" });
  return r.exitCode ?? 1;
}

async function launch(exe: string, slug: string, cwd: string): Promise<Bun.Subprocess> {
  const args = slug === "overview" ? [exe] : [exe, slug];
  return Bun.spawn(args, { cwd, stdout: "ignore", stderr: "ignore" });
}

// Same placement as `bun cmd/run.ts -compare`: rust on the left half, ours on
// the right, both 80% of the work area tall. Keeps the two shots comparable.
function placePair(rustHwnd: number, cppHwnd: number) {
  placeOnWorkAreaHalf(rustHwnd, "left");
  placeOnWorkAreaHalf(cppHwnd, "right");
}

type Pair = {
  rustProc: Bun.Subprocess;
  cppProc: Bun.Subprocess;
  rustHwnd: number;
  cppHwnd: number;
};

async function openPair(slug: string, debug: boolean): Promise<Pair> {
  const rustProc = await launch(rustExe(debug), slug, rustDir());
  const cppProc = await launch(cppExe(debug), slug, join(root, "out", debug ? "dbg" : "rel"));
  const rustHwnd = await waitForPidWindow(rustProc.pid ?? 0, 30000);
  const cppHwnd = await waitForPidWindow(cppProc.pid ?? 0, 15000);
  if (!rustHwnd) {
    await killAndWait(rustProc);
    await killAndWait(cppProc);
    throw new Error(`Rust showcase window did not appear (${slug})`);
  }
  if (!cppHwnd) {
    await killAndWait(rustProc);
    await killAndWait(cppProc);
    throw new Error(`C++ showcase window did not appear (${slug})`);
  }
  placePair(rustHwnd, cppHwnd);
  const wa = getWorkArea();
  setCursorPos(wa.left + 4, wa.top + 4);
  setForegroundWindow(rustHwnd);
  setForegroundWindow(cppHwnd);
  await sleep(400);
  return { rustProc, cppProc, rustHwnd, cppHwnd };
}

async function closePair(p: Pair) {
  // killAndWait waits for the process to actually go now, so there is nothing
  // left for a trailing sleep to cover.
  await Promise.all([killAndWait(p.rustProc), killAndWait(p.cppProc)]);
}

const { debug, keep, nobuild, pages } = parseArgs(Bun.argv.slice(2));
setProcessDpiAware();

let rustRoot: string;
try {
  rustRoot = ensureRustTree(root);
} catch (e) {
  die(e instanceof Error ? e.message : String(e));
}

if (!nobuild) {
  console.log(`Building C++ showcase (${debug ? "dbg" : "rel"})`);
  if (run(["bun", "cmd/build.ts", debug ? "-dbg" : "-rel", "showcase"], root) !== 0) {
    process.exit(1);
  }
  const rustArgs = ["build", ...(debug ? [] : ["--release"]), "-p", "gpui-base", "--example", "components"];
  console.log(`Building rust: cargo ${rustArgs.join(" ")}`);
  if (run(["cargo", ...rustArgs], rustRoot) !== 0) {
    process.exit(1);
  }
}
if (!existsSync(cppExe(debug))) {
  die(`Missing ${cppExe(debug)}`);
}
if (!existsSync(rustExe(debug))) {
  die(`Missing ${rustExe(debug)}`);
}

const outDir = join(root, "out", "compare");
if (existsSync(outDir) && pages.length === slugs.length) {
  rmSync(outDir, { recursive: true, force: true });
}
mkdirSync(outDir, { recursive: true });

for (const slug of pages) {
  console.log(`\n=== ${slug} ===`);
  const pair = await openPair(slug, debug);
  const rustPng = join(outDir, `${slug}-rust.png`);
  const cppPng = join(outDir, `${slug}-cpp.png`);
  if (!captureWindowToPng(pair.rustHwnd, rustPng)) {
    console.error(`  failed to capture rust ${slug}`);
  } else {
    console.log(`  ${rustPng}`);
  }
  if (!captureWindowToPng(pair.cppHwnd, cppPng)) {
    console.error(`  failed to capture cpp ${slug}`);
  } else {
    console.log(`  ${cppPng}`);
  }

  // Light interaction pass so hover/click pages are exercised too.
  const crc = getClientRect(pair.cppHwnd);
  const rrc = getClientRect(pair.rustHwnd);
  const cx = Math.floor((crc.right - crc.left) / 2);
  const cy = Math.floor((crc.bottom - crc.top) / 2);
  const rx = Math.floor((rrc.right - rrc.left) / 2);
  const ry = Math.floor((rrc.bottom - rrc.top) / 2);
  // Hover a control-ish point (slightly above center) so hover styles show.
  await hoverClient(pair.rustHwnd, rx, Math.max(8, ry - 24), 120);
  await hoverClient(pair.cppHwnd, cx, Math.max(8, cy - 24), 180);
  captureWindowToPng(pair.rustHwnd, join(outDir, `${slug}-rust-hover.png`));
  captureWindowToPng(pair.cppHwnd, join(outDir, `${slug}-cpp-hover.png`));

  // Client-relative fractions to hit the actual control, not just the window center.
  const clickAt: Record<string, Array<[number, number]>> = {
    accordion: [[0.5, 0.52]],
    "alert-dialog": [[0.5, 0.5]],
    checkbox: [[0.42, 0.5]],
    collapsible: [[0.5, 0.46]],
    dialog: [[0.5, 0.5]],
    popover: [[0.5, 0.5]],
    popup: [[0.5, 0.5]],
    sheet: [[0.5, 0.5]],
    toast: [[0.5, 0.5]],
    toggle: [[0.5, 0.5]],
    switch: [[0.52, 0.5]],
    "color-picker": [[0.5, 0.5]],
    combobox: [[0.5, 0.5]],
    select: [[0.5, 0.5]],
    "date-picker": [[0.5, 0.5]],
    tabs: [[0.55, 0.42]],
    tree: [[0.48, 0.55]],
    radio: [[0.5, 0.52]],
    "radio-group": [[0.5, 0.52]],
    "toggle-group": [[0.55, 0.5]],
    pagination: [[0.58, 0.5]],
    calendar: [[0.5, 0.55]],
  };
  const pts = clickAt[slug];
  if (pts) {
    const rustW = rrc.right - rrc.left;
    const rustH = rrc.bottom - rrc.top;
    const cppW = crc.right - crc.left;
    const cppH = crc.bottom - crc.top;
    for (const [fx, fy] of pts) {
      await clickClient(pair.rustHwnd, Math.floor(rustW * fx), Math.floor(rustH * fy), 220);
      await clickClient(pair.cppHwnd, Math.floor(cppW * fx), Math.floor(cppH * fy), 280);
    }
    captureWindowToPng(pair.rustHwnd, join(outDir, `${slug}-rust-click.png`));
    captureWindowToPng(pair.cppHwnd, join(outDir, `${slug}-cpp-click.png`));
    console.log(`  clicked ${slug}`);
  }

  if (keep) {
    console.log("  -keep: windows left open (Ctrl+C when done)");
    await Promise.all([pair.rustProc.exited, pair.cppProc.exited]);
    break;
  }
  await closePair(pair);
}

console.log(`\nWrote screenshots to ${outDir}\\`);
