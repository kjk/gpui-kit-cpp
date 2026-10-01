#ifndef GPUI_SRC_UI_TREE_H_
#define GPUI_SRC_UI_TREE_H_
/* Themed tree — crates/ui/src/tree.rs

   crates/ui's Tree is the base tree with the gallery's row: an indent per
   depth, a chevron for a folder, an icon and a label. The rows come from a
   TreeState and only the visible ones are built, which is what makes it a
   virtualized tree rather than a list of every node. */

#include "ui/sizing.h"

namespace gpui {

namespace component {

struct PopupMenu;

// Tree::context_menu's builder: handed the row's index and entry and a
// fresh menu, it answers the menu to show on a right press. Disabled rows
// get none.
using TreeContextMenuFn = PopupMenu* (*)(void* user, Ctx* cx, int ix,
                                         const TreeEntry& entry,
                                         PopupMenu* menu);

struct Tree {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Str id = {};
    Entity<TreeState> state = {};
    float h = 320;
    // Whether a row shows a file / folder icon beside its chevron.
    bool icons = true;
    TreeContextMenuFn contextMenu = nullptr;
    void* contextMenuUser = nullptr;

    static Tree* New(Ctx* cx, Str id, Entity<TreeState> state);
    Tree* H(float v);
    Tree* Icons(bool v);
    Tree* ContextMenu(TreeContextMenuFn fn, void* user = nullptr);
    El* IntoEl();
};

} // namespace component
} // namespace gpui
#endif // GPUI_SRC_UI_TREE_H_
