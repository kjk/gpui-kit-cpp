#ifndef GPUI_SHELL_STORAGE_H_
#define GPUI_SHELL_STORAGE_H_

#include "base/json.h"

namespace gpui::shell {

constexpr int kMaxStorageBytes = 8 * 1024 * 1024;
constexpr int kMaxStorageKeys = 4096;
constexpr int kMaxStorageValueBytes = 1024 * 1024;
constexpr int kMaxStorageWaiters = 1024;

bool StorageReplaceFile(Str temporary, Str path, Str* error);

struct StorageEntry {
    Str key;
    Str value;
};

struct StorageWrite {
    uint64_t revision = 0;
    Str path;
    Str body;

    void Free();
};

struct StorageOutcome {
    bool ok = false;
    Str error;
};

struct StorageWaiter {
    uint64_t revision = 0;
    Func1<StorageOutcome> settle;
};

class Storage {
  public:
    explicit Storage(bool persisted = false);
    Storage(const Storage&) = delete;
    Storage& operator=(const Storage&) = delete;
    ~Storage();

    bool SetPath(Str path, Str* error = nullptr);
    Str Get(Str key) const;
    bool Set(Str key, Str value, Str* error = nullptr);
    bool Remove(Str key, Str* error = nullptr);
    bool Clear(Str* error = nullptr);
    int Len() const { return entries.len; }
    Str Key(int index) const;
    bool HasPath() const { return path.s != nullptr; }

    uint64_t Revision() const { return revision; }
    uint64_t WrittenRevision() const { return written; }
    bool IsDirty() const;
    bool HasWriteInFlight() const { return inFlight != 0; }
    bool BeginWrite(StorageWrite* write, Str* error = nullptr);
    void FinishWrite(uint64_t writeRevision, bool ok,
                     Vec<StorageWaiter*>* ready);
    // The write never ran — the executor refused it. The waiters it would
    // have settled are handed back as failed, so no flush() hangs on it.
    void AbortWrite(uint64_t writeRevision, Vec<StorageWaiter*>* ready);
    bool Wait(Func1<StorageOutcome> settle, StorageWaiter** waiter,
              bool* immediate, Str* error = nullptr);
    void CancelWaiter(StorageWaiter* waiter);

  private:
    Vec<StorageEntry*> entries;
    Vec<StorageWaiter*> waiters;
    Str path;
    bool persisted = false;
    uint64_t revision = 0;
    uint64_t written = 0;
    uint64_t inFlight = 0;
    uint64_t failed = 0;

    bool Load(Str* error);
    bool Encode(Str* body, Str* error) const;
    bool ValidateEncoded(Str* error) const;
    void Touch();
    void ResetEntries();
    void ReadyThrough(uint64_t through, Vec<StorageWaiter*>* ready);
};

bool StoragePersist(const StorageWrite& write, Str* error = nullptr);

} // namespace gpui::shell
#endif // GPUI_SHELL_STORAGE_H_
