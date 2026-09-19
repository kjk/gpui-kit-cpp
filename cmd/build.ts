// Build a gpui example with the toolchain this machine has: MSVC (or
// clang-cl, with -clang) on Windows, g++/clang++ on Linux, clang++ on macOS,
// or emscripten with -wasm, which builds a page from any of the three.
//
//   bun cmd/build.ts                         # print example list
//   bun cmd/build.ts -rel system_monitor
//   bun cmd/build.ts -dbg -all
//   bun cmd/build.ts -rel -asan system_monitor
//   bun cmd/build.ts -rel -clean showcase
//   bun cmd/build.ts -clang -rel showcase    # Windows: clang-cl, not cl.exe
//   bun cmd/build.ts -wasm hello_world       # emscripten, on any host
//
// -wasm is the one target that does not follow the host: emscripten runs
// everywhere, so it is asked for by name rather than picked by platform. To
// build another *native* platform's binaries from here, see cmd/wsl-run.ts
// (Linux) and cmd/mac-build.ts (macOS).
//
// On Windows cl.exe is taken off PATH when it is there and the MSVC
// environment is already set. Otherwise Visual Studio is located with vswhere
// (then by scanning the default install roots, 2026 first) and its
// vcvars64.bat is run once to read back the INCLUDE/LIB/PATH it exports.
//
// Nothing here downloads what a build does not need. The Rust spec tree under
// .work/ is a reading reference, so only `cmd/run.ts -compare` clones it, and
// emscripten is only looked for when the target is wasm.
//
// cmd/run.ts imports this module and builds through it rather than spawning
// it, so the two can never disagree about a flag, an output directory or a
// compiler command line.
//
// This script runs in two trees. Here it lives in cmd/ and the amalgam it
// compiles is regenerated from src/** before every build. In gpui-kit-cpp-dist it
// lives at the top level beside a checked-in gpui.h + gpui.cpp, which are the
// only sources that repo has, and there is no src/** and no update-dist.ts to
// regenerate from. Everything that differs between the two hangs off `isDist`
// below.

import { existsSync, mkdirSync, cpSync, rmSync, readdirSync, statSync, readFileSync, writeFileSync } from "node:fs";
import { basename, dirname, join, resolve } from "node:path";
import { tmpdir } from "node:os";

/** The directory holding build.ts, run.ts and the rest of their siblings. */
export const scriptDir = import.meta.dir;

/**
 * True in a gpui-kit-cpp-dist checkout, false in gpui-kit-cpp. gpui.h beside the
 * script is the whole test, and it is a good one: it is the file the dist
 * repo exists to carry, and this repo never has one there — its amalgam goes
 * to gitignored .work/ precisely so nothing but cmd/update-dist.ts writes a
 * published copy.
 */
export const isDist = existsSync(join(scriptDir, "gpui.h"));

export const root = isDist ? scriptDir : resolve(scriptDir, "..");
process.chdir(root);

// Shader bytecode is checked in so an ordinary build needs neither fxc nor
// D3DCompiler_47.dll. Refuse to silently amalgamate stale bytecode after the
// HLSL changes; the explicit generator is the only writer.
function ensureWinShadersCurrent(fail: (msg: string) => never): void {
  if (isDist) {
    return;
  }
  const sourcePath = join(root, "src/gpui/paintgpu_win.hlsl");
  const generatedPath = join(root, "src/gpui/paintgpu_shaders_win.cpp");
  if (!existsSync(sourcePath) || !existsSync(generatedPath)) {
    fail("missing Windows shader source or bytecode; run bun cmd/update-win-shaders.ts");
  }
  const hash = new Bun.CryptoHasher("sha256").update(readFileSync(sourcePath)).digest("hex");
  const generated = readFileSync(generatedPath, "utf8");
  // Allow whitespace between the marker and the hash so a comment wrap does
  // not look like a stale compile; the 64-hex digest is the actual check.
  const recorded = generated.match(/source-sha256:\s*([0-9a-f]{64})/);
  if (!recorded || recorded[1] !== hash) {
    fail("Windows shader bytecode is stale; run bun cmd/update-win-shaders.ts");
  }
}

/**
 * Repo-relative directory holding the gpui.h + gpui.cpp a build compiles:
 * the top level in gpui-kit-cpp-dist, gitignored .work/ here. GPUI_AMALGAM_DIR
 * overrides both — cmd/update-dist.ts sets it to the dist checkout so the
 * examples get built against the published copy as its correctness check.
 */
export function amalgamDir(): string {
  return process.env.GPUI_AMALGAM_DIR ?? (isDist ? "." : ".work");
}

/** Whether the amalgam is this repo's own regenerated one, rather than a published source set. */
export function amalgamIsWork(): boolean {
  return amalgamDir() === ".work";
}

/**
 * A file in the amalgam directory, repo-relative: `.work/gpui.cpp` here,
 * plain `gpui.cpp` in gpui-kit-cpp-dist, where the directory is the repo root.
 * Joining by hand would make that one `./gpui.cpp`, which compiles the same
 * and reads like a path that got away from somebody -- and these strings are
 * matched against each other as well as printed, so one spelling is the only
 * safe number of spellings.
 */
export function amalgamPath(name: string): string {
  const dir = amalgamDir();
  return dir === "." ? name : `${dir}/${name}`;
}

/**
 * How to spell one of these scripts on a command line from the repo root:
 * `cmd/run.ts` here, `run.ts` in gpui-kit-cpp-dist. Only usage text needs it, but
 * usage text that tells a reader to type a path that is not there is worse
 * than no usage text.
 */
export function scriptPath(name: string): string {
  return isDist ? name : `cmd/${name}`;
}

// ─── size reporting ───────────────────────────────────────────────────────

export type SizeEntry = {
  /** What to show in the first column. */
  label: string;
  /** Absolute path to stat. */
  path: string;
};

function formatExactBytes(n: number): string {
  return `${n.toLocaleString("en-US")} b`;
}

function formatHumanBytes(n: number): string {
  if (n >= 1_000_000_000) {
    return `${(n / 1_000_000_000).toFixed(1)} GB`;
  }
  if (n >= 1_000_000) {
    return `${(n / 1_000_000).toFixed(1)} MB`;
  }
  if (n >= 1_000) {
    return `${(n / 1_000).toFixed(1)} kB`;
  }
  return `${n} b`;
}

/**
 * One row per entry: label, exact bytes, human readable. What a build just
 * wrote, and — under `cmd/run.ts -compare` — the Rust binary beside ours.
 */
export function printSizeTable(entries: SizeEntry[]): void {
  if (entries.length === 0) {
    return;
  }
  const rows = entries.map((e) => {
    if (!existsSync(e.path)) {
      return { label: e.label, bytes: "(not generated)", human: "" };
    }
    const size = statSync(e.path).size;
    return { label: e.label, bytes: formatExactBytes(size), human: formatHumanBytes(size) };
  });
  const labelW = Math.max(...rows.map((r) => r.label.length));
  const bytesW = Math.max(...rows.map((r) => r.bytes.length));
  const humanW = Math.max(...rows.map((r) => r.human.length));
  for (const r of rows) {
    console.log(`${r.label.padEnd(labelW)}  ${r.bytes.padStart(bytesW)}  ${r.human.padStart(humanW)}`);
  }
}

// ─── platforms ────────────────────────────────────────────────────────────

export type Platform = "win" | "linux" | "mac" | "wasm";

/** The platform this machine builds natively, or null if we do not know it. */
export function hostPlatform(): Platform | null {
  if (process.platform === "win32") {
    return "win";
  }
  if (process.platform === "linux") {
    return "linux";
  }
  if (process.platform === "darwin") {
    return "mac";
  }
  return null;
}

// ─── targets ──────────────────────────────────────────────────────────────

/** One .cpp under examples/, built as one binary. */
export const simpleExamples = [
  "hello_world",
  "window_title",
  "root_borderless",
  "dialog_overlay",
  "focus_trap",
  "fps_monitor",
  "input",
  "sidebar",
  "tooltip_top_edge",
  "table_in_scrollable",
  "text_selection",
  "text_max_lines",
  "motion",
  "markdown_table",
  "rich_text",
  "stream_markdown",
  "markdown",
  "html",
  "large_text",
  "dock",
  "brush",
  "editor",
  "webview",
];

/** A directory of .cpp built as one binary. */
export const dirExamples = ["showcase", "story"];

/** Not examples: console programs, so -all leaves them out. */
export const consoleTargets = new Set(["tests", "bench", "gpui_shell"]);

// There is no webview on the web: wry has no browser back end, and a page
// inside a page is not what crates/webview is.
function skippedOn(plat: Platform): Set<string> {
  return plat === "wasm" ? new Set(["webview", "gpui_shell"]) : new Set<string>();
}

/** Every example this platform builds, in the order -all builds them. */
export function examplesFor(plat: Platform): string[] {
  const skip = skippedOn(plat);
  return ["system_monitor", "app_assets", ...dirExamples, ...simpleExamples].filter((n) => !skip.has(n));
}

/** Every name the build accepts: examples, repository runners and the shell tool. */
export function targetsFor(plat: Platform): string[] {
  const skip = skippedOn(plat);
  // tests/ and bench/ are this repo's own suites, run against src/**. The
  // snapshot carries the examples and not them, so there they are not targets.
  const runners = isDist ? ["gpui_shell"] : [...consoleTargets];
  return ["system_monitor", "app_assets", ...dirExamples, ...runners, ...simpleExamples].filter((n) => !skip.has(n));
}

export function isKnownTarget(name: string, plat: Platform): boolean {
  return targetsFor(plat).includes(name);
}

// ─── flags ────────────────────────────────────────────────────────────────

export type BuildFlags = {
  /** -rel / -dbg, tracked separately so passing both can be an error. */
  sawRel: boolean;
  sawDbg: boolean;
  debug: boolean;
  asan: boolean;
  /** Windows: clang-cl instead of cl.exe. Linux: clang++ instead of g++. */
  clang: boolean;
  /** Build for the browser instead of for this host. */
  wasm: boolean;
  clean: boolean;
  /** Compile the library's individual source files instead of gpui.cpp. */
  nonAmalgam: boolean;
  /** Markdown parser implementation compiled into the library. */
  markdown: ParserVariant;
  /** HTML parser implementation compiled into the library. */
  html: ParserVariant;
  /** Windows renderer implementations compiled into the executable. */
  winBackend: WindowsBackend;
  sawWinBackend: boolean;
};

export type WindowsBackend = "d2d" | "d3d11" | "d3d12" | "all";
export type ParserVariant = "full" | "mini";

export function defaultBuildFlags(): BuildFlags {
  return {
    sawRel: false,
    sawDbg: false,
    debug: false,
    asan: false,
    clang: false,
    wasm: false,
    clean: false,
    nonAmalgam: false,
    markdown: "full",
    html: "full",
    winBackend: "d2d",
    sawWinBackend: false,
  };
}

/**
 * Consume one build flag into `f`. Returns false if the argument is not a
 * build flag, which is how cmd/run.ts layers its own on top of these.
 */
export function takeBuildFlag(arg: string, f: BuildFlags): boolean {
  const markdown = arg.match(/^-markdown=(mini|full)$/i);
  if (markdown) {
    f.markdown = markdown[1]!.toLowerCase() as ParserVariant;
    return true;
  }
  const html = arg.match(/^-html=(mini|full)$/i);
  if (html) {
    f.html = html[1]!.toLowerCase() as ParserVariant;
    return true;
  }
  const winBackend = arg.match(/^--win-backend=(d2d|d3d11|d3d12|all)$/i);
  if (winBackend) {
    f.winBackend = winBackend[1]!.toLowerCase() as WindowsBackend;
    f.sawWinBackend = true;
    return true;
  }
  switch (arg) {
    case "-rel":
      f.sawRel = true;
      f.debug = false;
      return true;
    case "-dbg":
      f.sawDbg = true;
      f.debug = true;
      return true;
    case "-asan":
      f.asan = true;
      return true;
    case "-clang":
      f.clang = true;
      return true;
    case "-wasm":
      f.wasm = true;
      return true;
    case "-clean":
      f.clean = true;
      return true;
    default:
      return false;
  }
}

/** The platform these flags build for: the host, unless -wasm says otherwise. */
export function platformFor(f: BuildFlags, fail: (msg: string) => never): Platform {
  if (f.wasm) {
    return "wasm";
  }
  const host = hostPlatform();
  if (!host) {
    fail(`Unsupported platform: ${process.platform}. gpui builds on Windows, Linux and macOS, and -wasm anywhere.`);
  }
  return host;
}

/** Reject the flag combinations no toolchain can satisfy. */
export function checkBuildFlags(f: BuildFlags, plat: Platform, fail: (msg: string) => never): void {
  if (f.sawRel && f.sawDbg) {
    fail("Cannot combine -rel and -dbg");
  }
  if (f.asan && plat === "wasm") {
    fail("-asan is not supported for the wasm target. Build native to run under AddressSanitizer.");
  }
  if (f.clang && plat === "wasm") {
    fail("-clang means nothing with -wasm: emscripten is clang.");
  }
  if (f.sawWinBackend && plat !== "win") {
    fail("--win-backend is only available for native Windows builds.");
  }
}

// ─── output layout ────────────────────────────────────────────────────────

/**
 * out/<this>/ — the configuration's own tree. Linux, macOS and wasm nest
 * under a directory of their own so one checkout built for several platforms
 * never clobbers another's binaries, objects or -clean. A build against a
 * published amalgam gets its own tree for the same reason, and so does a
 * clang-cl build next to a cl.exe one.
 */
export function outDirName(plat: Platform, f: BuildFlags): string {
  let name = f.debug ? "dbg" : "rel";
  if (f.asan) {
    name += "_asan";
  }
  if (plat === "win" && f.clang) {
    name += "_clang";
  }
  if (f.nonAmalgam) {
    name += "_nonamalgam";
  }
  // Only in this repo, where a plain build is the .work/ source set and a
  // GPUI_AMALGAM_DIR build is a published one: two amalgams, two out/ trees.
  // In gpui-kit-cpp-dist every build is the published source set, so out/rel is out/rel.
  if (!isDist && !amalgamIsWork()) {
    name += "_dist";
  }
  if (plat === "win") {
    return name;
  }
  return join(plat === "mac" ? "mac" : plat === "linux" ? "linux" : "wasm", name);
}

/** Repo-relative: out/rel, out/linux/dbg, out/wasm/rel … */
export function outDir(plat: Platform, f: BuildFlags): string {
  return join("out", outDirName(plat, f));
}

/** What the build writes for `name`: story.exe, story, story.html, tests.js … */
export function outFileName(plat: Platform, name: string): string {
  if (plat === "win") {
    return `${name}.exe`;
  }
  if (plat === "wasm") {
    return name + (consoleTargets.has(name) ? ".js" : ".html");
  }
  return name;
}

/** Absolute path to what the build writes for `name`. */
export function outFilePath(plat: Platform, f: BuildFlags, name: string): string {
  return join(root, outDir(plat, f), outFileName(plat, name));
}

// ─── sources ──────────────────────────────────────────────────────────────

/** The C++ amalgam plus the separately compiled QuickJS-NG C amalgam. */
function amalgamSrc(): string[] {
  return [amalgamPath("gpui.cpp"), amalgamPath("quickjs/quickjs.c")];
}

/**
 * The autocorrect port is not part of gpui.cpp: only the editor example
 * lints through it (and the tests exercise it), so its own amalgamated pair
 * is compiled and linked into exactly those targets.
 */
const autocorrectTargets = new Set(["editor", "tests"]);

function extraSrcFor(name: string): string[] {
  return autocorrectTargets.has(name) ? [amalgamPath("extras/autocorrect/autocorrect.cpp")] : [];
}

function cppDir(rel: string): string[] {
  const dir = join(root, rel);
  if (!existsSync(dir)) {
    return [];
  }
  return readdirSync(dir)
    .filter((f) => f.endsWith(".cpp"))
    .map((f) => `${rel}/${f}`)
    .sort();
}

function sourcePlatform(rel: string, plat: Platform): boolean {
  if (/_win\.cpp$/.test(rel)) return plat === "win";
  if (/_linux\.cpp$/.test(rel)) return plat === "linux";
  if (/_mac\.cpp$/.test(rel)) return plat === "mac";
  if (/_wasm\.cpp$/.test(rel)) return plat === "wasm";
  if (/_mem_posix\.cpp$/.test(rel)) return plat === "linux" || plat === "mac";
  if (/_posix\.cpp$/.test(rel)) return plat === "linux" || plat === "mac" || plat === "wasm";
  return true;
}

function allCppDir(rel: string): string[] {
  const dir = join(root, rel);
  if (!existsSync(dir)) return [];
  const result: string[] = [];
  for (const ent of readdirSync(dir, { withFileTypes: true }).sort((a, b) => a.name.localeCompare(b.name))) {
    const child = `${rel}/${ent.name}`;
    if (ent.isDirectory()) result.push(...allCppDir(child));
    else if (ent.name.endsWith(".cpp")) result.push(child);
  }
  return result;
}

function sourcesFor(name: string, plat: Platform, f: BuildFlags): string[] | null {
  if (f.nonAmalgam) {
    // hello_world proves every src object compiles and links as a normal
    // app; editor is the one example that additionally uses src/autocorrect
    // (the standard build compiles the extras/autocorrect amalgam instead,
    // and editor.cpp switches its include on GPUI_AMALGAM).
    if (name !== "hello_world" && name !== "hello_world_no_amalgam" && name !== "editor") return null;
    const example = name === "hello_world_no_amalgam" ? "examples/hello_world_no_amalgam.cpp" : `examples/${name}.cpp`;
    return [
      example,
      ...allCppDir("src").filter((rel) => {
        if (!sourcePlatform(rel, plat)) return false;
        if (f.markdown === "full" && rel.startsWith("src/markdown-mini/")) return false;
        if (f.markdown === "mini" && rel.startsWith("src/markdown/") && rel !== "src/markdown/mdast.cpp") return false;
        if (f.html === "full" && rel.startsWith("src/html5ever-mini/")) return false;
        if (f.html === "mini" && rel.startsWith("src/html5ever/")) return false;
        return true;
      }),
      "src/quickjs/quickjs.c",
    ];
  }
  if (dirExamples.includes(name)) {
    return [...amalgamSrc(), ...cppDir(`examples/${name}`)];
  }
  if (consoleTargets.has(name)) {
    return [...amalgamSrc(), ...extraSrcFor(name), ...cppDir(name)];
  }
  if (name === "system_monitor" || name === "app_assets" || simpleExamples.includes(name)) {
    return [...amalgamSrc(), ...extraSrcFor(name), `examples/${name}.cpp`];
  }
  return null;
}

// src/ui/button.cpp and examples/showcase/button.cpp would both write
// button.obj, so each group gets its own object directory.
function objGroup(f: string): string {
  if (/quickjs[\\/]quickjs\.c$/.test(f)) {
    return "quickjs";
  }
  if (/autocorrect[\\/]autocorrect\.cpp$/.test(f)) {
    return "autocorrect";
  }
  if (f.startsWith("ext/")) {
    return "ext";
  }
  if (f.startsWith(amalgamPath("gpui")) || f.startsWith("src/gpui/")) {
    return "gpui";
  }
  if (f.startsWith("src/")) {
    return `src-${dirname(f).replaceAll("/", "-")}`;
  }
  if (f.startsWith("examples/showcase/")) {
    return "showcase";
  }
  if (f.startsWith("examples/story/")) {
    return "story";
  }
  if (f.startsWith("tests/")) {
    return "tests";
  }
  if (f.startsWith("bench/")) {
    return "bench";
  }
  if (f.startsWith("gpui_shell/")) {
    return "gpui_shell";
  }
  return "ex";
}

// ─── incremental compile ──────────────────────────────────────────────────

function mtimeMs(rel: string): number {
  try {
    return statSync(join(root, rel)).mtimeMs;
  } catch {
    return 0;
  }
}

const includeRe = /^\s*#\s*include\s+"([^"]+)"/gm;

function quotedIncludes(rel: string, memo: Map<string, string[]>): string[] {
  const hit = memo.get(rel);
  if (hit) {
    return hit;
  }
  memo.set(rel, []);
  const abs = join(root, rel);
  if (!existsSync(abs)) {
    return [];
  }
  const text = readFileSync(abs, "utf8");
  const dir = dirname(rel).replaceAll("\\", "/");
  const deps: string[] = [];
  includeRe.lastIndex = 0;
  let m: RegExpExecArray | null;
  while ((m = includeRe.exec(text))) {
    const inc = m[1]!.replaceAll("\\", "/");
    // <amalgam>/extras mirrors the -I the compile gets, so the editor's
    // "autocorrect/autocorrect.h" tracks the generated pair header (which
    // carries internal.h too), not just the src/ original.
    const candidates = [`${dir}/${inc}`, amalgamPath(inc), amalgamPath(`extras/${inc}`), `src/${inc}`];
    for (const raw of candidates) {
      const norm = raw.replace(/\/\.\//g, "/").replace(/^\.\//, "");
      if (!existsSync(join(root, norm))) {
        continue;
      }
      deps.push(norm);
      for (const d of quotedIncludes(norm, memo)) {
        deps.push(d);
      }
      break;
    }
  }
  const uniq = [...new Set(deps)];
  memo.set(rel, uniq);
  return uniq;
}

function needsCompile(src: string, obj: string, includes: string[]): boolean {
  const ot = mtimeMs(obj);
  if (ot === 0 || mtimeMs(src) > ot) {
    return true;
  }
  return includes.some((h) => mtimeMs(h) > ot);
}

// ─── toolchains ───────────────────────────────────────────────────────────

export type Toolchain = {
  plat: Platform;
  /** What to spawn to compile and to link. */
  exe: string;
  /** Environment additions every spawn needs (MSVC's vcvars, emsdk's config). */
  env: Record<string, string>;
  /** cl-style /flags rather than gcc-style -flags. */
  msvcStyle: boolean;
  /** Goes in the "Building …" line and in the flags stamp. */
  label: string;
  /** Object file extension, without the dot. */
  objExt: string;
};

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

function isFile(p: string): boolean {
  try {
    return statSync(p).isFile();
  } catch {
    return false;
  }
}

// --- Windows -------------------------------------------------------------

const msvcInstallHelp = [
  "Install the C++ toolset, then try again:",
  "  winget install Microsoft.VisualStudio.2026.Community --override " +
    '"--add Microsoft.VisualStudio.Workload.NativeDesktop --includeRecommended"',
  'Or open the Visual Studio Installer and add the "Desktop development with C++" workload.',
].join("\n");

function vswhereExe(): string | null {
  const pf86 = process.env["ProgramFiles(x86)"] ?? "C:\\Program Files (x86)";
  const p = join(pf86, "Microsoft Visual Studio", "Installer", "vswhere.exe");
  return isFile(p) ? p : whichExe("vswhere.exe");
}

/**
 * Every Visual Studio with the C++ toolset, newest first. vswhere is the
 * supported way to ask; the directory scan behind it is for a machine where
 * the installer is gone but the toolset is still there. Visual Studio 2026 is
 * version 18, so it sorts above 2022's 17.
 */
function vsInstallDirs(): string[] {
  const found: string[] = [];
  const vswhere = vswhereExe();
  if (vswhere) {
    const r = Bun.spawnSync(
      [
        vswhere,
        "-latest",
        "-prerelease",
        "-products",
        "*",
        "-requires",
        "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
        "-property",
        "installationPath",
      ],
      { stdout: "pipe", stderr: "pipe" },
    );
    if ((r.exitCode ?? 1) === 0) {
      for (const line of decode(r.stdout).split(/\r?\n/)) {
        const dir = line.trim();
        if (dir.length > 0 && existsSync(dir)) {
          found.push(dir);
        }
      }
    }
  }
  // 18 is Visual Studio 2026 and 17 is 2022; both the version number and the
  // year are used as the directory name in the wild, so look for either.
  const versions = ["18", "2026", "17", "2022"];
  const editions = ["Insiders", "Preview", "Enterprise", "Professional", "Community", "BuildTools"];
  for (const base of [process.env["ProgramFiles"] ?? "C:\\Program Files", process.env["ProgramFiles(x86)"] ?? ""]) {
    if (!base) {
      continue;
    }
    for (const v of versions) {
      for (const ed of editions) {
        const dir = join(base, "Microsoft Visual Studio", v, ed);
        if (existsSync(join(dir, "VC", "Auxiliary", "Build", "vcvars64.bat")) && !found.includes(dir)) {
          found.push(dir);
        }
      }
    }
  }
  return found;
}

/**
 * The environment vcvars64.bat exports — INCLUDE, LIB, LIBPATH and a PATH
 * with the toolset on it. It only ever applies to the shell that calls it, so
 * a build that was not started from a developer prompt has to read it back
 * out and carry it itself.
 */
function vcvarsEnv(vsDir: string): Record<string, string> | null {
  const bat = join(vsDir, "VC", "Auxiliary", "Build", "vcvars64.bat");
  if (!isFile(bat)) {
    return null;
  }
  // vcvars refuses to run a second time in a shell it has already set up: it
  // sees VSCMD_VER, says so and exports nothing. A parent that was started
  // from a developer prompt would poison it that way, so start from the
  // environment as it was before any vcvars touched it.
  const env: Record<string, string> = {};
  for (const [k, v] of Object.entries(process.env)) {
    if (v === undefined) {
      continue;
    }
    const up = k.toUpperCase();
    if (up.startsWith("VSCMD_") || up.startsWith("__VSCMD_")) {
      continue;
    }
    if (["VSINSTALLDIR", "VCINSTALLDIR", "VCTOOLSINSTALLDIR", "DEVENVDIR", "INCLUDE", "LIB", "LIBPATH"].includes(up)) {
      continue;
    }
    env[k] = v;
  }
  const preinit = process.env["__VSCMD_PREINIT_PATH"];
  if (preinit) {
    env["PATH"] = preinit;
  }
  // Through a batch file rather than `cmd /c "call ... && set"`: the path has
  // spaces in it, and the quotes that would need are mangled on the way into
  // cmd.exe. One argument, no inner quoting, no ambiguity.
  const shim = join(tmpdir(), `gpui-vcvars-${process.pid}.bat`);
  writeFileSync(shim, ["@echo off", `call "${bat}" >nul 2>&1`, "set", ""].join("\r\n"));
  let r;
  try {
    r = Bun.spawnSync(["cmd.exe", "/c", shim], { stdout: "pipe", stderr: "pipe", env });
  } finally {
    rmSync(shim, { force: true });
  }
  if ((r.exitCode ?? 1) !== 0) {
    return null;
  }
  const exported: Record<string, string> = {};
  for (const line of decode(r.stdout).split(/\r?\n/)) {
    const eq = line.indexOf("=");
    if (eq <= 0) {
      continue;
    }
    exported[line.slice(0, eq)] = line.slice(eq + 1);
  }
  return exported["INCLUDE"] ? exported : null;
}

function findOnPathValue(name: string, pathValue: string): string | null {
  for (const dir of pathValue.split(";")) {
    if (!dir) {
      continue;
    }
    const p = join(dir, name);
    if (isFile(p)) {
      return p;
    }
  }
  return null;
}

// clang-cl ships inside the VC toolset ("C++ Clang Compiler for Windows"),
// and may also be on PATH from a standalone LLVM.
function clangClInVs(vsDir: string): string | null {
  for (const rel of [
    ["VC", "Tools", "Llvm", "x64", "bin", "clang-cl.exe"],
    ["VC", "Tools", "Llvm", "bin", "clang-cl.exe"],
  ]) {
    const p = join(vsDir, ...rel);
    if (isFile(p)) {
      return p;
    }
  }
  return null;
}

function findWindowsToolchain(clang: boolean, fail: (msg: string) => never): Toolchain {
  const wanted = clang ? "clang-cl.exe" : "cl.exe";
  const label = clang ? "clang-cl" : "cl";

  // Already in a developer prompt (or a CI step that called vcvars): the
  // compiler and the headers it needs are both set up, so use them as they
  // are and skip the vcvars round trip.
  const onPath = whichExe(wanted);
  if (onPath && process.env["INCLUDE"]) {
    return { plat: "win", exe: onPath, env: {}, msvcStyle: true, label, objExt: "obj" };
  }

  const installs = vsInstallDirs();
  if (installs.length === 0) {
    fail(
      `${wanted} is not on PATH and no Visual Studio installation was found.\n\n${msvcInstallHelp}\n\n` +
        'Already installed? Open an "x64 Native Tools Command Prompt" and build from there.',
    );
  }
  for (const vs of installs) {
    const env = vcvarsEnv(vs);
    if (!env) {
      continue;
    }
    const onVsPath = findOnPathValue(wanted, env["PATH"] ?? "");
    const exe = clang ? (clangClInVs(vs) ?? onVsPath) : onVsPath;
    if (!exe) {
      continue;
    }
    console.log(`Using ${label} from ${vs}`);
    // Only what the compiler and linker read; inheriting the whole vcvars
    // environment would drag its PROMPT and friends along too.
    const keep: Record<string, string> = {};
    for (const k of ["PATH", "INCLUDE", "LIB", "LIBPATH", "VCToolsInstallDir", "WindowsSdkDir", "UCRTVersion"]) {
      if (env[k]) {
        keep[k] = env[k]!;
      }
    }
    return { plat: "win", exe, env: keep, msvcStyle: true, label, objExt: "obj" };
  }
  if (clang) {
    fail(
      `Found Visual Studio at ${installs[0]} but no clang-cl.exe in it.\n\n` +
        "Add it from the Visual Studio Installer: Individual components →\n" +
        '"C++ Clang Compiler for Windows". Or drop -clang to build with cl.exe.',
    );
  }
  fail(
    `Found Visual Studio at ${installs[0]} but could not run its vcvars64.bat,\n` +
      `so ${wanted} has no INCLUDE/LIB to compile against.\n\n${msvcInstallHelp}`,
  );
}

// --- Linux / macOS -------------------------------------------------------

function findUnixToolchain(plat: "linux" | "mac", clang: boolean, fail: (msg: string) => never): Toolchain {
  const fromEnv = process.env["CXX"];
  if (fromEnv) {
    return { plat, exe: fromEnv, env: {}, msvcStyle: false, label: fromEnv, objExt: "o" };
  }
  // macOS is clang either way, so -clang is a no-op there.
  const order = plat === "mac" ? ["clang++"] : clang ? ["clang++", "g++"] : ["g++", "clang++"];
  for (const name of order) {
    if (whichExe(name)) {
      return { plat, exe: name, env: {}, msvcStyle: false, label: name, objExt: "o" };
    }
  }
  if (plat === "mac") {
    fail("clang++ not found. Install the command line tools: xcode-select --install");
  }
  fail(`No C++ compiler found (looked for ${order.join(", ")}). Run: bash cmd/ubuntu-install-deps.sh`);
}

// --- wasm ----------------------------------------------------------------

// Emscripten is found through $EMCC, then $EMSDK, then a sibling emsdk
// checkout, then PATH. It is em++ rather than emcc: the link needs the C++
// runtime and emcc leaves it out. If none of those has it:
//
//   git clone https://github.com/emscripten-core/emsdk ../.emsdk
//   cd ../.emsdk && ./emsdk install latest && ./emsdk activate latest

export type Emcc = { exe: string; env: Record<string, string> };

function firstExistingFile(...cands: string[]): string | null {
  for (const c of cands) {
    if (c && isFile(c)) {
      return c;
    }
  }
  return null;
}

// The emsdk root a checkout could plausibly be at: $EMSDK, then .emsdk beside
// this repo, then one inside it.
function emsdkRoots(): string[] {
  const roots: string[] = [];
  if (process.env["EMSDK"]) {
    roots.push(process.env["EMSDK"]!);
  }
  roots.push(resolve(root, "..", ".emsdk"));
  roots.push(resolve(root, "..", "emsdk"));
  roots.push(join(root, ".emsdk"));
  return roots;
}

/** Where em++ is, and the environment it needs. Only ever called for -wasm. */
export function findEmcc(): Emcc {
  const env: Record<string, string> = {};
  const fromEnv = process.env["EMCC"];
  if (fromEnv && isFile(fromEnv)) {
    return { exe: fromEnv, env };
  }
  for (const sdk of emsdkRoots()) {
    const em = join(sdk, "upstream", "emscripten");
    const exe = firstExistingFile(join(em, "em++.exe"), join(em, "em++"));
    if (!exe) {
      continue;
    }
    // An emsdk that was activated without --permanent leaves its config in
    // the checkout rather than in the environment; point at it explicitly so
    // this works from any shell.
    const cfg = join(sdk, ".emscripten");
    if (isFile(cfg)) {
      // Forward slashes: emscripten stamps the config's directory into its
      // cache sanity file, and a backslash spelling looks like a different
      // SDK to the next run, which then clears and rebuilds the cache.
      env["EM_CONFIG"] = cfg.replaceAll("\\", "/");
    }
    return { exe, env };
  }
  const onPath = whichExe("em++");
  if (onPath) {
    return { exe: onPath, env };
  }
  console.error("No emscripten found. Set $EMCC or $EMSDK, or install it:");
  console.error("  git clone https://github.com/emscripten-core/emsdk ../.emsdk");
  console.error("  cd ../.emsdk && ./emsdk install latest && ./emsdk activate latest");
  process.exit(1);
}

/**
 * The node emsdk installed beside the compiler, which is the one the modules
 * it builds were tested against. Falls back to whatever is on PATH. This is
 * how `cmd/run.ts -wasm tests` runs a console target.
 */
export function emsdkNode(emcc: Emcc): string {
  const sdk = resolve(dirname(emcc.exe), "..", "..");
  const nodeDir = join(sdk, "node");
  if (!existsSync(nodeDir)) {
    return "node";
  }
  let found = "node";
  for (const d of readdirSync(nodeDir)) {
    // Windows puts node at the top of the version directory; the POSIX
    // packages put it under bin/.
    for (const cand of [join(nodeDir, d, "node.exe"), join(nodeDir, d, "bin", "node")]) {
      if (existsSync(cand)) {
        found = cand;
      }
    }
  }
  return found;
}

function findWasmToolchain(): Toolchain {
  // Only here: nothing goes looking for emscripten unless the target is wasm.
  const emcc = findEmcc();
  return { plat: "wasm", exe: emcc.exe, env: emcc.env, msvcStyle: false, label: "em++", objExt: "o" };
}

/** The compiler for this platform, or a message saying how to install one. */
export function findToolchain(plat: Platform, f: BuildFlags, fail: (msg: string) => never): Toolchain {
  if (plat === "win") {
    return findWindowsToolchain(f.clang, fail);
  }
  if (plat === "wasm") {
    return findWasmToolchain();
  }
  return findUnixToolchain(plat, f.clang, fail);
}

// ─── platform link inputs ─────────────────────────────────────────────────

const winCommonLibs = [
  "dwrite.lib",
  "dwmapi.lib",
  "psapi.lib",
  "ole32.lib",
  // src/gpui/accessibility_win.cpp publishes the portable semantic tree.
  "uiautomationcore.lib",
  "windowscodecs.lib",
  "user32.lib",
  "imm32.lib",
  "gdi32.lib",
  "gdiplus.lib",
  "shlwapi.lib",
  "uxtheme.lib",
  "comctl32.lib",
  "oleaut32.lib",
  "shell32.lib",
  // sys/http_win.cpp — the one thing here that talks to the network.
  "winhttp.lib",
  // wry/wry_win.cpp reads the EdgeUpdate keys to find the WebView2 runtime,
  // which is the job the SDK's WebView2Loader would otherwise do.
  "advapi32.lib",
  // sys/gpu_win.cpp reads the GPU Engine performance counters, which is what
  // Task Manager's GPU column shows.
  "pdh.lib",
];

function winLibs(f: BuildFlags): string[] {
  const backend = f.winBackend;
  const renderer =
    backend === "d2d"
      ? ["d2d1.lib", "d3d11.lib", "dxgi.lib"]
      : backend === "d3d11"
        ? ["d3d11.lib", "dxgi.lib"]
        : backend === "d3d12"
          ? ["d3d12.lib", "dxgi.lib"]
          : ["d2d1.lib", "d3d11.lib", "d3d12.lib", "dxgi.lib"];
  return [...renderer, ...winCommonLibs];
}

// Cocoa pulls in AppKit, Foundation and CoreGraphics; CoreText shapes the
// glyphs and IOKit answers the battery question. WebKit is
// src/wry/wry_mac.cpp — the webview.
const macFrameworks = ["Cocoa", "CoreText", "CoreGraphics", "ImageIO", "IOKit", "WebKit"];

// x11 for the window, cairo + pangocairo for everything drawn in it.
const linuxPkgs = ["x11", "cairo", "pangocairo", "gdk-pixbuf-2.0", "gio-2.0"];

function pkgConfig(names: string[], kind: "--cflags" | "--libs", fail: (msg: string) => never): string[] {
  const r = Bun.spawnSync(["pkg-config", kind, ...names], { stdout: "pipe", stderr: "pipe" });
  if ((r.exitCode ?? 1) !== 0) {
    const err = decode(r.stderr).trim();
    fail(`${err}\npkg-config ${kind} ${names.join(" ")} failed. Run: bash cmd/ubuntu-install-deps.sh`);
  }
  return decode(r.stdout)
    .trim()
    .split(/\s+/)
    .filter((s) => s.length > 0);
}

type LinuxDeps = { cflags: string[]; libs: string[] };
let linuxDepsMemo: LinuxDeps | null = null;

function linuxDeps(fail: (msg: string) => never): LinuxDeps {
  if (linuxDepsMemo) {
    return linuxDepsMemo;
  }
  const cflags = pkgConfig(linuxPkgs, "--cflags", fail);
  const libs = pkgConfig(linuxPkgs, "--libs", fail);
  // libcurl is sys/http_linux.cpp's client, and the only soft dependency
  // here: a machine without libcurl4-openssl-dev still builds, it just cannot
  // fetch, and a remote image renders as its alt text the way it did before
  // there was an HTTP client at all. GPUI_HAVE_CURL is what the source
  // switches on.
  const curl = Bun.spawnSync(["pkg-config", "--exists", "libcurl"], { stdout: "pipe", stderr: "pipe" });
  if ((curl.exitCode ?? 1) === 0) {
    cflags.push(...pkgConfig(["libcurl"], "--cflags", fail), "-DGPUI_HAVE_CURL=1");
    libs.push(...pkgConfig(["libcurl"], "--libs", fail));
  } else {
    console.log("libcurl not found: remote images will not load. Install it with: bash cmd/ubuntu-install-deps.sh");
  }
  linuxDepsMemo = { cflags, libs };
  return linuxDepsMemo;
}

// ─── compiler flags ───────────────────────────────────────────────────────

function cflagsFor(tc: Toolchain, f: BuildFlags, fail: (msg: string) => never): string[] {
  if (tc.plat === "win") {
    const backend = f.winBackend;
    const backendDefine =
      backend === "all"
        ? "/DWIN_BACKEND_ALL=1"
        : backend === "d3d11"
          ? "/DWIN_BACKEND_D3D11=1"
          : backend === "d3d12"
            ? "/DWIN_BACKEND_D3D12=1"
            : "/DWIN_BACKEND_DIRECT2D=1";
    const flags = [
      "/nologo",
      "/std:c++20",
      // The tree does not use C++ exception handling or RTTI. Keep those
      // runtime features out of every Windows translation unit and executable.
      "/EHs-c-",
      "/GR-",
      // MSVC's STL switch; /EHs-c- disables compiler EH itself.
      "/D_HAS_EXCEPTIONS=0",
      "/utf-8",
      ...(f.nonAmalgam
        ? [
            "/I",
            "src",
            "/I",
            "src/gpui",
            "/FI",
            "markdown/markdown.h",
            "/FI",
            "base/lib.h",
            "/FI",
            "ui/lib.h",
            "/FI",
            "gpui/paint.h",
            "/FI",
            "gpui/assets.h",
            "/FI",
            "gpui/svg.h",
            "/FI",
            "gpui/accessibility_win.h",
            "/FI",
            "sys/executor.h",
            "/FI",
            "fps/fps.h",
          ]
        : // <amalgam>/extras holds the standalone autocorrect pair, so
          // `#include "autocorrect/autocorrect.h"` spells the same in the
          // amalgam and non-amalgam (-I src) builds.
          ["/I", amalgamDir(), "/I", amalgamPath("extras")]),
      backendDefine,
      "/DUNICODE",
      "/D_UNICODE",
      "/W4",
      "/WX",
      "/wd4996",
      // /MT /MTd: static CRT. Do not use /MD — that pulls vcruntime140.dll.
      // /Gy /Gw: one COMDAT per function/global so the linker can drop unused
      // code and fold identical functions. /DEBUG would otherwise disable that.
      ...(f.debug ? ["/Od", "/MTd", "/DDEBUG"] : ["/O2", "/Gy", "/Gw", "/MT", "/DNDEBUG"]),
    ];
    if (f.clang) {
      // clang-cl has no /MP (one process compiles the whole batch anyway) and
      // no PDB server for /FS to talk to. /Z7 puts the debug info in the
      // objects, which is what its -g does.
      flags.push("/Z7");
      // /W4 does not select the same warnings under clang-cl as under cl, and
      // /WX turns every extra one into an error. These four are Windows
      // idiom, not bugs, and the tree uses all of them on purpose:
      //   missing-field-initializers  the `TRACKMOUSEEVENT t = {sizeof(t)}`
      //                               idiom every Win32 struct is opened with
      //   microsoft-exception-spec    MSVC's throw() on system declarations
      //   delete-non-abstract-…-dtor  `delete this` in Release(), which is
      //                               the COM refcount pattern and is always
      //                               reached through the most-derived type
      //   unused-command-line-arg     cl-only switches clang-cl accepts and
      //                               ignores
      flags.push(
        "-Wno-missing-field-initializers",
        "-Wno-microsoft-exception-spec",
        "-Wno-delete-non-abstract-non-virtual-dtor",
        "-Wno-unused-command-line-argument",
      );
    } else {
      flags.push("/MP", "/FS", "/Zi");
    }
    if (f.asan) {
      flags.push("/fsanitize=address");
      // Instrumenting the amalgam creates more COFF sections than the
      // original object format can encode. clang-cl uses the extended format
      // automatically; cl.exe needs it requested explicitly.
      if (!f.clang) flags.push("/bigobj");
    }
    return flags;
  }

  const flags = [
    "-std=c++20",
    ...(f.nonAmalgam
      ? [
          "-I",
          "src",
          "-I",
          "src/gpui",
          "-include",
          "markdown/markdown.h",
          "-include",
          "base/lib.h",
          "-include",
          "ui/lib.h",
          "-include",
          "gpui/paint.h",
          "-include",
          "gpui/assets.h",
          "-include",
          "gpui/svg.h",
          "-include",
          "gpui/accessibility_win.h",
          "-include",
          "sys/executor.h",
          "-include",
          "fps/fps.h",
        ]
      : // See the MSVC branch: extras/ carries the autocorrect pair.
        ["-I", amalgamDir(), "-I", amalgamPath("extras")]),
    "-Wall",
    "-Wextra",
    "-Werror",
    "-fno-exceptions",
    "-fno-rtti",
    // Do not define __EXCEPTIONS here: it is the compiler's own predefined
    // macro, not a knob. With -fno-exceptions C++ leaves it undefined, but
    // Objective-C++ on macOS predefines it to 1 regardless (ObjC exceptions
    // are on by default), so -D__EXCEPTIONS=0 is a -Wmacro-redefined error
    // there. Code that cares tests it with #ifdef, which needs no help.
    ...(f.debug ? ["-O0", "-DDEBUG"] : ["-O2", "-DNDEBUG"]),
  ];
  if (tc.plat === "mac") {
    flags.push("-Wno-deprecated-declarations", "-g");
  } else if (tc.plat === "linux") {
    flags.push("-g", ...linuxDeps(fail).cflags);
  } else if (f.debug) {
    // wasm: -g costs a lot of output, so only the debug build carries it.
    flags.push("-g");
  }
  if (f.asan) {
    flags.push("-fsanitize=address", "-fno-omit-frame-pointer");
  }
  return flags;
}

// QuickJS-NG is upstream C11, intentionally not folded into gpui.cpp. These
// mirror the warnings its own CMake build enables and suppresses while keeping
// this tree's static CRT, debug and sanitizer choices.
function quickjsCflagsFor(tc: Toolchain, f: BuildFlags, fail: (msg: string) => never): string[] {
  if (tc.plat === "win") {
    const flags = [
      "/nologo",
      "/TC",
      "/std:c11",
      ...(f.clang ? [] : ["/experimental:c11atomics"]),
      "/utf-8",
      "/D_CRT_SECURE_NO_WARNINGS",
      "/D_CRT_NONSTDC_NO_DEPRECATE",
      "/DWIN32_LEAN_AND_MEAN",
      "/D_WIN32_WINNT=0x0601",
      "/W4",
      "/WX",
      "/wd4018",
      "/wd4061",
      "/wd4098",
      "/wd4100",
      "/wd4127",
      "/wd4132",
      "/wd4146",
      "/wd4200",
      "/wd4242",
      "/wd4244",
      "/wd4245",
      "/wd4267",
      "/wd4334",
      "/wd4388",
      "/wd4389",
      "/wd4456",
      "/wd4457",
      "/wd4701",
      "/wd4702",
      "/wd4703",
      "/wd4710",
      "/wd4711",
      "/wd4820",
      "/wd4996",
      "/wd5045",
      ...(f.debug ? ["/Od", "/MTd", "/DDEBUG"] : ["/O2", "/Gy", "/Gw", "/MT", "/DNDEBUG"]),
      ...(f.clang
        ? ["/Z7", "-Wno-unused-but-set-variable", "-Wno-unused-function", "-Wno-unused-command-line-argument"]
        : ["/FS", "/Zi"]),
    ];
    if (f.asan) {
      flags.push("/fsanitize=address");
      // ASan makes QuickJS-NG's debug allocation total unused in this
      // configuration; it is upstream diagnostic-only bookkeeping.
      if (!f.clang) flags.push("/wd4189");
    }
    return flags;
  }
  const flags = [
    "-x",
    "c",
    "-std=gnu11",
    "-D_GNU_SOURCE",
    "-Wall",
    "-Wextra",
    "-Werror",
    "-Wno-implicit-fallthrough",
    "-Wno-sign-compare",
    "-Wno-missing-field-initializers",
    "-Wno-unused-parameter",
    "-Wno-unused-but-set-variable",
    "-Wno-unused-function",
    "-Wno-unused-result",
    "-Wno-array-bounds",
    "-funsigned-char",
    ...(f.debug ? ["-O0", "-DDEBUG", "-g"] : ["-O2", "-DNDEBUG"]),
  ];
  if (tc.plat === "linux" && !tc.exe.toLowerCase().includes("clang")) {
    flags.push("-Wno-stringop-truncation");
  }
  if (tc.plat === "linux") flags.push(...linuxDeps(fail).cflags);
  if (f.asan) flags.push("-fsanitize=address", "-fno-omit-frame-pointer");
  return flags;
}

// The amalgam holds the mac half, which is Objective-C++, so that one
// translation unit is compiled as Objective-C++. Non-amalgam compiles each
// *_mac.cpp on its own, and those files #import Foundation/AppKit — clang
// will not parse that as C++. Examples stay plain C++.
const objcFlags = ["-x", "objective-c++", "-fobjc-arc"];

function needsMacObjC(srcFile: string): boolean {
  const n = srcFile.replaceAll("\\", "/");
  if (n === amalgamPath("gpui.cpp")) {
    return true;
  }
  return /_mac\.cpp$/.test(n);
}

/**
 * One line a reader can paste back into a shell. Only arguments that need it
 * are quoted -- a command line where every token is in quotes is unreadable,
 * and the point of printing it is to be read. Double quotes because they are
 * what cmd.exe and a POSIX shell agree on, and a Windows path's backslashes
 * survive inside them in both.
 */
export function formatCmd(cmd: string[]): string {
  return cmd
    .map((arg) => {
      if (arg === "") {
        return '""';
      }
      if (!/[\s"]/.test(arg)) {
        return arg;
      }
      return `"${arg.replaceAll('"', '\\"')}"`;
    })
    .join(" ");
}

/**
 * The `> ` marks a line as something to run, not something that happened.
 *
 * The tool is printed as its bare name -- `cl.exe`, not the 96 characters of
 * install path in front of it, which is most of the line and the same on every
 * one of them. That is still the command you can run, because the environment
 * these lines want is one where the tool is on PATH: a Developer Command
 * Prompt for cl.exe, a shell with cargo installed for cargo. The full path is
 * printed once, by whoever knows which tool this is.
 */
export function printCmd(cmd: string[]): void {
  const [exe, ...rest] = cmd;
  console.log(`> ${formatCmd([basename(exe ?? ""), ...rest])}`);
}

function spawnOrExit(tc: Toolchain, cmd: string[]): void {
  printCmd(cmd);
  const r = Bun.spawnSync(cmd, {
    cwd: root,
    stdout: "inherit",
    stderr: "inherit",
    env: Object.keys(tc.env).length > 0 ? { ...process.env, ...tc.env } : process.env,
  });
  if ((r.exitCode ?? 1) !== 0) {
    process.exit(r.exitCode ?? 1);
  }
}

// ─── the amalgam ──────────────────────────────────────────────────────────

/**
 * A plain build in this repo regenerates .work/gpui.h + .work/gpui.cpp and the
 * separate .work/quickjs source pair from src/** and compiles them. Two things
 * take the other path and compile that source set exactly as written:
 * GPUI_AMALGAM_DIR naming
 * another copy (cmd/update-dist.ts pointing at the dist repo), and being in
 * gpui-kit-cpp-dist at all, where the files beside this script *are* the source.
 *
 * cmd/update-dist.ts is imported dynamically, and only on the branch that
 * amalgamates: it does not exist in gpui-kit-cpp-dist, and a static import of it
 * would fail there at module load — before this function ever decides it has
 * nothing to amalgamate.
 */
/** How big a generated source group is. */
function amalgamSize(bytes: number, lines: number): string {
  return `${formatHumanBytes(bytes)}, ${lines.toLocaleString("en-US")} lines`;
}

/** The standalone extras/ pairs: <dir>/<file>.h + .cpp. */
const extrasPairs = [
  ["extras/autocorrect", "autocorrect"],
  ["extras/taffy", "taffy"],
  ["extras/markdown", "markdown"],
  ["extras/markdown-mini", "markdown"],
  ["extras/html5ever", "html5ever"],
  ["extras/html5ever-mini", "html5ever"],
  ["extras/wry", "wry"],
] as const;

export async function ensureAmalgam(f: BuildFlags, fail: (msg: string) => never): Promise<void> {
  if (!isDist && amalgamIsWork()) {
    const { buildDist } = await import("./update-dist.ts");
    const a = buildDist({ outDir: ".work", markdown: f.markdown, html5ever: f.html });
    const extrasBytes = a.extras.reduce((n, e) => n + e.bytes, 0);
    const extrasLines = a.extras.reduce((n, e) => n + e.lines, 0);
    console.log(
      `amalgam ${a.headerPath} + ${a.sourcePath} ` +
        `(${a.headerCount} headers, ${a.sourceCount} + ${a.platformSourceCount} sources, markdown ${a.markdown}, ` +
        `html5ever ${a.html5ever}, ` +
        `${amalgamSize(a.headerBytes + a.sourceBytes, a.headerLines + a.sourceLines)}); ` +
        `${a.quickjsHeaderPath} + ${a.quickjsSourcePath} ` +
        `(${amalgamSize(a.quickjsHeaderBytes + a.quickjsSourceBytes, a.quickjsHeaderLines + a.quickjsSourceLines)}); ` +
        `${a.extras.length} extras/ pairs (${amalgamSize(extrasBytes, extrasLines)})`,
    );
    return;
  }
  // A published source set reports the same two numbers, read off the files,
  // even when nothing generated it this run.
  let bytes = 0;
  let lines = 0;
  let quickjsBytes = 0;
  let quickjsLines = 0;
  let extrasBytes = 0;
  let extrasLines = 0;
  const published = [
    "gpui.h",
    "gpui.cpp",
    "quickjs/quickjs.h",
    "quickjs/quickjs.c",
    ...extrasPairs.flatMap(([dir, name]) => [`${dir}/${name}.h`, `${dir}/${name}.cpp`]),
  ];
  for (const f of published) {
    const abs = join(root, amalgamDir(), f);
    if (!existsSync(abs)) {
      fail(`missing ${amalgamPath(f)}`);
    }
    const text = readFileSync(abs, "utf8");
    const fileBytes = Buffer.byteLength(text, "utf8");
    const fileLines = text === "" ? 0 : text.split("\n").length - (text.endsWith("\n") ? 1 : 0);
    if (f.startsWith("quickjs/")) {
      quickjsBytes += fileBytes;
      quickjsLines += fileLines;
    } else if (f.startsWith("extras/")) {
      extrasBytes += fileBytes;
      extrasLines += fileLines;
    } else {
      bytes += fileBytes;
      lines += fileLines;
    }
  }
  console.log(
    `amalgam ${amalgamPath("gpui.h")} + ${amalgamPath("gpui.cpp")} ` +
      `(as published, ${amalgamSize(bytes, lines)}); ${amalgamPath("quickjs/quickjs.h")} + ` +
      `${amalgamPath("quickjs/quickjs.c")} (${amalgamSize(quickjsBytes, quickjsLines)}); ` +
      `${extrasPairs.length} ${amalgamPath("extras")}/ pairs (${amalgamSize(extrasBytes, extrasLines)})`,
  );
}

// ─── assets ───────────────────────────────────────────────────────────────

function copyAssets(dir: string): void {
  const src = join(root, "assets");
  if (!existsSync(src)) {
    return;
  }
  const dst = join(root, dir, "assets");
  mkdirSync(dst, { recursive: true });
  cpSync(src, dst, { recursive: true });
}

// ASan's own runtime (not the VC++ redistributable). Needed next to the exe.
function copyAsanDll(tc: Toolchain, dir: string): void {
  const compilerDir = dirname(tc.exe);
  const name = "clang_rt.asan_dynamic-x86_64.dll";
  for (const cand of [join(compilerDir, name), join(compilerDir, "..", "..", "..", "bin", name)]) {
    if (isFile(cand)) {
      cpSync(cand, join(root, dir, name));
      return;
    }
  }
}

// ─── build one target ─────────────────────────────────────────────────────

function buildOne(name: string, tc: Toolchain, f: BuildFlags, fail: (msg: string) => never): void {
  const started = performance.now();
  const src = sourcesFor(name, tc.plat, f);
  if (!src) {
    fail(`Unknown target: ${name}`);
  }
  const dir = outDir(tc.plat, f);
  mkdirSync(join(root, dir), { recursive: true });

  const cflags = cflagsFor(tc, f, fail);
  const quickjsCflags = quickjsCflagsFor(tc, f, fail);
  const outFile = join(dir, outFileName(tc.plat, name));
  const cfg = `${f.debug ? "dbg" : "rel"}${f.asan ? "+asan" : ""}`;
  console.log(`Building ${name} (${cfg}, ${tc.label}) -> ${outFile}`);

  const stampPath = join(dir, "obj", "cflags.txt");
  const flagsKey = [tc.exe, ...cflags, "--quickjs-c--", ...quickjsCflags].join(" ");
  let flagsChanged = true;
  if (existsSync(join(root, stampPath))) {
    flagsChanged = readFileSync(join(root, stampPath), "utf8") !== flagsKey;
  }

  // AppLog.cpp implements log() for every example.
  const all = ["examples/AppLog.cpp", ...src];
  const objs: string[] = [];
  const dirty: string[] = [];
  const includeMemo = new Map<string, string[]>();
  let skipped = 0;
  for (const srcFile of all) {
    const objDir = join(dir, "obj", objGroup(srcFile));
    mkdirSync(join(root, objDir), { recursive: true });
    const obj = join(objDir, basename(srcFile).replace(/\.(cpp|c)$/i, `.${tc.objExt}`));
    objs.push(obj);
    if (flagsChanged || needsCompile(srcFile, obj, quotedIncludes(srcFile, includeMemo))) {
      dirty.push(srcFile);
    } else {
      skipped++;
    }
  }

  if (tc.plat === "win") {
    // One compiler run per object directory: /Fo names a directory, and /MP
    // then compiles the batch in parallel.
    const byGroup = new Map<string, string[]>();
    for (const srcFile of dirty) {
      byGroup.set(objGroup(srcFile), [...(byGroup.get(objGroup(srcFile)) ?? []), srcFile]);
    }
    for (const [g, files] of byGroup) {
      const objDir = join(dir, "obj", g);
      const pdb = f.clang ? [] : [`/Fd${objDir}\\`];
      const flags = g === "quickjs" ? quickjsCflags : cflags;
      spawnOrExit(tc, [tc.exe, ...flags, "/c", `/Fo${objDir}\\`, ...pdb, ...files]);
    }
  } else {
    for (const srcFile of dirty) {
      const obj = join(dir, "obj", objGroup(srcFile), basename(srcFile).replace(/\.(cpp|c)$/i, `.${tc.objExt}`));
      const isQuickJs = objGroup(srcFile) === "quickjs";
      const flags = isQuickJs ? quickjsCflags : cflags;
      const extra = !isQuickJs && tc.plat === "mac" && needsMacObjC(srcFile) ? objcFlags : [];
      spawnOrExit(tc, [tc.exe, ...flags, ...extra, "-c", srcFile, "-o", obj]);
    }
  }
  mkdirSync(join(root, dir, "obj"), { recursive: true });
  writeFileSync(join(root, stampPath), flagsKey);

  const outTime = mtimeMs(outFile);
  let linkNeeded = dirty.length > 0 || outTime === 0;
  if (!linkNeeded) {
    linkNeeded = objs.some((o) => mtimeMs(o) > outTime);
  }
  if (!linkNeeded && tc.plat === "wasm" && !consoleTargets.has(name) && mtimeMs("web/shell.html") > outTime) {
    linkNeeded = true;
  }
  console.log(`compile ${dirty.length}, skip ${skipped}${linkNeeded ? "" : ", link skipped"}`);

  if (linkNeeded) {
    link(name, tc, f, dir, objs, outFile, fail);
  }

  if (tc.plat === "win" && f.asan) {
    copyAsanDll(tc, dir);
  }
  if (tc.plat !== "wasm") {
    copyAssets(dir);
  }
  // What it cost and what it came to, on the line that says it is done: the
  // size table below is the same numbers, but it only shows up once at the
  // end, and under -all that is 26 builds away from the one you were watching.
  // The time is this target's alone; `elapsed` at the end is the whole run.
  const shown = tc.plat === "win" ? outFile.replaceAll("/", "\\") : outFile;
  const took = formatElapsed(performance.now() - started);
  const abs = join(root, outFile);
  if (!existsSync(abs)) {
    console.log(`Built ${shown} in ${took}`);
    return;
  }
  const size = statSync(abs).size;
  console.log(`Built ${shown} in ${took} size: ${formatHumanBytes(size)} ${formatExactBytes(size)}`);
}

function link(
  name: string,
  tc: Toolchain,
  f: BuildFlags,
  dir: string,
  objs: string[],
  outFile: string,
  fail: (msg: string) => never,
): void {
  if (tc.plat === "win") {
    const args = [
      "/link",
      // The test and benchmark runners write their reports to stdout, so
      // they are console apps. The entry point is the amalgam's wWinMain
      // either way.
      consoleTargets.has(name) ? "/SUBSYSTEM:CONSOLE" : "/SUBSYSTEM:WINDOWS",
      // Layout recurses once per level of the tree, and taffy's `superdeep`
      // benchmarks nest a thousand of them. Windows reserves 1 MB by default,
      // where the Rust benchmarks get the 8 MB of a Rust main thread, so this
      // asks for the same 8 MB. Measured floor for the whole suite (`-small
      // -large`) is between 2 and 3 MB, which is the ~2-3 KB a level of nested
      // grid costs; 8 MB is that with room to spare. Reserve is address space,
      // not memory: pages commit as the stack grows.
      ...(name === "bench" ? ["/STACK:8388608"] : []),
      "/ENTRY:wWinMainCRTStartup",
      "/NODEFAULTLIB:msvcrt.lib",
      "/NODEFAULTLIB:msvcrtd.lib",
      "/NODEFAULTLIB:ucrt.lib",
      "/NODEFAULTLIB:ucrtd.lib",
      "/NODEFAULTLIB:vcruntime.lib",
      "/NODEFAULTLIB:vcruntimed.lib",
      ...winLibs(f),
    ];
    if (!f.debug) {
      // /DEBUG implies /OPT:NOREF unless we opt back in.
      args.push("/INCREMENTAL:NO", "/OPT:REF", "/OPT:ICF");
    } else if (f.asan) {
      args.push("/INCREMENTAL:NO");
    }
    args.push("/DEBUG", `/PDB:${join(dir, `${name}.pdb`)}`);
    spawnOrExit(tc, [tc.exe, "/nologo", `/Fe${outFile}`, `/Fd${dir}\\`, ...objs, ...args]);
    return;
  }

  if (tc.plat === "wasm") {
    const ldflags = [
      "-sALLOW_MEMORY_GROWTH=1",
      // The element tree, the taffy solver and the markdown parser all
      // recurse as deep as the document does, and emscripten's default stack
      // is 64 KB — a tenth of what every hosted platform gives a thread.
      "-sSTACK_SIZE=8MB",
      "-sENVIRONMENT=web,worker,node",
      // EM_JS bodies are the whole platform layer here, and they reach for
      // the heap views by name.
      "-sEXPORTED_RUNTIME_METHODS=HEAPU8,HEAP32,HEAPF32",
      ...(f.debug ? ["-O0", "-g", "-sASSERTIONS=2"] : ["-O2", "-sASSERTIONS=0"]),
    ];
    if (consoleTargets.has(name)) {
      // No canvas, no assets: these print and exit.
      ldflags.push("-sEXIT_RUNTIME=1");
    } else {
      // The examples read their icons and images off disk; MEMFS is the disk.
      if (existsSync(join(root, "assets"))) {
        ldflags.push("--preload-file", "assets@/assets");
      }
      ldflags.push("--shell-file", "web/shell.html");
    }
    spawnOrExit(tc, [tc.exe, ...objs, "-o", outFile, ...ldflags]);
    return;
  }

  const ldflags: string[] = [];
  if (tc.plat === "mac") {
    for (const fw of macFrameworks) {
      ldflags.push("-framework", fw);
    }
  } else {
    ldflags.push(...linuxDeps(fail).libs, "-lm", "-lpthread");
  }
  if (f.asan) {
    ldflags.push("-fsanitize=address");
  }
  spawnOrExit(tc, [tc.exe, ...objs, "-o", outFile, ...ldflags]);
}

// ─── build many ───────────────────────────────────────────────────────────

export type BuildRequest = {
  /** Target names to build, in order. */
  names: string[];
  plat: Platform;
  flags: BuildFlags;
  fail: (msg: string) => never;
  /** Skip the size table; cmd/run.ts prints its own next to the Rust binary. */
  quiet?: boolean;
};

/**
 * Amalgamate, then compile and link each name. cmd/run.ts builds through this
 * in its own process, so there is no second `bun cmd/build.ts` and no way for
 * the two to pick different flags.
 */
export async function build(req: BuildRequest): Promise<void> {
  const { names, plat, flags, fail } = req;
  ensureWinShadersCurrent(fail);
  const tc = findToolchain(plat, flags, fail);
  // Every compile and link below is echoed with a `> `, and what those lines
  // leave out is said once here rather than on every one of them: where they
  // run from, which compiler the bare name on them is, and -- on Windows --
  // the INCLUDE/LIB/PATH this script read out of vcvars, which a Developer
  // Command Prompt exports and a plain shell does not.
  const from = `run from ${root}`;
  console.log(Object.keys(tc.env).length > 0 ? `${from}, in a shell with the MSVC environment set` : from);
  console.log(`Using ${tc.exe}`);
  if (!flags.nonAmalgam) {
    await ensureAmalgam(flags, fail);
  }
  const dir = outDir(plat, flags);
  if (flags.clean) {
    const abs = join(root, dir);
    if (existsSync(abs)) {
      console.log(`Cleaning ${dir}${plat === "win" ? "\\" : "/"}`);
      rmSync(abs, { recursive: true, force: true });
    }
  }
  for (const n of names) {
    buildOne(n, tc, flags, fail);
  }
  if (req.quiet || names.length === 0) {
    return;
  }
  console.log("");
  printOutTable(plat, flags, names);
}

function printOutTable(plat: Platform, f: BuildFlags, names: string[]): void {
  const dir = outDir(plat, f);
  const rows: { label: string; path: string }[] = [];
  for (const n of names) {
    if (plat === "wasm") {
      rows.push({ label: join(dir, `${n}.wasm`), path: join(root, dir, `${n}.wasm`) });
    }
    rows.push({ label: join(dir, outFileName(plat, n)), path: outFilePath(plat, f, n) });
  }
  printSizeTable(rows.map((r) => ({ label: plat === "win" ? r.label.replaceAll("/", "\\") : r.label, path: r.path })));
}

export function formatElapsed(ms: number): string {
  const total = Math.max(0, Math.round(ms));
  const m = Math.floor(total / 60000);
  const s = Math.floor((total % 60000) / 1000);
  const milli = total % 1000;
  if (m > 0) {
    return `${m}m ${s}s ${milli}ms`;
  }
  if (s > 0) {
    return `${s}s ${milli}ms`;
  }
  return `${milli}ms`;
}

// ─── command line ─────────────────────────────────────────────────────────

const amalgamLine = isDist
  ? `Compiles the examples against gpui.h, gpui.cpp and quickjs/ beside this script.`
  : `Always writes .work/gpui.h, .work/gpui.cpp and .work/quickjs/, then compiles examples
against that source set.`;

const usage = `Usage: bun ${scriptPath("build.ts")} [-rel|-dbg] [-asan] [-clang] [-wasm] [-clean] [-all]
                     [-markdown=mini|full] [-html=mini|full]
                     [--win-backend=d2d|d3d11|d3d12|all] [<example>]

  -rel    release (default)
  -dbg    debug
  -asan   AddressSanitizer; combines with -rel or -dbg (not with -wasm)
  -clang  Windows: build with clang-cl instead of cl.exe
          Linux: prefer clang++ over g++
  -wasm   build a page for the browser with emscripten, from any host
  -clean  delete out/<dir>/ before building
  -all    build every example (amalgamation + compile); print total elapsed
  -markdown=mini|full  Markdown parser implementation (default full)
  -html=mini|full      HTML parser implementation (default full)

Windows renderer (compile-time, default d2d):
  --win-backend=d2d|d3d11|d3d12|all
  "all" retains the __paint process-start selector; fixed builds ignore it.

${amalgamLine}

Outputs:
  out/rel/         Windows release      out/linux/rel/  Linux
  out/dbg/         Windows debug        out/mac/rel/    macOS
  out/rel_asan/    release + asan       out/wasm/rel/   browser
  out/rel_clang/   release, clang-cl

A wasm build is a page, so serve it with: bun ${scriptPath("run.ts")} -wasm <example>`;

function die(msg?: string): never {
  if (msg) {
    console.error(msg);
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
  for (const n of targetsFor(plat)) {
    console.error(`  ${n}`);
  }
  process.exit(1);
}

async function main(): Promise<void> {
  const started = performance.now();
  const flags = defaultBuildFlags();
  let all = false;
  const names: string[] = [];
  for (const raw of Bun.argv.slice(2)) {
    if (takeBuildFlag(raw, flags)) {
      continue;
    }
    if (raw === "-all") {
      all = true;
      continue;
    }
    if (raw.startsWith("-")) {
      die(`Unknown flag: ${raw}`);
    }
    names.push(raw.toLowerCase());
  }
  const plat = platformFor(flags, die);
  checkBuildFlags(flags, plat, die);

  if (names.includes("all")) {
    all = true;
    if (names.some((n) => n !== "all")) {
      die("Cannot combine -all with an example name");
    }
    names.length = 0;
  }

  let wanted: string[];
  if (all) {
    if (names.length > 0) {
      die("Cannot combine -all with an example name");
    }
    // The test and benchmark runners are targets but not examples, so -all
    // leaves them to cmd/test.ts and cmd/bench.ts.
    wanted = examplesFor(plat);
  } else {
    if (names.length === 0) {
      printExamples(plat);
    }
    if (names.length !== 1) {
      die("Pass one example name, or -all");
    }
    const name = names[0]!;
    if (!isKnownTarget(name, plat)) {
      printExamples(plat, `Unknown example: ${name}`);
    }
    wanted = [name];
  }

  await build({ names: wanted, plat, flags, fail: die });
  console.log(`elapsed ${formatElapsed(performance.now() - started)}`);
}

if (import.meta.main) {
  await main();
}
