#ifndef GPUI_SHELL_HOST_MODULES_H_
#define GPUI_SHELL_HOST_MODULES_H_

#include "shell/error.h"

namespace gpui {

enum class HostValueKind : uint8_t {
    Null,
    Bool,
    Number,
    String,
    Array,
    Object,
};

struct HostValue;

struct HostField {
    Str name;
    HostValue* value = nullptr;
};

// Plain data crossing the script/host boundary. Ownership is explicit so the
// recursive shape remains usable without STL containers.
struct HostValue {
    HostValueKind kind = HostValueKind::Null;
    bool boolean = false;
    double number = 0;
    Str string;
    Vec<HostValue*> array;
    Vec<HostField> object;

    void Free();
    bool CopyFrom(const HostValue& other);
    void SetNull();
    void SetBool(bool value);
    void SetNumber(double value);
    bool SetString(Str value);
    bool Append(const HostValue& value);
    bool SetField(Str name, const HostValue& value);
    const HostValue* Get(Str name) const;
    const char* Describe() const;
};

struct HostError {
    Str message;

    bool IsSet() const { return message.s != nullptr; }
    void Set(Str value);
    void Clear();
};

struct HostArguments {
    Vec<HostValue*> values;

    void Free();
    int Len() const { return len(values); }
    const HostValue* Get(int index) const;
    bool Value(int index, const HostValue** value, HostError* error) const;
    bool String(int index, Str* value, HostError* error) const;
    bool Number(int index, double* value, HostError* error) const;
    bool Integer(int index, int64_t* value, HostError* error) const;
    bool Boolean(int index, bool* value, HostError* error) const;
};

struct HostCall {
    const HostArguments* arguments = nullptr;
    HostValue result;
    HostError error;

    void Fail(Str message) { error.Set(message); }
};

struct HostModules;

// The begin callback runs on the UI thread. It validates arguments and hands
// back work that is safe for the executor. `release` disposes anything the
// work callback captured after it runs or if scheduling fails.
struct HostAsyncRequest {
    const HostArguments* arguments = nullptr;
    Func1<HostCall*> work;
    Func0 release;
    HostModules* registry = nullptr;
    HostError error;
};

class HostModule {
  public:
    static HostModule* New(Str name);
    HostModule* Retain();
    void Release();

    HostModule* Function(Str name, Func1<HostCall*> body, Func0 release = {});
    HostModule* AsyncFunction(Str name, Func1<HostCall*> work,
                              Func0 release = {});
    HostModule* AsyncFunction(Str name, Func1<HostAsyncRequest*> begin,
                              Func0 release = {});
    HostModule* Declarations(Str typescript);

    Str Name() const { return name; }
    Str Declared() const { return declarations; }
    int FunctionCount() const { return functions.len; }
    Str FunctionName(int index) const;
    bool Has(Str function) const;
    bool IsAsync(Str function) const;
    bool Validate(HostError* error = nullptr) const;
    bool Call(Str function, HostCall* call) const;
    bool Begin(Str function, HostAsyncRequest* request) const;

  private:
    struct FunctionEntry;
    uint32_t refs = 1;
    Str name;
    Str declarations;
    Vec<FunctionEntry*> functions;

    explicit HostModule(Str name);
    ~HostModule();
    FunctionEntry* Find(Str function) const;
    HostModule* SetFunction(Str name, bool async, Func1<HostCall*> body,
                            Func1<HostAsyncRequest*> begin, Func0 release);
};

HostModules* HostModulesNew();
HostModules* HostModulesRetain(HostModules* modules);
void HostModulesRelease(HostModules* modules);
HostModules* HostModulesClone(HostModules* modules);
uint64_t HostModulesGeneration(const HostModules* modules);
int HostModulesCount(const HostModules* modules);
HostModule* HostModulesAt(const HostModules* modules, int index);
HostModule* HostModulesGet(const HostModules* modules, Str name);
bool HostModulesInsert(HostModules* modules, HostModule* module);

bool ShellExportModule(HostModule* module, HostError* error = nullptr);
void ShellClearExportedModules();

bool HostDispatch(Str module, Str function, HostCall* call);
bool HostDispatchBegin(Str module, Str function, HostAsyncRequest* request);
bool HostIsIdentifier(Str name);
bool HostIsReservedSpecifier(Str name);

} // namespace gpui

#endif // GPUI_SHELL_HOST_MODULES_H_
