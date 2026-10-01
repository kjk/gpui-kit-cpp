// crates/component-shell/src/lib.rs and src/shell/mod.rs

#include "component_shell/lib.h"
#include "component_shell/families.h"
#include "base/root.h"
#include "ui/lib.h"

namespace gpui::component_shell {

void Init(App* app) {
    component::Init(app);
}

// shell/mod.rs `register`, in its order: the order is the catalog's, which
// the declarations and the component ids follow.
static const RegisterFamily kFamilies[] = {
    &RegisterSpinner,
    &RegisterSeparator,
    &RegisterSkeleton,
    &RegisterChat,
    &RegisterEmpty,
    &RegisterInputGroup,
    &RegisterControls,
    &RegisterDelegateCollections,
    &RegisterDelegateCombobox,
    &RegisterDelegateSelect,
    &RegisterDataTable,
    &RegisterDisplay,
    &RegisterCompound,
    &RegisterTypedCompound,
    &RegisterLifecycle,
    &RegisterCollections,
    &RegisterCommand,
    &RegisterWindowEffects,
    &RegisterOverlays,
    &RegisterRetainedForms,
    &RegisterLayout,
    &RegisterMedia,
    &RegisterScroll,
    &RegisterSettings,
    &RegisterStructured,
    &RegisterNavigation,
    &RegisterBasic,
    &RegisterChart,
    &RegisterCarousel,
    &RegisterQuestionnaire,
};

bool Register(shell::ComponentRegistry* registry, shell::RegistryError* error) {
    for (RegisterFamily family : kFamilies) {
        if (!family(registry, error)) return false;
    }
    return true;
}

void MountWindowRoot(App* app, Window* window,
                     shell::ComponentWindowBuild build, void* data) {
    if (!app || !window || !build) return;
    EntityId inner = build(window, app, data);
    Entity<Root> root = Root::New(app, window, inner);
    window->root = root.id;
    AppInvalidate(window);
}

Window* OpenWindowWithRoot(App* app,
                           const shell::ComponentWindowOptions& options,
                           shell::ComponentWindowBuild build, void* data) {
    Window* window = WindowOpen(app, options.title, options.dipW, options.dipH,
                                options.opts);
    if (!window) return nullptr;
    MountWindowRoot(app, window, build, data);
    return window;
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
    registry.WithWindowOpener(&OpenWindowWithRoot);
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
