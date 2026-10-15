#include "shell/process.h"

#include <windows.h>

namespace gpui::shell {

constexpr int kProcessStreamLimit = 8 * 1024 * 1024;

static void ProcessError(Str* error, Str message) {
    if (!error) return;
    StrFree(*error);
    *error = StrDup(message);
}

// A child deadline is wall time. TimeNow() is the frame clock, and the
// test platform replaces it, so a 100 ms timeout would never arrive
// while a test holds that clock still.
static double WallSeconds() {
    static LARGE_INTEGER freq = {};
    static LARGE_INTEGER start = {};
    if (freq.QuadPart == 0) {
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&start);
    }
    LARGE_INTEGER now = {};
    QueryPerformanceCounter(&now);
    if (freq.QuadPart == 0) return 0;
    return (double)(now.QuadPart - start.QuadPart) / (double)freq.QuadPart;
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
        double started = WallSeconds();
        double timeoutSeconds = (options && options->timeoutSeconds > 0)
                                    ? options->timeoutSeconds
                                    : 30.0;
        int timeoutMs = (int)(timeoutSeconds * 1000.0 + 0.5);
        while (!done || !stdoutClosed || !stderrClosed) {
            if (!stdoutClosed)
                ok = DrainPipe(&outRead, &stdoutText, &stdoutClosed, error,
                               "stdout");
            if (ok && !stderrClosed)
                ok = DrainPipe(&errRead, &stderrText, &stderrClosed, error,
                               "stderr");
            bool cancelled = cancellation && cancellation->IsCancelled();
            if (!ok || cancelled || WallSeconds() - started >= timeoutSeconds) {
                if (ok && cancelled)
                    ProcessError(error, fmt("`%s` was cancelled", command));
                else if (ok)
                    ProcessError(error, fmt("`%s` timed out after %d ms",
                                            command, timeoutMs));
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

// A streaming child. The waiter joins the readers before it publishes Exit,
// so a poll never sees the exit ahead of the output that preceded it.
struct ProcessStream {
    ProcessEventBox box;
    Str command;
    Str cwd;
    double timeoutSeconds = 0;
    uint32_t pid = 0;
    HANDLE process = nullptr;
    HANDLE job = nullptr;
    HANDLE outRead = nullptr;
    HANDLE errRead = nullptr;
    HANDLE inWrite = nullptr;
    HANDLE readerOut = nullptr;
    HANDLE readerErr = nullptr;
    HANDLE waiter = nullptr;
    HANDLE stdinThread = nullptr;
    ProcessDecodeState outDecode;
    ProcessDecodeState errDecode;
};

struct ReaderLaunch {
    ProcessStream* stream = nullptr;
    HANDLE* pipeSlot = nullptr;
    ProcessDecodeState* decode = nullptr;
    ProcessStreamEvent::Kind kind = ProcessStreamEvent::Kind::Out;
};

static int TimeoutMilliseconds(double seconds) {
    return (int)(seconds * 1000.0 + 0.5);
}

static void JoinThread(HANDLE* thread) {
    if (!thread || !*thread) return;
    WaitForSingleObject(*thread, INFINITE);
    CloseHandle(*thread);
    *thread = nullptr;
}

static DWORD WINAPI StreamReaderMain(LPVOID param) {
    ReaderLaunch* launch = (ReaderLaunch*)param;
    ProcessStream* stream = launch->stream;
    HANDLE pipe = launch->pipeSlot ? *launch->pipeSlot : nullptr;
    uint8_t chunk[64 * 1024];
    bool failed = false;
    for (;;) {
        if (!pipe) break;
        // ReadFile on an anonymous pipe cannot be cancelled reliably, and the
        // waiter joins this thread before it publishes exit. Poll so a reaped
        // child — a timeout, in particular — is not stuck behind a blocked
        // read that still holds the exit event.
        DWORD available = 0;
        DWORD count = 0;
        if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr)) {
            // The write end closed. ReadFile returns whatever is still
            // buffered and then zero, so the last chunk is not dropped.
            if (!ReadFile(pipe, chunk, sizeof(chunk), &count, nullptr) ||
                count == 0) {
                break;
            }
        } else if (available == 0) {
            if (ProcessEventReaped(&stream->box)) break;
            Sleep(5);
            continue;
        } else {
            DWORD want =
                available > sizeof(chunk) ? (DWORD)sizeof(chunk) : available;
            if (!ReadFile(pipe, chunk, want, &count, nullptr) || count == 0) {
                break;
            }
        }
        Str error;
        if (!ProcessDecodeFeed(launch->decode, &stream->box, launch->kind,
                               chunk, (int)count, &error)) {
            ProcessEventSetReason(&stream->box, error);
            if (stream->job && !ProcessEventReaped(&stream->box))
                TerminateJobObject(stream->job, 1);
            failed = true;
            break;
        }
    }
    if (!failed)
        ProcessDecodeFinish(launch->decode, &stream->box, launch->kind);
    ProcessDecodeFree(launch->decode);
    if (launch->pipeSlot) Close(launch->pipeSlot);
    delete launch;
    return 0;
}

static DWORD WINAPI StreamStdinMain(LPVOID param) {
    ProcessStream* stream = (ProcessStream*)param;
    for (;;) {
        Vec<char> bytes;
        bool ended = false;
        if (!ProcessStdinTake(&stream->box, &bytes, &ended)) break;
        int offset = 0;
        bool ok = true;
        while (offset < bytes.len) {
            DWORD wrote = 0;
            if (!WriteFile(stream->inWrite, bytes.els + offset,
                           (DWORD)(bytes.len - offset), &wrote, nullptr) ||
                wrote == 0) {
                ok = false;
                break;
            }
            offset += (int)wrote;
        }
        VecReset(bytes);
        if (!ok) {
            ProcessStdinFail(&stream->box);
            break;
        }
        if (ended) break;
    }
    Close(&stream->inWrite);
    return 0;
}

static DWORD WINAPI StreamWaiterMain(LPVOID param) {
    ProcessStream* stream = (ProcessStream*)param;
    double started = WallSeconds();
    bool waitFailed = false;
    for (;;) {
        DWORD waited = WaitForSingleObject(stream->process, 20);
        if (waited == WAIT_OBJECT_0) break;
        if (waited != WAIT_TIMEOUT) {
            ProcessEventSetReason(
                &stream->box,
                StrDup(fmt("waiting for `%s` failed with Windows error %u",
                           stream->command, GetLastError())));
            waitFailed = true;
            break;
        }
        if (stream->timeoutSeconds > 0 &&
            WallSeconds() - started >= stream->timeoutSeconds) {
            ProcessEventSetReason(
                &stream->box,
                StrDup(fmt("`%s` timed out after %d ms", stream->command,
                           TimeoutMilliseconds(stream->timeoutSeconds))));
            if (stream->job) TerminateJobObject(stream->job, 1);
            break;
        }
    }
    if (stream->job) TerminateJobObject(stream->job, 1);
    // Readers poll and leave once this is set and the pipe is idle, so the
    // join below is not stuck in a blocking ReadFile.
    ProcessEventMarkReaped(&stream->box);
    JoinThread(&stream->readerOut);
    JoinThread(&stream->readerErr);
    ProcessStreamEvent event;
    event.kind = ProcessStreamEvent::Kind::Exit;
    if (!waitFailed) {
        DWORD code = 0;
        if (GetExitCodeProcess(stream->process, &code)) {
            event.hasCode = true;
            event.code = (int)code;
        }
    }
    Str reason = ProcessEventTakeReason(&stream->box);
    if (reason) {
        event.hasError = true;
        event.text = reason;
    }
    ProcessEventPush(&stream->box, event);
    return 0;
}

static HANDLE StartReader(ProcessStream* stream, HANDLE* pipe,
                          ProcessDecodeState* decode,
                          ProcessStreamEvent::Kind kind) {
    ReaderLaunch* launch = new ReaderLaunch();
    launch->stream = stream;
    launch->pipeSlot = pipe;
    launch->decode = decode;
    launch->kind = kind;
    HANDLE thread =
        CreateThread(nullptr, 0, StreamReaderMain, launch, 0, nullptr);
    if (!thread) delete launch;
    return thread;
}

static void ReleaseStream(ProcessStream* stream) {
    if (!stream) return;
    if (!ProcessEventReaped(&stream->box)) {
        ProcessEventMarkKilled(&stream->box);
        if (stream->job) TerminateJobObject(stream->job, 1);
    }
    ProcessStdinStop(&stream->box);
    JoinThread(&stream->waiter);
    JoinThread(&stream->readerOut);
    JoinThread(&stream->readerErr);
    JoinThread(&stream->stdinThread);
    Close(&stream->outRead);
    Close(&stream->errRead);
    Close(&stream->inWrite);
    Close(&stream->process);
    Close(&stream->job);
    ProcessDecodeFree(&stream->outDecode);
    ProcessDecodeFree(&stream->errDecode);
    ProcessEventBoxFree(&stream->box);
    StrFree(stream->command);
    StrFree(stream->cwd);
    delete stream;
}

bool ProcessStreamStart(Str command, const Str* args, int count,
                        const ProcessSpawnOptions* options, ProcessStream** out,
                        Str* error) {
    if (out) *out = nullptr;
    if (error) {
        StrFree(*error);
        *error = {};
    }
    WCHAR* executable = ResolveExecutable(command, error);
    if (!executable) return false;
    Vec<WCHAR> commandLine;
    bool built = AppendArgument(&commandLine, command);
    for (int i = 0; built && i < count; i++)
        built = AppendArgument(&commandLine, args[i]);
    if (built) built = VecAppend(commandLine, (WCHAR)0);
    if (!built) {
        Free(nullptr, executable);
        ProcessError(error,
                     StrL("building the child-process command line failed"));
        return false;
    }

    ProcessStream* stream = new ProcessStream();
    ProcessEventBoxInit(&stream->box, options && options->stdinPipe);
    stream->command = StrDup(command);
    stream->timeoutSeconds = options ? options->timeoutSeconds : 0;
    if (options && options->cwd) stream->cwd = StrDup(options->cwd);
    bool lines = options && options->lines;
    ProcessDecodeInit(&stream->outDecode, lines);
    ProcessDecodeInit(&stream->errDecode, lines);

    SECURITY_ATTRIBUTES security = {};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    HANDLE outWrite = nullptr;
    HANDLE errWrite = nullptr;
    HANDLE inRead = nullptr;
    HANDLE nullInput = nullptr;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
    STARTUPINFOW startup = {};
    PROCESS_INFORMATION process = {};
    WCHAR emptyEnvironment[2] = {};
    WCHAR* directory = nullptr;
    bool ok = true;
    bool wantStdin = false;
    bool threadsOk = false;
    if (!CreatePipe(&stream->outRead, &outWrite, &security, 0) ||
        !CreatePipe(&stream->errRead, &errWrite, &security, 0) ||
        !SetHandleInformation(stream->outRead, HANDLE_FLAG_INHERIT, 0) ||
        !SetHandleInformation(stream->errRead, HANDLE_FLAG_INHERIT, 0)) {
        ProcessError(error, fmt("creating child-process pipes failed with "
                                "Windows error %u",
                                GetLastError()));
        ok = false;
        goto done;
    }
    if (options && options->stdinPipe) {
        if (!CreatePipe(&inRead, &stream->inWrite, &security, 0) ||
            !SetHandleInformation(stream->inWrite, HANDLE_FLAG_INHERIT, 0)) {
            ProcessError(error, fmt("creating the child stdin pipe failed with "
                                    "Windows error %u",
                                    GetLastError()));
            ok = false;
            goto done;
        }
    } else {
        nullInput = CreateFileW(L"NUL", GENERIC_READ,
                                FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (nullInput == INVALID_HANDLE_VALUE) {
            nullInput = nullptr;
            ProcessError(error, fmt("opening the child-process null input "
                                    "failed with Windows error %u",
                                    GetLastError()));
            ok = false;
            goto done;
        }
    }
    stream->job = CreateJobObjectW(nullptr, nullptr);
    if (!stream->job) {
        ProcessError(error, fmt("creating the child-process Job Object failed "
                                "with Windows error %u",
                                GetLastError()));
        ok = false;
        goto done;
    }
    limits.BasicLimitInformation
        .LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(stream->job, JobObjectExtendedLimitInformation,
                                 &limits, sizeof(limits))) {
        ProcessError(error, fmt("configuring the child-process Job Object "
                                "failed with Windows error %u",
                                GetLastError()));
        ok = false;
        goto done;
    }
    if (stream->cwd) {
        directory = WideDup(stream->cwd);
        if (!directory) {
            ProcessError(error, StrL("the child-process working directory is "
                                     "not valid UTF-8"));
            ok = false;
            goto done;
        }
    }
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = (options && options->stdinPipe) ? inRead : nullInput;
    startup.hStdOutput = outWrite;
    startup.hStdError = errWrite;
    if (!CreateProcessW(
            executable, commandLine.els, nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT,
            emptyEnvironment, directory, &startup, &process)) {
        ProcessError(error, fmt("running `%s` failed with Windows error %u",
                                command, GetLastError()));
        ok = false;
        goto done;
    }
    stream->process = process.hProcess;
    stream->pid = process.dwProcessId;
    if (!AssignProcessToJobObject(stream->job, stream->process) ||
        ResumeThread(process.hThread) == (DWORD)-1) {
        ProcessError(error,
                     fmt("isolating `%s` process tree failed with Windows "
                         "error %u",
                         command, GetLastError()));
        ok = false;
        goto done;
    }
    CloseHandle(process.hThread);
    process.hThread = nullptr;
    Close(&outWrite);
    Close(&errWrite);
    Close(&inRead);
    Close(&nullInput);
    // Readers are stored before the waiter starts. The waiter joins those
    // handles and clears the fields, so it must not race this assignment.
    wantStdin = options && options->stdinPipe;
    stream
        ->readerOut = StartReader(stream, &stream->outRead, &stream->outDecode,
                                  ProcessStreamEvent::Kind::Out);
    stream
        ->readerErr = StartReader(stream, &stream->errRead, &stream->errDecode,
                                  ProcessStreamEvent::Kind::Err);
    if (wantStdin) {
        stream->stdinThread =
            CreateThread(nullptr, 0, StreamStdinMain, stream, 0, nullptr);
    }
    threadsOk = stream->readerOut && stream->readerErr &&
                (!wantStdin || stream->stdinThread);
    if (threadsOk) {
        stream->waiter =
            CreateThread(nullptr, 0, StreamWaiterMain, stream, 0, nullptr);
        threadsOk = stream->waiter != nullptr;
    }
    if (!threadsOk) {
        ProcessError(error, fmt("running `%s` failed: a stream thread could "
                                "not be started",
                                command));
        ok = false;
    }

done:
    Close(&outWrite);
    Close(&errWrite);
    Close(&inRead);
    Close(&nullInput);
    if (process.hThread) CloseHandle(process.hThread);
    Free(nullptr, executable);
    Free(nullptr, directory);
    if (!ok) {
        ReleaseStream(stream);
        return false;
    }
    if (out) *out = stream;
    return true;
}

uint32_t ProcessStreamPid(const ProcessStream* stream) {
    return stream ? stream->pid : 0;
}

void ProcessStreamPoll(ProcessStream* stream, Vec<ProcessStreamEvent>* out) {
    if (stream) ProcessEventTake(&stream->box, out);
}

bool ProcessStreamWrite(ProcessStream* stream, Str text, Str* error) {
    if (!stream) return false;
    return ProcessStdinEnqueue(&stream->box, text, error);
}

void ProcessStreamEnd(ProcessStream* stream) {
    if (stream) ProcessStdinEnd(&stream->box);
}

void ProcessStreamKill(ProcessStream* stream) {
    if (!stream || ProcessEventReaped(&stream->box)) return;
    ProcessEventMarkKilled(&stream->box);
    if (stream->job) TerminateJobObject(stream->job, 1);
    ProcessStdinStop(&stream->box);
}

void ProcessStreamFree(ProcessStream* stream) {
    ReleaseStream(stream);
}

} // namespace gpui::shell
