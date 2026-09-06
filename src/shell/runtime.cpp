#include "shell/runtime.h"

#include "base/theme.h"
#include "quickjs/quickjs.h"
#include "shell/a11y.h"
#include "shell/action.h"
#include "shell/dependencies.h"
#include "shell/dock.h"
#include "shell/fetch.h"
#include "shell/filesystem.h"
#include "shell/host_modules.h"
#include "shell/materialize.h"
#include "shell/process.h"
#include "shell/retained.h"
#include "shell/root.h"
#include "shell/scope.h"
#include "shell/standard.h"
#include "shell/theme_tokens.h"
#include "shell/view.h"
#include "sys/task.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace gpui {

constexpr uint64_t kMaxModuleBytes = 8ull * 1024ull * 1024ull;
constexpr int kMaxJobBatch = 1024;
constexpr int kMaxShellTasks = 1024;
static bool gShellDevelopmentMode = false;
static ShellExitHandler gShellExitHandler = nullptr;

void ShellSetDevelopmentMode(bool enabled) {
    gShellDevelopmentMode = enabled;
}

bool ShellDevelopmentMode() {
    return gShellDevelopmentMode;
}

void ShellOnExitRequest(ShellExitHandler handler) {
    gShellExitHandler = handler;
}

struct ShellRuntimeControl {
    uint32_t refs = 1;
    ShellRuntime* runtime = nullptr;
};

struct AppModule {
    Str root;
    uint32_t generation = 0;
    // The Git-backed packages the manifest declared, materialized before the
    // entry module was compiled. Rust keeps them on `ApplicationModules`.
    shell::MaterializedDependencies dependencies;
};

struct ScriptViewRegistration {
    EntityId view = {};
    bool* dirty = nullptr;
};

struct NestedViewEntry {
    uint32_t token = 0;
    EntityId view = {};
    EntityId owner = {};
    Policy* policy = nullptr;
    AppModule* application = nullptr;
    App* app = nullptr;
};

enum class ShellTaskKind : uint8_t {
    Spawn,
    Sleep,
    TimerOnce,
    TimerEvery,
    Process,
    Filesystem,
    Fetch,
    StorageFlush,
    HostAsync,
};

struct ProcessJob;
struct FsJob;
struct ShellFetchJob;
struct StorageFlushState;
struct HostAsyncJob;

struct ShellTask {
    uint32_t id = 0;
    ShellTaskKind kind = ShellTaskKind::Spawn;
    JSValue callback = JS_UNDEFINED;
    JSValue reject = JS_UNDEFINED;
    EntityId owner = {};
    Policy* policy = nullptr;
    AppModule* application = nullptr;
    App* app = nullptr;
    Window* window = nullptr;
    int timer = 0;
    ProcessJob* processJob = nullptr;
    FsJob* fsJob = nullptr;
    StorageFlushState* storageFlush = nullptr;
};

struct ShellTaskDriver {
    ShellRuntime* runtime = nullptr;

    static void OnTimer(ShellTaskDriver* self, Ctx* cx, const TickEvent*,
                        intptr_t id) {
        if (self && self->runtime) {
            self->runtime->ResumeTask((uint32_t)id, cx);
        }
    }
};

struct CallbackEntry {
    shell::CallbackId id = 0;
    uint64_t generation = 0;
    JSValue function = JS_UNDEFINED;
    EntityId view = {};
    Policy* policy = nullptr;
    AppModule* application = nullptr;
    uint64_t registeredIn = 0;
    bool committed = false;
};

struct CallbackArena {
    Vec<CallbackEntry*> entries;
    uint64_t nextGeneration = 0;
    // Zero is the element runtime's "no click" sentinel. Callback ids are
    // also used as explicit click ids for controls that supply a bool value.
    shell::CallbackId nextCallback = 1;
    uint64_t buildingGeneration = 0;
    int buildingStart = 0;
    bool building = false;

    uint64_t Begin(JSContext* ctx) {
        Abort(ctx);
        building = true;
        buildingStart = entries.len;
        buildingGeneration = nextGeneration++;
        return buildingGeneration;
    }

    shell::CallbackId Push(JSContext* ctx, JSValueConst function, EntityId view,
                           Policy* policy, uint64_t registeredIn,
                           AppModule* application) {
        if (!building || nextCallback == UINT64_MAX) return UINT64_MAX;
        CallbackEntry* entry = new CallbackEntry();
        entry->id = nextCallback++;
        entry->generation = buildingGeneration;
        entry->function = JS_DupValue(ctx, function);
        entry->view = view;
        entry->policy = PolicyRetain(policy);
        entry->application = application;
        entry->registeredIn = registeredIn;
        VecAppend(entries, entry);
        return entry->id;
    }

    shell::CallbackId PushPersistent(JSContext* ctx, JSValueConst function,
                                     EntityId view, Policy* policy,
                                     AppModule* application) {
        if (nextCallback == UINT64_MAX) return UINT64_MAX;
        CallbackEntry* entry = new CallbackEntry();
        entry->id = nextCallback++;
        entry->generation = UINT64_MAX;
        entry->function = JS_DupValue(ctx, function);
        entry->view = view;
        entry->policy = PolicyRetain(policy);
        entry->application = application;
        entry->registeredIn = shell::ScopeCurrentGeneration();
        entry->committed = true;
        VecAppend(entries, entry);
        return entry->id;
    }

    void Commit() {
        if (!building) return;
        for (int i = buildingStart; i < entries.len; i++) {
            entries[i]->committed = true;
        }
        building = false;
    }

    void Abort(JSContext* ctx) {
        if (!building) return;
        while (entries.len > buildingStart) {
            CallbackEntry* entry = entries[entries.len - 1];
            if (ctx) JS_FreeValue(ctx, entry->function);
            PolicyRelease(entry->policy);
            delete entry;
            entries.len--;
        }
        building = false;
    }

    CallbackEntry* Get(shell::CallbackId id) const {
        for (int i = 0; i < entries.len; i++) {
            CallbackEntry* entry = entries[i];
            if (entry->committed && entry->id == id) return entry;
        }
        return nullptr;
    }

    void Retire(JSContext* ctx, uint64_t generation) {
        int out = 0;
        for (int i = 0; i < entries.len; i++) {
            CallbackEntry* entry = entries[i];
            if (entry->committed && entry->generation == generation) {
                JS_FreeValue(ctx, entry->function);
                PolicyRelease(entry->policy);
                delete entry;
            } else {
                entries[out++] = entry;
            }
        }
        entries.len = out;
        if (buildingStart > out) buildingStart = out;
    }

    void RetireId(JSContext* ctx, shell::CallbackId id) {
        for (int i = 0; i < entries.len; i++) {
            CallbackEntry* entry = entries[i];
            if (entry->id != id) continue;
            JS_FreeValue(ctx, entry->function);
            PolicyRelease(entry->policy);
            delete entry;
            for (int j = i + 1; j < entries.len; j++) {
                entries[j - 1] = entries[j];
            }
            entries.len--;
            if (buildingStart > i) buildingStart--;
            return;
        }
    }

    void RetireApplication(JSContext* ctx, AppModule* application) {
        if (!application) return;
        int out = 0;
        for (int i = 0; i < entries.len; i++) {
            CallbackEntry* entry = entries[i];
            if (entry->application == application) {
                JS_FreeValue(ctx, entry->function);
                PolicyRelease(entry->policy);
                delete entry;
            } else {
                entries[out++] = entry;
            }
        }
        entries.len = out;
        if (buildingStart > out) buildingStart = out;
    }

    void Clear(JSContext* ctx) {
        for (int i = 0; i < entries.len; i++) {
            CallbackEntry* entry = entries[i];
            JS_FreeValue(ctx, entry->function);
            PolicyRelease(entry->policy);
            delete entry;
        }
        VecReset(entries);
        building = false;
        buildingStart = 0;
    }

    int Live() const {
        int count = 0;
        for (int i = 0; i < entries.len; i++) {
            if (entries[i]->committed) count++;
        }
        return count;
    }
};

struct ScriptPanelClass;

// One piece of a dock's chrome, described once and replayed until its handler
// or the container's own state moves. See ShellRuntime::DescribeDockChrome.
struct ChromeCacheEntry {
    shell::EntityHandle dock = 0;
    shell::DockChromeSlot slot = shell::DockChromeSlot::TabBar;
    uint64_t key = 0;
    shell::CallbackId callback = 0;
    Str payload = {};
    shell::SpecArena* arena = nullptr;
    shell::SpecId root = 0;
    bool hasRoot = false;
};

// The template being discovered, and the description it interrupted.
struct TemplateDiscovery {
    int arity = 0;
    Vec<shell::Slot> slots;
    // The arena the live render was recording into when the body started.
    // Swapped out rather than recorded around, so a template's ids are dense
    // and start at zero — which is what makes grafting one addition per id.
    shell::SpecArena* saved = nullptr;
};

struct ShellRuntimeImpl {
    ShellRuntime* owner = nullptr;
    JSRuntime* jsRuntime = nullptr;
    JSContext* context = nullptr;
    shell::SpecArena* scratch = nullptr;
    // Templates the script has defined, indexed by the id its closure keeps.
    // An entry is emptied when the application that defined it is released — a
    // hot reload re-evaluates the module and defines its templates again, so
    // without that this would grow by one arena per call site per save. The
    // slot itself stays, because the id is the index.
    Vec<shell::Template*> templates;
    // The template being discovered, while one is.
    TemplateDiscovery* discovery = nullptr;
    CallbackArena callbacks;
    shell::RetainedStore retained;
    shell::Metrics metrics;
    Vec<AppModule*> modules;
    Vec<ScriptViewRegistration> views;
    Vec<NestedViewEntry> nestedViews;
    // The panel builders this runtime registered. Declared here rather than
    // only in the process-wide panel registry because a panel the script
    // *adds* needs the same serialize/deserialize hooks a restored one gets,
    // and each holds a live JS class that must be released while the context
    // is still alive.
    Vec<ScriptPanelClass*> panelClasses;
    Vec<ChromeCacheEntry*> chromeCache;
    Vec<ShellTask*> tasks;
    Entity<ShellTaskDriver> taskDriver = {};
    App* taskApp = nullptr;
    uint32_t nextTask = 1;
    uint32_t nextNestedView = 1;
    uint32_t nextModuleGeneration = 1;
    // Empty: the per-user cache. A test points it at a scratch directory the
    // way Rust hands `new_isolated_with_dependency_store` a store.
    Str dependencyCacheRoot;
    uint64_t detachedExecution = 0;
    uint64_t interruptIdentity = UINT64_MAX;
    double interruptStarted = 0;
    bool interruptWasScoped = false;
};

struct ViewType {
    uint32_t refs = 1;
    ShellRuntime* runtime = nullptr;
    JSValue value = JS_UNDEFINED;
    AppModule* application = nullptr;
};

struct ViewObject {
    uint32_t refs = 1;
    ShellRuntime* runtime = nullptr;
    JSValue value = JS_UNDEFINED;
    AppModule* application = nullptr;
};

struct ShellRuntimeAccess {
    static ShellRuntimeImpl* Impl(ShellRuntime* runtime) {
        return runtime ? runtime->impl : nullptr;
    }
    static ShellRuntimeControl* Control(ShellRuntime* runtime) {
        return runtime ? runtime->control : nullptr;
    }
};

const shell::MaterializedDependencies* ViewTypeDependencies(ViewType* type) {
    return type && type->application ? &type->application->dependencies
                                     : nullptr;
}

static ViewObject* InstantiateObject(ShellRuntime* runtime, ViewType* type,
                                     Window* window, App* app, Policy* policy,
                                     JSValueConst props, ShellError* error,
                                     EntityId view);

static NestedViewEntry* FindNestedView(ShellRuntimeImpl* impl, uint32_t token,
                                       int* index = nullptr) {
    if (!impl || token == 0) return nullptr;
    for (int i = 0; i < impl->nestedViews.len; i++) {
        if (impl->nestedViews[i].token != token) continue;
        if (index) *index = i;
        return &impl->nestedViews[i];
    }
    return nullptr;
}

static bool NestedViewIsCurrent(const NestedViewEntry* entry) {
    return entry && entry->policy == shell::ScopeCurrentPolicy() &&
           entry->application == (AppModule*)shell::ScopeCurrentApplication();
}

static void DropNestedViewAt(ShellRuntimeImpl* impl, int at) {
    if (!impl || at < 0 || at >= impl->nestedViews.len) return;
    NestedViewEntry entry = impl->nestedViews[at];
    for (int i = at + 1; i < impl->nestedViews.len; i++) {
        impl->nestedViews[i - 1] = impl->nestedViews[i];
    }
    impl->nestedViews.len--;
    if (entry.app && entry.view.IsValid()) EntityDrop(entry.app, entry.view);
    PolicyRelease(entry.policy);
}

static void RollbackNestedViews(ShellRuntimeImpl* impl, uint32_t checkpoint) {
    for (;;) {
        int found = -1;
        for (int i = impl ? impl->nestedViews.len - 1 : -1; i >= 0; i--) {
            if (impl->nestedViews[i].token >= checkpoint) {
                found = i;
                break;
            }
        }
        if (found < 0) return;
        DropNestedViewAt(impl, found);
    }
}

static bool TakeNestedView(ShellRuntimeImpl* impl, uint32_t token,
                           Entity<ScriptView>* out) {
    int at = -1;
    NestedViewEntry* found = FindNestedView(impl, token, &at);
    if (!NestedViewIsCurrent(found) || !found->app ||
        !EntityGet(found->app, found->view))
        return false;
    NestedViewEntry entry = *found;
    for (int i = at + 1; i < impl->nestedViews.len; i++)
        impl->nestedViews[i - 1] = impl->nestedViews[i];
    impl->nestedViews.len--;
    PolicyRelease(entry.policy);
    *out = Entity<ScriptView>{entry.view};
    return true;
}

// What every background job begins with, so that one guard and one lease
// serve all of them rather than a preamble per `done` callback.
struct ShellJobHeader {
    ShellRuntimeControl* control = nullptr;
    uint32_t task = 0;
    ShellTaskKind kind = ShellTaskKind::Spawn;
};

struct ProcessJob {
    ShellJobHeader head;
    shell::ProcessCancellation cancellation;
    Str command;
    Vec<Str> args;
    shell::ProcessOutput output;
    Str error;

    void Free() {
        for (int i = 0; i < args.len; i++) StrFree(args[i]);
        VecReset(args);
        StrFree(command);
        output.Free();
        StrFree(error);
    }
};

struct FsJob {
    ShellJobHeader head;
    shell::FsOperation operation = shell::FsOperation::Read;
    CapabilityPath path;
    Str input;
    shell::FsResult result;
    Str error;
    bool text = false;
    bool withFileTypes = false;
    bool recursive = false;

    void Free() {
        path.Free();
        StrFree(input);
        result.Free();
        StrFree(error);
    }
};

struct ShellFetchJob {
    ShellJobHeader head;
    shell::FetchRequest request;
    Capabilities capabilities;
    shell::FetchResult result;

    void Free() {
        request.Free();
        result.Free();
    }
};

struct StorageFlushState {
    ShellRuntimeControl* control = nullptr;
    uint32_t task = 0;
    shell::Storage* storage = nullptr;
    shell::StorageWaiter* waiter = nullptr;
};

struct StorageWriteJob {
    Policy* policy = nullptr;
    shell::StorageWrite write;
    Str error;
    bool ok = false;

    void Free() {
        PolicyRelease(policy);
        write.Free();
        StrFree(error);
    }
};

struct HostAsyncJob {
    ShellJobHeader head;
    HostArguments arguments;
    Func1<HostCall*> work;
    Func0 release;
    HostModules* registry = nullptr;
    HostValue result;
    HostError error;

    void Free() {
        arguments.Free();
        result.Free();
        error.Clear();
        release.Call();
        HostModulesRelease(registry);
        registry = nullptr;
    }
};

static void ControlRetain(void* state) {
    if (state) ((ShellRuntimeControl*)state)->refs++;
}

static void ControlRelease(void* state) {
    ShellRuntimeControl* control = (ShellRuntimeControl*)state;
    if (control && --control->refs == 0) delete control;
}

void ShellRuntimeRetireSnapshot(void* state, uint64_t generation) {
    ShellRuntimeControl* control = (ShellRuntimeControl*)state;
    if (!control || !control->runtime || !control->runtime->impl) return;
    ShellRuntimeImpl* impl = control->runtime->impl;
    impl->callbacks.Retire(impl->context, generation);
}

static SnapshotRuntimeLease SnapshotLease(ShellRuntime* runtime) {
    SnapshotRuntimeLease lease = {};
    lease.state = ShellRuntimeAccess::Control(runtime);
    lease.retain = ControlRetain;
    lease.release = ControlRelease;
    lease.retireCallbacks = ShellRuntimeRetireSnapshot;
    return lease;
}

static void SetError(ShellError* error, Str message) {
    ShellErrorSet(error, message);
}

static bool TaskWindowLive(const ShellTask* task) {
    if (!task || !task->app || !task->window) return false;
    for (int i = 0; i < task->app->windows.len; i++) {
        if (task->app->windows[i] == task->window) return true;
    }
    return false;
}

static ShellTask* FindTask(ShellRuntimeImpl* impl, uint32_t id,
                           int* index = nullptr) {
    if (!impl || id == 0) return nullptr;
    for (int i = 0; i < impl->tasks.len; i++) {
        if (impl->tasks[i]->id != id) continue;
        if (index) *index = i;
        return impl->tasks[i];
    }
    return nullptr;
}

static void DestroyTask(ShellRuntimeImpl* impl, ShellTask* task,
                        bool cancelTimer) {
    if (!task) return;
    if (cancelTimer && task->timer && TaskWindowLive(task)) {
        WindowCancelTimer(task->window, task->timer);
    }
    if (task->processJob) task->processJob->cancellation.Cancel();
    if (task->storageFlush) {
        StorageFlushState* state = task->storageFlush;
        state->storage->CancelWaiter(state->waiter);
        ControlRelease(state->control);
        delete state;
    }
    if (impl && impl->context) JS_FreeValue(impl->context, task->callback);
    if (impl && impl->context) JS_FreeValue(impl->context, task->reject);
    PolicyRelease(task->policy);
    delete task;
}

static bool ForgetTask(ShellRuntimeImpl* impl, uint32_t id,
                       bool cancelTimer = true) {
    int at = -1;
    ShellTask* task = FindTask(impl, id, &at);
    if (!task) return false;
    for (int i = at + 1; i < impl->tasks.len; i++) {
        impl->tasks[i - 1] = impl->tasks[i];
    }
    impl->tasks.len--;
    DestroyTask(impl, task, cancelTimer);
    return true;
}

static Entity<ShellTaskDriver> TaskDriver(ShellRuntimeImpl* impl, App* app) {
    if (!impl || !app) return {};
    if (impl->taskDriver.IsValid() && impl->taskApp == app &&
        impl->taskDriver.Get(app)) {
        return impl->taskDriver;
    }
    if (impl->taskDriver.IsValid()) return {};
    impl->taskDriver = EntityNewState<ShellTaskDriver>(app);
    impl->taskApp = app;
    ShellTaskDriver* driver = impl->taskDriver.Get(app);
    if (driver) driver->runtime = impl->owner;
    return driver ? impl->taskDriver : Entity<ShellTaskDriver>{};
}

static uint32_t NewTask(ShellRuntimeImpl* impl, ShellTaskKind kind,
                        JSValueConst callback, App* app = nullptr,
                        Window* window = nullptr, bool ownerless = false,
                        JSValueConst reject = JS_UNDEFINED) {
    if (!impl || impl->tasks.len >= kMaxShellTasks || impl->nextTask == 0) {
        return 0;
    }
    ShellTask* task = new ShellTask();
    task->id = impl->nextTask++;
    task->kind = kind;
    task->callback = JS_DupValue(impl->context, callback);
    task->reject = JS_DupValue(impl->context, reject);
    task->owner = ownerless ? EntityId{} : shell::ScopeCurrentView();
    Policy* policy = shell::ScopeCurrentPolicy();
    task->policy = policy ? PolicyRetain(policy) : PolicyDefault();
    task->application = (AppModule*)shell::ScopeCurrentApplication();
    task->app = app;
    task->window = window;
    VecAppend(impl->tasks, task);
    return task->id;
}

static void BeginExecution(ShellRuntimeImpl* impl);
static Str ExceptionText(Arena* arena, JSContext* ctx);
static void DriveStorage(Policy* policy);

static void ProcessJobWork(ProcessJob* job) {
    shell::ProcessRunBounded(job->command, job->args.els, job->args.len,
                             &job->cancellation, &job->output, &job->error);
}

// A task record points back at its job so that cancelling can reach it. The
// coroutine frees the job, so the pointer is cleared first, on every path.
// Which of the three it is follows from the kind.
static void DetachTaskJob(ShellTask* task) {
    if (!task) {
        return;
    }
    switch (task->kind) {
        case ShellTaskKind::Process:
            task->processJob = nullptr;
            break;
        case ShellTaskKind::Filesystem:
            task->fsJob = nullptr;
            break;
        case ShellTaskKind::StorageFlush:
            task->storageFlush = nullptr;
            break;
        default:
            break;
    }
}

static bool ShellTaskOwnerAlive(void* user) {
    ShellJobHeader* head = (ShellJobHeader*)user;
    ShellRuntime* runtime = head->control ? head->control->runtime : nullptr;
    ShellRuntimeImpl* impl = ShellRuntimeAccess::Impl(runtime);
    ShellTask* task = impl ? FindTask(impl, head->task) : nullptr;
    return task && task->kind == head->kind && TaskWindowLive(task) &&
           (!task->owner.IsValid() || EntityGet(task->app, task->owner));
}

// Holds the task record and the runtime lease for as long as the coroutine
// frame lives. A frame dropped because its owner went away destroys this on
// the way out, which is the `else if (task) ForgetTask` arm the done callbacks
// carried; a frame that settles clears `settled` first, because the record has
// to be gone before the script can see it.
struct ShellTaskLease {
    ShellJobHeader* head = nullptr;
    // The job the header sits in, and how to free it. The lease owns it so
    // that the order is not something a coroutine body can get wrong: freeing
    // the job before the lease goes out of scope leaves this destructor
    // reading freed memory, which is what ASan reported the first time this
    // was written the other way around.
    void* job = nullptr;
    void (*destroy)(void* job) = nullptr;
    bool settled = false;

    ~ShellTaskLease() {
        if (head) {
            if (!settled) {
                ShellRuntime* runtime =
                    head->control ? head->control->runtime : nullptr;
                ShellRuntimeImpl* impl = ShellRuntimeAccess::Impl(runtime);
                if (impl && head->task) {
                    DetachTaskJob(FindTask(impl, head->task));
                    ForgetTask(impl, head->task, false);
                }
            }
            ControlRelease(head->control);
        }
        if (destroy) {
            destroy(job);
        }
    }
};

// Settling a promise from a background result, which was written out once per
// job kind and is the same every time: take the resolve or the reject, drop
// the task record before the script runs, enter the scope the task was created
// under, hand over the value and drain what the callback queued.
//
// `makeValue` builds the resolved value inside that scope, because building it
// needs the context. It is not called on the failing path, where the value is
// an Error carrying `message`.
static void SettleShellTask(ShellTaskLease* lease, bool failed, Str message,
                            JSValue (*makeValue)(ShellRuntimeImpl*, void*),
                            void* user) {
    ShellJobHeader* head = lease->head;
    ShellRuntime* runtime = head->control ? head->control->runtime : nullptr;
    ShellRuntimeImpl* impl = ShellRuntimeAccess::Impl(runtime);
    ShellTask* task = impl ? FindTask(impl, head->task) : nullptr;
    if (!task) {
        return;
    }
    Window* window = task->window;
    App* app = task->app;
    EntityId owner = task->owner;
    Policy* policy = PolicyRetain(task->policy);
    AppModule* application = task->application;
    JSValue settle =
        JS_DupValue(impl->context, failed ? task->reject : task->callback);
    DetachTaskJob(task);
    ForgetTask(impl, task->id, false);
    lease->settled = true;

    shell::CallScopeGuard scope = shell::ScopeEnter(
        window, app, ScopePhase::Task, owner, policy, runtime, application);
    PolicyRelease(policy);
    BeginExecution(impl);
    JSValue value = JS_UNDEFINED;
    if (failed) {
        value = JS_NewError(impl->context);
        JS_SetPropertyStr(
            impl->context, value, "message",
            JS_NewStringLen(impl->context, message.s ? message.s : "",
                            (size_t)(message.s ? message.len : 0)));
    } else {
        value = makeValue(impl, user);
    }
    JSValue settled = JS_IsException(value) ? JS_EXCEPTION
                                            : JS_Call(impl->context, settle,
                                                      JS_UNDEFINED, 1, &value);
    JS_FreeValue(impl->context, value);
    JS_FreeValue(impl->context, settle);
    if (JS_IsException(settled)) {
        Arena* arena = ArenaNew();
        log(ExceptionText(arena, impl->context));
        ArenaDelete(arena);
        return;
    }
    JS_FreeValue(impl->context, settled);
    ShellError error = {};
    runtime->DrainJobs(kMaxJobBatch, &error);
    if (error.IsSet()) {
        log(error.message);
        ShellErrorClear(&error);
    }
}

static JSValue ProcessJobResolved(ShellRuntimeImpl* impl, void* user) {
    ProcessJob* job = (ProcessJob*)user;
    JSValue value = JS_NewObject(impl->context);
    JS_SetPropertyStr(impl->context, value, "code",
                      JS_NewInt32(impl->context, job->output.code));
    JS_SetPropertyStr(
        impl->context, value, "stdout",
        JS_NewStringLen(impl->context,
                        job->output.out.s ? job->output.out.s : "",
                        (size_t)job->output.out.len));
    JS_SetPropertyStr(
        impl->context, value, "stderr",
        JS_NewStringLen(impl->context,
                        job->output.err.s ? job->output.err.s : "",
                        (size_t)job->output.err.len));
    return value;
}

static void ProcessJobDestroy(void* user) {
    ProcessJob* self = (ProcessJob*)user;
    self->Free();
    delete self;
}

// process.run, awaited. The bounded runner is still the one doing the work on
// the pool; what has gone is the preamble that proved the caller was still
// there and the four copies of settling a promise from a background result.
static Task ShellProcessTask(TaskGuard guard, ProcessJob* job) {
    (void)guard;
    ShellTaskLease lease{&job->head, job, ProcessJobDestroy};
    co_await BackgroundSpawn(MkFunc0(ProcessJobWork, job));
    SettleShellTask(&lease, job->error.s != nullptr, job->error,
                    ProcessJobResolved, job);
}

static void FsJobWork(FsJob* job) {
    shell::FsRun(job->operation, job->path.root, job->path.relative, job->input,
                 job->recursive, &job->result, &job->error);
}

static void StorageWriteWork(StorageWriteJob* job) {
    job->ok = shell::StoragePersist(job->write, &job->error);
}

static JSValue FsJobValue(JSContext* context, FsJob* job) {
    if (job->operation == shell::FsOperation::Read) {
        if (job->text) {
            return JS_NewStringLen(
                context, job->result.bytes.s ? job->result.bytes.s : "",
                (size_t)job->result.bytes.len);
        }
        return JS_NewUint8ArrayCopy(
            context,
            (const uint8_t*)(job->result.bytes.s ? job->result.bytes.s : ""),
            (size_t)job->result.bytes.len);
    }
    if (job->operation == shell::FsOperation::Exists) {
        return JS_NewBool(context, job->result.exists);
    }
    if (job->operation != shell::FsOperation::ReadDirectory) {
        return JS_UNDEFINED;
    }
    JSValue array = JS_NewArray(context);
    JSValue global = JS_GetGlobalObject(context);
    JSValue make = job->withFileTypes
                       ? JS_GetPropertyStr(context, global, "__shell_fs_dirent")
                       : JS_UNDEFINED;
    for (int i = 0; i < job->result.entries.len; i++) {
        const shell::FsEntry& entry = job->result.entries[i];
        JSValue name =
            JS_NewStringLen(context, entry.name.s, (size_t)entry.name.len);
        JSValue value = name;
        if (job->withFileTypes) {
            JSValue args[2] = {name, JS_NewBool(context, entry.isDirectory)};
            value = JS_Call(context, make, JS_UNDEFINED, 2, args);
            JS_FreeValue(context, args[0]);
            JS_FreeValue(context, args[1]);
        }
        if (JS_IsException(value) ||
            JS_SetPropertyUint32(context, array, (uint32_t)i, value) < 0) {
            JS_FreeValue(context, array);
            array = JS_EXCEPTION;
            break;
        }
    }
    JS_FreeValue(context, make);
    JS_FreeValue(context, global);
    return array;
}

static JSValue FsJobResolved(ShellRuntimeImpl* impl, void* user) {
    return FsJobValue(impl->context, (FsJob*)user);
}

static void FsJobDestroy(void* user) {
    FsJob* self = (FsJob*)user;
    self->Free();
    delete self;
}

// fs/promises, awaited rather than continued into. What used to be FsJobWork
// plus FsJobDone: the owner check is the guard's, the task record and the job
// are the lease's, and settling is shared with every other background path.
static Task ShellFsTask(TaskGuard guard, FsJob* job) {
    (void)guard;
    ShellTaskLease lease{&job->head, job, FsJobDestroy};
    co_await BackgroundSpawn(MkFunc0(FsJobWork, job));
    SettleShellTask(&lease, job->error.s != nullptr, job->error, FsJobResolved,
                    job);
}

static JSValue FetchJobValue(JSContext* context, ShellFetchJob* job) {
    JSValue global = JS_GetGlobalObject(context);
    JSValue make = JS_GetPropertyStr(context, global, "__shell_fetch_response");
    JSValue args[3] = {
        JS_NewInt32(context, job->result.status),
        JS_NewStringLen(context, job->result.url.s ? job->result.url.s : "",
                        (size_t)job->result.url.len),
        JS_NewStringLen(context, job->result.body.s ? job->result.body.s : "",
                        (size_t)job->result.body.len),
    };
    JSValue value = JS_IsException(make)
                        ? JS_EXCEPTION
                        : JS_Call(context, make, JS_UNDEFINED, 3, args);
    for (int i = 0; i < 3; i++) JS_FreeValue(context, args[i]);
    JS_FreeValue(context, make);
    JS_FreeValue(context, global);
    return value;
}

// The preamble every `done` callback used to write out: the runtime is still
// there, the task is still registered under its own kind, its window is still
// open, and the entity that asked for the work has not gone stale. As a
// TaskGuard it runs once, on sys/task.h's single resume path, and a false
// answer drops the continuation instead of running it.
static JSValue FetchJobResolved(ShellRuntimeImpl* impl, void* user) {
    return FetchJobValue(impl->context, (ShellFetchJob*)user);
}

#if !GPUI_OS_WASM
static void FetchJobWork(ShellFetchJob* job) {
    shell::FetchSend(job->request, job->capabilities, &job->result);
}
#endif

static void ShellFetchJobDestroy(void* job) {
    ShellFetchJob* self = (ShellFetchJob*)job;
    self->Free();
    delete self;
}

#if GPUI_OS_WASM
// HttpSendAsync already returns on the main thread, including when the browser
// resolves fetch(). Keep the same lease the coroutine path used: if the view
// or runtime disappeared while the request was in flight, SettleShellTask
// declines to enter it and the lease only releases the retained job.
static void ShellFetchDone(ShellFetchJob* job, shell::FetchAsyncResult landed) {
    if (landed.result) {
        job->result = *landed.result;
        *landed.result = {};
    }
    ShellTaskLease lease{&job->head, job, ShellFetchJobDestroy};
    bool failed = !landed.ok || job->result.error.s != nullptr;
    SettleShellTask(&lease, failed, job->result.error, FetchJobResolved, job);
}
#else
// The hosted clients are blocking. Keep them behind the executor-backed Task
// whose frame owns the job even when shutdown drops a late completion.
static Task ShellFetchTask(TaskGuard guard, ShellFetchJob* job) {
    (void)guard;
    ShellTaskLease lease{&job->head, job, ShellFetchJobDestroy};
    co_await BackgroundSpawn(MkFunc0(FetchJobWork, job));
    bool failed = job->result.error.s != nullptr;
    SettleShellTask(&lease, failed, job->result.error, FetchJobResolved, job);
}
#endif

static void StorageFlushDone(StorageFlushState* state,
                             shell::StorageOutcome outcome) {
    state->waiter = nullptr;
    ShellRuntime* runtime = state->control ? state->control->runtime : nullptr;
    ShellRuntimeImpl* impl = ShellRuntimeAccess::Impl(runtime);
    ShellTask* task = impl ? FindTask(impl, state->task) : nullptr;
    if (task && task->kind == ShellTaskKind::StorageFlush &&
        TaskWindowLive(task) &&
        (!task->owner.IsValid() || EntityGet(task->app, task->owner))) {
        Window* window = task->window;
        App* app = task->app;
        EntityId owner = task->owner;
        Policy* policy = PolicyRetain(task->policy);
        AppModule* application = task->application;
        JSValue settle = JS_DupValue(
            impl->context, outcome.ok ? task->callback : task->reject);
        task->storageFlush = nullptr;
        ForgetTask(impl, task->id, false);

        shell::CallScopeGuard scope = shell::ScopeEnter(
            window, app, ScopePhase::Task, owner, policy, runtime, application);
        PolicyRelease(policy);
        BeginExecution(impl);
        JSValue value = JS_UNDEFINED;
        int argc = 0;
        if (!outcome.ok) {
            const char* fallback = "writing localStorage failed";
            value = JS_NewError(impl->context);
            JS_SetPropertyStr(
                impl->context, value, "message",
                JS_NewStringLen(impl->context,
                                outcome.error.s ? outcome.error.s : fallback,
                                (size_t)(outcome.error.s ? outcome.error.len
                                                         : strlen(fallback))));
            argc = 1;
        }
        JSValue settled = JS_Call(impl->context, settle, JS_UNDEFINED, argc,
                                  argc ? &value : nullptr);
        JS_FreeValue(impl->context, value);
        JS_FreeValue(impl->context, settle);
        if (JS_IsException(settled)) {
            Arena* arena = ArenaNew();
            log(ExceptionText(arena, impl->context));
            ArenaDelete(arena);
        } else {
            JS_FreeValue(impl->context, settled);
            ShellError error = {};
            runtime->DrainJobs(kMaxJobBatch, &error);
            if (error.IsSet()) {
                log(error.message);
                ShellErrorClear(&error);
            }
        }
    } else if (task) {
        task->storageFlush = nullptr;
        ForgetTask(impl, task->id, false);
    }
    ControlRelease(state->control);
    delete state;
}

static void WakeStorageWaiters(Vec<shell::StorageWaiter*>* ready,
                               shell::StorageOutcome outcome) {
    for (int i = 0; i < ready->len; i++) {
        shell::StorageWaiter* waiter = (*ready)[i];
        Func1<shell::StorageOutcome> settle = waiter->settle;
        delete waiter;
        settle.Call(outcome);
    }
    VecReset(*ready);
}

static void StorageWriteDone(StorageWriteJob* job) {
    shell::Storage* storage = PolicyStorage(job->policy, false);
    Vec<shell::StorageWaiter*> ready;
    if (storage) {
        storage->FinishWrite(job->write.revision, job->ok, &ready);
    }
    shell::StorageOutcome outcome = {job->ok, job->error};
    WakeStorageWaiters(&ready, outcome);
    if (storage) DriveStorage(job->policy);
    job->Free();
    delete job;
}

static void DriveStorage(Policy* policy) {
    shell::Storage* storage = PolicyStorage(policy, false);
    if (!storage) return;
    shell::StorageWrite pending;
    Str error;
    bool encoded = storage->BeginWrite(&pending, &error);
    if (!encoded) {
        if (pending.revision) {
            Vec<shell::StorageWaiter*> ready;
            storage->FinishWrite(pending.revision, false, &ready);
            shell::StorageOutcome outcome = {false, error};
            WakeStorageWaiters(&ready, outcome);
        }
        if (error) log(error);
        StrFree(error);
        pending.Free();
        return;
    }
    StrFree(error);
    if (!pending.revision) return;
    StorageWriteJob* job = new StorageWriteJob();
    job->policy = PolicyRetain(policy);
    job->write = pending;
    pending = {};
    if (!ExecSpawn(MkFunc0(StorageWriteWork, job),
                   MkFunc0(StorageWriteDone, job))) {
        storage->AbortWrite(job->write.revision);
        job->Free();
        delete job;
    }
}

static void BeginExecution(ShellRuntimeImpl* impl) {
    impl->detachedExecution++;
    if (impl->detachedExecution == 0) impl->detachedExecution++;
}

static int Interrupt(JSRuntime*, void* opaque) {
    ShellRuntimeImpl* impl = (ShellRuntimeImpl*)opaque;
    uint64_t generation = shell::ScopeCurrentGeneration();
    bool scoped = generation != 0;
    uint64_t identity = scoped ? generation : impl->detachedExecution;
    if (impl->interruptIdentity != identity ||
        impl->interruptWasScoped != scoped) {
        impl->interruptIdentity = identity;
        impl->interruptWasScoped = scoped;
        impl->interruptStarted = TimeNow();
    }
    double budget = 5.0;
    if (scoped) {
        ScopePhase phase = shell::ScopeCurrentPhase();
        budget = phase == ScopePhase::Render || phase == ScopePhase::Layout
                     ? 0.050
                     : 0.500;
    }
    return TimeNow() - impl->interruptStarted > budget;
}

static Str ExceptionText(Arena* arena, JSContext* ctx) {
    JSValue exception = JS_GetException(ctx);
    StrBuilder out(arena);
    size_t messageLen = 0;
    const char* message = JS_ToCStringLen(ctx, &messageLen, exception);
    Str messageText;
    if (message) {
        messageText = StrDup(arena, Str(message, (int)messageLen));
        out.Append(messageText);
        JS_FreeCString(ctx, message);
    } else {
        out.Append(StrL("JavaScript exception"));
    }
    if (JS_IsError(exception)) {
        JSValue stack = JS_GetPropertyStr(ctx, exception, "stack");
        if (!JS_IsException(stack) && !JS_IsUndefined(stack)) {
            size_t stackLen = 0;
            const char* text = JS_ToCStringLen(ctx, &stackLen, stack);
            if (text && stackLen > 0) {
                Str stackText(text, (int)stackLen);
                if (!messageText || !StrStartsWith(stackText, messageText)) {
                    out.AppendChar('\n');
                    out.Append(stackText);
                } else if ((int)stackLen > (int)messageLen) {
                    out.Append(Str(text + messageLen,
                                   (int)stackLen - (int)messageLen));
                }
                JS_FreeCString(ctx, text);
            }
        }
        JS_FreeValue(ctx, stack);
    }
    JS_FreeValue(ctx, exception);
    return out.TakeStr();
}

static bool CaptureException(ShellRuntimeImpl* impl, ShellError* error) {
    Arena* arena = ArenaNew();
    Str text = ExceptionText(arena, impl->context);
    SetError(error, text);
    ArenaDelete(arena);
    return false;
}

static bool Await(ShellRuntimeImpl* impl, JSValueConst value,
                  ShellError* error) {
    if (!JS_IsPromise(value)) return true;
    int count = 0;
    while (JS_PromiseState(impl->context, value) == JS_PROMISE_PENDING &&
           JS_IsJobPending(impl->jsRuntime) && count++ < kMaxJobBatch) {
        JSContext* context = nullptr;
        int result = JS_ExecutePendingJob(impl->jsRuntime, &context);
        if (result < 0) return CaptureException(impl, error);
        if (result == 0) break;
    }
    JSPromiseStateEnum state = JS_PromiseState(impl->context, value);
    if (state == JS_PROMISE_REJECTED) {
        JSValue reason = JS_PromiseResult(impl->context, value);
        JS_Throw(impl->context, reason);
        return CaptureException(impl, error);
    }
    if (state == JS_PROMISE_PENDING) {
        SetError(error, StrL("module evaluation left a pending promise with no "
                             "host work able to settle it"));
        return false;
    }
    return true;
}

static TempStr ReadModuleFileTemp(Str path, ShellError* error) {
    if (path.len <= 0 || path.len >= kMaxPath) {
        SetError(error, StrL("module path is empty or too long"));
        return {};
    }
    TempStr source = ReadBoundedFileTemp(path, (int)kMaxModuleBytes);
    if (!source.s) {
        SetError(error, fmt("reading module `%s` failed", path));
    }
    return source;
}

static bool IsBuiltin(Str name) {
    return (StrEq(name, StrL("gpui")) || StrEq(name, StrL("gpui-kit"))) ||
           StrEq(name, StrL("gpui-base")) || StrEq(name, StrL("gpui-shell")) ||
           StrEq(name, StrL("gpui-fps")) || StrEq(name, StrL("buffer")) ||
           StrEq(name, StrL("console")) || StrEq(name, StrL("crypto")) ||
           StrEq(name, StrL("fs/promises")) || StrEq(name, StrL("os")) ||
           StrEq(name, StrL("path")) || StrEq(name, StrL("process")) ||
           StrEq(name, StrL("url")) || StrEq(name, StrL("zlib"));
}

static const char* const kGpuiExports[] = {
    "View", "div", "svg", "image", "PathBuilder", "Background", "with_cx"};
static const char* const kBaseExports[] = {
    "h_flex", "v_flex", "Button", "Link", "Checkbox", "Switch", "Tabs", "Tab",
    "Progress", "ProgressTrack", "ProgressIndicator", "Radio", "Toggle",
    "RadioGroup", "ToggleGroup", "Table", "TableHeader", "TableBody",
    "TableRow", "TableHead", "TableCell", "TableCaption", "h_resizable",
    "v_resizable", "resizable_panel", "Collapsible", "Popover", "HoverCard",
    "Popup", "Select", "Combobox", "DatePicker", "Scrollbar", "v_virtual_list",
    "h_virtual_list", "VirtualListScrollHandle", "Input", "InputState",
    "NumberInput", "Textarea", "TextareaState", "SliderState", "Slider",
    "SliderTrack", "SliderIndicator", "SliderThumb", "OtpState", "OtpInput",
    "Avatar", "AvatarImage", "AvatarFallback", "Pagination", "pagination_items",
    "CalendarState", "Accordion", "AccordionItem", "AccordionHeader",
    "AccordionPanel", "AccordionTrigger",
    // Dock. The area is the state and `dock_area` is one description of it,
    // which is the split `v_virtual_list` already has.
    "DockArea", "dock_area", "dock_content", "set_theme"};
static const char* const kFpsExports[] = {"fps_monitor", "show_fps_monitor",
                                          "hide_fps_monitor",
                                          "fps_monitor_visible"};
static const char* const kBufferExports[] = {"default", "Buffer"};
static const char* const kConsoleExports[] = {"default"};
static const char* const kCryptoExports[] = {
    "default",         "createHash", "randomBytes", "randomUUID",
    "getRandomValues", "crypto",     "webcrypto"};
static const char* const kFsExports[] = {"default", "readFile", "writeFile",
                                         "readdir", "exists",   "unlink",
                                         "rmdir",   "mkdir"};
static const char* const kOsExports[] = {"default", "platform", "arch", "EOL"};
static const char* const kPathExports[] = {
    "default", "sep",        "delimiter", "basename",  "dirname",
    "extname", "isAbsolute", "join",      "normalize", "relative",
    "resolve", "parse",      "format"};
static const char* const kProcessExports[] = {"default", "run",      "nextTick",
                                              "exit",    "platform", "arch"};
static const char* const kUrlExports[] = {"default", "URL", "URLSearchParams",
                                          "fileURLToPath", "pathToFileURL"};
static const char* const kZlibExports[] = {
    "default", "deflateSync", "inflateSync", "gzipSync", "gunzipSync"};

static void ModuleExports(Str name, const char* const** values, int* count) {
    *values = nullptr;
    *count = 0;
    if ((StrEq(name, StrL("gpui")) || StrEq(name, StrL("gpui-kit")))) {
        *values = kGpuiExports;
        *count = (int)(sizeof(kGpuiExports) / sizeof(kGpuiExports[0]));
    } else if (StrEq(name, StrL("gpui-base"))) {
        *values = kBaseExports;
        *count = (int)(sizeof(kBaseExports) / sizeof(kBaseExports[0]));
    } else if (StrEq(name, StrL("gpui-fps"))) {
        *values = kFpsExports;
        *count = (int)(sizeof(kFpsExports) / sizeof(kFpsExports[0]));
    } else if (StrEq(name, StrL("buffer"))) {
        *values = kBufferExports;
        *count = (int)(sizeof(kBufferExports) / sizeof(kBufferExports[0]));
    } else if (StrEq(name, StrL("console"))) {
        *values = kConsoleExports;
        *count = (int)(sizeof(kConsoleExports) / sizeof(kConsoleExports[0]));
    } else if (StrEq(name, StrL("crypto"))) {
        *values = kCryptoExports;
        *count = (int)(sizeof(kCryptoExports) / sizeof(kCryptoExports[0]));
    } else if (StrEq(name, StrL("fs/promises"))) {
        *values = kFsExports;
        *count = (int)(sizeof(kFsExports) / sizeof(kFsExports[0]));
    } else if (StrEq(name, StrL("os"))) {
        *values = kOsExports;
        *count = (int)(sizeof(kOsExports) / sizeof(kOsExports[0]));
    } else if (StrEq(name, StrL("path"))) {
        *values = kPathExports;
        *count = (int)(sizeof(kPathExports) / sizeof(kPathExports[0]));
    } else if (StrEq(name, StrL("process"))) {
        *values = kProcessExports;
        *count = (int)(sizeof(kProcessExports) / sizeof(kProcessExports[0]));
    } else if (StrEq(name, StrL("url"))) {
        *values = kUrlExports;
        *count = (int)(sizeof(kUrlExports) / sizeof(kUrlExports[0]));
    } else if (StrEq(name, StrL("zlib"))) {
        *values = kZlibExports;
        *count = (int)(sizeof(kZlibExports) / sizeof(kZlibExports[0]));
    }
}

static const char* BuiltinObject(Str name) {
    if ((StrEq(name, StrL("gpui")) || StrEq(name, StrL("gpui-kit"))) ||
        StrEq(name, StrL("gpui-base")) || StrEq(name, StrL("gpui-shell")) ||
        StrEq(name, StrL("gpui-fps")))
        return "__gpui";
    if (StrEq(name, StrL("buffer"))) return "__shell_buffer";
    if (StrEq(name, StrL("console"))) return "console";
    if (StrEq(name, StrL("crypto"))) return "__shell_crypto";
    if (StrEq(name, StrL("fs/promises"))) return "__shell_fs";
    if (StrEq(name, StrL("os"))) return "__shell_os";
    if (StrEq(name, StrL("path"))) return "__shell_path";
    if (StrEq(name, StrL("process"))) return "process";
    if (StrEq(name, StrL("url"))) return "__shell_url";
    if (StrEq(name, StrL("zlib"))) return "__shell_zlib";
    return "__gpui";
}

static int InitBuiltinModule(JSContext* ctx, JSModuleDef* module) {
    JSAtom atom = JS_GetModuleName(ctx, module);
    const char* name = JS_AtomToCString(ctx, atom);
    Str moduleName = name ? Str(name) : Str{};
    const char* const* exports = nullptr;
    int count = 0;
    ModuleExports(moduleName, &exports, &count);
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue api = JS_GetPropertyStr(ctx, global, BuiltinObject(moduleName));
    int result = 0;
    for (int i = 0; i < count; i++) {
        JSValue value = StrEq(Str(exports[i]), StrL("default"))
                            ? JS_DupValue(ctx, api)
                            : JS_GetPropertyStr(ctx, api, exports[i]);
        if (JS_IsException(value) ||
            JS_SetModuleExport(ctx, module, exports[i], value) < 0) {
            if (!JS_IsException(value)) JS_FreeValue(ctx, value);
            result = -1;
            break;
        }
    }
    JS_FreeValue(ctx, api);
    JS_FreeValue(ctx, global);
    if (name) JS_FreeCString(ctx, name);
    JS_FreeAtom(ctx, atom);
    return result;
}

static int LastByte(Str value, char needle) {
    for (int i = value.len - 1; i >= 0; i--) {
        if (value.s[i] == needle) return i;
    }
    return -1;
}

static AppModule* ApplicationForBase(ShellRuntimeImpl* impl, Str base) {
    int tag = LastByte(base, '?');
    if (tag < 0 || !StrStartsWith(Str(base.s + tag, base.len - tag), "?v="))
        return nullptr;
    uint32_t generation = 0;
    for (int at = tag + 3; at < base.len; at++) {
        if (base.s[at] < '0' || base.s[at] > '9') return nullptr;
        generation = generation * 10u + (uint32_t)(base.s[at] - '0');
    }
    for (int i = impl->modules.len - 1; i >= 0; i--) {
        if (impl->modules[i]->generation == generation) return impl->modules[i];
    }
    return nullptr;
}

static TempStr UntagTemp(Str name) {
    int tag = LastByte(name, '?');
    int len =
        tag >= 0 && StrStartsWith(Str(name.s + tag, name.len - tag), "?v=")
            ? tag
            : name.len;
    if (len >= kMaxPath) len = kMaxPath - 1;
    return StrDupTemp(Str(name.s, len));
}

static void DirectoryName(Str* path) {
    while (path->len > 0 && path->s[path->len - 1] != '/' &&
           path->s[path->len - 1] != '\\') {
        path->s[--path->len] = 0;
    }
    while (path->len > 1 &&
           (path->s[path->len - 1] == '/' || path->s[path->len - 1] == '\\')) {
        path->s[--path->len] = 0;
    }
}

static bool WithinRoot(Str root, Str path) {
    if (path.len < root.len) return false;
#if GPUI_OS_WINDOWS
    if (StrCmpNI(root.s, path.s, root.len) != 0) return false;
#else
    if (!StrEq(root, Str(path.s, root.len))) return false;
#endif
    return path.len == root.len || path.s[root.len] == '/';
}

static char* ModuleNormalize(JSContext* ctx, const char* base, const char* name,
                             void* opaque) {
    ShellRuntimeImpl* impl = (ShellRuntimeImpl*)opaque;
    Str baseName = Str(base);
    Str moduleName = Str(name);
    if (IsBuiltin(moduleName)) {
        char* out = (char*)js_malloc(ctx, (size_t)moduleName.len + 1);
        if (out) memcpy(out, moduleName.s, (size_t)moduleName.len + 1);
        return out;
    }
    if (name[0] != '.' && name[0] != '/' && name[0] != '\\') {
        bool release = false;
        Policy* policy = shell::ScopeCurrentPolicy();
        if (!policy) {
            policy = PolicyDefault();
            release = true;
        }
        HostModules* modules = PolicyHostModules(policy);
        HostModule* found = HostModulesGet(modules, moduleName);
        uint64_t generation = HostModulesGeneration(modules);
        HostModulesRelease(modules);
        if (release) PolicyRelease(policy);
        if (found) {
            TempStr tagged = fmt("host:%s?m=%llu", moduleName,
                                 (unsigned long long)generation);
            char* out = tagged ? (char*)js_malloc(ctx, (size_t)tagged.len + 1)
                               : nullptr;
            if (out) {
                memcpy(out, tagged.s, (size_t)tagged.len + 1);
            }
            return out;
        }
    }
    AppModule* application = ApplicationForBase(impl, baseName);
    if (!application) {
        JS_ThrowReferenceError(
            ctx, "cannot identify the application importing `%s` from `%s`",
            name, base);
        return nullptr;
    }
    // `AppModules::candidate`. A relative specifier stays inside whichever
    // tree the importing module lives in — the application, or the dependency
    // checkout — and a bare one names a dependency, its subpath, or nothing.
    TempStr basePath = UntagTemp(baseName);
    const shell::MaterializedDependency* importing = nullptr;
    for (int i = 0; i < application->dependencies.items.len; i++) {
        const shell::MaterializedDependency& dependency =
            application->dependencies.items[i];
        if (WithinRoot(dependency.root, basePath)) importing = &dependency;
    }
    TempStr start;
    TempStr candidate = AllocStrTemp(kMaxPath - 1);
    candidate.s[0] = 0;
    Str boundary = application->root;
    Str tail = moduleName;
    if (name[0] == '.') {
        start = StrDupTemp(basePath);
        DirectoryName(&start);
        if (importing) boundary = importing->root;
    } else {
        const shell::MaterializedDependency* named = nullptr;
        for (int i = 0; i < application->dependencies.items.len; i++) {
            const shell::MaterializedDependency& dependency =
                application->dependencies.items[i];
            Str dependencyName = dependency.name;
            if (!StrStartsWith(moduleName, dependencyName)) continue;
            char after = dependencyName.len < moduleName.len
                             ? moduleName.s[dependencyName.len]
                             : 0;
            if (after != 0 && after != '/') continue;
            if (!named || dependencyName.len > named->name.len)
                named = &dependency;
        }
        if (named) {
            if (moduleName.len == named->name.len) {
                // The entry was resolved and confined when it was
                // materialized; no second file test can improve on that.
                TempStr entryTagged =
                    fmt("%s?v=%u", named->entry, application->generation);
                if (!entryTagged || entryTagged.len >= kMaxPath + 32)
                    return nullptr;
                char* out = (char*)js_malloc(ctx, (size_t)entryTagged.len + 1);
                if (out)
                    memcpy(out, entryTagged.s, (size_t)entryTagged.len + 1);
                return out;
            }
            start = StrDupTemp(named->root);
            boundary = named->root;
            tail = Str(moduleName.s + named->name.len + 1,
                       moduleName.len - named->name.len - 1);
        } else if (importing) {
            // A dependency imports nothing but its own files and the
            // dependencies the application declared.
            JS_ThrowReferenceError(ctx, "cannot resolve module `%s` from `%s`",
                                   name, base);
            return nullptr;
        } else {
            start = StrDupTemp(application->root);
        }
    }
    TempStr joined = fmt("%s/%s", start, tail);
    if (!joined || joined.len >= kMaxPath) {
        JS_ThrowReferenceError(ctx, "module path `%s` is too long", name);
        return nullptr;
    }
    bool found = PlatCanonicalPath(joined.s, candidate.s, candidate.len + 1) &&
                 PlatFileExists(candidate.s);
    if (!found) {
        joined = fmt("%s/%s.js", start, tail);
        found = joined && joined.len < kMaxPath &&
                PlatCanonicalPath(joined.s, candidate.s, candidate.len + 1) &&
                PlatFileExists(candidate.s);
    }
    if (!found) {
        JS_ThrowReferenceError(ctx, "cannot resolve module `%s` from `%s`",
                               name, base);
        return nullptr;
    }
    Str canonical(candidate.s);
    if (!WithinRoot(boundary, canonical)) {
        JS_ThrowReferenceError(
            ctx, "module `%s` resolves outside the application directory `%s`",
            name, boundary.s);
        return nullptr;
    }
    TempStr tagged = fmt("%s?v=%u", canonical, application->generation);
    if (!tagged || tagged.len >= kMaxPath + 32) return nullptr;
    char* out = (char*)js_malloc(ctx, (size_t)tagged.len + 1);
    if (out) memcpy(out, tagged.s, (size_t)tagged.len + 1);
    return out;
}

static bool HostModuleTag(Str tagged, Str* module, uint64_t* generation) {
    if (!StrStartsWith(tagged, "host:")) return false;
    int tag = LastByte(tagged, '?');
    if (tag < 0 || tag == 5 ||
        !StrStartsWith(Str(tagged.s + tag, tagged.len - tag), "?m="))
        return false;
    uint64_t value = 0;
    for (int at = tag + 3; at < tagged.len; at++) {
        if (tagged.s[at] < '0' || tagged.s[at] > '9' ||
            value > (UINT64_MAX - (uint64_t)(tagged.s[at] - '0')) / 10)
            return false;
        value = value * 10 + (uint64_t)(tagged.s[at] - '0');
    }
    if (module) *module = Str(tagged.s + 5, tag - 5);
    if (generation) *generation = value;
    return true;
}

static void AppendJsQuoted(StrBuilder* out, Str value) {
    out->AppendChar('"');
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < value.len; i++) {
        uint8_t c = (uint8_t)value.s[i];
        if (c == '"' || c == '\\') {
            out->AppendChar('\\');
            out->AppendChar((char)c);
        } else if (c == '\n') {
            out->Append(StrL("\\n"));
        } else if (c == '\r') {
            out->Append(StrL("\\r"));
        } else if (c == '\t') {
            out->Append(StrL("\\t"));
        } else if (c < 0x20) {
            char escaped[] = {'\\', 'u', '0', '0', hex[c >> 4], hex[c & 15]};
            out->Append(Str(escaped, 6));
        } else {
            out->AppendChar((char)c);
        }
    }
    out->AppendChar('"');
}

static JSModuleDef* LoadHostModule(JSContext* ctx, Str name) {
    Str moduleName;
    uint64_t generation = 0;
    if (!HostModuleTag(name, &moduleName, &generation)) return nullptr;
    bool release = false;
    Policy* policy = shell::ScopeCurrentPolicy();
    if (!policy) {
        policy = PolicyDefault();
        release = true;
    }
    HostModules* modules = PolicyHostModules(policy);
    HostModule* module = HostModulesGet(modules, moduleName);
    if (!module || HostModulesGeneration(modules) != generation) {
        HostModulesRelease(modules);
        if (release) PolicyRelease(policy);
        JS_ThrowReferenceError(
            ctx,
            "the HostModule registry changed while `%.*s` was being imported; "
            "export modules before loading an application",
            moduleName.len, moduleName.s);
        return nullptr;
    }
    StrBuilder source;
    for (int i = 0; i < module->FunctionCount(); i++) {
        Str function = module->FunctionName(i);
        if (!HostIsIdentifier(function)) {
            HostModulesRelease(modules);
            if (release) PolicyRelease(policy);
            JS_ThrowReferenceError(ctx,
                                   "HostModule `%.*s` registered `%.*s`, which "
                                   "is not a JavaScript identifier",
                                   moduleName.len, moduleName.s, function.len,
                                   function.s);
            return nullptr;
        }
        source.Append(StrL("export const "));
        source.Append(function);
        source.Append(StrL("=(...args)=>"));
        source.Append(module->IsAsync(function) ? StrL("__host_async_call(")
                                                : StrL("__host_call("));
        AppendJsQuoted(&source, moduleName);
        source.AppendChar(',');
        AppendJsQuoted(&source, function);
        source.Append(StrL(",args);\n"));
    }
    HostModulesRelease(modules);
    if (release) PolicyRelease(policy);
    Str script = source.TakeStr();
    JSValue value =
        JS_Eval(ctx, script.s ? script.s : "", (size_t)script.len, name.s,
                JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    StrFree(script);
    if (JS_IsException(value)) return nullptr;
    JSModuleDef* definition = (JSModuleDef*)JS_VALUE_GET_PTR(value);
    JS_FreeValue(ctx, value);
    return definition;
}

static JSModuleDef* ModuleLoad(JSContext* ctx, const char* name, void*) {
    Str moduleName = Str(name);
    if (IsBuiltin(moduleName)) {
        JSModuleDef* module = JS_NewCModule(ctx, name, InitBuiltinModule);
        if (!module) return nullptr;
        const char* const* exports = nullptr;
        int count = 0;
        ModuleExports(moduleName, &exports, &count);
        for (int i = 0; i < count; i++) {
            if (JS_AddModuleExport(ctx, module, exports[i]) < 0) return nullptr;
        }
        return module;
    }
    if (StrStartsWith(moduleName, "host:"))
        return LoadHostModule(ctx, moduleName);
    TempStr path = UntagTemp(moduleName);
    ShellError error = {};
    TempStr source = ReadModuleFileTemp(path, &error);
    if (!source.s) {
        JS_ThrowReferenceError(
            ctx, "%.*s", error.message.len,
            error.message.s ? error.message.s : "module load failed");
        ShellErrorClear(&error);
        return nullptr;
    }
    JSValue value = JS_Eval(ctx, source.s, (size_t)source.len, name,
                            JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    if (JS_IsException(value)) return nullptr;
    JSModuleDef* module = (JSModuleDef*)JS_VALUE_GET_PTR(value);
    JS_FreeValue(ctx, value);
    return module;
}

static shell::ComponentKind ComponentKindOf(Str name) {
    struct NamedKind {
        const char* name;
        shell::ComponentKind kind;
    };
    static const NamedKind kinds[] = {
        {"div", shell::ComponentKind::Div},
        {"h_flex", shell::ComponentKind::HFlex},
        {"v_flex", shell::ComponentKind::VFlex},
        {"text", shell::ComponentKind::Text},
        {"Button", shell::ComponentKind::Button},
        {"Link", shell::ComponentKind::Link},
        {"Checkbox", shell::ComponentKind::Checkbox},
        {"Switch", shell::ComponentKind::Switch},
        {"Scrollbar", shell::ComponentKind::Scrollbar},
        {"Input", shell::ComponentKind::Input},
        {"Textarea", shell::ComponentKind::Textarea},
        {"NumberInput", shell::ComponentKind::NumberInput},
        {"OtpInput", shell::ComponentKind::OtpInput},
        {"svg", shell::ComponentKind::Svg},
        {"image", shell::ComponentKind::Image},
        {"Tabs", shell::ComponentKind::Tabs},
        {"Tab", shell::ComponentKind::Tab},
        {"Progress", shell::ComponentKind::Progress},
        {"ProgressTrack", shell::ComponentKind::ProgressTrack},
        {"ProgressIndicator", shell::ComponentKind::ProgressIndicator},
        {"FpsMonitor", shell::ComponentKind::FpsMonitor},
        {"Slider", shell::ComponentKind::Slider},
        {"SliderTrack", shell::ComponentKind::SliderTrack},
        {"SliderIndicator", shell::ComponentKind::SliderIndicator},
        {"SliderThumb", shell::ComponentKind::SliderThumb},
        {"Radio", shell::ComponentKind::Radio},
        {"Toggle", shell::ComponentKind::Toggle},
        {"RadioGroup", shell::ComponentKind::RadioGroup},
        {"ToggleGroup", shell::ComponentKind::ToggleGroup},
        {"Table", shell::ComponentKind::Table},
        {"TableHeader", shell::ComponentKind::TableHeader},
        {"TableBody", shell::ComponentKind::TableBody},
        {"TableRow", shell::ComponentKind::TableRow},
        {"TableHead", shell::ComponentKind::TableHead},
        {"TableCell", shell::ComponentKind::TableCell},
        {"TableCaption", shell::ComponentKind::TableCaption},
        {"h_resizable", shell::ComponentKind::HResizable},
        {"v_resizable", shell::ComponentKind::VResizable},
        {"ResizablePanel", shell::ComponentKind::ResizablePanel},
        {"Collapsible", shell::ComponentKind::Collapsible},
        {"Popover", shell::ComponentKind::Popover},
        {"HoverCard", shell::ComponentKind::HoverCard},
        {"Popup", shell::ComponentKind::Popup},
        {"Select", shell::ComponentKind::Select},
        {"Combobox", shell::ComponentKind::Combobox},
        {"DatePicker", shell::ComponentKind::DatePicker},
        {"Accordion", shell::ComponentKind::Accordion},
        {"AccordionItem", shell::ComponentKind::AccordionItem},
        {"AccordionHeader", shell::ComponentKind::AccordionHeader},
        {"AccordionPanel", shell::ComponentKind::AccordionPanel},
        {"AccordionTrigger", shell::ComponentKind::AccordionTrigger},
        {"Pagination", shell::ComponentKind::Pagination},
        {"Avatar", shell::ComponentKind::Avatar},
        {"AvatarImage", shell::ComponentKind::AvatarImage},
        {"AvatarFallback", shell::ComponentKind::AvatarFallback},
        {"dock_content", shell::ComponentKind::DockContent},
    };
    for (const NamedKind& named : kinds) {
        if (StrEq(name, named.name)) return named.kind;
    }
    return shell::ComponentKind::Div;
}

static bool JsString(JSContext* ctx, JSValueConst value, Arena* arena,
                     Str* out) {
    size_t len = 0;
    const char* text = JS_ToCStringLen(ctx, &len, value);
    if (!text) return false;
    *out = StrDup(arena, Str(text, (int)len));
    JS_FreeCString(ctx, text);
    return true;
}

static JSValue NativeComponent(JSContext* ctx, JSValueConst, int argc,
                               JSValueConst* argv) {
    ShellRuntimeImpl* impl = (ShellRuntimeImpl*)JS_GetContextOpaque(ctx);
    if (!impl || argc < 1)
        return JS_ThrowTypeError(ctx, "component kind is missing");
    Arena* arena = ArenaNew();
    Str kind;
    if (!JsString(ctx, argv[0], arena, &kind)) {
        ArenaDelete(arena);
        return JS_EXCEPTION;
    }
    shell::Component component = {};
    component.kind = ComponentKindOf(kind);
    if (argc > 1 && !JS_IsUndefined(argv[1]) && !JS_IsNull(argv[1]) &&
        !JsString(ctx, argv[1], arena, &component.text)) {
        ArenaDelete(arena);
        return JS_EXCEPTION;
    }
    if (argc > 2 && !JS_IsUndefined(argv[2]) && !JS_IsNull(argv[2])) {
        int64_t handle = 0;
        if (JS_ToInt64(ctx, &handle, argv[2]) < 0 || handle < 0) {
            ArenaDelete(arena);
            return JS_ThrowTypeError(ctx,
                                     "component handle must be non-negative");
        }
        component.handle = (uint64_t)handle;
    }
    if (argc > 3 && !JS_IsUndefined(argv[3]) && !JS_IsNull(argv[3])) {
        if (JS_ToUint32(ctx, &component.index, argv[3]) < 0) {
            ArenaDelete(arena);
            return JS_EXCEPTION;
        }
    }
    shell::SpecId id = impl->scratch->Push(component);
    ArenaDelete(arena);
    return JS_NewUint32(ctx, id);
}

static bool JsArrayString(JSContext* ctx, JSValueConst array, uint32_t index,
                          Arena* arena, Str* out) {
    JSValue value = JS_GetPropertyUint32(ctx, array, index);
    bool ok = !JS_IsException(value) && JsString(ctx, value, arena, out);
    JS_FreeValue(ctx, value);
    return ok;
}

static bool ParseFiniteText(Str text, float* out) {
    if (!text.s || text.len <= 0 || text.len >= 64) return false;
    TempStr buf = StrDupTemp(text);
    char* end = nullptr;
    double value = strtod(buf.s, &end);
    if (!end || end == buf.s || *end || !isfinite(value)) return false;
    *out = (float)value;
    return true;
}

static JSValue NativePath(JSContext* ctx, JSValueConst, int argc,
                          JSValueConst* argv) {
    ShellRuntimeImpl* impl = (ShellRuntimeImpl*)JS_GetContextOpaque(ctx);
    if (!impl || argc < 6)
        return JS_ThrowTypeError(ctx, "path description is incomplete");
    double opacity = 0, width = 0;
    if (JS_ToFloat64(ctx, &opacity, argv[3]) < 0 ||
        JS_ToFloat64(ctx, &width, argv[5]) < 0)
        return JS_EXCEPTION;
    if (!isfinite(opacity) || opacity < 0 || !isfinite(width) || width < 0)
        return JS_ThrowTypeError(
            ctx,
            "path opacity and stroke width must be finite and non-negative");
    int64_t count64 = 0;
    if (JS_GetLength(ctx, argv[2], &count64) < 0 || count64 < 0 || count64 > 8)
        return JS_ThrowTypeError(ctx, "path background has invalid values");
    Arena* arena = ArenaNew();
    Str kind, colorSpace;
    if (!JsString(ctx, argv[1], arena, &kind) ||
        !JsString(ctx, argv[4], arena, &colorSpace)) {
        ArenaDelete(arena);
        return JS_EXCEPTION;
    }
    Str values[8] = {};
    for (int i = 0; i < (int)count64; i++) {
        if (!JsArrayString(ctx, argv[2], (uint32_t)i, arena, &values[i])) {
            ArenaDelete(arena);
            return JS_EXCEPTION;
        }
    }
    shell::Component component = {};
    component.kind = JS_ToBool(ctx, argv[0]) ? shell::ComponentKind::PathFill
                                             : shell::ComponentKind::PathStroke;
    component.strokeWidth = (float)width;
    component.background.opacity = (float)opacity;
    component.background.colorSpace = colorSpace;
    bool valid = true;
    if (StrEq(kind, StrL("solid"))) {
        component.background.kind = shell::BackgroundKind::Solid;
        valid = count64 >= 1;
        if (valid) component.background.color = values[0];
    } else if (StrEq(kind, StrL("linear-gradient"))) {
        component.background.kind = shell::BackgroundKind::LinearGradient;
        valid =
            count64 >= 5 &&
            ParseFiniteText(values[0], &component.background.angle) &&
            ParseFiniteText(values[2], &component.background.fromPosition) &&
            ParseFiniteText(values[4], &component.background.toPosition);
        if (valid) {
            component.background.fromColor = values[1];
            component.background.toColor = values[3];
        }
    } else if (StrEq(kind, StrL("pattern-slash"))) {
        component.background.kind = shell::BackgroundKind::PatternSlash;
        valid = count64 >= 3 &&
                ParseFiniteText(values[1], &component.background.width) &&
                ParseFiniteText(values[2], &component.background.interval);
        if (valid) component.background.color = values[0];
    } else if (StrEq(kind, StrL("checkerboard"))) {
        component.background.kind = shell::BackgroundKind::Checkerboard;
        valid = count64 >= 2 &&
                ParseFiniteText(values[1], &component.background.size);
        if (valid) component.background.color = values[0];
    } else {
        valid = false;
    }
    if (!valid) {
        ArenaDelete(arena);
        return JS_ThrowTypeError(ctx, "invalid path Background description");
    }
    shell::SpecId id = impl->scratch->Push(component);
    ArenaDelete(arena);
    return JS_NewUint32(ctx, id);
}

static bool JsSpecId(JSContext* ctx, JSValueConst value, shell::SpecId* out) {
    return JS_ToUint32(ctx, out, value) == 0;
}

static JSValue SpecFailure(JSContext* ctx, const shell::SpecError& failure) {
    Arena* arena = ArenaNew();
    Str message = shell::SpecErrorMessage(arena, failure);
    JSValue result = JS_ThrowTypeError(ctx, "%.*s", message.len, message.s);
    ArenaDelete(arena);
    return result;
}

static JSValue NativeAttach(JSContext* ctx, JSValueConst, int argc,
                            JSValueConst* argv) {
    ShellRuntimeImpl* impl = (ShellRuntimeImpl*)JS_GetContextOpaque(ctx);
    shell::SpecId parent = 0, child = 0;
    if (!impl || argc < 2 || !JsSpecId(ctx, argv[0], &parent) ||
        !JsSpecId(ctx, argv[1], &child)) {
        return JS_ThrowTypeError(ctx, "child(element) expects an element");
    }
    shell::SpecError failure = {};
    if (!impl->scratch->Attach(parent, child, &failure)) {
        return SpecFailure(ctx, failure);
    }
    return JS_UNDEFINED;
}

static JSValue NativeState(JSContext* ctx, JSValueConst, int argc,
                           JSValueConst* argv) {
    ShellRuntimeImpl* impl = (ShellRuntimeImpl*)JS_GetContextOpaque(ctx);
    shell::SpecId owner = 0;
    if (!impl || argc < 2 || !JsSpecId(ctx, argv[0], &owner)) {
        return JS_ThrowTypeError(ctx, "state style needs an element");
    }
    Arena* arena = ArenaNew();
    Str name;
    if (!JsString(ctx, argv[1], arena, &name)) {
        ArenaDelete(arena);
        return JS_EXCEPTION;
    }
    shell::Component component = {};
    shell::SpecId state = impl->scratch->Push(component);
    shell::SpecError failure = {};
    if (!impl->scratch->Claim(state, &failure)) {
        ArenaDelete(arena);
        return SpecFailure(ctx, failure);
    }
    shell::SpecOp op = {};
    op.kind = shell::SpecOpKind::StateStyle;
    op.name = name;
    op.node = state;
    if (!impl->scratch->PushOp(owner, op, &failure)) {
        ArenaDelete(arena);
        return SpecFailure(ctx, failure);
    }
    ArenaDelete(arena);
    return JS_NewUint32(ctx, state);
}

static JSValue NativeSlot(JSContext* ctx, JSValueConst, int argc,
                          JSValueConst* argv) {
    ShellRuntimeImpl* impl = (ShellRuntimeImpl*)JS_GetContextOpaque(ctx);
    shell::SpecId owner = 0, element = 0;
    if (!impl || argc < 3 || !JsSpecId(ctx, argv[0], &owner) ||
        !JsSpecId(ctx, argv[2], &element)) {
        return JS_ThrowTypeError(ctx, "slot(element) expects an element");
    }
    Arena* arena = ArenaNew();
    Str name;
    if (!JsString(ctx, argv[1], arena, &name)) {
        ArenaDelete(arena);
        return JS_EXCEPTION;
    }
    shell::SpecError failure = {};
    if (!impl->scratch->Claim(element, &failure)) {
        ArenaDelete(arena);
        return SpecFailure(ctx, failure);
    }
    shell::SpecOp op = {};
    op.kind = shell::SpecOpKind::Slot;
    op.name = name;
    op.node = element;
    if (!impl->scratch->PushOp(owner, op, &failure)) {
        ArenaDelete(arena);
        return SpecFailure(ctx, failure);
    }
    ArenaDelete(arena);
    return JS_UNDEFINED;
}

static bool IsCallbackMethod(Str name) {
    static const char names[] =
        "on_click\0on_mouse_move\0on_hover\0on_item_click\0"
        "on_item_secondary_click\0on_change\0"
        "on_open_change\0on_confirm\0on_dismiss\0on_step\0on_resize\0"
        "on_key_down\0on_key_up\0on_mouse_down_out\0on_scroll_wheel\0"
        // A dock's chrome handlers. They are callbacks like any other; what
        // makes them different is that they are asked from inside the frame
        // rather than from render, which the Layout scope around the call and
        // the description cache behind it are for.
        "tab_bar\0empty_group\0drop_indicator\0dock\0tile_drag_bar\0"
        "tile_resize_handles\0";
    for (const char* at = names; *at; at += strlen(at) + 1) {
        if (StrEq(name, at)) return true;
    }
    return false;
}

// A dock command carries no script value: it names a container in the area
// and what to ask it. That is why a tab can report its click at all — a chrome
// handler runs once per frame for as long as the dock is on screen, so a
// callback registered inside one would pile up the way a virtual list's row
// handlers would.
static bool IsDockCommand(Str name) {
    static const char names[] =
        "select_tab\0close_panel\0toggle_zoom\0drag_tab\0drop_tab\0"
        "toggle_dock\0resize_dock\0move_tile\0resize_tile\0raise_tile\0"
        "toggle_tile_zoom\0close_tile\0";
    for (const char* at = names; *at; at += strlen(at) + 1) {
        if (StrEq(name, at)) return true;
    }
    return false;
}

// The two element methods that take an argument *and* a handler. The button
// is folded into the recorded op name — three fixed names GPUI's own
// MouseButton maps onto — so the op stays the name-and-id pair every other
// callback uses.
static const char* MouseButtonCallbackName(Str method, Str button) {
    bool down = StrEq(method, StrL("on_mouse_down"));
    if (StrEq(button, StrL("left"))) {
        return down ? "on_mouse_down_left" : "on_mouse_up_left";
    }
    if (StrEq(button, StrL("right"))) {
        return down ? "on_mouse_down_right" : "on_mouse_up_right";
    }
    if (StrEq(button, StrL("middle"))) {
        return down ? "on_mouse_down_middle" : "on_mouse_up_middle";
    }
    return nullptr;
}

static bool IsParamStyle(Str name) {
    static const char names[] =
        "w\0h\0size\0min_w\0min_h\0min_size\0max_w\0max_h\0max_size\0"
        "p\0px\0py\0pt\0pb\0pl\0pr\0m\0mx\0my\0mt\0mb\0ml\0mr\0"
        "inset\0top\0bottom\0left\0right\0gap\0gap_x\0gap_y\0"
        "flex_grow\0flex_shrink\0flex_basis\0bg\0text_color\0text_bg\0"
        "text_size\0font_family\0font_weight\0line_height\0opacity\0"
        "border\0border_t\0border_b\0border_l\0border_r\0border_x\0"
        "border_y\0border_color\0rounded\0rounded_t\0rounded_b\0"
        "rounded_l\0rounded_r\0rounded_tl\0rounded_tr\0rounded_bl\0"
        "rounded_br\0";
    for (const char* at = names; *at; at += strlen(at) + 1) {
        if (StrEq(name, at)) return true;
    }
    return false;
}

static bool IsBehavior(Str name) {
    static const char names[] =
        "disabled\0selected\0checked\0accessibility_label\0tooltip\0role\0"
        "aria_selected\0aria_active_descendant\0track_focus\0track_scroll\0"
        "content_focus_handle\0tab_index\0tab_stop\0href\0id\0"
        "overflow_scroll\0overflow_x_scroll\0overflow_y_scroll\0"
        "overflow_scrollbar\0overflow_x_scrollbar\0overflow_y_scrollbar\0"
        "mode\0scroll_size\0viewport_from_layout\0controls_right\0"
        "panel_visible\0panel_size\0size_range\0set_position\0pressed\0"
        "start\0value\0indeterminate\0axis\0row_count\0column_count\0"
        "open\0default_open\0overlay_closable\0anchor\0mouse_button\0"
        "open_delay\0close_delay\0transition\0spring\0"
        "with_item_to_measure_index\0close\0"
        "key_context\0aria_level\0keep_mounted\0";
    for (const char* at = names; *at; at += strlen(at) + 1) {
        if (StrEq(name, at)) return true;
    }
    return false;
}

static bool BridgeValue(JSContext* ctx, JSValueConst value, Arena* arena,
                        shell::Bridged* out) {
    if (JS_IsUndefined(value) || JS_IsNull(value)) {
        *out = shell::Bridged::Nil();
        return true;
    }
    if (JS_IsBool(value)) {
        *out = shell::Bridged::Bool(JS_ToBool(ctx, value) != 0);
        return true;
    }
    if (JS_IsNumber(value)) {
        double number = 0;
        if (JS_ToFloat64(ctx, &number, value) < 0) return false;
        *out = shell::Bridged::Number(number);
        return true;
    }
    if (JS_IsString(value)) {
        Str text;
        if (!JsString(ctx, value, arena, &text)) return false;
        *out = shell::Bridged::String(text);
        return true;
    }
    JS_ThrowTypeError(ctx,
                      "script values crossing into an element method must be "
                      "nil, boolean, number or string");
    return false;
}

// A description recorded once, and filled per call.
//
// A snapshot removes the cost of *no* change. It does nothing for a *small*
// one: when a price moves, the structure around it is identical and the whole
// view is described again anyway — every div(), every .gap(), every crossing.
// A template splits that description in two:
//
//   first call   body(sentinel, sentinel, ...)  ->  structure + slot list
//   every call   graft the structure, write the slots      (no script runs)
//
// The slots are found by running the body once with a sentinel object in each
// parameter position; wherever a sentinel comes to rest in the recorded
// description is a slot, and what is left over is structure. Three positions
// can hold one, which is the whole of SlotSite: the string a Text node
// carries, one argument of a recorded param style, and the CallbackId of a
// recorded handler.
//
// Two rules this enforces rather than documents. An argument may be passed
// through, not computed on: a template literal would consume the sentinel
// during discovery and bake a constant into the structure, so the sentinel
// refuses to become a primitive, in the prelude. And a handler must arrive as
// an argument: a closure written inside a body is created once and would
// capture that first call's values for as long as the template lived, so a
// body that registers one is refused at definition.
//
// It is deliberately not part of the script surface — asking an author to mark
// their hot paths is a performance annotation in the source. `globalThis
// .__template` is how the tests that pin its behavior reach it.

// The property a sentinel carries, and the only thing that identifies one.
static const char kTemplateSentinel[] = "__slot";

// The CallbackId a handler slot holds until a call fills it. A value no arena
// ever mints, so a template that reached definition holding a *real* callback
// is a body that registered an inline handler.
static const shell::CallbackId kTemplateUnfilled = UINT64_MAX;

// The template parameter a value stands for, if it is a sentinel.
//
// Costs one tag check for the values a description is actually made of — a
// string, a number, a boolean — because only an object can carry the marker.
static bool SlotIndex(JSContext* ctx, JSValueConst value, uint16_t* out) {
    if (!JS_IsObject(value) || JS_IsFunction(ctx, value)) return false;
    JSValue marker = JS_GetPropertyStr(ctx, value, kTemplateSentinel);
    if (JS_IsException(marker)) {
        JS_FreeValue(ctx, JS_GetException(ctx));
        return false;
    }
    double index = 0;
    bool ok = JS_IsNumber(marker) && JS_ToFloat64(ctx, &index, marker) == 0 &&
              index >= 0 && index <= 65535;
    JS_FreeValue(ctx, marker);
    if (!ok) return false;
    *out = (uint16_t)index;
    return true;
}

static bool RequireDiscovery(JSContext* ctx, ShellRuntimeImpl* impl) {
    if (impl && impl->discovery) return true;
    JS_ThrowTypeError(ctx,
                      "a template argument escaped the body that declared it. "
                      "It can be passed to a builder call inside the template, "
                      "and nowhere else");
    return false;
}

static bool NoteSlot(JSContext* ctx, ShellRuntimeImpl* impl,
                     const shell::Slot& slot) {
    if (!impl || !impl->discovery) {
        JS_ThrowTypeError(ctx, "no template is being defined");
        return false;
    }
    VecAppend(impl->discovery->slots, slot);
    return true;
}

// The index of the operation just pushed onto `id`.
static bool LastOpIndex(JSContext* ctx, ShellRuntimeImpl* impl,
                        shell::SpecId id, uint16_t* out) {
    const shell::SpecNode* node = impl->scratch->Node(id);
    if (!node || node->ops.len == 0) {
        JS_ThrowTypeError(ctx, "no operation to attach a slot to");
        return false;
    }
    if (node->ops.len - 1 > 65535) {
        JS_ThrowTypeError(ctx,
                          "one element recorded more operations than a "
                          "template can address");
        return false;
    }
    *out = (uint16_t)(node->ops.len - 1);
    return true;
}

// Records the text node a `.child(argument)` inside a body describes.
static JSValue NativeTextSlot(JSContext* ctx, JSValueConst, int argc,
                              JSValueConst* argv) {
    ShellRuntimeImpl* impl = (ShellRuntimeImpl*)JS_GetContextOpaque(ctx);
    uint32_t argument = 0;
    if (!impl || argc < 1 || JS_ToUint32(ctx, &argument, argv[0]) < 0) {
        return JS_ThrowTypeError(ctx, "a text slot needs its argument index");
    }
    if (!RequireDiscovery(ctx, impl)) return JS_EXCEPTION;
    shell::Component component = {};
    component.kind = shell::ComponentKind::Text;
    shell::SpecId node = impl->scratch->Push(component);
    shell::Slot slot = {};
    slot.node = node;
    slot.site.kind = shell::SlotSiteKind::Text;
    slot.argument = (uint16_t)argument;
    if (!NoteSlot(ctx, impl, slot)) return JS_EXCEPTION;
    return JS_NewUint32(ctx, node);
}

// Starts recording a template body.
//
// Nesting is refused rather than supported: a body that defines or calls
// another template would have to thread the outer sentinels through the inner
// template's slots.
static JSValue NativeTemplateBegin(JSContext* ctx, JSValueConst, int argc,
                                   JSValueConst* argv) {
    ShellRuntimeImpl* impl = (ShellRuntimeImpl*)JS_GetContextOpaque(ctx);
    uint32_t arity = 0;
    if (!impl || argc < 1 || JS_ToUint32(ctx, &arity, argv[0]) < 0) {
        return JS_ThrowTypeError(ctx, "template(build) needs an arity");
    }
    if (impl->discovery) {
        return JS_ThrowTypeError(
            ctx,
            "a template body cannot define or call another template. Build "
            "the inner structure inline, or call the inner template where the "
            "outer one is called");
    }
    TemplateDiscovery* discovery = new TemplateDiscovery();
    discovery->arity = (int)arity;
    discovery->saved = impl->scratch;
    impl->scratch = new shell::SpecArena();
    impl->discovery = discovery;
    return JS_UNDEFINED;
}

static void DropDiscovery(ShellRuntimeImpl* impl) {
    TemplateDiscovery* discovery = impl->discovery;
    if (!discovery) return;
    impl->discovery = nullptr;
    VecReset(discovery->slots);
    delete discovery;
}

// Abandons a body that threw, and puts the interrupted description back.
static JSValue NativeTemplateAbort(JSContext* ctx, JSValueConst, int,
                                   JSValueConst*) {
    ShellRuntimeImpl* impl = (ShellRuntimeImpl*)JS_GetContextOpaque(ctx);
    if (impl && impl->discovery) {
        delete impl->scratch;
        impl->scratch = impl->discovery->saved;
        DropDiscovery(impl);
    }
    return JS_UNDEFINED;
}

// The first handler a body registered itself, if it registered one.
//
// A slot-filled handler carries kTemplateUnfilled and a matching note;
// anything else is a closure the body created, which a template cannot hold.
static Str InlineHandler(const shell::SpecArena* recorded,
                         const Vec<shell::Slot>& slots) {
    for (shell::SpecId id = 0; id < (shell::SpecId)recorded->Len(); id++) {
        const shell::SpecNode* node = recorded->Node(id);
        if (!node) continue;
        for (int index = 0; index < node->ops.len; index++) {
            const shell::SpecOp& op = node->ops[index];
            if (op.kind != shell::SpecOpKind::Callback) continue;
            bool filled = false;
            for (int i = 0; i < slots.len && !filled; i++) {
                filled = slots[i].node == id &&
                         slots[i].site.kind == shell::SlotSiteKind::Handler &&
                         slots[i].site.op == (uint16_t)index;
            }
            if (op.callback != kTemplateUnfilled || !filled) return op.name;
        }
    }
    return {};
}

// The first declared parameter that reached no builder call.
static int UnusedArgument(int arity, const Vec<shell::Slot>& slots) {
    for (int argument = 0; argument < arity; argument++) {
        bool used = false;
        for (int i = 0; i < slots.len && !used; i++) {
            used = slots[i].argument == (uint16_t)argument;
        }
        if (!used) return argument;
    }
    return -1;
}

// Finishes a body and answers the id its closure will keep.
static JSValue NativeTemplateEnd(JSContext* ctx, JSValueConst, int argc,
                                 JSValueConst* argv) {
    ShellRuntimeImpl* impl = (ShellRuntimeImpl*)JS_GetContextOpaque(ctx);
    if (!impl || !impl->discovery) {
        return JS_ThrowTypeError(ctx, "no template is being defined");
    }
    TemplateDiscovery* discovery = impl->discovery;
    // The interrupted description goes back before any refusal below, or a
    // rejected template would take the render with it.
    shell::SpecArena* recorded = impl->scratch;
    impl->scratch = discovery->saved;

    shell::SpecId root = 0;
    bool haveRoot = argc >= 1 && JS_IsNumber(argv[0]) &&
                    JS_ToUint32(ctx, &root, argv[0]) == 0 &&
                    root < (shell::SpecId)recorded->Len();
    JSValue failure = JS_UNDEFINED;
    if (!haveRoot) {
        failure = JS_ThrowTypeError(
            ctx, "a template body must return one element built inside it");
    } else if (recorded->MountsAnEntity()) {
        failure = JS_ThrowTypeError(
            ctx,
            "a template cannot mount a nested view or a dock area: it is "
            "grafted once per call, and GPUI mounts one entity at one place. "
            "Put the entity where the template is called");
    } else if (Str method = InlineHandler(recorded, discovery->slots); method) {
        failure = JS_ThrowTypeError(
            ctx,
            "`%.*s` inside a template body registers one handler for the life "
            "of the template, which would capture this first call's values "
            "forever. Take the handler as a parameter and pass it in",
            method.len, method.s);
    } else if (int unused = UnusedArgument(discovery->arity, discovery->slots);
               unused >= 0) {
        failure = JS_ThrowTypeError(
            ctx,
            "template argument %d is never used in the body. A parameter that "
            "reaches no builder call fills nothing, which is usually a value "
            "that was formatted or compared instead of passed through",
            unused);
    }
    if (JS_IsException(failure)) {
        delete recorded;
        DropDiscovery(impl);
        return failure;
    }

    shell::Template* tmpl = new shell::Template();
    tmpl->arena = recorded;
    tmpl->root = root;
    tmpl->slots = discovery->slots;
    tmpl->arity = discovery->arity;
    tmpl->application = shell::ScopeCurrentApplication();
    discovery->slots = Vec<shell::Slot>{};
    DropDiscovery(impl);
    VecAppend(impl->templates, tmpl);
    return JS_NewUint32(ctx, (uint32_t)(impl->templates.len - 1));
}

// Converts one argument for the position it fills.
static bool SlotValueOf(JSContext* ctx, ShellRuntimeImpl* impl,
                        const shell::Slot& slot, JSValueConst argument,
                        Arena* arena, shell::SlotValue* out) {
    switch (slot.site.kind) {
        case shell::SlotSiteKind::Text: {
            // Validated through the bridge first, so that an object or a
            // function is refused rather than stringified, and coerced after —
            // because the ordinary path is `String(value)` in the prelude, and
            // a number has to read the same whichever path recorded it.
            shell::Bridged bridged = {};
            if (!BridgeValue(ctx, argument, arena, &bridged)) return false;
            if (bridged.kind == shell::BridgedKind::Nil) {
                JS_ThrowTypeError(ctx,
                                  "this template argument fills a text child "
                                  "and must be a string, a number or a "
                                  "boolean");
                return false;
            }
            out->kind = shell::SlotValueKind::Text;
            return JsString(ctx, argument, arena, &out->text);
        }
        case shell::SlotSiteKind::Argument: {
            out->kind = shell::SlotValueKind::Value;
            return BridgeValue(ctx, argument, arena, &out->value);
        }
        case shell::SlotSiteKind::Handler: {
            if (!JS_IsFunction(ctx, argument)) {
                JS_ThrowTypeError(ctx,
                                  "this template argument fills a handler and "
                                  "must be a function");
                return false;
            }
            shell::CallbackId callback = impl->callbacks.Push(
                ctx, argument, shell::ScopeCurrentView(),
                shell::ScopeCurrentPolicy(), shell::ScopeCurrentGeneration(),
                (AppModule*)shell::ScopeCurrentApplication());
            if (callback == UINT64_MAX) {
                JS_ThrowInternalError(
                    ctx, "a callback was registered outside a snapshot build");
                return false;
            }
            out->kind = shell::SlotValueKind::Handler;
            out->handler = callback;
            return true;
        }
    }
    return false;
}

// Grafts a template into the description being recorded and writes this call's
// arguments into its slots. The whole of an instantiation: no builder method
// is interpreted, and the structure is copied rather than described.
static JSValue NativeTemplateInstantiate(JSContext* ctx, JSValueConst, int argc,
                                         JSValueConst* argv) {
    ShellRuntimeImpl* impl = (ShellRuntimeImpl*)JS_GetContextOpaque(ctx);
    uint32_t id = 0;
    if (!impl || argc < 2 || JS_ToUint32(ctx, &id, argv[0]) < 0) {
        return JS_ThrowTypeError(ctx, "a template call needs its id");
    }
    if (impl->discovery) {
        return JS_ThrowTypeError(
            ctx,
            "a template body cannot call another template. Call it where the "
            "outer template is called");
    }
    shell::Template* tmpl =
        id < (uint32_t)impl->templates.len ? impl->templates[(int)id] : nullptr;
    if (!tmpl) {
        return JS_ThrowTypeError(
            ctx,
            "this template belongs to an application that has been "
            "unloaded");
    }
    int64_t given = 0;
    if (JS_GetLength(ctx, argv[1], &given) < 0) return JS_EXCEPTION;
    if (given != (int64_t)tmpl->arity) {
        return JS_ThrowTypeError(
            ctx, "this template takes %d argument(s) and was given %d",
            tmpl->arity, (int)given);
    }

    // Every value is converted and checked before the graft, so a bad argument
    // leaves the description it was being added to untouched rather than
    // half-grown.
    Arena* arena = ArenaNew();
    shell::SlotValue* values = nullptr;
    if (tmpl->slots.len > 0) {
        values = (shell::SlotValue*)Alloc(
            arena, (int)(sizeof(shell::SlotValue) * (size_t)tmpl->slots.len));
        for (int i = 0; i < tmpl->slots.len; i++) {
            values[i] = shell::SlotValue{};
        }
    }
    for (int i = 0; i < tmpl->slots.len; i++) {
        JSValue argument =
            JS_GetPropertyUint32(ctx, argv[1], tmpl->slots[i].argument);
        bool ok =
            !JS_IsException(argument) &&
            SlotValueOf(ctx, impl, tmpl->slots[i], argument, arena, &values[i]);
        JS_FreeValue(ctx, argument);
        if (!ok) {
            ArenaDelete(arena);
            return JS_EXCEPTION;
        }
    }

    shell::SpecId root = impl->scratch->Graft(*tmpl);
    shell::SpecId base = root - tmpl->root;
    for (int i = 0; i < tmpl->slots.len; i++) {
        shell::SpecError failure = {};
        if (!impl->scratch
                 ->WriteSlot(base, tmpl->slots[i], values[i], &failure)) {
            ArenaDelete(arena);
            return SpecFailure(ctx, failure);
        }
    }
    ArenaDelete(arena);
    return JS_NewUint32(ctx, root);
}

static JSValue NativeApply(JSContext* ctx, JSValueConst, int argc,
                           JSValueConst* argv) {
    ShellRuntimeImpl* impl = (ShellRuntimeImpl*)JS_GetContextOpaque(ctx);
    shell::SpecId id = 0;
    if (!impl || argc < 3 || !JsSpecId(ctx, argv[0], &id)) {
        return JS_ThrowTypeError(ctx, "element method has no live receiver");
    }
    Arena* arena = ArenaNew();
    Str name;
    if (!JsString(ctx, argv[1], arena, &name)) {
        ArenaDelete(arena);
        return JS_EXCEPTION;
    }
    int64_t argCount64 = 0;
    if (JS_GetLength(ctx, argv[2], &argCount64) < 0 || argCount64 < 0 ||
        argCount64 > 1024) {
        ArenaDelete(arena);
        return JS_ThrowRangeError(ctx, "element method has too many arguments");
    }
    int argCount = (int)argCount64;
    shell::SpecOp op = {};
    op.name = name;
    // The script's own name for an action, plus the handler, and the two
    // pointer builders that take a button before theirs. Both are rewritten
    // into the ordinary shapes before the table below sees them: an action is
    // an ActionCallback carrying the name, a button press is a Callback under
    // one of six fixed names.
    bool actionCallback = StrEq(name, StrL("on_action"));
    bool buttonCallback =
        StrEq(name, StrL("on_mouse_down")) || StrEq(name, StrL("on_mouse_up"));
    if (actionCallback || buttonCallback) {
        if (shell::ScopeCurrentPhase() == ScopePhase::Layout) {
            JSValue thrown = JS_ThrowTypeError(
                ctx,
                "`%.*s` cannot be registered from a virtual list's item "
                "renderer or a dock's chrome handler: those are rebuilt every "
                "frame, so a handler registered there would pile up for as "
                "long as the view stood",
                name.len, name.s);
            ArenaDelete(arena);
            return thrown;
        }
        Str first;
        JSValue firstValue =
            argCount > 0 ? JS_GetPropertyUint32(ctx, argv[2], 0) : JS_UNDEFINED;
        bool haveFirst =
            JS_IsString(firstValue) && JsString(ctx, firstValue, arena, &first);
        JS_FreeValue(ctx, firstValue);
        // The message names strings the arena holds, so it is built before the
        // arena goes rather than after it.
        if (!haveFirst || first.len == 0) {
            JSValue thrown = JS_ThrowTypeError(
                ctx,
                actionCallback
                    ? "on_action(action, handler) expects the action's name "
                      "first, as a non-empty string"
                    : "%.*s(button, handler) expects a button first: "
                      "\"left\", \"right\" or \"middle\"",
                name.len, name.s);
            ArenaDelete(arena);
            return thrown;
        }
        const char* recorded = nullptr;
        if (buttonCallback) {
            recorded = MouseButtonCallbackName(name, first);
            if (!recorded) {
                JSValue thrown = JS_ThrowTypeError(
                    ctx,
                    "`%.*s` is not a mouse button; expected \"left\", "
                    "\"right\" or \"middle\"",
                    first.len, first.s);
                ArenaDelete(arena);
                return thrown;
            }
        }
        JSValue handler =
            argCount > 1 ? JS_GetPropertyUint32(ctx, argv[2], 1) : JS_UNDEFINED;
        if (!JS_IsFunction(ctx, handler)) {
            JS_FreeValue(ctx, handler);
            JSValue thrown = JS_ThrowTypeError(
                ctx, "%.*s(%s, handler) expects a function second", name.len,
                name.s, actionCallback ? "action" : "button");
            ArenaDelete(arena);
            return thrown;
        }
        shell::CallbackId callback = impl->callbacks.Push(
            ctx, handler, shell::ScopeCurrentView(),
            shell::ScopeCurrentPolicy(), shell::ScopeCurrentGeneration(),
            (AppModule*)shell::ScopeCurrentApplication());
        JS_FreeValue(ctx, handler);
        if (callback == UINT64_MAX) {
            ArenaDelete(arena);
            return JS_ThrowInternalError(
                ctx, "a callback was registered outside a snapshot build");
        }
        op.callback = callback;
        if (actionCallback) {
            op.kind = shell::SpecOpKind::ActionCallback;
            op.name = first;
        } else {
            op.kind = shell::SpecOpKind::Callback;
            op.name = Str(recorded);
        }
        shell::SpecError buttonFailure = {};
        if (!impl->scratch->PushOp(id, op, &buttonFailure)) {
            ArenaDelete(arena);
            return SpecFailure(ctx, buttonFailure);
        }
        ArenaDelete(arena);
        return JS_UNDEFINED;
    }
    // Where a template's sentinel came to rest in this call, if one did. Only
    // reachable while a template body is being discovered, because nothing
    // else hands one out.
    int slotPosition = -1;
    uint16_t slotArgument = 0;
    shell::SlotSiteKind slotSite = shell::SlotSiteKind::Handler;
    if (IsCallbackMethod(name)) {
        if (shell::ScopeCurrentPhase() == ScopePhase::Layout) {
            ArenaDelete(arena);
            return JS_ThrowTypeError(
                ctx,
                "callbacks cannot be registered from a virtual list's item "
                "renderer: the rows are rebuilt every frame, so a handler "
                "registered there would pile up for as long as the view "
                "stood. Use `on_item_click((key, cx) => ...)` or "
                "`on_item_secondary_click((key, event, cx) => ...)` on the "
                "list itself, and read the row out of your own data with the "
                "stable key it gives you");
        }
        JSValue handler =
            argCount > 0 ? JS_GetPropertyUint32(ctx, argv[2], 0) : JS_UNDEFINED;
        // A sentinel in the handler position means this call is being recorded
        // into a template rather than into a description: the handler is
        // minted per call, so the op holds a placeholder and the position is
        // noted. Checked before the function test, which would reject it.
        if (SlotIndex(ctx, handler, &slotArgument)) {
            JS_FreeValue(ctx, handler);
            if (!RequireDiscovery(ctx, impl)) {
                ArenaDelete(arena);
                return JS_EXCEPTION;
            }
            op.kind = shell::SpecOpKind::Callback;
            op.callback = kTemplateUnfilled;
            slotPosition = 0;
            slotSite = shell::SlotSiteKind::Handler;
        } else {
            if (!JS_IsFunction(ctx, handler)) {
                JS_FreeValue(ctx, handler);
                ArenaDelete(arena);
                return JS_ThrowTypeError(
                    ctx, "%.*s(handler) expects a function", name.len, name.s);
            }
            shell::CallbackId callback = impl->callbacks.Push(
                ctx, handler, shell::ScopeCurrentView(),
                shell::ScopeCurrentPolicy(), shell::ScopeCurrentGeneration(),
                (AppModule*)shell::ScopeCurrentApplication());
            JS_FreeValue(ctx, handler);
            if (callback == UINT64_MAX) {
                ArenaDelete(arena);
                return JS_ThrowInternalError(
                    ctx, "a callback was registered outside a snapshot build");
            }
            op.kind = shell::SpecOpKind::Callback;
            op.callback = callback;
        }
    } else {
        op.kind = IsParamStyle(name) ? shell::SpecOpKind::ParamStyle
                                     : (!IsBehavior(name) && argCount == 0
                                            ? shell::SpecOpKind::NullaryStyle
                                            : shell::SpecOpKind::Method);
        op.argCount = argCount;
        if (argCount > 0) {
            op.args = (shell::Bridged*)Alloc(
                arena, (int)(sizeof(shell::Bridged) * (size_t)argCount));
            for (int i = 0; i < argCount; i++) {
                JSValue value = JS_GetPropertyUint32(ctx, argv[2], (uint32_t)i);
                if (JS_IsException(value)) {
                    ArenaDelete(arena);
                    return JS_EXCEPTION;
                }
                uint16_t argument = 0;
                if (SlotIndex(ctx, value, &argument)) {
                    JS_FreeValue(ctx, value);
                    // The value is not known yet. A placeholder is recorded
                    // and the position is noted; the call fills it. A style is
                    // the only argument position a template fills, and one per
                    // call: everything else is refused where it was written,
                    // naming what to do instead, rather than baked in as a
                    // constant that would never change again.
                    if (op.kind != shell::SpecOpKind::ParamStyle ||
                        slotPosition >= 0 || i > 255) {
                        JSValue thrown = JS_ThrowTypeError(
                            ctx,
                            "`%.*s` cannot take a template argument: a "
                            "template fills text children, style arguments "
                            "and handlers. Compute the value where the "
                            "template is called and pass the result",
                            name.len, name.s);
                        ArenaDelete(arena);
                        return thrown;
                    }
                    if (!RequireDiscovery(ctx, impl)) {
                        ArenaDelete(arena);
                        return JS_EXCEPTION;
                    }
                    op.args[i] = shell::Bridged::Nil();
                    slotPosition = i;
                    slotArgument = argument;
                    slotSite = shell::SlotSiteKind::Argument;
                    continue;
                }
                if (!BridgeValue(ctx, value, arena, &op.args[i])) {
                    JS_FreeValue(ctx, value);
                    ArenaDelete(arena);
                    return JS_EXCEPTION;
                }
                JS_FreeValue(ctx, value);
            }
        }
        if (IsDockCommand(name)) {
            // Every command takes the dock handle first, because it is
            // resolved against *that* area: the script passes the container
            // object it was handed and the prelude unpacks the handle out of
            // it. What follows names the container inside the area.
            if (argCount < 1 || op.args[0].kind != shell::BridgedKind::Number ||
                op.args[0].number < 0) {
                JSValue thrown = JS_ThrowTypeError(
                    ctx,
                    "%.*s(...) expects the group, dock or tile its chrome "
                    "handler was given as its first argument",
                    name.len, name.s);
                ArenaDelete(arena);
                return thrown;
            }
        }
        if (StrEq(name, StrL("role"))) {
            if (argCount != 1 || op.args[0]
                                         .kind != shell::BridgedKind::String) {
                ArenaDelete(arena);
                return JS_ThrowTypeError(
                    ctx, "role(name) expects one snake_case role string");
            }
            Str roleName = op.args[0].string;
            if (StrEq(roleName, StrL("generic_container"))) {
                ArenaDelete(arena);
                return JS_ThrowRangeError(
                    ctx,
                    "role(\"generic_container\") announces nothing because "
                    "GPUI filters it from the accessibility tree");
            }
            if (shell::AccessibilityRoleFromName(roleName) ==
                AccessibilityRole::None) {
                JSValue thrown =
                    JS_ThrowRangeError(ctx, "unknown accessibility role `%.*s`",
                                       roleName.len, roleName.s);
                ArenaDelete(arena);
                return thrown;
            }
        }
    }
    shell::SpecError failure = {};
    if (!impl->scratch->PushOp(id, op, &failure)) {
        ArenaDelete(arena);
        return SpecFailure(ctx, failure);
    }
    if (slotPosition >= 0) {
        shell::Slot slot = {};
        slot.node = id;
        slot.site.kind = slotSite;
        slot.site.argument = (uint8_t)slotPosition;
        slot.argument = slotArgument;
        if (!LastOpIndex(ctx, impl, id, &slot.site.op) ||
            !NoteSlot(ctx, impl, slot)) {
            ArenaDelete(arena);
            return JS_EXCEPTION;
        }
    }
    ArenaDelete(arena);
    return JS_UNDEFINED;
}

static bool JsHandle(JSContext* ctx, JSValueConst value,
                     shell::EntityHandle* out) {
    uint64_t handle = 0;
    if (JS_ToIndex(ctx, &handle, value) < 0) return false;
    *out = handle;
    return true;
}

static shell::RetainedEntry* LiveRetained(JSContext* ctx,
                                          shell::EntityHandle handle,
                                          shell::RetainedKind kind,
                                          const char* what) {
    ShellRuntimeImpl* impl = (ShellRuntimeImpl*)JS_GetContextOpaque(ctx);
    shell::RetainedEntry* entry = impl ? impl->retained.Find(handle) : nullptr;
    if (!entry || entry->kind != kind) {
        JS_ThrowTypeError(ctx, "this %s state has been released", what);
        return nullptr;
    }
    return entry;
}

static bool RefuseRetainedCreation(JSContext* ctx, const char* what) {
    if (shell::ScopeHasCurrent() &&
        (shell::ScopeCurrentPhase() == ScopePhase::Render ||
         shell::ScopeCurrentPhase() == ScopePhase::Layout)) {
        JS_ThrowTypeError(ctx,
                          "%s cannot run during render; create retained state "
                          "in init() or an event handler",
                          what);
        return true;
    }
    ShellRuntimeImpl* impl = (ShellRuntimeImpl*)JS_GetContextOpaque(ctx);
    if (impl && impl->retained.Len() + impl->nestedViews.len >=
                    shell::kMaxLiveEntities) {
        JS_ThrowRangeError(
            ctx,
            "the application reached gpui-shell's retained entity limit; "
            "release unused handles");
        return true;
    }
    return false;
}

static bool RefuseRetainedMutation(JSContext* ctx, const char* what) {
    if (shell::ScopeHasCurrent() &&
        (shell::ScopeCurrentPhase() == ScopePhase::Render ||
         shell::ScopeCurrentPhase() == ScopePhase::Layout)) {
        JS_ThrowTypeError(ctx,
                          "%s cannot run during render or layout; mutate "
                          "retained state from an event handler or task",
                          what);
        return true;
    }
    return false;
}

static JSValue ThrowNestedError(JSContext* ctx, ShellError* error,
                                const char* fallback) {
    JSValue result =
        error && error->IsSet()
            ? JS_ThrowInternalError(ctx, "%.*s", error->message.len,
                                    error->message.s)
            : JS_ThrowInternalError(ctx, "%s", fallback);
    ShellErrorClear(error);
    return result;
}

static JSValue NativeViewNew(JSContext* ctx, JSValueConst, int argc,
                             JSValueConst* argv) {
    // Rust defers this operation until its RefCell/GPUI context borrows have
    // ended. The C API holds neither across a native call, so construction can
    // enter QuickJS directly and put init-created state under the final child
    // owner. One observable difference is intentional: a construction or
    // update failure throws at cx.new/set_props and is catchable there.
    if (RefuseRetainedCreation(ctx, "cx.new(Class, props)")) {
        return JS_EXCEPTION;
    }
    ShellRuntimeImpl* impl = (ShellRuntimeImpl*)JS_GetContextOpaque(ctx);
    if (!impl || argc < 1 || !JS_IsFunction(ctx, argv[0])) {
        return JS_ThrowTypeError(
            ctx, "cx.new(Class, props) expects a View subclass");
    }
    EntityId owner = shell::ScopeCurrentView();
    if (!owner.IsValid() || shell::ScopeCurrentRuntime() != impl->owner) {
        return JS_ThrowTypeError(
            ctx,
            "cx.new(Class, props) needs a current script view; call it from "
            "a view's init(), event handler or task");
    }
    Window* window = nullptr;
    App* app = nullptr;
    {
        shell::ScopeHostContext host = shell::ScopeCurrentHost();
        if (!host.IsSet()) {
            return JS_ThrowTypeError(
                ctx, "cx.new(Class, props) needs a live Window/App context");
        }
        window = host.GetWindow();
        app = host.GetApp();
    }
    uint32_t token = impl->nextNestedView++;
    if (token == 0 || impl->nextNestedView == 0) {
        return JS_ThrowRangeError(ctx,
                                  "the nested Entity token space is exhausted");
    }

    ViewType* type = new ViewType();
    type->runtime = impl->owner->Retain();
    type->value = JS_DupValue(ctx, argv[0]);
    type->application = (AppModule*)shell::ScopeCurrentApplication();
    Policy* policy = shell::ScopeCurrentPolicy();
    Entity<ScriptView> entity = ScriptView::New(app, impl->owner, type, policy);
    ScriptView* view = entity.Get(app);
    if (!view) {
        ViewTypeRelease(type);
        return JS_ThrowInternalError(
            ctx, "could not allocate the nested script view");
    }
    ShellError error = {};
    JSValueConst props = argc > 1 ? argv[1] : JS_UNDEFINED;
    ViewObject* object = InstantiateObject(impl->owner, type, window, app,
                                           policy, props, &error, entity.id);
    if (!object) {
        EntityDrop(app, entity.id);
        ViewTypeRelease(type);
        return ThrowNestedError(ctx, &error,
                                "could not initialize the nested script view");
    }
    view = entity.Get(app);
    if (!view) {
        ViewObjectRelease(object);
        ViewTypeRelease(type);
        return JS_ThrowInternalError(
            ctx, "the nested script view was released during initialization");
    }
    view->object = object;
    NestedViewEntry nested = {};
    nested.token = token;
    nested.view = entity.id;
    nested.owner = owner;
    nested.policy = PolicyRetain(policy);
    nested.application = type->application;
    nested.app = app;
    VecAppend(impl->nestedViews, nested);
    ViewTypeRelease(type);
    return JS_NewUint32(ctx, token);
}

static JSValue NativeViewSetProps(JSContext* ctx, JSValueConst, int argc,
                                  JSValueConst* argv) {
    if (RefuseRetainedMutation(ctx, "entity.set_props(props)")) {
        return JS_EXCEPTION;
    }
    uint32_t token = 0;
    if (argc < 1 || JS_ToUint32(ctx, &token, argv[0]) < 0) {
        return JS_EXCEPTION;
    }
    ShellRuntimeImpl* impl = (ShellRuntimeImpl*)JS_GetContextOpaque(ctx);
    NestedViewEntry* nested = FindNestedView(impl, token);
    if (!NestedViewIsCurrent(nested)) {
        return JS_ThrowTypeError(
            ctx, "this Entity has been released and can no longer be updated");
    }
    NestedViewEntry current = *nested;
    Window* window = nullptr;
    App* app = nullptr;
    {
        shell::ScopeHostContext host = shell::ScopeCurrentHost();
        if (!host.IsSet()) {
            return JS_ThrowTypeError(
                ctx, "entity.set_props(props) needs a live Window/App context");
        }
        window = host.GetWindow();
        app = host.GetApp();
    }
    ScriptView* view = (ScriptView*)EntityGet(app, current.view);
    if (!view || !view->object) {
        return JS_ThrowTypeError(
            ctx, "this Entity has been released and can no longer be updated");
    }

    shell::CallScopeGuard scope =
        shell::ScopeEnter(window, app, ScopePhase::Event, current.view,
                          current.policy, impl->owner, current.application);
    uint32_t retainedCheckpoint = impl->retained.Checkpoint();
    uint32_t nestedCheckpoint = impl->nextNestedView;
    int taskCheckpoint = impl->tasks.len;
    BeginExecution(impl);
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue checkpoint = JS_GetPropertyStr(ctx, global, "__checkpoint_view");
    JSValue restore =
        JS_Call(ctx, checkpoint, JS_UNDEFINED, 1, &view->object->value);
    JS_FreeValue(ctx, checkpoint);
    JS_FreeValue(ctx, global);
    if (JS_IsException(restore)) {
        ShellError error = {};
        CaptureException(impl, &error);
        return ThrowNestedError(ctx, &error,
                                "could not checkpoint the nested script view");
    }

    JSValue update = JS_GetPropertyStr(ctx, view->object->value, "update");
    JSValue result = JS_UNDEFINED;
    if (JS_IsException(update)) {
        result = JS_EXCEPTION;
    } else if (!JS_IsUndefined(update) && !JS_IsNull(update)) {
        if (!JS_IsFunction(ctx, update)) {
            JS_ThrowTypeError(
                ctx, "a nested view's update property must be a function");
            result = JS_EXCEPTION;
        } else {
            JSValueConst props = argc > 1 ? argv[1] : JS_UNDEFINED;
            result = JS_Call(ctx, update, view->object->value, 1, &props);
        }
    }
    JS_FreeValue(ctx, update);
    if (JS_IsException(result)) {
        ShellError updateError = {};
        CaptureException(impl, &updateError);
        BeginExecution(impl);
        JSValue restored = JS_Call(ctx, restore, JS_UNDEFINED, 0, nullptr);
        ShellError restoreError = {};
        if (JS_IsException(restored))
            CaptureException(impl, &restoreError);
        else
            JS_FreeValue(ctx, restored);
        while (impl->tasks.len > taskCheckpoint) {
            ForgetTask(impl, impl->tasks[impl->tasks.len - 1]->id);
        }
        Vec<shell::CallbackId> retired;
        impl->retained.Rollback(retainedCheckpoint, &retired);
        for (int i = 0; i < retired.len; i++) {
            impl->callbacks.RetireId(ctx, retired[i]);
        }
        VecReset(retired);
        RollbackNestedViews(impl, nestedCheckpoint);
        JS_FreeValue(ctx, restore);
        if (restoreError.IsSet()) {
            Str message =
                StrDup(fmt("%s; failed to restore child state: %s",
                           updateError.message, restoreError.message));
            ShellErrorClear(&updateError);
            ShellErrorClear(&restoreError);
            JSValue thrown = JS_ThrowInternalError(ctx, "%s", message.s);
            StrFree(message);
            return thrown;
        }
        return ThrowNestedError(ctx, &updateError,
                                "the nested script view update failed");
    }
    JS_FreeValue(ctx, result);
    JS_FreeValue(ctx, restore);
    view->dirty = true;
    NotifyEntity(app, current.view, window);
    return JS_UNDEFINED;
}

static JSValue NativeViewRelease(JSContext* ctx, JSValueConst, int argc,
                                 JSValueConst* argv) {
    if (RefuseRetainedMutation(ctx, "entity.release()")) return JS_EXCEPTION;
    uint32_t token = 0;
    if (argc < 1 || JS_ToUint32(ctx, &token, argv[0]) < 0) {
        return JS_EXCEPTION;
    }
    ShellRuntimeImpl* impl = (ShellRuntimeImpl*)JS_GetContextOpaque(ctx);
    int at = -1;
    NestedViewEntry* nested = FindNestedView(impl, token, &at);
    if (!NestedViewIsCurrent(nested)) {
        return JS_ThrowTypeError(
            ctx, "this Entity has been released and can no longer be released");
    }
    App* app = nested->app;
    Window* window = nullptr;
    {
        shell::ScopeHostContext host = shell::ScopeCurrentHost();
        if (host.IsSet()) window = host.GetWindow();
    }
    EntityId owner = nested->owner;
    bool live = app && EntityGet(app, nested->view);
    DropNestedViewAt(impl, at);
    if (owner.IsValid() && app) NotifyEntity(app, owner, window);
    return JS_NewBool(ctx, live);
}

static JSValue NativeChildView(JSContext* ctx, JSValueConst, int argc,
                               JSValueConst* argv) {
    uint32_t token = 0;
    if (argc < 1 || JS_ToUint32(ctx, &token, argv[0]) < 0) {
        return JS_EXCEPTION;
    }
    ShellRuntimeImpl* impl = (ShellRuntimeImpl*)JS_GetContextOpaque(ctx);
    NestedViewEntry* nested = FindNestedView(impl, token);
    if (!NestedViewIsCurrent(nested) || !nested->app ||
        !EntityGet(nested->app, nested->view)) {
        return JS_ThrowTypeError(
            ctx, "this Entity has been released and can no longer be mounted");
    }
    shell::Component component = {};
    component.kind = shell::ComponentKind::ChildView;
    component.handle = token;
    shell::SpecId id = 0;
    shell::SpecError failure = {};
    if (!impl->scratch->PushChildView(component, &id, &failure)) {
        return SpecFailure(ctx, failure);
    }
    return JS_NewUint32(ctx, id);
}

static bool OverlayMutationAllowed(JSContext* ctx, const char* api) {
    if (shell::ScopeHasCurrent() &&
        ScopePhaseAllowsNotify(shell::ScopeCurrentPhase()))
        return true;
    JS_ThrowTypeError(
        ctx,
        "%s is not allowed during the `%s` phase; overlays may only be "
        "opened or closed while handling an event or a task",
        api,
        shell::ScopeHasCurrent() ? ScopePhaseName(shell::ScopeCurrentPhase())
                                 : "none");
    return false;
}

static bool OverlayHost(JSContext* ctx, const char* api,
                        ShellRuntimeImpl** outImpl, Ctx* out) {
    ShellRuntimeImpl* impl = (ShellRuntimeImpl*)JS_GetContextOpaque(ctx);
    shell::ScopeHostContext host = shell::ScopeCurrentHost();
    if (!impl || !host.IsSet()) {
        JS_ThrowTypeError(
            ctx,
            "%s needs a live host call; call it from init(), an event handler "
            "or a task",
            api);
        return false;
    }
    if (!ShellRootOf(host.GetWindow(), host.GetApp())) {
        JS_ThrowTypeError(
            ctx,
            "%s needs a ShellRoot as the window's first view; this window "
            "was opened with another view",
            api);
        return false;
    }
    *outImpl = impl;
    out->app = host.GetApp();
    out->win = host.GetWindow();
    out->a = host.GetWindow()->frameArena;
    out->self = shell::ScopeCurrentView();
    return true;
}

static bool KnownOptions(JSContext* ctx, JSValueConst object,
                         const char* const* known, int knownCount,
                         const char* api) {
    JSPropertyEnum* properties = nullptr;
    uint32_t count = 0;
    if (JS_GetOwnPropertyNames(ctx, &properties, &count, object,
                               JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) < 0)
        return false;
    bool ok = true;
    for (uint32_t i = 0; i < count && ok; i++) {
        const char* name = JS_AtomToCString(ctx, properties[i].atom);
        Str optionName = name ? Str(name) : Str{};
        bool found = false;
        for (int j = 0; name && j < knownCount; j++)
            if (StrEq(optionName, known[j])) found = true;
        if (!found) {
            JS_ThrowTypeError(ctx, "unknown option `%s` for %s",
                              name ? name : "<symbol>", api);
            ok = false;
        }
        if (name) JS_FreeCString(ctx, name);
    }
    JS_FreePropertyEnum(ctx, properties, count);
    return ok;
}

static bool OptionalBoolProperty(JSContext* ctx, JSValueConst object,
                                 const char* name, bool* value) {
    JSValue property = JS_GetPropertyStr(ctx, object, name);
    if (JS_IsException(property)) return false;
    if (JS_IsUndefined(property) || JS_IsNull(property)) {
        JS_FreeValue(ctx, property);
        return true;
    }
    if (!JS_IsBool(property)) {
        JS_FreeValue(ctx, property);
        JS_ThrowTypeError(ctx, "%s must be boolean", name);
        return false;
    }
    *value = JS_ToBool(ctx, property) != 0;
    JS_FreeValue(ctx, property);
    return true;
}

static bool DialogOptionsFromJs(JSContext* ctx, JSValueConst value,
                                DialogOptions* out) {
    if (JS_IsUndefined(value) || JS_IsNull(value)) return true;
    if (!JS_IsObject(value)) {
        JS_ThrowTypeError(ctx,
                          "window.open_dialog(content, options) expects an "
                          "options object");
        return false;
    }
    static const char* keys[] = {"escape_dismissable", "backdrop_dismissable"};
    return KnownOptions(ctx, value, keys, 2,
                        "window.open_dialog(content, options)") &&
           OptionalBoolProperty(ctx, value, keys[0], &out->escapeDismissable) &&
           OptionalBoolProperty(ctx, value, keys[1], &out->backdropDismissable);
}

static JSValue NativeOpenDialog(JSContext* ctx, JSValueConst, int argc,
                                JSValueConst* argv) {
    const char* api = "window.open_dialog(content, options)";
    if (!OverlayMutationAllowed(ctx, api)) return JS_EXCEPTION;
    uint32_t token = 0;
    DialogOptions options;
    if (argc < 1 || JS_ToUint32(ctx, &token, argv[0]) < 0 ||
        !DialogOptionsFromJs(ctx, argc > 1 ? argv[1] : JS_UNDEFINED, &options))
        return JS_EXCEPTION;
    ShellRuntimeImpl* impl = nullptr;
    Ctx native = {};
    if (!OverlayHost(ctx, api, &impl, &native)) return JS_EXCEPTION;
    Entity<ScriptView> content;
    if (!TakeNestedView(impl, token, &content))
        return JS_ThrowTypeError(ctx,
                                 "dialog content has already been released");
    int depth = ShellRootOpenDialog(&native, content, options);
    if (depth <= 0) {
        EntityDrop(native.app, content.id);
        return JS_ThrowInternalError(ctx, "could not mount dialog content");
    }
    return JS_NewInt32(ctx, depth);
}

static JSValue NativeCloseDialog(JSContext* ctx, JSValueConst, int,
                                 JSValueConst*) {
    const char* api = "window.close_dialog()";
    if (!OverlayMutationAllowed(ctx, api)) return JS_EXCEPTION;
    ShellRuntimeImpl* impl = nullptr;
    Ctx native = {};
    if (!OverlayHost(ctx, api, &impl, &native)) return JS_EXCEPTION;
    return JS_NewBool(ctx, ShellRootCloseDialog(&native));
}

static JSValue NativeCloseAllDialogs(JSContext* ctx, JSValueConst, int,
                                     JSValueConst*) {
    const char* api = "window.close_all_dialogs()";
    if (!OverlayMutationAllowed(ctx, api)) return JS_EXCEPTION;
    ShellRuntimeImpl* impl = nullptr;
    Ctx native = {};
    if (!OverlayHost(ctx, api, &impl, &native)) return JS_EXCEPTION;
    return JS_NewInt32(ctx, ShellRootCloseAllDialogs(&native));
}

static JSValue NativeHasDialog(JSContext* ctx, JSValueConst, int,
                               JSValueConst*) {
    ShellRuntimeImpl* impl = nullptr;
    Ctx native = {};
    if (!OverlayHost(ctx, "window.has_active_dialog()", &impl, &native))
        return JS_EXCEPTION;
    return JS_NewBool(ctx, ShellRootHasDialog(&native));
}

static bool SheetPlacementFromJs(JSContext* ctx, JSValueConst value,
                                 component::SheetPlacement* out) {
    if (JS_IsUndefined(value) || JS_IsNull(value)) {
        *out = component::SheetPlacement::Right;
        return true;
    }
    Arena* arena = ArenaNew();
    Str name;
    bool ok = JsString(ctx, value, arena, &name);
    if (ok) {
        if (StrEq(name, StrL("left")))
            *out = component::SheetPlacement::Left;
        else if (StrEq(name, StrL("right")))
            *out = component::SheetPlacement::Right;
        else if (StrEq(name, StrL("top")))
            *out = component::SheetPlacement::Top;
        else if (StrEq(name, StrL("bottom")))
            *out = component::SheetPlacement::Bottom;
        else {
            JS_ThrowTypeError(ctx,
                              "unknown sheet placement `%.*s`; expected left, "
                              "right, top or bottom",
                              name.len, name.s);
            ok = false;
        }
    }
    ArenaDelete(arena);
    return ok;
}

static JSValue NativeOpenSheet(JSContext* ctx, JSValueConst, int argc,
                               JSValueConst* argv) {
    const char* api = JS_IsUndefined(argc > 0 ? argv[0] : JS_UNDEFINED)
                          ? "window.open_sheet(content)"
                          : "window.open_sheet_at(placement, content)";
    if (!OverlayMutationAllowed(ctx, api)) return JS_EXCEPTION;
    component::SheetPlacement placement;
    uint32_t token = 0;
    if (!SheetPlacementFromJs(ctx, argc > 0 ? argv[0] : JS_UNDEFINED,
                              &placement) ||
        argc < 2 || JS_ToUint32(ctx, &token, argv[1]) < 0)
        return JS_EXCEPTION;
    ShellRuntimeImpl* impl = nullptr;
    Ctx native = {};
    if (!OverlayHost(ctx, api, &impl, &native)) return JS_EXCEPTION;
    Entity<ScriptView> content;
    if (!TakeNestedView(impl, token, &content))
        return JS_ThrowTypeError(ctx,
                                 "sheet content has already been released");
    if (!ShellRootOpenSheet(&native, content, placement)) {
        EntityDrop(native.app, content.id);
        return JS_ThrowInternalError(ctx, "could not mount sheet content");
    }
    return JS_UNDEFINED;
}

static JSValue NativeCloseSheet(JSContext* ctx, JSValueConst, int,
                                JSValueConst*) {
    const char* api = "window.close_sheet()";
    if (!OverlayMutationAllowed(ctx, api)) return JS_EXCEPTION;
    ShellRuntimeImpl* impl = nullptr;
    Ctx native = {};
    if (!OverlayHost(ctx, api, &impl, &native)) return JS_EXCEPTION;
    return JS_NewBool(ctx, ShellRootCloseSheet(&native));
}

static JSValue NativeHasSheet(JSContext* ctx, JSValueConst, int,
                              JSValueConst*) {
    ShellRuntimeImpl* impl = nullptr;
    Ctx native = {};
    if (!OverlayHost(ctx, "window.has_active_sheet()", &impl, &native))
        return JS_EXCEPTION;
    return JS_NewBool(ctx, ShellRootHasSheet(&native));
}

static bool OptionalStringProperty(JSContext* ctx, JSValueConst object,
                                   const char* name, Arena* arena, Str* out,
                                   bool* present);

// The performance HUD is the root's, not the script's: the script says whether
// and where, the host draws it above everything and keeps its history across a
// hide and a show. Nothing crosses back but a flag.
static JSValue NativeShowFpsMonitor(JSContext* ctx, JSValueConst, int argc,
                                    JSValueConst* argv) {
    const char* kApi = "show_fps_monitor(options)";
    if (!OverlayMutationAllowed(ctx, kApi)) return JS_EXCEPTION;
    ShellRuntimeImpl* impl = nullptr;
    Ctx native = {};
    if (!OverlayHost(ctx, kApi, &impl, &native)) return JS_EXCEPTION;
    FpsHudRequest request = {};
    if (argc > 0 && !JS_IsUndefined(argv[0]) && !JS_IsNull(argv[0])) {
        if (!JS_IsObject(argv[0]) || JS_IsArray(argv[0])) {
            return JS_ThrowTypeError(
                ctx,
                "%s expects an object, such as { anchor: \"bottom_left\" }",
                kApi);
        }
        static const char* const kKeys[] = {"anchor", "continuous",
                                            "frame_budget"};
        if (!KnownOptions(ctx, argv[0], kKeys, 3, kApi)) return JS_EXCEPTION;
        Arena* arena = ArenaNew();
        Str anchor;
        bool present = false;
        bool ok = OptionalStringProperty(ctx, argv[0], "anchor", arena, &anchor,
                                         &present);
        if (ok && present && !FpsAnchorFromName(anchor, &request.anchor)) {
            StrBuilder names;
            bool first = true;
            SeqStrings all = FpsAnchorNames();
            for (Str name = SeqStrFirst(all); name.len > 0;
                 name = SeqStrNext(name)) {
                if (!first) names.Append(StrL(", "));
                first = false;
                names.Append(name);
            }
            Str list = names.TakeStr();
            JSValue thrown = JS_ThrowTypeError(
                ctx, "%s: unknown anchor `%.*s`; expected one of %.*s", kApi,
                anchor.len, anchor.s, list.len, list.s);
            StrFree(list);
            ArenaDelete(arena);
            return thrown;
        }
        ArenaDelete(arena);
        if (!ok) return JS_EXCEPTION;
        if (!OptionalBoolProperty(ctx, argv[0], "continuous",
                                  &request.continuous)) {
            return JS_EXCEPTION;
        }
        JSValue budget = JS_GetPropertyStr(ctx, argv[0], "frame_budget");
        if (JS_IsException(budget)) return JS_EXCEPTION;
        if (!JS_IsUndefined(budget) && !JS_IsNull(budget)) {
            double millis = 0;
            bool valid = JS_IsNumber(budget) &&
                         JS_ToFloat64(ctx, &millis, budget) == 0 &&
                         isfinite(millis) && millis > 0;
            JS_FreeValue(ctx, budget);
            if (!valid) {
                return JS_ThrowTypeError(ctx,
                                         "%s: frame_budget must be a positive "
                                         "number of milliseconds",
                                         kApi);
            }
            request.hasFrameBudget = true;
            request.frameBudget = (float)(millis / 1000.0);
        } else {
            JS_FreeValue(ctx, budget);
        }
    }
    ShellRootShowFpsMonitor(&native, request);
    return JS_UNDEFINED;
}

static JSValue NativeHideFpsMonitor(JSContext* ctx, JSValueConst, int,
                                    JSValueConst*) {
    const char* kApi = "hide_fps_monitor()";
    if (!OverlayMutationAllowed(ctx, kApi)) return JS_EXCEPTION;
    ShellRuntimeImpl* impl = nullptr;
    Ctx native = {};
    if (!OverlayHost(ctx, kApi, &impl, &native)) return JS_EXCEPTION;
    return JS_NewBool(ctx, ShellRootHideFpsMonitor(&native));
}

static JSValue NativeFpsMonitorVisible(JSContext* ctx, JSValueConst, int,
                                       JSValueConst*) {
    ShellRuntimeImpl* impl = nullptr;
    Ctx native = {};
    if (!OverlayHost(ctx, "fps_monitor_visible()", &impl, &native))
        return JS_EXCEPTION;
    return JS_NewBool(ctx, ShellRootFpsMonitorVisible(&native));
}

static bool OptionalStringProperty(JSContext* ctx, JSValueConst object,
                                   const char* name, Arena* arena, Str* out,
                                   bool* present) {
    JSValue property = JS_GetPropertyStr(ctx, object, name);
    if (JS_IsException(property)) return false;
    *present = !JS_IsUndefined(property) && !JS_IsNull(property);
    bool ok = !*present || JsString(ctx, property, arena, out);
    JS_FreeValue(ctx, property);
    return ok;
}

static JSValue NativePushToast(JSContext* ctx, JSValueConst, int argc,
                               JSValueConst* argv) {
    const char* api = "window.push_toast(options)";
    if (!OverlayMutationAllowed(ctx, api)) return JS_EXCEPTION;
    if (argc < 1 || !JS_IsObject(argv[0]))
        return JS_ThrowTypeError(
            ctx, "%s expects an object, such as { title: \"Saved\" }", api);
    static const char* keys[] = {"title", "description", "level", "timeout",
                                 "id"};
    if (!KnownOptions(ctx, argv[0], keys, 5, api)) return JS_EXCEPTION;
    Arena* arena = ArenaNew();
    ToastRequest toast;
    bool title = false, description = false, level = false;
    bool ok = OptionalStringProperty(ctx, argv[0], "title", arena, &toast.title,
                                     &title) &&
              OptionalStringProperty(ctx, argv[0], "description", arena,
                                     &toast.description, &description);
    if (ok && !title) {
        JS_ThrowTypeError(
            ctx, "%s requires a `title`; it is the sentence the user reads",
            api);
        ok = false;
    }
    Str levelName;
    if (ok)
        ok = OptionalStringProperty(ctx, argv[0], "level", arena, &levelName,
                                    &level);
    if (ok && level && !ToastLevelFromName(levelName, &toast.level)) {
        JS_ThrowTypeError(ctx,
                          "unknown toast level `%.*s`; expected info, success, "
                          "warning or error",
                          levelName.len, levelName.s);
        ok = false;
    }
    bool id = false;
    if (ok)
        ok = OptionalStringProperty(ctx, argv[0], "id", arena, &toast.id, &id);
    toast.hasId = id;
    if (ok) {
        JSValue timeout = JS_GetPropertyStr(ctx, argv[0], "timeout");
        if (JS_IsException(timeout))
            ok = false;
        else if (JS_IsNull(timeout))
            toast.timeoutMs = 0;
        else if (!JS_IsUndefined(timeout)) {
            double value = 0;
            if (JS_ToFloat64(ctx, &value, timeout) < 0 || !isfinite(value) ||
                value < 0 || value > INT32_MAX) {
                JS_ThrowTypeError(
                    ctx,
                    "%s expects `timeout` to be a number of milliseconds, or "
                    "null to keep the toast until it is dismissed",
                    api);
                ok = false;
            } else {
                toast.timeoutMs = (int)value;
            }
        }
        JS_FreeValue(ctx, timeout);
    }
    ShellRuntimeImpl* impl = nullptr;
    Ctx native = {};
    if (ok) ok = OverlayHost(ctx, api, &impl, &native);
    bool pushed = ok && ShellRootPushToast(&native, toast);
    ArenaDelete(arena);
    if (!ok) return JS_EXCEPTION;
    if (!pushed) return JS_ThrowInternalError(ctx, "could not mount toast");
    return JS_UNDEFINED;
}

static JSValue NativeRemoveToast(JSContext* ctx, JSValueConst, int argc,
                                 JSValueConst* argv) {
    const char* api = "window.remove_toast(id)";
    if (!OverlayMutationAllowed(ctx, api)) return JS_EXCEPTION;
    Arena* arena = ArenaNew();
    Str id;
    bool ok = argc >= 1 && JsString(ctx, argv[0], arena, &id);
    ShellRuntimeImpl* impl = nullptr;
    Ctx native = {};
    if (ok) ok = OverlayHost(ctx, api, &impl, &native);
    bool removed = ok && ShellRootRemoveToast(&native, id);
    ArenaDelete(arena);
    return ok ? JS_NewBool(ctx, removed) : JS_EXCEPTION;
}

static JSValue NativeClearToasts(JSContext* ctx, JSValueConst, int,
                                 JSValueConst*) {
    const char* api = "window.clear_toasts()";
    if (!OverlayMutationAllowed(ctx, api)) return JS_EXCEPTION;
    ShellRuntimeImpl* impl = nullptr;
    Ctx native = {};
    if (!OverlayHost(ctx, api, &impl, &native)) return JS_EXCEPTION;
    ShellRootClearToasts(&native);
    return JS_UNDEFINED;
}

static bool OptionalJsString(JSContext* ctx, JSValueConst value, Arena* arena,
                             Str* out) {
    *out = {};
    return JS_IsUndefined(value) || JS_IsNull(value) ||
           JsString(ctx, value, arena, out);
}

static JSValue NativeInputStateNew(JSContext* ctx, JSValueConst, int argc,
                                   JSValueConst* argv) {
    if (RefuseRetainedCreation(ctx, "InputState.new(...)")) return JS_EXCEPTION;
    ShellRuntimeImpl* impl = (ShellRuntimeImpl*)JS_GetContextOpaque(ctx);
    if (!impl || impl->retained.Len() >= shell::kMaxLiveEntities) {
        return JS_ThrowRangeError(
            ctx,
            "the application reached gpui-shell's retained entity limit; "
            "release unused handles");
    }
    shell::ScopeHostContext host = shell::ScopeCurrentHost();
    if (!host.IsSet())
        return JS_ThrowTypeError(ctx,
                                 "InputState.new(...) needs a live host call");
    Arena* arena = ArenaNew();
    Str placeholder, value;
    bool ok =
        OptionalJsString(ctx, argc > 0 ? argv[0] : JS_UNDEFINED, arena,
                         &placeholder) &&
        OptionalJsString(ctx, argc > 1 ? argv[1] : JS_UNDEFINED, arena, &value);
    shell::EntityHandle handle =
        ok ? impl->retained.CreateInput(
                 false, placeholder, value, 0, host.GetApp(),
                 shell::ScopeCurrentView(), shell::ScopeCurrentApplication())
           : 0;
    ArenaDelete(arena);
    if (!ok) return JS_EXCEPTION;
    if (!handle)
        return JS_ThrowInternalError(ctx, "creating input state failed");
    return JS_NewInt64(ctx, (int64_t)handle);
}

static JSValue NativeTextareaStateNew(JSContext* ctx, JSValueConst, int argc,
                                      JSValueConst* argv) {
    if (RefuseRetainedCreation(ctx, "TextareaState.new(...)"))
        return JS_EXCEPTION;
    ShellRuntimeImpl* impl = (ShellRuntimeImpl*)JS_GetContextOpaque(ctx);
    if (!impl || impl->retained.Len() >= shell::kMaxLiveEntities) {
        return JS_ThrowRangeError(
            ctx,
            "the application reached gpui-shell's retained entity limit; "
            "release unused handles");
    }
    shell::ScopeHostContext host = shell::ScopeCurrentHost();
    if (!host.IsSet())
        return JS_ThrowTypeError(
            ctx, "TextareaState.new(...) needs a live host call");
    Arena* arena = ArenaNew();
    Str placeholder, value;
    int32_t rows = 0;
    bool ok =
        OptionalJsString(ctx, argc > 0 ? argv[0] : JS_UNDEFINED, arena,
                         &placeholder) &&
        OptionalJsString(ctx, argc > 1 ? argv[1] : JS_UNDEFINED, arena, &value);
    if (ok && argc > 2 && !JS_IsNull(argv[2]) && !JS_IsUndefined(argv[2])) {
        ok = JS_ToInt32(ctx, &rows, argv[2]) == 0 && rows > 0;
        if (!ok)
            JS_ThrowTypeError(
                ctx, "TextareaState.new rows must be a positive whole number");
    }
    shell::EntityHandle handle =
        ok ? impl->retained.CreateInput(
                 true, placeholder, value, rows, host.GetApp(),
                 shell::ScopeCurrentView(), shell::ScopeCurrentApplication())
           : 0;
    ArenaDelete(arena);
    if (!ok) return JS_EXCEPTION;
    if (!handle)
        return JS_ThrowInternalError(ctx, "creating textarea state failed");
    return JS_NewInt64(ctx, (int64_t)handle);
}

static JSValue NativeInputValue(JSContext* ctx, JSValueConst, int argc,
                                JSValueConst* argv) {
    shell::EntityHandle handle = 0;
    if (argc < 1 || !JsHandle(ctx, argv[0], &handle)) return JS_EXCEPTION;
    ShellRuntimeImpl* impl = (ShellRuntimeImpl*)JS_GetContextOpaque(ctx);
    shell::RetainedEntry* entry = impl ? impl->retained.Find(handle) : nullptr;
    if (!entry || (entry->kind != shell::RetainedKind::Input &&
                   entry->kind != shell::RetainedKind::Textarea)) {
        return JS_ThrowTypeError(ctx, "this text state has been released");
    }
    shell::ScopeHostContext host = shell::ScopeCurrentHost();
    if (!host.IsSet())
        return JS_ThrowTypeError(ctx, "value() needs a live host call");
    Str value = InputValue(entry->input);
    return JS_NewStringLen(ctx, value.s ? value.s : "", (size_t)value.len);
}

static JSValue NativeInputSetValue(JSContext* ctx, JSValueConst, int argc,
                                   JSValueConst* argv) {
    if (RefuseRetainedMutation(ctx, "set_value()")) return JS_EXCEPTION;
    shell::EntityHandle handle = 0;
    if (argc < 2 || !JsHandle(ctx, argv[0], &handle)) return JS_EXCEPTION;
    ShellRuntimeImpl* impl = (ShellRuntimeImpl*)JS_GetContextOpaque(ctx);
    shell::RetainedEntry* entry = impl ? impl->retained.Find(handle) : nullptr;
    if (!entry || (entry->kind != shell::RetainedKind::Input &&
                   entry->kind != shell::RetainedKind::Textarea)) {
        return JS_ThrowTypeError(ctx, "this text state has been released");
    }
    shell::ScopeHostContext host = shell::ScopeCurrentHost();
    if (!host.IsSet())
        return JS_ThrowTypeError(ctx, "set_value() needs a live host call");
    Arena* arena = ArenaNew();
    Str value;
    bool ok = JsString(ctx, argv[1], arena, &value);
    if (ok) InputSetValue(entry->input, value);
    ArenaDelete(arena);
    if (!ok) return JS_EXCEPTION;
    AppInvalidate(host.GetWindow());
    return JS_UNDEFINED;
}

static JSValue NativeInputNumberOption(JSContext* ctx, JSValueConst, int argc,
                                       JSValueConst* argv, int magic) {
    if (RefuseRetainedMutation(ctx, "numeric input setter"))
        return JS_EXCEPTION;
    shell::EntityHandle handle = 0;
    if (argc < 2 || !JsHandle(ctx, argv[0], &handle)) return JS_EXCEPTION;
    shell::RetainedEntry* entry =
        LiveRetained(ctx, handle, shell::RetainedKind::Input, "input");
    if (!entry) return JS_EXCEPTION;
    shell::ScopeHostContext host = shell::ScopeCurrentHost();
    if (!host.IsSet())
        return JS_ThrowTypeError(ctx,
                                 "numeric input setter needs a live host call");
    bool set = !JS_IsNull(argv[1]) && !JS_IsUndefined(argv[1]);
    double value = 0;
    if (set && (JS_ToFloat64(ctx, &value, argv[1]) < 0 || !isfinite(value))) {
        return JS_ThrowTypeError(ctx,
                                 "numeric input option must be finite or null");
    }
    if (magic == 0) {
        entry->number.hasStep = set;
        entry->number.step = value;
    } else if (magic == 1) {
        entry->number.hasMin = set;
        entry->number.min = value;
    } else {
        entry->number.hasMax = set;
        entry->number.max = value;
    }
    AppInvalidate(host.GetWindow());
    return JS_UNDEFINED;
}

static JSValue NativeInputFlag(JSContext* ctx, JSValueConst, int argc,
                               JSValueConst* argv, int magic) {
    if (RefuseRetainedMutation(ctx, "input setter")) return JS_EXCEPTION;
    shell::EntityHandle handle = 0;
    if (argc < 2 || !JsHandle(ctx, argv[0], &handle)) return JS_EXCEPTION;
    shell::RetainedEntry* entry =
        LiveRetained(ctx, handle, shell::RetainedKind::Input, "input");
    if (!entry) return JS_EXCEPTION;
    shell::ScopeHostContext host = shell::ScopeCurrentHost();
    if (!host.IsSet())
        return JS_ThrowTypeError(ctx, "input setter needs a live host call");
    bool value = JS_ToBool(ctx, argv[1]) != 0;
    if (magic == 0)
        entry->input->masked = value;
    else
        entry->input->loading = value;
    AppInvalidate(host.GetWindow());
    return JS_UNDEFINED;
}

static JSValue NativeTextareaRows(JSContext* ctx, JSValueConst, int argc,
                                  JSValueConst* argv, int magic) {
    if (RefuseRetainedMutation(ctx, "textarea setter")) return JS_EXCEPTION;
    shell::EntityHandle handle = 0;
    if (argc < 2 || !JsHandle(ctx, argv[0], &handle)) return JS_EXCEPTION;
    shell::RetainedEntry* entry =
        LiveRetained(ctx, handle, shell::RetainedKind::Textarea, "textarea");
    if (!entry) return JS_EXCEPTION;
    shell::ScopeHostContext host = shell::ScopeCurrentHost();
    if (!host.IsSet())
        return JS_ThrowTypeError(ctx, "textarea setter needs a live host call");
    if (magic == 2) {
        entry->input->softWrap = JS_ToBool(ctx, argv[1]) != 0;
    } else {
        int32_t first = 0, second = 0;
        if (JS_ToInt32(ctx, &first, argv[1]) < 0 || first <= 0) {
            return JS_ThrowTypeError(ctx, "textarea rows must be positive");
        }
        if (magic == 0) {
            entry->input->mode.kind = LayoutModeKind::PlainText;
            LayoutModeSetRows(&entry->input->mode, first);
        } else {
            if (argc < 3 || JS_ToInt32(ctx, &second, argv[2]) < 0 ||
                second < first) {
                return JS_ThrowTypeError(
                    ctx, "auto-grow max rows must not be below min rows");
            }
            entry->input->mode.kind = LayoutModeKind::AutoGrow;
            entry->input->mode.minRows = first;
            entry->input->mode.maxRows = second;
            LayoutModeSetRows(&entry->input->mode, first);
        }
    }
    AppInvalidate(host.GetWindow());
    return JS_UNDEFINED;
}

static bool ReadSliderValue(JSContext* ctx, JSValueConst value,
                            SliderValue* out) {
    int64_t count = 0;
    if (JS_GetLength(ctx, value, &count) < 0 || (count != 1 && count != 2)) {
        JS_ThrowTypeError(
            ctx, "slider value must contain one number or a [start, end] pair");
        return false;
    }
    double values[2] = {};
    for (int i = 0; i < (int)count; i++) {
        JSValue item = JS_GetPropertyUint32(ctx, value, (uint32_t)i);
        bool ok = !JS_IsException(item) &&
                  JS_ToFloat64(ctx, &values[i], item) == 0 &&
                  isfinite(values[i]);
        JS_FreeValue(ctx, item);
        if (!ok) {
            JS_ThrowTypeError(ctx, "slider values must be finite numbers");
            return false;
        }
    }
    *out = count == 1 ? SliderSingle((float)values[0])
                      : SliderRange((float)values[0], (float)values[1]);
    return true;
}

static JSValue SliderValueJs(JSContext* ctx, SliderValue value) {
    JSValue out = JS_NewArray(ctx);
    if (value.range) {
        JS_SetPropertyUint32(ctx, out, 0, JS_NewFloat64(ctx, value.lo));
        JS_SetPropertyUint32(ctx, out, 1, JS_NewFloat64(ctx, value.hi));
    } else {
        JS_SetPropertyUint32(ctx, out, 0, JS_NewFloat64(ctx, value.hi));
    }
    return out;
}

static JSValue NativeSliderStateNew(JSContext* ctx, JSValueConst, int argc,
                                    JSValueConst* argv) {
    if (RefuseRetainedCreation(ctx, "SliderState.new(...)"))
        return JS_EXCEPTION;
    ShellRuntimeImpl* impl = (ShellRuntimeImpl*)JS_GetContextOpaque(ctx);
    if (!impl || impl->retained.Len() >= shell::kMaxLiveEntities) {
        return JS_ThrowRangeError(
            ctx,
            "the application reached gpui-shell's retained entity limit; "
            "release unused handles");
    }
    double min = 0, max = 0, step = 0;
    Arena* arena = ArenaNew();
    Str scale;
    SliderValue value = {};
    bool ok = argc >= 5 && JS_ToFloat64(ctx, &min, argv[0]) == 0 &&
              JS_ToFloat64(ctx, &max, argv[1]) == 0 &&
              JS_ToFloat64(ctx, &step, argv[2]) == 0 &&
              JsString(ctx, argv[3], arena, &scale) &&
              ReadSliderValue(ctx, argv[4], &value);
    SliderScale nativeScale = StrEq(scale, StrL("logarithmic"))
                                  ? SliderScale::Logarithmic
                                  : SliderScale::Linear;
    ok = ok && isfinite(min) && isfinite(max) && isfinite(step) && max > min &&
         step > 0 && (nativeScale == SliderScale::Linear || min > 0) &&
         (StrEq(scale, StrL("linear")) || StrEq(scale, StrL("logarithmic")));
    if (!ok && !JS_HasException(ctx)) {
        JS_ThrowTypeError(ctx,
                          "SliderState.new needs a finite min below max, a "
                          "positive step and a valid scale");
    }
    shell::ScopeHostContext host = shell::ScopeCurrentHost();
    shell::EntityHandle handle =
        ok && host.IsSet()
            ? impl->retained.CreateSlider((float)min, (float)max, (float)step,
                                          nativeScale, value, host.GetApp(),
                                          shell::ScopeCurrentView(),
                                          shell::ScopeCurrentApplication())
            : 0;
    ArenaDelete(arena);
    if (!ok) return JS_EXCEPTION;
    if (!host.IsSet())
        return JS_ThrowTypeError(ctx,
                                 "SliderState.new(...) needs a live host call");
    if (!handle)
        return JS_ThrowInternalError(ctx, "creating slider state failed");
    return JS_NewInt64(ctx, (int64_t)handle);
}

static JSValue NativeSliderValue(JSContext* ctx, JSValueConst, int argc,
                                 JSValueConst* argv) {
    shell::EntityHandle handle = 0;
    if (argc < 1 || !JsHandle(ctx, argv[0], &handle)) return JS_EXCEPTION;
    shell::RetainedEntry* entry =
        LiveRetained(ctx, handle, shell::RetainedKind::Slider, "slider");
    if (!entry) return JS_EXCEPTION;
    shell::ScopeHostContext host = shell::ScopeCurrentHost();
    if (!host.IsSet())
        return JS_ThrowTypeError(ctx, "value() needs a live host call");
    return SliderValueJs(ctx, entry->slider->value);
}

static JSValue NativeSliderSetValue(JSContext* ctx, JSValueConst, int argc,
                                    JSValueConst* argv) {
    if (RefuseRetainedMutation(ctx, "SliderState.set_value()"))
        return JS_EXCEPTION;
    shell::EntityHandle handle = 0;
    SliderValue value = {};
    if (argc < 2 || !JsHandle(ctx, argv[0], &handle) ||
        !ReadSliderValue(ctx, argv[1], &value))
        return JS_EXCEPTION;
    shell::RetainedEntry* entry =
        LiveRetained(ctx, handle, shell::RetainedKind::Slider, "slider");
    if (!entry) return JS_EXCEPTION;
    shell::ScopeHostContext host = shell::ScopeCurrentHost();
    if (!host.IsSet())
        return JS_ThrowTypeError(ctx, "set_value() needs a live host call");
    SliderSetValue(entry->slider, SliderValueClamp(value, entry->slider->min,
                                                   entry->slider->max));
    AppInvalidate(host.GetWindow());
    return JS_UNDEFINED;
}

static JSValue NativeSliderBounds(JSContext* ctx, JSValueConst, int argc,
                                  JSValueConst* argv) {
    shell::EntityHandle handle = 0;
    if (argc < 1 || !JsHandle(ctx, argv[0], &handle)) return JS_EXCEPTION;
    shell::RetainedEntry* entry =
        LiveRetained(ctx, handle, shell::RetainedKind::Slider, "slider");
    if (!entry) return JS_EXCEPTION;
    shell::ScopeHostContext host = shell::ScopeCurrentHost();
    if (!host.IsSet())
        return JS_ThrowTypeError(ctx, "min_value() needs a live host call");
    JSValue out = JS_NewArray(ctx);
    JS_SetPropertyUint32(ctx, out, 0, JS_NewFloat64(ctx, entry->slider->min));
    JS_SetPropertyUint32(ctx, out, 1, JS_NewFloat64(ctx, entry->slider->max));
    JS_SetPropertyUint32(ctx, out, 2, JS_NewFloat64(ctx, entry->slider->step));
    return out;
}

static JSValue NativeOtpStateNew(JSContext* ctx, JSValueConst, int argc,
                                 JSValueConst* argv) {
    if (RefuseRetainedCreation(ctx, "OtpState.new(...)")) return JS_EXCEPTION;
    ShellRuntimeImpl* impl = (ShellRuntimeImpl*)JS_GetContextOpaque(ctx);
    if (!impl || impl->retained.Len() >= shell::kMaxLiveEntities) {
        return JS_ThrowRangeError(
            ctx,
            "the application reached gpui-shell's retained entity limit; "
            "release unused handles");
    }
    int32_t length = 0;
    if (argc < 1 || JS_ToInt32(ctx, &length, argv[0]) < 0 || length < 1 ||
        length > 64) {
        return JS_ThrowTypeError(
            ctx,
            "OtpState.new(length) expects a whole number between 1 and 64");
    }
    Arena* arena = ArenaNew();
    Str value;
    bool ok =
        OptionalJsString(ctx, argc > 1 ? argv[1] : JS_UNDEFINED, arena, &value);
    bool masked = argc > 2 && JS_ToBool(ctx, argv[2]) != 0;
    shell::ScopeHostContext host = shell::ScopeCurrentHost();
    shell::EntityHandle handle =
        ok && host.IsSet()
            ? impl->retained.CreateOtp(length, value, masked, host.GetApp(),
                                       shell::ScopeCurrentView(),
                                       shell::ScopeCurrentApplication())
            : 0;
    ArenaDelete(arena);
    if (!ok) return JS_EXCEPTION;
    if (!host.IsSet())
        return JS_ThrowTypeError(ctx,
                                 "OtpState.new(...) needs a live host call");
    if (!handle) return JS_ThrowInternalError(ctx, "creating OTP state failed");
    return JS_NewInt64(ctx, (int64_t)handle);
}

static JSValue NativeOtpValue(JSContext* ctx, JSValueConst, int argc,
                              JSValueConst* argv) {
    shell::EntityHandle handle = 0;
    if (argc < 1 || !JsHandle(ctx, argv[0], &handle)) return JS_EXCEPTION;
    ShellRuntimeImpl* impl = (ShellRuntimeImpl*)JS_GetContextOpaque(ctx);
    shell::RetainedEntry* entry = impl ? impl->retained.Find(handle) : nullptr;
    if (!entry || entry->kind != shell::RetainedKind::Otp) {
        return JS_ThrowTypeError(ctx, "this OTP state has been released");
    }
    shell::ScopeHostContext host = shell::ScopeCurrentHost();
    if (!host.IsSet())
        return JS_ThrowTypeError(ctx, "value() needs a live host call");
    OtpState* state = entry->otp.Get(entry->app);
    if (!state)
        return JS_ThrowTypeError(ctx, "this OTP state has been released");
    return JS_NewStringLen(ctx, state->value, (size_t)state->len);
}

static JSValue NativeOtpSetValue(JSContext* ctx, JSValueConst, int argc,
                                 JSValueConst* argv) {
    if (RefuseRetainedMutation(ctx, "OtpState.set_value()"))
        return JS_EXCEPTION;
    shell::EntityHandle handle = 0;
    if (argc < 2 || !JsHandle(ctx, argv[0], &handle)) return JS_EXCEPTION;
    ShellRuntimeImpl* impl = (ShellRuntimeImpl*)JS_GetContextOpaque(ctx);
    shell::RetainedEntry* entry = impl ? impl->retained.Find(handle) : nullptr;
    if (!entry || entry->kind != shell::RetainedKind::Otp) {
        return JS_ThrowTypeError(ctx, "this OTP state has been released");
    }
    shell::ScopeHostContext host = shell::ScopeCurrentHost();
    if (!host.IsSet())
        return JS_ThrowTypeError(ctx, "set_value() needs a live host call");
    OtpState* state = entry->otp.Get(entry->app);
    Arena* arena = ArenaNew();
    Str value;
    bool ok = state && JsString(ctx, argv[1], arena, &value);
    if (ok) {
        int n = value.len;
        if (n > (int)sizeof(state->value) - 1)
            n = (int)sizeof(state->value) - 1;
        if (n > 0) memcpy(state->value, value.s, (size_t)n);
        state->len = n;
        state->value[n] = 0;
    }
    ArenaDelete(arena);
    if (!ok) return JS_EXCEPTION;
    AppInvalidate(host.GetWindow());
    if (entry->owner.IsValid()) impl->owner->InvalidateScriptView(entry->owner);
    return JS_UNDEFINED;
}

static JSValue NativeOtpProperty(JSContext* ctx, JSValueConst, int argc,
                                 JSValueConst* argv, int magic) {
    shell::EntityHandle handle = 0;
    if (argc < 1 || !JsHandle(ctx, argv[0], &handle)) return JS_EXCEPTION;
    ShellRuntimeImpl* impl = (ShellRuntimeImpl*)JS_GetContextOpaque(ctx);
    shell::RetainedEntry* entry = impl ? impl->retained.Find(handle) : nullptr;
    OtpState* state = entry && entry->kind == shell::RetainedKind::Otp
                          ? entry->otp.Get(entry->app)
                          : nullptr;
    if (!state)
        return JS_ThrowTypeError(ctx, "this OTP state has been released");
    shell::ScopeHostContext host = shell::ScopeCurrentHost();
    if (!host.IsSet())
        return JS_ThrowTypeError(ctx,
                                 "OTP state access needs a live host call");
    if (magic == 0) return JS_NewInt32(ctx, state->length);
    if (magic == 1) return JS_NewBool(ctx, state->masked);
    if (magic == 2) {
        if (RefuseRetainedMutation(ctx, "OtpState.set_masked()"))
            return JS_EXCEPTION;
        if (argc < 2)
            return JS_ThrowTypeError(ctx, "set_masked expects a boolean");
        state->masked = JS_ToBool(ctx, argv[1]) != 0;
        AppInvalidate(host.GetWindow());
        if (entry->owner.IsValid())
            impl->owner->InvalidateScriptView(entry->owner);
        return JS_UNDEFINED;
    }
    if (RefuseRetainedMutation(ctx, "OtpState.focus()")) return JS_EXCEPTION;
    OtpFocus(state, host.GetApp(), host.GetWindow());
    FocusHandleFocus(host.GetWindow(), state->focus);
    AppInvalidate(host.GetWindow());
    return JS_UNDEFINED;
}

static bool RetainedEventOf(shell::RetainedKind kind, Str name,
                            shell::RetainedEvent* event, bool* replace) {
    *replace = false;
    if (kind == shell::RetainedKind::Input ||
        kind == shell::RetainedKind::Textarea) {
        if (StrEq(name, StrL("change")))
            *event = shell::RetainedEvent::InputChange;
        else if (StrEq(name, StrL("submit")))
            *event = shell::RetainedEvent::InputSubmit;
        else if (StrEq(name, StrL("focus")))
            *event = shell::RetainedEvent::InputFocus;
        else if (StrEq(name, StrL("blur")))
            *event = shell::RetainedEvent::InputBlur;
        else
            return false;
        return true;
    }
    if (kind == shell::RetainedKind::Slider) {
        if (StrEq(name, StrL("change")))
            *event = shell::RetainedEvent::SliderChange;
        else if (StrEq(name, StrL("release")))
            *event = shell::RetainedEvent::SliderRelease;
        else
            return false;
        return true;
    }
    if (kind == shell::RetainedKind::Otp) {
        *replace = true;
        if (StrEq(name, StrL("change")))
            *event = shell::RetainedEvent::OtpChange;
        else if (StrEq(name, StrL("complete")))
            *event = shell::RetainedEvent::OtpComplete;
        else if (StrEq(name, StrL("focus")))
            *event = shell::RetainedEvent::OtpFocus;
        else if (StrEq(name, StrL("blur")))
            *event = shell::RetainedEvent::OtpBlur;
        else
            return false;
        return true;
    }
    return false;
}

static JSValue NativeRetainedOn(JSContext* ctx, JSValueConst, int argc,
                                JSValueConst* argv) {
    if (RefuseRetainedMutation(ctx, "state.on()")) return JS_EXCEPTION;
    shell::EntityHandle handle = 0;
    if (argc < 3 || !JsHandle(ctx, argv[0], &handle) ||
        !JS_IsFunction(ctx, argv[2])) {
        return JS_ThrowTypeError(ctx,
                                 "state.on(event, handler) expects a function");
    }
    ShellRuntimeImpl* impl = (ShellRuntimeImpl*)JS_GetContextOpaque(ctx);
    shell::RetainedEntry* entry = impl ? impl->retained.Find(handle) : nullptr;
    if (!entry)
        return JS_ThrowTypeError(ctx, "this retained state has been released");
    shell::ScopeHostContext host = shell::ScopeCurrentHost();
    if (!host.IsSet()) {
        return JS_ThrowTypeError(ctx,
                                 "on(...) needs a live host call; subscribe "
                                 "from init() or an event handler");
    }
    Arena* arena = ArenaNew();
    Str name;
    bool converted = JsString(ctx, argv[1], arena, &name);
    shell::RetainedEvent event = {};
    bool replace = false;
    bool known =
        converted && RetainedEventOf(entry->kind, name, &event, &replace);
    ArenaDelete(arena);
    if (!converted) return JS_EXCEPTION;
    if (!known)
        return JS_ThrowTypeError(ctx, "unknown retained-state event name");
    shell::CallbackId callback = impl->callbacks.PushPersistent(
        ctx, argv[2], entry->owner, shell::ScopeCurrentPolicy(),
        (AppModule*)entry->application);
    if (callback == UINT64_MAX)
        return JS_ThrowInternalError(ctx, "callback id space is exhausted");
    shell::CallbackId replaced = 0;
    if (!impl->retained
             .AddCallback(handle, event, callback, replace, &replaced)) {
        impl->callbacks.RetireId(ctx, callback);
        return JS_ThrowTypeError(ctx, "this retained state has been released");
    }
    if (replaced) impl->callbacks.RetireId(ctx, replaced);
    return JS_TRUE;
}

static JSValue NativeRetainedRelease(JSContext* ctx, JSValueConst, int argc,
                                     JSValueConst* argv) {
    shell::EntityHandle handle = 0;
    if (argc < 1 || !JsHandle(ctx, argv[0], &handle)) return JS_EXCEPTION;
    ShellRuntimeImpl* impl = (ShellRuntimeImpl*)JS_GetContextOpaque(ctx);
    Vec<shell::CallbackId> callbacks;
    bool released = impl && impl->retained.Release(handle, &callbacks);
    for (int i = 0; impl && i < callbacks.len; i++) {
        impl->callbacks.RetireId(ctx, callbacks[i]);
    }
    VecReset(callbacks);
    return JS_NewBool(ctx, released);
}

static JSValue NativeRetainedComponent(JSContext* ctx, JSValueConst, int argc,
                                       JSValueConst* argv) {
    shell::EntityHandle handle = 0;
    if (argc < 2 || !JsHandle(ctx, argv[1], &handle)) return JS_EXCEPTION;
    Arena* arena = ArenaNew();
    Str name;
    bool converted = JsString(ctx, argv[0], arena, &name);
    ShellRuntimeImpl* impl = (ShellRuntimeImpl*)JS_GetContextOpaque(ctx);
    shell::RetainedEntry* entry = impl ? impl->retained.Find(handle) : nullptr;
    shell::Component component = {};
    component.kind = ComponentKindOf(name);
    component.handle = handle;
    shell::RetainedKind expected = shell::RetainedKind::Input;
    if (component.kind == shell::ComponentKind::Textarea)
        expected = shell::RetainedKind::Textarea;
    else if (component.kind == shell::ComponentKind::Slider ||
             component.kind == shell::ComponentKind::SliderTrack ||
             component.kind == shell::ComponentKind::SliderIndicator ||
             component.kind == shell::ComponentKind::SliderThumb)
        expected = shell::RetainedKind::Slider;
    else if (component.kind == shell::ComponentKind::OtpInput)
        expected = shell::RetainedKind::Otp;
    bool valid = converted && entry && entry->kind == expected;
    ArenaDelete(arena);
    if (!converted) return JS_EXCEPTION;
    if (!valid)
        return JS_ThrowTypeError(
            ctx, "this retained state has been released or has the wrong type");
    return JS_NewUint32(ctx, impl->scratch->Push(component));
}

static JSValue NativeFocusNew(JSContext* ctx, JSValueConst, int,
                              JSValueConst*) {
    if (RefuseRetainedCreation(ctx, "cx.focus_handle()")) return JS_EXCEPTION;
    ShellRuntimeImpl* impl = (ShellRuntimeImpl*)JS_GetContextOpaque(ctx);
    shell::ScopeHostContext host = shell::ScopeCurrentHost();
    if (!impl || !host.IsSet())
        return JS_ThrowTypeError(ctx,
                                 "cx.focus_handle() needs a live host call");
    if (impl->retained.Len() >= shell::kMaxLiveEntities) {
        return JS_ThrowRangeError(
            ctx,
            "the application reached gpui-shell's retained entity limit; "
            "release unused handles");
    }
    shell::EntityHandle handle =
        impl->retained.CreateFocus(host.GetApp(), shell::ScopeCurrentView(),
                                   shell::ScopeCurrentApplication());
    return handle ? JS_NewInt64(ctx, (int64_t)handle)
                  : JS_ThrowInternalError(ctx, "creating focus handle failed");
}

static JSValue NativeFocusOp(JSContext* ctx, JSValueConst, int argc,
                             JSValueConst* argv, int magic) {
    shell::EntityHandle handle = 0;
    if (argc < 1 || !JsHandle(ctx, argv[0], &handle)) return JS_EXCEPTION;
    shell::RetainedEntry* entry =
        LiveRetained(ctx, handle, shell::RetainedKind::Focus, "focus handle");
    if (!entry) return JS_EXCEPTION;
    shell::ScopeHostContext host = shell::ScopeCurrentHost();
    if (!host.IsSet())
        return JS_ThrowTypeError(ctx, "focus operation needs a live host call");
    if (magic == 0) {
        if (RefuseRetainedMutation(ctx, "FocusHandle.focus()"))
            return JS_EXCEPTION;
        FocusHandleFocus(host.GetWindow(), entry->focus);
        AppInvalidate(host.GetWindow());
        return JS_UNDEFINED;
    }
    return JS_NewBool(ctx,
                      FocusHandleIsFocused(host.GetWindow(), entry->focus));
}

static JSValue NativeVirtualScrollNew(JSContext* ctx, JSValueConst, int,
                                      JSValueConst*) {
    if (RefuseRetainedCreation(ctx, "VirtualListScrollHandle.new()"))
        return JS_EXCEPTION;
    ShellRuntimeImpl* impl = (ShellRuntimeImpl*)JS_GetContextOpaque(ctx);
    shell::ScopeHostContext host = shell::ScopeCurrentHost();
    if (!impl || !host.IsSet())
        return JS_ThrowTypeError(
            ctx, "VirtualListScrollHandle.new() needs a live host call");
    if (impl->retained.Len() >= shell::kMaxLiveEntities) {
        return JS_ThrowRangeError(
            ctx,
            "the application reached gpui-shell's retained entity limit; "
            "release unused handles");
    }
    shell::EntityHandle handle = impl->retained.CreateVirtualScroll(
        host.GetApp(), shell::ScopeCurrentView(),
        shell::ScopeCurrentApplication());
    return handle ? JS_NewInt64(ctx, (int64_t)handle)
                  : JS_ThrowInternalError(
                        ctx, "creating virtual scroll handle failed");
}

static JSValue NativeVirtualScrollOp(JSContext* ctx, JSValueConst, int argc,
                                     JSValueConst* argv, int magic) {
    shell::EntityHandle handle = 0;
    if (argc < 1 || !JsHandle(ctx, argv[0], &handle)) return JS_EXCEPTION;
    shell::RetainedEntry* entry = LiveRetained(
        ctx, handle, shell::RetainedKind::VirtualScroll, "scroll handle");
    if (!entry) return JS_EXCEPTION;
    if (magic == 1) {
        VirtualListScrollToBottomDeferred(&entry->scroll);
        return JS_UNDEFINED;
    }
    int32_t index = 0;
    if (argc < 2 || JS_ToInt32(ctx, &index, argv[1]) < 0 || index < 0) {
        return JS_ThrowTypeError(ctx,
                                 "scroll_to_item index must be non-negative");
    }
    Arena* arena = ArenaNew();
    Str strategy;
    bool ok = OptionalJsString(ctx, argc > 2 ? argv[2] : JS_UNDEFINED, arena,
                               &strategy);
    ScrollStrategy native = StrEq(strategy, StrL("center"))
                                ? ScrollStrategy::Center
                                : ScrollStrategy::Top;
    if (strategy && !StrEq(strategy, StrL("top")) &&
        !StrEq(strategy, StrL("center"))) {
        ok = false;
        JS_ThrowTypeError(ctx, "scroll strategy must be top or center");
    }
    ArenaDelete(arena);
    if (!ok) return JS_EXCEPTION;
    VirtualListScrollToItemDeferred(&entry->scroll, index, native);
    return JS_UNDEFINED;
}

static JSValue NativeVirtualList(JSContext* ctx, JSValueConst, int argc,
                                 JSValueConst* argv, int magic) {
    ShellRuntimeImpl* impl = (ShellRuntimeImpl*)JS_GetContextOpaque(ctx);
    if (!impl || argc < 5 || shell::ScopeCurrentPhase() == ScopePhase::Layout) {
        return JS_ThrowTypeError(ctx,
                                 "a virtual list cannot be built from inside "
                                 "another list's item renderer");
    }
    Arena* arena = ArenaNew();
    Str id;
    int64_t count64 = 0;
    bool ok = JsString(ctx, argv[0], arena, &id) &&
              JS_ToInt64(ctx, &count64, argv[1]) == 0 && count64 >= 0 &&
              count64 <= 1000000 && JS_IsFunction(ctx, argv[3]) &&
              JS_IsFunction(ctx, argv[4]);
    if (!ok) {
        ArenaDelete(arena);
        return JS_ThrowTypeError(
            ctx,
            "virtual list needs an id, a count up to 1000000, item sizes, "
            "get_key and render functions");
    }
    int count = (int)count64;
    if (!impl->scratch->ClaimVirtualItems((uint64_t)count, 1000000)) {
        ArenaDelete(arena);
        return JS_ThrowRangeError(ctx,
                                  "the virtual lists in one render may "
                                  "describe at most 1000000 items in total");
    }
    Size* sizes = count > 0
                      ? (Size*)Alloc(arena, (int)(sizeof(Size) * (size_t)count))
                      : nullptr;
    if (count > 0 && !sizes) {
        ArenaDelete(arena);
        return JS_ThrowOutOfMemory(ctx);
    }
    bool horizontal = magic != 0;
    if (JS_IsArray(argv[2])) {
        int64_t n = 0;
        ok = JS_GetLength(ctx, argv[2], &n) == 0 && n == count;
        for (int i = 0; ok && i < count; i++) {
            JSValue item = JS_GetPropertyUint32(ctx, argv[2], (uint32_t)i);
            double extent = 0;
            ok = !JS_IsException(item) &&
                 JS_ToFloat64(ctx, &extent, item) == 0 && isfinite(extent) &&
                 extent >= 0;
            JS_FreeValue(ctx, item);
            if (ok)
                sizes[i] = horizontal ? Size{(float)extent, 0}
                                      : Size{0, (float)extent};
        }
    } else {
        double extent = 0;
        ok = JS_ToFloat64(ctx, &extent, argv[2]) == 0 && isfinite(extent) &&
             extent >= 0;
        for (int i = 0; ok && i < count; i++) {
            sizes[i] =
                horizontal ? Size{(float)extent, 0} : Size{0, (float)extent};
        }
    }
    if (!ok) {
        ArenaDelete(arena);
        return JS_ThrowTypeError(ctx,
                                 "virtual-list item sizes must be one finite "
                                 "non-negative number or one per item");
    }
    shell::CallbackId getKey = impl->callbacks.Push(
        ctx, argv[3], shell::ScopeCurrentView(), shell::ScopeCurrentPolicy(),
        shell::ScopeCurrentGeneration(),
        (AppModule*)shell::ScopeCurrentApplication());
    shell::CallbackId render = impl->callbacks.Push(
        ctx, argv[4], shell::ScopeCurrentView(), shell::ScopeCurrentPolicy(),
        shell::ScopeCurrentGeneration(),
        (AppModule*)shell::ScopeCurrentApplication());
    if (getKey == UINT64_MAX || render == UINT64_MAX) {
        ArenaDelete(arena);
        return JS_ThrowInternalError(
            ctx,
            "virtual-list callbacks were registered outside a snapshot build");
    }
    shell::VirtualListSpec list = {};
    list.id = id;
    list.axis = horizontal ? Axis::Horizontal : Axis::Vertical;
    list.sizes = sizes;
    list.sizeCount = count;
    list.getKey = getKey;
    list.renderItems = render;
    shell::Component component = {};
    component.kind = horizontal ? shell::ComponentKind::HVirtualList
                                : shell::ComponentKind::VVirtualList;
    component.virtualList = &list;
    shell::SpecId result = impl->scratch->Push(component);
    ArenaDelete(arena);
    return JS_NewUint32(ctx, result);
}

// Shell's Rust snapshot rounds each Hsla channel to the nearest byte. The
// general C++ palette conversion intentionally truncates to match Colorize,
// so keep this API-specific rule here and preserve exact script hex colors.
static uint8_t ShellThemeByte(float value) {
    if (value <= 0) return 0;
    if (value >= 1) return 255;
    return (uint8_t)lroundf(value * 255.f);
}

static Rgba ShellThemeRgba(Hsla color) {
    float h = color.h, s = color.s, l = color.l;
    float k = (1.f - fabsf(2.f * l - 1.f)) * s;
    float x = k * (1.f - fabsf(fmodf(h * 6.f, 2.f) - 1.f));
    float m = l - k * 0.5f;
    float km = k + m;
    float xm = x + m;
    float r = 0, g = 0, b = 0;
    switch ((int)floorf(h * 6.f)) {
        case 0:
        case 6:
            r = km, g = xm, b = m;
            break;
        case 1:
            r = xm, g = km, b = m;
            break;
        case 2:
            r = m, g = km, b = xm;
            break;
        case 3:
            r = m, g = xm, b = km;
            break;
        case 4:
            r = xm, g = m, b = km;
            break;
        default:
            r = km, g = m, b = xm;
            break;
    }
    return Rgba8(ShellThemeByte(r), ShellThemeByte(g), ShellThemeByte(b),
                 ShellThemeByte(color.a));
}

static void AppendThemeColor(StrBuilder* out, Str name, bool comma) {
    Hsla color = {};
    if (!shell::ThemeTokenColor(name, &color)) return;
    Rgba rgba = ShellThemeRgba(color);
    if (comma) out->AppendChar(',');
    out->Append(fmt("\"%s\":\"#%02x%02x%02x\"", name, rgba.r, rgba.g, rgba.b));
}

static void AppendThemeColors(StrBuilder* out) {
    int index = 0;
    SeqStrings names = shell::ThemeColorTokenNames();
    for (Str name = SeqStrFirst(names); name.len > 0; name = SeqStrNext(name)) {
        AppendThemeColor(out, name, index++ > 0);
    }
}

static void AppendThemeScale(StrBuilder* out, SeqStrings names,
                             bool (*value)(Str, float*)) {
    int index = 0;
    for (Str name = SeqStrFirst(names); name.len > 0; name = SeqStrNext(name)) {
        float number = 0;
        if (value(name, &number)) {
            if (index++) out->AppendChar(',');
            out->Append(fmt("\"%s\":%g", name, number));
        }
    }
}

static bool ThemeRequiredProperty(JSContext* ctx, JSValueConst object,
                                  const char* key, JSValue* out,
                                  const char* container) {
    *out = JS_GetPropertyStr(ctx, object, key);
    if (JS_IsException(*out)) return false;
    if (!JS_IsUndefined(*out)) return true;
    JS_FreeValue(ctx, *out);
    *out = JS_UNDEFINED;
    JS_ThrowTypeError(ctx, "%s is missing `%s`", container, key);
    return false;
}

static bool SetThemeColor(ColorTokens* colors, Str name, Rgba value) {
    if (StrEq(name, StrL("background")))
        colors->background = value;
    else if (StrEq(name, StrL("foreground")))
        colors->foreground = value;
    else if (StrEq(name, StrL("surface")))
        colors->surface = value;
    else if (StrEq(name, StrL("surface_foreground")))
        colors->surfaceForeground = value;
    else if (StrEq(name, StrL("primary")))
        colors->primary = value;
    else if (StrEq(name, StrL("primary_foreground")))
        colors->primaryForeground = value;
    else if (StrEq(name, StrL("secondary")))
        colors->secondary = value;
    else if (StrEq(name, StrL("secondary_foreground")))
        colors->secondaryForeground = value;
    else if (StrEq(name, StrL("muted")))
        colors->muted = value;
    else if (StrEq(name, StrL("muted_foreground")))
        colors->mutedForeground = value;
    else if (StrEq(name, StrL("accent")))
        colors->accent = value;
    else if (StrEq(name, StrL("accent_foreground")))
        colors->accentForeground = value;
    else if (StrEq(name, StrL("destructive")))
        colors->destructive = value;
    else if (StrEq(name, StrL("destructive_foreground")))
        colors->destructiveForeground = value;
    else if (StrEq(name, StrL("border")))
        colors->border = value;
    else if (StrEq(name, StrL("input")))
        colors->input = value;
    else if (StrEq(name, StrL("ring")))
        colors->ring = value;
    else
        return false;
    return true;
}

static bool SetThemeSpacing(SpacingTokens* spacing, Str name, float value) {
    if (StrEq(name, StrL("xxs")))
        spacing->xxs = value;
    else if (StrEq(name, StrL("xs")))
        spacing->xs = value;
    else if (StrEq(name, StrL("sm")))
        spacing->sm = value;
    else if (StrEq(name, StrL("md")))
        spacing->md = value;
    else if (StrEq(name, StrL("lg")))
        spacing->lg = value;
    else if (StrEq(name, StrL("xl")))
        spacing->xl = value;
    else if (StrEq(name, StrL("xxl")))
        spacing->xxl = value;
    else
        return false;
    return true;
}

static bool SetThemeRadius(RadiusTokens* radius, Str name, float value) {
    if (StrEq(name, StrL("none")))
        radius->none = value;
    else if (StrEq(name, StrL("sm")))
        radius->sm = value;
    else if (StrEq(name, StrL("md")))
        radius->md = value;
    else if (StrEq(name, StrL("lg")))
        radius->lg = value;
    else if (StrEq(name, StrL("xl")))
        radius->xl = value;
    else if (StrEq(name, StrL("full")))
        radius->full = value;
    else
        return false;
    return true;
}

static bool ReadThemeColors(JSContext* ctx, JSValueConst object,
                            ColorTokens* colors) {
    SeqStrings names = shell::ThemeColorTokenNames();
    for (Str name = SeqStrFirst(names); name.len > 0; name = SeqStrNext(name)) {
        JSValue property = JS_UNDEFINED;
        if (!ThemeRequiredProperty(ctx, object, name.s, &property,
                                   "theme tokens.colors")) {
            return false;
        }
        if (!JS_IsString(property)) {
            JS_FreeValue(ctx, property);
            JS_ThrowTypeError(ctx, "theme color `%.*s` must be a string",
                              name.len, name.s);
            return false;
        }
        size_t len = 0;
        const char* text = JS_ToCStringLen(ctx, &len, property);
        Hsla color = {};
        ShellError error = {};
        bool ok = text && shell::BridgedAsColor(
                              shell::Bridged::String(Str(text, (int)len)),
                              &color, &error);
        if (text) JS_FreeCString(ctx, text);
        JS_FreeValue(ctx, property);
        if (!ok) {
            JSValue result = JS_ThrowTypeError(
                ctx, "theme color `%.*s`: %.*s", name.len, name.s,
                error.message.len, error.message.s ? error.message.s : "");
            (void)result;
            ShellErrorClear(&error);
            return false;
        }
        ShellErrorClear(&error);
        SetThemeColor(colors, name, ShellThemeRgba(color));
    }
    return true;
}

static bool ReadThemeScale(JSContext* ctx, JSValueConst object,
                           SeqStrings names, const char* container,
                           SpacingTokens* spacing, RadiusTokens* radius) {
    for (Str name = SeqStrFirst(names); name.len > 0; name = SeqStrNext(name)) {
        JSValue property = JS_UNDEFINED;
        if (!ThemeRequiredProperty(ctx, object, name.s, &property, container)) {
            return false;
        }
        double number = 0;
        bool ok = JS_IsNumber(property) &&
                  JS_ToFloat64(ctx, &number, property) == 0 &&
                  isfinite(number) && number >= 0;
        JS_FreeValue(ctx, property);
        if (!ok) {
            JS_ThrowTypeError(
                ctx, "theme token `%.*s` must be finite and non-negative",
                name.len, name.s);
            return false;
        }
        if (spacing)
            SetThemeSpacing(spacing, name, (float)number);
        else
            SetThemeRadius(radius, name, (float)number);
    }
    return true;
}

// The two font families a script may state, owned for the life of the process.
//
// `TypographyTokens` carries borrowed `Str`s — its defaults are literals — and
// the base theme is process-global, so a family a script supplies has to
// outlive the JavaScript string it arrived in. One owned copy per family, and
// the previous one is freed when it is replaced.
static Str gShellThemeSans;
static Str gShellThemeMono;

// Reads one optional text style. Every field left out keeps the token it would
// have replaced: that is what lets an application state the one size it has an
// opinion about — usually `md`, the window's base text size — without
// restating a line height, a weight, and two font families it does not.
static bool ReadThemeTextStyle(JSContext* ctx, JSValueConst object,
                               const char* name, TextStyleToken* token) {
    JSValue value = JS_GetPropertyStr(ctx, object, name);
    if (JS_IsException(value)) return false;
    if (JS_IsUndefined(value) || JS_IsNull(value)) {
        JS_FreeValue(ctx, value);
        return true;
    }
    if (!JS_IsObject(value) || JS_IsArray(value)) {
        JS_FreeValue(ctx, value);
        JS_ThrowTypeError(ctx, "theme typography `%s` must be an object", name);
        return false;
    }
    struct Field {
        const char* key;
        float* target;
        // Above zero, not merely non-negative: a size or a line height of zero
        // is text that cannot be read, and it would be applied silently. The
        // spacing and radius scales allow zero because a gap of zero is a real
        // answer.
        float low;
        float high;
        const char* bounds;
    };
    float weight = (float)(uint16_t)token->weight;
    Field fields[3] = {
        {"size", &token->size, 0, 0, "must be above zero"},
        {"line_height", &token->lineHeight, 0, 0, "must be above zero"},
        // The CSS range, which is what FontWeight's own constants span.
        {"weight", &weight, 1, 1000, "must be between 1 and 1000"},
    };
    for (const Field& field : fields) {
        JSValue supplied = JS_GetPropertyStr(ctx, value, field.key);
        if (JS_IsException(supplied)) {
            JS_FreeValue(ctx, value);
            return false;
        }
        if (JS_IsUndefined(supplied) || JS_IsNull(supplied)) {
            JS_FreeValue(ctx, supplied);
            continue;
        }
        double number = 0;
        bool ok = JS_IsNumber(supplied) &&
                  JS_ToFloat64(ctx, &number, supplied) == 0 && isfinite(number);
        JS_FreeValue(ctx, supplied);
        if (ok) {
            ok = field.high > 0 ? number >= field.low && number <= field.high
                                : number > 0;
        }
        if (!ok) {
            JS_FreeValue(ctx, value);
            JS_ThrowTypeError(ctx, "theme typography `%s.%s` %s", name,
                              field.key, field.bounds);
            return false;
        }
        *field.target = (float)number;
    }
    token->weight = (FontWeight)(uint16_t)(weight + 0.5f);
    JS_FreeValue(ctx, value);
    return true;
}

// Font families are taken as written — a family name this platform does not
// have falls back the way every other missing family does, and refusing one
// here would mean this module deciding which fonts exist.
static bool ReadThemeFamily(JSContext* ctx, JSValueConst object,
                            const char* name, Str* owned, Str* target) {
    JSValue value = JS_GetPropertyStr(ctx, object, name);
    if (JS_IsException(value)) return false;
    if (JS_IsUndefined(value) || JS_IsNull(value)) {
        JS_FreeValue(ctx, value);
        return true;
    }
    if (!JS_IsString(value)) {
        JS_FreeValue(ctx, value);
        JS_ThrowTypeError(ctx, "theme typography `%s` must be a string", name);
        return false;
    }
    size_t len = 0;
    const char* text = JS_ToCStringLen(ctx, &len, value);
    if (text) {
        StrFree(*owned);
        *owned = StrDup(Str(text, (int)len));
        *target = *owned;
        JS_FreeCString(ctx, text);
    }
    JS_FreeValue(ctx, value);
    return text != nullptr;
}

// The type scale, and the only block a theme may leave out.
//
// Colours, spacing and radius are required because a script that names them
// names all of them: a palette with half its roles missing is a window drawn in
// two themes. Typography is different — it arrived after themes were already
// being written, and a theme that says nothing about type is not incomplete, it
// is one that accepts the scale it was given.
static bool ReadThemeTypography(JSContext* ctx, JSValueConst tokensValue,
                                TypographyTokens* typography) {
    JSValue value = JS_GetPropertyStr(ctx, tokensValue, "typography");
    if (JS_IsException(value)) return false;
    if (JS_IsUndefined(value) || JS_IsNull(value)) {
        JS_FreeValue(ctx, value);
        return true;
    }
    if (!JS_IsObject(value) || JS_IsArray(value)) {
        JS_FreeValue(ctx, value);
        JS_ThrowTypeError(ctx, "theme tokens.typography must be an object");
        return false;
    }
    // `mono_md` is not a sixth step of the scale above — it is the size `mono`
    // is set at, and the two are read together.
    bool ok = ReadThemeFamily(ctx, value, "sans", &gShellThemeSans,
                              &typography->sans) &&
              ReadThemeFamily(ctx, value, "mono", &gShellThemeMono,
                              &typography->mono) &&
              ReadThemeTextStyle(ctx, value, "xs", &typography->xs) &&
              ReadThemeTextStyle(ctx, value, "sm", &typography->sm) &&
              ReadThemeTextStyle(ctx, value, "md", &typography->md) &&
              ReadThemeTextStyle(ctx, value, "lg", &typography->lg) &&
              ReadThemeTextStyle(ctx, value, "xl", &typography->xl) &&
              ReadThemeTextStyle(ctx, value, "mono_md", &typography->monoMd);
    JS_FreeValue(ctx, value);
    return ok;
}

// A font family name as a JSON string. Families are author-supplied, so the two
// characters JSON cannot carry raw are escaped rather than assumed absent.
static void AppendJsonString(StrBuilder* out, Str value) {
    out->AppendChar('"');
    for (int i = 0; i < value.len; i++) {
        char c = value.s[i];
        if (c == '\\' || c == '"') out->AppendChar('\\');
        out->AppendChar(c);
    }
    out->AppendChar('"');
}

static void AppendThemeTextStyle(StrBuilder* out, const char* name,
                                 const TextStyleToken& style) {
    out->Append(fmt("\"%s\":{\"size\":%g,\"line_height\":%g,\"weight\":%g}",
                    Str(name), (double)style.size, (double)style.lineHeight,
                    (double)(uint16_t)style.weight));
}

static void AppendThemeTypography(StrBuilder* out) {
    TypographyTokens typography = {};
    if (!shell::ThemeTypographyTokens(&typography)) return;
    out->Append(StrL("\"sans\":"));
    AppendJsonString(out, typography.sans);
    out->Append(StrL(",\"mono\":"));
    AppendJsonString(out, typography.mono);
    out->AppendChar(',');
    AppendThemeTextStyle(out, "xs", typography.xs);
    out->AppendChar(',');
    AppendThemeTextStyle(out, "sm", typography.sm);
    out->AppendChar(',');
    AppendThemeTextStyle(out, "md", typography.md);
    out->AppendChar(',');
    AppendThemeTextStyle(out, "lg", typography.lg);
    out->AppendChar(',');
    AppendThemeTextStyle(out, "xl", typography.xl);
    out->AppendChar(',');
    AppendThemeTextStyle(out, "mono_md", typography.monoMd);
}

static JSValue NativeSetTheme(JSContext* ctx, JSValueConst, int argc,
                              JSValueConst* argv) {
    if (shell::ScopeHasCurrent() &&
        (shell::ScopeCurrentPhase() == ScopePhase::Render ||
         shell::ScopeCurrentPhase() == ScopePhase::Layout)) {
        return JS_ThrowTypeError(
            ctx,
            "set_theme(theme) cannot run during render or layout; switch "
            "themes from an event handler or task");
    }
    if (argc < 1 || !JS_IsObject(argv[0]) || JS_IsArray(argv[0])) {
        return JS_ThrowTypeError(ctx, "set_theme(theme) expects an object");
    }
    shell::ScopeHostContext host = shell::ScopeCurrentHost();
    if (!host.IsSet()) {
        return JS_ThrowTypeError(ctx,
                                 "set_theme(theme) needs a live host call; "
                                 "call it from an event handler");
    }

    JSValue appearanceValue = JS_UNDEFINED;
    if (!ThemeRequiredProperty(ctx, argv[0], "appearance", &appearanceValue,
                               "theme")) {
        return JS_EXCEPTION;
    }
    if (!JS_IsString(appearanceValue)) {
        JS_FreeValue(ctx, appearanceValue);
        return JS_ThrowTypeError(ctx,
                                 "theme appearance must be `light` or `dark`");
    }
    size_t appearanceLen = 0;
    const char* appearanceText =
        JS_ToCStringLen(ctx, &appearanceLen, appearanceValue);
    Str appearance(appearanceText, (int)appearanceLen);
    bool dark = appearanceText && StrEq(appearance, StrL("dark"));
    bool light = appearanceText && StrEq(appearance, StrL("light"));
    if (appearanceText) JS_FreeCString(ctx, appearanceText);
    JS_FreeValue(ctx, appearanceValue);
    if (!dark && !light) {
        return JS_ThrowTypeError(ctx,
                                 "theme appearance must be `light` or `dark`");
    }

    JSValue tokensValue = JS_UNDEFINED;
    if (!ThemeRequiredProperty(ctx, argv[0], "tokens", &tokensValue, "theme")) {
        return JS_EXCEPTION;
    }
    if (!JS_IsObject(tokensValue) || JS_IsArray(tokensValue)) {
        JS_FreeValue(ctx, tokensValue);
        return JS_ThrowTypeError(ctx, "theme tokens must be an object");
    }
    JSValue colorsValue = JS_UNDEFINED;
    JSValue spacingValue = JS_UNDEFINED;
    JSValue radiusValue = JS_UNDEFINED;
    bool properties = ThemeRequiredProperty(ctx, tokensValue, "colors",
                                            &colorsValue, "theme tokens") &&
                      ThemeRequiredProperty(ctx, tokensValue, "spacing",
                                            &spacingValue, "theme tokens") &&
                      ThemeRequiredProperty(ctx, tokensValue, "radius",
                                            &radiusValue, "theme tokens");
    BaseTheme base = BaseTheme::Global(host.GetApp());
    SemanticThemeTokens tokens = base.tokens;
    if (properties) {
        properties = ReadThemeTypography(ctx, tokensValue, &tokens.typography);
    }
    JS_FreeValue(ctx, tokensValue);
    if (!properties) {
        JS_FreeValue(ctx, colorsValue);
        JS_FreeValue(ctx, spacingValue);
        JS_FreeValue(ctx, radiusValue);
        return JS_EXCEPTION;
    }
    if (!JS_IsObject(colorsValue) || JS_IsArray(colorsValue) ||
        !JS_IsObject(spacingValue) || JS_IsArray(spacingValue) ||
        !JS_IsObject(radiusValue) || JS_IsArray(radiusValue)) {
        JS_FreeValue(ctx, colorsValue);
        JS_FreeValue(ctx, spacingValue);
        JS_FreeValue(ctx, radiusValue);
        return JS_ThrowTypeError(
            ctx, "theme token colors, spacing, and radius must be objects");
    }

    bool ok =
        ReadThemeColors(ctx, colorsValue, &tokens.colors) &&
        ReadThemeScale(ctx, spacingValue, shell::ThemeSpacingTokenNames(),
                       "theme tokens.spacing", &tokens.spacing, nullptr) &&
        ReadThemeScale(ctx, radiusValue, shell::ThemeRadiusTokenNames(),
                       "theme tokens.radius", nullptr, &tokens.radius);
    JS_FreeValue(ctx, colorsValue);
    JS_FreeValue(ctx, spacingValue);
    JS_FreeValue(ctx, radiusValue);
    if (!ok) return JS_EXCEPTION;

    base.appearance =
        dark ? BaseThemeAppearance::Dark : BaseThemeAppearance::Light;
    base.tokens = tokens;
    BaseThemeSet(host.GetApp(), base);
    shell::ThemeTokensSync(host.GetApp());
    AppRefreshWindows(host.GetApp());
    return JS_UNDEFINED;
}

// The host a `cx.theme()` or `cx.theme` revision read belongs to.
//
// Both entry points take the same optional generation and refuse the same stale
// `cx`: a snapshot that could be read through a context whose call has ended
// would be the palette of a frame that is over.
static shell::ScopeHostContext ThemeHost(JSContext* ctx, int argc,
                                         JSValueConst* argv) {
    ShellError error = {};
    uint64_t generation = 0;
    bool explicitGeneration = argc > 0 && !JS_IsUndefined(argv[0]);
    if (explicitGeneration && JS_ToIndex(ctx, &generation, argv[0]) < 0) {
        return shell::ScopeHostForGeneration(UINT64_MAX);
    }
    shell::ScopeHostContext host =
        explicitGeneration ? shell::ScopeHostForGeneration(generation, &error)
                           : shell::ScopeCurrentHost();
    if (!host.IsSet()) {
        Str message = error.IsSet() ? error.message
                                    : StrL("cx.theme() needs a live host call");
        JS_ThrowTypeError(ctx, "%.*s", message.len, message.s);
    }
    ShellErrorClear(&error);
    return host;
}

// The palette's revision, which is what a script's theme cache compares.
//
// One number rather than the whole snapshot: a description that asks fifty
// components for a colour crosses fifty times either way, but only the crossing
// that finds a new revision builds a JSON document and parses it.
static JSValue NativeThemeRevision(JSContext* ctx, JSValueConst, int argc,
                                   JSValueConst* argv) {
    shell::ScopeHostContext host = ThemeHost(ctx, argc, argv);
    if (!host.IsSet()) return JS_EXCEPTION;
    return JS_NewUint32(ctx, shell::ThemeTokensSync(host.GetApp()));
}

static JSValue NativeThemeSnapshot(JSContext* ctx, JSValueConst, int argc,
                                   JSValueConst* argv) {
    ShellError error = {};
    shell::ScopeHostContext host = ThemeHost(ctx, argc, argv);
    if (!host.IsSet()) return JS_EXCEPTION;
    // One serialized palette per revision. An unchanged theme answers the copy
    // it answered last time rather than formatting forty tokens again.
    static Str gThemeSnapshotJson;
    static uint32_t gThemeSnapshotRevision = 0;
    uint32_t revision = shell::ThemeTokensSync(host.GetApp());
    if (gThemeSnapshotJson && revision == gThemeSnapshotRevision) {
        return JS_NewStringLen(ctx, gThemeSnapshotJson.s,
                               (size_t)gThemeSnapshotJson.len);
    }
    StrBuilder json;
    json.AppendChar('{');
    AppendThemeColors(&json);
    json.Append(StrL(",\"colors\":{"));
    AppendThemeColors(&json);
    json.Append(StrL("},\"spacing\":{"));
    AppendThemeScale(&json, shell::ThemeSpacingTokenNames(),
                     shell::ThemeTokenSpacing);
    json.Append(StrL("},\"radius\":{"));
    AppendThemeScale(&json, shell::ThemeRadiusTokenNames(),
                     shell::ThemeTokenRadius);
    json.Append(StrL("},\"typography\":{"));
    AppendThemeTypography(&json);
    BaseTheme base = BaseTheme::Global(host.GetApp());
    bool dark = base.appearance == BaseThemeAppearance::Dark;
    json.Append(dark ? StrL("},\"appearance\":\"dark\",\"is_dark\":true}")
                     : StrL("},\"appearance\":\"light\",\"is_dark\":false}"));
    StrFree(gThemeSnapshotJson);
    gThemeSnapshotJson = json.TakeStr();
    gThemeSnapshotRevision = revision;
    JSValue result = JS_NewStringLen(ctx, gThemeSnapshotJson.s,
                                     (size_t)gThemeSnapshotJson.len);
    ShellErrorClear(&error);
    return result;
}

static bool ValidOpenUrl(Str url) {
    if (!url || url.len > 32768) return false;
    int schemeEnd = StrFind(url, StrL("://"));
    if (schemeEnd <= 0) return false;
    Str scheme(url.s, schemeEnd);
    if (!StrEqI(scheme, StrL("http")) && !StrEqI(scheme, StrL("https"))) {
        return false;
    }
    int authorityStart = schemeEnd + 3;
    int authorityEnd = authorityStart;
    while (authorityEnd < url.len && url.s[authorityEnd] != '/' &&
           url.s[authorityEnd] != '?' && url.s[authorityEnd] != '#') {
        unsigned char c = (unsigned char)url.s[authorityEnd];
        if (c <= 0x20 || c >= 0x7f) return false;
        authorityEnd++;
    }
    int hostStart = authorityStart;
    for (int i = authorityStart; i < authorityEnd; i++) {
        if (url.s[i] == '@') hostStart = i + 1;
    }
    if (hostStart >= authorityEnd) return false;
    if (url.s[hostStart] == '[') {
        int close = hostStart + 1;
        while (close < authorityEnd && url.s[close] != ']') close++;
        return close > hostStart + 1 && close < authorityEnd;
    }
    int hostEnd = authorityEnd;
    for (int i = hostStart; i < authorityEnd; i++) {
        if (url.s[i] == ':') {
            hostEnd = i;
            break;
        }
    }
    return hostEnd > hostStart;
}

static JSValue NativeOpenUrl(JSContext* ctx, JSValueConst, int argc,
                             JSValueConst* argv) {
    if (argc < 2) {
        return JS_ThrowTypeError(
            ctx,
            "cx.open_url(url) expects an absolute HTTP(S) URL with a host");
    }
    uint64_t generation = 0;
    bool explicitGeneration = !JS_IsUndefined(argv[0]);
    if (explicitGeneration && JS_ToIndex(ctx, &generation, argv[0]) < 0) {
        return JS_EXCEPTION;
    }
    Arena* arena = ArenaNew();
    Str url;
    if (!JsString(ctx, argv[1], arena, &url)) {
        ArenaDelete(arena);
        return JS_EXCEPTION;
    }
    if (!ValidOpenUrl(url)) {
        ArenaDelete(arena);
        return JS_ThrowTypeError(
            ctx,
            "cx.open_url(url) expects an absolute HTTP(S) URL with a host");
    }
    ShellError error = {};
    shell::ScopeHostContext host =
        explicitGeneration ? shell::ScopeHostForGeneration(generation, &error)
                           : shell::ScopeCurrentHost();
    if (!host.IsSet()) {
        Str message = error.IsSet()
                          ? error.message
                          : StrL("cx.open_url(url) needs a live host call");
        JSValue result = JS_ThrowTypeError(ctx, "%.*s", message.len, message.s);
        ShellErrorClear(&error);
        ArenaDelete(arena);
        return result;
    }
    OpenUrl(url);
    ShellErrorClear(&error);
    ArenaDelete(arena);
    return JS_UNDEFINED;
}

static JSValue NativeNotify(JSContext* ctx, JSValueConst, int argc,
                            JSValueConst* argv) {
    uint64_t generation = 0;
    if (argc < 1 || JS_ToIndex(ctx, &generation, argv[0]) < 0) {
        return JS_EXCEPTION;
    }
    ShellError error = {};
    shell::ScopeHostContext host =
        shell::ScopeHostForGeneration(generation, &error);
    if (!host.IsSet()) {
        JSValue result =
            JS_ThrowTypeError(ctx, "%.*s", error.message.len, error.message.s);
        ShellErrorClear(&error);
        return result;
    }
    if (!ScopePhaseAllowsNotify(shell::ScopeCurrentPhase())) {
        return JS_ThrowTypeError(
            ctx,
            "cx.notify() is available only from an event handler or task, not "
            "during render or layout");
    }
    EntityId view = shell::ScopeCurrentView();
    if (!view.IsValid()) {
        return JS_ThrowTypeError(ctx,
                                 "cx.notify() needs a current script view");
    }
    ShellRuntime* runtime = shell::ScopeCurrentRuntime();
    if (runtime) runtime->InvalidateScriptView(view);
    NotifyEntity(host.GetApp(), view, host.GetWindow());
    return JS_UNDEFINED;
}

static JSValue NativeNotifyCurrent(JSContext* ctx, JSValueConst, int,
                                   JSValueConst*) {
    uint64_t current = shell::ScopeCurrentGeneration();
    if (current == 0) {
        return JS_ThrowTypeError(
            ctx, "cx.notify() was called with no host call in progress");
    }
    JSValue generation = JS_NewInt64(ctx, (int64_t)current);
    JSValue result = NativeNotify(ctx, JS_UNDEFINED, 1, &generation);
    JS_FreeValue(ctx, generation);
    return result;
}

static bool TaskOwnerless(JSContext* ctx, int argc, JSValueConst* argv, int at,
                          bool* ownerless) {
    *ownerless = false;
    if (argc <= at || JS_IsUndefined(argv[at]) || JS_IsNull(argv[at])) {
        *ownerless = argc > at && JS_IsNull(argv[at]);
        return true;
    }
    if (!JS_IsObject(argv[at])) {
        JS_ThrowTypeError(ctx, "task options must be an object");
        return false;
    }
    JSValue owner = JS_GetPropertyStr(ctx, argv[at], "owner");
    if (JS_IsException(owner)) return false;
    if (JS_IsNull(owner)) {
        *ownerless = true;
    } else if (!JS_IsUndefined(owner) && !JS_IsObject(owner)) {
        JS_FreeValue(ctx, owner);
        JS_ThrowTypeError(ctx, "opts.owner must be the current view or null");
        return false;
    }
    JS_FreeValue(ctx, owner);
    return true;
}

static bool TaskDelay(JSContext* ctx, JSValueConst value, int* ms) {
    double number = 0;
    if (JS_ToFloat64(ctx, &number, value) < 0 || !isfinite(number) ||
        number < 0 || number > 2147483647.0) {
        JS_ThrowTypeError(
            ctx, "timer expects a finite non-negative number of milliseconds");
        return false;
    }
    *ms = number < 1 ? 1 : (int)number;
    return true;
}

static JSValue NativeTaskNew(JSContext* ctx, JSValueConst, int argc,
                             JSValueConst* argv) {
    ShellRuntimeImpl* impl = (ShellRuntimeImpl*)JS_GetContextOpaque(ctx);
    bool ownerless = false;
    if (!impl || !TaskOwnerless(ctx, argc, argv, 0, &ownerless)) {
        return JS_EXCEPTION;
    }
    uint32_t id = NewTask(impl, ShellTaskKind::Spawn, JS_UNDEFINED, nullptr,
                          nullptr, ownerless);
    if (!id)
        return JS_ThrowRangeError(
            ctx, "the runtime reached its 1024 outstanding task limit");
    return JS_NewUint32(ctx, id);
}

static JSValue NativeTaskFinish(JSContext* ctx, JSValueConst, int argc,
                                JSValueConst* argv) {
    uint32_t id = 0;
    if (argc < 1 || JS_ToUint32(ctx, &id, argv[0]) < 0) return JS_EXCEPTION;
    ShellRuntimeImpl* impl = (ShellRuntimeImpl*)JS_GetContextOpaque(ctx);
    return JS_NewBool(ctx, ForgetTask(impl, id));
}

static JSValue NativeTaskReject(JSContext* ctx, JSValueConst, int argc,
                                JSValueConst* argv) {
    uint32_t id = 0;
    if (argc < 1 || JS_ToUint32(ctx, &id, argv[0]) < 0) return JS_EXCEPTION;
    if (argc > 1) {
        size_t n = 0;
        const char* message = JS_ToCStringLen(ctx, &n, argv[1]);
        if (message) {
            log(fmt("unhandled rejection in cx.spawn: %s",
                    Str(message, (int)n)));
            JS_FreeCString(ctx, message);
        }
    }
    ShellRuntimeImpl* impl = (ShellRuntimeImpl*)JS_GetContextOpaque(ctx);
    ForgetTask(impl, id);
    return JS_UNDEFINED;
}

static JSValue NativeTaskCancel(JSContext* ctx, JSValueConst, int argc,
                                JSValueConst* argv) {
    uint32_t id = 0;
    if (argc < 1 || JS_ToUint32(ctx, &id, argv[0]) < 0) return JS_EXCEPTION;
    ShellRuntimeImpl* impl = (ShellRuntimeImpl*)JS_GetContextOpaque(ctx);
    return JS_NewBool(ctx, ForgetTask(impl, id));
}

static JSValue NativeTaskIsDone(JSContext* ctx, JSValueConst, int argc,
                                JSValueConst* argv) {
    uint32_t id = 0;
    if (argc < 1 || JS_ToUint32(ctx, &id, argv[0]) < 0) return JS_EXCEPTION;
    ShellRuntimeImpl* impl = (ShellRuntimeImpl*)JS_GetContextOpaque(ctx);
    return JS_NewBool(ctx, FindTask(impl, id) == nullptr);
}

static JSValue NativeSleep(JSContext* ctx, JSValueConst, int argc,
                           JSValueConst* argv) {
    ShellRuntimeImpl* impl = (ShellRuntimeImpl*)JS_GetContextOpaque(ctx);
    int ms = 1;
    if (!impl || argc < 1 || !TaskDelay(ctx, argv[0], &ms)) return JS_EXCEPTION;
    shell::ScopeHostContext host = shell::ScopeCurrentHost();
    if (!host.IsSet()) {
        return JS_ThrowTypeError(
            ctx, "cx.sleep(ms) was called with no host call in progress");
    }
    Entity<ShellTaskDriver> driver = TaskDriver(impl, host.GetApp());
    if (!driver.IsValid())
        return JS_ThrowInternalError(ctx,
                                     "creating the shell task driver failed");
    JSValue resolving[2] = {JS_UNDEFINED, JS_UNDEFINED};
    JSValue promise = JS_NewPromiseCapability(ctx, resolving);
    if (JS_IsException(promise)) return promise;
    uint32_t id = NewTask(impl, ShellTaskKind::Sleep, resolving[0],
                          host.GetApp(), host.GetWindow());
    JS_FreeValue(ctx, resolving[0]);
    JS_FreeValue(ctx, resolving[1]);
    if (!id) {
        JS_FreeValue(ctx, promise);
        return JS_ThrowRangeError(
            ctx, "the runtime reached its 1024 outstanding task limit");
    }
    ShellTask* task = FindTask(impl, id);
    task->timer = WindowSetTimeout(
        host.GetWindow(), ms,
        ListenTo(driver, &ShellTaskDriver::OnTimer, (intptr_t)id));
    if (!task->timer) {
        ForgetTask(impl, id, false);
        JS_FreeValue(ctx, promise);
        return JS_ThrowInternalError(ctx, "arming cx.sleep(ms) failed");
    }
    return promise;
}

static JSValue NativeTimer(JSContext* ctx, JSValueConst, int argc,
                           JSValueConst* argv, int magic) {
    ShellRuntimeImpl* impl = (ShellRuntimeImpl*)JS_GetContextOpaque(ctx);
    int ms = 1;
    bool ownerless = false;
    if (!impl || argc < 2 || !TaskDelay(ctx, argv[0], &ms) ||
        !JS_IsFunction(ctx, argv[1]) ||
        !TaskOwnerless(ctx, argc, argv, 2, &ownerless)) {
        if (!JS_HasException(ctx))
            JS_ThrowTypeError(ctx, "timer needs a delay and callback function");
        return JS_EXCEPTION;
    }
    shell::ScopeHostContext host = shell::ScopeCurrentHost();
    if (!host.IsSet()) {
        return JS_ThrowTypeError(ctx, "cx.timer needs a live host call");
    }
    Entity<ShellTaskDriver> driver = TaskDriver(impl, host.GetApp());
    if (!driver.IsValid())
        return JS_ThrowInternalError(ctx,
                                     "creating the shell task driver failed");
    ShellTaskKind kind =
        magic ? ShellTaskKind::TimerEvery : ShellTaskKind::TimerOnce;
    uint32_t id = NewTask(impl, kind, argv[1], host.GetApp(), host.GetWindow(),
                          ownerless);
    if (!id)
        return JS_ThrowRangeError(
            ctx, "the runtime reached its 1024 outstanding task limit");
    ShellTask* task = FindTask(impl, id);
    Listener listener =
        ListenTo(driver, &ShellTaskDriver::OnTimer, (intptr_t)id);
    task->timer = magic ? WindowSetInterval(host.GetWindow(), ms, listener)
                        : WindowSetTimeout(host.GetWindow(), ms, listener);
    if (!task->timer) {
        ForgetTask(impl, id, false);
        return JS_ThrowInternalError(ctx, "arming the shell timer failed");
    }
    return JS_NewUint32(ctx, id);
}

static Policy* CurrentPolicy(bool* release) {
    Policy* policy = shell::ScopeCurrentPolicy();
    *release = policy == nullptr;
    return policy ? policy : PolicyDefault();
}

static shell::Storage* AllowedStorage(JSContext* ctx, bool session,
                                      Policy** held) {
    bool release = false;
    Policy* policy = CurrentPolicy(&release);
    if (!session && !PolicyCapabilities(policy).HasStorage()) {
        if (release) PolicyRelease(policy);
        JS_ThrowTypeError(
            ctx, "storage is not granted; set capabilities.storage to true");
        return nullptr;
    }
    *held = release ? policy : nullptr;
    shell::Storage* storage = PolicyStorage(policy, session);
    if (!storage || (!session && !storage->HasPath())) {
        if (release) PolicyRelease(policy);
        *held = nullptr;
        JS_ThrowTypeError(ctx,
                          "localStorage has no backing file; call "
                          "ShellSetStoragePath before loading the application");
        return nullptr;
    }
    return storage;
}

static JSValue NativeStorageGet(JSContext* ctx, JSValueConst, int argc,
                                JSValueConst* argv) {
    Policy* held = nullptr;
    bool session = argc > 1 && JS_ToBool(ctx, argv[1]) != 0;
    shell::Storage* storage = AllowedStorage(ctx, session, &held);
    if (!storage) return JS_EXCEPTION;
    Arena* arena = ArenaNew();
    Str key;
    bool ok = argc >= 1 && JsString(ctx, argv[0], arena, &key);
    Str value = ok ? storage->Get(key) : Str{};
    JSValue result = !ok     ? JS_EXCEPTION
                     : value ? JS_NewStringLen(ctx, value.s, (size_t)value.len)
                             : JS_NULL;
    ArenaDelete(arena);
    PolicyRelease(held);
    return result;
}

static JSValue NativeStorageSet(JSContext* ctx, JSValueConst, int argc,
                                JSValueConst* argv) {
    Policy* held = nullptr;
    bool session = argc > 2 && JS_ToBool(ctx, argv[2]) != 0;
    shell::Storage* storage = AllowedStorage(ctx, session, &held);
    if (!storage) return JS_EXCEPTION;
    Arena* arena = ArenaNew();
    Str key, value, error;
    bool ok = argc >= 2 && JsString(ctx, argv[0], arena, &key) &&
              JsString(ctx, argv[1], arena, &value) &&
              storage->Set(key, value, &error);
    if (ok && !session) {
        DriveStorage(held ? held : shell::ScopeCurrentPolicy());
    }
    JSValue result =
        ok      ? JS_UNDEFINED
        : error ? JS_ThrowInternalError(ctx, "%.*s", error.len, error.s)
                : JS_EXCEPTION;
    StrFree(error);
    ArenaDelete(arena);
    PolicyRelease(held);
    return result;
}

static JSValue NativeStorageRemove(JSContext* ctx, JSValueConst, int argc,
                                   JSValueConst* argv) {
    Policy* held = nullptr;
    bool session = argc > 1 && JS_ToBool(ctx, argv[1]) != 0;
    shell::Storage* storage = AllowedStorage(ctx, session, &held);
    if (!storage) return JS_EXCEPTION;
    Arena* arena = ArenaNew();
    Str key, error;
    bool ok = argc >= 1 && JsString(ctx, argv[0], arena, &key) &&
              storage->Remove(key, &error);
    if (ok && !session) {
        DriveStorage(held ? held : shell::ScopeCurrentPolicy());
    }
    JSValue result =
        ok      ? JS_UNDEFINED
        : error ? JS_ThrowInternalError(ctx, "%.*s", error.len, error.s)
                : JS_EXCEPTION;
    StrFree(error);
    ArenaDelete(arena);
    PolicyRelease(held);
    return result;
}

static JSValue NativeStorageClear(JSContext* ctx, JSValueConst, int,
                                  JSValueConst* argv) {
    Policy* held = nullptr;
    bool session = JS_ToBool(ctx, argv[0]) != 0;
    shell::Storage* storage = AllowedStorage(ctx, session, &held);
    if (!storage) return JS_EXCEPTION;
    Str error;
    bool ok = storage->Clear(&error);
    if (ok && !session) {
        DriveStorage(held ? held : shell::ScopeCurrentPolicy());
    }
    JSValue result =
        ok ? JS_UNDEFINED
           : JS_ThrowInternalError(ctx, "%.*s", error.len, error.s);
    StrFree(error);
    PolicyRelease(held);
    return result;
}

static JSValue NativeStorageLength(JSContext* ctx, JSValueConst, int,
                                   JSValueConst* argv) {
    Policy* held = nullptr;
    bool session = JS_ToBool(ctx, argv[0]) != 0;
    shell::Storage* storage = AllowedStorage(ctx, session, &held);
    if (!storage) return JS_EXCEPTION;
    JSValue result = JS_NewInt32(ctx, storage->Len());
    PolicyRelease(held);
    return result;
}

static JSValue NativeStorageKey(JSContext* ctx, JSValueConst, int argc,
                                JSValueConst* argv) {
    Policy* held = nullptr;
    bool session = argc > 1 && JS_ToBool(ctx, argv[1]) != 0;
    shell::Storage* storage = AllowedStorage(ctx, session, &held);
    if (!storage) return JS_EXCEPTION;
    int32_t index = -1;
    bool ok = argc >= 1 && JS_ToInt32(ctx, &index, argv[0]) == 0;
    Str key = ok ? storage->Key(index) : Str{};
    JSValue result = !ok   ? JS_EXCEPTION
                     : key ? JS_NewStringLen(ctx, key.s, (size_t)key.len)
                           : JS_NULL;
    PolicyRelease(held);
    return result;
}

static JSValue ResolvedPromise(JSContext* ctx) {
    JSValue resolving[2] = {JS_UNDEFINED, JS_UNDEFINED};
    JSValue promise = JS_NewPromiseCapability(ctx, resolving);
    if (!JS_IsException(promise)) {
        JSValue settled = JS_Call(ctx, resolving[0], JS_UNDEFINED, 0, nullptr);
        JS_FreeValue(ctx, settled);
    }
    JS_FreeValue(ctx, resolving[0]);
    JS_FreeValue(ctx, resolving[1]);
    return promise;
}

static JSValue NativeStorageFlush(JSContext* ctx, JSValueConst, int,
                                  JSValueConst* argv) {
    Policy* held = nullptr;
    bool session = JS_ToBool(ctx, argv[0]) != 0;
    shell::Storage* storage = AllowedStorage(ctx, session, &held);
    if (!storage) return JS_EXCEPTION;
    if (session || (!storage->IsDirty() && !storage->HasWriteInFlight())) {
        PolicyRelease(held);
        return ResolvedPromise(ctx);
    }
    shell::ScopeHostContext host = shell::ScopeCurrentHost();
    if (!host.IsSet()) {
        PolicyRelease(held);
        return JS_ThrowTypeError(ctx,
                                 "localStorage.flush() needs a live host task");
    }
    JSValue resolving[2] = {JS_UNDEFINED, JS_UNDEFINED};
    JSValue promise = JS_NewPromiseCapability(ctx, resolving);
    if (JS_IsException(promise)) {
        PolicyRelease(held);
        return JS_EXCEPTION;
    }
    ShellRuntimeImpl* impl = (ShellRuntimeImpl*)JS_GetContextOpaque(ctx);
    uint32_t taskId =
        NewTask(impl, ShellTaskKind::StorageFlush, resolving[0], host.GetApp(),
                host.GetWindow(), false, resolving[1]);
    JS_FreeValue(ctx, resolving[0]);
    JS_FreeValue(ctx, resolving[1]);
    if (!taskId) {
        JS_FreeValue(ctx, promise);
        PolicyRelease(held);
        return JS_ThrowRangeError(
            ctx, "the runtime reached its outstanding host task limit");
    }
    StorageFlushState* state = new StorageFlushState();
    state->control = ShellRuntimeAccess::Control(impl->owner);
    ControlRetain(state->control);
    state->task = taskId;
    state->storage = storage;
    ShellTask* task = FindTask(impl, taskId);
    task->storageFlush = state;
    Str error;
    bool immediate = false;
    bool ok = storage->Wait(MkFunc1(StorageFlushDone, state), &state->waiter,
                            &immediate, &error);
    if (!ok) {
        task->storageFlush = nullptr;
        ForgetTask(impl, taskId, false);
        ControlRelease(state->control);
        delete state;
        JS_FreeValue(ctx, promise);
        JSValue result = JS_ThrowInternalError(ctx, "%.*s", error.len, error.s);
        StrFree(error);
        PolicyRelease(held);
        return result;
    }
    StrFree(error);
    if (immediate) {
        StorageFlushDone(state, shell::StorageOutcome{true, {}});
    } else {
        DriveStorage(held ? held : shell::ScopeCurrentPolicy());
    }
    PolicyRelease(held);
    return promise;
}

static JSValue NativeClipboard(JSContext* ctx, JSValueConst, int argc,
                               JSValueConst* argv, int magic) {
    bool release = false;
    Policy* policy = CurrentPolicy(&release);
    const Capabilities& capabilities = PolicyCapabilities(policy);
    bool allowed = magic == 0 ? capabilities.IsClipboardReadable()
                              : capabilities.IsClipboardWritable();
    if (!allowed) {
        if (release) PolicyRelease(policy);
        return JS_ThrowTypeError(
            ctx, magic == 0 ? "reading the clipboard is not granted; declare "
                              "capabilities.clipboard.read"
                            : "writing the clipboard is not granted; declare "
                              "capabilities.clipboard.write");
    }
    shell::ScopeHostContext host = shell::ScopeCurrentHost();
    if (!host.IsSet()) {
        if (release) PolicyRelease(policy);
        return JS_ThrowTypeError(ctx,
                                 "clipboard access needs a live host call");
    }
    JSValue result = JS_UNDEFINED;
    if (magic == 0) {
        Arena* arena = ArenaNew();
        Str value = ClipboardGetText(arena, host.GetWindow());
        result =
            value ? JS_NewStringLen(ctx, value.s, (size_t)value.len) : JS_NULL;
        ArenaDelete(arena);
    } else {
        Arena* arena = ArenaNew();
        Str value;
        bool ok = argc >= 1 && JsString(ctx, argv[0], arena, &value);
        if (ok) ClipboardSetText(host.GetWindow(), value);
        ArenaDelete(arena);
        if (!ok) result = JS_EXCEPTION;
    }
    if (release) PolicyRelease(policy);
    return result;
}

static JSValue NativeConsole(JSContext* ctx, JSValueConst, int argc,
                             JSValueConst* argv, int magic) {
    StrBuilder out;
    static const char* levels[] = {"log", "debug", "info", "warn", "error"};
    out.Append(
        fmt("[script %s]", Str(levels[magic >= 0 && magic < 5 ? magic : 0])));
    for (int i = 0; i < argc; i++) {
        size_t n = 0;
        const char* value = JS_ToCStringLen(ctx, &n, argv[i]);
        out.AppendChar(' ');
        if (value) {
            out.Append(Str(value, (int)n));
            JS_FreeCString(ctx, value);
        } else {
            out.Append(StrL("<value>"));
            JSValue exception = JS_GetException(ctx);
            JS_FreeValue(ctx, exception);
        }
    }
    Str message = out.TakeStr();
    log(message);
    StrFree(message);
    return JS_UNDEFINED;
}

constexpr int kHostBridgeMaxDepth = 16;
constexpr int kHostBridgeMaxItems = 10000;

static bool HostFromJs(JSContext* ctx, JSValueConst value, int depth,
                       HostValue* out) {
    if (depth > kHostBridgeMaxDepth) {
        JS_ThrowTypeError(
            ctx, "a host argument may not nest more than 16 levels deep");
        return false;
    }
    if (JS_IsNull(value) || JS_IsUndefined(value)) {
        out->SetNull();
        return true;
    }
    if (JS_IsBool(value)) {
        out->SetBool(JS_ToBool(ctx, value) != 0);
        return true;
    }
    if (JS_IsNumber(value)) {
        double number = 0;
        if (JS_ToFloat64(ctx, &number, value) < 0) return false;
        out->SetNumber(number);
        return true;
    }
    if (JS_IsString(value)) {
        size_t len = 0;
        const char* text = JS_ToCStringLen(ctx, &len, value);
        if (!text) return false;
        bool ok = len <= (size_t)INT_MAX && out->SetString(Str(text, (int)len));
        JS_FreeCString(ctx, text);
        if (!ok) JS_ThrowOutOfMemory(ctx);
        return ok;
    }
    if (JS_IsArray(value)) {
        int64_t count = 0;
        if (JS_GetLength(ctx, value, &count) < 0) return false;
        if (count < 0 || count > kHostBridgeMaxItems) {
            JS_ThrowRangeError(ctx, "host arrays accept at most 10000 items");
            return false;
        }
        out->Free();
        out->kind = HostValueKind::Array;
        if (!VecReserve(out->array, (int)count)) {
            JS_ThrowOutOfMemory(ctx);
            return false;
        }
        for (int64_t i = 0; i < count; i++) {
            JSValue item = JS_GetPropertyUint32(ctx, value, (uint32_t)i);
            HostValue* converted = new HostValue();
            bool ok = !JS_IsException(item) &&
                      HostFromJs(ctx, item, depth + 1, converted) &&
                      VecAppend(out->array, converted);
            JS_FreeValue(ctx, item);
            if (!ok) {
                converted->Free();
                delete converted;
                if (!JS_HasException(ctx)) JS_ThrowOutOfMemory(ctx);
                return false;
            }
        }
        return true;
    }
    if (JS_IsFunction(ctx, value)) {
        JS_ThrowTypeError(ctx,
                          "a host function cannot be passed a callback; host "
                          "calls take and return plain data only");
        return false;
    }
    if (JS_IsObject(value)) {
        JSPropertyEnum* properties = nullptr;
        uint32_t count = 0;
        if (JS_GetOwnPropertyNames(ctx, &properties, &count, value,
                                   JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) < 0)
            return false;
        if (count > kHostBridgeMaxItems) {
            JS_FreePropertyEnum(ctx, properties, count);
            JS_ThrowRangeError(ctx, "host objects accept at most 10000 fields");
            return false;
        }
        out->Free();
        out->kind = HostValueKind::Object;
        bool ok = VecReserve(out->object, (int)count) != nullptr;
        for (uint32_t i = 0; ok && i < count; i++) {
            const char* name = JS_AtomToCString(ctx, properties[i].atom);
            JSValue item = JS_GetProperty(ctx, value, properties[i].atom);
            HostField field;
            field.name = name ? StrDup(Str(name)) : Str{};
            field.value = new HostValue();
            ok = name && !JS_IsException(item) && field.name.s &&
                 HostFromJs(ctx, item, depth + 1, field.value) &&
                 VecAppend(out->object, field);
            if (name) JS_FreeCString(ctx, name);
            JS_FreeValue(ctx, item);
            if (!ok) {
                StrFree(field.name);
                field.value->Free();
                delete field.value;
            }
        }
        JS_FreePropertyEnum(ctx, properties, count);
        if (!ok && !JS_HasException(ctx)) JS_ThrowOutOfMemory(ctx);
        return ok;
    }
    JS_ThrowTypeError(ctx,
                      "unsupported host argument; expected null, a boolean, a "
                      "number, a string, an array or a plain object");
    return false;
}

static bool HostArgumentsFromJs(JSContext* ctx, JSValueConst array,
                                HostArguments* arguments) {
    if (!JS_IsArray(array)) {
        JS_ThrowTypeError(ctx, "host call arguments must be an array");
        return false;
    }
    int64_t count = 0;
    if (JS_GetLength(ctx, array, &count) < 0) return false;
    if (count < 0 || count > kHostBridgeMaxItems) {
        JS_ThrowRangeError(ctx, "host calls accept at most 10000 arguments");
        return false;
    }
    if (!VecReserve(arguments->values, (int)count)) {
        JS_ThrowOutOfMemory(ctx);
        return false;
    }
    for (int64_t i = 0; i < count; i++) {
        JSValue item = JS_GetPropertyUint32(ctx, array, (uint32_t)i);
        HostValue* value = new HostValue();
        bool ok = !JS_IsException(item) && HostFromJs(ctx, item, 0, value) &&
                  VecAppend(arguments->values, value);
        JS_FreeValue(ctx, item);
        if (!ok) {
            value->Free();
            delete value;
            if (!JS_HasException(ctx)) JS_ThrowOutOfMemory(ctx);
            return false;
        }
    }
    return true;
}

static JSValue HostIntoJs(JSContext* ctx, const HostValue& value,
                          int depth = 0) {
    if (depth > kHostBridgeMaxDepth)
        return JS_ThrowRangeError(
            ctx, "a host result may not nest more than 16 levels deep");
    switch (value.kind) {
        case HostValueKind::Null:
            return JS_NULL;
        case HostValueKind::Bool:
            return JS_NewBool(ctx, value.boolean);
        case HostValueKind::Number:
            return JS_NewFloat64(ctx, value.number);
        case HostValueKind::String:
            return JS_NewStringLen(ctx, value.string.s ? value.string.s : "",
                                   (size_t)value.string.len);
        case HostValueKind::Array: {
            if (value.array.len > kHostBridgeMaxItems)
                return JS_ThrowRangeError(
                    ctx, "host result array exceeds 10000 items");
            JSValue array = JS_NewArray(ctx);
            for (int i = 0; i < value.array.len; i++) {
                JSValue item = value.array[i]
                                   ? HostIntoJs(ctx, *value.array[i], depth + 1)
                                   : JS_NULL;
                if (JS_IsException(item) ||
                    JS_SetPropertyUint32(ctx, array, (uint32_t)i, item) < 0) {
                    JS_FreeValue(ctx, array);
                    return JS_EXCEPTION;
                }
            }
            return array;
        }
        case HostValueKind::Object: {
            if (value.object.len > kHostBridgeMaxItems)
                return JS_ThrowRangeError(
                    ctx, "host result object exceeds 10000 fields");
            JSValue object = JS_NewObject(ctx);
            for (int i = 0; i < value.object.len; i++) {
                const HostField& field = value.object[i];
                JSValue item = field.value
                                   ? HostIntoJs(ctx, *field.value, depth + 1)
                                   : JS_NULL;
                if (JS_IsException(item) ||
                    JS_SetPropertyStr(ctx, object,
                                      field.name.s ? field.name.s : "",
                                      item) < 0) {
                    JS_FreeValue(ctx, object);
                    return JS_EXCEPTION;
                }
            }
            return object;
        }
    }
    return JS_NULL;
}

static bool HostNames(JSContext* ctx, int argc, JSValueConst* argv,
                      Arena* arena, Str* module, Str* function) {
    return argc >= 3 && JsString(ctx, argv[0], arena, module) &&
           JsString(ctx, argv[1], arena, function);
}

static JSValue NativeHostCall(JSContext* ctx, JSValueConst, int argc,
                              JSValueConst* argv) {
    Arena* arena = ArenaNew();
    Str module;
    Str function;
    HostArguments arguments;
    bool ok = HostNames(ctx, argc, argv, arena, &module, &function) &&
              HostArgumentsFromJs(ctx, argv[2], &arguments);
    if (!ok) {
        arguments.Free();
        ArenaDelete(arena);
        return JS_EXCEPTION;
    }
    HostCall call;
    call.arguments = &arguments;
    ShellRuntimeImpl* impl = (ShellRuntimeImpl*)JS_GetContextOpaque(ctx);
    shell::MetricsTimer timer =
        shell::MetricsBegin(&impl->metrics, shell::MetricsTimerKind::Native);
    ok = HostDispatch(module, function, &call);
    shell::MetricsEnd(&timer);
    JSValue result = JS_EXCEPTION;
    if (!ok) {
        result = JS_ThrowTypeError(
            ctx, "`%.*s.%.*s`: %.*s", module.len, module.s, function.len,
            function.s, call.error.message.len,
            call.error.message.s ? call.error.message.s : "host call failed");
    } else {
        result = HostIntoJs(ctx, call.result);
    }
    call.result.Free();
    call.error.Clear();
    arguments.Free();
    ArenaDelete(arena);
    return result;
}

static void HostAsyncWork(HostAsyncJob* job) {
    HostCall call;
    call.arguments = &job->arguments;
    job->work.Call(&call);
    job->result.kind = call.result.kind;
    job->result.boolean = call.result.boolean;
    job->result.number = call.result.number;
    job->result.string = call.result.string;
    call.result.string = {};
    job->result.array.els = call.result.array.els;
    job->result.array.len = call.result.array.len;
    job->result.array.cap = call.result.array.cap;
    call.result.array.els = nullptr;
    call.result.array.len = 0;
    call.result.array.cap = 0;
    job->result.object.els = call.result.object.els;
    job->result.object.len = call.result.object.len;
    job->result.object.cap = call.result.object.cap;
    call.result.object.els = nullptr;
    call.result.object.len = 0;
    call.result.object.cap = 0;
    job->error = call.error;
    call.error = {};
}

static JSValue HostAsyncResolved(ShellRuntimeImpl* impl, void* user) {
    HostAsyncJob* job = (HostAsyncJob*)user;
    return HostIntoJs(impl->context, job->result);
}

static void HostAsyncDestroy(void* user) {
    HostAsyncJob* self = (HostAsyncJob*)user;
    self->Free();
    delete self;
}

// A host module's asynchronous half, awaited. The registry stays retained for
// the length of the call by the job itself, which the lease frees on every
// path out.
static Task ShellHostAsyncTask(TaskGuard guard, HostAsyncJob* job) {
    (void)guard;
    ShellTaskLease lease{&job->head, job, HostAsyncDestroy};
    co_await BackgroundSpawn(MkFunc0(HostAsyncWork, job));
    SettleShellTask(&lease, job->error.IsSet(), job->error.message,
                    HostAsyncResolved, job);
}

static JSValue NativeHostAsyncCall(JSContext* ctx, JSValueConst, int argc,
                                   JSValueConst* argv) {
    Arena* arena = ArenaNew();
    Str module;
    Str function;
    HostArguments arguments;
    bool ok = HostNames(ctx, argc, argv, arena, &module, &function) &&
              HostArgumentsFromJs(ctx, argv[2], &arguments);
    if (!ok) {
        arguments.Free();
        ArenaDelete(arena);
        return JS_EXCEPTION;
    }
    HostAsyncRequest request;
    request.arguments = &arguments;
    ShellRuntimeImpl* impl = (ShellRuntimeImpl*)JS_GetContextOpaque(ctx);
    shell::MetricsTimer timer =
        shell::MetricsBegin(&impl->metrics, shell::MetricsTimerKind::Native);
    ok = HostDispatchBegin(module, function, &request);
    shell::MetricsEnd(&timer);
    if (!ok) {
        JSValue result = JS_ThrowTypeError(
            ctx, "`%.*s.%.*s`: %.*s", module.len, module.s, function.len,
            function.s, request.error.message.len,
            request.error.message.s ? request.error.message.s
                                    : "host call failed");
        request.error.Clear();
        request.release.Call();
        HostModulesRelease(request.registry);
        arguments.Free();
        ArenaDelete(arena);
        return result;
    }
    shell::ScopeHostContext host = shell::ScopeCurrentHost();
    if (!host.IsSet()) {
        request.release.Call();
        HostModulesRelease(request.registry);
        arguments.Free();
        ArenaDelete(arena);
        return JS_ThrowTypeError(
            ctx, "an asynchronous HostModule call needs a live host task");
    }

    HostAsyncJob* job = new HostAsyncJob();
    job->arguments.values.els = arguments.values.els;
    job->arguments.values.len = arguments.values.len;
    job->arguments.values.cap = arguments.values.cap;
    arguments.values.els = nullptr;
    arguments.values.len = 0;
    arguments.values.cap = 0;
    job->work = request.work;
    job->release = request.release;
    job->registry = request.registry;
    request.registry = nullptr;
    request.error.Clear();
    ArenaDelete(arena);

    JSValue resolving[2] = {JS_UNDEFINED, JS_UNDEFINED};
    JSValue promise = JS_NewPromiseCapability(ctx, resolving);
    if (JS_IsException(promise)) {
        job->Free();
        delete job;
        return JS_EXCEPTION;
    }
    uint32_t task =
        NewTask(impl, ShellTaskKind::HostAsync, resolving[0], host.GetApp(),
                host.GetWindow(), false, resolving[1]);
    JS_FreeValue(ctx, resolving[0]);
    JS_FreeValue(ctx, resolving[1]);
    if (!task) {
        JS_FreeValue(ctx, promise);
        job->Free();
        delete job;
        return JS_ThrowRangeError(
            ctx, "the runtime reached its outstanding host task limit");
    }
    job->head.control = ShellRuntimeAccess::Control(impl->owner);
    ControlRetain(job->head.control);
    job->head.task = task;
    job->head.kind = ShellTaskKind::HostAsync;
    TaskGuard guard;
    guard.alive = ShellTaskOwnerAlive;
    guard.user = &job->head;
    Task work = ShellHostAsyncTask(guard, job);
    if (!work.IsRunning()) {
        JS_FreeValue(ctx, promise);
        return JS_ThrowInternalError(
            ctx, "asynchronous HostModule work could not start");
    }
    return promise;
}

static bool ObjectOnlyOption(JSContext* ctx, JSValueConst object, Str allowed,
                             const char* what) {
    JSPropertyEnum* properties = nullptr;
    uint32_t count = 0;
    if (JS_GetOwnPropertyNames(ctx, &properties, &count, object,
                               JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) < 0) {
        return false;
    }
    bool ok = true;
    for (uint32_t i = 0; i < count; i++) {
        const char* name = JS_AtomToCString(ctx, properties[i].atom);
        bool matches = name && StrEq(Str(name), allowed);
        if (!matches) {
            JS_ThrowTypeError(ctx, "unknown option `%s` for %s; expected %s",
                              name ? name : "<symbol>", what, allowed.s);
            ok = false;
        }
        if (name) JS_FreeCString(ctx, name);
        if (!ok) break;
    }
    JS_FreePropertyEnum(ctx, properties, count);
    return ok;
}

static bool FsReadTextOption(JSContext* ctx, JSValueConst value, Arena* arena,
                             bool* text) {
    *text = false;
    if (JS_IsUndefined(value) || JS_IsNull(value)) return true;
    JSValue encoding = JS_UNDEFINED;
    if (JS_IsString(value)) {
        encoding = JS_DupValue(ctx, value);
    } else if (JS_IsObject(value)) {
        if (!ObjectOnlyOption(ctx, value, StrL("encoding"),
                              "fs.readFile options"))
            return false;
        encoding = JS_GetPropertyStr(ctx, value, "encoding");
    } else {
        JS_ThrowTypeError(
            ctx,
            "fs.readFile encoding must be \"utf8\" or { encoding: \"utf8\" }");
        return false;
    }
    Str name;
    bool ok =
        !JS_IsException(encoding) && JsString(ctx, encoding, arena, &name);
    JS_FreeValue(ctx, encoding);
    if (!ok) return false;
    if (!StrEqI(name, StrL("utf8")) && !StrEqI(name, StrL("utf-8"))) {
        JS_ThrowTypeError(ctx, "fs.readFile only supports UTF-8 text decoding");
        return false;
    }
    *text = true;
    return true;
}

static bool FsBoolOption(JSContext* ctx, JSValueConst value, const char* key,
                         const char* what, bool* result) {
    *result = false;
    if (JS_IsUndefined(value) || JS_IsNull(value)) return true;
    if (!JS_IsObject(value)) {
        JS_ThrowTypeError(ctx, "%s expects an options object", what);
        return false;
    }
    if (!ObjectOnlyOption(ctx, value, Str(key), what)) return false;
    JSValue option = JS_GetPropertyStr(ctx, value, key);
    if (JS_IsException(option)) return false;
    if (!JS_IsUndefined(option) && !JS_IsBool(option)) {
        JS_FreeValue(ctx, option);
        JS_ThrowTypeError(ctx, "%s.%s must be boolean", what, key);
        return false;
    }
    if (!JS_IsUndefined(option)) *result = JS_ToBool(ctx, option) != 0;
    JS_FreeValue(ctx, option);
    return true;
}

// __fetch_send(url, method, headers, body): the whole of `fetch`'s request
// surface. `headers` is a flat [name, value, ...] array, which is what the
// prelude has already validated into shape; `body` is a string or a
// Uint8Array, and undefined for a request that carries none.
static JSValue NativeFetch(JSContext* ctx, JSValueConst, int argc,
                           JSValueConst* argv) {
    Arena* arena = ArenaNew();
    Str url;
    bool converted = argc >= 1 && JsString(ctx, argv[0], arena, &url);
    Str method = StrL("GET");
    if (converted && argc >= 2 && !JS_IsUndefined(argv[1])) {
        converted = JsString(ctx, argv[1], arena, &method);
    }
    if (!converted) {
        ArenaDelete(arena);
        return JS_EXCEPTION;
    }
    if (!shell::FetchIsHttpMethod(method)) {
        JSValue thrown = JS_ThrowTypeError(
            ctx, "fetch(url, options).method `%.*s` is not an HTTP method",
            method.len, method.s ? method.s : "");
        ArenaDelete(arena);
        return thrown;
    }

    shell::FetchRequest request;
    request.url = StrDup(url);
    // Upper-cased on the way through, the way `fetch` does with a known
    // method, so a policy written in the ordinary spelling matches.
    Str upper = StrDup(method);
    for (int i = 0; i < upper.len; i++) {
        if (upper.s[i] >= 'a' && upper.s[i] <= 'z') {
            upper.s[i] = (char)(upper.s[i] - 'a' + 'A');
        }
    }
    request.method = upper;
    bool ok = request.url.s && request.method.s;

    if (ok && argc >= 3 && JS_IsArray(argv[2])) {
        int64_t count = 0;
        JSValue lengthValue = JS_GetPropertyStr(ctx, argv[2], "length");
        if (JS_ToInt64(ctx, &count, lengthValue) < 0) count = 0;
        JS_FreeValue(ctx, lengthValue);
        for (int64_t i = 0; ok && i + 1 < count; i += 2) {
            JSValue nameValue = JS_GetPropertyUint32(ctx, argv[2], (uint32_t)i);
            JSValue valueValue =
                JS_GetPropertyUint32(ctx, argv[2], (uint32_t)(i + 1));
            Str name;
            Str value;
            if (JsString(ctx, nameValue, arena, &name) &&
                JsString(ctx, valueValue, arena, &value)) {
                if (shell::FetchHeaderIsProhibited(name)) {
                    JS_FreeValue(ctx, nameValue);
                    JS_FreeValue(ctx, valueValue);
                    JSValue thrown = JS_ThrowTypeError(
                        ctx, "fetch(url, options).headers may not set `%.*s`",
                        name.len, name.s ? name.s : "");
                    request.Free();
                    ArenaDelete(arena);
                    return thrown;
                }
                shell::FetchHeader header;
                header.name = StrDup(name);
                header.value = StrDup(value);
                if (header.name.s && header.value.s) {
                    VecAppend(request.headers, header);
                } else {
                    StrFree(header.name);
                    StrFree(header.value);
                    ok = false;
                }
            } else {
                ok = false;
            }
            JS_FreeValue(ctx, nameValue);
            JS_FreeValue(ctx, valueValue);
        }
    }

    if (ok && argc >= 4 && !JS_IsUndefined(argv[3]) && !JS_IsNull(argv[3])) {
        if (JS_IsString(argv[3])) {
            Str text;
            if (JsString(ctx, argv[3], arena, &text)) {
                request.body = StrDup(text);
                ok = request.body.s || text.len == 0;
            } else {
                ok = false;
            }
        } else {
            size_t count = 0;
            uint8_t* bytes = JS_GetUint8Array(ctx, &count, argv[3]);
            if (!bytes) {
                request.Free();
                ArenaDelete(arena);
                return JS_EXCEPTION;
            }
            request.body = StrDup(Str((const char*)bytes, (int)count));
            ok = request.body.s || count == 0;
        }
    }
    if (ok && request.body.len > shell::kFetchMaxRequestBody) {
        JSValue thrown = JS_ThrowRangeError(
            ctx, "fetch request body exceeded the %d byte limit",
            shell::kFetchMaxRequestBody);
        request.Free();
        ArenaDelete(arena);
        return thrown;
    }
    if (!ok) {
        request.Free();
        ArenaDelete(arena);
        return JS_ThrowOutOfMemory(ctx);
    }

    bool release = false;
    Policy* policy = CurrentPolicy(&release);
    Str authorizationError;
    bool allowed =
        shell::FetchAuthorize(request.url, request.method,
                              PolicyCapabilities(policy), &authorizationError);
    if (!allowed) {
        JSValue result = JS_ThrowTypeError(ctx, "%.*s", authorizationError.len,
                                           authorizationError.s
                                               ? authorizationError.s
                                               : "fetch URL is not granted");
        StrFree(authorizationError);
        if (release) PolicyRelease(policy);
        request.Free();
        ArenaDelete(arena);
        return result;
    }
    shell::ScopeHostContext host = shell::ScopeCurrentHost();
    if (!host.IsSet()) {
        if (release) PolicyRelease(policy);
        request.Free();
        ArenaDelete(arena);
        return JS_ThrowTypeError(ctx, "fetch() needs a live host task");
    }

    ShellFetchJob* job = new ShellFetchJob();
    // The job takes the request whole. The local's owning fields are then
    // cleared rather than freed: VecTake hands the header storage over
    // without copying it, and VecReset here would free what the job holds.
    job->request = request;
    request.url = {};
    request.method = {};
    request.body = {};
    VecTake(request.headers);
    job->capabilities = PolicyCapabilities(policy);
    if (release) PolicyRelease(policy);
    ArenaDelete(arena);

    JSValue resolving[2] = {JS_UNDEFINED, JS_UNDEFINED};
    JSValue promise = JS_NewPromiseCapability(ctx, resolving);
    if (JS_IsException(promise)) {
        job->Free();
        delete job;
        return JS_EXCEPTION;
    }
    ShellRuntimeImpl* impl = (ShellRuntimeImpl*)JS_GetContextOpaque(ctx);
    uint32_t task =
        NewTask(impl, ShellTaskKind::Fetch, resolving[0], host.GetApp(),
                host.GetWindow(), false, resolving[1]);
    JS_FreeValue(ctx, resolving[0]);
    JS_FreeValue(ctx, resolving[1]);
    if (!task) {
        JS_FreeValue(ctx, promise);
        job->Free();
        delete job;
        return JS_ThrowRangeError(
            ctx, "the runtime reached its outstanding host task limit");
    }
    job->head.control = ShellRuntimeAccess::Control(impl->owner);
    ControlRetain(job->head.control);
    job->head.task = task;
    job->head.kind = ShellTaskKind::Fetch;
#if GPUI_OS_WASM
    if (!shell::FetchSendAsync(job->request, job->capabilities,
                               MkFunc1(ShellFetchDone, job))) {
        ForgetTask(impl, task, false);
        ControlRelease(job->head.control);
        job->Free();
        delete job;
        JS_FreeValue(ctx, promise);
        return JS_ThrowInternalError(ctx,
                                     "fetch could not start asynchronous work");
    }
#else
    TaskGuard guard;
    guard.alive = ShellTaskOwnerAlive;
    guard.user = &job->head;
    Task work = ShellFetchTask(guard, job);
    if (!work.IsRunning()) {
        JS_FreeValue(ctx, promise);
        return JS_ThrowInternalError(ctx,
                                     "fetch could not start background work");
    }
#endif
    return promise;
}

static JSValue NativeFs(JSContext* ctx, JSValueConst, int argc,
                        JSValueConst* argv, int magic) {
    if (magic < 0 || magic > 6)
        return JS_ThrowInternalError(ctx, "invalid filesystem operation");
    shell::FsOperation operation = (shell::FsOperation)magic;
    CapabilityAccess access =
        operation == shell::FsOperation::Read ||
                operation == shell::FsOperation::ReadDirectory ||
                operation == shell::FsOperation::Exists
            ? CapabilityAccess::Read
            : CapabilityAccess::Write;
    Arena* arena = ArenaNew();
    Str requested;
    bool ok = argc >= 1 && JsString(ctx, argv[0], arena, &requested);
    if (!ok) {
        ArenaDelete(arena);
        return JS_EXCEPTION;
    }
    bool release = false;
    Policy* policy = CurrentPolicy(&release);
    CapabilityPath path;
    CapabilityError capabilityError;
    ok = PolicyCapabilities(policy)
             .ResolvePath(requested, access, &path, &capabilityError);
    if (release) PolicyRelease(policy);
    if (!ok) {
        Str message = CapabilityErrorMessage(arena, capabilityError);
        JSValue result = JS_ThrowTypeError(ctx, "%.*s", message.len, message.s);
        CapabilityErrorFree(&capabilityError);
        ArenaDelete(arena);
        return result;
    }

    FsJob* job = new FsJob();
    job->operation = operation;
    job->path = path;
    if (operation == shell::FsOperation::Read) {
        ok = FsReadTextOption(ctx, argc > 1 ? argv[1] : JS_UNDEFINED, arena,
                              &job->text);
    } else if (operation == shell::FsOperation::ReadDirectory) {
        ok = FsBoolOption(ctx, argc > 1 ? argv[1] : JS_UNDEFINED,
                          "withFileTypes", "fs.readdir(path, options)",
                          &job->withFileTypes);
    } else if (operation == shell::FsOperation::MakeDirectory) {
        ok = FsBoolOption(ctx, argc > 1 ? argv[1] : JS_UNDEFINED, "recursive",
                          "fs.mkdir(path, options)", &job->recursive);
    } else if (operation == shell::FsOperation::Write) {
        if (argc < 2 ||
            (!JS_IsString(argv[1]) &&
             JS_GetTypedArrayType(argv[1]) != JS_TYPED_ARRAY_UINT8)) {
            JS_ThrowTypeError(
                ctx,
                "fs.writeFile(path, contents) expects a string or Uint8Array");
            ok = false;
        } else if (JS_IsString(argv[1])) {
            Str input;
            ok = JsString(ctx, argv[1], arena, &input);
            if (ok) job->input = StrDup(input);
        } else {
            size_t count = 0;
            uint8_t* bytes = JS_GetUint8Array(ctx, &count, argv[1]);
            ok = bytes != nullptr || count == 0;
            if (ok && count <= (size_t)INT_MAX) {
                job->input = StrDup(Str((const char*)bytes, (int)count));
            } else if (ok) {
                JS_ThrowRangeError(ctx, "fs.writeFile contents are too large");
                ok = false;
            }
        }
        if (ok && job->input.len > shell::kFsMaxWriteBytes) {
            JS_ThrowRangeError(
                ctx,
                "fs.writeFile contents exceed the 8388608-byte write limit");
            ok = false;
        }
        if (ok && !job->input.s && job->input.len != 0) {
            JS_ThrowOutOfMemory(ctx);
            ok = false;
        }
    }
    ArenaDelete(arena);
    if (!ok) {
        job->Free();
        delete job;
        return JS_EXCEPTION;
    }

    shell::ScopeHostContext host = shell::ScopeCurrentHost();
    if (!host.IsSet()) {
        job->Free();
        delete job;
        return JS_ThrowTypeError(ctx,
                                 "filesystem access needs a live host task");
    }
    JSValue resolving[2] = {JS_UNDEFINED, JS_UNDEFINED};
    JSValue promise = JS_NewPromiseCapability(ctx, resolving);
    if (JS_IsException(promise)) {
        job->Free();
        delete job;
        return JS_EXCEPTION;
    }
    ShellRuntimeImpl* impl = (ShellRuntimeImpl*)JS_GetContextOpaque(ctx);
    uint32_t task =
        NewTask(impl, ShellTaskKind::Filesystem, resolving[0], host.GetApp(),
                host.GetWindow(), false, resolving[1]);
    JS_FreeValue(ctx, resolving[0]);
    JS_FreeValue(ctx, resolving[1]);
    if (!task) {
        JS_FreeValue(ctx, promise);
        job->Free();
        delete job;
        return JS_ThrowRangeError(
            ctx, "the runtime reached its outstanding host task limit");
    }
    job->head.control = ShellRuntimeAccess::Control(impl->owner);
    ControlRetain(job->head.control);
    job->head.task = task;
    job->head.kind = ShellTaskKind::Filesystem;
    ShellTask* shellTask = FindTask(impl, task);
    shellTask->fsJob = job;
    TaskGuard guard;
    guard.alive = ShellTaskOwnerAlive;
    guard.user = &job->head;
    Task work = ShellFsTask(guard, job);
    if (!work.IsRunning()) {
        JS_FreeValue(ctx, promise);
        return JS_ThrowInternalError(ctx, "fs could not start background work");
    }
    return promise;
}

static JSValue NativeProcessRun(JSContext* ctx, JSValueConst, int argc,
                                JSValueConst* argv) {
    Arena* arena = ArenaNew();
    Str command;
    bool converted = argc >= 1 && JsString(ctx, argv[0], arena, &command);
    bool release = false;
    Policy* policy = CurrentPolicy(&release);
    bool allowed = converted && PolicyCapabilities(policy).MayRun(command);
    if (release) PolicyRelease(policy);
    if (converted && !allowed) {
        JSValue result = JS_ThrowTypeError(
            ctx,
            "running `%.*s` is not granted; add it to capabilities.fs.execute",
            command.len, command.s);
        ArenaDelete(arena);
        return result;
    }
    if (!converted) {
        ArenaDelete(arena);
        return JS_EXCEPTION;
    }
    shell::ScopeHostContext host = shell::ScopeCurrentHost();
    if (!host.IsSet()) {
        ArenaDelete(arena);
        return JS_ThrowTypeError(ctx, "process.run() needs a live host task");
    }

    ProcessJob* job = new ProcessJob();
    job->command = StrDup(command);
    bool ok = job->command.s != nullptr;
    int64_t argCount = 0;
    if (ok && argc > 1 && !JS_IsUndefined(argv[1]) && !JS_IsNull(argv[1])) {
        if (!JS_IsArray(argv[1]) || JS_GetLength(ctx, argv[1], &argCount) < 0) {
            JS_ThrowTypeError(ctx,
                              "process.run(command, args) expects args to be "
                              "an array of strings");
            ok = false;
        } else if (argCount < 0 || argCount > 4096) {
            JS_ThrowRangeError(
                ctx,
                "process.run(command, args) accepts at most 4096 arguments");
            ok = false;
        }
    }
    int totalBytes = 0;
    for (int64_t i = 0; ok && i < argCount; i++) {
        JSValue value = JS_GetPropertyUint32(ctx, argv[1], (uint32_t)i);
        Str argument;
        bool stringOk =
            !JS_IsException(value) && JsString(ctx, value, arena, &argument);
        JS_FreeValue(ctx, value);
        if (!stringOk) {
            ok = false;
            break;
        }
        if (argument.len > 1024 * 1024 - totalBytes) {
            JS_ThrowRangeError(ctx,
                               "process.run arguments exceed the 1 MiB limit");
            ok = false;
            break;
        }
        Str copy = StrDup(argument);
        if (!copy.s || !VecAppend(job->args, copy)) {
            StrFree(copy);
            JS_ThrowOutOfMemory(ctx);
            ok = false;
            break;
        }
        totalBytes += argument.len;
    }
    ArenaDelete(arena);
    if (!ok) {
        job->Free();
        delete job;
        return JS_EXCEPTION;
    }

    JSValue resolving[2] = {JS_UNDEFINED, JS_UNDEFINED};
    JSValue promise = JS_NewPromiseCapability(ctx, resolving);
    if (JS_IsException(promise)) {
        job->Free();
        delete job;
        return JS_EXCEPTION;
    }
    ShellRuntimeImpl* impl = (ShellRuntimeImpl*)JS_GetContextOpaque(ctx);
    uint32_t task =
        NewTask(impl, ShellTaskKind::Process, resolving[0], host.GetApp(),
                host.GetWindow(), false, resolving[1]);
    JS_FreeValue(ctx, resolving[0]);
    JS_FreeValue(ctx, resolving[1]);
    if (!task) {
        JS_FreeValue(ctx, promise);
        job->Free();
        delete job;
        return JS_ThrowRangeError(
            ctx, "the runtime reached its outstanding host task limit");
    }
    job->head.control = ShellRuntimeAccess::Control(impl->owner);
    ControlRetain(job->head.control);
    job->head.task = task;
    job->head.kind = ShellTaskKind::Process;
    ShellTask* shellTask = FindTask(impl, task);
    shellTask->processJob = job;
    TaskGuard guard;
    guard.alive = ShellTaskOwnerAlive;
    guard.user = &job->head;
    Task work = ShellProcessTask(guard, job);
    if (!work.IsRunning()) {
        JS_FreeValue(ctx, promise);
        return JS_ThrowInternalError(
            ctx, "process.run could not start background work");
    }
    return promise;
}

static JSValue NativeProcessExit(JSContext* ctx, JSValueConst, int argc,
                                 JSValueConst* argv) {
    int32_t code = 0;
    if (argc > 0 && JS_ToInt32(ctx, &code, argv[0]) < 0) return JS_EXCEPTION;
    bool release = false;
    Policy* policy = CurrentPolicy(&release);
    bool allowed = PolicyCapabilities(policy).MayExit();
    if (release) PolicyRelease(policy);
    if (!allowed) {
        return JS_ThrowTypeError(ctx,
                                 "process.exit() is not granted; set "
                                 "capabilities.process.exit to true");
    }
    if (!gShellExitHandler) {
        return JS_ThrowInternalError(ctx,
                                     "process.exit() is granted but the host "
                                     "installed no ShellOnExitRequest handler");
    }
    shell::ScopeHostContext host = shell::ScopeCurrentHost();
    if (!host.IsSet())
        return JS_ThrowTypeError(ctx, "process.exit() needs a live host call");
    Ctx native = {};
    native.app = host.GetApp();
    native.win = host.GetWindow();
    native.a = host.GetWindow()->frameArena;
    native.self = shell::ScopeCurrentView();
    ShellExitRequest request = {code, native.self};
    gShellExitHandler(request, &native);
    return JS_UNDEFINED;
}

static bool StandardBytes(JSContext* ctx, JSValueConst value,
                          const char* operation, Str* bytes) {
    if (JS_GetTypedArrayType(value) != JS_TYPED_ARRAY_UINT8) {
        JS_ThrowTypeError(ctx, "%s expects a Uint8Array", operation);
        return false;
    }
    size_t count = 0;
    uint8_t* data = JS_GetUint8Array(ctx, &count, value);
    if ((!data && count != 0) || count > (size_t)shell::kStandardDataLimit) {
        JS_ThrowRangeError(ctx, "%s input exceeds the 64 MiB limit", operation);
        return false;
    }
    *bytes = Str((const char*)data, (int)count);
    return true;
}

static JSValue NativeSha256(JSContext* ctx, JSValueConst, int argc,
                            JSValueConst* argv) {
    Str input;
    if (argc < 1 || !StandardBytes(ctx, argv[0], "crypto.createHash", &input)) {
        return JS_EXCEPTION;
    }
    uint8_t digest[32];
    shell::Sha256(input, digest);
    return JS_NewUint8ArrayCopy(ctx, digest, sizeof(digest));
}

static JSValue NativeRandom(JSContext* ctx, JSValueConst, int argc,
                            JSValueConst* argv) {
    double requested = 0;
    if (argc < 1 || JS_ToFloat64(ctx, &requested, argv[0]) < 0) {
        return JS_EXCEPTION;
    }
    if (!isfinite(requested) || requested < 0 ||
        requested > shell::kStandardDataLimit ||
        requested != (double)(int)requested) {
        return JS_ThrowRangeError(ctx,
                                  "crypto.randomBytes size must be a whole "
                                  "number from 0 to 67108864");
    }
    int count = (int)requested;
    Vec<uint8_t> bytes;
    if ((count > 0 && !VecAppendBlanks(bytes, count)) ||
        !shell::SecureRandom(bytes.els, count)) {
        return JS_ThrowInternalError(
            ctx, "the platform secure random generator failed");
    }
    return JS_NewUint8ArrayCopy(ctx, bytes.els, (size_t)bytes.len);
}

static JSValue NativeZlib(JSContext* ctx, JSValueConst, int argc,
                          JSValueConst* argv, int magic) {
    static const char* names[4] = {"zlib.deflateSync", "zlib.inflateSync",
                                   "zlib.gzipSync", "zlib.gunzipSync"};
    if (magic < 0 || magic >= 4) {
        return JS_ThrowInternalError(ctx, "invalid compression operation");
    }
    Str input;
    if (argc < 1 || !StandardBytes(ctx, argv[0], names[magic], &input)) {
        return JS_EXCEPTION;
    }
    Str output;
    Str error;
    bool inflate = (magic & 1) != 0;
    bool gzip = magic >= 2;
    bool ok = inflate ? shell::ZlibInflate(input, gzip, &output, &error)
                      : shell::ZlibDeflate(input, gzip, &output, &error);
    if (!ok) {
        const char* message =
            error.s ? error.s : "compression operation failed";
        int messageLen = error.s ? error.len : (int)strlen(message);
        JSValue result = JS_ThrowTypeError(ctx, "%.*s", messageLen, message);
        StrFree(error);
        StrFree(output);
        return result;
    }
    JSValue result = JS_NewUint8ArrayCopy(
        ctx, (const uint8_t*)(output.s ? output.s : ""), (size_t)output.len);
    StrFree(error);
    StrFree(output);
    return result;
}

static void SetGlobalFunction(JSContext* ctx, JSValueConst global,
                              const char* name, JSCFunction* function,
                              int length) {
    JS_SetPropertyStr(ctx, global, name,
                      JS_NewCFunction(ctx, function, name, length));
}

static void SetGlobalMagicFunction(JSContext* ctx, JSValueConst global,
                                   const char* name, JSCFunctionMagic* function,
                                   int length, int magic) {
    JS_SetPropertyStr(ctx, global, name,
                      JS_NewCFunctionMagic(ctx, function, name, length,
                                           JS_CFUNC_generic_magic, magic));
}

static const char kPrelude[] = R"JS(
globalThis.__gpui = (() => {
  const explicit = Object.create(null);
  const element = (id) => {
    let object;
    const target = { __id: id };
    object = new Proxy(target, {
      get(receiver, name) {
        if (name in receiver) return receiver[name];
        if (name in explicit) return explicit[name].bind(object);
        if (typeof name !== "string") return undefined;
        return (...args) => { __apply(id, name, args); return object; };
      },
    });
    return object;
  };
  const childId = (child) => {
    if (typeof child?.__id === "number") return child.__id;
    if (["string", "number", "boolean"].includes(typeof child)) {
      return __component("text", String(child));
    }
    // A template's sentinel, reached only while a template body is running.
    // Checked after elements and strings so the ordinary description pays
    // nothing for it.
    if (child?.__slot !== undefined) return __text_slot(child.__slot);
    if (child?.__entity) return __child_view(child.__handle);
    throw new TypeError("child(value) expects an element, primitive text, or an entity from cx.new(Class, props)");
  };
  explicit.child = function (child) { __attach(this.__id, childId(child)); return this; };
  explicit.children = function (children) {
    for (const child of children) __attach(this.__id, childId(child));
    return this;
  };
  explicit.map = function (transform) {
    if (typeof transform !== "function") throw new TypeError("map(transform) expects a function");
    return transform(this);
  };
  explicit.when = function (condition, branch) {
    if (!condition) return this;
    if (typeof branch !== "function") throw new TypeError("when(condition, branch) expects a function");
    const produced = branch(this);
    if (produced === undefined || produced === null) throw new Error("when(...) must return the element");
    return produced;
  };
  // Focus is held by handle, so the element records the handle rather than
  // the wrapper object around it — the same unwrapping `Input.new(state)`
  // does. Without it a script writes `.track_focus(handle.__handle)`, and a
  // keyboard handler on an element that never took focus hears nothing.
  explicit.track_focus = function (handle) {
    const value = typeof handle === "number" ? handle : handle?.__handle;
    if (typeof value !== "number") {
      throw new TypeError("track_focus(handle) expects a handle from cx.focus_handle()");
    }
    __apply(this.__id, "track_focus", [value]);
    return this;
  };
  explicit.track_scroll = function (handle) {
    if (typeof handle?.__handle !== "number") throw new TypeError("track_scroll(handle) expects a VirtualListScrollHandle");
    __apply(this.__id, "track_scroll", [handle.__handle]);
    return this;
  };
  // An avatar's two — base renders the image, or the fallback when there is
  // no image, and never both — and an accordion item's two, which are read
  // back for their own type rather than rendered.
  for (const name of ["content", "trigger", "input", "decrement_button", "increment_button",
                      "image", "fallback", "header", "panel"]) {
    explicit[name] = function (value) { __slot(this.__id, name, childId(value)); return this; };
  }
  for (const name of ["hover", "active", "focus", "range_style", "cell_style", "cell_active_style", "caret_style"]) {
    explicit[name] = function (declare) {
      if (typeof declare !== "function") throw new TypeError(name + "(declare) expects a function");
      declare(element(__state(this.__id, name)));
      return this;
    };
  }
  const finiteNonNegative = (value, name) => {
    if (typeof value !== "number" || !Number.isFinite(value) || value < 0) throw new TypeError(name + " must be a finite non-negative number");
    return value;
  };
  const finitePositive = (value, name) => {
    if (typeof value !== "number" || !Number.isFinite(value) || value <= 0) throw new TypeError(name + " must be a finite positive number");
    return value;
  };
  const finiteDuration = (value, name) => {
    value = finiteNonNegative(Number(value), name);
    if (value > 86400000) throw new RangeError(name + " must not exceed 86400000 milliseconds");
    return value;
  };
  explicit.transition = function (property, options) {
    property = String(property);
    if (!["opacity", "width", "height", "left", "top"].includes(property)) throw new TypeError("transition supports opacity, width, height, left or top");
    const policy = typeof options === "number" ? { duration: options } : (options ?? {});
    const duration = finiteDuration(policy.duration ?? 0, "transition duration");
    const delay = finiteDuration(policy.delay ?? 0, "transition delay");
    const easing = String(policy.easing ?? "ease-out");
    if (!["linear", "ease-in", "ease-out", "ease-in-out"].includes(easing)) throw new TypeError("transition easing must be linear, ease-in, ease-out or ease-in-out");
    __apply(this.__id, "transition", [property, duration, delay, easing]);
    return this;
  };
  explicit.spring = function (property, options = {}) {
    property = String(property);
    if (!["opacity", "width", "height", "left", "top"].includes(property)) throw new TypeError("spring supports opacity, width, height, left or top");
    __apply(this.__id, "spring", [property, finiteDuration(options.response ?? 250, "spring response"), finiteNonNegative(Number(options.damping ?? 1), "spring damping"), finitePositive(Number(options.epsilon ?? 0.001), "spring epsilon")]);
    return this;
  };
  const coordinate = (value, name) => {
    if (typeof value === "number" && Number.isFinite(value)) return value;
    if (typeof value === "string" && /^-?(?:\d+(?:\.\d*)?|\.\d+)%$/.test(value)) return value;
    throw new TypeError(name + " must be a finite pixel number or percentage string");
  };
  const background = (kind, values, opacityFactor = 1, colorSpace = "srgb") => Object.freeze({
    __background: true,
    kind,
    values: Object.freeze(values),
    opacityFactor,
    colorSpace,
    opacity(factor) { return background(kind, values, finiteNonNegative(factor, "background opacity"), colorSpace); },
    color_space(space) {
      space = String(space).toLowerCase();
      if (!["srgb", "oklab"].includes(space)) throw new TypeError("background color_space must be srgb or oklab");
      return background(kind, values, opacityFactor, space);
    },
  });
  const asBackground = (value) => value?.__background ? value : background("solid", [String(value)]);
  const pathBuilder = (fill, width) => {
    const commands = [];
    const builder = {};
    const command = (name, arity, coordinateCount = arity) => (...args) => {
      if (args.length < arity) throw new TypeError(name + " expects at least " + arity + " argument(s)");
      for (let index = 0; index < coordinateCount; index++) coordinate(args[index], name + " coordinate");
      commands.push(Object.freeze([name, ...args]));
      return builder;
    };
    builder.move_to = command("move_to", 2);
    builder.line_to = command("line_to", 2);
    builder.curve_to = command("curve_to", 4);
    builder.cubic_bezier_to = command("cubic_bezier_to", 6);
    builder.arc_to = (...args) => {
      if (args.length < 7) throw new TypeError("arc_to expects at least 7 argument(s)");
      coordinate(args[0], "arc x radius"); coordinate(args[1], "arc y radius");
      if (typeof args[2] !== "number" || !Number.isFinite(args[2])) throw new TypeError("arc rotation must be finite");
      coordinate(args[5], "arc destination x"); coordinate(args[6], "arc destination y");
      commands.push(Object.freeze(["arc_to", ...args]));
      return builder;
    };
    builder.close = () => { commands.push(Object.freeze(["close"])); return builder; };
    builder.dash_array = (values) => {
      if (fill) throw new TypeError("dash_array is only available on stroke paths");
      if (!Array.isArray(values) || values.some((value) => typeof value !== "number" || !Number.isFinite(value) || value <= 0)) throw new TypeError("dash_array(values) expects positive finite pixel numbers");
      commands.push(Object.freeze(["dash_array", ...values]));
      return builder;
    };
    builder.add_polygon = (points, closed = true) => {
      if (!Array.isArray(points) || points.length === 0) throw new TypeError("add_polygon(points) expects a non-empty array");
      points.forEach((point, index) => {
        if (!Array.isArray(point) || point.length < 2) throw new TypeError("each polygon point must be [x, y]");
        command(index === 0 ? "move_to" : "line_to", 2)(point[0], point[1]);
      });
      if (closed) builder.close();
      return builder;
    };
    builder.build = () => Object.freeze({ __path: true, fill, width, commands: Object.freeze(commands.slice()) });
    return builder;
  };
  const paintPath = (pathValue, paintValue) => {
    if (!pathValue?.__path) throw new TypeError("window.paint_path(path, background) expects a Path built by PathBuilder");
    const paint = asBackground(paintValue);
    const object = element(__path(pathValue.fill, paint.kind, paint.values.map(String), paint.opacityFactor, paint.colorSpace, pathValue.width));
    for (const [name, ...args] of pathValue.commands) __apply(object.__id, name, args);
    return object;
  };
  let deferInit = false;
  class View {
    constructor(props) {
      if (!deferInit && typeof this.init === "function") this.init(props, ambientContext);
    }
  }
  globalThis.__construct = (Class) => {
    deferInit = true;
    try { return new Class(); }
    finally { deferInit = false; }
  };
  globalThis.__initialize = (instance, props, cx) => {
    if (typeof instance.init === "function") instance.init(props, cx);
  };
  globalThis.__checkpoint_view = (instance) => {
    const snapshots = [];
    const seen = new Set();
    const pending = [instance];
    let propertyCount = 0;
    while (pending.length > 0) {
      const value = pending.pop();
      if (value === null || (typeof value !== "object" && typeof value !== "function") || seen.has(value)) continue;
      if (snapshots.length >= 10000) throw new RangeError("a nested view update reached the 10,000-object rollback limit");
      seen.add(value);
      const descriptors = Object.getOwnPropertyDescriptors(value);
      const keys = Reflect.ownKeys(descriptors);
      propertyCount += keys.length;
      if (propertyCount > 100000) throw new RangeError("a nested view update reached the 100,000-property rollback limit");
      snapshots.push([value, descriptors]);
      for (const key of keys) {
        const descriptor = descriptors[key];
        if (Object.prototype.hasOwnProperty.call(descriptor, "value")) pending.push(descriptor.value);
      }
    }
    return () => {
      for (let index = snapshots.length - 1; index >= 0; index -= 1) {
        const [value, descriptors] = snapshots[index];
        const saved = new Set(Reflect.ownKeys(descriptors));
        for (const key of Reflect.ownKeys(value)) {
          if (!saved.has(key)) {
            const current = Object.getOwnPropertyDescriptor(value, key);
            if (current?.configurable) delete value[key];
          }
        }
        Object.defineProperties(value, descriptors);
      }
    };
  };
  const taskHandle = (id) => Object.freeze({
    cancel: () => __task_cancel(id),
    is_done: () => __task_is_done(id),
  });
  let ambientContext;
  const spawn = (body, options) => {
    if (typeof body !== "function") throw new TypeError("cx.spawn(fn) expects a function");
    const id = __task_new(options);
    let started;
    try {
      started = body(ambientContext);
    } catch (error) {
      __task_reject(id, error);
      return taskHandle(id);
    }
    Promise.resolve(started).then(
      () => __task_finish(id),
      (error) => __task_reject(id, error),
    );
    return taskHandle(id);
  };
  const timer = Object.freeze({
    after: (ms, handler, options) => taskHandle(__timer_after(Number(ms), handler, options)),
    every: (ms, handler, options) => taskHandle(__timer_every(Number(ms), handler, options)),
  });
  const storage = (session) => {
    const object = {
      key: (index) => Number.isInteger(index) && index >= 0 ? __storage_key(index, session) : null,
      getItem: (key) => __storage_get(String(key), session),
      setItem: (key, value) => __storage_set(String(key), String(value), session),
      removeItem: (key) => __storage_remove(String(key), session),
      clear: () => __storage_clear(session),
      flush: () => __storage_flush(session),
    };
    Object.defineProperty(object, "length", { get: () => __storage_length(session) });
    return Object.freeze(object);
)JS"
                               R"JS(
  };
  const localStorage = storage(false);
  const sessionStorage = storage(true);
  // The theme, cached in JavaScript and refreshed only when the palette's
  // revision moves. A description that asks fifty components for a colour used
  // to build and parse the whole snapshot fifty times; now the revision check
  // is the only thing that runs, and __prepare_theme warms the cache once per
  // description so the first reader pays nothing the others do not.
  let cachedThemeSource;
  let cachedTheme;
  let cachedThemeRevision = -1;
  globalThis.__theme_dirty = true;
  const refreshTheme = (generation) => {
    const revision = __theme_revision(generation);
    if (!globalThis.__theme_dirty && revision === cachedThemeRevision &&
        cachedTheme !== undefined) {
      return;
    }
    const source = __theme_snapshot(generation);
    if (source !== cachedThemeSource) {
      cachedThemeSource = source;
      cachedTheme = JSON.parse(source);
      Object.freeze(cachedTheme.colors);
      Object.freeze(cachedTheme.spacing);
      Object.freeze(cachedTheme.radius);
      if (cachedTheme.typography) {
        for (const style of Object.values(cachedTheme.typography)) {
          if (style && typeof style === "object") Object.freeze(style);
        }
        Object.freeze(cachedTheme.typography);
      }
      Object.freeze(cachedTheme);
    }
    cachedThemeRevision = revision;
    globalThis.__theme_dirty = false;
  };
  globalThis.__prepare_theme = () => refreshTheme(undefined);
  const currentTheme = (generation) => {
    refreshTheme(generation);
    return cachedTheme;
  };
  const contentView = (build, api) => {
    if (typeof build !== "function") {
      throw new TypeError(api + " takes a function returning an element, not an element and not a view class");
    }
    class OverlayContent extends View {
      render() { return build(); }
    }
    return __view_new(OverlayContent, undefined);
  };
  const mountOverlay = (build, api, mount) => {
    const handle = contentView(build, api);
    try { return mount(handle); }
    catch (error) {
      try { __view_release(handle); } catch (_) {}
      throw error;
    }
  };
  globalThis.__context = (generation) => Object.freeze({
    theme: () => currentTheme(generation),
    open_url: (url) => __open_url(generation, String(url)),
    notify: () => __cx_notify(generation),
    focus_handle: () => focusHandle(__focus_handle_new()),
    new: (Class, props) => {
      if (typeof Class !== "function" || !(Class.prototype instanceof View)) throw new TypeError("cx.new(Class, props) expects a View subclass");
      return entity(__view_new(Class, props));
    },
    sleep: (ms = 0) => __sleep(Number(ms)),
    spawn,
    timer,
    // GPUI dispatches an event to every handler on the path unless one of
    // them says otherwise, so a script that puts a handler on a row inside a
    // list hears both. These are the two halves of App's own answer to that,
    // under their own names.
    stop_propagation: () => __stop_propagation(),
    propagate: () => __propagate(),
    // App::bind_keys, so cx. The keymap belongs to the application rather
    // than to a window, which is why binding a chord in one view makes it
    // live everywhere its `context` predicate matches.
    bind_keys: (bindings) => {
      if (!Array.isArray(bindings)) {
        throw new TypeError("cx.bind_keys(bindings) expects an array of { keystroke, action, context? }");
      }
      return __bind_keys(bindings);
    },
    read_from_clipboard: () => __clipboard_read_text(),
    write_to_clipboard: (text) => __clipboard_write_text(String(text)),
  });
  ambientContext = Object.freeze({
    theme: () => currentTheme(undefined),
    open_url: (url) => __open_url(undefined, String(url)),
    notify: () => __cx_notify_current(),
    focus_handle: () => focusHandle(__focus_handle_new()),
    new: (Class, props) => {
      if (typeof Class !== "function" || !(Class.prototype instanceof View)) throw new TypeError("cx.new(Class, props) expects a View subclass");
      return entity(__view_new(Class, props));
    },
    sleep: (ms = 0) => __sleep(Number(ms)),
    spawn,
    timer,
    // GPUI dispatches an event to every handler on the path unless one of
    // them says otherwise, so a script that puts a handler on a row inside a
    // list hears both. These are the two halves of App's own answer to that,
    // under their own names.
    stop_propagation: () => __stop_propagation(),
    propagate: () => __propagate(),
    // App::bind_keys, so cx. The keymap belongs to the application rather
    // than to a window, which is why binding a chord in one view makes it
    // live everywhere its `context` predicate matches.
    bind_keys: (bindings) => {
      if (!Array.isArray(bindings)) {
        throw new TypeError("cx.bind_keys(bindings) expects an array of { keystroke, action, context? }");
      }
      return __bind_keys(bindings);
    },
    read_from_clipboard: () => __clipboard_read_text(),
    write_to_clipboard: (text) => __clipboard_write_text(String(text)),
  });
  globalThis.__ambient_context = ambientContext;
  globalThis.window = Object.freeze({
    open_dialog: (build, options) => mountOverlay(
      build, "window.open_dialog", handle => __open_dialog(handle, options)),
    close_dialog: () => __close_dialog(),
    close_all_dialogs: () => __close_all_dialogs(),
    has_active_dialog: () => __has_active_dialog(),
    open_sheet: (build) => mountOverlay(
      build, "window.open_sheet", handle => __open_sheet(undefined, handle)),
    open_sheet_at: (placement, build) => mountOverlay(
      build, "window.open_sheet_at", handle => __open_sheet(String(placement), handle)),
    close_sheet: () => __close_sheet(),
    has_active_sheet: () => __has_active_sheet(),
    push_toast: (options) => __push_toast(options),
    remove_toast: (id) => __remove_toast(String(id)),
    clear_toasts: () => __clear_toasts(),
    localStorage,
    sessionStorage,
    paint_path: paintPath,

    // What the window measures. All legal from render(): a view that sizes
    // itself from the viewport, or spaces itself in rems, has to ask during
    // the pass that draws it.
    rem_size: () => __window_rem_size(),
    line_height: () => __window_line_height(),
    viewport_size: () => __window_viewport_size(),
    bounds: () => __window_bounds(),
    mouse_position: () => __window_mouse_position(),
    appearance: () => __window_appearance(),
    is_window_active: () => __window_is_active(),
    is_fullscreen: () => __window_is_fullscreen(),
    is_maximized: () => __window_is_maximized(),

    // What the window can be told. Refused from render() for the reason
    // cx.notify() is: a frame that changes the window it is drawing into is a
    // frame arguing with itself. `Window::dispatch_action` in GPUI, so
    // `window` here — it walks the focus path of *this* window;
    // `cx.bind_keys` is the other half and is on cx because the keymap is
    // App's.
    dispatch_action: (action) => __dispatch_action(String(action)),
    set_rem_size: (size) => __window_set_rem_size(Number(size)),
    refresh: () => __window_refresh(),
    focus_next: () => __window_focus_next(),
    focus_prev: () => __window_focus_prev(),
    activate_window: () => __window_activate(),
    minimize_window: () => __window_minimize(),
    zoom_window: () => __window_zoom(),
    toggle_fullscreen: () => __window_toggle_fullscreen(),
  });
  globalThis.localStorage = localStorage;
  globalThis.sessionStorage = sessionStorage;
)JS"
                               R"JS(
  globalThis.console = Object.freeze({
    log: (...args) => __console_log(...args),
    debug: (...args) => __console_debug(...args),
    info: (...args) => __console_info(...args),
    warn: (...args) => __console_warn(...args),
    error: (...args) => __console_error(...args),
  });
  globalThis.__shell_fs_dirent = (name, directory) => Object.freeze({
    name,
    isDirectory: () => directory,
  });
  globalThis.__shell_fs = Object.freeze({
    readFile: (path, encoding) => __fs_read(path, encoding),
    writeFile: (path, contents) => __fs_write(path, contents),
    readdir: (path, options) => __fs_readdir(path, options),
    exists: (path) => __fs_exists(path),
    unlink: (path) => __fs_unlink(path),
    rmdir: (path) => __fs_rmdir(path),
    mkdir: (path, options) => __fs_mkdir(path, options),
  });

  const utf8Encode = (text) => {
    const encoded = encodeURIComponent(String(text));
    const bytes = [];
    for (let i = 0; i < encoded.length; i++) {
      if (encoded[i] === "%") {
        bytes.push(parseInt(encoded.slice(i + 1, i + 3), 16));
        i += 2;
      } else bytes.push(encoded.charCodeAt(i));
    }
    return bytes;
  };
  const utf8Decode = (bytes) => {
    let encoded = "";
    for (const byte of bytes) encoded += byte < 128 && byte !== 37
      ? String.fromCharCode(byte)
      : "%" + byte.toString(16).padStart(2, "0");
    return decodeURIComponent(encoded);
  };
  const b64chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  const base64Encode = (bytes) => {
    let out = "";
    for (let i = 0; i < bytes.length; i += 3) {
      const a = bytes[i], b = i + 1 < bytes.length ? bytes[i + 1] : 0;
      const c = i + 2 < bytes.length ? bytes[i + 2] : 0;
      out += b64chars[a >> 2] + b64chars[((a & 3) << 4) | (b >> 4)] +
        (i + 1 < bytes.length ? b64chars[((b & 15) << 2) | (c >> 6)] : "=") +
        (i + 2 < bytes.length ? b64chars[c & 63] : "=");
    }
    return out;
  };
  const base64Decode = (text) => {
    const clean = String(text).replace(/\s/g, "");
    const out = [];
    for (let i = 0; i < clean.length; i += 4) {
      const a = b64chars.indexOf(clean[i]), b = b64chars.indexOf(clean[i + 1]);
      const c = clean[i + 2] === "=" ? 0 : b64chars.indexOf(clean[i + 2]);
      const d = clean[i + 3] === "=" ? 0 : b64chars.indexOf(clean[i + 3]);
      if (a < 0 || b < 0 || c < 0 || d < 0) throw new TypeError("invalid base64");
      out.push((a << 2) | (b >> 4));
      if (clean[i + 2] !== "=") out.push(((b & 15) << 4) | (c >> 2));
      if (clean[i + 3] !== "=") out.push(((c & 3) << 6) | d);
    }
    return out;
  };
  class Buffer extends Uint8Array {
    static from(value, encoding = "utf8") {
      if (typeof value === "string") {
        const bytes = encoding === "hex"
          ? (value.match(/../g) ?? []).map(part => parseInt(part, 16))
          : encoding === "base64" ? base64Decode(value) : utf8Encode(value);
        return new Buffer(bytes);
      }
      return new Buffer(value instanceof ArrayBuffer ? new Uint8Array(value) : value);
    }
    static alloc(size, fill = 0) { const out = new Buffer(Number(size)); out.fill(fill); return out; }
    static allocUnsafe(size) { return new Buffer(Number(size)); }
    static isBuffer(value) { return value instanceof Buffer; }
    static byteLength(value, encoding) { return Buffer.from(value, encoding).length; }
    static concat(values, length) {
      const size = length == null ? values.reduce((sum, value) => sum + value.length, 0) : Number(length);
      const out = Buffer.alloc(size); let at = 0;
      for (const value of values) { out.set(value.subarray(0, size - at), at); at += value.length; if (at >= size) break; }
      return out;
    }
    toString(encoding = "utf8", start = 0, end = this.length) {
      const bytes = this.subarray(start, end);
      if (encoding === "hex") return Array.from(bytes, byte => byte.toString(16).padStart(2, "0")).join("");
      if (encoding === "base64") return base64Encode(bytes);
      return utf8Decode(bytes);
    }
  }
  globalThis.Buffer = Buffer;
  globalThis.__shell_buffer = Object.freeze({ Buffer });

  const standardBytes = (value, operation) => {
    if (typeof value === "string") return Buffer.from(value);
    if (value instanceof Uint8Array) return value;
    throw new TypeError(operation + " expects a string or Uint8Array");
  };
  class Hash {
    constructor(algorithm) {
      const name = String(algorithm).toLowerCase().replaceAll("-", "");
      if (name !== "sha256") throw new TypeError("'" + algorithm + "' not available");
      this.chunks = [];
      this.size = 0;
      this.done = false;
    }
    update(value) {
      if (this.done) throw new Error("Digest already called");
      const bytes = standardBytes(value, "Hash.update");
      if (bytes.length > 67108864 - this.size) throw new RangeError("hash input exceeds the 64 MiB limit");
      const copy = Buffer.from(bytes);
      this.chunks.push(copy);
      this.size += copy.length;
      return this;
    }
    digest(encoding) {
      if (this.done) throw new Error("Digest already called");
      this.done = true;
      const result = Buffer.from(__crypto_sha256(Buffer.concat(this.chunks, this.size)));
      this.chunks = [];
      if (encoding === undefined) return result;
      const name = String(encoding).toLowerCase();
      if (name !== "hex" && name !== "base64" && name !== "utf8" && name !== "utf-8") {
        throw new TypeError("unsupported digest encoding: " + encoding);
      }
      return result.toString(name);
    }
  }
  const createHash = (algorithm) => new Hash(algorithm);
  const randomBytes = (size) => Buffer.from(__crypto_random(size));
  const getRandomValues = (value) => {
    if (!ArrayBuffer.isView(value) || value instanceof DataView ||
        value instanceof Float32Array || value instanceof Float64Array) {
      throw new TypeError("crypto.getRandomValues expects an integer typed array");
    }
    if (value.byteLength > 65536) throw new RangeError("crypto.getRandomValues accepts at most 65536 bytes");
    new Uint8Array(value.buffer, value.byteOffset, value.byteLength).set(__crypto_random(value.byteLength));
    return value;
  };
  const randomUUID = () => {
    const bytes = randomBytes(16);
    bytes[6] = (bytes[6] & 15) | 64;
    bytes[8] = (bytes[8] & 63) | 128;
    const hex = bytes.toString("hex");
    return hex.slice(0, 8) + "-" + hex.slice(8, 12) + "-" +
      hex.slice(12, 16) + "-" + hex.slice(16, 20) + "-" + hex.slice(20);
  };
  const subtle = Object.freeze({
    digest: (algorithm, value) => {
      const name = String(typeof algorithm === "object" ? algorithm?.name : algorithm)
        .toLowerCase().replaceAll("-", "");
      if (name !== "sha256") return Promise.reject(new TypeError("unsupported digest algorithm"));
      let bytes;
      if (value instanceof ArrayBuffer) bytes = new Uint8Array(value);
      else if (ArrayBuffer.isView(value)) {
        bytes = new Uint8Array(value.buffer, value.byteOffset, value.byteLength);
      } else return Promise.reject(new TypeError("crypto.subtle.digest expects a BufferSource"));
      const digest = __crypto_sha256(bytes);
      return Promise.resolve(digest.buffer);
    },
  });
  const webcrypto = Object.freeze({ getRandomValues, randomUUID, subtle });
  globalThis.crypto = webcrypto;
  globalThis.__shell_crypto = Object.freeze({
    createHash, randomBytes, randomUUID, getRandomValues,
    crypto: webcrypto, webcrypto,
  });

  const compression = (native, name) => (value) =>
    Buffer.from(native(standardBytes(value, "zlib." + name)));
  const deflateSync = compression(__zlib_deflate, "deflateSync");
  const inflateSync = compression(__zlib_inflate, "inflateSync");
  const gzipSync = compression(__zlib_gzip, "gzipSync");
  const gunzipSync = compression(__zlib_gunzip, "gunzipSync");
  globalThis.__shell_zlib = Object.freeze({
    deflateSync, inflateSync, gzipSync, gunzipSync,
  });
)JS"
                               R"JS(
  const pathSep = __shell_is_windows ? "\\" : "/";
  const pathDelimiter = __shell_is_windows ? ";" : ":";
  const pathParts = (value) => String(value).replace(/\\/g, "/").split("/");
  const pathNormalize = (value) => {
    value = String(value);
    const slash = value.replace(/\\/g, "/");
    const drive = __shell_is_windows && slash.length >= 2 && slash[1] === ":" ? slash.slice(0, 2) : "";
    const absolute = slash.startsWith("/") || drive !== "";
    const out = [];
    for (const part of pathParts(drive ? slash.slice(2) : slash)) {
      if (!part || part === ".") continue;
      if (part === "..") { if (out.length && out[out.length - 1] !== "..") out.pop(); else if (!absolute) out.push(part); }
      else out.push(part);
    }
    let result = (drive ? drive + "/" : absolute ? "/" : "") + out.join("/");
    if (!result) result = absolute ? "/" : ".";
    return __shell_is_windows ? result.replace(/\//g, "\\") : result;
  };
  const pathApi = {
    sep: pathSep, delimiter: pathDelimiter,
    normalize: pathNormalize,
    isAbsolute: (value) => { const text = String(value); return text.startsWith("/") || text.startsWith("\\") || (__shell_is_windows && text.length > 2 && text[1] === ":"); },
    join: (...values) => pathNormalize(values.filter(value => String(value).length).join(pathSep)),
    resolve: (...values) => pathNormalize(values.join(pathSep)),
    basename: (value, suffix = "") => { const parts = pathParts(value).filter(Boolean); let name = parts.pop() ?? ""; suffix = String(suffix); if (suffix && name.endsWith(suffix)) name = name.slice(0, -suffix.length); return name; },
    dirname: (value) => { const text = pathNormalize(value); const at = Math.max(text.lastIndexOf("/"), text.lastIndexOf("\\")); return at < 0 ? "." : at === 0 ? pathSep : text.slice(0, at); },
    extname: (value) => { const name = pathApi.basename(value); const at = name.lastIndexOf("."); return at <= 0 ? "" : name.slice(at); },
    relative: (from, to) => {
      const a = pathParts(pathNormalize(from)).filter(Boolean), b = pathParts(pathNormalize(to)).filter(Boolean);
      let same = 0; while (same < a.length && same < b.length && a[same].toLowerCase() === b[same].toLowerCase()) same++;
      return [...a.slice(same).map(() => ".."), ...b.slice(same)].join(pathSep) || "";
    },
    parse: (value) => { const dir = pathApi.dirname(value), base = pathApi.basename(value), ext = pathApi.extname(value); return { root: pathApi.isAbsolute(value) ? pathSep : "", dir, base, ext, name: ext ? base.slice(0, -ext.length) : base }; },
    format: (parts) => (parts.dir || parts.root || "") + ((parts.dir || parts.root) ? pathSep : "") + (parts.base || ((parts.name || "") + (parts.ext || ""))),
  };
  globalThis.__shell_path = Object.freeze(pathApi);
)JS"
                               R"JS(
  class URLSearchParams {
    constructor(value = "") { this.items = []; const text = String(value).replace(/^\?/, ""); if (text) for (const part of text.split("&")) { const at = part.indexOf("="); this.append(decodeURIComponent(at < 0 ? part : part.slice(0, at)), decodeURIComponent(at < 0 ? "" : part.slice(at + 1))); } }
    append(key, value) { this.items.push([String(key), String(value)]); }
    get(key) { const found = this.items.find(item => item[0] === String(key)); return found ? found[1] : null; }
    getAll(key) { return this.items.filter(item => item[0] === String(key)).map(item => item[1]); }
    has(key) { return this.items.some(item => item[0] === String(key)); }
    set(key, value) { this.delete(key); this.append(key, value); }
    delete(key) { key = String(key); this.items = this.items.filter(item => item[0] !== key); }
    toString() { return this.items.map(item => encodeURIComponent(item[0]) + "=" + encodeURIComponent(item[1])).join("&"); }
    *entries() { yield* this.items; }
    [Symbol.iterator]() { return this.entries(); }
  }
  class URL {
    constructor(input, base) {
      let text = String(input);
      if (base && !text.includes(":")) text = String(base).replace(/[^/]*$/, "") + text;
      const scheme = text.indexOf(":");
      if (scheme <= 0) throw new TypeError("invalid URL");
      this.protocol = text.slice(0, scheme + 1);
      let rest = text.slice(scheme + 1), authority = "";
      if (rest.startsWith("//")) { rest = rest.slice(2); const end = rest.search(/[\/#?]/); authority = end < 0 ? rest : rest.slice(0, end); rest = end < 0 ? "" : rest.slice(end); }
      const hashAt = rest.indexOf("#"); this.hash = hashAt < 0 ? "" : rest.slice(hashAt); if (hashAt >= 0) rest = rest.slice(0, hashAt);
      const searchAt = rest.indexOf("?"); this.search = searchAt < 0 ? "" : rest.slice(searchAt); this.pathname = searchAt < 0 ? rest : rest.slice(0, searchAt);
      this.pathname ||= "/"; this.host = authority; const portAt = authority.lastIndexOf(":"); this.hostname = portAt > 0 ? authority.slice(0, portAt) : authority; this.port = portAt > 0 ? authority.slice(portAt + 1) : "";
      this.searchParams = new URLSearchParams(this.search);
    }
    get origin() { return this.protocol + "//" + this.host; }
    get href() { const query = this.searchParams.toString(); return this.protocol + (this.host ? "//" + this.host : "") + this.pathname + (query ? "?" + query : "") + this.hash; }
    set href(value) { const parsed = new URL(value); Object.assign(this, parsed); }
    toString() { return this.href; }
    toJSON() { return this.href; }
  }
  const urlApi = {
    URL, URLSearchParams,
    pathToFileURL: (path) => new URL("file://" + (__shell_is_windows ? "/" : "") + String(path).replace(/\\/g, "/")),
    fileURLToPath: (url) => { const parsed = url instanceof URL ? url : new URL(url); if (parsed.protocol !== "file:") throw new TypeError("URL is not file:"); const path = decodeURIComponent(parsed.pathname); return __shell_is_windows ? path.replace(/^\//, "").replace(/\//g, "\\") : path; },
  };
  globalThis.URL = URL;
  globalThis.URLSearchParams = URLSearchParams;
  globalThis.__shell_url = Object.freeze(urlApi);

  globalThis.__shell_fetch_response = (status, url, body) => Object.freeze({
    status,
    ok: status >= 200 && status < 300,
    url,
    text: () => Promise.resolve(body),
    json: () => Promise.resolve().then(() => JSON.parse(body)),
  });
  globalThis.fetch = (url, options) => {
    let method = "GET";
    let headers = [];
    let body = undefined;
    if (options !== undefined && options !== null) {
      if (typeof options !== "object" || Array.isArray(options)) {
        throw new TypeError("fetch(url, options) expects an options object");
      }
      for (const key of Object.keys(options)) {
        if (key !== "method" && key !== "headers" && key !== "body") {
          throw new TypeError(
            "unknown option `" + key +
            "` for fetch(url, options); expected method, headers or body");
        }
      }
      if (options.method !== undefined && options.method !== null) {
        method = String(options.method);
      }
      if (options.headers !== undefined && options.headers !== null) {
        const source = options.headers;
        if (typeof source !== "object" || Array.isArray(source)) {
          throw new TypeError(
            "fetch(url, options).headers expects a plain object of string values");
        }
        for (const name of Object.keys(source)) {
          const value = source[name];
          if (typeof value !== "string") {
            throw new TypeError(
              "fetch(url, options).headers expects string header values");
          }
          headers.push(name, value);
        }
      }
      if (options.body !== undefined && options.body !== null) {
        const source = options.body;
        if (typeof source === "string") {
          body = source;
        } else if (source instanceof Uint8Array) {
          body = source;
        } else {
          throw new TypeError(
            "fetch(url, options).body expects a string or Uint8Array");
        }
      }
    }
    return __fetch_send(new URL(String(url)).href, method, headers, body);
  };

  globalThis.__shell_os = Object.freeze({
    platform: () => __shell_platform,
    arch: () => __shell_arch,
    EOL: __shell_is_windows ? "\r\n" : "\n",
  });
  globalThis.process = Object.freeze({
    run: (...args) => __process_run(...args),
    nextTick: (callback, ...args) => Promise.resolve().then(() => callback(...args)),
    exit: (code = 0) => __process_exit(Number(code)),
    platform: __shell_platform,
    arch: __shell_arch,
  });
)JS"
                               R"JS(
  const component = (kind, text, handle, index) =>
    element(__component(kind, text, handle, index));
  const named = (kind) => ({ new: (id) => component(kind, String(id)) });
  const plain = (kind) => ({ new: () => component(kind) });
  const retained = (kind) => ({
    new: (state) => element(__retained_component(kind, state?.__handle)),
  });
  const inputState = (handle) => ({
    __handle: handle,
    value: () => __input_value(handle),
    set_value: (value) => __input_set_value(handle, String(value ?? "")),
    set_step: (value) => __input_set_step(handle, value == null ? null : Number(value)),
    set_min: (value) => __input_set_min(handle, value == null ? null : Number(value)),
    set_max: (value) => __input_set_max(handle, value == null ? null : Number(value)),
    set_masked: (value) => __input_set_masked(handle, Boolean(value)),
    set_loading: (value) => __input_set_loading(handle, Boolean(value)),
    on: (event, handler) => __input_on(handle, String(event), handler),
    release: () => __input_release(handle),
  });
  const textareaState = (handle) => ({
    __handle: handle,
    value: () => __textarea_value(handle),
    set_value: (value) => __textarea_set_value(handle, String(value ?? "")),
    set_rows: (rows) => __textarea_set_rows(handle, Number(rows)),
    set_auto_grow: (min, max) => __textarea_set_auto_grow(handle, Number(min), Number(max)),
    set_soft_wrap: (value) => __textarea_set_soft_wrap(handle, Boolean(value)),
    on: (event, handler) => __textarea_on(handle, String(event), handler),
    release: () => __textarea_release(handle),
  });
  const sliderValues = (value) => Array.isArray(value) ? value : [value];
  const sliderState = (handle) => ({
    __handle: handle,
    value: () => { const values = __slider_value(handle); return values.length === 1 ? values[0] : values; },
    set_value: (value) => __slider_set_value(handle, sliderValues(value)),
    min_value: () => __slider_bounds(handle)[0],
    max_value: () => __slider_bounds(handle)[1],
    step_value: () => __slider_bounds(handle)[2],
    on: (event, handler) => __slider_on(handle, String(event), handler),
    release: () => __slider_release(handle),
  });
  const otpState = (handle) => ({
    __handle: handle,
    value: () => __otp_value(handle),
    set_value: (value) => __otp_set_value(handle, String(value ?? "")),
    len: () => __otp_len(handle),
    is_masked: () => __otp_is_masked(handle),
    set_masked: (value) => __otp_set_masked(handle, Boolean(value)),
    focus: () => __otp_focus(handle),
    on: (event, handler) => __otp_on(handle, String(event), handler),
    release: () => __otp_release(handle),
  });
  // A calendar's month, and the date chosen in it.
  //
  // month_days() is the reason this is bound at all: which dates fall in which
  // week, where the neighbouring months' days go, and how many weeks this
  // month needs. Everything else here is what it takes to move that grid and
  // read what was picked from it. The element is deliberately not bound — it
  // walks the month grid calling a renderer once per cell, up to forty-two
  // crossings per frame for cells that carry no behavior at all.
  //
  // The wire is a flat array either way; the narrowing to null, a string or a
  // pair happens here so a script never sees the flat form. The slot *count*
  // is what says which variant was meant: a single day and a range whose end
  // is not chosen yet hold the same one date and read back as the same string,
  // but base branches on the difference in is_single, is_complete and
  // is_in_range.
  const calendarDate = (parts) => {
    if (parts.length === 2) return [parts[0] ?? null, parts[1] ?? null];
    return parts[0] ?? null;
  };
  const calendarParts = (value, api) => {
    if (value === null || value === undefined) return [null];
    if (Array.isArray(value)) {
      if (value.length !== 2) throw new TypeError(api + " range expects a two-element array [start, end]");
      return [value[0] ?? null, value[1] ?? null];
    }
    if (typeof value !== "string") throw new TypeError(api + ' expects null, a "YYYY-MM-DD" string, or a pair of those');
    return [value];
  };
  const calendarState = (handle) => ({
    __handle: handle,
    month_days: () => __calendar_month_days(handle),
    year: () => __calendar_year(handle),
    month: () => __calendar_month(handle),
    today: () => __calendar_today(handle),
    value: () => calendarDate(__calendar_value(handle)),
    set_value: (next) => __calendar_set_value(handle, calendarParts(next, "set_value(value)")),
    next_month: () => __calendar_next_month(handle),
    prev_month: () => __calendar_prev_month(handle),
    on: (event, handler) =>
      __calendar_on(handle, String(event), (parts, cx) => handler(calendarDate(parts), cx)),
    release: () => __calendar_release(handle),
  });
  const focusHandle = (handle) => ({
    __handle: handle,
    focus: () => __focus_focus(handle),
    is_focused: () => __focus_is_focused(handle),
    release: () => __focus_release(handle),
  });
  const entity = (handle) => ({
    __entity: true,
    __handle: handle,
    set_props: (props) => __view_set_props(handle, props),
    release: () => __view_release(handle),
  });
  // A dockable layout, and the commands its chrome carries.
  //
  // Retained for a reason none of the other handles share: the layout is what
  // the *user* changed. A drag, a resize, a closed tab and a collapsed dock all
  // happen without this script rendering, so a dock rebuilt from a description
  // would put every one of them back the way the last render described it.
  const dockPlacement = (value, api) => {
    const name = String(value ?? "center");
    if (!["center", "left", "right", "bottom"].includes(name)) {
      throw new TypeError(api + ' expects "center", "left", "right" or "bottom"');
    }
    return name;
  };
  const wholeAt = (value, api) => {
    if (!Number.isSafeInteger(value) || value < 0) {
      throw new TypeError(api + " expects a whole, non-negative position");
    }
    return value;
  };
  const finiteDockNumber = (value, api) => {
    if (typeof value !== "number" || !Number.isFinite(value)) {
      throw new TypeError(api + " expects a finite number");
    }
    return value;
  };
  const nonNegativeDockNumber = (value, api) => {
    const number = finiteDockNumber(value, api);
    if (number < 0) throw new RangeError(api + " expects a non-negative number");
    return number;
  };
  // Every chrome handler is given base's own state for one container, with the
  // area it belongs to added on this side — the commands need it, and this
  // side already knows it, so it never has to cross.
  const dockTarget = (value, api) => {
    const handle = value?.__dock;
    if (typeof handle !== "number") {
      throw new TypeError(api + " expects the group, dock or tile your chrome handler was given as its first argument");
    }
    return handle;
  };
  const groupNode = (group, api) => {
    if (typeof group?.node !== "number") {
      throw new TypeError(api + " expects a tab group, which is what tab_bar and empty_group are given");
    }
    return group.node;
  };
  // Commands, not callbacks. A chrome handler runs once per frame for as long
  // as the dock is on screen, so a handler registered inside one would pile up
  // exactly the way a virtual list's row handlers would. A command carries no
  // script value at all: it names a container and what to ask it.
  explicit.select_tab = function (group, index) {
    const api = "select_tab(group, index)";
    __apply(this.__id, "select_tab", [dockTarget(group, api), groupNode(group, api), wholeAt(index, api)]);
    return this;
  };
  explicit.close_panel = function (group, panel) {
    const api = "close_panel(group, panel_id)";
    __apply(this.__id, "close_panel", [dockTarget(group, api), groupNode(group, api), Number(panel)]);
    return this;
  };
  explicit.toggle_zoom = function (group) {
    const api = "toggle_zoom(group)";
    __apply(this.__id, "toggle_zoom", [dockTarget(group, api), groupNode(group, api)]);
    return this;
  };
  explicit.drag_tab = function (group, index) {
    const api = "drag_tab(group, index)";
    __apply(this.__id, "drag_tab", [dockTarget(group, api), groupNode(group, api), wholeAt(index, api)]);
    return this;
  };
  // The one command with an optional argument: a tab bar that names no slot
  // means "append", which is what a drop past the last tab is.
  explicit.drop_tab = function (group, index) {
    const api = "drop_tab(group, index)";
    const at = index === undefined || index === null ? -1 : wholeAt(index, api);
    __apply(this.__id, "drop_tab", [dockTarget(group, api), groupNode(group, api), at]);
    return this;
  };
  explicit.toggle_dock = function (dock) {
    const api = "toggle_dock(dock)";
    __apply(this.__id, "toggle_dock", [dockTarget(dock, api), dockPlacement(dock?.placement, api)]);
    return this;
  };
  explicit.resize_dock = function (dock) {
    const api = "resize_dock(dock)";
    __apply(this.__id, "resize_dock", [dockTarget(dock, api), dockPlacement(dock?.placement, api)]);
    return this;
  };

  const DOCK_CHROME = ["tab_bar", "empty_group", "drop_indicator", "dock"];

)JS"
                               R"JS(
  // The chrome hooks are own properties of the one element that has them,
  // rather than prototype methods: every other element in the tree would
  // otherwise carry a `dock` and a `tab_bar` that mean nothing on it.
  const dockAreaElement = (area) => {
    const handle = area?.__dock;
    if (typeof handle !== "number") {
      throw new TypeError("dock_area(area) expects a DockArea from DockArea.new(id)");
    }
    const object = element(__dock_area_element(handle));
    for (const hook of DOCK_CHROME) {
      object[hook] = function (handler) {
        if (typeof handler !== "function") {
          throw new TypeError(hook + "(handler) expects a function returning an element");
        }
        __apply(this.__id, hook, [
          (payload, cx) => {
            payload.__dock = handle;
            return handler(payload, cx);
          },
        ]);
        return this;
      };
    }
    return object;
  };

  const dockArea = (handle) => ({
    __dock: handle,
    __handle: handle,
    add_panel: (view, options) => {
      if (typeof view?.__handle !== "number" || !view.__entity) {
        throw new TypeError("add_panel(view, options) expects a view from cx.new(Class): a panel's body is a view, not an element");
      }
      const settings = options ?? {};
      if (typeof settings.name !== "string" || settings.name.length === 0) {
        throw new TypeError("add_panel(view, options) needs a name: it is what the panel is filed under in a saved layout, and what register_panel finds it again by");
      }
      // No id comes back: the view is still being constructed when this is
      // called, so the panel it will hold does not exist yet. panels() names
      // every panel once the call that added them has returned.
      __dock_add_panel(handle, view.__handle, settings.name,
        dockPlacement(settings.placement, "add_panel placement"),
        settings.size === undefined || settings.size === null
          ? -1
          : nonNegativeDockNumber(settings.size, "add_panel(view, options) size"),
        settings.closable === undefined ? true : Boolean(settings.closable),
        settings.zoomable === undefined ? true : Boolean(settings.zoomable),
        settings.visible === undefined ? true : Boolean(settings.visible));
    },
    remove_panel: (id) => __dock_remove_panel(handle, wholeAt(id, "remove_panel(id)")),
    panels: () => JSON.parse(__dock_panels(handle)),
    // The layout as plain data, and back. `load` takes effect once this call
    // has returned: rebuilding a panel constructs a view, and a view cannot be
    // constructed while script is running.
    dump: () => JSON.parse(__dock_dump(handle)),
    load: (state) => __dock_load(handle, JSON.stringify(state)),
    has_dock: (placement) => __dock_has(handle, dockPlacement(placement, "has_dock(placement)")),
    is_dock_open: (placement) => __dock_is_open(handle, dockPlacement(placement, "is_dock_open(placement)")),
    toggle_dock: (placement) => __dock_toggle(handle, dockPlacement(placement, "toggle_dock(placement)")),
    remove_dock: (placement) => __dock_remove(handle, dockPlacement(placement, "remove_dock(placement)")),
    dock_size: (placement) => __dock_size(handle, dockPlacement(placement, "dock_size(placement)")),
    set_dock_size: (placement, size) => __dock_set_size(handle,
      dockPlacement(placement, "set_dock_size(placement, size)"),
      nonNegativeDockNumber(size, "set_dock_size(placement, size)")),
    set_dock_collapsible: (placement, collapsible) => __dock_set_collapsible(handle,
      dockPlacement(placement, "set_dock_collapsible(placement, collapsible)"),
      Boolean(collapsible)),
    is_locked: () => __dock_is_locked(handle),
    set_locked: (locked) => __dock_set_locked(handle, Boolean(locked)),
    is_zoomed: () => __dock_is_zoomed(handle),
    zoom_out: () => __dock_zoom_out(handle),
    on: (event, handler) => __dock_on(handle, String(event), handler),
    release: () => __dock_release(handle),
  });

  const virtualScrollHandle = (handle) => ({
    __handle: handle,
    scroll_to_item: (index, strategy = "top") => __virtual_scroll_to_item(handle, Number(index), String(strategy)),
    scroll_to_bottom: () => __virtual_scroll_to_bottom(handle),
    release: () => __virtual_scroll_release(handle),
  });
  const virtualList = (build, name) => (id, count, sizes, getKey, render) => {
    if (!Number.isInteger(count) || count < 0) throw new TypeError(name + " item_count must be a non-negative whole number");
    if (typeof getKey !== "function" || typeof render !== "function") throw new TypeError(name + " needs get_key and render functions");
    if (Array.isArray(sizes) && sizes.length !== count) throw new TypeError(name + " needs one size per item");
    return element(build(String(id), count, sizes, getKey, render));
  };
  // A description recorded once and filled per call.
  //
  // **Not part of the script surface**, and deliberately so. Asking an author
  // to mark their hot paths is a performance annotation in the source: two
  // ways to write the same interface, restrictions that only report at first
  // call, and a decision nobody should have to make while describing a panel.
  // The machinery is kept because the runtime is meant to apply it itself, and
  // globalThis.__template is how the tests that pin its behaviour reach it.
  //
  // The body runs a single time, with a sentinel in each parameter position;
  // wherever a sentinel comes to rest in what it describes is a slot, and what
  // is left over is structure. Every call after that grafts the structure and
  // writes its arguments into the slots, entering no builder method at all.
  //
  // The sentinel refuses to become a primitive. A template literal inside a
  // body would otherwise consume it and bake this first call's value into the
  // structure, which is a panel that silently stops updating — so it throws
  // where it was written instead.
  const templateSlot = (index) => {
    const refuse = () => {
      throw new TypeError("a template argument can be passed to a builder call but not computed on. Format or compare the value where the template is called, and pass the result");
    };
    return {
      __slot: index,
      toString: refuse,
      valueOf: refuse,
      [Symbol.toPrimitive]: refuse,
    };
  };
  const template = (build) => {
    if (typeof build !== "function") throw new TypeError("template(build) expects a function that builds one element");
    let id = -1;
    return (...args) => {
      if (id < 0) {
        const slots = [];
        for (let i = 0; i < build.length; i += 1) slots.push(templateSlot(i));
        __template_begin(build.length);
        let root;
        try {
          root = build(...slots);
        } catch (error) {
          __template_abort();
          throw error;
        }
        id = __template_end(root?.__id);
      }
      return element(__template_instantiate(id, args));
    };
  };
  globalThis.__template = template;
  const api = {
    View,
    div: () => component("div"),
    h_flex: () => component("h_flex"),
    v_flex: () => component("v_flex"),
    svg: (path) => component("svg", String(path)),
    image: (path) => component("image", String(path)),
    with_cx: (body) => {
      if (typeof body !== "function") throw new TypeError("with_cx(fn) expects a function");
      return body(ambientContext);
    },
    PathBuilder: Object.freeze({
      fill: () => pathBuilder(true, 0),
      stroke: (width) => pathBuilder(false, finitePositive(width, "stroke width")),
    }),
    Background: Object.freeze({
      solid: (color) => background("solid", [String(color)]),
      stop: (color, percentage) => {
        if (typeof percentage !== "number" || !Number.isFinite(percentage)) throw new TypeError("background stop percentage must be finite");
        return Object.freeze({ __backgroundStop: true, color: String(color), percentage });
      },
      linear_gradient: (angle, from, to) => {
        angle = Number(angle);
        if (!Number.isFinite(angle)) throw new TypeError("gradient angle must be finite");
        const stop = (value, fallback, name) => {
          if (typeof value === "string") return [value, fallback];
          if (!value?.__backgroundStop) throw new TypeError(name + " must be a color or Background.stop(color, percentage)");
          return [value.color, value.percentage];
        };
        const a = stop(from, 0, "gradient from stop"), b = stop(to, 1, "gradient to stop");
        return background("linear-gradient", [String(angle), a[0], String(a[1]), b[0], String(b[1])]);
      },
      pattern_slash: (color, width, interval) => background("pattern-slash", [String(color), String(finitePositive(width, "pattern width")), String(finitePositive(interval, "pattern interval"))]),
      checkerboard: (color, size) => background("checkerboard", [String(color), String(finitePositive(size, "checkerboard size"))]),
    }),
    Button: named("Button"), Link: named("Link"),
    Checkbox: named("Checkbox"), Switch: named("Switch"),
    Tabs: named("Tabs"), Tab: named("Tab"), Progress: named("Progress"),
    ProgressTrack: plain("ProgressTrack"), ProgressIndicator: plain("ProgressIndicator"),
    Radio: named("Radio"), Toggle: named("Toggle"),
    RadioGroup: named("RadioGroup"), ToggleGroup: named("ToggleGroup"),
    Table: named("Table"), TableHeader: named("TableHeader"),
    TableBody: named("TableBody"), TableCaption: named("TableCaption"),
    TableRow: { new: (id, index) => component("TableRow", String(id), undefined, index) },
    TableHead: { new: (id, index) => component("TableHead", String(id), undefined, index) },
    TableCell: { new: (id, index) => component("TableCell", String(id), undefined, index) },
    h_resizable: (id) => component("h_resizable", String(id)),
    v_resizable: (id) => component("v_resizable", String(id)),
    resizable_panel: () => component("ResizablePanel"),
    Collapsible: plain("Collapsible"), Popover: named("Popover"),
    HoverCard: named("HoverCard"), Popup: named("Popup"),
    Select: named("Select"), Combobox: named("Combobox"),
    DatePicker: { new: (id, focus) => component("DatePicker", String(id), focus?.__handle) },
    Scrollbar: named("Scrollbar"),
    v_virtual_list: virtualList(__v_virtual_list, "v_virtual_list"),
    h_virtual_list: virtualList(__h_virtual_list, "h_virtual_list"),
    VirtualListScrollHandle: { new: () => virtualScrollHandle(__virtual_scroll_new()) },
    InputState: { new: (options = {}) => inputState(__input_state_new(options.placeholder ?? null, options.value ?? null)) }, Input: retained("Input"),
    NumberInput: retained("NumberInput"),
    TextareaState: { new: (options = {}) => textareaState(__textarea_state_new(options.placeholder ?? null, options.value ?? null, options.rows ?? null)) }, Textarea: retained("Textarea"),
    SliderState: { new: (options = {}) => sliderState(__slider_state_new(options.min ?? 0, options.max ?? 100, options.step ?? 1, String(options.scale ?? "linear"), sliderValues(options.value ?? options.min ?? 0))) }, Slider: retained("Slider"),
    SliderTrack: retained("SliderTrack"), SliderIndicator: retained("SliderIndicator"),
    SliderThumb: retained("SliderThumb"),
    OtpState: { new: (length, options = {}) => otpState(__otp_state_new(Number(length), options.value ?? null, Boolean(options.masked))) }, OtpInput: retained("OtpInput"),
    fps_monitor: () => component("FpsMonitor"),
    // Base's own shape: a root that chooses between two slots, and two slot
    // types that are not elements on their own.
    Avatar: plain("Avatar"),
    AvatarImage: { new: (path) => component("AvatarImage", String(path)) },
    AvatarFallback: plain("AvatarFallback"),
    Accordion: named("Accordion"),
    AccordionItem: plain("AccordionItem"),
    // AccordionHeader.new takes the trigger, exactly as Popup.new takes its
    // own: a heading whose button arrived later would be a heading that
    // announced nothing for a frame.
    AccordionHeader: {
      new: (trigger) => {
        if (typeof trigger?.__id !== "number") {
          throw new TypeError("AccordionHeader.new(trigger) expects an AccordionTrigger element: the heading owns the button that opens the item, and base has none of its own");
        }
        return component("AccordionHeader").trigger(trigger);
      },
    },
    AccordionPanel: plain("AccordionPanel"),
    AccordionTrigger: named("AccordionTrigger"),
    Pagination: named("Pagination"),
    // Not a component: the one thing base contributes that a script cannot
    // write for itself is which page numbers to show, and that is arithmetic.
    pagination_items: (current_page, total_pages, visible_pages) =>
      __pagination_items(Number(current_page), Number(total_pages),
        visible_pages === undefined ? 7 : Number(visible_pages)),
    CalendarState: { new: () => calendarState(__calendar_state_new()) },
    DockArea: {
      new: (id, options) => {
        const version = options?.version;
        if (version !== undefined && version !== null && (!Number.isSafeInteger(version) || version < 0)) {
          throw new TypeError("DockArea.new(id, options) version expects a whole, non-negative safe integer");
        }
        return dockArea(__dock_area_new(String(id), version ?? -1));
      },
      // Not a method on an area: a builder is registered for the whole
      // application, and a layout is restored into whichever area asks for
      // it. Registering the same name twice replaces the class, which is what
      // a hot reload does.
      register_panel: (name, Class) => {
        if (typeof name !== "string" || name.length === 0) {
          throw new TypeError("DockArea.register_panel(name, Class) needs the name the panel is added under");
        }
        if (typeof Class !== "function" || !(Class.prototype instanceof View)) {
          throw new TypeError("DockArea.register_panel(name, Class) expects the View subclass the panel is rebuilt from");
        }
        return __dock_register_panel(name, Class);
      },
    },
    // Free functions, not DockArea.element(...): the area is the state and
    // this is one description of it, the same split v_virtual_list has.
    dock_area: dockAreaElement,
    dock_content: () => component("dock_content"),
    show_fps_monitor: (options) => __show_fps_monitor(options),
    hide_fps_monitor: () => __hide_fps_monitor(),
    fps_monitor_visible: () => __fps_monitor_visible(),
    set_theme: (theme) => {
      __set_theme(theme);
      globalThis.__theme_dirty = true;
    },
  };
  return Object.freeze(api);
})();
)JS";

static const char kSandbox[] = R"JS(
(() => {
  const unavailable = (name, hint) => function () {
    throw new TypeError("`" + name + "` is not available in the shell: " + hint);
  };
  for (const [name, hint] of [
    ["setTimeout", "use cx.timer.after(ms, callback)"],
    ["setInterval", "use cx.timer.every(ms, callback)"],
    ["clearTimeout", "cancel the handle returned by cx.timer.after"],
    ["clearInterval", "cancel the handle returned by cx.timer.every"],
    ["require", "this runtime uses ES modules; use `import`"],
  ]) {
    if (!(name in globalThis)) globalThis[name] = unavailable(name, hint);
  }
  const hint = "the shell sandbox withholds dynamic code; enable development mode to allow it";
  const deny = (label) => function () {
    throw new TypeError(label + " is disabled: " + hint);
  };
  const replaceConstructor = (holder, value) => {
    Object.defineProperty(Object.getPrototypeOf(holder), "constructor", {
      value, writable: true, enumerable: false, configurable: true,
    });
  };
  const blocked = deny("the Function constructor");
  blocked.prototype = Function.prototype;
  replaceConstructor(function () {}, blocked);
  replaceConstructor(async function () {}, deny("the AsyncFunction constructor"));
  replaceConstructor(function* () {}, deny("the GeneratorFunction constructor"));
  replaceConstructor(async function* () {}, deny("the AsyncGeneratorFunction constructor"));
  globalThis.Function = blocked;
  delete globalThis.eval;
  for (const proto of [Object.prototype, Array.prototype, Function.prototype,
                       String.prototype, Number.prototype]) Object.freeze(proto);
})();
)JS";

// ─── The window's own measurements and controls ───────────────────────────
//
// crates/shell/src/engine/quickjs/window_api.rs. The split between the two
// halves is the one render enforces everywhere else: reading a measurement
// during a render pass is the point — a view that sizes itself from the
// viewport has to ask while it is drawing — while changing the window is a
// mutation, and a mutation from inside render is a frame arguing with itself.

static bool WindowHost(JSContext* ctx, const char* api, bool mutation,
                       Window** window, App** app) {
    if (mutation && shell::ScopeHasCurrent() &&
        !ScopePhaseAllowsNotify(shell::ScopeCurrentPhase())) {
        JS_ThrowTypeError(
            ctx,
            "%s is not allowed during the `%s` phase; the window may only be "
            "changed while handling an event or a task",
            api, ScopePhaseName(shell::ScopeCurrentPhase()));
        return false;
    }
    shell::ScopeHostContext host = shell::ScopeCurrentHost();
    if (!host.IsSet() || !host.GetWindow() || !host.GetApp()) {
        JS_ThrowTypeError(ctx,
                          "%s needs a live host call; call it from render(), "
                          "init(), an event handler or a task",
                          api);
        return false;
    }
    *window = host.GetWindow();
    *app = host.GetApp();
    return true;
}

// The port's rem is fixed at 16, which is what a `1rem` length resolves to
// everywhere in this tree; there is no `Window::rem_size` to move, so
// `set_rem_size` has nothing to set and is not bound. Reported here rather
// than left out of the prelude, so a script that asks gets the number the
// lengths it writes are actually resolved against.
static const float kShellRemSize = 16.f;

static JSValue NativeWindowRemSize(JSContext* ctx, JSValueConst, int,
                                   JSValueConst*) {
    Window* window = nullptr;
    App* app = nullptr;
    if (!WindowHost(ctx, "window.rem_size()", false, &window, &app))
        return JS_EXCEPTION;
    return JS_NewFloat64(ctx, kShellRemSize);
}

static JSValue NativeWindowLineHeight(JSContext* ctx, JSValueConst, int,
                                      JSValueConst*) {
    Window* window = nullptr;
    App* app = nullptr;
    if (!WindowHost(ctx, "window.line_height()", false, &window, &app))
        return JS_EXCEPTION;
    return JS_NewFloat64(ctx, kShellRemSize * kLineHeight);
}

static JSValue JsSize(JSContext* ctx, float width, float height) {
    JSValue object = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, object, "width", JS_NewFloat64(ctx, width));
    JS_SetPropertyStr(ctx, object, "height", JS_NewFloat64(ctx, height));
    return object;
}

static JSValue JsPoint(JSContext* ctx, float x, float y) {
    JSValue object = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, object, "x", JS_NewFloat64(ctx, x));
    JS_SetPropertyStr(ctx, object, "y", JS_NewFloat64(ctx, y));
    return object;
}

static JSValue JsBounds(JSContext* ctx, Bounds bounds) {
    JSValue object = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, object, "x", JS_NewFloat64(ctx, bounds.x));
    JS_SetPropertyStr(ctx, object, "y", JS_NewFloat64(ctx, bounds.y));
    JS_SetPropertyStr(ctx, object, "width", JS_NewFloat64(ctx, bounds.w));
    JS_SetPropertyStr(ctx, object, "height", JS_NewFloat64(ctx, bounds.h));
    return object;
}

static JSValue NativeWindowViewportSize(JSContext* ctx, JSValueConst, int,
                                        JSValueConst*) {
    Window* window = nullptr;
    App* app = nullptr;
    if (!WindowHost(ctx, "window.viewport_size()", false, &window, &app))
        return JS_EXCEPTION;
    WinSize size = WindowSize(window);
    return JsSize(ctx, size.dipW, size.dipH);
}

static JSValue NativeWindowBounds(JSContext* ctx, JSValueConst, int,
                                  JSValueConst*) {
    Window* window = nullptr;
    App* app = nullptr;
    if (!WindowHost(ctx, "window.bounds()", false, &window, &app))
        return JS_EXCEPTION;
    // The client box, which is what every coordinate a script sees is in.
    WinSize size = WindowSize(window);
    return JsBounds(ctx, Bounds{0, 0, size.dipW, size.dipH});
}

static JSValue NativeWindowMousePosition(JSContext* ctx, JSValueConst, int,
                                         JSValueConst*) {
    Window* window = nullptr;
    App* app = nullptr;
    if (!WindowHost(ctx, "window.mouse_position()", false, &window, &app))
        return JS_EXCEPTION;
    return JsPoint(ctx, window->mouseX, window->mouseY);
}

// The platform appearance, reduced to the two a script can act on. The port's
// window has no vibrancy variants, so the semantic theme mode is the whole of
// the answer: what a script needs to know is whether it is drawing on light or
// on dark.
static JSValue NativeWindowAppearance(JSContext* ctx, JSValueConst, int,
                                      JSValueConst*) {
    Window* window = nullptr;
    App* app = nullptr;
    if (!WindowHost(ctx, "window.appearance()", false, &window, &app))
        return JS_EXCEPTION;
    return JS_NewString(ctx,
                        ThemeGet(app) == ThemeMode::Dark ? "dark" : "light");
}

static JSValue NativeWindowIsActive(JSContext* ctx, JSValueConst, int,
                                    JSValueConst*) {
    Window* window = nullptr;
    App* app = nullptr;
    if (!WindowHost(ctx, "window.is_window_active()", false, &window, &app))
        return JS_EXCEPTION;
    return JS_NewBool(ctx, window->active);
}

static JSValue NativeWindowIsMaximized(JSContext* ctx, JSValueConst, int,
                                       JSValueConst*) {
    Window* window = nullptr;
    App* app = nullptr;
    if (!WindowHost(ctx, "window.is_maximized()", false, &window, &app))
        return JS_EXCEPTION;
    return JS_NewBool(ctx, AppIsMaximized(window));
}

static JSValue NativeWindowRefresh(JSContext* ctx, JSValueConst, int,
                                   JSValueConst*) {
    Window* window = nullptr;
    App* app = nullptr;
    if (!WindowHost(ctx, "window.refresh()", true, &window, &app))
        return JS_EXCEPTION;
    AppRefreshWindows(app);
    return JS_UNDEFINED;
}

static JSValue NativeWindowFocusMove(JSContext* ctx, JSValueConst, int,
                                     JSValueConst*, int magic) {
    Window* window = nullptr;
    App* app = nullptr;
    const char* api = magic ? "window.focus_prev()" : "window.focus_next()";
    if (!WindowHost(ctx, api, true, &window, &app)) return JS_EXCEPTION;
    FocusTrapTab(window, magic != 0);
    AppInvalidate(window);
    return JS_UNDEFINED;
}

// Activate, minimize and zoom. `zoom_window` is GPUI's name for the maximise
// toggle, which is what the port's own title bar button runs.
static JSValue NativeWindowCommand(JSContext* ctx, JSValueConst, int,
                                   JSValueConst*, int magic) {
    Window* window = nullptr;
    App* app = nullptr;
    static const char* const names[] = {"window.activate_window()",
                                        "window.minimize_window()",
                                        "window.zoom_window()"};
    if (!WindowHost(ctx, names[magic], true, &window, &app))
        return JS_EXCEPTION;
    if (magic == 0) {
        AppActivate(window);
    } else if (magic == 1) {
        AppMinimize(window);
    } else {
        AppToggleMaximize(window);
    }
    return JS_UNDEFINED;
}

// cx.stop_propagation() / cx.propagate(). GPUI answers them on the App; here
// the flag belongs to the event being dispatched, so the dispatch that is
// running installs its own and these two write through it.
static bool* gShellPropagate = nullptr;

struct ShellPropagationGuard {
    bool* saved = nullptr;
    explicit ShellPropagationGuard(bool* flag) {
        saved = gShellPropagate;
        gShellPropagate = flag;
    }
    ~ShellPropagationGuard() { gShellPropagate = saved; }
};

static JSValue NativeStopPropagation(JSContext* ctx, JSValueConst, int,
                                     JSValueConst*) {
    if (gShellPropagate) *gShellPropagate = false;
    shell::ScopeHostContext host = shell::ScopeCurrentHost();
    if (host.IsSet() && host.GetWindow()) {
        host.GetWindow()->stopPropagation = true;
    }
    (void)ctx;
    return JS_UNDEFINED;
}

static JSValue NativePropagate(JSContext* ctx, JSValueConst, int,
                               JSValueConst*) {
    if (gShellPropagate) *gShellPropagate = true;
    shell::ScopeHostContext host = shell::ScopeCurrentHost();
    if (host.IsSet() && host.GetWindow()) {
        host.GetWindow()->stopPropagation = false;
    }
    (void)ctx;
    return JS_UNDEFINED;
}

// App::bind_keys, from a list of { keystroke, action, context? }.
//
// Whole-list rather than one at a time, and validated before any of it is
// installed: a keymap half applied because the fourth entry had a typo is a
// worse state than one not applied at all, and the script has no way to see
// which half made it.
static JSValue NativeBindKeys(JSContext* ctx, JSValueConst, int argc,
                              JSValueConst* argv) {
    if (shell::ScopeHasCurrent() &&
        !ScopePhaseAllowsNotify(shell::ScopeCurrentPhase())) {
        return JS_ThrowTypeError(
            ctx,
            "cx.bind_keys() is not allowed during the `%s` phase; bind keys "
            "from init(), an event handler or a task",
            ScopePhaseName(shell::ScopeCurrentPhase()));
    }
    int64_t count = 0;
    if (argc < 1 || JS_GetLength(ctx, argv[0], &count) < 0 || count < 0) {
        return JS_ThrowTypeError(
            ctx,
            "cx.bind_keys(bindings) expects an array of { keystroke, action, "
            "context? }");
    }
    if (count > 4096) {
        return JS_ThrowRangeError(ctx,
                                  "cx.bind_keys(bindings) has too many "
                                  "bindings");
    }
    Arena* arena = ArenaNew();
    Vec<KeyBinding> parsed;
    for (int64_t i = 0; i < count; i++) {
        JSValue entry = JS_GetPropertyUint32(ctx, argv[0], (uint32_t)i);
        JSValue strokeValue = JS_GetPropertyStr(ctx, entry, "keystroke");
        JSValue actionValue = JS_GetPropertyStr(ctx, entry, "action");
        JSValue contextValue = JS_GetPropertyStr(ctx, entry, "context");
        Str stroke, action, context;
        bool ok = JS_IsString(strokeValue) &&
                  JsString(ctx, strokeValue, arena, &stroke) &&
                  JS_IsString(actionValue) &&
                  JsString(ctx, actionValue, arena, &action);
        bool hasContext = ok && JS_IsString(contextValue) &&
                          JsString(ctx, contextValue, arena, &context);
        JS_FreeValue(ctx, strokeValue);
        JS_FreeValue(ctx, actionValue);
        JS_FreeValue(ctx, contextValue);
        JS_FreeValue(ctx, entry);
        if (!ok || action.len == 0 || stroke.len == 0) {
            VecReset(parsed);
            ArenaDelete(arena);
            return JS_ThrowTypeError(
                ctx,
                "binding %d needs a `keystroke`, such as \"cmd-s\", and a "
                "non-empty `action`",
                (int)i);
        }
        // KeyChordsParse answers 0 for a spec it cannot read, and a script
        // typo must not install a binding nothing can reach.
        KeyChord chords[kMaxStrokes] = {};
        if (KeyChordsParse(stroke, chords, kMaxStrokes) == 0) {
            JSValue thrown = JS_ThrowTypeError(
                ctx, "binding %d has an unparsable keystroke `%.*s`", (int)i,
                stroke.len, stroke.s);
            VecReset(parsed);
            ArenaDelete(arena);
            return thrown;
        }
        KeyBinding binding = {};
        binding.stroke = shell::ShellActionInternText(stroke);
        binding.action = shell::ShellActionOf(action);
        binding.context = hasContext && context.len > 0
                              ? shell::ShellActionInternText(context)
                              : nullptr;
        VecAppend(parsed, binding);
    }
    if (parsed.len > 0) KeymapBind(&parsed[0], parsed.len);
    uint32_t installed = (uint32_t)parsed.len;
    VecReset(parsed);
    ArenaDelete(arena);
    return JS_NewUint32(ctx, installed);
}

static JSValue NativeDispatchAction(JSContext* ctx, JSValueConst, int argc,
                                    JSValueConst* argv) {
    Arena* arena = ArenaNew();
    Str action;
    if (argc < 1 || !JsString(ctx, argv[0], arena, &action) ||
        action.len == 0) {
        ArenaDelete(arena);
        return JS_ThrowTypeError(
            ctx,
            "window.dispatch_action(action) expects a non-empty action name");
    }
    Window* window = nullptr;
    App* app = nullptr;
    if (!WindowHost(ctx, "window.dispatch_action()", true, &window, &app)) {
        ArenaDelete(arena);
        return JS_EXCEPTION;
    }
    uint32_t id = shell::ShellActionOf(action);
    ArenaDelete(arena);
    WindowDispatchAction(window, id);
    AppInvalidate(window);
    return JS_UNDEFINED;
}

// pagination_items(current, total, visible). A plain calculation rather than a
// component: what it answers is which page numbers to draw and where the gaps
// fall, and the buttons themselves are the script's.
static JSValue NativePaginationItems(JSContext* ctx, JSValueConst, int argc,
                                     JSValueConst* argv) {
    double current = 0, total = 0, visible = 7;
    if (argc < 2 || JS_ToFloat64(ctx, &current, argv[0]) < 0 ||
        JS_ToFloat64(ctx, &total, argv[1]) < 0 ||
        (argc > 2 && JS_ToFloat64(ctx, &visible, argv[2]) < 0)) {
        return JS_EXCEPTION;
    }
    if (!isfinite(current) || current < 0 || !isfinite(total) || total < 0 ||
        !isfinite(visible) || visible < 0) {
        return JS_ThrowTypeError(ctx,
                                 "pagination_items(current_page, total_pages, "
                                 "visible_pages?) expects non-negative "
                                 "numbers");
    }
    PaginationState state = PaginationStateNew((int)current, (int)total);
    if (visible > 0) state.visiblePages = (int)visible;
    PaginationItem items[64] = {};
    int count =
        PaginationItems(&state, items, (int)(sizeof(items) / sizeof(items[0])));
    JSValue out = JS_NewArray(ctx);
    for (int i = 0; i < count; i++) {
        JSValue object = JS_NewObject(ctx);
        if (items[i].page != 0) {
            JS_SetPropertyStr(ctx, object, "page",
                              JS_NewUint32(ctx, (uint32_t)items[i].page));
        } else {
            // The span an ellipsis stands for, inclusive at both ends — a
            // script showing "pages 4–8" wants the last page the gap covers.
            JSValue bounds = JS_NewArray(ctx);
            JS_SetPropertyUint32(ctx, bounds, 0,
                                 JS_NewUint32(ctx, (uint32_t)items[i].from));
            JS_SetPropertyUint32(ctx, bounds, 1,
                                 JS_NewUint32(ctx, (uint32_t)items[i].to));
            JS_SetPropertyStr(ctx, object, "ellipsis", bounds);
        }
        JS_SetPropertyUint32(ctx, out, (uint32_t)i, object);
    }
    return out;
}

// ─── CalendarState ────────────────────────────────────────────────────────
//
// The state is bound; base's Calendar element is not, and that is a decision
// rather than an omission. The element walks the month grid calling an item
// renderer once per cell — up to forty-two calls into the VM per frame, from
// inside the layout pass, for cells whose default renderer draws an unstyled
// box. What a script cannot work out for itself is the grid, and month_days()
// answers exactly that.
//
// Dates cross as "YYYY-MM-DD": sortable as text and readable by `new Date(s)`,
// so weekday names and localized month labels are the script's without this
// boundary inventing a date type.

static JSValue JsDate(JSContext* ctx, LocalDate date) {
    if (date.year == 0 || date.month == 0 || date.day == 0) {
        return JS_NULL;
    }
    TempStr text = fmt("%04d-%02d-%02d", date.year, date.month, date.day);
    return JS_NewString(ctx, text.s);
}

static bool JsToDate(Str text, LocalDate* out) {
    if (text.len != 10 || text.s[4] != '-' || text.s[7] != '-') return false;
    int year = 0, month = 0, day = 0;
    for (int i = 0; i < 10; i++) {
        if (i == 4 || i == 7) continue;
        if (text.s[i] < '0' || text.s[i] > '9') return false;
    }
    for (int i = 0; i < 4; i++) year = year * 10 + (text.s[i] - '0');
    for (int i = 5; i < 7; i++) month = month * 10 + (text.s[i] - '0');
    for (int i = 8; i < 10; i++) day = day * 10 + (text.s[i] - '0');
    if (month < 1 || month > 12 || day < 1 || day > 31) return false;
    *out = LocalDate{year, month, day};
    return true;
}

// A Date on the wire: one slot for a single day, two for a range.
//
// The slot *count* is what carries the variant, and it has to. A single day
// and a range whose end is not chosen yet hold the same one date and both
// render as the same string — but base branches on the difference in
// IsSingle, IsComplete and IsInRange, so a wire that dropped it would quietly
// turn every set_value("2026-08-15") into a half-open range.
static JSValue DateToParts(JSContext* ctx, Date date) {
    JSValue parts = JS_NewArray(ctx);
    JS_SetPropertyUint32(ctx, parts, 0, JsDate(ctx, date.start));
    if (date.kind == DateKind::Range) {
        JS_SetPropertyUint32(ctx, parts, 1, JsDate(ctx, date.end));
    }
    return parts;
}

static bool DateFromParts(JSContext* ctx, JSValueConst parts, Date* out) {
    int64_t count = 0;
    if (JS_GetLength(ctx, parts, &count) < 0 || count < 1 || count > 2) {
        JS_ThrowTypeError(ctx,
                          "set_value(value) expects null, a \"YYYY-MM-DD\" "
                          "string, or a pair of those");
        return false;
    }
    LocalDate slots[2] = {};
    for (int64_t i = 0; i < count; i++) {
        JSValue value = JS_GetPropertyUint32(ctx, parts, (uint32_t)i);
        if (JS_IsNull(value) || JS_IsUndefined(value)) {
            JS_FreeValue(ctx, value);
            continue;
        }
        Arena* arena = ArenaNew();
        Str text;
        bool ok =
            JsString(ctx, value, arena, &text) && JsToDate(text, &slots[i]);
        ArenaDelete(arena);
        JS_FreeValue(ctx, value);
        if (!ok) {
            JS_ThrowTypeError(ctx, "a calendar date must be \"YYYY-MM-DD\"");
            return false;
        }
    }
    *out =
        count == 2 ? Date::Range(slots[0], slots[1]) : Date::Single(slots[0]);
    return true;
}

static CalendarState* LiveCalendar(JSContext* ctx, JSValueConst value,
                                   App** app, Window** window) {
    shell::EntityHandle handle = 0;
    if (!JsHandle(ctx, value, &handle)) return nullptr;
    shell::RetainedEntry* entry =
        LiveRetained(ctx, handle, shell::RetainedKind::Calendar, "calendar");
    if (!entry) return nullptr;
    shell::ScopeHostContext host = shell::ScopeCurrentHost();
    if (!host.IsSet()) {
        JS_ThrowTypeError(ctx,
                          "a CalendarState needs a live host call; call it "
                          "from render(), init() or an event handler");
        return nullptr;
    }
    if (app) *app = host.GetApp();
    if (window) *window = host.GetWindow();
    return entry->calendar.Get(host.GetApp());
}

static JSValue NativeCalendarNew(JSContext* ctx, JSValueConst, int,
                                 JSValueConst*) {
    if (RefuseRetainedCreation(ctx, "CalendarState.new()")) return JS_EXCEPTION;
    ShellRuntimeImpl* impl = (ShellRuntimeImpl*)JS_GetContextOpaque(ctx);
    shell::ScopeHostContext host = shell::ScopeCurrentHost();
    if (!impl || !host.IsSet()) {
        return JS_ThrowTypeError(ctx,
                                 "CalendarState.new() needs a live host call; "
                                 "call it from init() or an event handler");
    }
    Ctx cx = {host.GetApp(),
              host.GetWindow(),
              host.GetWindow() ? host.GetWindow()->frameArena : nullptr,
              {}};
    shell::EntityHandle handle = impl->retained.CreateCalendar(
        &cx, shell::ScopeCurrentView(), shell::ScopeCurrentApplication());
    if (!handle) {
        return JS_ThrowRangeError(ctx,
                                  "the application reached gpui-shell's "
                                  "retained entity limit; release unused "
                                  "handles");
    }
    return JS_NewFloat64(ctx, (double)handle);
}

// month_days(): the grid, as weeks of "YYYY-MM-DD" days. One array per month
// the state is showing, so the shape matches base's own `month_days`.
static JSValue NativeCalendarMonthDays(JSContext* ctx, JSValueConst, int argc,
                                       JSValueConst* argv) {
    App* app = nullptr;
    CalendarState* state =
        argc >= 1 ? LiveCalendar(ctx, argv[0], &app, nullptr) : nullptr;
    if (!state) return JS_EXCEPTION;
    JSValue months = JS_NewArray(ctx);
    for (int m = 0; m < (state->numberOfMonths > 0 ? state->numberOfMonths : 1);
         m++) {
        int year = state->currentYear;
        int month = state->currentMonth + m;
        while (month > 12) {
            month -= 12;
            year++;
        }
        LocalDate first = {year, month, 1};
        int weekday = CalendarWeekday(year, month, 1);
        int offset = CalendarGridOffset(weekday);
        int days = CalendarDaysInMonth(year, month);
        int cells = CalendarGridCells(offset, days);
        LocalDate start = DateAddDays(first, -offset);
        JSValue weeks = JS_NewArray(ctx);
        uint32_t weekIndex = 0;
        for (int i = 0; i < cells; i += 7) {
            JSValue week = JS_NewArray(ctx);
            for (int d = 0; d < 7; d++) {
                JS_SetPropertyUint32(ctx, week, (uint32_t)d,
                                     JsDate(ctx, DateAddDays(start, i + d)));
            }
            JS_SetPropertyUint32(ctx, weeks, weekIndex++, week);
        }
        JS_SetPropertyUint32(ctx, months, (uint32_t)m, weeks);
    }
    return months;
}

// year(), month() and today(). Three readers rather than one answering all
// three: the prelude spells each as its own method, so a combined call would
// have every one of them pay for the other two.
static JSValue NativeCalendarRead(JSContext* ctx, JSValueConst, int argc,
                                  JSValueConst* argv, int magic) {
    App* app = nullptr;
    CalendarState* state =
        argc >= 1 ? LiveCalendar(ctx, argv[0], &app, nullptr) : nullptr;
    if (!state) return JS_EXCEPTION;
    if (magic == 0) return JS_NewInt32(ctx, state->currentYear);
    if (magic == 1) return JS_NewUint32(ctx, (uint32_t)state->currentMonth);
    if (magic == 2) return JsDate(ctx, state->today);
    return DateToParts(ctx, state->date);
}

static JSValue NativeCalendarSetValue(JSContext* ctx, JSValueConst, int argc,
                                      JSValueConst* argv) {
    App* app = nullptr;
    Window* window = nullptr;
    CalendarState* state =
        argc >= 2 ? LiveCalendar(ctx, argv[0], &app, &window) : nullptr;
    if (!state) return JS_EXCEPTION;
    Date date = {};
    if (!DateFromParts(ctx, argv[1], &date)) return JS_EXCEPTION;
    Ctx cx = {app, window, window ? window->frameArena : nullptr, {}};
    CalendarStateSetDate(state, date, &cx);
    return JS_UNDEFINED;
}

// Moving the month is a mutation, so it is refused during a render pass for
// the reason every other one is: a frame that moved the month it was drawing
// would draw one month and describe another.
static JSValue NativeCalendarStep(JSContext* ctx, JSValueConst, int argc,
                                  JSValueConst* argv, int magic) {
    if (RefuseRetainedMutation(ctx, magic ? "prev_month()" : "next_month()")) {
        return JS_EXCEPTION;
    }
    App* app = nullptr;
    Window* window = nullptr;
    CalendarState* state =
        argc >= 1 ? LiveCalendar(ctx, argv[0], &app, &window) : nullptr;
    if (!state) return JS_EXCEPTION;
    if (magic) {
        CalendarPrevMonth(state);
    } else {
        CalendarNextMonth(state);
    }
    if (window) AppInvalidate(window);
    return JS_UNDEFINED;
}

// A calendar's one event: a date was selected. One subscription and one
// handler, because CalendarEvent has one variant — there is nothing to key by
// and nothing a second registration could mean but "also this", which is not
// what every other `on(...)` in this API means.
static JSValue NativeCalendarOn(JSContext* ctx, JSValueConst, int argc,
                                JSValueConst* argv) {
    App* app = nullptr;
    CalendarState* state =
        argc >= 3 ? LiveCalendar(ctx, argv[0], &app, nullptr) : nullptr;
    if (!state) return JS_EXCEPTION;
    Arena* arena = ArenaNew();
    Str name;
    bool named = JsString(ctx, argv[1], arena, &name);
    bool isChange = named && StrEq(name, StrL("change"));
    ArenaDelete(arena);
    if (!named) return JS_EXCEPTION;
    if (!isChange) {
        return JS_ThrowTypeError(
            ctx,
            "unknown calendar event; the only one is \"change\", which fires "
            "when a date is selected");
    }
    if (!JS_IsFunction(ctx, argv[2])) {
        return JS_ThrowTypeError(ctx,
                                 "on(\"change\", handler) expects a function");
    }
    ShellRuntimeImpl* impl = (ShellRuntimeImpl*)JS_GetContextOpaque(ctx);
    shell::EntityHandle handle = 0;
    JsHandle(ctx, argv[0], &handle);
    shell::RetainedEntry* entry = impl->retained.Find(handle);
    if (!entry) return JS_NewBool(ctx, false);
    shell::CallbackId callback = impl->callbacks.PushPersistent(
        ctx, argv[2], shell::ScopeCurrentView(), shell::ScopeCurrentPolicy(),
        (AppModule*)shell::ScopeCurrentApplication());
    if (callback == UINT64_MAX) {
        return JS_ThrowInternalError(ctx,
                                     "a calendar handler was registered "
                                     "outside a live application");
    }
    shell::CallbackId replaced = 0;
    impl->retained.AddCallback(handle, shell::RetainedEvent::CalendarChange,
                               callback, true, &replaced);
    if (replaced) impl->callbacks.RetireId(ctx, replaced);
    if (entry->subscription.IsValid()) {
        EntityUnsubscribe(app, entry->subscription);
        entry->subscription = {};
    }
    entry->subscription = SubscribeTo(
        app, entry->calendar, Entity<ScriptView>{shell::ScopeCurrentView()},
        &ScriptView::OnCalendarEvent, (intptr_t)handle);
    return JS_NewBool(ctx, true);
}

static JSValue NativeCalendarRelease(JSContext* ctx, JSValueConst, int argc,
                                     JSValueConst* argv) {
    ShellRuntimeImpl* impl = (ShellRuntimeImpl*)JS_GetContextOpaque(ctx);
    shell::EntityHandle handle = 0;
    if (!impl || argc < 1 || !JsHandle(ctx, argv[0], &handle)) {
        return JS_NewBool(ctx, false);
    }
    Vec<shell::CallbackId> retired;
    bool released = impl->retained.Release(handle, &retired);
    for (int i = 0; i < retired.len; i++)
        impl->callbacks.RetireId(ctx, retired[i]);
    VecReset(retired);
    return JS_NewBool(ctx, released);
}

// ─── Panels come back after a restart ─────────────────────────────────────
//
// DockArea.register_panel(name, Class) teaches the panel registry to rebuild
// the panel from a persisted layout: it constructs Class, then hands the
// payload the last save wrote to the instance's own deserialize(data). Its
// serialize() is read on the way out. Both are ordinary methods on the view
// class, because a panel *is* a view — there is no second object to introduce.

struct ScriptPanelClass {
    ShellRuntimeImpl* impl = nullptr;
    Str name = {};
    JSValue klass = JS_UNDEFINED;
    Policy* policy = nullptr;
    AppModule* application = nullptr;
};

static Entity<ScriptView> BuildScriptPanel(Window* window, App* app,
                                           void* data) {
    auto* registered = (ScriptPanelClass*)data;
    if (!registered || !registered->impl || JS_IsUndefined(registered->klass)) {
        return {};
    }
    ShellRuntimeImpl* impl = registered->impl;
    ViewType* type = new ViewType();
    type->runtime = impl->owner->Retain();
    type->value = JS_DupValue(impl->context, registered->klass);
    type->application = registered->application;
    Entity<ScriptView> entity =
        ScriptView::New(app, impl->owner, type, registered->policy);
    ScriptView* view = entity.Get(app);
    if (!view) {
        ViewTypeRelease(type);
        return {};
    }
    ShellError error = {};
    ViewObject* object =
        InstantiateObject(impl->owner, type, window, app, registered->policy,
                          JS_UNDEFINED, &error, entity.id);
    if (!object) {
        log(error.IsSet() ? error.message
                          : StrL("a dock panel's script could not be built"));
        ShellErrorClear(&error);
        EntityDrop(app, entity.id);
        ViewTypeRelease(type);
        return {};
    }
    view = entity.Get(app);
    if (!view) {
        ViewObjectRelease(object);
        ViewTypeRelease(type);
        return {};
    }
    view->object = object;
    // ScriptView::New retained the type; this reference was ours.
    ViewTypeRelease(type);
    return entity;
}

// The instance's own serialize(), if it has one. No call scope is opened, and
// none can be: a dump is a read, so there is no window to open one with. A
// serialize() that calls back into the host therefore fails the way any host
// call outside a scope does, which is the documented contract.
static bool SerializeScriptPanel(Entity<ScriptView> handle, App* app,
                                 void* data, StrBuilder* out) {
    auto* registered = (ScriptPanelClass*)data;
    ScriptView* view = registered ? handle.Get(app) : nullptr;
    if (!view || !view->object) return false;
    JSContext* ctx = registered->impl->context;
    JSValue instance = view->object->value;
    JSValue method = JS_GetPropertyStr(ctx, instance, "serialize");
    if (!JS_IsFunction(ctx, method)) {
        JS_FreeValue(ctx, method);
        return false;
    }
    JSValue produced = JS_Call(ctx, method, instance, 0, nullptr);
    JS_FreeValue(ctx, method);
    if (JS_IsException(produced) || JS_IsUndefined(produced) ||
        JS_IsNull(produced)) {
        if (JS_IsException(produced)) {
            Arena* arena = ArenaNew();
            log(ExceptionText(arena, ctx));
            ArenaDelete(arena);
        } else {
            JS_FreeValue(ctx, produced);
        }
        return false;
    }
    JSValue text = JS_JSONStringify(ctx, produced, JS_UNDEFINED, JS_UNDEFINED);
    JS_FreeValue(ctx, produced);
    if (!JS_IsString(text)) {
        JS_FreeValue(ctx, text);
        return false;
    }
    size_t len = 0;
    const char* encoded = JS_ToCStringLen(ctx, &len, text);
    if (encoded) out->Append(Str(encoded, (int)len));
    JS_FreeCString(ctx, encoded);
    JS_FreeValue(ctx, text);
    return encoded != nullptr;
}

static void DeserializeScriptPanel(Entity<ScriptView> handle, Str json,
                                   Window* window, App* app, void* data) {
    auto* registered = (ScriptPanelClass*)data;
    ScriptView* view = registered ? handle.Get(app) : nullptr;
    if (!view || !view->object) return;
    JSContext* ctx = registered->impl->context;
    JSValue instance = view->object->value;
    JSValue method = JS_GetPropertyStr(ctx, instance, "deserialize");
    if (!JS_IsFunction(ctx, method)) {
        JS_FreeValue(ctx, method);
        return;
    }
    shell::CallScopeGuard scope = shell::ScopeEnter(
        window, app, ScopePhase::Event, handle.id, view->policy,
        registered->impl->owner, registered->application);
    (void)scope;
    JSValue payload = JS_ParseJSON(ctx, json.s, (size_t)json.len, "<panel>");
    if (JS_IsException(payload)) {
        Arena* arena = ArenaNew();
        log(ExceptionText(arena, ctx));
        ArenaDelete(arena);
        JS_FreeValue(ctx, method);
        return;
    }
    JSValue result = JS_Call(ctx, method, instance, 1, &payload);
    JS_FreeValue(ctx, payload);
    JS_FreeValue(ctx, method);
    if (JS_IsException(result)) {
        Arena* arena = ArenaNew();
        log(ExceptionText(arena, ctx));
        ArenaDelete(arena);
    } else {
        JS_FreeValue(ctx, result);
    }
    // The view described itself before the payload arrived, so what it
    // described is now out of date.
    view->dirty = true;
}

// The registration owns this: it is replaced by a later register_panel(name)
// and dropped when the panel manager goes. A class the runtime already retired
// is left alone — its JS value went with the context.
static void ReleaseScriptPanelClass(void* data) {
    auto* registered = (ScriptPanelClass*)data;
    if (!registered) return;
    if (registered->impl) {
        for (int i = 0; i < registered->impl->panelClasses.len; i++) {
            if (registered->impl->panelClasses[i] != registered) continue;
            for (int j = i + 1; j < registered->impl->panelClasses.len; j++) {
                registered->impl->panelClasses[j - 1] = registered->impl
                                                            ->panelClasses[j];
            }
            registered->impl->panelClasses.len--;
            break;
        }
        if (!JS_IsUndefined(registered->klass)) {
            JS_FreeValue(registered->impl->context, registered->klass);
        }
    }
    registered->klass = JS_UNDEFINED;
    PolicyRelease(registered->policy);
    delete registered;
}

static Str ShellRegisterPanelClass(ShellRuntimeImpl* impl, JSContext* ctx,
                                   App* app, Str panel, JSValueConst klass) {
    if (!app) {
        JS_ThrowTypeError(ctx,
                          "DockArea.register_panel(name, Class) needs a live "
                          "host call");
        return {};
    }
    auto* registered = new ScriptPanelClass();
    registered->impl = impl;
    registered->klass = JS_DupValue(ctx, klass);
    registered->policy = PolicyRetain(shell::ScopeCurrentPolicy());
    registered->application = (AppModule*)shell::ScopeCurrentApplication();
    VecAppend(impl->panelClasses, registered);

    shell::ShellPanelScript script = {};
    script.data = registered;
    script.build = BuildScriptPanel;
    script.serialize = SerializeScriptPanel;
    script.deserialize = DeserializeScriptPanel;
    script.release = ReleaseScriptPanelClass;
    // The registration outlives this runtime — it is filed in an App global —
    // so the class it holds is dropped from the runtime's own teardown rather
    // than through `release`. Registering the same name twice replaces the
    // class, which is what a hot reload does.
    registered->name = shell::ShellRegisterPanel(
        app, PolicyApplication(shell::ScopeCurrentPolicy()), panel, script);
    return registered->name;
}

// ─── The dockable layout ──────────────────────────────────────────────────
//
// crates/shell/src/engine/quickjs/dock_api.rs. `DockArea.new(id)` creates a
// base DockArea in the retained store and hands back a handle, for the reason
// an input's text is retained rather than described: the layout is what the
// *user* changed.
//
// Upstream queues `add_panel` and `load` until QuickJS has released its
// runtime lock, because both construct views and Rust cannot do that from
// inside a host call. This binding constructs a nested view directly — the
// same deviation `cx.new(Class)` already documents — so both apply where they
// are written, and a failure is catchable at the line that caused it.

static DockPlacement DockPlacementOf(Str name) {
    if (StrEq(name, StrL("left"))) return DockPlacement::Left;
    if (StrEq(name, StrL("right"))) return DockPlacement::Right;
    if (StrEq(name, StrL("bottom"))) return DockPlacement::Bottom;
    return DockPlacement::Center;
}

static const char* DockPlacementWord(DockPlacement placement) {
    switch (placement) {
        case DockPlacement::Left:
            return "left";
        case DockPlacement::Right:
            return "right";
        case DockPlacement::Bottom:
            return "bottom";
        default:
            return "center";
    }
}

static shell::RetainedEntry* LiveDock(JSContext* ctx, JSValueConst value,
                                      App** app, Window** window,
                                      DockState** state) {
    shell::EntityHandle handle = 0;
    if (!JsHandle(ctx, value, &handle)) return nullptr;
    ShellRuntimeImpl* impl = (ShellRuntimeImpl*)JS_GetContextOpaque(ctx);
    shell::RetainedEntry* entry = impl ? impl->retained.Find(handle) : nullptr;
    if (!entry || entry->kind != shell::RetainedKind::Dock) {
        JS_ThrowTypeError(ctx,
                          "this dock area has been released; a handle cannot "
                          "be used after release()");
        return nullptr;
    }
    shell::ScopeHostContext host = shell::ScopeCurrentHost();
    if (!host.IsSet()) {
        JS_ThrowTypeError(ctx,
                          "a DockArea needs a live host call; call it from "
                          "render(), init() or an event handler");
        return nullptr;
    }
    if (app) *app = host.GetApp();
    if (window) *window = host.GetWindow();
    if (state) {
        *state = entry->dock.Get(host.GetApp());
        if (!*state) {
            JS_ThrowTypeError(ctx, "this dock area is no longer live");
            return nullptr;
        }
    }
    return entry;
}

// Mutations are refused during a render pass for the reason every other one
// is: a frame that changed the layout it was describing would describe one
// layout and draw another. Layout is refused too, because a chrome handler
// runs inside the pass laying the frame out.
static bool RefuseDockChange(JSContext* ctx, const char* api) {
    if (shell::ScopeHasCurrent() &&
        (shell::ScopeCurrentPhase() == ScopePhase::Render ||
         shell::ScopeCurrentPhase() == ScopePhase::Layout)) {
        JS_ThrowTypeError(ctx,
                          "%s changes the layout and cannot be called while "
                          "one is being described or laid out; call it from "
                          "init(), an event handler or a task",
                          api);
        return true;
    }
    return false;
}

static JSValue NativeDockNew(JSContext* ctx, JSValueConst, int argc,
                             JSValueConst* argv) {
    if (RefuseDockChange(ctx, "DockArea.new(id)")) return JS_EXCEPTION;
    if (RefuseRetainedCreation(ctx, "DockArea.new(id)")) return JS_EXCEPTION;
    ShellRuntimeImpl* impl = (ShellRuntimeImpl*)JS_GetContextOpaque(ctx);
    shell::ScopeHostContext host = shell::ScopeCurrentHost();
    if (!impl || argc < 1 || !host.IsSet()) {
        return JS_ThrowTypeError(ctx,
                                 "DockArea.new(id) needs a live host call; "
                                 "call it from init() or an event handler");
    }
    Arena* arena = ArenaNew();
    Str id;
    if (!JsString(ctx, argv[0], arena, &id)) {
        ArenaDelete(arena);
        return JS_EXCEPTION;
    }
    int32_t version = -1;
    if (argc > 1 && !JS_IsNull(argv[1]) && !JS_IsUndefined(argv[1]) &&
        JS_ToInt32(ctx, &version, argv[1]) < 0) {
        ArenaDelete(arena);
        return JS_EXCEPTION;
    }
    Ctx cx = {host.GetApp(),
              host.GetWindow(),
              host.GetWindow() ? host.GetWindow()->frameArena : nullptr,
              {}};
    shell::EntityHandle handle = impl->retained.CreateDock(
        id, version >= 0, version, &cx, shell::ScopeCurrentView(),
        shell::ScopeCurrentApplication());
    ArenaDelete(arena);
    if (!handle) {
        return JS_ThrowRangeError(ctx,
                                  "the application reached gpui-shell's "
                                  "retained entity limit; release unused "
                                  "handles");
    }
    return JS_NewFloat64(ctx, (double)handle);
}

// The group a panel is added to: the Dock's own node, or the centre's first
// tab group, made if the area has none yet.
static int DockGroupFor(DockState* state, DockPlacement placement) {
    DockSide* side = DockSideOf(state, placement);
    if (side) {
        if (side->node < 0) side->node = DockNewTabs(state);
        return side->node;
    }
    int node = state->center;
    if (node < 0 || state->nodes[node].split) {
        node = node >= 0 && state->nodes[node].child.len > 0
                   ? state->nodes[node].child[0]
                   : DockNewTabs(state);
        if (state->center < 0) state->center = node;
    }
    return node;
}

static JSValue NativeDockAddPanel(JSContext* ctx, JSValueConst, int argc,
                                  JSValueConst* argv) {
    if (RefuseDockChange(ctx, "add_panel(view, options)")) return JS_EXCEPTION;
    App* app = nullptr;
    Window* window = nullptr;
    DockState* state = nullptr;
    if (argc < 8 || !LiveDock(ctx, argv[0], &app, &window, &state)) {
        return JS_EXCEPTION;
    }
    ShellRuntimeImpl* impl = (ShellRuntimeImpl*)JS_GetContextOpaque(ctx);
    uint32_t token = 0;
    if (JS_ToUint32(ctx, &token, argv[1]) < 0) return JS_EXCEPTION;
    NestedViewEntry* nested = FindNestedView(impl, token);
    if (!nested || !NestedViewIsCurrent(nested)) {
        return JS_ThrowTypeError(
            ctx,
            "add_panel(view, options) expects a view from cx.new(Class); this "
            "one has been released or belongs to another application");
    }
    Arena* arena = ArenaNew();
    Str name, placementName;
    if (!JsString(ctx, argv[2], arena, &name) ||
        !JsString(ctx, argv[3], arena, &placementName)) {
        ArenaDelete(arena);
        return JS_EXCEPTION;
    }
    double size = -1;
    if (JS_ToFloat64(ctx, &size, argv[4]) < 0) {
        ArenaDelete(arena);
        return JS_EXCEPTION;
    }
    DockPlacement placement = DockPlacementOf(placementName);
    // `shell:<application>/<panel>`, which is what the layout file holds and
    // what register_panel finds the class again by.
    Str qualified = shell::ShellPanelName(
        PolicyApplication(shell::ScopeCurrentPolicy()), name);
    shell::ShellPanelScript script = {};
    script.closable = JS_ToBool(ctx, argv[5]) != 0;
    script.zoomable = JS_ToBool(ctx, argv[6]) != 0;
    script.visible = JS_ToBool(ctx, argv[7]) != 0;
    // A panel the script *adds* gets the same serialize/deserialize hooks a
    // restored one does — otherwise a layout would round-trip only after a
    // restart, which is the one time nobody is watching. The registration
    // owns the class, so this borrows its hooks and does not free them.
    for (int i = 0; i < impl->panelClasses.len; i++) {
        ScriptPanelClass* registered = impl->panelClasses[i];
        if (!StrEq(registered->name, qualified)) continue;
        script.data = registered;
        script.serialize = SerializeScriptPanel;
        script.deserialize = DeserializeScriptPanel;
        break;
    }
    DockPanelDef def = shell::ScriptPanelNew(
        app, qualified, Entity<ScriptView>{nested->view}, &script);
    ArenaDelete(arena);
    if (!def.render) {
        return JS_ThrowInternalError(ctx, "could not build the dock panel");
    }
    int panel = DockAddPanelDef(state, def);
    int node = DockGroupFor(state, placement);
    DockTabsAdd(state, node, panel);
    DockSide* side = DockSideOf(state, placement);
    if (side && size >= 0) side->size = (float)size;
    if (window) AppInvalidate(window);
    return JS_UNDEFINED;
}

static JSValue NativeDockRemovePanel(JSContext* ctx, JSValueConst, int argc,
                                     JSValueConst* argv) {
    if (RefuseDockChange(ctx, "remove_panel(id)")) return JS_EXCEPTION;
    App* app = nullptr;
    Window* window = nullptr;
    DockState* state = nullptr;
    if (argc < 2 || !LiveDock(ctx, argv[0], &app, &window, &state)) {
        return JS_EXCEPTION;
    }
    double id = 0;
    if (JS_ToFloat64(ctx, &id, argv[1]) < 0) return JS_EXCEPTION;
    // Base removes a panel by index, which a script has no way to name — it
    // holds the id the area reported. Answers whether anything was removed, so
    // a script asking for a panel that has already gone hears about it.
    for (int i = 0; i < state->panels.len; i++) {
        if ((double)state->panels[i].id.AsU64() != id) continue;
        int node = DockNodeOfPanel(state, i);
        if (node < 0) return JS_NewBool(ctx, false);
        const DockNode& group = state->nodes[node];
        for (int ix = 0; ix < group.panel.len; ix++) {
            if (group.panel[ix] != i) continue;
            Ctx cx = {app, window, window ? window->frameArena : nullptr, {}};
            DockClosePanel(state, &cx, node, ix);
            return JS_NewBool(ctx, true);
        }
        return JS_NewBool(ctx, false);
    }
    return JS_NewBool(ctx, false);
}

// Walked node by node rather than through the panel list, because where a
// panel sits is half of what a script asks this for: which container holds it,
// where in that container it is, and whether it is the one on screen.
static JSValue NativeDockPanels(JSContext* ctx, JSValueConst, int argc,
                                JSValueConst* argv) {
    App* app = nullptr;
    DockState* state = nullptr;
    if (argc < 1 || !LiveDock(ctx, argv[0], &app, nullptr, &state)) {
        return JS_EXCEPTION;
    }
    StrBuilder out;
    JsonWriter json;
    json.out = &out;
    json.BeginArray();
    for (int node = 0; node < state->nodes.len; node++) {
        const DockNode& group = state->nodes[node];
        if (!group.used || group.split) continue;
        int active = DockActiveIx(state, node);
        DockPlacement placement = DockPlacementOfNode(state, node);
        for (int ix = 0; ix < group.panel.len; ix++) {
            int at = group.panel[ix];
            if (at < 0 || at >= state->panels.len) continue;
            const DockPanelDef& def = state->panels[at];
            json.BeginObject();
            json.Number("id", (double)def.id.AsU64());
            json.String("name", def.name);
            json.String("placement", Str(DockPlacementWord(placement)));
            json.Number("node", node);
            json.Number("index", ix);
            json.Bool("active", ix == active);
            json.Bool("visible", def.visible);
            json.Bool("closable", def.closable);
            json.Bool("zoomable", def.canZoom);
            json.EndObject();
        }
    }
    json.EndArray();
    Str text = out.TakeStr();
    JSValue result = JS_NewStringLen(ctx, text.s, (size_t)text.len);
    StrFree(text);
    return result;
}

static JSValue NativeDockDump(JSContext* ctx, JSValueConst, int argc,
                              JSValueConst* argv) {
    App* app = nullptr;
    DockState* state = nullptr;
    if (argc < 1 || !LiveDock(ctx, argv[0], &app, nullptr, &state)) {
        return JS_EXCEPTION;
    }
    DockAreaState saved;
    DockDump(state, &saved);
    StrBuilder out;
    DockAreaStateWrite(&saved, &out);
    Str text = out.TakeStr();
    JSValue result = JS_NewStringLen(ctx, text.s, (size_t)text.len);
    StrFree(text);
    return result;
}

static JSValue NativeDockLoad(JSContext* ctx, JSValueConst, int argc,
                              JSValueConst* argv) {
    if (RefuseDockChange(ctx, "load(state)")) return JS_EXCEPTION;
    App* app = nullptr;
    Window* window = nullptr;
    DockState* state = nullptr;
    shell::RetainedEntry* entry =
        argc >= 2 ? LiveDock(ctx, argv[0], &app, &window, &state) : nullptr;
    if (!entry) return JS_EXCEPTION;
    Arena* text = ArenaNew();
    Str json;
    if (!JsString(ctx, argv[1], text, &json)) {
        ArenaDelete(text);
        return JS_EXCEPTION;
    }
    // The parsed layout's strings outlive the call: DockLoad keeps the panel
    // names it was handed. The previous load's arena goes with the new one.
    Arena* arena = ArenaNew();
    DockAreaState parsed;
    bool ok = DockAreaStateParse(arena, json, &parsed);
    ArenaDelete(text);
    if (!ok) {
        ArenaDelete(arena);
        return JS_ThrowTypeError(ctx, "this is not a layout written by dump()");
    }
    if (!DockLoad(state, &parsed, arena, nullptr, app, window, entry->dock)) {
        ArenaDelete(arena);
        return JS_ThrowTypeError(ctx,
                                 "this layout has no centre to build from");
    }
    if (entry->dockArena) ArenaDelete(entry->dockArena);
    entry->dockArena = arena;
    if (window) AppInvalidate(window);
    return JS_UNDEFINED;
}

// The dock-by-dock properties. One entry point per verb rather than one taking
// a name, because that is how the prelude spells them and a combined call
// would make every reader pay for the others.
enum class DockVerb : int {
    Has,
    IsOpen,
    Toggle,
    Remove,
    Size,
    SetSize,
    SetCollapsible,
};

static JSValue NativeDockSideVerb(JSContext* ctx, JSValueConst, int argc,
                                  JSValueConst* argv, int magic) {
    DockVerb verb = (DockVerb)magic;
    static const char* const names[] = {
        "has_dock(placement)",
        "is_dock_open(placement)",
        "toggle_dock(placement)",
        "remove_dock(placement)",
        "dock_size(placement)",
        "set_dock_size(placement, size)",
        "set_dock_collapsible(placement, collapsible)"};
    bool mutation = verb == DockVerb::Toggle || verb == DockVerb::Remove ||
                    verb == DockVerb::SetSize ||
                    verb == DockVerb::SetCollapsible;
    if (mutation && RefuseDockChange(ctx, names[magic])) return JS_EXCEPTION;
    App* app = nullptr;
    Window* window = nullptr;
    DockState* state = nullptr;
    if (argc < 2 || !LiveDock(ctx, argv[0], &app, &window, &state)) {
        return JS_EXCEPTION;
    }
    Arena* arena = ArenaNew();
    Str placementName;
    if (!JsString(ctx, argv[1], arena, &placementName)) {
        ArenaDelete(arena);
        return JS_EXCEPTION;
    }
    DockPlacement placement = DockPlacementOf(placementName);
    ArenaDelete(arena);
    DockSide* side = DockSideOf(state, placement);
    Ctx cx = {app, window, window ? window->frameArena : nullptr, {}};
    switch (verb) {
        case DockVerb::Has:
            return JS_NewBool(ctx, side && side->node >= 0);
        case DockVerb::IsOpen:
            return JS_NewBool(ctx, side && side->node >= 0 && side->open);
        case DockVerb::Toggle:
            if (side && side->node >= 0) DockToggleSide(state, &cx, placement);
            if (window) AppInvalidate(window);
            return JS_UNDEFINED;
        case DockVerb::Remove:
            if (side) {
                side->node = -1;
                side->open = true;
            }
            if (window) AppInvalidate(window);
            return JS_UNDEFINED;
        case DockVerb::Size:
            return side && side->node >= 0 ? JS_NewFloat64(ctx, side->size)
                                           : JS_NULL;
        case DockVerb::SetSize: {
            double size = 0;
            if (argc < 3 || JS_ToFloat64(ctx, &size, argv[2]) < 0) {
                return JS_EXCEPTION;
            }
            if (side) side->size = (float)size;
            if (window) AppInvalidate(window);
            return JS_UNDEFINED;
        }
        case DockVerb::SetCollapsible:
            DockSetCollapsible(state, placement,
                               argc > 2 && JS_ToBool(ctx, argv[2]) != 0);
            if (window) AppInvalidate(window);
            return JS_UNDEFINED;
    }
    return JS_UNDEFINED;
}

static JSValue NativeDockAreaVerb(JSContext* ctx, JSValueConst, int argc,
                                  JSValueConst* argv, int magic) {
    static const char* const names[] = {"is_locked()", "set_locked(locked)",
                                        "is_zoomed()", "zoom_out()",
                                        "release()"};
    bool mutation = magic == 1 || magic == 3;
    if (mutation && RefuseDockChange(ctx, names[magic])) return JS_EXCEPTION;
    if (magic == 4) {
        shell::EntityHandle handle = 0;
        ShellRuntimeImpl* impl = (ShellRuntimeImpl*)JS_GetContextOpaque(ctx);
        if (argc < 1 || !impl || !JsHandle(ctx, argv[0], &handle)) {
            return JS_NewBool(ctx, false);
        }
        Vec<shell::CallbackId> retired;
        bool released = impl->retained.Release(handle, &retired);
        for (int i = 0; i < retired.len; i++) {
            impl->callbacks.RetireId(ctx, retired[i]);
        }
        VecReset(retired);
        return JS_NewBool(ctx, released);
    }
    App* app = nullptr;
    Window* window = nullptr;
    DockState* state = nullptr;
    if (argc < 1 || !LiveDock(ctx, argv[0], &app, &window, &state)) {
        return JS_EXCEPTION;
    }
    Ctx cx = {app, window, window ? window->frameArena : nullptr, {}};
    switch (magic) {
        case 0:
            return JS_NewBool(ctx, state->locked);
        case 1:
            state->locked = argc > 1 && JS_ToBool(ctx, argv[1]) != 0;
            if (window) AppInvalidate(window);
            return JS_UNDEFINED;
        case 2:
            return JS_NewBool(ctx, state->zoomPanel >= 0);
        default:
            if (state->zoomPanel >= 0) {
                DockToggleZoom(state, &cx, state->zoomPanel);
            }
            if (window) AppInvalidate(window);
            return JS_UNDEFINED;
    }
}

static JSValue NativeDockOn(JSContext* ctx, JSValueConst, int argc,
                            JSValueConst* argv) {
    App* app = nullptr;
    Window* window = nullptr;
    DockState* state = nullptr;
    shell::RetainedEntry* entry =
        argc >= 3 ? LiveDock(ctx, argv[0], &app, &window, &state) : nullptr;
    if (!entry) return JS_EXCEPTION;
    Arena* arena = ArenaNew();
    Str name;
    bool named = JsString(ctx, argv[1], arena, &name);
    bool isLayout = named && StrEq(name, StrL("layout_changed"));
    ArenaDelete(arena);
    if (!named) return JS_EXCEPTION;
    if (!isLayout) {
        return JS_ThrowTypeError(
            ctx,
            "unknown dock event; the only one is \"layout_changed\", which "
            "fires on every edit — including each step of a drag, so save on "
            "a timer rather than on every one");
    }
    if (!JS_IsFunction(ctx, argv[2])) {
        return JS_ThrowTypeError(
            ctx, "on(\"layout_changed\", handler) expects a function");
    }
    ShellRuntimeImpl* impl = (ShellRuntimeImpl*)JS_GetContextOpaque(ctx);
    // Persistent: a long-lived subscription outlives the snapshot that
    // registered it, the way an input's `on(...)` does, and is retired with
    // the retained state rather than with the render pass.
    shell::CallbackId callback = impl->callbacks.PushPersistent(
        ctx, argv[2], shell::ScopeCurrentView(), shell::ScopeCurrentPolicy(),
        (AppModule*)shell::ScopeCurrentApplication());
    if (callback == UINT64_MAX) {
        return JS_ThrowInternalError(ctx,
                                     "a dock handler was registered outside a "
                                     "live application");
    }
    // Replaces rather than appends, matching every other on(...) in this API:
    // registering twice means the second handler, not both of them.
    shell::CallbackId replaced = 0;
    shell::EntityHandle handle = 0;
    JsHandle(ctx, argv[0], &handle);
    impl->retained.AddCallback(handle, shell::RetainedEvent::DockLayoutChanged,
                               callback, true, &replaced);
    if (replaced) impl->callbacks.RetireId(ctx, replaced);
    state->onEvent = ListenTo(Entity<ScriptView>{shell::ScopeCurrentView()},
                              &ScriptView::OnDockEvent, (intptr_t)callback);
    return JS_NewBool(ctx, true);
}

static JSValue NativeDockAreaElement(JSContext* ctx, JSValueConst, int argc,
                                     JSValueConst* argv) {
    shell::EntityHandle handle = 0;
    ShellRuntimeImpl* impl = (ShellRuntimeImpl*)JS_GetContextOpaque(ctx);
    if (!impl || argc < 1 || !JsHandle(ctx, argv[0], &handle)) {
        return JS_EXCEPTION;
    }
    shell::RetainedEntry* entry = impl->retained.Find(handle);
    if (!entry || entry->kind != shell::RetainedKind::Dock) {
        return JS_ThrowTypeError(
            ctx, "this dock area has been released and can no longer be drawn");
    }
    shell::SpecId id = 0;
    shell::SpecError failure = {};
    if (!impl->scratch->PushDockArea(handle, &id, &failure)) {
        return SpecFailure(ctx, failure);
    }
    return JS_NewUint32(ctx, id);
}

static JSValue NativeDockRegisterPanel(JSContext* ctx, JSValueConst, int argc,
                                       JSValueConst* argv) {
    ShellRuntimeImpl* impl = (ShellRuntimeImpl*)JS_GetContextOpaque(ctx);
    shell::ScopeHostContext host = shell::ScopeCurrentHost();
    if (!impl || argc < 2 || !host.IsSet()) {
        return JS_ThrowTypeError(
            ctx,
            "DockArea.register_panel(name, Class) needs a live host call; "
            "call it from init() or an event handler");
    }
    Arena* arena = ArenaNew();
    Str panel;
    if (!JsString(ctx, argv[0], arena, &panel) ||
        !JS_IsFunction(ctx, argv[1])) {
        ArenaDelete(arena);
        return JS_ThrowTypeError(
            ctx,
            "DockArea.register_panel(name, Class) expects the View subclass "
            "the panel is rebuilt from");
    }
    Str name =
        ShellRegisterPanelClass(impl, ctx, host.GetApp(), panel, argv[1]);
    ArenaDelete(arena);
    if (!name) return JS_EXCEPTION;
    return JS_NewStringLen(ctx, name.s, (size_t)name.len);
}

static bool InstallRuntime(ShellRuntimeImpl* impl, ShellError* error) {
    JSValue global = JS_GetGlobalObject(impl->context);
#if GPUI_OS_WINDOWS
    const char* platform = "windows";
#elif GPUI_OS_MAC
    const char* platform = "macos";
#elif GPUI_OS_WASM
    const char* platform = "emscripten";
#else
    const char* platform = "linux";
#endif
#if defined(_M_ARM64) || defined(__aarch64__)
    const char* architecture = "aarch64";
#elif defined(__wasm32__)
    const char* architecture = "wasm32";
#elif defined(_M_IX86) || defined(__i386__)
    const char* architecture = "x86";
#else
    const char* architecture = "x86_64";
#endif
    JS_SetPropertyStr(impl->context, global, "__shell_is_windows",
                      JS_NewBool(impl->context, GPUI_OS_WINDOWS));
    JS_SetPropertyStr(impl->context, global, "__shell_platform",
                      JS_NewString(impl->context, platform));
    JS_SetPropertyStr(impl->context, global, "__shell_arch",
                      JS_NewString(impl->context, architecture));
    // The window's own measurements and controls, and the two halves of
    // GPUI's propagation answer. Installed before the prelude, which builds
    // the `window` object over them.
    SetGlobalFunction(impl->context, global, "__window_rem_size",
                      NativeWindowRemSize, 0);
    SetGlobalFunction(impl->context, global, "__window_line_height",
                      NativeWindowLineHeight, 0);
    SetGlobalFunction(impl->context, global, "__window_viewport_size",
                      NativeWindowViewportSize, 0);
    SetGlobalFunction(impl->context, global, "__window_bounds",
                      NativeWindowBounds, 0);
    SetGlobalFunction(impl->context, global, "__window_mouse_position",
                      NativeWindowMousePosition, 0);
    SetGlobalFunction(impl->context, global, "__window_appearance",
                      NativeWindowAppearance, 0);
    SetGlobalFunction(impl->context, global, "__window_is_active",
                      NativeWindowIsActive, 0);
    SetGlobalFunction(impl->context, global, "__window_is_maximized",
                      NativeWindowIsMaximized, 0);
    SetGlobalFunction(impl->context, global, "__window_refresh",
                      NativeWindowRefresh, 0);
    SetGlobalMagicFunction(impl->context, global, "__window_focus_next",
                           NativeWindowFocusMove, 0, 0);
    SetGlobalMagicFunction(impl->context, global, "__window_focus_prev",
                           NativeWindowFocusMove, 0, 1);
    SetGlobalMagicFunction(impl->context, global, "__window_activate",
                           NativeWindowCommand, 0, 0);
    SetGlobalMagicFunction(impl->context, global, "__window_minimize",
                           NativeWindowCommand, 0, 1);
    SetGlobalMagicFunction(impl->context, global, "__window_zoom",
                           NativeWindowCommand, 0, 2);
    SetGlobalFunction(impl->context, global, "__stop_propagation",
                      NativeStopPropagation, 0);
    SetGlobalFunction(impl->context, global, "__propagate", NativePropagate, 0);
    SetGlobalFunction(impl->context, global, "__bind_keys", NativeBindKeys, 1);
    SetGlobalFunction(impl->context, global, "__dispatch_action",
                      NativeDispatchAction, 1);
    SetGlobalFunction(impl->context, global, "__pagination_items",
                      NativePaginationItems, 3);
    SetGlobalFunction(impl->context, global, "__calendar_state_new",
                      NativeCalendarNew, 0);
    SetGlobalFunction(impl->context, global, "__calendar_month_days",
                      NativeCalendarMonthDays, 1);
    SetGlobalMagicFunction(impl->context, global, "__calendar_year",
                           NativeCalendarRead, 1, 0);
    SetGlobalMagicFunction(impl->context, global, "__calendar_month",
                           NativeCalendarRead, 1, 1);
    SetGlobalMagicFunction(impl->context, global, "__calendar_today",
                           NativeCalendarRead, 1, 2);
    SetGlobalMagicFunction(impl->context, global, "__calendar_value",
                           NativeCalendarRead, 1, 3);
    SetGlobalFunction(impl->context, global, "__calendar_set_value",
                      NativeCalendarSetValue, 2);
    SetGlobalMagicFunction(impl->context, global, "__calendar_next_month",
                           NativeCalendarStep, 1, 0);
    SetGlobalMagicFunction(impl->context, global, "__calendar_prev_month",
                           NativeCalendarStep, 1, 1);
    SetGlobalFunction(impl->context, global, "__calendar_on", NativeCalendarOn,
                      3);
    SetGlobalFunction(impl->context, global, "__calendar_release",
                      NativeCalendarRelease, 1);
    SetGlobalFunction(impl->context, global, "__dock_area_new", NativeDockNew,
                      2);
    SetGlobalFunction(impl->context, global, "__dock_add_panel",
                      NativeDockAddPanel, 8);
    SetGlobalFunction(impl->context, global, "__dock_remove_panel",
                      NativeDockRemovePanel, 2);
    SetGlobalFunction(impl->context, global, "__dock_panels", NativeDockPanels,
                      1);
    SetGlobalFunction(impl->context, global, "__dock_dump", NativeDockDump, 1);
    SetGlobalFunction(impl->context, global, "__dock_load", NativeDockLoad, 2);
    SetGlobalMagicFunction(impl->context, global, "__dock_has",
                           NativeDockSideVerb, 2, (int)DockVerb::Has);
    SetGlobalMagicFunction(impl->context, global, "__dock_is_open",
                           NativeDockSideVerb, 2, (int)DockVerb::IsOpen);
    SetGlobalMagicFunction(impl->context, global, "__dock_toggle",
                           NativeDockSideVerb, 2, (int)DockVerb::Toggle);
    SetGlobalMagicFunction(impl->context, global, "__dock_remove",
                           NativeDockSideVerb, 2, (int)DockVerb::Remove);
    SetGlobalMagicFunction(impl->context, global, "__dock_size",
                           NativeDockSideVerb, 2, (int)DockVerb::Size);
    SetGlobalMagicFunction(impl->context, global, "__dock_set_size",
                           NativeDockSideVerb, 3, (int)DockVerb::SetSize);
    SetGlobalMagicFunction(impl->context, global, "__dock_set_collapsible",
                           NativeDockSideVerb, 3,
                           (int)DockVerb::SetCollapsible);
    SetGlobalMagicFunction(impl->context, global, "__dock_is_locked",
                           NativeDockAreaVerb, 1, 0);
    SetGlobalMagicFunction(impl->context, global, "__dock_set_locked",
                           NativeDockAreaVerb, 2, 1);
    SetGlobalMagicFunction(impl->context, global, "__dock_is_zoomed",
                           NativeDockAreaVerb, 1, 2);
    SetGlobalMagicFunction(impl->context, global, "__dock_zoom_out",
                           NativeDockAreaVerb, 1, 3);
    SetGlobalMagicFunction(impl->context, global, "__dock_release",
                           NativeDockAreaVerb, 1, 4);
    SetGlobalFunction(impl->context, global, "__dock_on", NativeDockOn, 3);
    SetGlobalFunction(impl->context, global, "__dock_area_element",
                      NativeDockAreaElement, 1);
    SetGlobalFunction(impl->context, global, "__dock_register_panel",
                      NativeDockRegisterPanel, 2);
    SetGlobalFunction(impl->context, global, "__component", NativeComponent, 4);
    SetGlobalFunction(impl->context, global, "__path", NativePath, 6);
    SetGlobalFunction(impl->context, global, "__attach", NativeAttach, 2);
    SetGlobalFunction(impl->context, global, "__state", NativeState, 2);
    SetGlobalFunction(impl->context, global, "__slot", NativeSlot, 3);
    SetGlobalFunction(impl->context, global, "__apply", NativeApply, 3);
    // Templates. `begin` swaps the description being recorded for a fresh one
    // so the body's ids start at zero, `end` takes it back out, and `abort`
    // puts the interrupted one back when the body threw. Three calls rather
    // than one because the body runs in JavaScript between them.
    SetGlobalFunction(impl->context, global, "__template_begin",
                      NativeTemplateBegin, 1);
    SetGlobalFunction(impl->context, global, "__template_end",
                      NativeTemplateEnd, 1);
    SetGlobalFunction(impl->context, global, "__template_abort",
                      NativeTemplateAbort, 0);
    SetGlobalFunction(impl->context, global, "__template_instantiate",
                      NativeTemplateInstantiate, 2);
    SetGlobalFunction(impl->context, global, "__text_slot", NativeTextSlot, 1);
    SetGlobalFunction(impl->context, global, "__theme_snapshot",
                      NativeThemeSnapshot, 1);
    SetGlobalFunction(impl->context, global, "__theme_revision",
                      NativeThemeRevision, 1);
    SetGlobalFunction(impl->context, global, "__set_theme", NativeSetTheme, 1);
    SetGlobalFunction(impl->context, global, "__open_url", NativeOpenUrl, 2);
    SetGlobalFunction(impl->context, global, "__host_call", NativeHostCall, 3);
    SetGlobalFunction(impl->context, global, "__host_async_call",
                      NativeHostAsyncCall, 3);
    SetGlobalFunction(impl->context, global, "__cx_notify", NativeNotify, 1);
    SetGlobalFunction(impl->context, global, "__cx_notify_current",
                      NativeNotifyCurrent, 0);
    SetGlobalFunction(impl->context, global, "__view_new", NativeViewNew, 2);
    SetGlobalFunction(impl->context, global, "__view_set_props",
                      NativeViewSetProps, 2);
    SetGlobalFunction(impl->context, global, "__view_release",
                      NativeViewRelease, 1);
    SetGlobalFunction(impl->context, global, "__child_view", NativeChildView,
                      1);
    SetGlobalFunction(impl->context, global, "__open_dialog", NativeOpenDialog,
                      2);
    SetGlobalFunction(impl->context, global, "__close_dialog",
                      NativeCloseDialog, 0);
    SetGlobalFunction(impl->context, global, "__close_all_dialogs",
                      NativeCloseAllDialogs, 0);
    SetGlobalFunction(impl->context, global, "__has_active_dialog",
                      NativeHasDialog, 0);
    SetGlobalFunction(impl->context, global, "__open_sheet", NativeOpenSheet,
                      2);
    SetGlobalFunction(impl->context, global, "__close_sheet", NativeCloseSheet,
                      0);
    SetGlobalFunction(impl->context, global, "__has_active_sheet",
                      NativeHasSheet, 0);
    SetGlobalFunction(impl->context, global, "__push_toast", NativePushToast,
                      1);
    SetGlobalFunction(impl->context, global, "__remove_toast",
                      NativeRemoveToast, 1);
    SetGlobalFunction(impl->context, global, "__clear_toasts",
                      NativeClearToasts, 0);
    SetGlobalFunction(impl->context, global, "__show_fps_monitor",
                      NativeShowFpsMonitor, 1);
    SetGlobalFunction(impl->context, global, "__hide_fps_monitor",
                      NativeHideFpsMonitor, 0);
    SetGlobalFunction(impl->context, global, "__fps_monitor_visible",
                      NativeFpsMonitorVisible, 0);
    SetGlobalFunction(impl->context, global, "__task_new", NativeTaskNew, 1);
    SetGlobalFunction(impl->context, global, "__task_finish", NativeTaskFinish,
                      1);
    SetGlobalFunction(impl->context, global, "__task_reject", NativeTaskReject,
                      2);
    SetGlobalFunction(impl->context, global, "__task_cancel", NativeTaskCancel,
                      1);
    SetGlobalFunction(impl->context, global, "__task_is_done", NativeTaskIsDone,
                      1);
    SetGlobalFunction(impl->context, global, "__sleep", NativeSleep, 1);
    SetGlobalMagicFunction(impl->context, global, "__timer_after", NativeTimer,
                           3, 0);
    SetGlobalMagicFunction(impl->context, global, "__timer_every", NativeTimer,
                           3, 1);
    SetGlobalFunction(impl->context, global, "__storage_get", NativeStorageGet,
                      2);
    SetGlobalFunction(impl->context, global, "__storage_set", NativeStorageSet,
                      3);
    SetGlobalFunction(impl->context, global, "__storage_remove",
                      NativeStorageRemove, 2);
    SetGlobalFunction(impl->context, global, "__storage_clear",
                      NativeStorageClear, 1);
    SetGlobalFunction(impl->context, global, "__storage_length",
                      NativeStorageLength, 1);
    SetGlobalFunction(impl->context, global, "__storage_key", NativeStorageKey,
                      2);
    SetGlobalFunction(impl->context, global, "__storage_flush",
                      NativeStorageFlush, 1);
    SetGlobalMagicFunction(impl->context, global, "__clipboard_read_text",
                           NativeClipboard, 0, 0);
    SetGlobalMagicFunction(impl->context, global, "__clipboard_write_text",
                           NativeClipboard, 1, 1);
    SetGlobalMagicFunction(impl->context, global, "__console_log",
                           NativeConsole, 1, 0);
    SetGlobalMagicFunction(impl->context, global, "__console_debug",
                           NativeConsole, 1, 1);
    SetGlobalMagicFunction(impl->context, global, "__console_info",
                           NativeConsole, 1, 2);
    SetGlobalMagicFunction(impl->context, global, "__console_warn",
                           NativeConsole, 1, 3);
    SetGlobalMagicFunction(impl->context, global, "__console_error",
                           NativeConsole, 1, 4);
    SetGlobalFunction(impl->context, global, "__crypto_sha256", NativeSha256,
                      1);
    SetGlobalFunction(impl->context, global, "__crypto_random", NativeRandom,
                      1);
    SetGlobalMagicFunction(impl->context, global, "__zlib_deflate", NativeZlib,
                           1, 0);
    SetGlobalMagicFunction(impl->context, global, "__zlib_inflate", NativeZlib,
                           1, 1);
    SetGlobalMagicFunction(impl->context, global, "__zlib_gzip", NativeZlib, 1,
                           2);
    SetGlobalMagicFunction(impl->context, global, "__zlib_gunzip", NativeZlib,
                           1, 3);
    SetGlobalFunction(impl->context, global, "__fetch_send", NativeFetch, 4);
    SetGlobalMagicFunction(impl->context, global, "__fs_read", NativeFs, 2,
                           (int)shell::FsOperation::Read);
    SetGlobalMagicFunction(impl->context, global, "__fs_write", NativeFs, 2,
                           (int)shell::FsOperation::Write);
    SetGlobalMagicFunction(impl->context, global, "__fs_readdir", NativeFs, 2,
                           (int)shell::FsOperation::ReadDirectory);
    SetGlobalMagicFunction(impl->context, global, "__fs_exists", NativeFs, 1,
                           (int)shell::FsOperation::Exists);
    SetGlobalMagicFunction(impl->context, global, "__fs_unlink", NativeFs, 1,
                           (int)shell::FsOperation::RemoveFile);
    SetGlobalMagicFunction(impl->context, global, "__fs_rmdir", NativeFs, 1,
                           (int)shell::FsOperation::RemoveDirectory);
    SetGlobalMagicFunction(impl->context, global, "__fs_mkdir", NativeFs, 2,
                           (int)shell::FsOperation::MakeDirectory);
    SetGlobalFunction(impl->context, global, "__process_run", NativeProcessRun,
                      2);
    SetGlobalFunction(impl->context, global, "__process_exit",
                      NativeProcessExit, 1);
    SetGlobalFunction(impl->context, global, "__input_state_new",
                      NativeInputStateNew, 2);
    SetGlobalFunction(impl->context, global, "__textarea_state_new",
                      NativeTextareaStateNew, 3);
    SetGlobalFunction(impl->context, global, "__input_value", NativeInputValue,
                      1);
    SetGlobalFunction(impl->context, global, "__textarea_value",
                      NativeInputValue, 1);
    SetGlobalFunction(impl->context, global, "__input_set_value",
                      NativeInputSetValue, 2);
    SetGlobalFunction(impl->context, global, "__textarea_set_value",
                      NativeInputSetValue, 2);
    SetGlobalMagicFunction(impl->context, global, "__input_set_step",
                           NativeInputNumberOption, 2, 0);
    SetGlobalMagicFunction(impl->context, global, "__input_set_min",
                           NativeInputNumberOption, 2, 1);
    SetGlobalMagicFunction(impl->context, global, "__input_set_max",
                           NativeInputNumberOption, 2, 2);
    SetGlobalMagicFunction(impl->context, global, "__input_set_masked",
                           NativeInputFlag, 2, 0);
    SetGlobalMagicFunction(impl->context, global, "__input_set_loading",
                           NativeInputFlag, 2, 1);
    SetGlobalMagicFunction(impl->context, global, "__textarea_set_rows",
                           NativeTextareaRows, 2, 0);
    SetGlobalMagicFunction(impl->context, global, "__textarea_set_auto_grow",
                           NativeTextareaRows, 3, 1);
    SetGlobalMagicFunction(impl->context, global, "__textarea_set_soft_wrap",
                           NativeTextareaRows, 2, 2);
    SetGlobalFunction(impl->context, global, "__slider_state_new",
                      NativeSliderStateNew, 5);
    SetGlobalFunction(impl->context, global, "__slider_value",
                      NativeSliderValue, 1);
    SetGlobalFunction(impl->context, global, "__slider_set_value",
                      NativeSliderSetValue, 2);
    SetGlobalFunction(impl->context, global, "__slider_bounds",
                      NativeSliderBounds, 1);
    SetGlobalFunction(impl->context, global, "__otp_state_new",
                      NativeOtpStateNew, 3);
    SetGlobalFunction(impl->context, global, "__otp_value", NativeOtpValue, 1);
    SetGlobalFunction(impl->context, global, "__otp_set_value",
                      NativeOtpSetValue, 2);
    SetGlobalMagicFunction(impl->context, global, "__otp_len",
                           NativeOtpProperty, 1, 0);
    SetGlobalMagicFunction(impl->context, global, "__otp_is_masked",
                           NativeOtpProperty, 1, 1);
    SetGlobalMagicFunction(impl->context, global, "__otp_set_masked",
                           NativeOtpProperty, 2, 2);
    SetGlobalMagicFunction(impl->context, global, "__otp_focus",
                           NativeOtpProperty, 1, 3);
    SetGlobalFunction(impl->context, global, "__input_on", NativeRetainedOn, 3);
    SetGlobalFunction(impl->context, global, "__textarea_on", NativeRetainedOn,
                      3);
    SetGlobalFunction(impl->context, global, "__slider_on", NativeRetainedOn,
                      3);
    SetGlobalFunction(impl->context, global, "__otp_on", NativeRetainedOn, 3);
    SetGlobalFunction(impl->context, global, "__input_release",
                      NativeRetainedRelease, 1);
    SetGlobalFunction(impl->context, global, "__textarea_release",
                      NativeRetainedRelease, 1);
    SetGlobalFunction(impl->context, global, "__slider_release",
                      NativeRetainedRelease, 1);
    SetGlobalFunction(impl->context, global, "__otp_release",
                      NativeRetainedRelease, 1);
    SetGlobalFunction(impl->context, global, "__retained_component",
                      NativeRetainedComponent, 2);
    SetGlobalFunction(impl->context, global, "__focus_handle_new",
                      NativeFocusNew, 0);
    SetGlobalMagicFunction(impl->context, global, "__focus_focus",
                           NativeFocusOp, 1, 0);
    SetGlobalMagicFunction(impl->context, global, "__focus_is_focused",
                           NativeFocusOp, 1, 1);
    SetGlobalFunction(impl->context, global, "__focus_release",
                      NativeRetainedRelease, 1);
    SetGlobalFunction(impl->context, global, "__virtual_scroll_new",
                      NativeVirtualScrollNew, 0);
    SetGlobalMagicFunction(impl->context, global, "__virtual_scroll_to_item",
                           NativeVirtualScrollOp, 3, 0);
    SetGlobalMagicFunction(impl->context, global, "__virtual_scroll_to_bottom",
                           NativeVirtualScrollOp, 1, 1);
    SetGlobalFunction(impl->context, global, "__virtual_scroll_release",
                      NativeRetainedRelease, 1);
    SetGlobalMagicFunction(impl->context, global, "__v_virtual_list",
                           NativeVirtualList, 5, 0);
    SetGlobalMagicFunction(impl->context, global, "__h_virtual_list",
                           NativeVirtualList, 5, 1);
    JS_FreeValue(impl->context, global);
    BeginExecution(impl);
    JSValue result = JS_Eval(impl->context, kPrelude, sizeof(kPrelude) - 1,
                             "<gpui-shell prelude>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(result)) return CaptureException(impl, error);
    JS_FreeValue(impl->context, result);
    if (!ShellDevelopmentMode()) {
        BeginExecution(impl);
        result = JS_Eval(impl->context, kSandbox, sizeof(kSandbox) - 1,
                         "<gpui-shell sandbox>", JS_EVAL_TYPE_GLOBAL);
        if (JS_IsException(result)) return CaptureException(impl, error);
        JS_FreeValue(impl->context, result);
    }
    return true;
}

ShellRuntime::ShellRuntime() = default;

ShellRuntime::~ShellRuntime() {
    if (control) control->runtime = nullptr;
    if (impl) {
        if (impl->context) {
            for (int i = impl->tasks.len - 1; i >= 0; i--) {
                DestroyTask(impl, impl->tasks[i], true);
            }
            VecReset(impl->tasks);
            if (impl->taskDriver.IsValid() && impl->taskApp) {
                ShellTaskDriver* driver = impl->taskDriver.Get(impl->taskApp);
                if (driver) driver->runtime = nullptr;
                EntityDrop(impl->taskApp, impl->taskDriver.id);
                impl->taskDriver = {};
            }
            Vec<shell::CallbackId> retired;
            impl->retained.Clear(&retired);
            for (int i = 0; i < retired.len; i++) {
                impl->callbacks.RetireId(impl->context, retired[i]);
            }
            VecReset(retired);
            impl->callbacks.Clear(impl->context);
        }
        // Each holds a live view class, which must be released while the
        // context still exists — and the panel registry keeps a second
        // reference to every one of them in an App global that outlives this,
        // so clearing the vector is not enough on its own. Retiring the class
        // leaves the registration in place, answering with a draw-nothing
        // placeholder that still carries the panel's persisted state forward.
        for (int i = 0; i < impl->panelClasses.len; i++) {
            ScriptPanelClass* registered = impl->panelClasses[i];
            if (impl->context && !JS_IsUndefined(registered->klass)) {
                JS_FreeValue(impl->context, registered->klass);
            }
            registered->klass = JS_UNDEFINED;
            registered->impl = nullptr;
            PolicyRelease(registered->policy);
            registered->policy = nullptr;
        }
        VecReset(impl->panelClasses);
        for (int i = 0; i < impl->chromeCache.len; i++) {
            StrFree(impl->chromeCache[i]->payload);
            delete impl->chromeCache[i]->arena;
            delete impl->chromeCache[i];
        }
        VecReset(impl->chromeCache);
        if (impl->discovery) {
            delete impl->scratch;
            impl->scratch = impl->discovery->saved;
            VecReset(impl->discovery->slots);
            delete impl->discovery;
            impl->discovery = nullptr;
        }
        delete impl->scratch;
        StrFree(impl->dependencyCacheRoot);
        for (int i = 0; i < impl->templates.len; i++) delete impl->templates[i];
        VecReset(impl->templates);
        for (int i = 0; i < impl->modules.len; i++) {
            StrFree(impl->modules[i]->root);
            impl->modules[i]->dependencies.Free();
            delete impl->modules[i];
        }
        VecReset(impl->modules);
        VecReset(impl->views);
        VecReset(impl->nestedViews);
        if (impl->context) {
            JS_SetContextOpaque(impl->context, nullptr);
            JS_FreeContext(impl->context);
        }
        if (impl->jsRuntime) JS_FreeRuntime(impl->jsRuntime);
        delete impl;
    }
    ControlRelease(control);
}

ShellRuntime* ShellRuntime::New(App*, ShellError* error) {
    ShellErrorClear(error);
    ShellRuntime* runtime = new ShellRuntime();
    runtime->impl = new ShellRuntimeImpl();
    runtime->impl->owner = runtime;
    runtime->control = new ShellRuntimeControl();
    runtime->control->runtime = runtime;
    runtime->impl->scratch = new shell::SpecArena();
    runtime->impl->jsRuntime = JS_NewRuntime();
    if (!runtime->impl->jsRuntime) {
        SetError(error, StrL("could not create the QuickJS runtime"));
        runtime->Release();
        return nullptr;
    }
    JS_SetMemoryLimit(runtime->impl->jsRuntime, (size_t)256 * 1024 * 1024);
    JS_SetMaxStackSize(runtime->impl->jsRuntime, (size_t)1024 * 1024);
    JS_SetInterruptHandler(runtime->impl->jsRuntime, Interrupt, runtime->impl);
    JS_SetModuleLoaderFunc(runtime->impl->jsRuntime, ModuleNormalize,
                           ModuleLoad, runtime->impl);
    runtime->impl->context = JS_NewContext(runtime->impl->jsRuntime);
    if (!runtime->impl->context) {
        SetError(error, StrL("could not create the QuickJS context"));
        runtime->Release();
        return nullptr;
    }
    JS_SetContextOpaque(runtime->impl->context, runtime->impl);
    if (!InstallRuntime(runtime->impl, error)) {
        runtime->Release();
        return nullptr;
    }
    return runtime;
}

ShellRuntime* ShellRuntime::Retain() {
    refs++;
    return this;
}

void ShellRuntime::Release() {
    if (--refs == 0) delete this;
}

static ViewType* LoadModule(ShellRuntime* runtime, Str name, Str source,
                            AppModule* application, Policy* policy,
                            ShellError* error) {
    ShellRuntimeImpl* impl = ShellRuntimeAccess::Impl(runtime);
    shell::CallScopeGuard scope = shell::ScopeEnter(
        nullptr, nullptr, ScopePhase::Task, {}, policy, runtime, application);
    BeginExecution(impl);
    JSValue module =
        JS_Eval(impl->context, source.s, (size_t)source.len, name.s,
                JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    if (JS_IsException(module)) {
        CaptureException(impl, error);
        return nullptr;
    }
    JSModuleDef* definition = (JSModuleDef*)JS_VALUE_GET_PTR(module);
    JSValue evaluated =
        JS_EvalFunction(impl->context, JS_DupValue(impl->context, module));
    if (JS_IsException(evaluated)) {
        JS_FreeValue(impl->context, module);
        CaptureException(impl, error);
        return nullptr;
    }
    bool complete = Await(impl, evaluated, error);
    JS_FreeValue(impl->context, evaluated);
    if (!complete) {
        JS_FreeValue(impl->context, module);
        return nullptr;
    }
    JSValue nameSpace = JS_GetModuleNamespace(impl->context, definition);
    JSValue value = JS_GetPropertyStr(impl->context, nameSpace, "default");
    JS_FreeValue(impl->context, nameSpace);
    JS_FreeValue(impl->context, module);
    if (JS_IsException(value)) {
        CaptureException(impl, error);
        return nullptr;
    }
    if (!JS_IsFunction(impl->context, value)) {
        JS_FreeValue(impl->context, value);
        SetError(
            error,
            StrL("main.js must `export default` a class that extends View"));
        return nullptr;
    }
    ViewType* type = new ViewType();
    type->runtime = runtime->Retain();
    type->value = value;
    type->application = application;
    return type;
}

ViewType* ShellRuntime::LoadSource(Str name, Str source, ShellError* error) {
    return LoadSource(name, source, nullptr, error);
}

ViewType* ShellRuntime::LoadSource(Str name, Str source, Policy* policy,
                                   ShellError* error) {
    ShellErrorClear(error);
    if (!name.s || name.len == 0) name = StrL("<module>");
    Str ownedName = StrDup(name);
    ViewType* result =
        LoadModule(this, ownedName, source, nullptr, policy, error);
    StrFree(ownedName);
    return result;
}

void ShellRuntime::SetDependencyCacheRoot(Str root) {
    StrFree(impl->dependencyCacheRoot);
    impl->dependencyCacheRoot = StrDup(root);
}

ViewType* ShellRuntime::LoadApp(Str directory, Str entry, ShellError* error) {
    return LoadApp(directory, entry, nullptr, error);
}

ViewType* ShellRuntime::LoadApp(Str directory, Str entry, Policy* policy,
                                ShellError* error) {
    return ReloadApp(directory, entry, policy, nullptr, error);
}

ViewType* ShellRuntime::ReloadApp(Str directory, Str entry, Policy* policy,
                                  const shell::MaterializedDependencies* reuse,
                                  ShellError* error) {
    ShellErrorClear(error);
    if (directory.len <= 0 || directory.len >= kMaxPath) {
        SetError(error, StrL("application directory is empty or too long"));
        return nullptr;
    }
    TempStr input = StrDupTemp(directory);
    TempStr dir = AllocStrTemp(kMaxPath - 1);
    dir.s[0] = 0;
    if (!PlatCanonicalPath(input.s, dir.s, dir.len + 1) ||
        !PlatDirExists(dir.s)) {
        SetError(error,
                 fmt("application directory `%s` does not exist", directory));
        return nullptr;
    }
    dir.len = (int)strlen(dir.s);
    TempStr entryPath = fmt("%s/%s", dir, entry);
    TempStr canonical = AllocStrTemp(kMaxPath - 1);
    canonical.s[0] = 0;
    if (!entryPath || entryPath.len >= kMaxPath ||
        !PlatCanonicalPath(entryPath.s, canonical.s, canonical.len + 1) ||
        !PlatFileExists(canonical.s) ||
        !WithinRoot(Str(dir.s), Str(canonical.s))) {
        SetError(error, fmt("entry module `%s` is not a file inside `%s`",
                            entry, directory));
        return nullptr;
    }
    canonical.len = (int)strlen(canonical.s);
    // Manifest dependencies are fetched before the entry module compiles, and
    // the same checkouts are linked where an editor finds them. The link is
    // best effort — a read-only directory is not a reason to refuse to run.
    shell::MaterializedDependencies dependencies;
    if (reuse) {
        // A reload. The manifest cannot have changed — the watcher does not
        // scan it — so the replacement inherits the checkouts the running
        // application is already using, and nothing touches the network on
        // the UI thread.
        if (!dependencies.CopyFrom(*reuse)) {
            SetError(error, StrL("copying the application dependencies "
                                 "failed"));
            return nullptr;
        }
    }
    Str manifestPath =
        StrDup(fmt("%s/%s", dir, Str(shell::kShellManifestFile)));
    if (!reuse && PlatFileExists(manifestPath.s)) {
        shell::PluginManifest manifest;
        if (!shell::PluginManifestRead(dir, &manifest, error)) {
            StrFree(manifestPath);
            return nullptr;
        }
        shell::GitDependencyStore store(impl->dependencyCacheRoot);
        Str dependencyError = {};
        if (!store.MaterializeAll(manifest, &dependencies, &dependencyError)) {
            SetError(error, dependencyError);
            StrFree(dependencyError);
            StrFree(manifestPath);
            dependencies.Free();
            return nullptr;
        }
        StrFree(dependencyError);
        Str linkError = {};
        store.LinkForEditor(Str(dir), dependencies, nullptr, &linkError);
        StrFree(linkError);
    }
    StrFree(manifestPath);

    AppModule* application = new AppModule();
    application->root = StrDup(dir);
    application->dependencies = dependencies;
    application->generation = impl->nextModuleGeneration++;
    if (application->generation == 0) {
        application->generation = impl->nextModuleGeneration++;
    }
    VecAppend(impl->modules, application);

    TempStr source = ReadModuleFileTemp(canonical, error);
    if (!source.s) return nullptr;
    Str tagged = StrDup(fmt("%s?v=%u", canonical, application->generation));
    ViewType* result =
        LoadModule(this, tagged, source, application, policy, error);
    StrFree(tagged);
    return result;
}

static ViewObject* InstantiateObject(ShellRuntime* runtime, ViewType* type,
                                     Window* window, App* app, Policy* policy,
                                     JSValueConst props, ShellError* error,
                                     EntityId view) {
    ShellErrorClear(error);
    ShellRuntimeImpl* impl = ShellRuntimeAccess::Impl(runtime);
    if (!runtime || !impl || !type || type->runtime != runtime || !window ||
        !app) {
        SetError(error, StrL("instantiate needs a view type from this runtime "
                             "and a live Window/App"));
        return nullptr;
    }
    shell::CallScopeGuard scope =
        shell::ScopeEnter(window, app, ScopePhase::Event, view, policy, runtime,
                          type->application);
    uint32_t retainedCheckpoint = impl->retained.Checkpoint();
    uint32_t nestedCheckpoint = impl->nextNestedView;
    int taskCheckpoint = impl->tasks.len;
    BeginExecution(impl);
    JSValue global = JS_GetGlobalObject(impl->context);
    JSValue construct = JS_GetPropertyStr(impl->context, global, "__construct");
    JSValue object =
        JS_Call(impl->context, construct, JS_UNDEFINED, 1, &type->value);
    JS_FreeValue(impl->context, construct);
    if (JS_IsException(object)) {
        Vec<shell::CallbackId> retired;
        impl->retained.Rollback(retainedCheckpoint, &retired);
        for (int i = 0; i < retired.len; i++) {
            impl->callbacks.RetireId(impl->context, retired[i]);
        }
        VecReset(retired);
        while (impl->tasks.len > taskCheckpoint) {
            ForgetTask(impl, impl->tasks[impl->tasks.len - 1]->id);
        }
        RollbackNestedViews(impl, nestedCheckpoint);
        JS_FreeValue(impl->context, global);
        CaptureException(impl, error);
        return nullptr;
    }
    JSValue initialize =
        JS_GetPropertyStr(impl->context, global, "__initialize");
    JSValue context = JS_GetPropertyStr(impl->context, global, "__context");
    JSValue generation =
        JS_NewInt64(impl->context, (int64_t)scope.Generation());
    JSValue cx = JS_Call(impl->context, context, JS_UNDEFINED, 1, &generation);
    JSValue args[3] = {object, JS_DupValue(impl->context, props), cx};
    JSValue initialized =
        JS_Call(impl->context, initialize, JS_UNDEFINED, 3, args);
    JS_FreeValue(impl->context, generation);
    JS_FreeValue(impl->context, args[1]);
    JS_FreeValue(impl->context, cx);
    JS_FreeValue(impl->context, context);
    JS_FreeValue(impl->context, initialize);
    JS_FreeValue(impl->context, global);
    if (JS_IsException(initialized)) {
        JS_FreeValue(impl->context, object);
        Vec<shell::CallbackId> retired;
        impl->retained.Rollback(retainedCheckpoint, &retired);
        for (int i = 0; i < retired.len; i++) {
            impl->callbacks.RetireId(impl->context, retired[i]);
        }
        VecReset(retired);
        while (impl->tasks.len > taskCheckpoint) {
            ForgetTask(impl, impl->tasks[impl->tasks.len - 1]->id);
        }
        RollbackNestedViews(impl, nestedCheckpoint);
        CaptureException(impl, error);
        return nullptr;
    }
    JS_FreeValue(impl->context, initialized);
    ViewObject* result = new ViewObject();
    result->runtime = runtime->Retain();
    result->value = object;
    result->application = type->application;
    return result;
}

ViewObject* ShellRuntime::Instantiate(ViewType* type, Window* window, App* app,
                                      Policy* policy, ShellError* error,
                                      EntityId view) {
    return InstantiateObject(this, type, window, app, policy, JS_UNDEFINED,
                             error, view);
}

static bool ElementId(JSContext* ctx, JSValueConst value, shell::SpecId* id) {
    if (!JS_IsObject(value)) {
        JS_ThrowTypeError(ctx, "render(cx) must return an element");
        return false;
    }
    JSValue property = JS_GetPropertyStr(ctx, value, "__id");
    bool ok = !JS_IsException(property) && JS_ToUint32(ctx, id, property) == 0;
    JS_FreeValue(ctx, property);
    if (!ok) JS_ThrowTypeError(ctx, "render(cx) must return an element");
    return ok;
}

RenderSnapshot* ShellRuntime::BuildSnapshot(ViewObject* object, Window* window,
                                            App* app, EntityId view,
                                            Policy* policy, ShellError* error) {
    ShellErrorClear(error);
    if (!object || object->runtime != this || !window || !app) {
        SetError(error, StrL("snapshot build needs a view object from this "
                             "runtime and a live Window/App"));
        return nullptr;
    }
    // One theme sync per description makes cx.theme() a JS-only cache read
    // after the first: the native snapshot is rebuilt only when this revision
    // changes, rather than once per component asking for the theme.
    shell::ThemeTokensSync(app);
    impl->scratch->Reset();
    uint64_t callbackGeneration = impl->callbacks.Begin(impl->context);
    double started = TimeNow();
    shell::SpecId root = 0;
    bool succeeded = false;
    {
        shell::CallScopeGuard scope =
            shell::ScopeEnter(window, app, ScopePhase::Render, view, policy,
                              this, object->application);
        BeginExecution(impl);
        JSValue render =
            JS_GetPropertyStr(impl->context, object->value, "render");
        if (!JS_IsException(render) && JS_IsFunction(impl->context, render)) {
            JSValue global = JS_GetGlobalObject(impl->context);
            // Warms the script's theme cache inside this scope, so the first
            // cx.theme() in the description costs what every later one does.
            JSValue prepare =
                JS_GetPropertyStr(impl->context, global, "__prepare_theme");
            JSValue warmed =
                JS_Call(impl->context, prepare, JS_UNDEFINED, 0, nullptr);
            if (JS_IsException(warmed)) {
                JS_FreeValue(impl->context, JS_GetException(impl->context));
            }
            JS_FreeValue(impl->context, warmed);
            JS_FreeValue(impl->context, prepare);
            JSValue context =
                JS_GetPropertyStr(impl->context, global, "__context");
            JSValue generation =
                JS_NewInt64(impl->context, (int64_t)scope.Generation());
            JSValue cx =
                JS_Call(impl->context, context, JS_UNDEFINED, 1, &generation);
            JSValue produced =
                JS_Call(impl->context, render, object->value, 1, &cx);
            JS_FreeValue(impl->context, generation);
            JS_FreeValue(impl->context, cx);
            JS_FreeValue(impl->context, context);
            JS_FreeValue(impl->context, global);
            if (!JS_IsException(produced)) {
                succeeded = ElementId(impl->context, produced, &root);
            }
            JS_FreeValue(impl->context, produced);
        } else if (!JS_IsException(render)) {
            JS_ThrowTypeError(impl->context,
                              "view class has no render(cx) method");
        }
        JS_FreeValue(impl->context, render);
    }
    double elapsed = TimeNow() - started;
    shell::MetricsAdd(&impl->metrics, shell::MetricsTimerKind::ScriptRender,
                      elapsed <= 0 ? 0 : (uint64_t)(elapsed * 1e9));
    if (!succeeded) {
        impl->callbacks.Abort(impl->context);
        impl->scratch->Reset();
        CaptureException(impl, error);
        return nullptr;
    }
    impl->callbacks.Commit();
    shell::SpecArena* published = impl->scratch;
    impl->scratch = new shell::SpecArena();
    return new RenderSnapshot(callbackGeneration, root, published,
                              SnapshotLease(this));
}

Str ShellRuntime::RenderToSpec(Arena* into, ViewObject* object, Window* window,
                               App* app, EntityId view, Policy* policy,
                               ShellError* error) {
    RenderSnapshot* snapshot =
        BuildSnapshot(object, window, app, view, policy, error);
    if (!snapshot) return {};
    Str result = snapshot->DebugTree(into);
    delete snapshot;
    return result;
}

bool ShellRuntime::Eval(Str source, Str name, ShellError* error) {
    ShellErrorClear(error);
    BeginExecution(impl);
    Str file = name ? name : StrL("<eval>");
    Str owned = StrDup(file);
    JSValue value = JS_Eval(impl->context, source.s, (size_t)source.len,
                            owned.s, JS_EVAL_TYPE_GLOBAL);
    StrFree(owned);
    if (JS_IsException(value)) return CaptureException(impl, error);
    bool complete = Await(impl, value, error);
    JS_FreeValue(impl->context, value);
    return complete;
}

bool ShellRuntime::DrainJobs(int limit, ShellError* error) {
    ShellErrorClear(error);
    if (limit < 0) limit = 0;
    int count = 0;
    while (JS_IsJobPending(impl->jsRuntime) && count++ < limit) {
        JSContext* context = nullptr;
        if (JS_ExecutePendingJob(impl->jsRuntime, &context) < 0) {
            return CaptureException(impl, error);
        }
    }
    if (JS_IsJobPending(impl->jsRuntime)) {
        SetError(error, StrL("the QuickJS job queue exceeded its drain limit"));
        return false;
    }
    return true;
}

RuntimeMetrics ShellRuntime::ReadMetrics() const {
    return shell::MetricsRead(&impl->metrics);
}

void ShellRuntime::RecordMaterialize(uint64_t nanos) {
    shell::MetricsAdd(&impl->metrics, shell::MetricsTimerKind::Materialize,
                      nanos);
}

void ShellRuntime::RecordStructure(bool repeated) {
    shell::MetricsRecordStructure(&impl->metrics, repeated);
}

int ShellRuntime::LiveCallbacks() const {
    return impl->callbacks.Live();
}

int ShellRuntime::LiveEntities() const {
    return impl->retained.Len();
}

int ShellRuntime::LiveNestedViews() const {
    return impl->nestedViews.len;
}

int ShellRuntime::LiveTasks() const {
    return impl->tasks.len;
}

int ShellRuntime::LiveTemplates() const {
    int count = 0;
    for (int i = 0; i < impl->templates.len; i++) {
        if (impl->templates[i]) count++;
    }
    return count;
}

shell::RetainedEntry* ShellRuntime::Retained(shell::EntityHandle handle) const {
    return impl->retained.Find(handle);
}

EntityId ShellRuntime::NestedView(shell::EntityHandle handle, App* app) const {
    if (handle == 0 || handle > UINT32_MAX) return {};
    NestedViewEntry* entry = FindNestedView(impl, (uint32_t)handle);
    if (!entry || !app || entry->app != app || !EntityGet(app, entry->view)) {
        return {};
    }
    return entry->view;
}

void ShellRuntime::RegisterScriptView(EntityId view, bool* dirty) {
    if (!view.IsValid() || !dirty) return;
    for (int i = 0; i < impl->views.len; i++) {
        if (impl->views[i].view == view && impl->views[i].dirty == dirty)
            return;
    }
    VecAppend(impl->views, {view, dirty});
}

void ShellRuntime::UnregisterScriptView(EntityId view, bool* dirty) {
    for (int i = 0; i < impl->views.len; i++) {
        if (impl->views[i].view != view || impl->views[i].dirty != dirty)
            continue;
        for (int j = i + 1; j < impl->views.len; j++) {
            impl->views[j - 1] = impl->views[j];
        }
        impl->views.len--;
        return;
    }
}

void ShellRuntime::InvalidateScriptView(EntityId view) {
    for (int i = 0; i < impl->views.len; i++) {
        if (impl->views[i].view == view && impl->views[i].dirty) {
            *impl->views[i].dirty = true;
        }
    }
}

void ShellRuntime::ReleaseOwnedEntities(EntityId view) {
    for (;;) {
        int nested = -1;
        for (int i = impl->nestedViews.len - 1; i >= 0; i--) {
            if (impl->nestedViews[i].owner == view) {
                nested = i;
                break;
            }
        }
        if (nested < 0) break;
        DropNestedViewAt(impl, nested);
    }
    for (int i = impl->tasks.len - 1; i >= 0; i--) {
        if (impl->tasks[i]->owner == view) {
            ForgetTask(impl, impl->tasks[i]->id);
        }
    }
    Vec<shell::CallbackId> callbacks;
    impl->retained.ReleaseOwner(view, &callbacks);
    for (int i = 0; i < callbacks.len; i++) {
        impl->callbacks.RetireId(impl->context, callbacks[i]);
    }
    VecReset(callbacks);
}

void ShellRuntime::ReleaseApplicationState(ViewObject* object) {
    if (!object || object->runtime != this || !object->application) return;
    AppModule* application = object->application;
    for (;;) {
        int nested = -1;
        for (int i = impl->nestedViews.len - 1; i >= 0; i--) {
            if (impl->nestedViews[i].application == application) {
                nested = i;
                break;
            }
        }
        if (nested < 0) break;
        DropNestedViewAt(impl, nested);
    }
    for (int i = impl->tasks.len - 1; i >= 0; i--) {
        if (impl->tasks[i]->application == application) {
            ForgetTask(impl, impl->tasks[i]->id);
        }
    }
    Vec<shell::CallbackId> callbacks;
    impl->retained.ReleaseApplication(application, &callbacks);
    for (int i = 0; i < callbacks.len; i++) {
        impl->callbacks.RetireId(impl->context, callbacks[i]);
    }
    VecReset(callbacks);
    impl->callbacks.RetireApplication(impl->context, application);
    // A template is defined once and used for the life of the module, so
    // nothing in a render would ever free one. The slot is emptied rather than
    // removed, because a template's id is its index and a closure in a
    // still-loaded module may hold one: a script that reaches a retired id is
    // told so rather than handed someone else's structure.
    for (int i = 0; i < impl->templates.len; i++) {
        if (impl->templates[i] && impl->templates[i]
                                          ->application == (void*)application) {
            delete impl->templates[i];
            impl->templates[i] = nullptr;
        }
    }
}

void ShellRuntime::ResumeTask(uint32_t id, Ctx* cx) {
    if (!cx || !cx->app || !cx->win) return;
    ShellTask* task = FindTask(impl, id);
    if (!task || task->kind == ShellTaskKind::Spawn) return;
    if (task->owner.IsValid() && !EntityGet(cx->app, task->owner)) {
        ForgetTask(impl, id, false);
        return;
    }

    ShellTaskKind kind = task->kind;
    EntityId owner = task->owner;
    Policy* policy = PolicyRetain(task->policy);
    AppModule* application = task->application;
    JSValue callback = JS_DupValue(impl->context, task->callback);
    bool repeating = kind == ShellTaskKind::TimerEvery;
    if (!repeating) ForgetTask(impl, id, false);

    shell::CallScopeGuard scope = shell::ScopeEnter(
        cx->win, cx->app, ScopePhase::Task, owner, policy, this, application);
    PolicyRelease(policy);
    JSValue result = JS_UNDEFINED;
    if (kind == ShellTaskKind::Sleep) {
        result = JS_Call(impl->context, callback, JS_UNDEFINED, 0, nullptr);
    } else {
        JSValue global = JS_GetGlobalObject(impl->context);
        JSValue ambient =
            JS_GetPropertyStr(impl->context, global, "__ambient_context");
        result = JS_Call(impl->context, callback, JS_UNDEFINED, 1, &ambient);
        JS_FreeValue(impl->context, ambient);
        JS_FreeValue(impl->context, global);
    }
    JS_FreeValue(impl->context, callback);
    if (JS_IsException(result)) {
        Arena* arena = ArenaNew();
        log(ExceptionText(arena, impl->context));
        ArenaDelete(arena);
    } else {
        JS_FreeValue(impl->context, result);
    }
    ShellError error = {};
    DrainJobs(kMaxJobBatch, &error);
    if (error.IsSet()) {
        log(error.message);
        ShellErrorClear(&error);
    }
}

static JSValue ContextObject(ShellRuntimeImpl* impl, uint64_t generation) {
    JSValue global = JS_GetGlobalObject(impl->context);
    JSValue make = JS_GetPropertyStr(impl->context, global, "__context");
    JSValue value = JS_NewInt64(impl->context, (int64_t)generation);
    JSValue result = JS_Call(impl->context, make, JS_UNDEFINED, 1, &value);
    JS_FreeValue(impl->context, value);
    JS_FreeValue(impl->context, make);
    JS_FreeValue(impl->context, global);
    return result;
}

// The object every modifier-carrying payload gets, built in one place so a
// press reported through a row carries the same fields as one reported through
// the element it landed on.
static JSValue ModifiersObject(JSContext* ctx, const Modifiers& modifiers) {
    JSValue object = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, object, "shift", JS_NewBool(ctx, modifiers.shift));
    JS_SetPropertyStr(ctx, object, "control",
                      JS_NewBool(ctx, modifiers.control));
    JS_SetPropertyStr(ctx, object, "alt", JS_NewBool(ctx, modifiers.alt));
    JS_SetPropertyStr(ctx, object, "platform",
                      JS_NewBool(ctx, modifiers.platform));
    JS_SetPropertyStr(ctx, object, "function",
                      JS_NewBool(ctx, modifiers.function));
    return object;
}

// Calls one script handler with the arguments given plus its `cx`, under the
// scope, budget and job drain every dispatch shares. The arguments are consumed
// whether or not the call happens.
static void DispatchArgs(ShellRuntime* runtime, shell::CallbackId id,
                         JSValue* leading, int count, Window* window,
                         App* app) {
    ShellRuntimeImpl* impl = ShellRuntimeAccess::Impl(runtime);
    CallbackEntry* entry = impl->callbacks.Get(id);
    bool live = entry && window && app &&
                (!entry->view.IsValid() || EntityGet(app, entry->view));
    if (!live) {
        for (int i = 0; i < count; i++) JS_FreeValue(impl->context, leading[i]);
        return;
    }
    shell::CallScopeGuard scope =
        shell::ScopeEnter(window, app, ScopePhase::Event, entry->view,
                          entry->policy, runtime, entry->application);
    BeginExecution(impl);
    JSValue cx = ContextObject(impl, scope.Generation());
    JSValue args[3] = {JS_UNDEFINED, JS_UNDEFINED, JS_UNDEFINED};
    for (int i = 0; i < count; i++) args[i] = leading[i];
    args[count] = cx;
    JSValue result =
        JS_Call(impl->context, entry->function, JS_UNDEFINED, count + 1, args);
    JS_FreeValue(impl->context, cx);
    for (int i = 0; i < count; i++) JS_FreeValue(impl->context, leading[i]);
    if (JS_IsException(result)) {
        Arena* arena = ArenaNew();
        log(ExceptionText(arena, impl->context));
        ArenaDelete(arena);
    } else {
        JS_FreeValue(impl->context, result);
        ShellError error = {};
        runtime->DrainJobs(kMaxJobBatch, &error);
        if (error.IsSet()) {
            log(error.message);
            ShellErrorClear(&error);
        }
    }
}

static void Dispatch(ShellRuntime* runtime, shell::CallbackId id,
                     JSValue payload, Window* window, App* app) {
    DispatchArgs(runtime, id, &payload, 1, window, app);
}

void ShellRuntime::DispatchClick(shell::CallbackId callback,
                                 const ClickEvent& event, Window* window,
                                 App* app) {
    JSValue payload = JS_NewObject(impl->context);
    JS_SetPropertyStr(impl->context, payload, "click_count",
                      JS_NewInt32(impl->context, event.clickCount));
    JS_SetPropertyStr(impl->context, payload, "modifiers",
                      ModifiersObject(impl->context, event.modifiers));
    Dispatch(this, callback, payload, window, app);
}

// Delivers a secondary press on a virtual list row: the row's key, then the
// press exactly as a press on the row itself would be reported, with
// `local_position` measured from the row's own box.
void ShellRuntime::DispatchItemSecondaryClick(shell::CallbackId callback,
                                              Str key,
                                              const MouseDownEvent& event,
                                              Window* window, App* app) {
    JSValue payload = JS_NewObject(impl->context);
    JS_SetPropertyStr(
        impl->context, payload, "button",
        JS_NewString(
            impl->context,
            event.button == MouseButton::Right
                ? "right"
                : (event.button == MouseButton::Middle ? "middle" : "left")));
    JS_SetPropertyStr(impl->context, payload, "click_count",
                      JS_NewInt32(impl->context, event.clickCount));
    JSValue position = JS_NewObject(impl->context);
    JS_SetPropertyStr(impl->context, position, "x",
                      JS_NewFloat64(impl->context, event.x));
    JS_SetPropertyStr(impl->context, position, "y",
                      JS_NewFloat64(impl->context, event.y));
    JS_SetPropertyStr(impl->context, payload, "position", position);
    JSValue local = JS_NewObject(impl->context);
    JS_SetPropertyStr(impl->context, local, "x",
                      JS_NewFloat64(impl->context, event.x - event.el.x));
    JS_SetPropertyStr(impl->context, local, "y",
                      JS_NewFloat64(impl->context, event.y - event.el.y));
    JS_SetPropertyStr(impl->context, payload, "local_position", local);
    JS_SetPropertyStr(impl->context, payload, "modifiers",
                      ModifiersObject(impl->context, event.modifiers));
    JSValue leading[2] = {
        JS_NewStringLen(impl->context, key.s, (size_t)key.len), payload};
    DispatchArgs(this, callback, leading, 2, window, app);
}

void ShellRuntime::DispatchMouseMove(shell::CallbackId callback,
                                     const MouseMoveEvent& event,
                                     Window* window, App* app) {
    JSValue payload = JS_NewObject(impl->context);
    JSValue position = JS_NewObject(impl->context);
    JS_SetPropertyStr(impl->context, position, "x",
                      JS_NewFloat64(impl->context, event.x));
    JS_SetPropertyStr(impl->context, position, "y",
                      JS_NewFloat64(impl->context, event.y));
    JS_SetPropertyStr(impl->context, payload, "position", position);
    JSValue local = JS_NewObject(impl->context);
    JS_SetPropertyStr(impl->context, local, "x",
                      JS_NewFloat64(impl->context, event.x - event.el.x));
    JS_SetPropertyStr(impl->context, local, "y",
                      JS_NewFloat64(impl->context, event.y - event.el.y));
    JS_SetPropertyStr(impl->context, payload, "local_position", local);
    JSValue bounds = JS_NewObject(impl->context);
    JS_SetPropertyStr(impl->context, bounds, "x",
                      JS_NewFloat64(impl->context, event.el.x));
    JS_SetPropertyStr(impl->context, bounds, "y",
                      JS_NewFloat64(impl->context, event.el.y));
    JS_SetPropertyStr(impl->context, bounds, "width",
                      JS_NewFloat64(impl->context, event.el.w));
    JS_SetPropertyStr(impl->context, bounds, "height",
                      JS_NewFloat64(impl->context, event.el.h));
    JS_SetPropertyStr(impl->context, payload, "bounds", bounds);
    JS_SetPropertyStr(impl->context, payload, "x",
                      JS_NewFloat64(impl->context, event.x));
    JS_SetPropertyStr(impl->context, payload, "y",
                      JS_NewFloat64(impl->context, event.y));
    JS_SetPropertyStr(impl->context, payload, "dragging",
                      JS_NewBool(impl->context, event.Dragging()));
    JS_SetPropertyStr(impl->context, payload, "modifiers",
                      ModifiersObject(impl->context, event.modifiers));
    Dispatch(this, callback, payload, window, app);
}

void ShellRuntime::DispatchChange(shell::CallbackId callback, bool value,
                                  Window* window, App* app) {
    Dispatch(this, callback, JS_NewBool(impl->context, value), window, app);
}

void ShellRuntime::DispatchIndex(shell::CallbackId callback, uint32_t value,
                                 Window* window, App* app) {
    Dispatch(this, callback, JS_NewUint32(impl->context, value), window, app);
}

void ShellRuntime::DispatchNumbers(shell::CallbackId callback,
                                   const float* values, int count,
                                   Window* window, App* app) {
    JSValue array = JS_NewArray(impl->context);
    for (int i = 0; values && i < count; i++) {
        JS_SetPropertyUint32(impl->context, array, (uint32_t)i,
                             JS_NewFloat64(impl->context, values[i]));
    }
    Dispatch(this, callback, array, window, app);
}

void ShellRuntime::DispatchString(shell::CallbackId callback, Str value,
                                  Window* window, App* app) {
    Dispatch(this, callback,
             JS_NewStringLen(impl->context, value.s ? value.s : "",
                             (size_t)value.len),
             window, app);
}

void ShellRuntime::DispatchSignal(shell::CallbackId callback, Window* window,
                                  App* app) {
    Dispatch(this, callback, JS_NewObject(impl->context), window, app);
}

static void RetainedCallbackIds(const shell::RetainedEntry* entry,
                                shell::RetainedEvent event,
                                Vec<shell::CallbackId>* out);

// One codepoint as UTF-8. Written out rather than borrowed from
// src/markdown/, which is a ported crate this tree keeps to itself.
static TempStr ShellUtf8Temp(uint32_t cp) {
    TempStr out = AllocStrTemp(4);
    if (cp < 0x80) {
        out.s[0] = (char)cp;
        out.len = 1;
        return out;
    }
    if (cp < 0x800) {
        out.s[0] = (char)(0xC0 | (cp >> 6));
        out.s[1] = (char)(0x80 | (cp & 0x3F));
        out.len = 2;
        return out;
    }
    if (cp < 0x10000) {
        out.s[0] = (char)(0xE0 | (cp >> 12));
        out.s[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out.s[2] = (char)(0x80 | (cp & 0x3F));
        out.len = 3;
        return out;
    }
    out.s[0] = (char)(0xF0 | (cp >> 18));
    out.s[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out.s[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out.s[3] = (char)(0x80 | (cp & 0x3F));
    return out;
}

// One wheel notch, in the DIPs a ScrollWheelEvent reports — see the comment on
// its `deltaY`. What `delta_lines` divides by to recover the count a wheel
// with detents actually reported.
static const float kShellWheelNotch = 48.f;

// One chord, spelled the same way on every platform.
//
// Not the port's own `KeyName` plus its modifier order: GPUI spells the
// platform modifier for the platform it was built for — `cmd-` on macOS,
// `super-` on Linux, `win-` on Windows — which is right for a keymap a person
// reads and wrong for a string a program compares. A script is one file
// running on all three, so `event.keystroke === "cmd-s"` would work on macOS
// and silently do nothing everywhere else. `cmd` is the spelling because
// `KeyChordParse`, which `cx.bind_keys` goes through, takes `cmd`, `super`
// and `win` on every platform, so a binding and the event it produces agree by
// construction. The modifier order is GPUI's own, so a chord round-trips.
static Str ScriptKeystroke(Arena* arena, const KeyEvent& event) {
    StrBuilder out(arena);
    if (event.function) out.Append(StrL("fn-"));
    if (event.ctrl) out.Append(StrL("ctrl-"));
    if (event.alt) out.Append(StrL("alt-"));
    if (event.platform) out.Append(StrL("cmd-"));
    if (event.shift) out.Append(StrL("shift-"));
    Str key = event.vk ? KeyName(event.vk) : Str{};
    if (key) {
        out.Append(key);
    } else if (event.ch) {
        out.Append(ShellUtf8Temp(event.ch));
    }
    return out.TakeStr();
}

static JSValue JsModifiers(JSContext* ctx, bool shift, bool control, bool alt,
                           bool platform, bool function) {
    JSValue modifiers = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, modifiers, "shift", JS_NewBool(ctx, shift));
    JS_SetPropertyStr(ctx, modifiers, "control", JS_NewBool(ctx, control));
    JS_SetPropertyStr(ctx, modifiers, "alt", JS_NewBool(ctx, alt));
    JS_SetPropertyStr(ctx, modifiers, "platform", JS_NewBool(ctx, platform));
    JS_SetPropertyStr(ctx, modifiers, "function", JS_NewBool(ctx, function));
    return modifiers;
}

// The window position, and the element-relative position and box when the
// element has been painted. `local_position` and `bounds` are omitted rather
// than zeroed for an element with no geometry yet, so a script reading
// `undefined` knows the geometry was unavailable instead of being told the
// press landed at its top-left corner.
static void SetPointerGeometry(JSContext* ctx, JSValue payload, float x,
                               float y, Bounds bounds, bool hasBounds) {
    JS_SetPropertyStr(ctx, payload, "position", JsPoint(ctx, x, y));
    if (hasBounds) {
        JS_SetPropertyStr(ctx, payload, "local_position",
                          JsPoint(ctx, x - bounds.x, y - bounds.y));
        JS_SetPropertyStr(ctx, payload, "bounds", JsBounds(ctx, bounds));
    } else {
        JS_SetPropertyStr(ctx, payload, "local_position", JS_UNDEFINED);
        JS_SetPropertyStr(ctx, payload, "bounds", JS_UNDEFINED);
    }
}

void ShellRuntime::DispatchKey(shell::CallbackId callback,
                               const KeyEvent& event, bool* propagate,
                               Window* window, App* app) {
    Arena* arena = ArenaNew();
    Str keystroke = ScriptKeystroke(arena, event);
    Str key = event.vk ? KeyName(event.vk) : keystroke;
    JSValue payload = JS_NewObject(impl->context);
    JS_SetPropertyStr(impl->context, payload, "key",
                      JS_NewStringLen(impl->context, key.s, (size_t)key.len));
    JS_SetPropertyStr(
        impl->context, payload, "keystroke",
        JS_NewStringLen(impl->context, keystroke.s, (size_t)keystroke.len));
    if (event.ch) {
        TempStr utf8 = ShellUtf8Temp(event.ch);
        JS_SetPropertyStr(
            impl->context, payload, "key_char",
            JS_NewStringLen(impl->context, utf8.s, (size_t)utf8.len));
    } else {
        JS_SetPropertyStr(impl->context, payload, "key_char", JS_UNDEFINED);
    }
    // `is_held` distinguishes the two events rather than a second method doing
    // it: a release carries no repeat state, so `undefined` is what "this was
    // a release" looks like on the wire.
    if (event.down) {
        JS_SetPropertyStr(impl->context, payload, "is_held",
                          JS_NewBool(impl->context, false));
    }
    JS_SetPropertyStr(impl->context, payload, "modifiers",
                      JsModifiers(impl->context, event.shift, event.ctrl,
                                  event.alt, event.platform, event.function));
    ArenaDelete(arena);
    ShellPropagationGuard guard(propagate);
    Dispatch(this, callback, payload, window, app);
}

void ShellRuntime::DispatchMouseButton(shell::CallbackId callback,
                                       MouseButton button, float x, float y,
                                       int clickCount, Modifiers modifiers,
                                       Bounds bounds, bool hasBounds,
                                       Window* window, App* app) {
    JSValue payload = JS_NewObject(impl->context);
    const char* name =
        button == MouseButton::Right
            ? "right"
            : (button == MouseButton::Middle ? "middle" : "left");
    JS_SetPropertyStr(impl->context, payload, "button",
                      JS_NewString(impl->context, name));
    JS_SetPropertyStr(impl->context, payload, "click_count",
                      JS_NewInt32(impl->context, clickCount));
    SetPointerGeometry(impl->context, payload, x, y, bounds, hasBounds);
    JS_SetPropertyStr(
        impl->context, payload, "modifiers",
        JsModifiers(impl->context, modifiers.shift, modifiers.control,
                    modifiers.alt, modifiers.platform, modifiers.function));
    Dispatch(this, callback, payload, window, app);
}

void ShellRuntime::DispatchScrollWheel(shell::CallbackId callback,
                                       const ScrollWheelEvent& event,
                                       Bounds bounds, bool hasBounds,
                                       bool* propagate, Window* window,
                                       App* app) {
    JSValue payload = JS_NewObject(impl->context);
    JS_SetPropertyStr(impl->context, payload, "delta",
                      JsPoint(impl->context, event.deltaX, event.deltaY));
    // A wheel's delta is already in pixels here, and a device that measures
    // the gesture itself reports no line count — which is what `precise` says.
    if (!event.precise) {
        JS_SetPropertyStr(
            impl->context, payload, "delta_lines",
            JsPoint(impl->context, event.deltaX / kShellWheelNotch,
                    event.deltaY / kShellWheelNotch));
    } else {
        JS_SetPropertyStr(impl->context, payload, "delta_lines", JS_UNDEFINED);
    }
    const char* phase =
        event.phase == TouchPhase::Started
            ? "started"
            : (event.phase == TouchPhase::Ended ? "ended" : "moved");
    JS_SetPropertyStr(impl->context, payload, "touch_phase",
                      JS_NewString(impl->context, phase));
    SetPointerGeometry(impl->context, payload, event.x, event.y, bounds,
                       hasBounds);
    JS_SetPropertyStr(
        impl->context, payload, "modifiers",
        JsModifiers(impl->context, event.modifiers.shift,
                    event.modifiers.control, event.modifiers.alt,
                    event.modifiers.platform, event.modifiers.function));
    ShellPropagationGuard guard(propagate);
    Dispatch(this, callback, payload, window, app);
}

// The id is handed over even though the handler was registered for one action:
// a script that routes several ids into one function has the name it needs
// without closing over it, and a handler that ignores the argument costs
// nothing.
void ShellRuntime::DispatchAction(shell::CallbackId callback, Str action,
                                  bool* propagate, Window* window, App* app) {
    JSValue payload = JS_NewObject(impl->context);
    JS_SetPropertyStr(
        impl->context, payload, "action",
        JS_NewStringLen(impl->context, action.s, (size_t)action.len));
    ShellPropagationGuard guard(propagate);
    Dispatch(this, callback, payload, window, app);
}

void ShellRuntime::DispatchCalendarEvent(shell::EntityHandle handle,
                                         const CalendarEvent& event,
                                         Window* window, App* app) {
    if (event.kind != CalendarEventKind::Selected) return;
    shell::RetainedEntry* entry = impl->retained.Find(handle);
    if (!entry) return;
    Vec<shell::CallbackId> callbacks;
    RetainedCallbackIds(entry, shell::RetainedEvent::CalendarChange,
                        &callbacks);
    for (int i = 0; i < callbacks.len; i++) {
        // The same conversion `value()` answers with, so a handler and a read
        // cannot disagree about the shape of one date.
        Dispatch(this, callbacks[i], DateToParts(impl->context, event.date),
                 window, app);
    }
    VecReset(callbacks);
}

static void RetainedCallbackIds(const shell::RetainedEntry* entry,
                                shell::RetainedEvent event,
                                Vec<shell::CallbackId>* out) {
    if (!entry) return;
    for (int i = 0; i < entry->callbacks.len; i++) {
        if (entry->callbacks[i].event == event) {
            VecAppend(*out, entry->callbacks[i].callback);
        }
    }
}

static shell::RetainedEntry* EventRetained(ShellRuntimeImpl* impl,
                                           shell::EntityHandle handle) {
    return (handle >> 32) == 0 ? impl->retained.FindLocal((uint32_t)handle)
                               : impl->retained.Find(handle);
}

void ShellRuntime::DispatchInputEvent(shell::EntityHandle handle,
                                      const InputEvent& event, Window* window,
                                      App* app) {
    shell::RetainedEvent wanted = shell::RetainedEvent::InputChange;
    if (event.kind == InputEventKind::PressEnter) {
        wanted = shell::RetainedEvent::InputSubmit;
    } else if (event.kind == InputEventKind::Focus) {
        wanted = shell::RetainedEvent::InputFocus;
    } else if (event.kind == InputEventKind::Blur) {
        wanted = shell::RetainedEvent::InputBlur;
    }
    Vec<shell::CallbackId> callbacks;
    RetainedCallbackIds(EventRetained(impl, handle), wanted, &callbacks);
    for (int i = 0; i < callbacks.len; i++) {
        JSValue payload = JS_NewObject(impl->context);
        if (event.kind == InputEventKind::PressEnter) {
            JS_SetPropertyStr(impl->context, payload, "secondary",
                              JS_NewBool(impl->context, event.secondary));
            JS_SetPropertyStr(impl->context, payload, "shift",
                              JS_NewBool(impl->context, event.shift));
        }
        Dispatch(this, callbacks[i], payload, window, app);
    }
    VecReset(callbacks);
}

void ShellRuntime::DispatchSliderEvent(shell::EntityHandle handle,
                                       const SliderEvent& event, Window* window,
                                       App* app) {
    shell::RetainedEvent wanted = event.kind == SliderEventKind::Release
                                      ? shell::RetainedEvent::SliderRelease
                                      : shell::RetainedEvent::SliderChange;
    Vec<shell::CallbackId> callbacks;
    RetainedCallbackIds(EventRetained(impl, handle), wanted, &callbacks);
    for (int i = 0; i < callbacks.len; i++) {
        JSValue payload = event.value.range
                              ? SliderValueJs(impl->context, event.value)
                              : JS_NewFloat64(impl->context, event.value.hi);
        Dispatch(this, callbacks[i], payload, window, app);
    }
    VecReset(callbacks);
}

void ShellRuntime::DispatchOtpEvent(shell::EntityHandle handle,
                                    const OtpEvent& event, Window* window,
                                    App* app) {
    shell::RetainedEvent wanted = shell::RetainedEvent::OtpChange;
    if (event.kind == OtpEventKind::Complete) {
        wanted = shell::RetainedEvent::OtpComplete;
    } else if (event.kind == OtpEventKind::Focus) {
        wanted = shell::RetainedEvent::OtpFocus;
    } else if (event.kind == OtpEventKind::Blur) {
        wanted = shell::RetainedEvent::OtpBlur;
    }
    Vec<shell::CallbackId> callbacks;
    RetainedCallbackIds(EventRetained(impl, handle), wanted, &callbacks);
    for (int i = 0; i < callbacks.len; i++) {
        Dispatch(this, callbacks[i], JS_NewObject(impl->context), window, app);
    }
    VecReset(callbacks);
}

void ShellRuntime::RenderVirtualItems(shell::CallbackId renderId,
                                      shell::CallbackId keyId,
                                      shell::CallbackId onItemClick,
                                      shell::CallbackId onItemSecondaryClick,
                                      int first, int end, Ctx* cx, El** out) {
    if (!cx || !out || end <= first) return;
    for (int i = 0; i < end - first; i++) out[i] = nullptr;
    CallbackEntry* render = impl->callbacks.Get(renderId);
    CallbackEntry* key = impl->callbacks.Get(keyId);
    if (!render || !key || !cx->win || !cx->app) return;
    if (render->view.IsValid() && !EntityGet(cx->app, render->view)) return;

    double started = TimeNow();
    shell::SpecArena* outer = impl->scratch;
    shell::SpecArena* batch = new shell::SpecArena();
    impl->scratch = batch;
    Vec<shell::SpecId> roots;
    Vec<Str> itemKeys;
    bool succeeded = true;
    {
        shell::CallScopeGuard scope = shell::ScopeEnter(
            cx->win, cx->app, ScopePhase::Layout, render->view, render->policy,
            this, render->application);
        shell::ScopeAdopt(render->registeredIn);
        BeginExecution(impl);
        JSValue payload = JS_NewObject(impl->context);
        JS_SetPropertyStr(impl->context, payload, "start",
                          JS_NewInt32(impl->context, first));
        JS_SetPropertyStr(impl->context, payload, "end",
                          JS_NewInt32(impl->context, end));
        JSValue context = ContextObject(impl, scope.Generation());
        JSValue args[2] = {payload, context};
        JSValue produced =
            JS_Call(impl->context, render->function, JS_UNDEFINED, 2, args);
        JS_FreeValue(impl->context, context);
        JS_FreeValue(impl->context, payload);
        if (JS_IsException(produced) || !JS_IsArray(produced)) {
            if (!JS_IsException(produced)) {
                JS_ThrowTypeError(impl->context,
                                  "a virtual-list item renderer must return an "
                                  "array of elements");
            }
            succeeded = false;
        } else {
            int64_t count = 0;
            if (JS_GetLength(impl->context, produced, &count) < 0 ||
                count != end - first) {
                if (!JS_HasException(impl->context)) {
                    JS_ThrowTypeError(impl->context,
                                      "a virtual-list item renderer must "
                                      "return one element per item");
                }
                succeeded = false;
            }
            for (int i = 0; succeeded && i < (int)count; i++) {
                JSValue item =
                    JS_GetPropertyUint32(impl->context, produced, (uint32_t)i);
                shell::SpecId root = 0;
                succeeded = !JS_IsException(item) &&
                            ElementId(impl->context, item, &root);
                JS_FreeValue(impl->context, item);
                if (succeeded) VecAppend(roots, root);
            }
        }
        JS_FreeValue(impl->context, produced);

        Arena* keys = ArenaNew();
        Vec<Str> seen;
        for (int index = first; succeeded && index < end; index++) {
            JSValue value = JS_NewInt32(impl->context, index);
            JSValue result =
                JS_Call(impl->context, key->function, JS_UNDEFINED, 1, &value);
            JS_FreeValue(impl->context, value);
            Str text;
            succeeded = !JS_IsException(result) &&
                        JsString(impl->context, result, keys, &text);
            JS_FreeValue(impl->context, result);
            for (int i = 0; succeeded && i < seen.len; i++) {
                if (StrEq(seen[i], text)) {
                    JS_ThrowTypeError(
                        impl->context,
                        "virtual-list get_key returned a duplicate key");
                    succeeded = false;
                }
            }
            if (succeeded) {
                VecAppend(seen, text);
                VecAppend(itemKeys, StrDup(cx->a, text));
            }
        }
        VecReset(seen);
        ArenaDelete(keys);
    }
    impl->scratch = outer;
    if (!succeeded) {
        Arena* arena = ArenaNew();
        log(ExceptionText(arena, impl->context));
        ArenaDelete(arena);
    } else {
        ShellError error = {};
        for (int i = 0; i < roots.len; i++) {
            out[i] = ShellMaterializeSpec(cx, this, batch, roots[i], &error);
            if (error.IsSet()) {
                log(error.message);
                ShellErrorClear(&error);
            }
            // The hit box that reports the row's stable domain key, and only
            // when the script asked for it: a list with no row handler gets its
            // rows exactly as the renderer built them. A secondary press is
            // reported with the row's own box, the way a press on any other
            // element is, so `local_position` means the same thing in both
            // handlers.
            if ((onItemClick || onItemSecondaryClick) && out[i] &&
                i < itemKeys.len) {
                Str rowId = StrDup(
                    cx->a, fmt("gpui-shell-virtual-item:%s", itemKeys[i]));
                El* row =
                    Div(cx->a)->PathClick(rowId)->SizeFull()->Child(out[i]);
                if (onItemClick) {
                    ShellStringBinding* binding =
                        ArenaNew<ShellStringBinding>(cx->a);
                    binding->callback = onItemClick;
                    binding->value = itemKeys[i];
                    row->OnClick(ListenTo(Entity<ScriptView>{render->view},
                                          &ScriptView::OnBoundString,
                                          (intptr_t)binding));
                }
                if (onItemSecondaryClick) {
                    ShellStringBinding* binding =
                        ArenaNew<ShellStringBinding>(cx->a);
                    binding->callback = onItemSecondaryClick;
                    binding->value = itemKeys[i];
                    row->OnMouseDown(ListenTo(Entity<ScriptView>{render->view},
                                              &ScriptView::OnItemSecondaryPress,
                                              (intptr_t)binding));
                }
                out[i] = row;
            }
        }
    }
    VecReset(itemKeys);
    VecReset(roots);
    delete batch;
    double elapsed = TimeNow() - started;
    shell::MetricsAdd(&impl->metrics, shell::MetricsTimerKind::FrameScript,
                      elapsed <= 0 ? 0 : (uint64_t)(elapsed * 1e9));
}

// Past the bound the whole cache goes rather than one entry: which container
// is worth keeping is not a question this has an answer to, and an area with
// thousands of live containers is describing each of them again anyway.
static const int kMaxChromeCacheEntries = 4096;

static void ClearChromeCache(ShellRuntimeImpl* impl) {
    for (int i = 0; i < impl->chromeCache.len; i++) {
        StrFree(impl->chromeCache[i]->payload);
        delete impl->chromeCache[i]->arena;
        delete impl->chromeCache[i];
    }
    VecReset(impl->chromeCache);
}

El* ShellRuntime::DescribeDockChrome(Ctx* cx, shell::EntityHandle dock,
                                     shell::DockChromeSlot slot, uint64_t key,
                                     shell::CallbackId handler, Str payload) {
    if (!cx || !cx->win || !cx->app || !handler) return nullptr;
    CallbackEntry* entry = impl->callbacks.Get(handler);
    if (!entry) return nullptr;
    if (entry->view.IsValid() && !EntityGet(cx->app, entry->view)) {
        return nullptr;
    }

    ChromeCacheEntry* cached = nullptr;
    for (int i = 0; i < impl->chromeCache.len; i++) {
        ChromeCacheEntry* candidate = impl->chromeCache[i];
        if (candidate->dock != dock || candidate->slot != slot ||
            candidate->key != key) {
            continue;
        }
        cached = candidate;
        break;
    }
    // The handler runs only when there is no description for this callback and
    // this state yet. A description that threw is not one — it is retried on
    // the next frame, and whatever stood before it is left alone, because the
    // state it answers is the state that just failed. A description that
    // answered null *is* one: the hook is optional, and nothing is a valid
    // answer.
    bool stale = !cached || cached->callback != handler ||
                 !StrEq(cached->payload, payload);
    if (stale) {
        double started = TimeNow();
        shell::SpecArena* outer = impl->scratch;
        auto* batch = new shell::SpecArena();
        impl->scratch = batch;
        shell::SpecId root = 0;
        bool hasRoot = false;
        bool succeeded = true;
        {
            // The same three protections a virtual list's item renderer gets:
            // a Layout scope, which forbids cx.notify() and creating retained
            // state; an arena of its own, so the description cannot leak into
            // whichever snapshot is being built; and no job drain on the way
            // out, because a promise continuation is unbounded application
            // code and the layout pass is the last place to run one.
            shell::CallScopeGuard scope = shell::ScopeEnter(
                cx->win, cx->app, ScopePhase::Layout, entry->view,
                entry->policy, this, entry->application);
            // The handler is a closure the script wrote inside render(cx), and
            // the element helpers it calls take that cx. Layout is a frame of
            // its own, so without this the enclosing render's cx would read as
            // stale here.
            shell::ScopeAdopt(entry->registeredIn);
            BeginExecution(impl);
            JSValue state = JS_ParseJSON(impl->context, payload.s,
                                         (size_t)payload.len, "<dock>");
            JSValue context = ContextObject(impl, scope.Generation());
            JSValue args[2] = {state, context};
            JSValue produced = JS_IsException(state)
                                   ? JS_EXCEPTION
                                   : JS_Call(impl->context, entry->function,
                                             JS_UNDEFINED, 2, args);
            JS_FreeValue(impl->context, context);
            if (!JS_IsException(state)) JS_FreeValue(impl->context, state);
            if (JS_IsException(produced)) {
                succeeded = false;
            } else if (!JS_IsNull(produced) && !JS_IsUndefined(produced)) {
                succeeded = ElementId(impl->context, produced, &root);
                hasRoot = succeeded;
            }
            JS_FreeValue(impl->context, produced);
        }
        impl->scratch = outer;
        double elapsed = TimeNow() - started;
        shell::MetricsAdd(&impl->metrics, shell::MetricsTimerKind::FrameScript,
                          elapsed <= 0 ? 0 : (uint64_t)(elapsed * 1e9));
        if (!succeeded) {
            Arena* arena = ArenaNew();
            log(ExceptionText(arena, impl->context));
            ArenaDelete(arena);
            delete batch;
            return nullptr;
        }
        if (!cached) {
            if (impl->chromeCache.len >= kMaxChromeCacheEntries) {
                ClearChromeCache(impl);
            }
            cached = new ChromeCacheEntry();
            cached->dock = dock;
            cached->slot = slot;
            cached->key = key;
            VecAppend(impl->chromeCache, cached);
        }
        StrFree(cached->payload);
        delete cached->arena;
        cached->callback = handler;
        cached->payload = StrDup(payload);
        cached->arena = batch;
        cached->root = root;
        cached->hasRoot = hasRoot;
    }

    if (!cached->hasRoot) return nullptr;
    ShellError error = {};
    El* element =
        ShellMaterializeSpec(cx, this, cached->arena, cached->root, &error);
    if (error.IsSet()) {
        log(error.message);
        ShellErrorClear(&error);
    }
    return element;
}

ViewType* ViewTypeRetain(ViewType* type) {
    if (type) type->refs++;
    return type;
}

void ViewTypeRelease(ViewType* type) {
    if (!type || --type->refs != 0) return;
    JS_FreeValue(ShellRuntimeAccess::Impl(type->runtime)->context, type->value);
    type->runtime->Release();
    delete type;
}

ViewObject* ViewObjectRetain(ViewObject* object) {
    if (object) object->refs++;
    return object;
}

void ViewObjectRelease(ViewObject* object) {
    if (!object || --object->refs != 0) return;
    JS_FreeValue(ShellRuntimeAccess::Impl(object->runtime)->context,
                 object->value);
    object->runtime->Release();
    delete object;
}

} // namespace gpui
