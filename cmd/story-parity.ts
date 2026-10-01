// Story gallery inventory: compare each C++ story page with the Rust story it
// ports, by the user-visible text each one names.
//
//   bun cmd/story-parity.ts            # every page with a difference
//   bun cmd/story-parity.ts -all       # every page, matched text included
//   bun cmd/story-parity.ts button     # pages whose struct/slug contains it
//
// What it checks:
//   - gallery order: StoryContainer::panel::<X> in crates/story/src/gallery.rs
//     against the enum in examples/story/Story.h
//   - title and description: Story::title()/description() against kMeta in
//     examples/story/story.cpp
//   - sections: section("…") titles in Rust against StorySection(cx, "…") in
//     the C++ page (a Rust title found elsewhere in the page as a literal
//     counts as present, since some pages build sections from a table)
//   - visible text: literals passed to label/title/description/child/
//     placeholder/tooltip/menu/menu_with_check/sub_title/message in Rust that
//     appear nowhere in the C++ page
//
// It is a text inventory, not a proof: a difference is a place to read both
// files, and a match says nothing about layout. Pair it with
// `bun cmd/compare-story.ts <slug>` for the look.

import { existsSync, readdirSync, readFileSync, statSync } from "node:fs";
import { basename, dirname, join, resolve } from "node:path";
import { ensureRustTree, rustTreeDir } from "./run.ts";

const root = resolve(dirname(Bun.main), "..");
process.chdir(root);

let showAll = false;
const filters: string[] = [];
for (const a of process.argv.slice(2)) {
  if (a === "-all") {
    showAll = true;
  } else if (a.startsWith("-")) {
    console.error("Usage: bun cmd/story-parity.ts [-all] [filter…]");
    process.exit(1);
  } else {
    filters.push(a.toLowerCase());
  }
}

ensureRustTree(root);
const storyRs = join(rustTreeDir(root), "crates", "story", "src");
const storiesRs = join(storyRs, "stories");
const storyCpp = join(root, "examples", "story");

function walk(dir: string, ext: string, out: string[] = []): string[] {
  for (const name of readdirSync(dir)) {
    const p = join(dir, name);
    if (statSync(p).isDirectory()) {
      walk(p, ext, out);
    } else if (name.endsWith(ext)) {
      out.push(p);
    }
  }
  return out;
}

function unescape(s: string): string {
  return s
    .replace(/\\\r?\n\s*/g, "")
    .replace(/\\n/g, "\n")
    .replace(/\\t/g, "\t")
    .replace(/\\"/g, '"')
    .replace(/\\'/g, "'")
    .replace(/\\u\{([0-9a-fA-F]+)\}/g, (_, h) => String.fromCodePoint(parseInt(h, 16)))
    .replace(/\\\\/g, "\\");
}

// Strip comments so a literal quoted in a comment is not taken for code.
function stripComments(src: string): string {
  let out = "";
  let i = 0;
  while (i < src.length) {
    const c = src[i];
    if (c === '"') {
      let j = i + 1;
      while (j < src.length && src[j] !== '"') {
        j += src[j] === "\\" ? 2 : 1;
      }
      out += src.slice(i, j + 1);
      i = j + 1;
    } else if (c === "/" && src[i + 1] === "/") {
      while (i < src.length && src[i] !== "\n") i++;
    } else if (c === "/" && src[i + 1] === "*") {
      const e = src.indexOf("*/", i + 2);
      i = e < 0 ? src.length : e + 2;
    } else {
      out += c;
      i++;
    }
  }
  return out;
}

// Every C++ string literal, with adjacent literals ("a" "b") joined, and the
// u8 prefix dropped.
function cppLiterals(src: string): Set<string> {
  const set = new Set<string>();
  const re = /(?:u8)?"((?:[^"\\]|\\.)*)"(?:\s*(?:u8)?"((?:[^"\\]|\\.)*)")*/g;
  for (const m of src.matchAll(re)) {
    const parts = [...m[0].matchAll(/"((?:[^"\\]|\\.)*)"/g)].map((p) => unescape(p[1]));
    set.add(parts.join(""));
  }
  return set;
}

function rsCall(src: string, names: string): string[] {
  const out: string[] = [];
  const re = new RegExp(`(?<![\\w.])(?:${names})\\(\\s*"((?:[^"\\\\]|\\\\[\\s\\S])*)"`, "g");
  for (const m of src.matchAll(re)) out.push(unescape(m[1]));
  return out;
}

function rsMethod(src: string, names: string): string[] {
  const out: string[] = [];
  const re = new RegExp(`\\.(?:${names})\\(\\s*"((?:[^"\\\\]|\\\\[\\s\\S])*)"`, "g");
  for (const m of src.matchAll(re)) out.push(unescape(m[1]));
  return out;
}

function fnString(src: string, fn: string): string | null {
  const re = new RegExp(`fn ${fn}\\(\\)\\s*->\\s*&'static str\\s*\\{\\s*"((?:[^"\\\\]|\\\\[\\s\\S])*)"`);
  const m = src.match(re);
  return m ? unescape(m[1]) : null;
}

// Rust: struct name -> source text (the defining file plus a same-named
// directory of helpers, e.g. chart_story.rs + chart_story/).
const rsFiles = walk(storiesRs, ".rs");
const rsByStruct = new Map<string, { file: string; src: string }>();
for (const f of rsFiles) {
  const src = readFileSync(f, "utf8");
  for (const m of src.matchAll(/impl (?:super::|crate::stories::|crate::)?Story for (\w+)/g)) {
    let text = src;
    const sub = f.replace(/\.rs$/, "");
    if (existsSync(sub) && statSync(sub).isDirectory()) {
      for (const g of walk(sub, ".rs")) text += "\n" + readFileSync(g, "utf8");
    }
    rsByStruct.set(m[1], { file: f, src: stripComments(text) });
  }
}

const gallery = readFileSync(join(storyRs, "gallery.rs"), "utf8");
const rsOrder = [...gallery.matchAll(/StoryContainer::panel::<(\w+)>/g)].map((m) => m[1]);

// C++: Story.h enum order, STORY_PAGE(ID, Struct) pairs, kMeta rows.
const storyH = readFileSync(join(storyCpp, "Story.h"), "utf8");
const enumBody = storyH.slice(storyH.indexOf("StoryWelcome = 0"), storyH.indexOf("StoryCount,"));
const cppIds = [...stripComments(enumBody).matchAll(/\b(Story\w+)/g)].map((m) => m[1]);

const cppFiles = walk(storyCpp, ".cpp").concat(walk(storyCpp, ".h"));
const cppSrc = new Map<string, string>();
for (const f of cppFiles) cppSrc.set(f, stripComments(readFileSync(f, "utf8")));
const idToStruct = new Map<string, { struct: string; file: string }>();
for (const [f, src] of cppSrc) {
  for (const m of src.matchAll(/STORY_PAGE(?:_KEYS)?\((\w+),\s*(\w+)\)/g)) {
    if (m[1] !== "ID") idToStruct.set(m[1], { struct: m[2], file: f });
  }
}
const cppOrder = cppIds.map((id) => idToStruct.get(id)?.struct ?? `(${id}: no page)`);

const storyCppSrc = cppSrc.get(join(storyCpp, "story.cpp"))!;
const metaBody = storyCppSrc.slice(storyCppSrc.indexOf("kMeta[StoryCount]"));
const metaRe = /\{\s*"([^"]*)",\s*"([^"]*)",\s*(nullptr|(?:"(?:[^"\\]|\\.)*"\s*)+)\}/g;
const meta = [...metaBody.matchAll(metaRe)].map((m) => ({
  slug: m[1],
  title: m[2],
  desc: m[3] === "nullptr" ? null : [...m[3].matchAll(/"((?:[^"\\]|\\.)*)"/g)].map((p) => unescape(p[1])).join(""),
}));

let problems = 0;
const say = (s: string) => console.log(s);

// Gallery order.
{
  const a = rsOrder.join(" ");
  const b = cppOrder.join(" ");
  if (a !== b) {
    problems++;
    say("gallery order differs (Rust gallery.rs vs C++ Story.h):");
    const n = Math.max(rsOrder.length, cppOrder.length);
    for (let i = 0; i < n; i++) {
      if (rsOrder[i] !== cppOrder[i]) say(`  ${i}: rust ${rsOrder[i] ?? "-"}  cpp ${cppOrder[i] ?? "-"}`);
    }
  } else if (showAll) {
    say(`gallery order: ${rsOrder.length} pages, same`);
  }
}

const visible = "label|title|description|child|placeholder|tooltip|sub_title|message|menu|menu_with_check|content";

for (let i = 0; i < cppIds.length; i++) {
  const id = cppIds[i];
  const page = idToStruct.get(id);
  const struct = page?.struct ?? "";
  const m = meta[i];
  const slug = m?.slug ?? id;
  if (filters.length && !filters.some((f) => slug.includes(f) || struct.toLowerCase().includes(f))) continue;
  const rs = rsByStruct.get(struct);
  const lines: string[] = [];
  if (!page) lines.push("  no C++ page registered");
  if (!rs) {
    lines.push(`  no Rust story for ${struct}`);
  }
  if (rs && page) {
    const t = fnString(rs.src, "title");
    const d = fnString(rs.src, "description");
    if (t !== null && m && t !== m.title) lines.push(`  title: rust "${t}"  cpp "${m.title}"`);
    if (m && (d ?? "") !== (m.desc ?? "")) lines.push(`  description: rust "${d ?? ""}"  cpp "${m.desc ?? ""}"`);

    const pageSrc = cppSrc.get(page.file)!;
    const lits = cppLiterals(pageSrc);
    const allLits = new Set<string>();
    for (const s of cppSrc.values()) for (const l of cppLiterals(s)) allLits.add(l);

    const rsSections = rsCall(rs.src, "section");
    const cppSections: string[] = [];
    for (const mm of pageSrc.matchAll(/StorySection\(\s*cx,\s*((?:u8)?"(?:[^"\\]|\\.)*"(?:\s*"(?:[^"\\]|\\.)*")*)/g)) {
      cppSections.push([...mm[1].matchAll(/"((?:[^"\\]|\\.)*)"/g)].map((p) => unescape(p[1])).join(""));
    }
    const missing = rsSections.filter((s) => !lits.has(s));
    const extra = cppSections.filter((s) => !rsSections.includes(s));
    if (missing.length) lines.push(`  sections missing: ${missing.map((s) => JSON.stringify(s)).join(", ")}`);
    if (extra.length) lines.push(`  sections not in Rust: ${extra.map((s) => JSON.stringify(s)).join(", ")}`);
    if (showAll && rsSections.length) lines.push(`  sections: ${rsSections.map((s) => JSON.stringify(s)).join(", ")}`);

    const texts = [...new Set(rsMethod(rs.src, visible))].filter((s) => s.trim().length > 1 && !rsSections.includes(s));
    const absent = texts.filter((s) => !lits.has(s));
    const here = absent.filter((s) => !allLits.has(s));
    const elsewhere = absent.filter((s) => allLits.has(s));
    if (here.length) lines.push(`  text missing: ${here.map((s) => JSON.stringify(s)).join(", ")}`);
    if (elsewhere.length && showAll)
      lines.push(`  text in another file: ${elsewhere.map((s) => JSON.stringify(s)).join(", ")}`);
  }
  if (lines.length) {
    problems++;
    say(`${slug} (${rs ? basename(rs.file) : "?"} -> ${page ? basename(page.file) : "?"})`);
    for (const l of lines) say(l);
  } else if (showAll) {
    say(`${slug}: same`);
  }
}

for (const s of rsOrder) {
  if (!cppOrder.includes(s)) {
    problems++;
    say(`${s}: in gallery.rs, no C++ page`);
  }
}

say(problems ? `\n${problems} page(s) with differences` : "no differences");
