#include "shell/scope.h"

namespace gpui {

const char* ScopePhaseName(ScopePhase phase) {
    switch (phase) {
        case ScopePhase::Render:
            return "render";
        case ScopePhase::Event:
            return "event";
        case ScopePhase::Task:
            return "task";
        case ScopePhase::Layout:
            return "layout";
    }
    return "render";
}

bool ScopePhaseAllowsNotify(ScopePhase phase) {
    return phase == ScopePhase::Event || phase == ScopePhase::Task;
}

namespace shell {

struct ScopeFrame {
    Window* window = nullptr;
    App* app = nullptr;
    ScopePhase phase = ScopePhase::Render;
    uint64_t generation = 0;
    uint64_t adopted = 0;
    EntityId view = {};
    Policy* policy = nullptr;
    ShellRuntime* runtime = nullptr;
    void* application = nullptr;
    // A ScopeHostContext holds this frame's Window and App right now.
    bool hostBorrowed = false;
};

static thread_local Vec<ScopeFrame> gScopeStack;
static thread_local uint64_t gNextGeneration = 1;

// The frame `generation` names, wherever it sits in the stack. -1 for none.
static int ScopeFrameIndex(uint64_t generation) {
    for (int i = len(gScopeStack) - 1; i >= 0; i--) {
        if (gScopeStack[i].generation == generation) {
            return i;
        }
    }
    return -1;
}

CallScopeGuard::CallScopeGuard(uint64_t value)
    : generation(value), active(true) {}

CallScopeGuard::CallScopeGuard(CallScopeGuard&& other) noexcept {
    generation = other.generation;
    active = other.active;
    other.active = false;
}

CallScopeGuard& CallScopeGuard::operator=(CallScopeGuard&& other) noexcept {
    if (this != &other) {
        Leave();
        generation = other.generation;
        active = other.active;
        other.active = false;
    }
    return *this;
}

CallScopeGuard::~CallScopeGuard() {
    Leave();
}

void CallScopeGuard::Leave() {
    if (!active) return;
    active = false;
    int at = ScopeFrameIndex(generation);
    if (at < 0) {
        return;
    }
    // Normally the top. A guard that is move-assigned over leaves its old
    // frame from under the one it just entered, and a guard outlived by
    // an inner one leaves from the middle; either way the frame comes out
    // where it is rather than staying on the stack for good.
    if (at != len(gScopeStack) - 1) {
        logf("shell: scope %llu left out of order (%d frames above it)\n",
             (unsigned long long)generation, len(gScopeStack) - 1 - at);
    }
    PolicyRelease(gScopeStack[at].policy);
    VecRemoveAt(gScopeStack, at);
}

CallScopeGuard ScopeEnter(Window* window, App* app, ScopePhase phase,
                          EntityId view, Policy* policy, ShellRuntime* runtime,
                          void* application) {
    Policy* heldPolicy = nullptr;
    if (policy) {
        heldPolicy = PolicyRetain(policy);
    } else if (len(gScopeStack) > 0) {
        heldPolicy = PolicyRetain(gScopeStack[len(gScopeStack) - 1].policy);
    } else {
        heldPolicy = PolicyDefault();
    }

    uint64_t generation = gNextGeneration++;
    if (generation == 0) generation = gNextGeneration++;
    ScopeFrame frame = {window, app,        phase,   generation,  0,
                        view,   heldPolicy, runtime, application, false};
    VecAppend(gScopeStack, frame);
    return CallScopeGuard(generation);
}

void ScopeAdopt(uint64_t generation) {
    if (generation != 0 && len(gScopeStack) > 0) {
        gScopeStack[len(gScopeStack) - 1].adopted = generation;
    }
}

uint64_t ScopeCurrentGeneration() {
    return len(gScopeStack) > 0 ? gScopeStack[len(gScopeStack) - 1].generation
                                : 0;
}

ScopePhase ScopeCurrentPhase() {
    return len(gScopeStack) > 0 ? gScopeStack[len(gScopeStack) - 1].phase
                                : ScopePhase::Render;
}

bool ScopeHasCurrent() {
    return len(gScopeStack) > 0;
}

Policy* ScopeCurrentPolicy() {
    return len(gScopeStack) > 0 ? gScopeStack[len(gScopeStack) - 1].policy
                                : nullptr;
}

ShellRuntime* ScopeCurrentRuntime() {
    return len(gScopeStack) > 0 ? gScopeStack[len(gScopeStack) - 1].runtime
                                : nullptr;
}

EntityId ScopeCurrentView() {
    return len(gScopeStack) > 0 ? gScopeStack[len(gScopeStack) - 1].view
                                : EntityId{};
}

void* ScopeCurrentApplication() {
    return len(gScopeStack) > 0 ? gScopeStack[len(gScopeStack) - 1].application
                                : nullptr;
}

ScopeHostContext::ScopeHostContext(Window* win, App* application,
                                   uint64_t borrowed)
    : window(win), app(application), frame(borrowed), held(borrowed != 0) {}

// Starts holding nothing, so the assignment's Release has nothing to release.
ScopeHostContext::ScopeHostContext(ScopeHostContext&& other) noexcept {
    *this = static_cast<ScopeHostContext&&>(other);
}

ScopeHostContext& ScopeHostContext::operator=(
    ScopeHostContext&& other) noexcept {
    if (this != &other) {
        Release();
        window = other.window;
        app = other.app;
        frame = other.frame;
        held = other.held;
        other.window = nullptr;
        other.app = nullptr;
        other.frame = 0;
        other.held = false;
    }
    return *this;
}

ScopeHostContext::~ScopeHostContext() {
    Release();
}

void ScopeHostContext::Release() {
    if (!held) return;
    // The frame may already have left, in which case there is nothing to
    // give back; a frame that took its place has a generation of its own.
    int at = ScopeFrameIndex(frame);
    if (at >= 0) {
        gScopeStack[at].hostBorrowed = false;
    }
    window = nullptr;
    app = nullptr;
    frame = 0;
    held = false;
}

ScopeHostContext ScopeHostContext::Acquire(Window* window, App* app,
                                           uint64_t generation) {
    int at = ScopeFrameIndex(generation);
    if (!window || !app || at < 0 || gScopeStack[at].hostBorrowed) {
        return ScopeHostContext(nullptr, nullptr, 0);
    }
    gScopeStack[at].hostBorrowed = true;
    return ScopeHostContext(window, app, generation);
}

ScopeHostContext ScopeCurrentHost() {
    const ScopeFrame* frame =
        len(gScopeStack) > 0 ? &gScopeStack[len(gScopeStack) - 1] : nullptr;
    return ScopeHostContext::Acquire(frame ? frame->window : nullptr,
                                     frame ? frame->app : nullptr,
                                     frame ? frame->generation : 0);
}

Str ScopeStaleContextMessage() {
    return StrL(
        "cx is no longer valid: it was captured during an earlier call and "
        "used later. Use cx.spawn or take cx from the callback arguments "
        "instead.");
}

ScopeHostContext ScopeHostForGeneration(uint64_t generation,
                                        ShellError* error) {
    const ScopeFrame* frame =
        len(gScopeStack) > 0 ? &gScopeStack[len(gScopeStack) - 1] : nullptr;
    bool valid = frame && (frame->generation == generation ||
                           frame->adopted == generation);
    if (!valid || frame->hostBorrowed) {
        ShellErrorSet(error, ScopeStaleContextMessage());
        return ScopeHostContext(nullptr, nullptr, 0);
    }
    return ScopeHostContext::Acquire(frame->window, frame->app,
                                     frame->generation);
}

} // namespace shell

const Capabilities& CurrentCapabilities() {
    if (Policy* scoped = shell::ScopeCurrentPolicy()) {
        return PolicyCapabilities(scoped);
    }
    Policy* def = PolicyDefault();
    const Capabilities& capabilities = PolicyCapabilities(def);
    PolicyRelease(def);
    return capabilities;
}

} // namespace gpui
