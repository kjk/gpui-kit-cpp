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

// Pixel geometry of a grid at one container width. Columns share the width
// left over by the column gaps, so a span of n columns is n column widths
// plus the n - 1 gaps it covers. Rows use the row gap the same way.
struct GridMetrics {
    float width = 0;
    float columnWidth = 0;
    float rowHeight = 0;
    float gapWidth = 0;
    float gapHeight = 0;

    static GridMetrics New(float width, int columns, float rowHeight,
                           float gapWidth, float gapHeight);
    float Left(int col) const;
    float Top(int row) const;
    float SpanWidth(int colSpan) const;
    float SpanHeight(int rowSpan) const;
    int ColumnAt(float left) const;
    int RowAt(float top) const;
    int ColumnsSpanning(float width) const;
    int RowsSpanning(float height) const;
};

enum class GridGesture : uint8_t {
    Move,
    Resize
};

enum class GridLayoutEventKind : uint8_t {
    Change,
    Select
};

// Change carries the committed layout for the duration of the emit.
// Select carries the new id; hasId false is None. set_areas and
// set_selected_item do not emit: the caller already knows what it wrote.
struct GridLayoutEvent {
    GridLayoutEventKind kind = GridLayoutEventKind::Change;
    const GridArea* areas = nullptr;
    int nAreas = 0;
    bool hasId = false;
    Str id = {};
};

// What a pointer gesture is doing. Ids alias the state's arena.
struct GridDrag {
    Str item = {};
    GridGesture gesture = GridGesture::Move;
    float originX = 0;
    float originY = 0;
    Vec<GridArea> start;
    Vec<GridArea> preview;
    float deltaX = 0;
    float deltaY = 0;
    bool hasMetrics = false;
    GridMetrics metrics = {};
};

// The items' areas and the rules they settle under. Ids are copied into
// `ids` and aliased by every layout copy, including a gesture's start.
struct GridLayoutState {
    Vec<GridArea> areas;
    Arena* ids = nullptr;
    int columns = 12;
    bool hasCompaction = true;
    Axis compaction = Axis::Vertical;
    bool preventCollision = false;
    bool draggable = true;
    bool resizable = true;
    FocusHandle focus = {};
    bool hasSelected = false;
    Str selected = {};
    bool keyboardActive = false;
    bool hasPress = false;
    float pressX = 0;
    float pressY = 0;
    bool hasDrag = false;
    GridDrag drag;
    // Stamped by the element. Emit uses it, the way Context<Self> does.
    Entity<GridLayoutState> self = {};

    GridLayoutState();
    ~GridLayoutState();
    GridLayoutState(const GridLayoutState&) = delete;
    GridLayoutState& operator=(const GridLayoutState&) = delete;

    GridLayoutState* Cols(int cols);
    GridLayoutState* Compaction(bool has, Axis axis);
    GridLayoutState* PreventCollision(bool prevent);
    GridLayoutState* Draggable(bool value);
    GridLayoutState* Resizable(bool value);
    GridLayoutState* DefaultAreas(const GridArea* items, int n);

    const GridArea* Areas(int* n) const;
    const GridArea* Area(Str id) const;
    int ColumnCount() const { return columns; }
    bool HasCompaction() const { return hasCompaction; }
    Axis CompactionAxis() const { return compaction; }
    bool IsCollisionPrevented() const { return preventCollision; }
    bool IsDraggable() const { return draggable; }
    bool IsResizable() const { return resizable; }
    bool SelectedItem(Str* out) const;
    bool ActiveItem(Str* out) const;
    bool IsKeyboardActive() const { return keyboardActive; }

    void SetAreas(Ctx* cx, const GridArea* items, int n);
    void SetCols(Ctx* cx, int cols);
    void SetCompaction(Ctx* cx, bool has, Axis axis);
    void SetPreventCollision(Ctx* cx, bool prevent);
    void SetDraggable(Ctx* cx, bool value);
    void SetResizable(Ctx* cx, bool value);
    void SetSelectedItem(Ctx* cx, bool has, Str id);

    bool MoveItem(Ctx* cx, Str id, int col, int row);
    bool ResizeItem(Ctx* cx, Str id, int colSpan, int rowSpan);
    bool NudgeSelected(Ctx* cx, int cols, int rows);
    bool ResizeSelected(Ctx* cx, int cols, int rows);
    // False past either end, so Tab can leave the grid.
    bool SelectAdjacent(Ctx* cx, bool forward);

    void Press(Ctx* cx, Str id, float x, float y);
    void BeginDrag(Ctx* cx, Str id, GridGesture gesture);
    void DragTo(Ctx* cx, float x, float y, Bounds bounds, float rowHeight,
                float gapWidth, float gapHeight);
    void EndDrag(Ctx* cx);
    bool CancelDrag(Ctx* cx);

    const GridArea* DisplayedAreas(Ctx* cx, int* n) const;
    bool IsGestureAllowed(Str id, GridGesture gesture) const;

    GridLayoutRules Rules() const;

  private:
    void Normalize();
    void Adopt(const GridArea* items, int n);
    bool Commit(Ctx* cx, const Vec<GridArea>* next);
    void Select(Ctx* cx, bool has, Str id);
    bool SelectedArea(GridArea* out) const;
};

template <>
struct EventEmitter<GridLayoutState, GridLayoutEvent> {};

// actions!(grid_layout, [...]). Escape reuses ui::Cancel.
namespace grid_layout {
uint32_t MoveLeft();
uint32_t MoveRight();
uint32_t MoveUp();
uint32_t MoveDown();
uint32_t ShrinkWidth();
uint32_t GrowWidth();
uint32_t ShrinkHeight();
uint32_t GrowHeight();
uint32_t SelectNextItem();
uint32_t SelectPreviousItem();
const char* Context();
} // namespace grid_layout

void GridLayoutInitKeys();
// True when the command applied. False asks the caller to propagate.
bool GridLayoutHandleAction(GridLayoutState* state, Ctx* cx, uint32_t action);

} // namespace gpui
#endif // GPUI_BASE_GRID_LAYOUT_H_
