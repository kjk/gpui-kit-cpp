#include "shell/a11y.h"

namespace gpui::shell {

static const char kRoleNames[] =
    "Unknown\0TextRun\0Cell\0Label\0Image\0Link\0Row\0ListItem\0ListMarker\0"
    "TreeItem\0ListBoxOption\0MenuItem\0MenuListOption\0Paragraph\0CheckBox\0"
    "RadioButton\0TextInput\0Button\0DefaultButton\0Pane\0RowHeader\0"
    "ColumnHeader\0RowGroup\0List\0Table\0LayoutTableCell\0LayoutTableRow\0"
    "LayoutTable\0Switch\0Menu\0MultilineTextInput\0SearchInput\0DateInput\0"
    "DateTimeInput\0WeekInput\0MonthInput\0TimeInput\0EmailInput\0NumberInput\0"
    "PasswordInput\0PhoneNumberInput\0UrlInput\0Abbr\0Alert\0AlertDialog\0"
    "Application\0Article\0Audio\0Banner\0Blockquote\0Canvas\0Caption\0Caret\0"
    "Code\0ColorWell\0ComboBox\0EditableComboBox\0Complementary\0Comment\0"
    "ContentDeletion\0ContentInsertion\0ContentInfo\0Definition\0DescriptionLis"
    "t\0"
    "Details\0Dialog\0DisclosureTriangle\0Document\0EmbeddedObject\0Emphasis\0"
    "Feed\0FigureCaption\0Figure\0Footer\0Form\0Grid\0GridCell\0Group\0Header\0"
    "Heading\0Iframe\0IframePresentational\0ImeCandidate\0Keyboard\0Legend\0"
    "LineBreak\0ListBox\0Log\0Main\0Mark\0Marquee\0Math\0MenuBar\0"
    "MenuItemCheckBox\0MenuItemRadio\0MenuListPopup\0Meter\0Navigation\0Note\0"
    "PluginObject\0ProgressIndicator\0RadioGroup\0Region\0RootWebArea\0Ruby\0"
    "RubyAnnotation\0ScrollBar\0ScrollView\0Search\0Section\0SectionFooter\0"
    "SectionHeader\0Slider\0SpinButton\0Splitter\0Status\0Strong\0Suggestion\0"
    "SvgRoot\0Tab\0TabList\0TabPanel\0Term\0Time\0Timer\0TitleBar\0Toolbar\0"
    "Tooltip\0Tree\0TreeGrid\0Video\0WebView\0Window\0PdfActionableHighlight\0"
    "PdfRoot\0GraphicsDocument\0GraphicsObject\0GraphicsSymbol\0DocAbstract\0"
    "DocAcknowledgements\0DocAfterword\0DocAppendix\0DocBackLink\0DocBiblioEntr"
    "y\0"
    "DocBibliography\0DocBiblioRef\0DocChapter\0DocColophon\0DocConclusion\0"
    "DocCover\0DocCredit\0DocCredits\0DocDedication\0DocEndnote\0DocEndnotes\0"
    "DocEpigraph\0DocEpilogue\0DocErrata\0DocExample\0DocFootnote\0DocForeword"
    "\0"
    "DocGlossary\0DocGlossRef\0DocIndex\0DocIntroduction\0DocNoteRef\0DocNotice"
    "\0"
    "DocPageBreak\0DocPageFooter\0DocPageHeader\0DocPageList\0DocPart\0DocPrefa"
    "ce\0"
    "DocPrologue\0DocPullquote\0DocQna\0DocSubtitle\0DocTip\0DocToc\0ListGrid\0"
    "Terminal\0";

static bool RoleNameMatches(Str snake, const char* variantName) {
    if (!snake || !variantName) return false;
    int at = 0;
    for (int i = 0; variantName[i]; i++) {
        char ch = variantName[i];
        if (ch >= 'A' && ch <= 'Z') {
            if (i > 0) {
                if (at >= len(snake) || snake.s[at] != '_') return false;
                at++;
            }
            ch = (char)(ch - 'A' + 'a');
        }
        if (at >= len(snake) || snake.s[at] != ch) return false;
        at++;
    }
    return at == len(snake);
}

AccessibilityRole AccessibilityRoleFromName(Str name) {
    if (!name) return AccessibilityRole::None;
    int role = 1;
    for (const char* at = kRoleNames; *at; at += strlen(at) + 1, role++) {
        if (RoleNameMatches(name, at)) return (AccessibilityRole)role;
    }
    return AccessibilityRole::None;
}

int AccessibilityRoleNameCount() {
    int count = 0;
    for (const char* at = kRoleNames; *at; at += strlen(at) + 1) count++;
    return count;
}

} // namespace gpui::shell
