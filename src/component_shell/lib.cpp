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
    // TitleBar::window_options: the catalog draws the title bar, so sheets
    // and notifications line up under it. Copy the options; the reference
    // the host passed is const.
    shell::ComponentWindowOptions chrome = options;
    chrome.opts.clientTitleBar = true;
    Window* window =
        WindowOpen(app, chrome.title, chrome.dipW, chrome.dipH, chrome.opts);
    if (!window) return nullptr;
    MountWindowRoot(app, window, build, data);
    return window;
}

// The catalog carries its own startup, so a host holding only the frozen
// registry starts the components against the globals they need.
static void Initializer(App* app) {
    Init(app);
}

bool Registry(shell::ComponentRegistry* registry, shell::RegistryError* error) {
    if (!registry || !registry->Open(shell::kComponentRegistryApiVersion,
                                     shell::kDefaultComponentModule, error)) {
        return false;
    }
    registry->WithInitializer(&Initializer);
    registry->WithWindowOpener(&OpenWindowWithRoot);
    return Register(registry, error);
}

const shell::FrozenComponentRegistry* Components() {
    static shell::FrozenComponentRegistry frozen;
    static bool built = false;
    if (built) return &frozen;
    built = true;
    shell::ComponentRegistry registry;
    shell::RegistryError error;
    if (!Registry(&registry, &error) && error.IsSet()) {
        Arena* a = ArenaNew();
        logf("gpui-component-shell: %s\n",
             shell::RegistryErrorMessage(a, error));
        ArenaDelete(a);
    }
    registry.Freeze(&frozen);
    return &frozen;
}

} // namespace gpui::component_shell
