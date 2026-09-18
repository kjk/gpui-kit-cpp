#include "gpui/keymap.h"

namespace gpui {

// FNV-1a, the same one element ids are hashed with. An action is a name and
// nothing else, so two spellings of it are two actions — which is Rust's rule
// too, where two action types never compare equal.
static uint32_t HashName(Str s) {
    uint32_t h = 2166136261u;
    if (s.s) {
        for (int i = 0; i < len(s); i++) {
            h ^= (uint8_t)s.s[i];
            h *= 16777619u;
        }
    }
    // 0 is "no action", so a name that hashes there gets nudged off it.
    return h ? h : 1u;
}

uint32_t ActionOf(Str name) {
    return HashName(name);
}

static bool IsSpace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

// What a name is made of, in a context or in a predicate: "Editor",
// "single_line", "popup-menu", "gpui.Editor".
static bool IsNameChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
}

// --- parsing --------------------------------------------------------------

struct NamedKey {
    const char* name;
    int vk;
    // Rust names a shifted punctuation character itself ("?"), while this
    // port keeps the physical Win32 OEM code plus modifiers. The alias sets
    // the modifier so both spellings resolve to the same chord.
    bool shift = false;
};

// The names GPUI spells its keys with. The common codes are Windows virtual
// keys; the handful that only macOS/XKB can emit use the reserved range.
static const NamedKey kNamedKeys[] = {
    {"backspace", KeyBack},
    {"tab", KeyTab},
    {"enter", KeyReturn},
    {"return", KeyReturn},
    {"escape", KeyEscape},
    {"space", KeySpace},
    {"pageup", KeyPageUp},
    {"pagedown", KeyPageDown},
    {"end", KeyEnd},
    {"home", KeyHome},
    {"left", KeyLeft},
    {"up", KeyUp},
    {"right", KeyRight},
    {"down", KeyDown},
    {"insert", KeyInsert},
    {"delete", KeyDelete},
    {"back", KeyBrowserBack},
    {"forward", KeyBrowserForward},
    {"menu", KeyApps},
    // Linux names these dedicated XF86 keys. They remain valid bindings on
    // every platform, but only a keyboard that produces one can match them.
    {"cut", KeyCut},
    {"copy", KeyCopy},
    {"paste", KeyPaste},
    {"new", KeyNew},
    {"open", KeyOpen},
    {"save", KeySave},
    // XKB strips the KP_ prefix from the dedicated keypad operators.
    {"add", KeyKpAdd},
    {"subtract", KeyKpSubtract},
    {"multiply", KeyKpMultiply},
    {"divide", KeyKpDivide},
    {"decimal", KeyKpDecimal},
    {"separator", KeyKpSeparator},
    {"equal", KeyKpEqual},
    {"begin", KeyKpBegin},
    // Win32's OEM punctuation row, followed by its shifted aliases.
    {"-", KeyMinus},
    {"=", KeyEqual},
    {"[", KeyLeftBracket},
    {"]", KeyRightBracket},
    {"\\", KeyBackslash},
    {";", KeySemicolon},
    {"'", KeyQuote},
    {",", KeyComma},
    {".", KeyPeriod},
    {"/", KeySlash},
    {"`", KeyBacktick},
    {"_", KeyMinus, true},
    {"+", KeyEqual, true},
    {"{", KeyLeftBracket, true},
    {"}", KeyRightBracket, true},
    {"|", KeyBackslash, true},
    {":", KeySemicolon, true},
    {"\"", KeyQuote, true},
    {"<", KeyComma, true},
    {">", KeyPeriod, true},
    {"?", KeySlash, true},
    {"~", KeyBacktick, true},
    {"!", '1', true},
    {"@", '2', true},
    {"#", '3', true},
    {"$", '4', true},
    {"%", '5', true},
    {"^", '6', true},
    {"&", '7', true},
    {"*", '8', true},
    {"(", '9', true},
    {")", '0', true},
};

static int VkForName(Str name, bool* shift) {
    if (len(name) == 0) {
        return 0;
    }
    for (int i = 0; i < (int)(sizeof(kNamedKeys) / sizeof(kNamedKeys[0]));
         i++) {
        if (base::StrEqI(name, kNamedKeys[i].name)) {
            if (shift && kNamedKeys[i].shift) {
                *shift = true;
            }
            return kNamedKeys[i].vk;
        }
    }
    // Windows names F1..F24; macOS and XKB continue the same vocabulary to
    // F35. The latter live in the reserved portable range above the VKs.
    if (StrStartsWithAny(name, "fF") && len(name) >= 2 && len(name) <= 3) {
        int n = 0;
        for (int i = 1; i < len(name); i++) {
            if (name.s[i] < '0' || name.s[i] > '9') {
                return 0;
            }
            n = n * 10 + (name.s[i] - '0');
        }
        if (n >= 1 && n <= 24) {
            return KeyF1 + n - 1;
        }
        if (n >= 25 && n <= 35) {
            return KeyF25 + n - 25;
        }
        return 0;
    }
    if (len(name) != 1) {
        return 0;
    }
    char c = name.s[0];
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 'A';
    }
    if (c >= 'A' && c <= 'Z') {
        if (shift) {
            *shift = true;
        }
        return c;
    }
    if (c >= '0' && c <= '9') {
        return c;
    }
    return 0;
}

bool KeyChordParse(Str spec, KeyChord* out) {
    if (!out || len(spec) == 0) {
        return false;
    }
    KeyChord c = {};
    int i = 0;
    // The modifiers are dash-separated prefixes; the last field is the key,
    // which may itself be "-" — so a dash is only a separator when something
    // follows it.
    while (i < len(spec)) {
        int dash = -1;
        for (int j = i; j < len(spec) - 1; j++) {
            if (spec.s[j] == '-') {
                dash = j;
                break;
            }
        }
        if (dash < 0) {
            break;
        }
        Str part = Str(spec.s + i, dash - i);
        bool secondary = base::StrEqI(part, "secondary");
        if (base::StrEqI(part, "ctrl") || (secondary && !GPUI_OS_MAC)) {
            c.ctrl = true;
        } else if (base::StrEqI(part, "cmd") || base::StrEqI(part, "super") ||
                   base::StrEqI(part, "win")) {
            c.platform = true;
        }
#if GPUI_OS_MAC
        else if (secondary) {
            // The shortcut modifier: Command on macOS, Control elsewhere.
            // One `secondary-c` binding is Cmd-C on a Mac and Ctrl-C on the
            // other two, which is what Rust's `secondary-` means.
            c.platform = true;
        }
#endif
        else if (base::StrEqI(part, "alt") || base::StrEqI(part, "option")) {
            c.alt = true;
        } else if (base::StrEqI(part, "shift")) {
            c.shift = true;
        } else if (base::StrEqI(part, "fn")) {
            c.function = true;
        } else {
            return false;
        }
        i = dash + 1;
    }
    c.vk = VkForName(Str(spec.s + i, len(spec) - i), &c.shift);
    if (!c.vk) {
        return false;
    }
    *out = c;
    return true;
}

bool KeyChordEq(const KeyChord& a, const KeyChord& b) {
    return a.vk == b.vk && a.shift == b.shift && a.ctrl == b.ctrl &&
           a.alt == b.alt && a.platform == b.platform &&
           a.function == b.function;
}

int KeyChordsParse(Str spec, KeyChord* out, int maxChords) {
    if (!out || maxChords <= 0 || len(spec) == 0) {
        return 0;
    }
    int n = 0;
    int i = 0;
    while (i < len(spec)) {
        while (i < len(spec) && IsSpace(spec.s[i])) {
            i++;
        }
        int start = i;
        while (i < len(spec) && !IsSpace(spec.s[i])) {
            i++;
        }
        if (i == start) {
            break;
        }
        // A sequence longer than the matcher can hold is a spec it cannot
        // read, the same as a key it does not know.
        if (n >= maxChords) {
            return 0;
        }
        if (!KeyChordParse(Str(spec.s + start, i - start), &out[n])) {
            return 0;
        }
        n++;
    }
    return n;
}

// --- key contexts ---------------------------------------------------------

// More than an element declares: "Editor mode=full vim_mode=normal" is
// already past what anything in the Rust tree writes.
static const int kMaxIdents = 4;
static const int kMaxPairs = 4;

struct ParsedContext {
    uint32_t id = 0;
    uint32_t idents[kMaxIdents] = {};
    int nIdents = 0;
    uint32_t keys[kMaxPairs] = {};
    uint32_t vals[kMaxPairs] = {};
    int nPairs = 0;
};

// Every distinct context spelling in the process. An element rebuilds its own
// out of the same literal every frame, so this fills once and then only ever
// answers.
static const int kMaxContexts = 128;
static ParsedContext gContexts[kMaxContexts];
static int gNContexts = 0;

static void ParseContextInto(Str spec, ParsedContext* c) {
    int i = 0;
    while (i < len(spec)) {
        while (i < len(spec) && IsSpace(spec.s[i])) {
            i++;
        }
        int s0 = i;
        while (i < len(spec) && IsNameChar(spec.s[i])) {
            i++;
        }
        if (i == s0) {
            // Nothing a context is made of. Step over it rather than stop:
            // the id is the whole spelling either way.
            i++;
            continue;
        }
        uint32_t name = HashName(Str(spec.s + s0, i - s0));
        int j = i;
        while (j < len(spec) && IsSpace(spec.s[j])) {
            j++;
        }
        if (j < len(spec) && spec.s[j] == '=') {
            j++;
            while (j < len(spec) && IsSpace(spec.s[j])) {
                j++;
            }
            int v0 = j;
            while (j < len(spec) && IsNameChar(spec.s[j])) {
                j++;
            }
            if (j > v0 && c->nPairs < kMaxPairs) {
                c->keys[c->nPairs] = name;
                c->vals[c->nPairs] = HashName(Str(spec.s + v0, j - v0));
                c->nPairs++;
            }
            i = j;
            continue;
        }
        if (c->nIdents < kMaxIdents) {
            c->idents[c->nIdents++] = name;
        }
    }
}

uint32_t KeyContextOf(Str name) {
    uint32_t id = HashName(name);
    for (int i = 0; i < gNContexts; i++) {
        if (gContexts[i].id == id) {
            return id;
        }
    }
    if (gNContexts < kMaxContexts) {
        ParsedContext c;
        c.id = id;
        ParseContextInto(name, &c);
        gContexts[gNContexts++] = c;
    }
    return id;
}

// One level of the stack the matcher works on: the id the dispatch recorded,
// and the parse behind it. Resolved once per keystroke rather than once per
// node of every predicate, which is the difference between one table scan and
// a scan for each half of an `&&`.
struct CtxLevel {
    uint32_t id = 0;
    const ParsedContext* c = nullptr;
};

static CtxLevel ResolveContext(uint32_t id) {
    CtxLevel l;
    l.id = id;
    for (int i = 0; i < gNContexts; i++) {
        if (gContexts[i].id == id) {
            l.c = &gContexts[i];
            break;
        }
    }
    return l;
}

static bool ContextHasIdent(const CtxLevel& l, uint32_t ident) {
    if (!l.c) {
        // The table filled. A context that is one plain name hashes to that
        // name, so the common one still resolves.
        return ident == l.id;
    }
    for (int i = 0; i < l.c->nIdents; i++) {
        if (l.c->idents[i] == ident) {
            return true;
        }
    }
    return false;
}

static bool ContextValue(const CtxLevel& l, uint32_t key, uint32_t* val) {
    if (!l.c) {
        return false;
    }
    for (int i = 0; i < l.c->nPairs; i++) {
        if (l.c->keys[i] == key) {
            *val = l.c->vals[i];
            return true;
        }
    }
    return false;
}

// --- context predicates ---------------------------------------------------

// KeyBindingContextPredicate. Ident carries the name; Eq and Neq the key and
// the value; the rest their operands.
enum class PredOp : uint8_t {
    Ident,
    Eq,
    Neq,
    Not,
    And,
    Or,
    Child
};

struct PredNode {
    PredOp op = PredOp::Ident;
    uint32_t a = 0;
    uint32_t b = 0;
    int16_t l = -1;
    int16_t r = -1;
};

// Predicates live as long as the bindings that hold them, so the pool is
// emptied with the keymap.
static const int kMaxPreds = 256;
static PredNode gPreds[kMaxPreds];
static int gNPreds = 0;

struct PredParser {
    Str s;
    int i = 0;
    bool bad = false;
};

static void PredSkipWs(PredParser* p) {
    while (p->i < len(p->s) && IsSpace(p->s.s[p->i])) {
        p->i++;
    }
}

static int PredAlloc(PredParser* p, PredOp op, uint32_t a, uint32_t b, int l,
                     int r) {
    if (gNPreds >= kMaxPreds) {
        p->bad = true;
        return -1;
    }
    PredNode& n = gPreds[gNPreds];
    n.op = op;
    n.a = a;
    n.b = b;
    n.l = (int16_t)l;
    n.r = (int16_t)r;
    return gNPreds++;
}

static int ParsePredExpr(PredParser* p, int minPrec);

static int ParsePredPrimary(PredParser* p) {
    PredSkipWs(p);
    if (p->i >= len(p->s)) {
        p->bad = true;
        return -1;
    }
    char c = p->s.s[p->i];
    // `!` is negation unless it is the first half of `!=`, which is an
    // operator and belongs to the caller.
    if (c == '!' && !(p->i + 1 < len(p->s) && p->s.s[p->i + 1] == '=')) {
        p->i++;
        int inner = ParsePredPrimary(p);
        if (p->bad) {
            return -1;
        }
        return PredAlloc(p, PredOp::Not, 0, 0, inner, -1);
    }
    if (c == '(') {
        p->i++;
        int e = ParsePredExpr(p, 1);
        PredSkipWs(p);
        if (p->bad || p->i >= len(p->s) || p->s.s[p->i] != ')') {
            p->bad = true;
            return -1;
        }
        p->i++;
        return e;
    }
    int s0 = p->i;
    while (p->i < len(p->s) && IsNameChar(p->s.s[p->i])) {
        p->i++;
    }
    if (p->i == s0) {
        p->bad = true;
        return -1;
    }
    return PredAlloc(p, PredOp::Ident, HashName(Str(p->s.s + s0, p->i - s0)), 0,
                     -1, -1);
}

struct PredOpTok {
    PredOp op;
    int prec;
    int len;
};

// Rust's precedence: the child operator binds loosest, so "Workspace > Editor
// && mode == full" is a Workspace over an Editor that has the mode, not a
// child of a conjunction.
static bool PeekPredOp(PredParser* p, PredOpTok* out) {
    PredSkipWs(p);
    int n = len(p->s) - p->i;
    const char* s = p->s.s + p->i;
    if (n >= 2 && s[0] == '&' && s[1] == '&') {
        *out = {PredOp::And, 3, 2};
        return true;
    }
    if (n >= 2 && s[0] == '|' && s[1] == '|') {
        *out = {PredOp::Or, 2, 2};
        return true;
    }
    if (n >= 2 && s[0] == '=' && s[1] == '=') {
        *out = {PredOp::Eq, 4, 2};
        return true;
    }
    if (n >= 2 && s[0] == '!' && s[1] == '=') {
        *out = {PredOp::Neq, 4, 2};
        return true;
    }
    if (n >= 1 && s[0] == '>') {
        *out = {PredOp::Child, 1, 1};
        return true;
    }
    return false;
}

static int ParsePredExpr(PredParser* p, int minPrec) {
    int left = ParsePredPrimary(p);
    if (p->bad) {
        return -1;
    }
    for (;;) {
        PredOpTok t = {};
        if (!PeekPredOp(p, &t) || t.prec < minPrec) {
            break;
        }
        p->i += t.len;
        int right = ParsePredExpr(p, t.prec + 1);
        if (p->bad) {
            return -1;
        }
        if (t.op == PredOp::Eq || t.op == PredOp::Neq) {
            // Both sides of a comparison are plain names: `mode == full`.
            if (gPreds[left].op != PredOp::Ident ||
                gPreds[right].op != PredOp::Ident) {
                p->bad = true;
                return -1;
            }
            left = PredAlloc(p, t.op, gPreds[left].a, gPreds[right].a, -1, -1);
        } else {
            left = PredAlloc(p, t.op, 0, 0, left, right);
        }
        if (p->bad) {
            return -1;
        }
    }
    return left;
}

// The root of the parsed predicate, or -1 for one that cannot be read — which
// drops the binding, the same as a chord that cannot be read.
static int PredParse(Str spec, bool* ok) {
    PredParser p;
    p.s = spec;
    int root = ParsePredExpr(&p, 1);
    PredSkipWs(&p);
    if (p.bad || p.i != len(p.s) || root < 0) {
        *ok = false;
        return -1;
    }
    *ok = true;
    return root;
}

// `levels` is the ancestry from the level being tried outwards, innermost
// first; the innermost is the one an identifier or a comparison is read
// against, which is Rust's `contexts.last()`.
static bool PredEval(int ix, const CtxLevel* levels, int n) {
    if (ix < 0 || n <= 0) {
        // Rust reads nothing out of an empty stack, negation included.
        return false;
    }
    const PredNode& p = gPreds[ix];
    uint32_t val = 0;
    switch (p.op) {
        case PredOp::Ident:
            return ContextHasIdent(levels[0], p.a);
        case PredOp::Eq:
            return ContextValue(levels[0], p.a, &val) && val == p.b;
        case PredOp::Neq:
            // A key the context does not carry is not equal to the value,
            // which is `context.get(k) != Some(v)`.
            return !ContextValue(levels[0], p.a, &val) || val != p.b;
        case PredOp::Not:
            return !PredEval(p.l, levels, n);
        case PredOp::And:
            return PredEval(p.l, levels, n) && PredEval(p.r, levels, n);
        case PredOp::Or:
            return PredEval(p.l, levels, n) || PredEval(p.r, levels, n);
        case PredOp::Child:
            // The right half against this level, the left half against what
            // encloses it.
            return PredEval(p.r, levels, n) && PredEval(p.l, levels + 1, n - 1);
    }
    return false;
}

// --- the keymap -----------------------------------------------------------

struct BoundKey {
    KeyChord strokes[kMaxStrokes] = {};
    int nStrokes = 0;
    uint32_t action = 0;
    intptr_t arg = 0;
    int pred = -1; // -1: anywhere
};

// More than any application binds. Rust grows a Vec; this is a fixed table so
// the keymap costs nothing until something is bound.
static const int kMaxBindings = 256;
static BoundKey gBindings[kMaxBindings];
static int gNBindings = 0;

// The chords of a sequence that has begun but not finished. Rust keeps the
// same on the window's matcher; the keymap is process-wide here, and so is
// this.
static KeyChord gPending[kMaxStrokes];
static int gNPending = 0;

void KeymapClearPending() {
    gNPending = 0;
}

Str KeyName(int vk) {
    for (size_t i = 0; i < sizeof(kNamedKeys) / sizeof(kNamedKeys[0]); i++) {
        if (kNamedKeys[i].vk == vk && !kNamedKeys[i].shift) {
            return Str(kNamedKeys[i].name);
        }
    }
    // f1..f35, the other half of the parse above: a binding may be written on
    // one, so a menu row or a tooltip has to be able to say which. The buffer
    // is static for the same reason the one below is — the name outlives the
    // call, and only one is ever being looked at.
    int n = 0;
    if (vk >= KeyF1 && vk <= KeyF24) {
        n = vk - KeyF1 + 1;
    } else if (vk >= KeyF25 && vk <= KeyF35) {
        n = vk - KeyF25 + 25;
    }
    if (n) {
        static char fkey[4] = {};
        fkey[0] = 'f';
        if (n < 10) {
            fkey[1] = (char)('0' + n);
            return Str(fkey, 2);
        }
        fkey[1] = (char)('0' + n / 10);
        fkey[2] = (char)('0' + n % 10);
        return Str(fkey, 3);
    }
    // A letter or a digit is its own lowercase name, which is how a binding
    // spells it. The buffer is static because a name outlives the call and
    // there is only ever one being looked at.
    static char one[2] = {};
    if ((vk >= 'A' && vk <= 'Z')) {
        one[0] = (char)(vk - 'A' + 'a');
        return Str(one, 1);
    }
    if (vk >= '0' && vk <= '9') {
        one[0] = (char)vk;
        return Str(one, 1);
    }
    return {};
}

bool KeymapPending() {
    return gNPending > 0;
}

// Bumped by every clear. An init that bound into an older one binds again.
static uint32_t gGeneration = 1;

uint32_t KeymapGeneration() {
    return gGeneration;
}

void KeymapClear() {
    gNBindings = 0;
    gNPreds = 0;
    gNPending = 0;
    gGeneration++;
    if (!gGeneration) {
        gGeneration = 1;
    }
}

void KeymapBind(const KeyBinding* bindings, int n) {
    for (int i = 0; i < n && gNBindings < kMaxBindings; i++) {
        BoundKey b;
        if (!bindings[i].stroke || !bindings[i].action) {
            continue;
        }
        b.nStrokes =
            KeyChordsParse(Str(bindings[i].stroke), b.strokes, kMaxStrokes);
        if (!b.nStrokes) {
            continue;
        }
        if (bindings[i].context) {
            bool ok = false;
            b.pred = PredParse(Str(bindings[i].context), &ok);
            if (!ok) {
                continue;
            }
        }
        b.action = bindings[i].action;
        b.arg = bindings[i].arg;
        gBindings[gNBindings++] = b;
    }
}

// Whether a binding is in play at this level. A scoped pass wants a predicate
// that holds here; the unscoped pass, which runs last, is the bindings that
// named no context at all.
static bool BindingApplies(const BoundKey& b, const CtxLevel* levels, int n) {
    if (n == 0) {
        return b.pred < 0;
    }
    return b.pred >= 0 && PredEval(b.pred, levels, n);
}

// One level of the stack, or — with nothing passed — the unscoped pass. The
// last binding for a chord wins, so the search runs backwards. Answers the
// action of a binding this chord completes, and notes on the way whether one
// it only begins is left waiting.
static uint32_t MatchIn(const CtxLevel* levels, int n, bool* pending,
                        intptr_t* arg) {
    for (int i = gNBindings - 1; i >= 0; i--) {
        const BoundKey& b = gBindings[i];
        if (!BindingApplies(b, levels, n) || b.nStrokes < gNPending) {
            continue;
        }
        bool begins = true;
        for (int k = 0; k < gNPending; k++) {
            if (!KeyChordEq(b.strokes[k], gPending[k])) {
                begins = false;
                break;
            }
        }
        if (!begins) {
            continue;
        }
        if (b.nStrokes == gNPending) {
            *arg = b.arg;
            return b.action;
        }
        // Begun but not finished. Noted rather than returned: a binding that
        // is complete beats one that is only started, which is what makes
        // "ctrl-k" fire even with "ctrl-k ctrl-o" bound beside it.
        *pending = true;
    }
    return 0;
}

// The whole stack, innermost level first and the unscoped bindings last.
// `pending` comes back set when nothing completed but something was begun.
static uint32_t MatchStack(const CtxLevel* levels, int n, bool* pending,
                           intptr_t* arg) {
    for (int lvl = 0; lvl < n; lvl++) {
        uint32_t action = MatchIn(levels + lvl, n - lvl, pending, arg);
        if (action) {
            return action;
        }
    }
    return MatchIn(nullptr, 0, pending, arg);
}

// Whether `b` is in play at this level and names `action`, and if so what its
// first chord is.
static bool BindingNames(const BoundKey& b, uint32_t action,
                         const CtxLevel* levels, int n, KeyChord* out) {
    if (b.action != action || b.nStrokes <= 0 ||
        !BindingApplies(b, levels, n)) {
        return false;
    }
    *out = b.strokes[0];
    return true;
}

bool KeymapBindingForAction(uint32_t action, const uint32_t* contexts,
                            int nContexts, KeyChord* out) {
    if (!action || !out) {
        return false;
    }
    if (nContexts > kMaxContextDepth) {
        nContexts = kMaxContextDepth;
    }
    CtxLevel levels[kMaxContextDepth];
    for (int i = 0; i < nContexts && contexts; i++) {
        levels[i] = ResolveContext(contexts[i]);
    }
    if (!contexts) {
        nContexts = 0;
    }
    // The scoped passes first, innermost out, then the bindings that named no
    // context — the order KeymapMatch resolves a chord in, so the answer is
    // the binding that chord would actually have fired.
    for (int lvl = 0; lvl < nContexts; lvl++) {
        for (int i = gNBindings - 1; i >= 0; i--) {
            if (BindingNames(gBindings[i], action, levels + lvl,
                             nContexts - lvl, out)) {
                return true;
            }
        }
    }
    for (int i = gNBindings - 1; i >= 0; i--) {
        if (BindingNames(gBindings[i], action, nullptr, 0, out)) {
            return true;
        }
    }
    return false;
}

bool KeymapAnyBindingForAction(uint32_t action, KeyChord* out) {
    if (!action || !out) {
        return false;
    }
    // Backwards, so the last binding for an action wins here too.
    for (int i = gNBindings - 1; i >= 0; i--) {
        if (gBindings[i].action == action && gBindings[i].nStrokes > 0) {
            *out = gBindings[i].strokes[0];
            return true;
        }
    }
    return false;
}

KeyMatch KeymapMatch(const KeyChord& chord, const uint32_t* contexts,
                     int nContexts) {
    if (gNPending >= kMaxStrokes) {
        gNPending = 0;
    }
    gPending[gNPending++] = chord;

    if (nContexts > kMaxContextDepth) {
        nContexts = kMaxContextDepth;
    }
    CtxLevel levels[kMaxContextDepth];
    for (int i = 0; i < nContexts; i++) {
        levels[i] = ResolveContext(contexts[i]);
    }

    KeyMatch m;
    bool pending = false;
    m.action = MatchStack(levels, nContexts, &pending, &m.arg);
    if (!m.action && pending) {
        // Begun but not finished: the chord stays held, and the window keeps
        // the next keystroke for the keymap.
        m.pending = true;
        return m;
    }
    // Finished, or continued nothing — either way what was held is spent,
    // which is Rust's matcher clearing its pending keystrokes.
    gNPending = 0;
    return m;
}

} // namespace gpui
