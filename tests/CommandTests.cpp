/* Ported from crates/ui/src/command.
 *
 * The palette's model: which items a query leaves, what the rows around them
 * come to — a heading only for a group that kept something, a separator only
 * where something follows it — and where the highlight goes. An item's index
 * path is its place in the model as it was given, before any filtering, which
 * is what keeps a filtered selection pointing at the right business object. */

#include "Test.h"

using namespace gpui;
using namespace gpui::component;

static const Str kEmojiKeywords[] = {StrL("smile"), StrL("icon")};

static const CommandItem kSuggestions[] = {
    {StrL("Calendar")},
    {StrL("Search Emoji"), kEmojiKeywords, 2},
    // The disabled one, which the highlight skips.
    {StrL("Calculator"), nullptr, 0, IconName::None, 0, 0, nullptr, false,
     true},
};
static const CommandItem kSettings[] = {
    {StrL("Profile")},
    {StrL("Billing")},
};
static const CommandGroup kSuggestionsGroup = {StrL("Suggestions"),
                                               kSuggestions, 3};
static const CommandGroup kSettingsGroup = {StrL("Settings"), kSettings, 2};

static void Install(CommandState* s, const CommandEntry* entries, int n,
                    const char* query) {
    InputSetValue(&s->query, query ? Str(query) : Str{});
    CommandInstall(s, nullptr, entries, n, true);
}

static void TheQueryIsACaseInsensitiveSubstringOfTheLabelOrAKeyword() {
    utassert(CommandItemMatches(&kSuggestions[0], StrL("cal")));
    utassert(CommandItemMatches(&kSuggestions[0], StrL("CALENDAR")));
    // The keywords are searched beside the label, so "smile" finds the emoji
    // item whose label does not contain it.
    utassert(CommandItemMatches(&kSuggestions[1], StrL("smile")));
    utassert(!CommandItemMatches(&kSuggestions[1], StrL("smiley")));
    // An empty query matches everything, as `contains("")` does.
    utassert(CommandItemMatches(&kSuggestions[2], StrL("")));
}

static void GroupsFlattenIntoHeadingsAndItems() {
    CommandEntry entries[2] = {CommandEntryOf(kSuggestionsGroup),
                               CommandEntryOf(kSettingsGroup)};
    CommandState s;
    Install(&s, entries, 2, nullptr);
    // Two headings and five items.
    utassert(s.rows.len == 7);
    utassert(s.rows[0].kind == CommandRowKind::Heading);
    utassert(s.rows[1].kind == CommandRowKind::Item);
    utassert(s.rows[4].kind == CommandRowKind::Heading);
    utassert(CommandMatchedCount(&s) == 5);
    // Group and item positions, with no ungrouped section in front of them.
    utassert(s.matched[0].path.section == 0 && s.matched[0].path.row == 0);
    utassert(s.matched[3].path.section == 1 && s.matched[3].path.row == 0);
}

static void AHeadingIsHiddenWhileItsGroupIsFilteredOut() {
    CommandEntry entries[2] = {CommandEntryOf(kSuggestionsGroup),
                               CommandEntryOf(kSettingsGroup)};
    CommandState s;
    Install(&s, entries, 2, "profile");
    // Only the Settings group kept anything, so only its heading is drawn.
    utassert(s.rows.len == 2);
    utassert(s.rows[0].kind == CommandRowKind::Heading);
    utassert(CommandMatchedCount(&s) == 1);
    // And the item still reports where it was given, not where it landed.
    utassert(s.matched[0].path.section == 1 && s.matched[0].path.row == 0);
}

static void UngroupedItemsKeepTheirGivenRow() {
    CommandItem items[3] = {{StrL("Alpha")}, {StrL("Beta")}, {StrL("Gamma")}};
    CommandEntry entries[3] = {CommandEntryOf(items[0]),
                               CommandEntryOf(items[1]),
                               CommandEntryOf(items[2])};
    CommandState s;
    Install(&s, entries, 3, "gam");
    utassert(CommandMatchedCount(&s) == 1);
    // Section 0 and the position it was given — filtering does not renumber
    // it to 0, which is the whole point of the input-model coordinates.
    utassert(s.matched[0].path.section == 0 && s.matched[0].path.row == 2);
}

static void AnUngroupedSectionComesBeforeTheGroups() {
    CommandItem loose = {StrL("Loose")};
    CommandEntry entries[2] = {CommandEntryOf(loose),
                               CommandEntryOf(kSettingsGroup)};
    CommandState s;
    Install(&s, entries, 2, nullptr);
    utassert(s.matched[0].path.section == 0);
    // The group follows the implicit ungrouped section.
    utassert(s.matched[1].path.section == 1);
}

static void ASeparatorIsDrawnOnlyWhereSomethingFollowsIt() {
    CommandEntry entries[5] = {
        CommandSeparatorEntry(), CommandEntryOf(kSuggestionsGroup),
        CommandSeparatorEntry(), CommandEntryOf(kSettingsGroup),
        CommandSeparatorEntry()};
    CommandState s;
    Install(&s, entries, 5, nullptr);
    // The leading and the trailing one are dropped; the one between the two
    // groups is kept.
    int separators = 0;
    for (int i = 0; i < s.rows.len; i++) {
        if (s.rows[i].kind == CommandRowKind::Separator) {
            separators++;
        }
    }
    utassert(separators == 1);
    utassert(s.rows[0].kind == CommandRowKind::Heading);
    utassert(s.rows[s.rows.len - 1].kind == CommandRowKind::Item);

    // And a query that empties the second group drops that one too.
    Install(&s, entries, 5, "calendar");
    for (int i = 0; i < s.rows.len; i++) {
        utassert(s.rows[i].kind != CommandRowKind::Separator);
    }
}

static void TheHighlightStartsOnTheFirstItemThatCanBeConfirmed() {
    CommandEntry entries[1] = {CommandEntryOf(kSuggestionsGroup)};
    CommandState s;
    Install(&s, entries, 1, nullptr);
    IndexPath path = {};
    utassert(CommandSelectedIndex(&s, &path));
    utassert(path.section == 0 && path.row == 0);

    // A query that leaves only the disabled item leaves nothing highlighted.
    Install(&s, entries, 1, "calculator");
    utassert(CommandMatchedCount(&s) == 1);
    utassert(!CommandSelectedIndex(&s, &path));
}

static void TheArrowsWrapAroundAndSkipTheDisabled() {
    CommandEntry entries[1] = {CommandEntryOf(kSuggestionsGroup)};
    CommandState s;
    Install(&s, entries, 1, nullptr);
    IndexPath path = {};
    CommandSelectBy(&s, nullptr, 1);
    utassert(CommandSelectedIndex(&s, &path) && path.row == 1);
    // Calculator is disabled, so down from Search Emoji wraps to Calendar.
    CommandSelectBy(&s, nullptr, 1);
    utassert(CommandSelectedIndex(&s, &path) && path.row == 0);
    // And up from the first goes to the last one that is not disabled.
    CommandSelectBy(&s, nullptr, -1);
    utassert(CommandSelectedIndex(&s, &path) && path.row == 1);
}

static void TheHighlightFollowsItsItemAcrossAModelInstall() {
    CommandEntry entries[1] = {CommandEntryOf(kSuggestionsGroup)};
    CommandState s;
    Install(&s, entries, 1, nullptr);
    CommandSelectBy(&s, nullptr, 1);
    IndexPath path = {};
    utassert(CommandSelectedIndex(&s, &path) && path.row == 1);
    // The same model again — the highlight is on the item, not on the row.
    CommandInstall(&s, nullptr, entries, 1, true);
    utassert(CommandSelectedIndex(&s, &path) && path.row == 1);
}

static void AClearedHighlightStaysCleared() {
    CommandEntry entries[1] = {CommandEntryOf(kSuggestionsGroup)};
    CommandState s;
    Install(&s, entries, 1, nullptr);
    CommandSetSelectedIndex(&s, nullptr, nullptr);
    IndexPath path = {};
    utassert(!CommandSelectedIndex(&s, &path));
    // An install does not put it back on the first item.
    CommandInstall(&s, nullptr, entries, 1, true);
    utassert(!CommandSelectedIndex(&s, &path));
    // Naming a path highlights it again.
    IndexPath second = IndexPathNew(1).Section(0);
    CommandSetSelectedIndex(&s, nullptr, &second);
    utassert(CommandSelectedIndex(&s, &path) && path.row == 1);
    // A disabled one does not, and clears what there was.
    IndexPath disabled = IndexPathNew(2).Section(0);
    CommandSetSelectedIndex(&s, nullptr, &disabled);
    utassert(!CommandSelectedIndex(&s, &path));
}

static void AQueryChangeResetsTheHighlight() {
    CommandEntry entries[1] = {CommandEntryOf(kSuggestionsGroup)};
    CommandState s;
    Install(&s, entries, 1, nullptr);
    CommandSelectBy(&s, nullptr, 1);
    IndexPath path = {};
    utassert(CommandSelectedIndex(&s, &path) && path.row == 1);
    // A new query re-filters and puts the highlight on the first item that
    // can be confirmed, rather than carrying the old one across.
    Install(&s, entries, 1, "cal");
    utassert(CommandSelectedIndex(&s, &path) && path.row == 0);
}

static void AnUnfilterablePaletteKeepsEveryItem() {
    CommandEntry entries[2] = {CommandEntryOf(kSuggestionsGroup),
                               CommandEntryOf(kSettingsGroup)};
    CommandState s;
    Install(&s, entries, 2, nullptr);
    IndexPath second = IndexPathNew(1).Section(1);
    CommandSetSelectedIndex(&s, nullptr, &second);

    // "Bil" locally matches only Billing. A palette whose source answers the
    // query keeps every row it was given...
    InputSetValue(&s.query, Str("Bil"));
    CommandInstall(&s, nullptr, entries, 2, true, false);
    utassert(CommandMatchedCount(&s) == 5);
    // ...and the highlight goes back to the first item rather than to the
    // textual match.
    IndexPath path = {};
    utassert(CommandSelectedIndex(&s, &path));
    utassert(path.section == 0 && path.row == 0);

    // The same query with the filtering on is the one item it matches.
    CommandInstall(&s, nullptr, entries, 2, true, true);
    utassert(CommandMatchedCount(&s) == 1);
}

// state.rs after 466e6da8: a hover selection does not scroll, and neither
// does the model reinstall the notify it causes — otherwise the list moves a
// frame later and slides the next row under the resting cursor.
static void AReinstalledModelDoesNotScrollAPreservedSelection() {
    CommandEntry entries[1] = {CommandEntryOf(kSuggestionsGroup)};
    CommandState s;
    Install(&s, entries, 1, nullptr);
    // The install seeds the highlight and asks for the scroll that goes with
    // it; the frame that renders consumes it.
    s.pendingScroll = -1;

    // A pointer-style selection: the highlight moves, nothing scrolls.
    HoverEvent hover = {};
    hover.hovered = true;
    CommandState::OnRowHover(&s, nullptr, &hover, 1);
    IndexPath path = {};
    utassert(CommandSelectedIndex(&s, &path) && path.row == 1);
    utassert(s.pendingScroll == -1);

    // The host re-render that notify causes reinstalls the model with the
    // selection preserved. That must not scroll either.
    CommandInstall(&s, nullptr, entries, 1, true);
    utassert(CommandSelectedIndex(&s, &path) && path.row == 1);
    utassert(s.pendingScroll == -1);

    // Keyboard navigation still reveals what it moves to.
    CommandSelectBy(&s, nullptr, 1);
    utassert(CommandSelectedIndex(&s, &path) && path.row == 0);
    utassert(s.pendingScroll == s.matched[s.selected].row);
}

static void AQueryIsTrimmedBeforeItIsMatched() {
    CommandEntry entries[1] = {CommandEntryOf(kSuggestionsGroup)};
    CommandState s;
    Install(&s, entries, 1, "  calendar  ");
    utassert(CommandMatchedCount(&s) == 1);
}

// A custom row of a stated height: Rust's tests build theirs as
// `div().h(px(84.))`, with the item row's own py_1p5 around it.
static El* TallRow(Ctx* cx, const CommandItem*) {
    return Div(cx->a)->W(kFill)->H(72);
}
static El* ShortRow(Ctx* cx, const CommandItem*) {
    return Div(cx->a)->W(kFill)->H(32);
}

// state.rs reinstalling_an_unchanged_model_keeps_the_measured_rows (#3268):
// a host re-render hands the palette an equal model, and its rows and their
// sizes stay as they were without being measured again. A custom row, a
// changed label and a changed disabled flag are measured again, and the
// matches stay right.
static void ReinstallingAnUnchangedModelKeepsTheMeasuredRows() {
    CommandEntry entries[2] = {CommandEntryOf(kSuggestionsGroup),
                               CommandEntryOf(kSettingsGroup)};
    CommandState s;
    Install(&s, entries, 2, nullptr);
    utassert(CommandMatchedCount(&s) == 5);
    utassert(s.measureCount == 1);
    float sizes[8] = {};
    int nSizes = s.rowSizes.len;
    utassert(nSizes == 7);
    for (int i = 0; i < nSizes && i < 8; i++) {
        sizes[i] = s.rowSizes[i];
    }

    // A freshly built but equal model: no measuring.
    CommandEntry again[2] = {CommandEntryOf(kSuggestionsGroup),
                             CommandEntryOf(kSettingsGroup)};
    Install(&s, again, 2, nullptr);
    utassert(s.measureCount == 1);
    utassert(CommandMatchedCount(&s) == 5);
    utassert(s.rowSizes.len == nSizes);
    for (int i = 0; i < nSizes && i < 8; i++) {
        utassertnear(s.rowSizes[i], sizes[i]);
    }

    // A changed label is measured again.
    CommandItem renamed[2] = {{StrL("Profile and account")}, {StrL("Billing")}};
    CommandGroup renamedGroup = {StrL("Settings"), renamed, 2};
    CommandEntry relabeled[2] = {CommandEntryOf(kSuggestionsGroup),
                                 CommandEntryOf(renamedGroup)};
    Install(&s, relabeled, 2, nullptr);
    utassert(s.measureCount == 2);
    // So is a changed disabled flag, and it is what the matches see.
    renamed[1].disabled = true;
    Install(&s, relabeled, 2, nullptr);
    utassert(s.measureCount == 3);
    utassert(s.matched[4].disabled);
    Install(&s, relabeled, 2, nullptr);
    utassert(s.measureCount == 3);

    // A new query is new rows.
    Install(&s, relabeled, 2, "bill");
    utassert(s.measureCount == 4);
    utassert(s.rowSizes.len == s.rows.len);
}

// state.rs custom rows: each one is laid out on its own and the list is
// handed the height it came to, the standard rows beside them keeping
// theirs — no height is stated by the caller. A model with a custom row is
// measured on every install, since the row can read state outside the item.
static void ACustomRowIsMeasured() {
    App app = {};
    component::Init(&app);
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.app = &app;
    cx.a = a;

    CommandItem tall = {StrL("Tall")};
    tall.content = &TallRow;
    CommandItem shortRow = {StrL("Short")};
    shortRow.content = &ShortRow;
    CommandItem plain = {StrL("Plain")};
    CommandEntry entries[4] = {CommandEntryOf(tall), CommandSeparatorEntry(),
                               CommandEntryOf(plain), CommandEntryOf(shortRow)};
    CommandState s;
    CommandInstall(&s, &cx, entries, 4, true);
    utassert(s.rows.len == 4 && s.rowSizes.len == 4);
    utassertnear(s.rowSizes[0], 84.f);
    utassertnear(s.rowSizes[1], 9.f);
    utassertnear(s.rowSizes[2], 14.f * kLineHeight + 12.f);
    utassertnear(s.rowSizes[3], 44.f);
    int measured = s.measureCount;
    CommandInstall(&s, &cx, entries, 4, true);
    utassert(s.measureCount == measured + 1);

    // The rows are measured at the width the list's content had last
    // frame, and a new width is measured again.
    CommandItem plainOnly[1] = {{StrL("Plain")}};
    CommandEntry one[1] = {CommandEntryOf(plainOnly[0])};
    CommandState w;
    CommandInstall(&w, &cx, one, 1, true);
    utassert(w.measuredW < 0);
    int before = w.measureCount;
    w.listW = 240;
    CommandInstall(&w, &cx, one, 1, true);
    utassert(w.measureCount == before + 1 && w.measuredW == 240.f);
    CommandInstall(&w, &cx, one, 1, true);
    utassert(w.measureCount == before + 1);

    AppGlobalClear(&app);
    ArenaDelete(a);
}

// impl Styled for Command: `.refine_style(&self.options.style)` lands on the
// palette's box after its popover surface and its border, so a caller's
// `min_h` or `rounded` wins over what the palette chose. And
// `.text_color(popover_foreground)` is what the rows inherit.
static void ACommandIsStyled() {
    App app = {};
    component::Init(&app);
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.app = &app;
    cx.a = a;
    const Theme& th = ThemeNow(&app);

    Style refine = {};
    refine.minH = 320;
    refine.radius = 0;
    El* box = Command::New(&cx, StrL("styled"), {})
                  ->Refine(refine, StyleFieldMinHeight)
                  ->Refine(refine, StyleFieldRadius)
                  ->IntoEl();
    utassert(box->style.hasColor && RgbaEq(box->style.color, th.popoverFg));
    utassertnear(box->style.radius, th.radiusLg);
    ElStyleStates* states = box->StyleStates();
    utassert(states &&
             states->refineSet == (StyleFieldMinHeight | StyleFieldRadius));
    utassert(states && states->refine.minH == 320.f);
    utassert(states && states->refine.radius == 0.f);

    // Unstyled, the palette carries no refinement at all.
    El* plain = Command::New(&cx, StrL("plain"), {})->IntoEl();
    utassert(!plain->StyleStates() || plain->StyleStates()->refineSet == 0);

    // The new size fields refine the way the rest do, fraction and all.
    Style over = {};
    over.minW = 10;
    over.maxW = 20;
    over.maxWFrac = 0.5f;
    over.maxH = 30;
    over.relLengths = kRelMinW;
    Style into = {};
    StyleApplyFields(
        &into, over,
        StyleFieldMinWidth | StyleFieldMaxWidth | StyleFieldMaxHeight);
    utassert(into.minW == 10.f && into.maxW == 20.f && into.maxWFrac == 0.5f &&
             into.maxH == 30.f);
    utassert(into.minH == kAuto && into.relLengths == kRelMinW);

    AppGlobalClear(&app);
    ArenaDelete(a);
}

void TestCommand() {
    TestSuite("command");
    ACommandIsStyled();
    TheQueryIsACaseInsensitiveSubstringOfTheLabelOrAKeyword();
    GroupsFlattenIntoHeadingsAndItems();
    ReinstallingAnUnchangedModelKeepsTheMeasuredRows();
    ACustomRowIsMeasured();
    AHeadingIsHiddenWhileItsGroupIsFilteredOut();
    UngroupedItemsKeepTheirGivenRow();
    AnUngroupedSectionComesBeforeTheGroups();
    ASeparatorIsDrawnOnlyWhereSomethingFollowsIt();
    TheHighlightStartsOnTheFirstItemThatCanBeConfirmed();
    TheArrowsWrapAroundAndSkipTheDisabled();
    TheHighlightFollowsItsItemAcrossAModelInstall();
    AClearedHighlightStaysCleared();
    AQueryChangeResetsTheHighlight();
    AnUnfilterablePaletteKeepsEveryItem();
    AReinstalledModelDoesNotScrollAPreservedSelection();
    AQueryIsTrimmedBeforeItIsMatched();
}
