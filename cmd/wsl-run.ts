// Run the Linux build of a gpui example from a Windows checkout, by handing
// cmd/run.ts to WSL. The window comes up through WSLg.
//
//   bun cmd/wsl-run.ts hello_world
//   bun cmd/wsl-run.ts -dbg system_monitor
//   bun cmd/wsl-run.ts -d Ubuntu-24.04 -rel showcase
//
// Flags other than -d/--distro go straight to cmd/run.ts. The Linux
// build writes to out/linux/<cfg>/, so it never collides with the Windows
// out/<cfg>/.
//
// First time on a fresh distro:
//   wsl bash cmd/ubuntu-install-deps.sh

import { dirname, resolve } from "node:path";

const root = resolve(dirname(Bun.main), "..");
process.chdir(root);

const usage = `Usage: bun cmd/wsl-run.ts [-d <distro>] [run.ts flags] <example>

  -d, --distro <name>  WSL distribution to use (default: the WSL default)

Everything else is forwarded to cmd/run.ts inside WSL.`;

function die(msg: string): never {
  console.error(msg);
  console.error("");
  console.error(usage);
  process.exit(1);
}

if (process.platform !== "win32") {
  die("cmd/wsl-run.ts only makes sense on Windows. On Linux just use cmd/run.ts.");
}

const argv = Bun.argv.slice(2);
let distro: string | null = null;
const forward: string[] = [];
for (let i = 0; i < argv.length; i++) {
  const a = argv[i]!;
  if (a === "-d" || a === "--distro") {
    distro = argv[++i] ?? null;
    if (!distro) {
      die("-d needs a distribution name");
    }
    continue;
  }
  forward.push(a);
}
if (forward.length === 0) {
  die("Pass an example name.");
}

// wsl.exe maps the current Windows directory to /mnt/<drive>/..., so running
// it with cwd = the repo root lands in the same checkout.
function wslPath(winPath: string): string {
  const r = Bun.spawnSync(["wsl.exe", "wslpath", "-a", winPath.replaceAll("\\", "/")], {
    stdout: "pipe",
    stderr: "pipe",
  });
  if ((r.exitCode ?? 1) !== 0) {
    die(`wslpath failed for ${winPath}. Is WSL installed? (wsl --install)`);
  }
  // WSL writes UTF-16 for some commands; strip the NULs either way.
  return new TextDecoder().decode(r.stdout).replaceAll("\0", "").trim();
}

const linuxRoot = wslPath(root);

// Ubuntu's ~/.bashrc bails out early for a non-interactive shell, so the PATH
// entries the bun and rustup installers add there never run. Add them here.
const inner = [
  'export PATH="$HOME/.bun/bin:$HOME/.cargo/bin:$PATH"',
  "&&",
  "cd",
  quote(linuxRoot),
  "&&",
  "bun",
  "cmd/run.ts",
  ...forward.map(quote),
].join(" ");

function quote(s: string): string {
  return `'${s.replaceAll("'", `'\\''`)}'`;
}

const wslArgs = ["wsl.exe"];
if (distro) {
  wslArgs.push("-d", distro);
}
wslArgs.push("--", "bash", "-c", inner);

console.log(`wsl: ${inner}`);
const r = Bun.spawnSync(wslArgs, { cwd: root, stdout: "inherit", stderr: "inherit" });
if ((r.exitCode ?? 1) !== 0) {
  console.error("");
  console.error("If bun is missing inside WSL, install the toolchain first:");
  console.error("  wsl bash cmd/ubuntu-install-deps.sh");
}
process.exit(r.exitCode ?? 1);
