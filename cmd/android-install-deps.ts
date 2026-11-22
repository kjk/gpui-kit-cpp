// Install the pinned Android NDK that `bun cmd/mobile-build.ts -android` uses,
// beside any NDKs Android Studio installed, after checking the archive's SHA-1.
//
//   bun cmd/android-install-deps.ts                  install if missing
//   bun cmd/android-install-deps.ts -check           exit 1 if missing
//   bun cmd/android-install-deps.ts -sdk-root=DIR    install under DIR/ndk/
//
// The SDK root defaults to $ANDROID_HOME, then $ANDROID_SDK_ROOT, then
// %LOCALAPPDATA%\Android\Sdk. Windows only: the pinned archive and checksum
// are the Windows ones, and that is the host CI cross-compiles Android on.

import {
  closeSync,
  cpSync,
  existsSync,
  mkdirSync,
  openSync,
  readFileSync,
  readSync,
  renameSync,
  rmSync,
} from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join, resolve } from "node:path";

/** The NDK mobile-build.ts compiles with; its source.properties Pkg.Revision. */
export const androidNdkVersion = "30.0.16248370";
const archiveRevision = "r30";
const archiveSha1 = "9bf167a1985fa7d4a036186b78f702eab9179408";
const archiveUrl = `https://dl.google.com/android/repository/android-ndk-${archiveRevision}-windows.zip`;

/** Whether `dir` holds exactly the pinned NDK. */
export function isPinnedNdk(dir: string): boolean {
  const properties = join(dir, "source.properties");
  if (!existsSync(properties)) {
    return false;
  }
  const text = readFileSync(properties, "utf8");
  return new RegExp(`Pkg\\.Revision\\s*=\\s*${androidNdkVersion.replaceAll(".", "\\.")}\\s*$`, "m").test(text);
}

/** SDK roots to look under, most specific first. */
export function androidSdkRoots(): string[] {
  return [
    process.env.ANDROID_HOME,
    process.env.ANDROID_SDK_ROOT,
    process.env.LOCALAPPDATA ? join(process.env.LOCALAPPDATA, "Android", "Sdk") : undefined,
  ].filter((value): value is string => !!value);
}

function die(message: string): never {
  console.error(message);
  process.exit(1);
}

async function download(url: string, path: string): Promise<void> {
  const response = await fetch(url);
  if (!response.ok || !response.body) {
    throw new Error(`downloading ${url} failed: HTTP ${response.status}`);
  }
  const total = Number(response.headers.get("content-length") ?? 0);
  const writer = Bun.file(path).writer();
  let got = 0;
  let shown = -1;
  for await (const chunk of response.body) {
    writer.write(chunk);
    got += chunk.length;
    const percent = total > 0 ? Math.floor((got * 100) / total) : -1;
    if (percent !== shown && percent % 5 === 0) {
      shown = percent;
      process.stdout.write(`\r  ${percent}% of ${(total / 1048576).toFixed(0)} MB`);
    }
  }
  await writer.end();
  if (total > 0) {
    process.stdout.write("\n");
  }
}

function sha1OfFile(path: string): string {
  const hasher = new Bun.CryptoHasher("sha1");
  // The archive is about 1 GB, so hash it in pieces rather than reading it whole.
  const fd = openSync(path, "r");
  const buf = Buffer.alloc(8 * 1024 * 1024);
  try {
    for (;;) {
      const n = readSync(fd, buf, 0, buf.length, null);
      if (n <= 0) {
        break;
      }
      hasher.update(buf.subarray(0, n));
    }
  } finally {
    closeSync(fd);
  }
  return hasher.digest("hex");
}

async function main(): Promise<void> {
  let sdkRoot = "";
  let check = false;
  for (const arg of Bun.argv.slice(2)) {
    if (arg === "-check" || arg === "--check") {
      check = true;
    } else if (arg.startsWith("-sdk-root=")) {
      sdkRoot = arg.slice("-sdk-root=".length);
    } else {
      die(`unknown argument ${arg}\nusage: bun cmd/android-install-deps.ts [-check] [-sdk-root=DIR]`);
    }
  }
  if (!sdkRoot) {
    sdkRoot = androidSdkRoots()[0] ?? "";
  }
  if (!sdkRoot) {
    die("no Android SDK root: set ANDROID_HOME or pass -sdk-root=DIR");
  }
  const target = join(resolve(sdkRoot), "ndk", androidNdkVersion);
  if (isPinnedNdk(target)) {
    console.log(`Android NDK ${androidNdkVersion} is installed at ${target}`);
    return;
  }
  if (check) {
    die(`Android NDK ${androidNdkVersion} is missing. Run bun cmd/android-install-deps.ts without -check.`);
  }
  if (process.platform !== "win32") {
    die("cmd/android-install-deps.ts installs the Windows NDK only; on this host install NDK " + androidNdkVersion);
  }
  if (existsSync(target)) {
    die(`Refusing to replace unexpected existing directory: ${target}`);
  }

  const tempRoot = join(tmpdir(), `gpui-android-ndk-${crypto.randomUUID().replaceAll("-", "")}`);
  const archive = join(tempRoot, `android-ndk-${archiveRevision}-windows.zip`);
  const unpack = join(tempRoot, "unpack");
  mkdirSync(unpack, { recursive: true });
  try {
    console.log(`Downloading Android NDK ${androidNdkVersion} (${archiveRevision})...`);
    await download(archiveUrl, archive);
    const actual = sha1OfFile(archive);
    if (actual !== archiveSha1) {
      throw new Error(`Android NDK archive checksum mismatch: expected ${archiveSha1}, got ${actual}`);
    }
    // Windows' bsdtar reads zip archives.
    const tar = Bun.spawnSync(["tar.exe", "-xf", archive, "-C", unpack], { stdout: "inherit", stderr: "inherit" });
    if (tar.exitCode !== 0) {
      throw new Error(`tar failed to extract Android NDK ${androidNdkVersion}`);
    }
    const extracted = join(unpack, `android-ndk-${archiveRevision}`);
    if (!existsSync(join(extracted, "source.properties"))) {
      throw new Error(`Downloaded archive did not contain android-ndk-${archiveRevision}`);
    }
    mkdirSync(dirname(target), { recursive: true });
    try {
      renameSync(extracted, target);
    } catch {
      // The temp directory and the SDK can be on different volumes.
      cpSync(extracted, target, { recursive: true });
    }
    console.log(`Installed Android NDK ${androidNdkVersion} at ${target}`);
  } finally {
    rmSync(tempRoot, { recursive: true, force: true });
  }
}

if (import.meta.main) {
  try {
    await main();
  } catch (e) {
    die(e instanceof Error ? e.message : String(e));
  }
}
