#ifndef GPUI_BASE_GRID_LAYOUT_H_
#define GPUI_BASE_GRID_LAYOUT_H_
/* Grid placement — crates/base/src/grid_layout/.
   Cells, not pixels, until the element asks the metrics for a box. An area
   does not own its id: the string is a literal or arena text, and a copied
   layout aliases it. */

#include "gpui/gpui.h"
#include "base/json.h"
#include "base/motion.h"
#include "base/state_style.h"

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
    // Stamped each frame from the element. A drag listener is a function on
    // the state, so the row height and gap the render closed over live here
    // until the next frame replaces them.
    float paintRowHeight = 0;
    float paintGapW = 0;
    float paintGapH = 0;
    int paintColumns = 12;
    bool paintReadOnly = false;

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

// The side of the square, at an item's trailing-bottom corner, that the
// pointer resizes the item by.
constexpr float kGridResizeHandle = 16.f;

// What marks the drop cells, or the grip in the resize corner. Called while
// the grid is built, once per frame (the placeholder) or once per resizable
// item (the grip).
using GridPartFn = El* (*)(Ctx * cx, void* data);

// One child of a GridItem. The node is arena storage; the item aliases it.
struct GridItemChild {
    El* el = nullptr;
    GridItemChild* next = nullptr;
};

// One item of a GridLayout, paired with the GridArea of the same id. It does
// not own its id or its children: both are the caller's, for this frame.
struct GridItem {
    Arena* arena = nullptr;
    Str id = {};
    Str label = {};
    bool hasLabel = false;
    bool handleOnly = false;
    StateStyle style = {};
    GridItemChild* children = nullptr;
    GridItemChild* lastChild = nullptr;

    static GridItem* New(Ctx* cx, Str id);
    GridItem* Label(Str text);
    GridItem* DragHandleOnly(bool value);
    GridItem* Pad(float x, float y);
    GridItem* Child(El* el);
};

// The part of an item the pointer moves it by, when the item is
// drag-handle-only. A press here starts a move; the item's own press still
// selects.
El* GridDragHandle(Ctx* cx, Entity<GridLayoutState> state, Str item);

struct GridItemNode {
    GridItem item = {};
    GridItemNode* next = nullptr;
};

// The element. Like every base control it paints nothing of its own: gaps,
// the placeholder, the resize grip and the dragged and selected styles come
// from the builders. The component façade supplies them from the theme.
struct GridLayout {
    Ctx* cx = nullptr;
    Arena* arena = nullptr;
    Str id = {};
    Str label = {};
    Entity<GridLayoutState> state = {};
    GridItemNode* items = nullptr;
    GridItemNode* lastItem = nullptr;
    int nItems = 0;
    StateStyle gridStyle = {};
    float gapX = 0;
    float gapY = 0;
    float rowHeightRems = 2.5f;
    Motion settle = MotionNew(0);
    StateStyle draggingItem = {};
    StateStyle selectedItem = {};
    StateStyle focusedItem = {};
    bool hasReflow = false;
    int reflowCols = 1;
    GridPartFn placeholder = nullptr;
    void* placeholderData = nullptr;
    GridPartFn resizeHandle = nullptr;
    void* resizeHandleData = nullptr;

    static GridLayout* New(Ctx* cx, Str id, Entity<GridLayoutState> state);
    GridLayout* Label(Str text);
    GridLayout* Item(GridItem* item);
    GridLayout* RowHeight(float rems);
    GridLayout* Gap(float px);
    GridLayout* GapX(float px);
    GridLayout* GapY(float px);
    GridLayout* Transition(Motion transition);
    GridLayout* DraggingItem(const StateStyle& style);
    GridLayout* SelectedItem(const StateStyle& style);
    GridLayout* FocusedItem(const StateStyle& style);
    // Display over `cols` columns without writing the stored layout. 0
    // becomes 1. While reflowed the grid is read-only; selection still works.
    GridLayout* Reflow(int cols);
    GridLayout* Placeholder(GridPartFn fn, void* data);
    GridLayout* ResizeHandle(GridPartFn fn, void* data);
    // gap_x / gap_y as pixels. A fractional gap is not stored: it counts as
    // none, which is what resolve_gap does when the length is not absolute.
    void ResolvedGap(float remSize, float* x, float* y) const;
    El* IntoEl();
};

} // namespace gpui
#endif // GPUI_BASE_GRID_LAYOUT_H_
