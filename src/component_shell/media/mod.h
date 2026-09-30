#ifndef GPUI_COMPONENT_SHELL_MEDIA_MOD_H_
#define GPUI_COMPONENT_SHELL_MEDIA_MOD_H_

// crates/component-shell/src/shell/media/: renderable media bindings with
// closed resource and retained-state contracts. What the modules' own tests
// and the public-host probe reach.

#include "component_shell/support.h"
#include "shell/input_tokens.h"

namespace gpui::component_shell::media::image {

// asset_path: the trimmed path when it stays beneath the application asset
// root, else false with Rust's message in `error`.
bool AssetPath(Str path, Str* out, Str* error);

} // namespace gpui::component_shell::media::image

namespace gpui::component_shell::media::editor {

// Rust's `Entity<EditorState>`. The port's EditorState is the retained
// InputState in its editor mode, and the syntax language the Rust state
// carries is kept beside it, since here the Editor frame value is what binds
// a language to the state.
struct EditorStateValue {
    shell::TextStateEntity text;
    // "rust" or "json", or null for none.
    const char* language = nullptr;
};

// require_leaf: "Editor does not accept children".
bool RequireLeaf(int children, Str* error);

} // namespace gpui::component_shell::media::editor
#endif // GPUI_COMPONENT_SHELL_MEDIA_MOD_H_
