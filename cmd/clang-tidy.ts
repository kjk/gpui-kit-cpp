// Run clang-tidy over every source translation unit under src/.
// The arguments before `--` are passed to clang-tidy; arguments after it are
// appended to the compiler command line (for example, a project-specific
// -D). Pass --host to restrict the list to the current platform's files when
// system headers for the other platforms are unavailable.
//
//   bun cmd/clang-tidy.ts -checks=bugprone-*,performance-*
//   bun cmd/clang-tidy.ts -fix -- -DGPUI_MARKDOWN_MINI=1

import { dirname, join } from "path";
import { existsSync, readdirSync } from "fs";

type Platform = "win" | "linux" | "mac";

const root = process.cwd();

function die(message: string): never {
  console.error(message);
  process.exit(1);
}

function hostPlatform(): Platform {
  switch (process.platform) {
    case "win32":
      return "win";
    case "linux":
      return "linux";
    case "darwin":
      return "mac";
    default:
      die(`Unsupported platform: ${process.platform}`);
  }
}

function sourcePlatform(rel: string, plat: Platform): boolean {
  if (/_win\.cpp$/.test(rel)) return plat === "win";
  if (/_linux\.cpp$/.test(rel)) return plat === "linux";
  if (/_inotify\.cpp$/.test(rel)) return plat === "linux";
  if (/_mac\.cpp$/.test(rel)) return plat === "mac";
  if (/_(ios|android)\.cpp$/.test(rel)) return false;
  if (/_wasm\.cpp$/.test(rel)) return false;
  if (/_mem_posix\.cpp$/.test(rel)) return plat === "linux" || plat === "mac";
  if (/_posix\.cpp$/.test(rel)) return plat === "linux" || plat === "mac";
  return true;
}

function sourceFiles(rel: string, plat: Platform | null): string[] {
  const dir = join(root, rel);
  if (!existsSync(dir)) return [];
  const result: string[] = [];
  for (const ent of readdirSync(dir, { withFileTypes: true }).sort((a, b) => a.name.localeCompare(b.name))) {
    const child = `${rel}/${ent.name}`;
    if (ent.isDirectory()) {
      result.push(...sourceFiles(child, plat));
    } else if ((ent.name.endsWith(".cpp") || ent.name.endsWith(".c")) && (!plat || sourcePlatform(child, plat))) {
      result.push(child);
    }
  }
  return result;
}

function findExecutable(name: string): string | null {
  const finder = process.platform === "win32" ? "where.exe" : "which";
  const r = Bun.spawnSync([finder, name], { stdout: "pipe", stderr: "pipe" });
  if ((r.exitCode ?? 1) !== 0 || !r.stdout) return null;
  const text = new TextDecoder().decode(r.stdout);
  return (
    text
      .split(/\r?\n/)
      .map((s) => s.trim())
      .find((s) => s.length > 0) ?? null
  );
}

function findClangTidy(): string {
  const fromPath = findExecutable(process.platform === "win32" ? "clang-tidy.exe" : "clang-tidy");
  if (fromPath) return fromPath;

  // Visual Studio installs clang-tidy beside clang-cl, even when neither is
  // added to PATH. This also works from a regular PowerShell prompt.
  if (process.platform === "win32") {
    const clangCl = findExecutable("clang-cl.exe");
    if (clangCl) {
      const sibling = join(dirname(clangCl), "clang-tidy.exe");
      if (existsSync(sibling)) return sibling;
    }
  }
  die("clang-tidy was not found. Install LLVM (or the Visual Studio C++ Clang tools) and put it on PATH.");
}

function emscriptenInclude(): string | null {
  const candidates: string[] = [];
  const emcc = process.env["EMCC"];
  if (emcc) candidates.push(emcc);
  const onPath = findExecutable(process.platform === "win32" ? "em++.exe" : "em++");
  if (onPath) candidates.push(onPath);
  const roots = [process.env["EMSDK"], join(root, "..", ".emsdk"), join(root, "..", "emsdk"), join(root, ".emsdk")];
  for (const sdk of roots) {
    if (sdk) candidates.push(join(sdk, "upstream", "emscripten", "em++"));
  }
  for (const exe of candidates) {
    const dir = dirname(exe);
    const include = join(dir, "cache", "sysroot", "include");
    if (existsSync(join(include, "emscripten", "emscripten.h"))) return include;
  }
  return null;
}

function shuffle<T>(items: T[]): void {
  // Fisher–Yates: unlike sort(() => Math.random() - 0.5), every ordering has
  // the same probability and the source list itself remains the only state.
  for (let i = items.length - 1; i > 0; i--) {
    const j = Math.floor(Math.random() * (i + 1));
    [items[i], items[j]] = [items[j]!, items[i]!];
  }
}

function main(): void {
  const tidyArgs: string[] = [];
  const extraArgs: string[] = [];
  let afterSeparator = false;
  for (const arg of Bun.argv.slice(2)) {
    if (arg === "--") {
      afterSeparator = true;
    } else if (!afterSeparator && arg === "--host") {
      // Retained for compatibility; platform filtering is now always on.
    } else if (afterSeparator) {
      extraArgs.push(arg);
    } else {
      tidyArgs.push(arg);
    }
  }

  const plat = hostPlatform();
  const wasmInclude = emscriptenInclude();
  // Platform-specific translation units require that platform's SDK headers.
  // Always restrict the scan to the host platform; otherwise a Windows run
  // would try to parse macOS/Linux files (and vice versa).
  let files = sourceFiles("src", plat);
  if (!wasmInclude) {
    files = files.filter((file) => !/_wasm\.cpp$/.test(file));
    console.log("Emscripten not found; skipping wasm source files");
  }
  // QuickJS-NG is vendored upstream C, not part of this project's code.
  files = files.filter((file) => file !== "src/quickjs/quickjs.c");
  if (files.length === 0) die("No source files found under src/.");
  shuffle(files);
  const exe = findClangTidy();
  // The normal clang-tidy summary includes a repetitive hint about
  // non-system headers whenever the project's HeaderFilterRegex suppresses
  // diagnostics from them. Individual diagnostics remain visible; only that
  // summary (and its hint) is quieted unless the caller explicitly asks for
  // verbose output.
  if (!tidyArgs.includes("-quiet") && !tidyArgs.includes("--quiet")) {
    tidyArgs.unshift("-quiet");
  }
  if (!tidyArgs.some((arg) => arg === "-header-filter" || arg.startsWith("-header-filter="))) {
    // Do not report diagnostics from QuickJS-NG, which is vendored upstream C.
    tidyArgs.push("-header-filter=^src/(base|fps|gpui|markdown|shell|sys|taffy|ui|wry)/");
  }
  const cxxCompileArgs = [
    "-std=c++20",
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
    ...(plat === "win" ? ["-DWIN_BACKEND_ALL=1", "-DUNICODE", "-D_UNICODE"] : []),
    ...extraArgs,
  ];
  const cCompileArgs = ["-std=c11", ...extraArgs];
  const wasmCompileArgs = wasmInclude
    ? ["-std=c++20", "-I", "src", "-I", "src/gpui", "-I", wasmInclude, "-U_WIN32", "-D__EMSCRIPTEN__=1", ...extraArgs]
    : cxxCompileArgs;

  console.log(`Running ${exe} on ${files.length} source files (${plat})`);
  for (let i = 0; i < files.length; i++) {
    const file = files[i]!;
    console.log(`${i + 1}/${files.length} ${file}`);
    const compileArgs = file.endsWith(".c")
      ? cCompileArgs
      : /_wasm\.cpp$/.test(file)
        ? wasmCompileArgs
        : cxxCompileArgs;
    const result = Bun.spawnSync([exe, ...tidyArgs, file, "--", ...compileArgs], {
      cwd: root,
      stdout: "inherit",
      stderr: "inherit",
    });
    if ((result.exitCode ?? 1) !== 0) {
      die(`clang-tidy failed for ${file} (exit ${result.exitCode ?? 1})`);
    }
  }
}

main();
