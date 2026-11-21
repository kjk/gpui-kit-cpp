/* The Cocoa window: event pump, chrome routing, timers, clipboard, and the
   process entry point. The mirror of Window_win.cpp and Window_linux.cpp;
   everything any of them decides is delegated to WindowCommon.cpp.

   Objective-C++ under ARC (-x objective-c++ -fobjc-arc). The view is flipped,
   so a frame is drawn in points with the origin at the top left. One point is
   one DIP, which on a Retina display means the backing store is 2x and the
   drawing comes out crisp for free. */

#include "gpui/platform.h"
#include "base/input.h"
#include "gpui/paint.h"
#include "gpui/window_loop.h"
#include "sys/clock.h"
#include "sys/executor.h"

#import <Cocoa/Cocoa.h>
// Not WebKit.h: its legacy JavaScriptCore bridge collides with QuickJS in the
// one-file dist (see wry_mac.cpp).
#import <WebKit/WKWebView.h>
#import <objc/runtime.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <time.h>

@class GpuiView;
@class GpuiWindowDelegate;
@class GpuiAccessibilityElement;

// WinOpts::frameless with ::activatable: AppKit lets a borderless window
// become key only when its class says so. Never main: the parent stays the
// main window, as a tool palette leaves it.
@interface GpuiKeyableWindow : NSWindow
@end

@implementation GpuiKeyableWindow
- (BOOL)canBecomeKeyWindow {
    return YES;
}
- (BOOL)canBecomeMainWindow {
    return NO;
}
@end

namespace gpui {

struct PlatWindow {
    NSWindow* window = nil;
    GpuiView* view = nil;
    // The window does not retain its delegate; this reference is what keeps
    // it alive.
    GpuiWindowDelegate* delegate = nil;
    // Native semantic handles are cached by the portable node id. They keep
    // no frame strings or element pointers and resolve every query against
    // Window::accessibility, so VoiceOver gets stable identity without
    // retaining an arena frame.
    NSMutableDictionary<NSNumber*, GpuiAccessibilityElement*>*
        accessibilityElements = nil;
    // Monotonic deadline for the next tick; 0 when the timer is off.
    double nextTick = 0;
    // Shown only after the first paint. Ordering front at creation puts an
    // empty window on screen for as long as the caller spends before AppRun.
    bool ordered = false;
    // WinOpts::show false, or WindowSetVisible(false): the first paint does
    // not order it front. `activateOnShow` is how the first paint shows it.
    bool hidden = false;
    bool activateOnShow = true;
    // A move with the button down: polled until the button is let go, which
    // is the end of the move (AppKit says when a live resize ends, not a
    // move).
    NSTimer* moveEndPoll = nil;
    // Whether the window's class has been taught accessibilityHitTest:. Rust
    // installs it once, when the Root view is created; a Root here is built
    // every frame, so the window remembers instead.
    bool a11yInstalled = false;
};

double TimeNow() {
    double simulated = 0;
    if (TestPlatformNow(&simulated)) {
        return simulated;
    }
    return ClockMonotonicSecondsSinceStart();
}

// Defined below, once NSEvent is in scope for the whole file.
bool WindowMacKeyDown(Window* win, NSEvent* event);
void WindowMacKeyUp(Window* win, NSEvent* event);

} // namespace gpui

@interface GpuiAccessibilityElement : NSAccessibilityElement {
  @public
    gpui::Window* gpuiWindow;
    uint32_t gpuiNodeId;
}
@end

static const gpui::AccessibilityNode* GpuiAccessibilityNode(
    GpuiAccessibilityElement* element) {
    return element && element->gpuiWindow
               ? gpui::WindowAccessibilityNode(element->gpuiWindow,
                                               element->gpuiNodeId)
               : nullptr;
}

static NSString* GpuiAccessibilityString(gpui::Str value) {
    if (!value.s || len(value) <= 0) {
        return @"";
    }
    return [[NSString alloc] initWithBytes:value.s
                                    length:(NSUInteger)len(value)
                                  encoding:NSUTF8StringEncoding];
}

static NSString* GpuiAccessibilityRole(gpui::AccessibilityRole role) {
    using gpui::AccessibilityRole;
    if (gpui::AccessibilityRoleTakesText(role)) {
        return role == AccessibilityRole::MultilineTextInput
                   ? NSAccessibilityTextAreaRole
                   : NSAccessibilityTextFieldRole;
    }
    switch (role) {
        case AccessibilityRole::Alert:
        case AccessibilityRole::Heading:
        case AccessibilityRole::Label:
        case AccessibilityRole::Paragraph:
        case AccessibilityRole::TextRun:
            return NSAccessibilityStaticTextRole;
        case AccessibilityRole::Document:
            return NSAccessibilityGroupRole;
        case AccessibilityRole::Button:
        case AccessibilityRole::DefaultButton:
        case AccessibilityRole::DisclosureTriangle:
            return NSAccessibilityButtonRole;
        case AccessibilityRole::CheckBox:
        case AccessibilityRole::Switch:
            return NSAccessibilityCheckBoxRole;
        case AccessibilityRole::RadioButton:
            return NSAccessibilityRadioButtonRole;
        case AccessibilityRole::Link:
            return NSAccessibilityLinkRole;
        case AccessibilityRole::Image:
        case AccessibilityRole::GraphicsObject:
        case AccessibilityRole::GraphicsSymbol:
            return NSAccessibilityImageRole;
        case AccessibilityRole::Row:
        case AccessibilityRole::LayoutTableRow:
            return NSAccessibilityRowRole;
        case AccessibilityRole::Cell:
        case AccessibilityRole::GridCell:
        case AccessibilityRole::LayoutTableCell:
            return NSAccessibilityCellRole;
        case AccessibilityRole::ColumnHeader:
            return NSAccessibilityColumnRole;
        case AccessibilityRole::Table:
        case AccessibilityRole::Grid:
        case AccessibilityRole::LayoutTable:
        case AccessibilityRole::TreeGrid:
            return NSAccessibilityTableRole;
        case AccessibilityRole::List:
        case AccessibilityRole::ListBox:
        case AccessibilityRole::Tree:
            return NSAccessibilityListRole;
        case AccessibilityRole::ListItem:
        case AccessibilityRole::ListBoxOption:
        case AccessibilityRole::TreeItem:
            return NSAccessibilityRowRole;
        case AccessibilityRole::Menu:
        case AccessibilityRole::MenuBar:
        case AccessibilityRole::MenuListPopup:
            return NSAccessibilityMenuRole;
        case AccessibilityRole::MenuItem:
        case AccessibilityRole::MenuItemCheckBox:
        case AccessibilityRole::MenuItemRadio:
        case AccessibilityRole::MenuListOption:
            return NSAccessibilityMenuItemRole;
        case AccessibilityRole::Slider:
        case AccessibilityRole::ScrollBar:
            return NSAccessibilitySliderRole;
        case AccessibilityRole::ProgressIndicator:
        case AccessibilityRole::Meter:
            return NSAccessibilityProgressIndicatorRole;
        case AccessibilityRole::Tab:
            return NSAccessibilityRadioButtonRole;
        case AccessibilityRole::TabList:
            return NSAccessibilityTabGroupRole;
        case AccessibilityRole::Toolbar:
            return NSAccessibilityToolbarRole;
        case AccessibilityRole::Window:
            return NSAccessibilityWindowRole;
        default:
            return NSAccessibilityGroupRole;
    }
}

static GpuiAccessibilityElement* GpuiAccessibilityElementFor(gpui::Window* win,
                                                             uint32_t id) {
    if (!win || !win->plat || !id) {
        return nil;
    }
    if (!win->plat->accessibilityElements) {
        win->plat->accessibilityElements = [[NSMutableDictionary alloc] init];
    }
    NSNumber* key = [NSNumber numberWithUnsignedInt:id];
    GpuiAccessibilityElement* element =
        [win->plat->accessibilityElements objectForKey:key];
    if (!element) {
        element = [[GpuiAccessibilityElement alloc] init];
        element->gpuiWindow = win;
        element->gpuiNodeId = id;
        [win->plat->accessibilityElements setObject:element forKey:key];
    }
    return element;
}

static NSArray* GpuiAccessibilityChildren(gpui::Window* win, int parent) {
    if (!win) {
        return @[];
    }
    NSMutableArray* children = [[NSMutableArray alloc] init];
    for (int i = 0; i < win->accessibility.len; i++) {
        if (win->accessibility[i].parent == parent) {
            GpuiAccessibilityElement* child =
                GpuiAccessibilityElementFor(win, win->accessibility[i].id);
            if (child) {
                [children addObject:child];
            }
        }
    }
    return children;
}

@implementation GpuiAccessibilityElement

- (BOOL)isAccessibilityElement {
    return GpuiAccessibilityNode(self) != nullptr;
}
- (NSString*)accessibilityRole {
    const gpui::AccessibilityNode* node = GpuiAccessibilityNode(self);
    return node ? GpuiAccessibilityRole(node->info.role)
                : NSAccessibilityUnknownRole;
}
- (NSString*)accessibilityLabel {
    const gpui::AccessibilityNode* node = GpuiAccessibilityNode(self);
    return node ? GpuiAccessibilityString(node->info.label) : @"";
}
- (NSString*)accessibilityIdentifier {
    const gpui::AccessibilityNode* node = GpuiAccessibilityNode(self);
    return node ? GpuiAccessibilityString(node->info.authorId) : @"";
}
- (NSString*)accessibilityHelp {
    const gpui::AccessibilityNode* node = GpuiAccessibilityNode(self);
    if (!node) {
        return @"";
    }
    // aria_description is what VoiceOver reads after the name, role and
    // value; a node without one keeps offering its placeholder.
    if (node->info.description.s) {
        return GpuiAccessibilityString(node->info.description);
    }
    return GpuiAccessibilityString(node->info.placeholder);
}
- (id)accessibilityValue {
    const gpui::AccessibilityNode* node = GpuiAccessibilityNode(self);
    if (!node) {
        return nil;
    }
    if (node->info.hasNumericValue) {
        return [NSNumber numberWithDouble:node->info.numericValue];
    }
    if (node->info.toggled != gpui::AccessibilityToggled::Unset) {
        return [NSNumber
            numberWithInt:node->info.toggled == gpui::AccessibilityToggled::True
                              ? 1
                          : node->info.toggled ==
                                  gpui::AccessibilityToggled::Mixed
                              ? 2
                              : 0];
    }
    return GpuiAccessibilityString(node->info.value);
}
- (id)accessibilityMinValue {
    const gpui::AccessibilityNode* node = GpuiAccessibilityNode(self);
    return node && node->info.hasMinNumericValue
               ? [NSNumber numberWithDouble:node->info.minNumericValue]
               : nil;
}
- (id)accessibilityMaxValue {
    const gpui::AccessibilityNode* node = GpuiAccessibilityNode(self);
    return node && node->info.hasMaxNumericValue
               ? [NSNumber numberWithDouble:node->info.maxNumericValue]
               : nil;
}
- (BOOL)isAccessibilityEnabled {
    const gpui::AccessibilityNode* node = GpuiAccessibilityNode(self);
    return node && !node->info.disabled;
}
- (BOOL)isAccessibilityFocused {
    const gpui::AccessibilityNode* node = GpuiAccessibilityNode(self);
    return node && node->focusId && node->focusId == gpuiWindow->focusId;
}
- (void)setAccessibilityFocused:(BOOL)focused {
    if (focused && gpuiWindow) {
        gpui::WindowAccessibilityPerform(gpuiWindow, gpuiNodeId,
                                         gpui::AccessibilityAction::Focus);
    }
}
- (BOOL)isAccessibilitySelected {
    const gpui::AccessibilityNode* node = GpuiAccessibilityNode(self);
    return node && node->info.hasSelected && node->info.selected;
}
- (BOOL)isAccessibilityExpanded {
    const gpui::AccessibilityNode* node = GpuiAccessibilityNode(self);
    return node && node->info.hasExpanded && node->info.expanded;
}
- (NSAccessibilityOrientation)accessibilityOrientation {
    const gpui::AccessibilityNode* node = GpuiAccessibilityNode(self);
    return node && node->info.orientation ==
                       gpui::AccessibilityOrientation::Vertical
               ? NSAccessibilityOrientationVertical
               : NSAccessibilityOrientationHorizontal;
}
- (NSRect)accessibilityFrame {
    const gpui::AccessibilityNode* node = GpuiAccessibilityNode(self);
    if (!node || !gpuiWindow->plat || !gpuiWindow->plat->view) {
        return NSZeroRect;
    }
    const gpui::Bounds& b = node->bounds;
    NSRect local = NSMakeRect(b.x, b.y, b.w, b.h);
    NSView* view = (NSView*)gpuiWindow->plat->view;
    NSRect inWindow = [view convertRect:local toView:nil];
    return [[view window] convertRectToScreen:inWindow];
}
- (id)accessibilityParent {
    int index = gpui::WindowAccessibilityNodeIndex(gpuiWindow, gpuiNodeId);
    if (index < 0) {
        return nil;
    }
    int parent = gpuiWindow->accessibility[index].parent;
    return parent >= 0 ? (id)GpuiAccessibilityElementFor(
                             gpuiWindow, gpuiWindow->accessibility[parent].id)
                       : (id)gpuiWindow->plat->view;
}
- (NSArray*)accessibilityChildren {
    int index = gpui::WindowAccessibilityNodeIndex(gpuiWindow, gpuiNodeId);
    return GpuiAccessibilityChildren(gpuiWindow, index);
}
- (NSArray<NSString*>*)accessibilityActionNames {
    const gpui::AccessibilityNode* node = GpuiAccessibilityNode(self);
    if (!node) {
        return @[];
    }
    NSMutableArray<NSString*>* actions = [[NSMutableArray alloc] init];
    if (node->actions & gpui::AccessibilityActionDefault) {
        [actions addObject:NSAccessibilityPressAction];
    }
    if (node->actions & gpui::AccessibilityActionIncrement) {
        [actions addObject:NSAccessibilityIncrementAction];
    }
    if (node->actions & gpui::AccessibilityActionDecrement) {
        [actions addObject:NSAccessibilityDecrementAction];
    }
    return actions;
}
- (void)accessibilityPerformAction:(NSString*)action {
    if ([action isEqualToString:NSAccessibilityPressAction]) {
        gpui::WindowAccessibilityPerform(gpuiWindow, gpuiNodeId,
                                         gpui::AccessibilityAction::Default);
    } else if ([action isEqualToString:NSAccessibilityIncrementAction]) {
        gpui::WindowAccessibilityPerform(gpuiWindow, gpuiNodeId,
                                         gpui::AccessibilityAction::Increment);
    } else if ([action isEqualToString:NSAccessibilityDecrementAction]) {
        gpui::WindowAccessibilityPerform(gpuiWindow, gpuiNodeId,
                                         gpui::AccessibilityAction::Decrement);
    }
}
- (BOOL)accessibilityPerformPress {
    return gpui::WindowAccessibilityPerform(gpuiWindow, gpuiNodeId,
                                            gpui::AccessibilityAction::Default);
}
- (BOOL)accessibilityPerformIncrement {
    return gpui::WindowAccessibilityPerform(
        gpuiWindow, gpuiNodeId, gpui::AccessibilityAction::Increment);
}
- (BOOL)accessibilityPerformDecrement {
    return gpui::WindowAccessibilityPerform(
        gpuiWindow, gpuiNodeId, gpui::AccessibilityAction::Decrement);
}
- (void)setAccessibilityValue:(id)value {
    const gpui::AccessibilityNode* node = GpuiAccessibilityNode(self);
    if (!node || !(node->actions & gpui::AccessibilityActionSetValue)) {
        return;
    }
    if ([value isKindOfClass:[NSNumber class]]) {
        gpui::WindowAccessibilitySetNumericValue(gpuiWindow, gpuiNodeId,
                                                 [value floatValue]);
        return;
    }
    if ([value isKindOfClass:[NSString class]]) {
        const char* utf8 = [(NSString*)value UTF8String];
        gpui::WindowAccessibilityPerform(
            gpuiWindow, gpuiNodeId, gpui::AccessibilityAction::SetValue,
            utf8 ? gpui::Str(utf8, (int)strlen(utf8)) : gpui::Str{});
    }
}
- (NSInteger)accessibilityNumberOfCharacters {
    const gpui::AccessibilityNode* node = GpuiAccessibilityNode(self);
    gpui::Str text =
        node ? gpui::WindowAccessibilityTextValue(*node) : gpui::Str{};
    return gpui::RopeOffsetToOffsetUtf16(text, len(text));
}
- (NSRange)accessibilitySelectedTextRange {
    const gpui::AccessibilityNode* node = GpuiAccessibilityNode(self);
    gpui::Str text =
        node ? gpui::WindowAccessibilityTextValue(*node) : gpui::Str{};
    gpui::Selection selection;
    if (!text.s || !gpui::WindowAccessibilityTextSelection(*node, &selection)) {
        return NSMakeRange(NSNotFound, 0);
    }
    int lo = gpui::RopeOffsetToOffsetUtf16(text, selection.start);
    int hi = gpui::RopeOffsetToOffsetUtf16(text, selection.end);
    return NSMakeRange((NSUInteger)std::min(lo, hi),
                       (NSUInteger)(std::max(lo, hi) - std::min(lo, hi)));
}
- (void)setAccessibilitySelectedTextRange:(NSRange)range {
    const gpui::AccessibilityNode* node = GpuiAccessibilityNode(self);
    gpui::Str text =
        node ? gpui::WindowAccessibilityTextValue(*node) : gpui::Str{};
    if (!text.s || !node->input || range.location == NSNotFound) {
        return;
    }
    int lo = gpui::RopeOffsetUtf16ToOffset(text, (int)range.location);
    int hi = gpui::RopeOffsetUtf16ToOffset(
        text, (int)(range.location + range.length));
    gpui::InputSetSelectedRange(node->input, gpuiWindow->app, gpuiWindow, lo,
                                hi);
    gpui::AppInvalidate(gpuiWindow);
}
- (NSRange)accessibilityVisibleCharacterRange {
    NSInteger length = [self accessibilityNumberOfCharacters];
    return NSMakeRange(0, (NSUInteger)length);
}
- (NSInteger)accessibilityLineForIndex:(NSInteger)index {
    const gpui::AccessibilityNode* node = GpuiAccessibilityNode(self);
    gpui::Str text =
        node ? gpui::WindowAccessibilityTextValue(*node) : gpui::Str{};
    if (!text.s || index < 0 ||
        index > gpui::RopeOffsetToOffsetUtf16(text, len(text))) {
        return NSNotFound;
    }
    int offset = gpui::RopeOffsetUtf16ToOffset(text, (int)index);
    return gpui::RopeOffsetToPoint(text, offset).row;
}
- (NSInteger)accessibilityInsertionPointLineNumber {
    const gpui::AccessibilityNode* node = GpuiAccessibilityNode(self);
    gpui::Str text =
        node ? gpui::WindowAccessibilityTextValue(*node) : gpui::Str{};
    return text.s ? gpui::RopeOffsetToPoint(
                        text, gpui::WindowAccessibilityTextCaret(*node))
                        .row
                  : NSNotFound;
}
- (NSRange)accessibilityRangeForLine:(NSInteger)line {
    const gpui::AccessibilityNode* node = GpuiAccessibilityNode(self);
    gpui::Str text =
        node ? gpui::WindowAccessibilityTextValue(*node) : gpui::Str{};
    gpui::Selection range;
    if (!text.s || line < 0 || line > INT32_MAX ||
        !gpui::AccessibilityTextLineRange(text, (int)line, &range)) {
        return NSMakeRange(NSNotFound, 0);
    }
    int start = gpui::RopeOffsetToOffsetUtf16(text, range.start);
    int end = gpui::RopeOffsetToOffsetUtf16(text, range.end);
    return NSMakeRange((NSUInteger)start, (NSUInteger)(end - start));
}
- (NSString*)accessibilityStringForRange:(NSRange)range {
    const gpui::AccessibilityNode* node = GpuiAccessibilityNode(self);
    gpui::Str text =
        node ? gpui::WindowAccessibilityTextValue(*node) : gpui::Str{};
    if (!text.s || range.location == NSNotFound) {
        return nil;
    }
    int lo = gpui::RopeOffsetUtf16ToOffset(text, (int)range.location);
    int hi = gpui::RopeOffsetUtf16ToOffset(
        text, (int)(range.location + range.length));
    return GpuiAccessibilityString(gpui::Str(text.s + lo, hi - lo));
}
- (NSRange)accessibilityRangeForPosition:(NSPoint)point {
    const gpui::AccessibilityNode* node = GpuiAccessibilityNode(self);
    gpui::Str text =
        node ? gpui::WindowAccessibilityTextValue(*node) : gpui::Str{};
    if (!text.s || !node->input || !gpuiWindow->plat ||
        !gpuiWindow->plat->view) {
        return NSMakeRange(NSNotFound, 0);
    }
    NSView* view = (NSView*)gpuiWindow->plat->view;
    NSPoint inWindow = [[view window] convertPointFromScreen:point];
    NSPoint local = [view convertPoint:inWindow fromView:nil];
    int offset = gpui::InputIndexForPosition(node->input, &gpuiWindow->paint,
                                             (float)local.x, (float)local.y);
    return NSMakeRange((NSUInteger)gpui::RopeOffsetToOffsetUtf16(text, offset),
                       0);
}
- (NSRect)accessibilityFrameForRange:(NSRange)range {
    const gpui::AccessibilityNode* node = GpuiAccessibilityNode(self);
    gpui::Str text =
        node ? gpui::WindowAccessibilityTextValue(*node) : gpui::Str{};
    if (!text.s || !node->input || range.location == NSNotFound ||
        !gpuiWindow->plat || !gpuiWindow->plat->view) {
        return NSZeroRect;
    }
    if (!gpui::InputIsSingleLine(node->input)) {
        return [self accessibilityFrame];
    }
    int lo = gpui::RopeOffsetUtf16ToOffset(text, (int)range.location);
    int hi = gpui::RopeOffsetUtf16ToOffset(
        text, (int)(range.location + range.length));
    gpui::Bounds bounds;
    if (!gpui::WindowAccessibilityTextRangeBounds(gpuiWindow, *node, {lo, hi},
                                                  &bounds)) {
        return NSZeroRect;
    }
    NSView* view = (NSView*)gpuiWindow->plat->view;
    NSRect local = NSMakeRect(bounds.x, bounds.y, bounds.w, bounds.h);
    return
        [[view window] convertRectToScreen:[view convertRect:local toView:nil]];
}

@end

// ─── the view ─────────────────────────────────────────────────────────────

namespace gpui {

// GPUI's Modifiers, out of an NSEvent's flags. `platform` is Command, and
// macOS is the one platform that reports Fn.
static Modifiers ModsOf(NSEvent* event) {
    NSEventModifierFlags f = [event modifierFlags];
    Modifiers m;
    m.control = (f & NSEventModifierFlagControl) != 0;
    m.alt = (f & NSEventModifierFlagOption) != 0;
    m.shift = (f & NSEventModifierFlagShift) != 0;
    m.platform = (f & NSEventModifierFlagCommand) != 0;
    m.function = (f & NSEventModifierFlagFunction) != 0;
    return m;
}

// An NSEvent buttonNumber as a MouseButton.
static MouseButton MouseButtonOf(NSInteger number) {
    switch (number) {
        case 0:
            return MouseButton::Left;
        case 1:
            return MouseButton::Right;
        case 2:
            return MouseButton::Middle;
        case 3:
            return MouseButton::NavigateBack;
        default:
            return MouseButton::NavigateForward;
    }
}

// Rust's Option<MouseButton> on a move: the first button currently held.
static bool PressedButton(MouseButton* out) {
    NSUInteger mask = [NSEvent pressedMouseButtons];
    for (NSInteger i = 0; i < 5; i++) {
        if (mask & (1u << i)) {
            *out = MouseButtonOf(i);
            return true;
        }
    }
    return false;
}

} // namespace gpui

@interface GpuiView : NSView <NSTextInputClient> {
  @public
    gpui::Window* win;
    __strong NSString* textContentType;
    // The drag over the view carries text or a link, not files.
    BOOL textDrag;
}
@end

// An NSTextInputClient range, in UTF-16 units of `s`, as the byte range the
// field counts in.
static gpui::Selection Utf16RangeToUtf8(gpui::Str s, NSRange r) {
    gpui::Selection out = {};
    out.start = gpui::Utf16OffsetToUtf8(s, (int)r.location);
    out.end = gpui::Utf16OffsetToUtf8(s, (int)(r.location + r.length));
    return out;
}

// The text an NSTextInputClient call hands over, an NSString or an
// NSAttributedString, as UTF-8 that lives as long as the autorelease pool.
static gpui::Str TextInputString(id string) {
    NSString* str = [string isKindOfClass:[NSAttributedString class]]
                        ? [(NSAttributedString*)string string]
                        : (NSString*)string;
    const char* utf8 = [str UTF8String];
    return utf8 ? gpui::Str(utf8, (int)strlen(utf8)) : gpui::Str{};
}

@implementation GpuiView

- (BOOL)isAccessibilityElement {
    return NO;
}
- (NSArray*)accessibilityChildren {
    return GpuiAccessibilityChildren(win, -1);
}
- (id)accessibilityHitTest:(NSPoint)point {
    if (!win) {
        return self;
    }
    NSPoint inWindow = [[self window] convertPointFromScreen:point];
    NSPoint local = [self convertPoint:inWindow fromView:nil];
    int found = -1;
    for (int i = 0; i < win->accessibility.len; i++) {
        const gpui::Bounds& b = win->accessibility[i].bounds;
        if ((float)local.x >= b.x && (float)local.x <= b.Right() &&
            (float)local.y >= b.y && (float)local.y <= b.Bottom()) {
            // The flat tree is preorder. A later containing node is the
            // deepest semantic descendant or the later painted sibling.
            found = i;
        }
    }
    return found >= 0 ? (id)GpuiAccessibilityElementFor(
                            win, win->accessibility[found].id)
                      : (id)self;
}
- (id)accessibilityFocusedUIElement {
    if (!win) {
        return self;
    }
    for (int i = 0; i < win->accessibility.len; i++) {
        const gpui::AccessibilityNode& node = win->accessibility[i];
        if (node.focusId && node.focusId == win->focusId) {
            return GpuiAccessibilityElementFor(win, node.id);
        }
    }
    return self;
}

- (BOOL)isFlipped {
    return YES;
}
// Opaque so AppKit does not fill the view with the window background before
// drawRect. A skipped identical frame draws nothing; that fill would flash.
- (BOOL)isOpaque {
    return YES;
}
- (BOOL)acceptsFirstResponder {
    return YES;
}
- (NSString*)contentType {
    return textContentType;
}
- (void)setContentType:(NSString*)value {
    textContentType = [value copy];
}
- (BOOL)acceptsFirstMouse:(NSEvent*)event {
    (void)event;
    return YES;
}
- (BOOL)mouseDownCanMoveWindow {
    // Client title bars decide which pixels drag. Without this, AppKit also
    // moves a full-size-content window when a menu or tool button is dragged.
    return NO;
}

// Finder file drags. Paths are read on enter and again on drop: a promise
// can arrive without names until the drop. The kind is ExternalPaths.
- (gpui::Str)fileDropPaths:(id<NSDraggingInfo>)sender {
    NSArray<NSURL*>* urls = [[sender draggingPasteboard]
        readObjectsForClasses:@[ [NSURL class] ]
                      options:@{NSPasteboardURLReadingFileURLsOnlyKey : @YES}];
    gpui::Arena* scratch = gpui::ArenaNew();
    gpui::StrBuilder b(scratch);
    for (NSURL* url in urls) {
        const char* path = url.path.UTF8String;
        if (!path || !path[0]) {
            continue;
        }
        if (b.len > 0) {
            b.AppendChar('\n');
        }
        b.Append(gpui::Str(path));
    }
    gpui::Str out = b.len > 0 ? gpui::StrDup(b.TakeStr()) : gpui::Str{};
    gpui::ArenaDelete(scratch);
    return out;
}

// Not in Rust: a drag of text or a link (a browser's image) and no files is
// an ExternalText drag. Returns false when it has neither.
- (bool)textDrop:(id<NSDraggingInfo>)sender at:(NSPoint)p {
    NSPasteboard* pb = [sender draggingPasteboard];
    NSString* text = [pb stringForType:NSPasteboardTypeString];
    NSString* url = nil;
    NSArray<NSURL*>* urls =
        [pb readObjectsForClasses:@[ [NSURL class] ] options:nil];
    for (NSURL* u in urls) {
        if (![u isFileURL]) {
            url = [u absoluteString];
            break;
        }
    }
    const char* t = text.UTF8String;
    const char* u = url.UTF8String;
    if ((!t || !t[0]) && (!u || !u[0])) {
        return false;
    }
    gpui::PlatformInput in = gpui::InputTextDrop(
        gpui::FileDropPhase::Entered, (float)p.x, (float)p.y,
        t ? gpui::Str(t) : gpui::Str{}, u ? gpui::Str(u) : gpui::Str{});
    gpui::WindowDispatchInput(win, &in);
    return true;
}

- (void)fileDrop:(id<NSDraggingInfo>)sender phase:(gpui::FileDropPhase)phase {
    if (!win) {
        return;
    }
    NSPoint p = [self convertPoint:[sender draggingLocation] fromView:nil];
    gpui::Str paths = {};
    if (phase == gpui::FileDropPhase::Entered) {
        paths = [self fileDropPaths:sender];
        textDrag = gpui::len(paths) == 0 && [self textDrop:sender at:p];
        if (textDrag) {
            return;
        }
    }
    gpui::PlatformInput in =
        gpui::InputFileDrop(phase, (float)p.x, (float)p.y, paths);
    gpui::WindowDispatchInput(win, &in);
    gpui::StrFree(paths);
}

// Files are always offered a copy, as in Rust; text only over a taker.
- (NSDragOperation)dropOperation {
    if (!textDrag) {
        return NSDragOperationCopy;
    }
    bool taken = win && win->fileDrop && win->dragOverId != 0;
    return taken ? NSDragOperationCopy : NSDragOperationNone;
}

- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender {
    [self fileDrop:sender phase:gpui::FileDropPhase::Entered];
    return [self dropOperation];
}
- (NSDragOperation)draggingUpdated:(id<NSDraggingInfo>)sender {
    [self fileDrop:sender phase:gpui::FileDropPhase::Pending];
    return [self dropOperation];
}
- (void)draggingExited:(id<NSDraggingInfo>)sender {
    [self fileDrop:sender phase:gpui::FileDropPhase::Exited];
}
- (BOOL)prepareForDragOperation:(id<NSDraggingInfo>)sender {
    (void)sender;
    return YES;
}
- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
    // Names that were not on the pasteboard at enter are here now.
    [self fileDrop:sender phase:gpui::FileDropPhase::Entered];
    [self fileDrop:sender phase:gpui::FileDropPhase::Submit];
    return YES;
}

- (void)drawRect:(NSRect)dirty {
    (void)dirty;
    if (!win) {
        return;
    }
    CGContextRef cg =
        (CGContextRef)[[NSGraphicsContext currentContext] CGContext];
    NSRect b = [self bounds];
    NSRect px = [self convertRectToBacking:b];
    win->paint.dpi = 96;
    gpui::WindowDrawFrame(win, cg, (int)px.size.width, (int)px.size.height,
                          (float)b.size.width, (float)b.size.height);
}

// A tracking area is what turns on mouseMoved / mouseExited.
- (void)updateTrackingAreas {
    for (NSTrackingArea* a in [self trackingAreas]) {
        [self removeTrackingArea:a];
    }
    NSTrackingAreaOptions opts =
        NSTrackingMouseMoved | NSTrackingMouseEnteredAndExited |
        NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect;
    NSTrackingArea* area = [[NSTrackingArea alloc] initWithRect:[self bounds]
                                                        options:opts
                                                          owner:self
                                                       userInfo:nil];
    [self addTrackingArea:area];
    [super updateTrackingAreas];
}

- (NSPoint)gpuiPoint:(NSEvent*)event {
    return [self convertPoint:[event locationInWindow] fromView:nil];
}

- (void)mouseMoved:(NSEvent*)event {
    NSPoint p = [self gpuiPoint:event];
    gpui::MouseButton held = gpui::MouseButton::Left;
    bool any = gpui::PressedButton(&held);
    gpui::PlatformInput in = gpui::InputMouseMove((float)p.x, (float)p.y, any,
                                                  held, gpui::ModsOf(event));
    gpui::WindowDispatchInput(win, &in);
}
- (void)mouseDragged:(NSEvent*)event {
    [self mouseMoved:event];
}
- (void)rightMouseDragged:(NSEvent*)event {
    [self mouseMoved:event];
}
- (void)otherMouseDragged:(NSEvent*)event {
    [self mouseMoved:event];
}
- (void)mouseExited:(NSEvent*)event {
    NSPoint p = [self gpuiPoint:event];
    gpui::MouseButton held = gpui::MouseButton::Left;
    bool any = gpui::PressedButton(&held);
    gpui::PlatformInput in = gpui::InputMouseExited((float)p.x, (float)p.y, any,
                                                    held, gpui::ModsOf(event));
    gpui::WindowDispatchInput(win, &in);
}

- (void)mouseDown:(NSEvent*)event {
    NSPoint p = [self gpuiPoint:event];
    float x = (float)p.x;
    float y = (float)p.y;
    // Counted before the chrome is claimed: the caption drags on the first
    // press and zooms on the second, so the chrome needs the answer.
    int clicks = gpui::WindowClickCount(win, x, y, gpui::MouseButton::Left);
    // The custom chrome is claimed before the element tree sees the press,
    // the way WM_NCHITTEST takes it on Windows.
    int chrome = gpui::WindowChromeHit(win, x, y);
    if (chrome == gpui::ClickWinMin) {
        gpui::AppMinimize(win);
        return;
    }
    if (chrome == gpui::ClickWinMax) {
        gpui::AppToggleMaximize(win);
        return;
    }
    if (chrome == gpui::ClickWinClose) {
        gpui::AppClose(win);
        return;
    }
    if (chrome == gpui::ClickWinCaption) {
        if (clicks == 2) {
            gpui::AppToggleMaximize(win);
            return;
        }
        [[self window] performWindowDragWithEvent:event];
        return;
    }
    // first_mouse would be the press that activated the window; a Cocoa view
    // does not accept the first mouse, so that press never reaches here.
    gpui::PlatformInput in = gpui::InputMouseDown(
        gpui::MouseButton::Left, x, y, gpui::ModsOf(event), clicks, false);
    gpui::WindowDispatchInput(win, &in);
}

- (void)press:(NSEvent*)event button:(gpui::MouseButton)button {
    NSPoint p = [self gpuiPoint:event];
    float x = (float)p.x;
    float y = (float)p.y;
    gpui::PlatformInput in =
        gpui::InputMouseDown(button, x, y, gpui::ModsOf(event),
                             gpui::WindowClickCount(win, x, y, button), false);
    gpui::WindowDispatchInput(win, &in);
}

- (void)release:(NSEvent*)event button:(gpui::MouseButton)button {
    NSPoint p = [self gpuiPoint:event];
    gpui::PlatformInput in =
        gpui::InputMouseUp(button, (float)p.x, (float)p.y, gpui::ModsOf(event),
                           gpui::WindowCurrentClickCount(win));
    gpui::WindowDispatchInput(win, &in);
}

- (void)mouseUp:(NSEvent*)event {
    [self release:event button:gpui::MouseButton::Left];
}

- (void)rightMouseDown:(NSEvent*)event {
    [self press:event button:gpui::MouseButton::Right];
}

- (void)rightMouseUp:(NSEvent*)event {
    [self release:event button:gpui::MouseButton::Right];
}

// Everything that is not the left or right button: 2 is the middle one, 3 and
// 4 are the thumb buttons GPUI calls MouseButton::Navigate.
- (void)otherMouseDown:(NSEvent*)event {
    [self press:event button:gpui::MouseButtonOf([event buttonNumber])];
}

- (void)otherMouseUp:(NSEvent*)event {
    [self release:event button:gpui::MouseButtonOf([event buttonNumber])];
}

- (void)scrollWheel:(NSEvent*)event {
    NSPoint p = [self gpuiPoint:event];
    // A line of scroll is WheelNotchPixels(), the step the other two windows
    // use; a precise (trackpad) delta is already in points, which is GPUI's
    // ScrollDelta::Pixels.
    bool precise = [event hasPreciseScrollingDeltas];
    float scale = precise ? 1.f : gpui::WheelNotchPixels(win->app);
    float dx = (float)[event scrollingDeltaX] * scale;
    float dy = (float)[event scrollingDeltaY] * scale;
    gpui::TouchPhase phase = gpui::TouchPhase::Moved;
    NSEventPhase ph = [event phase];
    if (ph & NSEventPhaseBegan) {
        phase = gpui::TouchPhase::Started;
    } else if (ph & NSEventPhaseEnded) {
        phase = gpui::TouchPhase::Ended;
    } else if (ph & NSEventPhaseCancelled) {
        phase = gpui::TouchPhase::Cancelled;
    }
    gpui::PlatformInput in = gpui::InputScrollWheel(
        (float)p.x, (float)p.y, dx, dy, precise, gpui::ModsOf(event), phase);
    gpui::WindowDispatchInput(win, &in);
}

// GPUI's NSEventType::Magnify: a trackpad pinch
- (void)magnifyWithEvent:(NSEvent*)event {
    NSPoint p = [self gpuiPoint:event];
    gpui::TouchPhase phase = gpui::TouchPhase::Moved;
    NSEventPhase ph = [event phase];
    if (ph & (NSEventPhaseMayBegin | NSEventPhaseBegan)) {
        phase = gpui::TouchPhase::Started;
    } else if (ph & NSEventPhaseEnded) {
        phase = gpui::TouchPhase::Ended;
    }
    gpui::PlatformInput in =
        gpui::InputPinch((float)p.x, (float)p.y, (float)[event magnification],
                         gpui::ModsOf(event), phase);
    gpui::WindowDispatchInput(win, &in);
}

- (void)keyDown:(NSEvent*)event {
    // The keys are the window's, as they have always been. The *text* half
    // goes to the input method when a field has the keyboard: that is what
    // makes marked text possible, and it is what GPUI's own view does.
    if (gpui::WindowMacKeyDown(win, event)) {
        [self interpretKeyEvents:@[ event ]];
    }
}

// --- NSTextInputClient ---------------------------------------------------
//
// The focused InputState is the document, the way GPUI answers these against
// the window's focused EntityInputHandler. Cocoa counts in UTF-16 and the
// field counts in bytes, so every range crosses through Utf16 helpers.

- (gpui::InputState*)gpuiInput {
    gpui::InputState* in = win ? win->input : nullptr;
    return (in && in->focused) ? in : nullptr;
}

- (BOOL)hasMarkedText {
    gpui::InputState* in = [self gpuiInput];
    return (in && gpui::InputMarkedRange(in, nullptr)) ? YES : NO;
}

- (NSRange)markedRange {
    gpui::InputState* in = [self gpuiInput];
    gpui::Selection m = {};
    if (!in || !gpui::InputMarkedRange(in, &m)) {
        return NSMakeRange(NSNotFound, 0);
    }
    gpui::Str text = gpui::InputValue(in);
    NSUInteger lo = (NSUInteger)gpui::Utf8OffsetToUtf16(text, m.start);
    NSUInteger hi = (NSUInteger)gpui::Utf8OffsetToUtf16(text, m.end);
    return NSMakeRange(lo, hi - lo);
}

- (NSRange)selectedRange {
    gpui::InputState* in = [self gpuiInput];
    if (!in) {
        return NSMakeRange(NSNotFound, 0);
    }
    gpui::Str text = gpui::InputValue(in);
    NSUInteger lo =
        (NSUInteger)gpui::Utf8OffsetToUtf16(text, in->selectedRange.start);
    NSUInteger hi =
        (NSUInteger)gpui::Utf8OffsetToUtf16(text, in->selectedRange.end);
    return NSMakeRange(lo, hi - lo);
}

// EntityInputHandler::set_selected_text_range. The IME and the system
// selection handles count in UTF-16 and ask the field to follow.
- (void)setSelectedRange:(NSRange)range {
    gpui::InputState* in = [self gpuiInput];
    if (!in || range.location == NSNotFound) {
        return;
    }
    gpui::InputSetSelectedTextRangeUtf16(in, win->app, win, (int)range.location,
                                         (int)(range.location + range.length));
}

- (void)setMarkedText:(id)string
        selectedRange:(NSRange)selected
     replacementRange:(NSRange)replacement {
    gpui::InputState* in = [self gpuiInput];
    if (!in) {
        return;
    }
    gpui::Str text = TextInputString(string);
    gpui::Str doc = gpui::InputValue(in);
    gpui::Selection range = {};
    bool hasRange = replacement.location != NSNotFound;
    if (hasRange) {
        range = Utf16RangeToUtf8(doc, replacement);
    }
    // The selection Cocoa reports is inside the new text, in UTF-16 units.
    gpui::Selection sel = {};
    bool hasSel = selected.location != NSNotFound;
    if (hasSel) {
        sel = Utf16RangeToUtf8(text, selected);
    }
    gpui::InputReplaceAndMarkText(in, win->app, win,
                                  hasRange ? &range : nullptr, text,
                                  hasSel ? &sel : nullptr);
    gpui::AppInvalidate(win);
}

- (void)unmarkText {
    gpui::InputState* in = [self gpuiInput];
    if (in) {
        gpui::InputUnmarkText(in, win->app, win);
        gpui::AppInvalidate(win);
    }
}

- (void)insertText:(id)string replacementRange:(NSRange)replacement {
    gpui::InputState* in = [self gpuiInput];
    if (!in) {
        return;
    }
    gpui::Str text = TextInputString(string);
    if (len(text) == 0) {
        // An empty commit ends the composition without leaving anything.
        gpui::InputReplaceAndMarkText(in, win->app, win, nullptr, gpui::Str{},
                                      nullptr);
        gpui::AppInvalidate(win);
        return;
    }
    gpui::Str doc = gpui::InputValue(in);
    gpui::Selection range = {};
    bool hasRange = replacement.location != NSNotFound;
    if (hasRange) {
        range = Utf16RangeToUtf8(doc, replacement);
    }
    // A null range is the marked run, which is what a commit replaces.
    gpui::InputReplaceTextInRange(in, win->app, win,
                                  hasRange ? &range : nullptr, text);
    gpui::AppInvalidate(win);
}

- (void)doCommandBySelector:(SEL)selector {
    // Every command key — Return, Tab, the arrows, Backspace — has already
    // gone through WindowKeyDown, which is where this port binds them. Taking
    // them again here would run them twice, and letting NSView have them
    // would make Cocoa beep.
    (void)selector;
}

- (NSAttributedString*)attributedSubstringForProposedRange:(NSRange)range
                                               actualRange:
                                                   (NSRangePointer)actual {
    gpui::InputState* in = [self gpuiInput];
    if (!in || range.location == NSNotFound) {
        return nil;
    }
    gpui::Str doc = gpui::InputValue(in);
    int lo = gpui::Utf16OffsetToUtf8(doc, (int)range.location);
    int hi = gpui::Utf16OffsetToUtf8(doc, (int)(range.location + range.length));
    if (lo < 0 || hi > len(doc) || hi < lo) {
        return nil;
    }
    if (actual) {
        *actual = range;
    }
    NSString* str = [[NSString alloc] initWithBytes:doc.s + lo
                                             length:(NSUInteger)(hi - lo)
                                           encoding:NSUTF8StringEncoding];
    return str ? [[NSAttributedString alloc] initWithString:str] : nil;
}

- (NSArray<NSAttributedStringKey>*)validAttributesForMarkedText {
    return @[];
}

- (NSRect)firstRectForCharacterRange:(NSRange)range
                         actualRange:(NSRangePointer)actual {
    gpui::InputState* in = [self gpuiInput];
    if (!in) {
        return NSZeroRect;
    }
    gpui::Str text = gpui::InputValue(in);
    int cursor = gpui::InputCursor(in);
    gpui::Selection bytes = {cursor, cursor};
    if (range.location != NSNotFound) {
        NSUInteger length =
            (NSUInteger)gpui::Utf8OffsetToUtf16(text, len(text));
        NSUInteger start = std::min(range.location, length);
        NSUInteger count = std::min(range.length, length - start);
        bytes = {gpui::Utf16OffsetToUtf8(text, (int)start),
                 gpui::Utf16OffsetToUtf8(text, (int)(start + count))};
        if (actual) {
            *actual = NSMakeRange(start, count);
        }
    } else if (actual) {
        *actual =
            NSMakeRange((NSUInteger)gpui::Utf8OffsetToUtf16(text, cursor), 0);
    }
    gpui::Bounds candidate;
    if (!gpui::InputImeCandidateBounds(in, win, bytes, &candidate)) {
        return NSZeroRect;
    }
    NSRect local =
        NSMakeRect(candidate.x, candidate.y, candidate.w, candidate.h);
    NSRect inWindow = [self convertRect:local toView:nil];
    return [[self window] convertRectToScreen:inWindow];
}

- (NSUInteger)characterIndexForPoint:(NSPoint)point {
    gpui::InputState* in = [self gpuiInput];
    if (!in || ![self window]) {
        return NSNotFound;
    }
    NSPoint inWindow = [[self window] convertPointFromScreen:point];
    NSPoint local = [self convertPoint:inWindow fromView:nil];
    int hit = gpui::InputCharacterIndexForPoint(
        in, win, {(float)local.x, (float)local.y});
    return hit >= 0 ? (NSUInteger)hit : NSNotFound;
}

- (void)keyUp:(NSEvent*)event {
    gpui::WindowMacKeyUp(win, event);
}

// Command chords belong to the menu bar first: ⌘Q is Quit, not a letter.
// NSApplication asks the key window before the main menu, and this view
// used to return NO without offering the event on — the custom event loop
// never asked the menu a second time, so Quit never ran.
- (BOOL)performKeyEquivalent:(NSEvent*)event {
    if ([[NSApp mainMenu] performKeyEquivalent:event]) {
        return YES;
    }
    return NO;
}

@end

// ─── the window delegate ──────────────────────────────────────────────────

@interface GpuiWindowDelegate : NSObject <NSWindowDelegate> {
  @public
    gpui::Window* win;
}
@end

@implementation GpuiWindowDelegate

- (BOOL)windowShouldClose:(NSWindow*)sender {
    (void)sender;
    return gpui::WindowShouldClose(win) ? YES : NO;
}

- (void)windowWillClose:(NSNotification*)note {
    (void)note;
    __attribute__((objc_precise_lifetime)) GpuiWindowDelegate* keepAlive = self;
    gpui::PlatWindow* plat = win ? win->plat : nullptr;
    if (plat) {
        [plat->moveEndPoll invalidate];
        plat->moveEndPoll = nil;
        for (GpuiAccessibilityElement* element in
             [plat->accessibilityElements allValues]) {
            element->gpuiWindow = nullptr;
        }
        plat->accessibilityElements = nil;
        if (plat->view) {
            plat->view->win = nullptr;
        }
    }
    gpui::WindowClosed(win);
    delete plat;
    (void)keepAlive;
}

- (void)windowDidBecomeKey:(NSNotification*)note {
    (void)note;
    gpui::WindowSetActive(win, true);
}

- (void)windowDidResignKey:(NSNotification*)note {
    (void)note;
    gpui::WindowSetActive(win, false);
}

- (void)windowDidResize:(NSNotification*)note {
    (void)note;
    gpui::AppInvalidate(win);
    gpui::WindowBoundsNotify(win, gpui::WindowBoundsEvent::Changed);
}

- (void)windowDidEndLiveResize:(NSNotification*)note {
    (void)note;
    gpui::WindowBoundsNotify(win, gpui::WindowBoundsEvent::MoveResizeEnded);
}

- (void)windowDidMove:(NSNotification*)note {
    (void)note;
    gpui::WindowBoundsNotify(win, gpui::WindowBoundsEvent::Changed);
    gpui::PlatWindow* pw = win ? win->plat : nullptr;
    if (!pw || pw->moveEndPoll || [NSEvent pressedMouseButtons] == 0) {
        return;
    }
    gpui::Window* w = win;
    pw->moveEndPoll = [NSTimer
        scheduledTimerWithTimeInterval:0.05
                               repeats:YES
                                 block:^(NSTimer* timer) {
                                   if ([NSEvent pressedMouseButtons] != 0) {
                                       return;
                                   }
                                   [timer invalidate];
                                   if (!w->plat) {
                                       return;
                                   }
                                   w->plat->moveEndPoll = nil;
                                   gpui::WindowBoundsNotify(
                                       w, gpui::WindowBoundsEvent::
                                              MoveResizeEnded);
                                 }];
}

- (void)windowDidMiniaturize:(NSNotification*)note {
    (void)note;
    if (win) {
        win->minimized = true;
    }
}

- (void)windowDidDeminiaturize:(NSNotification*)note {
    (void)note;
    if (win) {
        win->minimized = false;
        gpui::AppInvalidate(win);
    }
}

- (void)windowDidChangeBackingProperties:(NSNotification*)note {
    (void)note;
    gpui::AppInvalidate(win);
}

@end

namespace gpui {

// ─── keys ─────────────────────────────────────────────────────────────────

static int KeyFor(unichar c) {
    switch (c) {
        case NSUpArrowFunctionKey:
            return KeyUp;
        case NSDownArrowFunctionKey:
            return KeyDown;
        case NSLeftArrowFunctionKey:
            return KeyLeft;
        case NSRightArrowFunctionKey:
            return KeyRight;
        case NSHomeFunctionKey:
            return KeyHome;
        case NSEndFunctionKey:
            return KeyEnd;
        case NSPageUpFunctionKey:
            return KeyPageUp;
        case NSPageDownFunctionKey:
            return KeyPageDown;
        // Rust GPUI observes the Insert key as NSHelpFunctionKey rather than
        // NSInsertFunctionKey on macOS.
        case NSHelpFunctionKey:
            return KeyInsert;
        case NSDeleteFunctionKey:
            return KeyDelete;
        case 0x7f: // the key labelled Delete on a Mac keyboard
            return KeyBack;
        case '\r':
        case 0x03:
            return KeyReturn;
        case '\t':
        case 0x19: // back-tab, what Shift-Tab produces
            return KeyTab;
        case 0x1b:
            return KeyEscape;
        case ' ':
            return KeySpace;
        case '[':
        case '{':
            return KeyLeftBracket;
        case ']':
        case '}':
            return KeyRightBracket;
        case '-':
        case '_':
            return KeyMinus;
        case '=':
        case '+':
            return KeyEqual;
        case '\\':
        case '|':
            return KeyBackslash;
        case ';':
        case ':':
            return KeySemicolon;
        case '\'':
        case '"':
            return KeyQuote;
        case ',':
        case '<':
            return KeyComma;
        case '.':
        case '>':
            return KeyPeriod;
        case '/':
        case '?':
            return KeySlash;
        case '`':
        case '~':
            return KeyBacktick;
        case '!':
            return '1';
        case '@':
            return '2';
        case '#':
            return '3';
        case '$':
            return '4';
        case '%':
            return '5';
        case '^':
            return '6';
        case '&':
            return '7';
        case '*':
            return '8';
        case '(':
            return '9';
        case ')':
            return '0';
        default:
            break;
    }
    if (c >= NSF1FunctionKey && c <= NSF24FunctionKey) {
        return KeyF1 + (int)(c - NSF1FunctionKey);
    }
    if (c >= NSF25FunctionKey && c <= NSF35FunctionKey) {
        return KeyF25 + (int)(c - NSF25FunctionKey);
    }
    if (c >= 'a' && c <= 'z') {
        return (int)(c - 'a') + 'A';
    }
    if (c >= 'A' && c <= 'Z') {
        return (int)c;
    }
    if (c >= '0' && c <= '9') {
        return (int)c;
    }
    return 0;
}

// Apple's hardware codes keep keypad digits distinct even though
// charactersIgnoringModifiers spells them like the digit row.
static int KeyForEvent(NSEvent* event, unichar c) {
    switch ([event keyCode]) {
        case 82:
            return KeyNumpad0;
        case 83:
        case 84:
        case 85:
        case 86:
        case 87:
        case 88:
        case 89:
            return KeyNumpad0 + (int)[event keyCode] - 82;
        case 91:
        case 92:
            return KeyNumpad0 + (int)[event keyCode] - 83;
        default:
            return KeyFor(c);
    }
}

// The release of a key. Only the activation keys do anything with one, and
// they need no text, so this is the modifier and code half of the press.
void WindowMacKeyUp(Window* win, NSEvent* event) {
    if (!win) {
        return;
    }
    NSEventModifierFlags mods = [event modifierFlags];
    NSString* bare = [event charactersIgnoringModifiers];
    unichar first = [bare length] > 0 ? [bare characterAtIndex:0] : 0;
    int key = KeyForEvent(event, first);
    if (!key) {
        return;
    }
    bool function =
        (mods & NSEventModifierFlagFunction) != 0 &&
        !(first >= NSUpArrowFunctionKey && first <= NSModeSwitchFunctionKey);
    WindowKeyUp(win, key, (mods & NSEventModifierFlagShift) != 0,
                (mods & NSEventModifierFlagControl) != 0,
                (mods & NSEventModifierFlagOption) != 0,
                (mods & NSEventModifierFlagCommand) != 0, function);
}

// True when the caller should hand the event to the input method for its
// text: a field has the keyboard and this keystroke was not a chord the
// window took for itself.
bool WindowMacKeyDown(Window* win, NSEvent* event) {
    if (!win) {
        return false;
    }
    NSEventModifierFlags mods = [event modifierFlags];
    bool shift = (mods & NSEventModifierFlagShift) != 0;
    // Command and Control are two modifiers, not one. state.rs binds
    // ctrl-backspace and cmd-backspace to different actions in the same
    // context and ctrl-cmd-space to a third, so folding them here would lose
    // one of each pair. `secondary-` in a binding is the one that means
    // Command on this platform.
    bool ctrl = (mods & NSEventModifierFlagControl) != 0;
    bool platform = (mods & NSEventModifierFlagCommand) != 0;
    bool alt = (mods & NSEventModifierFlagOption) != 0;

    NSString* bare = [event charactersIgnoringModifiers];
    unichar first = [bare length] > 0 ? [bare characterAtIndex:0] : 0;
    bool function =
        (mods & NSEventModifierFlagFunction) != 0 &&
        !(first >= NSUpArrowFunctionKey && first <= NSModeSwitchFunctionKey);
    int key = KeyForEvent(event, first);
    if (key) {
        // gpui_macos events.rs: `is_held: native_event.isARepeat()`. A key
        // that lands while the focused field has marked text belongs to the
        // input method's composition (Keystroke::is_ime_in_progress).
        KeyDownFlags flags;
        flags.held = [event isARepeat] == YES;
        InputState* in = win->input;
        flags
            .imeInProgress = in && in->focused && InputMarkedRange(in, nullptr);
        WindowKeyDown(win, key, shift, ctrl, alt, platform, function, flags);
    }
    // Backspace arrives as a key only; the bound InputState edits on the
    // control code the Windows window delivers through WM_CHAR.
    if (key == KeyBack) {
        WindowChar(win, 8, ctrl, alt);
        return false;
    }
    // `platform` belongs in this list as much as ctrl does: Cmd-C is a chord,
    // not a "c" to type. It used to be covered because Command landed on
    // ctrl; now it has to say so.
    if (ctrl || alt || platform || key == KeyReturn || key == KeyTab ||
        key == KeyEscape) {
        return false;
    }
    // A focused field's text is the input method's to deliver, through
    // insertText: and setMarkedText:. Everywhere else the codepoints go
    // straight through, which is what the window's own key listener reads.
    if (win->input && win->input->focused) {
        return true;
    }
    NSString* text = [event characters];
    NSUInteger n = [text length];
    for (NSUInteger i = 0; i < n; i++) {
        unichar u = [text characterAtIndex:i];
        uint32_t cp = u;
        // Recombine a surrogate pair before handing over a codepoint.
        if (u >= 0xd800 && u <= 0xdbff && i + 1 < n) {
            unichar lo = [text characterAtIndex:i + 1];
            if (lo >= 0xdc00 && lo <= 0xdfff) {
                cp = 0x10000 + ((uint32_t)(u - 0xd800) << 10) + (lo - 0xdc00);
                i++;
            }
        }
        if (cp >= 32 && cp != 0x7f) {
            WindowChar(win, cp, ctrl, alt);
        }
    }
    return false;
}

// ─── drawing ──────────────────────────────────────────────────────────────

// A Floating or Dialog window goes above its parent as it is shown.
static void AttachToParent(Window* win) {
    bool keeps = win->opts.kind == WindowKind::Floating ||
                 win->opts.kind == WindowKind::Dialog;
    Window* parent = win->opts.parent;
    if (!keeps || !parent || !parent->plat) {
        return;
    }
    NSWindow* w = win->plat->window;
    NSWindow* p = parent->plat->window;
    if (w.parentWindow != p) {
        [w.parentWindow removeChildWindow:w];
        [p addChildWindow:w ordered:NSWindowAbove];
    }
}

static void OrderFront(Window* win, bool activate) {
    PlatWindow* pw = win->plat;
    AttachToParent(win);
    if (!activate || !win->opts.activatable) {
        [pw->window orderFront:nil];
        return;
    }
    [pw->window makeFirstResponder:pw->view];
    [pw->window makeKeyAndOrderFront:nil];
}

static void OrderFrontOnce(Window* win) {
    PlatWindow* pw = win->plat;
    if (!pw || pw->ordered || pw->hidden) {
        return;
    }
    pw->ordered = true;
    OrderFront(win, pw->activateOnShow);
    if (pw->activateOnShow && win->opts.activatable) {
        [NSApp activateIgnoringOtherApps:YES];
    }
}

static void Redraw(Window* win) {
    PlatWindow* pw = win->plat;
    if (!pw || !pw->view) {
        return;
    }
    win->dirty = false;
    win->maximized = [pw->window isZoomed] ? true : false;
    [pw->view display];
    OrderFrontOnce(win);
}

// ─── window commands ──────────────────────────────────────────────────────

void AppQuit(Window* win) {
    if (win && win->plat && win->plat->window) {
        WindowModalRelease(win);
        // windowWillClose is what calls WindowClosed.
        [win->plat->window close];
    }
}

bool WindowClientDecorated(Window* win) {
    // The traffic lights are Cocoa's over a transparent title bar, and
    // component::TitleBar draws no controls of its own here.
    return win && (win->opts.clientTitleBar || win->opts.borderless);
}

float WindowScaleFactor(Window* win) {
    if (!win || !win->plat || !win->plat->window) {
        return 1;
    }
    float scale = (float)[win->plat->window backingScaleFactor];
    return scale > 0 ? scale : 1;
}

// --- WindowCapturePngTemp ---------------------------------------------------

constexpr double kCaptureWebSnapshotTimeoutSec = 5;

static void CaptureCollectWebViews(NSView* view,
                                   NSMutableArray<WKWebView*>* out) {
    for (NSView* sub in view.subviews) {
        if ([sub isKindOfClass:[WKWebView class]]) {
            [out addObject:(WKWebView*)sub];
            continue;
        }
        CaptureCollectWebViews(sub, out);
    }
}

// The view as it is on screen, at its backing scale; drawRect: runs into the
// bitmap. Its web views and their screen rects go to `webs` / `webRects`.
static NSBitmapImageRep* CaptureView(NSView* view,
                                     NSMutableArray<WKWebView*>* webs,
                                     NSMutableArray<NSValue*>* webRects) {
    NSRect b = view.bounds;
    if (b.size.width <= 0 || b.size.height <= 0) {
        return nil;
    }
    NSBitmapImageRep* rep = [view bitmapImageRepForCachingDisplayInRect:b];
    if (!rep) {
        return nil;
    }
    [view cacheDisplayInRect:b toBitmapImageRep:rep];
    NSMutableArray<WKWebView*>* found = [NSMutableArray array];
    CaptureCollectWebViews(view, found);
    for (WKWebView* web in found) {
        if (web.hiddenOrHasHiddenAncestor || web.bounds.size.width <= 0 ||
            web.bounds.size.height <= 0) {
            continue;
        }
        NSRect r = [web convertRect:web.bounds toView:nil];
        [webs addObject:web];
        [webRects addObject:[NSValue valueWithRect:[web.window
                                                       convertRectToScreen:r]]];
    }
    return rep;
}

static bool CaptureWritePng(NSBitmapImageRep* rep, NSString* dst) {
    NSData* png =
        [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
    return png && [png writeToFile:dst atomically:YES];
}

// A WKWebView renders out of process and is blank in cacheDisplayInRect:, so
// its own snapshot is drawn over it. Snapshots arrive on the main queue after
// the capture returns: the PNG is written once the last one is in.
static void CaptureDrawWebViewsThenWrite(NSBitmapImageRep* out,
                                         NSPoint outOrigin,
                                         NSArray<WKWebView*>* webs,
                                         NSArray<NSValue*>* webRects,
                                         NSString* dst) {
    __block NSUInteger left = webs.count;
    __block bool written = false;
    void (^finish)(void) = ^{
      if (!written) {
          written = true;
          CaptureWritePng(out, dst);
      }
    };
    for (NSUInteger i = 0; i < webs.count; i++) {
        NSRect r = webRects[i].rectValue;
        r.origin.x -= outOrigin.x;
        r.origin.y -= outOrigin.y;
        [webs[i]
            takeSnapshotWithConfiguration:nil
                        completionHandler:^(NSImage* img, NSError*) {
                          if (img && !written) {
                              NSGraphicsContext* gc = [NSGraphicsContext
                                  graphicsContextWithBitmapImageRep:out];
                              [NSGraphicsContext saveGraphicsState];
                              [NSGraphicsContext setCurrentContext:gc];
                              [img drawInRect:r
                                     fromRect:NSZeroRect
                                    operation:NSCompositingOperationSourceOver
                                     fraction:1];
                              [NSGraphicsContext restoreGraphicsState];
                          }
                          if (--left == 0) {
                              finish();
                          }
                        }];
    }
    // A snapshot that never comes still leaves a file.
    dispatch_after(
        dispatch_time(DISPATCH_TIME_NOW,
                      (int64_t)(kCaptureWebSnapshotTimeoutSec * NSEC_PER_SEC)),
        dispatch_get_main_queue(), finish);
}

// The windows to capture, back to front: `main`, then with `all` the app's
// other visible windows (tool windows, panels, ...).
static NSArray<NSWindow*>* CaptureWindowList(NSWindow* main, bool all) {
    if (!all) {
        return @[ main ];
    }
    NSMutableArray<NSWindow*>* out = [NSMutableArray array];
    for (NSWindow* w in [[NSApp orderedWindows] reverseObjectEnumerator]) {
        if (!w.isVisible || !w.contentView || w.isMiniaturized) {
            continue;
        }
        if (w != main && w.parentWindow != main &&
            w.level < NSNormalWindowLevel) {
            continue;
        }
        [out addObject:w];
    }
    if (![out containsObject:main]) {
        [out insertObject:main atIndex:0];
    }
    return out;
}

// `win`'s content view (no title bar); other windows with their frame, which
// the content view's superview draws.
TempStr WindowCapturePngTemp(Window* win, Str path, bool allWindows) {
    NSView* mainView = (win && win->plat) ? win->plat->view : nil;
    if (!mainView || !mainView.window) {
        return StrL("ERR no-window");
    }
    if (len(path) == 0) {
        return StrL("ERR no-path");
    }
    @autoreleasepool {
        NSArray<NSWindow*>* wins =
            CaptureWindowList(mainView.window, allWindows);
        float scale = WindowScaleFactor(win);

        // each view and where it is on screen (points, bottom-up)
        NSMutableArray<NSBitmapImageRep*>* reps = [NSMutableArray array];
        NSMutableArray<NSValue*>* rects = [NSMutableArray array];
        NSRect total = NSZeroRect;
        NSMutableArray<WKWebView*>* webs = [NSMutableArray array];
        NSMutableArray<NSValue*>* webRects = [NSMutableArray array];
        for (NSWindow* w in wins) {
            NSView* v = w == mainView.window
                            ? mainView
                            : (w.contentView.superview ?: w.contentView);
            NSBitmapImageRep* rep = CaptureView(v, webs, webRects);
            if (!rep) {
                continue;
            }
            NSRect r =
                [w convertRectToScreen:[v convertRect:v.bounds toView:nil]];
            [reps addObject:rep];
            [rects addObject:[NSValue valueWithRect:r]];
            total = reps.count == 1 ? r : NSUnionRect(total, r);
        }
        if (reps.count == 0) {
            return StrL("ERR empty");
        }

        NSBitmapImageRep* out = reps[0];
        if (reps.count > 1) {
            NSInteger pxW = (NSInteger)ceil(total.size.width * scale);
            NSInteger pxH = (NSInteger)ceil(total.size.height * scale);
            out = [[NSBitmapImageRep alloc]
                initWithBitmapDataPlanes:nullptr
                              pixelsWide:pxW
                              pixelsHigh:pxH
                           bitsPerSample:8
                         samplesPerPixel:4
                                hasAlpha:YES
                                isPlanar:NO
                          colorSpaceName:NSCalibratedRGBColorSpace
                             bytesPerRow:0
                            bitsPerPixel:0];
            out.size = total.size;
            NSGraphicsContext* gc =
                [NSGraphicsContext graphicsContextWithBitmapImageRep:out];
            [NSGraphicsContext saveGraphicsState];
            [NSGraphicsContext setCurrentContext:gc];
            for (NSUInteger i = 0; i < reps.count; i++) {
                NSRect r = rects[i].rectValue;
                r.origin.x -= total.origin.x;
                r.origin.y -= total.origin.y;
                [reps[i] drawInRect:r];
            }
            [NSGraphicsContext restoreGraphicsState];
        }

        NSString* dst = [[NSString alloc] initWithBytes:path.s
                                                 length:(NSUInteger)len(path)
                                               encoding:NSUTF8StringEncoding];
        if (!dst) {
            return StrL("ERR path");
        }
        [[NSFileManager defaultManager] removeItemAtPath:dst error:nil];
        int nWeb = (int)webs.count;
        if (nWeb > 0) {
            NSPoint origin =
                reps.count > 1 ? total.origin : rects[0].rectValue.origin;
            CaptureDrawWebViewsThenWrite(out, origin, webs, webRects, dst);
        } else if (!CaptureWritePng(out, dst)) {
            return StrL("ERR write");
        }
        return WindowCaptureOkTemp((int)out.pixelsWide, (int)out.pixelsHigh,
                                   scale, (int)reps.count, nWeb);
    }
}

void AppActivate(Window* win) {
    if (!win || !win->plat) {
        return;
    }
    if ([win->plat->window isMiniaturized]) {
        [win->plat->window deminiaturize:nil];
    }
    win->plat->hidden = false;
    if (!win->plat->ordered) {
        win->plat->activateOnShow = true;
        Redraw(win);
        return;
    }
    [NSApp activateIgnoringOtherApps:YES];
    [win->plat->window makeKeyAndOrderFront:nil];
}

void AppMinimize(Window* win) {
    if (win && win->plat) {
        [win->plat->window miniaturize:nil];
    }
}

// ─── window frame and placement ───────────────────────────────────────────
//
// AppKit's screen coordinates grow up from the bottom of the primary display;
// ScreenBounds grow down from its top.

static NSWindow* NsWindowOf(Window* win) {
    return win && win->plat ? win->plat->window : nil;
}

// The screen whose origin is (0, 0): its top is y = 0 in screen bounds.
static CGFloat FlipHeight() {
    for (NSScreen* screen in [NSScreen screens]) {
        if (screen.frame.origin.x == 0 && screen.frame.origin.y == 0) {
            return screen.frame.size.height;
        }
    }
    NSScreen* screen = [NSScreen mainScreen];
    return screen ? screen.frame.size.height : 0;
}

static ScreenBounds FromMac(NSRect r) {
    CGFloat h = FlipHeight();
    return {(int)r.origin.x, (int)(h - (r.origin.y + r.size.height)),
            (int)r.size.width, (int)r.size.height};
}

static NSRect ToMac(ScreenBounds r) {
    CGFloat h = FlipHeight();
    return NSMakeRect(r.x, h - (r.y + r.h), r.w, r.h);
}

static NSScreen* ScreenOf(Window* win) {
    NSWindow* w = NsWindowOf(win);
    if (w && w.screen) {
        return w.screen;
    }
    NSArray<NSScreen*>* screens = [NSScreen screens];
    return screens.count > 0 ? screens[0] : [NSScreen mainScreen];
}

ScreenBounds WindowFrameBounds(Window* win) {
    NSWindow* w = NsWindowOf(win);
    return w ? FromMac(w.frame) : ScreenBounds{};
}

ScreenBounds WindowContentBounds(Window* win) {
    NSWindow* w = NsWindowOf(win);
    return w ? FromMac([w contentRectForFrameRect:w.frame]) : ScreenBounds{};
}

void WindowSetFrameBounds(Window* win, ScreenBounds outer) {
    NSWindow* w = NsWindowOf(win);
    if (!w || outer.IsEmpty()) {
        return;
    }
    [w setFrame:ToMac(outer) display:YES];
}

static NSWindowStyleMask FrameStyleMask(const WinOpts& opts) {
    if (opts.frameless || opts.kind == WindowKind::PopUp || opts.borderless) {
        return NSWindowStyleMaskBorderless;
    }
    NSWindowStyleMask mask =
        NSWindowStyleMaskTitled | NSWindowStyleMaskClosable;
    if (opts.resizable) {
        mask |= NSWindowStyleMaskResizable;
    }
    if (opts.utility) {
        mask |= NSWindowStyleMaskUtilityWindow;
    } else if (opts.minimizable) {
        mask |= NSWindowStyleMaskMiniaturizable;
    }
    if (opts.clientTitleBar) {
        mask |= NSWindowStyleMaskFullSizeContentView;
    }
    return mask;
}

// AppKit frames have no side borders; the title bar is on top.
ScreenBounds WindowFrameForContent(Window* nearby, const WinOpts& opts,
                                   ScreenBounds content) {
    (void)nearby;
    NSRect c = NSMakeRect(0, 0, content.w, content.h);
    NSRect f =
        [NSWindow frameRectForContentRect:c styleMask:FrameStyleMask(opts)];
    int dw = (int)(f.size.width - c.size.width);
    int dh = (int)(f.size.height - c.size.height);
    return {content.x - dw / 2, content.y - dh, content.w + dw, content.h + dh};
}

ScreenBounds WindowDisplayBounds(Window* win) {
    NSScreen* screen = ScreenOf(win);
    return screen ? FromMac(screen.frame) : ScreenBounds{};
}

ScreenBounds WindowDisplayVisibleBounds(Window* win) {
    NSScreen* screen = ScreenOf(win);
    return screen ? FromMac(screen.visibleFrame) : ScreenBounds{};
}

void PlatWindowShow(Window* win, bool visible, bool activate) {
    PlatWindow* pw = win ? win->plat : nullptr;
    if (!pw) {
        return;
    }
    pw->hidden = !visible;
    if (!visible) {
        [pw->window orderOut:nil];
        return;
    }
    if (!pw->ordered) {
        // The first paint orders it front.
        pw->activateOnShow = activate;
        AppInvalidate(win);
        return;
    }
    OrderFront(win, activate);
    AppInvalidate(win);
}

bool WindowIsVisible(Window* win) {
    NSWindow* w = NsWindowOf(win);
    if (!w) {
        return false;
    }
    // Not on screen yet, but on its way with the first paint.
    return w.isVisible || (!win->plat->ordered && !win->plat->hidden);
}

// The key window changes only while the application is the active one.
void WindowActivateInApp(Window* win) {
    NSWindow* w = NsWindowOf(win);
    if (w && NSApp.isActive && !w.isKeyWindow) {
        [w makeKeyWindow];
    }
}

bool WindowIsActive(Window* win) {
    NSWindow* w = NsWindowOf(win);
    return w && w.isKeyWindow;
}

void PlatWindowSetParent(Window* win, Window* parent) {
    NSWindow* w = NsWindowOf(win);
    if (!w) {
        return;
    }
    NSWindow* p = NsWindowOf(parent);
    if (w.parentWindow && w.parentWindow != p) {
        [w.parentWindow removeChildWindow:w];
    }
    // A hidden one is attached when it is shown.
    if (p && w.isVisible) {
        AttachToParent(win);
    }
}

// Clicks and keys aimed at the parent bring the dialog forward instead.
// One dialog at a time holds a monitor, as one modal session would.
static id gModalMonitor = nil;

void PlatWindowSetModal(Window* dlg, Window* parent, bool on) {
    if (gModalMonitor) {
        [NSEvent removeMonitor:gModalMonitor];
        gModalMonitor = nil;
    }
    NSWindow* tool = NsWindowOf(dlg);
    NSWindow* p = NsWindowOf(parent);
    if (!on || !tool || !p) {
        return;
    }
    NSEventMask mask = NSEventMaskLeftMouseDown | NSEventMaskRightMouseDown |
                       NSEventMaskOtherMouseDown | NSEventMaskKeyDown;
    gModalMonitor = [NSEvent
        addLocalMonitorForEventsMatchingMask:mask
                                     handler:^NSEvent*(NSEvent* event) {
                                       if (event.window == p) {
                                           [tool makeKeyAndOrderFront:nil];
                                           return nil;
                                       }
                                       return event;
                                     }];
}

void WindowSetMinSize(Window* win, Size content) {
    if (!win) {
        return;
    }
    win->opts.minSize = content;
    NSWindow* w = NsWindowOf(win);
    if (w && content.w > 0 && content.h > 0) {
        [w setContentMinSize:NSMakeSize(content.w, content.h)];
    }
}

void WindowSetMaxSize(Window* win, Size content) {
    if (!win) {
        return;
    }
    win->opts.maxSize = content;
    NSWindow* w = NsWindowOf(win);
    if (w) {
        // FLT_MAX is AppKit's own "unbounded".
        CGFloat dx = content.w > 0 ? content.w : FLT_MAX;
        CGFloat dy = content.h > 0 ? content.h : FLT_MAX;
        [w setContentMaxSize:NSMakeSize(dx, dy)];
    }
}

void WindowSetFrameAppearance(Window* win, FrameAppearance appearance) {
    NSWindow* w = NsWindowOf(win);
    if (!w) {
        return;
    }
    NSAppearance* a = nil;
    if (appearance == FrameAppearance::Dark) {
        a = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
    } else if (appearance == FrameAppearance::Light) {
        a = [NSAppearance appearanceNamed:NSAppearanceNameAqua];
    }
    [w setAppearance:a];
}

void WindowSendNativeKey(Window* win, int macKeyCode) {
    PlatWindow* pw = win ? win->plat : nullptr;
    if (!pw) {
        return;
    }
    NSWindow* w = pw->window;
    [w makeFirstResponder:pw->view];
    NSString* chars = macKeyCode == 0 ? @"a" : @"";
    auto event = ^(NSEventType type, NSString* text) {
      return [NSEvent keyEventWithType:type
                              location:NSZeroPoint
                         modifierFlags:0
                             timestamp:[NSProcessInfo processInfo].systemUptime
                          windowNumber:w.windowNumber
                               context:nil
                            characters:text
           charactersIgnoringModifiers:chars
                             isARepeat:NO
                               keyCode:(unsigned short)macKeyCode];
    };
    [NSApp sendEvent:event(NSEventTypeKeyDown, chars)];
    [NSApp sendEvent:event(NSEventTypeKeyUp, @"")];
}

void AppToggleMaximize(Window* win) {
    if (win && win->plat) {
        [win->plat->window zoom:nil];
        win->maximized = [win->plat->window isZoomed] ? true : false;
    }
}

void WindowSetFullScreen(Window* win, bool fullScreen) {
    if (!win || !win->plat) {
        return;
    }
    NSWindow* window = win->plat->window;
    bool current = ([window styleMask] & NSWindowStyleMaskFullScreen) != 0;
    if (current != fullScreen) {
        [window toggleFullScreen:nil];
    }
}

void AppWindowSetOpacity(Window* win, int percent) {
    if (!win || !win->plat) {
        return;
    }
    int clamped = percent < 20 ? 20 : percent > 100 ? 100 : percent;
    [win->plat->window setAlphaValue:clamped / 100.0];
}

// Not done here: the view's layer is drawn opaque.
bool PlatWindowCanBeTransparent() {
    return false;
}

void AppWindowSetClickThrough(Window* win, bool on) {
    if (!win || !win->plat) {
        return;
    }
    [win->plat->window setIgnoresMouseEvents:on ? YES : NO];
}

void AppDrag(Window* win) {
    if (!win || !win->plat) {
        return;
    }
    NSEvent* ev = [NSApp currentEvent];
    if (ev) {
        [win->plat->window performWindowDragWithEvent:ev];
    }
}

void AppSetTitle(Window* win, Str title) {
    if (!win || !win->plat || !title.s) {
        return;
    }
    NSString* s = [[NSString alloc] initWithBytes:title.s
                                           length:(NSUInteger)len(title)
                                         encoding:NSUTF8StringEncoding];
    if (s) {
        [win->plat->window setTitle:s];
    }
}

void PlatSetTimer(Window* win, int ms) {
    if (!win || !win->plat) {
        return;
    }
    win->plat->nextTick = ms > 0 ? TimeNow() + ms / 1000.0 : 0;
}

void PlatSetMouseCapture(Window* win, bool capture) {
    // Cocoa already routes mouseDragged: and mouseUp: to the window that took
    // the mouseDown:, wherever the pointer has got to, so there is nothing to
    // grab and nothing to give back.
    (void)win;
    (void)capture;
}

static void CursorImageRelease(void* plat) {
    CFBridgingRelease(plat);
}

// The pixels are device pixels; the image is sized in points for the
// window's backing scale, so a scale change makes a new cursor.
static NSCursor* CursorImageCursor(CursorImage* img, float scale) {
    if (!img) {
        return nil;
    }
    if (img->plat && img->platScale == scale) {
        return (__bridge NSCursor*)img->plat;
    }
    if (img->plat) {
        img->platFree(img->plat);
        img->plat = nullptr;
    }
    NSBitmapImageRep* rep =
        [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nullptr
                                                pixelsWide:img->w
                                                pixelsHigh:img->h
                                             bitsPerSample:8
                                           samplesPerPixel:4
                                                  hasAlpha:YES
                                                  isPlanar:NO
                                            colorSpaceName:NSDeviceRGBColorSpace
                                              bitmapFormat:(NSBitmapFormat)0
                                               bytesPerRow:img->w * 4
                                              bitsPerPixel:32];
    if (!rep || ![rep bitmapData]) {
        return nil;
    }
    memcpy([rep bitmapData], img->rgba, (size_t)img->w * (size_t)img->h * 4);
    NSImage* image = [[NSImage alloc]
        initWithSize:NSMakeSize(img->w / scale, img->h / scale)];
    [image addRepresentation:rep];
    NSCursor* cursor = [[NSCursor alloc]
        initWithImage:image
              hotSpot:NSMakePoint(img->hotX / scale, img->hotY / scale)];
    if (!cursor) {
        return nil;
    }
    img->plat = (void*)CFBridgingRetain(cursor);
    img->platScale = scale;
    img->platFree = CursorImageRelease;
    return cursor;
}

void PlatSetCursor(Window* win, CursorKind kind) {
    static bool hidden = false;
    bool wantHidden = win && win->cursorHidden;
    if (wantHidden != hidden) {
        hidden = wantHidden;
        if (hidden) {
            [NSCursor hide];
        } else {
            [NSCursor unhide];
        }
    }
    if (wantHidden) {
        return;
    }
    if (NSCursor* image = CursorImageCursor(win ? win->cursorImage : nullptr,
                                            WindowScaleFactor(win))) {
        [image set];
        return;
    }
    if (kind == CursorKind::IBeam) {
        [[NSCursor IBeamCursor] set];
    } else if (kind == CursorKind::Pointer) {
        [[NSCursor pointingHandCursor] set];
    } else if (kind == CursorKind::ColResize) {
        [[NSCursor resizeLeftRightCursor] set];
    } else if (kind == CursorKind::RowResize) {
        [[NSCursor resizeUpDownCursor] set];
    } else if (kind == CursorKind::Crosshair) {
        [[NSCursor crosshairCursor] set];
    } else if (kind == CursorKind::ClosedHand) {
        [[NSCursor closedHandCursor] set];
    } else if (kind == CursorKind::OpenHand) {
        [[NSCursor openHandCursor] set];
    } else if (kind == CursorKind::ResizeLeft) {
        [[NSCursor resizeLeftCursor] set];
    } else if (kind == CursorKind::ResizeRight) {
        [[NSCursor resizeRightCursor] set];
    } else if (kind == CursorKind::ResizeLeftRight) {
        [[NSCursor resizeLeftRightCursor] set];
    } else if (kind == CursorKind::ResizeUp) {
        [[NSCursor resizeUpCursor] set];
    } else if (kind == CursorKind::ResizeDown) {
        [[NSCursor resizeDownCursor] set];
    } else if (kind == CursorKind::ResizeUpDown) {
        [[NSCursor resizeUpDownCursor] set];
    } else if (kind == CursorKind::IBeamVertical) {
        [[NSCursor IBeamCursorForVerticalLayout] set];
    } else if (kind == CursorKind::NotAllowed) {
        [[NSCursor operationNotAllowedCursor] set];
    } else if (kind == CursorKind::DragLink) {
        [[NSCursor dragLinkCursor] set];
    } else if (kind == CursorKind::DragCopy) {
        [[NSCursor dragCopyCursor] set];
    } else if (kind == CursorKind::ContextMenu) {
        [[NSCursor contextualMenuCursor] set];
    } else {
        // Including the two diagonal resizes, which AppKit has no public
        // cursor for.
        [[NSCursor arrowCursor] set];
    }
}

int PlatDoubleClickMs() {
    // NSEvent tracks its own clickCount, but the window counts presses in
    // shared code so all three platforms agree on what a run is; this is the
    // one number the OS owns.
    return (int)([NSEvent doubleClickInterval] * 1000);
}

uint64_t PlatWindowDisplay(Window* win) {
    NSScreen* screen = win && win->plat && win->plat->window
                           ? [win->plat->window screen]
                           : nil;
    if (!screen) {
        return 0;
    }
    // NSScreenNumber is the CGDirectDisplayID, which is what GPUI's DisplayId
    // is on this platform.
    NSNumber* number =
        [[screen deviceDescription] objectForKey:@"NSScreenNumber"];
    return number ? (uint64_t)[number unsignedIntValue] : 0;
}

double PlatDisplayRefreshPeriod(uint64_t display) {
    if (!display) {
        return 0;
    }
    CGDisplayModeRef mode =
        CGDisplayCopyDisplayMode((CGDirectDisplayID)display);
    if (!mode) {
        return 0;
    }
    double hertz = CGDisplayModeGetRefreshRate(mode);
    CGDisplayModeRelease(mode);
    // Zero rather than an error is how CoreGraphics says this display has no
    // fixed rate, which is what a built-in panel reports: on ProMotion there
    // genuinely is not one, and the nominal period would have to come from
    // CoreVideo instead.
    return hertz > 1. ? 1. / hertz : 0;
}

// macos_accessibility.rs hit_test_forwarder: the window hands the point to
// its content view, which is the one thing in the tree that knows what was
// drawn where. class_addMethod leaves an existing implementation alone, so a
// window that already answers keeps its own.
static id GpuiAccessibilityHitTest(id self, SEL cmd, NSPoint point) {
    (void)cmd;
    NSView* view = [(NSWindow*)self contentView];
    if (!view) {
        return nil;
    }
    return [view accessibilityHitTest:point];
}

void* PlatWindowHandle(Window* win) {
    // The content view, which is what a WKWebView would be added to.
    if (!win || !win->plat) {
        return nullptr;
    }
    return (__bridge void*)win->plat->view;
}

void PlatInstallAccessibilityHitTest(Window* win) {
    if (!win || !win->plat || !win->plat->window) {
        return;
    }
    if (win->plat->a11yInstalled) {
        return;
    }
    win->plat->a11yInstalled = true;
    Class cls = object_getClass(win->plat->window);
    if (!cls) {
        return;
    }
    // "@@:{CGPoint=dd}": returns an object, takes self, the selector and a
    // point of two doubles — the encoding Rust builds from the same pieces.
    class_addMethod(cls, @selector(accessibilityHitTest:),
                    (IMP)GpuiAccessibilityHitTest, "@@:{CGPoint=dd}");
}

void PlatAccessibilityTreeChanged(Window* win) {
    if (!win || !win->plat || !win->plat->view) {
        return;
    }
    NSMutableArray<NSNumber*>* stale = [[NSMutableArray alloc] init];
    for (NSNumber* key in win->plat->accessibilityElements) {
        if (!WindowAccessibilityNode(win, [key unsignedIntValue])) {
            GpuiAccessibilityElement* element =
                [win->plat->accessibilityElements objectForKey:key];
            element->gpuiWindow = nullptr;
            [stale addObject:key];
        }
    }
    [win->plat->accessibilityElements removeObjectsForKeys:stale];
    NSAccessibilityPostNotification(win->plat->view,
                                    NSAccessibilityLayoutChangedNotification);
}

void PlatAccessibilityFocusChanged(Window* win, int focusId) {
    if (!win || !win->plat || !focusId) {
        return;
    }
    for (int i = 0; i < win->accessibility.len; i++) {
        const AccessibilityNode& node = win->accessibility[i];
        if (node.focusId == focusId) {
            GpuiAccessibilityElement* element =
                GpuiAccessibilityElementFor(win, node.id);
            if (element) {
                NSAccessibilityPostNotification(
                    element,
                    NSAccessibilityFocusedUIElementChangedNotification);
            }
            return;
        }
    }
}

// VoiceOver queries NSAccessibility without announcing itself first.
bool PlatAccessibilityWanted(Window* win) {
    return win != nullptr;
}

bool PlatHasMenu() {
    return true;
}

// Which row was chosen. popUpMenuPositioningItem runs its own tracking loop
// and returns once the menu is gone, so one variable is enough to carry the
// answer back out of the action.
static int gMenuChoice = 0;

} // namespace gpui

// The object every row of a native menu sends its action to.
@interface GpuiMenuTarget : NSObject
@end

@implementation GpuiMenuTarget
- (void)gpuiMenuItemChosen:(id)sender {
    gpui::gMenuChoice = (int)[(NSMenuItem*)sender tag];
}
@end

namespace gpui {

// The rows as an NSMenu. A row's tag is the id it reports; a submenu row
// carries no tag and opens onto a menu built the same way.
// MENU_IMAGE_SIZE: the side a menu item's image is scaled to, in points.
static const int kMenuImageSize = 16;

// One icon as an image the menu can show. It is loaded as a template image,
// which is what makes AppKit tint it with the row's text — so it reads right
// in either appearance and while the row is highlighted.
static NSImage* MenuIconImage(Window* win, const PlatMenuItem& it) {
    bool fromSvg = it.iconSvg && it.iconSvgLen > 0;
    if (!win || (!fromSvg && (!it.iconPath || !it.iconPath[0]))) {
        return nil;
    }
    // Rasterized at twice the point size, so it stays sharp on a Retina
    // display; the image is then told it measures the point size.
    const int px = kMenuImageSize * 2;
    Vec<uint8_t> buf;
    VecAppendBlanks(buf, px * px * 4);
    // The colour does not matter for a template image, only the coverage.
    // `Icon::data` rasterizes from its bytes; a path goes through the assets.
    bool drew =
        fromSvg ? SvgRasterizeXml(win->paint.pa, Str(it.iconSvg, it.iconSvgLen),
                                  px, Rgb(0, 0, 0), buf.els)
                : SvgRasterize(win->paint.pa, Str(it.iconPath), px,
                               Rgb(0, 0, 0), buf.els);
    if (!drew) {
        return nil;
    }
    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    CGBitmapInfo bitmapInfo =
        (CGBitmapInfo)((uint32_t)kCGImageAlphaPremultipliedFirst |
                       (uint32_t)kCGBitmapByteOrder32Little);
    CGContextRef bmp = CGBitmapContextCreate(buf.els, (size_t)px, (size_t)px, 8,
                                             (size_t)px * 4, space, bitmapInfo);
    CGColorSpaceRelease(space);
    if (!bmp) {
        return nil;
    }
    CGImageRef cgImage = CGBitmapContextCreateImage(bmp);
    CGContextRelease(bmp);
    if (!cgImage) {
        return nil;
    }
    NSImage* image = [[NSImage alloc]
        initWithCGImage:cgImage
                   size:NSMakeSize(kMenuImageSize, kMenuImageSize)];
    CGImageRelease(cgImage);
    [image setTemplate:YES];
    return image;
}

// A row's key equivalent as AppKit spells it: one character, which for the
// keys that have no character of their own is the private-use code point
// AppKit reserves for it. A key with neither is a row with no shortcut —
// which is what a key nothing here names would be on the keyboard anyway.
static NSString* KeyEquivalent(Str key) {
    if (!key) {
        return @"";
    }
    struct Named {
        const char* name;
        unichar ch;
    };
    static const Named kNamed[] = {
        {"enter", 0x0d},
        {"return", 0x0d},
        {"tab", 0x09},
        {"escape", 0x1b},
        {"space", ' '},
        {"backspace", 0x08},
        {"delete", NSDeleteFunctionKey},
        {"up", NSUpArrowFunctionKey},
        {"down", NSDownArrowFunctionKey},
        {"left", NSLeftArrowFunctionKey},
        {"right", NSRightArrowFunctionKey},
        {"home", NSHomeFunctionKey},
        {"end", NSEndFunctionKey},
        {"pageup", NSPageUpFunctionKey},
        {"pagedown", NSPageDownFunctionKey},
    };
    for (size_t i = 0; i < sizeof(kNamed) / sizeof(kNamed[0]); i++) {
        if (StrEq(key, kNamed[i].name)) {
            unichar ch = kNamed[i].ch;
            return [NSString stringWithCharacters:&ch length:1];
        }
    }
    // f1..f12, which AppKit also names with a code point of its own.
    if (StrStartsWithAny(key, "fF") && len(key) >= 2 && key.s[1] >= '0' &&
        key.s[1] <= '9') {
        int n = key.s[1] - '0';
        if (len(key) == 3 && key.s[2] >= '0' && key.s[2] <= '9') {
            n = n * 10 + (key.s[2] - '0');
        } else if (len(key) != 2) {
            n = 0;
        }
        if (n >= 1 && n <= 12) {
            unichar ch = (unichar)(NSF1FunctionKey + n - 1);
            return [NSString stringWithCharacters:&ch length:1];
        }
        return @"";
    }
    // A letter, a digit or a punctuation key is its own equivalent, and a
    // binding already spells it lowercase — which is what AppKit wants, with
    // the shift in the modifier mask rather than in the character.
    if (len(key) == 1) {
        TempStr keyZ = StrDupTemp(key);
        return [NSString stringWithUTF8String:keyZ.s];
    }
    return @"";
}

static NSEventModifierFlags KeyEquivalentMask(const Modifiers& mods) {
    NSEventModifierFlags mask = 0;
    if (mods.control) {
        mask |= NSEventModifierFlagControl;
    }
    if (mods.alt) {
        mask |= NSEventModifierFlagOption;
    }
    if (mods.shift) {
        mask |= NSEventModifierFlagShift;
    }
    if (mods.platform) {
        mask |= NSEventModifierFlagCommand;
    }
    if (mods.function) {
        mask |= NSEventModifierFlagFunction;
    }
    return mask;
}

// `target` and `sel` are what a chosen row reports to: a popup menu answers
// into the variable its tracking loop is waiting on, and the application menu
// bar, which outlives every tracking loop, dispatches the row's action.
static NSMenu* BuildMenu(Window* win, const PlatMenuItem* items, int n,
                         id target, SEL sel) {
    NSMenu* menu = [[NSMenu alloc] init];
    // Without this AppKit greys every row whose target does not answer
    // validateMenuItem:, which is all of them.
    [menu setAutoenablesItems:NO];
    for (int i = 0; i < n; i++) {
        const PlatMenuItem& it = items[i];
        if (it.separator) {
            [menu addItem:[NSMenuItem separatorItem]];
            continue;
        }
        NSString* label =
            [NSString stringWithUTF8String:(it.label ? it.label : "")];
        NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:label
                                                      action:nil
                                               keyEquivalent:@""];
        [item setEnabled:(it.disabled ? NO : YES)];
        if (it.iconPath || it.iconSvg) {
            NSImage* image = MenuIconImage(win, it);
            if (image) {
                [item setImage:image];
            }
        }
        if (it.submenu && it.submenuN > 0) {
            [item setSubmenu:BuildMenu(win, it.submenu, it.submenuN, target,
                                       sel)];
        } else {
            [item setTag:it.id];
            [item setState:(it.checked ? NSControlStateValueOn
                                       : NSControlStateValueOff)];
            NSString* equivalent = KeyEquivalent(Str(it.key));
            if ([equivalent length] > 0) {
                [item setKeyEquivalent:equivalent];
                [item
                    setKeyEquivalentModifierMask:KeyEquivalentMask(it.keyMods)];
            }
            if (!it.disabled) {
                [item setTarget:target];
                [item setAction:sel];
            }
        }
        [menu addItem:item];
    }
    return menu;
}

int PlatShowMenu(Window* win, const PlatMenuItem* items, int n, float x,
                 float y, bool dark) {
    (void)dark;
    if (!win || !win->plat || !items || n <= 0) {
        return 0;
    }
    GpuiMenuTarget* target = [[GpuiMenuTarget alloc] init];
    NSMenu* menu =
        BuildMenu(win, items, n, target, @selector(gpuiMenuItemChosen:));
    gMenuChoice = 0;
    // The view is flipped, so the position is the one the window works in.
    NSPoint at = NSMakePoint(x, y);
    [menu popUpMenuPositioningItem:nil atLocation:at inView:win->plat->view];
    return gMenuChoice;
}

} // namespace gpui

// The object every row of the application menu bar sends its action to. The
// popup menus have one per menu, made and dropped inside the tracking loop;
// this one is the application's and lives as long as the bar does.
@interface GpuiAppMenuTarget : NSObject
@end

@implementation GpuiAppMenuTarget
- (void)gpuiAppMenuItemChosen:(id)sender {
    gpui::AppMenuChosen((int)[(NSMenuItem*)sender tag]);
}
@end

namespace gpui {

static App* gRunningApp = nullptr;
// the App PlatInit made, for what arrives before AppRun (open URLs)
static App* gPlatApp = nullptr;
static GpuiAppMenuTarget* gAppMenuTarget = nil;

} // namespace gpui

// terminate: only stops [NSApp run]. The examples drive their own loop with
// nextEventMatchingMask, so Quit and ⌘Q have to close the windows here or
// the process keeps running.
@interface GpuiAppDelegate : NSObject <NSApplicationDelegate>
@end

@implementation GpuiAppDelegate
- (NSApplicationTerminateReply)applicationShouldTerminate:
    (NSApplication*)sender {
    (void)sender;
    if (gpui::gRunningApp) {
        gpui::AppQuitAll(gpui::gRunningApp);
    }
    return NSTerminateCancel;
}

// GPUI's open_urls: documents and URLs the system hands the application
- (void)application:(NSApplication*)application
           openURLs:(NSArray<NSURL*>*)urls {
    (void)application;
    gpui::Vec<gpui::Str> strs;
    for (NSURL* url in urls) {
        const char* s = url.absoluteString.UTF8String;
        if (s) {
            gpui::VecAppend(strs, gpui::Str((char*)s));
        }
    }
    gpui::AppOpenUrls(gpui::gPlatApp, strs.els, gpui::len(strs));
}
@end

static GpuiAppDelegate* gAppDelegate = nil;

namespace gpui {

static void InstallDefaultAppMenu() {
    NSString* name = [[NSProcessInfo processInfo] processName];
    NSMenu* bar = [[NSMenu alloc] init];
    NSMenu* appMenu = [[NSMenu alloc] initWithTitle:name];
    NSMenuItem* hide = [[NSMenuItem alloc]
        initWithTitle:[@"Hide " stringByAppendingString:name]
               action:@selector(hide:)
        keyEquivalent:@"h"];
    [appMenu addItem:hide];
    NSMenuItem* hideOthers =
        [[NSMenuItem alloc] initWithTitle:@"Hide Others"
                                   action:@selector(hideOtherApplications:)
                            keyEquivalent:@"h"];
    [hideOthers setKeyEquivalentModifierMask:NSEventModifierFlagCommand |
                                             NSEventModifierFlagOption];
    [appMenu addItem:hideOthers];
    NSMenuItem* showAll =
        [[NSMenuItem alloc] initWithTitle:@"Show All"
                                   action:@selector(unhideAllApplications:)
                            keyEquivalent:@""];
    [appMenu addItem:showAll];
    [appMenu addItem:[NSMenuItem separatorItem]];
    NSMenuItem* quit = [[NSMenuItem alloc]
        initWithTitle:[@"Quit " stringByAppendingString:name]
               action:@selector(terminate:)
        keyEquivalent:@"q"];
    [appMenu addItem:quit];
    NSMenuItem* appItem =
        [[NSMenuItem alloc] initWithTitle:name action:nil keyEquivalent:@""];
    [appItem setSubmenu:appMenu];
    [bar addItem:appItem];
    [NSApp setMainMenu:bar];
}

bool PlatHasAppMenu() {
    return true;
}

void PlatSetMenuBarVisible(bool visible) {
    [NSMenu setMenuBarVisible:visible ? YES : NO];
}

void PlatSetAppMenu(App* app, const PlatMenuItem* items, int n) {
    (void)app;
    if (!items || n <= 0) {
        InstallDefaultAppMenu();
        return;
    }
    if (!gAppMenuTarget) {
        gAppMenuTarget = [[GpuiAppMenuTarget alloc] init];
    }
    NSMenu* bar = [[NSMenu alloc] init];
    [bar setAutoenablesItems:NO];
    for (int i = 0; i < n; i++) {
        const PlatMenuItem& it = items[i];
        NSString* title =
            [NSString stringWithUTF8String:(it.label ? it.label : "")];
        // A menu bar is a row of submenus: the item carries the name and the
        // menu under it carries the rows. AppKit reads the *first* one as the
        // application menu and titles it after the process, whatever this
        // says — which is why the story's first menu is the one named for the
        // application in Rust too.
        NSMenu* sub =
            BuildMenu(nullptr, it.submenu, it.submenuN, gAppMenuTarget,
                      @selector(gpuiAppMenuItemChosen:));
        [sub setTitle:title];
        NSMenuItem* row = [[NSMenuItem alloc] initWithTitle:title
                                                     action:nil
                                              keyEquivalent:@""];
        [row setSubmenu:sub];
        [bar addItem:row];
        // The menu AppKit keeps the window list in. Telling it which one adds
        // the Minimize / Zoom / Bring All to Front rows and one row per open
        // window, which is what makes a Mac application's Window menu behave
        // the way every other one does.
        if (StrEq(Str(it.label), StrL("Window"))) {
            [NSApp setWindowsMenu:sub];
        }
    }
    [NSApp setMainMenu:bar];
}

// cx.open_url. NSWorkspace hands the URL to whichever application is
// registered for its scheme.
// The macOS switch is Accessibility ▸ Display ▸ Reduce motion, which
// NSWorkspace answers for directly.
bool PlatReduceMotion() {
    return
        [[NSWorkspace sharedWorkspace] accessibilityDisplayShouldReduceMotion]
            ? true
            : false;
}

void OpenUrl(Str url) {
    if (!url.s || len(url) <= 0) {
        return;
    }
    NSString* s = [[NSString alloc] initWithBytes:url.s
                                           length:(NSUInteger)len(url)
                                         encoding:NSUTF8StringEncoding];
    NSURL* u = s ? [NSURL URLWithString:s] : nil;
    if (u) {
        [[NSWorkspace sharedWorkspace] openURL:u];
    }
}

void OpenPath(Str path) {
    if (!path.s || len(path) <= 0) {
        return;
    }
    NSString* s = [[NSString alloc] initWithBytes:path.s
                                           length:(NSUInteger)len(path)
                                         encoding:NSUTF8StringEncoding];
    NSURL* u = s ? [NSURL fileURLWithPath:s] : nil;
    if (u) {
        [[NSWorkspace sharedWorkspace] openURL:u];
    }
}

// ─── window.prompt (gpui_macos window.rs prompt) ────────────────────────────

static NSString* PromptNSString(Str s) {
    NSString* res = [[NSString alloc] initWithBytes:s.s ? s.s : ""
                                             length:(NSUInteger)len(s)
                                           encoding:NSUTF8StringEncoding];
    return res ? res : @"";
}

static NSWindow* PromptSheetParent(PromptRequest* req) {
    for (int i = 0; req->app && i < len(req->app->windows); i++) {
        Window* w = req->app->windows[i];
        if (w == req->win && w->plat) {
            return w->plat->window;
        }
    }
    return nil;
}

// NSAlert's first button keeps Return and Cancel keeps Escape, but keyboard
// focus (and so Space) starts on Cancel, which leaves the middle button of
// "Save / Don't Save / Cancel" unreachable. Rust moves the initial focus to
// the last non-cancel button after the first.
static void PromptRun(PromptRequest* req) {
    NSWindow* parent = PromptSheetParent(req);
    if (!parent) {
        PromptFinish(req, -1);
        return;
    }
    int focusIx = -1;
    for (int i = req->n - 1; i > 0 && focusIx < 0; i--) {
        if (!req->answers[i].IsCancel()) {
            focusIx = i;
        }
    }
    NSAlert* alert = [[NSAlert alloc] init];
    NSAlertStyle style = NSAlertStyleInformational;
    if (req->level == PromptLevel::Warning) {
        style = NSAlertStyleWarning;
    } else if (req->level == PromptLevel::Critical) {
        style = NSAlertStyleCritical;
    }
    [alert setAlertStyle:style];
    [alert setMessageText:PromptNSString(req->message)];
    if (len(req->detail) > 0) {
        [alert setInformativeText:PromptNSString(req->detail)];
    }
    // Not in Rust: PromptOptions::defaultIx takes Return off the first button.
    int defaultIx = req->opts.defaultIx;
    NSButton* focusButton = nil;
    NSButton* first = nil;
    for (int i = 0; i < req->n; i++) {
        NSButton* button =
            [alert addButtonWithTitle:PromptNSString(req->answers[i].label)];
        [button setTag:i];
        if (i == 0) {
            first = button;
        }
        if (req->answers[i].IsCancel()) {
            [button setKeyEquivalent:@"\x1b"];
        } else if (i == focusIx) {
            focusButton = button;
        }
        if (defaultIx > 0 && i == defaultIx) {
            [button setKeyEquivalent:@"\r"];
        }
    }
    if (defaultIx > 0 && defaultIx < req->n && first &&
        [[first keyEquivalent] isEqualToString:@"\r"]) {
        [first setKeyEquivalent:@""];
    }
    if (focusButton) {
        [[alert window] setInitialFirstResponder:focusButton];
    }
    // Retained until the sheet ends, so a closing window can end it.
    req->plat = (__bridge_retained void*)alert;
    [alert beginSheetModalForWindow:parent
                  completionHandler:^(NSModalResponse answer) {
                    NSAlert* done = (__bridge_transfer NSAlert*)req->plat;
                    req->plat = nullptr;
                    (void)done;
                    PromptFinish(req, (int)answer);
                  }];
}

// Rust's closed flag keeps a sheet off a closed window; one already up is
// ended here, and its completion frees the request.
void PlatPromptAbort(Window* win, PromptRequest* req) {
    if (!req || !req->plat || !win || !win->plat || !win->plat->window) {
        return;
    }
    NSAlert* alert = (__bridge NSAlert*)req->plat;
    [win->plat->window endSheet:[alert window] returnCode:NSModalResponseAbort];
}

// The sheet starts from the main-thread queue, as Rust spawns it on the
// foreground executor, so a listener never begins a modal session.
bool PlatWindowPrompt(Window* win, PromptRequest* req) {
    if (!win || !win->plat || !win->plat->window) {
        return false;
    }
    ExecPost(MkFunc0(PromptRun, req));
    return true;
}

// ─── drag out (no Rust twin) ─────────────────────────────────────────────────

} // namespace gpui

// The source of a drag out, and the delegate writing the image's promised
// file. The request holds it (CFBridgingRetain) until the session ends; the
// promise provider keeps it as userInfo until the file is written.
@interface GpuiDragSource
    : NSObject <NSDraggingSource, NSFilePromiseProviderDelegate> {
  @public
    gpui::DragOutRequest* req;
    __strong NSData* png;
    __strong NSString* pngName;
}
@end

@implementation GpuiDragSource
- (NSDragOperation)draggingSession:(NSDraggingSession*)session
    sourceOperationMaskForDraggingContext:(NSDraggingContext)context {
    (void)session, (void)context;
    return NSDragOperationCopy;
}
- (void)draggingSession:(NSDraggingSession*)session
           endedAtPoint:(NSPoint)point
              operation:(NSDragOperation)op {
    (void)session, (void)point;
    gpui::DragOutResult result = gpui::DragOutResult::Canceled;
    if (op & NSDragOperationMove) {
        result = gpui::DragOutResult::Moved;
    } else if (op & NSDragOperationCopy) {
        result = gpui::DragOutResult::Copied;
    } else if (op & NSDragOperationLink) {
        result = gpui::DragOutResult::Linked;
    }
    gpui::DragOutRequest* r = req;
    req = nullptr;
    if (!r) {
        return;
    }
    void* plat = r->plat;
    r->plat = nullptr;
    gpui::DragOutFinish(r, result);
    if (plat) {
        CFBridgingRelease(plat);
    }
}
- (NSString*)filePromiseProvider:(NSFilePromiseProvider*)provider
                 fileNameForType:(NSString*)fileType {
    (void)provider, (void)fileType;
    return pngName;
}
- (void)filePromiseProvider:(NSFilePromiseProvider*)provider
          writePromiseToURL:(NSURL*)url
          completionHandler:(void (^)(NSError* error))done {
    (void)provider;
    NSError* err = nil;
    [png writeToURL:url options:NSDataWritingAtomic error:&err];
    done(err);
}
@end

namespace gpui {

// The image scaled into the preview's size, on white with a 1px border.
static NSImage* DragOutPreviewImage(NSImage* img, const DragOutPreview& p) {
    NSSize size = NSMakeSize(p.w, p.h);
    return [NSImage
         imageWithSize:size
               flipped:NO
        drawingHandler:^BOOL(NSRect r) {
          [[NSColor whiteColor] setFill];
          NSRectFill(r);
          [img drawInRect:r
                 fromRect:NSZeroRect
                operation:NSCompositingOperationSourceOver
                 fraction:1];
          [[NSColor colorWithCalibratedWhite:60.0 / 255.0 alpha:1] setFill];
          NSFrameRect(r);
          return YES;
        }];
}

// One dragging item per payload: the text, each file, the promised image.
// The first carries the preview under the pointer; files show their icons.
static NSArray<NSDraggingItem*>* DragOutItems(const DragOutData& d,
                                              GpuiDragSource* src, NSPoint at) {
    NSMutableArray<NSDraggingItem*>* items = [NSMutableArray array];
    DragOutFormat formats[3];
    int n = DragOutFormats(d, formats);
    for (int i = 0; i < n; i++) {
        if (formats[i] == DragOutFormat::Text) {
            NSPasteboardItem* pi = [[NSPasteboardItem alloc] init];
            [pi setString:PromptNSString(d.text)
                  forType:NSPasteboardTypeString];
            NSDraggingItem* item =
                [[NSDraggingItem alloc] initWithPasteboardWriter:pi];
            [item setDraggingFrame:NSMakeRect(at.x, at.y, 1, 1) contents:nil];
            [items addObject:item];
        } else if (formats[i] == DragOutFormat::Paths) {
            const float kIconSize = 32;
            Str rest = d.paths;
            while (len(rest) > 0) {
                int k = 0;
                while (k < len(rest) && rest.s[k] != '\n') {
                    k++;
                }
                Str line(rest.s, k);
                int skip = k < len(rest) ? k + 1 : k;
                rest = Str(rest.s + skip, len(rest) - skip);
                if (len(line) == 0) {
                    continue;
                }
                NSString* path = PromptNSString(line);
                NSURL* url = [NSURL fileURLWithPath:path];
                NSDraggingItem* item =
                    [[NSDraggingItem alloc] initWithPasteboardWriter:url];
                NSRect frame =
                    NSMakeRect(at.x - kIconSize / 2, at.y - kIconSize / 2,
                               kIconSize, kIconSize);
                [item setDraggingFrame:frame
                              contents:[[NSWorkspace sharedWorkspace]
                                           iconForFile:path]];
                [items addObject:item];
            }
        } else {
            NSFilePromiseProvider* prov =
                [[NSFilePromiseProvider alloc] initWithFileType:@"public.png"
                                                       delegate:src];
            prov.userInfo = src;
            NSDraggingItem* item =
                [[NSDraggingItem alloc] initWithPasteboardWriter:prov];
            NSImage* img = [[NSImage alloc] initWithData:src->png];
            DragOutPreview p = img ? DragOutPreviewSize(d, (int)img.size.width,
                                                        (int)img.size.height, 1)
                                   : DragOutPreview{};
            if (p.w > 0) {
                // The view is flipped: hotY is down from the top.
                NSRect frame =
                    NSMakeRect(at.x - p.hotX, at.y - p.hotY, p.w, p.h);
                [item setDraggingFrame:frame
                              contents:DragOutPreviewImage(img, p)];
            } else {
                [item setDraggingFrame:NSMakeRect(at.x, at.y, 1, 1)
                              contents:nil];
            }
            // The preview goes first, under the pointer.
            [items insertObject:item atIndex:0];
        }
    }
    return items;
}

// AppKit's session needs the mouse event that started the drag, so this runs
// from the handler of the press or move rather than from the queue.
bool PlatStartDragOut(Window* win, DragOutRequest* req) {
    GpuiView* view = win && win->plat ? win->plat->view : nil;
    NSEvent* ev = [NSApp currentEvent];
    if (!view || !ev) {
        return false;
    }
    NSEventType t = [ev type];
    if (t != NSEventTypeLeftMouseDown && t != NSEventTypeLeftMouseDragged) {
        return false;
    }
    GpuiDragSource* src = [[GpuiDragSource alloc] init];
    src->req = req;
    if (req->data.imageBytes && req->data.imageBytesLen > 0) {
        src->png = [NSData dataWithBytes:req->data.imageBytes
                                  length:(NSUInteger)req->data.imageBytesLen];
        src->pngName = PromptNSString(DragOutImageName(req->data));
    }
    NSPoint at = [view convertPoint:[ev locationInWindow] fromView:nil];
    NSArray<NSDraggingItem*>* items = DragOutItems(req->data, src, at);
    if ([items count] == 0) {
        return false;
    }
    req->plat = (void*)CFBridgingRetain(src);
    NSDraggingSession* session =
        [view beginDraggingSessionWithItems:items event:ev source:src];
    session.animatesToStartingPositionsOnCancelOrFail = YES;
    return true;
}

// The UTTypes of `exts`, or nil for none. Looked up by name: UTType lives in
// UniformTypeIdentifiers, which AppKit loads and this library does not link.
static NSArray* PanelContentTypes(const Vec<Str>& exts) {
    Class cls = NSClassFromString(@"UTType");
    if (!cls || len(exts) == 0) {
        return nil;
    }
    NSMutableArray* types = [NSMutableArray array];
    for (int i = 0; i < len(exts); i++) {
        UTType* t = [cls typeWithFilenameExtension:PromptNSString(exts[i])];
        if (t) {
            [types addObject:t];
        }
    }
    return [types count] > 0 ? types : nil;
}

static void PanelSetDirectory(NSSavePanel* panel, Str dir) {
    if (len(dir) > 0) {
        [panel setDirectoryURL:[NSURL fileURLWithPath:PromptNSString(dir)
                                          isDirectory:YES]];
    }
}

// cx.prompt_for_paths. NSOpenPanel, run modally: it takes over the loop until
// the user is done, which is what the other two platforms' dialogs do too.
// An open panel has no file type menu: it shows what any of the filters
// takes, everything when one of them takes any file, and never says which.
TempStr PromptForPathTemp(Window* win, const PathPrompt& opts) {
    (void)win;
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    [panel setCanChooseFiles:opts.files ? YES : NO];
    [panel setCanChooseDirectories:opts.directories ? YES : NO];
    [panel setAllowsMultipleSelection:opts.multiple ? YES : NO];
    if (len(opts.title) > 0) {
        [panel setMessage:PromptNSString(opts.title)];
    }
    PanelSetDirectory(panel, opts.directory);
    Vec<Str> exts;
    bool any = opts.filters.count == 0;
    for (int32_t i = 0; i < opts.filters.count && !any; i++) {
        any = !PathPatternExts(opts.filters.items[i].patterns, &exts);
    }
    NSArray* types = any ? nil : PanelContentTypes(exts);
    if (types) {
        [panel setAllowedContentTypes:types];
    }
    if (opts.filters.chosen) {
        *opts.filters.chosen = -1;
    }
    if ([panel runModal] != NSModalResponseOK) {
        return {};
    }
    // several, with `multiple`, a path a line
    StrBuilder paths;
    for (NSURL* url in [panel URLs]) {
        const char* path = url ? [[url path] UTF8String] : nullptr;
        if (!path) {
            continue;
        }
        if (len(paths) > 0) {
            paths.AppendChar('\n');
        }
        paths.Append(Str(path));
        if (!opts.multiple) {
            break;
        }
    }
    if (len(paths) == 0) {
        return {};
    }
    return StrDupTemp(Str(paths.els, len(paths)));
}

// cx.prompt_for_new_path: NSSavePanel, run modally like the open panel
// (Rust begins it with a completion handler). A save panel has no file type
// menu either: it takes the extensions of the filter shown first, or the
// default extension, adds the first to a name typed without one and asks
// before overwriting. Another extension typed is kept.
TempStr PromptForNewPathTemp(Window* win, const NewPathPrompt& opts) {
    (void)win;
    NSSavePanel* panel = [NSSavePanel savePanel];
    if (!panel) {
        return {};
    }
    PanelSetDirectory(panel, opts.directory);
    if (len(opts.suggestedName) > 0) {
        [panel setNameFieldStringValue:PromptNSString(opts.suggestedName)];
    }
    if (len(opts.title) > 0) {
        [panel setMessage:PromptNSString(opts.title)];
    }
    Vec<Str> exts;
    const PathFilters& filters = opts.filters;
    if (filters.index >= 0 && filters.index < filters.count) {
        PathPatternExts(filters.items[filters.index].patterns, &exts);
    }
    Str ext = opts.defaultExt;
    if (len(ext) > 0 && ext.s[0] == '.') {
        ext = Str(ext.s + 1, len(ext) - 1);
    }
    if (len(exts) == 0 && len(ext) > 0) {
        VecAppend(exts, ext);
    }
    NSArray* types = PanelContentTypes(exts);
    if (types) {
        [panel setAllowedContentTypes:types];
        [panel setAllowsOtherFileTypes:YES];
    }
    if (filters.chosen) {
        *filters.chosen = -1;
    }
    if ([panel runModal] != NSModalResponseOK) {
        return {};
    }
    NSURL* url = [panel URL];
    const char* chosen =
        url && [url isFileURL] ? [[url path] UTF8String] : nullptr;
    if (!chosen || !*chosen) {
        return {};
    }
    TempStr path = StrDupTemp(Str(chosen));
    NSOperatingSystemVersion sequoia = {15, 0, 0};
    if ([[NSProcessInfo processInfo] isOperatingSystemAtLeastVersion:sequoia]) {
        path = PathDropRepeatedExtTemp(path);
    }
    return PathWithDefaultExtTemp(path, ext);
}

void PlatClipboardSetText(Window* win, Str text) {
    (void)win;
    NSString* s = [[NSString alloc] initWithBytes:text.s
                                           length:(NSUInteger)len(text)
                                         encoding:NSUTF8StringEncoding];
    if (!s) {
        return;
    }
    NSPasteboard* pb = [NSPasteboard generalPasteboard];
    [pb clearContents];
    [pb setString:s forType:NSPasteboardTypeString];
}

void WindowSetTextContentType(Window* win, Str value) {
    if (!win || !win->plat || !win->plat->view) return;
    // native.rs installs NSTextContent dynamically because older SDKs do not
    // publish the protocol. Keep that availability behavior while the view's
    // two accessors retain the value under ARC.
    static bool installed = false;
    if (!installed) {
        installed = true;
        Protocol* protocol = objc_getProtocol("NSTextContent");
        if (protocol) class_addProtocol([GpuiView class], protocol);
    }
    NSString* content = nil;
    if (value.s && len(value) > 0) {
        content = [[NSString alloc] initWithBytes:value.s
                                           length:(NSUInteger)len(value)
                                         encoding:NSUTF8StringEncoding];
    }
    [win->plat->view setContentType:content];
}

// PNG and TIFF both: AppKit apps read TIFF, the others PNG
bool PlatClipboardSetImage(Window* win, const uint8_t* png, int pngLen) {
    (void)win;
    NSData* data = [NSData dataWithBytes:png length:(NSUInteger)pngLen];
    NSImage* image = [[NSImage alloc] initWithData:data];
    if (!image) {
        return false;
    }
    NSPasteboard* pb = [NSPasteboard generalPasteboard];
    [pb clearContents];
    [pb declareTypes:@[ NSPasteboardTypePNG, NSPasteboardTypeTIFF ] owner:nil];
    [pb setData:data forType:NSPasteboardTypePNG];
    NSData* tiff = [image TIFFRepresentation];
    if (tiff) {
        [pb setData:tiff forType:NSPasteboardTypeTIFF];
    }
    return true;
}

Str PlatClipboardGetText(Arena* a, Window* win) {
    (void)win;
    NSPasteboard* pb = [NSPasteboard generalPasteboard];
    NSString* s = [pb stringForType:NSPasteboardTypeString];
    return s ? StrDup(a, Str([s UTF8String])) : Str();
}

ClipboardItem PlatClipboardGetItem(Arena* a, Window* win) {
    ClipboardItem out;
    out.text = PlatClipboardGetText(a, win);
    NSPasteboard* pb = [NSPasteboard generalPasteboard];
    NSData* image = [pb dataForType:NSPasteboardTypePNG];
    if (!image) image = [pb dataForType:NSPasteboardTypeTIFF];
    if (image && [image length] > 0 && [image length] <= INT_MAX) {
        int len = (int)[image length];
        auto* bytes = (uint8_t*)Alloc(a, len);
        if (bytes) {
            memcpy(bytes, [image bytes], (size_t)len);
            out.imageBytes = bytes;
            out.imageBytesLen = len;
        }
    }
    // files copied in Finder
    NSArray* urls =
        [pb readObjectsForClasses:@[ [NSURL class] ]
                          options:@{
                              NSPasteboardURLReadingFileURLsOnlyKey : @YES
                          }];
    if (urls.count > 0) {
        StrBuilder paths(a);
        for (NSURL* url in urls) {
            const char* path = url.fileSystemRepresentation;
            if (!path || !*path) {
                continue;
            }
            if (len(paths) > 0) {
                paths.AppendChar('\n');
            }
            paths.Append(Str(path));
        }
        out.externalPaths = paths.TakeStr();
    }
    return out;
}

// ─── app lifecycle ────────────────────────────────────────────────────────

// ─── waking the loop ──────────────────────────────────────────────────────
//
// The main dispatch queue, which is the one thing here that is documented to
// be safe to hand work to from any thread. The main run loop drains it in the
// common modes, and nextEventMatchingMask below runs that run loop, so the
// block lands even while the loop is asleep waiting for an event.
//
// It drains the queue itself rather than only waking: servicing the dispatch
// queue does not produce an NSEvent, so nextEventMatchingMask would otherwise
// go on sleeping until its deadline with the work already sitting there. The
// event posted after it is what gets the loop around to drawing whatever the
// tasks changed, and posting one is legal from the main thread, which is
// where the block runs.
static void WakeEventPost() {
    NSEvent* ev = [NSEvent otherEventWithType:NSEventTypeApplicationDefined
                                     location:NSZeroPoint
                                modifierFlags:0
                                    timestamp:0
                                 windowNumber:0
                                      context:nil
                                      subtype:0
                                        data1:0
                                        data2:0];
    if (ev) {
        [NSApp postEvent:ev atStart:YES];
    }
}

void PlatWake(App* app) {
    (void)app;
    dispatch_async(dispatch_get_main_queue(), ^{
      ExecDrain();
      WakeEventPost();
    });
}

// update_ime_position: ask AppKit to read the caret again. firstRect
// answers with where it is now.
static void UpdateImePosition(Window* win) {
    if (!win || !win->plat || !win->plat->view) {
        return;
    }
    NSTextInputContext* context = [win->plat->view inputContext];
    [context invalidateCharacterCoordinates];
}

bool PlatInit(App* app) {
    gPlatApp = app;
    PlatInstallImePositionUpdate(UpdateImePosition);
    @autoreleasepool {
        [NSApplication sharedApplication];
        // AppKit turns automatic termination back on at the end of launch
        // while it counts no windows. A process that is never frontmost (one
        // under lldb) then exits with status 0.
        NSProcessInfo* info = [NSProcessInfo processInfo];
        info.automaticTerminationSupportEnabled = NO;
        [info disableAutomaticTermination:@"gpui"];
        // Regular, not accessory: the examples run straight from a terminal
        // with no bundle, and this is what gives them a Dock tile and focus.
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        // NSApp.delegate is weak: this is what keeps the object alive.
        if (!gAppDelegate) {
            gAppDelegate = [[GpuiAppDelegate alloc] init];
        }
        [NSApp setDelegate:gAppDelegate];
        [NSApp finishLaunching];
        // finishLaunching's menu is empty when we are not a bundled app.
        // ⌘Q and the application menu's Quit row both call terminate:, which
        // the delegate turns into AppQuitAll.
        InstallDefaultAppMenu();
        [NSApp activateIgnoringOtherApps:YES];
    }
    return true;
}

void PlatShutdown(App* app) {
    (void)app;
}

Window* WindowOpen(App* app, Str title, int dipW, int dipH, WinOpts opts) {
    Window* win = WindowAlloc(app, opts);
    if (!win) {
        return nullptr;
    }
    @autoreleasepool {
        NSWindowStyleMask style =
            NSWindowStyleMaskTitled | NSWindowStyleMaskClosable;
        if (opts.minimizable) {
            style |= NSWindowStyleMaskMiniaturizable;
        }
        if (opts.resizable) {
            style |= NSWindowStyleMaskResizable;
        }
        if (opts.clientTitleBar) {
            style |= NSWindowStyleMaskFullSizeContentView;
        }
        if (opts.borderless) {
            style =
                NSWindowStyleMaskBorderless | NSWindowStyleMaskMiniaturizable;
            if (opts.resizable) {
                style |= NSWindowStyleMaskResizable;
            }
        }
        // Bounds::centered clamps to the display's visible bounds: the work
        // area below the menu bar and above the Dock. [window center] below
        // centres in the same area.
        NSRect screen = [[NSScreen mainScreen] visibleFrame];
        WindowClampToDisplay(&dipW, &dipH, (int)screen.size.width,
                             (int)screen.size.height);
        bool frameless = opts.frameless || opts.kind == WindowKind::PopUp;
        if (frameless) {
            style = NSWindowStyleMaskBorderless;
            if (opts.resizable) {
                style |= NSWindowStyleMaskResizable;
            }
        }
        NSRect frame = NSMakeRect(0, 0, dipW, dipH);
        Class cls = frameless && opts.activatable ? [GpuiKeyableWindow class]
                                                  : [NSWindow class];
        NSWindow* window =
            [[cls alloc] initWithContentRect:frame
                                   styleMask:style
                                     backing:NSBackingStoreBuffered
                                       defer:NO];
        if (!window) {
            return nullptr;
        }
        if (opts.utility && !frameless) {
            // Set after init, the way it is accepted on an NSWindow.
            [window setStyleMask:(style & ~NSWindowStyleMaskMiniaturizable) |
                                 NSWindowStyleMaskUtilityWindow];
        }
        if (frameless) {
            [window setHasShadow:YES];
            [window setMovableByWindowBackground:NO];
        }
        if (opts.kind == WindowKind::PopUp) {
            [window setLevel:NSPopUpMenuWindowLevel];
        } else if (opts.kind == WindowKind::Floating && !opts.parent) {
            [window setLevel:NSFloatingWindowLevel];
        }
        // Every colour the portable paint API accepts is an 8-bit sRGB
        // value.  Leaving AppKit to infer the window space on an EDR display
        // gives this otherwise-SDR view an RGBA-float16 backing store, which
        // doubles its IOSurface memory and makes Core Graphics convert every
        // fill and glyph through the float16 compositor.  Name the space the
        // content is actually in so the window gets an 8-bit SDR surface.
        [window setColorSpace:[NSColorSpace sRGBColorSpace]];
        if (opts.clientTitleBar) {
            [window setTitleVisibility:NSWindowTitleHidden];
            [window setTitlebarAppearsTransparent:YES];
            [window setTitlebarSeparatorStyle:NSTitlebarSeparatorStyleNone];
            [window setMovableByWindowBackground:NO];
        }
        GpuiView* view = [[GpuiView alloc] initWithFrame:frame];
        view->win = win;
        [view registerForDraggedTypes:@[
            NSPasteboardTypeFileURL, NSPasteboardTypeURL, NSPasteboardTypeString
        ]];
        GpuiWindowDelegate* del = [[GpuiWindowDelegate alloc] init];
        del->win = win;

        auto* pw = new PlatWindow();
        pw->window = window;
        pw->view = view;
        pw->delegate = del;
        win->plat = pw;

        [window setContentView:view];
        [window makeFirstResponder:view];
        [window setAcceptsMouseMovedEvents:YES];
        [window setReleasedWhenClosed:NO];
        [window setDelegate:del];
        if (opts.bounds.IsEmpty()) {
            [window center];
        } else {
            WindowSetFrameBounds(win, opts.bounds);
        }
        if (opts.minSize.w > 0 || opts.minSize.h > 0) {
            WindowSetMinSize(win, opts.minSize);
        }
        if (opts.maxSize.w > 0 || opts.maxSize.h > 0) {
            WindowSetMaxSize(win, opts.maxSize);
        }
        pw->hidden = !opts.show;
        pw->activateOnShow = opts.focus;
        if (opts.show) {
            WindowModalTake(win);
        }

        AppSetTitle(win, title);
        // Shown from Redraw, after the first paint. Order front here and the
        // window sits empty until AppRun, which is the flash of its area.
        PlatSetTimer(win, WindowTimerMs(win));
    }
    return win;
}

// Media and volume keys reach an app as NSEventTypeSystemDefined events of
// subtype 8 (NX_SUBTYPE_AUX_CONTROL_BUTTONS), never as keyDown:. data1 holds
// the NX_KEYTYPE_* code in its high 16 bits, the key state (0xA down, 0xB up)
// in bits 8-15 and the repeat flag in bit 0.
static int MediaKeyFor(int nxKey) {
    switch (nxKey) {
        case 0: // NX_KEYTYPE_SOUND_UP
            return KeyVolumeUp;
        case 1: // NX_KEYTYPE_SOUND_DOWN
            return KeyVolumeDown;
        case 7: // NX_KEYTYPE_MUTE
            return KeyVolumeMute;
        case 16: // NX_KEYTYPE_PLAY
            return KeyMediaPlayPause;
        case 17: // NX_KEYTYPE_NEXT
            return KeyMediaNextTrack;
        case 18: // NX_KEYTYPE_PREVIOUS
            return KeyMediaPrevTrack;
        default:
            return 0;
    }
}

// Hands a media key to the key window as a key press. The caller still sends
// every event on, so the system keeps its own handling of the key.
static void DispatchMediaKey(App* app, NSEvent* ev) {
    if ([ev type] != NSEventTypeSystemDefined || (int)[ev subtype] != 8) {
        return;
    }
    NSInteger data = [ev data1];
    int key = MediaKeyFor((int)((data & 0xFFFF0000) >> 16));
    if (!key) {
        return;
    }
    int state = (int)((data & 0xFF00) >> 8);
    NSWindow* keyWin = [NSApp keyWindow];
    for (int i = 0; i < app->windows.len; i++) {
        Window* w = app->windows[i];
        if (!w->plat || w->plat->window != keyWin) {
            continue;
        }
        NSEventModifierFlags mods = [ev modifierFlags];
        bool shift = (mods & NSEventModifierFlagShift) != 0;
        bool ctrl = (mods & NSEventModifierFlagControl) != 0;
        bool alt = (mods & NSEventModifierFlagOption) != 0;
        bool platform = (mods & NSEventModifierFlagCommand) != 0;
        if (state == 0xA) {
            KeyDownFlags flags;
            flags.held = (data & 0x1) != 0;
            WindowKeyDown(w, key, shift, ctrl, alt, platform, false, flags);
        } else if (state == 0xB) {
            WindowKeyUp(w, key, shift, ctrl, alt, platform, false);
        }
        return;
    }
}

int AppRun(App* app) {
    if (!app) {
        return 1;
    }
    gRunningApp = app;
    while (AppAnyWindowOpen(app)) {
        @autoreleasepool {
            // Drain everything queued, then draw, then block until the next
            // event or tick — the same shape as the X11 loop.
            for (;;) {
                NSEvent* ev = [NSApp nextEventMatchingMask:NSEventMaskAny
                                                 untilDate:nil
                                                    inMode:NSDefaultRunLoopMode
                                                   dequeue:YES];
                if (!ev) {
                    break;
                }
                DispatchMediaKey(app, ev);
                [NSApp sendEvent:ev];
            }
            if (!AppAnyWindowOpen(app)) {
                break;
            }

            RunLoopRedrawDirty<PlatWindow>(app, Redraw);

            bool anyDirty = false;
            double waitS = RunLoopWaitFor<PlatWindow>(app, &anyDirty);
            if (!anyDirty && ExecQueued() == 0) {
                NSDate* deadline =
                    waitS <= 0 ? [NSDate distantPast]
                               : [NSDate dateWithTimeIntervalSinceNow:waitS];
                NSEvent* ev = [NSApp nextEventMatchingMask:NSEventMaskAny
                                                 untilDate:deadline
                                                    inMode:NSDefaultRunLoopMode
                                                   dequeue:YES];
                if (ev) {
                    DispatchMediaKey(app, ev);
                    [NSApp sendEvent:ev];
                }
            }
            // PlatWake's block usually got here first; this is for a task
            // posted from the main thread itself, which never wakes anything.
            ExecDrain();

            RunLoopTickDue<PlatWindow>(app);
        }
    }
    gRunningApp = nullptr;
    return app->exitCode;
}

} // namespace gpui

// The process entry point. Examples implement GpuiMain(argc, argv).
int main(int argc, char** argv) {
    // Strip the -gpui-* flags here too, so an example parses the same argv on
    // every platform. -gpui-window itself is only honoured on Windows, where
    // the screenshot harness runs.
    argc = gpui::GpuiTakeRuntimeArgs(argc, argv);
    return GpuiMain(argc, argv);
}
