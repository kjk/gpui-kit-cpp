// Amalgamate src/**/*.h and src/**/*.cpp into gpui.h and gpui.cpp. QuickJS
// stays separate: its generated C11 amalgam and public header are copied as
// quickjs/quickjs.c + quickjs.h. Each ported library crate is additionally
// amalgamated into a standalone pair under extras/ (base.h inlined behind a
// GPUI_BASE_H_ guard shared with gpui.h, so a pair header and gpui.h can
// meet in one translation unit): autocorrect — the one not inside gpui.cpp,
// compiled and linked only into the editor example and the tests — plus
// taffy, markdown, markdown-mini, html5ever, html5ever-mini and wry, which
// ARE inside gpui.cpp and
// whose pairs exist for using one library without gpui (each carries the
// base implementation, so those must never link beside gpui.cpp). All the
// generated files are the same on every platform.
//
// A source file belongs to a platform by suffix: _win.cpp, _linux.cpp,
// _mac.cpp, _ios.cpp, _android.cpp, _wasm.cpp, _mem_posix.cpp for hosted
// POSIX targets, and _posix.cpp for every POSIX target. Each of those
// goes into gpui.cpp inside its own `#if GPUI_OS_*`, so <windows.h>, <X11/*>
// and <Cocoa/*> still never reach the same translation unit — the preprocessor
// drops the two halves that are not this platform's before anything parses
// them. On macOS the whole file is Objective-C++, because the mac half is.
//
// The ported crates' implementation-private headers (markdown's tokenizer,
// taffy's compute internals, autocorrect's internal.h) are inlined behind
// `#if GPUI_INCLUDE_PRIVATE_API`, which gpui.h defaults to 0: a consumer of
// the amalgam sees only the public surface. gpui.cpp (and the autocorrect
// pair's .cpp) define it to 1 before including the header, being the
// implementation; a test that reaches into the internals does the same.
// The split is computed from the include graph, not maintained by hand.
//
// The published source set lives in a repo of its own and this script run by hand is
// the only thing that writes it. Nothing automatic may: the three platform
// builds, the test runner and CI all amalgamate into .work/, which is
// gitignored, so an ordinary build never touches what gets published. `outDir`
// is required for that reason — there is no default to fall into.
//
//   bun cmd/update-dist.ts              # sync, build, check, copy, readme, publish
//   bun cmd/update-dist.ts -no-publish  # everything but the commit and push
//   bun cmd/update-dist.ts -work        # just the .work/ sources a build compiles
//
// import { buildDist } from "./update-dist.ts";
// buildDist({ outDir: ".work", markdown: "full", html5ever: "full" });
//
// The two destinations differ. dist/ is what a reader opens: GPUI comments are
// stripped, runs of blank lines collapse to one, and the #include lines are
// lifted out of the chunks to the top of gpui.cpp and de-duplicated, since one
// translation unit only needs each header once. .work/ is the copy every build
// compiles, and GPUI is the sources concatenated and nothing else -- comments and
// all -- so a line in it is the line the `#line 1 "src/..."` marker above it
// says, and a debugger or a compiler diagnostic lands where you expect.

import { cpSync, existsSync, mkdirSync, readdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { dirname, join, relative, resolve } from "node:path";
// The upstream pins live in run.ts (which guards its main, so this import
// runs no CLI); the dist readme names the extras crate versions from there.
import {
  autocorrect as autocorrectPin,
  html5ever as html5everPin,
  markdown as markdownPin,
  taffy as taffyPin,
  wry as wryPin,
} from "./run.ts";

const root = resolve(import.meta.dir, "..");

// Where the source lives, and where the published source set lives. The GPUI
// pair plus the QuickJS pair are the compiled part of gpui-kit-cpp-dist; it has no
// history of its own worth reading, it is a snapshot of this repo.
export const srcRepoUrl = "https://github.com/kjk/gpui-kit-cpp";
export const srcBranch = "main";
export const distRepoUrl = "https://github.com/kjk/gpui-kit-cpp-dist.git";
export const distRepoDir = ".work/gpui-kit-cpp-dist";
export const distBranch = "main";

export type DistOutDir = ".work" | typeof distRepoDir;

export type Platform = "win" | "linux" | "mac" | "ios" | "android" | "wasm";

export const allPlatforms: Platform[] = ["win", "linux", "mac", "ios", "android", "wasm"];

export type BuildDistOpts = {
  /**
   * Destination directory relative to the repo root. Required: the dist repo
   * checkout is what gets published and only `bun cmd/update-dist.ts` may write
   * it, so a build script has to say ".work" out loud.
   */
  outDir: DistOutDir;
  /** Parser implementations to include in the amalgam. */
  markdown: MarkdownVariant;
  html5ever: Html5everVariant;
};

export type BuildDistResult = {
  outDir: string;
  headerPath: string;
  sourcePath: string;
  quickjsHeaderPath: string;
  quickjsSourcePath: string;
  quickjsHeaderBytes: number;
  quickjsSourceBytes: number;
  quickjsHeaderLines: number;
  quickjsSourceLines: number;
  /** The standalone extras/ pairs, one entry per library. */
  extras: { dir: string; bytes: number; lines: number }[];
  headerBytes: number;
  sourceBytes: number;
  headerLines: number;
  sourceLines: number;
  headerCount: number;
  /** Portable sources. */
  sourceCount: number;
  /** All platform-suffixed sources, including the shared POSIX halves. */
  platformSourceCount: number;
  /** Parser implementation compiled into gpui.cpp. */
  markdown: MarkdownVariant;
  /** HTML parser implementation compiled into gpui.cpp. */
  html5ever: Html5everVariant;
};

export type MarkdownVariant = "full" | "mini";
export type Html5everVariant = "full" | "mini";

// Which platform halves a source file belongs to. Empty means it is portable
// and goes in gpui.cpp; the two _posix suffixes belong to several targets.
function filePlatforms(rel: string): Platform[] {
  if (/_win\.cpp$/.test(rel)) {
    return ["win"];
  }
  if (/_linux\.cpp$/.test(rel)) {
    return ["linux"];
  }
  if (/_mac\.cpp$/.test(rel)) {
    return ["mac"];
  }
  if (/_ios\.cpp$/.test(rel)) {
    return ["ios"];
  }
  if (/_android\.cpp$/.test(rel)) {
    return ["android"];
  }
  if (/_wasm\.cpp$/.test(rel)) {
    return ["wasm"];
  }
  // Tested before _posix.cpp, which it also ends with: _mem_posix.cpp is the
  // mmap half of the platform layer, and wasm has no reserve/commit split to
  // put behind it. Everything else POSIX-shaped it does have.
  if (/_mem_posix\.cpp$/.test(rel)) {
    return ["linux", "mac", "ios", "android"];
  }
  if (/_posix\.cpp$/.test(rel)) {
    return ["linux", "mac", "ios", "android", "wasm"];
  }
  return [];
}

const osMacro: Record<Platform, string> = {
  win: "GPUI_OS_WINDOWS",
  linux: "GPUI_OS_LINUX",
  mac: "GPUI_OS_MAC",
  ios: "GPUI_OS_IOS",
  android: "GPUI_OS_ANDROID",
  wasm: "GPUI_OS_WASM",
};

// src/base.h defines all six, exactly one of them 1, so a plain #if is all a
// platform chunk needs to be there for its own platform and nowhere else.
function guardFor(plats: Platform[]): string {
  return `#if ${plats.map((p) => osMacro[p]).join(" || ")}`;
}

const quotedIncRe = /^\s*#\s*include\s+"([^"]+)"/;
const angleIncRe = /^\s*#\s*include\s+<([^>]+)>/;
const pragmaOnceRe = /^\s*#\s*pragma\s+once\b/;
// Source headers use conventional `*_H_` include guards so they can also be
// compiled as separate translation units. The amalgam has one outer guard;
// leave these wrapper directives out of each lifted header while preserving
// the source line count for .work diagnostics.
const headerGuardRe = /^\s*#\s*(?:ifndef\s+\w+_H_|define\s+\w+_H_|endif\s*\/\/\s*\w+_H_)\s*$/;
const staticDeclRe = /^static\b[^=\n{]*?\b([A-Za-z_]\w*)\s*(?=\(|\[|=|;)/gm;

function walkFiles(dir: string, ext: string, out: string[]): void {
  if (!existsSync(dir)) {
    return;
  }
  const ents = readdirSync(dir, { withFileTypes: true });
  ents.sort((a, b) => a.name.localeCompare(b.name));
  for (const ent of ents) {
    const abs = join(dir, ent.name);
    if (ent.isDirectory()) {
      walkFiles(abs, ext, out);
      continue;
    }
    if (ent.name.endsWith(ext)) {
      out.push(abs);
    }
  }
}

function srcRel(abs: string): string {
  return relative(root, abs).replaceAll("\\", "/");
}

function listSrc(ext: string): string[] {
  const files: string[] = [];
  walkFiles(join(root, "src"), ext, files);
  return files.map(srcRel);
}

function readLf(rel: string): string {
  return readFileSync(join(root, rel), "utf8").replace(/\r\n/g, "\n").replace(/\r/g, "\n");
}

function resolveQuoted(fromRel: string, inc: string): string | null {
  const incNorm = inc.replaceAll("\\", "/");
  const fromDir = dirname(fromRel).replaceAll("\\", "/");
  const candidates = [`src/${incNorm}`, `${fromDir}/${incNorm}`, `src/${incNorm.split("/").pop()}`];
  for (const raw of candidates) {
    const norm = raw.replace(/\/\.\//g, "/").replace(/^\.\//, "");
    if (existsSync(join(root, norm))) {
      return norm;
    }
  }
  return null;
}

function quotedDeps(rel: string, text: string): string[] {
  const deps: string[] = [];
  for (const line of text.split("\n")) {
    const m = quotedIncRe.exec(line);
    if (!m) {
      continue;
    }
    const hit = resolveQuoted(rel, m[1]!);
    if (hit) {
      deps.push(hit);
    }
  }
  return deps;
}

function topoHeaders(headers: string[]): string[] {
  const texts = new Map<string, string>();
  for (const h of headers) {
    texts.set(h, readLf(h));
  }
  const seen = new Set<string>();
  const order: string[] = [];
  const visit = (rel: string) => {
    if (seen.has(rel)) {
      return;
    }
    seen.add(rel);
    const text = texts.get(rel);
    if (text) {
      for (const d of quotedDeps(rel, text)) {
        visit(d);
      }
    }
    order.push(rel);
  };
  const preferred = [
    "src/base.h",
    "src/gpui/gpui.h",
    "src/gpui/assets.h",
    "src/gpui/svg.h",
    "src/base/element_ext.h",
    "src/base/lib.h",
    "src/ui/sizing.h",
    "src/ui/lib.h",
    "src/sys/sysinfo.h",
  ];
  for (const p of preferred) {
    if (texts.has(p)) {
      visit(p);
    }
  }
  for (const h of headers) {
    visit(h);
  }
  return order;
}

// Every transform here replaces rather than removes lines: an amalgam chunk
// starts with `#line 1 "<original>"`, and that is only true if the chunk has
// exactly as many lines as the file it came from. A compiler diagnostic then
// points at the real line of the real file, which -Werror makes worth the
// handful of blank lines it costs.
function stripInternalIncludes(fromRel: string, text: string): string {
  const lines = text.split("\n");
  const keep: string[] = [];
  for (const line of lines) {
    if (pragmaOnceRe.test(line) || headerGuardRe.test(line)) {
      keep.push("");
      continue;
    }
    const m = quotedIncRe.exec(line);
    const resolved = m ? resolveQuoted(fromRel, m[1]!) : null;
    // QuickJS is copied beside gpui.cpp and compiled as a separate C11
    // translation unit. Its API therefore remains a real private include in
    // gpui.cpp instead of being folded into the public gpui.h amalgam.
    if (resolved && !resolved.startsWith("src/quickjs/")) {
      keep.push("");
      continue;
    }
    keep.push(line);
  }
  return keep.join("\n");
}

// Return the byte after a C++ raw string that starts at `R`, or null when the
// bytes at `start` are not a raw-string introducer. Encoding prefixes (`u8R`,
// `uR`, `UR`, `LR`) need no special case because the scanner reaches their R
// after copying the prefix. Raw-string contents are data: the shell typings in
// particular contain both `//` URLs and `/** ... */` documentation comments.
function rawStringEnd(src: string, start: number): number | null {
  if (src[start] !== "R" || src[start + 1] !== '"') {
    return null;
  }
  const delimiterStart = start + 2;
  let open = delimiterStart;
  while (open < src.length && open - delimiterStart <= 16 && src[open] !== "(") {
    const c = src[open]!;
    if (c.charCodeAt(0) <= 0x20 || c === ")" || c === "\\") {
      return null;
    }
    open++;
  }
  if (open >= src.length || src[open] !== "(" || open - delimiterStart > 16) {
    return null;
  }
  const delimiter = src.slice(delimiterStart, open);
  const close = `)${delimiter}\"`;
  const closeAt = src.indexOf(close, open + 1);
  return closeAt < 0 ? src.length : closeAt + close.length;
}

function quotedEnd(src: string, start: number): number {
  const quote = src[start]!;
  let i = start + 1;
  while (i < src.length) {
    const c = src[i]!;
    i++;
    if (c === "\\" && i < src.length) {
      i++;
      continue;
    }
    if (c === quote || c === "\n") {
      break;
    }
  }
  return i;
}

function stripComments(src: string): string {
  let out = "";
  let i = 0;
  const n = src.length;
  while (i < n) {
    const c = src[i]!;
    const d = i + 1 < n ? src[i + 1]! : "";
    const rawEnd = rawStringEnd(src, i);
    if (rawEnd !== null) {
      out += src.slice(i, rawEnd);
      i = rawEnd;
      continue;
    }
    if (c === '"') {
      const end = quotedEnd(src, i);
      out += src.slice(i, end);
      i = end;
      continue;
    }
    if (c === "'") {
      const end = quotedEnd(src, i);
      out += src.slice(i, end);
      i = end;
      continue;
    }
    if (c === "/" && d === "/") {
      i += 2;
      while (i < n && src[i] !== "\n") {
        i++;
      }
      continue;
    }
    if (c === "/" && d === "*") {
      const start = i;
      i += 2;
      while (i + 1 < n && !(src[i] === "*" && src[i + 1] === "/")) {
        i++;
      }
      i = i + 1 < n ? i + 2 : n;
      // Keep the newlines the comment spanned, so the #line directives above
      // each chunk stay true and a compiler diagnostic points at the real
      // line of the real file.
      let nl = "";
      for (let k = start; k < i; k++) {
        if (src[k] === "\n") {
          nl += "\n";
        }
      }
      out += nl.length > 0 ? nl : " ";
      continue;
    }
    out += c;
    i++;
  }
  return out;
}

function trimTrailingSpace(src: string): string {
  let out = "";
  let i = 0;
  while (i < src.length) {
    const rawEnd = rawStringEnd(src, i);
    if (rawEnd !== null) {
      out += src.slice(i, rawEnd);
      i = rawEnd;
      continue;
    }
    const c = src[i]!;
    if (c === '"' || c === "'") {
      const end = quotedEnd(src, i);
      out += src.slice(i, end);
      i = end;
      continue;
    }
    if (c === " " || c === "\t") {
      let end = i + 1;
      while (end < src.length && (src[end] === " " || src[end] === "\t")) {
        end++;
      }
      if (src[end] !== "\n") {
        out += src.slice(i, end);
      }
      i = end;
      continue;
    }
    out += c;
    i++;
  }
  return out.replace(/\n+$/g, "") + "\n";
}

function staticNames(src: string): string[] {
  const names: string[] = [];
  staticDeclRe.lastIndex = 0;
  let m: RegExpExecArray | null;
  while ((m = staticDeclRe.exec(src))) {
    names.push(m[1]!);
  }
  return names;
}

function filePrefix(rel: string): string {
  return rel
    .replace(/^src\//, "")
    .replace(/\.[^.]+$/, "")
    .replace(/[^A-Za-z0-9]+/g, "_");
}

function renameIdents(src: string, names: Set<string>, prefix: string): string {
  if (names.size === 0) {
    return src;
  }
  const re = new RegExp(`\\b(${[...names].sort().join("|")})\\b`, "g");
  return src.replace(re, (name) => `${prefix}_${name}`);
}

function preferredCppOrder(cpps: string[]): string[] {
  const rank = (rel: string): number => {
    if (rel === "src/base.cpp") {
      return 0;
    }
    if (rel.startsWith("src/gpui/")) {
      return 1;
    }
    if (rel.startsWith("src/base/")) {
      return 2;
    }
    if (rel.startsWith("src/ui/")) {
      return 3;
    }
    if (rel.startsWith("src/sys/")) {
      return 4;
    }
    return 5;
  };
  return [...cpps].sort((a, b) => {
    const d = rank(a) - rank(b);
    return d !== 0 ? d : a.localeCompare(b);
  });
}

function finish(text: string, strip: boolean): string {
  return trimTrailingSpace(strip ? stripComments(text) : text);
}

const includeRe = /^[ \t]*#[ \t]*(?:include|import)[ \t]*[<"]/;
const condOpenRe = /^[ \t]*#[ \t]*(?:if|ifdef|ifndef)\b/;
const condCloseRe = /^[ \t]*#[ \t]*endif\b/;

// The #include and #import lines a chunk asks for, taken out of it. Only the
// ones at the top level of the file: an include inside a #if is there for a
// reason, and hoisting it would answer a question the source meant to ask.
function liftIncludes(body: string): { body: string; includes: string[] } {
  const kept: string[] = [];
  const includes: string[] = [];
  let depth = 0;
  for (const line of body.split("\n")) {
    if (condOpenRe.test(line)) {
      depth++;
    } else if (condCloseRe.test(line)) {
      depth--;
    } else if (depth === 0 && includeRe.test(line)) {
      includes.push(line.trim());
      continue;
    }
    kept.push(line);
  }
  return { body: kept.join("\n"), includes };
}

// The angle-bracket includes first, then the quoted ones, each name once,
// so the block reads as a list.
function sortedIncludes(lines: Iterable<string>): string[] {
  const key = (l: string) => {
    const angle = l.includes("<");
    const name = l.replace(/^[^<"]*[<"]/, "").replace(/[>"].*$/, "");
    return `${angle ? 0 : 1}${name}`;
  };
  return [...new Set(lines)].sort((a, b) => key(a).localeCompare(key(b)));
}

// Stripping the comments out of 100-odd files leaves runs of blank lines
// behind. dist/ collapses them, since it is read as one document; .work/ keeps
// them, so its line numbers stay in step with the `#line` markers the builds
// compile against.
function collapseBlankRuns(text: string): string {
  let out = "";
  let i = 0;
  while (i < text.length) {
    const rawEnd = rawStringEnd(text, i);
    if (rawEnd !== null) {
      out += text.slice(i, rawEnd);
      i = rawEnd;
      continue;
    }
    const c = text[i]!;
    if (c === '"' || c === "'") {
      const end = quotedEnd(text, i);
      out += text.slice(i, end);
      i = end;
      continue;
    }
    if (c === "\n") {
      let end = i + 1;
      while (end < text.length && text[end] === "\n") {
        end++;
      }
      out += text.slice(i, Math.min(end, i + 2));
      i = end;
      continue;
    }
    out += c;
    i++;
  }
  return out;
}

// src/taffy, src/markdown, src/html5ever, src/wry and src/autocorrect are
// ports of crates
// that have never heard of gpui, and they are kept that way on purpose: each
// is written against
// base.h and its own headers, so it can be read against the Rust without
// this tree's vocabulary in the way, and lifted out of it without untangling
// anything. The amalgam compiles the whole of src/ as one translation unit,
// so nothing else would notice the day one of them reached for a gpui type.
// This does.
const isolatedDirs = [
  "src/taffy/",
  "src/markdown/",
  "src/markdown-mini/",
  "src/html5ever/",
  "src/html5ever-mini/",
  "src/wry/",
  "src/autocorrect/",
];

function checkIsolation(files: string[]): void {
  const bad: string[] = [];
  for (const rel of files) {
    const dir = isolatedDirs.find((d) => rel.startsWith(d));
    if (!dir) {
      continue;
    }
    const text = readLf(rel);
    const own = dir.slice("src/".length);
    const lines = text.split("\n");
    for (let i = 0; i < lines.length; i++) {
      const inc = quotedIncRe.exec(lines[i]);
      // markdown-mini deliberately implements markdown/markdown.h: sharing
      // the public mdast keeps the API identical and costs far less than a
      // second representation plus an adapter in ui/text.cpp.
      const markdownApi = dir === "src/markdown-mini/" && inc && inc[1].startsWith("markdown/");
      const html5everApi = dir === "src/html5ever-mini/" && inc && inc[1].startsWith("html5ever/");
      if (inc && inc[1] !== "base.h" && !inc[1].startsWith(own) && !markdownApi && !html5everApi) {
        bad.push(`${rel}:${i + 1}: includes "${inc[1]}"`);
      }
    }
    // A comment may name gpui; code may not.
    const code = stripComments(text);
    const hit = /\bgpui\s*::/.exec(code);
    if (hit) {
      bad.push(`${rel}: names gpui::`);
    }
  }
  if (bad.length > 0) {
    throw new Error("isolated crates may only use base.h and their own headers:\n  " + bad.join("\n  "));
  }
}

export function buildDist(opts: BuildDistOpts): BuildDistResult {
  const outDir = opts.outDir;
  const markdown = opts.markdown;
  const html5ever = opts.html5ever;
  // QuickJS is already its own upstream-generated amalgam, compiled as C11.
  // Folding it into gpui.h/gpui.cpp would both expose its API as GPUI's and
  // ask a C++ compiler to parse C source. The autocorrect port also stays
  // out of the GPUI pair: only the editor example (and the tests) lint
  // through it, so it becomes its own pair beside quickjs/ and is compiled
  // and linked only into the targets that ask for it.
  const allHeaders = listSrc(".h").filter((rel) => !rel.startsWith("src/quickjs/"));
  const allFoundCpps = preferredCppOrder(listSrc(".cpp"));
  checkIsolation([...allHeaders, ...allFoundCpps]);
  const acHeaders = allHeaders.filter((rel) => rel.startsWith("src/autocorrect/"));
  const acCpps = allFoundCpps.filter((rel) => rel.startsWith("src/autocorrect/"));
  const headers = allHeaders.filter((rel) => !rel.startsWith("src/autocorrect/"));
  const foundCpps = allFoundCpps.filter((rel) => !rel.startsWith("src/autocorrect/"));
  const allCpps = foundCpps.filter((rel) => {
    if (markdown === "full" && rel.startsWith("src/markdown-mini/")) return false;
    if (markdown === "mini" && rel.startsWith("src/markdown/") && rel !== "src/markdown/mdast.cpp") {
      return false;
    }
    if (html5ever === "full" && rel.startsWith("src/html5ever-mini/")) return false;
    if (html5ever === "mini" && rel.startsWith("src/html5ever/")) return false;
    return true;
  });
  const cpps = allCpps.filter((f) => filePlatforms(f).length === 0);
  const platCpps = allCpps.filter((f) => filePlatforms(f).length > 0);
  if (headers.length === 0 || cpps.length === 0) {
    throw new Error("no src/**/*.h or src/**/*.cpp to amalgamate");
  }
  for (const p of allPlatforms) {
    if (!platCpps.some((f) => filePlatforms(f).includes(p))) {
      throw new Error(`no src/**/*_${p}.cpp to amalgamate`);
    }
  }

  // Which library headers are implementation-private. The ported crates
  // each have a small public surface (markdown.h + mdast.h, the taffy tree
  // and style headers gpui.h reaches, wry.h) and a larger private one
  // (tokenizers, compute internals); consumers of the amalgam get only the
  // public closure, and gpui.cpp — which is the implementation — defines
  // GPUI_INCLUDE_PRIVATE_API 1 to see the rest. The split is computed, not
  // listed: a crate header is public exactly when some header outside the
  // crate's own directory (transitively) includes it.
  const privateDirs = isolatedDirs.filter((d) => d !== "src/autocorrect/");
  const inPrivateDir = (rel: string) => privateDirs.some((d) => rel.startsWith(d));
  const publicHeaders = new Set<string>();
  const visitPublic = (rel: string) => {
    if (publicHeaders.has(rel)) {
      return;
    }
    publicHeaders.add(rel);
    for (const dep of quotedDeps(rel, readLf(rel))) {
      if (dep.endsWith(".h")) {
        visitPublic(dep);
      }
    }
  };
  for (const rel of headers) {
    if (!inPrivateDir(rel)) {
      visitPublic(rel);
    }
  }
  const isPrivateHeader = (rel: string) => inPrivateDir(rel) && !publicHeaders.has(rel);

  const headerOrder = topoHeaders(headers);
  const headerChunks: string[] = [
    "#ifndef GPUI_H_",
    "#define GPUI_H_",
    "#ifndef GPUI_AMALGAM",
    "#define GPUI_AMALGAM 1",
    "#endif",
    `#define GPUI_MARKDOWN_FULL ${markdown === "full" ? 1 : 0}`,
    `#define GPUI_MARKDOWN_MINI ${markdown === "mini" ? 1 : 0}`,
    `#define GPUI_HTML5EVER_FULL ${html5ever === "full" ? 1 : 0}`,
    `#define GPUI_HTML5EVER_MINI ${html5ever === "mini" ? 1 : 0}`,
    "#ifndef GPUI_INCLUDE_PRIVATE_API",
    "#define GPUI_INCLUDE_PRIVATE_API 0",
    "#endif",
    "",
  ];
  for (const rel of headerOrder) {
    const body = stripInternalIncludes(rel, readLf(rel));
    if (!body.trim()) {
      continue;
    }
    if (rel === "src/base.h") {
      // The standalone extras/autocorrect pair inlines base.h too; the
      // shared guard lets one translation unit include both amalgams, in
      // either order, without base being declared twice.
      headerChunks.push("#ifndef GPUI_BASE_H_", "#define GPUI_BASE_H_", `#line 1 "${rel}"`, body, "#endif", "");
      continue;
    }
    if (isPrivateHeader(rel)) {
      headerChunks.push("#if GPUI_INCLUDE_PRIVATE_API", `#line 1 "${rel}"`, body, "#endif", "");
      continue;
    }
    headerChunks.push(`#line 1 "${rel}"`, body, "");
  }
  headerChunks.push("#endif");

  const cppTexts = new Map<string, string>();
  const fileNames = new Map<string, string[]>();
  for (const rel of [...cpps, ...platCpps]) {
    const body = stripInternalIncludes(rel, readLf(rel));
    cppTexts.set(rel, body);
    fileNames.set(rel, staticNames(body));
  }
  // Two files only clash if they can be visible at once, and the three
  // platform halves never are: Window_win.cpp and Window_linux.cpp may both
  // have a `ClientDecorated` and neither has to be renamed for it. So the
  // collision set is whatever repeats within one platform's view of the tree.
  const colliding = new Set<string>();
  for (const p of allPlatforms) {
    const visible = [...cpps, ...platCpps.filter((f) => filePlatforms(f).includes(p))];
    const nameToFiles = new Map<string, Set<string>>();
    for (const rel of visible) {
      for (const name of fileNames.get(rel) ?? []) {
        const set = nameToFiles.get(name) ?? new Set<string>();
        set.add(rel);
        nameToFiles.set(name, set);
      }
    }
    for (const [name, files] of nameToFiles) {
      if (files.size > 1) {
        colliding.add(name);
      }
    }
  }

  // dist/ lifts the includes; .work/ leaves every chunk exactly as its file
  // reads. Whatever is lifted lands in `lifted`, keyed by the guard it needs.
  // .work/ is the copy a build compiles; every other destination is published.
  const tidy = outDir !== ".work";
  const portableIncludes = new Set<string>();
  const platIncludes = new Map<string, { plats: Platform[]; lines: Set<string> }>();
  const takeIncludes = (plats: Platform[], lines: string[]) => {
    if (plats.length === 0) {
      for (const l of lines) {
        portableIncludes.add(l);
      }
      return;
    }
    const key = plats.join("+");
    const group = platIncludes.get(key) ?? { plats, lines: new Set<string>() };
    for (const l of lines) {
      group.lines.add(l);
    }
    platIncludes.set(key, group);
  };

  const chunkFor = (rel: string): string | null => {
    let body = cppTexts.get(rel) ?? "";
    const local = new Set((fileNames.get(rel) ?? []).filter((n) => colliding.has(n)));
    body = renameIdents(body, local, filePrefix(rel));
    if (!body.trim()) {
      return null;
    }
    const plats = filePlatforms(rel);
    if (tidy) {
      const lifted = liftIncludes(body);
      body = lifted.body;
      takeIncludes(plats, lifted.includes);
    }
    if (plats.length === 0) {
      return [`#line 1 "${rel}"`, body, ""].join("\n");
    }
    return [guardFor(plats), `#line 1 "${rel}"`, body, "#endif", ""].join("\n");
  };

  // The portable chunks first, then the platform ones, which is what lets a
  // platform's headers be lifted at all: <X11/Xlib.h> defines None and Window,
  // and Cocoa brings its own crowd, so they must not stand in front of the
  // portable code the way the truly portable headers can.
  const portableChunks: string[] = [];
  for (const rel of cpps) {
    const chunk = chunkFor(rel);
    if (chunk) {
      portableChunks.push(chunk);
    }
  }
  const platformChunks: string[] = [];
  for (const rel of platCpps) {
    const chunk = chunkFor(rel);
    if (chunk) {
      platformChunks.push(chunk);
    }
  }

  // Comments do not survive into dist/, so the blocks below carry none: what
  // they are is documented at the top of this script instead. gpui.cpp is
  // the implementation, so it opts into the private library headers before
  // pulling the amalgam in.
  const chunks: string[] = ["#define GPUI_INCLUDE_PRIVATE_API 1", '#include "gpui.h"'];
  if (portableIncludes.size > 0) {
    chunks.push("", ...sortedIncludes(portableIncludes));
  }
  chunks.push("");
  chunks.push(...portableChunks);
  for (const group of platIncludes.values()) {
    // A header the portable block already pulled in unconditionally does not
    // need pulling in again behind a guard.
    const lines = [...group.lines].filter((l) => !portableIncludes.has(l));
    if (lines.length === 0) {
      continue;
    }
    chunks.push(guardFor(group.plats), ...sortedIncludes(lines), "#endif", "");
  }
  chunks.push(...platformChunks);

  const headerText = tidy
    ? collapseBlankRuns(finish(headerChunks.join("\n"), true))
    : finish(headerChunks.join("\n"), false);
  const sourceText = tidy ? collapseBlankRuns(finish(chunks.join("\n"), true)) : finish(chunks.join("\n"), false);

  const absOut = join(root, outDir);
  mkdirSync(absOut, { recursive: true });
  const headerPath = join(absOut, "gpui.h");
  const sourcePath = join(absOut, "gpui.cpp");
  const quickjsDir = join(absOut, "quickjs");
  const quickjsHeaderPath = join(quickjsDir, "quickjs.h");
  const quickjsSourcePath = join(quickjsDir, "quickjs.c");
  const writeIfChanged = (path: string, text: string) => {
    if (existsSync(path) && readFileSync(path, "utf8") === text) {
      return;
    }
    rmSync(path, { force: true });
    writeFileSync(path, text, "utf8");
  };
  writeIfChanged(headerPath, headerText);
  writeIfChanged(sourcePath, sourceText);
  mkdirSync(quickjsDir, { recursive: true });
  const quickjsHeaderText = readLf("src/quickjs/quickjs.h");
  const quickjsSourceText = readLf("src/quickjs/quickjs.c");
  writeIfChanged(quickjsHeaderPath, quickjsHeaderText);
  writeIfChanged(quickjsSourcePath, quickjsSourceText);

  // The extras/ pairs — each ported library crate as a standalone amalgam,
  // one header + one source. Every pair's header inlines base.h (the only
  // thing the ports depend on) behind the same GPUI_BASE_H_ guard gpui.h
  // wraps its own copy in, so gpui.h and a pair header can meet in one
  // translation unit in either order, and carries the library's private
  // headers behind the same GPUI_INCLUDE_PRIVATE_API gate (defaulted here
  // too, so a pair stands alone).
  //
  // Two consumption models, told apart by `withBaseImpl`. autocorrect is
  // not in gpui.cpp, so its pair holds declarations only and links beside
  // gpui.cpp, which provides the base implementation. taffy, markdown,
  // markdown-mini, html5ever, html5ever-mini and wry ARE in gpui.cpp — their
  // pairs exist for using one library without gpui at all, so each inlines
  // the base implementation (base.cpp and its platform halves behind
  // GPUI_OS_* guards) and must NOT be linked beside gpui.cpp: the symbols
  // would be there twice.
  const baseImplCpps = allFoundCpps.filter((rel) => /^src\/base(_\w+)?\.cpp$/.test(rel));
  const extras: { dir: string; bytes: number; lines: number }[] = [];
  const writeExtrasPair = (spec: {
    dir: string; // "extras/taffy"
    headerName: string; // "taffy.h"
    guard: string; // "TAFFY_AMALGAM_H_"
    headers: string[];
    isPublic: (rel: string) => boolean;
    cpps: string[];
    withBaseImpl: boolean;
  }) => {
    if (spec.headers.length === 0 || spec.cpps.length === 0) {
      throw new Error(`no sources to amalgamate for ${spec.dir}`);
    }
    const headerChunks: string[] = [
      `#ifndef ${spec.guard}`,
      `#define ${spec.guard}`,
      "#ifndef GPUI_INCLUDE_PRIVATE_API",
      "#define GPUI_INCLUDE_PRIVATE_API 0",
      "#endif",
      "#ifndef GPUI_BASE_H_",
      "#define GPUI_BASE_H_",
      '#line 1 "src/base.h"',
      stripInternalIncludes("src/base.h", readLf("src/base.h")),
      "#endif",
      "",
    ];
    // topoHeaders orders by dependency and pulls base.h into the walk; only
    // the pair's own headers belong here — base is inlined above.
    for (const rel of topoHeaders(spec.headers).filter((h) => spec.headers.includes(h))) {
      const body = stripInternalIncludes(rel, readLf(rel));
      if (!spec.isPublic(rel)) {
        headerChunks.push("#if GPUI_INCLUDE_PRIVATE_API", `#line 1 "${rel}"`, body, "#endif", "");
        continue;
      }
      headerChunks.push(`#line 1 "${rel}"`, body, "");
    }
    headerChunks.push("#endif");

    const sources = spec.withBaseImpl ? [...baseImplCpps, ...spec.cpps] : spec.cpps;
    const portable = sources.filter((f) => filePlatforms(f).length === 0);
    const platform = sources.filter((f) => filePlatforms(f).length > 0);
    // The same static-name collision rule as gpui.cpp, over this pair's own
    // translation unit: two files clash only when one platform's view of the
    // pair holds both.
    const names = new Map<string, string[]>();
    for (const rel of sources) {
      names.set(rel, staticNames(stripInternalIncludes(rel, readLf(rel))));
    }
    const clashing = new Set<string>();
    for (const p of allPlatforms) {
      const visible = [...portable, ...platform.filter((f) => filePlatforms(f).includes(p))];
      const owners = new Map<string, Set<string>>();
      for (const rel of visible) {
        for (const name of names.get(rel) ?? []) {
          const set = owners.get(name) ?? new Set<string>();
          set.add(rel);
          owners.set(name, set);
        }
      }
      for (const [name, files] of owners) {
        if (files.size > 1) {
          clashing.add(name);
        }
      }
    }
    const pairChunk = (rel: string, lift: Set<string> | null): string => {
      let body = stripInternalIncludes(rel, readLf(rel));
      const local = new Set((names.get(rel) ?? []).filter((n) => clashing.has(n)));
      body = renameIdents(body, local, filePrefix(rel));
      if (lift && tidy) {
        const lifted = liftIncludes(body);
        body = lifted.body;
        for (const l of lifted.includes) {
          lift.add(l);
        }
      }
      const plats = filePlatforms(rel);
      if (plats.length === 0) {
        return [`#line 1 "${rel}"`, body, ""].join("\n");
      }
      // Platform chunks keep their includes in place, inside the guard.
      return [guardFor(plats), `#line 1 "${rel}"`, body, "#endif", ""].join("\n");
    };
    const liftedIncludes = new Set<string>();
    const portableChunks = portable.map((rel) => pairChunk(rel, liftedIncludes));
    const platformChunks = platform.map((rel) => pairChunk(rel, null));
    // Quoted includes resolve against the including file first, so the pair
    // works wherever the two files sit beside each other.
    const sourceChunks: string[] = ["#define GPUI_INCLUDE_PRIVATE_API 1", `#include "${spec.headerName}"`];
    if (liftedIncludes.size > 0) {
      sourceChunks.push("", ...sortedIncludes(liftedIncludes));
    }
    sourceChunks.push("", ...portableChunks, ...platformChunks);
    const headerText = tidy
      ? collapseBlankRuns(finish(headerChunks.join("\n"), true))
      : finish(headerChunks.join("\n"), false);
    const sourceText = tidy
      ? collapseBlankRuns(finish(sourceChunks.join("\n"), true))
      : finish(sourceChunks.join("\n"), false);
    const dirAbs = join(absOut, spec.dir);
    mkdirSync(dirAbs, { recursive: true });
    writeIfChanged(join(dirAbs, spec.headerName), headerText);
    writeIfChanged(join(dirAbs, spec.headerName.replace(/\.h$/, ".cpp")), sourceText);
    extras.push({
      dir: spec.dir,
      bytes: Buffer.byteLength(headerText, "utf8") + Buffer.byteLength(sourceText, "utf8"),
      lines: countLines(headerText) + countLines(sourceText),
    });
  };

  writeExtrasPair({
    dir: "extras/autocorrect",
    headerName: "autocorrect.h",
    guard: "AUTOCORRECT_AMALGAM_H_",
    headers: acHeaders,
    isPublic: (rel) => rel === "src/autocorrect/autocorrect.h",
    cpps: acCpps,
    withBaseImpl: false,
  });
  writeExtrasPair({
    dir: "extras/taffy",
    headerName: "taffy.h",
    guard: "TAFFY_AMALGAM_H_",
    headers: headers.filter((rel) => rel.startsWith("src/taffy/")),
    isPublic: (rel) => publicHeaders.has(rel),
    cpps: foundCpps.filter((rel) => rel.startsWith("src/taffy/")),
    withBaseImpl: true,
  });
  writeExtrasPair({
    dir: "extras/markdown",
    headerName: "markdown.h",
    guard: "MARKDOWN_AMALGAM_H_",
    headers: headers.filter((rel) => rel.startsWith("src/markdown/")),
    isPublic: (rel) => publicHeaders.has(rel),
    cpps: foundCpps.filter((rel) => rel.startsWith("src/markdown/")),
    withBaseImpl: true,
  });
  writeExtrasPair({
    dir: "extras/html5ever",
    headerName: "html5ever.h",
    guard: "HTML5EVER_AMALGAM_H_",
    headers: headers.filter((rel) => rel.startsWith("src/html5ever/")),
    isPublic: (rel) => rel === "src/html5ever/html5ever.h",
    cpps: foundCpps.filter((rel) => rel.startsWith("src/html5ever/")),
    withBaseImpl: true,
  });
  writeExtrasPair({
    dir: "extras/wry",
    headerName: "wry.h",
    guard: "WRY_AMALGAM_H_",
    headers: headers.filter((rel) => rel.startsWith("src/wry/")),
    isPublic: (rel) => publicHeaders.has(rel),
    cpps: foundCpps.filter((rel) => rel.startsWith("src/wry/")),
    withBaseImpl: true,
  });
  // markdown-mini implements markdown's own public header, so its pair
  // carries the same markdown.h/mdast.h surface (and the same guard: the
  // two parsers are interchangeable, never both) over the mini sources plus
  // the shared mdast storage — which also needs constant.h, private, for
  // its tab arithmetic.
  writeExtrasPair({
    dir: "extras/markdown-mini",
    headerName: "markdown.h",
    guard: "MARKDOWN_AMALGAM_H_",
    headers: [
      "src/markdown/markdown.h",
      "src/markdown/mdast.h",
      "src/markdown/constant.h",
      ...headers.filter((rel) => rel.startsWith("src/markdown-mini/")),
    ],
    isPublic: (rel) => publicHeaders.has(rel),
    cpps: [...foundCpps.filter((rel) => rel.startsWith("src/markdown-mini/")), "src/markdown/mdast.cpp"],
    withBaseImpl: true,
  });
  writeExtrasPair({
    dir: "extras/html5ever-mini",
    headerName: "html5ever.h",
    guard: "HTML5EVER_AMALGAM_H_",
    headers: ["src/html5ever/html5ever.h", ...headers.filter((rel) => rel.startsWith("src/html5ever-mini/"))],
    isPublic: (rel) => rel === "src/html5ever/html5ever.h",
    cpps: foundCpps.filter((rel) => rel.startsWith("src/html5ever-mini/")),
    withBaseImpl: true,
  });

  return {
    outDir,
    headerPath: srcRel(headerPath),
    sourcePath: srcRel(sourcePath),
    quickjsHeaderPath: srcRel(quickjsHeaderPath),
    quickjsSourcePath: srcRel(quickjsSourcePath),
    quickjsHeaderBytes: Buffer.byteLength(quickjsHeaderText, "utf8"),
    quickjsSourceBytes: Buffer.byteLength(quickjsSourceText, "utf8"),
    quickjsHeaderLines: countLines(quickjsHeaderText),
    quickjsSourceLines: countLines(quickjsSourceText),
    extras,
    headerBytes: Buffer.byteLength(headerText, "utf8"),
    sourceBytes: Buffer.byteLength(sourceText, "utf8"),
    headerLines: countLines(headerText),
    sourceLines: countLines(sourceText),
    headerCount: headerOrder.length + 1,
    sourceCount: cpps.length,
    platformSourceCount: platCpps.length,
    markdown,
    html5ever,
  };
}

// A file's line count is how many lines an editor shows, so a trailing
// newline does not add one.
function countLines(text: string): number {
  if (text === "") {
    return 0;
  }
  const n = text.split("\n").length;
  return text.endsWith("\n") ? n - 1 : n;
}

// 123,434 — thousands separated, so generated file sizes can be compared at a
// glance rather than counted digit by digit.
function formatCount(n: number): string {
  return n.toLocaleString("en-US");
}

function formatBytes(n: number): string {
  if (n < 1024) {
    return `${n} b`;
  }
  if (n < 1024 * 1024) {
    return `${(n / 1024).toFixed(1)} KB`;
  }
  return `${(n / (1024 * 1024)).toFixed(2)} MB`;
}

function die(msg: string): never {
  console.error(msg);
  process.exit(1);
}

function run(cmd: string[], opts?: { cwd?: string; env?: Record<string, string> }): number {
  const r = Bun.spawnSync(cmd, {
    cwd: opts?.cwd ?? root,
    env: { ...process.env, ...(opts?.env ?? {}) },
    stdout: "inherit",
    stderr: "inherit",
  });
  return r.exitCode ?? 1;
}

function capture(cmd: string[], cwd?: string): string {
  const r = Bun.spawnSync(cmd, { cwd: cwd ?? root, stdout: "pipe", stderr: "pipe" });
  return new TextDecoder().decode(r.stdout).trim();
}

function cloneDistRepo(): void {
  const abs = join(root, distRepoDir);
  console.log(`cloning ${distRepoUrl} into ${distRepoDir}`);
  mkdirSync(dirname(abs), { recursive: true });
  if (run(["git", "clone", distRepoUrl, distRepoDir]) !== 0) {
    die(`git clone ${distRepoUrl} failed`);
  }
}

// Why the checkout has to be clean: this script writes the whole of it and
// then commits whatever `git status` reports, so a stray file or a checked-out
// branch that is not main would be published along with the snapshot. Nothing
// in there is written by hand and nothing in there is worth keeping, so the
// cheapest way back to a known state is to throw the directory away and clone
// it again rather than to try to repair it.
function syncDistRepo(): void {
  const abs = join(root, distRepoDir);
  if (!existsSync(join(abs, ".git"))) {
    cloneDistRepo();
    return;
  }
  const branch = capture(["git", "-C", distRepoDir, "rev-parse", "--abbrev-ref", "HEAD"]);
  const dirty = capture(["git", "-C", distRepoDir, "status", "--porcelain"]);
  if (branch !== distBranch || dirty !== "") {
    const why = branch !== distBranch ? `on ${branch || "a detached HEAD"}, not ${distBranch}` : "has local changes";
    console.log(`${distRepoDir} ${why}; removing it and cloning afresh`);
    rmSync(abs, { recursive: true, force: true });
    cloneDistRepo();
    return;
  }
  console.log(`updating ${distRepoDir}`);
  if (run(["git", "-C", distRepoDir, "pull", "--ff-only"]) !== 0) {
    die(`could not fast-forward ${distRepoDir}`);
  }
}

// Everything the snapshot carries besides the two amalgamated files.
//
// build.ts and run.ts land at the top level rather than in a cmd/ of their
// own, and that placement is what tells them which tree they are in: both
// resolve the repo root and the amalgam relative to their own directory, and
// gpui.h beside them means "this is the snapshot". winapi.ts and
// mac-window-place.m come along because run.ts -compare reaches for them by
// name — they are how it puts the two windows on the two halves of the
// screen. gpui_shell/ is the script host command. assets/ is what the
// examples load at runtime and web/ is the wasm shell page, so without them
// app_assets and every -wasm build are broken.
//
// Each directory is emptied before it is written, so a file deleted here is
// deleted there too rather than lingering as something that no longer
// compiles.
const distScripts = ["build.ts", "run.ts", "winapi.ts", "mac-window-place.m"];
const distDirs = ["examples", "gpui_shell", "assets", "web"];

// The snapshot is a working checkout, not just the generated sources: `bun run.ts -rel
// showcase` writes out/, `-compare` clones .work/gpui-component, and a -wasm
// build may install .emsdk/. None of that is the snapshot, and the publish
// step commits whatever git reports, so it has to be ignored over there too.
const distGitignore = `${[".work/", "out/", ".emsdk/", "*.obj", "*.exe", "*.pdb", "*.ilk", "*.exp", "*.lib"].join("\n")}\n`;

function copyDistExtras(): void {
  const abs = join(root, distRepoDir);
  for (const dir of distDirs) {
    const dst = join(abs, dir);
    rmSync(dst, { recursive: true, force: true });
    cpSync(join(root, dir), dst, { recursive: true });
  }
  for (const name of distScripts) {
    cpSync(join(root, "cmd", name), join(abs, name));
  }
  writeFileSync(join(abs, ".gitignore"), distGitignore, "utf8");
  console.log(`copied ${distDirs.map((d) => `${d}/`).join(", ")} and ${distScripts.join(", ")} into ${distRepoDir}`);
}

// readme-dist.md in this repo is the published readme. It is the one hand-
// written file the snapshot carries, and it is written here rather than over
// there: this script overwrites the dist copy every run, so an edit made in
// the dist repo would be lost the next time without ever reaching a reader of
// the source. `<checkin-sha1>` in it stands for the commit the snapshot was
// cut from, and every occurrence of it is filled in on the way across.
const shaPlaceholder = /<checkin-sha1>/g;

function writeDistReadme(sha: string): string {
  const src = join(root, "readme-dist.md");
  const versions = [
    ["<autocorrect-version>", autocorrectPin.version],
    ["<taffy-version>", taffyPin.version],
    ["<markdown-version>", markdownPin.version],
    ["<html5ever-version>", html5everPin.version],
    ["<wry-version>", wryPin.version],
  ] as const;
  let text = readFileSync(src, "utf8").replace(shaPlaceholder, sha);
  if (!text.includes(sha)) {
    die("readme-dist.md has no <checkin-sha1> to fill in");
  }
  for (const [placeholder, version] of versions) {
    if (!text.includes(placeholder)) {
      die(`readme-dist.md has no ${placeholder} to fill in`);
    }
    text = text.replaceAll(placeholder, version);
  }
  const abs = join(root, distRepoDir, "readme.md");
  writeFileSync(abs, text, "utf8");
  return srcRel(abs);
}

// Build every example and the Shell command against the amalgam that was just
// written, which is the only check that matters: it is what someone
// downloading these sources does. GPUI_AMALGAM_DIR points the platform build
// at this copy instead of .work/, and its objects go to their own out/ tree.
function checkExamples(): void {
  console.log(`building every example and gpui_shell against ${distRepoDir}`);
  // cmd/build.ts picks this host's toolchain itself.
  const env = { GPUI_AMALGAM_DIR: distRepoDir };
  const examples = run(["bun", "cmd/build.ts", "-rel", "-all"], { env });
  if (examples !== 0) {
    die(`the examples do not build against ${distRepoDir} (exit ${examples})`);
  }
  const shell = run(["bun", "cmd/build.ts", "-rel", "gpui_shell"], { env });
  if (shell !== 0) {
    die(`gpui_shell does not build against ${distRepoDir} (exit ${shell})`);
  }
}

// The commit that carries this snapshot. The message names the source commit
// in full, so the dist repo's log reads as a list of what it is a copy of.
function publishDistRepo(sha: string): void {
  const cwd = join(root, distRepoDir);
  const msg = `updating to ${srcRepoUrl} ${sha}`;
  if (run(["git", "add", "-A"], { cwd }) !== 0) {
    die("git add failed");
  }
  if (run(["git", "commit", "-m", msg], { cwd }) !== 0) {
    die("git commit failed");
  }
  // -u origin HEAD, so the first push of a fresh clone sets its upstream and
  // every push after it is the same command.
  if (run(["git", "push", "-u", "origin", "HEAD"], { cwd }) !== 0) {
    die("git push failed");
  }
  console.log(`published ${distRepoDir}: ${msg}`);
}

function parseCli(argv: string[]): {
  outDir: DistOutDir;
  check: boolean;
  sync: boolean;
  publish: boolean;
  markdown: MarkdownVariant;
  html5ever: Html5everVariant;
} {
  let outDir: DistOutDir = distRepoDir;
  let check = true;
  let sync = true;
  let publish = true;
  let markdown: MarkdownVariant = "full";
  let html5ever: Html5everVariant = "full";
  const usage =
    "usage: bun cmd/update-dist.ts [-work] [-no-check] [-no-sync] [-no-publish] " +
    "[-markdown=mini|full] [-html=mini|full]";
  for (const raw of argv) {
    if (raw === "-work" || raw === "--work") {
      outDir = ".work";
      check = false;
      sync = false;
      publish = false;
      continue;
    }
    if (raw === "-no-publish" || raw === "--no-publish") {
      publish = false;
      continue;
    }
    if (raw === "-no-check" || raw === "--no-check") {
      check = false;
      continue;
    }
    if (raw === "-no-sync" || raw === "--no-sync") {
      sync = false;
      continue;
    }
    const markdownMatch = raw.match(/^-markdown=(mini|full)$/i);
    if (markdownMatch) {
      markdown = markdownMatch[1]!.toLowerCase() as MarkdownVariant;
      continue;
    }
    const htmlMatch = raw.match(/^-html=(mini|full)$/i);
    if (htmlMatch) {
      html5ever = htmlMatch[1]!.toLowerCase() as Html5everVariant;
      continue;
    }
    const what = raw.startsWith("-") ? "unknown option" : "unknown argument";
    die(`${what}: ${raw}\n${usage}`);
  }
  return { outDir, check, sync, publish, markdown, html5ever };
}

function main(): void {
  const { outDir, check, sync, publish, markdown, html5ever } = parseCli(Bun.argv.slice(2));
  if (sync) {
    syncDistRepo();
  }
  const built = buildDist({ outDir, markdown, html5ever });
  console.log(
    `wrote ${built.headerPath} (${formatBytes(built.headerBytes)}, ${formatCount(built.headerBytes)} bytes, ` +
      `${formatCount(built.headerLines)} lines, ${built.headerCount} headers)`,
  );
  console.log(
    `wrote ${built.sourcePath} (${formatBytes(built.sourceBytes)}, ${formatCount(built.sourceBytes)} bytes, ` +
      `${formatCount(built.sourceLines)} lines, ` +
      `${built.sourceCount} + ${built.platformSourceCount} sources, markdown ${built.markdown}, ` +
      `html5ever ${built.html5ever})`,
  );
  if (outDir === distRepoDir) {
    copyDistExtras();
  }
  if (check) {
    checkExamples();
  }
  if (outDir !== distRepoDir) {
    return;
  }
  const sha = capture(["git", "rev-parse", "HEAD"]);
  const readme = writeDistReadme(sha);
  console.log(`wrote ${readme} for ${sha.slice(0, 12)}`);
  if (capture(["git", "branch", "-r", "--contains", sha]) === "") {
    console.error(
      `warning: ${sha.slice(0, 12)} is not on any remote yet, so the links in ` +
        "the readme stay broken until gpui-cpp is pushed",
    );
  }
  const dirty = capture(["git", "status", "--porcelain"], join(root, distRepoDir));
  if (dirty === "") {
    console.log(`${distRepoDir} is unchanged; nothing to publish`);
    return;
  }
  if (!publish) {
    console.log(`${distRepoDir} is ready; -no-publish, so it is not committed`);
    return;
  }
  publishDistRepo(sha);
}

if (import.meta.main) {
  process.chdir(root);
  main();
}
