#include "ui/tree.h"
#include "base/list_settings.h"
#include "ui/menu.h"

namespace gpui {

namespace component {

Tree* Tree::New(Ctx* cx, Str id, Entity<TreeState> state) {
    Arena* a = cx->a;
    Tree* t = ArenaNew<Tree>(a);
    t->a = a;
    t->cx = cx;
    t->id = id;
    t->state = state;
    return t;
}
Tree* Tree::H(float v) {
    h = v;
    return this;
}
Tree* Tree::Icons(bool v) {
    icons = v;
    return this;
}
Tree* Tree::ContextMenu(TreeContextMenuFn fn, void* user) {
    contextMenu = fn;
    contextMenuUser = user;
    return this;
}

// The themed row — crates/ui/src/tree.rs. tree_story.rs's row is an icon and
// a label and nothing else: File for a leaf, FolderOpen for an open folder
// and Folder for a shut one.
static El* TreeRow(void* user, Ctx* cx, int ix, const TreeEntry& entry,
                   TreeEntryState entryState) {
    Tree* self = (Tree*)user;
    const Theme& th = ThemeNow(cx->app);
    TreeState* s = self->state.Get(cx);
    const TreeItem* it = entry.item;
    if (!it) {
        return nullptr;
    }
    Arena* a = cx->a;
    El* row = Div(a)
                  ->FlexRow()
                  ->W(kFill)
                  ->H(s->rowH)
                  // ListItem is px_3, and the story's row adds
                  // `pl(px(16.) * entry.depth() + px(12.))` on top of it,
                  // which is the whole of the indent: there is no spacer
                  // child and no chevron column.
                  ->PadR(12)
                  ->PadL(12 + (float)it->depth * 16)
                  ->Gap(8)
                  ->ItemsCenter()
                  ->Radius(th.radius);
    // The story's row is a ListItem: no hover background while it is
    // selected or right-clicked, the selection filled, and a right-clicked
    // row outlined in `selection` over whatever fill it has (upstream #3155).
    bool active = entryState.IsSelected() || entryState.IsRightClicked();
    if (!it->disabled && !active) {
        row->HoverBg(th.tokens.listHover);
    }
    if (entryState.IsSelected()) {
        row->Bg(th.tokens.accent);
    }
    if (!it->disabled && entryState.IsRightClicked()) {
        row->Child(ListActiveOverlay(a, th.selection, th.radius));
    }
    if (self->icons) {
        IconName ic = !it->folder    ? IconName::File
                      : it->expanded ? IconName::FolderOpen
                                     : IconName::Folder;
        row->Child(IconEl(a, ic, 16)
                       ->Fg(it->disabled ? th.mutedFg : th.foreground));
    }
    // ListItem is text_base, not text_sm.
    row->Child(TextEl(a, it->label)
                   ->Font(16)
                   ->Fg(it->disabled ? th.mutedFg : th.foreground));
    // div().child(item).context_menu(..): the builder sees the row's entry,
    // and a disabled row has no menu.
    if (self->contextMenu && !it->disabled) {
        PopupMenu* menu = PopupMenu::New(
            cx, StrDup(a, fmt("%s-context-menu-%d", self->id, ix)));
        menu = self->contextMenu(self->contextMenuUser, cx, ix, entry, menu);
        if (menu) {
            return ContextMenuExt::Wrap(
                       cx, StrDup(a, fmt("%s-context-%d", self->id, ix)), row,
                       menu)
                ->IntoEl();
        }
    }
    return row;
}

El* Tree::IntoEl() {
    if (!state.Get(cx)) {
        return Div(a)->H(h);
    }
    return TreeList::New(cx, id, state, h, &TreeRow, this);
}

} // namespace component
} // namespace gpui
