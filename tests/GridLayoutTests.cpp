/* crates/base/src/grid_layout/layout.rs */

#include "Test.h"

#include <stdio.h>

// An area at col, row covering colSpan by rowSpan cells.
static GridArea Area(const char* id, int col, int row, int colSpan,
                     int rowSpan) {
    return GridArea::New(Str(id)).Col(col).Row(row).ColSpan(colSpan).RowSpan(
        rowSpan);
}

static GridLayoutRules Rules(bool hasCompaction, Axis compaction) {
    GridLayoutRules rules;
    rules.columns = 12;
    rules.hasCompaction = hasCompaction;
    rules.compaction = compaction;
    rules.preventCollision = false;
    return rules;
}

static const GridArea* At(const GridArea* items, int n, const char* id) {
    for (int i = 0; i < n; i++) {
        if (StrEq(items[i].id, id)) {
            return &items[i];
        }
    }
    return nullptr;
}

static const GridArea* At(const Vec<GridArea>& items, const char* id) {
    return At(items.els, len(items), id);
}

static void ExpectPos(const GridArea* items, int n, const char* id, int col,
                      int row) {
    const GridArea* item = At(items, n, id);
    utassert(item != nullptr);
    if (!item) {
        return;
    }
    utassert(item->col == col);
    utassert(item->row == row);
}

static void ExpectPos(const Vec<GridArea>& items, const char* id, int col,
                      int row) {
    ExpectPos(items.els, len(items), id, col, row);
}

static bool Overlaps(const GridArea& a, const GridArea& b) {
    return a.col < b.ColEnd() && b.col < a.ColEnd() && a.row < b.RowEnd() &&
           b.row < a.RowEnd();
}

static void AssertNoOverlaps(const GridArea* items, int n) {
    for (int a = 0; a < n; a++) {
        for (int b = a + 1; b < n; b++) {
            utassert(!Overlaps(items[a], items[b]));
        }
    }
}

static void AssertNoOverlaps(const Vec<GridArea>& items) {
    AssertNoOverlaps(items.els, len(items));
}

static void TestGridAreaBuilder() {
    GridArea item = GridArea::New(StrL("a"))
                        .Col(1)
                        .Row(2)
                        .ColSpan(0)
                        .RowSpan(3)
                        .MinSpan(2, 0)
                        .MaxSpan(6, 4)
                        .Locked(true);
    utassert(item.col == 1 && item.row == 2 && item.colSpan == 1 &&
             item.rowSpan == 3);
    utassert(item.minColSpan == 2 && item.minRowSpan == 1);
    utassert(item.maxColSpan == 6 && item.maxRowSpan == 4);
    utassert(item.locked);
    utassert(item.ColEnd() == 2 && item.RowEnd() == 5);
}

static void VerticalCompactionFloatsItemsUp() {
    GridArea items[3] = {Area("a", 0, 3, 4, 2), Area("b", 0, 9, 4, 2),
                         Area("c", 4, 7, 4, 1)};
    GridCompact(items, 3, Rules(true, Axis::Vertical));
    ExpectPos(items, 3, "a", 0, 0);
    ExpectPos(items, 3, "b", 0, 2);
    ExpectPos(items, 3, "c", 4, 0);
}

static void HorizontalCompactionFloatsItemsLeading() {
    GridArea items[2] = {Area("a", 5, 0, 2, 2), Area("b", 9, 0, 2, 2)};
    GridCompact(items, 2, Rules(true, Axis::Horizontal));
    ExpectPos(items, 2, "a", 0, 0);
    ExpectPos(items, 2, "b", 2, 0);
}

static void CompactionFlowsAroundLockedItems() {
    GridArea items[2] = {Area("wall", 0, 0, 12, 1).Locked(true),
                         Area("a", 0, 0, 4, 2)};
    GridNormalize(items, 2, Rules(true, Axis::Vertical));
    ExpectPos(items, 2, "wall", 0, 0);
    ExpectPos(items, 2, "a", 0, 1);
}

static void NormalizeFitsItemsToTheColumnsAndRemovesOverlaps() {
    GridArea items[3] = {Area("wide", 4, 0, 20, 1), Area("a", 0, 0, 2, 1),
                         Area("b", 0, 0, 2, 1)};
    GridNormalize(items, 3, Rules(false, Axis::Vertical));
    const GridArea* wide = At(items, 3, "wide");
    utassert(wide != nullptr);
    if (wide) {
        utassert(wide->colSpan == 12);
        utassert(wide->col == 0);
    }
    AssertNoOverlaps(items, 3);
}

static void NormalizeAppliesChangedSizeLimits() {
    GridArea items[1] = {Area("a", 0, 0, 2, 6).MinSpan(4, 1).MaxSpan(12, 3)};
    GridNormalize(items, 1, Rules(false, Axis::Vertical));
    utassert(items[0].colSpan == 4 && items[0].rowSpan == 3);
}

static bool SameArea(const GridArea& a, const GridArea& b) {
    return StrEq(a.id, b.id) && a.col == b.col && a.row == b.row &&
           a.colSpan == b.colSpan && a.rowSpan == b.rowSpan &&
           a.minColSpan == b.minColSpan && a.minRowSpan == b.minRowSpan &&
           a.maxColSpan == b.maxColSpan && a.maxRowSpan == b.maxRowSpan &&
           a.locked == b.locked;
}

static void GridAreaJsonIsAStableFormat() {
    Arena* arena = ArenaNew();
    JsonValue* saved = JsonParse(
        arena, StrL("{\"id\":\"a\",\"col\":1,\"row\":2,\"col_span\":3,"
                    "\"row_span\":4}"));
    GridArea item;
    utassert(GridAreaFromJson(saved, &item));
    utassert(SameArea(item, Area("a", 1, 2, 3, 4)));

    StrBuilder sb;
    JsonWriter writer;
    writer.out = &sb;
    GridAreaWriteJson(&writer, Area("a", 1, 2, 3, 4).Locked(true));
    Str text = sb.TakeStr();
    JsonValue* written = JsonParse(arena, text);
    GridArea loaded;
    utassert(GridAreaFromJson(written, &loaded));
    utassert(StrEq(loaded.id, "a"));
    utassert(loaded.col == 1 && loaded.row == 2);
    utassert(loaded.colSpan == 3 && loaded.rowSpan == 4);
    utassert(loaded.minColSpan == 1 && loaded.minRowSpan == 1);
    utassert(loaded.maxColSpan == 0 && loaded.maxRowSpan == 0);
    utassert(loaded.locked);
    StrFree(text);
    ArenaDelete(arena);
}

static void ReflowingStacksACopyInReadingOrder() {
    GridArea areas[3] = {Area("b", 6, 0, 6, 2),
                         Area("a", 0, 0, 6, 3).MinSpan(4, 1),
                         Area("c", 0, 3, 12, 1).Locked(true)};
    Vec<GridArea> stacked;
    GridReflowed(areas, 3, 1, &stacked);
    ExpectPos(stacked, "a", 0, 0);
    ExpectPos(stacked, "b", 0, 3);
    ExpectPos(stacked, "c", 0, 5);
    utassert(len(stacked) == 3);
    for (int i = 0; i < len(stacked); i++) {
        utassert(stacked[i].colSpan == 1);
    }
    AssertNoOverlaps(stacked);
    const GridArea* a = At(areas, 3, "a");
    const GridArea* c = At(areas, 3, "c");
    utassert(a && a->colSpan == 6);
    utassert(c && c->locked);
}

static void MovingOntoAnItemPushesItDown() {
    GridArea items[2] = {Area("a", 0, 0, 4, 2), Area("b", 4, 0, 4, 2)};
    Vec<GridArea> next;
    utassert(GridMoveItem(items, 2, StrL("a"), 4, 1,
                          Rules(false, Axis::Vertical), &next));
    ExpectPos(next, "a", 4, 1);
    ExpectPos(next, "b", 4, 3);
    AssertNoOverlaps(next);
}

static void MovingUpOntoAnItemSwapsPlaces() {
    GridArea items[2] = {Area("a", 0, 0, 4, 2), Area("b", 0, 2, 4, 2)};
    Vec<GridArea> next;
    utassert(GridMoveItem(items, 2, StrL("b"), 0, 0,
                          Rules(true, Axis::Vertical), &next));
    ExpectPos(next, "b", 0, 0);
    ExpectPos(next, "a", 0, 2);
    AssertNoOverlaps(next);
}

static void MovesAndResizesNeverLeaveOverlaps() {
    char ids[7][16];
    GridArea items[8];
    for (int n = 0; n < 7; n++) {
        snprintf(ids[n], 16, "item-%d", n);
        items[n] = Area(ids[n], (n % 3) * 4, (n / 3) * 2, 4, 2 + n % 2);
    }
    items[7] = Area("wall", 5, 5, 2, 1).Locked(true);
    GridLayoutRules modes[3] = {Rules(false, Axis::Vertical),
                                Rules(true, Axis::Vertical),
                                Rules(true, Axis::Horizontal)};
    for (int mode = 0; mode < 3; mode++) {
        GridArea start[8];
        for (int i = 0; i < 8; i++) {
            start[i] = items[i];
        }
        GridNormalize(start, 8, modes[mode]);
        AssertNoOverlaps(start, 8);
        const GridArea* wall0 = At(start, 8, "wall");
        utassert(wall0 != nullptr);
        if (!wall0) {
            continue;
        }
        int wallCol = wall0->col;
        int wallRow = wall0->row;
        for (int n = 0; n < 7; n++) {
            for (int x = 0; x < 12; x++) {
                for (int y = 0; y < 9; y++) {
                    Vec<GridArea> next;
                    utassert(GridMoveItem(start, 8, Str(ids[n]), x, y,
                                          modes[mode], &next));
                    AssertNoOverlaps(next);
                    ExpectPos(next, "wall", wallCol, wallRow);
                    Vec<GridArea> resized;
                    if (GridResizeItem(start, 8, Str(ids[n]), x + 1, y + 1,
                                       modes[mode], &resized)) {
                        AssertNoOverlaps(resized);
                    }
                }
            }
        }
    }
}

static void AMoveIsClampedToTheColumns() {
    GridArea items[1] = {Area("a", 0, 0, 4, 2)};
    Vec<GridArea> next;
    utassert(GridMoveItem(items, 1, StrL("a"), 11, 0,
                          Rules(false, Axis::Vertical), &next));
    ExpectPos(next, "a", 8, 0);
}

static void LockedItemsNeitherMoveNorGetPushed() {
    GridArea items[2] = {Area("wall", 4, 0, 4, 2).Locked(true),
                         Area("a", 0, 0, 4, 2)};
    Vec<GridArea> refused;
    utassert(!GridMoveItem(items, 2, StrL("wall"), 0, 4,
                           Rules(false, Axis::Vertical), &refused));
    Vec<GridArea> next;
    utassert(GridMoveItem(items, 2, StrL("a"), 4, 0,
                          Rules(true, Axis::Vertical), &next));
    ExpectPos(next, "wall", 4, 0);
    AssertNoOverlaps(next);
}

static void PreventCollisionRefusesOverlappingMoves() {
    GridArea items[2] = {Area("a", 0, 0, 4, 2), Area("b", 4, 0, 4, 2)};
    GridLayoutRules rules = Rules(false, Axis::Vertical);
    rules.preventCollision = true;
    Vec<GridArea> refused;
    utassert(!GridMoveItem(items, 2, StrL("a"), 2, 0, rules, &refused));
    Vec<GridArea> next;
    utassert(GridMoveItem(items, 2, StrL("a"), 0, 3, rules, &next));
    ExpectPos(next, "a", 0, 3);
}

static void ResizeRespectsLimitsAndPushesNeighbours() {
    GridArea items[2] = {Area("a", 0, 0, 4, 2).MinSpan(2, 2).MaxSpan(8, 4),
                         Area("b", 4, 0, 4, 2)};
    Vec<GridArea> next;
    utassert(GridResizeItem(items, 2, StrL("a"), 20, 1,
                            Rules(true, Axis::Vertical), &next));
    const GridArea* a = At(next, "a");
    utassert(a != nullptr);
    if (a) {
        utassert(a->colSpan == 8 && a->rowSpan == 2);
    }
    ExpectPos(next, "b", 4, 2);
    AssertNoOverlaps(next);
}

static void ResizeStopsAtLockedItems() {
    GridArea items[2] = {Area("a", 0, 0, 4, 2), Area("wall", 4, 0, 4, 2)
                                                    .Locked(true)};
    Vec<GridArea> next;
    utassert(GridResizeItem(items, 2, StrL("a"), 6, 3,
                            Rules(false, Axis::Vertical), &next));
    const GridArea* a = At(next, "a");
    utassert(a != nullptr);
    if (a) {
        utassert(a->colSpan == 4 && a->rowSpan == 3);
    }
    AssertNoOverlaps(next);
}

void TestGridAreaLayout() {
    TestSuite("grid_layout");
    TestGridAreaBuilder();
    VerticalCompactionFloatsItemsUp();
    HorizontalCompactionFloatsItemsLeading();
    CompactionFlowsAroundLockedItems();
    NormalizeFitsItemsToTheColumnsAndRemovesOverlaps();
    NormalizeAppliesChangedSizeLimits();
    GridAreaJsonIsAStableFormat();
    ReflowingStacksACopyInReadingOrder();
    MovingOntoAnItemPushesItDown();
    MovingUpOntoAnItemSwapsPlaces();
    MovesAndResizesNeverLeaveOverlaps();
    AMoveIsClampedToTheColumns();
    LockedItemsNeitherMoveNorGetPushed();
    PreventCollisionRefusesOverlappingMoves();
    ResizeRespectsLimitsAndPushesNeighbours();
    ResizeStopsAtLockedItems();
}
