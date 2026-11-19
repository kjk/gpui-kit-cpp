/* crates/component/tests/table_bench.rs. The same sizes, scroll patterns,
   viewport, warmup and frame counts. Memory here is bytes used in the frame
   arena, where the C++ element tree is built. */

#define GPUI_INCLUDE_PRIVATE_API 1
#include "Bench.h"

#include <stdio.h>
#include <stdlib.h>

static constexpr int kColumns = 12;
static constexpr float kColumnWidth = 120;
static constexpr float kRowHeight = 32;
static constexpr int kWarmup = 20;
static constexpr int kFrames = 300;

enum class TableFrameScenario : uint8_t {
    Stationary,
    ScrollY,
    ScrollX,
};

static El* BenchTableCell(Ctx* cx, void*, int row, int col) {
    return TextEl(cx->a, StrDup(cx->a, fmt("r%d c%d", row, col)));
}

struct TableFrameView {
    Entity<TableState> table;
    int rows = 0;
    float dy = 0;
    bool plain = false;

    static El* PlainRow(void*, Ctx* cx, int index) {
        El* row = Div(cx->a)->FlexRow()->H(kRowHeight);
        for (int col = 0; col < kColumns; col++) {
            row->Child(Div(cx->a)
                           ->W(kColumnWidth)
                           ->Shrink0()
                           ->PadX(Rems(cx, 0.5f))
                           ->Child(BenchTableCell(cx, nullptr, index, col)));
        }
        return row;
    }

    static El* Render(TableFrameView* self, Ctx* cx) {
        if (self->plain) {
            VirtualListOpts options;
            options.count = self->rows;
            options.rowH = kRowHeight;
            options.scrollY = self->dy;
            options.row = PlainRow;
            return VirtualList::New(cx, StrL("plain-table"), options);
        }
        component::TableColumn columns[kColumns];
        for (int col = 0; col < kColumns; col++) {
            Str id = StrDup(cx->a, fmt("c%d", col));
            Str label = StrDup(cx->a, fmt("Column %d", col));
            columns[col] = component::TableColumn::New(id, label)
                               .Width(kColumnWidth);
        }
        return component::DataTable::New(cx, StrL("benchmark-table"),
                                         self->table)
            ->Columns(columns, kColumns)
            ->RowHeight(kRowHeight)
            ->Rows(self->rows, nullptr, BenchTableCell)
            ->IntoEl();
    }
};

static int CompareFrameTimes(const void* lhs, const void* rhs) {
    double a = *(const double*)lhs;
    double b = *(const double*)rhs;
    return (a > b) - (a < b);
}

static void MeasureTableFrames(int rows, bool plain,
                               TableFrameScenario scenario) {
    App* app = TestAppNew();
    component::Init(app);
    auto view = EntityNew<TableFrameView>(app);
    auto* self = view.Get(app);
    self->rows = rows;
    self->plain = plain;
    if (!plain) {
        self->table = EntityNewState<TableState>(app);
    }
    Window* win = TestWindowOpen(app, view, 1280, 800);
    TableState* table = self->table.Get(app);
    const int maxRow = std::max(rows - 30, 1);
    double us[kFrames];
    uint64_t bytes = 0;
    for (int frame = 0; frame < kWarmup + kFrames; frame++) {
        if (scenario == TableFrameScenario::ScrollY) {
            self->dy = kRowHeight * (float)(frame % maxRow);
            if (table) {
                table->scrollY = self->dy;
            }
        } else if (scenario == TableFrameScenario::ScrollX) {
            TableScrollToCol(table, frame % kColumns, ScrollStrategy::Start);
        }
        double start = ClockMonotonicSecondsSinceStart();
        TestDraw(win);
        double elapsed = ClockMonotonicSecondsSinceStart() - start;
        if (frame >= kWarmup) {
            us[frame - kWarmup] = elapsed * 1000000;
            bytes += ArenaUsed(win->frameArena);
        }
    }
    qsort(us, kFrames, sizeof(double), CompareFrameTimes);
    const char* name = "stationary";
    if (scenario == TableFrameScenario::ScrollY) {
        name = "scroll-y";
    } else if (scenario == TableFrameScenario::ScrollX) {
        name = "scroll-x";
    }
    printf("| %s | %d | %s | %.1f | %.1f | %.1f |\n",
           plain ? "uniform_list" : "DataTable", rows, name, us[kFrames / 2],
           us[kFrames * 95 / 100], (double)bytes / (kFrames * 1024.0));
    TestAppFree(app);
}

void BenchTableFrames() {
    // Rust marks the frame bench ignored by default.
    if (!gBenchFilter || !BenchWanted("table", "table_frames")) {
        return;
    }
    printf(
        "\n| table | rows | scenario | median us | p95 us | arena KiB/frame |\n"
        "|---|---|---|---|---|---|\n");
    for (int rows : {20, 2000}) {
        MeasureTableFrames(rows, false, TableFrameScenario::Stationary);
        MeasureTableFrames(rows, false, TableFrameScenario::ScrollY);
        MeasureTableFrames(rows, false, TableFrameScenario::ScrollX);
        MeasureTableFrames(rows, true, TableFrameScenario::Stationary);
        MeasureTableFrames(rows, true, TableFrameScenario::ScrollY);
    }
}
