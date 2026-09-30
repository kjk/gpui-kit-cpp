#ifndef GPUI_SHELL_MATERIALIZE_H_
#define GPUI_SHELL_MATERIALIZE_H_

#include "shell/runtime.h"

namespace gpui {

// Replays one immutable script snapshot into the ordinary native element
// tree. This function does not enter QuickJS; callbacks only carry ids back
// to ScriptView for a later event dispatch.
El* ShellMaterialize(Ctx* cx, ShellRuntime* runtime,
                     const RenderSnapshot* snapshot,
                     ShellError* error = nullptr);
El* ShellMaterializeSpec(Ctx* cx, ShellRuntime* runtime,
                         const shell::SpecArena* specs, shell::SpecId root,
                         ShellError* error = nullptr);
// A node's own style methods and motions, applied to `target`: what a
// registered component takes as its style.
void ShellApplyNodeStyle(Ctx* cx, const shell::SpecArena* specs,
                         shell::SpecId id, El* target,
                         ShellError* error = nullptr);

namespace shell {

// materialize/text_view.rs, the seams its tests reach.

// image_url: whether a document image at `url` may be requested at all — an
// absolute http(s) URL with no credentials that the grant allows a GET to.
bool TextViewImageUrl(const Capabilities& capabilities, Str url);

struct TextViewImageResponse {
    bool ok = false;
    // Borrowed for the callback.
    Str bytes = {};
};

// request_image: GETs `url` without its fragment, re-authorizing every
// redirect, and answers the body of a success status within the 8 MiB limit.
// False, with no callback and no request, for a URL TextViewImageUrl refuses.
bool TextViewImageRequest(const Capabilities& capabilities, Str url,
                          Func1<TextViewImageResponse> done);

// refuse_svg_file_references: an SVG with an `<image>` naming anything but a
// data URL, which a document image is refused for.
bool TextViewSvgReferencesFile(Str bytes);

} // namespace shell
} // namespace gpui
#endif // GPUI_SHELL_MATERIALIZE_H_
