#ifndef GPUI_SHELL_PROCESS_H_
#define GPUI_SHELL_PROCESS_H_

#include "base.h"
#include "sys/clock.h"

namespace gpui::shell {

struct ProcessCancellation {
    Mutex mutex;
    bool cancelled = false;

    void Cancel();
    bool IsCancelled();
};

struct ProcessOutput {
    int code = -1;
    Str out;
    Str err;

    void Free();
};

// What a host-owned run needs beyond `process.run`'s. Scripts never reach
// this: the dependency fetcher runs `git` in a cache directory, and git wants
// the host environment the way Rust's `Command` gives it, plus the two
// variables that keep a credential prompt from blocking a background fetch.
struct ProcessOptions {
    // Empty: the child inherits the host's working directory.
    Str workingDirectory;
    // "NAME=VALUE" pairs added to whichever environment is selected below.
    const Str* environment = nullptr;
    int environmentCount = 0;
    // False — `process.run`'s contract — clears the child environment.
    bool inheritEnvironment = false;
    // 0 keeps process.run's 30 second deadline. A caller that asked for
    // `timeoutMs` passes that duration in seconds.
    double timeoutSeconds = 0;
};

// Runs one explicitly granted executable with an empty child environment,
// captures each output stream up to 8 MiB, and kills the process tree after
// 30 seconds unless `options->timeoutSeconds` names another deadline. This
// is blocking and belongs on ExecSpawn's worker half.
bool ProcessRunBounded(Str command, const Str* args, int count,
                       ProcessCancellation* cancellation, ProcessOutput* out,
                       Str* error, const ProcessOptions* options = nullptr);

// The longest line a line-mode child may write before it is stopped. Total
// output is not bounded: a long-running child streams for as long as it runs.
constexpr int kProcessMaxLineBytes = 8 * 1024 * 1024;

// UTF-8 decoding that survives a character split across two reads. An
// incomplete tail is held; a byte that can never be valid becomes U+FFFD.
struct Utf8Carry {
    uint8_t pending[4] = {};
    int pendingLen = 0;
};

void Utf8CarryInit(Utf8Carry* carry);
Str Utf8CarryDecode(Utf8Carry* carry, const uint8_t* bytes, int count);
Str Utf8CarryFinish(Utf8Carry* carry);

// Joins a line that spans several reads. The strings in `lines` are owned
// by the caller. False means a line passed kProcessMaxLineBytes.
struct LineCarry {
    StrBuilder partial;
};

void LineCarryInit(LineCarry* carry);
void LineCarryFree(LineCarry* carry);
bool LineCarryPush(LineCarry* carry, Str text, Vec<Str>* lines, Str* error);
Str LineCarryFinish(LineCarry* carry);

// What a streaming child was asked to do. `timeoutSeconds <= 0` is no
// deadline, unlike process.run.
struct ProcessSpawnOptions {
    Str cwd;
    bool stdinPipe = false;
    bool lines = false;
    double timeoutSeconds = 0;
};

struct ProcessStreamEvent {
    enum class Kind : uint8_t {
        Out,
        Err,
        Exit
    };
    Kind kind = Kind::Out;
    Str text;
    bool hasCode = false;
    int code = 0;
    Str signal;
    bool hasError = false;

    void Free();
};

// A running child. The platform file owns the OS handles; this header only
// moves events and stdin bytes. Exit is always the last event, and only
// after both streams have delivered what they had.
struct ProcessStream;

bool ProcessStreamStart(Str command, const Str* args, int count,
                        const ProcessSpawnOptions* options, ProcessStream** out,
                        Str* error);
uint32_t ProcessStreamPid(const ProcessStream* stream);
void ProcessStreamPoll(ProcessStream* stream, Vec<ProcessStreamEvent>* out);
bool ProcessStreamWrite(ProcessStream* stream, Str text, Str* error);
void ProcessStreamEnd(ProcessStream* stream);
void ProcessStreamKill(ProcessStream* stream);
void ProcessStreamFree(ProcessStream* stream);

// Shared by the platform streams. The OS handles stay in the platform file.
struct ProcessEventBox {
    Mutex mutex;
    Vec<ProcessStreamEvent> pending;
    bool killed = false;
    bool reaped = false;
    Str reason;
    Mutex stdinMutex;
    CondVar stdinCv;
    Vec<char> stdinBytes;
    bool stdinPiped = false;
    bool stdinEnded = false;
    bool stdinFailed = false;
    bool stdinStop = false;
};

struct ProcessDecodeState {
    Utf8Carry utf8;
    LineCarry lines;
    bool lineMode = false;
};

void ProcessEventBoxInit(ProcessEventBox* box, bool stdinPipe);
void ProcessEventBoxFree(ProcessEventBox* box);
void ProcessEventPush(ProcessEventBox* box, ProcessStreamEvent event);
void ProcessEventTake(ProcessEventBox* box, Vec<ProcessStreamEvent>* out);
void ProcessEventSetReason(ProcessEventBox* box, Str reason);
Str ProcessEventTakeReason(ProcessEventBox* box);
bool ProcessEventKilled(ProcessEventBox* box);
void ProcessEventMarkKilled(ProcessEventBox* box);
bool ProcessEventReaped(ProcessEventBox* box);
void ProcessEventMarkReaped(ProcessEventBox* box);
bool ProcessStdinEnqueue(ProcessEventBox* box, Str text, Str* error);
void ProcessStdinEnd(ProcessEventBox* box);
void ProcessStdinStop(ProcessEventBox* box);
void ProcessStdinFail(ProcessEventBox* box);
// Copies queued bytes into `out`. False when the writer should exit.
bool ProcessStdinTake(ProcessEventBox* box, Vec<char>* out, bool* ended);

void ProcessDecodeInit(ProcessDecodeState* state, bool lines);
void ProcessDecodeFree(ProcessDecodeState* state);
bool ProcessDecodeFeed(ProcessDecodeState* state, ProcessEventBox* box,
                       ProcessStreamEvent::Kind kind, const uint8_t* bytes,
                       int count, Str* error);
void ProcessDecodeFinish(ProcessDecodeState* state, ProcessEventBox* box,
                         ProcessStreamEvent::Kind kind);

// What every platform's ProcessStream carries before its OS handles. The
// platform struct derives from it, so the shared half is reached by name.
struct ProcessStreamCore {
    ProcessEventBox box;
    Str command;
    Str cwd;
    double timeoutSeconds = 0;
    uint32_t pid = 0;
    ProcessDecodeState outDecode;
    ProcessDecodeState errDecode;
};

void ProcessStreamCoreInit(ProcessStreamCore* stream, Str command,
                           const ProcessSpawnOptions* options);
// Frees the event box, both decoders and the owned strings. The platform has
// joined its threads and closed its handles first.
void ProcessStreamCoreFree(ProcessStreamCore* stream);
// Records the deadline as the reason the waiter is about to kill the child.
void ProcessStreamCoreTimedOut(ProcessStreamCore* stream);
// The shared half of a platform's stream: the derived-to-base conversion,
// written where ProcessStream is complete, so ProcessStreamPid, Poll, Write
// and End are written once in process.cpp. Null for null.
ProcessStreamCore* ProcessStreamCoreOf(ProcessStream* stream);
// The end of a platform's ProcessStreamStart: hand the stream out when it
// started, or free it (ProcessStreamFree) and answer false.
bool ProcessStreamStarted(ProcessStream* stream, bool ok, ProcessStream** out);
// The error for a stream whose reader, writer or waiter thread did not start.
void ProcessStreamThreadsFailed(Str command, Str* error);

// The stdin writer thread's body: hands queued bytes to writeSome until the
// script ends the stream, the stream stops, or a write fails. writeSome writes
// up to count bytes and returns how many, or <= 0 when the pipe is gone.
void ProcessStdinPump(ProcessEventBox* box,
                      int (*writeSome)(void* ctx, const char* bytes, int count),
                      void* ctx);

// Publishes the Exit event the waiter built, carrying whichever failure was
// recorded first as its error. Exit is always the last event.
void ProcessEventPushExit(ProcessEventBox* box, ProcessStreamEvent event);

int ProcessTimeoutMilliseconds(double seconds);

// Appends one read from a bounded run's stdout or stderr (`name`), refusing
// to let either grow past 8 MiB.
bool ProcessCaptureAppend(StrBuilder* out, const char* bytes, int count,
                          Str* error, const char* name);

// process.run's deadline: `options->timeoutSeconds`, or 30 seconds.
double ProcessBoundedTimeoutSeconds(const ProcessOptions* options);
// The reason a bounded run stopped before its child exited.
void ProcessBoundedStopError(Str command, bool cancelled, double timeoutSeconds,
                             Str* error);

// The bounded run's pump, the same on every host. Each pass drains stdout,
// then stderr, until both close (`drain(stream, out, closed, name)`, stream 0
// or 1, false on a read error that set `error`); a drain error, cancellation
// or the deadline calls `stop()` to end the child and makes the run fail;
// `poll()` answers 1 once the child has exited, 0 while it runs and -1 when
// waiting failed (it set `error`). True when the child exited and both pipes
// closed. `*exited` is whether `poll()` saw the exit.
template <typename Drain, typename Stop, typename Poll>
bool ProcessBoundedPump(Str command, const ProcessOptions* options,
                        ProcessCancellation* cancellation, Str* error,
                        StrBuilder* out, StrBuilder* err, bool closed[2],
                        bool* exited, Drain drain, Stop stop, Poll poll) {
    double started = ClockMonotonicSecondsSinceStart();
    double timeoutSeconds = ProcessBoundedTimeoutSeconds(options);
    while (!*exited || !closed[0] || !closed[1]) {
        bool ok = true;
        if (!closed[0]) ok = drain(0, out, &closed[0], "stdout");
        if (ok && !closed[1]) ok = drain(1, err, &closed[1], "stderr");
        bool cancelled = cancellation && cancellation->IsCancelled();
        if (!ok || cancelled ||
            ClockMonotonicSecondsSinceStart() - started >= timeoutSeconds) {
            if (ok)
                ProcessBoundedStopError(command, cancelled, timeoutSeconds,
                                        error);
            stop();
            return false;
        }
        if (!*exited) {
            int polled = poll();
            if (polled < 0) return false;
            *exited = polled > 0;
        }
        if (!*exited || !closed[0] || !closed[1]) PlatSleepMs(5);
    }
    return true;
}

} // namespace gpui::shell
#endif // GPUI_SHELL_PROCESS_H_
