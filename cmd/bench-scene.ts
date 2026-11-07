// Measure scene modes under actual window input and timer invalidations.
// Windows only, because only paint_win.cpp currently records a scene and the
// driver uses the same Win32 input seam as the screenshot tools.
//
//   bun cmd/bench-scene.ts
//   bun cmd/bench-scene.ts -n=20 -nobuild
//   bun cmd/bench-scene.ts -only=introduction-scroll
//   bun cmd/bench-scene.ts -only=icon-scroll -paint=d3d11
//   bun cmd/bench-scene.ts -only=icon-scroll -absolute-paths
//   bun cmd/bench-scene.ts -only=introduction-scroll -settle=2000
//
// -settle=ms waits that long after the interaction before the final
// capture (150 by default), so a scrollbar fading out has finished and the
// skip and damage captures can be compared pixel for pixel.

import { existsSync, mkdirSync, rmSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import {
  captureWindowSurfaceToPng,
  clickClient,
  clientToScreen,
  getClientRect,
  getCursorPos,
  hoverClient,
  killAndWait,
  moveWindow,
  packCoords,
  sendMessage,
  setCursorPos,
  setForegroundWindow,
  setProcessDpiAware,
  setTopmost,
  sleep,
  waitForPidWindow,
  WM_CLOSE,
} from "./winapi.ts";

const root = resolve(dirname(Bun.main), "..");
process.chdir(root);

if (process.platform !== "win32") {
  console.error("cmd/bench-scene.ts currently requires Windows; only the Windows painter records a scene.");
  process.exit(1);
}

let cycles = 12;
let build = true;
let onlyName = "";
let paintName = "";
let absolutePaths = false;
let settleMs = 150;
for (const arg of Bun.argv.slice(2)) {
  if (arg === "-nobuild") {
    build = false;
  } else if (arg.startsWith("-n=")) {
    cycles = Math.max(4, Number.parseInt(arg.slice(3), 10) || cycles);
  } else if (arg.startsWith("-only=")) {
    onlyName = arg.slice(6);
  } else if (arg.startsWith("-paint=")) {
    paintName = arg.slice(7);
  } else if (arg.startsWith("-settle=")) {
    settleMs = Math.max(0, Number.parseInt(arg.slice(8), 10) || 0);
  } else if (arg === "-absolute-paths") {
    absolutePaths = true;
  } else {
    console.error(`unknown argument: ${arg}`);
    process.exit(1);
  }
}
if (paintName && !["d2d", "d3d11", "d3d12"].includes(paintName)) {
  console.error(`unknown paint backend: ${paintName}`);
  process.exit(1);
}

type Scenario = {
  name: string;
  exe: string;
  args: string[];
  drive: (hwnd: number) => Promise<void>;
  deterministic: boolean;
  expectVisibleChange: boolean;
};

const scenarios: Scenario[] = [
  {
    name: "scroll",
    exe: "table_in_scrollable",
    args: [],
    deterministic: true,
    expectVisibleChange: true,
    drive: async (hwnd) => {
      const r = getClientRect(hwnd);
      const x = Math.floor(r.right / 2);
      const y = Math.floor(r.bottom / 2);
      const screen = clientToScreen(hwnd, x, y);
      for (let i = 0; i < cycles; i++) {
        const delta = i < Math.floor((cycles * 3) / 4) ? -120 : 120;
        sendMessage(hwnd, 0x020a /* WM_MOUSEWHEEL */, (delta << 16) >>> 0, packCoords(screen.x, screen.y));
        await sleep(45);
      }
    },
  },
  {
    name: "introduction-scroll",
    exe: "story",
    args: ["introduction"],
    deterministic: true,
    expectVisibleChange: true,
    drive: async (hwnd) => {
      const r = getClientRect(hwnd);
      const x = Math.floor(r.right / 2);
      const y = Math.floor(r.bottom / 2);
      const screen = clientToScreen(hwnd, x, y);
      for (let i = 0; i < cycles; i++) {
        const delta = i < Math.floor((cycles * 3) / 4) ? -120 : 120;
        sendMessage(hwnd, 0x020a /* WM_MOUSEWHEEL */, (delta << 16) >>> 0, packCoords(screen.x, screen.y));
        await sleep(45);
      }
    },
  },
  {
    name: "icon-scroll",
    exe: "story",
    // The long Button story has eighteen icon paths spread through enough
    // sections to move them through the viewport while scrolling.
    args: ["button"],
    deterministic: true,
    expectVisibleChange: true,
    drive: async (hwnd) => {
      const r = getClientRect(hwnd);
      const x = Math.floor(r.right / 2);
      const y = Math.floor(r.bottom / 2);
      const screen = clientToScreen(hwnd, x, y);
      for (let i = 0; i < cycles; i++) {
        const delta = i < Math.floor((cycles * 3) / 4) ? -120 : 120;
        sendMessage(hwnd, 0x020a /* WM_MOUSEWHEEL */, (delta << 16) >>> 0, packCoords(screen.x, screen.y));
        await sleep(45);
      }
    },
  },
  {
    name: "hover",
    exe: "showcase",
    args: ["button"],
    deterministic: true,
    expectVisibleChange: false,
    drive: async (hwnd) => {
      // The centered row starts left of the window midpoint. Use the primary
      // button so each move crosses an element with a visible hover style.
      const r = getClientRect(hwnd);
      const inside = { x: Math.floor(r.right / 2) - 110, y: Math.floor(r.bottom / 2) };
      const outside = { x: 12, y: 12 };
      for (let i = 0; i < cycles; i++) {
        const p = i + 1 === cycles || (i & 1) === 0 ? inside : outside;
        if (!(await hoverClient(hwnd, p.x, p.y, 45))) {
          throw new Error("the desktop refused to place the pointer; hover measurements would not be real");
        }
      }
    },
  },
  {
    name: "caret",
    exe: "input",
    args: [],
    deterministic: false,
    expectVisibleChange: false,
    drive: async (hwnd) => {
      const r = getClientRect(hwnd);
      // The 32-DIP input is centered; its bottom edge is exactly the client
      // midpoint in this example, and hit-testing excludes that edge.
      await clickClient(hwnd, Math.floor(r.right / 2), Math.floor(r.bottom / 2) - 8, 100);
      await sleep(cycles * 520);
    },
  },
  {
    name: "popup",
    exe: "showcase",
    args: ["popup"],
    deterministic: true,
    expectVisibleChange: true,
    drive: async (hwnd) => {
      const r = getClientRect(hwnd);
      const x = Math.floor(r.right / 2);
      const y = Math.floor(r.bottom / 2);
      // Finish open in every run so the final visible-surface capture checks
      // the deferred layer rather than the less interesting closed state.
      const count = cycles | 1;
      for (let i = 0; i < count; i++) {
        await clickClient(hwnd, x, y, 70);
      }
    },
  },
  {
    name: "chart-tick",
    exe: "system_monitor",
    args: [],
    deterministic: false,
    expectVisibleChange: false,
    drive: async () => {
      await sleep(cycles * 520);
    },
  },
];

const modes = ["off", "skip", "damage"] as const;
type Mode = (typeof modes)[number];
type Frame = {
  draw: number;
  build: number;
  layout: number;
  paint: number;
  presented: number;
  damage: number;
  pathHits: number;
  pathMisses: number;
  pathBuild: number;
  arena: number;
  arenaAllocs: number;
  privateBytes: number;
};
type Result = {
  scenario: string;
  mode: Mode;
  frames: Frame[];
  image: string;
  imageHash: string;
  visiblyChanged: boolean;
};

function runBuild(target: string): void {
  const args = ["bun", "cmd/build.ts", "-rel"];
  if (paintName) {
    args.push("--win-backend=all");
  }
  args.push(target);
  const result = Bun.spawnSync(args, {
    cwd: root,
    stdout: "inherit",
    stderr: "inherit",
  });
  if ((result.exitCode ?? 1) !== 0) {
    process.exit(result.exitCode ?? 1);
  }
}

const selectedScenarios = onlyName ? scenarios.filter((scenario) => scenario.name === onlyName) : scenarios;
if (selectedScenarios.length === 0) {
  console.error(`unknown scenario: ${onlyName}`);
  process.exit(1);
}

if (build) {
  for (const target of [...new Set(selectedScenarios.map((scenario) => scenario.exe))]) {
    runBuild(target);
  }
}

const outDir = join(root, "out", "scene-bench");
mkdirSync(outDir, { recursive: true });
const logPath = join(root, "out", "gpui.log");

function parseFrames(text: string): Frame[] {
  const frames: Frame[] = [];
  const pattern =
    /interaction-bench frame=\d+ draw=([\d.]+) build=([\d.]+) layout=([\d.]+) paint=([\d.]+) presented=(\d+) invalidations=\d+ prims=-?\d+ changed=-?\d+ damage=(-?[\d.]+) pathHits=(-?\d+) pathMisses=(-?\d+) pathBuild=(-?[\d.]+) arena=(\d+) arenaAllocs=(\d+) private=(\d+)/g;
  for (const match of text.matchAll(pattern)) {
    frames.push({
      draw: Number(match[1]),
      build: Number(match[2]),
      layout: Number(match[3]),
      paint: Number(match[4]),
      presented: Number(match[5]),
      damage: Number(match[6]),
      pathHits: Number(match[7]),
      pathMisses: Number(match[8]),
      pathBuild: Number(match[9]),
      arena: Number(match[10]),
      arenaAllocs: Number(match[11]),
      privateBytes: Number(match[12]),
    });
  }
  return frames;
}

async function hashFile(path: string): Promise<string> {
  const hasher = new Bun.CryptoHasher("sha256");
  hasher.update(await Bun.file(path).arrayBuffer());
  return hasher.digest("hex");
}

async function closeProcess(proc: Bun.Subprocess, hwnd: number): Promise<void> {
  sendMessage(hwnd, WM_CLOSE, 0, 0);
  const closed = await Promise.race([proc.exited.then(() => true), sleep(3000).then(() => false)]);
  if (!closed) {
    await killAndWait(proc);
  }
}

async function runOne(scenario: Scenario, mode: Mode): Promise<Result> {
  const exe = join(root, "out", "rel", `${scenario.exe}.exe`);
  if (!existsSync(exe)) {
    throw new Error(`missing ${exe}`);
  }
  rmSync(logPath, { force: true });
  const args = [exe, ...scenario.args, `__scene=${mode}`];
  if (paintName) {
    args.push(`__paint=${paintName}`);
  }
  const proc = Bun.spawn(args, {
    cwd: root,
    env: {
      ...process.env,
      GPUI_HOVER_HOLD: "1",
      GPUI_INTERACTION_BENCH: "1",
      ...(absolutePaths ? { GPUI_PATH_CACHE_TRANSLATION: "off" } : {}),
    },
    stdout: "ignore",
    stderr: "ignore",
  });
  const hwnd = await waitForPidWindow(proc.pid ?? 0, 15000);
  if (!hwnd) {
    await killAndWait(proc);
    throw new Error(`${scenario.name}/${mode}: window did not appear`);
  }
  moveWindow(hwnd, 40, 40, 1100, 750);
  // The captures read the desktop, so nothing else may sit over the window.
  setTopmost(hwnd);
  setForegroundWindow(hwnd);
  await sleep(700);
  // Establish the same unhovered baseline even when a prior run left the
  // desktop pointer where this run's centered control will appear.
  await hoverClient(hwnd, 12, 12, 100);
  const startupFrames = parseFrames(await Bun.file(logPath).text()).length;
  const before = join(outDir, `${scenario.name}-${mode}-before.png`);
  if (!captureWindowSurfaceToPng(hwnd, before)) {
    await closeProcess(proc, hwnd);
    throw new Error(`${scenario.name}/${mode}: initial visible-surface capture failed`);
  }
  await scenario.drive(hwnd);
  await sleep(settleMs);
  const image = join(outDir, `${scenario.name}-${mode}.png`);
  if (!captureWindowSurfaceToPng(hwnd, image)) {
    await closeProcess(proc, hwnd);
    throw new Error(`${scenario.name}/${mode}: visible-surface capture failed`);
  }
  await closeProcess(proc, hwnd);
  const log = await Bun.file(logPath).text();
  const frames = parseFrames(log).slice(startupFrames);
  if (frames.length < 2) {
    throw new Error(`${scenario.name}/${mode}: only ${frames.length} measured frames`);
  }
  const imageHash = await hashFile(image);
  const visiblyChanged = (await hashFile(before)) !== imageHash;
  if (scenario.expectVisibleChange && !visiblyChanged) {
    throw new Error(`${scenario.name}/${mode}: interaction did not visibly change the client surface`);
  }
  return { scenario: scenario.name, mode, frames, image, imageHash, visiblyChanged };
}

function percentile(frames: Frame[], field: keyof Pick<Frame, "draw" | "paint" | "pathBuild">, p: number): number {
  const values = frames.map((frame) => frame[field]).sort((a, b) => a - b);
  return values[Math.min(values.length - 1, Math.floor(values.length * p))]!;
}

function mean(
  frames: Frame[],
  field: keyof Pick<Frame, "build" | "layout" | "damage" | "pathBuild" | "arena" | "arenaAllocs" | "privateBytes">,
): number {
  return frames.reduce((sum, frame) => sum + frame[field], 0) / frames.length;
}

setProcessDpiAware();
const cursor = getCursorPos();
const results: Result[] = [];
try {
  for (const scenario of selectedScenarios) {
    for (const mode of modes) {
      console.log(`${scenario.name}/${mode}`);
      results.push(await runOne(scenario, mode));
    }
  }
} finally {
  setCursorPos(cursor.x, cursor.y);
}

const lines = [
  "# Scene interaction benchmark",
  "",
  `Generated ${new Date().toISOString()} with \`bun cmd/bench-scene.ts -n=${cycles}${onlyName ? ` -only=${onlyName}` : ""}${paintName ? ` -paint=${paintName}` : ""}${absolutePaths ? " -absolute-paths" : ""}\`.`,
  "Times are milliseconds. Startup frames are excluded before each interaction begins.",
  "",
  "| scenario | scene | frames | draw median | draw p95 | paint median | paint p95 | build mean | layout mean | arena KiB | arena allocs | private MiB | mean damage | path hits | path misses | path build mean / p95 | presents |",
  "| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
];
for (const result of results) {
  const f = result.frames;
  lines.push(
    `| ${result.scenario} | ${result.mode} | ${f.length} | ${percentile(f, "draw", 0.5).toFixed(3)} | ${percentile(f, "draw", 0.95).toFixed(3)} | ${percentile(f, "paint", 0.5).toFixed(3)} | ${percentile(f, "paint", 0.95).toFixed(3)} | ${mean(f, "build").toFixed(3)} | ${mean(f, "layout").toFixed(3)} | ${(mean(f, "arena") / 1024).toFixed(1)} | ${mean(f, "arenaAllocs").toFixed(1)} | ${(mean(f, "privateBytes") / (1024 * 1024)).toFixed(1)} | ${mean(f, "damage").toFixed(3)} | ${f.reduce((n, frame) => n + Math.max(0, frame.pathHits), 0)} | ${f.reduce((n, frame) => n + Math.max(0, frame.pathMisses), 0)} | ${mean(f, "pathBuild").toFixed(3)} / ${percentile(f, "pathBuild", 0.95).toFixed(3)} | ${f.reduce((n, frame) => n + frame.presented, 0)} |`,
  );
}

lines.push("", "## Visible-surface checks", "");
for (const result of results) {
  lines.push(
    `- ${result.scenario}/${result.mode}: interaction ${result.visiblyChanged ? "changed the surface" : "left the surface unchanged"}.`,
  );
}
lines.push("");
for (const scenario of selectedScenarios.filter((item) => item.deterministic)) {
  const skip = results.find((result) => result.scenario === scenario.name && result.mode === "skip")!;
  const damage = results.find((result) => result.scenario === scenario.name && result.mode === "damage")!;
  lines.push(
    `- ${scenario.name}: ${skip.imageHash === damage.imageHash ? "identical" : "DIFFERENT"} skip/damage captures (${skip.imageHash.slice(0, 12)} / ${damage.imageHash.slice(0, 12)}).`,
  );
}
lines.push("");

const report = join(outDir, "results.md");
await Bun.write(report, `${lines.join("\n")}\n`);
console.log(`\n${lines.join("\n")}`);
console.log(`report ${report}`);
