#ifndef GPUI_SHELL_BUNDLE_H_
#define GPUI_SHELL_BUNDLE_H_

#include "base.h"

// crates/shell/src/host/bundle.rs. The command line lives in host.h; this is
// the packaging half `gpui-shell bundle` runs.

namespace gpui::shell {

struct PluginManifest;
struct Invocation;

enum class BundlePlatform : uint8_t {
    MacOS,
    Linux,
    Windows,
};

bool BundlePlatformParse(Str value, BundlePlatform* out);
// Rust's `Platform::host`: macOS, Windows, and Linux for every other target.
BundlePlatform BundleHostPlatform();

// `bundle::create`. `report` is owned. False leaves the sentence in `error`.
bool ShellBundle(Str directory, const Invocation& invocation, Str* report,
                 Str* error);

struct BundleNames {
    Str display;
    Str command;
    Str appBundle;
    Str archiveStem;

    void Free() {
        StrFree(display);
        StrFree(command);
        StrFree(appBundle);
        StrFree(archiveStem);
        display = command = appBundle = archiveStem = {};
    }
};

bool BundleNamesFrom(const PluginManifest& manifest, BundlePlatform platform,
                     BundleNames* out);
// False when the cleaned name is empty. `out` is owned.
bool BundleFileName(Str name, Str* out);
Str BundleInfoPlist(const PluginManifest& manifest, const BundleNames& names,
                    bool hasIcon);

struct BundleScriptValues {
    Str id;
    Str name;
    Str command;
    Str appVersion;
    Str runtimeVersion;
    Str iconSize;
    Str archive;
    Str archiveSha256;
    Str downloadUrl;
    Str stem;
};

Str BundleShellScript(Str templ, const BundleScriptValues& values);
bool BundleWriteIcns(const uint8_t* rgba, int width, Str* out, Str* error);
bool BundleApplicationPaths(Str root, Str prefix, Str exclude, Str* paths,
                            Str* error);
bool BundleWriteSampleArchives(Str directory, Str* error);
bool BundleTarMode(Str tarGz, Str path, uint32_t* mode, Str* error);
bool BundleZipEntryCount(Str zipPath, int* count, Str* error);

using BundleDownloadFn = bool (*)(Str url, void* data, Str* bytes, Str* error);
bool BundleWindowsRuntimeFrom(Str cache, Str version, BundleDownloadFn download,
                              void* data, Str* executable, Str* error);
bool BundleZipEntry(Str archive, Str name, Str* out, Str* error);
// A release zip holding `gpui-shell-<target>/gpui-shell.exe`, for the
// runtime test. `zip` is owned.
bool BundleReleaseZip(Str executable, Str* zip, Str* error);
// Writes the GUI subsystem into a PE image the caller owns.
bool BundleMarkAsGuiProgram(Str bytes, Str* error);

} // namespace gpui::shell

#endif // GPUI_SHELL_BUNDLE_H_
