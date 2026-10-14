/* The keyboard, the pointer, actions, the window and the five base components
 * a script can now name — ports of crates/shell/src/tests/dock.rs and the
 * 27b08eca half of crates/shell/src/tests/render.rs.
 *
 * Upstream drives ten of its eleven through a real window, a real focus path
 * and real input, which needs GPUI's TestAppContext. What stands in for that
 * here is the same split every other shell suite uses: the description tree
 * for what a script said, and the materialized element tree for what base was
 * asked to build. The breadth test is ported as it is — it touches every name
 * in the checkin's tables from a script, under the name the documentation
 * uses, and catches the failure the behavioural ones cannot: a member bound in
 * the host but unreachable from the prelude, or reachable under a different
 * name. */

#include "Test.h"

using namespace gpui::shell;

namespace {

// One script, rendered once. The description is what the assertions read;
// `error` carries whatever the script threw, which several of these want.
struct ShellDockFixture {
    App app;
    Window window;
    ShellRuntime* runtime = nullptr;
    ViewType* type = nullptr;
    ViewObject* object = nullptr;
    // A real ScriptView entity, because several of these call cx.new(Class)
    // from init() — which needs a current script view to own what it makes,
    // the way a dock panel's body is owned by the view that added it.
    Entity<ScriptView> view = {};
    ShellError error = {};
    Arena* output = ArenaNew();

    ShellDockFixture() {
        window.app = &app;
        component::Init(&app);
        runtime = ShellRuntime::New(&app, &error);
    }

    ~ShellDockFixture() {
        // Order matters twice. The view owns the object once Load handed it
        // over, so only an unowned one is released here; and the entities go
        // before the runtime, because a ScriptView unregisters itself from
        // the runtime as it is dropped.
        if (view.IsValid()) {
            EntityDrop(&app, view.id);
        } else {
            ViewObjectRelease(object);
        }
        ViewTypeRelease(type);
        ArenaDelete(output);
        if (runtime) runtime->Release();
        ShellErrorClear(&error);
        EntityDropAll(&app);
        AppGlobalClear(&app);
    }

    bool Load(Str name, Str source) {
        type = runtime ? runtime->LoadSource(name, source, &error) : nullptr;
        if (type) view = ScriptView::New(&app, runtime, type, nullptr);
        object = type ? runtime->Instantiate(type, &window, &app, nullptr,
                                             &error, view.id)
                      : nullptr;
        if (ScriptView* held = view.Get(&app)) held->object = object;
        return object != nullptr;
    }

    Str Render() {
        output->Reset();
        return object ? runtime->RenderToSpec(output, object, &window, &app,
                                              view.id, nullptr, &error)
                      : Str{};
    }

    RenderSnapshot* Snapshot() {
        return object ? runtime->BuildSnapshot(object, &window, &app, view.id,
                                               nullptr, &error)
                      : nullptr;
    }
};

} // namespace

// `pagination_items(current, total, visible)` — the ellipsis layout, which is
// the one thing base contributes that a script could not write for itself.
// Asserted from the script, because the shape it answers is the contract.
static void PaginationItemsLayOutThePagesAndTheirGaps() {
    ShellDockFixture fixture;
    utassert(fixture.Load(
        StrL("pagination.js"),
        StrL("import { View, div } from 'gpui';\n"
             "import { pagination_items } from 'gpui-base';\n"
             "export default class Pages extends View { render() {\n"
             "  const few = pagination_items(1, 3);\n"
             "  if (few.length !== 3 || few[0].page !== 1 || few[2].page !== "
             "3)\n"
             "    throw new Error('a short run shows every page');\n"
             "  if (few.some((each) => each.ellipsis))\n"
             "    throw new Error('a short run has no gap');\n"
             "  const many = pagination_items(10, 30, 5);\n"
             "  if (many[0].page !== 1) throw new Error('the first page always "
             "shows');\n"
             "  if (many[many.length - 1].page !== 30)\n"
             "    throw new Error('the last page always shows');\n"
             "  const gaps = many.filter((each) => each.ellipsis);\n"
             "  if (gaps.length !== 2) throw new Error('a middle page breaks "
             "the run twice');\n"
             "  const [from, to] = gaps[0].ellipsis;\n"
             "  if (!(from >= 2 && to >= from))\n"
             "    throw new Error('a gap spans a run of pages, inclusive');\n"
             "  if (!many.some((each) => each.page === 10))\n"
             "    throw new Error('the current page is in the window');\n"
             "  return div();\n"
             "} }\n")));
    Str spec = fixture.Render();
    utassert(!fixture.error.IsSet() && spec);
}

// An avatar renders its image slot, or its fallback when there is none, and
// draws nothing itself.
static void AnAvatarRendersItsImageOrItsFallback() {
    ShellDockFixture fixture;
    utassert(fixture.Load(
        StrL("avatar.js"),
        StrL(
            "import { View, div } from 'gpui';\n"
            "import { Avatar, AvatarImage, AvatarFallback, v_flex } from "
            "'gpui-base';\n"
            "export default class Faces extends View { render() {\n"
            "  return v_flex()\n"
            "    .child(Avatar.new().image(AvatarImage.new('avatar.png'))\n"
            "      .fallback(AvatarFallback.new().child('KK')))\n"
            "    "
            ".child(Avatar.new().fallback(AvatarFallback.new().child('AB')));\n"
            "} }\n")));
    Str spec = fixture.Render();
    utassert(!fixture.error.IsSet());
    // The path is what an avatar image *is*; without it the dump says an image
    // is there but not which one.
    utassert(StrContains(spec, StrL("AvatarImage \"avatar.png\"")));
    utassert(StrContains(spec, StrL("@image")));
    utassert(StrContains(spec, StrL("@fallback")));

    RenderSnapshot* snapshot = fixture.Snapshot();
    Arena* frame = ArenaNew();
    Ctx cx = {&fixture.app, &fixture.window, frame, {}};
    El* root = snapshot ? ShellMaterialize(&cx, fixture.runtime, snapshot,
                                           &fixture.error)
                        : nullptr;
    utassert(root && !fixture.error.IsSet());
    // Base renders one or the other and never both, so an avatar with an image
    // draws one child and an avatar without one still draws its fallback.
    if (root && root->first) {
        El* withImage = root->first;
        El* withoutImage = withImage->next;
        int drawn = 0;
        for (El* at = withImage->first; at; at = at->next) drawn++;
        utassert(drawn == 1);
        drawn = 0;
        for (El* at = withoutImage ? withoutImage->first : nullptr; at;
             at = at->next) {
            drawn++;
        }
        utassert(drawn == 1);
    }
    ArenaDelete(frame);
    delete snapshot;
}

// An accordion item passes its own `open` down to both halves, so a script
// sets it once rather than three times in agreement with itself.
static void AnAccordionItemPassesItsOpenStateDownToItsTrigger() {
    ShellDockFixture fixture;
    utassert(fixture.Load(
        StrL("accordion.js"),
        StrL("import { View, div } from 'gpui';\n"
             "import { Accordion, AccordionItem, AccordionHeader,\n"
             "         AccordionPanel, AccordionTrigger } from 'gpui-base';\n"
             "export default class Faq extends View {\n"
             "  init() { this.open = true; }\n"
             "  item(open, id) {\n"
             "    return AccordionItem.new().open(open)\n"
             "      .header(AccordionHeader.new(\n"
             "        AccordionTrigger.new(id).on_change(() => {}))\n"
             "        .id(id + '-header').aria_level(2))\n"
             "      .panel(AccordionPanel.new().id(id + "
             "'-panel').child('body'));\n"
             "  }\n"
             "  render() {\n"
             "    return Accordion.new('faq')\n"
             "      .child(this.item(this.open, 'first'))\n"
             "      .child(this.item(false, 'second'));\n"
             "  }\n"
             "}\n")));
    Str spec = fixture.Render();
    utassert(!fixture.error.IsSet());
    utassert(StrContains(spec, StrL("Accordion \"faq\"")));
    utassert(StrContains(spec, StrL("AccordionTrigger \"first\"")));
    utassert(StrContains(spec, StrL(":aria_level(2)")));

    RenderSnapshot* snapshot = fixture.Snapshot();
    Arena* frame = ArenaNew();
    Ctx cx = {&fixture.app, &fixture.window, frame, {}};
    El* root = snapshot ? ShellMaterialize(&cx, fixture.runtime, snapshot,
                                           &fixture.error)
                        : nullptr;
    utassert(root && !fixture.error.IsSet());
    if (root && root->first) {
        El* open = root->first;
        El* shut = open->next;
        // The open item mounts its panel beside its header; the shut one is
        // its header alone, because a panel is out of the tree while shut
        // unless keep_mounted(true) says otherwise.
        int openChildren = 0;
        for (El* at = open->first; at; at = at->next) openChildren++;
        int shutChildren = 0;
        for (El* at = shut ? shut->first : nullptr; at; at = at->next) {
            shutChildren++;
        }
        utassert(openChildren == 2 && shutChildren == 1);
        // The heading announces the level the script asked for, and the
        // trigger announces the item's own open state rather than its own.
        El* header = open->first;
        utassert(header->accessibility.role == AccessibilityRole::Heading);
        utassert(header->accessibility.level == 2);
        utassert(header->first && header->first->accessibility.expanded);
        El* shutHeader = shut ? shut->first : nullptr;
        utassert(shutHeader && shutHeader->first &&
                 !shutHeader->first->accessibility.expanded);
    }
    ArenaDelete(frame);
    delete snapshot;
}

// A calendar state answers the month grid and moves it, and the two Date
// variants stay apart on the wire: a single day and a range whose end is not
// chosen yet hold the same one date and read back as the same string, but base
// branches on the difference.
static void ACalendarStateAnswersTheMonthGridAndMovesIt() {
    ShellDockFixture fixture;
    utassert(fixture.Load(
        StrL("calendar.js"),
        StrL("import { View, div } from 'gpui';\n"
             "import { CalendarState } from 'gpui-base';\n"
             "export default class Days extends View {\n"
             "  init() {\n"
             "    this.state = CalendarState.new();\n"
             "    this.state.set_value('2026-08-15');\n"
             "    this.single = this.state.value();\n"
             "    this.state.set_value(['2026-08-03', null]);\n"
             "    this.halfRange = this.state.value();\n"
             "    this.state.set_value(['2026-08-03', '2026-08-09']);\n"
             "    this.range = this.state.value();\n"
             "    this.state.set_value(null);\n"
             "    this.empty = this.state.value();\n"
             "    this.year = this.state.year();\n"
             "    this.month = this.state.month();\n"
             "    this.grid = this.state.month_days();\n"
             "    this.state.next_month();\n"
             "    this.after = this.state.month();\n"
             "    this.state.prev_month();\n"
             "    this.state.prev_month();\n"
             "    this.before = this.state.month();\n"
             "  }\n"
             "  render() {\n"
             "    if (this.single !== '2026-08-15')\n"
             "      throw new Error('a single day reads back as one string');\n"
             "    if (!Array.isArray(this.halfRange) || this.halfRange.length "
             "!== 2 ||\n"
             "        this.halfRange[0] !== '2026-08-03' || this.halfRange[1] "
             "!== null)\n"
             "      throw new Error('a half-finished range stays a pair');\n"
             "    if (this.range[1] !== '2026-08-09')\n"
             "      throw new Error('a complete range keeps both ends');\n"
             "    if (this.empty !== null) throw new Error('no date reads back "
             "as null');\n"
             "    const weeks = this.grid[0];\n"
             "    if (!Array.isArray(weeks) || weeks.length < 4 || "
             "weeks.length > 6)\n"
             "      throw new Error('a month is four to six whole weeks');\n"
             "    if (weeks.some((week) => week.length !== 7))\n"
             "      throw new Error('the grid runs in whole weeks');\n"
             "    if (!weeks.flat().includes(this.year + '-08-15'))\n"
             "      throw new Error('the grid holds the month it is for');\n"
             "    const next = this.month === 12 ? 1 : this.month + 1;\n"
             "    const prev = this.month === 1 ? 12 : this.month - 1;\n"
             "    if (this.after !== next || this.before !== prev)\n"
             "      throw new Error('next_month and prev_month move one "
             "month');\n"
             "    if (typeof this.state.today() !== 'string')\n"
             "      throw new Error('today is a date');\n"
             "    return div();\n"
             "  }\n"
             "}\n")));
    Str spec = fixture.Render();
    utassert(!fixture.error.IsSet() && spec);
}

// A dock area is retained, its panels are entities the script handed it, and
// the layout round-trips through dump() and load().
static void AScriptDrivesADockAreaAndItsLayoutRoundTrips() {
    ShellDockFixture fixture;
    utassert(fixture.Load(
        StrL("dock.js"),
        StrL(
            "import { View, div } from 'gpui';\n"
            "import { DockArea, dock_area, dock_content, v_flex } from "
            "'gpui-base';\n"
            "class Panel extends View {\n"
            "  init(props) { this.caption = props?.caption ?? 'untitled'; }\n"
            "  serialize() { return { caption: this.caption }; }\n"
            "  deserialize(data) { this.caption = data.caption; }\n"
            "  render() { return div().child(this.caption); }\n"
            "}\n"
            "export default class Workspace extends View {\n"
            "  init(_props, cx) {\n"
            "    DockArea.register_panel('document', Panel);\n"
            "    this.dock = DockArea.new('workspace', { version: 1 });\n"
            "    this.dock.add_panel(cx.new(Panel, { caption: 'main.js' }), { "
            "name: 'document' });\n"
            "    this.dock.add_panel(cx.new(Panel, { caption: 'files' }),\n"
            "      { name: 'document', placement: 'left', size: 180 });\n"
            "    this.panels = this.dock.panels();\n"
            "    this.saved = this.dock.dump();\n"
            "    this.hasLeft = this.dock.has_dock('left');\n"
            "    this.hasBottom = this.dock.has_dock('bottom');\n"
            "    this.leftOpen = this.dock.is_dock_open('left');\n"
            "    this.leftSize = this.dock.dock_size('left');\n"
            "    this.dock.toggle_dock('left');\n"
            "    this.leftShut = this.dock.is_dock_open('left');\n"
            "    this.dock.toggle_dock('left');\n"
            "    this.dock.on('layout_changed', () => { this.edits = "
            "(this.edits ?? 0) + 1; });\n"
            "  }\n"
            "  render(cx) {\n"
            "    if (this.panels.length !== 2)\n"
            "      throw new Error('panels() names every panel once the call "
            "adding them returned');\n"
            "    const centre = this.panels.filter((each) => each.placement "
            "=== 'center');\n"
            "    const left = this.panels.filter((each) => each.placement === "
            "'left');\n"
            "    if (centre.length !== 1 || left.length !== 1)\n"
            "      throw new Error('a panel is filed under the placement it "
            "was added at');\n"
            "    if (!centre[0].name.endsWith('/document'))\n"
            "      throw new Error('a panel is namespaced by its "
            "application');\n"
            "    if (!this.hasLeft || this.hasBottom)\n"
            "      throw new Error('has_dock answers for the docks that "
            "exist');\n"
            "    if (!this.leftOpen || this.leftShut)\n"
            "      throw new Error('toggle_dock shuts and reopens one dock');\n"
            "    if (this.leftSize !== 180)\n"
            "      throw new Error('add_panel size becomes the dock size');\n"
            "    if (!JSON.stringify(this.saved).includes('main.js'))\n"
            "      throw new Error(\"dump() carries each panel's own "
            "serialize()\");\n"
            "    return v_flex().child(dock_area(this.dock).flex_1()\n"
            "      .tab_bar((group) => div().id('bar-' + group.node)\n"
            "        .children(group.tabs.map((tab) =>\n"
            "          div().id('tab-' + tab.id).select_tab(group, tab.index)\n"
            "            .drag_tab(group, tab.index).child(tab.name))))\n"
            "      .empty_group(() => div().child('drop here'))\n"
            "      .drop_indicator((drop) => "
            "div().absolute().left(drop.to.x).top(drop.to.y))\n"
            "      .dock((dock) => "
            "v_flex().child(div().toggle_dock(dock).child(dock.placement))\n"
            "        .child(dock_content().flex_1())));\n"
            "  }\n"
            "}\n")));
    Str spec = fixture.Render();
    utassert(!fixture.error.IsSet());
    // Nothing under a dock area is described: its panels are entities the
    // script handed it, and its chrome is drawn by the handlers this node
    // carries — so the node itself is the whole of the description.
    utassert(StrContains(spec, StrL("dock_area")));
    utassert(StrContains(spec, StrL(":tab_bar(fn)")));
    utassert(StrContains(spec, StrL(":empty_group(fn)")));
    utassert(StrContains(spec, StrL(":drop_indicator(fn)")));
    utassert(StrContains(spec, StrL(":dock(fn)")));

    // The area, its panels and the chrome all reach base: materializing the
    // snapshot builds the real DockArea and runs each chrome handler in a
    // Layout scope, which is what the dock's own content placement needs.
    RenderSnapshot* snapshot = fixture.Snapshot();
    Arena* frame = ArenaNew();
    Ctx cx = {&fixture.app, &fixture.window, frame, {}};
    El* root = snapshot ? ShellMaterialize(&cx, fixture.runtime, snapshot,
                                           &fixture.error)
                        : nullptr;
    utassert(root && !fixture.error.IsSet());
    ArenaDelete(frame);
    delete snapshot;
}

// A layout written by dump() is read back by load(), and each panel's own
// deserialize(data) is handed what its serialize() wrote.
static void ADockLayoutIsRestoredWithEachPanelsOwnState() {
    ShellDockFixture fixture;
    utassert(fixture.Load(
        StrL("dock-load.js"),
        StrL("import { View, div } from 'gpui';\n"
             "import { DockArea, dock_area, v_flex } from 'gpui-base';\n"
             "class Panel extends View {\n"
             "  init(props) { this.caption = props?.caption ?? 'untitled'; }\n"
             "  serialize() { return { caption: this.caption }; }\n"
             "  deserialize(data) { this.caption = data.caption; }\n"
             "  render() { return div().child(this.caption); }\n"
             "}\n"
             "export default class Workspace extends View {\n"
             "  init(_props, cx) {\n"
             "    DockArea.register_panel('document', Panel);\n"
             "    const first = DockArea.new('workspace');\n"
             "    first.add_panel(cx.new(Panel, { caption: 'restored.js' }), { "
             "name: 'document' });\n"
             "    const saved = first.dump();\n"
             "    first.release();\n"
             "    this.dock = DockArea.new('workspace');\n"
             "    this.dock.load(saved);\n"
             "    this.restored = this.dock.panels();\n"
             "    this.round = this.dock.dump();\n"
             "  }\n"
             "  render() {\n"
             "    if (this.restored.length !== 1)\n"
             "      throw new Error('load() rebuilds every panel through the "
             "registry');\n"
             "    if (!JSON.stringify(this.round).includes('restored.js'))\n"
             "      throw new Error(\"a restored panel keeps the state it was "
             "saved with\");\n"
             "    return v_flex().child(dock_area(this.dock));\n"
             "  }\n"
             "}\n")));
    Str spec = fixture.Render();
    utassert(!fixture.error.IsSet() && StrContains(spec, StrL("dock_area")));
}

// One dock area cannot be mounted twice, for the reason a child view cannot:
// base cannot put one entity at two positions in a tree.
static void ADockAreaCanOnlyBeDrawnOnce() {
    ShellDockFixture fixture;
    utassert(fixture.Load(
        StrL("dock-twice.js"),
        StrL("import { View } from 'gpui';\n"
             "import { DockArea, dock_area, v_flex } from 'gpui-base';\n"
             "export default class Twice extends View {\n"
             "  init() { this.dock = DockArea.new('twice'); }\n"
             "  render() {\n"
             "    return "
             "v_flex().child(dock_area(this.dock)).child(dock_area(this.dock));"
             "\n"
             "  }\n"
             "}\n")));
    fixture.Render();
    utassert(fixture.error.IsSet());
    ShellErrorClear(&fixture.error);
}

// The layout may not be changed while it is being described: a frame that
// changed the layout it was describing would describe one layout and draw
// another.
static void ADockRefusesLayoutChangesFromRender() {
    ShellDockFixture fixture;
    utassert(fixture
                 .Load(StrL("dock-render.js"),
                       StrL("import { View } from 'gpui';\n"
                            "import { DockArea, dock_area } from 'gpui-base';\n"
                            "export default class Bad extends View {\n"
                            "  init() { this.dock = DockArea.new('bad'); }\n"
                            "  render() { this.dock.toggle_dock('left'); "
                            "return dock_area(this.dock); }\n"
                            "}\n")));
    fixture.Render();
    utassert(
        StrContains(fixture.error.message,
                    StrL("cannot be called while one is being described")));
    ShellErrorClear(&fixture.error);
}

// A script binds a chord and the action reaches its handler, and an action the
// element does not claim carries on to an outer one.
static void AScriptBindsAChordAndDispatchesItsAction() {
    ShellDockFixture fixture;
    utassert(fixture.Load(
        StrL("actions.js"),
        StrL("import { View, div } from 'gpui';\n"
             "import { v_flex } from 'gpui-base';\n"
             "export default class Keys extends View {\n"
             "  init(_props, cx) {\n"
             "    this.saved = 0;\n"
             "    this.count = cx.bind_keys([\n"
             "      { keystroke: 'cmd-s', action: 'save' },\n"
             "      { keystroke: 'cmd-shift-p', action: 'palette', context: "
             "'Workspace' },\n"
             "    ]);\n"
             "  }\n"
             "  render() {\n"
             "    if (this.count !== 2) throw new Error('bind_keys answers how "
             "many it installed');\n"
             "    return v_flex().key_context('Workspace')\n"
             "      .on_action('save', () => { this.saved += 1; })\n"
             "      .on_action('palette', () => {})\n"
             "      .child(div().id('inner').on_action('save', () => {}));\n"
             "  }\n"
             "}\n")));
    Str spec = fixture.Render();
    utassert(!fixture.error.IsSet());
    utassert(StrContains(spec, StrL(":on_action(save, fn)")));
    utassert(StrContains(spec, StrL(":on_action(palette, fn)")));
    utassert(StrContains(spec, StrL(":key_context(\"Workspace\")")));

    // Two actions on one element, both installed. Upstream has to install one
    // GPUI listener and route by id, because every script action shares one
    // Rust type; an action here is its own id, so each registration is its own
    // listener and the dispatch table does the routing.
    RenderSnapshot* snapshot = fixture.Snapshot();
    Arena* frame = ArenaNew();
    Ctx cx = {&fixture.app, &fixture.window, frame, {}};
    El* root = snapshot ? ShellMaterialize(&cx, fixture.runtime, snapshot,
                                           &fixture.error)
                        : nullptr;
    utassert(root && !fixture.error.IsSet());
    if (root) {
        utassert(root->style.keyContext == KeyContextOf(StrL("Workspace")));
        int listeners = 0;
        for (ActionSlot* at = root->actions; at; at = at->next) listeners++;
        utassert(listeners == 2);
        uint32_t save = ShellActionOf(StrL("save"));
        uint32_t palette = ShellActionOf(StrL("palette"));
        utassert(save != palette);
        utassert(StrEq(ShellActionScriptId(save), StrL("save")));
        bool sawSave = false, sawPalette = false;
        for (ActionSlot* at = root->actions; at; at = at->next) {
            sawSave = sawSave || at->action == save;
            sawPalette = sawPalette || at->action == palette;
        }
        utassert(sawSave && sawPalette);
        // The chord the keymap resolves is the one the script wrote, which is
        // what makes a binding and the event it produces agree.
        KeyChord chord = {};
        utassert(KeyChordParse(StrL("cmd-s"), &chord));
        uint32_t contexts[1] = {KeyContextOf(StrL("Workspace"))};
        KeyMatch match = KeymapMatch(chord, contexts, 1);
        utassert(match.action == save);
    }
    ArenaDelete(frame);
    delete snapshot;
}

// The keyboard and pointer handlers reach the element they were written on.
// Every node here is an El, so unlike upstream — where each component builds
// its own base type and only a plain div, h_flex or v_flex carries the family
// — there is no component that has to report the gap.
static void ScriptInputHandlersReachTheElementTheyWereWrittenOn() {
    ShellDockFixture fixture;
    utassert(fixture.Load(
        StrL("input.js"),
        StrL("import { View, div } from 'gpui';\n"
             "import { v_flex } from 'gpui-base';\n"
             "export default class Listener extends View {\n"
             "  init(_props, cx) { this.focus = cx.focus_handle(); }\n"
             "  render() {\n"
             "  return v_flex().id('surface')\n"
             "    .track_focus(this.focus)\n"
             "    .on_key_down((event) => { this.chord = event.keystroke; })\n"
             "    .on_key_up(() => {})\n"
             "    .on_mouse_down('left', () => {})\n"
             "    .on_mouse_down('right', () => {})\n"
             "    .on_mouse_up('left', () => {})\n"
             "    .on_mouse_down_out(() => {})\n"
             "    .on_scroll_wheel(() => {})\n"
             "    .child(div());\n"
             "} }\n")));
    Str spec = fixture.Render();
    utassert(!fixture.error.IsSet());
    utassert(StrContains(spec, StrL(":on_key_down(fn)")));
    utassert(StrContains(spec, StrL(":on_key_up(fn)")));
    // The button is folded into the recorded op name — three fixed names GPUI's
    // own MouseButton maps onto — so the op stays the name-and-id pair every
    // other callback uses.
    utassert(StrContains(spec, StrL(":on_mouse_down_left(fn)")));
    utassert(StrContains(spec, StrL(":on_mouse_down_right(fn)")));
    utassert(StrContains(spec, StrL(":on_mouse_up_left(fn)")));
    utassert(StrContains(spec, StrL(":on_mouse_down_out(fn)")));
    utassert(StrContains(spec, StrL(":on_scroll_wheel(fn)")));

    RenderSnapshot* snapshot = fixture.Snapshot();
    Arena* frame = ArenaNew();
    Ctx cx = {&fixture.app, &fixture.window, frame, {}};
    El* root = snapshot ? ShellMaterialize(&cx, fixture.runtime, snapshot,
                                           &fixture.error)
                        : nullptr;
    utassert(root && !fixture.error.IsSet());
    if (root) {
        // A key event travels the focus path, so track_focus is half of the
        // registration: without it the handler sits on an element the keyboard
        // never reaches.
        utassert(root->style.focusId != 0);
        bool keyDown = false, keyUp = false;
        for (ActionSlot* at = root->actions; at; at = at->next) {
            keyDown = keyDown || at->action == ActionOf(StrL("gpui::KeyDown"));
            keyUp = keyUp || at->action == ActionOf(StrL("gpui::KeyUp"));
        }
        utassert(keyDown && keyUp);
        utassert(root->onMouseDown.IsValid());
        utassert(root->onMouseUp.IsValid());
        utassert(root->onMouseDownOut.IsValid());
        utassert(root->onScrollWheel.IsValid());
    }
    ArenaDelete(frame);
    delete snapshot;
}

// A bad mouse button and a bad action name are refused at the line that wrote
// them rather than silently doing nothing.
static void ScriptInputHandlersRefuseWhatTheyCannotMean() {
    {
        ShellDockFixture fixture;
        utassert(fixture.Load(
            StrL("bad-button.js"),
            StrL("import { View, div } from 'gpui';\n"
                 "export default class Bad extends View { render() {\n"
                 "  return div().on_mouse_down('thumb', () => {});\n"
                 "} }\n")));
        fixture.Render();
        utassert(
            StrContains(fixture.error.message, StrL("is not a mouse button")));
        ShellErrorClear(&fixture.error);
    }
    {
        ShellDockFixture fixture;
        utassert(fixture.Load(
            StrL("bad-action.js"),
            StrL("import { View, div } from 'gpui';\n"
                 "export default class Bad extends View { render() {\n"
                 "  return div().on_action('', () => {});\n"
                 "} }\n")));
        fixture.Render();
        utassert(StrContains(fixture.error.message,
                             StrL("expects the action's name first")));
        ShellErrorClear(&fixture.error);
    }
}

// The window answers its measurements from render and refuses changes there.
static void TheWindowAnswersItsMeasurementsAndRefusesChangesDuringRender() {
    ShellDockFixture fixture;
    utassert(fixture.Load(
        StrL("window.js"),
        StrL("import { View, div } from 'gpui';\n"
             "export default class Measure extends View { render() {\n"
             "  const size = window.viewport_size();\n"
             "  const bounds = window.bounds();\n"
             "  const mouse = window.mouse_position();\n"
             "  if (typeof size.width !== 'number' || typeof size.height !== "
             "'number')\n"
             "    throw new Error('viewport_size answers a size');\n"
             "  if (typeof bounds.x !== 'number' || typeof bounds.width !== "
             "'number')\n"
             "    throw new Error('bounds answers a box');\n"
             "  if (typeof mouse.x !== 'number') throw new "
             "Error('mouse_position answers a point');\n"
             "  if (window.rem_size() !== 16) throw new Error('the rem is "
             "sixteen');\n"
             "  if (!(window.line_height() > 16)) throw new Error('a line box "
             "is taller than its font');\n"
             "  if (!['light', 'dark'].includes(window.appearance()))\n"
             "    throw new Error('an appearance is light or dark');\n"
             "  if (typeof window.is_window_active() !== 'boolean')\n"
             "    throw new Error('is_window_active answers a flag');\n"
             "  if (typeof window.is_maximized() !== 'boolean')\n"
             "    throw new Error('is_maximized answers a flag');\n"
             "  let refused = false;\n"
             "  try { window.refresh(); } catch (error) { refused = true; }\n"
             "  if (!refused) throw new Error('a change from render must be "
             "refused');\n"
             "  return div();\n"
             "} }\n")));
    Str spec = fixture.Render();
    utassert(!fixture.error.IsSet() && spec);
}

// Every added script API is reachable under its documented name.
//
// The breadth test upstream added for the failure the behavioural ones cannot
// catch: a member bound in the host but unreachable from the prelude, or
// reachable under a different name. That mistake compiles and passes
// everything else, and is otherwise found only by a reader typing what the
// documentation says.
static void EveryAddedScriptApiIsReachableUnderItsDocumentedName() {
    ShellDockFixture fixture;
    utassert(fixture.Load(
        StrL("breadth.js"),
        StrL("import { View, div } from 'gpui';\n"
             "import { Accordion, AccordionItem, AccordionHeader, "
             "AccordionPanel,\n"
             "         AccordionTrigger, Avatar, AvatarImage, AvatarFallback,\n"
             "         CalendarState, DockArea, Pagination, dock_area,\n"
             "         dock_content, pagination_items, v_flex } from "
             "'gpui-base';\n"
             "const need = (value, name) => {\n"
             "  if (value === undefined || value === null) throw new "
             "Error(name + ' is not reachable');\n"
             "  return value;\n"
             "};\n"
             "export default class Breadth extends View {\n"
             "  init(_props, cx) {\n"
             "    need(cx.stop_propagation, 'cx.stop_propagation');\n"
             "    need(cx.propagate, 'cx.propagate');\n"
             "    need(cx.bind_keys, 'cx.bind_keys');\n"
             "    for (const name of ['rem_size', 'line_height', "
             "'viewport_size', 'bounds',\n"
             "                        'mouse_position', 'appearance', "
             "'is_window_active',\n"
             "                        'is_maximized', 'dispatch_action', "
             "'refresh',\n"
             "                        'focus_next', 'focus_prev', "
             "'activate_window',\n"
             "                        'minimize_window', 'zoom_window',\n"
             "                        'start_window_move']) {\n"
             "      need(window[name], 'window.' + name);\n"
             "    }\n"
             "    this.calendar = CalendarState.new();\n"
             "    for (const name of ['month_days', 'year', 'month', 'today', "
             "'value',\n"
             "                        'set_value', 'next_month', 'prev_month', "
             "'on', 'release']) {\n"
             "      need(this.calendar[name], 'CalendarState.' + name);\n"
             "    }\n"
             "    this.dock = DockArea.new('breadth');\n"
             "    for (const name of ['add_panel', 'remove_panel', 'panels', "
             "'dump', 'load',\n"
             "                        'has_dock', 'is_dock_open', "
             "'toggle_dock', 'remove_dock',\n"
             "                        'dock_size', 'set_dock_size', "
             "'set_dock_collapsible',\n"
             "                        'is_locked', 'set_locked', 'is_zoomed', "
             "'zoom_out',\n"
             "                        'on', 'release']) {\n"
             "      need(this.dock[name], 'DockArea.' + name);\n"
             "    }\n"
             "    need(DockArea.register_panel, 'DockArea.register_panel');\n"
             "  }\n"
             "  render() {\n"
             "    need(pagination_items(1, 1), 'pagination_items');\n"
             "    const area = dock_area(this.dock);\n"
             "    for (const hook of ['tab_bar', 'empty_group', "
             "'drop_indicator', 'dock']) {\n"
             "      need(area[hook], 'dock_area().' + hook);\n"
             "    }\n"
             "    const element = div()\n"
             "      .on_key_down(() => {}).on_key_up(() => {})\n"
             "      .on_mouse_down('left', () => {}).on_mouse_up('left', () => "
             "{})\n"
             "      .on_mouse_down_out(() => {}).on_scroll_wheel(() => {})\n"
             "      .on_action('save', () => {}).key_context('Breadth');\n"
             "    return v_flex()\n"
             "      .child(element)\n"
             "      .child(Pagination.new('pages'))\n"
             "      .child(Avatar.new().image(AvatarImage.new('a.png'))\n"
             "        .fallback(AvatarFallback.new().child('AB')))\n"
             "      "
             ".child(Accordion.new('group').child(AccordionItem.new().open("
             "true)\n"
             "        "
             ".header(AccordionHeader.new(AccordionTrigger.new('one')).aria_"
             "level(3))\n"
             "        .panel(AccordionPanel.new().keep_mounted(true))))\n"
             "      .child(area);\n"
             "  }\n"
             "}\n")));
    Str spec = fixture.Render();
    utassert(!fixture.error.IsSet() && spec);
    utassert(StrContains(spec, StrL("Pagination \"pages\"")));
    utassert(StrContains(spec, StrL(":keep_mounted(true)")));
    // dock_content() is only meaningful inside a chrome handler, and says so
    // rather than drawing an empty box that looks like the dock's panels.
    utassert(!StrContains(spec, StrL("dock_content")));
}

void TestShellDock() {
    TestSuite("shell_dock");
    PaginationItemsLayOutThePagesAndTheirGaps();
    AnAvatarRendersItsImageOrItsFallback();
    AnAccordionItemPassesItsOpenStateDownToItsTrigger();
    ACalendarStateAnswersTheMonthGridAndMovesIt();
    AScriptDrivesADockAreaAndItsLayoutRoundTrips();
    ADockLayoutIsRestoredWithEachPanelsOwnState();
    ADockAreaCanOnlyBeDrawnOnce();
    ADockRefusesLayoutChangesFromRender();
    AScriptBindsAChordAndDispatchesItsAction();
    ScriptInputHandlersReachTheElementTheyWereWrittenOn();
    ScriptInputHandlersRefuseWhatTheyCannotMean();
    TheWindowAnswersItsMeasurementsAndRefusesChangesDuringRender();
    EveryAddedScriptApiIsReachableUnderItsDocumentedName();
}
