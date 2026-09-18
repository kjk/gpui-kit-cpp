/* The small JSON reader and writer in base, which is what a saved dock
 * layout goes through. serde does this for Rust; these are the cases the
 * layout code depends on. */

#include "Test.h"

static void AnObjectReadsBackByName() {
    Arena* a = ArenaNew();
    JsonValue* v = JsonParse(
        a, StrL("{\"name\": \"tabs\", \"open\": true, \"size\": 350.5,"
                " \"none\": null, \"sizes\": [1, 2.5, -3]}"));
    utassert(v && v->kind == JsonKind::Object);
    utassert(StrEqI(JsonString(JsonGet(v, "name")), "tabs"));
    utassert(JsonBool(JsonGet(v, "open")));
    utassertnear((float)JsonNumber(JsonGet(v, "size")), 350.5f);
    const JsonValue* none = JsonGet(v, "none");
    utassert(none && none->kind == JsonKind::Null);

    const JsonValue* sizes = JsonGet(v, "sizes");
    utassert(sizes && sizes->kind == JsonKind::Array);
    utassert(JsonLen(sizes) == 3);
    utassertnear((float)JsonNumber(JsonAt(sizes, 0)), 1.f);
    utassertnear((float)JsonNumber(JsonAt(sizes, 1)), 2.5f);
    utassertnear((float)JsonNumber(JsonAt(sizes, 2)), -3.f);
    utassert(JsonAt(sizes, 3) == nullptr);

    // A member that is not there, and a read of the wrong kind, both answer
    // what the caller said to fall back to.
    utassert(JsonGet(v, "missing") == nullptr);
    utassertnear((float)JsonNumber(JsonGet(v, "name"), 7), 7.f);
    utassert(JsonBool(JsonGet(v, "size"), true));
    ArenaDelete(a);
}

static void TheEscapesComeBackOut() {
    Arena* a = ArenaNew();
    JsonValue* v = JsonParse(a, StrL("{\"s\": \"a\\\"b\\\\c\\nd\\u00e9\"}"));
    utassert(v);
    Str s = JsonString(JsonGet(v, "s"));
    // a " b \ c newline d é — nine bytes, since é takes two of them.
    utassert(len(s) == 9);
    utassert(s.s[1] == '"' && s.s[3] == '\\' && s.s[5] == '\n');
    utassert(s.s[6] == 'd');
    // é, as the two bytes UTF-8 spells it with.
    utassert((unsigned char)s.s[7] == 0xC3 && (unsigned char)s.s[8] == 0xA9);
    ArenaDelete(a);
}

static void WhatIsNotJsonIsRefused() {
    Arena* a = ArenaNew();
    utassert(JsonParse(a, StrL("{\"a\": }")) == nullptr);
    utassert(JsonParse(a, StrL("{\"a\" 1}")) == nullptr);
    utassert(JsonParse(a, StrL("[1, 2")) == nullptr);
    utassert(JsonParse(a, StrL("")) == nullptr);
    utassert(JsonParse(a, StrL("tru")) == nullptr);
    ArenaDelete(a);
}

static void WhatIsWrittenParsesBack() {
    StrBuilder sb;
    JsonWriter w;
    w.out = &sb;
    w.BeginObject(nullptr);
    w.Number("version", 2);
    w.String("name", StrL("a \"quoted\" name"));
    w.Bool("open", false);
    w.BeginArray("sizes");
    w.Number(nullptr, 704);
    w.Number(nullptr, 263.5);
    w.EndArray();
    w.BeginObject("info");
    w.Null("panel");
    w.EndObject();
    w.EndObject();
    Str text = sb.TakeStr();

    Arena* a = ArenaNew();
    JsonValue* v = JsonParse(a, text);
    utassert(v && v->kind == JsonKind::Object);
    utassertnear((float)JsonNumber(JsonGet(v, "version")), 2.f);
    utassert(StrEqI(JsonString(JsonGet(v, "name")), "a \"quoted\" name"));
    utassert(!JsonBool(JsonGet(v, "open"), true));
    utassert(JsonLen(JsonGet(v, "sizes")) == 2);
    utassertnear((float)JsonNumber(JsonAt(JsonGet(v, "sizes"), 1)), 263.5f);
    const JsonValue* panel = JsonGet(JsonGet(v, "info"), "panel");
    utassert(panel && panel->kind == JsonKind::Null);
    ArenaDelete(a);
    StrFree(text);
}

void TestJson() {
    TestSuite("json");
    AnObjectReadsBackByName();
    TheEscapesComeBackOut();
    WhatIsNotJsonIsRefused();
    WhatIsWrittenParsesBack();
}
