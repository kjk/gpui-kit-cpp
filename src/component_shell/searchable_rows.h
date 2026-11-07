#ifndef GPUI_COMPONENT_SHELL_SEARCHABLE_ROWS_H_
#define GPUI_COMPONENT_SHELL_SEARCHABLE_ROWS_H_

// What delegate_select and delegate_combobox share: a script's row snapshot
// turned into SearchableListItems, the retained copy the host keeps between
// renders, and the arguments both validate the same way.

#include "component_shell/support.h"
#include "ui/searchable_list.h"

namespace gpui::component_shell {

// delegate() / snapshot(): each row requires a string `id` and `label`; an
// optional boolean `disabled` is honoured. The items, and the error, are in
// cx->a; the strings are the rows' own.
bool SearchableItemsFromRows(Ctx* cx, const char* component,
                             const shell::ComponentDataValue* rows, int count,
                             component::SearchableListItem** items, Str* error);

// The rows a retained state was last given, with their strings in `arena`.
struct RetainedSearchableItems {
    Arena* arena = nullptr;
    Vec<component::SearchableListItem> items;

    // Whether `next` holds the same ids, titles and disabled flags.
    bool Same(const component::SearchableListItem* next, int count) const;
    // Takes a copy of `next`, replacing the previous rows.
    void Set(const component::SearchableListItem* next, int count);
    void Free();
};

// The constructor's shape: a non-empty string id, then three callbacks.
bool IsIdAndThreeCallbacks(const ComponentArgument* args, int count);
// `<component>.menu_width(width)`: a positive width an f32 can hold. False
// after build->Fail.
bool MenuWidthArgument(PayloadBuild* build, const char* component,
                       const ComponentArgument* args, int count, float* out);

} // namespace gpui::component_shell

#endif // GPUI_COMPONENT_SHELL_SEARCHABLE_ROWS_H_
