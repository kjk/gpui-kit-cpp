#include "Story.h"

// One entry per section, in the order the Rust story renders them.
enum {
    DlgDefault = 0,
    DlgCustomButtons,
    DlgScrollable,
    DlgTable,
    DlgNoTitle,
    DlgPadding,
    DlgStyle,
    DlgContent,
    DlgTextView,
};

enum {
    DlgOptOverlay = ToolbarOptMultiple,
    DlgOptOverlayClosable = ToolbarOptIcon,
    DlgOptCloseButton = ToolbarOptDisabled,
    DlgOptKeyboard = ToolbarOptBordered
};

struct DialogStory {
    int open = -1;
    bool otherOpen = false;
    bool overlay = true;
    bool overlayClosable = true;
    bool closeButton = true;
    bool keyboard = true;
    InputState focusInput;
    InputState basicInput;
    Entity<component::SelectState> basicSelect = {};
    Entity<TableState> table = {};
    LocalDate basicDate = {};
    bool basicDateOpen = false;
    float dialogScrollY = 0;
    bool seeded = false;
    StoryToolbarState toolbar;

    static El* Render(DialogStory* self, Ctx* cx);
};

static component::SearchableItem gDialogOptions[3];

static void DlgToolbarAct(DialogStory* self, Ctx* cx, const ClickEvent*,
                          intptr_t act) {
    switch (act) {
        case DlgOptOverlay:
            self->overlay = !self->overlay;
            break;
        case DlgOptOverlayClosable:
            self->overlayClosable = !self->overlayClosable;
            break;
        case DlgOptCloseButton:
            self->closeButton = !self->closeButton;
            break;
        case DlgOptKeyboard:
            self->keyboard = !self->keyboard;
            break;
        default:
            StoryToolbarApply(&self->toolbar, nullptr, (int)act);
            break;
    }
    Notify(cx);
}

static void OpenDialog(DialogStory* self, Ctx* cx, const ClickEvent*,
                       intptr_t which) {
    self->open = (int)which;
    self->otherOpen = false;
    Notify(cx);
}

static void ResetDialogState(DialogStory* self, Ctx* cx) {
    self->open = -1;
    self->otherOpen = false;
    self->basicInput.focused = false;
    self->basicDateOpen = false;
    if (component::SelectState* state = self->basicSelect.Get(cx)) {
        state->state.open = false;
    }
}

static void CloseDialog(DialogStory* self, Ctx* cx, const ClickEvent*) {
    ResetDialogState(self, cx);
    Notify(cx);
}

static void ConfirmBasicDialog(DialogStory* self, Ctx* cx, const ClickEvent*) {
    StoryPushNotification(cx, StrL("You have pressed confirm."));
    ResetDialogState(self, cx);
    Notify(cx);
}

static void OpenOtherDialog(DialogStory* self, Ctx* cx, const ClickEvent*) {
    self->otherOpen = true;
    Notify(cx);
}

static void CloseOtherDialog(DialogStory* self, Ctx* cx, const ClickEvent*) {
    self->otherOpen = false;
    Notify(cx);
}

static void PressLater(DialogStory* self, Ctx* cx, const ClickEvent*) {
    StoryPushNotification(cx, StrL("You have pressed later."));
    ResetDialogState(self, cx);
    Notify(cx);
}

static void PressRestart(DialogStory* self, Ctx* cx, const ClickEvent*) {
    StoryPushNotification(cx, StrL("You have pressed restart."));
    ResetDialogState(self, cx);
    Notify(cx);
}

static void RunTestAction(DialogStory*, Ctx* cx, const ClickEvent*) {
    StoryPushNotification(cx, StrL("You have clicked the TestAction."));
    Notify(cx);
}

static void FocusInput(DialogStory* self, Ctx* cx, const ClickEvent*) {
    self->focusInput.focused = true;
    self->basicInput.focused = false;
    Notify(cx);
}

static void FocusBasicInput(DialogStory* self, Ctx* cx, const ClickEvent*) {
    self->focusInput.focused = false;
    self->basicInput.focused = true;
    Notify(cx);
}

static void ToggleBasicSelect(DialogStory* self, Ctx* cx, const ClickEvent*) {
    component::SelectToggleOpen(self->basicSelect.Get(cx), cx);
}

static void ToggleBasicDate(DialogStory* self, Ctx* cx, const ClickEvent*) {
    self->basicDateOpen = !self->basicDateOpen;
    Notify(cx);
}

static void PickBasicDate(DialogStory* self, Ctx* cx, const ClickEvent*,
                          intptr_t day) {
    self->basicDate.day = (int)day;
    self->basicDateOpen = false;
    Notify(cx);
}

static void OnDialogScroll(DialogStory* self, Ctx* cx, const ScrollEvent* ev) {
    self->dialogScrollY = ev->offsetY;
    Notify(cx);
}

static El* StoryDialogTrigger(Ctx* cx, int which, const char* id,
                              const char* label) {
    return component::Button::New(cx, Str(id))
        ->Label(Str(label))
        ->Outline()
        ->OnClick(ListenerArg(Listen(cx, &OpenDialog), which))
        ->IntoEl();
}

static El* DialogTitleText(Ctx* cx, Str text, Rgba color) {
    return StoryTxt(cx, text, 16, color)->Semibold()->LineHeight(1.f);
}

static El* DialogDescriptionText(Ctx* cx, Str text) {
    return StoryTxt(cx, text, 14, ThemeNow(cx->app).mutedFg)->Wrap()->W(kFill);
}

static El* DialogHeader(Ctx* cx, Str title, Str description) {
    El* header = Div(cx->a)->FlexCol()->W(kFill)->Gap(8);
    if (len(title) > 0) {
        header->Child(DialogTitleText(cx, title, ThemeNow(cx->app).foreground));
    }
    if (len(description) > 0) {
        header->Child(DialogDescriptionText(cx, description));
    }
    return header;
}

static El* DialogButton(Ctx* cx, Str id, Str label, Listener onClick,
                        bool primary) {
    component::Button* button =
        component::Button::New(cx, id)->Label(label)->OnClick(onClick);
    if (primary) {
        button->Primary();
    } else {
        button->Outline();
    }
    return button->IntoEl();
}

static component::Dialog* NewOpenDialog(DialogStory*, Ctx* cx) {
    Listener close = Listen(cx, &CloseDialog);
    return component::Dialog::New(cx)
        ->Open(true)
        ->OnClose(close)
        ->OnCancel(close)
        ->OnOk(close);
}

static void AddDialog(El* section, component::Dialog* dialog, Ctx* cx) {
    section->Child(dialog->IntoEl(WindowSize(cx->win)));
}

static El* DialogTableCell(Ctx* cx, void*, int row, int col) {
    const Theme& th = ThemeNow(cx->app);
    switch (col) {
        case 0:
            return StoryTxt(cx, StoryFmt(cx, "%d", row), 16, th.foreground)
                ->LineHeight(1.f);
        case 1:
            return StoryTxt(cx, StoryFmt(cx, "User %d", row), 16, th.foreground)
                ->LineHeight(1.f);
        case 2:
            return StoryTxt(cx, StoryFmt(cx, "user-%d@mail.com", row), 16,
                            th.foreground)
                ->LineHeight(1.f);
        case 3:
            return StoryTxt(cx, StrL("User"), 16, th.foreground)
                ->LineHeight(1.f);
        default:
            return StoryTxt(cx, StrL("Active"), 16, th.foreground)
                ->LineHeight(1.f);
    }
}

// render_basic_dialog
static El* RenderBasicDialog(DialogStory* self, Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    El* section = StorySection(cx, "Default",
                               "Compose form controls and footer actions.");
    StorySectionAdd(section, StoryDialogTrigger(cx, DlgDefault, "show-dialog",
                                                "Open Dialog"));
    if (self->open != DlgDefault) {
        return section;
    }

    El* content = Div(a)->FlexCol()->W(kFill);
    content->Child(DialogHeader(cx, StrL("Basic Dialog"),
                                StrL("This is a basic dialog created using the "
                                     "declarative API."))
                       ->Pad(16));
    El* body = Div(a)->FlexCol()->W(kFill)->Gap(12)->PadX(16)->PadB(16);
    body->Child(StoryTxt(cx,
                         StrL("This is a dialog dialog, you can put anything "
                              "here."),
                         16, th.foreground));
    body->Child(
        component::Input::New(cx, StrL("dialog-name"), &self->basicInput)
            ->OnFocus(Listen(cx, &FocusBasicInput))
            ->IntoEl());
    body->Child(
        component::Select::New(cx, StrL("dialog-select"), self->basicSelect)
            ->Items(gDialogOptions, 3)
            ->W(kFill)
            ->OnToggle(Listen(cx, &ToggleBasicSelect))
            ->IntoEl());
    body->Child(component::DatePicker::New(cx)
                    ->Year(self->basicDate.year)
                    ->Month(self->basicDate.month)
                    ->Day(self->basicDate.day)
                    ->Placeholder(StrL("Date of Birth"))
                    ->W(kFill)
                    ->Open(self->basicDateOpen)
                    ->OnToggle(Listen(cx, &ToggleBasicDate))
                    ->OnDay(Listen(cx, &PickBasicDate))
                    ->IntoEl());
    content->Child(body);

    El* actions = Div(a)->FlexRow()->Gap(8)->ItemsCenter();
    actions->Child(DialogButton(cx, StrL("cancel"), StrL("Cancel"),
                                Listen(cx, &CloseDialog), false));
    actions->Child(DialogButton(cx, StrL("confirm"), StrL("Confirm"),
                                Listen(cx, &ConfirmBasicDialog), true));
    El* footer = Div(a)
                     ->FlexRow()
                     ->W(kFill)
                     ->Pad(16)
                     ->Gap(8)
                     ->ItemsCenter()
                     ->JustifyBetween()
                     ->Bg(th.tokens.muted);
    footer
        ->Child(DialogButton(cx, StrL("new-dialog"), StrL("Open Other Dialog"),
                             Listen(cx, &OpenOtherDialog), false));
    footer->Child(actions);
    content->Child(footer);

    component::Dialog* dialog = NewOpenDialog(self, cx)
                                    ->OnOk(Listen(cx, &ConfirmBasicDialog))
                                    ->Keyboard(self->keyboard)
                                    ->Overlay(self->overlay)
                                    ->OverlayClosable(self->overlayClosable)
                                    ->CloseButton(self->closeButton)
                                    ->Surface(content);
    AddDialog(section, dialog, cx);

    // window.open_dialog pushes a second Root dialog without replacing the
    // declarative one. Root offsets each additional layer by 16px.
    if (self->otherOpen) {
        El* other = Div(a)->FlexCol()->W(kFill)->Gap(16)->Pad(16);
        other->Child(DialogTitleText(cx, StrL("Other Dialog"), th.foreground));
        other->Child(
            StoryTxt(cx, StrL("This is another dialog."), 16, th.foreground));
        component::Dialog* otherDialog =
            component::Dialog::New(cx)
                ->Open(true)
                ->Layer(1)
                ->H(100)
                ->OverlayClosable(self->overlayClosable)
                ->CloseButton()
                ->OnClose(Listen(cx, &CloseOtherDialog))
                ->OnCancel(Listen(cx, &CloseOtherDialog))
                ->OnOk(Listen(cx, &CloseOtherDialog))
                ->Surface(other);
        AddDialog(section, otherDialog, cx);
    }
    return section;
}

// render_focus_return_check
static El* RenderFocusReturnCheck(DialogStory* self, Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    El* row = Div(a)->FlexRow()->W(kFill)->JustifyCenter();
    El* card = Div(a)
                   ->FlexCol()
                   ->W(384)
                   ->Gap(12)
                   ->Pad(12)
                   ->Radius(th.radiusLg)
                   ->Bg(RgbaOpacity(th.muted, 0.45f))
                   ->Border(1, th.border);
    El* header = Div(a)->FlexCol()->Gap(4);
    header->Child(StoryTxt(cx, StrL("Focus return check"), 16, th.foreground)
                      ->Medium());
    header
        ->Child(StoryTxt(cx, StrL("Type here, then open and close any dialog."),
                         12, th.mutedFg));
    card->Child(header);

    El* controls = Div(a)->FlexRow()->W(kFill)->Gap(8)->ItemsCenter();
    controls->Child(Div(a)->Flex1()->Child(
        component::Input::New(cx, StrL("focus-input"), &self->focusInput)
            ->OnFocus(Listen(cx, &FocusInput))
            ->IntoEl()));
    controls->Child(
        component::Button::New(cx, StrL("test-action"))
            ->Label(StrL("Run Action"))
            ->Outline()
            ->OnClick(Listen(cx, &RunTestAction))
            ->Tooltip(StrL("Verify actions still dispatch after a dialog "
                           "closes."))
            ->IntoEl());
    card->Child(controls);
    row->Child(card);
    return row;
}

// render_dialog_without_title
static El* RenderDialogWithoutTitle(DialogStory* self, Ctx* cx) {
    El* section =
        StorySection(cx, "Without title", "Render content without a heading.");
    StorySectionAdd(section,
                    StoryDialogTrigger(cx, DlgNoTitle, "dialog-no-title",
                                       "Dialog without Title"));
    if (self->open != DlgNoTitle) {
        return section;
    }

    El* content = Div(cx->a)->W(kFill)->Pad(16)->Child(
        StoryTxt(cx,
                 StrL("This is a dialog without title, you can use it "
                      "when the title is not necessary."),
                 16, ThemeNow(cx->app).foreground)
            ->Wrap()
            ->W(kFill));
    component::Dialog* dialog = NewOpenDialog(self, cx)
                                    ->Overlay(self->overlay)
                                    ->OverlayClosable(self->overlayClosable)
                                    ->CloseButton()
                                    ->Surface(content);
    AddDialog(section, dialog, cx);
    return section;
}

// render_custom_buttons
static El* RenderCustomButtons(DialogStory* self, Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    El* section = StorySection(cx, "Custom actions",
                               "Replace the default footer actions.");
    StorySectionAdd(section,
                    StoryDialogTrigger(cx, DlgCustomButtons, "confirm-dialog1",
                                       "Custom Buttons"));
    if (self->open != DlgCustomButtons) {
        return section;
    }

    El* surface = Div(a)->FlexCol()->W(kFill)->Gap(16)->Pad(16);
    El* body = Div(a)->FlexCol()->W(kFill)->Gap(12)->ItemsCenter();
    body->Child(
        Div(a)
            ->W(48)
            ->H(48)
            ->ItemsCenter()
            ->JustifyCenter()
            ->Radius(th.radiusLg)
            ->Bg(RgbaOpacity(th.warning, 0.2f))
            ->Child(IconEl(a, IconName::TriangleAlert, 32)->Fg(th.warning)));
    body->Child(StoryTxt(
        cx, StrL("Update successful, we need to restart the application."), 16,
        th.foreground));
    surface->Child(body);
    El* footer = Div(a)->FlexRow()->W(kFill)->Gap(8)->JustifyEnd();
    // DialogClose and DialogAction wrap their button in a `size_full` box, so
    // the two of them share the footer between them rather than sitting at
    // its right edge.
    footer->Child(Div(a)->W(kFill)->Child(DialogButton(
        cx, StrL("cancel"), StrL("Later"), Listen(cx, &PressLater), false)));
    footer->Child(Div(a)->W(kFill)->Child(DialogButton(
        cx, StrL("ok"), StrL("Restart Now"), Listen(cx, &PressRestart), true)));
    surface->Child(footer);

    component::Dialog* dialog = NewOpenDialog(self, cx)
                                    ->OnOk(Listen(cx, &PressRestart))
                                    ->OnCancel(Listen(cx, &PressLater))
                                    ->Radius(th.radiusLg)
                                    ->Overlay(self->overlay)
                                    ->OverlayClosable(self->overlayClosable)
                                    ->CloseButton()
                                    ->Surface(surface);
    AddDialog(section, dialog, cx);
    return section;
}

// render_scrollable_dialog
static El* RenderScrollableDialog(DialogStory* self, Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    El* section = StorySection(cx, "Scrollable",
                               "Keep long content inside a fixed dialog size.");
    StorySectionAdd(section,
                    StoryDialogTrigger(cx, DlgScrollable, "scrollable-dialog",
                                       "Scrollable Dialog"));
    if (self->open != DlgScrollable) {
        return section;
    }

    TempStr markdown = AssetsLoadTextTemp(StrL("story/README.md"));
    Str source = markdown.s ? Str(markdown.s) : StrL("# README.md is missing");
    // The 16px sides belong to each part, not to the surface: Rust hangs them
    // off the title, the scroll box's *contents* and the footer separately
    // (`pl(paddings.left).pr(paddings.right)` three times in dialog.rs), so
    // the scroll box itself reaches the panel's edges and its bar sits there
    // rather than 16px in from it.
    El* surface = Div(a)->FlexCol()->W(kFill)->H(kFill)->Gap(16)->PadY(16);
    surface->Child(
        DialogTitleText(cx, StrL("Dialog with scrollbar"), th.foreground)
            ->PadX(16));
    surface->Child(component::Scrollable::New(cx, StrL("dialog-scroll"))
                       ->H(484)
                       ->ScrollY(self->dialogScrollY)
                       ->OnScroll(Listen(cx, &OnDialogScroll))
                       ->Child(Div(a)->FlexCol()->W(kFill)->PadX(16)->Child(
                           component::TextView::New(cx, source)->IntoEl()))
                       ->IntoEl());
    // DialogClose and DialogAction wrap their button in a `size_full` box, so
    // the two of them share the footer between them rather than sitting at
    // its right edge — the same shape render_custom_buttons builds.
    El* footer = Div(a)->FlexRow()->W(kFill)->Gap(8)->JustifyEnd()->PadX(16);
    footer->Child(Div(a)->W(kFill)->Child(DialogButton(
        cx, StrL("cancel"), StrL("Cancel"), Listen(cx, &CloseDialog), false)));
    footer->Child(Div(a)->W(kFill)->Child(DialogButton(
        cx, StrL("confirm"), StrL("Confirm"), Listen(cx, &CloseDialog), true)));
    surface->Child(footer);

    component::Dialog* dialog = NewOpenDialog(self, cx)
                                    ->W(720)
                                    ->H(600)
                                    ->Overlay(self->overlay)
                                    ->OverlayClosable(self->overlayClosable)
                                    ->CloseButton()
                                    ->Surface(surface);
    AddDialog(section, dialog, cx);
    return section;
}

// render_table_in_dialog
static El* RenderTableInDialog(DialogStory* self, Ctx* cx) {
    static const component::TableColumn kColumns[] = {
        {StrL("ID"), 50, false, false, true},
        {StrL("Name"), 150, false, false, true},
        {StrL("Email"), 250, false, false, true},
        {StrL("Role"), 150, false, false, true},
        {StrL("Status"), 100, false, false, true},
    };
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    El* section =
        StorySection(cx, "Data table", "Embed a full interactive component.");
    StorySectionAdd(section, StoryDialogTrigger(cx, DlgTable, "table-dialog",
                                                "Table Dialog"));
    if (self->open != DlgTable) {
        return section;
    }

    El* surface = Div(a)->FlexCol()->W(kFill)->H(kFill)->Gap(16)->Pad(16);
    surface
        ->Child(DialogTitleText(cx, StrL("Dialog with Table"), th.foreground));
    El* body = Div(a)->FlexCol()->W(kFill)->Gap(12)->Flex1();
    body->Child(StoryTxt(cx,
                         StrL("This is a dialog contains a table component."),
                         16, th.foreground));
    // Rust hands the table a bare `DataTable::new(&table)` inside a
    // `size_full` column and the dialog's body gives it the rest of the
    // panel. `DataTable::H` is the height of the *rows*, and it has to be a
    // number rather than a fill: it is what decides how many rows are built,
    // and a virtualized list cannot ask the layout what it got. So the story
    // spells out what is above it, the way the panel does.
    const float kTablePadY = 16.f + 16.f;  // the surface's own padding
    const float kTableTitle = 16.f + 16.f; // the title, and the gap under it
    const float kTableIntro = 20.f + 12.f; // the line of prose, and its gap
    const float kTableHeadH = 28.f;        // the column heads
    body->Child(
        component::DataTable::New(cx, StrL("dialog-table"), self->table)
            ->Columns(kColumns, 5)
            ->Rows(200, self, DialogTableCell)
            ->H(600.f - kTablePadY - kTableTitle - kTableIntro - kTableHeadH)
            ->IntoEl());
    surface->Child(body);

    component::Dialog* dialog = NewOpenDialog(self, cx)
                                    ->W(800)
                                    ->H(600)
                                    ->Overlay(self->overlay)
                                    ->OverlayClosable(self->overlayClosable)
                                    ->CloseButton()
                                    ->Surface(surface);
    AddDialog(section, dialog, cx);
    return section;
}

// render_custom_paddings
static El* RenderCustomPaddings(DialogStory* self, Ctx* cx) {
    const Theme& th = ThemeNow(cx->app);
    El* section =
        StorySection(cx, "Padding", "Control spacing around dialog content.");
    StorySectionAdd(section,
                    StoryDialogTrigger(cx, DlgPadding, "custom-dialog-paddings",
                                       "Custom Paddings"));
    if (self->open != DlgPadding) {
        return section;
    }

    El* surface = Div(cx->a)->FlexCol()->W(kFill)->Gap(12)->Pad(12);
    surface->Child(
        DialogTitleText(cx, StrL("Custom Dialog Title"), th.foreground));
    surface->Child(
        StoryTxt(cx,
                 StrL("This is a custom dialog content, we can use paddings "
                      "to control the layout and spacing within the dialog."),
                 16, th.foreground)
            ->Wrap()
            ->W(kFill));
    AddDialog(section, NewOpenDialog(self, cx)->CloseButton()->Surface(surface),
              cx);
    return section;
}

// render_custom_style
static El* RenderCustomStyle(DialogStory* self, Ctx* cx) {
    const Theme& th = ThemeNow(cx->app);
    El* section = StorySection(cx, "Custom style",
                               "Customize color, radius, and foreground.");
    StorySectionAdd(section,
                    StoryDialogTrigger(cx, DlgStyle, "custom-dialog-style",
                                       "Custom Dialog Style"));
    if (self->open != DlgStyle) {
        return section;
    }

    El* surface = Div(cx->a)->FlexCol()->W(kFill)->Gap(16)->Pad(16);
    surface->Child(DialogTitleText(cx, StrL("Custom Dialog Title"), th.infoFg));
    surface->Child(
        StoryTxt(cx, StrL("This is a custom dialog content."), 16, th.infoFg));
    component::Dialog* dialog = NewOpenDialog(self, cx)
                                    ->Radius(th.radiusLg)
                                    ->Bg(th.cyan)
                                    ->Fg(th.infoFg)
                                    ->CloseButton()
                                    ->Surface(surface);
    AddDialog(section, dialog, cx);
    return section;
}

// render_dialog_with_content
static El* RenderDialogWithContent(DialogStory* self, Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    El* section = StorySection(cx, "Custom content",
                               "Compose header, body, and footer explicitly.");
    StorySectionAdd(
        section, StoryDialogTrigger(cx, DlgContent, "custom-width-dialog-btn",
                                    "Custom Width (400px)"));
    if (self->open != DlgContent) {
        return section;
    }

    El* content = Div(a)->FlexCol()->W(kFill)->Gap(16)->Pad(16);
    content
        ->Child(DialogHeader(cx, StrL("Custom Width"),
                             StrL("This dialog has a custom width of 400px.")));
    content->Child(
        StoryTxt(cx,
                 StrL("Content area with custom width configuration, and the "
                      "footer is used flex 1 button widths."),
                 16, th.foreground)
            ->Wrap()
            ->W(kFill));
    El* footer = Div(a)->FlexRow()->W(kFill)->Gap(8)->JustifyCenter();
    footer->Child(DialogButton(cx, StrL("cancel"), StrL("Cancel"),
                               Listen(cx, &CloseDialog), false)
                      ->Flex1());
    footer->Child(DialogButton(cx, StrL("done"), StrL("Done"),
                               Listen(cx, &CloseDialog), true)
                      ->Flex1());
    content->Child(footer);

    AddDialog(section,
              NewOpenDialog(self, cx)->W(400)->CloseButton()->Surface(content),
              cx);
    return section;
}

// render_textview_dialog
static El* RenderTextViewDialog(DialogStory* self, Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    El* section =
        StorySection(cx, "Selectable text", "Embed selectable rich text.");
    StorySectionAdd(section,
                    StoryDialogTrigger(cx, DlgTextView, "textview-dialog-btn",
                                       "TextView Dialog"));
    if (self->open != DlgTextView) {
        return section;
    }

    El* content = Div(a)->FlexCol()->W(kFill);
    content->Child(Div(a)->W(kFill)->Pad(16)->Child(
        DialogTitleText(cx, StrL("TextView Dialog"), th.foreground)));
    content->Child(Div(a)->W(kFill)->PadX(16)->PadB(16)->Child(
        component::TextView::New(
            cx, StrL("This is a dialog with a selectable TextView in it. "
                     "This text should be selectable."))
            ->Selectable()
            ->IntoEl()));
    El* footer = Div(a)->FlexRow()->W(kFill)->Pad(16)->Gap(8)->JustifyEnd()->Bg(
        th.muted);
    footer->Child(DialogButton(cx, StrL("cancel"), StrL("Cancel"),
                               Listen(cx, &CloseDialog), false));
    footer->Child(DialogButton(cx, StrL("confirm"), StrL("Confirm"),
                               Listen(cx, &CloseDialog), true));
    content->Child(footer);

    component::Dialog* dialog = NewOpenDialog(self, cx)
                                    ->Keyboard(self->keyboard)
                                    ->Overlay(self->overlay)
                                    ->OverlayClosable(self->overlayClosable)
                                    ->CloseButton(self->closeButton)
                                    ->Surface(content);
    AddDialog(section, dialog, cx);
    return section;
}

static void EnsureDialogState(DialogStory* self, Ctx* cx) {
    if (self->seeded) {
        return;
    }
    self->seeded = true;
    InputSetPlaceholder(&self->basicInput, StrL("Your Name"));
    InputSetPlaceholder(&self->focusInput,
                        StrL("Type before opening a dialog"));
    self->basicSelect = component::SelectState::New(cx->app);
    self->table = EntityNewState<TableState>(cx->app);
    self->basicDate = DateToday();
    self->basicDate.day = 0;
    static const char* kOptions[] = {"Option 1", "Option 2", "Option 3"};
    for (int i = 0; i < 3; i++) {
        gDialogOptions[i].title = Str(kOptions[i]);
        gDialogOptions[i].value = Str(kOptions[i]);
    }
}

El* DialogStory::Render(DialogStory* self, Ctx* cx) {
    EnsureDialogState(self, cx);
    if (self->basicInput.focused) {
        cx->win->input = &self->basicInput;
    } else if (self->focusInput.focused) {
        cx->win->input = &self->focusInput;
    }

    El* page = Div(cx->a)->FlexCol()->Gap(24)->W(kFill);
    StoryToolbarOpt opts[4] = {
        {"Overlay", self->overlay, DlgOptOverlay},
        {"Close on overlay click", self->overlayClosable,
         DlgOptOverlayClosable},
        {"Close button", self->closeButton, DlgOptCloseButton},
        {"Keyboard", self->keyboard, DlgOptKeyboard},
    };
    page->Child(StoryToolbarOptions(cx, self, opts, 4,
                                    Listen(cx, &DlgToolbarAct), false));
    page->Child(RenderBasicDialog(self, cx));
    page->Child(RenderCustomButtons(self, cx));
    page->Child(RenderScrollableDialog(self, cx));
    page->Child(RenderTableInDialog(self, cx));
    page->Child(RenderDialogWithoutTitle(self, cx));
    page->Child(RenderCustomPaddings(self, cx));
    page->Child(RenderCustomStyle(self, cx));
    page->Child(RenderDialogWithContent(self, cx));
    page->Child(RenderTextViewDialog(self, cx));
    page->Child(RenderFocusReturnCheck(self, cx));
    return page;
}

STORY_PAGE(StoryDialog, DialogStory);
