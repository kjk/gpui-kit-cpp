#ifndef GPUI_SHELL_DEPENDENCIES_H_
#define GPUI_SHELL_DEPENDENCIES_H_

#include "shell/plugin.h"

// crates/shell/src/dependencies.rs: materializes the Git-backed JavaScript
// packages a manifest declares, in gpui-shell's own user cache, and points an
// editor at the same checkouts the runtime is about to execute.
//
// Everything here shells out to the `git` executable through the existing
// bounded process runner. Nothing in this file speaks a network protocol;
// the system HTTP client in src/sys/http.h is untouched.

namespace gpui::shell {

// GIT_TIMEOUT and LOCK_TIMEOUT.
constexpr double kGitDependencyTimeout = 30.0;
constexpr double kGitDependencyLockTimeout = 2 * 60.0;
// EDITOR_MODULE_DIRECTORY and EDITOR_LINK_MARKER.
constexpr const char* kEditorModuleDirectory = "node_modules";
constexpr const char* kEditorLinkMarker = ".gpui-shell-link";
// Pruning walks node_modules, which is a directory this port refuses to walk
// without a bound.
constexpr int kEditorPruneMaxEntries = 4096;

struct MaterializedDependency {
    Str name;
    Str root;
    Str entry;

    void Free();
};

struct MaterializedDependencies {
    // Sorted by name, the order the manifest keeps.
    Vec<MaterializedDependency> items;

    const MaterializedDependency* Find(Str name) const;
    // A deep copy, because each AppModule frees the strings it holds. Reload
    // hands the running application's set to its replacement rather than
    // fetching it again.
    bool CopyFrom(const MaterializedDependencies& other);
    void Free();
};

// `dependency_cache_root`: <home>/.gpui-shell/cache/dependencies.
Str GitDependencyCacheRoot(Str home);
// `for_user_with_environment`: HOME, then USERPROFILE, and each must be an
// absolute path or there is no private cache to speak of.
bool GitDependencyUserCacheRoot(Str home, Str userProfile, Str* out,
                                Str* error);

// `digest(&[("git", url)])`, hex SHA-256, the per-remote cache key.
Str GitDependencyRemoteKey(Str git);

class GitDependencyStore {
  public:
    // The per-user cache, resolved from the environment.
    GitDependencyStore();
    explicit GitDependencyStore(Str root);
    GitDependencyStore(const GitDependencyStore&) = delete;
    GitDependencyStore& operator=(const GitDependencyStore&) = delete;
    ~GitDependencyStore();

    bool IsValid() const { return root.s != nullptr; }
    Str Root() const { return root; }
    Str Error() const { return initError; }

    // `materialize`: one immutable, commit-addressed checkout, published
    // atomically under a cross-process lock.
    bool Materialize(Str name, const GitDependency& dependency,
                     MaterializedDependency* out, Str* error);
    // `materialize_all`, in manifest order.
    bool MaterializeAll(const PluginManifest& manifest,
                        MaterializedDependencies* out, Str* error);
    // `link_for_editor`: <app>/node_modules/<name> -> the checkout, so an
    // editor resolves the same files the runtime will execute. Only entries
    // this store wrote are ever replaced or removed.
    bool LinkForEditor(Str applicationRoot,
                       const MaterializedDependencies& dependencies,
                       int* linked, Str* error);

  private:
    void Prune(Str modules, const Vec<Str>& declared);

    Str root;
    Str initError;
};

// --- platform seam -----------------------------------------------------
//
// The four things Rust gets from std::fs and fs2 that a single expression
// cannot answer on all three hosts: an advisory whole-file lock, a directory
// symlink, its removal, and reading it back. Implemented in
// dependencies_win.cpp and dependencies_posix.cpp.

struct DependencyLock {
    // A HANDLE on Windows, a file descriptor plus one on POSIX; zero when the
    // lock is not held.
    int64_t handle = 0;
};

bool DependencyMakeDirectories(Str path, Str* error);
void DependencyRemoveTree(Str path);
// Fails when the destination already exists, which is how two processes
// publishing the same checkout stay on one of them.
bool DependencyRenameDirectory(Str from, Str to);
// `CacheLock::acquire`: waits up to kGitDependencyLockTimeout.
bool DependencyLockAcquire(Str path, Str name, DependencyLock* out, Str* error);
void DependencyLockRelease(DependencyLock* lock);
bool DependencySymlinkDirectory(Str target, Str link);
bool DependencyRemoveDirectoryLink(Str link);
// False when `link` is not a symlink or its target cannot be read.
bool DependencyReadDirectoryLink(Str link, Str* target);
uint32_t DependencyProcessId();

// `gpui_shell::write_dependency_links`: fetches and links what a manifest
// declares, for tooling that has to report the failure to its caller. An
// application without a manifest, or one that declares nothing, is not an
// error.
bool ShellWriteDependencyLinks(Str applicationRoot, int* linked, Str* error);

} // namespace gpui::shell

#endif // GPUI_SHELL_DEPENDENCIES_H_
