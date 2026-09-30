// crates/component-shell/src/shell/display/common.rs

#include "component_shell/display/common.h"

namespace gpui::component_shell::display::common {

bool NonEmptyId(PayloadBuild* build, const char* component, Str id) {
    if (len(id) == 0)
        return build->Fail(fmt("%s id must not be empty", Str(component)));
    return true;
}

bool EnsureNoChildren(MaterializeRequest* request, const char* component) {
    if (request->ChildrenLen() == 0) return true;
    request->Fail(fmt("%s does not accept child elements", Str(component)));
    return false;
}

} // namespace gpui::component_shell::display::common
