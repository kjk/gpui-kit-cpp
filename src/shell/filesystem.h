#ifndef GPUI_SHELL_FILESYSTEM_H_
#define GPUI_SHELL_FILESYSTEM_H_

#include "base.h"

namespace gpui::shell {

constexpr int kFsMaxReadBytes = 64 * 1024 * 1024;
constexpr int kFsMaxWriteBytes = 8 * 1024 * 1024;
constexpr int kFsMaxDirectoryEntries = 10000;
constexpr int kFsMaxDirectoryNameBytes = 1024 * 1024;

enum class FsOperation : uint8_t {
    Read,
    Write,
    ReadDirectory,
    Exists,
    RemoveFile,
    RemoveDirectory,
    MakeDirectory,
    // Appended so the natives already bound to 0..6 keep those magics.
    Stat,
    Lstat,
    ReadLink,
    Rename,
};

struct FsEntry {
    Str name;
    bool isDirectory = false;
};

struct FsResult {
    Str bytes;
    Vec<FsEntry> entries;
    bool exists = false;
    // Set by Stat and Lstat. Times the filesystem does not record stay 0,
    // which is the epoch a script's Date reads. `mode` is the Unix st_mode
    // on POSIX and 0444 or 0666 on Windows.
    bool hasStats = false;
    bool isFile = false;
    bool isDirectory = false;
    bool isSymbolicLink = false;
    uint64_t size = 0;
    uint32_t mode = 0;
    double mtimeMs = 0;
    double atimeMs = 0;
    double birthtimeMs = 0;

    void Free();
};

// `root` is the granted directory and `relative` is the normalized name
// inside it. The platform implementation resolves every component through a
// directory handle and refuses symlinks/reparse points, so this check and the
// operation are one authority-preserving traversal rather than a string test
// followed by an ambient path syscall.
bool FsRun(FsOperation operation, Str root, Str relative, Str input,
           bool recursive, FsResult* result, Str* error);

// Both ends are granted write roots. A symlink is renamed as itself.
// `stat` does not follow a symlink: the shared POSIX body cannot check
// that a followed descriptor stayed inside the root without an OS test,
// so a link is an error from `stat` and a description from `lstat`.
bool FsRename(Str fromRoot, Str fromRelative, Str toRoot, Str toRelative,
              Str* error);

} // namespace gpui::shell
#endif // GPUI_SHELL_FILESYSTEM_H_
