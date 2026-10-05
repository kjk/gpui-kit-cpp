// Translate the pinned crate's generated trie, pair table and safe-pair set.
// Usage: bun cmd/update-unicode-linebreak.ts [unpacked crate directory]
// Ordinary builds use checked-in tables and never download the crate.
import { readFileSync, writeFileSync } from "node:fs";
import { resolve, join } from "node:path";
import { unicodeLinebreak } from "./run.ts";

const root = resolve(import.meta.dir, "..");
const source = resolve(
  Bun.argv[2] ?? join(root, `.work/unicode-linebreak/unicode-linebreak-${unicodeLinebreak.version}`),
);
const cargo = readFileSync(join(source, "Cargo.toml"), "utf8");
if (!cargo.includes(`version = "${unicodeLinebreak.version}"`)) throw new Error("Wrong crate version");
const rust = readFileSync(join(source, "src/tables.rs"), "utf8");
const shared = readFileSync(join(source, "src/shared.rs"), "utf8");
const classes = [...shared.matchAll(/^    (\w+),$/gm)].map((m) => m[1]);
if (classes.length !== 43) throw new Error("Unexpected break classes");
const aliases = new Map<string, number>();
for (const m of shared.matchAll(/(\w+) as (\w+)/g)) aliases.set(m[2], classes.indexOf(m[1]));
const array = (name: string) => {
  const match = rust.match(new RegExp(`static ${name}: [^=]+ = (\\[[\\s\\S]*?\\]);`));
  if (!match) throw new Error(`Missing ${name}`);
  return match[1];
};
const numeric = (name: string) => [...array(name).matchAll(/\d+/g)].map((m) => Number(m[0]));
const index = numeric("BREAK_PROP_TRIE_INDEX");
const data = [...array("BREAK_PROP_TRIE_DATA").matchAll(/\b[A-Z][A-Z0-9]*\b/g)].map((m) => {
  const value = aliases.get(m[0]);
  if (value === undefined || value < 0) throw new Error(`Unknown class ${m[0]}`);
  return value;
});
const pairs = numeric("PAIR_TABLE");
if (index.length !== 2844 || data.length !== 12996 || pairs.length !== 53 * 44)
  throw new Error("Unexpected table size");
const safe = Array<bigint>(43).fill(0n);
for (const m of rust.matchAll(/\(([A-Z][A-Z0-9]*),\s*([A-Z][A-Z0-9]*)\)/g)) {
  const a = aliases.get(m[1]);
  const b = aliases.get(m[2]);
  if (a === undefined || b === undefined) throw new Error("Unknown safe pair");
  safe[a] |= 1n << BigInt(b);
}
if (safe.every((v) => v === 0n)) throw new Error("Missing safe pairs");
// Upstream lists UNSAFE pairs with !matches!, rather than safe pairs.
if (!rust.includes("!matches!((a, b),")) throw new Error("Unexpected safe-pair predicate");
const allClasses = (1n << 43n) - 1n;
for (let i = 0; i < safe.length; i++) safe[i] = allClasses ^ safe[i];
const rows = (values: number[], size: number) => {
  const result: string[] = [];
  for (let i = 0; i < values.length; i += size) result.push(`    ${values.slice(i, i + size).join(", ")},`);
  return result.join("\n");
};
const high = rust.match(/BREAK_PROP_TRIE_HIGH_START: u32 = (\d+)/)?.[1];
if (!high) throw new Error("Missing high start");
const output = `// Generated from unicode-linebreak ${unicodeLinebreak.version} src/tables.rs.
// Apache-2.0. Regenerate with bun cmd/update-unicode-linebreak.ts.
#pragma once
#include "base.h"
namespace unicode_linebreak {
constexpr uint32_t kBreakPropTrieHighStart = ${high};
static const uint16_t kBreakPropTrieIndex[${index.length}] = {
${rows(index, 12)}
};
static const uint8_t kBreakPropTrieData[${data.length}] = {
${rows(data, 20)}
};
static const uint8_t kPairTable[53][44] = {
${Array.from({ length: 53 }, (_, i) => `    {${pairs.slice(i * 44, (i + 1) * 44).join(", ")}},`).join("\n")}
};
static const uint64_t kSafePairs[43] = {
${safe.map((v) => `    0x${v.toString(16)}ULL,`).join("\n")}
};
} // namespace unicode_linebreak
`;
writeFileSync(join(root, "src/unicode-linebreak/tables.h"), output);
console.log(
  `unicode-linebreak ${unicodeLinebreak.version}: ${index.length} indices, ${data.length} properties, 53 states`,
);

// Optional Unicode test corpus, as used by upstream tests/test_default.rs.
if (Bun.argv[3]) {
  const corpus = readFileSync(resolve(Bun.argv[3]), "utf8");
  if (!corpus.startsWith("# LineBreakTest-15.0.0.txt")) throw new Error("Wrong Unicode test version");
  const escaped = (bytes: Uint8Array) => [...bytes].map((b) => `\\x${b.toString(16).padStart(2, "0")}`).join("");
  const cases: string[] = [];
  for (const line of corpus.split("\n")) {
    if (!line.trim() || line.startsWith("#")) continue;
    // Same tailored-rule exclusions as the Rust conformance test.
    if (line.includes("[30.22]") || line.includes("[999.0]")) continue;
    const items = line.split("#")[0].trim().split(/\s+/);
    let text = "";
    const offsets: number[] = [];
    for (let i = 1; i < items.length; i += 2) {
      text += String.fromCodePoint(parseInt(items[i], 16));
      if (items[i + 1] === "÷") offsets.push(Buffer.byteLength(text));
      else if (items[i + 1] !== "×") throw new Error("Bad test marker");
    }
    if (offsets.some((n) => n > 255)) throw new Error("Test offset exceeds byte");
    cases.push(`    {"${escaped(Buffer.from(text))}", "${escaped(Uint8Array.from(offsets))}"},`);
  }
  writeFileSync(
    join(root, "tests/UnicodeLinebreakData.h"),
    `// Generated from Unicode LineBreakTest-15.0.0.txt, used by unicode-linebreak.
// Copyright 2022 Unicode, Inc. Terms: https://www.unicode.org/terms_of_use.html
// Regenerate: bun cmd/update-unicode-linebreak.ts <crate> <LineBreakTest.txt>
// Skips [30.22] and [999.0], exactly like upstream tests/test_default.rs.
static const struct { const char* text; const char* offsets; } kLineBreakCases[] = {
${cases.join("\n")}
};
`,
  );
  console.log(`${cases.length} Unicode conformance cases`);
}
