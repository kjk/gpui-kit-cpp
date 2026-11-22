// Write compile_commands.json for clangd.
//
//   bun cmd/gen-compile-commands.ts              release flags, ./compile_commands.json
//   bun cmd/gen-compile-commands.ts -dbg         debug flags
//   bun cmd/gen-compile-commands.ts -o=FILE      somewhere else
//
// The amalgam (.work/gpui.cpp) is the only translation unit that really
// compiles; src/** is concatenated into it with #line markers. So the
// amalgam and gpui.h get full commands, and every src/** file gets the same
// flags with no main file, which gives clangd the amalgam's defines and the
// -I.work include path when one of them is open. Naming the file there too
// would make clangd compile it alongside the amalgam and see every header
// twice. Regenerates the amalgam first, like a build does.
//
// The flags are clang-cl spellings of build.ts's Windows ones; clangd reads
// them on any host.

import { readdirSync, writeFileSync } from "node:fs";
import { join, resolve } from "node:path";
import { amalgamDir, defaultBuildFlags, ensureAmalgam, findToolchain, root } from "./build.ts";

function die(message: string): never {
  console.error(message);
  process.exit(1);
}

function clangClPath(): string {
  if (process.platform === "win32") {
    try {
      const f = { ...defaultBuildFlags(), clang: true };
      return findToolchain("win", f, (msg) => {
        throw new Error(msg);
      }).exe;
    } catch {
      // Fall through to the placeholder.
    }
  }
  console.warn("clang-cl not found; using placeholder 'clang-cl.exe'.");
  console.warn("For full IntelliSense install 'C++ Clang Compiler for Windows' via the VS Installer.");
  return "clang-cl.exe";
}

function sourceFiles(dir: string, out: string[]): string[] {
  for (const entry of readdirSync(dir, { withFileTypes: true })) {
    const path = join(dir, entry.name);
    if (entry.isDirectory()) {
      sourceFiles(path, out);
    } else if (/\.(cpp|h|hpp|mm|c)$/.test(entry.name)) {
      out.push(path);
    }
  }
  return out;
}

async function main(): Promise<void> {
  let debug = false;
  let outFile = join(root, "compile_commands.json");
  for (const arg of Bun.argv.slice(2)) {
    if (arg === "-dbg") {
      debug = true;
    } else if (arg.startsWith("-o=")) {
      outFile = resolve(arg.slice(3));
    } else {
      die(`unknown argument ${arg}\nusage: bun cmd/gen-compile-commands.ts [-dbg] [-o=FILE]`);
    }
  }

  await ensureAmalgam({ ...defaultBuildFlags(), debug }, die);
  const work = resolve(root, amalgamDir());
  const gpuiCpp = join(work, "gpui.cpp");
  const gpuiH = join(work, "gpui.h");

  const flags = [
    clangClPath(),
    "/nologo",
    "/std:c++20",
    // build.ts compiles without exceptions or RTTI.
    "/EHs-c-",
    "/GR-",
    "/D_HAS_EXCEPTIONS=0",
    "/utf-8",
    "/I",
    work,
    "/DUNICODE",
    "/D_UNICODE",
    // An explicit architecture and MSVC version, so the SDK headers don't
    // stop with STL1003 "Unexpected compiler" under clangd's clang-cl driver.
    "/D_M_X64",
    "/D_MSC_VER=1930",
    "/W4",
    // C++ for every file, .h included; clangd otherwise reads .h as C.
    "/TP",
    "/WX",
    "/wd4996",
    "/Z7",
    "-Wno-missing-field-initializers",
    "-Wno-microsoft-exception-spec",
    "-Wno-delete-non-abstract-non-virtual-dtor",
    "-Wno-unused-command-line-argument",
    "-Wno-unused-function",
    ...(debug ? ["/Od", "/MTd", "/DDEBUG"] : ["/O2", "/Gy", "/Gw", "/MT", "/DNDEBUG"]),
  ];

  const commands = [
    { directory: root, file: gpuiCpp, arguments: [...flags, gpuiCpp] },
    { directory: root, file: gpuiH, arguments: [...flags, gpuiH] },
    ...sourceFiles(join(root, "src"), []).map((file) => ({ directory: root, file, arguments: flags })),
  ];
  writeFileSync(outFile, JSON.stringify(commands, null, 2) + "\n");
  console.log(`Wrote ${outFile} with ${commands.length} entries.`);
  console.log("Restart the language server (Zed: 'editor: restart language server') to pick it up.");
}

if (import.meta.main) {
  await main();
}
