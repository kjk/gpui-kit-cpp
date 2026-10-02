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
ListItem* ListItem::CheckIcon(IconName icon) {
    checkIcon = icon;
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

ListItem* ListItem::AccessibilityLabel(Str label) {
    accessibilityLabel = label;
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
                  ->PadX(Rems(cx, 0.75f)) // px_3
                  ->PadY(Rems(cx, 0.25f)) // py_1
                  ->Gap(Rems(cx, 0.25f))  // gap_x_1
                  ->Font(16)              // text_base
                  ->Fg(disabled ? th.mutedFg : th.foreground)
                  ->ItemsCenter()
                  ->JustifyBetween()
                  ->Radius(th.radius);
    // list_item.rs registers `hover` for every selectable item and leaves
    // the style empty while the item is active (confirmed, selected or right
    // clicked): an active item takes no hover background.
    bool isActive = confirmed || selected || secondarySelected;
    if (!disabled && !isActive) {
        row->HoverBg(th.tokens.listHover);
    }
    // refine_style(&self.style), here rather than through `ElRefine`: a
    // refinement put on the element lands at layout time and would win over
    // the selection below it, where list_item.rs applies the caller's style
    // first and lets the selection refine it.
    if (style.set) {
        StyleApplyFields(&row->style, style.style, style.set);
    }
    if (accessibilityLabel.s) {
        row->AriaLabel(accessibilityLabel);
    }
    if (!disabled && selected) {
        // list_item.rs: the selection takes the active highlight when the
        // setting is on — the list.active tint — and plain `accent` when it
        // is off.
        ListActiveStyle st =
            ListActiveStyleOf(ListSettingsNow(cx->app), th.tokens.listActive,
                              th.listActiveBorder, th.tokens.accent, true);
        row->Bg(st.bg);
    }
    // `h_flex().w_full().justify_between().gap_x_1()`: the children take
    // the row, beside the check slot when the item has a check icon --
    // `div().w_5()`, holding the small muted mark once it is confirmed.
    El* content =
        Div(a)->FlexRow()->W(kFill)->ItemsCenter()->JustifyBetween()->Gap(
            Rems(cx, 0.25f));
    El* children = Div(a)->W(kFill);
    if (child) {
        children->Child(child);
    }
    content->Child(children);
    if (checkIcon != IconName::None) {
        El* slot = Div(a)
                       ->FlexRow()
                       ->W(Rems(cx, 1.25f))
                       ->ItemsCenter()
                       ->JustifyCenter()
                       ->Shrink0();
        if (confirmed) {
            slot->Child(IconEl(a, checkIcon, UiIconPx(cx, UiSize::Small))
                            ->Fg(th.mutedFg));
        }
        content->Child(slot);
    }
    row->Child(content);
    if (!disabled && secondarySelected) {
        // list_item.rs: a right-clicked item is outlined in `selection`, the
        // token Table uses for its right-clicked row, on top of whatever
        // background it has. The outline is an absolute child, so it repeats
        // the item's own radius.
        row->Child(ListActiveOverlay(a, th.selection, row->style.radius));
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
// The query row, in rems: the field with appearance(false), the Medium
// input's h_8, and its bottom border. And the rows a list that fills its box
// builds with before it has been laid out, the height a List had before it
// filled.
static const float kListSearchRowRems = 2;
static const float kListDefaultH = 320;

List* List::H(float px) {
    h = px;
    return this;
}

// list/loading.rs: three placeholder rows, each a wide bar over a narrower
// secondary one. Rust builds them out of ListItem so they carry a row's own
// padding; the shape is the same either way.
El* ListLoadingView(Ctx* cx, float h) {
    Arena* a = cx->a;
    // py_2p5 gap_3 around the rows; gap_1p5, h_5 w_48 and h_3 w_64 in them.
    El* body = Div(a)
                   ->FlexCol()
                   ->W(kFill)
                   ->PadY(Rems(cx, 0.625f))
                   ->Gap(Rems(cx, 0.75f));
    if (h > 0) {
        body->H(h);
    }
    for (int i = 0; i < 3; i++) {
        body->Child(Div(a)
                        ->FlexCol()
                        ->W(kFill)
                        // The disabled ListItem around it: px_3 py_1.
                        ->PadX(Rems(cx, 0.75f))
                        ->PadY(Rems(cx, 0.25f))
                        ->Gap(Rems(cx, 0.375f))
                        // max_w_full: the bars keep their own widths and a
                        // list narrower than they are clips them rather than
                        // letting them hang over its edge.
                        ->ItemsStart()
                        ->ClipX()
                        ->Child(Skeleton::New(cx)
                                    ->W(Rems(cx, 12.f))
                                    ->H(Rems(cx, 1.25f))
                                    ->IntoEl())
                        ->Child(Skeleton::New(cx)
                                    ->Secondary()
                                    ->W(Rems(cx, 16.f))
                                    ->H(Rems(cx, 0.75f))
                                    ->IntoEl()));
    }
    return body;
}

// render_empty: an Inbox icon in muted_foreground at 60%, centred in what the
// list would have filled.
// render_empty is `size_full()`: a fixed list's rows area, or what a list
// that fills its box leaves under the query row (`h` 0).
static El* DefaultEmpty(Ctx* cx, float h) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    El* box = Div(a)->FlexCol()->W(kFill);
    if (h > 0) {
        box->H(h);
    } else {
        box->Flex1()->MinH(0);
    }
    return box->ItemsCenter()
        ->JustifyCenter()
        ->Child(IconEl(a, IconName::Inbox, Rems(cx, 3.f)) // size_12
                    ->Fg(RgbaOpacity(th.mutedFg, 0.6f)));
}

// The list as its box is laid out: the viewport scroll_to_item measures
// against and load_more looks past, taken from the bounds layout gave it.
// The rows themselves are bound at prepaint by the VirtualList body.
static bool ListViewportAt(void* data, Ctx* cx, El*, float height) {
    auto* s = (ListState*)data;
    s->viewportH = height;
    int total = ListRowCount(s);
    const float* sizes = ListRowHeights(s);
    VirtualRange range =
        sizes ? VirtualListVisibleRange(sizes, total, s->scrollY, height)
              : VirtualListVisibleRows(total, s->rowH, s->scrollY, height);
    if (!s->loading && s->count > 0 && ListShouldLoadMore(s, range.end) &&
        s->onLoadMore.IsValid()) {
        ListRequestLoadMore(s, cx);
    }
    return false;
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
    // `div().id("list").refine_style(&self.style).child(state)`, and the
    // state's own render is the `v_flex().id("list-state")` that declares the
    // key context, tracks the focus, carries the List role and holds the query
    // row and the rows. The outer one is what the caller styles: the p_8, the
    // border and the radius here are the story's.
    El* root = Div(a)->PathClick(id)->FlexCol()->W(kFill);
    // `v_flex().size_full().relative().overflow_hidden()`: no gap between the
    // query row and the rows under it — the row's own bottom border is what
    // separates them.
    El* inner = Div(a)->FlexCol()->W(kFill);
    root->Child(inner);
    // A list without a fixed height is `size_full()`, and its rows take
    // what the query row leaves.
    bool fill = h <= 0;
    if (fill) {
        // min_h_0 stands in for Rust's `overflow_hidden()` on the state's
        // v_flex, which is what lets a size_full list that a caller made
        // flex_1 shrink to what its siblings leave: a clipping box has no
        // content-based minimum. The rows clip in the body as it is.
        root->H(kFill)->MinH(0);
        inner->Flex1()->MinH(0);
    }
    // The viewport worked out before layout: what the list's box held last
    // frame less the query row and the rows' padding (UseLaidOutHeight), and
    // on the first frame the fixed height or 320. A box laid out at another
    // height corrects it at prepaint (ListViewportAt), and the rows are bound
    // there by the VirtualList body from the bounds it was given.
    float searchH = search ? Rems(cx, kListSearchRowRems) : 0;
    float first = h > 0 ? h : kListDefaultH;
    LaidOutHeight* laid = UseLaidOutHeight(cx, id, first);
    if (laid) {
        laid->contentBox = true;
        laid->inset = searchH + padding * 2;
    }
    float viewH = laid ? laid->built : first;
    if (s) {
        TrackLaidOutHeight(cx, root, laid, &ListViewportAt, s);
    } else {
        TrackLaidOutHeight(cx, root, laid);
    }
    if (search) {
        // list.rs: `div().px_2().border_b_1().child(Input::new(..)
        // .prefix(Icon::new(Search)).cleanable(true).p_0().appearance(false))`
        // — the magnifier is the field's own prefix, so the gap between it
        // and the text is the input's, not a row's.
        El* searchRow = Div(a)
                            ->FlexRow()
                            ->W(kFill)
                            ->H(searchH)
                            ->Shrink0()
                            ->ItemsCenter()
                            ->BorderB(1, th.border);
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
                ->IntoEl()
                // `.p_0()` on the Input.
                ->Pad(0)));
        inner->Child(searchRow);
    }
    if (!s) {
        return root;
    }
    // The height the list was laid out at, which is what scroll_to_item and
    // the visible range are worked out against.
    s->viewportH = viewH;

    if (s->loading) {
        El* loadingView = delegate.renderLoading
                              ? delegate.renderLoading(cx, delegate.data)
                              : loading;
        inner->Child(loadingView ? loadingView
                                 : ListLoadingView(cx, fill ? 0 : viewH));
        return root;
    }
    // render_initial: what the list shows before anything has been searched
    // for. Rust asks for it only while the query field is empty, which is the
    // one place it could be reached from.
    if (search && len(search->text) == 0) {
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
        inner
            ->Child(emptyView ? emptyView : DefaultEmpty(cx, fill ? 0 : viewH));
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
        sizes ? VirtualListVisibleRange(sizes, total, s->scrollY, viewH)
              : VirtualListVisibleRows(total, s->rowH, s->scrollY, viewH);
    struct ListVirtualUser {
        ListState* s = nullptr;
        ListDelegate delegate = {};
        Listener click = {};
        Listener down = {};
    };
    ListVirtualUser* rows = ArenaNew<ListVirtualUser>(a);
    rows->s = s;
    rows->delegate = delegate;
    rows->click = ListenTo(state, &ListState::OnRowClick, 0);
    rows->down = ListenTo(state, &ListState::OnRowMouseDown, 0);
    VirtualListOpts opts;
    opts.count = total;
    opts.rowH = s->rowH;
    // A list that fills its box lets its body fill what is left of it; the
    // rows are bound at prepaint from the body's bounds either way.
    opts.viewH = fill ? 0 : viewH;
    opts.sizes = sizes;
    opts.scrollY = s->scrollY;
    opts.pad = padding;
    opts.axis = ScrollAxis::Vertical;
    opts.row = [](void* user, Ctx* cx, int r) -> El* {
        auto* u = (ListVirtualUser*)user;
        ListState* s = u->s;
        ListRow row = ListRowAt(s, r);
        El* el = nullptr;
        if (row.kind == ListRowKind::SectionHeader) {
            el = u->delegate.renderSectionHeader
                     ? u->delegate.renderSectionHeader(cx, u->delegate.data,
                                                       row.section)
                     : nullptr;
        } else if (row.kind == ListRowKind::SectionFooter) {
            el = u->delegate.renderSectionFooter
                     ? u->delegate.renderSectionFooter(cx, u->delegate.data,
                                                       row.section)
                     : nullptr;
        } else if (u->delegate.renderItem) {
            ListItem* it = u->delegate.renderItem(
                cx, u->delegate.data, row.section, row.row, row.entry);
            if (it) {
                it->selected = s->selectable && s->selected == row.entry;
                it->secondarySelected = s->rightClicked == row.entry;
                el =
                    it->IntoEl(StrDup(cx->a, IndexPathIdStr(cx->a, row.Path())),
                               ListenerArg(u->click, row.entry),
                               ListenerArg(u->down, row.entry));
                el->AriaPositionInSet(row.row + 1)->AriaSizeOfSet(s->count);
            }
        }
        return el;
    };
    opts.user = rows;
    opts.onScroll = ListenTo(state, &ListState::OnScroll);
    // The wheel reaches a scroll box only through a handler of its own:
    // VirtualList::New attaches `onScroll` only beside a numeric scroll id,
    // and this body takes its id from its path.
    El* body = gpui::VirtualList::New(cx, StrL("body"), opts)
                   ->ScrollFromPath()
                   ->OnScroll(opts.onScroll);
    if (fill) {
        body->Flex1()->MinH(0);
    }
    if (!scrollbarVisible) {
        body->HideScrollbar();
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
    // Role::List goes on the element that takes the focus: a focused node
    // without a role is not painted into the accessibility tree, so focus
    // would vanish from assistive technology.
    inner->PathClick(StrL("list-state"))
        ->Role(AccessibilityRole::List)
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
