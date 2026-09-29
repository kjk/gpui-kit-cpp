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

static El* CustomRow(Ctx*, const CommandItem*) {
    return nullptr;
}

// state.rs reinstalling_an_unchanged_model_keeps_the_measured_rows (#3268):
// a host re-render hands the palette an equal model, and its rows and their
// sizes stay as they were; a custom row keeps the height its item states.
// Rust also asserts when rows are measured again;
// rows are never measured here (contentH is the custom row's height), so
// what is left to check is the answer.
static void ReinstallingAnUnchangedModelKeepsItsRows() {
    CommandEntry entries[2] = {CommandEntryOf(kSuggestionsGroup),
                               CommandEntryOf(kSettingsGroup)};
    CommandState s;
    Install(&s, entries, 2, nullptr);
    utassert(CommandMatchedCount(&s) == 5);
    float sizes[8] = {};
    int nSizes = s.rowSizes.len;
    utassert(nSizes == 7);
    for (int i = 0; i < nSizes && i < 8; i++) {
        sizes[i] = s.rowSizes[i];
    }

    CommandEntry again[2] = {CommandEntryOf(kSuggestionsGroup),
                             CommandEntryOf(kSettingsGroup)};
    Install(&s, again, 2, nullptr);
    utassert(CommandMatchedCount(&s) == 5);
    utassert(s.rowSizes.len == nSizes);
    for (int i = 0; i < nSizes && i < 8; i++) {
        utassertnear(s.rowSizes[i], sizes[i]);
    }

    CommandItem custom = {StrL("Custom")};
    custom.content = &CustomRow;
    custom.contentH = 44;
    CommandEntry one[1] = {CommandEntryOf(custom)};
    Install(&s, one, 1, nullptr);
    utassert(CommandMatchedCount(&s) == 1);
    utassert(s.rowSizes.len == 1);
    utassertnear(s.rowSizes[0], 44.f);
}

void TestCommand() {
    TestSuite("command");
    TheQueryIsACaseInsensitiveSubstringOfTheLabelOrAKeyword();
    GroupsFlattenIntoHeadingsAndItems();
    ReinstallingAnUnchangedModelKeepsItsRows();
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
