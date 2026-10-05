#include "shell/snapshot.h"

namespace gpui {

RenderSnapshot::RenderSnapshot(uint64_t generation, shell::SpecId root,
                               shell::SpecArena* arena,
                               SnapshotRuntimeLease runtime)
    : generation(generation), root(root), arena(arena), runtime(runtime) {
    if (arena) arena->snapshot = this;
    if (runtime.state && runtime.retain) runtime.retain(runtime.state);
}

RenderSnapshot::~RenderSnapshot() {
    if (runtime.state && runtime.retireCallbacks) {
        runtime.retireCallbacks(runtime.state, generation);
    }
    if (runtime.state && runtime.release) runtime.release(runtime.state);
    delete arena;
}

shell::StructureFingerprint RenderSnapshot::Structure() const {
    return arena ? arena->Structure() : shell::StructureFingerprint{};
}

int RenderSnapshot::Len() const {
    return arena ? arena->Len() : 0;
}

bool RenderSnapshot::IsEmpty() const {
    return !arena || arena->IsEmpty();
}

Str RenderSnapshot::DebugTree(Arena* into) const {
    return arena ? arena->DebugTree(into, root) : Str{};
}

} // namespace gpui
