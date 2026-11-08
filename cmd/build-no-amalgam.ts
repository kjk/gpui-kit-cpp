// Build the library as ordinary translation units: one object per .cpp,
// followed by separately compiled examples — hello_world_no_amalgam (which
// includes the source tree's public headers by their real paths) and editor
// (the one example that uses src/autocorrect, which the standard build
// compiles as the extras/autocorrect amalgam instead). This is a structural
// check that the amalgams are not hiding declaration or link errors.
//
//   bun cmd/build-no-amalgam.ts -rel
//   bun cmd/build-no-amalgam.ts -clang -rel

import { build, checkBuildFlags, defaultBuildFlags, formatElapsed, platformFor, takeBuildFlag } from "./build.ts";

const usage = `Usage: bun cmd/build-no-amalgam.ts [-rel|-dbg] [-asan] [-clang] [-clean]
                         [-markdown=mini|full] [-html=mini|full]

Compiles every platform-appropriate src/**/*.cpp into its own object and links
examples/hello_world_no_amalgam.cpp as hello_world_no_amalgam and
examples/editor.cpp as editor (the example that compiles against
src/autocorrect directly).`;

function die(message?: string): never {
  if (message) {
    console.error(message);
  }
  console.error(usage);
  process.exit(1);
}

async function main(): Promise<void> {
  const started = performance.now();
  const flags = defaultBuildFlags();
  flags.nonAmalgam = true;
  for (const arg of Bun.argv.slice(2)) {
    if (takeBuildFlag(arg, flags)) {
      continue;
    }
    die(`Unknown flag: ${arg}`);
  }
  const plat = platformFor(flags, die);
  if (plat === "wasm") {
    die("-wasm is not supported by the native non-amalgam check");
  }
  checkBuildFlags(flags, plat, die);
  await build({
    names: ["hello_world_no_amalgam", "editor"],
    plat,
    flags,
    fail: die,
  });
  console.log(`elapsed ${formatElapsed(performance.now() - started)}`);
}

if (import.meta.main) {
  await main();
}
