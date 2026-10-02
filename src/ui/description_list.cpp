#include "ui/description_list.h"

namespace gpui {

namespace component {

DescriptionText DescriptionText::From(Str text) {
    DescriptionText out;
    out.string = text;
    return out;
}

DescriptionText DescriptionText::Text(El* text) {
    DescriptionText out;
    out.kind = DescriptionTextKind::Text;
    out.element = text;
    return out;
}

DescriptionText DescriptionText::AnyElement(El* element) {
    DescriptionText out;
    out.kind = DescriptionTextKind::AnyElement;
    out.element = element;
    return out;
}

El* DescriptionText::IntoEl(Ctx* cx) const {
    if (kind == DescriptionTextKind::String) {
        return Div(cx->a)->Child(TextEl(cx->a, string)->Wrap());
    }
    return element ? element : Div(cx->a);
}

DescriptionItem DescriptionItem::New(DescriptionText labelText) {
    DescriptionItem out;
    out.label = labelText;
    out.value = DescriptionText::From(Str{});
    return out;
}

DescriptionItem DescriptionItem::Separator() {
    DescriptionItem out;
    out.separator = true;
    return out;
}

DescriptionItem& DescriptionItem::Value(DescriptionText valueText) {
    if (!separator) {
        value = valueText;
    }
    return *this;
}

DescriptionItem& DescriptionItem::Span(int spanValue) {
    if (!separator) {
        // Rust's public value is usize. Preserve zero (which is meaningful
        // to flex-basis) but do not let the signed C++ seam invent a value
        // the source API cannot express.
        span = spanValue < 0 ? 0 : spanValue;
    }
    return *this;
}

int DescriptionGroupRows(const DescriptionItem* items, int n, int columns,
                         int* rowCounts, int capacity) {
    if (!items || n <= 0) {
        return 0;
    }
    if (columns < 1) {
        columns = 1;
    } else if (columns > 10) {
        columns = 10;
    }
    int rows = 0;
    int currentSpan = 0;
    auto addRow = [&]() {
        if (rowCounts && rows < capacity) {
            rowCounts[rows] = 0;
        }
        rows++;
        currentSpan = 0;
    };
    for (int i = 0; i < n; i++) {
        int span = items[i].separator ? columns : items[i].span;
        if (rows == 0) {
            addRow();
        }
        if (currentSpan + span > columns) {
            addRow();
        }
        if (rowCounts && rows - 1 < capacity) {
            rowCounts[rows - 1]++;
        }
        currentSpan += span;
    }
    // Rust removes empty rows only from the end. The algorithm above cannot
    // produce a trailing empty one, but it can deliberately retain a leading
    // empty row for an over-wide first item.
    return rows;
}

DescriptionList* DescriptionList::New(Ctx* cx) {
    Arena* a = cx->a;
    DescriptionList* d = ArenaNew<DescriptionList>(a);
    d->a = a;
    d->cx = cx;
    return d;
}

DescriptionList* DescriptionList::Horizontal(Ctx* cx) {
    return New(cx);
}

DescriptionList* DescriptionList::Vertical(Ctx* cx) {
    return New(cx)->Vertical();
}

DescriptionList* DescriptionList::Item(Str label, Str value, int span) {
    return Item(DescriptionText::From(label), DescriptionText::From(value),
                span);
}

DescriptionList* DescriptionList::Item(DescriptionText label,
                                       DescriptionText value, int span) {
    DescriptionItem item = DescriptionItem::New(label);
    item.Value(value).Span(span);
    return Child(item);
}

DescriptionList* DescriptionList::ItemEl(Str label, El* value, int span) {
    return Item(DescriptionText::From(label),
                DescriptionText::AnyElement(value), span);
}

DescriptionList* DescriptionList::Child(const DescriptionItem& item) {
    items.Append(a, item);
    return this;
}

DescriptionList* DescriptionList::Separator() {
    return Child(DescriptionItem::Separator());
}

DescriptionList* DescriptionList::Columns(int v) {
    columns = v < 1 ? 1 : (v > 10 ? 10 : v);
    return this;
}
DescriptionList* DescriptionList::LabelWidth(float w) {
    labelWidth = w;
    return this;
}
DescriptionList* DescriptionList::Bordered(bool v) {
    bordered = v;
    return this;
}
DescriptionList* DescriptionList::Layout(Axis axis) {
    vertical = axis == Axis::Vertical;
    return this;
}
DescriptionList* DescriptionList::Vertical(bool v) {
    vertical = v;
    return this;
}
DescriptionList* DescriptionList::WithSize(UiSize s) {
    size = s;
    return this;
}

El* DescriptionList::IntoEl() {
    const Theme& th = ThemeNow(cx->app);
    // Medium: px 8 / py 4 inside a bordered box; unbordered drops the padding
    // and spaces the rows by the base gap instead.
    float padX = 8, padY = 4;
    if (size == UiSize::Small || size == UiSize::XSmall) {
        padX = 4;
        padY = 2;
    } else if (size == UiSize::Large) {
        padX = 12;
        padY = 6;
    }
    float baseGap =
        size == UiSize::Large ? 8.f : (size == UiSize::Medium ? 4.f : 2.f);
    float gap = bordered ? 0.f : baseGap;
    if (!bordered) {
        padX = 0;
        padY = 0;
    }

    El* root = Div(a)->FlexCol()->Gap(gap)->ClipX()->ClipY();
    if (bordered) {
        root->Border(1, th.border)->Radius(th.radius);
    }

    int* rowCounts = (int*)Alloc(a, (int)sizeof(int) * (items.len + 1));
    DescriptionItem* flatItems = items.Flatten(a);
    int rows = DescriptionGroupRows(flatItems, items.len, columns, rowCounts,
                                    items.len + 1);
    int itemAt = 0;
    for (int rowIx = 0; rowIx < rows; rowIx++) {
        int count = rowCounts ? rowCounts[rowIx] : 0;
        El* row = Div(a)->FlexRow()->ItemsCenter();
        bool last = rowIx == rows - 1;
        if (bordered && !last) {
            row->BorderB(1, th.border);
        }
        for (int k = 0; k < count; k++) {
            const DescriptionItem& it = items[itemAt++];
            if (it.separator) {
                // h_2().w_full()
                El* separator = Div(a)->H(Rems(cx, 0.5f))->W(kFill);
                if (bordered) {
                    separator->Bg(th.descListLabel);
                }
                row->Child(separator);
                continue;
            }
            // flex_1().flex_basis(relative(span / columns)), and the value's
            // flex_1() is relative(0.) too: in a list sized to its content
            // neither percentage resolves, so each starts from its text.
            El* cell = Div(a)
                           ->Flex1Rel()
                           ->BasisFrac((float)it.span / (float)columns)
                           ->ClipX();
            cell = vertical ? cell->FlexCol() : cell->FlexRow()->H(kFill);
            El* label = Div(a)
                            ->PadX(padX)
                            ->PadY(padY)
                            ->Font(14)
                            ->Fg(th.descListLabelFg)
                            ->Child(it.label.IntoEl(cx));
            if (!vertical) {
                label->W(labelWidth)->H(kFill)->Shrink0();
            } else {
                label->W(kFill);
            }
            if (bordered) {
                label->Bg(th.descListLabel);
                if (vertical) {
                    label->BorderB(1, th.border);
                } else {
                    label->BorderR(1, th.border);
                    if (k != 0) {
                        label->BorderL(1, th.border);
                    }
                }
            }
            El* value =
                Div(a)->Flex1Rel()->PadX(padX)->PadY(padY)->ClipX()->ClipY();
            value->Child(it.value.IntoEl(cx));
            cell->Child(label)->Child(value);
            row->Child(cell);
        }
        root->Child(row);
    }
    return root;
}

} // namespace component
} // namespace gpui
