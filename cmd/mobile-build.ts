// Cross-compile the portable GPUI library for an externally hosted mobile
// application. Mobile owns the native view and event-loop integration; this
// command proves that the complete C/C++ implementation and its public header
// compile for the real iOS or Android SDK, then archives them as libgpui.a.
//
//   bun cmd/mobile-build.ts -android
//   bun cmd/mobile-build.ts -ios
//   bun cmd/mobile-build.ts -android -dbg -clean

import { existsSync, mkdirSync, readFileSync, rmSync, statSync } from "node:fs";
import { basename, dirname, join, resolve } from "node:path";
import { defaultBuildFlags, ensureAmalgam, formatCmd } from "./build.ts";

const root = resolve(import.meta.dir, "..");
process.chdir(root);

const androidNdkVersion = "30.0.16248370";
const androidApi = 24;
type MobilePlatform = "android" | "ios";

const usage = `Usage: bun cmd/mobile-build.ts (-android|-ios) [-rel|-dbg] [-clean]

  -android  arm64-v8a, Android API ${androidApi}, NDK ${androidNdkVersion}
  -ios      arm64 device library, iOS 15 or later (requires macOS + Xcode)
  -rel      optimized library (default)
  -dbg      unoptimized library with debug info
  -clean    remove this target's output directory first`;

function die(message: string): never {
  console.error(message);
  console.error("");
  console.error(usage);
  process.exit(1);
}

function run(command: string[], env?: Record<string, string>): void {
  console.log(`> ${formatCmd([basename(command[0]!), ...command.slice(1)])}`);
  const result = Bun.spawnSync(command, {
    cwd: root,
    stdout: "inherit",
    stderr: "inherit",
    env: env ? { ...process.env, ...env } : process.env,
  });
  if ((result.exitCode ?? 1) !== 0) process.exit(result.exitCode ?? 1);
}

function output(command: string[]): string {
  const result = Bun.spawnSync(command, { cwd: root, stdout: "pipe", stderr: "pipe" });
  if ((result.exitCode ?? 1) !== 0) {
    const error = new TextDecoder().decode(result.stderr).trim();
    die(`${formatCmd(command)} failed${error ? `: ${error}` : ""}`);
  }
  return new TextDecoder().decode(result.stdout).trim();
}

function isFile(path: string): boolean {
  try {
    return statSync(path).isFile();
  } catch {
    return false;
  }
}

function androidNdkRoot(): string {
  const sdkRoots = [
    process.env.ANDROID_HOME,
    process.env.ANDROID_SDK_ROOT,
    process.env.LOCALAPPDATA ? join(process.env.LOCALAPPDATA, "Android", "Sdk") : undefined,
  ].filter((value): value is string => !!value);
  const candidates = [
    process.env.ANDROID_NDK_HOME,
    process.env.ANDROID_NDK_ROOT,
    ...sdkRoots.map((sdk) => join(sdk, "ndk", androidNdkVersion)),
  ].filter((value): value is string => !!value);
  for (const candidate of candidates) {
    const properties = join(candidate, "source.properties");
    if (isFile(properties) && readFileSync(properties, "utf8").includes(`Pkg.Revision = ${androidNdkVersion}`)) {
      return resolve(candidate);
    }
  }
  die(
    `Android NDK ${androidNdkVersion} is not installed. Run powershell -ExecutionPolicy Bypass -File cmd/android-install-deps.ps1`,
  );
}

function androidTools(): { cxx: string; cc: string; ar: string; target: string } {
  const ndk = androidNdkRoot();
  const host =
    process.platform === "win32" ? "windows-x86_64" : process.platform === "darwin" ? "darwin-x86_64" : "linux-x86_64";
  const bin = join(ndk, "toolchains", "llvm", "prebuilt", host, "bin");
  const ext = process.platform === "win32" ? ".exe" : "";
  const cxx = join(bin, `clang++${ext}`);
  const cc = join(bin, `clang${ext}`);
  const ar = join(bin, `llvm-ar${ext}`);
  if (!isFile(cxx) || !isFile(cc) || !isFile(ar)) {
    die(`NDK ${androidNdkVersion} has no ${host} LLVM toolchain at ${bin}`);
  }
  return { cxx, cc, ar, target: `aarch64-linux-android${androidApi}` };
}

function iosTools(): { cxx: string; cc: string; ar: string; sdk: string; target: string } {
  if (process.platform !== "darwin") die("The iOS SDK is available only through Xcode on macOS.");
  return {
    cxx: output(["xcrun", "--sdk", "iphoneos", "--find", "clang++"]),
    cc: output(["xcrun", "--sdk", "iphoneos", "--find", "clang"]),
    ar: output(["xcrun", "--sdk", "iphoneos", "--find", "ar"]),
    sdk: output(["xcrun", "--sdk", "iphoneos", "--show-sdk-path"]),
    target: "arm64-apple-ios15.0",
  };
}

let platform: MobilePlatform | null = null;
let debug = false;
let clean = false;
for (const arg of Bun.argv.slice(2)) {
  if (arg === "-android" || arg === "-ios") {
    const next = arg.slice(1) as MobilePlatform;
    if (platform && platform !== next) die("Choose exactly one of -android and -ios.");
    platform = next;
  } else if (arg === "-dbg") {
    debug = true;
  } else if (arg === "-rel") {
    debug = false;
  } else if (arg === "-clean") {
    clean = true;
  } else {
    die(`Unknown argument: ${arg}`);
  }
}
if (!platform) die("Choose -android or -ios.");

const flags = defaultBuildFlags();
flags.debug = debug;
flags.sawDbg = debug;
flags.sawRel = !debug;
await ensureAmalgam(flags, die);

const tools = platform === "android" ? androidTools() : iosTools();
const dir = join("out", platform, "arm64", debug ? "dbg" : "rel");
const absDir = join(root, dir);
if (clean && existsSync(absDir)) rmSync(absDir, { recursive: true, force: true });
mkdirSync(absDir, { recursive: true });

const targetFlags = ["--target=" + tools.target, ...(platform === "ios" ? ["-isysroot", tools.sdk] : [])];
const optimize = debug ? ["-O0", "-DDEBUG", "-g"] : ["-O2", "-DNDEBUG"];
const common = [...targetFlags, "-I", ".work", "-I", ".work/extras", "-Wall", "-Wextra", "-Werror", ...optimize];
const gpuiObj = join(dir, "gpui.o");
const quickjsObj = join(dir, "quickjs.o");
const library = join(dir, "libgpui.a");

run([
  tools.cxx,
  ...common,
  "-std=c++20",
  "-fno-exceptions",
  "-fno-rtti",
  ...(platform === "ios" ? ["-x", "objective-c++", "-fobjc-arc"] : []),
  "-c",
  ".work/gpui.cpp",
  "-o",
  gpuiObj,
]);
run([
  tools.cc,
  ...common,
  "-x",
  "c",
  "-std=gnu11",
  "-D_GNU_SOURCE",
  "-Wno-implicit-fallthrough",
  "-Wno-sign-compare",
  "-Wno-missing-field-initializers",
  "-Wno-unused-parameter",
  "-Wno-unused-but-set-variable",
  "-Wno-unused-function",
  "-Wno-unused-result",
  "-Wno-array-bounds",
  "-funsigned-char",
  "-c",
  ".work/quickjs/quickjs.c",
  "-o",
  quickjsObj,
]);
run([tools.ar, "rcs", library, gpuiObj, quickjsObj]);
console.log(`Built ${library}`);
