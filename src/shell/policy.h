#ifndef GPUI_SHELL_POLICY_H_
#define GPUI_SHELL_POLICY_H_

#include "shell/capability.h"
#include "shell/host_modules.h"
#include "shell/storage.h"

namespace gpui {

struct Policy;

Policy* PolicyNew();
Policy* PolicyNew(const Capabilities& capabilities);
Policy* PolicyRetain(Policy* policy);
void PolicyRelease(Policy* policy);
const Capabilities& PolicyCapabilities(const Policy* policy);

// The name this application's panels are filed under. A dock layout persists
// a panel by name, and two applications that both call a panel `inbox` must
// not collide in one layout file — so the name is namespaced by the
// application, and this is where the application half comes from. The policy
// carries it rather than the runtime because one runtime can host several
// plugins, and because a policy already answers "under whose authority?".
// A host that loads one application never has to set it: the default is
// `app`, and a single-application layout file has nothing to collide with.
Str PolicyApplication(const Policy* policy);
void PolicySetApplication(Policy* policy, Str name);
// The application's data directory, shown to script as `process.dataDir`.
// A path, not a grant: reaching it still needs a filesystem root.
void PolicySetDataDir(Policy* policy, Str path);
Str PolicyDataDir(const Policy* policy);
// The default policy's name, which `set_bundle_id` places beside its store.
void PolicyUpdateDefaultApplication(Str name);

// Returns a retained handle. Capability grants are frozen once handed out;
// edits replace the default policy and never widen an existing holder.
Policy* PolicyDefault();
void PolicySetDefault(Policy* policy);
void PolicyUpdateDefaultCapabilities(const Capabilities& capabilities);
shell::Storage* PolicyStorage(Policy* policy, bool session);
bool PolicySetStoragePath(Policy* policy, Str path, Str* error = nullptr);
bool ShellSetStoragePath(Str path, Str* error = nullptr);
HostModules* PolicyHostModules(Policy* policy);
bool PolicyAddHostModule(Policy* policy, HostModule* module,
                         HostError* error = nullptr);
void PolicyClearHostModules(Policy* policy);

} // namespace gpui
#endif // GPUI_SHELL_POLICY_H_
