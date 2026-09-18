// List the gpui-kit checkins after the pinned one, oldest first — the work
// list for /update-port (.claude/skills/update-port/SKILL.md).
//
//   bun cmd/upstream-pending.ts              # fetch origin, list pin..origin/main
//   bun cmd/upstream-pending.ts <sha>        # list pin..<sha> instead
//   bun cmd/upstream-pending.ts -no-fetch    # use what .work/gpui-component has
//
// Each row names the areas the checkin touches (crates/<name>, examples,
// cargo, ci, docs, …) and, when its Cargo.lock moves a crate this tree
// ports or the gpui-pre reference, the versions before and after. A crate
// Cargo.lock carries at more than one version lists them all; which one we
// port is the one its consumer's Cargo.toml asks for.
//
// Reads only. Does not move the pin or check anything out.

import { existsSync } from "node:fs";
import { join } from "node:path";
import { root } from "./build.ts";
import { gpuiComponent, rustTreeDir } from "./run.ts";

// Cargo.lock package names whose versions a checkin can move under us: the
// five ported crates, the Zed GPUI reference, and the gpui-kit workspace's own.
const watched = [
  "taffy",
  "markdown",
  "html5ever",
  "lb-wry",
  "autocorrect",
  "gpui-pre",
  ...Object.keys(gpuiComponent.crates),
];

function git(args: string[], cwd: string): string {
  const r = Bun.spawnSync(["git", ...args], { cwd, stdout: "pipe", stderr: "pipe" });
  if ((r.exitCode ?? 1) !== 0) {
    throw new Error(`git ${args.join(" ")}: ${new TextDecoder().decode(r.stderr).trim()}`);
  }
  return new TextDecoder().decode(r.stdout).trim();
}

/** package name -> versions, sorted, for the watched names in one Cargo.lock. */
function lockVersions(dir: string, sha: string): Map<string, string> {
  const out = new Map<string, string>();
  let text = "";
  try {
    text = git(["show", `${sha}:Cargo.lock`], dir);
  } catch {
    return out;
  }
  const byName = new Map<string, string[]>();
  for (const m of text.matchAll(/^\[\[package\]\]\r?\nname = "([^"]+)"\r?\nversion = "([^"]+)"/gm)) {
    const [, name, version] = m;
    if (!watched.includes(name!)) {
      continue;
    }
    byName.set(name!, [...(byName.get(name!) ?? []), version!]);
  }
  for (const [name, versions] of byName) {
    out.set(name, versions.sort().join(" + "));
  }
  return out;
}

function lockChanges(before: Map<string, string>, after: Map<string, string>): string[] {
  const changes: string[] = [];
  for (const name of watched) {
    const a = before.get(name) ?? "-";
    const b = after.get(name) ?? "-";
    if (a !== b) {
      changes.push(`${name} ${a} -> ${b}`);
    }
  }
  return changes;
}

function areas(dir: string, sha: string): string {
  const files = git(["show", "--format=", "--name-only", sha], dir).split(/\r?\n/).filter(Boolean);
  const found = new Set<string>();
  for (const f of files) {
    const parts = f.split("/");
    if (parts[0] === "crates" && parts.length > 2) {
      found.add(`crates/${parts[1]}`);
    } else if (parts[0] === "examples") {
      found.add("examples");
    } else if (f === "Cargo.lock" || f.endsWith("Cargo.toml")) {
      found.add("cargo");
    } else if (parts[0] === ".github") {
      found.add("ci");
    } else if (["docs", "website", "skills", ".claude"].includes(parts[0]!) || f.endsWith(".md")) {
      found.add("docs");
    } else if (parts[0] === "themes") {
      found.add("themes");
    } else {
      found.add("other");
    }
  }
  return [...found].sort().join(" ");
}

const args = Bun.argv.slice(2);
const noFetch = args.includes("-no-fetch");
const targetArg = args.find((a) => !a.startsWith("-"));

const dir = rustTreeDir(root);
if (!existsSync(join(dir, ".git"))) {
  console.error(`${gpuiComponent.dir} is missing; run: bun cmd/run.ts -versions`);
  process.exit(1);
}
if (!noFetch) {
  git(["fetch", "--quiet", "origin", "main"], dir);
}
const pin = gpuiComponent.sha;
const target = git(["rev-parse", targetArg ?? "origin/main"], dir);
const shas = git(["rev-list", "--reverse", "--first-parent", `${pin}..${target}`], dir)
  .split(/\r?\n/)
  .filter(Boolean);

console.log(`pinned ${pin.slice(0, 8)} ${gpuiComponent.date} ${gpuiComponent.subject}`);
console.log(`target ${target.slice(0, 8)} ${targetArg ?? "origin/main"}: ${shas.length} checkins to port\n`);

let prev = lockVersions(dir, pin);
shas.forEach((sha, i) => {
  const [short, date, ...subject] = git(["show", "-s", "--format=%h%x09%cs%x09%s", "--abbrev=8", sha], dir).split("\t");
  console.log(`${String(i + 1).padStart(3)}  ${short} ${date}  ${subject.join("\t")}`);
  console.log(`       ${areas(dir, sha)}`);
  const now = lockVersions(dir, sha);
  for (const change of lockChanges(prev, now)) {
    console.log(`       Cargo.lock: ${change}`);
  }
  prev = now;
});
