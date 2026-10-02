#include "shell/component_registry.h"

#include "shell/materialize.h"

#include <string.h>

namespace gpui::shell {

const char kDefaultComponentModule[] = "gpui-component";

const char* const kRegisteredCommonBehaviors[3] = {"disabled", "selected",
                                                   "on_click"};

// Module names the runtime answers to itself. A component catalog may not
// claim one: the runtime's resolvers run first.
static const char* const kRuntimeModuleSpecifiers[] = {
    "gpui-kit", "gpui", "gpui-base", "gpui-shell", "gpui-fps"};

static const char* const kAnchorNames[] = {
    "top_left",      "top_center",   "top_right",   "bottom_left",
    "bottom_center", "bottom_right", "left_center", "right_center"};
static const char* const kAxisNames[] = {"horizontal", "vertical"};
static const char* const kModeNames[] = {"scrolling", "hover", "always"};
static const char* const kMouseButtonNames[] = {"left", "right", "middle"};

// The prototype entry wins over the generic descriptor dispatch, so a
// descriptor that declares one of these names inherits that check. If the two
// vocabularies disagree the method becomes unreachable by any value.
static const CheckedVocabulary kCheckedVocabularies[] = {
    {"anchor", kAnchorNames},
    {"axis", kAxisNames},
    {"mode", kModeNames},
    {"mouse_button", kMouseButtonNames},
};

Slice<CheckedVocabulary> PreludeCheckedVocabularies() {
    return kCheckedVocabularies;
}

bool IsRegisteredCommonSlot(Str name) {
    static const char names[] =
        "content\0trigger\0input\0decrement_button\0increment_button\0image\0"
        "fallback\0header\0footer\0panel\0";
    for (const char* at = names; *at; at += strlen(at) + 1) {
        if (StrEq(name, at)) return true;
    }
    return false;
}

bool IsJavaScriptIdentifier(Str name) {
    if (len(name) == 0) return false;
    auto alpha = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    };
    char first = name.s[0];
    if (!(first == '_' || first == '$' || alpha(first))) return false;
    for (int i = 1; i < len(name); i++) {
        char c = name.s[i];
        if (!(c == '_' || c == '$' || alpha(c) || (c >= '0' && c <= '9')))
            return false;
    }
    static const char reserved[] =
        "arguments\0await\0break\0case\0catch\0class\0const\0continue\0"
        "debugger\0default\0delete\0do\0else\0enum\0eval\0export\0extends\0"
        "false\0finally\0for\0function\0if\0implements\0import\0in\0"
        "instanceof\0interface\0let\0new\0null\0package\0private\0"
        "protected\0public\0return\0static\0super\0switch\0this\0throw\0"
        "true\0try\0typeof\0var\0void\0while\0with\0yield\0";
    for (const char* at = reserved; *at; at += strlen(at) + 1) {
        if (StrEq(name, at)) return false;
    }
    return true;
}

static bool Same(const char* a, const char* b) {
    if (a == b) return true;
    if (!a || !b) return false;
    return strcmp(a, b) == 0;
}

bool RegistryError::operator==(const RegistryError& other) const {
    return kind == other.kind && expected == other.expected &&
           actual == other.actual && Same(component, other.component) &&
           Same(callable, other.callable) && Same(argument, other.argument) &&
           Same(reason, other.reason) && Same(literal, other.literal);
}

static Str S(const char* text) {
    return Str(text ? text : "");
}

Str RegistryErrorMessage(Arena* into, const RegistryError& error) {
    TempStr text;
    switch (error.kind) {
        case RegistryErrorKind::None:
            text = fmt("");
            break;
        case RegistryErrorKind::IncompatibleApiVersion:
            text =
                fmt("component registry API version %d is incompatible; "
                    "expected %d",
                    (int)error.actual, (int)error.expected);
            break;
        case RegistryErrorKind::DuplicateComponent:
            text =
                fmt("component `%s` is already registered", S(error.component));
            break;
        case RegistryErrorKind::InvalidComponent:
            text =
                fmt("component name `%s` is not a valid non-reserved "
                    "identifier",
                    S(error.component));
            break;
        case RegistryErrorKind::DuplicateExport:
            text = fmt("JavaScript export `%s` is already registered",
                       S(error.callable));
            break;
        case RegistryErrorKind::InvalidExport:
            text =
                fmt("JavaScript export `%s` is not a valid non-reserved "
                    "identifier",
                    S(error.callable));
            break;
        case RegistryErrorKind::InvalidMethod:
            text =
                fmt("component `%s` method `%s` is not a valid "
                    "non-reserved identifier",
                    S(error.component), S(error.callable));
            break;
        case RegistryErrorKind::InvalidArgument:
            text =
                fmt("component `%s` callable `%s` argument `%s` is not a "
                    "valid non-reserved identifier",
                    S(error.component), S(error.callable), S(error.argument));
            break;
        case RegistryErrorKind::DuplicateArgument:
            text =
                fmt("component `%s` callable `%s` repeats argument `%s`",
                    S(error.component), S(error.callable), S(error.argument));
            break;
        case RegistryErrorKind::InvalidArgumentSchema:
            text =
                fmt("component `%s` callable `%s` argument `%s` has an "
                    "invalid schema: %s",
                    S(error.component), S(error.callable), S(error.argument),
                    S(error.reason));
            break;
        case RegistryErrorKind::RequiredArgumentAfterOptional:
            text =
                fmt("component `%s` callable `%s` has required argument "
                    "`%s` after an optional argument",
                    S(error.component), S(error.callable), S(error.argument));
            break;
        case RegistryErrorKind::UndocumentedMethod:
            text =
                fmt("component `%s` method `%s` has no documentation; call "
                    "MethodDescriptor::with_documentation",
                    S(error.component), S(error.callable));
            break;
        case RegistryErrorKind::UnreachableMethodVocabulary:
            text =
                fmt("component `%s` method `%s` declares `%s`, which the "
                    "runtime's own `%s` check rejects before the call can "
                    "reach the component; the method would be unreachable "
                    "by any value, so either match that vocabulary or "
                    "choose another method name",
                    S(error.component), S(error.callable), S(error.literal),
                    S(error.callable));
            break;
        case RegistryErrorKind::DuplicateMethod:
            text = fmt("component `%s` registers method `%s` twice",
                       S(error.component), S(error.callable));
            break;
        case RegistryErrorKind::InvalidDeprecationReplacement:
            text =
                fmt("component `%s` deprecated export `%s` must name "
                    "another export from the same descriptor, not `%s`",
                    S(error.component), S(error.callable), S(error.argument));
            break;
        case RegistryErrorKind::EmptyConstructorList:
            text = fmt("component `%s` has no JavaScript constructor",
                       S(error.component));
            break;
        case RegistryErrorKind::InvalidStateKind:
            text = fmt("state kind `%s` is not a valid non-reserved identifier",
                       S(error.component));
            break;
        case RegistryErrorKind::DuplicateStateKind:
            text = fmt("state kind `%s` is already registered",
                       S(error.component));
            break;
        case RegistryErrorKind::InvalidModuleSpecifier:
            text =
                fmt("`%s` is not a usable component module specifier; it "
                    "must be non-empty and must not be one of the "
                    "runtime's own modules",
                    S(error.component));
            break;
    }
    return StrDup(into, text);
}

static bool Fill(RegistryError* error, RegistryError value) {
    if (error) *error = value;
    return false;
}

// ─── Validation ────────────────────────────────────────────────────────────

static const char* ValidateSchema(const ArgumentSchema& schema, bool topLevel) {
    switch (schema.kind) {
        case SchemaKind::String:
        case SchemaKind::Number:
        case SchemaKind::Boolean:
        case SchemaKind::Element:
            return nullptr;
        case SchemaKind::Entity:
            if (len(StrTrim(S(schema.text))) == 0)
                return "entity kind must not be empty";
            return nullptr;
        case SchemaKind::Callback:
            if (len(StrTrim(S(schema.text))) == 0)
                return "callback signature must not be empty";
            return nullptr;
        case SchemaKind::Enum: {
            if (schema.values.count == 0)
                return "enum must contain at least one literal";
            for (const char* value : schema.values) {
                if (!value || !*value) return "enum literals must not be empty";
            }
            for (int i = 0; i < schema.values.count; i++) {
                for (int j = i + 1; j < schema.values.count; j++) {
                    if (strcmp(schema.values[i], schema.values[j]) == 0)
                        return "enum literals must be unique";
                }
            }
            return nullptr;
        }
        case SchemaKind::Array:
            return schema.item ? ValidateSchema(*schema.item, false)
                               : "array schema has no item";
        case SchemaKind::Optional:
            if (!topLevel)
                return "optional schemas are only valid for top-level "
                       "arguments";
            return schema.item ? ValidateSchema(*schema.item, false)
                               : "optional schema has no item";
    }
    return nullptr;
}

static bool ValidateArguments(const char* component, const char* callable,
                              Slice<ArgumentDescriptor> arguments,
                              RegistryError* error) {
    bool sawOptional = false;
    for (int i = 0; i < arguments.count; i++) {
        const ArgumentDescriptor& argument = arguments[i];
        if (!IsJavaScriptIdentifier(S(argument.name))) {
            RegistryError e;
            e.kind = RegistryErrorKind::InvalidArgument;
            e.component = component;
            e.callable = callable;
            e.argument = argument.name;
            return Fill(error, e);
        }
        for (int j = 0; j < i; j++) {
            if (strcmp(arguments[j].name, argument.name) == 0) {
                RegistryError e;
                e.kind = RegistryErrorKind::DuplicateArgument;
                e.component = component;
                e.callable = callable;
                e.argument = argument.name;
                return Fill(error, e);
            }
        }
        if (const char* reason = ValidateSchema(argument.schema, true)) {
            RegistryError e;
            e.kind = RegistryErrorKind::InvalidArgumentSchema;
            e.component = component;
            e.callable = callable;
            e.argument = argument.name;
            e.reason = reason;
            return Fill(error, e);
        }
        if (argument.schema.kind == SchemaKind::Optional) {
            sawOptional = true;
        } else if (sawOptional) {
            RegistryError e;
            e.kind = RegistryErrorKind::RequiredArgumentAfterOptional;
            e.component = component;
            e.callable = callable;
            e.argument = argument.name;
            return Fill(error, e);
        }
    }
    return true;
}

// The first enum literal an argument list declares, at any depth, that
// `accepted` does not contain.
static const char* ForeignLiteral(const ArgumentSchema& schema,
                                  Slice<const char*> accepted) {
    if (schema.kind == SchemaKind::Enum) {
        for (const char* value : schema.values) {
            bool found = false;
            for (const char* known : accepted) {
                if (strcmp(known, value) == 0) found = true;
            }
            if (!found) return value;
        }
        return nullptr;
    }
    if ((schema.kind == SchemaKind::Array ||
         schema.kind == SchemaKind::Optional) &&
        schema.item) {
        return ForeignLiteral(*schema.item, accepted);
    }
    return nullptr;
}

ComponentRegistry::~ComponentRegistry() {
    VecReset(descriptors);
    VecReset(states);
    VecReset(exports);
    VecReset(stateKinds);
}

bool ComponentRegistry::Open(uint32_t apiVersion, const char* module,
                             RegistryError* error) {
    if (apiVersion != kComponentRegistryApiVersion) {
        RegistryError e;
        e.kind = RegistryErrorKind::IncompatibleApiVersion;
        e.expected = kComponentRegistryApiVersion;
        e.actual = apiVersion;
        return Fill(error, e);
    }
    bool reserved = false;
    for (const char* name : kRuntimeModuleSpecifiers) {
        if (module && strcmp(name, module) == 0) reserved = true;
    }
    if (!module || len(StrTrim(Str(module))) == 0 || reserved) {
        RegistryError e;
        e.kind = RegistryErrorKind::InvalidModuleSpecifier;
        e.component = module;
        return Fill(error, e);
    }
    moduleSpecifier = module;
    return true;
}

void ComponentRegistry::WithInitializer(ComponentInitializer value) {
    initializer = value;
}

void ComponentRegistry::WithWindowOpener(ComponentWindowOpener value) {
    windowOpener = value;
}

bool ComponentRegistry::HasExport(const char* name) const {
    for (int i = 0; i < len(exports); i++) {
        if (strcmp(exports[i], name) == 0) return true;
    }
    return false;
}

bool ComponentRegistry::Register(const ComponentDescriptor* d,
                                 RegistryError* error, uint32_t* id) {
    RegistryError e;
    e.component = d->name;
    for (int i = 0; i < len(descriptors); i++) {
        if (strcmp(descriptors[i]->name, d->name) == 0) {
            e.kind = RegistryErrorKind::DuplicateComponent;
            return Fill(error, e);
        }
    }
    if (!IsJavaScriptIdentifier(S(d->name))) {
        e.kind = RegistryErrorKind::InvalidComponent;
        return Fill(error, e);
    }
    if (d->constructors.count == 0) {
        e.kind = RegistryErrorKind::EmptyConstructorList;
        return Fill(error, e);
    }
    for (int i = 0; i < d->methods.count; i++) {
        const MethodDescriptor& method = d->methods[i];
        e.callable = method.name;
        if (!IsJavaScriptIdentifier(S(method.name))) {
            e.kind = RegistryErrorKind::InvalidMethod;
            return Fill(error, e);
        }
        for (int j = 0; j < i; j++) {
            if (strcmp(d->methods[j].name, method.name) == 0) {
                e.kind = RegistryErrorKind::DuplicateMethod;
                return Fill(error, e);
            }
        }
        for (const CheckedVocabulary& checked : kCheckedVocabularies) {
            if (strcmp(checked.method, method.name) != 0) continue;
            for (const ArgumentDescriptor& argument : method.arguments) {
                if (const char* literal =
                        ForeignLiteral(argument.schema, checked.accepted)) {
                    e.kind = RegistryErrorKind::UnreachableMethodVocabulary;
                    e.literal = literal;
                    return Fill(error, e);
                }
            }
        }
        // Every method becomes a line of gpui-kit.d.ts a script author reads
        // in an editor, so an undocumented one is refused rather than filled
        // in with a default sentence.
        if (!method.documentation) {
            e.kind = RegistryErrorKind::UndocumentedMethod;
            return Fill(error, e);
        }
        if (!ValidateArguments(d->name, method.name, method.arguments, error))
            return false;
    }
    for (int i = 0; i < d->constructors.count; i++) {
        const ConstructorDescriptor& constructor = d->constructors[i];
        if (!IsJavaScriptIdentifier(S(constructor.exportName))) {
            RegistryError x;
            x.kind = RegistryErrorKind::InvalidExport;
            x.callable = constructor.exportName;
            return Fill(error, x);
        }
        if (!ValidateArguments(d->name, constructor.exportName,
                               constructor.arguments, error))
            return false;
        bool repeated = HasExport(constructor.exportName);
        for (int j = 0; j < i; j++) {
            if (strcmp(d->constructors[j].exportName, constructor.exportName) ==
                0)
                repeated = true;
        }
        if (repeated) {
            RegistryError x;
            x.kind = RegistryErrorKind::DuplicateExport;
            x.callable = constructor.exportName;
            return Fill(error, x);
        }
    }
    for (const ConstructorDescriptor& constructor : d->constructors) {
        const char* replacement = constructor.deprecationReplacement;
        if (!replacement) continue;
        bool known = false;
        for (const ConstructorDescriptor& other : d->constructors) {
            if (strcmp(other.exportName, replacement) == 0) known = true;
        }
        if (strcmp(replacement, constructor.exportName) == 0 || !known) {
            e.kind = RegistryErrorKind::InvalidDeprecationReplacement;
            e.callable = constructor.exportName;
            e.argument = replacement;
            return Fill(error, e);
        }
    }
    if (id) *id = (uint32_t)len(descriptors);
    for (const ConstructorDescriptor& constructor : d->constructors)
        VecAppend(exports, constructor.exportName);
    VecAppend(descriptors, d);
    return true;
}

bool ComponentRegistry::RegisterState(const StateDescriptor* d,
                                      RegistryError* error) {
    if (!IsJavaScriptIdentifier(S(d->exportName))) {
        RegistryError e;
        e.kind = RegistryErrorKind::InvalidExport;
        e.callable = d->exportName;
        return Fill(error, e);
    }
    if (!IsJavaScriptIdentifier(S(d->kind))) {
        RegistryError e;
        e.kind = RegistryErrorKind::InvalidStateKind;
        e.component = d->kind;
        return Fill(error, e);
    }
    if (HasExport(d->exportName)) {
        RegistryError e;
        e.kind = RegistryErrorKind::DuplicateExport;
        e.callable = d->exportName;
        return Fill(error, e);
    }
    for (int i = 0; i < len(stateKinds); i++) {
        if (strcmp(stateKinds[i], d->kind) == 0) {
            RegistryError e;
            e.kind = RegistryErrorKind::DuplicateStateKind;
            e.component = d->kind;
            return Fill(error, e);
        }
    }
    for (int i = 0; i < d->methods.count; i++) {
        bool repeated = false;
        for (int j = 0; j < i; j++) {
            if (strcmp(d->methods[j].name, d->methods[i].name) == 0)
                repeated = true;
        }
        if (!IsJavaScriptIdentifier(S(d->methods[i].name)) || repeated) {
            RegistryError e;
            e.kind = RegistryErrorKind::InvalidMethod;
            e.component = d->kind;
            e.callable = d->methods[i].name;
            return Fill(error, e);
        }
    }
    if (!ValidateArguments(d->kind, d->exportName, d->arguments, error))
        return false;
    VecAppend(exports, d->exportName);
    VecAppend(stateKinds, d->kind);
    VecAppend(states, d);
    return true;
}

void ComponentRegistry::Freeze(FrozenComponentRegistry* out) {
    VecReset(out->descriptors);
    VecReset(out->states);
    out->moduleSpecifier = moduleSpecifier;
    out->initializer = initializer;
    out->windowOpener = windowOpener;
    for (int i = 0; i < len(descriptors); i++)
        VecAppend(out->descriptors, descriptors[i]);
    for (int i = 0; i < len(states); i++) VecAppend(out->states, states[i]);
    VecReset(descriptors);
    VecReset(states);
    VecReset(exports);
    VecReset(stateKinds);
    moduleSpecifier = nullptr;
    initializer = nullptr;
    windowOpener = nullptr;
}

FrozenComponentRegistry::~FrozenComponentRegistry() {
    VecReset(descriptors);
    VecReset(states);
}

const ComponentDescriptor* FrozenComponentRegistry::Descriptor(
    uint32_t id) const {
    return (int)id < len(descriptors) ? descriptors[(int)id] : nullptr;
}

const StateDescriptor* FrozenComponentRegistry::State(int at) const {
    return at >= 0 && at < len(states) ? states[at] : nullptr;
}

const StateDescriptor* FrozenComponentRegistry::StateOfKind(Str kind) const {
    for (int i = 0; i < len(states); i++) {
        if (StrEq(kind, states[i]->kind)) return states[i];
    }
    return nullptr;
}

const ComponentDescriptor* FrozenComponentRegistry::Find(Str name,
                                                         uint32_t* id) const {
    for (int i = 0; i < len(descriptors); i++) {
        if (StrEq(name, descriptors[i]->name)) {
            if (id) *id = (uint32_t)i;
            return descriptors[i];
        }
    }
    return nullptr;
}

const MethodDescriptor* FrozenComponentRegistry::Method(uint32_t component,
                                                        Str name) const {
    const ComponentDescriptor* d = Descriptor(component);
    if (!d) return nullptr;
    for (const MethodDescriptor& method : d->methods) {
        if (StrEq(name, method.name)) return &method;
    }
    return nullptr;
}

static void AppendQuoted(StrBuilder* out, Str value) {
    out->AppendChar('"');
    for (int i = 0; i < len(value); i++) {
        char c = value.s[i];
        if (c == '"' || c == '\\') out->AppendChar('\\');
        out->AppendChar(c);
    }
    out->AppendChar('"');
}

Str FrozenComponentRegistry::JavaScriptModuleSource(Str stateProof) const {
    StrBuilder source;
    source
        .Append(StrL("const __stateHandles = new WeakMap();\n"
                     "const __stateProof = "));
    AppendQuoted(&source, stateProof);
    source.Append(StrL(
        ";\nfunction __unwrapState(value) {\n"
        "if (__stateHandles.has(value)) return { __componentStateHandle: "
        "__stateHandles.get(value), __componentStateProof: __stateProof };\n"
        "if (Array.isArray(value)) return value.map(__unwrapState);\n"
        "return value;\n}\n"));
    for (int i = 0; i < len(states); i++) {
        const StateDescriptor* state = states[i];
        source.Append(StrL("function "));
        source.Append(Str(state->exportName));
        source
            .Append(StrL("(...args) { const handle = "
                         "globalThis.__gpui_components["));
        AppendQuoted(&source, Str(state->exportName));
        source.Append(StrL("](args); const value = Object.freeze({"));
        for (const StateMethodDescriptor& method : state->methods) {
            AppendQuoted(&source, Str(method.name));
            source.Append(StrL(": (...args) => globalThis.__gpui_components["));
            AppendQuoted(&source, fmt("%s.%s", Str(state->exportName),
                                      Str(method.name)));
            source.Append(StrL("](__stateProof, handle, args),"));
        }
        source
            .Append(StrL("}); __stateHandles.set(value, handle); return "
                         "value; }\nexport { "));
        source.Append(Str(state->exportName));
        source.Append(StrL(" };\n"));
    }
    for (int i = 0; i < len(descriptors); i++) {
        for (const ConstructorDescriptor& constructor : descriptors[i]
                                                            ->constructors) {
            source.Append(StrL("function "));
            source.Append(Str(constructor.exportName));
            source
                .Append(StrL("(...args) { return "
                             "globalThis.__gpui.__element(globalThis.__gpui_"
                             "components["));
            AppendQuoted(&source, Str(constructor.exportName));
            source.Append(StrL("](args.map(__unwrapState))); }\nexport { "));
            source.Append(Str(constructor.exportName));
            source.Append(StrL(" };\n"));
        }
    }
    return source.TakeStr();
}

// ─── Declarations: typings.rs declarations_with_components ─────────────────

static void AppendArgumentType(StrBuilder* out, const ArgumentSchema& schema) {
    switch (schema.kind) {
        case SchemaKind::String:
            out->Append(StrL("string"));
            break;
        case SchemaKind::Number:
            out->Append(StrL("number"));
            break;
        case SchemaKind::Boolean:
            out->Append(StrL("boolean"));
            break;
        case SchemaKind::Element:
            out->Append(StrL("Element"));
            break;
        case SchemaKind::Entity:
        case SchemaKind::Callback:
            out->Append(S(schema.text));
            break;
        case SchemaKind::Enum:
            for (int i = 0; i < schema.values.count; i++) {
                if (i) out->Append(StrL(" | "));
                AppendQuoted(out, Str(schema.values[i]));
            }
            break;
        case SchemaKind::Array:
            out->Append(StrL("Array<"));
            if (schema.item) AppendArgumentType(out, *schema.item);
            out->AppendChar('>');
            break;
        case SchemaKind::Optional:
            if (schema.item) AppendArgumentType(out, *schema.item);
            break;
    }
}

static void AppendArguments(StrBuilder* out,
                            Slice<ArgumentDescriptor> arguments) {
    for (int i = 0; i < arguments.count; i++) {
        if (i) out->Append(StrL(", "));
        out->Append(S(arguments[i].name));
        if (arguments[i].schema.kind == SchemaKind::Optional)
            out->AppendChar('?');
        out->Append(StrL(": "));
        AppendArgumentType(out, arguments[i].schema);
    }
}

static void AppendJsdoc(StrBuilder* out, const char* documentation,
                        const char* deprecated, const char* indent) {
    if (!documentation && !deprecated) return;
    out->Append(S(indent));
    out->Append(StrL("/**\n"));
    if (documentation) {
        // Rust's `str::lines`: split on \n, drop a trailing \r, and no empty
        // line after a final newline.
        const char* at = documentation;
        while (*at) {
            const char* end = strchr(at, '\n');
            int n = end ? (int)(end - at) : (int)strlen(at);
            int shown = n > 0 && at[n - 1] == '\r' ? n - 1 : n;
            out->Append(S(indent));
            out->Append(StrL(" * "));
            out->Append(Str(at, shown));
            out->AppendChar('\n');
            if (!end) break;
            at = end + 1;
        }
    }
    if (deprecated) {
        out->Append(S(indent));
        out->Append(StrL(" * @deprecated "));
        out->Append(S(deprecated));
        out->AppendChar('\n');
    }
    out->Append(S(indent));
    out->Append(StrL(" */\n"));
}

void AppendComponentElementUnion(StrBuilder* out,
                                 const FrozenComponentRegistry* components) {
    for (int i = 0; components && i < components->DescriptorCount(); i++) {
        out->Append(StrL(" | import(\"gpui-component\")."));
        out->Append(S(components->Descriptor((uint32_t)i)->name));
        out->Append(StrL("Element"));
    }
}

void AppendComponentDeclarations(StrBuilder* out,
                                 const FrozenComponentRegistry* components,
                                 Str inlineTokenTypes) {
    out->Append(StrL("declare module \"gpui-component\" {\n"));
    out
        ->Append(StrL("  import { ClickEvent, Context, Element, NativeElement "
                      "} from \"gpui-kit\";\n"));
    out->Append(inlineTokenTypes);
    for (int i = 0; components && i < components->StateCount(); i++) {
        const StateDescriptor* state = components->State(i);
        AppendJsdoc(out, state->documentation, nullptr, "  ");
        out->Append(StrL("  export interface "));
        out->Append(S(state->kind));
        out->Append(StrL(" { readonly __gpuiComponentState: unique symbol"));
        for (const StateMethodDescriptor& method : state->methods) {
            out->Append(StrL(";\n    "));
            out->Append(S(method.name));
            out->Append(S(method.signature));
        }
        out->Append(StrL(" }\n"));
        AppendJsdoc(out, state->documentation, nullptr, "  ");
        out->Append(StrL("  export function "));
        out->Append(S(state->exportName));
        out->AppendChar('(');
        AppendArguments(out, state->arguments);
        out->Append(StrL("): "));
        out->Append(S(state->kind));
        out->Append(StrL(";\n"));
    }
    for (int i = 0; components && i < components->DescriptorCount(); i++) {
        const ComponentDescriptor* d = components->Descriptor((uint32_t)i);
        AppendJsdoc(out, d->documentation, nullptr, "  ");
        out->Append(StrL("  export type "));
        out->Append(S(d->name));
        out->Append(StrL("Element = "));
        // Two reasons a name leaves `Element`: a declared method is declared
        // again below with the descriptor's own signature, and an undeclared
        // common behavior is refused at run time for a registered component.
        const char* withheld[5] = {};
        int withheldCount = 0;
        const char* candidates[5] = {
            kRegisteredCommonBehaviors[0], kRegisteredCommonBehaviors[1],
            kRegisteredCommonBehaviors[2], "role", "transition"};
        for (const char* behavior : candidates) {
            bool declared = false;
            for (const MethodDescriptor& method : d->methods) {
                if (strcmp(method.name, behavior) == 0) declared = true;
            }
            if (!declared) withheld[withheldCount++] = behavior;
        }
        if (d->methods.count == 0 && withheldCount == 0) {
            out->Append(StrL("NativeElement & {\n"));
        } else {
            out->Append(StrL("Omit<NativeElement, "));
            bool first = true;
            for (const MethodDescriptor& method : d->methods) {
                if (!first) out->Append(StrL(" | "));
                first = false;
                AppendQuoted(out, Str(method.name));
            }
            for (int w = 0; w < withheldCount; w++) {
                if (!first) out->Append(StrL(" | "));
                first = false;
                AppendQuoted(out, Str(withheld[w]));
            }
            out->Append(StrL("> & {\n"));
        }
        for (const MethodDescriptor& method : d->methods) {
            AppendJsdoc(out, method.documentation, nullptr, "    ");
            out->Append(StrL("    "));
            out->Append(S(method.name));
            out->AppendChar('(');
            AppendArguments(out, method.arguments);
            out->Append(StrL("): "));
            out->Append(S(d->name));
            out->Append(StrL("Element;\n"));
        }
        for (int w = 0; w < withheldCount; w++) {
            out
                ->Append(StrL("    /**\n     * Not available on this "
                              "component: `"));
            out->Append(S(d->name));
            out->Append(StrL("` does not declare `"));
            out->Append(S(withheld[w]));
            out->Append(StrL("`, and the runtime refuses it.\n     */\n    "));
            out->Append(S(withheld[w]));
            out->Append(StrL("(unavailable: never): never;\n"));
        }
        out->Append(StrL("  }\n"));
        for (const ConstructorDescriptor& constructor : d->constructors) {
            AppendJsdoc(out, d->documentation, constructor.deprecationMessage,
                        "  ");
            out->Append(StrL("  export const "));
            out->Append(S(constructor.exportName));
            out->Append(StrL(": { new("));
            AppendArguments(out, constructor.arguments);
            out->Append(StrL("): "));
            out->Append(S(d->name));
            out->Append(StrL("Element };\n"));
        }
    }
    out->Append(StrL("}\n\n"));
}

// ─── Values ────────────────────────────────────────────────────────────────

const ComponentArgument* ComponentArgument::Some() const {
    if (kind != ComponentArgumentKind::Optional) return this;
    return count > 0 ? items : nullptr;
}

ComponentDataValue ComponentDataValue::Null() {
    return {};
}
ComponentDataValue ComponentDataValue::Boolean(bool value) {
    ComponentDataValue out;
    out.kind = DataKind::Boolean;
    out.boolean = value;
    return out;
}
ComponentDataValue ComponentDataValue::Number(double value) {
    ComponentDataValue out;
    out.kind = DataKind::Number;
    out.number = value;
    return out;
}
ComponentDataValue ComponentDataValue::String(Str value) {
    ComponentDataValue out;
    out.kind = DataKind::String;
    out.string = value;
    return out;
}
ComponentDataValue ComponentDataValue::Array(const ComponentDataValue* items,
                                             int count) {
    ComponentDataValue out;
    out.kind = DataKind::Array;
    out.items = items;
    out.count = count;
    return out;
}

const ComponentDataValue* ComponentDataValue::Get(Str key) const {
    if (kind != DataKind::Object) return nullptr;
    for (int i = 0; i < count; i++) {
        if (StrEq(keys[i], key)) return &items[i];
    }
    return nullptr;
}

bool PayloadBuild::Fail(Str message) {
    error = StrDup(a, message);
    return false;
}

bool StateBuild::Fail(Str message) {
    error = StrDup(a, message);
    return false;
}

bool StateCall::Fail(Str message) {
    error = StrDup(a, message);
    return false;
}

// ─── Retained component state ──────────────────────────────────────────────

ComponentStateStore::~ComponentStateStore() {
    Clear();
}

bool ComponentStateStore::Insert(const char* kind, void* owner, void* value,
                                 void (*destroy)(void*), uint64_t* handle,
                                 Str* error, Arena* a) {
    if (len(entries) >= kMaxRetainedComponentStates) {
        if (destroy) destroy(value);
        if (error)
            *error = StrDup(a, fmt("retained component state limit (%d) "
                                   "reached",
                                   kMaxRetainedComponentStates));
        return false;
    }
    if (nextHandle == UINT64_MAX) {
        if (destroy) destroy(value);
        if (error)
            *error = StrDup(a, StrL("retained component state handle space "
                                    "exhausted"));
        return false;
    }
    Entry entry;
    entry.handle = nextHandle++;
    entry.kind = kind;
    entry.owner = owner;
    entry.value = value;
    entry.destroy = destroy;
    VecAppend(entries, entry);
    *handle = entry.handle;
    return true;
}

const char* ComponentStateStore::Kind(uint64_t handle) const {
    for (int i = 0; i < len(entries); i++) {
        if (entries[i].handle == handle) return entries[i].kind;
    }
    return nullptr;
}

void* ComponentStateStore::Get(uint64_t handle, const char* kind, Str* error,
                               Arena* a) const {
    for (int i = 0; i < len(entries); i++) {
        const Entry& entry = entries[i];
        if (entry.handle != handle) continue;
        if (strcmp(entry.kind, kind) != 0) {
            if (error)
                *error = StrDup(a, fmt("retained state kind mismatch: "
                                       "expected `%s`, found `%s`",
                                       Str(kind), Str(entry.kind)));
            return nullptr;
        }
        return entry.value;
    }
    if (error)
        *error = StrDup(a, StrL("retained state handle has been released"));
    return nullptr;
}

void ComponentStateStore::ReleaseApplication(void* application) {
    if (!application) return;
    int out = 0;
    for (int i = 0; i < len(entries); i++) {
        Entry entry = entries[i];
        if (entry.owner == application) {
            if (entry.destroy) entry.destroy(entry.value);
        } else {
            entries[out++] = entry;
        }
    }
    entries.len = out;
}

void ComponentStateStore::Clear() {
    for (int i = 0; i < len(entries); i++) {
        if (entries[i].destroy) entries[i].destroy(entries[i].value);
    }
    VecReset(entries);
}

// ─── MaterializeRequest ────────────────────────────────────────────────────

enum : uint8_t {
    kLaneUnclaimed = 0,
    kLaneOrdinary = 1,
    kLaneTyped = 2,
};

int MaterializeRequest::MethodCount() const {
    int count = 0;
    for (const SpecOp& op : node->ops) {
        if (op.kind == SpecOpKind::RegisteredMethod) count++;
    }
    return count;
}

RecordedComponentMethod MaterializeRequest::Method(int at) const {
    int index = 0;
    for (const SpecOp& op : node->ops) {
        if (op.kind != SpecOpKind::RegisteredMethod) continue;
        if (index++ == at) return {op.name, op.payload};
    }
    return {};
}

bool MaterializeRequest::HasStyle() const {
    for (const SpecOp& op : node->ops) {
        if (op.kind == SpecOpKind::NullaryStyle ||
            op.kind == SpecOpKind::ParamStyle)
            return true;
    }
    return false;
}

El* MaterializeRequest::ApplyStyle(El* target) {
    if (styleTaken || !target) return target;
    styleTaken = true;
    ShellApplyNodeStyle(cx, specs, id, target, error);
    return target;
}

// What TakeStyle's refiner replays: the node's own style, from the
// description the frame is materializing.
struct DeferredNodeStyle {
    Ctx* cx = nullptr;
    const SpecArena* specs = nullptr;
    SpecId id = 0;
    ShellError* error = nullptr;
};

static void ApplyDeferredNodeStyle(El* target, void* user) {
    DeferredNodeStyle* style = (DeferredNodeStyle*)user;
    ShellApplyNodeStyle(style->cx, style->specs, style->id, target,
                        style->error);
}

ElRefiner MaterializeRequest::TakeStyle() {
    if (styleTaken) return {};
    styleTaken = true;
    DeferredNodeStyle* style = ArenaNew<DeferredNodeStyle>(cx->a);
    style->cx = cx;
    style->specs = specs;
    style->id = id;
    style->error = error;
    ElRefiner refiner;
    refiner.apply = &ApplyDeferredNodeStyle;
    refiner.user = style;
    return refiner;
}

int MaterializeRequest::ChildrenLen() const {
    int remaining = node->children.len - childrenTaken;
    for (int i = 0; i < typedCount; i++) {
        if (!typed[i].consumed) remaining++;
    }
    return remaining;
}

bool MaterializeRequest::TakeChildren(El*** out, int* count) {
    *out = nullptr;
    *count = 0;
    if (lane == kLaneTyped) {
        Fail(
            fmt("%s already issued typed children; take_children is "
                "exclusive with take_typed_children",
                Str(ComponentName())));
        return false;
    }
    lane = kLaneOrdinary;
    int total = node->children.len - childrenTaken;
    if (total <= 0) return true;
    El** children = (El**)Alloc(cx->a, (int)sizeof(El*) * total);
    int at = 0;
    int index = 0;
    for (SpecId child : node->children) {
        if (index++ < childrenTaken) continue;
        children[at++] = ShellMaterializeSpec(cx, runtime, specs, child, error);
        childrenTaken++;
    }
    *out = children;
    *count = at;
    return true;
}

bool MaterializeRequest::AppendChildren(El* parent) {
    El** children = nullptr;
    int count = 0;
    if (!TakeChildren(&children, &count)) return false;
    for (int i = 0; i < count; i++) {
        if (children[i]) parent->Child(children[i]);
    }
    return true;
}

bool MaterializeRequest::TakeTypedChildren(ComponentChild** out, int* count) {
    *out = nullptr;
    *count = 0;
    if (lane == kLaneOrdinary) {
        Fail(
            fmt("%s already materialized ordinary children; "
                "take_typed_children is exclusive with take_children",
                Str(ComponentName())));
        return false;
    }
    lane = kLaneTyped;
    int total = node->children.len;
    if (total == 0 || typed) {
        *out = typed;
        *count = typedCount;
        return true;
    }
    typed = (ComponentChild*)Alloc(cx->a, (int)sizeof(ComponentChild) * total);
    typedCount = 0;
    for (SpecId child : node->children) {
        ComponentChild& entry = typed[typedCount++];
        entry = {};
        entry.id = child;
        const SpecNode* childNode = specs->Node(child);
        if (childNode && childNode->component
                                 .kind == ComponentKind::Registered) {
            entry.componentName = childNode->component.text.s;
            entry.payload = &childNode->component.payload;
        }
    }
    childrenTaken = node->children.len;
    *out = typed;
    *count = typedCount;
    return true;
}

El* MaterializeRequest::MaterializeChild(ComponentChild* child) {
    if (!child || child < typed || child >= typed + typedCount) {
        return Fail(
            StrL("component child belongs to a different runtime or "
                 "materialization request"));
    }
    if (child->consumed)
        return Fail(
            StrL("component child was already "
                 "consumed"));
    El* element = ShellMaterializeSpec(cx, runtime, specs, child->id, error);
    child->consumed = true;
    return element;
}

static int OpCount(const SpecNode* node) {
    int count = 0;
    for (const SpecOp& op : node->ops) {
        (void)op;
        count++;
    }
    return count;
}

// The index of the next untaken Slot op named `name`, or -1.
static int NextSlot(MaterializeRequest* request, const char* name,
                    SpecId* target) {
    if (!request->slotTaken) {
        int count = OpCount(request->node);
        request->slotTaken =
            (bool*)Alloc(request->cx->a, (int)sizeof(bool) * (count + 1));
        memset(request->slotTaken, 0, sizeof(bool) * (size_t)(count + 1));
    }
    int index = 0;
    for (const SpecOp& op : request->node->ops) {
        int at = index++;
        if (op.kind != SpecOpKind::Slot || request->slotTaken[at]) continue;
        if (!StrEq(op.name, name)) continue;
        *target = op.node;
        return at;
    }
    return -1;
}

El* MaterializeRequest::TakeSlot(const char* name) {
    SpecId target = 0;
    int at = NextSlot(this, name, &target);
    if (at < 0) return nullptr;
    slotTaken[at] = true;
    return ShellMaterializeSpec(cx, runtime, specs, target, error);
}

int MaterializeRequest::TakeSlots(const char* name, El*** out) {
    *out = nullptr;
    int count = 0;
    for (const SpecOp& op : node->ops) {
        if (op.kind == SpecOpKind::Slot && StrEq(op.name, name)) count++;
    }
    if (count == 0) return 0;
    El** slots = (El**)Alloc(cx->a, (int)sizeof(El*) * count);
    int taken = 0;
    while (El* slot = TakeSlot(name)) slots[taken++] = slot;
    *out = slots;
    return taken;
}

ComponentElementFactory MaterializeRequest::TakeSlotFactory(const char* name) {
    SpecId target = 0;
    int at = NextSlot(this, name, &target);
    if (at < 0) return {};
    slotTaken[at] = true;
    ComponentElementFactory factory;
    factory.id = target;
    factory.set = true;
    return factory;
}

El* MaterializeRequest::BuildFactory(ComponentElementFactory factory) {
    if (!factory.IsSet()) return nullptr;
    return ShellMaterializeSpec(cx, runtime, specs, factory.id, error);
}

El* MaterializeRequest::Finish(El* element) {
    ApplyStyle(element);
    if (lane != kLaneTyped) AppendChildren(element);
    return element;
}

El* MaterializeRequest::ResolveElement(const ComponentArgument& argument) {
    const ComponentArgument* value = argument.Some();
    if (!value || value->kind != ComponentArgumentKind::Element)
        return Fail(StrL("component argument is not an Element"));
    return ShellMaterializeSpec(cx, runtime, specs, value->element, error);
}

ComponentCallback MaterializeRequest::ResolveCallback(
    const ComponentArgument& argument) const {
    const ComponentArgument* value = argument.Some();
    if (!value || value->kind != ComponentArgumentKind::Callback) return {};
    return {value->callback};
}

El* MaterializeRequest::Fail(Str message) {
    if (!failure.s) failure = StrDup(cx->a, message);
    return nullptr;
}

// ─── Native effects ───────────────────────────────────────────────────────

static int FindKey(const Vec<Str>& keys, Str key) {
    for (int i = 0; i < len(keys); i++) {
        if (StrEq(keys[i], key)) return i;
    }
    return -1;
}

ComponentEventEffects::~ComponentEventEffects() {
    VecReset(completed);
    VecReset(inProgress);
}

bool ComponentEventEffects::RunOnce(Str key, ComponentEffectBody body,
                                    void* user, bool* executed, Str* error) {
    if (executed) *executed = false;
    if (FindKey(completed, key) >= 0) return true;
    if (FindKey(inProgress, key) >= 0) {
        if (error)
            *error = StrDup(a, fmt("component window effect `%s` is already "
                                   "running",
                                   key));
        return false;
    }
    Str owned = StrDup(a, key);
    VecAppend(inProgress, owned);
    bool ok = body(user, window, app, error, a);
    int at = FindKey(inProgress, owned);
    if (at >= 0) VecRemoveAt(inProgress, at);
    if (!ok) return false;
    VecAppend(completed, owned);
    if (executed) *executed = true;
    return true;
}

ComponentAppEffectQueue::~ComponentAppEffectQueue() {
    for (PendingAppEffect& p : pending) {
        StrFree(p.key);
        StrFree(p.revision);
    }
    for (InstalledAppEffect& e : installed) {
        StrFree(e.key);
        StrFree(e.revision);
        if (e.cleanup.drop) e.cleanup.drop(e.cleanup.user);
    }
    VecReset(pending);
    VecReset(installed);
}

const Str* ComponentAppEffectQueue::Pending(Str key) const {
    for (int i = 0; i < len(pending); i++) {
        if (StrEq(pending[i].key, key)) return &pending[i].revision;
    }
    return nullptr;
}

InstalledAppEffect* ComponentAppEffectQueue::Installed(Str key) {
    for (int i = 0; i < len(installed); i++) {
        if (StrEq(installed[i].key, key)) return &installed[i];
    }
    return nullptr;
}

void ComponentAppEffectQueue::RemovePending(Str key) {
    for (int i = 0; i < len(pending); i++) {
        if (StrEq(pending[i].key, key)) {
            StrFree(pending[i].key);
            StrFree(pending[i].revision);
            VecRemoveAt(pending, i);
            return;
        }
    }
}

bool QueueComponentAppEffect(ComponentAppEffectQueue* queue, Str key,
                             Str revision) {
    const Str* pending = queue->Pending(key);
    if (pending && StrEq(*pending, revision)) return false;
    const InstalledAppEffect* installed = queue->Installed(key);
    if (installed && StrEq(installed->revision, revision)) {
        queue->RemovePending(key);
        return false;
    }
    queue->RemovePending(key);
    VecAppend(queue->pending, PendingAppEffect{StrDup(key), StrDup(revision)});
    return true;
}

} // namespace gpui::shell
