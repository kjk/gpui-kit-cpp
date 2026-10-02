#ifndef GPUI_UI_PAGINATION_H_
#define GPUI_UI_PAGINATION_H_
/* Themed pagination — crates/ui/src/pagination.rs */

#include "ui/sizing.h"

namespace gpui {

namespace component {

// What an ellipsis opens onto is the pages it stands for, and a menu reports
// the row that was taken rather than the page. The row index is only a page
// once the range's first page is beside it, which is what this keyed state
// carries between the frame that built the menu and the click that runs.
struct PaginationMenuState {
    int64_t firstPage = 1;
    Listener onChange = {};

    static void OnItem(PaginationMenuState* self, Ctx* cx, const ClickEvent* ev,
                       int64_t ix);
};

// pagination.rs MAX_ELLIPSIS_MENU_PAGES: the most pages an ellipsis menu
// lists. The menu renders every item on each frame it is open, so listing
// every hidden page of a large set makes each hover repaint thousands of rows.
constexpr int kMaxEllipsisMenuPages = 100;

// ellipsis_menu_pages: the pages an ellipsis menu lists — the hidden pages
// [hiddenStart, hiddenEnd) closest to the current page, at most
// kMaxEllipsisMenuPages of them — as [*start, *end).
void PaginationEllipsisMenuPages(int64_t hiddenStart, int64_t hiddenEnd,
                                 int64_t currentPage, int64_t* start,
                                 int64_t* end);

struct Pagination {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Str id = {};
    // Rust's usize pages, in 64 bits. A page reaches onChange as the
    // listener's argument, which is as wide as a pointer.
    int64_t page = 1;
    int64_t total = 1;
    // How many page buttons stay visible before the list collapses to
    // ellipses; crates/base clamps this to 5.
    int64_t visiblePages = 5;
    bool compact = false;
    bool disabled = false;
    UiSize size = UiSize::Medium;
    Listener onChange;

    static Pagination* New(Ctx* cx, int64_t page, int64_t total);
    Pagination* Id(Str s);
    // current_page: at least 1.
    Pagination* CurrentPage(int64_t page);
    // total_pages: at least 1, and the current page no further than it.
    Pagination* TotalPages(int64_t pages);
    Pagination* VisiblePages(int64_t n);
    Pagination* Compact(bool v = true);
    Pagination* Disabled(bool v);
    Pagination* WithSize(UiSize s);
    Pagination* OnChange(Listener fn);
    El* IntoEl();
};

} // namespace component
} // namespace gpui
#endif // GPUI_UI_PAGINATION_H_
