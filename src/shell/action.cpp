#include "shell/action.h"

namespace gpui::shell {

namespace {

// One table for both directions and for the raw text a binding keeps. The
// arena is never reset: every entry is something the keymap or an action
// listener may still name, and the set is bounded by the distinct names a run
// produces rather than by how often they are used.
struct ActionNames {
    Arena* arena = ArenaNew();
    Mutex mutex;
    Vec<uint32_t> ids;
    Vec<Str> scriptIds;
    Vec<Str> texts;

    ~ActionNames() {
        VecReset(ids);
        VecReset(scriptIds);
        VecReset(texts);
        ArenaDelete(arena);
    }
};

ActionNames& ActionTable() {
    static ActionNames table;
    return table;
}

} // namespace

uint32_t ShellActionOf(Str id) {
    if (!id || len(id) <= 0) {
        return 0;
    }
    StrBuilder qualified;
    qualified.Append(StrL("shell::"));
    qualified.Append(id);
    Str name = qualified.TakeStr();
    uint32_t action = ActionOf(name);
    StrFree(name);

    ActionNames& table = ActionTable();
    table.mutex.Lock();
    for (int i = 0; i < table.ids.len; i++) {
        if (table.ids[i] == action) {
            table.mutex.Unlock();
            return action;
        }
    }
    VecAppend(table.ids, action);
    VecAppend(table.scriptIds, StrDup(table.arena, id));
    table.mutex.Unlock();
    return action;
}

Str ShellActionScriptId(uint32_t action) {
    if (!action) {
        return {};
    }
    ActionNames& table = ActionTable();
    table.mutex.Lock();
    Str found = {};
    for (int i = 0; i < table.ids.len; i++) {
        if (table.ids[i] == action) {
            found = table.scriptIds[i];
            break;
        }
    }
    table.mutex.Unlock();
    return found;
}

const char* ShellActionInternText(Str value) {
    if (!value) {
        return nullptr;
    }
    ActionNames& table = ActionTable();
    table.mutex.Lock();
    for (int i = 0; i < table.texts.len; i++) {
        if (StrEq(table.texts[i], value)) {
            const char* found = table.texts[i].s;
            table.mutex.Unlock();
            return found;
        }
    }
    // StrDup into an arena writes the terminating zero, which is what makes
    // the result usable as the `const char*` a KeyBinding holds.
    Str interned = StrDup(table.arena, value);
    VecAppend(table.texts, interned);
    table.mutex.Unlock();
    return interned.s;
}

} // namespace gpui::shell
