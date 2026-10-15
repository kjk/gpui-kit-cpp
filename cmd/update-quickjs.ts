// Refresh the two tracked QuickJS-NG amalgam files from the pinned checkout.
//
// This is deliberately manual. Ordinary builds use src/quickjs/quickjs.{h,c}
// and never fetch or rewrite dependencies:
//
//   bun cmd/update-quickjs.ts
//   git apply src/quickjs/js_make_error.patch
//
// The generator overwrites src/quickjs and strips every comment, which drops
// the local JS_MakeError change. Apply the patch afterwards. If it does not
// apply, the upstream function moved: update the patch and apply that.
//
// The source checkout is disposable and gitignored; cmd/run.ts is the source
// of truth for the revision. Upstream's amalgam.js emits one C file with the
// public header embedded and optionally quickjs-libc. We keep the public API
// as the one separate header consumers need and omit quickjs-libc: gpui-shell
// owns its module loader, scheduler, filesystem, process and network surface.

import { existsSync, mkdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { dirname, join, resolve } from "node:path";

import { quickjsNg } from "./run.ts";

const root = resolve(import.meta.dir, "..");
const checkout = join(root, quickjsNg.checkout);
const outDir = join(root, quickjsNg.dir);

function run(args: string[], cwd = root): void {
  const result = Bun.spawnSync(args, { cwd, stdout: "inherit", stderr: "inherit" });
  if ((result.exitCode ?? 1) !== 0) {
    throw new Error(`${args.join(" ")} failed with exit ${result.exitCode ?? 1}`);
  }
}

function capture(args: string[], cwd = root): string {
  const result = Bun.spawnSync(args, { cwd, stdout: "pipe", stderr: "pipe" });
  if ((result.exitCode ?? 1) !== 0) {
    throw new Error(new TextDecoder().decode(result.stderr).trim() || `${args.join(" ")} failed`);
  }
  return new TextDecoder().decode(result.stdout).trim();
}

function syncCheckout(): void {
  if (!existsSync(join(checkout, ".git"))) {
    mkdirSync(dirname(checkout), { recursive: true });
    run(["git", "clone", quickjsNg.repo, checkout]);
  } else {
    run(["git", "fetch", "origin", "master"], checkout);
  }
  run(["git", "checkout", "--detach", "--force", quickjsNg.sha], checkout);
  const actual = capture(["git", "rev-parse", "HEAD"], checkout);
  if (actual !== quickjsNg.sha) {
    throw new Error(`QuickJS checkout is ${actual}, expected ${quickjsNg.sha}`);
  }
}

function read(name: string): string {
  return readFileSync(join(checkout, name), "utf8").replaceAll("\r\n", "\n").replaceAll("\r", "\n");
}

type StrippedSource = {
  text: string;
  comments: string[];
};

function stripCComments(src: string): StrippedSource {
  let text = "";
  const comments: string[] = [];
  let i = 0;
  while (i < src.length) {
    const c = src[i]!;
    const next = i + 1 < src.length ? src[i + 1]! : "";
    if (c === '"' || c === "'") {
      const quote = c;
      text += c;
      i++;
      while (i < src.length) {
        const ch = src[i]!;
        text += ch;
        i++;
        if (ch === "\\" && i < src.length) {
          text += src[i]!;
          i++;
          continue;
        }
        if (ch === quote || ch === "\n") {
          break;
        }
      }
      continue;
    }
    if (c === "/" && next === "/") {
      i += 2;
      const start = i;
      while (i < src.length && src[i] !== "\n") {
        i++;
      }
      comments.push(src.slice(start, i));
      continue;
    }
    if (c === "/" && next === "*") {
      i += 2;
      const start = i;
      while (i + 1 < src.length && !(src[i] === "*" && src[i + 1] === "/")) {
        i++;
      }
      const end = i;
      i = i + 1 < src.length ? i + 2 : src.length;
      comments.push(src.slice(start, end));
      let newlines = "";
      for (let k = start; k < i; k++) {
        if (src[k] === "\n") {
          newlines += "\n";
        }
      }
      text += newlines.length > 0 ? newlines : " ";
      continue;
    }
    text += c;
    i++;
  }
  return { text, comments };
}

function normalizeNotice(comment: string): string {
  const lines = comment.split("\n").map((line) => line.replace(/^\s*\* ?/, "").replace(/[ \t]+$/g, ""));
  while (lines.length > 0 && lines[0] === "") {
    lines.shift();
  }
  while (lines.length > 0 && lines[lines.length - 1] === "") {
    lines.pop();
  }
  return lines.join("\n");
}

function vendorPreamble(comments: string[]): string {
  const notices: string[] = [];
  const seen = new Set<string>();
  for (const comment of comments) {
    if (!comment.includes("Permission is hereby granted") && !comment.includes("UNICODE LICENSE V3")) {
      continue;
    }
    const notice = normalizeNotice(comment);
    if (notice.length === 0 || seen.has(notice)) {
      continue;
    }
    seen.add(notice);
    notices.push(notice);
  }
  const lines = [
    "#ifndef GPUI_QUICKJS_NG_VENDOR_METADATA",
    "#define GPUI_QUICKJS_NG_VENDOR_METADATA 1",
    `#define GPUI_QUICKJS_NG_VENDOR_REPOSITORY ${JSON.stringify(quickjsNg.repo)}`,
    `#define GPUI_QUICKJS_NG_VENDOR_VERSION ${JSON.stringify(quickjsNg.version)}`,
    `#define GPUI_QUICKJS_NG_VENDOR_SHA1 ${JSON.stringify(quickjsNg.sha)}`,
    "#if 0",
  ];
  for (const notice of notices) {
    for (const line of notice.split("\n")) {
      lines.push(JSON.stringify(line + "\n"));
    }
    lines.push(JSON.stringify("\n"));
  }
  lines.push("#endif", "#endif", "");
  return lines.join("\n");
}

function assertCommentFree(name: string, source: string): void {
  const remaining = stripCComments(source).comments.length;
  if (remaining !== 0) {
    throw new Error(`${name} still contains ${remaining} comment${remaining === 1 ? "" : "s"}`);
  }
}

function writeGenerated(path: string, source: string): void {
  rmSync(path, { force: true });
  writeFileSync(path, source, "utf8");
  const diskSource = readFileSync(path, "utf8");
  if (diskSource === source) {
    return;
  }
  let at = 0;
  while (at < source.length && at < diskSource.length && source[at] === diskSource[at]) {
    at++;
  }
  throw new Error(
    `${path} did not round-trip through disk: differs at ${at} (${source.length} generated, ${diskSource.length} read)`,
  );
}

function formatGenerated(source: string): string {
  const result: string[] = [];
  let previousWasEmpty = false;
  for (const rawLine of source.split("\n")) {
    const line = rawLine.replace(/[ \t]+$/g, "");
    const isEmpty = line.length === 0;
    if (isEmpty && previousWasEmpty) {
      continue;
    }
    result.push(line);
    previousWasEmpty = isEmpty;
  }
  while (result.length > 0 && result[result.length - 1] === "") {
    result.pop();
  }
  return result.join("\n") + "\n";
}

function replaceInclude(source: string, name: string, body: string): string {
  return source.replaceAll(`#include "${name}"`, body);
}

function generate(): void {
  mkdirSync(outDir, { recursive: true });

  const rawHeader = read("quickjs.h");

  // Same order as upstream amalgam.js. The public quickjs.h remains an
  // include between the internal declarations and implementation rather than
  // being pasted into this file, giving the repository exactly one .h and one
  // .c as its tracked QuickJS source.
  let rawSource =
    read("quickjs-c-atomics.h") +
    read("cutils.h") +
    read("dtoa.h") +
    read("list.h") +
    read("libunicode.h") +
    read("libregexp.h") +
    read("libunicode-table.h") +
    '#include "quickjs.h"\n' +
    read("quickjs.c") +
    read("dtoa.c") +
    read("libregexp.c") +
    read("libunicode.c");

  rawSource = replaceInclude(rawSource, "quickjs-atom.h", read("quickjs-atom.h"));
  rawSource = replaceInclude(rawSource, "quickjs-opcode.h", read("quickjs-opcode.h"));
  rawSource = replaceInclude(rawSource, "libregexp-opcode.h", read("libregexp-opcode.h"));
  rawSource = replaceInclude(rawSource, "builtin-array-fromasync.h", read("builtin-array-fromasync.h"));
  rawSource = replaceInclude(rawSource, "builtin-iterator-zip.h", read("builtin-iterator-zip.h"));
  rawSource = replaceInclude(rawSource, "builtin-iterator-zip-keyed.h", read("builtin-iterator-zip-keyed.h"));

  // Internal quoted includes have all been pasted. Keep only our one public
  // include, inserted after stripping so it cannot be removed here.
  rawSource = rawSource.replace(/^\s*#\s*include\s+"[^"]+"\s*$/gm, "");
  const strippedHeader = stripCComments(rawHeader);
  const strippedSource = stripCComments(rawSource);
  const header = formatGenerated(
    vendorPreamble([...strippedHeader.comments, ...strippedSource.comments]) + strippedHeader.text,
  );
  const source = formatGenerated('#define QUICKJS_NG_BUILD 1\n#include "quickjs.h"\n' + strippedSource.text);

  assertCommentFree("src/quickjs/quickjs.h", header);
  assertCommentFree("src/quickjs/quickjs.c", source);

  const headerPath = join(outDir, "quickjs.h");
  const sourcePath = join(outDir, "quickjs.c");
  writeGenerated(headerPath, header);
  writeGenerated(sourcePath, source);
  console.log(
    `generated src/quickjs/quickjs.h (${Buffer.byteLength(header, "utf8")} bytes) and ` +
      `quickjs.c (${Buffer.byteLength(source, "utf8")} bytes) from ${quickjsNg.sha}`,
  );
}

syncCheckout();
generate();
