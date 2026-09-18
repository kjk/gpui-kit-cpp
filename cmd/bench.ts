// Build and run the layout benchmarks: bun cmd/bench.ts [-dbg|-rel] [-asan]
//                                                       [-clang] [-clean]
//                                                       [-markdown=mini|full]
//                                                       [-html=mini|full]
//                                                       [-small] [-large]
//                                                       [-n=<count>] [<filter>]
//
// The benchmarks live in bench/. The layout ones are ports of taffy's
// benches/ directory, which is a crate of its own and not part of the
// published crate — see .claude/skills/update-port/crates.md for the
// checkout. The markdown and html5ever ones are ours: neither crate carries a
// comparable benchmark to port. The runner is an ordinary build target, so
// every flag build.ts takes works here too, and everything else goes through
// to the binary.
//
//   bun cmd/bench.ts markdown     # just the parser, after changing it
//   bun cmd/bench.ts html5ever    # the large HTML document parser
//
// Release is the default and the only setting worth reading a number from; a
// debug build measures the assertions, not the layout.

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
// Flags the build knows about; everything else is the binary's.
const runFlags: string[] = [];
for (const a of Bun.argv.slice(2)) {
  const buildArg = a.startsWith("--win-backend=") ? a : a.replace(/^--/, "-");
  if (takeBuildFlag(buildArg, flags)) {
    continue;
  }
  runFlags.push(a);
}
if (flags.wasm) {
  die("The wasm benchmarks run under node, not here: bun cmd/run.ts -wasm bench");
}
const plat = platformFor(flags, die);
checkBuildFlags(flags, plat, die);

await build({ names: ["bench"], plat, flags, fail: die, quiet: true });

// build.ts owns the out/ layout — Linux, macOS and a clang-cl build each keep
// a tree of their own — so ask it where the binary landed rather than
// spelling the rule out a second time.
const dir = join(root, outDir(plat, flags));
const r = Bun.spawnSync([join(dir, outFileName(plat, "bench")), ...runFlags], {
  cwd: dir,
  stdout: "inherit",
  stderr: "inherit",
});
process.exit(r.exitCode ?? 1);
