#ifndef GPUI_UI_FORM_H_
#define GPUI_UI_FORM_H_
/* Themed form — crates/ui/src/form */

#include "ui/sizing.h"

namespace gpui {

namespace component {

// items_start / items_center / items_end: how the label and the control line
// up against each other in a horizontal field.
enum class FieldAlign : uint8_t {
    Center,
    Start,
    End
};

enum class FieldBuilderKind : uint8_t {
    None,
    String,
    Element
};

// field.rs's string/element/view union. Views render to El in this runtime,
// so Element is the shared no-refcount representation of the latter two.
struct FieldBuilder {
    FieldBuilderKind kind = FieldBuilderKind::None;
    Str string = {};
    El* element = nullptr;

    static FieldBuilder String(Str value);
    static FieldBuilder Element(El* value);
    bool IsSet() const { return kind != FieldBuilderKind::None; }
};

struct Field {
    FieldBuilder label = {};
    El* control = nullptr;
    // description sits under the control; required draws the danger asterisk
    // next to the label.
    FieldBuilder description = {};
    bool required = false;
    int colSpan = 1;
    int colStart = -1;
    int colEnd = -1;
    // visible(false): the field is built and then left out, which is what
    // lets a form keep its shape while a field comes and goes.
    bool visible = true;
    // label_indent(false): no label column at all, so the control starts at
    // the form's edge.
    bool labelIndent = true;
    FieldAlign align = FieldAlign::Center;

    static Field New(El* control = nullptr);
    Field& Label(Str value);
    Field& Label(El* value);
    Field& Description(Str value);
    Field& Description(El* value);
    Field& Required(bool value = true);
    Field& Visible(bool value);
    Field& LabelIndent(bool value);
    Field& Align(FieldAlign value);
    Field& ColSpan(int value);
    Field& ColStart(int value);
    Field& ColEnd(int value);
};

using FormField = Field;

struct Form {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    ArenaVec<FormField> fields;
    bool horizontal = false;
    int columns = 1;
    float labelWidth = 0; // 0: 140, or 100 in a multi-column form
    // The size the gaps come from: Large gaps by eight, everything else by
    // four, and a vertical field halves it between its parts.
    UiSize size = UiSize::Medium;
    // label_text_size. 0 keeps text_sm.
    float labelTextSize = 0;
    El* footer = nullptr;

    static Form* New(Ctx* cx);
    Form* Child(const component::Field& field);
    // A label-less field spans the row on its own, the way Rust's
    // `field().label_indent(false)` does.
    Form* Field(Str label, El* control);
    // A field with a label the caller built rather than a string.
    Form* FieldEl(El* label, El* control);
    Form* Required(bool v = true);
    Form* Description(Str s);
    Form* DescriptionEl(El* e);
    Form* SpanAll(bool v = true);
    Form* Visible(bool v);
    Form* LabelIndent(bool v);
    Form* Align(FieldAlign v);
    // Label/control orientation, independent of the field column count.
    Form* LabelLayout(Axis axis);
    Form* Horizontal(bool v = true);
    Form* Columns(int n);
    // Full-width trailing content after the fields. A later call replaces it.
    Form* Footer(El* content);
    Form* LabelWidth(float w);
    Form* WithSize(UiSize v);
    Form* LabelTextSize(float px);
    El* IntoEl();
};

Form* v_form(Ctx* cx);
Form* h_form(Ctx* cx);
component::Field field(El* control = nullptr);

} // namespace component
} // namespace gpui
#endif // GPUI_UI_FORM_H_
