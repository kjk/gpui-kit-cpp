// Build a gpui example and launch it — as a detached desktop process, under
// a debugger, or, with -wasm, as a page off a local server.
//
//   bun cmd/run.ts                         # print example list
//   bun cmd/run.ts app_assets
//   bun cmd/run.ts -dbg hello_world
//   bun cmd/run.ts -rel -asan system_monitor
//   bun cmd/run.ts -debugger showcase      # windbg / lldb / gdb, whichever is here
//   bun cmd/run.ts -windbg showcase        # force one (also -cdb, -gdb, -lldb)
//   bun cmd/run.ts -rel -compare story     # rust left half, ours right half
//   bun cmd/run.ts -wasm story             # build, serve, open a tab
//
// The build is cmd/build.ts, imported rather than spawned, so -rel / -dbg /
// -asan / -clang / -clean / -wasm mean exactly what they mean there and land
// in the same out/ directory. -dbg is the debug *build*; -debugger is what
// runs it under a debugger.
//
// Nothing is downloaded that the run does not need: the Rust spec tree under
// .work/ is cloned only for -compare, and emscripten is only looked for with
// -wasm.
//
// The upstream pins are here too — which gpui-kit checkin this port
// matches, and which crates it ports — because -compare is the only thing
// that fetches anything from them, and this script has to carry them into
// gpui-kit-cpp-dist, where it and build.ts are the whole of cmd/. `-versions`
// prints them and syncs the tree.
//
// To run the Linux build from a Windows checkout, use cmd/wsl-run.ts.

import { existsSync, lstatSync, mkdirSync, readdirSync, rmSync, statSync } from "node:fs";
import { extname, join, relative } from "node:path";
import {
  build,
  checkBuildFlags,
  consoleTargets,
  defaultBuildFlags,
  emsdkNode,
  examplesFor,
  findEmcc,
  isKnownTarget,
  outDir,
  outFilePath,
  platformFor,
  printSizeTable,
  formatCmd,
  printCmd,
  root,
  scriptDir,
  scriptPath,
  takeBuildFlag,
  type BuildFlags,
  type Platform,
} from "./build.ts";

// ─── command line ─────────────────────────────────────────────────────────

/** Named on the command line to force one debugger; null means "any". */
type DebuggerKind = "windbg" | "cdb" | "gdb" | "lldb";

const self = scriptPath("run.ts");

const usage = `Usage: bun ${self} [-rel|-dbg] [-asan] [-clang] [-wasm] [-clean]
                     [-markdown=mini|full] [-html=mini|full]
                     [--win-backend=d2d|d3d11|d3d12|all]
                     [-debugger|-windbg|-cdb|-gdb|-lldb] [-compare]
                     [-no-build] [-no-open] [-port N] <example> [-- <args...>]
       bun ${self} -versions

  -rel        release (default)
  -dbg        debug build (this is the build, not the debugger)
  -asan       AddressSanitizer; combines with -rel or -dbg
  -clang      Windows: build with clang-cl instead of cl.exe
  -clean      delete out/<dir>/ before building
  -markdown=mini|full  Markdown parser implementation (default full)
  -html=mini|full      HTML parser implementation (default full)
  -no-build   launch what is already in out/, without compiling
  --win-backend=d2d|d3d11|d3d12|all
              Windows renderer implementations compiled into the executable;
              d2d is the default

  -debugger   run under whichever debugger this machine has
              Windows: windbg, then cdb.  Linux: gdb, then lldb.  macOS: lldb.
  -windbg     Windows: force WinDbg (windbgx / DbgX.Shell)
  -cdb        Windows: force cdb, the console debugger
  -gdb        Linux, macOS: force gdb
  -lldb       Linux, macOS: force lldb

  -compare    also cargo-build and launch the Rust example from
              .work/gpui-component (cloned at the pinned SHA if missing);
              prints both binary sizes, then puts rust on the left half of
              the screen and ours on the right
  -versions   print the upstream checkins this port matches, sync
              .work/gpui-component to that SHA, and exit

  -wasm       build for the browser, serve out/wasm/<cfg>/ and open a tab
  -no-open    -wasm: do not launch a browser
  -port N     -wasm: listen on N (default 8000; the next free port if taken)
  --          pass every remaining argument unchanged to the selected binary;
              the runtime consumes __layout_reuse=off|on before GpuiMain;
              Windows also consumes __paint=d2d|d3d11|d3d12, __msaa=1|2|4|8,
              __scene=off|replay|cache|skip|damage and the test-only
              __gpu_reset_every=N

The target name is the last argument before --. -all is not accepted — pick one binary.`;

function die(msg?: string): never {
  if (msg) {
    console.error(msg);
    console.error("");
  }
  console.error(usage);
  process.exit(1);
}

function printExamples(plat: Platform, msg?: string): never {
  if (msg) {
    console.error(msg);
    console.error("");
  }
  console.error(usage);
  console.error("");
  console.error("Examples:");
  for (const n of examplesFor(plat)) {
    console.error(`  ${n}`);
  }
  process.exit(1);
}

type RunArgs = {
  target: string;
  flags: BuildFlags;
  plat: Platform;
  /** -debugger, or one of the forcing flags. */
  debugger: "any" | DebuggerKind | null;
  compare: boolean;
  noBuild: boolean;
  open: boolean;
  port: number;
  /** Arguments after --, passed unchanged to the selected binary. */
  appArgs: string[];
};

function parseArgs(argv: string[]): RunArgs {
  const flags = defaultBuildFlags();
  let dbgr: "any" | DebuggerKind | null = null;
  let compare = false;
  let noBuild = false;
  let open = true;
  let port = 8000;
  let appArgs: string[] = [];
  const names: string[] = [];
  for (let i = 0; i < argv.length; i++) {
    const raw = argv[i]!;
    if (raw === "--") {
      appArgs = argv.slice(i + 1);
      break;
    }
    if (takeBuildFlag(raw, flags)) {
      continue;
    }
    switch (raw) {
      case "-debugger":
        dbgr = "any";
        continue;
      case "-windbg":
        dbgr = "windbg";
        continue;
      case "-cdb":
        dbgr = "cdb";
        continue;
      case "-gdb":
        dbgr = "gdb";
        continue;
      case "-lldb":
        dbgr = "lldb";
        continue;
      case "-compare":
        compare = true;
        continue;
      case "-versions":
        // Prints and exits, so it is handled before anything asks for a
        // target: `bun cmd/run.ts -versions` is a question, not a run.
        printVersions();
      case "-no-build":
        noBuild = true;
        continue;
      case "-no-open":
        open = false;
        continue;
      case "-port":
        port = Number(argv[++i]);
        if (!Number.isFinite(port) || port <= 0) {
          die("-port wants a number");
        }
        continue;
      case "-all":
        die(`${self} launches one binary; -all is a build-only flag.`);
    }
    if (raw.startsWith("-")) {
      die(`Unknown flag: ${raw}`);
    }
    names.push(raw.toLowerCase());
  }

  const plat = platformFor(flags, die);
  checkBuildFlags(flags, plat, die);
  if (names.length === 0) {
    printExamples(plat);
  }
  if (names.length !== 1) {
    die("Pass one example name");
  }
  const target = names[0]!;
  if (!isKnownTarget(target, plat)) {
    printExamples(plat, `Unknown example: ${target}`);
  }
  if (plat === "wasm") {
    if (dbgr) {
      die("A wasm build runs in a browser; debug it with the browser's own devtools.");
    }
    if (compare) {
      die("-compare launches two desktop apps side by side, which the wasm target has no way to do.");
    }
  }
  // A port-only example has nothing to put on the other half of the screen.
  // Here rather than in buildRustTwin, for the reason below: this is a thing
  // you asked for that cannot happen, and finding that out should not cost a
  // compile of our side first.
  if (compare && !rustTwin(target)) {
    die(
      `-compare has nothing to compare ${target} against: it is a port-only ` +
        "example, with no counterpart in gpui-kit at the pinned SHA.\n\n" +
        "Drop -compare to launch just this one.",
    );
  }
  if (compare && appArgs.length > 0) {
    die("Arguments after -- cannot be combined with -compare.");
  }
  if (plat === "wasm" && appArgs.length > 0) {
    die("Arguments after -- are only available to native binaries.");
  }
  // Before the build, not after it: naming the wrong platform's debugger is a
  // typo, and a typo should not cost a compile first.
  if (dbgr && dbgr !== "any") {
    const isWinDbgr = dbgr === "windbg" || dbgr === "cdb";
    if (isWinDbgr && plat !== "win") {
      die(`-${dbgr} is a Windows debugger; this is ${process.platform}. Use -gdb or -lldb.`);
    }
    if (!isWinDbgr && plat === "win") {
      die(`-${dbgr} is not a Windows debugger. Use -windbg or -cdb.`);
    }
  }
  return { target, flags, plat, debugger: dbgr, compare, noBuild, open, port, appArgs };
}

// ─── small helpers ────────────────────────────────────────────────────────

function decode(buf: Uint8Array | undefined): string {
  return buf ? new TextDecoder().decode(buf) : "";
}

function whichExe(name: string): string | null {
  const finder = process.platform === "win32" ? "where" : "which";
  const r = Bun.spawnSync([finder, name], { stdout: "pipe", stderr: "pipe" });
  if ((r.exitCode ?? 1) !== 0) {
    return null;
  }
  const first = decode(r.stdout)
    .split(/\r?\n/)
    .map((l) => l.trim())
    .find((l) => l.length > 0);
  return first && first.length > 0 ? first : null;
}

// A Store/WinGet execution alias is a 0-byte reparse point. Bun's spawn stats
// the path and fails with ENOENT, so only a real file can be spawned directly.
function isSpawnableExe(p: string): boolean {
  try {
    const st = statSync(p);
    return st.isFile() && st.size > 0;
  } catch {
    return false;
  }
}

function pathLooksPresent(p: string): boolean {
  try {
    lstatSync(p);
    return true;
  } catch {
    return false;
  }
}

function run(cmd: string[], cwd: string): number {
  const r = Bun.spawnSync(cmd, { cwd, stdout: "inherit", stderr: "inherit" });
  return r.exitCode ?? 1;
}

/** Outlives this script and the shell that started it. */
function launchDetached(cmd: string[], cwd: string, env?: Record<string, string>): ReturnType<typeof Bun.spawn> {
  // Linux has setsid for the new session; Bun's own detach is enough on
  // Windows and macOS, neither of which ships setsid.
  const argv = process.platform === "linux" ? ["setsid", ...cmd] : cmd;
  const proc = Bun.spawn(argv, {
    cwd,
    env,
    stdin: "ignore",
    stdout: "ignore",
    stderr: "ignore",
    detached: true,
  });
  proc.unref();
  return proc;
}

/** Repo-relative if it lives here (the rust tree is under .work/), absolute otherwise. */
function repoPath(p: string): string {
  const rel = relative(root, p);
  if (rel.startsWith("..")) {
    return p;
  }
  return process.platform === "win32" ? rel.replaceAll("/", "\\") : rel;
}

// ─── debuggers ────────────────────────────────────────────────────────────

type DebugLaunch = {
  kind: DebuggerKind;
  /** The full argv that starts the target under the debugger. */
  cmd: string[];
  /** A console debugger takes over this terminal; a GUI one is detached. */
  foreground: boolean;
};

// --- Windows: windbg ------------------------------------------------------

function appxWindbgDir(): string | null {
  const r = Bun.spawnSync(
    [
      "powershell.exe",
      "-NoProfile",
      "-NonInteractive",
      "-Command",
      "(Get-AppxPackage -Name Microsoft.WinDbg | Select-Object -First 1).InstallLocation",
    ],
    { stdout: "pipe", stderr: "pipe" },
  );
  if ((r.exitCode ?? 1) !== 0) {
    return null;
  }
  const dir = decode(r.stdout).trim();
  return dir.length > 0 ? dir : null;
}

function windbgInDir(dir: string): string | null {
  for (const name of ["DbgX.Shell.exe", "WinDbgX.exe", "windbgx.exe"]) {
    const p = join(dir, name);
    if (isSpawnableExe(p)) {
      return p;
    }
  }
  return null;
}

function findUnderWindowsApps(): string | null {
  const apps = join(process.env["ProgramFiles"] ?? "C:\\Program Files", "WindowsApps");
  if (!existsSync(apps)) {
    return null;
  }
  let ents: string[];
  try {
    ents = readdirSync(apps);
  } catch {
    return null;
  }
  for (const ent of ents) {
    if (!ent.toLowerCase().startsWith("microsoft.windbg")) {
      continue;
    }
    const found = windbgInDir(join(apps, ent));
    if (found) {
      return found;
    }
  }
  return null;
}

function findWindbg(): string | null {
  const pkg = appxWindbgDir();
  if (pkg) {
    const found = windbgInDir(pkg);
    if (found) {
      return found;
    }
  }
  const under = findUnderWindowsApps();
  if (under) {
    return under;
  }
  const fromPath = whichExe("WinDbgX") ?? whichExe("windbgx") ?? whichExe("WinDbgX.exe") ?? whichExe("windbgx.exe");
  if (fromPath && isSpawnableExe(fromPath)) {
    return fromPath;
  }
  const local = process.env["LOCALAPPDATA"] ?? "";
  const candidates = [
    "C:\\Debugger\\windbgx.exe",
    join(local, "Microsoft", "WindowsApps", "WinDbgX.exe"),
    join(local, "Microsoft", "WindowsApps", "windbgx.exe"),
  ];
  for (const p of candidates) {
    if (p && isSpawnableExe(p)) {
      return p;
    }
  }
  // Alias path: not spawnable by Bun, but cmd.exe can resolve it.
  if (fromPath && pathLooksPresent(fromPath)) {
    return fromPath;
  }
  for (const p of candidates) {
    if (p && pathLooksPresent(p)) {
      return p;
    }
  }
  return null;
}

// --- Windows: cdb ---------------------------------------------------------

function findCdb(): string | null {
  const onPath = whichExe("cdb.exe");
  if (onPath && isSpawnableExe(onPath)) {
    return onPath;
  }
  // Debugging Tools for Windows, installed with the Windows SDK.
  for (const base of [
    process.env["ProgramFiles(x86)"] ?? "C:\\Program Files (x86)",
    process.env["ProgramFiles"] ?? "C:\\Program Files",
  ]) {
    for (const kit of ["10", "8.1"]) {
      const p = join(base, "Windows Kits", kit, "Debuggers", "x64", "cdb.exe");
      if (isSpawnableExe(p)) {
        return p;
      }
    }
  }
  return null;
}

const debuggerHelp: Record<DebuggerKind, string> = {
  windbg: "Install WinDbg:\n  winget install Microsoft.WinDbg",
  cdb:
    "cdb ships with the Debugging Tools for Windows:\n" +
    "  winget install Microsoft.WinDbg          (the modern UI, and cdb with it)\n" +
    "  or add Debugging Tools for Windows from the Windows SDK installer",
  gdb:
    process.platform === "darwin"
      ? "Install gdb:\n  brew install gdb\n" +
        "It also has to be code-signed to control another process; lldb needs none of that,\n" +
        "so prefer -lldb on macOS."
      : "Install gdb:\n  sudo apt install gdb\n  (or: bash cmd/ubuntu-install-deps.sh)",
  lldb:
    process.platform === "darwin"
      ? "lldb ships with the Xcode command line tools:\n  xcode-select --install"
      : "Install lldb:\n  sudo apt install lldb",
};

/**
 * The debugger to use. `want` is "any" for -debugger, or the one a forcing
 * flag named — and a named debugger that is not installed is an error with
 * the command that installs it, never a silent fallback to another one.
 */
function findDebugger(want: "any" | DebuggerKind, plat: Platform, exe: string, asan: boolean): DebugLaunch {
  const order: DebuggerKind[] =
    want !== "any" ? [want] : plat === "win" ? ["windbg", "cdb"] : plat === "mac" ? ["lldb", "gdb"] : ["gdb", "lldb"];

  for (const kind of order) {
    if (kind === "windbg") {
      const dbg = findWindbg();
      if (!dbg) {
        continue;
      }
      // -c at the initial break: `sxd eh` makes C++ EH second-chance only
      // (Windows, COM and DWrite throw and catch e06d7363 constantly). ASan
      // also raises and handles access violations while its debugger support
      // initializes, so those must be second-chance-only in sanitizer runs.
      // Then `g` runs. -G still ignores the process-exit breakpoint. Do not
      // use -g here: it skips the initial break, and -c would never run.
      const init = asan ? "sxd eh; sxd av; g" : "sxd eh; g";
      const cmd = isSpawnableExe(dbg)
        ? [dbg, "-c", init, "-G", exe]
        : ["cmd.exe", "/c", `"${dbg}" -c "${init}" -G "${exe}"`];
      return { kind, cmd, foreground: false };
    }
    if (kind === "cdb") {
      const dbg = findCdb();
      if (!dbg) {
        continue;
      }
      // Console debugger: it owns this terminal, so it runs in the
      // foreground. Same second-chance-only rules as WinDbg.
      const init = asan ? "sxd eh; sxd av; g" : "sxd eh; g";
      return { kind, cmd: [dbg, "-c", init, "-G", exe], foreground: true };
    }
    if (kind === "gdb") {
      if (!whichExe("gdb")) {
        continue;
      }
      return { kind, cmd: ["gdb", "-q", "-ex", "run", "--args", exe], foreground: true };
    }
    if (!whichExe("lldb")) {
      continue;
    }
    return { kind, cmd: ["lldb", "-o", "run", "--", exe], foreground: true };
  }

  if (want !== "any") {
    die(`${want} is not installed.\n\n${debuggerHelp[want]}`);
  }
  const tried = order.join(", ");
  die(`No debugger found (looked for ${tried}).\n\n${debuggerHelp[order[0]!]}`);
  // Unreachable; keeps the checker happy about `cwd` being used.
  void cwd;
}

// ─── upstream pins ────────────────────────────────────────────────────────

// Exact upstream checkins this C++ port is matching. Source of truth —
// AGENTS.md / port-*.md point here. Bump the SHAs (then re-run
// `bun run.ts -versions`) when ingesting a newer gpui-kit. Do not
// read HEAD of a random clone.
//
// These live in run.ts rather than in a module of their own because -compare
// is the only thing that acts on them, and run.ts is one of the two scripts
// gpui-kit-cpp-dist carries: a snapshot has to be able to fetch and cargo-build
// the Rust twin without the rest of cmd/ coming along for the ride.

/** Spec we port: crates/base, crates/component, crates/story, crates/webview, crates/shell, crates/component-shell, examples. */
export const gpuiComponent = {
  repo: "https://github.com/longbridge/gpui-kit",
  sha: "b0a1836b1e2e3053b8998517f1816ab47e4474ae",
  date: "2026-09-04",
  subject: "markdown: reflow soft line breaks instead of rendering them as hard breaks (#2947)",
  crates: {
    "gpui-kit": "0.6.0",
    "gpui-base": "0.6.0",
    "gpui-component": "0.6.0",
    "gpui-component-story": "0.6.0",
    "gpui-wry": "0.6.0",
    "gpui-shell": "0.6.0",
    "gpui-component-shell": "0.6.0",
  },
  dir: ".work/gpui-component",
} as const;

/**
 * Zed reference snapshot recorded in gpui-pre 0.3.2's package metadata.
 * Cargo.lock now resolves registry packages rather than a Zed git source.
 */
export const zedGpui = {
  repo: "https://github.com/zed-industries/zed",
  sha: "801c087af22dd189dc1aa49e2f370b4f04190b19",
  date: "2026-09-03",
  subject: "git: Separate revisions from paths in git commands (#63666)",
  crates: {
    "gpui-pre": "0.3.2",
    "gpui-pre-platform": "0.3.2",
    "gpui-pre-macros": "0.3.2",
  },
  lock: "registry+https://github.com/rust-lang/crates.io-index#gpui-pre@0.3.2",
} as const;

/**
 * The layout crate that gpui-kit's pinned Cargo.lock resolves for
 * `gpui`. We port it: `src/taffy/` is a C++ port of exactly this version, and
 * `src/gpui` lays out through it. See
 * `src/taffy/readme.md`.
 */
export const taffy = {
  repo: "https://github.com/DioxusLabs/taffy",
  version: "0.13.0",
  crateSha256: "c034e05f6ee85a12daa63863c2245797715075c70649947aa0da54f3f2ab1d0f",
  dir: "src/taffy",
} as const;

/**
 * The CommonMark + GFM parser gpui-kit's `crates/component/Cargo.toml` asks
 * for (`markdown = { version = "1.0.0", features = ["serde"] }`). We port it:
 * `src/markdown/` is a C++ port of exactly this version, and
 * `component::TextView` parses through it. See `src/markdown/readme.md`.
 */
export const markdown = {
  repo: "https://github.com/wooorm/markdown-rs",
  version: "1.0.0",
  crateSha256: "a5cab8f2cadc416a82d2e783a1946388b31654d391d1c7d92cc1f03e295b1deb",
  dir: "src/markdown",
} as const;

/**
 * The HTML parser `gpui-base` asks for (`html5ever = "0.27"`). We port the
 * crate's tokenizer and tree-builder surface under src/html5ever; the smaller
 * interchangeable parser under src/html5ever-mini is port-specific.
 */
export const html5ever = {
  repo: "https://github.com/servo/html5ever",
  version: "0.27.0",
  crateSha256: "c13771afe0e6e846f1e67d038d4cb29998a6779f93c809212e4e9c32efd244d4",
  dir: "src/html5ever",
} as const;

/**
 * The webview crate `crates/webview` (the `gpui-wry` crate) is built on:
 * `wry = { version = "0.53.3", package = "lb-wry" }`, longbridge's fork. We
 * port it: `src/wry/` is a C++ port of exactly this version, and
 * `src/webview/` is the gpui-side view `crates/webview` is. See
 * `src/wry/readme.md` for what is ported and what is not.
 */
export const wry = {
  repo: "https://github.com/tauri-apps/wry",
  crate: "lb-wry",
  version: "0.53.3",
  crateSha256: "d9cfe72bff8acf9af0d6d276569be5b9cb3f313f9882761ada5a50d3044214d4",
  dir: "src/wry",
} as const;

/**
 * The CJK copywriting linter/formatter `crates/story`'s editor example lints
 * every open document with (`autocorrect = "2.14.2"` in
 * `crates/story/Cargo.toml`). We port it: `src/autocorrect/` is a C++ port of
 * exactly this version, and `examples/editor.cpp` lints and walks its file
 * tree through it. See `src/autocorrect/readme.md`.
 */
export const autocorrect = {
  repo: "https://github.com/huacnlee/autocorrect",
  version: "2.14.2",
  crateSha256: "3199cc73b9b6af61f5034dcdb28c21e6050ffdc1229a20abfc71c244083ab9dc",
  dir: "src/autocorrect",
} as const;

/** JavaScript engine used by the C++ port of crates/shell. */
export const quickjsNg = {
  repo: "https://github.com/quickjs-ng/quickjs",
  sha: "5cbbc675f13067ae2113b2ccacbdd05db2595496",
  date: "2026-08-26",
  subject: "docs: add Vayu to projects",
  version: "v0.16.2-9-g5cbbc67",
  checkout: ".work/quickjs-ng",
  dir: "src/quickjs",
} as const;

export function rustTreeDir(repoRoot: string): string {
  return join(repoRoot, gpuiComponent.dir);
}

function gitOut(args: string[], cwd: string): { ok: boolean; out: string; err: string } {
  const r = Bun.spawnSync(["git", ...args], { cwd, stdout: "pipe", stderr: "pipe" });
  return { ok: (r.exitCode ?? 1) === 0, out: decode(r.stdout).trim(), err: decode(r.stderr).trim() };
}

function gitRun(args: string[], cwd: string): number {
  return run(["git", ...args], cwd);
}

function headSha(dir: string): string | null {
  const r = gitOut(["rev-parse", "HEAD"], dir);
  return r.ok && r.out ? r.out : null;
}

function hasGit(dir: string): boolean {
  return existsSync(join(dir, ".git"));
}

function checkoutPin(dir: string, sha: string): void {
  if (headSha(dir) === sha) {
    return;
  }
  const have = gitOut(["cat-file", "-t", sha], dir);
  if (!have.ok) {
    console.log(`Fetching gpui-kit ${sha.slice(0, 12)}`);
    if (gitRun(["fetch", "--depth", "1", "origin", sha], dir) !== 0) {
      if (gitRun(["fetch", "origin", sha], dir) !== 0) {
        throw new Error(`git fetch ${sha} failed in ${dir}`);
      }
    }
  }
  if (gitRun(["checkout", "--detach", "--force", sha], dir) !== 0) {
    throw new Error(`git checkout ${sha} failed in ${dir}`);
  }
  if (headSha(dir) !== sha) {
    throw new Error(`HEAD in ${dir} is ${headSha(dir)}, wanted ${sha}`);
  }
}

/**
 * Clone or reset `.work/gpui-component` to `gpuiComponent.sha`.
 *
 * The tree is a reading reference, not a build input, so a compile-only run
 * (CI) can skip the clone with GPUI_NO_RUST_TREE=1.
 */
export function ensureRustTree(repoRoot: string): string {
  const dir = rustTreeDir(repoRoot);
  const sha = gpuiComponent.sha;
  if (process.env["GPUI_NO_RUST_TREE"]) {
    return dir;
  }
  if (hasGit(dir)) {
    if (headSha(dir) === sha) {
      return dir;
    }
    console.log(`Updating ${gpuiComponent.dir} to ${sha.slice(0, 12)}`);
    checkoutPin(dir, sha);
    return dir;
  }
  if (existsSync(dir)) {
    rmSync(dir, { recursive: true, force: true });
  }
  mkdirSync(dir, { recursive: true });
  console.log(`Cloning ${gpuiComponent.repo} @ ${sha.slice(0, 12)} -> ${gpuiComponent.dir}`);
  if (gitRun(["init"], dir) !== 0) {
    throw new Error(`git init failed in ${dir}`);
  }
  if (gitRun(["remote", "add", "origin", gpuiComponent.repo], dir) !== 0) {
    throw new Error(`git remote add failed in ${dir}`);
  }
  if (gitRun(["fetch", "--depth", "1", "origin", sha], dir) !== 0) {
    throw new Error(`git fetch ${sha} failed (is git on PATH and the network up?)`);
  }
  if (gitRun(["checkout", "--detach", "FETCH_HEAD"], dir) !== 0) {
    throw new Error(`git checkout FETCH_HEAD failed in ${dir}`);
  }
  if (headSha(dir) !== sha) {
    throw new Error(`HEAD in ${dir} is ${headSha(dir)}, wanted ${sha}`);
  }
  return dir;
}

/** `-versions`: what the port is matching, and the spec tree at that SHA. */
function printVersions(): never {
  console.log("gpui-kit      ", gpuiComponent.sha, gpuiComponent.date);
  console.log("  ", gpuiComponent.subject);
  console.log("  crates", gpuiComponent.crates);
  console.log("zed gpui     ", zedGpui.sha, zedGpui.date, "(reference only)");
  console.log("crates ported", `taffy ${taffy.version} -> ${taffy.dir}`);
  console.log("             ", `markdown ${markdown.version} -> ${markdown.dir}`);
  console.log("             ", `html5ever ${html5ever.version} -> ${html5ever.dir}`);
  console.log("             ", `${wry.crate} ${wry.version} -> ${wry.dir}`);
  console.log("             ", `autocorrect ${autocorrect.version} -> ${autocorrect.dir}`);
  console.log("engine       ", `quickjs-ng ${quickjsNg.version} ${quickjsNg.sha} -> ${quickjsNg.dir}`);
  try {
    const dir = ensureRustTree(root);
    console.log("tree", dir, headSha(dir));
  } catch (e) {
    console.error(e instanceof Error ? e.message : e);
    process.exit(1);
  }
  process.exit(0);
}

// ─── the Rust side of -compare ────────────────────────────────────────────

function findCargo(): string | null {
  const fromPath = whichExe(process.platform === "win32" ? "cargo.exe" : "cargo");
  if (fromPath && (process.platform !== "win32" || isSpawnableExe(fromPath))) {
    return fromPath;
  }
  const home = process.env["USERPROFILE"] ?? process.env["HOME"] ?? "";
  const local = join(home, ".cargo", "bin", process.platform === "win32" ? "cargo.exe" : "cargo");
  if (existsSync(local)) {
    return local;
  }
  return fromPath;
}

/**
 * Where a port target's twin lives in the Rust tree at the pinned SHA. Three
 * shapes, and one target with no twin at all:
 *
 *  - a workspace member under `examples/`, where the package name, the binary
 *    it builds and our name for it are all the same string;
 *  - an example of a crate: the eight under `crates/story/examples/`, and the
 *    showcase, which is `crates/base/examples/components.rs`;
 *  - `crates/story` itself, which is a binary rather than an example.
 *
 * This replaces a plain `-p <target>`, which was right for the first shape and
 * wrong for the other nine: `cargo build -p editor` asks for a package that
 * does not exist, and said so.
 */
type RustTwin = { pkg: string; example?: string };

/** examples/<name>/ in the Rust tree; package, binary and our name agree. */
const rustExamplePkgs = new Set([
  "app_assets",
  "dialog_overlay",
  "focus_trap",
  "fps_monitor",
  "hello_world",
  "input",
  "markdown_table",
  "root_borderless",
  "sidebar",
  "system_monitor",
  "table_in_scrollable",
  "text_selection",
  "text_max_lines",
  "tooltip_top_edge",
  "webview",
  "window_title",
]);

/**
 * crates/story/examples/<file>.rs. Only one file is spelled differently from
 * the port's name for it, and cargo keeps that spelling in the binary it
 * writes -- dashes become underscores in a *library* crate name and nowhere
 * else.
 */
const rustStoryExamples: Record<string, string> = {
  brush: "brush",
  dock: "dock",
  editor: "editor",
  html: "html",
  large_text: "large-text",
  markdown: "markdown",
  stream_markdown: "stream_markdown",
  tiles: "tiles",
};

/** null when the port wrote this example and gpui-kit has no such thing. */
function rustTwin(target: string): RustTwin | null {
  if (target === "showcase") {
    return { pkg: "gpui-base", example: "components" };
  }
  if (target === "story") {
    return { pkg: "gpui-component-story" };
  }
  if (rustExamplePkgs.has(target)) {
    return { pkg: target };
  }
  const example = rustStoryExamples[target];
  return example ? { pkg: "gpui-component-story", example } : null;
}

function rustBuildArgs(twin: RustTwin, debug: boolean): string[] {
  const prof = debug ? [] : ["--release"];
  const example = twin.example ? ["--example", twin.example] : [];
  return ["build", ...prof, "-p", twin.pkg, ...example];
}

function rustExePath(twin: RustTwin, debug: boolean): string {
  const prof = debug ? "debug" : "release";
  const dir = rustTreeDir(root);
  const ext = process.platform === "win32" ? ".exe" : "";
  if (twin.example) {
    return join(dir, "target", prof, "examples", `${twin.example}${ext}`);
  }
  return join(dir, "target", prof, `${twin.pkg}${ext}`);
}

/** cargo-build the Rust twin and return its binary, or exit saying why not. */
function buildRustTwin(target: string, debug: boolean): string {
  const twin = rustTwin(target);
  if (!twin) {
    // parseArgs has already refused this, so reaching here is a bug rather
    // than a typo; say which, so it is not mistaken for the other.
    die(`internal: no rust twin for ${target}`);
  }
  let rustRoot: string;
  try {
    // The only thing in this tree that clones .work/gpui-component. A plain
    // build never needs it.
    rustRoot = ensureRustTree(root);
  } catch (e) {
    die(e instanceof Error ? e.message : String(e));
  }
  const cargo = findCargo();
  if (!cargo) {
    die(
      "Could not find cargo, so -compare has nothing to build the Rust side with.\n\n" +
        "Install Rust (https://rustup.rs), or drop -compare to launch only the C++ port.",
    );
  }
  const args = rustBuildArgs(twin, debug);
  // Two things the echoed line cannot say for itself: it runs in the spec
  // tree rather than in this repo, and its `cargo` is whichever one was
  // found -- off PATH, or the one in ~/.cargo/bin that is not always on it.
  console.log(`Building rust, from ${rustRoot}`);
  console.log(`Using ${cargo}`);
  printCmd([cargo, ...args]);
  const rc = run([cargo, ...args], rustRoot);
  if (rc !== 0) {
    process.exit(rc);
  }
  const exe = rustExePath(twin, debug);
  if (!existsSync(exe)) {
    die(`Missing rust binary after cargo build: ${exe}`);
  }
  return exe;
}

// macOS: neither GPUI implementation exposes its NSWindow through
// Accessibility, so a tiny Cocoa shim is injected into each locally-built
// process to place its own first window when that window becomes visible.
function ensureMacWindowPlacer(): string {
  // Beside this script: cmd/ here, the top level in gpui-kit-cpp-dist.
  const source = join(scriptDir, "mac-window-place.m");
  const outputDir = join(root, ".work/mac-window-place");
  const output = join(outputDir, "mac-window-place.dylib");
  if (existsSync(output) && statSync(source).mtimeMs <= statSync(output).mtimeMs) {
    return output;
  }
  mkdirSync(outputDir, { recursive: true });
  console.log("Building macOS compare window placer");
  const cmd = ["xcrun", "clang", "-fobjc-arc", "-dynamiclib", "-framework", "Cocoa", "-o", output, source];
  printCmd(cmd);
  const rc = run(cmd, root);
  if (rc !== 0 || !existsSync(output)) {
    die("Could not build the macOS compare window placer. Install the command line tools: xcode-select --install");
  }
  return output;
}

function macPlacerEnv(placer: string, half: "left" | "right"): Record<string, string> {
  const env: Record<string, string> = {};
  for (const [name, value] of Object.entries(process.env)) {
    if (value !== undefined) {
      env[name] = value;
    }
  }
  const old = env["DYLD_INSERT_LIBRARIES"];
  env["DYLD_INSERT_LIBRARIES"] = old ? `${placer}:${old}` : placer;
  env["GPUI_COMPARE_WINDOW_HALF"] = half;
  return env;
}

// ─── native run ───────────────────────────────────────────────────────────

// All gpui apps share this WNDCLASS (src/gpui/window_common.cpp). WinDbg's
// own UI does not, which is how the launched app is told apart from it.
const cppWndClass = "GpuiSystemMonitor";

async function runNative(a: RunArgs): Promise<never> {
  if (!a.noBuild) {
    await build({ names: [a.target], plat: a.plat, flags: a.flags, fail: die, quiet: true });
  }
  const exe = outFilePath(a.plat, a.flags, a.target);
  if (!existsSync(exe)) {
    die(a.noBuild ? `Missing ${repoPath(exe)}. Drop -no-build to compile it.` : `Missing ${repoPath(exe)} after build`);
  }
  // Both sides run from the repo root rather than from wherever each binary
  // landed -- the C++ one in out/, the rust one under .work/ -- so a relative
  // path means the same file to both, and a comparison run is comparing the
  // programs and not their working directories.
  const cwd = root;

  if (
    a.plat === "linux" &&
    !consoleTargets.has(a.target) &&
    !process.env["DISPLAY"] &&
    !process.env["WAYLAND_DISPLAY"]
  ) {
    console.error("DISPLAY is not set: there is no X server to open a window on.");
    console.error("Under WSL, make sure WSLg is available (wsl --update).");
    process.exit(1);
  }

  const rustExe = a.compare ? buildRustTwin(a.target, a.flags.debug) : null;
  if (rustExe) {
    // Both binaries exist now; how big each came out is the first thing a
    // comparison run wants to know.
    console.log("");
    printSizeTable([
      { label: repoPath(exe), path: exe },
      { label: repoPath(rustExe), path: rustExe },
    ]);
    console.log("");
  }

  const dbg = a.debugger ? findDebugger(a.debugger, a.plat, exe, a.flags.asan) : null;
  const cppCmd = [...(dbg ? dbg.cmd : [exe]), ...a.appArgs];

  if (dbg?.foreground) {
    // The debugger owns this terminal, so nothing can be placed beside it.
    if (rustExe) {
      console.log(`Launching rust ${formatCmd([rustExe])}`);
      launchDetached([rustExe], cwd);
    }
    console.log(`Launching ${dbg.kind} ${formatCmd([exe])}`);
    process.exit(run(cppCmd, cwd));
  }

  if (consoleTargets.has(a.target)) {
    console.log(`Running ${formatCmd(cppCmd)}`);
    process.exit(run(cppCmd, cwd));
  }

  console.log(`Launching ${formatCmd(cppCmd)}`);
  if (!rustExe) {
    launchDetached(cppCmd, cwd);
    process.exit(0);
  }

  if (a.plat === "win") {
    await placeWindowsPair(cppCmd, cwd, rustExe, dbg !== null);
    process.exit(0);
  }
  if (a.plat === "mac") {
    const placer = ensureMacWindowPlacer();
    console.log(`Launching rust ${formatCmd([rustExe])} (left)`);
    launchDetached([rustExe], cwd, macPlacerEnv(placer, "left"));
    launchDetached(cppCmd, cwd, macPlacerEnv(placer, "right"));
    process.exit(0);
  }
  // Linux has no window placement here; the window manager decides.
  launchDetached(cppCmd, cwd);
  console.log(`Launching rust ${formatCmd([rustExe])}`);
  launchDetached([rustExe], cwd);
  process.exit(0);
}

// Windows only, and imported only here: cmd/winapi.ts dlopens user32 at
// import time, which no other platform can do.
async function placeWindowsPair(cppCmd: string[], cwd: string, rustExe: string, underDebugger: boolean): Promise<void> {
  const { findVisibleClassWindows, placeOnWorkAreaHalf, setProcessDpiAware, waitForNewClassWindow, waitForPidWindow } =
    await import("./winapi.ts");

  async function place(hwnd: number, side: "left" | "right", label: string): Promise<void> {
    if (!hwnd) {
      console.error(`${label} window did not appear`);
      return;
    }
    if (!placeOnWorkAreaHalf(hwnd, side)) {
      console.error(`Failed to place ${label} window on the ${side} half`);
    }
  }

  setProcessDpiAware();
  const existingCpp = new Set(findVisibleClassWindows(cppWndClass));
  console.log(`Launching rust ${formatCmd([rustExe])}`);
  const rustProc = launchDetached([rustExe], cwd);
  console.log(`Launching c++ (will wait for ${cppWndClass})`);
  launchDetached(cppCmd, cwd);
  // A debugger stops at the initial break first, so its window takes longer.
  const cppWaitMs = underDebugger ? 120000 : 45000;
  await Promise.all([
    (async () => {
      const pid = rustProc.pid ?? 0;
      const hwnd = await waitForPidWindow(pid, 45000);
      if (!hwnd) {
        console.error(`rust window did not appear (pid ${pid})`);
        return;
      }
      await place(hwnd, "left", "rust");
    })(),
    (async () => {
      const hwnd = await waitForNewClassWindow(cppWndClass, existingCpp, cppWaitMs);
      if (!hwnd) {
        console.error(`c++ window did not appear (${cppWndClass}, waited ${cppWaitMs}ms)`);
        return;
      }
      await place(hwnd, "right", "c++");
    })(),
  ]);
}

// ─── wasm run ─────────────────────────────────────────────────────────────

const mime: Record<string, string> = {
  ".html": "text/html; charset=utf-8",
  ".js": "text/javascript; charset=utf-8",
  ".mjs": "text/javascript; charset=utf-8",
  ".wasm": "application/wasm",
  ".data": "application/octet-stream",
  ".json": "application/json",
  ".css": "text/css; charset=utf-8",
  ".svg": "image/svg+xml",
  ".png": "image/png",
  ".map": "application/json",
};

/**
 * A wasm module has to come off a server: a browser refuses to instantiate
 * one fetched from a file:// URL, so there is no double-clicking the .html.
 * This is that server and nothing more — no caching, no compression, no
 * directory listing.
 */
async function runWasm(a: RunArgs): Promise<never> {
  if (!a.noBuild) {
    await build({ names: [a.target], plat: "wasm", flags: a.flags, fail: die, quiet: true });
  }
  const dir = join(root, outDir("wasm", a.flags));

  // tests and bench print and exit; there is nothing to serve. emsdk ships
  // the node they were built against, so use that one when it is there.
  if (consoleTargets.has(a.target)) {
    const js = join(dir, `${a.target}.js`);
    if (!existsSync(js)) {
      die(`${repoPath(js)} not built`);
    }
    process.exit(run([emsdkNode(findEmcc()), js], dir));
  }

  const page = join(dir, `${a.target}.html`);
  if (!existsSync(page)) {
    die(`${repoPath(page)} not built`);
  }

  function serve(p: number) {
    return Bun.serve({
      port: p,
      fetch(req) {
        let path = new URL(req.url).pathname;
        if (path === "/" || path === "") {
          path = `/${a.target}.html`;
        }
        // Everything is served out of the one output directory: no traversal
        // out of it, and nothing else on the disk is reachable.
        const abs = join(dir, path.replace(/^\/+/, ""));
        if (!abs.startsWith(dir) || !existsSync(abs) || !statSync(abs).isFile()) {
          return new Response("not found", { status: 404 });
        }
        return new Response(Bun.file(abs), {
          headers: {
            "content-type": mime[extname(abs).toLowerCase()] ?? "application/octet-stream",
            // A rebuild while the tab is open should be one reload away.
            "cache-control": "no-store",
          },
        });
      },
    });
  }

  let server: ReturnType<typeof serve> | null = null;
  for (let p = a.port; p < a.port + 20 && !server; p++) {
    try {
      server = serve(p);
    } catch {
      // In use; try the next one.
    }
  }
  if (!server) {
    die(`No free port in ${a.port}..${a.port + 19}`);
  }

  const url = `http://localhost:${server.port}/`;
  console.log(`serving ${repoPath(dir)} at ${url}`);
  console.log("ctrl-c to stop");

  if (a.open) {
    const cmd =
      process.platform === "win32"
        ? ["cmd", "/c", "start", "", url]
        : process.platform === "darwin"
          ? ["open", url]
          : ["xdg-open", url];
    Bun.spawn(cmd, { stdout: "ignore", stderr: "ignore" });
  }
  // Bun.serve keeps the process alive; this never returns.
  return undefined as never;
}

// ─── main ─────────────────────────────────────────────────────────────────

// Guarded: cmd/compare-*.ts import this module for the upstream pins and
// ensureRustTree, and an import must not parse *their* command line.
if (import.meta.main) {
  const args = parseArgs(Bun.argv.slice(2));
  (args.plat === "wasm" ? runWasm(args) : runNative(args)).catch((err) => {
    console.error(err);
    process.exit(1);
  });
}
