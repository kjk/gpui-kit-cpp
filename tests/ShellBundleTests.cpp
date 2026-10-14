// Ports of crates/shell/src/host/bundle.rs's tests.

#include "Test.h"

#include <stdio.h>
#include <string.h>

namespace gpui::shell {
extern const char kBundleLinuxLauncherSh[];
extern const char kBundleLinuxInstallSh[];
extern const char kBundleMacosLauncherSh[];
} // namespace gpui::shell

namespace {

bool EndsWith(Str value, Str suffix) {
    if (len(suffix) > len(value) || !value.s || !suffix.s) return false;
    return memcmp(value.s + len(value) - len(suffix), suffix.s,
                  (size_t)len(suffix)) == 0;
}

uint32_t Be32(const char* p) {
    const unsigned char* b = (const unsigned char*)p;
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
           ((uint32_t)b[2] << 8) | b[3];
}

bool PlaceholderLeft(Str script) {
    int at = 0;
    bool inside = false;
    for (int i = 0; i < len(script); i++) {
        if (script.s[i] != '@') continue;
        if (!inside) {
            inside = true;
            at = i + 1;
            continue;
        }
        int n = i - at;
        bool name = n > 0;
        for (int c = 0; name && c < n; c++) {
            unsigned char ch = (unsigned char)script.s[at + c];
            bool ok = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                      (ch >= '0' && ch <= '9') || ch == '_';
            if (!ok) name = false;
        }
        if (name) return true;
        inside = false;
    }
    return false;
}

shell::BundleScriptValues Values(Str name) {
    shell::BundleScriptValues values;
    values.id = StrL("com.example.its");
    values.name = name;
    values.command = StrL("its");
    values.appVersion = StrL("1.0.0");
    values.runtimeVersion = StrL("0.8.0");
    values.iconSize = StrL("256");
    values.archive = StrL("its-1.0.0-linux.tar.gz");
    values.archiveSha256 = StrL(
        "abababababababababababababababababababababababababababababababab");
    values.downloadUrl = StrL("");
    values.stem = StrL("its-1.0.0-linux");
    return values;
}

void FillManifest(shell::PluginManifest* manifest, Str name, Str id) {
    manifest->name = name;
    manifest->id = id;
    manifest->version = StrL("1.2.3-beta.1");
    manifest->entry = StrL("main.js");
}

void NamesComeFromTheManifest() {
    shell::PluginManifest manifest;
    FillManifest(&manifest, StrL("Folio Desk"), StrL("com.folio.desk"));
    shell::BundleNames names;
    utassert(
        shell::BundleNamesFrom(manifest, shell::BundlePlatform::Linux, &names));
    utassert(StrEq(names.display, StrL("Folio Desk")));
    utassert(StrEq(names.command, StrL("folio-desk")));
    utassert(StrEq(names.appBundle, StrL("Folio Desk.app")));
    utassert(StrEq(names.archiveStem, StrL("folio-desk-1.2.3-beta.1-linux")));
    names.Free();

    shell::PluginManifest clock;
    FillManifest(&clock, StrL("时钟"), StrL("com.example.clock"));
    utassert(
        shell::BundleNamesFrom(clock, shell::BundlePlatform::MacOS, &names));
    utassert(StrEq(names.command, StrL("clock")));
    utassert(StrEq(names.display, StrL("时钟")));
    names.Free();

    Str cleaned;
    utassert(shell::BundleFileName(StrL("A/B: C?"), &cleaned));
    utassert(StrEq(cleaned, StrL("A-B- C-")));
    StrFree(cleaned);
    utassert(!shell::BundleFileName(StrL(" .. "), &cleaned));
}

void TheInfoPlistNamesTheExecutableAndEscapesValues() {
    shell::PluginManifest manifest;
    FillManifest(&manifest, StrL("Tom & Jerry"), StrL("com.example.tom"));
    shell::BundleNames names;
    utassert(
        shell::BundleNamesFrom(manifest, shell::BundlePlatform::MacOS, &names));
    Str plist = shell::BundleInfoPlist(manifest, names, true);
    utassert(StrContains(
        plist, StrL("<key>CFBundleExecutable</key>\n  <string>Tom &amp; "
                    "Jerry</string>")));
    utassert(StrContains(plist, StrL("<key>CFBundleIdentifier</key>\n  "
                                     "<string>com.example.tom</string>")));
    utassert(StrContains(plist, StrL("<key>CFBundleVersion</key>\n  "
                                     "<string>1.2.3</string>")));
    utassert(StrContains(plist, StrL("<string>1.2.3-beta.1</string>")));
    utassert(StrContains(plist, StrL("<key>CFBundleIconFile</key>")));
    StrFree(plist);
    names.Free();
}

void ShellTemplatesQuoteEveryValueOnce() {
    shell::BundleScriptValues values = Values(StrL("It's @ID@"));
    Str script = shell::BundleShellScript(
        StrL("id=@ID@\nname=@NAME@\nsize=@ICON_SIZE@\nmail=me@example.com "
             "@UNKNOWN@\n"),
        values);
    utassert(
        StrEq(script, StrL("id='com.example.its'\nname='It'\\''s @ID@'\n"
                           "size='256'\nmail=me@example.com @UNKNOWN@\n")));
    StrFree(script);

    values = Values(StrL("A"));
    const char* templates[] = {shell::kBundleLinuxLauncherSh,
                               shell::kBundleLinuxInstallSh,
                               shell::kBundleMacosLauncherSh};
    for (const char* templ : templates) {
        script = shell::BundleShellScript(Str(templ), values);
        utassert(!PlaceholderLeft(script));
        StrFree(script);
    }
    script = shell::BundleShellScript(
        StrL("#!/bin/sh\n@RUNTIME_FUNCTIONS@\nexec x\n"), values);
    utassert(StrStartsWith(script, StrL("#!/bin/sh\n")));
    utassert(EndsWith(script, StrL("\nexec x\n")));
    utassert(!StrContains(script, StrL("@RUNTIME_FUNCTIONS@")));
    StrFree(script);
}

void AnIcnsFileHoldsEverySizeUpToTheSource() {
    uint8_t pixels[64 * 64 * 4];
    memset(pixels, 0, sizeof(pixels));
    Str icns;
    Str error = {};
    utassert(shell::BundleWriteIcns(pixels, 64, &icns, &error));
    utassert(len(icns) >= 8 && memcmp(icns.s, "icns", 4) == 0);
    utassert(Be32(icns.s + 4) == (uint32_t)len(icns));
    char kinds[32];
    int kindCount = 0;
    int at = 8;
    while (at + 8 <= len(icns)) {
        if (kindCount < 3) memcpy(kinds + kindCount * 4, icns.s + at, 4);
        kindCount++;
        at += (int)Be32(icns.s + at + 4);
    }
    utassert(kindCount == 3);
    utassert(memcmp(kinds, "icp4icp5icp6", 12) == 0);
    utassert(!error);
    StrFree(icns);
    StrFree(error);
}

#if !GPUI_OS_WASM
static bool Touch(const char* path) {
    FILE* file = fopen(path, "wb");
    if (!file) return false;
    fclose(file);
    return true;
}

void TheApplicationLeavesOutDevelopmentFilesAndTheOutput() {
    Str root = StrL("shell_bundle_tree");
    shell::DependencyRemoveTree(root);
    Str error = {};
    const char* dirs[] = {"shell_bundle_tree/lib",
                          "shell_bundle_tree/node_modules/x",
                          "shell_bundle_tree/.git", "shell_bundle_tree/dist"};
    for (const char* dir : dirs)
        utassert(shell::DependencyMakeDirectories(Str(dir), &error));
    const char* files[] = {"shell_bundle_tree/main.js",
                           "shell_bundle_tree/gpui-shell.json",
                           "shell_bundle_tree/gpui-kit.d.ts",
                           "shell_bundle_tree/jsconfig.json",
                           "shell_bundle_tree/AGENTS.md",
                           "shell_bundle_tree/lib/util.js",
                           "shell_bundle_tree/lib/gpui-kit.d.ts",
                           "shell_bundle_tree/lib/AGENTS.md",
                           "shell_bundle_tree/node_modules/x/index.js",
                           "shell_bundle_tree/.git/HEAD",
                           "shell_bundle_tree/dist/old.tar.gz"};
    for (const char* file : files) utassert(Touch(file));
    char canonical[kMaxPath];
    utassert(PlatCanonicalPath("shell_bundle_tree/dist", canonical, kMaxPath));
    Str paths;
    utassert(shell::BundleApplicationPaths(root, StrL("top/app"),
                                           Str(canonical), &paths, &error));
    utassert(StrEq(paths, StrL("top\n"
                               "top/app\n"
                               "top/app/gpui-shell.json\n"
                               "top/app/lib\n"
                               "top/app/lib/AGENTS.md\n"
                               "top/app/lib/util.js\n"
                               "top/app/main.js")));
    StrFree(paths);
    StrFree(error);
    shell::DependencyRemoveTree(root);
}

void ArchivesRoundTripWithTheirModes() {
    Str directory = StrL("shell_bundle_archive");
    shell::DependencyRemoveTree(directory);
    Str error = {};
    utassert(shell::DependencyMakeDirectories(directory, &error));
    utassert(shell::BundleWriteSampleArchives(directory, &error));
    uint32_t mode = 0;
    utassert(shell::BundleTarMode(StrL("shell_bundle_archive/bundle.tar.gz"),
                                  StrL("top/run"), &mode, &error));
    utassert(mode == 0755u);
    utassert(shell::BundleTarMode(StrL("shell_bundle_archive/bundle.tar.gz"),
                                  StrL("top/app/main.js"), &mode, &error));
    utassert(mode == 0644u);
    int count = 0;
    utassert(shell::BundleZipEntryCount(StrL("shell_bundle_archive/bundle.zip"),
                                        &count, &error));
    utassert(count == 4);
    utassert(!PlatFileExists("shell_bundle_archive/bundle.zip.partial"));
    StrFree(error);
    shell::DependencyRemoveTree(directory);
}

struct ReleaseDownload {
    Str zip;
    int calls = 0;
    Str url;
};

bool ServeRelease(Str url, void* data, Str* bytes, Str*) {
    ReleaseDownload* state = (ReleaseDownload*)data;
    state->calls++;
    StrFree(state->url);
    state->url = StrDup(url);
    if (bytes) *bytes = StrDup(state->zip);
    return true;
}

bool RefuseDownload(Str, void*, Str*, Str* error) {
    if (error && !*error)
        *error = StrDup(StrL("the cache should have answered"));
    return false;
}
#endif

void TheWindowsExecutableComesFromTheCacheOrTheReleaseZip() {
    unsigned char executable[0x200];
    memset(executable, 0, sizeof(executable));
    executable[0] = 'M';
    executable[1] = 'Z';
    executable[0x3c] = 0x80;
    memcpy(executable + 0x80, "PE\0\0", 4);
    executable[0x98] = 0x0b;
    executable[0x99] = 0x02;
    executable[0x98 + 68] = 3;
    Str image = Str((char*)executable, 0x200);
    Str zip;
    Str error = {};
    utassert(shell::BundleReleaseZip(image, &zip, &error));
    Str missing;
    utassert(
        !shell::BundleZipEntry(zip, StrL("missing.exe"), &missing, &error));
    Str script = StrDup(StrL("#!/bin/sh\n"));
    utassert(!shell::BundleMarkAsGuiProgram(script, &error));
    StrFree(script);

#if !GPUI_OS_WASM
    Str root = StrL("shell_bundle_runtime");
    shell::DependencyRemoveTree(root);
    ReleaseDownload state;
    state.zip = zip;
    Str cache = StrL("shell_bundle_runtime/cache/gpui-shell.exe");
    Str got;
    utassert(shell::BundleWindowsRuntimeFrom(cache, StrL("0.8.0"), ServeRelease,
                                             &state, &got, &error));
    utassert(state.calls == 1);
    utassert(EndsWith(state.url,
                      StrL("/v0.8.0/gpui-shell-x86_64-pc-windows-msvc.zip")));
    utassert(len(got) == 0x200 && memcmp(got.s, executable, 0x200) == 0);
    Str cached;
    utassert(shell::BundleWindowsRuntimeFrom(
        cache, StrL("0.8.0"), RefuseDownload, nullptr, &cached, &error));
    utassert(len(cached) == 0x200 && memcmp(cached.s, executable, 0x200) == 0);
    Str marked = StrDup(cached);
    utassert(shell::BundleMarkAsGuiProgram(marked, &error));
    utassert((unsigned char)marked.s[0x98 + 68] == 2);
    StrFree(marked);
    StrFree(cached);
    StrFree(got);
    StrFree(state.url);
    shell::DependencyRemoveTree(root);
#else
    Str marked = StrDup(image);
    utassert(shell::BundleMarkAsGuiProgram(marked, &error));
    utassert((unsigned char)marked.s[0x98 + 68] == 2);
    StrFree(marked);
#endif
    StrFree(zip);
    StrFree(error);
}

} // namespace

void TestShellBundle() {
    TestSuite("bundle");
    NamesComeFromTheManifest();
    TheInfoPlistNamesTheExecutableAndEscapesValues();
    ShellTemplatesQuoteEveryValueOnce();
    AnIcnsFileHoldsEverySizeUpToTheSource();
#if !GPUI_OS_WASM
    TheApplicationLeavesOutDevelopmentFilesAndTheOutput();
    ArchivesRoundTripWithTheirModes();
#endif
    TheWindowsExecutableComesFromTheCacheOrTheReleaseZip();
}
