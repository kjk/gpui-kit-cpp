#include "Story.h"

// One entry per select on the page.
enum {
    SelCountry = 0,
    SelFruit,
    SelDisabled,
    SelUi1,
    SelMenuH,
    SelLanguage,
    SelEmpty,
    SelAppearance,
    SelCount
};

// crates/story/src/fixtures/countries.json, which the country select groups
// by the letter each name starts with. `title()` is the name, so that is what
// a row shows and what a query matches; `display_title()` is the name and the
// code, which is what the trigger shows instead.
struct CountryRow {
    const char* name;
    const char* code;
};

static const CountryRow kCountries[] = {
    {"Afghanistan", "AF"},
    {"Åland Islands", "AX"},
    {"Albania", "AL"},
    {"Algeria", "DZ"},
    {"American Samoa", "AS"},
    {"AndorrA", "AD"},
    {"Angola", "AO"},
    {"Anguilla", "AI"},
    {"Antarctica", "AQ"},
    {"Antigua and Barbuda", "AG"},
    {"Argentina", "AR"},
    {"Armenia", "AM"},
    {"Aruba", "AW"},
    {"Australia", "AU"},
    {"Austria", "AT"},
    {"Azerbaijan", "AZ"},
    {"Bahamas", "BS"},
    {"Bahrain", "BH"},
    {"Bangladesh", "BD"},
    {"Barbados", "BB"},
    {"Belarus", "BY"},
    {"Belgium", "BE"},
    {"Belize", "BZ"},
    {"Benin", "BJ"},
    {"Bermuda", "BM"},
    {"Bhutan", "BT"},
    {"Bolivia", "BO"},
    {"Bosnia and Herzegovina", "BA"},
    {"Botswana", "BW"},
    {"Bouvet Island", "BV"},
    {"Brazil", "BR"},
    {"British Indian Ocean Territory", "IO"},
    {"Brunei Darussalam", "BN"},
    {"Bulgaria", "BG"},
    {"Burkina Faso", "BF"},
    {"Burundi", "BI"},
    {"Cambodia", "KH"},
    {"Cameroon", "CM"},
    {"Canada", "CA"},
    {"Cape Verde", "CV"},
    {"Cayman Islands", "KY"},
    {"Central African Republic", "CF"},
    {"Chad", "TD"},
    {"Chile", "CL"},
    {"China", "CN"},
    {"Christmas Island", "CX"},
    {"Cocos (Keeling) Islands", "CC"},
    {"Colombia", "CO"},
    {"Comoros", "KM"},
    {"Congo", "CG"},
    {"Congo, The Democratic Republic of the", "CD"},
    {"Cook Islands", "CK"},
    {"Costa Rica", "CR"},
    {"Cote D'Ivoire", "CI"},
    {"Croatia", "HR"},
    {"Cuba", "CU"},
    {"Cyprus", "CY"},
    {"Czech Republic", "CZ"},
    {"Denmark", "DK"},
    {"Djibouti", "DJ"},
    {"Dominica", "DM"},
    {"Dominican Republic", "DO"},
    {"Ecuador", "EC"},
    {"Egypt", "EG"},
    {"El Salvador", "SV"},
    {"Equatorial Guinea", "GQ"},
    {"Eritrea", "ER"},
    {"Estonia", "EE"},
    {"Ethiopia", "ET"},
    {"Falkland Islands (Malvinas)", "FK"},
    {"Faroe Islands", "FO"},
    {"Fiji", "FJ"},
    {"Finland", "FI"},
    {"France", "FR"},
    {"French Guiana", "GF"},
    {"French Polynesia", "PF"},
    {"French Southern Territories", "TF"},
    {"Gabon", "GA"},
    {"Gambia", "GM"},
    {"Georgia", "GE"},
    {"Germany", "DE"},
    {"Ghana", "GH"},
    {"Gibraltar", "GI"},
    {"Greece", "GR"},
    {"Greenland", "GL"},
    {"Grenada", "GD"},
    {"Guadeloupe", "GP"},
    {"Guam", "GU"},
    {"Guatemala", "GT"},
    {"Guernsey", "GG"},
    {"Guinea", "GN"},
    {"Guinea-Bissau", "GW"},
    {"Guyana", "GY"},
    {"Haiti", "HT"},
    {"Heard Island and Mcdonald Islands", "HM"},
    {"Holy See (Vatican City State)", "VA"},
    {"Honduras", "HN"},
    {"Hong Kong", "HK"},
    {"Hungary", "HU"},
    {"Iceland", "IS"},
    {"India", "IN"},
    {"Indonesia", "ID"},
    {"Iran, Islamic Republic Of", "IR"},
    {"Iraq", "IQ"},
    {"Ireland", "IE"},
    {"Isle of Man", "IM"},
    {"Israel", "IL"},
    {"Italy", "IT"},
    {"Jamaica", "JM"},
    {"Japan", "JP"},
    {"Jersey", "JE"},
    {"Jordan", "JO"},
    {"Kazakhstan", "KZ"},
    {"Kenya", "KE"},
    {"Kiribati", "KI"},
    {"Korea, Democratic People'S Republic of", "KP"},
    {"Korea, Republic of", "KR"},
    {"Kuwait", "KW"},
    {"Kyrgyzstan", "KG"},
    {"Lao People'S Democratic Republic", "LA"},
    {"Latvia", "LV"},
    {"Lebanon", "LB"},
    {"Lesotho", "LS"},
    {"Liberia", "LR"},
    {"Libyan Arab Jamahiriya", "LY"},
    {"Liechtenstein", "LI"},
    {"Lithuania", "LT"},
    {"Luxembourg", "LU"},
    {"Macao", "MO"},
    {"Macedonia, The Former Yugoslav Republic of", "MK"},
    {"Madagascar", "MG"},
    {"Malawi", "MW"},
    {"Malaysia", "MY"},
    {"Maldives", "MV"},
    {"Mali", "ML"},
    {"Malta", "MT"},
    {"Marshall Islands", "MH"},
    {"Martinique", "MQ"},
    {"Mauritania", "MR"},
    {"Mauritius", "MU"},
    {"Mayotte", "YT"},
    {"Mexico", "MX"},
    {"Micronesia, Federated States of", "FM"},
    {"Moldova, Republic of", "MD"},
    {"Monaco", "MC"},
    {"Mongolia", "MN"},
    {"Montserrat", "MS"},
    {"Morocco", "MA"},
    {"Mozambique", "MZ"},
    {"Myanmar", "MM"},
    {"Namibia", "NA"},
    {"Nauru", "NR"},
    {"Nepal", "NP"},
    {"Netherlands", "NL"},
    {"Netherlands Antilles", "AN"},
    {"New Caledonia", "NC"},
    {"New Zealand", "NZ"},
    {"Nicaragua", "NI"},
    {"Niger", "NE"},
    {"Nigeria", "NG"},
    {"Niue", "NU"},
    {"Norfolk Island", "NF"},
    {"Northern Mariana Islands", "MP"},
    {"Norway", "NO"},
    {"Oman", "OM"},
    {"Pakistan", "PK"},
    {"Palau", "PW"},
    {"Palestinian Territory, Occupied", "PS"},
    {"Panama", "PA"},
    {"Papua New Guinea", "PG"},
    {"Paraguay", "PY"},
    {"Peru", "PE"},
    {"Philippines", "PH"},
    {"Pitcairn", "PN"},
    {"Poland", "PL"},
    {"Portugal", "PT"},
    {"Puerto Rico", "PR"},
    {"Qatar", "QA"},
    {"Reunion", "RE"},
    {"Romania", "RO"},
    {"Russian Federation", "RU"},
    {"RWANDA", "RW"},
    {"Saint Helena", "SH"},
    {"Saint Kitts and Nevis", "KN"},
    {"Saint Lucia", "LC"},
    {"Saint Pierre and Miquelon", "PM"},
    {"Saint Vincent and the Grenadines", "VC"},
    {"Samoa", "WS"},
    {"San Marino", "SM"},
    {"Sao Tome and Principe", "ST"},
    {"Saudi Arabia", "SA"},
    {"Senegal", "SN"},
    {"Serbia and Montenegro", "CS"},
    {"Seychelles", "SC"},
    {"Sierra Leone", "SL"},
    {"Singapore", "SG"},
    {"Slovakia", "SK"},
    {"Slovenia", "SI"},
    {"Solomon Islands", "SB"},
    {"Somalia", "SO"},
    {"South Africa", "ZA"},
    {"South Georgia and the South Sandwich Islands", "GS"},
    {"Spain", "ES"},
    {"Sri Lanka", "LK"},
    {"Sudan", "SD"},
    {"Suriname", "SR"},
    {"Svalbard and Jan Mayen", "SJ"},
    {"Swaziland", "SZ"},
    {"Sweden", "SE"},
    {"Switzerland", "CH"},
    {"Syrian Arab Republic", "SY"},
    {"Tajikistan", "TJ"},
    {"Tanzania, United Republic of", "TZ"},
    {"Thailand", "TH"},
    {"Timor-Leste", "TL"},
    {"Togo", "TG"},
    {"Tokelau", "TK"},
    {"Tonga", "TO"},
    {"Trinidad and Tobago", "TT"},
    {"Tunisia", "TN"},
    {"Turkey", "TR"},
    {"Turkmenistan", "TM"},
    {"Turks and Caicos Islands", "TC"},
    {"Tuvalu", "TV"},
    {"Uganda", "UG"},
    {"Ukraine", "UA"},
    {"United Arab Emirates", "AE"},
    {"United Kingdom", "GB"},
    {"United States", "US"},
    {"United States Minor Outlying Islands", "UM"},
    {"Uruguay", "UY"},
    {"Uzbekistan", "UZ"},
    {"Vanuatu", "VU"},
    {"Venezuela", "VE"},
    {"Viet Nam", "VN"},
    {"Virgin Islands, British", "VG"},
    {"Virgin Islands, U.S.", "VI"},
    {"Wallis and Futuna", "WF"},
    {"Western Sahara", "EH"},
    {"Yemen", "YE"},
    {"Zambia", "ZM"},
    {"Zimbabwe", "ZW"},
};
static const int kNCountries =
    (int)(sizeof(kCountries) / sizeof(kCountries[0]));
static const char* kFruits[] = {
    "Apple",
    "Orange",
    "Banana",
    "Grape",
    "Pineapple",
    "Watermelon & This is a long long long long long long long long long title",
    "Avocado",
};
static const char* kUi[] = {"GPUI", "Iced",  "egui",  "Makepad", "Slint",
                            "QT",   "ImGui", "Cocoa", "WinUI"};
static const char* kLanguages[] = {"Rust", "Go", "C++", "JavaScript"};
static const char* kCodes[] = {"CN", "US", "HK", "JP", "KR"};

// The items each select shows. A SearchableList keeps a pointer to them, so
// they have to outlive the frame — which is what makes these static rather
// than built on the frame arena.
static component::SearchableItem gItems[SelCount][16];
static int gCounts[SelCount];
// The country list is its own array: it is far longer than any other select
// on the page, and it is the only one with sections.
static component::SearchableItem gCountryItems[kNCountries];
static Str gCountrySections[kNCountries];
static int gNCountrySections;

// itertools' chunk_by over the first character of each name: a run of names
// that start alike is one section, so a list that is not sorted by that
// character opens the same letter more than once — which is what the Å
// between the two runs of A does.
static void BuildCountries() {
    if (gNCountrySections > 0) {
        return;
    }
    for (int i = 0; i < kNCountries; i++) {
        Str name = Str(kCountries[i].name);
        // The section is named by the first character, not the first byte:
        // "Åland Islands" starts with two bytes of one.
        int n = 1;
        while (n < name.len && ((uint8_t)name.s[n] & 0xc0) == 0x80) {
            n++;
        }
        Str letter = Str(name.s, n);
        if (gNCountrySections == 0 ||
            !StrEqI(gCountrySections[gNCountrySections - 1], letter)) {
            gCountrySections[gNCountrySections++] = letter;
        }
        gCountryItems[i].title = name;
        gCountryItems[i].value = Str(kCountries[i].code);
        gCountryItems[i]
            .display = StrDup(fmt("%s (%s)", name, Str(kCountries[i].code)));
        gCountryItems[i].section = gNCountrySections - 1;
    }
}

static void BuildItems(int which, const char* const* names, int n) {
    if (n > 16) {
        n = 16;
    }
    for (int i = 0; i < n; i++) {
        gItems[which][i].title = Str(names[i]);
        // The title is the value here: nothing on this page has an id of its
        // own behind what it shows.
        gItems[which][i].value = Str(names[i]);
    }
    gCounts[which] = n;
}

struct SelectStory {
    // SelectState is an entity in Rust too. It owns the SearchableList under
    // the trigger while keeping the committed select and its events distinct.
    Entity<component::SelectState> sel[SelCount] = {};
    bool disabled = false;
    InputState phone;
    // One query per searchable select: three of them are `.searchable(true)`,
    // and each keeps its own.
    InputState search[SelCount];
    StoryToolbarState toolbar;
    bool seeded = false;

    static El* Render(SelectStory* self, Ctx* cx);
};

enum {
    SelOptDisabled = ToolbarOptDisabled
};

static void SelToolbarAct(SelectStory* self, Ctx* cx, const ClickEvent*,
                          intptr_t act) {
    if (act == SelOptDisabled) {
        self->disabled = !self->disabled;
    } else {
        StoryToolbarApply(&self->toolbar, nullptr, (int)act);
    }
    Notify(cx);
}

// Only one select is open at a time, which is what closing the rest does.
static void ToggleSel(SelectStory* self, Ctx* cx, const ClickEvent*,
                      intptr_t which) {
    for (int i = 0; i < SelCount; i++) {
        component::SelectState* s = self->sel[i].Get(cx);
        if (!s) {
            continue;
        }
        if (i == (int)which) {
            component::SelectToggleOpen(s, cx);
        } else {
            s->state.open = false;
        }
    }
    Notify(cx);
}
static void ClearSel(SelectStory* self, Ctx* cx, const ClickEvent*,
                     intptr_t which) {
    component::SelectClear(self->sel[which].Get(cx), cx);
}
static void SelBlurAll(SelectStory* self) {
    self->phone.focused = false;
    for (int i = 0; i < SelCount; i++) {
        self->search[i].focused = false;
    }
}
static void FocusPhone(SelectStory* self, Ctx* cx, const ClickEvent*) {
    SelBlurAll(self);
    self->phone.focused = true;
    Notify(cx);
}
static void FocusSearch(SelectStory* self, Ctx* cx, const ClickEvent*,
                        intptr_t which) {
    SelBlurAll(self);
    self->search[which].focused = true;
    Notify(cx);
}

static component::Select* Sel(SelectStory* self, Ctx* cx, int which,
                              const char* id, Listener toggle, Listener clear) {
    const component::SearchableItem* items =
        which == SelCountry ? gCountryItems : gItems[which];
    int n = which == SelCountry ? kNCountries : gCounts[which];
    component::Select* sel =
        component::Select::New(cx, Str(id), self->sel[which])->Items(items, n);
    if (which == SelCountry) {
        sel->Sections(gCountrySections, gNCountrySections);
    }
    return sel->W(280)
        ->WithSize(self->toolbar.size)
        ->Disabled(self->disabled)
        ->OnToggle(ListenerArg(toggle, which))
        ->OnClear(ListenerArg(clear, which));
}

El* SelectStory::Render(SelectStory* self, Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    if (!self->seeded) {
        self->seeded = true;
        InputSetPlaceholder(&self->phone, StrL("Your phone number"));
        for (int i = 0; i < SelCount; i++) {
            InputSetPlaceholder(&self->search[i], StrL("Search..."));
        }
        for (int i = 0; i < SelCount; i++) {
            self->sel[i] = component::SelectState::New(cx->app);
        }
        BuildCountries();
        BuildItems(SelFruit, kFruits, (int)(sizeof(kFruits) / sizeof(char*)));
        BuildItems(SelUi1, kUi, (int)(sizeof(kUi) / sizeof(char*)));
        BuildItems(SelMenuH, kUi, (int)(sizeof(kUi) / sizeof(char*)));
        BuildItems(SelLanguage, kLanguages,
                   (int)(sizeof(kLanguages) / sizeof(char*)));
        BuildItems(SelAppearance, kCodes,
                   (int)(sizeof(kCodes) / sizeof(char*)));
        // The three selects Rust seeds with `Some(IndexPath::default())`
        // open with a value already picked: the country, and the two that
        // carry a title prefix.
        component::SelectState* country = self->sel[SelCountry].Get(cx);
        if (country) {
            // IndexPath::default().row(8).section(2): the ninth name of the
            // third run, which is Argentina.
            component::SearchableListSelectOnly(country->List(), 10);
        }
        for (int which : {SelUi1, SelMenuH, SelAppearance}) {
            component::SelectState* st = self->sel[which].Get(cx);
            if (st) {
                component::SearchableListSelectOnly(st->List(), 0);
            }
        }
    }
    if (self->phone.focused) {
        cx->win->input = &self->phone;
    } else {
        for (int i = 0; i < SelCount; i++) {
            if (self->search[i].focused) {
                cx->win->input = &self->search[i];
                break;
            }
        }
    }
    Listener toggle = Listen(cx, &ToggleSel);
    Listener clear = Listen(cx, &ClearSel);
    Listener focusQuery = Listen(cx, &FocusSearch);

    El* page = Div(a)->FlexCol()->Gap(16)->W(kFill)->ItemsCenter();
    StoryToolbarOpt opts[1] = {{"Disabled", self->disabled, SelOptDisabled}};
    page->Child(
        StoryToolbarOptions(cx, self, opts, 1, Listen(cx, &SelToolbarAct)));

    El* search = StorySection(cx, "Search and clear",
                              "Search options and clear the value.");
    StorySectionBody(search)->W(280)->ItemsCenter();
    StorySectionAdd(search,
                    Sel(self, cx, SelCountry, "country", toggle, clear)
                        // The searchable select carries the accessible name
                        // upstream's story gives its `simple_select2`.
                        ->AccessibilityLabel(StrL("Programming language"))
                        ->Cleanable()
                        ->Searchable(&self->search[SelCountry],
                                     ListenerArg(focusQuery, SelCountry))
                        ->IntoEl());
    page->Child(search);

    El* width = StorySection(cx, "Menu width",
                             "Set trigger and menu widths independently.");
    StorySectionBody(width)->W(280)->ItemsCenter();
    StorySectionAdd(width, Sel(self, cx, SelFruit, "fruit", toggle, clear)
                               ->Icon(IconName::Search)
                               ->MenuWidth(400)
                               ->Searchable(&self->search[SelFruit],
                                            ListenerArg(focusQuery, SelFruit))
                               ->IntoEl());
    page->Child(width);

    El* dis = StorySection(cx, "Disabled", "Keep the selected value visible.");
    StorySectionBody(dis)->W(280)->ItemsCenter();
    StorySectionAdd(dis, component::Select::New(cx, StrL("select-disabled"),
                                                self->sel[SelDisabled])
                             ->W(280)
                             ->WithSize(self->toolbar.size)
                             ->Disabled(true)
                             ->IntoEl());
    page->Child(dis);

    El* prefix = StorySection(cx, "Title prefix", "Prefix the selected value.");
    StorySectionBody(prefix)->W(280)->ItemsCenter();
    StorySectionAdd(prefix, Sel(self, cx, SelUi1, "ui1", toggle, clear)
                                ->Placeholder(StrL("UI"))
                                ->TitlePrefix(StrL("UI: "))
                                ->IntoEl());
    page->Child(prefix);

    El* menuH = StorySection(cx, "Menu height", "Limit the popup height.");
    StorySectionBody(menuH)->W(280)->ItemsCenter();
    StorySectionAdd(menuH, Sel(self, cx, SelMenuH, "menu-h", toggle, clear)
                               ->Placeholder(StrL("UI"))
                               ->TitlePrefix(StrL("UI: "))
                               ->MenuMaxH(96)
                               ->IntoEl());
    page->Child(menuH);

    El* multi = StorySection(cx, "Multiple",
                             "Pick more than one; the trigger says how many.");
    StorySectionAdd(multi,
                    Sel(self, cx, SelLanguage, "language", toggle, clear)
                        ->Placeholder(StrL("Language"))
                        ->Multiple()
                        ->Searchable(&self->search[SelLanguage],
                                     ListenerArg(focusQuery, SelLanguage))
                        ->IntoEl());
    page->Child(multi);

    El* empty = StorySection(cx, "Empty", "Render a custom empty state.");
    StorySectionBody(empty)->W(280)->ItemsCenter();
    StorySectionAdd(empty, component::Select::New(cx, StrL("select-empty"),
                                                  self->sel[SelEmpty])
                               ->W(280)
                               ->WithSize(self->toolbar.size)
                               ->Disabled(self->disabled)
                               ->Empty(StrL("No Data"))
                               ->OnToggle(ListenerArg(toggle, SelEmpty))
                               ->IntoEl());
    page->Child(empty);

    El* custom =
        StorySection(cx, "Custom appearance",
                     "Compose an appearance-free select with another control.");
    StorySectionBody(custom)->W(280)->ItemsCenter();
    // A country code, a divider, a phone field and the send button, all in
    // one bordered row.
    El* row = Div(a)
                  ->FlexRow()
                  ->W(280)
                  ->Gap(4)
                  ->ItemsCenter()
                  ->Radius(th.radiusLg)
                  ->Border(1, th.inputBorder);
    row->Child(Div(a)->W(140)->Child(
        component::Select::New(cx, StrL("appearance"), self->sel[SelAppearance])
            ->Items(gItems[SelAppearance], gCounts[SelAppearance])
            ->W(140)
            ->WithSize(self->toolbar.size)
            ->Appearance(false)
            ->OnToggle(ListenerArg(toggle, SelAppearance))
            ->IntoEl()));
    row->Child(component::Separator::Vertical(cx)->IntoEl()->H(20));
    row->Child(Div(a)->Flex1()->MinW(0)->Child(
        component::Input::New(cx, StrL("phone"), &self->phone)
            ->Appearance(false)
            ->WithSize(self->toolbar.size)
            ->OnFocus(Listen(cx, &FocusPhone))
            ->IntoEl()));
    row->Child(Div(a)->Pad(8)->Child(component::Button::New(cx, StrL("send"))
                                         ->Ghost()
                                         ->WithSize(self->toolbar.size)
                                         ->Label(StrL("Send"))
                                         ->IntoEl()));
    StorySectionAdd(custom, row);
    page->Child(custom);

    El* values = StorySection(cx, "Values", "Read selected values from state.");
    StorySectionBody(values)->W(512);
    // The column shrink-wraps and the section centers it; only the section
    // itself is `w_128()`.
    El* valueCol = Div(a)->FlexCol()->Gap(12);
    const char* labels[] = {"Country", "fruit", "UI", "Language"};
    int slots[] = {SelCountry, SelFruit, SelUi1, SelLanguage};
    for (int i = 0; i < 4; i++) {
        component::SelectState* s = self->sel[slots[i]].Get(cx);
        const component::SearchableItem* items =
            slots[i] == SelCountry ? gCountryItems : gItems[slots[i]];
        Str line = StoryFmt(cx, "%s: None", labels[i]);
        if (s && s->state.selected.len > 0) {
            // selected_value(): the item's value, which for a country is its
            // code rather than its name.
            line = StoryFmt(cx, "%s: Some(\"%s\")", labels[i],
                            items[s->state.selected[0]].value);
        }
        valueCol->Child(StoryTxt(cx, line, 16, th.foreground));
    }
    valueCol
        ->Child(StoryTxt(cx, StrL("This is other text."), 16, th.foreground));
    StorySectionAdd(values, valueCol);
    page->Child(values);
    return page;
}

STORY_PAGE(StorySelect, SelectStory);
