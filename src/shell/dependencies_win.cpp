#include "shell/dependencies.h"

#include "shell/error.h"
#include "shell/wide.h"

#include <windows.h>

namespace gpui::shell {

static WCHAR* Wide(Str value) {
    if (!value) return nullptr;
    return (WCHAR*)ToCWstrTemp(value);
}

bool DependencyMakeDirectory(Str path) {
    // A drive root ("C:") is not a directory anyone creates.
    if (len(path) == 2 && path.s[1] == ':') return true;
    return CreateDirectoryW(Wide(path), nullptr) ||
           GetLastError() == ERROR_ALREADY_EXISTS;
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

bool DependencyLockOpen(Str path, DependencyLock* lock) {
    HANDLE file = CreateFileW(Wide(path), GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    lock->handle = (intptr_t)file;
    return true;
}

int DependencyLockTry(DependencyLock* lock) {
    OVERLAPPED overlapped = {};
    if (LockFileEx((HANDLE)lock->handle,
                   LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0,
                   &overlapped)) {
        return 1;
    }
    DWORD code = GetLastError();
    return code == ERROR_LOCK_VIOLATION || code == ERROR_IO_PENDING ? 0 : -1;
}

void DependencyLockClose(DependencyLock* lock) {
    CloseHandle((HANDLE)lock->handle);
    lock->handle = 0;
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
        Str utf8 = Utf8Dup(name, nameLen);
        if (!utf8.s) {
            ok = false;
        } else if (target) {
            *target = utf8;
        } else {
            StrFree(utf8);
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
