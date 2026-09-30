// crates/component-shell/src/lib.rs and src/shell/mod.rs

#include "component_shell/lib.h"
#include "component_shell/families.h"
#include "ui/lib.h"

namespace gpui::component_shell {

void Init(App* app) {
    component::Init(app);
}

// shell/mod.rs `register`, in its order: the order is the catalog's, which
// the declarations and the component ids follow.
static const RegisterFamily kFamilies[] = {
    &RegisterSpinner,  &RegisterSeparator,     &RegisterSkeleton,
    &RegisterEmpty,    &RegisterControls,      &RegisterDisplay,
    &RegisterCompound, &RegisterTypedCompound, &RegisterBasic,
};

bool Register(shell::ComponentRegistry* registry, shell::RegistryError* error) {
    for (RegisterFamily family : kFamilies) {
        if (!family(registry, error)) return false;
    }
    return true;
}

// The catalog carries its own startup, so a host holding only the frozen
// registry starts the components against the globals they need.
static void Initializer(App* app) {
    Init(app);
}

const shell::FrozenComponentRegistry* Components() {
    static shell::FrozenComponentRegistry frozen;
    static bool built = false;
    if (built) return &frozen;
    built = true;
    shell::ComponentRegistry registry;
    shell::RegistryError error;
    if (!registry.Open(shell::kComponentRegistryApiVersion,
                       shell::kDefaultComponentModule, &error)) {
        return &frozen;
    }
    registry.WithInitializer(&Initializer);
    if (!Register(&registry, &error)) {
        Arena* a = ArenaNew();
        logf("gpui-component-shell: %s\n",
             shell::RegistryErrorMessage(a, error));
        ArenaDelete(a);
    }
    registry.Freeze(&frozen);
    return &frozen;
}

} // namespace gpui::component_shell
