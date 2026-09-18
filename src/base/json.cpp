#include "base/json.h"

#include <stdlib.h>

namespace gpui {

// Where the parse has got to. Everything it builds goes in `a`.
struct JsonParser {
    Arena* a = nullptr;
    const char* p = nullptr;
    const char* end = nullptr;
    bool bad = false;
};

static void SkipSpace(JsonParser* jp) {
    while (jp->p < jp->end) {
        char c = *jp->p;
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            jp->p++;
            continue;
        }
        break;
    }
}

static bool Eat(JsonParser* jp, char c) {
    SkipSpace(jp);
    if (jp->p < jp->end && *jp->p == c) {
        jp->p++;
        return true;
    }
    return false;
}

static bool Literal(JsonParser* jp, const char* word) {
    SkipSpace(jp);
    int n = (int)strlen(word);
    if (jp->end - jp->p < n || !StrEq(Str(jp->p, n), Str(word, n))) {
        return false;
    }
    jp->p += n;
    return true;
}

// A string, with its escapes undone. \u is taken as UTF-8, which is what the
// rest of the port is written in.
static bool ParseString(JsonParser* jp, Str* out) {
    if (!Eat(jp, '"')) {
        return false;
    }
    // The unescaped text is never longer than what it came from.
    int cap = (int)(jp->end - jp->p) + 1;
    char* buf = (char*)Alloc(jp->a, cap + 1);
    if (!buf) {
        return false;
    }
    int n = 0;
    while (jp->p < jp->end) {
        char c = *jp->p++;
        if (c == '"') {
            buf[n] = 0;
            *out = Str(buf, n);
            return true;
        }
        if (c != '\\') {
            buf[n++] = c;
            continue;
        }
        if (jp->p >= jp->end) {
            break;
        }
        char e = *jp->p++;
        switch (e) {
            case 'n':
                buf[n++] = '\n';
                break;
            case 't':
                buf[n++] = '\t';
                break;
            case 'r':
                buf[n++] = '\r';
                break;
            case 'b':
                buf[n++] = '\b';
                break;
            case 'f':
                buf[n++] = '\f';
                break;
            case 'u': {
                if (jp->end - jp->p < 4) {
                    return false;
                }
                unsigned cp = 0;
                for (int i = 0; i < 4; i++) {
                    char h = *jp->p++;
                    unsigned v = 0;
                    if (h >= '0' && h <= '9') {
                        v = (unsigned)(h - '0');
                    } else if (h >= 'a' && h <= 'f') {
                        v = (unsigned)(h - 'a') + 10;
                    } else if (h >= 'A' && h <= 'F') {
                        v = (unsigned)(h - 'A') + 10;
                    } else {
                        return false;
                    }
                    cp = cp * 16 + v;
                }
                if (cp < 0x80) {
                    buf[n++] = (char)cp;
                } else if (cp < 0x800) {
                    buf[n++] = (char)(0xC0 | (cp >> 6));
                    buf[n++] = (char)(0x80 | (cp & 0x3F));
                } else {
                    buf[n++] = (char)(0xE0 | (cp >> 12));
                    buf[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                    buf[n++] = (char)(0x80 | (cp & 0x3F));
                }
                break;
            }
            default:
                // \" \\ \/ and anything else stand for themselves.
                buf[n++] = e;
                break;
        }
    }
    return false;
}

static JsonValue* ParseValue(JsonParser* jp);

static JsonValue* NewValue(JsonParser* jp, JsonKind kind) {
    auto* v = ArenaNew<JsonValue>(jp->a);
    if (v) {
        v->kind = kind;
    }
    return v;
}

static JsonValue* ParseObject(JsonParser* jp) {
    JsonValue* obj = NewValue(jp, JsonKind::Object);
    if (!obj) {
        return nullptr;
    }
    SkipSpace(jp);
    if (Eat(jp, '}')) {
        return obj;
    }
    JsonValue* last = nullptr;
    for (;;) {
        Str key = {};
        if (!ParseString(jp, &key)) {
            jp->bad = true;
            return nullptr;
        }
        if (!Eat(jp, ':')) {
            jp->bad = true;
            return nullptr;
        }
        JsonValue* child = ParseValue(jp);
        if (!child) {
            jp->bad = true;
            return nullptr;
        }
        child->key = key;
        if (last) {
            last->next = child;
        } else {
            obj->first = child;
        }
        last = child;
        if (Eat(jp, ',')) {
            continue;
        }
        if (Eat(jp, '}')) {
            return obj;
        }
        jp->bad = true;
        return nullptr;
    }
}

static JsonValue* ParseArray(JsonParser* jp) {
    JsonValue* arr = NewValue(jp, JsonKind::Array);
    if (!arr) {
        return nullptr;
    }
    SkipSpace(jp);
    if (Eat(jp, ']')) {
        return arr;
    }
    JsonValue* last = nullptr;
    for (;;) {
        JsonValue* child = ParseValue(jp);
        if (!child) {
            jp->bad = true;
            return nullptr;
        }
        if (last) {
            last->next = child;
        } else {
            arr->first = child;
        }
        last = child;
        if (Eat(jp, ',')) {
            continue;
        }
        if (Eat(jp, ']')) {
            return arr;
        }
        jp->bad = true;
        return nullptr;
    }
}

static JsonValue* ParseValue(JsonParser* jp) {
    SkipSpace(jp);
    if (jp->p >= jp->end) {
        jp->bad = true;
        return nullptr;
    }
    char c = *jp->p;
    if (c == '{') {
        jp->p++;
        return ParseObject(jp);
    }
    if (c == '[') {
        jp->p++;
        return ParseArray(jp);
    }
    if (c == '"') {
        Str s = {};
        if (!ParseString(jp, &s)) {
            jp->bad = true;
            return nullptr;
        }
        JsonValue* v = NewValue(jp, JsonKind::String);
        if (v) {
            v->str = s;
        }
        return v;
    }
    if (Literal(jp, "true")) {
        JsonValue* v = NewValue(jp, JsonKind::Bool);
        if (v) {
            v->b = true;
        }
        return v;
    }
    if (Literal(jp, "false")) {
        return NewValue(jp, JsonKind::Bool);
    }
    if (Literal(jp, "null")) {
        return NewValue(jp, JsonKind::Null);
    }
    // A number. strtod takes whatever JSON allows, but it reads to a NUL, so
    // it is given a copy of what could still be part of one.
    char buf[64];
    int n = (int)(jp->end - jp->p);
    if (n > 63) {
        n = 63;
    }
    memcpy(buf, jp->p, (size_t)n);
    buf[n] = 0;
    char* stop = nullptr;
    double d = strtod(buf, &stop);
    if (!stop || stop == buf) {
        jp->bad = true;
        return nullptr;
    }
    jp->p += stop - buf;
    JsonValue* v = NewValue(jp, JsonKind::Number);
    if (v) {
        v->num = d;
    }
    return v;
}

JsonValue* JsonParse(Arena* a, Str text) {
    if (!a || !text.s || len(text) <= 0) {
        return nullptr;
    }
    JsonParser jp;
    jp.a = a;
    jp.p = text.s;
    jp.end = text.s + len(text);
    JsonValue* v = ParseValue(&jp);
    if (jp.bad) {
        return nullptr;
    }
    return v;
}

const JsonValue* JsonGet(const JsonValue* v, const char* key) {
    if (!v || v->kind != JsonKind::Object || !key) {
        return nullptr;
    }
    for (const JsonValue* c = v->first; c; c = c->next) {
        if (c->key.s && base::StrEqI(c->key, key)) {
            return c;
        }
    }
    return nullptr;
}

const JsonValue* JsonAt(const JsonValue* v, int index) {
    if (!v || index < 0) {
        return nullptr;
    }
    int i = 0;
    for (const JsonValue* c = v->first; c; c = c->next) {
        if (i == index) {
            return c;
        }
        i++;
    }
    return nullptr;
}

int JsonLen(const JsonValue* v) {
    int n = 0;
    if (!v) {
        return 0;
    }
    for (const JsonValue* c = v->first; c; c = c->next) {
        n++;
    }
    return n;
}

double JsonNumber(const JsonValue* v, double fallback) {
    return v && v->kind == JsonKind::Number ? v->num : fallback;
}

bool JsonBool(const JsonValue* v, bool fallback) {
    return v && v->kind == JsonKind::Bool ? v->b : fallback;
}

Str JsonString(const JsonValue* v, Str fallback) {
    return v && v->kind == JsonKind::String ? v->str : fallback;
}

// ─── the writer ──────────────────────────────────────────────────────────

// The comma before anything but the first thing at this depth, and the key
// when there is one.
static void Prefix(JsonWriter* w, const char* key) {
    if (!w->out) {
        return;
    }
    if (w->depth >= 0 && w->depth < 32 && w->wrote[w->depth]) {
        w->out->AppendChar(',');
    }
    if (w->depth >= 0 && w->depth < 32) {
        w->wrote[w->depth] = true;
    }
    if (!key) {
        return;
    }
    w->out->AppendChar('"');
    w->out->Append(Str(key));
    w->out->Append(StrL("\":"));
}

static void Push(JsonWriter* w) {
    w->depth++;
    if (w->depth < 32) {
        w->wrote[w->depth] = false;
    }
}

static void Pop(JsonWriter* w) {
    if (w->depth > 0) {
        w->depth--;
    }
}

void JsonWriter::BeginObject(const char* key) {
    Prefix(this, key);
    if (out) {
        out->AppendChar('{');
    }
    Push(this);
}
void JsonWriter::EndObject() {
    Pop(this);
    if (out) {
        out->AppendChar('}');
    }
}
void JsonWriter::BeginArray(const char* key) {
    Prefix(this, key);
    if (out) {
        out->AppendChar('[');
    }
    Push(this);
}
void JsonWriter::EndArray() {
    Pop(this);
    if (out) {
        out->AppendChar(']');
    }
}
void JsonWriter::Number(const char* key, double v) {
    Prefix(this, key);
    if (!out) {
        return;
    }
    // A whole number is written without a fractional part, the way serde
    // writes an integer; anything else keeps enough digits to come back.
    if (v == (double)(long long)v) {
        out->Append(fmt("%lld", (long long)v));
    } else {
        out->Append(fmt("%.6g", v));
    }
}
void JsonWriter::Bool(const char* key, bool v) {
    Prefix(this, key);
    if (out) {
        out->Append(v ? StrL("true") : StrL("false"));
    }
}
void JsonWriter::String(const char* key, Str v) {
    Prefix(this, key);
    if (!out) {
        return;
    }
    out->AppendChar('"');
    for (int i = 0; i < len(v); i++) {
        char c = v.s[i];
        if (c == '"' || c == '\\') {
            out->AppendChar('\\');
            out->AppendChar(c);
        } else if (c == '\n') {
            out->Append(StrL("\\n"));
        } else if (c == '\t') {
            out->Append(StrL("\\t"));
        } else if (c == '\r') {
            out->Append(StrL("\\r"));
        } else {
            out->AppendChar(c);
        }
    }
    out->AppendChar('"');
}
void JsonWriter::Null(const char* key) {
    Prefix(this, key);
    if (out) {
        out->Append(StrL("null"));
    }
}

void JsonWriter::Raw(const char* key, Str json) {
    Prefix(this, key);
    if (out) out->Append(json);
}

void JsonWriter::Value(const char* key, const JsonValue* value) {
    if (!value) {
        Null(key);
        return;
    }
    switch (value->kind) {
        case JsonKind::Null:
            Null(key);
            break;
        case JsonKind::Bool:
            Bool(key, value->b);
            break;
        case JsonKind::Number:
            Number(key, value->num);
            break;
        case JsonKind::String:
            String(key, value->str);
            break;
        case JsonKind::Array:
            BeginArray(key);
            for (const JsonValue* child = value->first; child;
                 child = child->next)
                Value(nullptr, child);
            EndArray();
            break;
        case JsonKind::Object:
            BeginObject(key);
            for (const JsonValue* child = value->first; child;
                 child = child->next) {
                Value(child->key ? child->key.s : nullptr, child);
            }
            EndObject();
            break;
    }
}

} // namespace gpui
