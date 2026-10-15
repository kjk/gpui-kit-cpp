#ifndef GPUI_SHELL_PROCESS_H_
#define GPUI_SHELL_PROCESS_H_

#include "base.h"

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

} // namespace gpui::shell
#endif // GPUI_SHELL_PROCESS_H_
