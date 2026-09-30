#ifndef GPUI_UI_AVATAR_H_
#define GPUI_UI_AVATAR_H_
/* Themed avatar — crates/ui/src/avatar */

#include "ui/sizing.h"

namespace gpui {

namespace component {

// crates/ui/src/avatar/mod.rs avatar_size()
float AvatarSizePx(UiSize s);

// avatar.rs extract_text_initials: the first letter of each of the first two
// words, upper-cased; a single-word name gives its first two letters instead.
// Temp-arena backed; callers that keep the initials copy them into their
// owning arena.
TempStr AvatarInitialsTemp(Str name);

struct Avatar {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Str initials = {};
    Background bg = {};
    bool hasBg = false;
    float size = 48;
    // Set by WithSize; drives the fallback text size the way avatar_text_size
    // does. -1 means the caller gave an explicit pixel size.
    float textPx = -1;
    // avatar_text_size: only a Large avatar sets its initials semibold.
    bool textSemibold = false;
    float radius = -1;
    float borderW = 1;
    Rgba borderC = {};
    bool hasBorderC = false;
    IconName placeholder = IconName::User;
    // Avatar::src: the picture, which replaces the fallback entirely when it
    // is given — Rust does not fall back when a load fails either, since
    // `img()` simply draws nothing until the bytes land.
    Str src = {};

    static Avatar* New(Ctx* cx);
    // Avatar::name: the whole name, of which the fallback shows the initials
    // and off which its color is picked.
    Avatar* Name(Str s);
    Avatar* Src(Str url);
    Avatar* Initials(Str s);
    Avatar* Bg(Background c);
    Avatar* Size(float v);
    Avatar* WithSize(UiSize s);
    Avatar* Radius(float v);
    Avatar* Border(float w, Rgba c);
    Avatar* Placeholder(IconName n);
    El* IntoEl();
};

// crates/ui/src/avatar/avatar_group.rs: overlapping avatars, at most `limit`
// of them, with an optional ⋯ chip for the rest.
struct AvatarGroup {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    ArenaVec<Avatar*> avatars;
    UiSize size = UiSize::Medium;
    int limit = 3;
    bool ellipsis = false;

    static AvatarGroup* New(Ctx* cx);
    AvatarGroup* Child(Avatar* av);
    AvatarGroup* WithSize(UiSize s);
    AvatarGroup* Limit(int v);
    AvatarGroup* Ellipsis();
    El* IntoEl();
};

} // namespace component
} // namespace gpui
#endif // GPUI_UI_AVATAR_H_
