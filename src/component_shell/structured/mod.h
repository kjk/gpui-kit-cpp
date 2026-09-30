#ifndef GPUI_COMPONENT_SHELL_STRUCTURED_MOD_H_
#define GPUI_COMPONENT_SHELL_STRUCTURED_MOD_H_

// crates/component-shell/src/shell/structured/mod.rs and common.rs: the
// structured, stateless bindings (DescriptionList, Form, Table) and the
// argument checks they share. The typed-part element common.rs defines
// (TypedChildElement, take_element) is the one typed_compound/mod.h already
// has; the families share it here instead of carrying a second copy.
//
// What is declared here beyond the registrations is what the Rust modules'
// own tests reach, exposed for tests/ComponentShellTests.cpp.

#include "component_shell/support.h"

namespace gpui::component_shell::structured {

// ─── common.rs ────────────────────────────────────────────────────────────
// Each answers false with Rust's message in `*error`, a temporary string.

// positive_usize: a finite whole number from 1 up to (not including) 2^64,
// which is `usize::MAX as f64`; "<label> expects an exactly representable
// positive integer" otherwise.
bool PositiveUsize(double value, Str label, uint64_t* out, Str* error);

// positive_u16: a positive_usize no greater than 65535; "<label> expects an
// integer no greater than 65535" otherwise.
bool PositiveU16(double value, Str label, uint16_t* out, Str* error);

// nonnegative_f32: a finite number from 0 through f32::MAX; "<label> expects
// a nonnegative finite number representable as f32" otherwise.
bool NonnegativeF32(double value, Str label, float* out, Str* error);

// ─── description_list.rs ──────────────────────────────────────────────────

struct ListOp {
    enum Kind : uint8_t {
        Vertical,
        Bordered,
        Columns,
        Size,
    } kind = Vertical;
    bool bordered = false;
    int columns = 0;
    UiSize size = UiSize::Medium;
};

// ListConfig: what the list's recorded operations fold into, in script
// order.
struct ListConfig {
    bool vertical = false;
    bool bordered = true;
    int columns = 3;
    UiSize size = UiSize::Medium;

    void Apply(const ListOp& op);
};

// positive_usize_payload for DescriptionItem.span, and columns_payload for
// DescriptionList.columns: the recorders, reachable with a PayloadBuild.
bool RecordItemSpan(PayloadBuild* build, const ComponentArgument* args,
                    int count);
bool RecordListColumns(PayloadBuild* build, const ComponentArgument* args,
                       int count);

// ensure_item_surface: DescriptionItem takes neither children nor style.
bool EnsureItemSurface(int children, bool styled, Str* error);

// ─── form.rs ──────────────────────────────────────────────────────────────

// form_columns: Form.columns' recorder.
bool RecordFormColumns(PayloadBuild* build, const ComponentArgument* args,
                       int count);

// ─── table.rs ─────────────────────────────────────────────────────────────

// The typed-part tags of the parts whose C++ value type is shared: a
// header, a body and a footer are all a component::TableGroup, a head and a
// cell both a component::TableCellEl. A row is tagged by
// component::TableRow and a caption by component::TableCaption.
struct TableHeaderPart {};
struct TableBodyPart {};
struct TableFooterPart {};
struct TableHeadPart {};
struct TableCellPart {};

// cell_span: TableHead/TableCell col_span's check.
bool RecordCellSpan(PayloadBuild* build, const ComponentArgument* args,
                    int count);

} // namespace gpui::component_shell::structured
#endif // GPUI_COMPONENT_SHELL_STRUCTURED_MOD_H_
