// crates/component-shell/src/shell/controls/support.rs

#include "component_shell/controls/support.h"

namespace gpui::component_shell::controls::support {

static bool RecordText(PayloadBuild* build, CommonOp::Kind kind, Str text) {
    CommonOp* op = build->New<CommonOp>();
    op->kind = kind;
    op->text = text;
    return true;
}

bool RecordLabel(PayloadBuild* build, const ComponentArgument* args, int) {
    return RecordText(build, CommonOp::Label, args[0].string);
}

bool RecordTooltip(PayloadBuild* build, const ComponentArgument* args, int) {
    return RecordText(build, CommonOp::Tooltip, args[0].string);
}

bool RecordChecked(PayloadBuild* build, const ComponentArgument* args, int) {
    CommonOp* op = build->New<CommonOp>();
    op->kind = CommonOp::Checked;
    op->checked = args[0].boolean;
    return true;
}

bool RecordSize(PayloadBuild* build, const ComponentArgument* args, int) {
    CommonOp* op = build->New<CommonOp>();
    op->kind = CommonOp::Size;
    op->size = SizeOfLiteral(args[0].string);
    return true;
}

bool RecordOutline(PayloadBuild* build, const ComponentArgument*, int) {
    build->New<CommonOp>()->kind = CommonOp::Outline;
    return true;
}

bool RecordChange(PayloadBuild* build, const ComponentArgument* args, int) {
    CommonOp* op = build->New<CommonOp>();
    op->kind = CommonOp::Change;
    op->change = args[0];
    return true;
}

} // namespace gpui::component_shell::controls::support
