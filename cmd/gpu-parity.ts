// Automated D2D/custom-GPU visual parity and recovery stress checks.
//
//   bun cmd/gpu-parity.ts
//   bun cmd/gpu-parity.ts -nobuild introduction text-view
//   bun cmd/gpu-parity.ts -list
//
// Each case is captured from fresh D2D, D3D11 and D3D12 processes at the
// same geometry. The two custom backends are then resized repeatedly while
// __gpu_reset_every drives the production device-recovery path. Captures and
// machine-readable results land under out/gpu-parity/.

import { existsSync, mkdirSync, readFileSync, rmSync, statSync, writeFileSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { comparePngFiles } from "./imgdiff.ts";
import {
  bringToTopAndRedraw,
  captureWindowToPng,
  getWindowRect,
  killAndWait,
  moveWindow,
  setProcessDpiAware,
  sleep,
  waitForPidWindow,
} from "./winapi.ts";

const root = resolve(dirname(Bun.main), "..");
process.chdir(root);

const cases = ["introduction", "calendar", "text-view"];
const backends = ["d2d", "d3d11", "d3d12"] as const;
type Backend = (typeof backends)[number];

type Args = {
  debug: boolean;
  nobuild: boolean;
  list: boolean;
  selected: string[];
};

type Comparison = {
  case: string;
  name: string;
  status: "pass" | "fail";
  anyPct?: number;
  bigPct?: number;
  maxAnyPct: number;
  maxBigPct: number;
  error?: string;
};

function usage(): never {
  console.error("Usage: bun cmd/gpu-parity.ts [-rel|-dbg] [-nobuild] [-list] [case ...]");
  process.exit(2);
}

function parseArgs(argv: string[]): Args {
  let debug = false;
  let nobuild = false;
  let list = false;
  const selected: string[] = [];
  for (const arg of argv) {
    if (arg === "-rel") debug = false;
    else if (arg === "-dbg") debug = true;
    else if (arg === "-nobuild") nobuild = true;
    else if (arg === "-list") list = true;
    else if (arg.startsWith("-")) usage();
    else selected.push(arg);
  }
  return { debug, nobuild, list, selected };
}

function pct(n: number, total: number): number {
  return total ? (n * 100) / total : 0;
}

function run(command: string[]): number {
  console.log(`> ${command.join(" ")}`);
  const result = Bun.spawnSync(command, { cwd: root, stdout: "inherit", stderr: "inherit" });
  return result.exitCode ?? 1;
}

function pathFor(outDir: string, testCase: string, backend: Backend, stress: boolean): string {
  return join(outDir, `${testCase}-${backend}-${stress ? "stress" : "fresh"}.png`);
}

async function capture(
  exe: string,
  outDir: string,
  testCase: string,
  backend: Backend,
  stress: boolean,
): Promise<number> {
  const dst = pathFor(outDir, testCase, backend, stress);
  const logPath = join(root, "out", "gpui.log");
  try {
    await Bun.file(logPath).delete();
  } catch {}
  const runtime = ["-gpui-window=80,80,900,700", testCase, `__paint=${backend}`, "__msaa=4"];
  if (stress) runtime.push("__gpu_reset_every=3");
  const proc = Bun.spawn([exe, ...runtime], {
    cwd: root,
    stdout: "ignore",
    stderr: "ignore",
    env: { ...process.env, GPUI_TODAY: "2026-02-16", GPUI_HOVER_HOLD: "1" },
  });
  try {
    const hwnd = await waitForPidWindow(proc.pid ?? 0, 15000);
    if (!hwnd) throw new Error(`${backend} window did not appear`);
    await sleep(500);
    if (stress) {
      const initial = getWindowRect(hwnd);
      const widths = [641, 1037, 777, 920, 683, 1111];
      const heights = [509, 733, 621, 547, 759, 665];
      for (let i = 0; i < 24; i++) {
        if (proc.exitCode !== null) throw new Error(`${backend} exited during resize stress`);
        if (!moveWindow(hwnd, initial.left, initial.top, widths[i % widths.length]!, heights[i % heights.length]!)) {
          throw new Error(`${backend} MoveWindow failed at resize ${i + 1}`);
        }
        await sleep(20);
      }
      moveWindow(hwnd, initial.left, initial.top, 900, 700);
      await sleep(600);
    }
    bringToTopAndRedraw(hwnd);
    await sleep(300);
    for (let attempt = 0; attempt < 5; attempt++) {
      if (!captureWindowToPng(hwnd, dst)) throw new Error(`${backend} capture failed`);
      // A genuinely blank 900x700 PrintWindow image is below 5 KB. Sparse
      // pages such as calendar legitimately compress below the 20 KB probe
      // threshold used by the denser story comparison suite.
      if (statSync(dst).size > 5000) break;
      bringToTopAndRedraw(hwnd);
      await sleep(250);
    }
    if (statSync(dst).size <= 5000) throw new Error(`${backend} capture stayed blank`);
    if (!stress) return 0;
    const log = existsSync(logPath) ? readFileSync(logPath, "utf8") : "";
    const recoveries = log.match(new RegExp(`paint/${backend}: injected device recovery`, "g"))?.length ?? 0;
    if (recoveries < 5) {
      throw new Error(`${backend} exercised only ${recoveries} injected recoveries`);
    }
    return recoveries;
  } finally {
    await killAndWait(proc);
  }
}

function compare(
  testCase: string,
  name: string,
  a: string,
  b: string,
  maxAnyPct: number,
  maxBigPct: number,
): Comparison {
  try {
    const diff = comparePngFiles(a, b, 34, 90);
    const anyPct = pct(diff.any, diff.total);
    const bigPct = pct(diff.big, diff.total);
    return {
      case: testCase,
      name,
      status: anyPct <= maxAnyPct && bigPct <= maxBigPct ? "pass" : "fail",
      anyPct,
      bigPct,
      maxAnyPct,
      maxBigPct,
    };
  } catch (error) {
    return {
      case: testCase,
      name,
      status: "fail",
      maxAnyPct,
      maxBigPct,
      error: error instanceof Error ? error.message : String(error),
    };
  }
}

const args = parseArgs(Bun.argv.slice(2));
if (args.list) {
  for (const testCase of cases) console.log(testCase);
  process.exit(0);
}
if (process.platform !== "win32") {
  console.error("The GPU parity suite requires Windows D3D and native window capture.");
  process.exit(2);
}
const selected = args.selected.length ? cases.filter((testCase) => args.selected.includes(testCase)) : cases;
for (const testCase of args.selected) {
  if (!cases.includes(testCase)) {
    console.error(`Unknown GPU parity case: ${testCase}`);
    usage();
  }
}
const profile = args.debug ? "dbg" : "rel";
if (
  !args.nobuild &&
  run(["bun", "cmd/build.ts", args.debug ? "-dbg" : "-rel", "--win-backend=all", "showcase"]) !== 0
) {
  process.exit(1);
}
const exe = join(root, "out", profile, "showcase.exe");
if (!existsSync(exe)) {
  console.error(`Missing ${exe}; rerun without -nobuild.`);
  process.exit(1);
}

setProcessDpiAware();
const outDir = join(root, "out", "gpu-parity");
rmSync(outDir, { recursive: true, force: true });
mkdirSync(outDir, { recursive: true });
const results: Comparison[] = [];
const recoveryCounts: Record<string, number> = {};
let harnessFailed = false;

for (const testCase of selected) {
  console.log(`\n=== GPU parity: ${testCase} ===`);
  try {
    for (const backend of backends) {
      await capture(exe, outDir, testCase, backend, false);
      console.log(`  captured ${backend}`);
    }
    for (const backend of ["d3d11", "d3d12"] as const) {
      const count = await capture(exe, outDir, testCase, backend, true);
      recoveryCounts[`${testCase}-${backend}`] = count;
      console.log(`  stressed ${backend}: ${count} recoveries`);
    }
    const d2d = pathFor(outDir, testCase, "d2d", false);
    const d11 = pathFor(outDir, testCase, "d3d11", false);
    const d12 = pathFor(outDir, testCase, "d3d12", false);
    results.push(compare(testCase, "D2D / D3D11", d2d, d11, 9, 1));
    results.push(compare(testCase, "D2D / D3D12", d2d, d12, 9, 1));
    results.push(compare(testCase, "D3D11 / D3D12", d11, d12, 0.02, 0.005));
    results.push(compare(testCase, "D3D11 after stress", d11, pathFor(outDir, testCase, "d3d11", true), 0.02, 0.005));
    results.push(compare(testCase, "D3D12 after stress", d12, pathFor(outDir, testCase, "d3d12", true), 0.02, 0.005));
  } catch (error) {
    harnessFailed = true;
    const message = error instanceof Error ? error.message : String(error);
    results.push({
      case: testCase,
      name: "harness",
      status: "fail",
      maxAnyPct: 0,
      maxBigPct: 0,
      error: message,
    });
    console.error(`  FAIL ${message}`);
  }
}

for (const result of results) {
  const metrics =
    result.anyPct === undefined
      ? (result.error ?? "no result")
      : `any=${result.anyPct.toFixed(3)}%/${result.maxAnyPct.toFixed(3)}% big=${result.bigPct!.toFixed(3)}%/${result.maxBigPct.toFixed(3)}%`;
  console.log(`  ${result.status.toUpperCase()} ${result.case} ${result.name}: ${metrics}`);
}
const failed = results.filter((result) => result.status === "fail").length;
writeFileSync(
  join(outDir, "results.json"),
  `${JSON.stringify({ generatedAt: new Date().toISOString(), profile, recoveryCounts, results }, null, 2)}\n`,
);
const md = [
  "# Windows GPU parity suite",
  "",
  `Generated ${new Date().toISOString()} with \`bun cmd/gpu-parity.ts${args.nobuild ? " -nobuild" : ""}\`.`,
  "",
  "| case | comparison | result | any / budget | big / budget |",
  "| --- | --- | --- | ---: | ---: |",
  ...results.map(
    (result) =>
      `| ${result.case} | ${result.name} | ${result.status} | ${result.anyPct === undefined ? "-" : `${result.anyPct.toFixed(3)}% / ${result.maxAnyPct.toFixed(3)}%`} | ${result.bigPct === undefined ? "-" : `${result.bigPct.toFixed(3)}% / ${result.maxBigPct.toFixed(3)}%`} |`,
  ),
  "",
  `${results.length - failed} passed, ${failed} failed.`,
  "",
].join("\n");
writeFileSync(join(outDir, "results.md"), md);
console.log(`\n${md}`);
console.log(`Artifacts: ${outDir}`);
process.exit(harnessFailed || failed ? 1 : 0);
