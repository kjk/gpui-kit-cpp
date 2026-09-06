#ifndef GPUI_BASE_HISTORY_H_
#define GPUI_BASE_HISTORY_H_
/* Browser-style navigation trail -- crates/base/src/history.rs.
   Values are POD-friendly copies; pointer ownership stays with callers. */
#include "gpui/gpui.h"
namespace gpui {
template <typename T>
struct HistoryForwardEntries {
    const T* items = nullptr;
    int len = 0;
    const T& operator[](int i) const { return items[len - 1 - i]; }
};
template <typename T>
struct History {
    Vec<T> entries;
    Vec<T> forwardEntries;
    int maxEntries = 1000;
    History& MaxEntries(int n) {
        maxEntries = std::max(0, n);
        EnforceMaxEntries();
        return *this;
    }
    void Push(T entry) {
        VecClear(forwardEntries);
        if (maxEntries == 0) return;
        VecAppend(entries, entry);
        EnforceMaxEntries();
    }
    const T* Current() const {
        return entries.len ? &entries[entries.len - 1] : nullptr;
    }
    void ReplaceCurrent(T entry) {
        if (entries.len)
            entries[entries.len - 1] = entry;
        else
            Push(entry);
    }
    bool RemoveCurrent(T* out = nullptr) {
        if (!entries.len) return false;
        if (out) *out = entries[entries.len - 1];
        entries.len--;
        return true;
    }
    bool CanBack() const { return entries.len > 1; }
    bool CanForward() const { return forwardEntries.len > 0; }
    bool Back(T* out = nullptr) {
        if (!CanBack()) return false;
        VecAppend(forwardEntries, entries[entries.len - 1]);
        entries.len--;
        if (out) *out = *Current();
        return true;
    }
    bool Forward(T* out = nullptr) {
        if (maxEntries == 0 || !CanForward()) return false;
        VecAppend(entries, forwardEntries[forwardEntries.len - 1]);
        forwardEntries.len--;
        EnforceMaxEntries();
        if (out) *out = *Current();
        return true;
    }
    const Vec<T>& Entries() const { return entries; }
    HistoryForwardEntries<T> ForwardEntries() const {
        return {forwardEntries.els, forwardEntries.len};
    }
    void Retain(bool (*keep)(const T&, void*), void* user = nullptr) {
        RetainIf(entries, keep, user);
        RetainIf(forwardEntries, keep, user);
    }
    void Clear() {
        VecClear(entries);
        VecClear(forwardEntries);
    }

  private:
    void EnforceMaxEntries() {
        int excess = entries.len - maxEntries;
        if (excess <= 0) return;
        for (int i = excess; i < entries.len; i++)
            entries[i - excess] = entries[i];
        entries.len -= excess;
    }
    static void RetainIf(Vec<T>& values, bool (*keep)(const T&, void*),
                         void* user) {
        if (!keep) return;
        int n = 0;
        for (const T& value : values)
            if (keep(value, user)) values[n++] = value;
        values.len = n;
    }
};
} // namespace gpui
#endif // GPUI_BASE_HISTORY_H_
