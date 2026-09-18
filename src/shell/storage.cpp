#include "shell/storage.h"

#include <errno.h>
#include <stdio.h>

namespace gpui::shell {

static void StorageError(Str* error, Str message) {
    if (!error) return;
    StrFree(*error);
    *error = StrDup(message);
}

static void ClearError(Str* error) {
    if (!error) return;
    StrFree(*error);
    *error = {};
}

void StorageWrite::Free() {
    StrFree(path);
    StrFree(body);
    *this = {};
}

Storage::Storage(bool writes) : persisted(writes) {}

Storage::~Storage() {
    ResetEntries();
    for (int i = 0; i < waiters.len; i++) delete waiters[i];
    VecReset(waiters);
    StrFree(path);
}

void Storage::ResetEntries() {
    for (int i = 0; i < entries.len; i++) {
        StrFree(entries[i]->key);
        StrFree(entries[i]->value);
        delete entries[i];
    }
    VecReset(entries);
}

bool Storage::SetPath(Str value, Str* error) {
    ClearError(error);
    if (inFlight || waiters.len) {
        StorageError(error, StrL("cannot replace the localStorage path while a "
                                 "write or flush is pending"));
        return false;
    }
    ResetEntries();
    StrFree(path);
    path = StrDup(value);
    revision = 0;
    written = 0;
    inFlight = 0;
    failed = 0;
    if (!path.s && len(value) != 0) {
        StorageError(error, StrL("allocating the storage path failed"));
        return false;
    }
    return Load(error);
}

bool Storage::Load(Str* error) {
    if (!persisted || !path) return true;
    FILE* file = fopen(path.s, "rb");
    if (!file) {
        if (errno == ENOENT) return true;
        StorageError(error, fmt("cannot read storage file `%s`", path));
        return false;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        StorageError(error, fmt("cannot read storage file `%s`", path));
        return false;
    }
    long size = ftell(file);
    if (size < 0 || size > kMaxStorageBytes || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        StorageError(error,
                     fmt("storage file `%s` exceeds the 8 MiB limit", path));
        return false;
    }
    char* bytes = (char*)Alloc(nullptr, (int)size + 1);
    if (!bytes) {
        fclose(file);
        StorageError(error, StrL("allocating the storage file failed"));
        return false;
    }
    size_t got = fread(bytes, 1, (size_t)size, file);
    fclose(file);
    bytes[size] = 0;
    if (got != (size_t)size) {
        Free(nullptr, bytes);
        StorageError(error, fmt("cannot read storage file `%s`", path));
        return false;
    }
    Arena* arena = ArenaNew();
    JsonValue* root = JsonParse(arena, Str(bytes, (int)size));
    bool ok = root && root->kind == JsonKind::Object;
    for (JsonValue* item = ok ? root->first : nullptr; item;
         item = item->next) {
        if (item->kind != JsonKind::String || entries.len >= kMaxStorageKeys ||
            len(item->key) > kMaxStorageValueBytes ||
            item->str.len > kMaxStorageValueBytes) {
            ok = false;
            break;
        }
        StorageEntry* entry = new StorageEntry();
        entry->key = StrDup(item->key);
        entry->value = StrDup(item->str);
        if ((!entry->key.s && len(item->key)) ||
            (!entry->value.s && item->str.len) || !VecAppend(entries, entry)) {
            StrFree(entry->key);
            StrFree(entry->value);
            delete entry;
            ok = false;
            break;
        }
    }
    ArenaDelete(arena);
    Free(nullptr, bytes);
    if (ok) ok = ValidateEncoded(error);
    if (!ok) {
        ResetEntries();
        if (!error || !*error) {
            StorageError(error,
                         fmt("`%s` is not a valid shell storage file", path));
        }
    }
    return ok;
}

Str Storage::Get(Str key) const {
    for (int i = 0; i < entries.len; i++) {
        if (StrEq(entries[i]->key, key)) return entries[i]->value;
    }
    return {};
}

bool Storage::Encode(Str* encoded, Str* error) const {
    if (encoded) {
        StrFree(*encoded);
        *encoded = {};
    }
    StrBuilder body;
    JsonWriter writer;
    writer.out = &body;
    writer.BeginObject();
    for (int i = 0; i < entries.len; i++) {
        writer.String(entries[i]->key.s, entries[i]->value);
    }
    writer.EndObject();
    Str result = body.TakeStr();
    if (!result.s && len(result) != 0) {
        StorageError(error, StrL("allocating the encoded storage file failed"));
        return false;
    }
    if (len(result) > kMaxStorageBytes) {
        StorageError(error,
                     StrL("the encoded storage file exceeds the 8 MiB limit"));
        StrFree(result);
        return false;
    }
    if (encoded)
        *encoded = result;
    else
        StrFree(result);
    return true;
}

bool Storage::ValidateEncoded(Str* error) const {
    return Encode(nullptr, error);
}

void Storage::Touch() {
    revision++;
    if (revision == 0) revision = 1;
}

bool Storage::Set(Str key, Str value, Str* error) {
    ClearError(error);
    if (len(key) > kMaxStorageValueBytes ||
        len(value) > kMaxStorageValueBytes) {
        StorageError(error,
                     StrL("a storage key or value exceeds the 1 MiB limit"));
        return false;
    }
    Str copy = StrDup(value);
    if (!copy.s && len(value) != 0) {
        StorageError(error, StrL("allocating the storage value failed"));
        return false;
    }
    for (int i = 0; i < entries.len; i++) {
        if (!StrEq(entries[i]->key, key)) continue;
        Str old = entries[i]->value;
        entries[i]->value = copy;
        if (!ValidateEncoded(error)) {
            entries[i]->value = old;
            StrFree(copy);
            return false;
        }
        StrFree(old);
        Touch();
        return true;
    }
    if (entries.len >= kMaxStorageKeys) {
        StrFree(copy);
        StorageError(error, StrL("storage reached its 4096-key limit"));
        return false;
    }
    StorageEntry* entry = new StorageEntry();
    entry->key = StrDup(key);
    entry->value = copy;
    if ((!entry->key.s && len(key) != 0) || !VecAppend(entries, entry)) {
        StrFree(entry->key);
        StrFree(entry->value);
        delete entry;
        StorageError(error, StrL("allocating the storage entry failed"));
        return false;
    }
    if (!ValidateEncoded(error)) {
        entries.len--;
        StrFree(entry->key);
        StrFree(entry->value);
        delete entry;
        return false;
    }
    Touch();
    return true;
}

bool Storage::Remove(Str key, Str* error) {
    ClearError(error);
    for (int i = 0; i < entries.len; i++) {
        if (!StrEq(entries[i]->key, key)) continue;
        StorageEntry* entry = entries[i];
        for (int j = i + 1; j < entries.len; j++) entries[j - 1] = entries[j];
        entries.len--;
        StrFree(entry->key);
        StrFree(entry->value);
        delete entry;
        break;
    }
    Touch();
    return true;
}

bool Storage::Clear(Str* error) {
    ClearError(error);
    if (entries.len) {
        ResetEntries();
        Touch();
    }
    return true;
}

Str Storage::Key(int index) const {
    return index >= 0 && index < entries.len ? entries[index]->key : Str{};
}

bool Storage::IsDirty() const {
    return persisted && revision > written;
}

bool Storage::BeginWrite(StorageWrite* write, Str* error) {
    ClearError(error);
    if (!write) {
        StorageError(error, StrL("a storage write needs an output record"));
        return false;
    }
    write->Free();
    if (inFlight || !IsDirty() || failed == revision) return true;
    if (!path) {
        StorageError(error, StrL("localStorage has no backing file; call "
                                 "ShellSetStoragePath first"));
        return false;
    }
    uint64_t snapshotRevision = revision;
    StorageWrite pending;
    pending.revision = snapshotRevision;
    pending.path = StrDup(path);
    if ((!pending.path.s && len(path) != 0) || !Encode(&pending.body, error)) {
        pending.Free();
        write->revision = snapshotRevision;
        return false;
    }
    inFlight = snapshotRevision;
    *write = pending;
    pending = {};
    pending.Free();
    return true;
}

void Storage::ReadyThrough(uint64_t through, Vec<StorageWaiter*>* ready) {
    int keep = 0;
    for (int i = 0; i < waiters.len; i++) {
        StorageWaiter* waiter = waiters[i];
        if (waiter->revision <= through && ready && VecAppend(*ready, waiter)) {
            continue;
        } else if (waiter->revision <= through && !ready) {
            delete waiter;
            continue;
        } else {
            waiters[keep++] = waiter;
        }
    }
    waiters.len = keep;
}

void Storage::FinishWrite(uint64_t writeRevision, bool ok,
                          Vec<StorageWaiter*>* ready) {
    if (inFlight == writeRevision) inFlight = 0;
    if (ok) {
        if (written < writeRevision) written = writeRevision;
        if (failed && failed <= writeRevision) failed = 0;
    } else {
        failed = writeRevision;
    }
    ReadyThrough(writeRevision, ready);
}

void Storage::AbortWrite(uint64_t writeRevision) {
    if (inFlight == writeRevision) inFlight = 0;
}

bool Storage::Wait(Func1<StorageOutcome> settle, StorageWaiter** waiter,
                   bool* immediate, Str* error) {
    ClearError(error);
    if (waiter) *waiter = nullptr;
    if (immediate) *immediate = false;
    if (!IsDirty() && !inFlight) {
        if (immediate) *immediate = true;
        return true;
    }
    if (waiters.len >= kMaxStorageWaiters) {
        StorageError(
            error,
            StrL(
                "localStorage.flush() exceeded the 1024 pending-waiter limit"));
        return false;
    }
    if (!inFlight && failed == revision) failed = 0;
    StorageWaiter* pending = new StorageWaiter();
    pending->revision = revision;
    pending->settle = settle;
    if (!VecAppend(waiters, pending)) {
        delete pending;
        StorageError(error,
                     StrL("allocating a localStorage flush waiter failed"));
        return false;
    }
    if (waiter) *waiter = pending;
    return true;
}

void Storage::CancelWaiter(StorageWaiter* waiter) {
    if (!waiter) return;
    for (int i = 0; i < waiters.len; i++) {
        if (waiters[i] != waiter) continue;
        for (int j = i + 1; j < waiters.len; j++) waiters[j - 1] = waiters[j];
        waiters.len--;
        delete waiter;
        return;
    }
}

bool StoragePersist(const StorageWrite& write, Str* error) {
    ClearError(error);
    if (!write.path) {
        StorageError(error, StrL("localStorage write has no path"));
        return false;
    }
    Str temporary = StrDup(fmt("%s.tmp", write.path));
    FILE* file = temporary.s ? fopen(temporary.s, "wb") : nullptr;
    bool ok = file && fwrite(write.body.s, 1, (size_t)len(write.body), file) ==
                          (size_t)len(write.body);
    if (file && fclose(file) != 0) ok = false;
    if (ok) ok = StorageReplaceFile(temporary, write.path, error);
    if (!ok) {
        if (temporary.s) remove(temporary.s);
        if (!error || !*error) {
            StorageError(error,
                         fmt("cannot write storage file `%s`", write.path));
        }
    }
    StrFree(temporary);
    return ok;
}

} // namespace gpui::shell
