#include "shell/process.h"

#if GPUI_OS_WINDOWS

#include <windows.h>

namespace gpui::shell {

constexpr int kProcessStreamLimit = 8 * 1024 * 1024;
constexpr double kProcessTimeout = 30.0;

static void ProcessError(Str* error, Str message) {
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

static bool ExecutableFile(const WCHAR* path) {
    DWORD attributes = GetFileAttributesW(path);
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static bool HasPathComponent(const WCHAR* command) {
    for (const WCHAR* at = command; *at; at++) {
        if (*at == L'\\' || *at == L'/' || *at == L':') return true;
    }
    return false;
}

static bool HasExtension(const WCHAR* command) {
    const WCHAR* dot = nullptr;
    for (const WCHAR* at = command; *at; at++) {
        if (*at == L'\\' || *at == L'/')
            dot = nullptr;
        else if (*at == L'.')
            dot = at;
    }
    return dot != nullptr;
}

static WCHAR* JoinExecutable(const WCHAR* directory, int directoryLen,
                             const WCHAR* command, const WCHAR* extension) {
    while (directoryLen > 1 && (directory[directoryLen - 1] == L'\\' ||
                                directory[directoryLen - 1] == L'/')) {
        directoryLen--;
    }
    int commandLen = (int)wcslen(command);
    int extensionLen = extension ? (int)wcslen(extension) : 0;
    bool separator = directoryLen > 0 && directory[directoryLen - 1] != L'\\' &&
                     directory[directoryLen - 1] != L'/';
    WCHAR* result = AllocArray<WCHAR>(directoryLen + (separator ? 1 : 0) +
                                      commandLen + extensionLen + 1);
    if (!result) return nullptr;
    int at = 0;
    if (directoryLen > 0) {
        memcpy(result, directory, (size_t)directoryLen * sizeof(WCHAR));
        at += directoryLen;
    }
    if (separator) result[at++] = L'\\';
    memcpy(result + at, command, (size_t)commandLen * sizeof(WCHAR));
    at += commandLen;
    if (extensionLen > 0) {
        memcpy(result + at, extension, (size_t)extensionLen * sizeof(WCHAR));
        at += extensionLen;
    }
    result[at] = 0;
    return result;
}

static WCHAR* ResolveExecutable(Str command, Str* error) {
    WCHAR* wide = WideDup(command);
    if (!wide || !wide[0]) {
        Free(nullptr, wide);
        ProcessError(error, StrL("process.run command must be non-empty UTF-8 "
                                 "without NUL bytes"));
        return nullptr;
    }
    if (HasPathComponent(wide)) return wide;

    DWORD needed = GetEnvironmentVariableW(L"PATH", nullptr, 0);
    if (needed == 0) {
        Free(nullptr, wide);
        ProcessError(error, fmt("running `%s` failed: the host PATH "
                                "environment variable is not set",
                                command));
        return nullptr;
    }
    WCHAR* path = AllocArray<WCHAR>((int)needed + 1);
    if (!path || GetEnvironmentVariableW(L"PATH", path, needed) == 0) {
        Free(nullptr, path);
        Free(nullptr, wide);
        ProcessError(
            error,
            fmt("running `%s` failed: reading the host PATH failed", command));
        return nullptr;
    }
    static const WCHAR* extensions[] = {L"", L".exe", L".com", L".bat",
                                        L".cmd"};
    int extensionCount = HasExtension(wide) ? 1 : 5;
    WCHAR* result = nullptr;
    const WCHAR* at = path;
    while (*at && !result) {
        const WCHAR* end = wcschr(at, L';');
        int count = end ? (int)(end - at) : (int)wcslen(at);
        while (count > 0 && (*at == L' ' || *at == L'\t')) {
            at++;
            count--;
        }
        while (count > 0 && (at[count - 1] == L' ' || at[count - 1] == L'\t'))
            count--;
        if (count >= 2 && at[0] == L'"' && at[count - 1] == L'"') {
            at++;
            count -= 2;
        }
        for (int i = 0; i < extensionCount && !result; i++) {
            WCHAR* candidate = JoinExecutable(at, count, wide, extensions[i]);
            if (candidate && ExecutableFile(candidate))
                result = candidate;
            else
                Free(nullptr, candidate);
        }
        if (!end) break;
        at = end + 1;
    }
    Free(nullptr, path);
    Free(nullptr, wide);
    if (!result) {
        ProcessError(error, fmt("running `%s` failed: executable was not found "
                                "on the host PATH",
                                command));
    }
    return result;
}

static bool NeedsQuotes(const WCHAR* value) {
    if (!value[0]) return true;
    for (const WCHAR* at = value; *at; at++) {
        if (*at == L' ' || *at == L'\t' || *at == L'"') return true;
    }
    return false;
}

static bool AppendWide(Vec<WCHAR>* out, WCHAR value, int count = 1) {
    for (int i = 0; i < count; i++) {
        if (!VecAppend(*out, value)) return false;
    }
    return true;
}

static bool AppendArgument(Vec<WCHAR>* out, Str argument) {
    WCHAR* value = WideDup(argument);
    if (!value) return false;
    if (out->len > 0 && !VecAppend(*out, L' ')) {
        Free(nullptr, value);
        return false;
    }
    if (!NeedsQuotes(value)) {
        for (const WCHAR* at = value; *at; at++) {
            if (!VecAppend(*out, *at)) {
                Free(nullptr, value);
                return false;
            }
        }
        Free(nullptr, value);
        return true;
    }
    bool ok = VecAppend(*out, L'"');
    int slashes = 0;
    for (const WCHAR* at = value; ok && *at; at++) {
        if (*at == L'\\') {
            slashes++;
        } else if (*at == L'"') {
            ok =
                AppendWide(out, L'\\', slashes * 2 + 1) && VecAppend(*out, *at);
            slashes = 0;
        } else {
            ok = AppendWide(out, L'\\', slashes) && VecAppend(*out, *at);
            slashes = 0;
        }
    }
    if (ok) ok = AppendWide(out, L'\\', slashes * 2) && VecAppend(*out, L'"');
    Free(nullptr, value);
    return ok;
}

static void Close(HANDLE* handle) {
    if (*handle && *handle != INVALID_HANDLE_VALUE) CloseHandle(*handle);
    *handle = nullptr;
}

static bool DrainPipe(HANDLE* pipe, StrBuilder* output, bool* closed,
                      Str* error, const char* name) {
    for (;;) {
        DWORD available = 0;
        if (!PeekNamedPipe(*pipe, nullptr, 0, nullptr, &available, nullptr)) {
            DWORD code = GetLastError();
            if (code == ERROR_BROKEN_PIPE) {
                Close(pipe);
                *closed = true;
                return true;
            }
            ProcessError(error,
                         fmt("reading child %s failed with Windows error %u",
                             Str(name), code));
            return false;
        }
        if (available == 0) return true;
        char bytes[8192];
        DWORD wanted =
            available < sizeof(bytes) ? available : (DWORD)sizeof(bytes);
        DWORD count = 0;
        if (!ReadFile(*pipe, bytes, wanted, &count, nullptr)) {
            DWORD code = GetLastError();
            if (code == ERROR_BROKEN_PIPE) {
                Close(pipe);
                *closed = true;
                return true;
            }
            ProcessError(error,
                         fmt("reading child %s failed with Windows error %u",
                             Str(name), code));
            return false;
        }
        if (count == 0) return true;
        if (output->len > kProcessStreamLimit - (int)count) {
            ProcessError(error, fmt("child %s exceeded 8 MiB", Str(name)));
            return false;
        }
        output->Append(Str(bytes, (int)count));
    }
}

// A CREATE_UNICODE_ENVIRONMENT block: NUL-separated NAME=VALUE, then one more
// NUL. `inherit` starts from the host's own block, which is what git needs.
static WCHAR* BuildEnvironment(const ProcessOptions* options, bool* ok) {
    *ok = true;
    int extra = options ? options->environmentCount : 0;
    bool inherit = options && options->inheritEnvironment;
    if (!inherit && extra == 0) return nullptr;
    Vec<WCHAR> block;
    if (inherit) {
        WCHAR* host = GetEnvironmentStringsW();
        if (!host) {
            *ok = false;
            return nullptr;
        }
        for (const WCHAR* at = host; *at;) {
            int len = (int)wcslen(at);
            memcpy(VecAppendBlanks(block, len + 1), at,
                   (size_t)(len + 1) * sizeof(WCHAR));
            at += len + 1;
        }
        FreeEnvironmentStringsW(host);
    }
    for (int i = 0; i < extra; i++) {
        WCHAR* value = WideDup(options->environment[i]);
        if (!value) {
            VecReset(block);
            *ok = false;
            return nullptr;
        }
        int len = (int)wcslen(value);
        memcpy(VecAppendBlanks(block, len + 1), value,
               (size_t)(len + 1) * sizeof(WCHAR));
        Free(nullptr, value);
    }
    VecAppend(block, (WCHAR)0);
    WCHAR* result = block.els;
    block.els = nullptr;
    block.len = 0;
    block.cap = 0;
    VecReset(block);
    return result;
}

bool ProcessRunBounded(Str command, const Str* args, int count,
                       ProcessCancellation* cancellation, ProcessOutput* output,
                       Str* error, const ProcessOptions* options) {
    if (output) output->Free();
    if (error) {
        StrFree(*error);
        *error = {};
    }
    WCHAR* executable = ResolveExecutable(command, error);
    if (!executable) return false;
    Vec<WCHAR> commandLine;
    Str executableUtf8 = command;
    bool ok = AppendArgument(&commandLine, executableUtf8);
    for (int i = 0; ok && i < count; i++)
        ok = AppendArgument(&commandLine, args[i]);
    if (!ok) {
        Free(nullptr, executable);
        ProcessError(error,
                     StrL("building the child-process command line failed"));
        return false;
    }

    SECURITY_ATTRIBUTES security = {};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    HANDLE outRead = nullptr, outWrite = nullptr;
    HANDLE errRead = nullptr, errWrite = nullptr;
    HANDLE nullInput = INVALID_HANDLE_VALUE;
    HANDLE job = nullptr;
    PROCESS_INFORMATION process = {};
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
    STARTUPINFOW startup = {};
    WCHAR emptyEnvironment[2] = {};
    WCHAR* environment = nullptr;
    WCHAR* directory = nullptr;
    bool environmentOk = true;
    if (!CreatePipe(&outRead, &outWrite, &security, 0) ||
        !CreatePipe(&errRead, &errWrite, &security, 0) ||
        !SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0) ||
        !SetHandleInformation(errRead, HANDLE_FLAG_INHERIT, 0)) {
        ProcessError(
            error,
            fmt("creating child-process pipes failed with Windows error %u",
                GetLastError()));
        ok = false;
        goto cleanup;
    }
    nullInput =
        CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (nullInput == INVALID_HANDLE_VALUE) {
        ProcessError(error, fmt("opening the child-process null input failed "
                                "with Windows error %u",
                                GetLastError()));
        ok = false;
        goto cleanup;
    }
    job = CreateJobObjectW(nullptr, nullptr);
    if (!job) {
        ProcessError(error, fmt("creating the child-process Job Object failed "
                                "with Windows error %u",
                                GetLastError()));
        ok = false;
        goto cleanup;
    }
    limits.BasicLimitInformation
        .LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation,
                                 &limits, sizeof(limits))) {
        ProcessError(error, fmt("configuring the child-process Job Object "
                                "failed with Windows error %u",
                                GetLastError()));
        ok = false;
        goto cleanup;
    }

    environment = BuildEnvironment(options, &environmentOk);
    if (!environmentOk) {
        ProcessError(error,
                     StrL("building the child-process environment failed"));
        ok = false;
        goto cleanup;
    }
    if (options && options->workingDirectory) {
        directory = WideDup(options->workingDirectory);
        if (!directory) {
            ProcessError(
                error,
                StrL("the child-process working directory is not valid UTF-8"));
            ok = false;
            goto cleanup;
        }
    }

    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = nullInput;
    startup.hStdOutput = outWrite;
    startup.hStdError = errWrite;
    if (!CreateProcessW(
            executable, commandLine.els, nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT,
            environment ? environment : emptyEnvironment, directory, &startup,
            &process)) {
        ProcessError(error, fmt("running `%s` failed with Windows error %u",
                                command, GetLastError()));
        ok = false;
        goto cleanup;
    }
    if (!AssignProcessToJobObject(job, process.hProcess)) {
        ProcessError(
            error,
            fmt("isolating `%s` process tree failed with Windows error %u",
                command, GetLastError()));
        ok = false;
        goto cleanup;
    }
    if (ResumeThread(process.hThread) == (DWORD)-1) {
        ProcessError(error, fmt("resuming `%s` failed with Windows error %u",
                                command, GetLastError()));
        ok = false;
        goto cleanup;
    }
    Close(&process.hThread);
    Close(&outWrite);
    Close(&errWrite);
    Close(&nullInput);

    {
        StrBuilder stdoutText, stderrText;
        bool stdoutClosed = false, stderrClosed = false, done = false;
        DWORD exitCode = (DWORD)-1;
        double started = TimeNow();
        while (!done || !stdoutClosed || !stderrClosed) {
            if (!stdoutClosed)
                ok = DrainPipe(&outRead, &stdoutText, &stdoutClosed, error,
                               "stdout");
            if (ok && !stderrClosed)
                ok = DrainPipe(&errRead, &stderrText, &stderrClosed, error,
                               "stderr");
            bool cancelled = cancellation && cancellation->IsCancelled();
            if (!ok || cancelled || TimeNow() - started >= kProcessTimeout) {
                if (ok && cancelled)
                    ProcessError(error, fmt("`%s` was cancelled", command));
                else if (ok)
                    ProcessError(error,
                                 fmt("`%s` timed out after 30000 ms", command));
                ok = false;
                TerminateJobObject(job, 1);
                WaitForSingleObject(process.hProcess, 5000);
                break;
            }
            if (!done &&
                WaitForSingleObject(process.hProcess, 0) == WAIT_OBJECT_0) {
                done = true;
                GetExitCodeProcess(process.hProcess, &exitCode);
            }
            if (!done || !stdoutClosed || !stderrClosed) PlatSleepMs(5);
        }
        if (ok && output) {
            output->code = exitCode == STILL_ACTIVE ? -1 : (int)exitCode;
            output->out = stdoutText.TakeStr();
            output->err = stderrText.TakeStr();
        }
    }

cleanup:
    if (!ok && process.hProcess) {
        if (job)
            TerminateJobObject(job, 1);
        else
            TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, 5000);
    }
    Close(&process.hThread);
    Close(&process.hProcess);
    Close(&outRead);
    Close(&outWrite);
    Close(&errRead);
    Close(&errWrite);
    Close(&nullInput);
    Close(&job);
    Free(nullptr, executable);
    Free(nullptr, environment);
    Free(nullptr, directory);
    return ok;
}

} // namespace gpui::shell

#endif
