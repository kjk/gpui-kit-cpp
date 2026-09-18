#include "shell/policy.h"

namespace gpui {

struct PolicyShared {
    uint32_t refs = 1;
    shell::Storage local{true};
    shell::Storage session{false};
    HostModules* modules = HostModulesNew();

    ~PolicyShared() { HostModulesRelease(modules); }
};

// The application name a policy carries when the host named none.
static const char* const kDefaultApplication = "app";

struct Policy {
    uint32_t refs = 1;
    Capabilities capabilities;
    // Owned, and carried by every copy: the name is what the application's
    // persisted panels are filed under, and a copy that renamed them would
    // lose the layout the user had.
    Str application = StrDup(Str(kDefaultApplication));
    PolicyShared* shared = nullptr;

    ~Policy() { StrFree(application); }
};

static PolicyShared* SharedRetain(PolicyShared* shared) {
    if (shared) shared->refs++;
    return shared;
}

static void SharedRelease(PolicyShared* shared) {
    if (shared && --shared->refs == 0) delete shared;
}

Policy* PolicyNew() {
    Policy* policy = new Policy();
    policy->shared = new PolicyShared();
    return policy;
}

Policy* PolicyNew(const Capabilities& capabilities) {
    Policy* policy = PolicyNew();
    policy->capabilities = capabilities;
    return policy;
}

Policy* PolicyRetain(Policy* policy) {
    if (policy) policy->refs++;
    return policy;
}

void PolicyRelease(Policy* policy) {
    if (!policy || --policy->refs != 0) return;
    SharedRelease(policy->shared);
    delete policy;
}

Str PolicyApplication(const Policy* policy) {
    return policy ? policy->application : Str(kDefaultApplication);
}

void PolicySetApplication(Policy* policy, Str name) {
    if (!policy) return;
    Str trimmed = StrTrimAscii(name);
    StrFree(policy->application);
    policy->application = StrDup(
        trimmed && len(trimmed) > 0 ? trimmed : Str(kDefaultApplication));
}

const Capabilities& PolicyCapabilities(const Policy* policy) {
    static const Capabilities denied;
    return policy ? policy->capabilities : denied;
}

static thread_local Policy* gDefaultPolicy = nullptr;

static Policy* EnsureDefault() {
    if (!gDefaultPolicy) gDefaultPolicy = PolicyNew();
    return gDefaultPolicy;
}

Policy* PolicyDefault() {
    return PolicyRetain(EnsureDefault());
}

void PolicySetDefault(Policy* policy) {
    Policy* replacement = policy ? PolicyRetain(policy) : PolicyNew();
    PolicyRelease(gDefaultPolicy);
    gDefaultPolicy = replacement;
}

void PolicyUpdateDefaultCapabilities(const Capabilities& capabilities) {
    Policy* current = EnsureDefault();
    Policy* replacement = new Policy();
    replacement->capabilities = capabilities;
    PolicySetApplication(replacement, current->application);
    replacement->shared = SharedRetain(current->shared);
    gDefaultPolicy = replacement;
    PolicyRelease(current);
}

void PolicyUpdateDefaultApplication(Str name) {
    Policy* current = EnsureDefault();
    Policy* replacement = new Policy();
    replacement->capabilities = current->capabilities;
    PolicySetApplication(replacement, name);
    replacement->shared = SharedRetain(current->shared);
    gDefaultPolicy = replacement;
    PolicyRelease(current);
}

shell::Storage* PolicyStorage(Policy* policy, bool session) {
    if (!policy || !policy->shared) return nullptr;
    return session ? &policy->shared->session : &policy->shared->local;
}

bool PolicySetStoragePath(Policy* policy, Str path, Str* error) {
    shell::Storage* storage = PolicyStorage(policy, false);
    return storage && storage->SetPath(path, error);
}

bool ShellSetStoragePath(Str path, Str* error) {
    Policy* policy = PolicyDefault();
    bool ok = PolicySetStoragePath(policy, path, error);
    PolicyRelease(policy);
    return ok;
}

HostModules* PolicyHostModules(Policy* policy) {
    return policy && policy->shared ? HostModulesRetain(policy->shared->modules)
                                    : HostModulesNew();
}

bool PolicyAddHostModule(Policy* policy, HostModule* module, HostError* error) {
    if (error) error->Clear();
    if (!policy || !policy->shared || !module || !module->Validate(error))
        return false;
    HostModules* replacement = HostModulesClone(policy->shared->modules);
    if (!replacement || !HostModulesInsert(replacement, module)) {
        HostModulesRelease(replacement);
        if (error)
            error->Set(StrL("could not add HostModule within memory limits"));
        return false;
    }
    HostModulesRelease(policy->shared->modules);
    policy->shared->modules = replacement;
    return true;
}

void PolicyClearHostModules(Policy* policy) {
    if (!policy || !policy->shared) return;
    HostModules* empty = HostModulesNew();
    HostModulesRelease(policy->shared->modules);
    policy->shared->modules = empty;
}

} // namespace gpui
