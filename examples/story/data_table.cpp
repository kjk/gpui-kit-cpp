#include "Story.h"

// The Rust story fills the table with random stocks; ours keeps a fixed set
// with the same columns.
struct Stock {
    const char* market;
    const char* name;
    const char* symbol;
    const char* price;
    const char* chg;
    const char* pct;
    bool up;
};

static const Stock kStocks[] = {
    {"US", "Coinbase Global Inc.", "COIN.US", "536.27", "+37.79", "+7.05%",
     true},
    {"US", "Lowe's Companies Inc.", "LOW.US", "404.59", "-22.41", "-5.54%",
     false},
    {"US", "Zoetis Inc.", "ZTS.US", "633.99", "-62.24", "-9.82%", false},
    {"US", "Tesla Inc.", "TSLA.US", "949.92", "+30.37", "+3.20%", true},
    {"HK", "Xiaomi Corp.", "1810.HK", "462.56", "-1.66", "-0.36%", false},
    {"US", "PepsiCo Inc.", "PEP.US", "75.89", "+1.39", "+1.83%", true},
    {"US", "Viomi Technology Co. Lt...", "VIOT.US", "927.28", "-9.54", "-1.03%",
     false},
    {"HK", "CSOP FTSE China A50 ETF", "2822.HK", "632.39", "+47.13", "+7.45%",
     true},
    {"US", "Zepp Health Corp. ADR", "ZEPP.US", "401.68", "+38.18", "+9.51%",
     true},
    {"HK", "China Oilfield Services Ltd.", "2883.HK", "854.84", "+82.09",
     "+9.60%", true},
    {"US", "Zscaler Inc.", "ZS.US", "145.76", "-2.00", "-1.37%", false},
    {"US", "American Express Co.", "AXP.US", "484.57", "+36.92", "+7.62%",
     true},
    {"US", "Workday Inc.", "WDAY.US", "111.25", "-1.46", "-1.31%", false},
    {"US", "SOS Ltd. ADR", "SOS.US", "712.91", "+52.24", "+7.33%", true},
    {"US", "Unity Software Inc.", "U.US", "609.16", "+49.35", "+8.10%", true},
    {"US", "Uber Technologies Inc.", "UBER.US", "198.21", "+17.81", "+8.99%",
     true},
    {"HK", "China Resources Power...", "0836.HK", "300.31", "-20.94", "-6.97%",
     false},
};

enum {
    DtMenuSize = 1,
    DtMenuRows,
    DtMenuExtra,
    DtMenuOptions,
    DtMenuGoTo
};

enum {
    DtActSize = 480,   // + index into kSizes
    DtActRows = 500,   // + index into kRowCounts
    DtActExtra = 520,  // + index into kExtraCounts
    DtActOption = 540, // + index into kDtOptions
    DtActClear = 559,
    DtActGoTo = 560
};

// The rows and columns the toolbar can ask for, and how each reads.
static const int kRowCounts[] = {100, 500, 5000, 10000, 1000000};
static const char* const kRowLabels[] = {"100", "500", "5,000", "10,000",
                                         "1,000,000"};
static const int kNRowCounts = 5;
static const int kExtraCounts[] = {0, 4, 8, 16, 32};
static const char* const kExtraLabels[] = {"None", "4", "8", "16", "32"};
static const int kNExtraCounts = 5;
// Size::table_row_height: 48px, Large, Medium, Small, XSmall.
static const float kSizeRowH[] = {48, 40, 32, 30, 26};
static const char* const kSizeLabels[] = {"48px", "Large", "Medium", "Small",
                                          "XSmall"};
static const int kNSizes = 5;

// The Options dropdown, in Rust's own order.
enum {
    DtOptLoop = 0,
    DtOptColResize,
    DtOptColOrder,
    DtOptSortable,
    DtOptColSelect,
    DtOptRowSelect,
    DtOptCellSelect,
    DtOptRowHeader,
    DtOptFixedColumn,
    DtOptStriped,
    DtOptLoading,
    DtOptLazyLoad,
    DtOptRefresh,
    DtOptGroupHeaders,
    DtOptCount
};
static const char* const kDtOptions[DtOptCount] = {
    "Loop Selection",    "Column Resize",  "Column Order",    "Sortable",
    "Column Selectable", "Row Selectable", "Cell Selectable", "Row Header",
    "Fixed Column",      "Striped Rows",   "Loading",         "Lazy Load",
    "Refresh Data",      "Group Headers"};
static const char* const kGoToRows[] = {"Top", "Bottom", "Cell 5:3",
                                        "Cell 10:7"};
static const int kNGoTo = 4;

// The order the rows are shown in, which is what the delegate's perform_sort
// rewrites. -1 means the table's own order.
struct DataTableStory {
    int rowCount = 2; // 5,000
    int extra = 0;    // no extra columns
    int size = 2;     // Medium
    int openMenu = 0;
    // TableState's own defaults: everything but cell selection and the four
    // switches under the separator.
    bool options[DtOptCount] = {true, true, true,  true,  true,  true,  false,
                                true, true, false, false, false, false, true};
    StoryToolbarState toolbar;
    // TableState is an entity in Rust too, which is what the row and head
    // closures capture.
    Entity<TableState> table = {};
    bool seeded = false;
    // What the last table event said, shown under the table.
    Str message = {};
    // dump_csv: the click asks, and the frame that has the built table
    // answers — the table is a per-frame builder here, so a handler between
    // frames has nothing to dump.
    bool wantExport = false;
    // visible_rows_changed / visible_columns_changed: the story keeps what
    // it was last told and shows it in the status line, which is the whole
    // demonstration of the two hooks.
    int visRowFirst = 0, visRowEnd = 0;
    int visColFirst = 0, visColEnd = 0;
    // The order the rows are shown in, which is what the delegate's
    // perform_sort rewrites.
    int order[17] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};

    ~DataTableStory() { StrFree(message); }
    static El* Render(DataTableStory* self, Ctx* cx);
};

// The delegate's perform_sort: the story reorders its own rows, which is the
// whole point of the event.
static float StockKey(const Stock& s, int col) {
    switch (col) {
        case 4:
            return StrToFloatUnchecked(Str(s.price));
        case 5:
            return StrToFloatUnchecked(Str(s.chg));
        default:
            return StrToFloatUnchecked(Str(s.pct));
    }
}

static void SortRows(DataTableStory* self, int col, ColumnSort dir) {
    const int n = (int)(sizeof(kStocks) / sizeof(kStocks[0]));
    for (int i = 0; i < n; i++) {
        self->order[i] = i;
    }
    if (dir == ColumnSort::Default) {
        return;
    }
    bool asc = dir == ColumnSort::Ascending;
    // An insertion sort: seventeen rows, and it keeps equal rows in the order
    // the table had them.
    for (int i = 1; i < n; i++) {
        int v = self->order[i];
        int j = i - 1;
        while (j >= 0) {
            bool swap;
            if (col == 3) {
                int c = StrCmp(Str(kStocks[self->order[j]].symbol),
                               Str(kStocks[v].symbol));
                swap = asc ? c > 0 : c < 0;
            } else {
                float a = StockKey(kStocks[self->order[j]], col);
                float b = StockKey(kStocks[v], col);
                swap = asc ? a > b : a < b;
            }
            if (!swap) {
                break;
            }
            self->order[j + 1] = self->order[j];
            j--;
        }
        self->order[j + 1] = v;
    }
}

// cx.subscribe(&table, ..): the story says what it was told.
static void OnTableEvent(DataTableStory* self, Ctx* cx, const TableEvent* ev) {
    StrFree(self->message);
    switch (ev->kind) {
        case TableEventKind::SelectRow:
            self->message = StrDup(fmt("Selected row %d", ev->row));
            break;
        case TableEventKind::SelectCol:
            self->message = StrDup(fmt("Selected column %d", ev->col));
            break;
        case TableEventKind::SelectCell:
            self->message =
                StrDup(fmt("Selected cell %d:%d", ev->row, ev->col));
            break;
        case TableEventKind::DoubleClickedRow:
            self->message = StrDup(fmt("Double clicked row %d", ev->row));
            break;
        case TableEventKind::DoubleClickedCell:
            self->message =
                StrDup(fmt("Double clicked cell %d:%d", ev->row, ev->col));
            break;
        // The story prints these; here they say the same thing on the status
        // line. A row event with -1 is Rust's RightClickedRow(None), which
        // says the mark has gone rather than that a row was clicked.
        case TableEventKind::RightClickedRow:
            self->message = ev->row >= 0
                                ? StrDup(fmt("Right clicked row %d", ev->row))
                                : StrDup(StrL("Right click cleared"));
            break;
        case TableEventKind::RightClickedCell:
            self->message =
                StrDup(fmt("Right clicked cell %d:%d", ev->row, ev->col));
            break;
        case TableEventKind::MoveColumn:
            self->message =
                StrDup(fmt("Moved column %d to %d", ev->col, ev->row));
            break;
        case TableEventKind::ColumnWidthsChanged: {
            // ColumnWidthsChanged carries every column's width, not just the
            // one the drag moved.
            StrBuilder sb;
            sb.Append(StrL("Column widths"));
            for (int i = 0; i < ev->nWidths; i++) {
                sb.Append(fmt(" %d", (int)ev->widths[i]));
            }
            self->message = sb.TakeStr();
            break;
        }
        case TableEventKind::Sort:
            SortRows(self, ev->col, ev->sort);
            self->message = StrDup(
                fmt("Sorted column %d %s", ev->col,
                    Str(ev->sort == ColumnSort::Ascending    ? "ascending"
                        : ev->sort == ColumnSort::Descending ? "descending"
                                                             : "off")));
            break;
        default:
            self->message = StrDup(StrL("Selection cleared"));
            break;
    }
    Notify(cx);
}

static void DtMenuOpen(DataTableStory* self, Ctx* cx, const ClickEvent*,
                       intptr_t which) {
    self->openMenu = self->openMenu == (int)which ? 0 : (int)which;
    Notify(cx);
}
// dump_csv. Rust hands the pair to the `csv` crate and then to a save
// dialog; this tree has neither, so the click asks for the dump and the
// count and the header row come back in the message line under the table.
static void DtExport(DataTableStory* self, Ctx* cx, const ClickEvent*) {
    (void)cx;
    self->wantExport = true;
    Notify(cx);
}

static void DtMenuAct(DataTableStory* self, Ctx* cx, const ClickEvent*,
                      intptr_t act) {
    if (act >= DtActGoTo) {
        // Top and Bottom scroll_to_row; the other two set_selected_cell.
        TableState* st = self->table.Get(cx);
        int which = (int)(act - DtActGoTo);
        if (st && which == 0) {
            TableScrollToRow(st, 0, ScrollStrategy::Center);
        } else if (st && which == 1) {
            TableScrollToRow(st, st->rowCount - 1, ScrollStrategy::Center);
        } else if (st && which == 2) {
            TableSetSelectedCell(st, cx, 5, 3);
        } else if (st) {
            TableSetSelectedCell(st, cx, 10, 7);
        }
    } else if (act == DtActClear) {
        TableState* st = self->table.Get(cx);
        if (st) {
            TableClearSelection(st, cx);
        }
    } else if (act >= DtActOption) {
        int i = (int)(act - DtActOption);
        self->options[i] = !self->options[i];
    } else if (act >= DtActExtra) {
        self->extra = (int)(act - DtActExtra);
    } else if (act >= DtActRows) {
        self->rowCount = (int)(act - DtActRows);
    } else if (act >= DtActSize) {
        self->size = (int)(act - DtActSize);
    }
    self->openMenu = 0;
    Notify(cx);
}

// TableDelegate::context_menu. A secondary press on a row opens this where
// the pointer is: the row it was opened on, then the five sizes the toolbar
// also offers. The row line carries `OpenDetail(row_ix)` in Rust, which
// nothing handles, so it does nothing here either.
static void OnDtContextItem(DataTableStory* self, Ctx* cx, const ClickEvent*,
                            intptr_t ix) {
    // 0 is the row line, 1 the separator, and the sizes follow.
    int size = (int)ix - 2;
    if (size >= 0 && size < kNSizes) {
        self->size = size;
        Notify(cx);
    }
}

static component::PopupMenu* DtContextMenu(Ctx* cx, void* data, int row,
                                           component::PopupMenu* menu) {
    (void)data;
    menu->Menu(StoryFmt(cx, "Selected Row: %d", row))->Separator();
    for (int i = 0; i < kNSizes; i++) {
        menu->Menu(StoryFmt(cx, "Size %s", kSizeLabels[i]));
    }
    PopupMenuState* st = menu->state.Get(cx);
    if (st) {
        st->onConfirm = Listen(cx, &OnDtContextItem);
    }
    return menu;
}

// The base columns' count, which is where the "Column N" extras start.
static const int kBaseColumns = 45;

// The Rust delegate fakes every field per row; a hash of the cell does the
// same job here, and gives the same cell the same value every frame.
static float DtNoise(int row, int col) {
    uint32_t h = (uint32_t)row * 2654435761u ^ (uint32_t)col * 2246822519u;
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    return (float)(h % 100000u) / 100000.f;
}

// `(lo..hi).fake()`, off the same hash: the k-th draw of a row.
static float DtRand(int row, int k, float lo, float hi) {
    return lo + DtNoise(row, k) * (hi - lo);
}

// One row's quote. random_stocks_exact's own note is what this is for: the
// fields of a row hang together the way a real quote does — the turnover is
// that volume at that price, the market cap is the price over the shares
// outstanding, and the bid, ask, open, high and low stay inside a day's range
// of it. A table of unrelated numbers reads as noise, and reads wrong besides:
// a market cap drawn on its own lands in the millions where a real one is in
// the billions, and a TTM lands there too where Rust's is between 5 and 80.
struct DtQuote {
    float volume, turnover, marketCap, ttm;
    float rank5m, rank60d, yearChangePercent;
    float bid, bidVolume, ask, askVolume;
    float open, high, low;
    float turnoverRate, riseRate, amplitude, pe, pb, volumeRatio, bidAskRatio;
    float preClose, postClose;
    float preMarketCap, preMarketPercent, preMarketChange;
    float postMarketCap, postMarketPercent, postMarketChange;
    float floatCap, shares, sharesFloat;
    float rank5d, rank10d, rank30d, rank120d, rank250d;
};

static DtQuote DtQuoteFor(int row, float price) {
    DtQuote q = {};
    float shares = DtRand(row, 3, 1e6f, 3e9f);
    q.volume = DtRand(row, 1, 1e4f, 5e7f);
    q.turnover = q.volume * price;
    q.marketCap = price * shares;
    q.ttm = DtRand(row, 4, 5.f, 80.f);
    q.rank5m = DtRand(row, 5, 0.f, 1000.f);
    q.rank60d = DtRand(row, 6, 0.f, 1000.f);
    q.yearChangePercent = DtRand(row, 7, -1.f, 1.f);
    q.bid = price * (1.f - DtRand(row, 8, 0.f, 0.01f));
    q.bidVolume = DtRand(row, 9, 100.f, 5e4f);
    q.ask = price * (1.f + DtRand(row, 10, 0.f, 0.01f));
    q.askVolume = DtRand(row, 11, 100.f, 5e4f);
    q.open = price * (1.f + DtRand(row, 12, -0.05f, 0.05f));
    q.high = price * (1.f + DtRand(row, 13, 0.f, 0.08f));
    q.low = price * (1.f - DtRand(row, 14, 0.f, 0.08f));
    q.turnoverRate = DtRand(row, 15, 0.f, 0.2f);
    q.riseRate = DtRand(row, 16, 0.f, 1.f);
    q.amplitude = DtRand(row, 17, 0.f, 0.15f);
    q.pe = DtRand(row, 18, 5.f, 60.f);
    q.pb = DtRand(row, 19, 0.5f, 12.f);
    q.volumeRatio = DtRand(row, 20, 0.f, 3.f);
    q.bidAskRatio = DtRand(row, 21, 0.f, 3.f);
    q.preClose = price * (1.f + DtRand(row, 22, -0.03f, 0.03f));
    q.postClose = price * (1.f + DtRand(row, 23, -0.03f, 0.03f));
    q.preMarketCap = q.marketCap;
    q.preMarketPercent = DtRand(row, 24, -0.05f, 0.05f);
    q.preMarketChange = price * DtRand(row, 25, -0.05f, 0.05f);
    q.postMarketCap = q.marketCap;
    q.postMarketPercent = DtRand(row, 26, -0.05f, 0.05f);
    q.postMarketChange = price * DtRand(row, 27, -0.05f, 0.05f);
    q.floatCap = q.marketCap * DtRand(row, 28, 0.3f, 1.f);
    q.shares = shares;
    q.sharesFloat = shares * DtRand(row, 29, 0.3f, 1.f);
    q.rank5d = DtRand(row, 30, 0.f, 1000.f);
    q.rank10d = DtRand(row, 31, 0.f, 1000.f);
    q.rank30d = DtRand(row, 32, 0.f, 1000.f);
    q.rank120d = DtRand(row, 33, 0.f, 1000.f);
    q.rank250d = DtRand(row, 34, 0.f, 1000.f);
    return q;
}

// How a cell reads, which is which of the delegate's three renderers it goes
// through: a plain number, a signed change in the trend colour, or a
// percentage tinted over the whole cell.
enum class DtCell : uint8_t {
    Number,
    Change,
    Percent
};

struct DtCellVal {
    Str text = {};
    DtCell kind = DtCell::Number;
    // What render_change and render_percent colour by; unused for a number.
    float signal = 0;
};

// compact(): the shortest reading of a large number. The scale is chosen off
// the magnitude, so a change of -1.24B reads as one too.
static Str DtCompact(Ctx* cx, float v) {
    float m = v < 0 ? -v : v;
    if (m >= 1e12f) {
        return StoryFmt(cx, "%.2fT", (double)(v / 1e12f));
    }
    if (m >= 1e9f) {
        return StoryFmt(cx, "%.2fB", (double)(v / 1e9f));
    }
    if (m >= 1e6f) {
        return StoryFmt(cx, "%.2fM", (double)(v / 1e6f));
    }
    if (m >= 1e3f) {
        return StoryFmt(cx, "%.2fK", (double)(v / 1e3f));
    }
    return StoryFmt(cx, "%.2f", (double)v);
}

// The delegate's match arm for one of the 38 columns past Chg%: which field
// of the row's quote it reads, and which renderer it goes through. Both the
// cell and the CSV go through here, the way Rust's render_td and cell_text
// read the same struct.
static DtCellVal DtValueFor(Ctx* cx, const Stock& s, int row, int col) {
    float price = StrToFloatUnchecked(Str(s.price));
    DtQuote q = DtQuoteFor(row, price);
    DtCellVal v;
    auto num = [&](Str t) {
        v.text = t;
        v.kind = DtCell::Number;
    };
    auto pct = [&](float val) {
        v.text = StoryFmt(cx, "%+.2f%%", (double)(val * 100.f));
        v.kind = DtCell::Percent;
        v.signal = val;
    };
    auto chg = [&](float val) {
        v.text = StoryFmt(cx, "%+.2f", (double)val);
        v.kind = DtCell::Change;
        v.signal = val;
    };
    auto fixed2 = [&](float val) { num(StoryFmt(cx, "%.2f", (double)val)); };
    auto rank = [&](float val) { num(StoryFmt(cx, "%.0f", (double)val)); };
    auto rate = [&](float val) {
        num(StoryFmt(cx, "%.2f%%", (double)(val * 100.f)));
    };
    switch (col) {
        case 7:
            num(DtCompact(cx, q.volume));
            break;
        case 8:
            num(DtCompact(cx, q.turnover));
            break;
        case 9:
            num(DtCompact(cx, q.marketCap));
            break;
        case 10:
            num(DtCompact(cx, q.ttm));
            break;
        case 11:
            rank(q.rank5m);
            break;
        case 12:
            rank(q.rank60d);
            break;
        case 13:
            pct(q.yearChangePercent);
            break;
        case 14:
            fixed2(q.bid);
            break;
        case 15:
            num(DtCompact(cx, q.bidVolume));
            break;
        case 16:
            fixed2(q.ask);
            break;
        case 17:
            num(DtCompact(cx, q.askVolume));
            break;
        case 18:
            fixed2(q.open);
            break;
        // prev_close is a change away from the price, which is the one field
        // that reads off the row's own Chg rather than off a draw.
        case 19:
            fixed2(price - StrToFloatUnchecked(Str(s.chg)));
            break;
        case 20:
            fixed2(q.high);
            break;
        case 21:
            fixed2(q.low);
            break;
        case 22:
            rate(q.turnoverRate);
            break;
        case 23:
            rate(q.riseRate);
            break;
        case 24:
            rate(q.amplitude);
            break;
        case 25:
            fixed2(q.pe);
            break;
        case 26:
            fixed2(q.pb);
            break;
        case 27:
            fixed2(q.volumeRatio);
            break;
        case 28:
            fixed2(q.bidAskRatio);
            break;
        case 29:
            fixed2(q.preClose);
            break;
        case 30:
            fixed2(q.postClose);
            break;
        case 31:
            num(DtCompact(cx, q.preMarketCap));
            break;
        case 32:
            pct(q.preMarketPercent);
            break;
        case 33:
            chg(q.preMarketChange);
            break;
        case 34:
            num(DtCompact(cx, q.postMarketCap));
            break;
        case 35:
            pct(q.postMarketPercent);
            break;
        case 36:
            chg(q.postMarketChange);
            break;
        case 37:
            num(DtCompact(cx, q.floatCap));
            break;
        case 38:
            num(DtCompact(cx, q.shares));
            break;
        case 39:
            num(DtCompact(cx, q.sharesFloat));
            break;
        case 40:
            rank(q.rank5d);
            break;
        case 41:
            rank(q.rank10d);
            break;
        case 42:
            rank(q.rank30d);
            break;
        case 43:
            rank(q.rank120d);
            break;
        default:
            rank(q.rank250d);
            break;
    }
    return v;
}

// render_td: the delegate's cell, which the table places and styles.
static El* DtCellFor(Ctx* cx, void* data, int row, int col) {
    DataTableStory* self = (DataTableStory*)data;
    const Theme& th = ThemeNow(cx->app);
    // The Rust story generates a row per index; ours repeats the fixed set,
    // so a table of five thousand rows is five thousand rows to scroll.
    const int nStocks = (int)(sizeof(kStocks) / sizeof(kStocks[0]));
    const Stock& s = kStocks[self->order[row % nStocks]];
    Rgba trend = s.up ? th.green : th.red;
    switch (col) {
        case 0:
            return StoryTxt(cx, StoryFmt(cx, "%d", row), 16, th.mutedFg)
                ->LineHeight(1.f);
        case 1:
            // render_td's market arm: US in blue and every other exchange
            // in magenta.
            return StoryTxt(
                       cx, Str(s.market), 16,
                       StrEq(Str(s.market), StrL("US")) ? th.blue : th.magenta)
                ->LineHeight(1.f);
        case 2:
            // The table clips the cell to its column, so a long name is cut
            // where the column ends however wide it has been dragged.
            return StoryTxt(cx, Str(s.name), 16, th.foreground)
                ->LineHeight(1.f);
        case 3:
            return StoryTxt(cx, Str(s.symbol), 16, th.foreground)
                ->Medium()
                ->LineHeight(1.f);
        case 4:
            return StoryTxt(cx, Str(s.price), 16, th.foreground)
                ->Semibold()
                ->LineHeight(1.f);
        case 5:
            return StoryTxt(cx, Str(s.chg), 16, trend)->LineHeight(1.f);
        case 6:
            // render_percent: the percentage is tinted over the whole cell,
            // the way a ticker table does, at 5% of the trend color.
            return Div(cx->a)
                ->FlexRow()
                ->W(kFill)
                ->H(kFill)
                ->ItemsCenter()
                ->JustifyEnd()
                ->Bg(RgbaOpacity(trend, 0.05f))
                ->Child(StoryTxt(cx, Str(s.pct), 16, trend)->LineHeight(1.f));
        default:
            break;
    }
    if (col >= kBaseColumns) {
        // The delegate has no field for a "Column N", so every one of them
        // reads the same.
        return StoryTxt(cx, StrL("--"), 16, th.mutedFg)->LineHeight(1.f);
    }
    DtCellVal v = DtValueFor(cx, s, row, col);
    if (v.kind == DtCell::Number) {
        return StoryTxt(cx, v.text, 16, th.foreground)->LineHeight(1.f);
    }
    // change_colors: a rise is green, a fall red, and an unchanged value
    // keeps the cell's own colour.
    bool flat = v.signal == 0;
    Rgba c = v.signal > 0 ? th.green : th.red;
    Rgba light = v.signal > 0 ? th.greenLight : th.redLight;
    if (v.kind == DtCell::Change) {
        return StoryTxt(cx, v.text, 16, flat ? th.foreground : c)
            ->LineHeight(1.f);
    }
    El* cell =
        Div(cx->a)->FlexRow()->W(kFill)->H(kFill)->ItemsCenter()->JustifyEnd();
    if (!flat) {
        cell->Bg(RgbaOpacity(light, 0.05f));
    }
    return cell->Child(StoryTxt(cx, v.text, 16, flat ? th.foreground : c)
                           ->LineHeight(1.f));
}

// visible_rows_changed / visible_columns_changed. Rust's note that these
// must be fast holds here for the same reason: they run while the frame's
// tree is being built, so a repaint from one would loop.
static void DtVisibleRows(Ctx* cx, void* data, int first, int end) {
    DataTableStory* self = (DataTableStory*)data;
    (void)cx;
    self->visRowFirst = first;
    self->visRowEnd = end;
}

static void DtVisibleCols(Ctx* cx, void* data, int first, int end) {
    DataTableStory* self = (DataTableStory*)data;
    (void)cx;
    self->visColFirst = first;
    self->visColEnd = end;
}

// cell_text: the same values render_td shows, as text. Rust keeps the two
// apart as well — one builds an element and the other a string, and only the
// string is what an export reads.
static Str DtCellText(Ctx* cx, void* data, int row, int col) {
    DataTableStory* self = (DataTableStory*)data;
    const int nStocks = (int)(sizeof(kStocks) / sizeof(kStocks[0]));
    const Stock& s = kStocks[self->order[row % nStocks]];
    switch (col) {
        case 0:
            return StoryFmt(cx, "%d", row);
        case 1:
            return Str(s.market);
        case 2:
            return Str(s.name);
        case 3:
            return Str(s.symbol);
        case 4:
            return Str(s.price);
        case 5:
            return Str(s.chg);
        case 6:
            return Str(s.pct);
        default:
            break;
    }
    if (col >= kBaseColumns) {
        return StrL("--");
    }
    return DtValueFor(cx, s, row, col).text;
}

El* DataTableStory::Render(DataTableStory* self, Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    Listener openMenu = Listen(cx, &DtMenuOpen);
    Listener act = Listen(cx, &DtMenuAct);
    if (!self->seeded) {
        self->seeded = true;
        self->table = EntityNewState<TableState>(cx->app);
    }
    El* page = Div(a)->FlexCol()->Gap(16)->W(kFill);

    // One group holding the size, rows, extra columns, options, go-to and
    // export controls.
    El* toolbarRow = Div(a)->FlexRow()->W(kFill)->JustifyEnd()->ItemsStart();
    El* group = StoryToolbarGroup(cx);
    StoryToolbarOpt sizeRows[kNSizes];
    for (int i = 0; i < kNSizes; i++) {
        sizeRows[i].label = kSizeLabels[i];
        sizeRows[i].checked = self->size == i;
        sizeRows[i].act = DtActSize + i;
    }
    group->Child(StoryToolbarDropdown(
        cx, StrL("dt-size"), StoryFmt(cx, "Size: %s", kSizeLabels[self->size]),
        self->openMenu == DtMenuSize, ListenerArg(openMenu, DtMenuSize),
        sizeRows, kNSizes, act));
    group->Child(StoryToolbarDivider(cx));
    StoryToolbarOpt rowRows[kNRowCounts];
    for (int i = 0; i < kNRowCounts; i++) {
        rowRows[i].label = kRowLabels[i];
        rowRows[i].checked = self->rowCount == i;
        rowRows[i].act = DtActRows + i;
    }
    group->Child(StoryToolbarDropdown(
        cx, StrL("dt-rows"),
        StoryFmt(cx, "Rows: %d", kRowCounts[self->rowCount]),
        self->openMenu == DtMenuRows, ListenerArg(openMenu, DtMenuRows),
        rowRows, kNRowCounts, act));
    group->Child(StoryToolbarDivider(cx));
    StoryToolbarOpt extraRows[kNExtraCounts];
    for (int i = 0; i < kNExtraCounts; i++) {
        extraRows[i].label = kExtraLabels[i];
        extraRows[i].checked = self->extra == i;
        extraRows[i].act = DtActExtra + i;
    }
    group->Child(StoryToolbarDropdown(
        cx, StrL("dt-extra"),
        StoryFmt(cx, "Extra Columns: %d", kExtraCounts[self->extra]),
        self->openMenu == DtMenuExtra, ListenerArg(openMenu, DtMenuExtra),
        extraRows, kNExtraCounts, act));
    group->Child(StoryToolbarDivider(cx));
    // The Options menu, with the two separators Rust puts in it and the
    // Clear Selection row under the last one.
    StoryToolbarOpt optRows[DtOptCount + 1];
    for (int i = 0; i < DtOptCount; i++) {
        optRows[i].label = kDtOptions[i];
        optRows[i].checked = self->options[i];
        optRows[i].act = DtActOption + i;
        optRows[i].sep = i == DtOptStriped;
    }
    optRows[DtOptCount].label = "Clear Selection";
    optRows[DtOptCount].act = DtActClear;
    optRows[DtOptCount].plain = true;
    optRows[DtOptCount].sep = true;
    group->Child(StoryToolbarDropdown(cx, StrL("dt-options"), StrL("Options"),
                                      self->openMenu == DtMenuOptions,
                                      ListenerArg(openMenu, DtMenuOptions),
                                      optRows, DtOptCount + 1, act));
    group->Child(StoryToolbarDivider(cx));
    StoryToolbarOpt goRows[kNGoTo];
    for (int i = 0; i < kNGoTo; i++) {
        goRows[i].label = kGoToRows[i];
        goRows[i].act = DtActGoTo + i;
        goRows[i].plain = true;
        goRows[i].sep = i == 2;
    }
    group->Child(StoryToolbarDropdown(
        cx, StrL("dt-go-to"), StrL("Go To"), self->openMenu == DtMenuGoTo,
        ListenerArg(openMenu, DtMenuGoTo), goRows, kNGoTo, act));
    group->Child(StoryToolbarDivider(cx));
    El* exportBtn =
        Div(a)
            ->H(24)
            ->PadX(8)
            ->ItemsCenter()
            ->JustifyCenter()
            ->HoverBg(th.tokens.muted)
            ->Child(StoryTxt(cx, StrL("Export CSV"), 14, th.foreground));
    component::BindClick(exportBtn, StrL("dt-dump-csv"), Listen(cx, &DtExport));
    group->Child(exportBtn);
    toolbarRow->Child(group);
    page->Child(toolbarRow);

    // The columns the Rust delegate declares, in its own order. The first
    // four are .fixed(ColumnFixed::Left); everything after them scrolls. The
    // width is what a column starts at — dragging its right edge makes the
    // width the table's own. Fields: title, width, right, sortable,
    // selectable, resizable, fixed.
    static const component::TableColumn kColumns[] = {
        {StrL("ID"), 60, false, false, false, true, true},
        {StrL("Market"), 60, false, false, true, true, true},
        {StrL("Name"), 180, false, false, true, true, true},
        {StrL("Symbol"), 100, false, true, true, true, true},
        {StrL("Price"), 100, true, true, true},
        {StrL("Chg"), 100, true, true, true},
        {StrL("Chg%"), 110, true, true, true},
        {StrL("Volume"), 100, true, false, true},
        {StrL("Turnover"), 100, true, false, true},
        {StrL("Market Cap"), 110, true, false, true},
        {StrL("TTM"), 100, true, false, true},
        {StrL("5m Ranking"), 110, true, false, true},
        {StrL("60d Ranking"), 110, true, false, true},
        {StrL("Year Chg%"), 110, true, false, true},
        {StrL("Bid"), 100, true, false, true},
        {StrL("Bid Vol"), 100, true, false, true},
        {StrL("Ask"), 100, true, false, true},
        {StrL("Ask Vol"), 100, true, false, true},
        {StrL("Open"), 100, true, false, true},
        {StrL("Prev Close"), 110, true, false, true},
        {StrL("High"), 100, true, false, true},
        {StrL("Low"), 100, true, false, true},
        {StrL("Turnover Rate"), 120, true, false, true},
        {StrL("Rise Rate"), 100, true, false, true},
        {StrL("Amplitude"), 110, true, false, true},
        {StrL("P/E"), 100, true, false, true},
        {StrL("P/B"), 100, true, false, true},
        {StrL("Volume Ratio"), 120, true, false, true},
        {StrL("Bid Ask Ratio"), 120, true, false, true},
        {StrL("Latest Pre Close"), 140, true, false, true},
        {StrL("Latest Post Close"), 140, true, false, true},
        {StrL("Pre Mkt Cap"), 120, true, false, true},
        {StrL("Pre Mkt%"), 100, true, false, true},
        {StrL("Pre Mkt Chg"), 120, true, false, true},
        {StrL("Post Mkt Cap"), 120, true, false, true},
        {StrL("Post Mkt%"), 110, true, false, true},
        {StrL("Post Mkt Chg"), 120, true, false, true},
        {StrL("Float Cap"), 110, true, false, true},
        {StrL("Shares"), 100, true, false, true},
        {StrL("Float Shares"), 120, true, false, true},
        {StrL("5d Ranking"), 110, true, false, true},
        {StrL("10d Ranking"), 110, true, false, true},
        {StrL("30d Ranking"), 110, true, false, true},
        {StrL("120d Ranking"), 120, true, false, true},
        {StrL("250d Ranking"), 120, true, false, true},
    };
    const int nBase = (int)(sizeof(kColumns) / sizeof(kColumns[0]));
    // columns_count(): the delegate's own plus however many extras the
    // toolbar asked for, each of which the delegate names "Column N".
    const int nExtra = kExtraCounts[self->extra];
    const int nColumns = nBase + nExtra;
    auto* cols = (component::TableColumn*)Alloc(
        a, (int)sizeof(component::TableColumn) * nColumns);
    for (int i = 0; i < nBase; i++) {
        cols[i] = kColumns[i];
    }
    for (int i = 0; i < nExtra; i++) {
        cols[nBase + i] = {StoryFmt(cx, "Column %d", i + 1), 100, false, false,
                           true};
    }
    TableState* st = self->table.Get(cx);
    if (st) {
        st->loopSelection = self->options[DtOptLoop];
        st->colFixed = self->options[DtOptFixedColumn];
        st->colResizable = self->options[DtOptColResize];
        // col_movable: whether a head can be dragged into another place.
        st->colMovable = self->options[DtOptColOrder];
        st->sortable = self->options[DtOptSortable];
        st->colSelectable = self->options[DtOptColSelect];
        st->rowSelectable = self->options[DtOptRowSelect];
        st->cellSelectable = self->options[DtOptCellSelect];
        st->rowHeader = self->options[DtOptRowHeader];
        st->loading = self->options[DtOptLoading];
        // lazy_load: has_more, so the table asks for another page when the
        // last rows it built come near the end.
        st->hasMore = self->options[DtOptLazyLoad];
        st->onEvent = Listen(cx, &OnTableEvent);
    }

    // group_headers: two levels of bands, each spanning every column, the
    // lower one subdividing the upper. The trailing band takes the extra
    // "Column N" columns with it, so both rows still cover the table.
    const int trailing = kExtraCounts[self->extra];
    const component::TableGroupCell kGroup1[] = {
        {StrL("Stock"), 4},          {StrL("Market Data"), 10},
        {StrL("Quotes"), 8},         {StrL("Stats"), 7},
        {StrL("Extended Hours"), 8}, {StrL("Shares & Rankings"), 8 + trailing},
    };
    const component::TableGroupCell kGroup2[] = {
        {StrL("Identity"), 4},   {StrL("Price & Change"), 3},
        {StrL("Turnover"), 4},   {StrL("Momentum"), 3},
        {StrL("Order Book"), 4}, {StrL("Session"), 4},
        {StrL("Activity"), 3},   {StrL("Valuation"), 2},
        {StrL("Ratios"), 2},     {StrL("Pre & Post Market"), 8},
        {StrL("Shares"), 3},     {StrL("Rankings"), 5 + trailing},
    };

    component::DataTable* table =
        component::DataTable::New(cx, StrL("data-table"), self->table)
            ->Columns(cols, nColumns)
            ->Rows(kRowCounts[self->rowCount], self, DtCellFor)
            // The story's table is `v_flex().min_h_0().flex_1()`, so the body
            // takes what the pane has left over the toolbar, the gap and the
            // status line under it. Virtualization needs that as a number
            // before the tree is laid out, so it comes off the window.
            ->H(WindowSize(cx->win).dipH - 368)
            ->RowHeight(kSizeRowH[self->size])
            ->Stripe(self->options[DtOptStriped])
            ->ContextMenu(DtContextMenu)
            ->OnVisibleRows(DtVisibleRows)
            ->OnVisibleCols(DtVisibleCols)
            ->CellText(DtCellText);
    if (self->options[DtOptGroupHeaders]) {
        table->GroupHeader(kGroup1, 6)->GroupHeader(kGroup2, 12);
    }
    if (self->wantExport) {
        self->wantExport = false;
        Vec<Str> heads, cells;
        table->Dump(&heads, &cells);
        StrBuilder sb;
        for (int i = 0; i < len(heads) && i < 4; i++) {
            sb.Append(i ? StrL(", ") : StrL(""));
            sb.Append(heads[i]);
        }
        Str headLine = sb.TakeStr();
        StrFree(self->message);
        self->message = StrDup(fmt(
            "Dumped %d rows × %d columns · %s, … · "
            "first row: %s, %s, %s",
            kRowCounts[self->rowCount], nColumns, headLine,
            cells.len > 0 ? cells[0] : Str(), cells.len > 1 ? cells[1] : Str(),
            cells.len > 2 ? cells[2] : Str()));
        StrFree(headLine);
        VecReset(heads);
        VecReset(cells);
    }
    El* box = table->IntoEl();

    // The status line under the table: min_h_9, px_3, muted at 35%, text_xs.
    El* status = Div(a)
                     ->FlexRow()
                     ->W(kFill)
                     ->MinH(36)
                     ->PadX(12)
                     ->Gap(12)
                     ->JustifyBetween()
                     ->ItemsCenter()
                     ->Bg(RgbaOpacity(th.muted, 0.35f))
                     ->Font(12)
                     ->Fg(th.mutedFg);
    status->Child(TextEl(a, StoryFmt(cx, "Total · %d rows · %d columns",
                                     kRowCounts[self->rowCount], nColumns)));
    El* right = Div(a)->FlexRow()->Gap(8)->ItemsCenter()->JustifyEnd();
    if (self->options[DtOptLoading]) {
        right->Child(
            component::Spinner::New(cx)->WithSize(UiSize::XSmall)->IntoEl());
    }
    // Both ranges are the table's now, handed over as it built: the story
    // no longer works out for itself which rows are on screen.
    right
        ->Child(TextEl(a, StoryFmt(cx, "Current · rows %d..%d · columns %d..%d",
                                   self->visRowFirst, self->visRowEnd,
                                   self->visColFirst, self->visColEnd)));
    if (st && st->selectedCellRow >= 0) {
        right->Child(TextEl(a, StoryFmt(cx, "· cell %d:%d", st->selectedCellRow,
                                        st->selectedCellCol)));
    }
    // eof, which set_stocks writes as `stocks.len() <= 50`: the dataset is
    // small enough that there is nothing left to page in. It is not the
    // lazy-load switch, which only says whether the table may ask for more.
    if (kRowCounts[self->rowCount] <= 50) {
        right->Child(TextEl(a, StrL("· complete")));
    }
    status->Child(right);
    // The table and the line under it share one `v_flex().min_h_0().flex_1()`
    // with no gap of its own; the page's gap_4 is between the toolbar and it.
    page->Child(Div(a)->FlexCol()->W(kFill)->Child(box)->Child(status));
    if (self->message.s) {
        page->Child(StoryTxt(cx, self->message, 14, th.mutedFg));
    }
    return page;
}

STORY_PAGE(StoryDataTable, DataTableStory);
