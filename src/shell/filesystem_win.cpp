#include "shell/filesystem.h"

#if GPUI_OS_WINDOWS

#include <windows.h>
#include <winternl.h>
#include <stdlib.h>

namespace gpui::shell {

#ifndef FILE_OPEN_REPARSE_POINT
#define FILE_OPEN_REPARSE_POINT 0x00200000
#endif

using NtCreateFileProc = NTSTATUS(NTAPI*)(PHANDLE, ACCESS_MASK,
                                          POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK,
                                          PLARGE_INTEGER, ULONG, ULONG, ULONG,
                                          ULONG, PVOID, ULONG);

static void FsError(Str* error, Str message) {
    if (!error) return;
    StrFree(*error);
    *error = StrDup(message);
}

static WCHAR* WideDup(Str value) {
    if (!value.s || len(value) < 0) return nullptr;
    for (int i = 0; i < len(value); i++) {
        if (value.s[i] == 0) return nullptr;
    }
    int count = len(value) == 0
                    ? 0
                    : MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                          value.s, len(value), nullptr, 0);
    if (len(value) > 0 && count <= 0) return nullptr;
    WCHAR* result = AllocArray<WCHAR>(count + 1);
    if (!result) return nullptr;
    if (count > 0 && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.s,
                                         len(value), result, count) != count) {
        Free(nullptr, result);
        return nullptr;
    }
    result[count] = 0;
    return result;
}

static Str Utf8Dup(const WCHAR* value, int count) {
    int bytes = WideCharToMultiByte(CP_UTF8, 0, value, count, nullptr, 0,
                                    nullptr, nullptr);
    if (bytes <= 0) return {};
    char* result = (char*)Alloc(nullptr, bytes + 1);
    if (!result) return {};
    WideCharToMultiByte(CP_UTF8, 0, value, count, result, bytes, nullptr,
                        nullptr);
    result[bytes] = 0;
    return Str(result, bytes);
}

static NtCreateFileProc NtCreateFileAddress() {
    static NtCreateFileProc function = (NtCreateFileProc)GetProcAddress(
        GetModuleHandleW(L"ntdll.dll"), "NtCreateFile");
    return function;
}

static bool NtSuccess(NTSTATUS status) {
    return status >= 0;
}

static bool NtMissing(NTSTATUS status) {
    return status == (NTSTATUS)0xC0000034L || status == (NTSTATUS)0xC000003AL ||
           status == (NTSTATUS)0xC000000FL;
}

static HANDLE OpenAt(HANDLE root, const WCHAR* name, ACCESS_MASK access,
                     ULONG disposition, ULONG options, NTSTATUS* statusOut) {
    NtCreateFileProc create = NtCreateFileAddress();
    if (!create) {
        if (statusOut) *statusOut = (NTSTATUS)0xC0000002L;
        return nullptr;
    }
    UNICODE_STRING unicode = {};
    unicode.Buffer = (PWSTR)name;
    unicode.Length = (USHORT)(wcslen(name) * sizeof(WCHAR));
    unicode.MaximumLength = unicode.Length;
    OBJECT_ATTRIBUTES attributes = {};
    InitializeObjectAttributes(&attributes, &unicode, OBJ_CASE_INSENSITIVE,
                               root, nullptr);
    IO_STATUS_BLOCK io = {};
    HANDLE result = nullptr;
    NTSTATUS status = create(
        &result, access, &attributes, &io, nullptr, FILE_ATTRIBUTE_NORMAL,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, disposition,
        options | FILE_OPEN_REPARSE_POINT | FILE_SYNCHRONOUS_IO_NONALERT,
        nullptr, 0);
    if (statusOut) *statusOut = status;
    return NtSuccess(status) ? result : nullptr;
}

static bool IsReparse(HANDLE handle) {
    FILE_ATTRIBUTE_TAG_INFO info = {};
    return GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &info,
                                        sizeof(info)) &&
           (info.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

static void NtError(Str* error, const char* verb, Str root, Str relative,
                    NTSTATUS status) {
    FsError(error, fmt("cannot %s `%s/%s`: Windows status 0x%08x", Str(verb),
                       root, relative, (uint32_t)status));
}

static bool EnsureAmbientRoot(Str root, Str* error) {
    WCHAR* path = WideDup(root);
    if (!path) {
        FsError(error, StrL("filesystem root is not valid UTF-8"));
        return false;
    }
    int length = (int)wcslen(path);
    int start = length >= 3 && path[1] == L':' ? 3 : 1;
    for (int i = start; i < length; i++) {
        if (path[i] != L'\\' && path[i] != L'/') continue;
        WCHAR saved = path[i];
        path[i] = 0;
        if (path[0] && !CreateDirectoryW(path, nullptr) &&
            GetLastError() != ERROR_ALREADY_EXISTS) {
            FsError(error,
                    fmt("cannot create granted root `%s`: Windows error %u",
                        root, GetLastError()));
            Free(nullptr, path);
            return false;
        }
        path[i] = saved;
    }
    bool ok = CreateDirectoryW(path, nullptr) ||
              GetLastError() == ERROR_ALREADY_EXISTS;
    if (!ok) {
        FsError(error, fmt("cannot create granted root `%s`: Windows error %u",
                           root, GetLastError()));
    }
    Free(nullptr, path);
    return ok;
}

static HANDLE OpenRoot(Str root, bool create, Str* error) {
    WCHAR* path = WideDup(root);
    if (!path) {
        FsError(error, StrL("filesystem root is not valid UTF-8"));
        return nullptr;
    }
    HANDLE result = CreateFileW(
        path,
        FILE_LIST_DIRECTORY | FILE_TRAVERSE | FILE_READ_ATTRIBUTES |
            SYNCHRONIZE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    DWORD firstError = result == INVALID_HANDLE_VALUE ? GetLastError() : 0;
    if (result == INVALID_HANDLE_VALUE && create &&
        (firstError == ERROR_PATH_NOT_FOUND ||
         firstError == ERROR_FILE_NOT_FOUND) &&
        EnsureAmbientRoot(root, error)) {
        result = CreateFileW(
            path,
            FILE_LIST_DIRECTORY | FILE_TRAVERSE | FILE_READ_ATTRIBUTES |
                SYNCHRONIZE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    }
    Free(nullptr, path);
    if (result == INVALID_HANDLE_VALUE) {
        FsError(error, fmt("cannot open granted root `%s`: Windows error %u",
                           root, GetLastError()));
        return nullptr;
    }
    return result;
}

static bool ValidComponent(const WCHAR* value) {
    return value[0] && wcscmp(value, L".") != 0 && wcscmp(value, L"..") != 0 &&
           wcschr(value, L'\\') == nullptr;
}

static HANDLE OpenDirectoryAt(HANDLE parent, const WCHAR* name,
                              NTSTATUS* status) {
    HANDLE result = OpenAt(parent, name,
                           FILE_LIST_DIRECTORY | FILE_TRAVERSE |
                               FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                           FILE_OPEN, FILE_DIRECTORY_FILE, status);
    if (result && IsReparse(result)) {
        CloseHandle(result);
        if (status) *status = (NTSTATUS)0xC000050BL;
        return nullptr;
    }
    return result;
}

static bool OpenParent(HANDLE root, Str rootName, Str relative, HANDLE* parent,
                       WCHAR leaf[256], Str* error) {
    *parent = nullptr;
    leaf[0] = 0;
    if (StrEq(relative, StrL("."))) {
        if (!DuplicateHandle(GetCurrentProcess(), root, GetCurrentProcess(),
                             parent, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
            FsError(error, fmt("cannot open `%s/%s`: Windows error %u",
                               rootName, relative, GetLastError()));
            return false;
        }
        wcscpy(leaf, L".");
        return true;
    }
    WCHAR* path = WideDup(relative);
    if (!path) {
        FsError(error, StrL("filesystem path is not valid UTF-8"));
        return false;
    }
    HANDLE current = nullptr;
    bool ok = DuplicateHandle(GetCurrentProcess(), root, GetCurrentProcess(),
                              &current, 0, FALSE, DUPLICATE_SAME_ACCESS) != 0;
    WCHAR* at = path;
    while (ok) {
        WCHAR* slash = wcschr(at, L'/');
        if (slash) *slash = 0;
        if (!ValidComponent(at) || wcslen(at) >= 256) {
            FsError(error,
                    fmt("refusing invalid path component in `%s`", relative));
            ok = false;
            break;
        }
        if (!slash) {
            wcscpy(leaf, at);
            break;
        }
        NTSTATUS status = 0;
        HANDLE next = OpenDirectoryAt(current, at, &status);
        if (!next) {
            NtError(error, "open", rootName, relative, status);
            ok = false;
            break;
        }
        CloseHandle(current);
        current = next;
        at = slash + 1;
    }
    Free(nullptr, path);
    if (!ok) {
        if (current) CloseHandle(current);
        return false;
    }
    *parent = current;
    return true;
}

static HANDLE OpenDirectory(HANDLE root, Str rootName, Str relative,
                            Str* error) {
    if (StrEq(relative, StrL("."))) {
        HANDLE copy = nullptr;
        DuplicateHandle(GetCurrentProcess(), root, GetCurrentProcess(), &copy,
                        0, FALSE, DUPLICATE_SAME_ACCESS);
        return copy;
    }
    HANDLE parent = nullptr;
    WCHAR leaf[256];
    if (!OpenParent(root, rootName, relative, &parent, leaf, error))
        return nullptr;
    NTSTATUS status = 0;
    HANDLE result = OpenDirectoryAt(parent, leaf, &status);
    if (!result) NtError(error, "open directory", rootName, relative, status);
    CloseHandle(parent);
    return result;
}

static bool ReadGrantedFile(HANDLE root, Str rootName, Str relative,
                            FsResult* result, Str* error) {
    HANDLE parent = nullptr;
    WCHAR leaf[256];
    if (!OpenParent(root, rootName, relative, &parent, leaf, error))
        return false;
    NTSTATUS status = 0;
    HANDLE file = OpenAt(parent, leaf, GENERIC_READ | SYNCHRONIZE, FILE_OPEN,
                         FILE_NON_DIRECTORY_FILE, &status);
    CloseHandle(parent);
    if (!file || IsReparse(file)) {
        if (file) CloseHandle(file);
        NtError(error, "read", rootName, relative,
                file ? (NTSTATUS)0xC000050BL : status);
        return false;
    }
    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(file, &size)) {
        FsError(error, fmt("cannot read `%s/%s`: Windows error %u", rootName,
                           relative, GetLastError()));
        CloseHandle(file);
        return false;
    }
    if (size.QuadPart > kFsMaxReadBytes) {
        FsError(
            error,
            fmt("`%s/%s` is %lld bytes, over the %d-byte limit for fs.readFile",
                rootName, relative, size.QuadPart, kFsMaxReadBytes));
        CloseHandle(file);
        return false;
    }
    StrBuilder output;
    bool ok = true;
    while (output.len <= kFsMaxReadBytes) {
        char bytes[8192];
        DWORD count = 0;
        if (!::ReadFile(file, bytes, sizeof(bytes), &count, nullptr)) {
            FsError(error, fmt("cannot read `%s/%s`: Windows error %u",
                               rootName, relative, GetLastError()));
            ok = false;
            break;
        }
        if (count == 0) break;
        if (output.len > kFsMaxReadBytes - (int)count) {
            FsError(error,
                    fmt("`%s/%s` grew over the %d-byte limit for fs.readFile",
                        rootName, relative, kFsMaxReadBytes));
            ok = false;
            break;
        }
        output.Append(Str(bytes, (int)count));
    }
    CloseHandle(file);
    if (ok) result->bytes = output.TakeStr();
    return ok;
}

static bool WriteGrantedFile(HANDLE root, Str rootName, Str relative, Str input,
                             Str* error) {
    HANDLE parent = nullptr;
    WCHAR leaf[256];
    if (!OpenParent(root, rootName, relative, &parent, leaf, error))
        return false;
    NTSTATUS status = 0;
    HANDLE file = OpenAt(parent, leaf, GENERIC_WRITE | SYNCHRONIZE,
                         FILE_OVERWRITE_IF, FILE_NON_DIRECTORY_FILE, &status);
    CloseHandle(parent);
    if (!file || IsReparse(file)) {
        if (file) CloseHandle(file);
        NtError(error, "write", rootName, relative,
                file ? (NTSTATUS)0xC000050BL : status);
        return false;
    }
    int written = 0;
    while (written < len(input)) {
        DWORD count = 0;
        DWORD wanted = (DWORD)(len(input) - written);
        if (!::WriteFile(file, input.s + written, wanted, &count, nullptr) ||
            count == 0) {
            FsError(error, fmt("cannot write `%s/%s`: Windows error %u",
                               rootName, relative, GetLastError()));
            CloseHandle(file);
            return false;
        }
        written += (int)count;
    }
    if (!FlushFileBuffers(file)) {
        FsError(error, fmt("cannot write `%s/%s`: Windows error %u", rootName,
                           relative, GetLastError()));
        CloseHandle(file);
        return false;
    }
    CloseHandle(file);
    return true;
}

static int CompareEntry(const void* left, const void* right) {
    const FsEntry* a = (const FsEntry*)left;
    const FsEntry* b = (const FsEntry*)right;
    return StrCmp(a->name, b->name);
}

static bool ReadDirectory(HANDLE root, Str rootName, Str relative,
                          FsResult* result, Str* error) {
    HANDLE directory = OpenDirectory(root, rootName, relative, error);
    if (!directory) return false;
    char buffer[64 * 1024];
    FILE_INFO_BY_HANDLE_CLASS infoClass = FileIdBothDirectoryRestartInfo;
    int nameBytes = 0;
    bool ok = true;
    for (;;) {
        if (!GetFileInformationByHandleEx(directory, infoClass, buffer,
                                          sizeof(buffer))) {
            DWORD code = GetLastError();
            if (code == ERROR_NO_MORE_FILES) break;
            FsError(error, fmt("cannot list `%s/%s`: Windows error %u",
                               rootName, relative, code));
            ok = false;
            break;
        }
        infoClass = FileIdBothDirectoryInfo;
        FILE_ID_BOTH_DIR_INFO* entry = (FILE_ID_BOTH_DIR_INFO*)buffer;
        for (;;) {
            int chars = (int)(entry->FileNameLength / sizeof(WCHAR));
            bool dot = (chars == 1 && entry->FileName[0] == L'.') ||
                       (chars == 2 && entry->FileName[0] == L'.' &&
                        entry->FileName[1] == L'.');
            if (!dot) {
                Str name = Utf8Dup(entry->FileName, chars);
                nameBytes += len(name);
                if (!name.s || result->entries.len >= kFsMaxDirectoryEntries ||
                    nameBytes > kFsMaxDirectoryNameBytes) {
                    StrFree(name);
                    FsError(error, fmt("directory exceeded the %d-entry or "
                                       "%d-name-byte fs.readdir limit",
                                       kFsMaxDirectoryEntries,
                                       kFsMaxDirectoryNameBytes));
                    ok = false;
                    break;
                }
                FsEntry value = {name, (entry->FileAttributes &
                                        FILE_ATTRIBUTE_DIRECTORY) != 0 &&
                                           (entry->FileAttributes &
                                            FILE_ATTRIBUTE_REPARSE_POINT) == 0};
                if (!VecAppend(result->entries, value)) {
                    StrFree(name);
                    FsError(error,
                            StrL("allocating an fs.readdir result failed"));
                    ok = false;
                    break;
                }
            }
            if (entry->NextEntryOffset == 0) break;
            entry =
                (FILE_ID_BOTH_DIR_INFO*)((char*)entry + entry->NextEntryOffset);
        }
        if (!ok) break;
    }
    CloseHandle(directory);
    if (ok && result->entries.len > 1) {
        qsort(result->entries.els, (size_t)result->entries.len, sizeof(FsEntry),
              CompareEntry);
    }
    return ok;
}

static bool MakeDirectoryRecursive(HANDLE root, Str rootName, Str relative,
                                   Str* error) {
    if (StrEq(relative, StrL("."))) return true;
    WCHAR* path = WideDup(relative);
    if (!path) return false;
    HANDLE current = nullptr;
    bool ok = DuplicateHandle(GetCurrentProcess(), root, GetCurrentProcess(),
                              &current, 0, FALSE, DUPLICATE_SAME_ACCESS) != 0;
    WCHAR* at = path;
    while (ok && *at) {
        WCHAR* slash = wcschr(at, L'/');
        if (slash) *slash = 0;
        if (!ValidComponent(at) || wcslen(at) >= 256) {
            FsError(error,
                    fmt("refusing invalid path component in `%s`", relative));
            ok = false;
            break;
        }
        NTSTATUS status = 0;
        HANDLE next = OpenAt(current, at,
                             FILE_LIST_DIRECTORY | FILE_TRAVERSE |
                                 FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                             FILE_OPEN_IF, FILE_DIRECTORY_FILE, &status);
        if (!next || IsReparse(next)) {
            if (next) CloseHandle(next);
            NtError(error, "create", rootName, relative,
                    next ? (NTSTATUS)0xC000050BL : status);
            ok = false;
            break;
        }
        CloseHandle(current);
        current = next;
        if (!slash) break;
        at = slash + 1;
    }
    if (current) CloseHandle(current);
    Free(nullptr, path);
    return ok;
}

static bool DeleteHandle(HANDLE handle, Str root, Str relative, Str* error) {
    FILE_DISPOSITION_INFO disposition = {TRUE};
    if (SetFileInformationByHandle(handle, FileDispositionInfo, &disposition,
                                   sizeof(disposition)))
        return true;
    FsError(error, fmt("cannot remove `%s/%s`: Windows error %u", root,
                       relative, GetLastError()));
    return false;
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
    HANDLE root = OpenRoot(rootName, writeAccess, error);
    if (!root) return false;
    FsResult unused;
    if (!result) result = &unused;
    bool ok = false;
    if (operation == FsOperation::Read) {
        ok = ReadGrantedFile(root, rootName, relative, result, error);
    } else if (operation == FsOperation::Write) {
        ok = WriteGrantedFile(root, rootName, relative, input, error);
    } else if (operation == FsOperation::ReadDirectory) {
        ok = ReadDirectory(root, rootName, relative, result, error);
    } else if (operation == FsOperation::MakeDirectory && recursive) {
        ok = MakeDirectoryRecursive(root, rootName, relative, error);
    } else {
        HANDLE parent = nullptr;
        WCHAR leaf[256];
        ok = OpenParent(root, rootName, relative, &parent, leaf, error);
        if (ok) {
            NTSTATUS status = 0;
            if (operation == FsOperation::Exists) {
                HANDLE target =
                    OpenAt(parent, leaf, FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                           FILE_OPEN, 0, &status);
                result->exists = target != nullptr;
                if (target)
                    CloseHandle(target);
                else if (!NtMissing(status)) {
                    NtError(error, "inspect", rootName, relative, status);
                    ok = false;
                }
            } else if (operation == FsOperation::MakeDirectory) {
                HANDLE target =
                    OpenAt(parent, leaf,
                           FILE_LIST_DIRECTORY | FILE_TRAVERSE |
                               FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                           FILE_CREATE, FILE_DIRECTORY_FILE, &status);
                ok = target != nullptr && !IsReparse(target);
                if (target) CloseHandle(target);
                if (!ok) NtError(error, "create", rootName, relative, status);
            } else {
                ULONG options = operation == FsOperation::RemoveDirectory
                                    ? FILE_DIRECTORY_FILE
                                    : FILE_NON_DIRECTORY_FILE;
                HANDLE target = OpenAt(
                    parent, leaf, DELETE | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                    FILE_OPEN, options, &status);
                ok = target != nullptr;
                if (target) {
                    ok = DeleteHandle(target, rootName, relative, error);
                    CloseHandle(target);
                } else {
                    NtError(error, "remove", rootName, relative, status);
                }
            }
            CloseHandle(parent);
        }
    }
    CloseHandle(root);
    return ok;
}

} // namespace gpui::shell

#endif
