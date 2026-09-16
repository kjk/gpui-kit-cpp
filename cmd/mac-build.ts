// Compile the macOS build on the Mac, from any checkout that can reach it
// over ssh. There is no cross-compiler for Cocoa, so the source has to get
// to a Mac: this pushes the working tree to a scratch branch, then has the
// Mac fetch it and run cmd/build.ts.
//
//   bun cmd/mac-build.ts -rel hello_world
//   bun cmd/mac-build.ts -dbg -all
//   bun cmd/mac-build.ts -ios -rel
//   bun cmd/mac-build.ts -h kjk@other-mac -rel showcase
//
// It compiles and nothing else — no tests, no launching. A Cocoa window needs
// a login session, so run the app on the Mac itself with cmd/run.ts.
//
// The scratch branch is force-pushed every time and is not meant to be merged;
// the working tree, the index and the current branch are all left alone.

import { dirname, resolve } from "node:path";

const root = resolve(dirname(Bun.main), "..");
process.chdir(root);

const defaultHost = "kjk@macbook-pro-14";
const defaultDir = "~/src/gpui-cpp";
const defaultBranch = "mac-build-tmp";

const usage = `Usage: bun cmd/mac-build.ts [-h <user@host>] [-d <dir>] [-b <branch>] [build.ts flags] <example>

  -h, --host <user@host>  ssh target (default: ${defaultHost})
  -d, --dir <path>        checkout on the Mac (default: ${defaultDir})
  -b, --branch <name>     scratch branch to push (default: ${defaultBranch})

Everything else is forwarded to cmd/build.ts. With -ios it is forwarded to
cmd/mobile-build.ts instead, so no example name is needed.`;

function die(msg: string): never {
  console.error(msg);
  console.error("");
  console.error(usage);
  process.exit(1);
}

const argv = Bun.argv.slice(2);
let host = defaultHost;
let dir = defaultDir;
let branch = defaultBranch;
const forward: string[] = [];
for (let i = 0; i < argv.length; i++) {
  const a = argv[i]!;
  if (a === "-h" || a === "--host") {
    host = argv[++i] ?? die("-h needs a user@host");
    continue;
  }
  if (a === "-d" || a === "--dir") {
    dir = argv[++i] ?? die("-d needs a path");
    continue;
  }
  if (a === "-b" || a === "--branch") {
    branch = argv[++i] ?? die("-b needs a branch name");
    continue;
  }
  forward.push(a);
}
if (forward.length === 0) {
  die("Pass an example name, or -all.");
}
const ios = forward.includes("-ios");

function out(cmd: string[]): { ok: boolean; text: string } {
  const r = Bun.spawnSync(cmd, { cwd: root, stdout: "pipe", stderr: "pipe" });
  const text = new TextDecoder().decode(r.stdout).trim();
  const err = new TextDecoder().decode(r.stderr).trim();
  return { ok: (r.exitCode ?? 1) === 0, text: text || err };
}

function git(args: string[], env?: Record<string, string>): { ok: boolean; text: string } {
  const r = Bun.spawnSync(["git", ...args], {
    cwd: root,
    stdout: "pipe",
    stderr: "pipe",
    env: env ? { ...process.env, ...env } : process.env,
  });
  const text = new TextDecoder().decode(r.stdout).trim();
  const err = new TextDecoder().decode(r.stderr).trim();
  return { ok: (r.exitCode ?? 1) === 0, text: text || err };
}

// Snapshot HEAD plus every uncommitted change into a commit object, without
// touching the working tree, the real index, or the current branch. A scratch
// index file is what keeps `git add -A` out of the user's way.
function snapshotCommit(): string {
  const tmpIndex = ".git/mac-build-index";
  const env = { GIT_INDEX_FILE: tmpIndex };
  const head = git(["rev-parse", "HEAD"]);
  if (!head.ok) {
    die(`not a git repo, or no commits yet: ${head.text}`);
  }
  let r = git(["read-tree", "HEAD"], env);
  if (!r.ok) {
    die(`git read-tree failed: ${r.text}`);
  }
  r = git(["add", "-A"], env);
  if (!r.ok) {
    die(`git add failed: ${r.text}`);
  }
  const tree = git(["write-tree"], env);
  if (!tree.ok) {
    die(`git write-tree failed: ${tree.text}`);
  }
  const commit = git(["commit-tree", tree.text, "-p", head.text, "-m", "mac-build snapshot"]);
  if (!commit.ok) {
    die(`git commit-tree failed: ${commit.text}`);
  }
  return commit.text;
}

const commit = snapshotCommit();
const shortSha = commit.slice(0, 12);
console.log(`Pushing ${shortSha} to origin/${branch}`);
const pushed = git(["push", "--force", "--quiet", "origin", `${commit}:refs/heads/${branch}`]);
if (!pushed.ok) {
  die(`git push failed: ${pushed.text}`);
}

// The Mac's non-interactive shell will not have bun on PATH; add the usual
// install location. Detached HEAD on the pushed commit, so the scratch branch
// can be force-pushed again without a conflict.
const remote = [
  'export PATH="$HOME/.bun/bin:$PATH"',
  `&& cd ${dir}`,
  `&& git fetch --quiet --force origin ${branch}`,
  `&& git checkout --quiet --detach ${commit}`,
  `&& bun cmd/${ios ? "mobile-build" : "build"}.ts ${forward.join(" ")}`,
].join(" ");

console.log(`ssh ${host}: ${ios ? "mobile-build" : "build"}.ts ${forward.join(" ")}`);
const r = Bun.spawnSync(["ssh", host, remote], {
  cwd: root,
  stdout: "inherit",
  stderr: "inherit",
});
if ((r.exitCode ?? 1) !== 0) {
  console.error("");
  console.error(`Build failed on ${host}. First time on a fresh Mac:`);
  console.error(`  ssh ${host} 'xcode-select --install'`);
  console.error(`  ssh ${host} 'curl -fsSL https://bun.sh/install | bash'`);
  console.error(`  ssh ${host} 'git clone https://github.com/kjk/gpui-kit-cpp.git ${dir}'`);
}
const check = out(["git", "status", "--porcelain"]);
if (check.ok && check.text) {
  // Nothing above should have touched the tree; say so if it did.
  console.log("");
  console.log("local working tree still has uncommitted changes (as expected)");
}
process.exit(r.exitCode ?? 1);
