#include "Story.h"

// The Rust story (crates/story/src/tree_story.rs) walks the working directory
// and puts it in a Tree. So does this: the top level, then one level under
// each folder, which is enough rows for the list to be worth virtualizing.
struct TreeStory {
    Entity<TreeState> tree = {};
    bool loaded = false;
    // cx.subscribe's return: the page holds it for as long as it wants to
    // hear the tree.
    Subscription sub = {};
    // What the last TreeEvent said, shown under the tree.
    Str message = {};

    static El* Render(TreeStory* self, Ctx* cx);
    static void OnTreeEvent(TreeStory* self, Ctx* cx, const TreeEvent* ev);
    static void OnRevealRandom(TreeStory* self, Ctx* cx, const ClickEvent* ev);
};

void TreeStory::OnTreeEvent(TreeStory* self, Ctx* cx, const TreeEvent* ev) {
    if (self->message.s) {
        StrFree(self->message);
    }
    Str what = ev->kind == TreeEventKind::Expanded ? StrL("Expanded")
                                                   : StrL("Collapsed");
    self->message = StrDup(fmt("%s %s", what, ev->id));
    Notify(cx);
}

// reveal_item: open everything above an item and scroll it into view. No RNG
// here; the monotonic clock in milliseconds is random enough for "pick one".
void TreeStory::OnRevealRandom(TreeStory* self, Ctx* cx, const ClickEvent*) {
    TreeState* s = self->tree.Get(cx);
    if (!s || s->items.len == 0) {
        return;
    }
    int item = (int)((uint64_t)(TimeNow() * 1000.0) % (uint64_t)s->items.len);
    int ix = TreeRevealItem(s, cx, s->items[item].id, ScrollStrategy::Center);
    if (ix >= 0) {
        s->selected = ix;
    }
    Notify(cx);
}

// Rust filters with the repo gitignore; ours skips the same few by name.
static bool TreeSkip(Str name) {
    static const char* kSkip[] = {".git", "out", "node_modules", ".work"};
    for (size_t i = 0; i < sizeof(kSkip) / sizeof(kSkip[0]); i++) {
        if (StrEq(name, kSkip[i])) {
            return true;
        }
    }
    return false;
}

// Folders first, then by name — the order a file tree is normally shown in.
static void SortDir(DirEntry* e, int n) {
    for (int i = 1; i < n; i++) {
        DirEntry key = e[i];
        int j = i - 1;
        while (j >= 0) {
            bool after = e[j].isDir != key.isDir
                             ? (!e[j].isDir && key.isDir)
                             : StrCmp(Str(e[j].name), Str(key.name)) > 0;
            if (!after) {
                break;
            }
            e[j + 1] = e[j];
            j--;
        }
        e[j + 1] = key;
    }
}

static void LoadDir(TreeState* s, Str path, int parent, int depth) {
    // One listing per level, on the heap: a static array here would be the
    // same array the level above is still walking.
    const int kMaxEntries = 512;
    DirEntry* found = AllocArray<DirEntry>(kMaxEntries);
    if (!found) {
        return;
    }
    TempStr pathZ = StrDupTemp(path);
    int got = PlatListDir(pathZ.s, found, kMaxEntries);
    SortDir(found, got);
    for (int i = 0; i < got; i++) {
        if (TreeSkip(Str(found[i].name))) {
            continue;
        }
        // The path this row stands for. A name is at most 260 bytes and the
        // walk is two deep, so the buffer is the sum rather than a guess —
        // which is also what keeps the compiler from calling it a truncation.
        TempStr child = fmt("%s/%s", path, Str(found[i].name));
        if (len(child) >= 1024) {
            continue;
        }
        // The id has to be unique and stable, so it is the path; the label is
        // the name. TreeAddItem copies both; the state owns and frees them.
        int ix = TreeAddItem(s, child, Str(found[i].name), parent);
        if (ix < 0) {
            break;
        }
        // A directory is a folder because it is one, not because its children
        // happen to have been read in yet.
        s->items[ix].folder = found[i].isDir;
        if (found[i].isDir && depth > 0) {
            LoadDir(s, child, ix, depth - 1);
        }
    }
    Free(nullptr, found);
}

El* TreeStory::Render(TreeStory* self, Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    if (!self->loaded) {
        self->loaded = true;
        self->tree = EntityNewState<TreeState>(cx->app);
        TreeState* s = self->tree.Get(cx);
        if (s) {
            // Two levels are read in so a folder opens without a pause;
            // nothing starts open, which is where Rust's tree starts.
            LoadDir(s, StrL("."), -1, 2);
            TreeRebuild(s);
            // cx.subscribe(&tree, ..) rather than the state's own listener:
            // the same handler, hung off the entity, so anything else on the
            // page could hear the tree too. `loaded` is what keeps the page
            // from subscribing again on every frame — Rust keeps the
            // Subscription in the view for the same reason.
            self->sub = Subscribe(cx, self->tree, &TreeStory::OnTreeEvent);
        }
    }
    TreeState* s = self->tree.Get(cx);
    El* page = Div(a)->FlexCol()->Gap(12)->W(kFill);

    El* btnRow = Div(a)->FlexRow()->Gap(12);
    btnRow->Child(component::Button::New(cx, StrL("select-item"))
                      ->Label(StrL("Select Item"))
                      ->Outline()
                      ->OnClick(Listen(cx, &TreeStory::OnRevealRandom))
                      ->IntoEl());
    page->Child(btnRow);

    El* sec = StorySection(cx, "File tree", nullptr);
    StorySectionSubTitle(
        sec, StoryTxt(cx,
                      StrL("Press `enter` to rename. Right-click for context "
                           "menu."),
                      16, th.mutedFg));
    StorySectionBody(sec)->W(480);
    El* col = Div(a)->FlexCol()->W(kFill)->Gap(16);
    El* box = Div(a)->FlexCol()->W(kFill)->Pad(4)->Radius(th.radius)->Border(
        1, th.border);
    // `.p_1().border_1().rounded(radius).h(px(540.))` is on the tree itself
    // in Rust, so the 540 takes the padding and the border with it.
    box->Child(
        component::Tree::New(cx, StrL("tree"), self->tree)->H(530)->IntoEl());
    col->Child(box);

    El* status = Div(a)->FlexRow()->W(kFill)->Gap(12)->JustifyBetween();
    if (s && s->selected >= 0) {
        const TreeItem* it = TreeEntryItem(s, s->selected);
        status->Child(StoryTxt(cx,
                               StoryFmt(cx, "Selected Index: %d", s->selected),
                               16, th.foreground));
        if (it) {
            status->Child(component::Label::New(cx, StrL("Selected:"))
                              ->Secondary(it->label)
                              ->IntoEl());
        }
    }
    col->Child(status);
    if (self->message.s) {
        col->Child(StoryTxt(cx, self->message, 14, th.mutedFg));
    }
    StorySectionAdd(sec, col);
    page->Child(sec);
    return page;
}

STORY_PAGE(StoryTree, TreeStory);
