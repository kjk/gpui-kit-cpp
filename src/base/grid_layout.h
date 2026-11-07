#ifndef GPUI_BASE_GRID_LAYOUT_H_
#define GPUI_BASE_GRID_LAYOUT_H_
/* Grid placement arithmetic — crates/base/src/grid_layout/layout.rs.
   Cells, not pixels. An area does not own its id: the string is a literal
   or arena text, and a copied layout aliases it. */

#include "gpui/gpui.h"
#include "base/json.h"

namespace gpui {

// Where one item sits, in zero-based cells. col is a cell index, not a
// one-based CSS grid line. A max span of 0 is None: the width limit is the
// grid, and the height limit is unbounded.
struct GridArea {
    Str id = {};
    int col = 0;
    int row = 0;
    int colSpan = 1;
    int rowSpan = 1;
    int minColSpan = 1;
    int minRowSpan = 1;
    int maxColSpan = 0;
    int maxRowSpan = 0;
    bool locked = false;

    static GridArea New(Str id);
    GridArea Col(int value) const;
    GridArea Row(int value) const;
    GridArea ColSpan(int value) const;
    GridArea RowSpan(int value) const;
    GridArea MinSpan(int colSpan, int rowSpan) const;
    GridArea MaxSpan(int colSpan, int rowSpan) const;
    GridArea Locked(bool value) const;
    int RowEnd() const { return row + rowSpan; }
    int ColEnd() const { return col + colSpan; }
};

// How a layout responds to edits. hasCompaction false leaves items where
// they are put, aside from pushing overlaps down.
struct GridLayoutRules {
    int columns = 1;
    bool hasCompaction = false;
    Axis compaction = Axis::Vertical;
    bool preventCollision = false;
};

int GridRows(const GridArea* items, int n);
void GridNormalize(GridArea* items, int n, GridLayoutRules rules);
void GridCompact(GridArea* items, int n, GridLayoutRules rules);
// The layout `items` would take over `columns` columns. Locks are dropped
// on the copy. `out` is replaced.
void GridReflowed(const GridArea* items, int n, int columns,
                  Vec<GridArea>* out);
// False when the rules refuse the edit. `out` is the settled layout.
bool GridMoveItem(const GridArea* items, int n, Str id, int x, int y,
                  GridLayoutRules rules, Vec<GridArea>* out);
bool GridResizeItem(const GridArea* items, int n, Str id, int w, int h,
                    GridLayoutRules rules, Vec<GridArea>* out);

// The persisted shape. Missing min spans load as 1, missing max spans as
// none, and a missing lock as unlocked.
bool GridAreaFromJson(const JsonValue* value, GridArea* out);
void GridAreaWriteJson(JsonWriter* writer, const GridArea& area);

} // namespace gpui
#endif // GPUI_BASE_GRID_LAYOUT_H_
