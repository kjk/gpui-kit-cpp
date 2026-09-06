#include "ui/i18n.h"
#include "ui/list.h"
#include "base/list_settings.h"
#include "ui/input.h"
#include "ui/skeleton.h"

namespace gpui {

namespace component {

ListItem* ListItem::New(Ctx* cx, El* child) {
    Arena* a = cx->a;
    ListItem* it = ArenaNew<ListItem>(a);
    it->a = a;
    it->cx = cx;
    it->child = child;
    return it;
}
ListItem* ListItem::Selected(bool v) {
    selected = v;
    return this;
}
ListItem* ListItem::SecondarySelected(bool v) {
    secondarySelected = v;
    return this;
}
ListItem* ListItem::Confirmed(bool v) {
    confirmed = v;
    return this;
}
ListItem* ListItem::Disabled(bool v) {
    disabled = v;
    return this;
}
ListItem* ListItem::Style(const StateStyle& s) {
    style = s;
    return this;
}

El* ListItem::IntoEl(Str id, Listener onClick, Listener onMouseDown) {
    const Theme& th = ThemeNow(cx->app);
    El* row = Div(a)
                  ->Role(AccessibilityRole::ListItem)
                  ->AriaSelected(selected)
                  ->AriaDisabled(disabled)
                  ->FlexRow()
                  ->W(kFill)
                  ->PadX(8)
                  ->PadY(4)
                  ->Gap(8)
                  ->ItemsCenter()
                  ->JustifyBetween()
                  ->Radius(th.radius);
    if (!disabled) {
        row->HoverBg(th.tokens.accent);
    }
    // refine_style(&self.style), here rather than through `ElRefine`: a
    // refinement put on the element lands at layout time and would win over
    // the selection below it, where list_item.rs applies the caller's style
    // first and lets the selection refine it.
    if (style.set) {
        StyleApplyFields(&row->style, style.style, style.set);
    }
    if (selected || secondarySelected) {
        // list_item.rs: the selection takes the active highlight when the
        // setting is on — the list.active tint, ruled with list.active.border
        // — and plain `accent` when it is off. A row a right press marked is
        // outlined rather than filled, so it is not mistaken for the
        // selection.
        ListActiveStyle st =
            ListActiveStyleOf(ListSettingsNow(cx->app), th.tokens.listActive,
                              th.listActiveBorder, th.tokens.accent, selected);
        if (!secondarySelected) {
            row->Bg(st.bg);
        }
        if (st.hasBorder) {
            row->Child(ListActiveOverlay(a, st.border, th.radius));
        } else if (secondarySelected) {
            row->Border(1, th.border);
        }
    }
    if (child) {
        row->Child(child);
    }
    if (confirmed) {
        row->Child(IconEl(a, IconName::Check, 16)->Fg(th.foreground));
    }
    if (!disabled) {
        BindClick(row, id, onClick);
        if (onMouseDown.IsValid()) {
            row->OnMouseDown(onMouseDown);
        }
    }
    return row;
}

List* List::New(Ctx* cx, Str id, Entity<ListState> state) {
    Arena* a = cx->a;
    List* l = ArenaNew<List>(a);
    l->a = a;
    l->cx = cx;
    l->id = id;
    l->state = state;
    return l;
}
List* List::WithDelegate(const ListDelegate& value) {
    delegate = value;
    delegateSet = true;
    return this;
}
List* List::Sections(const int* counts, int n) {
    ListState* s = state.Get(cx);
    if (s) {
        ListSetSections(s, counts, n, delegate.renderSectionHeader != nullptr,
                        delegate.renderSectionFooter != nullptr);
    }
    return this;
}
List* List::Count(int n) {
    ListState* s = state.Get(cx);
    if (s) {
        ListSetCount(s, n);
    }
    return this;
}
List* List::Items(void* d, ListItem* (*fn)(Ctx*, void*, int, int, int)) {
    delegate.data = d;
    delegate.renderItem = fn;
    return this;
}
List* List::Headers(El* (*headerFn)(Ctx*, void*, int),
                    El* (*footerFn)(Ctx*, void*, int)) {
    delegate.renderSectionHeader = headerFn;
    delegate.renderSectionFooter = footerFn;
    // The flattening depends on whether there are headers and footers at all,
    // so a list that says so after its sections says it again.
    ListState* s = state.Get(cx);
    if (s) {
        s->sectionHeaders = delegate.renderSectionHeader != nullptr;
        s->sectionFooters = delegate.renderSectionFooter != nullptr;
    }
    return this;
}
ListItem* ListSeparatorItem(Ctx* cx, El* child) {
    return ListItem::New(cx, child)->Disabled(true);
}

List* List::Searchable(InputState* s, Listener onFocus) {
    search = s;
    onSearchFocus = onFocus;
    return this;
}

List* List::SearchPlaceholder(Str value) {
    searchPlaceholder = value;
    return this;
}

List* List::WithSize(UiSize value) {
    size = value;
    return this;
}

List* List::ScrollbarVisible(bool value) {
    scrollbarVisible = value;
    return this;
}

List* List::Padding(float value) {
    padding = value < 0 ? 0 : value;
    return this;
}

List* List::Loading(El* e) {
    loading = e;
    return this;
}

List* List::Initial(El* e) {
    initial = e;
    return this;
}
List* List::Empty(El* e) {
    empty = e;
    return this;
}
List* List::H(float px) {
    h = px;
    return this;
}

// list/loading.rs: three placeholder rows, each a wide bar over a narrower
// secondary one. Rust builds them out of ListItem so they carry a row's own
// padding; the shape is the same either way.
El* ListLoadingView(Ctx* cx, float h) {
    Arena* a = cx->a;
    El* body = Div(a)->FlexCol()->W(kFill)->PadY(10)->Gap(12);
    if (h > 0) {
        body->H(h);
    }
    for (int i = 0; i < 3; i++) {
        body->Child(
            Div(a)
                ->FlexCol()
                ->W(kFill)
                ->PadX(8)
                ->Gap(6)
                // max_w_full: the bars keep their own widths and a
                // list narrower than they are clips them rather than
                // letting them hang over its edge.
                ->ItemsStart()
                ->ClipX()
                ->Child(Skeleton::New(cx)->W(192)->H(20)->IntoEl())
                ->Child(
                    Skeleton::New(cx)->Secondary()->W(256)->H(12)->IntoEl()));
    }
    return body;
}

// render_empty: an Inbox icon in muted_foreground at 60%, centred in what the
// list would have filled.
static El* DefaultEmpty(Ctx* cx, float h) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    return Div(a)
        ->FlexCol()
        ->W(kFill)
        ->H(h)
        ->ItemsCenter()
        ->JustifyCenter()
        ->Child(IconEl(a, IconName::Inbox, 48)
                    ->Fg(RgbaOpacity(th.mutedFg, 0.6f)));
}

El* List::IntoEl() {
    const Theme& th = ThemeNow(cx->app);
    ListState* s = state.Get(cx);
    if (s) {
        s->self = state;
        s->queryInput = search;
        s->onPerformSearch = delegate.performSearch;
        s->onSetSelectedIndex = delegate.setSelectedIndex;
        s->onSetRightClickedIndex = delegate.setRightClickedIndex;
        s->onConfirm = delegate.confirm;
        s->onCancel = delegate.cancel;
        if (delegateSet) {
            s->onLoadMore = delegate.loadMore;
        }
        if (delegateSet) {
            int sections = delegate.sectionsCount
                               ? delegate.sectionsCount(cx, delegate.data)
                               : 1;
            if (sections < 1) {
                sections = 1;
            }
            if (delegate.itemsCount) {
                VecClear(s->sectionCounts);
                s->count = 0;
                for (int section = 0; section < sections; section++) {
                    int n = delegate.itemsCount(cx, delegate.data, section);
                    if (n < 0) {
                        n = 0;
                    }
                    VecAppend(s->sectionCounts, n);
                    s->count += n;
                }
                s->sectionHeaders = delegate.renderSectionHeader != nullptr;
                s->sectionFooters = delegate.renderSectionFooter != nullptr;
            }
            s->loading = delegate.isLoading
                             ? delegate.isLoading(cx, delegate.data)
                             : false;
            s->hasMore =
                delegate.hasMore ? delegate.hasMore(cx, delegate.data) : false;
            s->loadMoreThreshold = delegate.loadMoreThreshold
                                       ? delegate
                                             .loadMoreThreshold(delegate.data)
                                       : 20;
            if (s->loadMoreThreshold < 0) {
                s->loadMoreThreshold = 0;
            }
        }
    }

    // Two elements, because Rust has two views. `List::render` is
    // `div().id("list").role(List).refine_style(&self.style).child(state)`,
    // and the state's own render is the `v_flex().id("list-state")` that
    // declares the key context, tracks the focus and holds the query row and
    // the rows. The outer one is what the caller styles: the p_8, the border
    // and the radius here are the story's.
    El* root = Div(a)
                   ->PathClick(id)
                   ->Role(AccessibilityRole::List)
                   ->FlexCol()
                   ->W(kFill);
    // `v_flex().size_full().relative().overflow_hidden()`: no gap between the
    // query row and the rows under it — the row's own bottom border is what
    // separates them.
    El* inner = Div(a)->FlexCol()->W(kFill);
    root->Child(inner);
    if (search) {
        // list.rs: `div().px_2().border_b_1().child(Input::new(..)
        // .prefix(Icon::new(Search)).cleanable(true).p_0().appearance(false))`
        // — the magnifier is the field's own prefix, so the gap between it
        // and the text is the input's, not a row's.
        El* searchRow =
            Div(a)->FlexRow()->W(kFill)->H(32)->ItemsCenter()->BorderB(
                1, th.border);
        // InputState::new(..).placeholder(t!("List.search_placeholder")),
        // which is "Search..." in the locale this tree ships. Rust sets it on
        // the state when the list makes it, so a caller that gave a field of
        // its own with a placeholder already on it keeps that one.
        InputSetPlaceholder(search, searchPlaceholder.s
                                        ? searchPlaceholder
                                        : Tr("List.search_placeholder"));
        search->onChange = ListenTo(state, &ListState::OnQueryInput);
        searchRow->Child(Div(a)->Flex1()->Child(
            Input::New(cx, StrL("search"), search)
                ->WithSize(size)
                ->Appearance(false)
                ->Cleanable(true)
                ->Prefix(IconEl(a, IconName::Search, 16)->Fg(th.mutedFg))
                ->OnFocus(onSearchFocus)
                ->IntoEl()));
        inner->Child(searchRow);
    }
    if (!s) {
        return root;
    }
    // The height the list was laid out at, which is what scroll_to_item and
    // the visible range are worked out against.
    s->viewportH = h;

    if (s->loading) {
        El* loadingView = delegate.renderLoading
                              ? delegate.renderLoading(cx, delegate.data)
                              : loading;
        inner->Child(loadingView ? loadingView : ListLoadingView(cx, h));
        return root;
    }
    // render_initial: what the list shows before anything has been searched
    // for. Rust asks for it only while the query field is empty, which is the
    // one place it could be reached from.
    if (search && search->text.len == 0) {
        El* initialView = delegate.renderInitial
                              ? delegate.renderInitial(cx, delegate.data)
                              : initial;
        if (initialView) {
            inner->Child(initialView);
            return root;
        }
    }
    if (s->count == 0) {
        El* emptyView = delegate.renderEmpty
                            ? delegate.renderEmpty(cx, delegate.data)
                            : empty;
        inner->Child(emptyView ? emptyView : DefaultEmpty(cx, h));
        return root;
    }

    // prepare_items_if_needed: the item that stands for the rest, and a
    // section header and footer, each laid out on its own to see what it
    // wants to be. Rust measures at MinContent on both axes and so does
    // `MeasureEl`; the pass runs on the frame arena and the elements are
    // thrown away, since what is wanted is the number.
    //
    // A row that measures nothing keeps whatever the state had — a delegate
    // that answers null for the row it was asked to measure should not
    // collapse the list to zero-height rows.
    float itemH = s->rowH;
    // The row measured is chosen in this order: the configured index if both
    // its section and row exist; otherwise the first row in the first
    // non-empty section, which is what the flattened entry 0 is; and with
    // every section empty no item is measured at all. The fallback is only
    // for this measurement — `itemToMeasure` is the caller's setting and
    // stays put, so once the configured row exists again it is measured.
    int measureEntry = ListEntryOf(s, s->itemToMeasure);
    if (measureEntry < 0 && s->count > 0) {
        measureEntry = 0;
    }
    if (delegate.renderItem && measureEntry >= 0) {
        ListRow m = ListRowAt(s, ListRowOfEntry(s, measureEntry));
        ListItem* probe =
            delegate.renderItem(cx, delegate.data, m.section, m.row, m.entry);
        if (probe) {
            float got = MeasureEl(cx->win ? &cx->win->paint : nullptr,
                                  probe->IntoEl(StrL("list-measure"), {}, {}))
                            .h;
            if (got > 0) {
                itemH = got;
            }
        }
    }
    float headerH = s->sectionHeaders ? s->headerH : 0;
    if (s->sectionHeaders && delegate.renderSectionHeader) {
        float got =
            MeasureEl(cx->win ? &cx->win->paint : nullptr,
                      delegate.renderSectionHeader(cx, delegate.data, 0))
                .h;
        if (got > 0) {
            headerH = got;
        }
    }
    float footerH = s->sectionFooters ? s->footerH : 0;
    if (s->sectionFooters && delegate.renderSectionFooter) {
        float got =
            MeasureEl(cx->win ? &cx->win->paint : nullptr,
                      delegate.renderSectionFooter(cx, delegate.data, 0))
                .h;
        if (got > 0) {
            footerH = got;
        }
    }
    ListPrepareRowHeights(s, itemH, headerH, footerH);

    int total = ListRowCount(s);
    const float* sizes = ListRowHeights(s);
    VirtualRange range =
        sizes ? VirtualListVisibleRange(sizes, total, s->scrollY, h)
              : VirtualListVisibleRows(total, s->rowH, s->scrollY, h);
    El* body = Div(a)
                   ->Id(StrL("body"))
                   ->FlexCol()
                   ->W(kFill)
                   ->H(h)
                   ->Pad(padding)
                   ->ClipY()
                   ->ScrollY(s->scrollY)
                   ->ScrollFromPath()
                   ->OnScroll(ListenTo(state, &ListState::OnScroll));
    if (!scrollbarVisible) {
        body->HideScrollbar();
    }
    // The two spacers stand in for the rows that were not built. With a size
    // per row they are the running scan `VirtualListItemOrigin` does, not a
    // count times one height.
    if (range.first > 0) {
        float before = sizes ? VirtualListItemOrigin(sizes, total, range.first)
                             : (float)range.first * s->rowH;
        body->Child(Div(a)->W(kFill)->Shrink0()->H(before));
    }
    Listener click = ListenTo(state, &ListState::OnRowClick, 0);
    Listener down = ListenTo(state, &ListState::OnRowMouseDown, 0);
    for (int r = range.first; r < range.end; r++) {
        ListRow row = ListRowAt(s, r);
        El* el = nullptr;
        if (row.kind == ListRowKind::SectionHeader) {
            el = delegate.renderSectionHeader
                     ? delegate
                           .renderSectionHeader(cx, delegate.data, row.section)
                     : nullptr;
        } else if (row.kind == ListRowKind::SectionFooter) {
            el = delegate.renderSectionFooter
                     ? delegate
                           .renderSectionFooter(cx, delegate.data, row.section)
                     : nullptr;
        } else if (delegate.renderItem) {
            ListItem* it = delegate.renderItem(cx, delegate.data, row.section,
                                               row.row, row.entry);
            if (it) {
                it->selected = s->selectable && s->selected == row.entry;
                it->secondarySelected = s->rightClicked == row.entry;
                // Each row names the state and carries its own index, which
                // is what Rust's per-row closure captures. The name is the
                // row's IndexPath, the way `impl From<IndexPath> for
                // ElementId` spells it, so it stays the same when a section
                // above it grows or shrinks and the flat index shifts.
                el = it->IntoEl(StrDup(a, IndexPathIdStr(a, row.Path())),
                                ListenerArg(click, row.entry),
                                ListenerArg(down, row.entry));
                el->AriaPositionInSet(row.row + 1)->AriaSizeOfSet(s->count);
            }
        }
        // Each row takes the height it was measured at, which is what lets
        // the two spacers stand in for the rest. Shrink0 because the rows are
        // taller than the box they scroll in: a flex column shrinks what
        // overflows it, and a row squeezed to fit is a row the visible range
        // -- worked out against these same heights -- no longer measures,
        // which left a band of empty list under the last one.
        float slotH = sizes && r < total ? sizes[r] : s->rowH;
        El* slot = Div(a)->FlexCol()->W(kFill)->Shrink0()->H(slotH);
        if (el) {
            slot->Child(el);
        }
        body->Child(slot);
    }
    if (range.end < total) {
        float after = sizes ? VirtualListContentSize(sizes, total) -
                                  VirtualListItemOrigin(sizes, total, range.end)
                            : (float)(total - range.end) * s->rowH;
        body->Child(Div(a)->W(kFill)->Shrink0()->H(after));
    }
    inner->Child(body);

    // load_more: coming within the threshold of the end asks the caller for
    // more rows. Rust runs it as a background task; here it is a listener the
    // caller answers on the next frame.
    if (ListShouldLoadMore(s, range.end) && s->onLoadMore.IsValid()) {
        ListRequestLoadMore(s, cx);
    }
    // list.rs declares the "List" context on the element it tracks focus on
    // — `list-state`, not `list` — and both are hit targets, which is what
    // puts them on the chain a press walks, so a press on a row is a press on
    // the list and the list is what takes the focus.
    // list.rs `self.focus_handle(cx).focus(window, cx)` on a row press.
    if (!s->focus.IsValid()) {
        s->focus = FocusHandleNew(cx);
    }
    inner->PathClick(StrL("list-state"))
        ->TrackFocus(s->focus)
        ->FocusRing(false)
        ->FocusOnPress();
    if (s->rightClicked >= 0) {
        inner->OnMouseDownOut(ListenTo(state, &ListState::OnMouseDownOut));
    }
    ListBindKeys(cx, inner, state);
    return root;
}

} // namespace component
} // namespace gpui
