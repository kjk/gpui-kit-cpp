#include "shell/watch.h"

#include <string.h>

namespace gpui::shell {

struct PendingDirectory {
    char path[kMaxPath] = {};
    int depth = 0;
};

static bool IsSource(Str name) {
    return StrEndsWith(name, ".js") || StrEndsWith(name, ".mjs");
}

static bool SkipDirectory(Str name) {
    return !name || name.s[0] == '.' || StrEq(name, StrL("node_modules")) ||
           StrEq(name, StrL("target"));
}

bool ScanSourceTree(Str directory, SourceTreeStamp* stamp, ShellError* error,
                    int maxFiles) {
    ShellErrorClear(error);
    if (stamp) *stamp = {};
    if (!directory.s || directory.len <= 0 || directory.len >= kMaxPath ||
        maxFiles < 0) {
        ShellErrorSet(error,
                      StrL("source watch directory is empty or too long"));
        return false;
    }

    Vec<PendingDirectory> pending;
    PendingDirectory root;
    memcpy(root.path, directory.s, (size_t)directory.len);
    root.path[directory.len] = 0;
    if (!VecAppend(pending, root)) {
        ShellErrorSet(error, StrL("out of memory while scanning source tree"));
        return false;
    }

    constexpr int kMaxEntriesPerDirectory = kSourceWatchMaxFiles + 1;
    DirEntry* entries = AllocArray<DirEntry>(kMaxEntriesPerDirectory);
    if (!entries) {
        ShellErrorSet(error, StrL("out of memory while scanning source tree"));
        return false;
    }

    SourceTreeStamp found;
    bool ok = true;
    while (len(pending) > 0 && ok) {
        PendingDirectory dir = pending[len(pending) - 1];
        pending.len--;
        int count = PlatListDir(dir.path, entries, kMaxEntriesPerDirectory);
        if (count >= kMaxEntriesPerDirectory) {
            ShellErrorSet(error, fmt("source watch for `%s` exceeds the "
                                     "%d-entry per-directory limit",
                                     directory, kSourceWatchMaxFiles));
            ok = false;
            break;
        }
        for (int i = 0; i < count; i++) {
            const DirEntry& item = entries[i];
            if (item.isSymlink || item.name[0] == '.') continue;
            if (item.isDir) {
                if (dir.depth >= kSourceWatchMaxDepth ||
                    SkipDirectory(Str(item.name))) {
                    continue;
                }
                if (len(pending) >= kSourceWatchMaxFiles) {
                    ShellErrorSet(error, fmt("source watch for `%s` exceeds "
                                             "the %d-directory limit",
                                             directory, kSourceWatchMaxFiles));
                    ok = false;
                    break;
                }
                PendingDirectory child;
                child.depth = dir.depth + 1;
                TempStr childPath = fmt("%s/%s", Str(dir.path), Str(item.name));
                if (!childPath || childPath.len >= (int)sizeof(child.path)) {
                    ShellErrorSet(error, StrL("source path is too long or "
                                              "could not be recorded"));
                    ok = false;
                    break;
                }
                memcpy(child.path, childPath.s, (size_t)childPath.len + 1);
                if (!VecAppend(pending, child)) {
                    ShellErrorSet(error, StrL("source path is too long or "
                                              "could not be recorded"));
                    ok = false;
                    break;
                }
                continue;
            }
            if (!item.isFile || !IsSource(Str(item.name))) continue;
            if (found.files >= (uint32_t)maxFiles) {
                ShellErrorSet(
                    error,
                    fmt("source watch for `%s` exceeds the %d-file limit",
                        directory, maxFiles));
                ok = false;
                break;
            }
            found.files++;
            if (UINT64_MAX - found.bytes < item.size)
                found.bytes = UINT64_MAX;
            else
                found.bytes += item.size;
            if (found.newest < item.modified) found.newest = item.modified;
        }
    }
    Free(nullptr, entries);
    if (ok && stamp) *stamp = found;
    return ok;
}

SourceWatcher::~SourceWatcher() {
    StrFree(directory);
}

bool SourceWatcher::Init(Str value, ShellError* error, int debounce) {
    ShellErrorClear(error);
    SourceTreeStamp initial;
    if (!ScanSourceTree(value, &initial, error)) return false;
    Str copy = StrDup(value);
    if (!copy.s && value.len > 0) {
        ShellErrorSet(error, StrL("out of memory while starting source watch"));
        return false;
    }
    StrFree(directory);
    directory = copy;
    stamp = initial;
    changedAt = 0;
    debounceMs = debounce < 0 ? 0 : debounce;
    pending = false;
    return true;
}

bool SourceWatcher::Poll(bool* changed, ShellError* error) {
    return PollAt(TimeNow(), changed, error);
}

bool SourceWatcher::PollAt(double now, bool* changed, ShellError* error) {
    if (changed) *changed = false;
    SourceTreeStamp next;
    if (!ScanSourceTree(directory, &next, error)) return false;
    return Observe(next, now, changed);
}

bool SourceWatcher::Observe(const SourceTreeStamp& next, double now,
                            bool* changed) {
    if (changed) *changed = false;
    if (next != stamp) {
        stamp = next;
        changedAt = now;
        pending = true;
    }
    if (pending && (now - changedAt) * 1000.0 >= debounceMs) {
        pending = false;
        if (changed) *changed = true;
    }
    return true;
}

} // namespace gpui::shell

namespace gpui {

struct SourceScanJob {
    App* app = nullptr;
    Window* window = nullptr;
    EntityId watcher = {};
    Str directory;
    shell::SourceTreeStamp stamp;
    ShellError error = {};
    bool ok = false;

    ~SourceScanJob() {
        StrFree(directory);
        ShellErrorClear(&error);
    }
};

static bool WatchWindowLive(App* app, Window* window) {
    if (!app || !window) return false;
    for (int i = 0; i < app->windows.len; i++) {
        if (app->windows[i] == window) return true;
    }
    return false;
}

static void SourceScanWork(SourceScanJob* job) {
    job->ok = shell::ScanSourceTree(job->directory, &job->stamp, &job->error);
}

static void SourceScanDone(SourceScanJob* job) {
    ShellWatcher* watcher =
        job && job->app ? (ShellWatcher*)EntityGet(job->app, job->watcher)
                        : nullptr;
    if (!watcher || watcher->scanJob != job ||
        !WatchWindowLive(job->app, job->window)) {
        delete job;
        return;
    }
    watcher->scanJob = nullptr;
    watcher->scanTask = 0;
    if (!job->ok) {
        ShellErrorClear(&watcher->error);
        watcher->error = job->error;
        job->error = {};
        if (watcher->timer) {
            WindowCancelTimer(watcher->window, watcher->timer);
            watcher->timer = 0;
        }
        delete job;
        return;
    }

    bool changed = false;
    watcher->source.Observe(job->stamp, TimeNow(), &changed);
    delete job;
    if (!changed) return;
    ScriptView* view =
        (ScriptView*)EntityGet(watcher->window->app, watcher->view);
    if (!view) {
        if (watcher->timer) {
            WindowCancelTimer(watcher->window, watcher->timer);
            watcher->timer = 0;
        }
        return;
    }
    Ctx cx = {watcher->window->app, watcher->window,
              watcher->window->frameArena, watcher->view};
    ShellError reload = {};
    if (!ScriptView::Reload(view, &cx, watcher->directory, watcher->entry,
                            &reload)) {
        ShellErrorClear(&watcher->error);
        watcher->error = reload;
        reload = {};
        if (watcher->error.IsSet()) log(watcher->error.message);
        return;
    }
    ShellErrorClear(&watcher->error);
}

ShellWatcher::~ShellWatcher() {
    if (window && timer) WindowCancelTimer(window, timer);
    if (scanTask && ExecCancel(scanTask)) {
        delete (SourceScanJob*)scanJob;
    }
    scanTask = 0;
    scanJob = nullptr;
    ShellErrorClear(&error);
    StrFree(directory);
    StrFree(entry);
    if (runtime) runtime->Release();
}

Entity<ShellWatcher> ShellWatcher::Start(ShellRuntime* runtime,
                                         Entity<ScriptView> view, Str directory,
                                         Str entry, Window* window, App* app,
                                         ShellError* error) {
    ShellErrorClear(error);
    ScriptView* target = view.Get(app);
    if (!runtime || !target || target->runtime != runtime || !window ||
        window->app != app) {
        ShellErrorSet(error, StrL("source watch needs a live ScriptView from "
                                  "this runtime and window"));
        return {};
    }
    Entity<ShellWatcher> entity = EntityNewState<ShellWatcher>(app);
    ShellWatcher* watcher = entity.Get(app);
    if (!watcher) {
        ShellErrorSet(error, StrL("could not allocate source watcher"));
        return {};
    }
    watcher->runtime = runtime->Retain();
    watcher->view = view.id;
    watcher->window = window;
    watcher->directory = StrDup(directory);
    watcher->entry = StrDup(entry);
    if ((!watcher->directory.s && directory.len > 0) ||
        (!watcher->entry.s && entry.len > 0) ||
        !watcher->source.Init(directory, error)) {
        EntityDrop(app, entity.id);
        return {};
    }
    watcher->timer = WindowSetInterval(window, shell::kSourceWatchPollMs,
                                       ListenTo(entity, &ShellWatcher::OnPoll));
    if (!watcher->timer) {
        ShellErrorSet(error, StrL("could not arm source watcher timer"));
        EntityDrop(app, entity.id);
        return {};
    }
    return entity;
}

void ShellWatcher::OnPoll(ShellWatcher* self, Ctx* cx, const TickEvent*) {
    if (!self || !cx || self->scanTask || self->scanJob) return;
    SourceScanJob* job = new SourceScanJob();
    job->app = cx->app;
    job->window = cx->win;
    job->watcher = cx->self;
    job->directory = StrDup(self->directory);
    if (!job->directory.s && self->directory.len > 0) {
        delete job;
        ShellErrorSet(&self->error,
                      StrL("out of memory while scheduling source scan"));
        return;
    }
    int task =
        ExecSpawn(MkFunc0(SourceScanWork, job), MkFunc0(SourceScanDone, job));
    if (!task) {
        delete job;
        ShellErrorSet(&self->error,
                      StrL("could not schedule source directory scan"));
        return;
    }
    self->scanJob = job;
    self->scanTask = task;
}

} // namespace gpui
