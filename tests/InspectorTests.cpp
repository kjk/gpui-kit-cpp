/* The inspector's style editor — crates/ui/src/inspector.rs.

   Rust round-trips a whole `StyleRefinement` through serde and parses an
   edited one back, showing the error when it does not parse. There is no
   reflection table here, so the JSON is the subset `StyleField` names; these
   are the three things that has to get right — what comes out reads back in,
   an edit names only what it changed, and a bad value is an error rather than
   a value silently dropped. */

#include "Test.h"

using namespace gpui::component;

static void WhatComesOutReadsBackIn() {
    Arena* a = ArenaNew();
    Style s;
    s.hasBg = true;
    s.bg = Rgba{0x17, 0x17, 0x17, 255};
    s.hasColor = true;
    s.color = Rgba{0xfa, 0xfa, 0xfa, 0x80};
    s.borderColor = Rgba{0x26, 0x26, 0x26, 255};
    s.pad = Edges::New(10, 12, 4, 6); // left, right, top, bottom
    s.gapX = 8;
    s.gapY = 8;
    s.radius = 6;
    s.border = 1;
    s.fontSize = 14;
    s.opacity = 0.5f;
    s.width = 220;

    Str json = StyleToJson(a, s);
    Style back;
    uint32_t fields = 0;
    Str err = {};
    utassert(StyleFromJson(a, json, &back, &fields, &err));
    utassert(err.s == nullptr);
    // Every field that was written comes back, and says it was named.
    utassert(fields & StyleFieldBg);
    utassert(fields & StyleFieldColor);
    utassert(fields & StyleFieldWidth);
    utassert(back.bg.color.r == 0x17 && back.bg.color.g == 0x17 &&
             back.bg.color.b == 0x17);
    utassert(back.color.a == 0x80);
    utassert(back.borderColor.b == 0x26);
    utassertnear(back.pad.left, 10.f);
    utassertnear(back.pad.right, 12.f);
    utassertnear(back.gapX, 8.f);
    utassertnear(back.radius, 6.f);
    utassertnear(back.border, 1.f);
    utassertnear(back.fontSize, 14.f);
    utassertnear(back.opacity, 0.5f);
    utassertnear(back.width, 220.f);
    // An unset height is written as nothing at all rather than as a number
    // that would pin the box.
    utassert(!(fields & StyleFieldHeight));
    ArenaDelete(a);
}

static void AnEditNamesOnlyWhatItChanged() {
    Arena* a = ArenaNew();
    Style s;
    s.gapX = 99;
    s.gapY = 99;
    uint32_t fields = 0;
    Str err = {};
    utassert(StyleFromJson(a, StrL("{ \"gap\": 4 }"), &s, &fields, &err));
    utassert(fields == StyleFieldGap);
    utassertnear(s.gapX, 4.f);
    utassertnear(s.gapY, 4.f);
    // The three-digit form, and the four edges as one number.
    Style t;
    utassert(StyleFromJson(a,
                           StrL("{ \"background\": \"#f00\", \"padding\": 5 }"),
                           &t, &fields, &err));
    utassert(fields == (StyleFieldBg | StyleFieldPad));
    utassert(t.bg.color.r == 255 && t.bg.color.g == 0 && t.bg.color.b == 0);
    utassertnear(t.pad.top, 5.f);
    utassertnear(t.pad.left, 5.f);
    ArenaDelete(a);
}

static void ABadValueIsAnError() {
    Arena* a = ArenaNew();
    Style s;
    uint32_t fields = 0;
    Str err = {};
    utassert(!StyleFromJson(a, StrL("not json"), &s, &fields, &err));
    utassert(err.s != nullptr);
    StrFree(err);

    utassert(
        !StyleFromJson(a, StrL("{ \"gap\": \"wide\" }"), &s, &fields, &err));
    utassert(err.s != nullptr);
    StrFree(err);

    utassert(!StyleFromJson(a, StrL("{ \"background\": \"blue\" }"), &s,
                            &fields, &err));
    utassert(err.s != nullptr);
    StrFree(err);
    ArenaDelete(a);
}

// The override table: keyed by click id, patching only what was named, and
// dropped by Reset.
static void AnOverridePatchesOnlyWhatItNamed() {
    Arena* a = ArenaNew();
    El* e = Div(a)->Gap(2)->Radius(3);
    e->clickId = 41;
    Style patch;
    patch.gapX = 10;
    patch.gapY = 10;
    StyleOverrideSet(41, StyleFieldGap, patch);

    StyleOverrideApply(e);
    utassertnear(e->style.gapX, 10.f);
    // What the JSON did not name is still the element's own.
    utassertnear(e->style.radius, 3.f);

    // Another element's id is not this one's.
    El* other = Div(a)->Gap(2);
    other->clickId = 42;
    StyleOverrideApply(other);
    utassertnear(other->style.gapX, 2.f);

    StyleOverrideClear(41);
    El* again = Div(a)->Gap(2);
    again->clickId = 41;
    StyleOverrideApply(again);
    utassertnear(again->style.gapX, 2.f);
    StyleOverrideClearAll();
    ArenaDelete(a);
}

static void DivInspectorOwnsUpdateEditAndReset() {
    App app;
    Window* win = new Window();
    win->app = &app;
    Arena* arena = ArenaNew();
    win->frameArena = arena;
    Entity<DivInspector> entity = EntityNewState<DivInspector>(&app);
    Ctx cx = {&app, win, arena, entity.id};
    DivInspector* inspector = entity.Get(&app);

    InspectorPick pick;
    pick.id = 77;
    pick.style.gapX = 2;
    pick.style.gapY = 2;
    pick.style.radius = 3;
    inspector->UpdateInspectedElement(pick, &cx);
    utassert(inspector->inspectorId == 77);
    utassert(inspector->jsonInput.kind == InputKind::Textarea);
    utassert(
        base::StrEq(InputValue(&inspector->jsonInput), inspector->applied));
    utassert(len(InputValue(&inspector->jsonInput)) > 0);

    utassert(inspector->EditJson(StrL("{ \"gap\": 9 }"), &cx));
    utassert(inspector->error.s == nullptr);
    El* edited = Div(arena)->Gap(2)->Radius(3);
    edited->clickId = 77;
    StyleOverrideApply(edited);
    utassertnear(edited->style.gapX, 9.f);
    // The JSON named no radius, so the element keeps its own.
    utassertnear(edited->style.radius, 3.f);

    utassert(!inspector->EditJson(StrL("{ \"gap\": \"wide\" }"), &cx));
    utassert(inspector->error.s != nullptr);
    inspector->Reset(&cx);
    utassert(inspector->error.s == nullptr);
    utassert(
        base::StrEq(InputValue(&inspector->jsonInput), inspector->applied));
    El* reset = Div(arena)->Gap(2);
    reset->clickId = 77;
    StyleOverrideApply(reset);
    utassertnear(reset->style.gapX, 2.f);

    // A new inspected identity reloads its independent initial style.
    pick.id = 78;
    pick.style.gapX = 5;
    pick.style.gapY = 5;
    inspector->UpdateInspectedElement(pick, &cx);
    utassert(inspector->inspectorId == 78);
    utassertnear(inspector->initialStyle.gapX, 5.f);
    ClickEvent focus = {};
    DivInspector::OnFocus(inspector, &cx, &focus);
    utassert(inspector->jsonInput.focused);

    StyleOverrideClearAll();
    delete win;
    ArenaDelete(arena);
    EntityDropAll(&app);
}

void TestInspector() {
    TestSuite("inspector");
    WhatComesOutReadsBackIn();
    AnEditNamesOnlyWhatItChanged();
    ABadValueIsAnError();
    AnOverridePatchesOnlyWhatItNamed();
    DivInspectorOwnsUpdateEditAndReset();
}
