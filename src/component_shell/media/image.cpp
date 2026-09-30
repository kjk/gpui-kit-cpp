// crates/component-shell/src/shell/media/image.rs

#include "component_shell/families.h"
#include "component_shell/media/mod.h"

namespace gpui::component_shell::media::image {

struct Source {
    Str path;
};

// Path::new(path).components() yields a ParentDir for any `..` segment and a
// RootDir for a leading separator; a Windows prefix needs a `:`, which is
// refused on its own, and so is a backslash.
static bool EscapesRoot(Str path) {
    if (len(path) > 0 && path.s[0] == '/') return true;
    int start = 0;
    for (int i = 0; i <= len(path); i++) {
        if (i < len(path) && path.s[i] != '/') continue;
        Str segment = Str(path.s + start, i - start);
        if (StrEq(segment, "..")) return true;
        start = i + 1;
    }
    return false;
}

bool AssetPath(Str path, Str* out, Str* error) {
    // Rust's `str::trim` trims Unicode whitespace; a path's is in practice
    // only ever the ASCII kind.
    path = StrTrimAscii(path);
    if (len(path) == 0) {
        *error = StrL("Image path must not be empty");
        return false;
    }
    if (StrContains(path, StrL(":")) || StrContains(path, StrL("\\")) ||
        StrStartsWith(path, StrL("data:")) ||
        StrStartsWith(path, StrL("file:")) || EscapesRoot(path)) {
        *error = StrL(
            "Image path must stay inside the application asset root; URLs are "
            "not accepted");
        return false;
    }
    *out = path;
    return true;
}

static El* Materialize(MaterializeRequest* request) {
    const Source* source = request->PayloadAs<Source>();
    if (!source)
        return request->Fail(StrL("Image received an incompatible payload"));
    if (request->ChildrenLen() != 0)
        return request->Fail(StrL("Image does not accept children"));
    // gpui::img(path): loaded through the installed application assets, the
    // way the shell's own `img` is.
    return request->ApplyStyle(ImageEl(request->cx->a, source->path));
}

static bool Construct(PayloadBuild* build, const ComponentArgument* args,
                      int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::String)
        return build
            ->Fail(StrL("Image expects one application-relative asset path"));
    Str path, error;
    if (!AssetPath(args[0].string, &path, &error)) return build->Fail(error);
    build->New<Source>()->path = path;
    return true;
}

static constexpr ArgumentDescriptor kPathArgs[] = {{"path", SchemaString()}};
static constexpr ConstructorDescriptor kConstructors[] = {
    {"Image", kPathArgs, &Construct}};
static constexpr ComponentDescriptor kImage = {
    "Image",
    kConstructors,
    {},
    "A local image loaded only from a relative path beneath the application "
    "asset root. URLs, absolute paths, traversal, and children are rejected; "
    "shell style is honored.",
    &Materialize};

} // namespace gpui::component_shell::media::image

namespace gpui::component_shell {

bool RegisterMediaImage(shell::ComponentRegistry* registry,
                        shell::RegistryError* error) {
    return registry->Register(&media::image::kImage, error);
}

} // namespace gpui::component_shell
