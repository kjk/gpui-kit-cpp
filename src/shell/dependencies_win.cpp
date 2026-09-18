#include "shell/dependencies.h"

#if GPUI_OS_WINDOWS

#include <windows.h>

namespace gpui::shell {

static void DependencyError(Str* error, Str message) {
    if (!error) return;
    StrFree(*error);
    *error = StrDup(message);
}

static WCHAR* Wide(Str value) {
    if (!value) return nullptr;
    return (WCHAR*)ToCWstrTemp(value);
}

bool DependencyMakeDirectories(Str path, Str* error) {
    if (!path || len(path) >= kMaxPath) {
        DependencyError(error,
                        StrL("dependency cache path is empty or too long"));
        return false;
    }
    TempStr buffer = StrDupTemp(path);
    for (int i = 1; i <= len(path); i++) {
        if (i < len(path) && buffer.s[i] != '/' && buffer.s[i] != '\\')
            continue;
        char saved = buffer.s[i];
        buffer.s[i] = 0;
        // A drive root ("C:") is not a directory anyone creates.
        bool root = i == 2 && buffer.s[1] == ':';
        if (!root && !CreateDirectoryW(Wide(Str(buffer.s, i)), nullptr) &&
            GetLastError() != ERROR_ALREADY_EXISTS) {
            buffer.s[i] = saved;
            DependencyError(error, fmt("creating %s failed", path));
            return false;
        }
        buffer.s[i] = saved;
    }
    return true;
}

static void RemoveTreeAt(Str path) {
    if (len(path) + 3 >= kMaxPath) return;
    DWORD attributes = GetFileAttributesW(Wide(path));
    if (attributes == INVALID_FILE_ATTRIBUTES) return;
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        SetFileAttributesW(Wide(path), FILE_ATTRIBUTE_NORMAL);
        DeleteFileW(Wide(path));
        return;
    }
    if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) {
        RemoveDirectoryW(Wide(path));
        return;
    }
    WIN32_FIND_DATAW found;
    HANDLE search = FindFirstFileW(Wide(fmt("%s\\*", path)), &found);
    if (search != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(found.cFileName, L".") == 0 ||
                wcscmp(found.cFileName, L"..") == 0)
                continue;
            TempStr name = AllocStrTemp(kMaxPath - 1);
            int n = WideCharToMultiByte(CP_UTF8, 0, found.cFileName, -1, name.s,
                                        len(name) + 1, nullptr, nullptr);
            if (n <= 1) continue;
            RemoveTreeAt(fmt("%s\\%s", path, Str(name.s, n - 1)));
        } while (FindNextFileW(search, &found));
        FindClose(search);
    }
    SetFileAttributesW(Wide(path), FILE_ATTRIBUTE_NORMAL);
    RemoveDirectoryW(Wide(path));
}

void DependencyRemoveTree(Str path) {
    if (!path || len(path) >= kMaxPath) return;
    RemoveTreeAt(path);
}

bool DependencyRenameDirectory(Str from, Str to) {
    // No MOVEFILE_REPLACE_EXISTING: a destination that already exists is the
    // other process having published first.
    return from && to && MoveFileW(Wide(from), Wide(to)) != 0;
}

bool DependencyLockAcquire(Str path, Str name, DependencyLock* out,
                           Str* error) {
    if (out) *out = {};
    HANDLE file = CreateFileW(Wide(path), GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        DependencyError(
            error, fmt("opening Git dependency cache lock %s failed", path));
        return false;
    }
    double started = TimeNow();
    for (;;) {
        OVERLAPPED overlapped = {};
        if (LockFileEx(file,
                       LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0,
                       1, 0, &overlapped)) {
            if (out) out->handle = (intptr_t)file;
            return true;
        }
        DWORD code = GetLastError();
        if (code != ERROR_LOCK_VIOLATION && code != ERROR_IO_PENDING) {
            CloseHandle(file);
            DependencyError(error, StrL("locking Git dependency cache failed"));
            return false;
        }
        if (TimeNow() - started >= kGitDependencyLockTimeout) {
            CloseHandle(file);
            DependencyError(
                error, fmt("timed out waiting for another process to finish "
                           "Git dependency `%s`",
                           name));
            return false;
        }
        PlatSleepMs(25);
    }
}

void DependencyLockRelease(DependencyLock* lock) {
    if (!lock || lock->handle == 0) return;
    HANDLE file = (HANDLE)lock->handle;
    OVERLAPPED overlapped = {};
    UnlockFileEx(file, 0, 1, 0, &overlapped);
    CloseHandle(file);
    lock->handle = 0;
}

bool DependencySymlinkDirectory(Str target, Str link) {
    if (!target || !link) return false;
    // Without developer mode an unprivileged process is refused, which is the
    // arm that writes a re-export package instead.
    DWORD flags = SYMBOLIC_LINK_FLAG_DIRECTORY |
                  SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
    if (CreateSymbolicLinkW(Wide(link), Wide(target), flags)) return true;
    return CreateSymbolicLinkW(Wide(link), Wide(target),
                               SYMBOLIC_LINK_FLAG_DIRECTORY) != 0;
}

bool DependencyRemoveDirectoryLink(Str link) {
    return link && RemoveDirectoryW(Wide(link)) != 0;
}

// The reparse-point layouts this reads back. Declared here rather than pulled
// from ntifs.h, which is not in the SDK a user-mode build has.
struct DependencyReparseHeader {
    ULONG ReparseTag;
    USHORT ReparseDataLength;
    USHORT Reserved;
};

struct DependencySymbolicLinkBuffer {
    DependencyReparseHeader header;
    USHORT SubstituteNameOffset;
    USHORT SubstituteNameLength;
    USHORT PrintNameOffset;
    USHORT PrintNameLength;
    ULONG Flags;
    WCHAR PathBuffer[1];
};

struct DependencyMountPointBuffer {
    DependencyReparseHeader header;
    USHORT SubstituteNameOffset;
    USHORT SubstituteNameLength;
    USHORT PrintNameOffset;
    USHORT PrintNameLength;
    WCHAR PathBuffer[1];
};

bool DependencyReadDirectoryLink(Str link, Str* target) {
    if (target) *target = {};
    if (!link) return false;
    DWORD attributes = GetFileAttributesW(Wide(link));
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0)
        return false;
    HANDLE handle = CreateFileW(
        Wide(link), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    const int kMaxReparse = 16 * 1024;
    uint8_t* buffer = AllocArray<uint8_t>(kMaxReparse);
    DWORD returned = 0;
    bool ok =
        buffer && DeviceIoControl(handle, FSCTL_GET_REPARSE_POINT, nullptr, 0,
                                  buffer, kMaxReparse, &returned, nullptr) != 0;
    CloseHandle(handle);
    const WCHAR* name = nullptr;
    int nameLen = 0;
    if (ok) {
        DependencyReparseHeader* header = (DependencyReparseHeader*)buffer;
        if (header->ReparseTag == IO_REPARSE_TAG_SYMLINK) {
            DependencySymbolicLinkBuffer* value =
                (DependencySymbolicLinkBuffer*)buffer;
            name =
                value->PathBuffer + value->SubstituteNameOffset / sizeof(WCHAR);
            nameLen = value->SubstituteNameLength / (int)sizeof(WCHAR);
        } else if (header->ReparseTag == IO_REPARSE_TAG_MOUNT_POINT) {
            DependencyMountPointBuffer* value =
                (DependencyMountPointBuffer*)buffer;
            name =
                value->PathBuffer + value->SubstituteNameOffset / sizeof(WCHAR);
            nameLen = value->SubstituteNameLength / (int)sizeof(WCHAR);
        } else {
            ok = false;
        }
    }
    if (ok && nameLen > 0) {
        // The NT namespace prefix a symlink stores is not part of the path an
        // application wrote.
        if (nameLen > 4 && name[0] == L'\\' && name[1] == L'?' &&
            name[2] == L'?' && name[3] == L'\\') {
            name += 4;
            nameLen -= 4;
        }
        int bytes = WideCharToMultiByte(CP_UTF8, 0, name, nameLen, nullptr, 0,
                                        nullptr, nullptr);
        if (bytes > 0) {
            char* utf8 = AllocArray<char>(bytes + 1);
            if (utf8 && WideCharToMultiByte(CP_UTF8, 0, name, nameLen, utf8,
                                            bytes, nullptr, nullptr) == bytes) {
                utf8[bytes] = 0;
                if (target)
                    *target = Str(utf8, bytes);
                else
                    Free(nullptr, utf8);
            } else {
                Free(nullptr, utf8);
                ok = false;
            }
        } else {
            ok = false;
        }
    } else {
        ok = false;
    }
    Free(nullptr, buffer);
    return ok;
}

uint32_t DependencyProcessId() {
    return (uint32_t)GetCurrentProcessId();
}

} // namespace gpui::shell

#endif
