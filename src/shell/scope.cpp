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
};

static thread_local Vec<ScopeFrame> gScopeStack;
static thread_local uint64_t gNextGeneration = 1;
static thread_local bool gHostContextBorrowed = false;

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
    if (len(gScopeStack) == 0) {
        active = false;
        return;
    }
    ScopeFrame& frame = gScopeStack[len(gScopeStack) - 1];
    if (frame.generation != generation) {
        active = false;
        return;
    }
    PolicyRelease(frame.policy);
    gScopeStack.len--;
    active = false;
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
    ScopeFrame frame = {window, app,        phase,   generation, 0,
                        view,   heldPolicy, runtime, application};
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

ScopeHostContext::ScopeHostContext(Window* win, App* application, bool owns)
    : window(win), app(application), held(owns) {}

ScopeHostContext::ScopeHostContext(ScopeHostContext&& other) noexcept {
    window = other.window;
    app = other.app;
    held = other.held;
    other.window = nullptr;
    other.app = nullptr;
    other.held = false;
}

ScopeHostContext& ScopeHostContext::operator=(
    ScopeHostContext&& other) noexcept {
    if (this != &other) {
        Release();
        window = other.window;
        app = other.app;
        held = other.held;
        other.window = nullptr;
        other.app = nullptr;
        other.held = false;
    }
    return *this;
}

ScopeHostContext::~ScopeHostContext() {
    Release();
}

void ScopeHostContext::Release() {
    if (!held) return;
    if (!gHostContextBorrowed) {
        held = false;
        return;
    }
    gHostContextBorrowed = false;
    window = nullptr;
    app = nullptr;
    held = false;
}

ScopeHostContext ScopeHostContext::Acquire(Window* window, App* app) {
    if (!window || !app || gHostContextBorrowed) {
        return ScopeHostContext(nullptr, nullptr, false);
    }
    gHostContextBorrowed = true;
    return ScopeHostContext(window, app, true);
}

ScopeHostContext ScopeCurrentHost() {
    const ScopeFrame* frame =
        len(gScopeStack) > 0 ? &gScopeStack[len(gScopeStack) - 1] : nullptr;
    return ScopeHostContext::Acquire(frame ? frame->window : nullptr,
                                     frame ? frame->app : nullptr);
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
    if (!valid || gHostContextBorrowed) {
        ShellErrorSet(error, ScopeStaleContextMessage());
        return ScopeHostContext(nullptr, nullptr, false);
    }
    return ScopeHostContext::Acquire(frame->window, frame->app);
}

} // namespace shell
} // namespace gpui
