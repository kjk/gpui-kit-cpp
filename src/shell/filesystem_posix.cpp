#include "shell/filesystem.h"

#if !GPUI_OS_WINDOWS && !GPUI_OS_WASM

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

namespace gpui::shell {

static void FsError(Str* error, Str message) {
    if (!error) return;
    StrFree(*error);
    *error = StrDup(message);
}

static void SyscallError(Str* error, const char* verb, Str root, Str relative) {
    FsError(error, fmt("cannot %s `%s/%s`: %s", Str(verb), root, relative,
                       Str(strerror(errno))));
}

static bool EnsureAmbientRoot(Str root, Str* error) {
    char* path = StrDup(root).s;
    if (!path) {
        FsError(error, StrL("allocating the filesystem root path failed"));
        return false;
    }
    int length = (int)strlen(path);
    for (int i = 1; i < length; i++) {
        if (path[i] != '/') continue;
        path[i] = 0;
        if (path[0] && mkdir(path, 0777) != 0 && errno != EEXIST) {
            SyscallError(error, "create", root, StrL("."));
            Free(nullptr, path);
            return false;
        }
        path[i] = '/';
    }
    bool ok = mkdir(path, 0777) == 0 || errno == EEXIST;
    if (!ok) SyscallError(error, "create", root, StrL("."));
    Free(nullptr, path);
    return ok;
}

static int OpenRoot(Str root, bool create, Str* error) {
    int fd = open(root.s, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd >= 0) return fd;
    if (create && errno == ENOENT && EnsureAmbientRoot(root, error)) {
        fd = open(root.s, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (fd >= 0) return fd;
    }
    SyscallError(error, "open granted root", root, StrL("."));
    return -1;
}

static bool ValidComponent(Str value) {
    return value && !StrEq(value, StrL(".")) && !StrEq(value, StrL("..")) &&
           StrFind(value, "\\") < 0;
}

static int OpenDirectoryAt(int parent, const char* name) {
    return openat(parent, name,
                  O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
}

static bool OpenParent(int root, Str relative, int* parent, TempStr* leaf,
                       Str rootName, Str* error) {
    *parent = -1;
    *leaf = {};
    if (StrEq(relative, StrL("."))) {
        int copy = dup(root);
        if (copy < 0)
            SyscallError(error, "open", rootName, relative);
        else {
            *parent = copy;
            *leaf = StrDupTemp(StrL("."));
        }
        return copy >= 0;
    }
    TempStr path = StrDupTemp(relative);
    if (!path.s) {
        FsError(error, StrL("allocating a filesystem path failed"));
        return false;
    }
    int current = dup(root);
    bool ok = current >= 0;
    char* at = path.s;
    while (ok) {
        char* slash = strchr(at, '/');
        if (slash) *slash = 0;
        Str component = Str(at);
        if (!ValidComponent(component) || len(component) >= 256) {
            FsError(error,
                    fmt("refusing invalid path component in `%s`", relative));
            ok = false;
            break;
        }
        if (!slash) {
            *leaf = StrDupTemp(Str(at));
            break;
        }
        int next = OpenDirectoryAt(current, at);
        if (next < 0) {
            SyscallError(error, "open", rootName, relative);
            ok = false;
            break;
        }
        close(current);
        current = next;
        at = slash + 1;
    }
    if (!ok) {
        if (current >= 0) close(current);
        return false;
    }
    *parent = current;
    return true;
}

static int OpenDirectory(int root, Str relative, Str rootName, Str* error) {
    if (StrEq(relative, StrL("."))) return dup(root);
    int parent = -1;
    TempStr leaf;
    if (!OpenParent(root, relative, &parent, &leaf, rootName, error)) return -1;
    int result = OpenDirectoryAt(parent, leaf.s);
    if (result < 0) SyscallError(error, "open directory", rootName, relative);
    close(parent);
    return result;
}

static bool ReadFile(int root, Str rootName, Str relative, FsResult* result,
                     Str* error) {
    int parent = -1;
    TempStr leaf;
    if (!OpenParent(root, relative, &parent, &leaf, rootName, error))
        return false;
    int file = openat(parent, leaf.s, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    close(parent);
    if (file < 0) {
        SyscallError(error, "read", rootName, relative);
        return false;
    }
    struct stat info = {};
    if (fstat(file, &info) != 0) {
        SyscallError(error, "read", rootName, relative);
        close(file);
        return false;
    }
    if (info.st_size > kFsMaxReadBytes) {
        FsError(
            error,
            fmt("`%s/%s` is %lld bytes, over the %d-byte limit for fs.readFile",
                rootName, relative, (int64_t)info.st_size, kFsMaxReadBytes));
        close(file);
        return false;
    }
    StrBuilder output;
    bool ok = true;
    while (output.len <= kFsMaxReadBytes) {
        char bytes[8192];
        ssize_t count = read(file, bytes, sizeof(bytes));
        if (count > 0) {
            if (output.len > kFsMaxReadBytes - (int)count) {
                FsError(
                    error,
                    fmt("`%s/%s` grew over the %d-byte limit for fs.readFile",
                        rootName, relative, kFsMaxReadBytes));
                ok = false;
                break;
            }
            output.Append(Str(bytes, (int)count));
        } else if (count == 0)
            break;
        else if (errno != EINTR) {
            SyscallError(error, "read", rootName, relative);
            ok = false;
            break;
        }
    }
    close(file);
    if (ok) result->bytes = output.TakeStr();
    return ok;
}

static bool WriteFile(int root, Str rootName, Str relative, Str input,
                      Str* error) {
    int parent = -1;
    TempStr leaf;
    if (!OpenParent(root, relative, &parent, &leaf, rootName, error))
        return false;
    int file =
        openat(parent, leaf.s,
               O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0666);
    close(parent);
    if (file < 0) {
        SyscallError(error, "write", rootName, relative);
        return false;
    }
    int written = 0;
    while (written < len(input)) {
        ssize_t count =
            write(file, input.s + written, (size_t)(len(input) - written));
        if (count > 0)
            written += (int)count;
        else if (count < 0 && errno == EINTR)
            continue;
        else {
            SyscallError(error, "write", rootName, relative);
            close(file);
            return false;
        }
    }
    if (close(file) != 0) {
        SyscallError(error, "write", rootName, relative);
        return false;
    }
    return true;
}

static int CompareEntry(const void* left, const void* right) {
    const FsEntry* a = (const FsEntry*)left;
    const FsEntry* b = (const FsEntry*)right;
    return StrCmp(a->name, b->name);
}

static bool ReadDirectory(int root, Str rootName, Str relative,
                          FsResult* result, Str* error) {
    int fd = OpenDirectory(root, relative, rootName, error);
    if (fd < 0) return false;
    DIR* directory = fdopendir(fd);
    if (!directory) {
        close(fd);
        SyscallError(error, "list", rootName, relative);
        return false;
    }
    int nameBytes = 0;
    bool ok = true;
    for (;;) {
        errno = 0;
        dirent* entry = readdir(directory);
        if (!entry) {
            if (errno != 0) {
                SyscallError(error, "list", rootName, relative);
                ok = false;
            }
            break;
        }
        Str name = Str(entry->d_name);
        if (StrEq(name, StrL(".")) || StrEq(name, StrL(".."))) continue;
        int nameLen = len(name);
        nameBytes += nameLen;
        if (result->entries.len >= kFsMaxDirectoryEntries ||
            nameBytes > kFsMaxDirectoryNameBytes) {
            FsError(error,
                    fmt("directory exceeded the %d-entry or %d-name-byte "
                        "fs.readdir limit",
                        kFsMaxDirectoryEntries, kFsMaxDirectoryNameBytes));
            ok = false;
            break;
        }
        struct stat info = {};
        bool directoryEntry = fstatat(dirfd(directory), entry->d_name, &info,
                                      AT_SYMLINK_NOFOLLOW) == 0 &&
                              S_ISDIR(info.st_mode);
        FsEntry value = {StrDup(Str(entry->d_name, nameLen)), directoryEntry};
        if (!value.name.s || !VecAppend(result->entries, value)) {
            StrFree(value.name);
            FsError(error, StrL("allocating an fs.readdir result failed"));
            ok = false;
            break;
        }
    }
    closedir(directory);
    if (ok && result->entries.len > 1) {
        qsort(result->entries.els, (size_t)result->entries.len, sizeof(FsEntry),
              CompareEntry);
    }
    return ok;
}

static bool MakeDirectoryRecursive(int root, Str rootName, Str relative,
                                   Str* error) {
    if (StrEq(relative, StrL("."))) return true;
    char* path = StrDup(relative).s;
    if (!path) return false;
    int current = dup(root);
    bool ok = current >= 0;
    char* at = path;
    while (ok && *at) {
        char* slash = strchr(at, '/');
        if (slash) *slash = 0;
        Str component = Str(at);
        if (!ValidComponent(component) || len(component) >= 256) {
            FsError(error,
                    fmt("refusing invalid path component in `%s`", relative));
            ok = false;
            break;
        }
        if (mkdirat(current, at, 0777) != 0 && errno != EEXIST) {
            SyscallError(error, "create", rootName, relative);
            ok = false;
            break;
        }
        int next = OpenDirectoryAt(current, at);
        if (next < 0) {
            SyscallError(error, "open", rootName, relative);
            ok = false;
            break;
        }
        close(current);
        current = next;
        if (!slash) break;
        at = slash + 1;
    }
    if (current >= 0) close(current);
    Free(nullptr, path);
    return ok;
}

bool FsRun(FsOperation operation, Str rootName, Str relative, Str input,
           bool recursive, FsResult* result, Str* error) {
    if (result) result->Free();
    if (error) {
        StrFree(*error);
        *error = {};
    }
    bool writeAccess = operation == FsOperation::Write ||
                       operation == FsOperation::RemoveFile ||
                       operation == FsOperation::RemoveDirectory ||
                       operation == FsOperation::MakeDirectory;
    int root = OpenRoot(rootName, writeAccess, error);
    if (root < 0) return false;
    FsResult unused;
    if (!result) result = &unused;
    bool ok = false;
    if (operation == FsOperation::Read) {
        ok = ReadFile(root, rootName, relative, result, error);
    } else if (operation == FsOperation::Write) {
        ok = WriteFile(root, rootName, relative, input, error);
    } else if (operation == FsOperation::ReadDirectory) {
        ok = ReadDirectory(root, rootName, relative, result, error);
    } else if (operation == FsOperation::Exists) {
        int parent = -1;
        TempStr leaf;
        ok = OpenParent(root, relative, &parent, &leaf, rootName, error);
        if (ok) {
            struct stat info = {};
            result->exists =
                fstatat(parent, leaf.s, &info, AT_SYMLINK_NOFOLLOW) == 0;
            if (!result->exists && errno != ENOENT && errno != ENOTDIR) {
                SyscallError(error, "inspect", rootName, relative);
                ok = false;
            }
            close(parent);
        }
    } else if (operation == FsOperation::MakeDirectory && recursive) {
        ok = MakeDirectoryRecursive(root, rootName, relative, error);
    } else {
        int parent = -1;
        TempStr leaf;
        ok = OpenParent(root, relative, &parent, &leaf, rootName, error);
        if (ok) {
            int status = -1;
            const char* verb = "remove";
            if (operation == FsOperation::RemoveFile)
                status = unlinkat(parent, leaf.s, 0);
            else if (operation == FsOperation::RemoveDirectory)
                status = unlinkat(parent, leaf.s, AT_REMOVEDIR);
            else if (operation == FsOperation::MakeDirectory) {
                status = mkdirat(parent, leaf.s, 0777);
                verb = "create";
            }
            ok = status == 0;
            if (!ok) SyscallError(error, verb, rootName, relative);
            close(parent);
        }
    }
    close(root);
    return ok;
}

} // namespace gpui::shell

#elif GPUI_OS_WASM

namespace gpui::shell {
bool FsRun(FsOperation, Str root, Str relative, Str, bool, FsResult*,
           Str* error) {
    if (error)
        *error = StrDup(
            fmt("filesystem mutation `%s/%s` is unavailable in a browser", root,
                relative));
    return false;
}
} // namespace gpui::shell

#endif
