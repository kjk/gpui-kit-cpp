#ifndef GPUI_BASE_TOAST_H_
#define GPUI_BASE_TOAST_H_
/* Unstyled toast — crates/base/src/toast.rs */

#include "base/motion.h"

namespace gpui {

enum class ToastTransitionStatus : uint8_t {
    Starting,
    Present,
    Ending
};

// Compatibility spelling used by the original notification port.
using ToastStatus = ToastTransitionStatus;

struct ToastMotion {
    int durationMs = 400;
    int exitDurationMs = 200;
    float collapsedPeek = 14.f;
    float expandedGap = 14.f;
    float collapsedScaleStep = 0.05f;
    int collapsedVisible = 3;

    static ToastMotion Sonner();
};

struct ToastOptions {
    bool hasTimeout = false;
    int timeoutMs = 0;

    static ToastOptions Timeout(int milliseconds);
    static ToastOptions Persistent();
};

template <typename I, typename T>
struct ManagedToast {
    I id = {};
    T value = {};
    ToastTransitionStatus status = ToastTransitionStatus::Starting;
    bool hasTimeout = false;
    int timeoutRemainingMs = 0;
    int transitionElapsedMs = 0;
    int64_t lastAdvanceMs = 0;
};

template <typename I, typename T>
struct ToastRemoved {
    I id = {};
    T value = {};
};

// Rust returns Vecs from advance. Vec is the owning no-STL equivalent; I and
// T must consequently be POD, like every persistent Vec element in this tree.
template <typename I, typename T>
struct ToastAdvance {
    bool changed = false;
    Vec<I> presented;
    Vec<I> ending;
    Vec<ToastRemoved<I, T>> removed;
};

template <typename I, typename T>
struct ToastVisible {
    const I* id = nullptr;
    const T* value = nullptr;
    ToastTransitionStatus status = ToastTransitionStatus::Starting;
};

template <typename I, typename T>
struct ToastManager {
    Vec<ManagedToast<I, T>> entries;
    int transitionDurationMs = 400;
    int exitDurationMs = 200;

    static ToastManager New(ToastMotion motion) {
        ToastManager out;
        out.transitionDurationMs = motion.durationMs;
        out.exitDurationMs = motion.exitDurationMs;
        return out;
    }

    int Len() const { return len(entries); }
    bool IsEmpty() const { return len(entries) == 0; }

    const ManagedToast<I, T>* At(int index) const {
        return index >= 0 && index < len(entries) ? &entries[index] : nullptr;
    }

    const T* Get(const I& id) const {
        for (int i = 0; i < len(entries); i++) {
            if (entries[i].id == id) {
                return &entries[i].value;
            }
        }
        return nullptr;
    }

    // Writes newest visible active entries plus every ending entry, in
    // display order. The returned views borrow this manager.
    int Visible(int limit, ToastVisible<I, T>* out, int cap) const {
        int active = 0;
        for (int i = 0; i < len(entries); i++) {
            if (entries[i].status != ToastTransitionStatus::Ending) {
                active++;
            }
        }
        int first = active > limit ? active - limit : 0;
        int activeIndex = 0;
        int count = 0;
        for (int i = 0; i < len(entries); i++) {
            const ManagedToast<I, T>& entry = entries[i];
            bool ending = entry.status == ToastTransitionStatus::Ending;
            bool visible = ending || activeIndex >= first;
            if (!ending) {
                activeIndex++;
            }
            if (!visible) {
                continue;
            }
            if (out && count < cap) {
                out[count] = {&entry.id, &entry.value, entry.status};
            }
            count++;
        }
        return count;
    }

    // `hadReplaced` is Rust's Option<T> discriminator; when it is true the
    // removed value is transferred through `replaced`.
    bool Push(const I& id, const T& value, ToastOptions options, int64_t nowMs,
              T* replaced = nullptr, bool* hadReplaced = nullptr) {
        if (hadReplaced) {
            *hadReplaced = false;
        }
        for (int i = 0; i < len(entries); i++) {
            if (!(entries[i].id == id)) {
                continue;
            }
            if (replaced) {
                *replaced = entries[i].value;
            }
            if (hadReplaced) {
                *hadReplaced = true;
            }
            EraseAt(i);
            break;
        }
        ManagedToast<I, T> entry;
        entry.id = id;
        entry.value = value;
        entry.hasTimeout = options.hasTimeout;
        entry.timeoutRemainingMs = options.timeoutMs;
        entry.lastAdvanceMs = nowMs;
        return VecAppend(entries, entry);
    }

    bool Dismiss(const I& id, int64_t nowMs) {
        for (int i = 0; i < len(entries); i++) {
            ManagedToast<I, T>& entry = entries[i];
            if (!(entry.id == id) ||
                entry.status == ToastTransitionStatus::Ending) {
                continue;
            }
            entry.status = ToastTransitionStatus::Ending;
            entry.transitionElapsedMs = 0;
            entry.lastAdvanceMs = nowMs;
            return true;
        }
        return false;
    }

    Vec<I> DismissAll(int64_t nowMs) {
        Vec<I> changed;
        for (int i = 0; i < len(entries); i++) {
            ManagedToast<I, T>& entry = entries[i];
            if (entry.status == ToastTransitionStatus::Ending) {
                continue;
            }
            entry.status = ToastTransitionStatus::Ending;
            entry.transitionElapsedMs = 0;
            entry.lastAdvanceMs = nowMs;
            VecAppend(changed, entry.id);
        }
        return changed;
    }

    ToastAdvance<I, T> Advance(int64_t nowMs, bool paused) {
        ToastAdvance<I, T> out;
        for (int i = 0; i < len(entries); i++) {
            ManagedToast<I, T>& entry = entries[i];
            int64_t elapsed = nowMs - entry.lastAdvanceMs;
            int delta = elapsed > 0x7fffffffLL ? 0x7fffffff
                        : elapsed > 0          ? (int)elapsed
                                               : 0;
            entry.lastAdvanceMs = nowMs;
            switch (entry.status) {
                case ToastTransitionStatus::Starting:
                    entry.transitionElapsedMs += delta;
                    if (entry.transitionElapsedMs >= transitionDurationMs) {
                        entry.status = ToastTransitionStatus::Present;
                        entry.transitionElapsedMs = 0;
                        VecAppend(out.presented, entry.id);
                        out.changed = true;
                    }
                    break;
                case ToastTransitionStatus::Present:
                    if (!paused && entry.hasTimeout) {
                        entry.timeoutRemainingMs -= delta;
                        if (entry.timeoutRemainingMs <= 0) {
                            entry.timeoutRemainingMs = 0;
                            entry.status = ToastTransitionStatus::Ending;
                            entry.transitionElapsedMs = 0;
                            VecAppend(out.ending, entry.id);
                            out.changed = true;
                        }
                    }
                    break;
                case ToastTransitionStatus::Ending:
                    entry.transitionElapsedMs += delta;
                    break;
            }
        }
        int index = 0;
        while (index < len(entries)) {
            ManagedToast<I, T>& entry = entries[index];
            if (entry.status != ToastTransitionStatus::Ending ||
                entry.transitionElapsedMs < exitDurationMs) {
                index++;
                continue;
            }
            ToastRemoved<I, T> removed = {entry.id, entry.value};
            if (!VecAppend(out.removed, removed)) {
                index++;
                continue;
            }
            EraseAt(index);
            out.changed = true;
        }
        return out;
    }

  private:
    void EraseAt(int index) {
        for (int i = index; i < len(entries) - 1; i++) {
            entries[i] = entries[i + 1];
        }
        entries.len--;
        if (entries.els) {
            entries.els[len(entries)] = {};
        }
    }
};

struct ToastEntry {
    int id = 0;
    ToastTransitionStatus status = ToastTransitionStatus::Starting;
    bool hasTimeout = false;
    int timeoutRemainingMs = 0;
    int elapsedMs = 0;
};

constexpr int kToastTransitionMs = 400;
constexpr int kToastExitMs = 200;
constexpr float kToastCollapsedPeek = 14.f;
constexpr float kToastExpandedGap = 14.f;
constexpr float kToastCollapsedScaleStep = 0.05f;
constexpr int kToastCollapsedVisible = 3;

struct ToastMeasurement {
    uint32_t id = 0;
    Bounds bounds = {};
};

struct ToastStackState {
    // Compatibility storage used by ui/notification. New lifecycle code can
    // own a ToastManager independently, as the Rust source does.
    Vec<ToastEntry> entries;
    Vec<ToastMeasurement> heights;
    Bounds bounds = {};
    int transitionMs = kToastTransitionMs;
    int exitMs = kToastExitMs;
    bool hovered = false;
    bool focused = false;

    bool IsExpanded() const { return hovered || focused; }
};

float ToastStackGeometry(const float* heights, int n, float peek, float gap,
                         bool anchoredBottom, float* collapsedOffsets,
                         float* expandedOffsets, float* expandedHeight);

bool ToastPush(ToastStackState* state, int id, int timeoutMs);
bool ToastRemove(ToastStackState* state, int id);
bool ToastStackAdvance(ToastStackState* state, int deltaMs, bool paused);

struct ToastStackItem {
    Str id = {};
    uint32_t key = 0;
    El* child = nullptr;
};

struct ToastStack {
    Arena* arena = nullptr;
    Ctx* cx = nullptr;
    Str id = {};
    ToastStackState* state = nullptr;
    ToastMotion motion = {};
    Anchor placement = Anchor::TopRight;
    FocusHandle focus = {};
    bool hasFocus = false;
    Style style = {};
    uint32_t styleFields = 0;
    ArenaVec<ToastStackItem> children;

    static ToastStack* New(Ctx* cx, Str id, ToastStackState* state);
    ToastStack* Item(Str id, El* child);
    ToastStack* Child(El* child);
    ToastStack* Motion(ToastMotion value);
    ToastStack* Placement(Anchor value);
    ToastStack* Focus(FocusHandle value);
    ToastStack* Refine(const Style& value, uint32_t fields);
    El* IntoEl();
};

struct Toast {
    El* root = nullptr;
    ToastTransitionStatus transitionStatus = ToastTransitionStatus::Starting;

    static Toast* New(Ctx* cx, Str id);
    Toast* TransitionStatus(ToastTransitionStatus value);
    ToastTransitionStatus Status() const;
    Toast* Child(El* child);
    Toast* Refine(const Style& value, uint32_t fields);
    El* IntoEl();
};

} // namespace gpui
#endif // GPUI_BASE_TOAST_H_
