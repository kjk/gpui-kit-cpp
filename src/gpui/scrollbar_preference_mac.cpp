#include "gpui/platform.h"

#import <AppKit/NSScroller.h>

namespace gpui {

bool PlatAutoHideScrollbars() {
    return [NSScroller preferredScrollerStyle] == NSScrollerStyleOverlay;
}

} // namespace gpui
