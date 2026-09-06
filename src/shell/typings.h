#ifndef GPUI_SHELL_TYPINGS_H_
#define GPUI_SHELL_TYPINGS_H_

#include "shell/error.h"
#include "shell/host_modules.h"

namespace gpui::shell {

constexpr const char* kShellTypesFile = "gpui-kit.d.ts";
// The editor configuration, in the spelling a JavaScript project uses, and
// the TypeScript one it is written beside and defers to.
constexpr const char* kShellConfigFile = "jsconfig.json";
constexpr const char* kShellTypeScriptConfigFile = "tsconfig.json";
constexpr int kShellTypesMaxDepth = 8;
constexpr int kShellTypesMaxFiles = 4096;

void AppendBuiltinTypeDeclarations(StrBuilder* out);

// Appends the generated built-ins followed by declarations for modules the
// embedding host grants.
void ShellTypeDeclarations(StrBuilder* out,
                           const HostModules* modules = nullptr);

// Writes gpui-kit.d.ts at the application root and beside scripts in nested
// directories that import a built-in module. Identical files are untouched.
bool ShellWriteTypeDeclarations(Str directory,
                                const HostModules* modules = nullptr,
                                int* written = nullptr,
                                ShellError* error = nullptr);

} // namespace gpui::shell

#endif // GPUI_SHELL_TYPINGS_H_
