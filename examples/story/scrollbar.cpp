#include "Story.h"

// The four datasets the Rust story cycles through.
struct ScrollDataset {
    const char* label;
    int count;
};

static const ScrollDataset kDatasets[] = {
    {"Standard", 5000},
    {"Wide", 100},
    {"Stress", 500000},
    {"Short", 5},
};

// ITEM_HEIGHT in the Rust story.
static const float kItemHeight = 50;

static const ScrollbarPlacement kPlacements[] = {
    ScrollbarPlacement::TopLeft,
    ScrollbarPlacement::TopRight,
    ScrollbarPlacement::BottomLeft,
    ScrollbarPlacement::BottomRight,
};
static const char* kPlacementLabels[] = {"TopLeft", "TopRight", "BottomLeft",
                                         "BottomRight"};

struct ScrollbarStory {
    int dataset = 0;
    float scrollY = 0;
    float scrollX = 0;
    ScrollbarPlacement placement = ScrollbarPlacement::BottomRight;

    static El* Render(ScrollbarStory* self, Ctx* cx);
};

// The view stores the offsets reported by the integrated scrollbar.
static void ScrollTo(ScrollbarStory* self, Ctx* cx, const ScrollEvent* ev) {
    self->scrollY = ev->offsetY;
    self->scrollX = ev->offsetX;
    Notify(cx);
}

static void PickDataset(ScrollbarStory* self, Ctx* cx, const ClickEvent*,
                        int64_t index) {
    self->dataset = (int)index;
    self->scrollY = 0;
    self->scrollX = 0;
    Notify(cx);
}

static void PickPlacement(ScrollbarStory* self, Ctx* cx, const ClickEvent*,
                          int64_t index) {
    self->placement = kPlacements[index];
    Notify(cx);
}

El* ScrollbarStory::Render(ScrollbarStory* self, Ctx* cx) {
    WinSize win = WindowSize(cx->win);
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    // v_flex().size_full(): the story container gives this page the pane's
    // height (StoryContainerBody), and the list takes what the toolbar leaves.
    El* page = Div(a)->FlexCol()->Gap(16)->SizeFull();

    // story_toolbar_group() with the dataset and placement submenus.
    El* toolbarRow = Div(a)->FlexRow()->W(kFill)->JustifyEnd()->ItemsStart();
    El* group = StoryToolbarGroup(cx);
    auto* dataset = component::PopupMenu::New(cx, StrL("scrollbar-dataset"));
    auto* placement =
        component::PopupMenu::New(cx, StrL("scrollbar-placement"));
    for (int i = 0; i < 4; i++) {
        dataset->MenuWithCheck(Str(kDatasets[i].label), self->dataset == i)
            ->OnClick(Listen(cx, &PickDataset, i));
        placement
            ->MenuWithCheck(Str(kPlacementLabels[i]),
                            self->placement == kPlacements[i])
            ->OnClick(Listen(cx, &PickPlacement, i));
    }
    auto* options =
        component::PopupMenu::New(cx, StrL("scrollbar-options-menu"))
            ->Submenu(StrL("Dataset"), dataset)
            ->Submenu(StrL("Placement"), placement);
    group->Child(
        component::DropdownMenu::New(cx, StrL("scrollbar-options-dropdown"))
            ->AnchorRight()
            ->Trigger(component::Button::New(cx, StrL("scrollbar-options"))
                          ->Label(StrL("Options"))
                          ->IntoEl())
            ->Menu(options)
            ->IntoEl());
    toolbarRow->Child(group);
    page->Child(toolbarRow);

    // The list fills what is left of the page, inside a bordered frame.
    El* frame = Div(a)
                    ->FlexCol()
                    ->W(kFill)
                    ->Flex1()
                    ->MinH(0)
                    ->PadX(12)
                    ->PadY(4)
                    ->Border(1, th.border)
                    ->ClipY()
                    ->ScrollY(self->scrollY)
                    ->ScrollX(self->scrollX)
                    ->ScrollPlacement(self->placement)
                    ->ScrollId(HashClickId(StrL("scrollbar-list")))
                    ->OnScroll(Listen(cx, &ScrollTo));
    int count = kDatasets[self->dataset].count;
    float width = self->dataset == 0 ? 3000.f : 10000.f;
    // The Rust list is virtualized, and so is this one: only the rows the
    // frame can show are built, with a spacer at each end standing in for the
    // rest — which is what makes half a million of them cost nothing.
    // The frame is never taller than the window, so building that many rows
    // covers it.
    float frameH = win.dipH;
    VirtualRange range =
        VirtualListVisibleRows(count, kItemHeight, self->scrollY, frameH);
    if (range.first > 0) {
        frame->Child(Div(a)->W(kFill)->H((float)range.first * kItemHeight));
    }
    for (int i = range.first; i < range.end; i++) {
        frame->Child(
            Div(a)
                ->H(kItemHeight)
                ->W(width)
                // A row of a uniform_list keeps its height: the frame holds
                // far more than it can show, and a flex child that shrinks
                // would give every row back a seventh of it.
                ->Shrink0()
                ->PadT(4)
                // .items_center() in the Rust story: the row is 50px tall and
                // the chip inside keeps its own height rather than stretching
                // to fill it.
                ->ItemsCenter()
                ->Child(Div(a)
                            ->W(kFill)
                            ->Pad(8)
                            ->Bg(th.tokens.secondary)
                            ->Child(StoryTxt(cx, StoryFmt(cx, "Item %d", i), 14,
                                             th.foreground))));
    }
    if (range.end < count) {
        frame->Child(
            Div(a)->W(kFill)->H((float)(count - range.end) * kItemHeight));
    }
    page->Child(frame);

    return page;
}

STORY_PAGE(StoryScrollbar, ScrollbarStory);
