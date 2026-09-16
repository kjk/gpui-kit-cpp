#include "ui/form.h"

namespace gpui {

namespace component {

FieldBuilder FieldBuilder::String(Str value) {
    FieldBuilder builder;
    builder.kind = FieldBuilderKind::String;
    builder.string = value;
    return builder;
}

FieldBuilder FieldBuilder::Element(El* value) {
    FieldBuilder builder;
    builder.kind = value ? FieldBuilderKind::Element : FieldBuilderKind::None;
    builder.element = value;
    return builder;
}

Field Field::New(El* control) {
    Field result;
    result.control = control;
    return result;
}
Field& Field::Label(Str value) {
    label = FieldBuilder::String(value);
    return *this;
}
Field& Field::Label(El* value) {
    label = FieldBuilder::Element(value);
    return *this;
}
Field& Field::Description(Str value) {
    description = FieldBuilder::String(value);
    return *this;
}
Field& Field::Description(El* value) {
    description = FieldBuilder::Element(value);
    return *this;
}
Field& Field::Required(bool value) {
    required = value;
    return *this;
}
Field& Field::Visible(bool value) {
    visible = value;
    return *this;
}
Field& Field::LabelIndent(bool value) {
    labelIndent = value;
    return *this;
}
Field& Field::Align(FieldAlign value) {
    align = value;
    return *this;
}
Field& Field::ColSpan(int value) {
    colSpan = value > 0 ? value : 1;
    return *this;
}
Field& Field::ColStart(int value) {
    colStart = value;
    return *this;
}
Field& Field::ColEnd(int value) {
    colEnd = value;
    return *this;
}

Form* Form::New(Ctx* cx) {
    Arena* a = cx->a;
    Form* f = ArenaNew<Form>(a);
    f->a = a;
    f->cx = cx;
    return f;
}
Form* Form::Child(const component::Field& field) {
    fields.Append(a, field);
    return this;
}
Form* Form::Field(Str label, El* control) {
    FormField field = {};
    field.label = FieldBuilder::String(label);
    field.control = control;
    fields.Append(a, field);
    return this;
}
Form* Form::FieldEl(El* label, El* control) {
    FormField field = {};
    field.label = FieldBuilder::Element(label);
    field.control = control;
    fields.Append(a, field);
    return this;
}

// Both apply to the field added last, the way the Rust builder chains onto
// the field it is building.
Form* Form::Required(bool v) {
    if (fields.len > 0) {
        fields[fields.len - 1].required = v;
    }
    return this;
}
Form* Form::Description(Str s) {
    if (fields.len > 0) {
        fields[fields.len - 1].description = FieldBuilder::String(s);
    }
    return this;
}
Form* Form::DescriptionEl(El* e) {
    if (fields.len > 0) {
        fields[fields.len - 1].description = FieldBuilder::Element(e);
    }
    return this;
}
Form* Form::SpanAll(bool v) {
    if (fields.len > 0) {
        fields[fields.len - 1].colSpan = v ? 0x7fff : 1;
    }
    return this;
}
Form* Form::Visible(bool v) {
    if (fields.len > 0) {
        fields[fields.len - 1].visible = v;
    }
    return this;
}
Form* Form::LabelIndent(bool v) {
    if (fields.len > 0) {
        fields[fields.len - 1].labelIndent = v;
    }
    return this;
}
Form* Form::Align(FieldAlign v) {
    if (fields.len > 0) {
        fields[fields.len - 1].align = v;
    }
    return this;
}
Form* Form::WithSize(UiSize v) {
    size = v;
    return this;
}
Form* Form::LabelTextSize(float px) {
    labelTextSize = px;
    return this;
}

Form* Form::Horizontal(bool v) {
    horizontal = v;
    return this;
}
Form* Form::LabelLayout(Axis axis) {
    return Horizontal(axis == Axis::Horizontal);
}
Form* Form::Columns(int c) {
    columns = c < 1 ? 1 : c;
    return this;
}
Form* Form::Footer(El* content) {
    footer = content;
    return this;
}
Form* Form::LabelWidth(float w) {
    labelWidth = w;
    return this;
}

El* Form::IntoEl() {
    const Theme& th = ThemeNow(cx->app);
    // The gap comes from the size: eight at Large, four otherwise, and a
    // vertical field halves it between the label and the control.
    float formGap = size == UiSize::Large                               ? 12.f
                    : (size == UiSize::XSmall || size == UiSize::Small) ? 6.f
                                                                        : 8.f;
    float fieldGap = size == UiSize::Large ? 8.f : 4.f;
    float inner = horizontal ? fieldGap : fieldGap * 0.5f;
    float labelFont = labelTextSize > 0 ? labelTextSize : 14.f;
    float lw = labelWidth > 0 ? labelWidth : 140.f;

    El* col = Div(a)->FlexCol()->W(kFill)->GapX(formGap * 3.f)->GapY(formGap);
    El* row = nullptr; // the row being filled, when the form has columns
    int inRow = 0;
    for (int i = 0; i < fields.len; i++) {
        const FormField& fld = fields[i];
        // visible(false): the field is left out of the form entirely.
        if (!fld.visible) {
            continue;
        }
        El* f = Div(a)->FlexCol()->W(kFill)->Gap(fieldGap * 0.5f);

        El* head = Div(a)->W(kFill)->Gap(inner);
        if (horizontal) {
            head->FlexRow();
            if (fld.align == FieldAlign::Start) {
                head->ItemsStart();
            } else if (fld.align == FieldAlign::End) {
                head->ItemsEnd();
            } else {
                head->ItemsCenter();
            }
        } else {
            head->FlexCol();
        }
        bool hasLabel = fld.labelIndent;
        if (hasLabel) {
            El* label = Div(a)->FlexRow()->Gap(4)->ItemsCenter();
            if (horizontal) {
                label->W(lw);
                label->style.flexShrink = 0;
            }
            if (fld.label.kind == FieldBuilderKind::Element) {
                label->Child(fld.label.element);
            } else if (fld.label.kind == FieldBuilderKind::String) {
                label->Child(TextEl(a, fld.label.string)
                                 ->Font(labelFont)
                                 ->Medium()
                                 ->Fg(th.foreground));
            }
            if (fld.required && fld.label.IsSet()) {
                label->Child(
                    TextEl(a, StrL("*"))->Font(labelFont)->Fg(th.danger));
            }
            head->Child(label);
        }
        El* control = Div(a)->W(kFill)->Flex1();
        if (fld.control) {
            control->Child(fld.control);
        }
        head->Child(control);
        f->Child(head);

        // Rust always adds the description row, so a field without one is
        // still a half-gap taller.
        El* desc = Div(a)->FlexRow()->W(kFill)->Gap(inner);
        if (fld.description.IsSet()) {
            // Horizontal, the description lines up under the control.
            if (horizontal && hasLabel) {
                desc->Child(Div(a)->W(lw));
            }
            if (fld.description.kind == FieldBuilderKind::Element) {
                desc->Child(fld.description.element);
            } else {
                desc->Child(TextEl(a, fld.description.string)
                                ->Font(12)
                                ->Fg(th.mutedFg));
            }
        }
        f->Child(desc);

        if (columns <= 1) {
            col->Child(f);
            continue;
        }
        // A grid of `columns` cells per row, with the wide fields on their own.
        int span = fld.colSpan < 1 ? 1 : fld.colSpan;
        if (span >= columns) {
            row = nullptr;
            inRow = 0;
            col->Child(f);
            continue;
        }
        if (row && inRow + span > columns) {
            for (int j = inRow; j < columns; j++) {
                row->Child(Div(a)->Flex1());
            }
            row = nullptr;
            inRow = 0;
        }
        if (!row) {
            row = Div(a)->FlexRow()->W(kFill)->Gap(formGap * 3.f)->ItemsStart();
            col->Child(row);
            inRow = 0;
        }
        El* cell = Div(a)->Child(f);
        cell->style.flexGrow = (float)span;
        cell->style.flexBasis = 0;
        row->Child(cell);
        inRow += span;
        if (inRow >= columns) {
            row = nullptr;
        }
    }
    // Pad the last row so a lone field keeps its column width.
    if (row) {
        for (int i = inRow; i < columns; i++) {
            row->Child(Div(a)->Flex1());
        }
    }
    if (footer) {
        col->Child(
            Div(a)->FlexRow()->W(kFill)->MinW(0)->JustifyEnd()->Child(footer));
    }
    return col;
}

Form* v_form(Ctx* cx) {
    return Form::New(cx)->Horizontal(false);
}

Form* h_form(Ctx* cx) {
    return Form::New(cx)->Horizontal(true);
}

component::Field field(El* control) {
    return component::Field::New(control);
}

} // namespace component
} // namespace gpui
