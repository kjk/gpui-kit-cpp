#ifndef GPUI_UI_LINK_H_
#define GPUI_UI_LINK_H_
/* Themed link — crates/ui/src/link.rs */

#include "ui/sizing.h"

namespace gpui {

namespace component {

struct Link {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Str id = {};
    Str href = {};
    Str text = {};
    bool disabled = false;
    Listener onOpen;
    // ParentElement: link.rs renders its children in place of any text.
    ArenaVec<El*> children;

    static Link* New(Ctx* cx, Str id);
    Link* Href(Str s);
    Link* Text(Str s);
    Link* Disabled(bool v);
    Link* OnOpen(Listener fn);
    Link* Child(El* e);
    El* IntoEl();
};

} // namespace component
} // namespace gpui
#endif // GPUI_UI_LINK_H_
