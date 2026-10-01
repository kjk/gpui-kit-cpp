#ifndef GPUI_UI_COMMAND_H_
#define GPUI_UI_COMMAND_H_
/* Themed command palette — crates/ui/src/command

   A search field over a filtered list of commands: groups, separators,
   keybinding hints and keyboard navigation. Rust splits it in two —
   `Command` owns the entries and the rendering policy and is pushed into
   `CommandState` on every render, and `CommandState` keeps the interaction
   state: the query, the selection, focus, loading and scrolling. The same
   split holds here, except that the model is not copied into the state: the
   entries are the caller's array, the way a SearchableList's items are, so
   they have to outlive the frame. */

#include "ui/sizing.h"
#include "ui/kbd.h"
#include "base/index_path.h"
#include "base/virtual_list.h"

namespace gpui {

namespace component {

// A single command. Rust's CommandItem is a render element rather than a
// business object: the application keeps its own model and answers a select
// or a confirm through the IndexPath it is told about.
struct CommandItem {
    Str label = {};
    // CommandItem::keywords: extra terms the search matches besides the
    // label. The caller's array, like everything else on an item.
    const Str* keywords = nullptr;
    int nKeywords = 0;
    IconName icon = IconName::None;
    // CommandItem::action: what a confirm dispatches, and whose binding the
    // row shows at its trailing end. An action here is the hash of its name
    // and `actionArg` is the rest of it — ActionEvent::arg.
    uint32_t action = 0;
    intptr_t actionArg = 0;
    // The context the binding is looked up in, for a chord bound under one.
    const char* actionContext = nullptr;
    // A check at the right end of the row. A resolved binding takes that
    // slot, so an item with one shows no check.
    bool checked = false;
    bool disabled = false;
    // CommandItem::child: the whole row drawn by the caller in place of the
    // icon and the label — including any keybinding hint, which only the
    // default row adds. Rust's is a closure; a builder here is handed the
    // item it is drawing.
    El* (*content)(Ctx* cx, const CommandItem* item) = nullptr;
    // How tall that row is. Rust measures every row with `layout_as_root`
    // before handing the sizes to the virtual list; there is no measure pass
    // here, so a custom row that is not the standard height says so.
    float contentH = 0;
    // What the caller knows the row by, for one that would rather not map an
    // IndexPath back to its own model. Rides along on every event.
    intptr_t data = 0;
};

// A titled section of items. The heading is hidden while every item in the
// group is filtered out.
struct CommandGroup {
    Str heading = {};
    const CommandItem* items = nullptr;
    int nItems = 0;
};

enum class CommandEntryKind : uint8_t {
    Item,
    Group,
    // A divider. One that ends up leading, trailing or next to another
    // separator once the query has filtered the list is not drawn.
    Separator
};

// A top-level entry. Rust's is an enum of the three; a tagged struct here,
// since a caller writes them out as one static array.
struct CommandEntry {
    CommandEntryKind kind = CommandEntryKind::Item;
    CommandItem item = {};
    CommandGroup group = {};
};

CommandEntry CommandEntryOf(const CommandItem& item);
CommandEntry CommandEntryOf(const CommandGroup& group);
CommandEntry CommandSeparatorEntry();

// CommandItem::matches: a case-insensitive substring of the label or of one
// of the keywords. An empty query matches everything.
bool CommandItemMatches(const CommandItem* item, Str query);

// One rendered line. Groups are flattened into headings and items so the list
// is a single sequence of rows, which is what the virtual list scrolls over.
enum class CommandRowKind : uint8_t {
    Heading,
    Item,
    Separator
};

struct CommandRow {
    CommandRowKind kind = CommandRowKind::Item;
    Str heading = {};
    // Which match this row shows, for CommandRowKind::Item.
    int match = 0;
};

// An item that survived the current query, and where it landed.
struct CommandMatch {
    int entry = 0;
    int itemIx = 0;
    // The item's place in the model as it was given, before any filtering:
    // an ungrouped item is section 0 and its own position, a grouped one is
    // its group and its position in it. This is what a select or a confirm
    // reports.
    IndexPath path = {};
    int row = 0;
    bool disabled = false;
    intptr_t data = 0;
};

enum class CommandEventKind : uint8_t {
    Query,
    Select,
    Confirm,
    Cancel
};

struct CommandEvent {
    CommandEventKind kind = CommandEventKind::Select;
    IndexPath path = {};
    Str query = {};
    intptr_t data = 0;
};

// command/state.rs CONTEXT and init: escape, enter, up and down under
// "Command".
Str CommandContext();
void CommandInitKeys();

struct CommandState {
    // Rust's CommandState owns its query input and subscribes to its Change
    // event; the field is a member here, and a change is what `applied` no
    // longer matches — the same test its `applied_query` makes.
    InputState query;
    VirtualListScrollHandle scroll = {};
    // The model the last Command render installed. The caller's array.
    const CommandEntry* entries = nullptr;
    int nEntries = 0;
    bool searchable = true;
    // Command::filterable: whether the query filters the entries here. Off is
    // a palette whose source already answered the query — an async search —
    // where a local substring pass only drops matches the source understood.
    bool filterable = true;
    Vec<CommandRow> rows;
    Vec<CommandMatch> matched;
    Vec<float> rowSizes;
    // Which match is highlighted, as an index into `matched`.
    int selected = -1;
    // set_selected_index(None): an owner that cleared the highlight keeps it
    // cleared through the next model install, rather than the install putting
    // it back on the first item.
    bool preserveNoSelection = false;
    bool loading = false;
    // deferred_scroll_to_item: the row to bring into view at the next layout.
    int pendingScroll = -1;
    // The query the matches were last worked out for, owned so that a change
    // can be seen. Rust's `applied_query`.
    Vec<char> applied;
    Listener onQuery = {};
    Listener onSelect = {};
    Listener onConfirm = {};
    Listener onCancel = {};

    static void OnRowClick(CommandState* self, Ctx* cx, const ClickEvent* ev,
                           intptr_t match);
    static void OnRowHover(CommandState* self, Ctx* cx, const HoverEvent* ev,
                           intptr_t match);
    static void OnAction(CommandState* self, Ctx* cx, const ActionEvent* ev);

    ~CommandState() {
        VecReset(rows);
        VecReset(matched);
        VecReset(rowSizes);
        VecReset(applied);
    }
};

// install_model: the entries this render is showing, the matches they come to
// under the query, and the selection carried across — by the item's path, so
// filtering does not move the highlight onto somebody else.
void CommandInstall(CommandState* s, Ctx* cx, const CommandEntry* entries,
                    int nEntries, bool searchable, bool filterable = true);
// The highlighted item's path in the model as it was given, before filtering.
// False when nothing is highlighted.
bool CommandSelectedIndex(const CommandState* s, IndexPath* out);
// Highlight an item by that path, or clear the highlight with null. A path
// that is filtered out or disabled clears it; a visible one is scrolled to.
void CommandSetSelectedIndex(CommandState* s, Ctx* cx, const IndexPath* path);
// select_by: the highlight moves `step` items on, wrapping around and
// skipping the disabled ones. Private in Rust; the seam the arrow keys and
// the tests both go through here.
void CommandSelectBy(CommandState* s, Ctx* cx, int step);
// The number of items matching the current query.
int CommandMatchedCount(const CommandState* s);
// Replace the query, as if it had been typed.
void CommandSetQuery(CommandState* s, Ctx* cx, Str query);
// Show or hide the field's spinner, which also suppresses the empty message:
// a search in flight has nothing to show yet, which is not the same as no
// match.
void CommandSetLoading(CommandState* s, Ctx* cx, bool loading);

struct Command {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Str id = {};
    Entity<CommandState> state = {};
    const CommandEntry* entries = nullptr;
    int nEntries = 0;
    bool searchable = true;
    bool filterable = true;
    Str placeholder = {};
    // Command::empty / header / footer: Rust builds them from a closure it
    // hands the state; they are elements here, built by the caller for this
    // frame like every other child.
    El* empty = nullptr;
    El* header = nullptr;
    El* footer = nullptr;
    // The header built the way Rust builds it: from the state, once this
    // render has installed the model — so a header that reports
    // `matched_count` counts the matches for the query now in the field
    // rather than the last frame's. Takes precedence over `header`.
    El* (*headerFn)(Ctx* cx, const CommandState* s) = nullptr;
    float maxH = 300;
    bool bordered = true;
    float w = kFill;
    // impl Styled for Command: the refinement Rust keeps in
    // `CommandOptions::style` and lays over the palette's box after its
    // surface and border (`.refine_style(&self.options.style)`).
    Style style = {};
    uint32_t styleSet = 0;
    Listener onQuery = {};
    Listener onSelect = {};
    Listener onConfirm = {};
    Listener onCancel = {};

    static Command* New(Ctx* cx, Str id, Entity<CommandState> state);
    Command* Entries(const CommandEntry* entries, int n);
    // The ungrouped form: the items occupy section 0 and keep the position
    // they were given, which is what Rust's `.items(..)` comes to.
    Command* Items(const CommandItem* items, int n);
    Command* Searchable(bool v);
    // Keep the field, the spinner and the keys, and leave the filtering to
    // whoever answers `OnQuery`. A query change then hands the highlight back
    // to the first item rather than to a local textual match.
    Command* Filterable(bool v);
    Command* Placeholder(Str s);
    Command* Empty(El* e);
    Command* Header(El* e);
    Command* Header(El* (*build)(Ctx* cx, const CommandState* s));
    Command* Footer(El* e);
    Command* MaxH(float v);
    Command* Bordered(bool v);
    Command* W(float v);
    Command* Refine(const Style& s, uint32_t fields);
    Command* OnQuery(Listener fn);
    Command* OnSelect(Listener fn);
    Command* OnConfirm(Listener fn);
    Command* OnCancel(Listener fn);
    El* IntoEl();
};

} // namespace component
} // namespace gpui
#endif // GPUI_UI_COMMAND_H_
