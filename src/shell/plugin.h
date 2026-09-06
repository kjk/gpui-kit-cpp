#ifndef GPUI_SHELL_PLUGIN_H_
#define GPUI_SHELL_PLUGIN_H_

#include "shell/assets.h"
#include "shell/root.h"

namespace gpui::shell {

constexpr const char* kShellVersion = "0.6.0";
constexpr const char* kShellManifestFile = "gpui-shell.json";
constexpr int kShellMaxManifestBytes = 1024 * 1024;
// `default_dependency_entry` in crates/shell/src/plugin.rs.
constexpr const char* kGitDependencyDefaultEntry = "index.js";

struct PluginHttpGrant {
    Str scheme;
    Str host;
    uint16_t port = 0;
    bool hasPort = false;
    Vec<Str> methods;
    Vec<Str> paths;
    Vec<Str> pathPrefixes;
};

// One JavaScript package fetched from Git before an application starts.
struct GitDependency {
    Str name;
    Str git;
    Str branch;
    Str tag;
    Str entry;
    Str reference;
    bool packageEntry = false;
};

struct PluginManifest {
    Arena* arena = nullptr;
    Str id;
    Str name;
    Str version;
    Str shellVersion;
    Str entry;
    // Sorted by name, the order Rust's BTreeMap iterates.
    Vec<GitDependency> dependencies;
    Vec<Str> readRoots;
    Vec<Str> writeRoots;
    Vec<Str> execute;
    Vec<Str> networkHosts;
    Vec<PluginHttpGrant*> http;
    bool executeUnrestricted = false;
    bool storage = true;
    bool clipboardRead = false;
    bool clipboardWrite = false;
    bool exit = false;

    PluginManifest();
    PluginManifest(const PluginManifest&) = delete;
    PluginManifest& operator=(const PluginManifest&) = delete;
    ~PluginManifest();

    Capabilities Grant(Str pluginDirectory, Str dataDirectory) const;
};

bool PluginManifestParse(Str source, PluginManifest* out,
                         ShellError* error = nullptr);
bool PluginManifestRead(Str directory, PluginManifest* out,
                        ShellError* error = nullptr);
void PluginManifestSchema(StrBuilder* out);

// Per-user application storage follows gpui-shell's Rust layout:
// <data-home>/gpui-shell/apps/<path identity>.
Str ShellDataHome();
Str ShellBundleIdForPath(Str root);
Str ShellAppDataDirectory(Str id, Arena* arena, ShellError* error = nullptr);

struct PluginDiscovery {
    PluginManifest* manifest = nullptr;
    Str root;
    Str error;
};

struct Plugin {
    PluginManifest* manifest = nullptr;
    Str root;
    Str dataDirectory;
    Str storePath;
    Policy* policy = nullptr;
    ShellRuntime* runtime = nullptr;
    Entity<ScriptView> view = {};
    AppAssets* assets = nullptr;
    App* app = nullptr;
};

using PluginAuthorizeFn = bool (*)(const PluginManifest* manifest, void* data);

class PluginManager {
  public:
    PluginManager();
    explicit PluginManager(Str directory);
    PluginManager(const PluginManager&) = delete;
    PluginManager& operator=(const PluginManager&) = delete;
    ~PluginManager();

    PluginManager& AddDirectory(Str directory);
    PluginManager& DataHome(Str directory);
    const Vec<PluginDiscovery>& Discover();
    bool Load(ShellRuntime* runtime, Str id, PluginAuthorizeFn authorize,
              void* authorizeData, Window* window, App* app,
              ShellError* error = nullptr);
    bool Unload(Str id, App* app);
    const Plugin* Loaded(Str id) const;
    Str DataDirectory(Str id, Arena* arena) const;

  private:
    Vec<Str> directories;
    Str dataHome;
    Vec<PluginDiscovery> catalog;
    Vec<Plugin*> loaded;
    bool discovered = false;

    void ClearCatalog();
};

// The single-application host facade. A manifest selects the entry file but
// does not grant its requested capabilities; the supplied policy remains the
// host's authority ceiling.
Entity<ShellRoot> ShellLoadApplication(ShellRuntime* runtime, Str directory,
                                       Window* window, App* app,
                                       Policy* policy = nullptr,
                                       ShellError* error = nullptr,
                                       Str* resolvedEntry = nullptr);
Str ShellCheckApplication(Arena* arena, ShellRuntime* runtime, Str directory,
                          Window* window, App* app, Policy* policy = nullptr,
                          ShellError* error = nullptr);

} // namespace gpui::shell

#endif // GPUI_SHELL_PLUGIN_H_
