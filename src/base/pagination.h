#ifndef GPUI_BASE_PAGINATION_H_
#define GPUI_BASE_PAGINATION_H_
/* Unstyled pagination — crates/base/src/pagination.rs */

#include "gpui/gpui.h"

namespace gpui {

// Rust's PaginationItem: a destination is either a page or the range an
// ellipsis stands for. `page` is 0 for the ellipsis, and `from`..`to` is then
// the inclusive span it covers — Rust's half-open Range<usize> written the way
// this tree counts.
struct PaginationItem {
    int64_t page = 0;
    int64_t from = 0;
    int64_t to = 0;
};

// The controlled behavior every part of a pagination control shares. Rust
// keeps the guards here rather than in each button, so the same bounds,
// disabled and same-page checks apply wherever a page is requested.
// Pages are Rust's usize, counted in 64 bits.
struct PaginationState {
    int64_t currentPage = 1;
    int64_t totalPages = 1;
    int64_t visiblePages = 5;
    bool disabled = false;
};

// Clamps the pair the way PaginationState::new does: at least one page, and a
// current page inside it.
PaginationState PaginationStateNew(int64_t currentPage, int64_t totalPages);

// previous_page / next_page: 0 where Rust answers None, which a disabled
// control and either end both do.
int64_t PaginationPrevPage(const PaginationState* s);
int64_t PaginationNextPage(const PaginationState* s);

// request_page's guards, without the call: disabled, the page it is already
// on, and anything outside 1..=totalPages are all refused. A caller attaches
// its handler only where this says yes.
bool PaginationCanRequest(const PaginationState* s, int64_t page);

// items(): the first and last page always show, with a window around the
// current one and an ellipsis for each gap. Returns how many were written.
int PaginationItems(const PaginationState* s, PaginationItem* out, int cap);

// The navigation landmark. Identity and nothing else; every destination in it
// is the caller's own element.
struct Pagination {
    static El* New(Ctx* cx, Str id);
};
} // namespace gpui
#endif // GPUI_BASE_PAGINATION_H_
