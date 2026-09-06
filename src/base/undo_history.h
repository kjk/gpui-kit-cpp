#ifndef GPUI_BASE_UNDO_HISTORY_H_
#define GPUI_BASE_UNDO_HISTORY_H_
/* Grouped transactions — crates/base/src/undo_history.rs. A transaction owns
   its Vec; only pointers to transactions are stored in the outer POD Vec. */
#include "gpui/gpui.h"

namespace gpui {
template <typename T>
struct UndoHistory {
    Vec<Vec<T>*> undos;
    Vec<Vec<T>*> redos;
    double lastChangedAt = 0;
    bool hasLastChangedAt = false;
    int maxUndos = 1000;
    double groupInterval = 0;
    bool hasGroupInterval = false;
    bool grouping = false;
    bool ignoring = false;

    UndoHistory() = default;
    UndoHistory(const UndoHistory&) = delete;
    UndoHistory& operator=(const UndoHistory&) = delete;
    ~UndoHistory() { Clear(); }
    UndoHistory& MaxUndos(int n) {
        maxUndos = std::max(0, n);
        EnforceMaxUndos();
        return *this;
    }
    UndoHistory& GroupInterval(double seconds) {
        groupInterval = std::max(0.0, seconds);
        hasGroupInterval = true;
        return *this;
    }
    UndoHistory& GroupIntervalMs(int64_t ms) {
        return GroupInterval((double)ms / 1000);
    }
    void StartGrouping() { grouping = true; }
    void EndGrouping() { grouping = false; }
    bool IsIgnoring() const { return ignoring; }
    void SetIgnoring(bool value) { ignoring = value; }
    bool CanUndo() const { return undos.len > 0; }
    bool CanRedo() const { return redos.len > 0; }
    void Push(T item) {
        if (ignoring || maxUndos == 0) return;
        double now = TimeNow();
        bool group = grouping || (hasLastChangedAt && hasGroupInterval &&
                                  now - lastChangedAt <= groupInterval);
        if (!group || !undos.len) {
            VecAppend(undos, new Vec<T>());
            EnforceMaxUndos();
        }
        VecAppend(*undos[undos.len - 1], item);
        lastChangedAt = now;
        hasLastChangedAt = true;
        ClearStack(redos);
    }
    Vec<T> Undo() { return Move(undos, redos, true); }
    Vec<T> Redo() {
        if (maxUndos == 0) return Vec<T>();
        Vec<T> result = Move(redos, undos, false);
        EnforceMaxUndos();
        return result;
    }
    void Clear() {
        ClearStack(undos);
        ClearStack(redos);
        hasLastChangedAt = false;
    }

  private:
    static void ClearStack(Vec<Vec<T>*>& stack) {
        for (auto* transaction : stack) delete transaction;
        VecClear(stack);
    }
    void EnforceMaxUndos() {
        int excess = undos.len - maxUndos;
        if (excess <= 0) return;
        for (int i = 0; i < excess; i++) delete undos[i];
        for (int i = excess; i < undos.len; i++) undos[i - excess] = undos[i];
        undos.len -= excess;
    }
    Vec<T> Move(Vec<Vec<T>*>& from, Vec<Vec<T>*>& to, bool reverse) {
        Vec<T> result;
        if (!from.len) return result;
        Vec<T>* transaction = from[from.len - 1];
        from.len--;
        for (int i = 0; i < transaction->len; i++) {
            VecAppend(result,
                      (*transaction)[reverse ? transaction->len - 1 - i : i]);
        }
        VecAppend(to, transaction);
        hasLastChangedAt = false;
        return result;
    }
};
} // namespace gpui
#endif // GPUI_BASE_UNDO_HISTORY_H_
