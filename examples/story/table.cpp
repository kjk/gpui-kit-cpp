#include "Story.h"

struct Invoice {
    const char* id;
    const char* status;
    const char* method;
    const char* method2; // the second line of a two-line method cell
    const char* amount;
    const char* date;
};

static const Invoice kInvoices[] = {
    {"INV001", "Paid", "Credit Card", nullptr, "$250.00", "2024-01-15"},
    {"INV002", "Pending", "PayPal", nullptr, "$150.00", "2024-02-01"},
    {"INV003", "Unpaid", "Bank Transfer", nullptr, "$350.00", "2024-02-15"},
    {"INV004", "Paid", "Credit Card", "Master Card / Visa", "$450.00",
     "2024-03-01"},
    {"INV005", "Paid", "PayPal", nullptr, "$550.00", "2024-03-15"},
    {"INV006", "Pending", "Bank Transfer", nullptr, "$200.00", "2024-04-01"},
    {"INV007", "Unpaid", "Credit Card", nullptr, "$300.00", "2024-04-15"},
};

struct TableStory {
    StoryToolbarState toolbar;

    static El* Render(TableStory* self, Ctx* cx);
};

// status_tag(): an xsmall outline tag in the status color.
static El* StatusTag(Ctx* cx, Str status) {
    component::Tag* tag =
        component::Tag::New(cx, status)->Outline()->WithSize(UiSize::XSmall);
    if (StrEq(status, StrL("Paid"))) {
        tag->Success();
    } else if (StrEq(status, StrL("Pending"))) {
        tag->Warning();
    } else if (StrEq(status, StrL("Unpaid"))) {
        tag->Danger();
    }
    return tag->IntoEl();
}

// A cell holding one line of text. A Table is `.text_sm()` throughout and
// colour is not inherited here, so each run names its own.
static component::TableCellEl* TextHead(Ctx* cx, Str text) {
    return component::TableHead::New(cx)
        ->Child(StoryTxt(cx, text, 14, ThemeNow(cx->app).tableHeadFg));
}

static component::TableCellEl* TextCell(Ctx* cx, Str text) {
    return component::TableCell::New(cx)
        ->Child(StoryTxt(cx, text, 14, ThemeNow(cx->app).foreground));
}

El* TableStory::Render(TableStory* self, Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    UiSize size = self->toolbar.size;
    const int nInvoices = (int)(sizeof(kInvoices) / sizeof(kInvoices[0]));
    El* page = Div(a)->FlexCol()->Gap(24)->W(kFill);
    page->Child(StoryToolbar(cx, self));

    El* def = StorySection(cx, "Default", nullptr);
    StorySectionBody(def)->W(kFill);
    component::Table* table = component::Table::New(cx, StrL("invoices"))
                                  ->WithSize(size);
    table->Child(component::TableHeader::New(cx)->Child(
        component::TableRow::New(cx)
            ->Child(TextHead(cx, StrL("Invoice"))->W(150))
            ->Child(TextHead(cx, StrL("Status"))->ColSpan(2))
            ->Child(TextHead(cx, StrL("Amount"))->TextRight())
            ->Child(TextHead(cx, StrL("Date"))->TextRight())));
    component::TableGroup* body = component::TableBody::New(cx);
    StateStyle hover;
    hover.Bg(th.tokens.tableHover);
    for (int i = 0; i < nInvoices; i++) {
        const Invoice& inv = kInvoices[i];
        // The method cell is one string with a newline in it upstream, which
        // is two runs in a column here.
        component::TableCellEl* method = component::TableCell::New(cx);
        El* methodCol = Div(a)->FlexCol();
        methodCol->Child(StoryTxt(cx, Str(inv.method), 14, th.foreground));
        if (inv.method2) {
            methodCol->Child(StoryTxt(cx, Str(inv.method2), 14, th.foreground));
        }
        method->Child(methodCol);
        body->Child(component::TableRow::New(cx)
                        ->Hover(hover)
                        ->Child(TextCell(cx, Str(inv.id))->W(150))
                        ->Child(component::TableCell::New(cx)
                                    ->Child(StatusTag(cx, Str(inv.status))))
                        ->Child(method)
                        ->Child(TextCell(cx, Str(inv.amount))->TextRight())
                        ->Child(TextCell(cx, Str(inv.date))->TextRight()));
    }
    table->Child(body);
    table->Child(component::TableFooter::New(cx)->Child(
        component::TableRow::New(cx)
            ->Child(component::TableCell::New(cx)->ColSpan(3)->Child(
                StoryTxt(cx, StrL("Total"), 14, th.tableFootFg)))
            ->Child(
                component::TableCell::New(cx)->ColSpan(2)->TextRight()->Child(
                    StoryTxt(cx, StrL("$2,250.00"), 14, th.tableFootFg)))));
    table->Child(component::TableCaption::New(cx)->Child(
        StoryTxt(cx, StrL("A list of your recent invoices."), 14, th.mutedFg)));
    StorySectionAdd(def, table->IntoEl());
    page->Child(def);

    El* bordered = StorySection(cx, "Bordered", nullptr);
    StorySectionBody(bordered)->W(kFill);
    component::Table* box = component::Table::New(cx, StrL("invoices-bordered"))
                                ->WithSize(size)
                                ->Bordered();
    box->Child(component::TableHeader::New(cx)->Child(
        component::TableRow::New(cx)
            ->Child(TextHead(cx, StrL("Invoice"))->W(100))
            ->Child(TextHead(cx, StrL("Method")))
            ->Child(TextHead(cx, StrL("Amount"))->TextRight())
            ->Child(TextHead(cx, StrL("Date"))->TextRight())));
    component::TableGroup* bbody = component::TableBody::New(cx);
    for (int i = 0; i < 6; i++) {
        const Invoice& inv = kInvoices[i];
        component::TableCellEl* method = component::TableCell::New(cx);
        El* methodCol = Div(a)->FlexCol();
        methodCol->Child(StoryTxt(cx, Str(inv.method), 14, th.foreground));
        if (inv.method2) {
            methodCol->Child(StoryTxt(cx, Str(inv.method2), 14, th.foreground));
        }
        method->Child(methodCol);
        component::TableRow* row =
            component::TableRow::New(cx)
                ->Hover(hover)
                ->Child(TextCell(cx, Str(inv.id))->W(100))
                ->Child(method)
                ->Child(TextCell(cx, Str(inv.amount))->TextRight())
                ->Child(TextCell(cx, Str(inv.date))->TextRight());
        if (i % 2 != 0) {
            row->Bg(th.tokens.tableEven);
        }
        bbody->Child(row);
    }
    box->Child(bbody);
    StorySectionAdd(bordered, box->IntoEl());
    page->Child(bordered);
    return page;
}

STORY_PAGE(StoryTable, TableStory);
