// crates/component-shell/src/shell/navigation/icon.rs

#include "component_shell/families.h"
#include "component_shell/navigation/mod.h"
#include "ui/icon.h"

#include <float.h>
#include <math.h>

namespace gpui::component_shell::navigation {

bool IconSize(Str value, UiSize* out, Str* error) {
    for (const char* literal : kSizeLiterals) {
        if (StrEq(value, Str(literal))) {
            *out = SizeOfLiteral(value);
            return true;
        }
    }
    *error = fmt("unsupported Icon size `%s`", value);
    return false;
}

bool IconRotation(double value, float* out, Str* error) {
    if (isfinite(value) && value >= -(double)FLT_MAX &&
        value <= (double)FLT_MAX) {
        *out = (float)value;
        return true;
    }
    *error = StrL("Icon.rotate expects finite radians representable as f32");
    return false;
}

// Path::new(path).components() hits a Prefix, a RootDir or a ParentDir. The
// separators and the drive prefix are Windows' — the stricter reading, so a
// path refused on one platform is refused on every one.
static bool EscapesRoot(Str path) {
    if (len(path) == 0) return false;
    if (path.s[0] == '/' || path.s[0] == '\\') return true;
    if (len(path) >= 2 && path.s[1] == ':' &&
        ((path.s[0] >= 'a' && path.s[0] <= 'z') ||
         (path.s[0] >= 'A' && path.s[0] <= 'Z')))
        return true;
    int start = 0;
    for (int i = 0; i <= len(path); i++) {
        if (i < len(path) && path.s[i] != '/' && path.s[i] != '\\') continue;
        if (StrEq(Str(path.s + start, i - start), StrL(".."))) return true;
        start = i + 1;
    }
    return false;
}

bool IconPath(Str path, Str* error) {
    // Rust's `str::trim` trims Unicode whitespace; a path's is in practice
    // only ever the ASCII kind.
    if (len(StrTrim(path)) == 0) {
        *error = StrL("Icon path must not be empty");
        return false;
    }
    if (EscapesRoot(path)) {
        *error = StrL("Icon path must stay inside the application asset root");
        return false;
    }
    return true;
}

} // namespace gpui::component_shell::navigation

namespace gpui::component_shell::navigation::icon {

struct IconPayload {
    Str path;
};

struct IconOp {
    enum Kind : uint8_t {
        Size,
        Color,
        Rotate,
    } kind = Size;
    UiSize size = UiSize::Medium;
    Rgba color = {};
    float radians = 0;
};

static El* Materialize(MaterializeRequest* request) {
    const IconPayload* payload = request->PayloadAs<IconPayload>();
    if (!payload)
        return request->Fail(StrL("Icon received an incompatible payload"));
    Ctx* cx = request->cx;
    component::Icon* icon = component::Icon::Empty(cx)->Path(payload->path);
    EachMethod<IconOp>(request, [&](const IconOp& op) {
        switch (op.kind) {
            case IconOp::Size:
                icon->Size(op.size);
                break;
            case IconOp::Color:
                icon->Color(op.color);
                break;
            case IconOp::Rotate:
                // gpui::radians(value): the port's Icon rotates in turns.
                icon->Rotate(op.radians / (2.f * 3.14159265358979f));
                break;
        }
    });
    ElRefiner style = request->TakeStyle();
    El** children = nullptr;
    int count = 0;
    if (!request->TakeChildren(&children, &count)) return nullptr;
    if (count != 0) return request->Fail(StrL("Icon does not accept children"));
    El* element = icon->IntoEl();
    style.Apply(element);
    return element;
}

static bool Construct(PayloadBuild* build, const ComponentArgument* args,
                      int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::String)
        return build->Fail(StrL("Icon expects one asset path string"));
    Str error;
    if (!IconPath(args[0].string, &error)) return build->Fail(error);
    build->New<IconPayload>()->path = args[0].string;
    return true;
}

static bool RecordSize(PayloadBuild* build, const ComponentArgument* args,
                       int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Enum)
        return build->Fail(StrL("Icon.size expects a semantic size literal"));
    UiSize size;
    Str error;
    if (!IconSize(args[0].string, &size, &error)) return build->Fail(error);
    IconOp* op = build->New<IconOp>();
    op->kind = IconOp::Size;
    op->size = size;
    return true;
}

static bool RecordColor(PayloadBuild* build, const ComponentArgument* args,
                        int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::String)
        return build->Fail(StrL("Icon.color expects one color string"));
    Rgba color;
    if (!ParseColorArgument(build, "Icon", args[0].string, &color))
        return false;
    IconOp* op = build->New<IconOp>();
    op->kind = IconOp::Color;
    op->color = color;
    return true;
}

static bool RecordRotate(PayloadBuild* build, const ComponentArgument* args,
                         int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Number)
        return build->Fail(StrL("Icon.rotate expects one number of radians"));
    float radians = 0;
    Str error;
    if (!IconRotation(args[0].number, &radians, &error))
        return build->Fail(error);
    IconOp* op = build->New<IconOp>();
    op->kind = IconOp::Rotate;
    op->radians = radians;
    return true;
}

static constexpr ArgumentDescriptor kPathArgs[] = {{"path", SchemaString()}};
static constexpr ArgumentDescriptor kSizeArgs[] = {
    {"size", SchemaEnum(kSizeLiterals)}};
static constexpr ArgumentDescriptor kColorArgs[] = {{"color", SchemaString()}};
static constexpr ArgumentDescriptor kRotateArgs[] = {
    {"radians", SchemaNumber()}};
static constexpr ConstructorDescriptor kConstructors[] = {
    {"Icon", kPathArgs, &Construct}};
static constexpr MethodDescriptor kMethods[] = {
    {"size", kSizeArgs, "Sets the semantic icon size.", &RecordSize},
    {"color", kColorArgs, "Sets the icon color from a supported color token.",
     &RecordColor},
    {"rotate", kRotateArgs, "Rotates the icon by a finite number of radians.",
     &RecordRotate},
};
static constexpr ComponentDescriptor kIcon = {
    "Icon", kConstructors, kMethods,
    "An SVG icon loaded from a relative path beneath the application's asset "
    "root. Absolute paths and parent traversal are rejected.",
    &Materialize};

} // namespace gpui::component_shell::navigation::icon

namespace gpui::component_shell {

bool RegisterNavigationIcon(shell::ComponentRegistry* registry,
                            shell::RegistryError* error) {
    return registry->Register(&navigation::icon::kIcon, error);
}

} // namespace gpui::component_shell
