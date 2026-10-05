#ifndef GPUI_SHELL_SNAPSHOT_H_
#define GPUI_SHELL_SNAPSHOT_H_

#include "shell/spec.h"

namespace gpui {

struct SnapshotRuntimeLease {
    void* state = nullptr;
    void (*retain)(void* state) = nullptr;
    void (*release)(void* state) = nullptr;
    void (*retireCallbacks)(void* state, uint64_t generation) = nullptr;
};

class RenderSnapshot {
  public:
    RenderSnapshot(uint64_t generation, shell::SpecId root,
                   shell::SpecArena* arena, SnapshotRuntimeLease runtime = {});
    RenderSnapshot(const RenderSnapshot&) = delete;
    RenderSnapshot& operator=(const RenderSnapshot&) = delete;
    ~RenderSnapshot();

    // App-thread leases keep both the description and its callbacks alive.
    // Release the owning reference when a view replaces its snapshot.
    RenderSnapshot* Retain() {
        references++;
        return this;
    }
    void Release() {
        if (--references == 0) delete this;
    }

    uint64_t Generation() const { return generation; }
    shell::SpecId Root() const { return root; }
    const shell::SpecArena* Specs() const { return arena; }
    int Len() const;
    // The shape of this description, with its values left out. Compared
    // against its predecessor's by ScriptView to count how often a rebuild
    // produced the structure it replaced.
    shell::StructureFingerprint Structure() const;
    bool IsEmpty() const;
    Str DebugTree(Arena* into) const;

  private:
    int references = 1;
    uint64_t generation = 0;
    shell::SpecId root = 0;
    shell::SpecArena* arena = nullptr;
    SnapshotRuntimeLease runtime;
};

} // namespace gpui
#endif // GPUI_SHELL_SNAPSHOT_H_
