/* wry/src/wkwebview/ — the WKWebView backend.
 *
 * Part of the C++ port of lb-wry 0.53.3 (see src/wry/readme.md).
 *
 * Objective-C++ under ARC, like the rest of the macOS half of this tree: the
 * amalgam is compiled `-x objective-c++ -fobjc-arc`, so an Objective-C
 * pointer held in one of the C++ structs below is a strong reference and the
 * `Retained<T>` fields of Rust's `InnerWebView` need nothing said about them.
 *
 * Rust's delegates are `define_class!` invocations with an ivars struct;
 * here each is a real `@interface` at file scope — an Objective-C class
 * cannot live inside a C++ namespace — holding the `wry::WebView*` those
 * ivars would have held. The file is laid out the way window_mac.cpp is: the
 * namespace's types first, then the classes, then the API.
 *
 * Where this differs from wkwebview/mod.rs, and why:
 *
 *   - **A webview that is not a child is added as a subview** that resizes
 *     with the parent, rather than becoming the window's `contentView`.
 *     Rust replaces the content view with a `WryWebViewParent` so the page
 *     gets key events; the content view here is the gpui view that draws
 *     everything else and owns the window's input, so evicting it would take
 *     the application with it.
 *   - **No download delegate and no cookies**, the same two omissions the
 *     Windows backend has and for the same reasons (readme.md).
 *   - **`set_theme` and `set_memory_usage_level` answer false.** Both are
 *     `WebViewExtWindows` in Rust and have no WKWebView counterpart.
 *   - **`reparent` is ours**, not the crate's — Rust has it on Windows only.
 *     It is `removeFromSuperview` plus `addSubview`, which is what the one
 *     caller (a webview following its gpui window) means by it.
 */

#include "wry/wry.h"

#import <Cocoa/Cocoa.h>
// WebKit.h also imports WebKitLegacy, whose Objective-C JSContext and JSValue
// classes collide with QuickJS's C API when the dist build puts every source
// in one translation unit. Wry uses only the modern WK API, so include that
// surface directly and keep the legacy JavaScriptCore bridge out.
#import <WebKit/WKFrameInfo.h>
#import <WebKit/WKNavigation.h>
#import <WebKit/WKNavigationAction.h>
#import <WebKit/WKNavigationDelegate.h>
#import <WebKit/WKOpenPanelParameters.h>
#import <WebKit/WKPreferences.h>
#import <WebKit/WKScriptMessage.h>
#import <WebKit/WKScriptMessageHandler.h>
#import <WebKit/WKSecurityOrigin.h>
#import <WebKit/WKUIDelegate.h>
#import <WebKit/WKURLSchemeHandler.h>
#import <WebKit/WKURLSchemeTask.h>
#import <WebKit/WKUserContentController.h>
#import <WebKit/WKUserScript.h>
#import <WebKit/WKWebView.h>
#import <WebKit/WKWebViewConfiguration.h>
#import <WebKit/WKWebsiteDataStore.h>
#import <WebKit/WKWindowFeatures.h>

@class GpuiWryScriptHandler;
@class GpuiWryNavigationDelegate;
@class GpuiWryUIDelegate;
@class GpuiWryTitleObserver;
@class GpuiWrySchemeHandler;
@class GpuiWryWebView;

namespace wry {

using base::AllocStrTemp;
using base::logf;
using base::Str;
using base::StrDup;
using base::StrFree;
using base::Vec;

// `IPC_MESSAGE_HANDLER_NAME`.
static NSString* const kIpcHandlerName = @"ipc";

// The script wry injects into every webview: a frozen `window.ipc` whose
// postMessage is WebKit's own message handler.
static NSString* const kIpcScript =
    @"Object.defineProperty(window, 'ipc', {\n"
    @"  value: Object.freeze({postMessage: function(s) "
    @"{window.webkit.messageHandlers.ipc.postMessage(s);}})\n"
    @"});";

struct ProtocolCopy {
    Str name; // heap
    void* ctx;
    void (*handler)(void* ctx, Str id, const Request* request,
                    RequestResponder* responder);
};

struct WebView {
    Str id = {}; // heap
    WKWebView* webview = nil;
    WKUserContentController* manager = nil;
    NSView* parentView = nil;
    bool isChild = false;
    bool visible = true;

    void* ctx = nullptr;
    void (*ipcHandler)(void* ctx, Str url, Str body) = nullptr;
    bool (*navigationHandler)(void* ctx, Str url) = nullptr;
    void (*documentTitleChangedHandler)(void* ctx, Str title) = nullptr;
    void (*onPageLoadHandler)(void* ctx, PageLoadEvent event,
                              Str url) = nullptr;
    NewWindowResponse (*newWindowReqHandler)(
        void* ctx, Str url, const NewWindowFeatures* features,
        WebView** createdWebView) = nullptr;

    Vec<ProtocolCopy> protocols;

    // `pending_scripts`: an eval before the first navigation commits has
    // nothing to run in, so it is held and replayed by didCommitNavigation.
    // Rust keeps an `Option<Vec<String>>` and takes the option to close it.
    Vec<Str> pendingScripts;
    bool pendingOpen = true;

    // The scheme tasks a handler may still answer. Rust checks a per-task
    // UUID for the same reason: a task that has stopped is a dangling
    // pointer, and an outdated responder must not touch it.
    NSMutableSet* liveTasks = nil;

    // The delegates, held because nothing else holds them — Rust keeps the
    // same set of `Retained<..>` fields for the reference count.
    GpuiWryScriptHandler* ipcDelegate = nil;
    GpuiWryNavigationDelegate* navDelegate = nil;
    GpuiWryUIDelegate* uiDelegate = nil;
    GpuiWryTitleObserver* titleObserver = nil;
    NSMutableArray* schemeHandlers = nil;
};

// `RequestAsyncResponder`. The task is held until the handler answers, which
// it may do from another thread; delivery hops to the main thread, the only
// one WebKit takes a `didReceive*` on.
struct RequestResponder {
    // Kept independently of WebView so a worker may answer after the view
    // was closed. WebViewFree empties the set; the late delivery observes
    // that its task is no longer live and drops it. This is Rust's global
    // webview-id plus per-task UUID check without a process-global map.
    NSMutableSet* liveTasks = nil;
    id<WKURLSchemeTask> task = nil;
    NSURL* url = nil;
    int32_t answered = 0;
};

// ─── strings ─────────────────────────────────────────────────────────────

static NSString* ToNS(Str s) {
    if (!s.s || len(s) == 0) {
        return @"";
    }
    NSString* res = [[NSString alloc] initWithBytes:s.s
                                             length:(NSUInteger)len(s)
                                           encoding:NSUTF8StringEncoding];
    return res ? res : @"";
}

static Str FromNSTemp(NSString* s) {
    if (!s) {
        return {};
    }
    const char* utf8 = [s UTF8String];
    if (!utf8) {
        return {};
    }
    int n = (int)strlen(utf8);
    Str res = AllocStrTemp(n + 1);
    if (!res.s) {
        return {};
    }
    memcpy(res.s, utf8, (size_t)n);
    res.s[n] = 0;
    res.len = n;
    return res;
}

// `url_from_webview`.
static Str UrlFromWebView(WKWebView* webview) {
    NSURL* url = webview.URL;
    return url ? FromNSTemp(url.absoluteString) : Str();
}

// Defined below, once the classes that call them are in scope.
static void FlushPendingScripts(WebView* wv);
static void HandleSchemeTask(WebView* wv, int index, id<WKURLSchemeTask> task);

} // namespace wry

// ─── the delegates ───────────────────────────────────────────────────────

/** `WryWebView`. WKWebView itself consumes Command-key equivalents before
    the containing GPUI window can route its menu actions. The pinned class
    deliberately declines them for a child webview, and also owns the
    macOS-only first-click and navigation-button behavior. */
@interface GpuiWryWebView : WKWebView
@property(nonatomic, assign) BOOL childWebView;
@property(nonatomic, assign) BOOL acceptFirstMouseEnabled;
@end

@implementation GpuiWryWebView
- (BOOL)performKeyEquivalent:(NSEvent*)event {
    if (self.childWebView) {
        return NO;
    }
    return [super performKeyEquivalent:event];
}

- (BOOL)acceptsFirstMouse:(NSEvent*)event {
    (void)event;
    return self.acceptFirstMouseEnabled;
}

- (NSString*)syntheticMouseScript:(NSEvent*)event
                             down:(BOOL)down
                             back:(BOOL)back {
    NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    NSUInteger x = p.x < 0 ? 0 : (NSUInteger)p.x;
    NSUInteger y = p.y < 0 ? 0 : (NSUInteger)p.y;
    NSUInteger buttons = [NSEvent pressedMouseButtons];
    NSEventModifierFlags mods = event.modifierFlags;
    return [NSString
        stringWithFormat:
            @"(() => { const el = document.elementFromPoint(%lu,%lu);"
             "if (!el) return; const ev = new MouseEvent('%@', {"
             "view:window,button:%d,buttons:%lu,x:%lu,y:%lu,bubbles:true,"
             "detail:%ld,cancelBubble:false,cancelable:true,clientX:%lu,"
             "clientY:%lu,composed:true,layerX:%lu,layerY:%lu,pageX:%lu,"
             "pageY:%lu,screenX:window.screenX+%lu,screenY:window.screenY+%lu,"
             "ctrlKey:%s,metaKey:%s,shiftKey:%s,altKey:%s});"
             "el.dispatchEvent(ev); if (!ev.defaultPrevented && '%@' === "
             "'mouseup') { if (ev.button === 3) history.back();"
             "if (ev.button === 4) history.forward(); } })()",
            (unsigned long)x, (unsigned long)y,
            down ? @"mousedown" : @"mouseup", back ? 3 : 4,
            (unsigned long)buttons, (unsigned long)x, (unsigned long)y,
            (long)event.clickCount, (unsigned long)x, (unsigned long)y,
            (unsigned long)x, (unsigned long)y, (unsigned long)x,
            (unsigned long)y, (unsigned long)x, (unsigned long)y,
            (mods & NSEventModifierFlagControl) ? "true" : "false",
            (mods & NSEventModifierFlagCommand) ? "true" : "false",
            (mods & NSEventModifierFlagShift) ? "true" : "false",
            (mods & NSEventModifierFlagOption) ? "true" : "false",
            down ? @"mousedown" : @"mouseup"];
}

- (void)otherMouseDown:(NSEvent*)event {
    NSInteger button = event.buttonNumber;
    if (event.type == NSEventTypeOtherMouseDown &&
        (button == 3 || button == 4)) {
        [self evaluateJavaScript:[self syntheticMouseScript:event
                                                       down:YES
                                                       back:button == 3]
               completionHandler:nil];
        return;
    }
    [self mouseDown:event];
}

- (void)otherMouseUp:(NSEvent*)event {
    NSInteger button = event.buttonNumber;
    if (event.type == NSEventTypeOtherMouseUp && (button == 3 || button == 4)) {
        [self evaluateJavaScript:[self syntheticMouseScript:event
                                                       down:NO
                                                       back:button == 3]
               completionHandler:nil];
        return;
    }
    [self mouseUp:event];
}
@end

/** `WryWebViewDelegate` — the IPC message handler. */
@interface GpuiWryScriptHandler : NSObject <WKScriptMessageHandler>
@property(nonatomic, assign) wry::WebView* wv;
@end

@implementation GpuiWryScriptHandler
- (void)userContentController:(WKUserContentController*)controller
      didReceiveScriptMessage:(WKScriptMessage*)message {
    (void)controller;
    wry::WebView* wv = self.wv;
    if (!wv || !wv->ipcHandler) {
        return;
    }
    if (![message.body isKindOfClass:[NSString class]]) {
        // Rust logs "WebView received invalid IPC call" and drops it.
        return;
    }
    NSString* body = (NSString*)message.body;
    NSURL* url = message.frameInfo.request.URL;
    wv->ipcHandler(wv->ctx,
                   url ? wry::FromNSTemp(url.absoluteString) : wry::Str(),
                   wry::FromNSTemp(body));
}
@end

/** `DocumentTitleChangedObserver` — KVO on the webview's title. */
@interface GpuiWryTitleObserver : NSObject
@property(nonatomic, assign) wry::WebView* wv;
@end

@implementation GpuiWryTitleObserver
- (void)observeValueForKeyPath:(NSString*)keyPath
                      ofObject:(id)object
                        change:(NSDictionary*)change
                       context:(void*)context {
    (void)change;
    (void)context;
    wry::WebView* wv = self.wv;
    if (!wv || !wv->documentTitleChangedHandler ||
        ![keyPath isEqualToString:@"title"]) {
        return;
    }
    NSString* title = [object title];
    wv->documentTitleChangedHandler(wv->ctx, wry::FromNSTemp(title));
}
@end

/** `WryNavigationDelegate`, minus its two download methods. */
@interface GpuiWryNavigationDelegate : NSObject <WKNavigationDelegate>
@property(nonatomic, assign) wry::WebView* wv;
@end

@implementation GpuiWryNavigationDelegate
- (void)webView:(WKWebView*)webView
    decidePolicyForNavigationAction:(WKNavigationAction*)action
                    decisionHandler:
                        (void (^)(WKNavigationActionPolicy))handler {
    (void)webView;
    wry::WebView* wv = self.wv;
    // `shouldPerformDownload` is macOS 11.3+, and with no download handler
    // wry cancels the navigation rather than letting it through.
    if ([action respondsToSelector:@selector(shouldPerformDownload)] &&
        action.shouldPerformDownload) {
        handler(WKNavigationActionPolicyCancel);
        return;
    }
    if (!wv || !wv->navigationHandler) {
        handler(WKNavigationActionPolicyAllow);
        return;
    }
    NSURL* url = action.request.URL;
    bool allow = wv->navigationHandler(
        wv->ctx, url ? wry::FromNSTemp(url.absoluteString) : wry::Str());
    handler(allow ? WKNavigationActionPolicyAllow
                  : WKNavigationActionPolicyCancel);
}

- (void)webView:(WKWebView*)webView
    didCommitNavigation:(WKNavigation*)navigation {
    (void)navigation;
    wry::WebView* wv = self.wv;
    if (!wv) {
        return;
    }
    if (wv->onPageLoadHandler) {
        wv->onPageLoadHandler(wv->ctx, wry::PageLoadEvent::Started,
                              wry::UrlFromWebView(webView));
    }
    wry::FlushPendingScripts(wv);
}

- (void)webView:(WKWebView*)webView
    didFinishNavigation:(WKNavigation*)navigation {
    (void)navigation;
    wry::WebView* wv = self.wv;
    if (wv && wv->onPageLoadHandler) {
        wv->onPageLoadHandler(wv->ctx, wry::PageLoadEvent::Finished,
                              wry::UrlFromWebView(webView));
    }
}
@end

/** The delegate of a window `window.open` opened, which is how the UI
    delegate learns to let go of it. `WryNSWindowDelegate`. */
@interface GpuiWryNewWindowDelegate : NSObject <NSWindowDelegate>
@property(nonatomic, weak) GpuiWryUIDelegate* owner;
@property(nonatomic, weak) NSWindow* window;
@end

/** `WryWebViewUIDelegate`. */
@interface GpuiWryUIDelegate : NSObject <WKUIDelegate>
@property(nonatomic, assign) wry::WebView* wv;
// The windows `window.open` has opened, kept alive the way Rust's
// `new_windows: Rc<RefCell<Vec<NewWindow>>>` is.
@property(nonatomic, strong) NSMutableArray* openedWindows;
- (void)forgetWindow:(NSWindow*)window;
@end

@implementation GpuiWryNewWindowDelegate
- (void)windowWillClose:(NSNotification*)notification {
    (void)notification;
    [self.owner forgetWindow:self.window];
}
@end

@implementation GpuiWryUIDelegate
- (void)forgetWindow:(NSWindow*)window {
    if (!window || !self.openedWindows) {
        return;
    }
    for (NSUInteger i = 0; i < self.openedWindows.count; i++) {
        NSArray* entry = self.openedWindows[i];
        if (entry[0] == window) {
            [self.openedWindows removeObjectAtIndex:i];
            return;
        }
    }
}

- (void)webView:(WKWebView*)webView
    runOpenPanelWithParameters:(WKOpenPanelParameters*)parameters
              initiatedByFrame:(WKFrameInfo*)frame
             completionHandler:(void (^)(NSArray<NSURL*>*))handler {
    (void)webView;
    (void)frame;
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.canChooseFiles = YES;
    panel.allowsMultipleSelection = parameters.allowsMultipleSelection;
    panel.canChooseDirectories = parameters.allowsDirectories;
    if ([panel runModal] == NSModalResponseOK) {
        handler(panel.URLs);
    } else {
        handler(nil);
    }
}

- (void)webView:(WKWebView*)webView
    requestMediaCapturePermissionForOrigin:(WKSecurityOrigin*)origin
                          initiatedByFrame:(WKFrameInfo*)frame
                                      type:(WKMediaCaptureType)type
                           decisionHandler:
                               (void (^)(WKPermissionDecision))handler {
    (void)webView;
    (void)origin;
    (void)frame;
    (void)type;
    handler(WKPermissionDecisionGrant);
}

- (WKWebView*)webView:(WKWebView*)webView
    createWebViewWithConfiguration:(WKWebViewConfiguration*)configuration
               forNavigationAction:(WKNavigationAction*)action
                    windowFeatures:(WKWindowFeatures*)windowFeatures {
    wry::WebView* wv = self.wv;
    if (!wv || !wv->newWindowReqHandler) {
        return nil;
    }
    NSURL* url = action.request.URL;
    wry::NewWindowFeatures features;
    features.opener = wv;
    features.targetConfiguration = (__bridge void*)configuration;
    if (windowFeatures.x && windowFeatures.y) {
        features.hasPosition = true;
        features.x = windowFeatures.x.doubleValue;
        features.y = windowFeatures.y.doubleValue;
    }
    if (windowFeatures.width && windowFeatures.height) {
        features.hasSize = true;
        features.width = windowFeatures.width.doubleValue;
        features.height = windowFeatures.height.doubleValue;
    }

    wry::WebView* created = nullptr;
    wry::NewWindowResponse response = wv->newWindowReqHandler(
        wv->ctx, url ? wry::FromNSTemp(url.absoluteString) : wry::Str(),
        &features, &created);
    if (response == wry::NewWindowResponse::Deny) {
        return nil;
    }
    if (response == wry::NewWindowResponse::Create) {
        return created ? created->webview : nil;
    }

    // Allow: open the window ourselves and hand WebKit the view inside it,
    // which is what the crate's `NewWindowResponse::Allow` arm does.
    NSWindow* current = webView.window;
    NSRect defaults = current ? current.frame : NSMakeRect(0, 0, 800, 600);
    NSSize size =
        NSMakeSize(features.hasSize ? features.width : defaults.size.width,
                   features.hasSize ? features.height : defaults.size.height);
    NSPoint origin = defaults.origin;
    if (features.hasPosition) {
        NSScreen* screen = current ? current.screen : [NSScreen mainScreen];
        CGFloat screenHeight = screen ? screen.frame.size.height : size.height;
        origin =
            NSMakePoint(features.x, screenHeight - features.y - size.height);
    }

    NSWindowStyleMask mask = NSWindowStyleMaskTitled |
                             NSWindowStyleMaskClosable |
                             NSWindowStyleMaskMiniaturizable;
    bool resizable = windowFeatures.allowsResizing
                         ? windowFeatures.allowsResizing.boolValue
                         : true;
    if (resizable) {
        mask |= NSWindowStyleMaskResizable;
    }

    NSRect rect = NSMakeRect(origin.x, origin.y, size.width, size.height);
    NSWindow* window =
        [[NSWindow alloc] initWithContentRect:rect
                                    styleMask:mask
                                      backing:NSBackingStoreBuffered
                                        defer:NO];
    // The window is made outside a window controller, so it must not release
    // itself when closed — the array below is what owns it.
    window.releasedWhenClosed = NO;

    WKWebView* child = [[WKWebView alloc] initWithFrame:window.frame
                                          configuration:configuration];
    GpuiWryNewWindowDelegate* delegate =
        [[GpuiWryNewWindowDelegate alloc] init];
    delegate.owner = self;
    delegate.window = window;
    window.delegate = delegate;
    window.contentView = child;
    [window makeKeyAndOrderFront:nil];

    if (!self.openedWindows) {
        self.openedWindows = [NSMutableArray array];
    }
    [self.openedWindows addObject:@[ window, child, delegate ]];
    return child;
}
@end

/** `url_scheme_handler::create`. Rust builds one class per scheme at runtime
    because its ivars are the only place to put the index; a property does
    the same job with a class written out once. */
@interface GpuiWrySchemeHandler : NSObject <WKURLSchemeHandler>
@property(nonatomic, assign) wry::WebView* wv;
@property(nonatomic, assign) int index;
@end

@implementation GpuiWrySchemeHandler
- (void)webView:(WKWebView*)webView
    startURLSchemeTask:(id<WKURLSchemeTask>)task {
    (void)webView;
    if (self.wv) {
        wry::HandleSchemeTask(self.wv, self.index, task);
    }
}

- (void)webView:(WKWebView*)webView
    stopURLSchemeTask:(id<WKURLSchemeTask>)task {
    (void)webView;
    wry::WebView* wv = self.wv;
    if (wv && wv->liveTasks) {
        [wv->liveTasks
            removeObject:[NSValue valueWithPointer:(__bridge const void*)task]];
    }
}
@end

namespace wry {

// ─── init scripts and eval ───────────────────────────────────────────────

// `InnerWebView::init`.
static void AddUserScript(WebView* wv, Str js, bool forMainFrameOnly) {
    WKUserScript* script = [[WKUserScript alloc]
          initWithSource:ToNS(js)
           injectionTime:WKUserScriptInjectionTimeAtDocumentStart
        forMainFrameOnly:forMainFrameOnly ? YES : NO];
    [wv->manager addUserScript:script];
}

static void FlushPendingScripts(WebView* wv) {
    if (!wv->pendingOpen) {
        return;
    }
    for (int i = 0; i < wv->pendingScripts.len; i++) {
        [wv->webview evaluateJavaScript:ToNS(wv->pendingScripts[i])
                      completionHandler:nil];
        StrFree(wv->pendingScripts[i]);
    }
    VecReset(wv->pendingScripts);
    wv->pendingOpen = false;
}

// ─── custom protocols ────────────────────────────────────────────────────

static void HandleSchemeTask(WebView* wv, int index, id<WKURLSchemeTask> task) {
    if (index < 0 || index >= wv->protocols.len) {
        return;
    }
    NSURLRequest* request = task.request;
    NSURL* url = request.URL;
    if (!url) {
        return;
    }
    if (!wv->liveTasks) {
        wv->liveTasks = [NSMutableSet set];
    }
    [wv->liveTasks
        addObject:[NSValue valueWithPointer:(__bridge const void*)task]];

    Vec<Header> headerStore;
    NSDictionary<NSString*, NSString*>* all = request.allHTTPHeaderFields;
    for (NSString* name in all) {
        Header h;
        h.name = FromNSTemp(name);
        h.value = FromNSTemp(all[name]);
        VecAppend(headerStore, h);
    }

    // The body, whether it came whole or as a stream.
    Vec<uint8_t> bodyStore;
    NSData* body = request.HTTPBody;
    if (body) {
        uint8_t* dst = VecAppendBlanks(bodyStore, (int)body.length);
        if (dst) {
            memcpy(dst, body.bytes, body.length);
        }
    } else if (request.HTTPBodyStream) {
        NSInputStream* stream = request.HTTPBodyStream;
        [stream open];
        uint8_t buf[1024];
        while (stream.hasBytesAvailable) {
            NSInteger got = [stream read:buf maxLength:sizeof(buf)];
            if (got <= 0) {
                break;
            }
            uint8_t* dst = VecAppendBlanks(bodyStore, (int)got);
            if (!dst) {
                break;
            }
            memcpy(dst, buf, (size_t)got);
        }
        [stream close];
    }

    Request req;
    req.method = FromNSTemp(request.HTTPMethod ? request.HTTPMethod : @"GET");
    req.uri = FromNSTemp(url.absoluteString);
    req.headers = len(headerStore) > 0 ? &headerStore[0] : nullptr;
    req.headerCount = len(headerStore);
    req.body = len(bodyStore) > 0 ? &bodyStore[0] : nullptr;
    req.bodyLen = len(bodyStore);

    RequestResponder* responder = new RequestResponder();
    responder->liveTasks = wv->liveTasks;
    responder->task = task;
    responder->url = url;

    ProtocolCopy& p = wv->protocols[index];
    if (p.handler) {
        p.handler(p.ctx, wv->id, &req, responder);
    } else {
        Response response;
        response.status = 500;
        Respond(responder, &response);
    }

    VecReset(headerStore);
    VecReset(bodyStore);
}

// `didReceiveResponse` / `didReceiveData` / `didFinish`, on the main thread
// and only while the task is still one the webview knows about — Rust's
// per-task UUID check, which is there because a stopped task is a dangling
// pointer.
static void DeliverResponse(RequestResponder* responder,
                            NSHTTPURLResponse* response, NSData* data) {
    NSValue* key =
        [NSValue valueWithPointer:(__bridge const void*)responder->task];
    if (responder->liveTasks && [responder->liveTasks containsObject:key]) {
        @try {
            [responder->task didReceiveResponse:response];
            [responder->task didReceiveData:data];
            [responder->task didFinish];
        } @catch (NSException* e) {
            (void)e;
            logf(
                "wry: the custom protocol task went away before it was "
                "answered\n");
        }
        [responder->liveTasks removeObject:key];
    }
    responder->liveTasks = nil;
    responder->task = nil;
    responder->url = nil;
    delete responder;
}

void Respond(RequestResponder* responder, const Response* response) {
    if (!responder) {
        return;
    }
    if (!__sync_bool_compare_and_swap(&responder->answered, 0, 1)) {
        logf("wry: a custom protocol request was answered twice\n");
        return;
    }

    int status = response ? response->status : 500;
    int bodyLen = (response && response->body) ? response->bodyLen : 0;
    NSMutableDictionary* headers = [NSMutableDictionary dictionary];
    // wry sets Content-Length itself and lets the handler's own headers
    // override anything it set.
    headers[@"Content-Length"] = [NSString stringWithFormat:@"%d", bodyLen];
    if (response) {
        for (int i = 0; i < response->headerCount; i++) {
            headers[ToNS(response->headers[i].name)] =
                ToNS(response->headers[i].value);
        }
    }
    NSData* data = bodyLen > 0 ? [NSData dataWithBytes:response->body
                                                length:(NSUInteger)bodyLen]
                               : [NSData data];
    NSHTTPURLResponse* http =
        [[NSHTTPURLResponse alloc] initWithURL:responder->url
                                    statusCode:status
                                   HTTPVersion:@"HTTP/1.1"
                                  headerFields:headers];
    if (!http) {
        logf(
            "wry: could not build the response for a custom protocol "
            "request\n");
        if (responder->liveTasks && responder->task) {
            NSValue* key = [NSValue
                valueWithPointer:(__bridge const void*)responder->task];
            [responder->liveTasks removeObject:key];
        }
        delete responder;
        return;
    }

    if ([NSThread isMainThread]) {
        DeliverResponse(responder, http, data);
        return;
    }
    dispatch_async(dispatch_get_main_queue(), ^{
      DeliverResponse(responder, http, data);
    });
}

// ─── geometry ────────────────────────────────────────────────────────────

// `window_position`: wry's coordinates are top-left with y down, a plain
// NSView's are bottom-left with y up. The gpui view is flipped, so the first
// branch is the one this tree takes.
static NSPoint WindowPosition(NSView* view, double x, double y, double height) {
    if (view.isFlipped) {
        return NSMakePoint(x, y);
    }
    return NSMakePoint(x, view.frame.size.height - y - height);
}

// A physical size is device pixels; AppKit lays out in points, which is what
// wry calls logical.
static double ToLogical(double value, bool logical, double scale) {
    return logical ? value : value / scale;
}

static double ScaleFactor(NSView* view) {
    NSWindow* window = view.window;
    return window ? window.backingScaleFactor : 1.0;
}

// `WryWebViewParent::set_traffic_light_inset`. The C++ GPUI window keeps its
// own content view instead of installing WryWebViewParent, but the controls
// belong to the NSWindow and can be positioned in exactly the same way.
static void SetTrafficLightInset(NSWindow* window, Position position) {
    if (!window) {
        return;
    }
    double scale = window.backingScaleFactor;
    double x = ToLogical(position.x, position.logical, scale);
    double y = ToLogical(position.y, position.logical, scale);
    NSButton* close = [window standardWindowButton:NSWindowCloseButton];
    NSButton* mini = [window standardWindowButton:NSWindowMiniaturizeButton];
    NSButton* zoom = [window standardWindowButton:NSWindowZoomButton];
    NSView* container = close.superview.superview;
    if (!close || !mini || !container) {
        return;
    }

    NSRect closeFrame = close.frame;
    CGFloat titleHeight = closeFrame.size.height + y;
    NSRect titleFrame = container.frame;
    titleFrame.size.height = titleHeight;
    titleFrame.origin.y = window.frame.size.height - titleHeight;
    container.frame = titleFrame;

    CGFloat spacing = mini.frame.origin.x - closeFrame.origin.x;
    closeFrame.origin.x = x;
    close.frame = closeFrame;
    NSRect miniFrame = mini.frame;
    miniFrame.origin.x = x + spacing;
    mini.frame = miniFrame;
    if (zoom) {
        NSRect zoomFrame = zoom.frame;
        zoomFrame.origin.x = x + 2 * spacing;
        zoom.frame = zoomFrame;
    }
}

// ─── the webview ─────────────────────────────────────────────────────────

WebView* WebViewNew(void* parentWindow, const WebViewAttributes* attrs,
                    bool asChild) {
    if (!parentWindow || !attrs) {
        return nullptr;
    }
    if (![NSThread isMainThread]) {
        // `MainThreadMarker::new().ok_or(Error::NotMainThread)`.
        logf("wry: a webview can only be made on the main thread\n");
        return nullptr;
    }
    NSView* parentView = (__bridge NSView*)parentWindow;

    bool usingExistingConfig = attrs->webviewConfiguration != nullptr;
    WKWebViewConfiguration* config =
        usingExistingConfig
            ? (__bridge WKWebViewConfiguration*)attrs->webviewConfiguration
            : [[WKWebViewConfiguration alloc] init];
    if (!usingExistingConfig) {
        if (attrs->incognito) {
            config
                .websiteDataStore = [WKWebsiteDataStore nonPersistentDataStore];
        } else if (attrs->hasDataStoreIdentifier) {
            if (@available(macOS 14.0, *)) {
                NSUUID* identifier = [[NSUUID alloc]
                    initWithUUIDBytes:attrs->dataStoreIdentifier];
                config.websiteDataStore =
                    [WKWebsiteDataStore dataStoreForIdentifier:identifier];
            } else {
                config.websiteDataStore = [WKWebsiteDataStore defaultDataStore];
            }
        } else {
            config.websiteDataStore = [WKWebsiteDataStore defaultDataStore];
        }
    }

    WebView* wv = new WebView();
    wv->parentView = parentView;
    wv->isChild = asChild;
    wv->visible = attrs->visible;
    wv->ctx = attrs->ctx;
    wv->ipcHandler = attrs->ipcHandler;
    wv->navigationHandler = attrs->navigationHandler;
    wv->documentTitleChangedHandler = attrs->documentTitleChangedHandler;
    wv->onPageLoadHandler = attrs->onPageLoadHandler;
    wv->newWindowReqHandler = attrs->newWindowReqHandler;
    wv->liveTasks = [NSMutableSet set];
    wv->schemeHandlers = [NSMutableArray array];
    // `attributes.id.unwrap_or_else(|| COUNTER.next().to_string())`.
    static int nextId = 1;
    wv->id = len(attrs->id) > 0 ? StrDup(attrs->id)
                                : StrDup(base::FormatTemp("%d", nextId++));

    // Custom protocols, before the webview exists: a scheme handler can only
    // be set on a configuration.
    for (int i = 0; i < attrs->customProtocolCount; i++) {
        NSString* scheme = ToNS(attrs->customProtocols[i].name);
        if (usingExistingConfig &&
            [config urlSchemeHandlerForURLScheme:scheme]) {
            continue;
        }
        ProtocolCopy p;
        p.name = StrDup(attrs->customProtocols[i].name);
        p.ctx = attrs->customProtocols[i].ctx;
        p.handler = attrs->customProtocols[i].handler;
        int protocolIndex = wv->protocols.len;
        VecAppend(wv->protocols, p);

        GpuiWrySchemeHandler* handler = [[GpuiWrySchemeHandler alloc] init];
        handler.wv = wv;
        handler.index = protocolIndex;
        [wv->schemeHandlers addObject:handler];
        // WebKit raises for a scheme it handles itself (http, https, file …),
        // which is `Error::UrlSchemeRegisterError` in Rust.
        @try {
            [config setURLSchemeHandler:handler forURLScheme:scheme];
        } @catch (NSException* e) {
            (void)e;
            logf("wry: could not register the custom protocol '%s'\n", p.name);
            WebViewFree(wv);
            return nullptr;
        }
    }

    WKPreferences* preferences = config.preferences;
    if (attrs->javascriptDisabled) {
        config.defaultWebpagePreferences.allowsContentJavaScript = NO;
    }
    if (attrs->autoplay) {
        config.mediaTypesRequiringUserActionForPlayback =
            WKAudiovisualMediaTypeNone;
    }
    if (attrs->hasBackgroundThrottling) {
        if (@available(macOS 14.0, *)) {
            // WKInactiveSchedulingPolicy is Suspend=0, Throttle=1, None=2.
            int policy = 2;
            if (attrs->backgroundThrottling ==
                BackgroundThrottlingPolicy::Suspend) {
                policy = 0;
            } else if (attrs->backgroundThrottling ==
                       BackgroundThrottlingPolicy::Throttle) {
                policy = 1;
            }
            [preferences setValue:@(policy) forKey:@"inactiveSchedulingPolicy"];
        }
    }
    if (attrs->transparent) {
        [config setValue:@NO forKey:@"drawsBackground"];
    }
    [preferences setValue:@YES forKey:@"allowsPictureInPictureMediaPlayback"];
    [preferences setValue:@YES forKey:@"tabFocusesLinks"];

    // The frame it starts at: a child is placed where the attributes say,
    // anything else fills the parent.
    double scale = ScaleFactor(parentView);
    NSRect frame;
    if (asChild && attrs->hasBounds) {
        double x = ToLogical(attrs->bounds.position.x,
                             attrs->bounds.position.logical, scale);
        double y = ToLogical(attrs->bounds.position.y,
                             attrs->bounds.position.logical, scale);
        double w = ToLogical(attrs->bounds.size.width,
                             attrs->bounds.size.logical, scale);
        double h = ToLogical(attrs->bounds.size.height,
                             attrs->bounds.size.logical, scale);
        frame.origin = WindowPosition(parentView, x, y, h);
        frame.size = NSMakeSize(w, h);
    } else {
        frame = parentView.bounds;
    }

    GpuiWryWebView* webview =
        [[GpuiWryWebView alloc] initWithFrame:frame configuration:config];
    webview.childWebView = asChild ? YES : NO;
    webview.acceptFirstMouseEnabled = attrs->acceptFirstMouse ? YES : NO;
    wv->webview = webview;
    wv->manager = config.userContentController;

    if (asChild) {
        // A fixed element: it is moved by set_bounds, not by the parent.
        wv->webview.autoresizingMask = NSViewMinYMargin;
    } else {
        wv->webview.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    }
    wv->webview.allowsBackForwardNavigationGestures =
        attrs->backForwardNavigationGestures ? YES : NO;
    wv->webview.allowsLinkPreview = attrs->allowLinkPreview ? YES : NO;
    if (!attrs->visible) {
        wv->webview.hidden = YES;
    }
    if (attrs->devtools) {
        if ([wv->webview respondsToSelector:@selector(setInspectable:)]) {
            wv->webview.inspectable = YES;
        }
        // Not an else: on macOS the preference is needed as well.
        [preferences setValue:@YES forKey:@"developerExtrasEnabled"];
    }

    // The IPC channel: the frozen `window.ipc` and the handler behind it.
    AddUserScript(wv, Str((char*)[kIpcScript UTF8String]), true);
    if (attrs->ipcHandler) {
        wv->ipcDelegate = [[GpuiWryScriptHandler alloc] init];
        wv->ipcDelegate.wv = wv;
        @try {
            [wv->manager addScriptMessageHandler:wv->ipcDelegate
                                            name:kIpcHandlerName];
        } @catch (NSException* e) {
            (void)e;
            logf("wry: could not install the ipc message handler\n");
        }
    }
    for (int i = 0; i < attrs->initializationScriptCount; i++) {
        AddUserScript(wv, attrs->initializationScripts[i].script,
                      attrs->initializationScripts[i].forMainFrameOnly);
    }

    if (attrs->documentTitleChangedHandler) {
        wv->titleObserver = [[GpuiWryTitleObserver alloc] init];
        wv->titleObserver.wv = wv;
        [wv->webview addObserver:wv->titleObserver
                      forKeyPath:@"title"
                         options:NSKeyValueObservingOptionNew
                         context:nullptr];
    }

    wv->navDelegate = [[GpuiWryNavigationDelegate alloc] init];
    wv->navDelegate.wv = wv;
    wv->webview.navigationDelegate = wv->navDelegate;

    wv->uiDelegate = [[GpuiWryUIDelegate alloc] init];
    wv->uiDelegate.wv = wv;
    wv->webview.UIDelegate = wv->uiDelegate;

    if (len(attrs->userAgent) > 0) {
        wv->webview.customUserAgent = ToNS(attrs->userAgent);
    }

    // Navigation.
    if (len(attrs->url) > 0) {
        NSURL* url = [NSURL URLWithString:ToNS(attrs->url)];
        if (url) {
            NSMutableURLRequest* request =
                [NSMutableURLRequest requestWithURL:url];
            for (int i = 0; i < attrs->headerCount; i++) {
                [request addValue:ToNS(attrs->headers[i].value)
                    forHTTPHeaderField:ToNS(attrs->headers[i].name)];
            }
            [wv->webview loadRequest:request];
        } else {
            logf("wry: the url could not be parsed\n");
        }
    } else if (len(attrs->html) > 0) {
        [wv->webview loadHTMLString:ToNS(attrs->html) baseURL:nil];
    }

    // Into the window. Rust makes the webview the window's content view when
    // it is not a child; here it is a subview either way — see the top of
    // this file.
    [parentView addSubview:wv->webview];
    if (!asChild) {
        wv->webview.frame = parentView.bounds;
        NSWindow* window = parentView.window;
        if (window) {
            [window makeFirstResponder:wv->webview];
        }
    }
    // `focused` is unsupported by wry on macOS. A non-child webview is made
    // first responder by WryWebViewParent; a child must not steal GPUI focus.
    (void)attrs->focused;
    NSWindow* window = parentView.window;
    if (window &&
        [window respondsToSelector:@selector(setTitlebarSeparatorStyle:)]) {
        window.titlebarSeparatorStyle = NSTitlebarSeparatorStyleNone;
    }
    if (!asChild && attrs->hasTrafficLightInset) {
        SetTrafficLightInset(window, attrs->trafficLightInset);
    }
    if (@available(macOS 14.0, *)) {
        [NSApp activate];
    } else {
        [NSApp activateIgnoringOtherApps:YES];
    }
    return wv;
}

void WebViewFree(WebView* wv) {
    if (!wv) {
        return;
    }
    if (wv->ipcDelegate && wv->manager) {
        [wv->manager removeScriptMessageHandlerForName:kIpcHandlerName];
    }
    if (wv->titleObserver && wv->webview) {
        [wv->webview removeObserver:wv->titleObserver forKeyPath:@"title"];
    }
    if (wv->webview) {
        wv->webview.navigationDelegate = nil;
        wv->webview.UIDelegate = nil;
        [wv->webview removeFromSuperview];
    }
    // Every Objective-C delegate stores a non-owning WebView pointer. WebKit
    // may still retain a delegate or scheme handler while queued callbacks
    // drain, so sever those pointers before deleting the C++ state.
    wv->ipcDelegate.wv = nullptr;
    wv->navDelegate.wv = nullptr;
    wv->uiDelegate.wv = nullptr;
    wv->titleObserver.wv = nullptr;
    for (GpuiWrySchemeHandler* handler in wv->schemeHandlers) {
        handler.wv = nullptr;
    }
    [wv->liveTasks removeAllObjects];
    for (int i = 0; i < wv->protocols.len; i++) {
        StrFree(wv->protocols[i].name);
    }
    VecReset(wv->protocols);
    for (int i = 0; i < wv->pendingScripts.len; i++) {
        StrFree(wv->pendingScripts[i]);
    }
    VecReset(wv->pendingScripts);
    StrFree(wv->id);
    // ARC releases the delegates, the webview and the collections as the
    // struct's members go out of scope.
    delete wv;
}

// ─── the public API ──────────────────────────────────────────────────────

Str WebViewId(WebView* wv) {
    return wv ? wv->id : Str();
}

bool WebViewEval(WebView* wv, Str js) {
    if (!wv) {
        return false;
    }
    // An eval before the first navigation commits is held, which is what
    // `pending_scripts` is; didCommitNavigation replays them.
    if (wv->pendingOpen) {
        VecAppend(wv->pendingScripts, StrDup(js));
        return true;
    }
    [wv->webview evaluateJavaScript:ToNS(js) completionHandler:nil];
    return true;
}

bool WebViewEvalWithCallback(WebView* wv, Str js, void* ctx,
                             void (*callback)(void* ctx, Str result)) {
    if (!wv) {
        return false;
    }
    if (!callback) {
        return WebViewEval(wv, js);
    }
    if (wv->pendingOpen) {
        // A callback cannot be queued the way a bare script can: Rust's
        // pending list holds strings and drops the callback here too.
        VecAppend(wv->pendingScripts, StrDup(js));
        return true;
    }
    [wv->webview evaluateJavaScript:ToNS(js)
                  completionHandler:^(id result, NSError* error) {
                    (void)error;
                    if (!result) {
                        callback(ctx, Str());
                        return;
                    }
                    // The value as JSON, which is what Rust serialises here
                    // and what the Windows half hands back.
                    NSData* json = [NSJSONSerialization
                        dataWithJSONObject:result
                                   options:NSJSONWritingFragmentsAllowed
                                     error:nil];
                    if (!json) {
                        callback(ctx, Str());
                        return;
                    }
                    NSString* text =
                        [[NSString alloc] initWithData:json
                                              encoding:NSUTF8StringEncoding];
                    callback(ctx, FromNSTemp(text));
                  }];
    return true;
}

Str WebViewUrlTemp(WebView* wv) {
    return wv ? UrlFromWebView(wv->webview) : Str();
}

bool WebViewLoadUrl(WebView* wv, Str url) {
    return WebViewLoadUrlWithHeaders(wv, url, nullptr, 0);
}

bool WebViewLoadUrlWithHeaders(WebView* wv, Str url, const Header* headers,
                               int headerCount) {
    if (!wv) {
        return false;
    }
    NSURL* nsurl = [NSURL URLWithString:ToNS(url)];
    if (!nsurl) {
        return false;
    }
    NSMutableURLRequest* request = [NSMutableURLRequest requestWithURL:nsurl];
    for (int i = 0; i < headerCount; i++) {
        [request addValue:ToNS(headers[i].value)
            forHTTPHeaderField:ToNS(headers[i].name)];
    }
    [wv->webview loadRequest:request];
    return true;
}

bool WebViewLoadHtml(WebView* wv, Str html) {
    if (!wv) {
        return false;
    }
    [wv->webview loadHTMLString:ToNS(html) baseURL:nil];
    return true;
}

bool WebViewReload(WebView* wv) {
    if (!wv) {
        return false;
    }
    [wv->webview reload];
    return true;
}

bool WebViewBounds(WebView* wv, Rect* out) {
    if (!wv || !out) {
        return false;
    }
    NSView* parent = wv->webview.superview;
    if (!parent) {
        return false;
    }
    NSRect frame = wv->webview.frame;
    double y = parent.isFlipped ? frame.origin.y
                                : parent.frame.size.height - frame.origin.y -
                                      frame.size.height;
    out->position = LogicalPosition(frame.origin.x, y);
    out->size = LogicalSize(frame.size.width, frame.size.height);
    return true;
}

bool WebViewSetBounds(WebView* wv, Rect bounds) {
    if (!wv) {
        return false;
    }
    if (!wv->isChild) {
        // set_bounds is a child's; a full-window webview follows the view it
        // was added to.
        return true;
    }
    NSView* parent = wv->webview.superview;
    if (!parent) {
        return false;
    }
    double scale = ScaleFactor(parent);
    double x = ToLogical(bounds.position.x, bounds.position.logical, scale);
    double y = ToLogical(bounds.position.y, bounds.position.logical, scale);
    double w = ToLogical(bounds.size.width, bounds.size.logical, scale);
    double h = ToLogical(bounds.size.height, bounds.size.logical, scale);
    NSRect frame;
    frame.origin = WindowPosition(parent, x, y, h);
    frame.size = NSMakeSize(w, h);
    wv->webview.frame = frame;
    return true;
}

bool WebViewSetVisible(WebView* wv, bool visible) {
    if (!wv) {
        return false;
    }
    wv->webview.hidden = visible ? NO : YES;
    wv->visible = visible;
    return true;
}

bool WebViewFocus(WebView* wv) {
    if (!wv) {
        return false;
    }
    NSWindow* window = wv->webview.window;
    if (!window) {
        return false;
    }
    [window makeFirstResponder:wv->webview];
    return true;
}

bool WebViewFocusParent(WebView* wv) {
    if (!wv) {
        return false;
    }
    NSWindow* window = wv->webview.window;
    if (!window) {
        return false;
    }
    [window makeFirstResponder:wv->parentView];
    return true;
}

bool WebViewZoom(WebView* wv, double scaleFactor) {
    if (!wv) {
        return false;
    }
    wv->webview.pageZoom = scaleFactor;
    return true;
}

// `set_background_color` is the iOS half of the crate; on macOS the colour
// comes from the page, and the only knob is the transparency attribute,
// which is set on the configuration before the webview exists.
bool WebViewSetBackgroundColor(WebView*, Rgba) {
    // Rust's macOS arm is an intentional no-op but still returns Ok(()).
    return true;
}

// WebViewExtWindows, both of them: no WKWebView counterpart.
bool WebViewSetTheme(WebView*, Theme) {
    return false;
}

bool WebViewSetMemoryUsageLevel(WebView*, MemoryUsageLevel) {
    return false;
}

bool WebViewReparent(WebView* wv, void* parentWindow) {
    if (!wv || !parentWindow) {
        return false;
    }
    NSView* parent = (__bridge NSView*)parentWindow;
    [wv->webview removeFromSuperview];
    [parent addSubview:wv->webview];
    wv->parentView = parent;
    if (!wv->isChild) {
        wv->webview.frame = parent.bounds;
    }
    return true;
}

bool WebViewSetTrafficLightInset(WebView* wv, Position position) {
    if (!wv) {
        return false;
    }
    // WryWebViewParent only exists for a non-child webview. The extension is
    // specified as a successful no-op for a child.
    if (!wv->isChild) {
        SetTrafficLightInset(wv->webview.window, position);
    }
    return true;
}

bool WebViewPrint(WebView* wv) {
    if (!wv) {
        return false;
    }
    if (![wv->webview
            respondsToSelector:@selector(printOperationWithPrintInfo:)]) {
        return false;
    }
    NSWindow* window = wv->webview.window;
    if (!window) {
        return false;
    }
    NSPrintInfo* info = [NSPrintInfo sharedPrintInfo];
    NSPrintOperation* operation =
        [wv->webview printOperationWithPrintInfo:info];
    // Let the modal detach from this thread rather than block the app.
    operation.canSpawnSeparateThread = YES;
    [operation runOperationModalForWindow:window
                                 delegate:nil
                           didRunSelector:nullptr
                              contextInfo:nullptr];
    return true;
}

bool WebViewClearAllBrowsingData(WebView* wv) {
    if (!wv) {
        return false;
    }
    WKWebsiteDataStore* store = wv->webview.configuration.websiteDataStore;
    NSSet* types = [WKWebsiteDataStore allWebsiteDataTypes];
    NSDate* since = [NSDate dateWithTimeIntervalSince1970:0];
    [store removeDataOfTypes:types
               modifiedSince:since
           completionHandler:^{
           }];
    return true;
}

bool WebViewCookies(WebView*, Vec<Cookie>* out) {
    CookieListFree(out);
    return false;
}

bool WebViewCookiesForUrl(WebView*, Str, Vec<Cookie>* out) {
    CookieListFree(out);
    return false;
}

bool WebViewSetCookie(WebView*, const Cookie*) {
    return false;
}

bool WebViewDeleteCookie(WebView*, const Cookie*) {
    return false;
}

// The inspector is a private selector on WKWebView, which is what the crate
// reaches for and the only way to open the devtools without the context
// menu. Called through NSInvocation because ARC refuses a performSelector
// whose selector it cannot see the memory rules for.
static id Inspector(WebView* wv) {
    if (!wv || !wv->webview) {
        return nil;
    }
    SEL sel = NSSelectorFromString(@"_inspector");
    if (![wv->webview respondsToSelector:sel]) {
        return nil;
    }
    NSMethodSignature* sig = [wv->webview methodSignatureForSelector:sel];
    if (!sig) {
        return nil;
    }
    NSInvocation* call = [NSInvocation invocationWithMethodSignature:sig];
    call.selector = sel;
    [call invokeWithTarget:wv->webview];
    void* result = nullptr;
    [call getReturnValue:&result];
    return (__bridge id)result;
}

static void CallInspector(WebView* wv, NSString* name) {
    id inspector = Inspector(wv);
    SEL sel = NSSelectorFromString(name);
    if (!inspector || ![inspector respondsToSelector:sel]) {
        return;
    }
    NSMethodSignature* sig = [inspector methodSignatureForSelector:sel];
    if (!sig) {
        return;
    }
    NSInvocation* call = [NSInvocation invocationWithMethodSignature:sig];
    call.selector = sel;
    [call invokeWithTarget:inspector];
}

void WebViewOpenDevtools(WebView* wv) {
    CallInspector(wv, @"show");
}

void WebViewCloseDevtools(WebView* wv) {
    CallInspector(wv, @"close");
}

bool WebViewIsDevtoolsOpen(WebView* wv) {
    id inspector = Inspector(wv);
    SEL sel = NSSelectorFromString(@"isVisible");
    if (!inspector || ![inspector respondsToSelector:sel]) {
        return false;
    }
    NSMethodSignature* sig = [inspector methodSignatureForSelector:sel];
    if (!sig) {
        return false;
    }
    NSInvocation* call = [NSInvocation invocationWithMethodSignature:sig];
    call.selector = sel;
    [call invokeWithTarget:inspector];
    BOOL result = NO;
    [call getReturnValue:&result];
    return result == YES;
}

// `platform_webview_version`: WebKit's own bundle version.
Str WebViewVersionTemp() {
    NSBundle* bundle = [NSBundle bundleWithIdentifier:@"com.apple.WebKit"];
    if (!bundle) {
        return {};
    }
    NSString* version = [bundle.infoDictionary objectForKey:@"CFBundleVersion"];
    return version ? FromNSTemp(version) : Str();
}

// WebKit is part of the system; there is nothing to look for.
bool WebViewAvailable() {
    return true;
}

} // namespace wry
