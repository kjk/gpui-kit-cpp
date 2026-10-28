// Build and run the test suite: bun cmd/test.ts [-dbg|-rel] [-asan] [-clang]
//                                      [-clean] [--win-backend=<backend>]
//                                      [-markdown=mini|full] [-html=mini|full]
//
// The tests live in tests/ and are ports of the pure-logic ones in
// .work/gpui-component at the SHA pinned in cmd/run.ts. The runner is an
// ordinary build target, so every flag build.ts takes works here too; the
// binary prints its own report and exits nonzero on the first failure.
//
// For the wasm build of the same suite — the only place this tree is checked
// on a 32-bit word — use `bun cmd/run.ts -wasm tests`, which runs it under
// the node emsdk ships.

import { join } from "node:path";
import {
  build,
  checkBuildFlags,
  defaultBuildFlags,
  outDir,
  outFileName,
  platformFor,
  root,
  takeBuildFlag,
} from "./build.ts";

function die(msg: string): never {
  console.error(msg);
  process.exit(1);
}

const flags = defaultBuildFlags();
for (const a of Bun.argv.slice(2)) {
  // --dbg and friends have always been accepted here too.
  const buildArg = a.startsWith("--win-backend=") ? a : a.replace(/^--/, "-");
  if (takeBuildFlag(buildArg, flags)) {
    continue;
  }
  die(`Unknown flag: ${a}`);
}
if (flags.wasm) {
  die("The wasm suite runs under node, not here: bun cmd/run.ts -wasm tests");
}
const plat = platformFor(flags, die);
checkBuildFlags(flags, plat, die);

await build({ names: ["tests"], plat, flags, fail: die, quiet: true });

// build.ts owns the out/ layout — Linux, macOS and a clang-cl build each keep
// a tree of their own — so ask it where the binary landed rather than
// spelling the rule out a second time.
const dir = join(root, outDir(plat, flags));
const r = Bun.spawnSync([join(dir, outFileName(plat, "tests"))], {
  cwd: dir,
  stdout: "inherit",
  stderr: "inherit",
});
if (r.signalCode) {
  console.error(`tests killed by ${r.signalCode}`);
}
process.exit(r.exitCode ?? 1);
