/* crates/ui/src/description_list.rs: row grouping and render structure. */

#include "Test.h"

using namespace gpui::component;

static DescriptionItem DlItem(Str label, int span = 1) {
    DescriptionItem item = DescriptionItem::New(DescriptionText::From(label));
    item.Value(DescriptionText::From(StrL("value"))).Span(span);
    return item;
}

static void GroupsSpansLikeRust() {
    DescriptionItem items[7] = {
        DlItem(StrL("test1")), DlItem(StrL("test2"), 2),
        DlItem(StrL("test3")), DlItem(StrL("test4")),
        DlItem(StrL("test5")), DlItem(StrL("test6"), 3),
        DlItem(StrL("test7")),
    };
    int counts[8] = {};
    int rows = DescriptionGroupRows(items, 7, 3, counts, 8);

    utassert(rows == 4);
    utassert(counts[0] == 2);
    utassert(counts[1] == 3);
    utassert(counts[2] == 1);
    utassert(counts[3] == 1);
}

static void SeparatorOccupiesItsOwnFullSpanRow() {
    DescriptionItem items[3] = {
        DlItem(StrL("before")),
        DescriptionItem::Separator(),
        DlItem(StrL("after")),
    };
    int counts[4] = {};
    int rows = DescriptionGroupRows(items, 3, 3, counts, 4);

    utassert(rows == 3);
    utassert(counts[0] == 1);
    utassert(counts[1] == 1);
    utassert(counts[2] == 1);
}

static void BuildersPreserveVariantsAndLimits() {
    App app = {};
    component::Init(&app);
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.app = &app;
    cx.a = a;

    El* text = TextEl(a, StrL("text"));
    El* any = Div(a)->Id(StrL("any"));
    DescriptionText stringValue = DescriptionText::From(StrL("string"));
    DescriptionText textValue = DescriptionText::Text(text);
    DescriptionText anyValue = DescriptionText::AnyElement(any);
    utassert(stringValue.kind == DescriptionTextKind::String);
    utassert(textValue.kind == DescriptionTextKind::Text);
    utassert(anyValue.kind == DescriptionTextKind::AnyElement);
    utassert(textValue.IntoEl(&cx) == text);
    utassert(anyValue.IntoEl(&cx) == any);

    DescriptionList* low = DescriptionList::New(&cx)->Columns(0);
    DescriptionList* high = DescriptionList::New(&cx)->Columns(99);
    utassert(low->columns == 1);
    utassert(high->columns == 10);
    utassert(!DescriptionList::Horizontal(&cx)->vertical);
    utassert(DescriptionList::Vertical(&cx)->vertical);

    DescriptionItem signedSpan = DlItem(StrL("signed"));
    signedSpan.Span(-1);
    utassert(signedSpan.span == 0);

    AppGlobalClear(&app);
    ArenaDelete(a);
}

static void RenderKeepsFractionalCellsAndSeparators() {
    App app = {};
    component::Init(&app);
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.app = &app;
    cx.a = a;

    DescriptionList* list = DescriptionList::New(&cx)->Columns(3);
    list->Item(StrL("first"), StrL("one"))
        ->Item(StrL("second"), StrL("two"), 2)
        ->Separator()
        ->Item(StrL("last"), StrL("three"));
    El* root = list->IntoEl();

    El* firstRow = root->first;
    El* separatorRow = firstRow ? firstRow->next : nullptr;
    El* lastRow = separatorRow ? separatorRow->next : nullptr;
    utassert(firstRow != nullptr);
    utassert(separatorRow != nullptr);
    utassert(lastRow != nullptr);
    utassert(lastRow && lastRow->next == nullptr);
    utassert(root->style.overflowX == Overflow::Hidden);
    utassert(root->style.overflowY == Overflow::Hidden);

    El* firstCell = firstRow ? firstRow->first : nullptr;
    El* secondCell = firstCell ? firstCell->next : nullptr;
    utassert(firstCell != nullptr);
    utassert(secondCell != nullptr);
    utassertnear(firstCell ? firstCell->style.flexBasisFrac : 0, 1.f / 3.f);
    utassertnear(secondCell ? secondCell->style.flexBasisFrac : 0, 2.f / 3.f);
    utassert(firstCell && firstCell->style.overflowX == Overflow::Hidden);
    utassert(firstCell && firstCell->style.height == kFill);

    El* firstLabel = firstCell ? firstCell->first : nullptr;
    El* secondLabel = secondCell ? secondCell->first : nullptr;
    utassert(firstLabel && firstLabel->style.borderR == 1);
    utassert(firstLabel && firstLabel->style.borderL == 0);
    utassert(secondLabel && secondLabel->style.borderR == 1);
    utassert(secondLabel && secondLabel->style.borderL == 1);

    El* separator = separatorRow ? separatorRow->first : nullptr;
    utassert(separator != nullptr);
    utassert(separator && separator->style.height == 8);
    utassert(separator && separator->style.width == kFill);
    utassert(separator && separator->style.hasBg);

    AppGlobalClear(&app);
    ArenaDelete(a);
}

void TestDescriptionList() {
    TestSuite("description_list");
    GroupsSpansLikeRust();
    SeparatorOccupiesItsOwnFullSpanRow();
    BuildersPreserveVariantsAndLimits();
    RenderKeepsFractionalCellsAndSeparators();
}
