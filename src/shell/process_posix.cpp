#include "shell/process.h"

#include "shell/error.h"
#include "sys/clock.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

// libc's environ. Qualified at the use so a using-directive for base,
// which declares the same symbol, does not make the name ambiguous
// when this file is compiled inside the amalgam.
extern "C" char** environ;

namespace gpui::shell {

// A child deadline is wall time. TimeNow() is the frame clock, and the
// test platform replaces it, so a timeout would never arrive while a
// test holds that clock still.
static bool HasSlash(Str command) {
    for (int i = 0; i < len(command); i++) {
        if (command.s[i] == '/') return true;
    }
    return false;
}

static Str ResolveExecutable(Str command, Str* error) {
    if (HasSlash(command)) {
        if (access(command.s, X_OK) == 0) return StrDup(command);
        ErrorOutSet(error, fmt("running `%s` failed: executable was not found",
                               command));
        return {};
    }
    const char* path = getenv("PATH");
    if (!path) {
        ErrorOutSet(error,
                    fmt("running `%s` failed: PATH is not set", command));
        return {};
    }
    const char* at = path;
    while (*at) {
        const char* end = strchr(at, ':');
        int len = end ? (int)(end - at) : (int)strlen(at);
        Str candidate = StrDup(fmt("%s/%s", Str(at, len), command));
        if (access(candidate.s, X_OK) == 0) return candidate;
        StrFree(candidate);
        if (!end) break;
        at = end + 1;
    }
    ErrorOutSet(
        error,
        fmt("running `%s` failed: executable was not found on PATH", command));
    return {};
}

// In a forked child: stdin reads end of file, as Rust's Stdio::null gives.
static void StdinFromDevNull() {
    int nullFd = open("/dev/null", O_RDONLY);
    if (nullFd >= 0) dup2(nullFd, STDIN_FILENO);
}

static bool DrainFd(int fd, StrBuilder* out, bool* closed, Str* error,
                    const char* name) {
    char bytes[8192];
    for (;;) {
        ssize_t n = read(fd, bytes, sizeof(bytes));
        if (n > 0) {
            if (!ProcessCaptureAppend(out, bytes, (int)n, error, name))
                return false;
            continue;
        }
        if (n == 0) {
            *closed = true;
            close(fd);
            return true;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) return true;
        if (errno == EINTR) continue;
        ErrorOutSet(error, fmt("reading child %s failed", Str(name)));
        return false;
    }
}

bool ProcessRunBounded(Str command, const Str* args, int count,
                       ProcessCancellation* cancellation, ProcessOutput* output,
                       Str* error, const ProcessOptions* options) {
    if (output) output->Free();
    ErrorOutClear(error);
    Str executable = ResolveExecutable(command, error);
    if (!executable) return false;
    int outPipe[2] = {-1, -1};
    int errPipe[2] = {-1, -1};
    if (pipe(outPipe) != 0 || pipe(errPipe) != 0) {
        if (outPipe[0] >= 0) {
            close(outPipe[0]);
            close(outPipe[1]);
        }
        ErrorOutSet(error, StrL("creating child-process pipes failed"));
        StrFree(executable);
        return false;
    }
    char** argv = AllocArray<char*>(count + 2);
    argv[0] = executable.s;
    for (int i = 0; i < count; i++) argv[i + 1] = args[i].s;
    argv[count + 1] = nullptr;
    int extra = options ? options->environmentCount : 0;
    int inherited = 0;
    if (options && options->inheritEnvironment) {
        while (::environ[inherited]) inherited++;
    }
    char** envp = AllocArray<char*>(inherited + extra + 1);
    for (int i = 0; i < inherited; i++) envp[i] = ::environ[i];
    for (int i = 0; i < extra; i++)
        envp[inherited + i] = options->environment[i].s;
    envp[inherited + extra] = nullptr;
    pid_t pid = fork();
    if (pid == 0) {
        setpgid(0, 0);
        if (options && options->workingDirectory &&
            chdir(options->workingDirectory.s) != 0) {
            _exit(127);
        }
        dup2(outPipe[1], STDOUT_FILENO);
        dup2(errPipe[1], STDERR_FILENO);
        StdinFromDevNull();
        close(outPipe[0]);
        close(outPipe[1]);
        close(errPipe[0]);
        close(errPipe[1]);
        execve(executable.s, argv, envp);
        _exit(127);
    }
    Free(nullptr, envp);
    Free(nullptr, argv);
    StrFree(executable);
    close(outPipe[1]);
    close(errPipe[1]);
    if (pid < 0) {
        close(outPipe[0]);
        close(errPipe[0]);
        ErrorOutSet(error, fmt("running `%s` failed", command));
        return false;
    }
    fcntl(outPipe[0], F_SETFL, fcntl(outPipe[0], F_GETFL) | O_NONBLOCK);
    fcntl(errPipe[0], F_SETFL, fcntl(errPipe[0], F_GETFL) | O_NONBLOCK);
    StrBuilder stdoutText, stderrText;
    bool stdoutClosed = false, stderrClosed = false, done = false, ok = true;
    int status = 0;
    double started = ClockMonotonicSecondsSinceStart();
    double timeoutSeconds = ProcessBoundedTimeoutSeconds(options);
    while (!done || !stdoutClosed || !stderrClosed) {
        if (!stdoutClosed)
            ok = DrainFd(outPipe[0], &stdoutText, &stdoutClosed, error,
                         "stdout");
        if (ok && !stderrClosed)
            ok = DrainFd(errPipe[0], &stderrText, &stderrClosed, error,
                         "stderr");
        bool cancelled = cancellation && cancellation->IsCancelled();
        if (!ok || cancelled ||
            ClockMonotonicSecondsSinceStart() - started >= timeoutSeconds) {
            if (ok)
                ProcessBoundedStopError(command, cancelled, timeoutSeconds,
                                        error);
            kill(-pid, SIGKILL);
            waitpid(pid, &status, 0);
            done = true;
            ok = false;
            break;
        }
        if (!done) {
            pid_t waited = waitpid(pid, &status, WNOHANG);
            if (waited == pid)
                done = true;
            else if (waited < 0 && errno != EINTR) {
                ErrorOutSet(error, fmt("waiting for `%s` failed", command));
                ok = false;
                break;
            }
        }
        if (!done || !stdoutClosed || !stderrClosed) PlatSleepMs(5);
    }
    if (!stdoutClosed) close(outPipe[0]);
    if (!stderrClosed) close(errPipe[0]);
    if (!ok) {
        if (!done) {
            kill(-pid, SIGKILL);
            waitpid(pid, &status, 0);
        }
        return false;
    }
    if (output) {
        output->code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        output->out = stdoutText.TakeStr();
        output->err = stderrText.TakeStr();
    }
    return true;
}

// A streaming child. The waiter joins the readers before it publishes Exit.
static char* kEmptyEnv[] = {nullptr};

struct ProcessStream : ProcessStreamCore {
    Vec<Str> owned;
    char** argv = nullptr;
    pid_t child = -1;
    int outFd = -1;
    int errFd = -1;
    int inFd = -1;
    pthread_t readerOut = {};
    pthread_t readerErr = {};
    pthread_t waiter = {};
    pthread_t stdinThread = {};
    bool haveReaderOut = false;
    bool haveReaderErr = false;
    bool haveWaiter = false;
    bool haveStdin = false;
};

struct PosixReader {
    ProcessStream* stream = nullptr;
    int* fd = nullptr;
    ProcessDecodeState* decode = nullptr;
    ProcessStreamEvent::Kind kind = ProcessStreamEvent::Kind::Out;
};

static Str SignalName(int signal) {
    switch (signal) {
        case SIGKILL:
            return StrDup(StrL("SIGKILL"));
        case SIGTERM:
            return StrDup(StrL("SIGTERM"));
        case SIGINT:
            return StrDup(StrL("SIGINT"));
        case SIGHUP:
            return StrDup(StrL("SIGHUP"));
        case SIGQUIT:
            return StrDup(StrL("SIGQUIT"));
        case SIGABRT:
            return StrDup(StrL("SIGABRT"));
        case SIGSEGV:
            return StrDup(StrL("SIGSEGV"));
        case SIGPIPE:
            return StrDup(StrL("SIGPIPE"));
        default:
            return StrDup(fmt("signal %d", signal));
    }
}

static void CloseFd(int* fd) {
    if (fd && *fd >= 0) close(*fd);
    if (fd) *fd = -1;
}

static void* PosixReaderMain(void* param) {
    PosixReader* launch = (PosixReader*)param;
    ProcessStream* stream = launch->stream;
    int fd = launch->fd ? *launch->fd : -1;
    uint8_t chunk[64 * 1024];
    bool failed = false;
    for (;;) {
        ssize_t count = fd >= 0 ? read(fd, chunk, sizeof(chunk)) : 0;
        if (count == 0) break;
        if (count < 0) {
            if (errno == EINTR) continue;
            break;
        }
        Str error;
        if (!ProcessDecodeFeed(launch->decode, &stream->box, launch->kind,
                               chunk, (int)count, &error)) {
            ProcessEventSetReason(&stream->box, error);
            if (stream->child > 0 && !ProcessEventReaped(&stream->box))
                kill(-stream->child, SIGKILL);
            failed = true;
            break;
        }
    }
    if (!failed)
        ProcessDecodeFinish(launch->decode, &stream->box, launch->kind);
    ProcessDecodeFree(launch->decode);
    if (fd >= 0) close(fd);
    if (launch->fd) *launch->fd = -1;
    delete launch;
    return nullptr;
}

static int PosixStdinWrite(void* ctx, const char* bytes, int count) {
    ProcessStream* stream = (ProcessStream*)ctx;
    for (;;) {
        ssize_t wrote = write(stream->inFd, bytes, (size_t)count);
        if (wrote >= 0) return (int)wrote;
        if (errno != EINTR) return -1;
    }
}

// Joins a stream thread once, if it was started.
static void JoinThread(pthread_t* thread, bool* started) {
    if (!*started) return;
    pthread_join(*thread, nullptr);
    *started = false;
}

static void* PosixStdinMain(void* param) {
    ProcessStream* stream = (ProcessStream*)param;
    ProcessStdinPump(&stream->box, PosixStdinWrite, stream);
    CloseFd(&stream->inFd);
    return nullptr;
}

static void* PosixWaiterMain(void* param) {
    ProcessStream* stream = (ProcessStream*)param;
    double started = ClockMonotonicSecondsSinceStart();
    int status = 0;
    bool waitFailed = false;
    for (;;) {
        pid_t got = waitpid(stream->child, &status, WNOHANG);
        if (got == stream->child) break;
        if (got < 0 && errno != EINTR) {
            ProcessEventSetReason(
                &stream->box,
                StrDup(fmt("waiting for `%s` failed", stream->command)));
            waitFailed = true;
            break;
        }
        if (stream->timeoutSeconds > 0 &&
            ClockMonotonicSecondsSinceStart() - started >=
                stream->timeoutSeconds) {
            ProcessStreamCoreTimedOut(stream);
            if (stream->child > 0) kill(-stream->child, SIGKILL);
        }
        PlatSleepMs(20);
    }
    if (stream->child > 0) kill(-stream->child, SIGKILL);
    ProcessEventMarkReaped(&stream->box);
    JoinThread(&stream->readerOut, &stream->haveReaderOut);
    JoinThread(&stream->readerErr, &stream->haveReaderErr);
    ProcessStreamEvent event;
    if (!waitFailed) {
        if (WIFSIGNALED(status)) {
            event.signal = SignalName(WTERMSIG(status));
        } else {
            event.hasCode = true;
            event.code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        }
    }
    ProcessEventPushExit(&stream->box, event);
    return nullptr;
}

static bool StartPosixReader(ProcessStream* stream, pthread_t* thread, int* fd,
                             ProcessDecodeState* decode,
                             ProcessStreamEvent::Kind kind) {
    PosixReader* launch = new PosixReader();
    launch->stream = stream;
    launch->fd = fd;
    launch->decode = decode;
    launch->kind = kind;
    if (pthread_create(thread, nullptr, PosixReaderMain, launch) != 0) {
        delete launch;
        return false;
    }
    return true;
}

static void ReleaseStream(ProcessStream* stream) {
    if (!stream) return;
    if (stream->child > 0 && !ProcessEventReaped(&stream->box)) {
        ProcessEventMarkKilled(&stream->box);
        kill(-stream->child, SIGKILL);
    }
    ProcessStdinStop(&stream->box);
    JoinThread(&stream->waiter, &stream->haveWaiter);
    JoinThread(&stream->readerOut, &stream->haveReaderOut);
    JoinThread(&stream->readerErr, &stream->haveReaderErr);
    JoinThread(&stream->stdinThread, &stream->haveStdin);
    if (stream->child > 0 && !ProcessEventReaped(&stream->box)) {
        int status = 0;
        while (waitpid(stream->child, &status, 0) < 0 && errno == EINTR) {
        }
    }
    CloseFd(&stream->outFd);
    CloseFd(&stream->errFd);
    CloseFd(&stream->inFd);
    ProcessStreamCoreFree(stream);
    for (int i = 0; i < len(stream->owned); i++) StrFree(stream->owned[i]);
    VecReset(stream->owned);
    Free(nullptr, stream->argv);
    delete stream;
}

bool ProcessStreamStart(Str command, const Str* args, int count,
                        const ProcessSpawnOptions* options, ProcessStream** out,
                        Str* error) {
    if (out) *out = nullptr;
    ErrorOutClear(error);
    Str executable = ResolveExecutable(command, error);
    if (!executable) return false;
    ProcessStream* stream = new ProcessStream();
    bool stdinPipe = options && options->stdinPipe;
    ProcessStreamCoreInit(stream, command, options);
    VecAppend(stream->owned, executable);
    for (int i = 0; i < count; i++) VecAppend(stream->owned, StrDup(args[i]));
    stream->argv = AllocArray<char*>(len(stream->owned) + 1);
    for (int i = 0; i < len(stream->owned); i++)
        stream->argv[i] = stream->owned[i].s;

    int outPipe[2] = {-1, -1};
    int errPipe[2] = {-1, -1};
    int inPipe[2] = {-1, -1};
    if (pipe(outPipe) != 0 || pipe(errPipe) != 0 ||
        (stdinPipe && pipe(inPipe) != 0)) {
        CloseFd(&outPipe[0]);
        CloseFd(&outPipe[1]);
        CloseFd(&errPipe[0]);
        CloseFd(&errPipe[1]);
        CloseFd(&inPipe[0]);
        CloseFd(&inPipe[1]);
        ErrorOutSet(error, StrL("creating child-process pipes failed"));
        ReleaseStream(stream);
        return false;
    }
    pid_t pid = fork();
    if (pid == 0) {
        setpgid(0, 0);
        if (stream->cwd.s && chdir(stream->cwd.s) != 0) _exit(127);
        dup2(outPipe[1], STDOUT_FILENO);
        dup2(errPipe[1], STDERR_FILENO);
        if (stdinPipe) {
            dup2(inPipe[0], STDIN_FILENO);
        } else {
            StdinFromDevNull();
        }
        close(outPipe[0]);
        close(outPipe[1]);
        close(errPipe[0]);
        close(errPipe[1]);
        if (inPipe[0] >= 0) close(inPipe[0]);
        if (inPipe[1] >= 0) close(inPipe[1]);
        execve(stream->argv[0], stream->argv, kEmptyEnv);
        _exit(127);
    }
    close(outPipe[1]);
    close(errPipe[1]);
    if (inPipe[0] >= 0) close(inPipe[0]);
    if (pid < 0) {
        close(outPipe[0]);
        close(errPipe[0]);
        if (inPipe[1] >= 0) close(inPipe[1]);
        ErrorOutSet(error, fmt("running `%s` failed", command));
        ReleaseStream(stream);
        return false;
    }
    setpgid(pid, pid);
    stream->child = pid;
    stream->pid = (uint32_t)pid;
    stream->outFd = outPipe[0];
    stream->errFd = errPipe[0];
    if (stdinPipe) stream->inFd = inPipe[1];

    bool threadsOk =
        StartPosixReader(stream, &stream->readerOut, &stream->outFd,
                         &stream->outDecode, ProcessStreamEvent::Kind::Out);
    if (threadsOk) stream->haveReaderOut = true;
    if (threadsOk) {
        threadsOk =
            StartPosixReader(stream, &stream->readerErr, &stream->errFd,
                             &stream->errDecode, ProcessStreamEvent::Kind::Err);
        if (threadsOk) stream->haveReaderErr = true;
    }
    if (threadsOk && stdinPipe) {
        threadsOk = pthread_create(&stream->stdinThread, nullptr,
                                   PosixStdinMain, stream) == 0;
        if (threadsOk) stream->haveStdin = true;
    }
    if (threadsOk) {
        threadsOk = pthread_create(&stream->waiter, nullptr, PosixWaiterMain,
                                   stream) == 0;
        if (threadsOk) stream->haveWaiter = true;
    }
    if (!threadsOk) ProcessStreamThreadsFailed(command, error);
    return ProcessStreamStarted(stream, threadsOk, out);
}

ProcessStreamCore* ProcessStreamCoreOf(ProcessStream* stream) {
    return stream;
}

void ProcessStreamKill(ProcessStream* stream) {
    if (!stream || stream->child <= 0 || ProcessEventReaped(&stream->box))
        return;
    ProcessEventMarkKilled(&stream->box);
    kill(-stream->child, SIGKILL);
    ProcessStdinStop(&stream->box);
}

void ProcessStreamFree(ProcessStream* stream) {
    ReleaseStream(stream);
}

} // namespace gpui::shell
