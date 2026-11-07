#include "component_shell/searchable_rows.h"

namespace gpui::component_shell {

using component::SearchableListItem;

bool SearchableItemsFromRows(Ctx* cx, const char* component,
                             const shell::ComponentDataValue* rows, int count,
                             SearchableListItem** items, Str* error) {
    SearchableListItem* out =
        count ? (SearchableListItem*)Alloc(
                    cx->a, (int)sizeof(SearchableListItem) * count)
              : nullptr;
    for (int i = 0; i < count; i++) {
        const shell::ComponentDataValue* id = rows[i].Get(StrL("id"));
        if (!id || id->kind != shell::DataKind::String) {
            *error = StrDup(cx->a, fmt("%s row %d requires a string `id`",
                                       Str(component), i));
            return false;
        }
        const shell::ComponentDataValue* label = rows[i].Get(StrL("label"));
        if (!label || label->kind != shell::DataKind::String) {
            *error = StrDup(cx->a, fmt("%s row %d requires a string `label`",
                                       Str(component), i));
            return false;
        }
        const shell::ComponentDataValue* disabled = rows[i]
                                                        .Get(StrL("disabled"));
        out[i] = SearchableListItem{};
        out[i].value = id->string;
        out[i].title = label->string;
        out[i].disabled = disabled &&
                          disabled->kind == shell::DataKind::Boolean &&
                          disabled->boolean;
    }
    *items = out;
    return true;
}

bool RetainedSearchableItems::Same(const SearchableListItem* next,
                                   int count) const {
    if (len(items) != count) return false;
    for (int i = 0; i < count; i++) {
        if (!StrEq(items[i].value, next[i].value) ||
            !StrEq(items[i].title, next[i].title) ||
            items[i].disabled != next[i].disabled)
            return false;
    }
    return true;
}

void RetainedSearchableItems::Set(const SearchableListItem* next, int count) {
    Arena* fresh = ArenaNew();
    VecClear(items);
    for (int i = 0; i < count; i++) {
        SearchableListItem item = next[i];
        item.value = StrDup(fresh, next[i].value);
        item.title = StrDup(fresh, next[i].title);
        VecAppend(items, item);
    }
    if (arena) ArenaDelete(arena);
    arena = fresh;
}

void RetainedSearchableItems::Free() {
    VecReset(items);
    if (arena) ArenaDelete(arena);
    arena = nullptr;
}

} // namespace gpui::component_shell
