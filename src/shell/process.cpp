#include "shell/process.h"

namespace gpui::shell {

void ProcessCancellation::Cancel() {
    mutex.Lock();
    cancelled = true;
    mutex.Unlock();
}

bool ProcessCancellation::IsCancelled() {
    mutex.Lock();
    bool result = cancelled;
    mutex.Unlock();
    return result;
}

void ProcessOutput::Free() {
    StrFree(out);
    StrFree(err);
    *this = {};
}

void ProcessStreamEvent::Free() {
    StrFree(text);
    StrFree(signal);
    text = {};
    signal = {};
    hasCode = false;
    hasError = false;
    code = 0;
    kind = Kind::Out;
}

void Utf8CarryInit(Utf8Carry* carry) {
    if (carry) carry->pendingLen = 0;
}

static int Utf8Width(uint8_t lead) {
    if ((lead & 0x80) == 0) return 1;
    if ((lead & 0xE0) == 0xC0) return 2;
    if ((lead & 0xF0) == 0xE0) return 3;
    if ((lead & 0xF8) == 0xF0) return 4;
    return 0;
}

static bool Utf8SequenceOk(const uint8_t* bytes, int width) {
    if (width <= 1) return width == 1;
    for (int i = 1; i < width; i++) {
        if ((bytes[i] & 0xC0) != 0x80) return false;
    }
    uint32_t cp = 0;
    if (width == 2) {
        cp = ((uint32_t)(bytes[0] & 0x1F) << 6) | (bytes[1] & 0x3F);
        if (cp < 0x80) return false;
    } else if (width == 3) {
        cp = ((uint32_t)(bytes[0] & 0x0F) << 12) |
             ((uint32_t)(bytes[1] & 0x3F) << 6) | (bytes[2] & 0x3F);
        if (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
    } else {
        cp = ((uint32_t)(bytes[0] & 0x07) << 18) |
             ((uint32_t)(bytes[1] & 0x3F) << 12) |
             ((uint32_t)(bytes[2] & 0x3F) << 6) | (bytes[3] & 0x3F);
        if (cp < 0x10000 || cp > 0x10FFFF) return false;
    }
    return true;
}

static Str DecodeBytes(Utf8Carry* carry, const uint8_t* bytes, int count,
                       bool finish) {
    int incoming = count > 0 ? count : 0;
    int total = carry->pendingLen + incoming;
    if (total <= 0) {
        carry->pendingLen = 0;
        return {};
    }
    uint8_t* buf = AllocArray<uint8_t>(total);
    if (!buf) return {};
    for (int i = 0; i < carry->pendingLen; i++) buf[i] = carry->pending[i];
    if (incoming > 0) memcpy(buf + carry->pendingLen, bytes, (size_t)incoming);
    StrBuilder out;
    int i = 0;
    while (i < total) {
        int width = Utf8Width(buf[i]);
        if (width == 0) {
            out.Append(StrL("\xEF\xBF\xBD"));
            i++;
            continue;
        }
        if (i + width > total) {
            if (finish) {
                out.Append(StrL("\xEF\xBF\xBD"));
                i = total;
            }
            break;
        }
        if (!Utf8SequenceOk(buf + i, width)) {
            out.Append(StrL("\xEF\xBF\xBD"));
            i++;
            continue;
        }
        out.Append(Str((const char*)buf + i, width));
        i += width;
    }
    carry->pendingLen = 0;
    if (!finish && i < total) {
        int tail = total - i;
        if (tail > 4) tail = 4;
        for (int n = 0; n < tail; n++) carry->pending[n] = buf[i + n];
        carry->pendingLen = tail;
    }
    Free(nullptr, buf);
    return out.TakeStr();
}

Str Utf8CarryDecode(Utf8Carry* carry, const uint8_t* bytes, int count) {
    if (!carry) return {};
    return DecodeBytes(carry, bytes, count, false);
}

Str Utf8CarryFinish(Utf8Carry* carry) {
    if (!carry) return {};
    return DecodeBytes(carry, nullptr, 0, true);
}

void LineCarryInit(LineCarry* carry) {
    (void)carry;
}

void LineCarryFree(LineCarry* carry) {
    if (carry) VecReset(carry->partial);
}

bool LineCarryPush(LineCarry* carry, Str text, Vec<Str>* lines, Str* error) {
    if (!carry) return false;
    int at = 0;
    while (at < len(text)) {
        int newline = at;
        while (newline < len(text) && text.s[newline] != '\n') newline++;
        if (newline >= len(text)) break;
        carry->partial.Append(Str(text.s + at, newline - at));
        if (carry->partial.len > 0 &&
            carry->partial.els[carry->partial.len - 1] == '\r') {
            carry->partial.len--;
        }
        Str line = carry->partial.TakeStr();
        if (lines) {
            if (!VecAppend(*lines, line)) StrFree(line);
        } else {
            StrFree(line);
        }
        at = newline + 1;
    }
    if (at < len(text)) carry->partial.Append(Str(text.s + at, len(text) - at));
    if (carry->partial.len > kProcessMaxLineBytes) {
        if (error) {
            StrFree(*error);
            *error =
                StrDup(fmt("a line of output exceeded %d MiB without a "
                           "newline",
                           kProcessMaxLineBytes >> 20));
        }
        return false;
    }
    return true;
}

Str LineCarryFinish(LineCarry* carry) {
    if (!carry || carry->partial.len <= 0) return {};
    return carry->partial.TakeStr();
}

void ProcessEventBoxInit(ProcessEventBox* box, bool stdinPipe) {
    if (!box) return;
    box->stdinPiped = stdinPipe;
}

void ProcessEventBoxFree(ProcessEventBox* box) {
    if (!box) return;
    for (int i = 0; i < len(box->pending); i++) box->pending[i].Free();
    VecReset(box->pending);
    StrFree(box->reason);
    box->reason = {};
    VecReset(box->stdinBytes);
}

void ProcessEventPush(ProcessEventBox* box, ProcessStreamEvent event) {
    if (!box) {
        event.Free();
        return;
    }
    box->mutex.Lock();
    if (!VecAppend(box->pending, event)) event.Free();
    box->mutex.Unlock();
}

void ProcessEventTake(ProcessEventBox* box, Vec<ProcessStreamEvent>* out) {
    if (!box || !out) return;
    box->mutex.Lock();
    for (int i = 0; i < len(box->pending); i++)
        VecAppend(*out, box->pending[i]);
    box->pending.len = 0;
    box->mutex.Unlock();
}

void ProcessEventSetReason(ProcessEventBox* box, Str reason) {
    if (!box) {
        StrFree(reason);
        return;
    }
    box->mutex.Lock();
    if (box->reason) {
        StrFree(reason);
    } else {
        box->reason = reason;
    }
    box->mutex.Unlock();
}

Str ProcessEventTakeReason(ProcessEventBox* box) {
    if (!box) return {};
    box->mutex.Lock();
    Str reason = box->reason;
    box->reason = {};
    box->mutex.Unlock();
    return reason;
}

bool ProcessEventKilled(ProcessEventBox* box) {
    if (!box) return false;
    box->mutex.Lock();
    bool killed = box->killed;
    box->mutex.Unlock();
    return killed;
}

void ProcessEventMarkKilled(ProcessEventBox* box) {
    if (!box) return;
    box->mutex.Lock();
    box->killed = true;
    box->mutex.Unlock();
}

bool ProcessEventReaped(ProcessEventBox* box) {
    if (!box) return false;
    box->mutex.Lock();
    bool reaped = box->reaped;
    box->mutex.Unlock();
    return reaped;
}

void ProcessEventMarkReaped(ProcessEventBox* box) {
    if (!box) return;
    box->mutex.Lock();
    box->reaped = true;
    box->mutex.Unlock();
}

bool ProcessStdinEnqueue(ProcessEventBox* box, Str text, Str* error) {
    if (!box) return false;
    box->stdinMutex.Lock();
    if (!box->stdinPiped) {
        box->stdinMutex.Unlock();
        if (error)
            *error = StrDup(StrL(
                "ChildProcess.write(text) needs a child spawned with { stdin: "
                "\"pipe\" }"));
        return false;
    }
    if (box->stdinEnded) {
        box->stdinMutex.Unlock();
        if (error) *error = StrDup(StrL("stdin has already been closed"));
        return false;
    }
    if (box->stdinFailed || box->stdinStop) {
        box->stdinMutex.Unlock();
        if (error)
            *error = StrDup(
                StrL("stdin is closed: the process is no longer reading it"));
        return false;
    }
    for (int i = 0; i < len(text); i++) VecAppend(box->stdinBytes, text.s[i]);
    box->stdinCv.WakeOne();
    box->stdinMutex.Unlock();
    return true;
}

void ProcessStdinEnd(ProcessEventBox* box) {
    if (!box) return;
    box->stdinMutex.Lock();
    box->stdinEnded = true;
    box->stdinCv.WakeOne();
    box->stdinMutex.Unlock();
}

void ProcessStdinStop(ProcessEventBox* box) {
    if (!box) return;
    box->stdinMutex.Lock();
    box->stdinStop = true;
    box->stdinCv.WakeOne();
    box->stdinMutex.Unlock();
}

void ProcessStdinFail(ProcessEventBox* box) {
    if (!box) return;
    box->stdinMutex.Lock();
    box->stdinFailed = true;
    box->stdinCv.WakeOne();
    box->stdinMutex.Unlock();
}

bool ProcessStdinTake(ProcessEventBox* box, Vec<char>* out, bool* ended) {
    if (ended) *ended = false;
    if (!box || !out) return false;
    box->stdinMutex.Lock();
    while (box->stdinBytes.len == 0 && !box->stdinEnded && !box->stdinStop &&
           !box->stdinFailed) {
        box->stdinCv.Wait(&box->stdinMutex, 200);
    }
    bool stop = box->stdinStop || box->stdinFailed;
    bool done = box->stdinEnded;
    if (box->stdinBytes.len > 0 && !stop) {
        for (int i = 0; i < box->stdinBytes.len; i++)
            VecAppend(*out, box->stdinBytes[i]);
        box->stdinBytes.len = 0;
        if (ended) *ended = done;
        box->stdinMutex.Unlock();
        return true;
    }
    box->stdinMutex.Unlock();
    if (ended) *ended = done;
    return false;
}

void ProcessDecodeInit(ProcessDecodeState* state, bool lines) {
    if (!state) return;
    Utf8CarryInit(&state->utf8);
    LineCarryInit(&state->lines);
    state->lineMode = lines;
}

void ProcessDecodeFree(ProcessDecodeState* state) {
    if (state) LineCarryFree(&state->lines);
}

static void PushText(ProcessEventBox* box, ProcessStreamEvent::Kind kind,
                     Str text) {
    if (!text) return;
    ProcessStreamEvent event;
    event.kind = kind;
    event.text = text;
    ProcessEventPush(box, event);
}

bool ProcessDecodeFeed(ProcessDecodeState* state, ProcessEventBox* box,
                       ProcessStreamEvent::Kind kind, const uint8_t* bytes,
                       int count, Str* error) {
    if (!state || !box) return false;
    Str text = Utf8CarryDecode(&state->utf8, bytes, count);
    if (!state->lineMode) {
        PushText(box, kind, text);
        return true;
    }
    Vec<Str> lines;
    bool ok = LineCarryPush(&state->lines, text, &lines, error);
    StrFree(text);
    for (int i = 0; i < len(lines); i++) {
        if (ok)
            PushText(box, kind, lines[i]);
        else
            StrFree(lines[i]);
    }
    VecReset(lines);
    return ok;
}

void ProcessDecodeFinish(ProcessDecodeState* state, ProcessEventBox* box,
                         ProcessStreamEvent::Kind kind) {
    if (!state || !box) return;
    Str rest = Utf8CarryFinish(&state->utf8);
    if (!state->lineMode) {
        PushText(box, kind, rest);
        return;
    }
    Vec<Str> lines;
    Str ignored;
    LineCarryPush(&state->lines, rest, &lines, &ignored);
    StrFree(rest);
    StrFree(ignored);
    for (int i = 0; i < len(lines); i++) PushText(box, kind, lines[i]);
    VecReset(lines);
    Str tail = LineCarryFinish(&state->lines);
    PushText(box, kind, tail);
}

} // namespace gpui::shell
