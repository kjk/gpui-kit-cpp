#include "shell/host_modules.h"

#include "shell/policy.h"
#include "shell/scope.h"

#include <math.h>
#include <string.h>

namespace gpui {

static bool HostStrLess(Str a, Str b) {
    return StrCmp(a, b) < 0;
}

static HostValue* CopyValue(const HostValue& value) {
    HostValue* copy = new HostValue();
    if (!copy->CopyFrom(value)) {
        copy->Free();
        delete copy;
        return nullptr;
    }
    return copy;
}

void HostValue::Free() {
    StrFree(string);
    string = {};
    for (int i = 0; i < array.len; i++) {
        if (array[i]) {
            array[i]->Free();
            delete array[i];
        }
    }
    VecReset(array);
    for (int i = 0; i < object.len; i++) {
        StrFree(object[i].name);
        if (object[i].value) {
            object[i].value->Free();
            delete object[i].value;
        }
    }
    VecReset(object);
    kind = HostValueKind::Null;
    boolean = false;
    number = 0;
}

bool HostValue::CopyFrom(const HostValue& other) {
    if (this == &other) return true;
    Free();
    kind = other.kind;
    boolean = other.boolean;
    number = other.number;
    if (other.string.s) {
        string = StrDup(other.string);
        if (!string.s) {
            Free();
            return false;
        }
    }
    for (int i = 0; i < other.array.len; i++) {
        HostValue* value =
            other.array[i] ? CopyValue(*other.array[i]) : nullptr;
        if (!value || !VecAppend(array, value)) {
            if (value) {
                value->Free();
                delete value;
            }
            Free();
            return false;
        }
    }
    for (int i = 0; i < other.object.len; i++) {
        HostField field;
        field.name = StrDup(other.object[i].name);
        field.value =
            other.object[i].value ? CopyValue(*other.object[i].value) : nullptr;
        if ((!field.name.s && other.object[i].name.len > 0) || !field.value ||
            !VecAppend(object, field)) {
            StrFree(field.name);
            if (field.value) {
                field.value->Free();
                delete field.value;
            }
            Free();
            return false;
        }
    }
    return true;
}

void HostValue::SetNull() {
    Free();
}

void HostValue::SetBool(bool value) {
    Free();
    kind = HostValueKind::Bool;
    boolean = value;
}

void HostValue::SetNumber(double value) {
    Free();
    kind = HostValueKind::Number;
    number = value;
}

bool HostValue::SetString(Str value) {
    Free();
    kind = HostValueKind::String;
    string = StrDup(value);
    if (!string.s && value.len > 0) {
        kind = HostValueKind::Null;
        return false;
    }
    return true;
}

bool HostValue::Append(const HostValue& value) {
    if (kind != HostValueKind::Array) {
        Free();
        kind = HostValueKind::Array;
    }
    HostValue* copy = CopyValue(value);
    if (!copy || !VecAppend(array, copy)) {
        if (copy) {
            copy->Free();
            delete copy;
        }
        return false;
    }
    return true;
}

bool HostValue::SetField(Str fieldName, const HostValue& value) {
    if (kind != HostValueKind::Object) {
        Free();
        kind = HostValueKind::Object;
    }
    for (int i = 0; i < object.len; i++) {
        if (!StrEq(object[i].name, fieldName)) continue;
        HostValue* copy = CopyValue(value);
        if (!copy) return false;
        object[i].value->Free();
        delete object[i].value;
        object[i].value = copy;
        return true;
    }
    HostField field;
    field.name = StrDup(fieldName);
    field.value = CopyValue(value);
    if ((!field.name.s && fieldName.len > 0) || !field.value ||
        !VecAppend(object, field)) {
        StrFree(field.name);
        if (field.value) {
            field.value->Free();
            delete field.value;
        }
        return false;
    }
    return true;
}

const HostValue* HostValue::Get(Str fieldName) const {
    if (kind != HostValueKind::Object) return nullptr;
    for (int i = 0; i < object.len; i++) {
        if (StrEq(object[i].name, fieldName)) return object[i].value;
    }
    return nullptr;
}

const char* HostValue::Describe() const {
    switch (kind) {
        case HostValueKind::Null:
            return "null";
        case HostValueKind::Bool:
            return "a boolean";
        case HostValueKind::Number:
            return "a number";
        case HostValueKind::String:
            return "a string";
        case HostValueKind::Array:
            return "an array";
        case HostValueKind::Object:
            return "an object";
    }
    return "a value";
}

void HostError::Set(Str value) {
    StrFree(message);
    message = StrDup(value);
}

void HostError::Clear() {
    StrFree(message);
    message = {};
}

void HostArguments::Free() {
    for (int i = 0; i < values.len; i++) {
        if (values[i]) {
            values[i]->Free();
            delete values[i];
        }
    }
    VecReset(values);
}

const HostValue* HostArguments::Get(int index) const {
    return index >= 0 && index < values.len ? values[index] : nullptr;
}

bool HostArguments::Value(int index, const HostValue** value,
                          HostError* error) const {
    const HostValue* found = Get(index);
    if (found) {
        if (value) *value = found;
        return true;
    }
    if (error)
        error->Set(fmt("argument %d is missing; %d were passed", index + 1,
                       values.len));
    return false;
}

static bool Mistyped(int index, const char* expected, const HostValue* got,
                     HostError* error) {
    if (error)
        error->Set(fmt("argument %d must be %s, got %s", index + 1,
                       Str(expected), Str(got ? got->Describe() : "nothing")));
    return false;
}

bool HostArguments::String(int index, Str* value, HostError* error) const {
    const HostValue* found = nullptr;
    if (!Value(index, &found, error)) return false;
    if (found->kind != HostValueKind::String)
        return Mistyped(index, "a string", found, error);
    if (value) *value = found->string;
    return true;
}

bool HostArguments::Number(int index, double* value, HostError* error) const {
    const HostValue* found = nullptr;
    if (!Value(index, &found, error)) return false;
    if (found->kind != HostValueKind::Number)
        return Mistyped(index, "a number", found, error);
    if (value) *value = found->number;
    return true;
}

bool HostArguments::Integer(int index, int64_t* value, HostError* error) const {
    double number = 0;
    if (!Number(index, &number, error)) return false;
    if (!isfinite(number) || floor(number) != number ||
        number < (double)INT64_MIN || number > (double)INT64_MAX) {
        if (error)
            error->Set(fmt("argument %d must be a whole number", index + 1));
        return false;
    }
    if (value) *value = (int64_t)number;
    return true;
}

bool HostArguments::Boolean(int index, bool* value, HostError* error) const {
    const HostValue* found = nullptr;
    if (!Value(index, &found, error)) return false;
    if (found->kind != HostValueKind::Bool)
        return Mistyped(index, "a boolean", found, error);
    if (value) *value = found->boolean;
    return true;
}

struct HostModule::FunctionEntry {
    Str name;
    bool async = false;
    Func1<HostCall*> body;
    Func1<HostAsyncRequest*> begin;
    Func0 release;
};

HostModule::HostModule(Str value) : name(StrDup(value)) {}

HostModule::~HostModule() {
    StrFree(name);
    StrFree(declarations);
    for (int i = 0; i < functions.len; i++) {
        StrFree(functions[i]->name);
        functions[i]->release.Call();
        delete functions[i];
    }
    VecReset(functions);
}

HostModule* HostModule::New(Str name) {
    return new HostModule(name);
}

HostModule* HostModule::Retain() {
    refs++;
    return this;
}

void HostModule::Release() {
    if (--refs == 0) delete this;
}

HostModule::FunctionEntry* HostModule::Find(Str function) const {
    for (int i = 0; i < functions.len; i++) {
        if (StrEq(functions[i]->name, function)) return functions[i];
    }
    return nullptr;
}

HostModule* HostModule::SetFunction(Str function, bool async,
                                    Func1<HostCall*> body,
                                    Func1<HostAsyncRequest*> begin,
                                    Func0 release) {
    FunctionEntry* entry = Find(function);
    if (!entry) {
        entry = new FunctionEntry();
        entry->name = StrDup(function);
        if (!VecAppend(functions, entry)) {
            StrFree(entry->name);
            delete entry;
            release.Call();
            return this;
        }
    } else {
        entry->release.Call();
    }
    entry->async = async;
    entry->body = body;
    entry->begin = begin;
    entry->release = release;
    for (int i = functions.len - 1; i > 0; i--) {
        if (HostStrLess(functions[i - 1]->name, functions[i]->name)) break;
        FunctionEntry* swap = functions[i - 1];
        functions[i - 1] = functions[i];
        functions[i] = swap;
    }
    return this;
}

HostModule* HostModule::Function(Str function, Func1<HostCall*> body,
                                 Func0 release) {
    return SetFunction(function, false, body, {}, release);
}

HostModule* HostModule::AsyncFunction(Str function, Func1<HostCall*> work,
                                      Func0 release) {
    return SetFunction(function, true, work, {}, release);
}

HostModule* HostModule::AsyncFunction(Str function,
                                      Func1<HostAsyncRequest*> begin,
                                      Func0 release) {
    return SetFunction(function, true, {}, begin, release);
}

HostModule* HostModule::Declarations(Str value) {
    StrFree(declarations);
    declarations = StrDup(value);
    return this;
}

Str HostModule::FunctionName(int index) const {
    return index >= 0 && index < functions.len ? functions[index]->name : Str{};
}

bool HostModule::Has(Str function) const {
    return Find(function) != nullptr;
}

bool HostModule::IsAsync(Str function) const {
    FunctionEntry* entry = Find(function);
    return entry && entry->async;
}

static const char* const kReserved[] = {
    "gpui-kit", "gpui",   "gpui-base",   "gpui-shell", "gpui-fps", "buffer",
    "console",  "crypto", "fs/promises", "net",        "os",       "path",
    "process",  "url",    "websocket",   "zlib",
};

bool HostIsReservedSpecifier(Str value) {
    for (int i = 0; i < (int)(sizeof(kReserved) / sizeof(kReserved[0])); i++) {
        if (StrEq(value, kReserved[i])) return true;
    }
    return false;
}

bool HostIsIdentifier(Str value) {
    if (!value.s || value.len == 0) return false;
    char first = value.s[0];
    if (!((first >= 'a' && first <= 'z') || (first >= 'A' && first <= 'Z') ||
          first == '_' || first == '$'))
        return false;
    for (int i = 1; i < value.len; i++) {
        char c = value.s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '$'))
            return false;
    }
    return true;
}

static void AppendNames(StrBuilder* out, const HostModule* module,
                        const Vec<Str>* names = nullptr) {
    int count = names ? names->len : module ? module->FunctionCount() : 0;
    if (count == 0) {
        out->Append(StrL("nothing"));
        return;
    }
    for (int i = 0; i < count; i++) {
        if (i) out->Append(StrL(", "));
        out->Append(names ? (*names)[i] : module->FunctionName(i));
    }
}

bool HostModule::Validate(HostError* error) const {
    if (error) error->Clear();
    if (!name.s || name.len == 0) {
        if (error) error->Set(StrL("HostModule name cannot be empty"));
        return false;
    }
    if (HostIsReservedSpecifier(name)) {
        if (error)
            error
                ->Set(fmt("`%s` is one of the runtime's own module names and "
                          "cannot be registered",
                          name));
        return false;
    }
    if (!declarations.s) return true;

    Vec<Str> declared;
    const char* at = declarations.s;
    const char* end = declarations.s + declarations.len;
    while (at < end) {
        const char* lineEnd = (const char*)memchr(at, '\n', (size_t)(end - at));
        if (!lineEnd) lineEnd = end;
        while (at < lineEnd && (*at == ' ' || *at == '\t')) at++;
        static const char* prefixes[] = {
            "export function ", "export declare function ", "export const "};
        const char* rest = nullptr;
        for (int i = 0; i < 3; i++) {
            Str prefix = Str(prefixes[i]);
            if (lineEnd - at >= prefix.len &&
                StrEq(Str(at, prefix.len), prefix)) {
                rest = at + prefix.len;
                break;
            }
        }
        if (rest) {
            const char* stop = rest;
            while (stop < lineEnd) {
                char c = *stop;
                if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') || c == '_' || c == '$'))
                    break;
                stop++;
            }
            VecAppend(declared, Str(rest, (int)(stop - rest)));
        }
        at = lineEnd < end ? lineEnd + 1 : end;
    }

    Vec<Str> missing;
    Vec<Str> extra;
    for (int i = 0; i < functions.len; i++) {
        bool found = false;
        for (int j = 0; j < declared.len; j++)
            if (StrEq(functions[i]->name, declared[j])) found = true;
        if (!found) VecAppend(missing, functions[i]->name);
    }
    for (int i = 0; i < declared.len; i++) {
        if (!Has(declared[i])) VecAppend(extra, declared[i]);
    }
    if (missing.len == 0 && extra.len == 0) return true;
    StrBuilder message;
    message
        .Append(fmt("HostModule `%s` declares a different set of functions "
                    "than it registers",
                    name));
    if (missing.len) {
        message.Append(StrL("; registered but not declared: "));
        AppendNames(&message, nullptr, &missing);
    }
    if (extra.len) {
        message.Append(StrL("; declared but not registered: "));
        AppendNames(&message, nullptr, &extra);
    }
    Str text = message.TakeStr();
    if (error) error->Set(text);
    StrFree(text);
    return false;
}

bool HostModule::Call(Str function, HostCall* call) const {
    FunctionEntry* entry = Find(function);
    if (!entry || entry->async || !entry->body.IsValid()) {
        if (call)
            call->error
                .Set(fmt("HostModule `%s` has no synchronous function `%s`",
                         name, function));
        return false;
    }
    entry->body.Call(call);
    return call && !call->error.IsSet();
}

bool HostModule::Begin(Str function, HostAsyncRequest* request) const {
    FunctionEntry* entry = Find(function);
    if (!entry || !entry->async) {
        if (request)
            request->error
                .Set(fmt("HostModule `%s` has no asynchronous function `%s`",
                         name, function));
        return false;
    }
    if (entry->begin.IsValid())
        entry->begin.Call(request);
    else
        request->work = entry->body;
    if (!request->work.IsValid() && !request->error.IsSet()) {
        request->error.Set(StrL("asynchronous host function returned no work"));
    }
    return !request->error.IsSet();
}

struct HostModules {
    uint32_t refs = 1;
    uint64_t generation = 0;
    Vec<HostModule*> modules;
};

static thread_local uint64_t gNextHostModulesGeneration = 1;

static uint64_t NextHostGeneration() {
    uint64_t value = gNextHostModulesGeneration++;
    if (value == 0) value = gNextHostModulesGeneration++;
    return value;
}

HostModules* HostModulesNew() {
    HostModules* modules = new HostModules();
    modules->generation = NextHostGeneration();
    return modules;
}

HostModules* HostModulesRetain(HostModules* modules) {
    if (modules) modules->refs++;
    return modules;
}

void HostModulesRelease(HostModules* modules) {
    if (!modules || --modules->refs != 0) return;
    for (int i = 0; i < modules->modules.len; i++)
        modules->modules[i]->Release();
    VecReset(modules->modules);
    delete modules;
}

HostModules* HostModulesClone(HostModules* source) {
    HostModules* copy = HostModulesNew();
    if (!source) return copy;
    for (int i = 0; i < source->modules.len; i++) {
        HostModule* module = source->modules[i]->Retain();
        if (!VecAppend(copy->modules, module)) {
            module->Release();
            HostModulesRelease(copy);
            return nullptr;
        }
    }
    return copy;
}

uint64_t HostModulesGeneration(const HostModules* modules) {
    return modules ? modules->generation : 0;
}

int HostModulesCount(const HostModules* modules) {
    return modules ? modules->modules.len : 0;
}

HostModule* HostModulesAt(const HostModules* modules, int index) {
    return modules && index >= 0 && index < modules->modules.len
               ? modules->modules[index]
               : nullptr;
}

HostModule* HostModulesGet(const HostModules* modules, Str name) {
    if (!modules) return nullptr;
    for (int i = 0; i < modules->modules.len; i++) {
        if (StrEq(modules->modules[i]->Name(), name))
            return modules->modules[i];
    }
    return nullptr;
}

bool HostModulesInsert(HostModules* modules, HostModule* module) {
    if (!modules || !module) return false;
    for (int i = 0; i < modules->modules.len; i++) {
        if (!StrEq(modules->modules[i]->Name(), module->Name())) continue;
        modules->modules[i]->Release();
        modules->modules[i] = module->Retain();
        modules->generation = NextHostGeneration();
        return true;
    }
    if (!VecAppend(modules->modules, module->Retain())) return false;
    for (int i = modules->modules.len - 1; i > 0; i--) {
        if (HostStrLess(modules->modules[i - 1]->Name(), modules->modules[i]
                                                             ->Name()))
            break;
        HostModule* swap = modules->modules[i - 1];
        modules->modules[i - 1] = modules->modules[i];
        modules->modules[i] = swap;
    }
    modules->generation = NextHostGeneration();
    return true;
}

bool ShellExportModule(HostModule* module, HostError* error) {
    if (!module || !module->Validate(error)) return false;
    Policy* policy = PolicyDefault();
    bool ok = PolicyAddHostModule(policy, module, error);
    PolicyRelease(policy);
    return ok;
}

void ShellClearExportedModules() {
    Policy* policy = PolicyDefault();
    PolicyClearHostModules(policy);
    PolicyRelease(policy);
}

static thread_local bool gInHostCall = false;

struct HostCallGuard {
    bool active = false;
    HostCallGuard() {
        if (!gInHostCall) {
            gInHostCall = true;
            active = true;
        }
    }
    ~HostCallGuard() {
        if (active) gInHostCall = false;
    }
};

static HostModules* CurrentModules() {
    Policy* policy = shell::ScopeCurrentPolicy();
    if (policy) return PolicyHostModules(policy);
    Policy* fallback = PolicyDefault();
    HostModules* modules = PolicyHostModules(fallback);
    PolicyRelease(fallback);
    return modules;
}

static void MissingModule(Str module, HostError* error,
                          const HostModules* modules) {
    if (!error) return;
    if (HostModulesCount(modules) == 0) {
        error->Set(
            fmt("HostModule `%s` is not available: this Host registered none",
                module));
        return;
    }
    StrBuilder out;
    out.Append(fmt("unknown HostModule `%s`; this Host registered: ", module));
    for (int i = 0; i < modules->modules.len; i++) {
        if (i) out.Append(StrL(", "));
        out.Append(modules->modules[i]->Name());
    }
    Str message = out.TakeStr();
    error->Set(message);
    StrFree(message);
}

bool HostDispatch(Str module, Str function, HostCall* call) {
    if (!call) return false;
    if (gInHostCall) {
        call->error
            .Set(fmt("`%s.%s` was reached from inside another host call: a "
                     "host function may not call back into the script engine",
                     module, function));
        return false;
    }
    HostModules* modules = CurrentModules();
    HostModule* found = HostModulesGet(modules, module);
    if (!found) MissingModule(module, &call->error, modules);
    HostCallGuard guard;
    bool ok = found && found->Call(function, call);
    HostModulesRelease(modules);
    return ok;
}

bool HostDispatchBegin(Str module, Str function, HostAsyncRequest* request) {
    if (!request) return false;
    if (gInHostCall) {
        request->error
            .Set(fmt("`%s.%s` was reached from inside another host call: a "
                     "host function may not call back into the script engine",
                     module, function));
        return false;
    }
    HostModules* modules = CurrentModules();
    HostModule* found = HostModulesGet(modules, module);
    if (!found) MissingModule(module, &request->error, modules);
    HostCallGuard guard;
    bool ok = found && found->Begin(function, request);
    if (ok)
        request->registry = modules;
    else
        HostModulesRelease(modules);
    return ok;
}

} // namespace gpui
