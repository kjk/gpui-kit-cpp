#include "shell/process.h"

#if !GPUI_OS_WINDOWS && !GPUI_OS_WASM

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace gpui::shell {

constexpr int kProcessStreamLimit = 8 * 1024 * 1024;
constexpr double kProcessTimeout = 30.0;

static void ProcessError(Str* error, Str message) {
    if (!error) return;
    StrFree(*error);
    *error = StrDup(message);
}

static bool HasSlash(Str command) {
    for (int i = 0; i < len(command); i++) {
        if (command.s[i] == '/') return true;
    }
    return false;
}

static Str ResolveExecutable(Str command, Str* error) {
    if (HasSlash(command)) {
        if (access(command.s, X_OK) == 0) return StrDup(command);
        ProcessError(error, fmt("running `%s` failed: executable was not found",
                                command));
        return {};
    }
    const char* path = getenv("PATH");
    if (!path) {
        ProcessError(error,
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
    ProcessError(
        error,
        fmt("running `%s` failed: executable was not found on PATH", command));
    return {};
}

static bool DrainFd(int fd, StrBuilder* out, bool* closed, Str* error,
                    const char* name) {
    char bytes[8192];
    for (;;) {
        ssize_t n = read(fd, bytes, sizeof(bytes));
        if (n > 0) {
            if (out->len > kProcessStreamLimit - (int)n) {
                ProcessError(error, fmt("child %s exceeded 8 MiB", Str(name)));
                return false;
            }
            out->Append(Str(bytes, (int)n));
            continue;
        }
        if (n == 0) {
            *closed = true;
            close(fd);
            return true;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) return true;
        if (errno == EINTR) continue;
        ProcessError(error, fmt("reading child %s failed", Str(name)));
        return false;
    }
}

bool ProcessRunBounded(Str command, const Str* args, int count,
                       ProcessCancellation* cancellation, ProcessOutput* output,
                       Str* error, const ProcessOptions* options) {
    if (output) output->Free();
    if (error) {
        StrFree(*error);
        *error = {};
    }
    Str executable = ResolveExecutable(command, error);
    if (!executable) return false;
    int outPipe[2] = {-1, -1};
    int errPipe[2] = {-1, -1};
    if (pipe(outPipe) != 0 || pipe(errPipe) != 0) {
        if (outPipe[0] >= 0) {
            close(outPipe[0]);
            close(outPipe[1]);
        }
        ProcessError(error, StrL("creating child-process pipes failed"));
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
        while (environ[inherited]) inherited++;
    }
    char** envp = AllocArray<char*>(inherited + extra + 1);
    for (int i = 0; i < inherited; i++) envp[i] = environ[i];
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
        int nullFd = open("/dev/null", O_RDONLY);
        if (nullFd >= 0) dup2(nullFd, STDIN_FILENO);
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
        ProcessError(error, fmt("running `%s` failed", command));
        return false;
    }
    fcntl(outPipe[0], F_SETFL, fcntl(outPipe[0], F_GETFL) | O_NONBLOCK);
    fcntl(errPipe[0], F_SETFL, fcntl(errPipe[0], F_GETFL) | O_NONBLOCK);
    StrBuilder stdoutText, stderrText;
    bool stdoutClosed = false, stderrClosed = false, done = false, ok = true;
    int status = 0;
    double started = TimeNow();
    while (!done || !stdoutClosed || !stderrClosed) {
        if (!stdoutClosed)
            ok = DrainFd(outPipe[0], &stdoutText, &stdoutClosed, error,
                         "stdout");
        if (ok && !stderrClosed)
            ok = DrainFd(errPipe[0], &stderrText, &stderrClosed, error,
                         "stderr");
        bool cancelled = cancellation && cancellation->IsCancelled();
        if (!ok || cancelled || TimeNow() - started >= kProcessTimeout) {
            if (ok && cancelled) {
                ProcessError(error, fmt("`%s` was cancelled", command));
            } else if (ok) {
                ProcessError(error,
                             fmt("`%s` timed out after 30000 ms", command));
            }
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
                ProcessError(error, fmt("waiting for `%s` failed", command));
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

} // namespace gpui::shell

#elif GPUI_OS_WASM

namespace gpui::shell {
bool ProcessRunBounded(Str command, const Str*, int, ProcessCancellation*,
                       ProcessOutput*, Str* error, const ProcessOptions*) {
    if (error)
        *error =
            StrDup(fmt("running `%s` is unavailable in a browser", command));
    return false;
}
} // namespace gpui::shell

#endif
