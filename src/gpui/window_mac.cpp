/* The Cocoa window: event pump, chrome routing, timers, clipboard, and the
   process entry point. The mirror of Window_win.cpp and Window_linux.cpp;
   everything any of them decides is delegated to WindowCommon.cpp.

   Objective-C++ under ARC (-x objective-c++ -fobjc-arc). The view is flipped,
   so a frame is drawn in points with the origin at the top left. One point is
   one DIP, which on a Retina display means the backing store is 2x and the
   drawing comes out crisp for free. */

#include "gpui/platform.h"
#include "gpui/paint.h"
#include "sys/executor.h"

#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>

#include <time.h>

@class GpuiView;
@class GpuiWindowDelegate;
@class GpuiAccessibilityElement;

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
    bool dirty = true;
    // Monotonic deadline for the next tick; 0 when the timer is off.
    double nextTick = 0;
    // Whether the window's class has been taught accessibilityHitTest:. Rust
    // installs it once, when the Root view is created; a Root here is built
    // every frame, so the window remembers instead.
    bool a11yInstalled = false;
};

double TimeNow() {
    static bool started = false;
    static struct timespec start = {};
    struct timespec now = {};
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (!started) {
        start = now;
        started = true;
    }
    return (double)(now.tv_sec - start.tv_sec) +
           (double)(now.tv_nsec - start.tv_nsec) / 1e9;
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
    if (!value.s || value.len <= 0) {
        return @"";
    }
    return [[NSString alloc] initWithBytes:value.s
                                    length:(NSUInteger)value.len
                                  encoding:NSUTF8StringEncoding];
}

static NSString* GpuiAccessibilityRole(gpui::AccessibilityRole role) {
    using gpui::AccessibilityRole;
    switch (role) {
        case AccessibilityRole::Alert:
        case AccessibilityRole::Heading:
        case AccessibilityRole::Label:
        case AccessibilityRole::Paragraph:
        case AccessibilityRole::TextRun:
            return NSAccessibilityStaticTextRole;
        case AccessibilityRole::Button:
        case AccessibilityRole::DefaultButton:
        case AccessibilityRole::DisclosureTriangle:
            return NSAccessibilityButtonRole;
        case AccessibilityRole::CheckBox:
        case AccessibilityRole::Switch:
            return NSAccessibilityCheckBoxRole;
        case AccessibilityRole::RadioButton:
            return NSAccessibilityRadioButtonRole;
        case AccessibilityRole::TextInput:
        case AccessibilityRole::SearchInput:
        case AccessibilityRole::DateInput:
        case AccessibilityRole::DateTimeInput:
        case AccessibilityRole::WeekInput:
        case AccessibilityRole::MonthInput:
        case AccessibilityRole::TimeInput:
        case AccessibilityRole::EmailInput:
        case AccessibilityRole::NumberInput:
        case AccessibilityRole::PhoneNumberInput:
        case AccessibilityRole::UrlInput:
            return NSAccessibilityTextFieldRole;
        case AccessibilityRole::PasswordInput:
            return NSAccessibilityTextFieldRole;
        case AccessibilityRole::MultilineTextInput:
            return NSAccessibilityTextAreaRole;
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

static int GpuiAccessibilityNodeIndex(gpui::Window* win, uint32_t id) {
    if (!win) {
        return -1;
    }
    for (int i = 0; i < win->accessibility.len; i++) {
        if (win->accessibility[i].id == id) {
            return i;
        }
    }
    return -1;
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
    int index = GpuiAccessibilityNodeIndex(gpuiWindow, gpuiNodeId);
    if (index < 0) {
        return nil;
    }
    int parent = gpuiWindow->accessibility[index].parent;
    return parent >= 0 ? (id)GpuiAccessibilityElementFor(
                             gpuiWindow, gpuiWindow->accessibility[parent].id)
                       : (id)gpuiWindow->plat->view;
}
- (NSArray*)accessibilityChildren {
    int index = GpuiAccessibilityNodeIndex(gpuiWindow, gpuiNodeId);
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
        node && node->input ? gpui::InputValue(node->input) : gpui::Str{};
    return gpui::RopeOffsetToOffsetUtf16(text, text.len);
}
- (NSRange)accessibilitySelectedTextRange {
    const gpui::AccessibilityNode* node = GpuiAccessibilityNode(self);
    if (!node || !node->input) {
        return NSMakeRange(NSNotFound, 0);
    }
    gpui::Str text = gpui::InputValue(node->input);
    int lo =
        gpui::RopeOffsetToOffsetUtf16(text, node->input->selectedRange.start);
    int hi =
        gpui::RopeOffsetToOffsetUtf16(text, node->input->selectedRange.end);
    return NSMakeRange((NSUInteger)std::min(lo, hi),
                       (NSUInteger)(std::max(lo, hi) - std::min(lo, hi)));
}
- (void)setAccessibilitySelectedTextRange:(NSRange)range {
    const gpui::AccessibilityNode* node = GpuiAccessibilityNode(self);
    if (!node || !node->input || range.location == NSNotFound) {
        return;
    }
    gpui::Str text = gpui::InputValue(node->input);
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
- (NSString*)accessibilityStringForRange:(NSRange)range {
    const gpui::AccessibilityNode* node = GpuiAccessibilityNode(self);
    if (!node || !node->input || range.location == NSNotFound) {
        return nil;
    }
    gpui::Str text = gpui::InputValue(node->input);
    int lo = gpui::RopeOffsetUtf16ToOffset(text, (int)range.location);
    int hi = gpui::RopeOffsetUtf16ToOffset(
        text, (int)(range.location + range.length));
    return GpuiAccessibilityString(gpui::Str(text.s + lo, hi - lo));
}
- (NSRange)accessibilityRangeForPosition:(NSPoint)point {
    const gpui::AccessibilityNode* node = GpuiAccessibilityNode(self);
    if (!node || !node->input || !gpuiWindow->plat || !gpuiWindow->plat->view) {
        return NSMakeRange(NSNotFound, 0);
    }
    NSView* view = (NSView*)gpuiWindow->plat->view;
    NSPoint inWindow = [[view window] convertPointFromScreen:point];
    NSPoint local = [view convertPoint:inWindow fromView:nil];
    int offset = gpui::InputIndexForPosition(node->input, &gpuiWindow->paint,
                                             (float)local.x, (float)local.y);
    gpui::Str text = gpui::InputValue(node->input);
    return NSMakeRange((NSUInteger)gpui::RopeOffsetToOffsetUtf16(text, offset),
                       0);
}
- (NSRect)accessibilityFrameForRange:(NSRange)range {
    (void)range;
    return [self accessibilityFrame];
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
}
@end

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

- (void)setMarkedText:(id)string
        selectedRange:(NSRange)selected
     replacementRange:(NSRange)replacement {
    gpui::InputState* in = [self gpuiInput];
    if (!in) {
        return;
    }
    NSString* str = [string isKindOfClass:[NSAttributedString class]]
                        ? [(NSAttributedString*)string string]
                        : (NSString*)string;
    const char* utf8 = [str UTF8String];
    gpui::Str text = utf8 ? gpui::Str(utf8, (int)strlen(utf8)) : gpui::Str{};
    gpui::Str doc = gpui::InputValue(in);
    gpui::Selection range = {};
    bool hasRange = replacement.location != NSNotFound;
    if (hasRange) {
        range.start = gpui::Utf16OffsetToUtf8(doc, (int)replacement.location);
        range.end = gpui::Utf16OffsetToUtf8(
            doc, (int)(replacement.location + replacement.length));
    }
    // The selection Cocoa reports is inside the new text, in UTF-16 units.
    gpui::Selection sel = {};
    bool hasSel = selected.location != NSNotFound;
    if (hasSel) {
        sel.start = gpui::Utf16OffsetToUtf8(text, (int)selected.location);
        sel.end = gpui::Utf16OffsetToUtf8(
            text, (int)(selected.location + selected.length));
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
    NSString* str = [string isKindOfClass:[NSAttributedString class]]
                        ? [(NSAttributedString*)string string]
                        : (NSString*)string;
    const char* utf8 = [str UTF8String];
    if (!utf8 || !*utf8) {
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
        range.start = gpui::Utf16OffsetToUtf8(doc, (int)replacement.location);
        range.end = gpui::Utf16OffsetToUtf8(
            doc, (int)(replacement.location + replacement.length));
    }
    // A null range is the marked run, which is what a commit replaces.
    gpui::InputReplaceTextInRange(in, win->app, win,
                                  hasRange ? &range : nullptr,
                                  gpui::Str(utf8, (int)strlen(utf8)));
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
    if (lo < 0 || hi > doc.len || hi < lo) {
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
    (void)range;
    if (actual) {
        *actual = range;
    }
    gpui::InputState* in = [self gpuiInput];
    if (!in) {
        return NSZeroRect;
    }
    // Where the candidate list hangs from: the caret, in screen space.
    float x = in->lastBounds.x + in->caretX - in->scrollX;
    float y = in->lastBounds.y - in->scrollY;
    NSRect local = NSMakeRect(x, y, 1, in->lastLineH);
    NSRect inWindow = [self convertRect:local toView:nil];
    return [[self window] convertRectToScreen:inWindow];
}

- (NSUInteger)characterIndexForPoint:(NSPoint)point {
    (void)point;
    return NSNotFound;
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

- (void)windowWillClose:(NSNotification*)note {
    (void)note;
    __attribute__((objc_precise_lifetime)) GpuiWindowDelegate* keepAlive = self;
    gpui::PlatWindow* plat = win ? win->plat : nullptr;
    if (plat) {
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

// The release of a key. Only the activation keys do anything with one, and
// they need no text, so this is the modifier and code half of the press.
void WindowMacKeyUp(Window* win, NSEvent* event) {
    if (!win) {
        return;
    }
    NSEventModifierFlags mods = [event modifierFlags];
    NSString* bare = [event charactersIgnoringModifiers];
    unichar first = [bare length] > 0 ? [bare characterAtIndex:0] : 0;
    int key = KeyFor(first);
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
    int key = KeyFor(first);
    if (key) {
        WindowKeyDown(win, key, shift, ctrl, alt, platform, function);
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

static void Redraw(Window* win) {
    PlatWindow* pw = win->plat;
    if (!pw || !pw->view) {
        return;
    }
    pw->dirty = false;
    win->maximized = [pw->window isZoomed] ? true : false;
    [pw->view display];
}

// ─── window commands ──────────────────────────────────────────────────────

void AppQuit(Window* win) {
    if (win && win->plat && win->plat->window) {
        // windowWillClose is what calls WindowClosed.
        [win->plat->window close];
    }
}

void AppInvalidate(Window* win) {
    if (win) {
        win->invalidations++;
    }
    if (win && win->plat) {
        win->plat->dirty = true;
    }
}

bool WindowClientDecorated(Window* win) {
    // The traffic lights are Cocoa's over a transparent title bar, and
    // component::TitleBar draws no controls of its own here.
    return win && (win->opts.clientTitleBar || win->opts.borderless);
}

void AppActivate(Window* win) {
    if (!win || !win->plat) {
        return;
    }
    if ([win->plat->window isMiniaturized]) {
        [win->plat->window deminiaturize:nil];
    }
    [NSApp activateIgnoringOtherApps:YES];
    [win->plat->window makeKeyAndOrderFront:nil];
}

void AppMinimize(Window* win) {
    if (win && win->plat) {
        [win->plat->window miniaturize:nil];
    }
}

void AppToggleMaximize(Window* win) {
    if (win && win->plat) {
        [win->plat->window zoom:nil];
        win->maximized = [win->plat->window isZoomed] ? true : false;
    }
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
                                           length:(NSUInteger)title.len
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

void PlatSetCursor(Window* win, CursorKind kind) {
    (void)win;
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
    } else {
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
    if (StrStartsWithAny(key, "fF") && key.len >= 2 && key.s[1] >= '0' &&
        key.s[1] <= '9') {
        int n = key.s[1] - '0';
        if (key.len == 3 && key.s[2] >= '0' && key.s[2] <= '9') {
            n = n * 10 + (key.s[2] - '0');
        } else if (key.len != 2) {
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
    if (key.len == 1) {
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
    if (!url.s || url.len <= 0) {
        return;
    }
    NSString* s = [[NSString alloc] initWithBytes:url.s
                                           length:(NSUInteger)url.len
                                         encoding:NSUTF8StringEncoding];
    NSURL* u = s ? [NSURL URLWithString:s] : nil;
    if (u) {
        [[NSWorkspace sharedWorkspace] openURL:u];
    }
}

// cx.prompt_for_paths. NSOpenPanel, run modally: it takes over the loop until
// the user is done, which is what the other two platforms' dialogs do too.
TempStr PromptForPathTemp(Window* win, const PathPrompt& opts) {
    (void)win;
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    [panel setCanChooseFiles:opts.files ? YES : NO];
    [panel setCanChooseDirectories:opts.directories ? YES : NO];
    [panel setAllowsMultipleSelection:NO];
    if (opts.title.len > 0) {
        NSString* t = [[NSString alloc] initWithBytes:opts.title.s
                                               length:(NSUInteger)opts.title.len
                                             encoding:NSUTF8StringEncoding];
        if (t) {
            [panel setMessage:t];
        }
    }
    if ([panel runModal] != NSModalResponseOK) {
        return {};
    }
    NSURL* url = [[panel URLs] firstObject];
    const char* path = url ? [[url path] UTF8String] : nullptr;
    if (!path) {
        return {};
    }
    return StrDupTemp(Str(path));
}

void ClipboardSetText(Window* win, Str text) {
    (void)win;
    if (!text.s || text.len <= 0) {
        return;
    }
    NSString* s = [[NSString alloc] initWithBytes:text.s
                                           length:(NSUInteger)text.len
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
    if (value.s && value.len > 0) {
        content = [[NSString alloc] initWithBytes:value.s
                                           length:(NSUInteger)value.len
                                         encoding:NSUTF8StringEncoding];
    }
    [win->plat->view setContentType:content];
}

Str ClipboardGetText(Arena* a, Window* win) {
    (void)win;
    NSPasteboard* pb = [NSPasteboard generalPasteboard];
    NSString* s = [pb stringForType:NSPasteboardTypeString];
    if (!s) {
        return {};
    }
    const char* utf8 = [s UTF8String];
    if (!utf8) {
        return {};
    }
    int n = (int)strlen(utf8);
    char* buf = (char*)Alloc(a, n + 1);
    if (!buf) {
        return {};
    }
    memcpy(buf, utf8, (size_t)n + 1);
    return Str(buf, n);
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

bool PlatInit(App* app) {
    (void)app;
    @autoreleasepool {
        [NSApplication sharedApplication];
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
            NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
            NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;
        if (opts.clientTitleBar) {
            style |= NSWindowStyleMaskFullSizeContentView;
        }
        if (opts.borderless) {
            style = NSWindowStyleMaskBorderless | NSWindowStyleMaskResizable |
                    NSWindowStyleMaskMiniaturizable;
        }
        // GPUI's primary_display().bounds() is the full display, not the
        // work area below the menu bar and above the Dock.
        NSRect screen = [[NSScreen mainScreen] frame];
        WindowClampToDisplay(&dipW, &dipH, (int)screen.size.width,
                             (int)screen.size.height);
        NSRect frame = NSMakeRect(0, 0, dipW, dipH);
        NSWindow* window =
            [[NSWindow alloc] initWithContentRect:frame
                                        styleMask:style
                                          backing:NSBackingStoreBuffered
                                            defer:NO];
        if (!window) {
            return nullptr;
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
        [window center];

        AppSetTitle(win, title);
        [window makeKeyAndOrderFront:nil];
        // AppNew may run well before a window is opened (system_monitor does
        // an initial metrics sweep). Activate again now that Cocoa has a key
        // window to bring forward.
        [NSApp activateIgnoringOtherApps:YES];
        PlatSetTimer(win, WindowTimerMs(win));
    }
    return win;
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
                [NSApp sendEvent:ev];
            }
            if (!AppAnyWindowOpen(app)) {
                break;
            }

            for (int i = 0; i < app->windows.len; i++) {
                Window* w = app->windows[i];
                if (w->plat && w->plat->dirty) {
                    Redraw(w);
                }
            }

            double now = TimeNow();
            double waitS = 1.0;
            bool anyDirty = false;
            for (int i = 0; i < app->windows.len; i++) {
                Window* w = app->windows[i];
                if (!w->plat) {
                    continue;
                }
                if (w->plat->dirty) {
                    anyDirty = true;
                }
                if (w->plat->nextTick > 0) {
                    double d = w->plat->nextTick - now;
                    if (d < waitS) {
                        waitS = d;
                    }
                }
            }
            if (!anyDirty && ExecQueued() == 0) {
                NSDate* deadline =
                    waitS <= 0 ? [NSDate distantPast]
                               : [NSDate dateWithTimeIntervalSinceNow:waitS];
                NSEvent* ev = [NSApp nextEventMatchingMask:NSEventMaskAny
                                                 untilDate:deadline
                                                    inMode:NSDefaultRunLoopMode
                                                   dequeue:YES];
                if (ev) {
                    [NSApp sendEvent:ev];
                }
            }
            // PlatWake's block usually got here first; this is for a task
            // posted from the main thread itself, which never wakes anything.
            ExecDrain();

            now = TimeNow();
            for (int i = 0; i < app->windows.len; i++) {
                Window* w = app->windows[i];
                if (!w->plat || w->plat->nextTick <= 0) {
                    continue;
                }
                if (now >= w->plat->nextTick) {
                    // WindowTimerTick re-arms through PlatSetTimer.
                    WindowTimerTick(w);
                }
            }
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
