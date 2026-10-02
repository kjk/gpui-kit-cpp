#ifndef GPUI_UI_LIST_H_
#define GPUI_UI_LIST_H_
/* Themed list — crates/ui/src/list */

#include "ui/sizing.h"
#include "base/list.h" // behavior shared while ui/list extraction completes

namespace gpui {

namespace component {

// One row. Rust's ListItem is what a delegate's render_item hands back: the
// content is the caller's and the selected / confirmed styling is the list's.
// list/loading.rs: the skeleton view a loading list shows, which is what
// `render_loading` returns when a delegate does not replace it.
El* ListLoadingView(Ctx* cx, float h = 0);

struct ListItem {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    El* child = nullptr;
    bool selected = false;
    bool secondarySelected = false;
    bool confirmed = false;
    bool disabled = false;
    // check_icon: the mark a confirmed row shows, in a slot every row given
    // one keeps so their content lines up. None shows no mark at all.
    IconName checkIcon = IconName::None;
    // `impl Styled for ListItem`, landed by `refine_style(&self.style)`. It
    // goes on before the selection does, which is where list_item.rs puts it:
    // a selected row's own fill and ring still win over what a caller
    // chained on.
    StateStyle style = {};
    // accessibility_label: without it a row reaches assistive technology
    // with no name -- its visible children do not become the item's name.
    Str accessibilityLabel = {};

    static ListItem* New(Ctx* cx, El* child);
    ListItem* Selected(bool v);
    ListItem* SecondarySelected(bool v);
    ListItem* Confirmed(bool v);
    ListItem* CheckIcon(IconName icon);
    ListItem* Disabled(bool v);
    ListItem* Style(const StateStyle& s);
    ListItem* AccessibilityLabel(Str label);
    El* IntoEl(Str id, Listener onClick, Listener onMouseDown);
};

// list/separator_item.rs: a row that is a rule rather than an item. It is a
// disabled ListItem in Rust, so it is never selected and never answers a
// click; the caller's children are what it draws.
ListItem* ListSeparatorItem(Ctx* cx, El* child = nullptr);

// crates/ui/src/list/delegate.rs. Rust expresses this as a generic trait;
// C++ keeps the render half as a POD function table and the calls that must
// survive the frame as generational Listeners. Null entries have the source
// defaults: one section, no header/footer/initial view, the built-in empty and
// loading views, not loading, no more rows, threshold 20, and no-op lifecycle
// hooks. `itemsCount` and `renderItem` are the two required trait methods.
struct ListDelegate {
    void* data = nullptr;
    int (*sectionsCount)(Ctx* cx, void* data) = nullptr;
    int (*itemsCount)(Ctx* cx, void* data, int section) = nullptr;
    ListItem* (*renderItem)(Ctx* cx, void* data, int section, int row,
                            int entry) = nullptr;
    El* (*renderSectionHeader)(Ctx* cx, void* data, int section) = nullptr;
    El* (*renderSectionFooter)(Ctx* cx, void* data, int section) = nullptr;
    El* (*renderEmpty)(Ctx* cx, void* data) = nullptr;
    El* (*renderInitial)(Ctx* cx, void* data) = nullptr;
    bool (*isLoading)(Ctx* cx, void* data) = nullptr;
    El* (*renderLoading)(Ctx* cx, void* data) = nullptr;
    bool (*hasMore)(Ctx* cx, void* data) = nullptr;
    int (*loadMoreThreshold)(void* data) = nullptr;

    Listener performSearch = {};
    Listener setSelectedIndex = {};
    Listener setRightClickedIndex = {};
    Listener confirm = {};
    Listener cancel = {};
    Listener loadMore = {};
};

// The list, bound to the ListState that answers its keys and clicks the way
// an Input is bound to an InputState. The rows come from the caller one at a
// time rather than all at once: only the ones the viewport can show are ever
// built, which is what `ListDelegate::render_item` over a virtual list is.
struct List {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Str id = {};
    Entity<ListState> state = {};
    ListDelegate delegate = {};
    // True only for the complete function-table path. The older Items /
    // Headers builders remain source-compatible and leave state-owned loading
    // policy alone.
    bool delegateSet = false;
    // The search field, when the list is searchable.
    InputState* search = nullptr;
    Listener onSearchFocus;
    // render_loading / render_initial: what the list shows instead of its
    // rows while the delegate is loading, and before anything has been
    // searched for. Null takes the built-in skeleton view for the first and
    // nothing at all for the second, which are Rust's defaults.
    El* loading = nullptr;
    El* initial = nullptr;
    // render_empty: what to show when there is nothing in the list. Null
    // takes Rust's own — a muted Inbox icon.
    El* empty = nullptr;
    Str searchPlaceholder = {};
    UiSize size = UiSize::Medium;
    bool scrollbarVisible = true;
    // Styled padding is extracted from the outer List and passed to the
    // virtual rows upstream, so it does not inset the search field.
    float padding = 0;
    // A fixed height for the rows' viewport. 0, the default, fills the box
    // the list is given, as Rust's `size_full()` list does: the rows are
    // bound at prepaint from the viewport's bounds, and what is worked out
    // before layout (scroll_to_item, load_more) uses the height the list was
    // laid out at last frame. A fixed height is also the first frame's.
    float h = 0;

    static List* New(Ctx* cx, Str id, Entity<ListState> state);
    List* WithDelegate(const ListDelegate& value);
    // The sections and their item counts, which the state flattens.
    List* Sections(const int* counts, int n);
    List* Count(int n);
    List* Items(void* data,
                ListItem* (*fn)(Ctx*, void*, int section, int row, int entry));
    List* Headers(El* (*headerFn)(Ctx*, void*, int),
                  El* (*footerFn)(Ctx*, void*, int) = nullptr);
    List* Searchable(InputState* search, Listener onFocus);
    List* SearchPlaceholder(Str value);
    List* WithSize(UiSize value);
    List* ScrollbarVisible(bool value);
    List* Padding(float value);
    List* Loading(El* e);
    List* Initial(El* e);
    List* Empty(El* e);
    List* H(float px);
    El* IntoEl();
};

} // namespace component
} // namespace gpui
#endif // GPUI_UI_LIST_H_
