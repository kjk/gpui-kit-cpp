// Compile the Windows GPU backend's four Shader Model 5 entry points once and
// embed their DXBC in a checked-in C++ source file. Ordinary builds never run
// fxc; cmd/build.ts compares the HLSL hash recorded in the generated file and
// tells the developer to run this script when it is stale.
//
//   bun cmd/update-win-shaders.ts
//   bun cmd/update-win-shaders.ts --check
//   bun cmd/update-win-shaders.ts --force  # compiler/encoding recipe changed

import { existsSync, mkdtempSync, readFileSync, readdirSync, rmSync, statSync, writeFileSync } from "node:fs";
import { join, resolve } from "node:path";
import { tmpdir } from "node:os";

const root = resolve(import.meta.dir, "..");
const hlslPath = join(root, "src/gpui/paintgpu_win.hlsl");
const cppPath = join(root, "src/gpui/paintgpu_shaders_win.cpp");
const check = process.argv.includes("--check");
const force = process.argv.includes("--force");

type Shader = {
  symbol: string;
  entry: string;
  target: "vs_5_0" | "ps_5_0";
};

const shaders: Shader[] = [
  { symbol: "VSQuad", entry: "VSQuad", target: "vs_5_0" },
  { symbol: "PSQuad", entry: "PSQuad", target: "ps_5_0" },
  { symbol: "VSTri", entry: "VSTri", target: "vs_5_0" },
  { symbol: "PSTri", entry: "PSTri", target: "ps_5_0" },
];

function die(message: string): never {
  console.error(message);
  process.exit(1);
}

function isFile(path: string): boolean {
  try {
    return statSync(path).isFile();
  } catch {
    return false;
  }
}

function findFxc(): string {
  const onPath = Bun.which("fxc.exe") ?? Bun.which("fxc");
  if (onPath) {
    return onPath;
  }
  const programFilesX86 = process.env["ProgramFiles(x86)"] ?? "C:/Program Files (x86)";
  const bin = join(programFilesX86, "Windows Kits/10/bin");
  if (!existsSync(bin)) {
    die("fxc.exe was not found; install the Windows 10/11 SDK");
  }
  const versions = readdirSync(bin, { withFileTypes: true })
    .filter((e) => e.isDirectory() && /^\d+\.\d+/.test(e.name))
    .map((e) => e.name)
    .sort((a, b) => b.localeCompare(a, undefined, { numeric: true }));
  for (const version of versions) {
    for (const arch of ["x64", "x86"]) {
      const candidate = join(bin, version, arch, "fxc.exe");
      if (isFile(candidate)) {
        return candidate;
      }
    }
  }
  die("fxc.exe was not found under the Windows SDK bin directory");
}

function sourceHash(source: Uint8Array): string {
  return new Bun.CryptoHasher("sha256").update(source).digest("hex");
}

// basE95: the basE91 bit-packing scheme generalized to every printable ASCII
// character (space through '~'), the largest ASCII alphabet that stays
// readable and source-safe in a C++ raw string. Two digits hold either 13
// bits, or 14 bits when the low 13 are in the 833-code spare range. It
// approaches 1.22 source bytes per binary byte, versus 1.25 for base85 and
// 1.33 for base64.
function encodeBase95(bytes: Uint8Array): string {
  let bits = 0;
  let bitCount = 0;
  let encoded = "";
  for (const byte of bytes) {
    bits |= byte << bitCount;
    bitCount += 8;
    if (bitCount > 13) {
      let value = bits & 8191;
      if (value > 832) {
        bits >>>= 13;
        bitCount -= 13;
      } else {
        value = bits & 16383;
        bits >>>= 14;
        bitCount -= 14;
      }
      encoded += String.fromCharCode(32 + (value % 95), 32 + Math.floor(value / 95));
    }
  }
  if (bitCount > 0) {
    encoded += String.fromCharCode(32 + (bits % 95));
    if (bitCount > 7 || bits > 94) {
      encoded += String.fromCharCode(32 + Math.floor(bits / 95));
    }
  }
  return encoded;
}

function decodeBase95(encoded: string, expected: number): Uint8Array {
  const decoded = new Uint8Array(expected);
  let bits = 0;
  let bitCount = 0;
  let pending = -1;
  let at = 0;
  for (let i = 0; i < encoded.length; i++) {
    const digit = encoded.charCodeAt(i) - 32;
    if (pending < 0) {
      pending = digit;
      continue;
    }
    const value = pending + digit * 95;
    bits |= value << bitCount;
    bitCount += (value & 8191) > 832 ? 13 : 14;
    while (bitCount >= 8) {
      if (at >= expected) {
        die("internal basE94 round-trip overflow");
      }
      decoded[at++] = bits & 255;
      bits >>>= 8;
      bitCount -= 8;
    }
    pending = -1;
  }
  if (pending >= 0) {
    if (at >= expected) {
      die("internal basE94 round-trip overflow");
    }
    decoded[at++] = (bits | (pending << bitCount)) & 255;
  }
  if (at !== expected) {
    die(`internal basE94 round-trip size mismatch: ${at} != ${expected}`);
  }
  return decoded;
}

function wrap(encoded: string): string {
  const lines: string[] = [];
  for (let i = 0; i < encoded.length;) {
    let end = Math.min(i + 100, encoded.length);
    // Space is a digit. Pick a nearby boundary that keeps it from becoming
    // trailing source whitespace; the last chunk meets the raw terminator on
    // the same line for the same reason.
    while (end < encoded.length && end > i && encoded.charCodeAt(end - 1) === 32) {
      end--;
    }
    if (end === i) {
      end = Math.min(i + 100, encoded.length);
      while (end < encoded.length && encoded.charCodeAt(end - 1) === 32) {
        end++;
      }
    }
    // A non-alphabet sentinel keeps arbitrary payload from looking like a
    // preprocessor directive to line-oriented amalgamation passes. The C++
    // decoder removes the first dot on every physical payload line.
    lines.push(`.${encoded.slice(i, end)}`);
    i = end;
  }
  return lines.join("\n");
}

function rawDelimiter(payloads: string[]): string {
  for (let i = 0; i < 1000; i++) {
    const delimiter = i === 0 ? "GPUI95" : `GPUI95_${i}`;
    if (delimiter.length > 16) {
      break;
    }
    const terminator = `)${delimiter}\"`;
    if (payloads.every((payload) => !payload.includes(terminator))) {
      return delimiter;
    }
  }
  die("could not find a collision-free C++ raw-string delimiter");
}

function compile(fxc: string, temp: string, shader: Shader): Uint8Array {
  const out = join(temp, `${shader.entry}.dxbc`);
  const args = [fxc, "/nologo", "/T", shader.target, "/E", shader.entry, "/O3", "/WX", "/Fo", out, hlslPath];
  const result = Bun.spawnSync(args, { stdout: "pipe", stderr: "pipe" });
  if ((result.exitCode ?? 1) !== 0) {
    const stdout = new TextDecoder().decode(result.stdout).trim();
    const stderr = new TextDecoder().decode(result.stderr).trim();
    die([`fxc failed for ${shader.entry}/${shader.target}`, stdout, stderr].filter(Boolean).join("\n"));
  }
  return readFileSync(out);
}

const source = readFileSync(hlslPath);
const hash = sourceHash(source);
const old = existsSync(cppPath) ? readFileSync(cppPath, "utf8") : "";
const recorded = old.match(/source-sha256:\s*([0-9a-f]{64})/);
const hashCurrent = recorded?.[1] === hash;
if (hashCurrent && !force) {
  console.log("Windows shader bytecode is current");
  process.exit(0);
}
if (check) {
  die("src/gpui/paintgpu_shaders_win.cpp is stale; run bun cmd/update-win-shaders.ts");
}
if (process.platform !== "win32") {
  die("Windows shader generation requires fxc.exe and must run on Windows");
}

const fxc = findFxc();
const temp = mkdtempSync(join(tmpdir(), "gpui-win-shaders-"));
try {
  const compiled = shaders.map((shader) => ({ shader, bytes: compile(fxc, temp, shader) }));
  const payloads = compiled.map(({ bytes, shader }) => {
    const encoded = encodeBase95(bytes);
    const decoded = decodeBase95(encoded, bytes.length);
    if (!bytes.every((byte, i) => byte === decoded[i])) {
      die(`internal basE95 round-trip mismatch for ${shader.entry}`);
    }
    return encoded;
  });
  const delimiter = rawDelimiter(payloads);
  const definitions = compiled
    .map(({ shader, bytes }, i) => {
      const name = `kShader${shader.symbol}`;
      return `extern const char ${name}95[] = R\"${delimiter}(\n${wrap(payloads[i])})${delimiter}\";
extern const int ${name}Size = ${bytes.length};
uint8_t ${name}Bytes[${bytes.length}] = {};`;
    })
    .join("\n\n");
  const generated = `/* Generated by cmd/update-win-shaders.ts. Do not edit.
   source-sha256: ${hash}
   encoding: basE95 over printable ASCII space through ~ */

#include "gpui/paintgpu.h"

#if WIN_BACKEND_GPU
namespace gpui {
namespace gpuw {

${definitions}

} // namespace gpuw
} // namespace gpui
#endif
`;
  if (old === generated) {
    console.log("Windows shader bytecode is current");
  } else {
    writeFileSync(cppPath, generated);
    console.log(`wrote src/gpui/paintgpu_shaders_win.cpp with ${compiled.length} shaders using ${fxc}`);
  }
} finally {
  rmSync(temp, { recursive: true, force: true });
}
