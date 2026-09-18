/* NSURLSession, waited on. Foundation is already linked for the window, so
   the GET costs nothing this build did not already carry — no libcurl, no
   TLS of our own, and the system's proxy and root store for free.

   The session is asynchronous by design and this call is not, so a semaphore
   turns one into the other. That is fine here: HttpGet only ever runs on a
   worker thread the fetch table started. */

#include "sys/http.h"

#import <Foundation/Foundation.h>

@interface GpuiNoRedirectDelegate : NSObject <NSURLSessionTaskDelegate>
@end

@implementation GpuiNoRedirectDelegate
- (void)URLSession:(NSURLSession*)session
                          task:(NSURLSessionTask*)task
    willPerformHTTPRedirection:(NSHTTPURLResponse*)response
                    newRequest:(NSURLRequest*)request
             completionHandler:(void (^)(NSURLRequest*))completionHandler {
    (void)session;
    (void)task;
    (void)response;
    (void)request;
    completionHandler(nil);
}
@end

namespace gpui {

// "image/png; charset=..." -> "image/png", lowercased where it stands.
static void TrimMediaType(Str* s) {
    for (int i = 0; i < s->len; i++) {
        char c = s->s[i];
        if (c == ';' || c == ' ') {
            s->len = i;
            break;
        }
        if (c >= 'A' && c <= 'Z') {
            s->s[i] = (char)(c - 'A' + 'a');
        }
    }
}

static Str StrFromNS(NSString* s) {
    if (!s) {
        return {};
    }
    const char* u = [s UTF8String];
    if (!u) {
        return {};
    }
    return StrDup(Str(u));
}

static NSString* NSFromStr(Str s) {
    if (len(s) <= 0) {
        return @"";
    }
    return [[NSString alloc] initWithBytes:s.s
                                    length:(NSUInteger)len(s)
                                  encoding:NSUTF8StringEncoding];
}

bool HttpSend(const HttpReq& request, HttpRsp* out) {
    Str url = request.url;
    bool noRedirect = request.noRedirect;
    if (!out || !HttpUrlIsRemote(url)) {
        return false;
    }
    @autoreleasepool {
        NSString* s = [[NSString alloc] initWithBytes:url.s
                                               length:(NSUInteger)len(url)
                                             encoding:NSUTF8StringEncoding];
        NSURL* u = s ? [NSURL URLWithString:s] : nil;
        if (!u) {
            return false;
        }
        NSMutableURLRequest* req = [NSMutableURLRequest
             requestWithURL:u
                cachePolicy:NSURLRequestUseProtocolCachePolicy
            timeoutInterval:(NSTimeInterval)kHttpTimeoutMs / 1000.0];
        NSString* verb =
            len(request.method) > 0 ? NSFromStr(request.method) : @"GET";
        [req setHTTPMethod:verb];
        [req setValue:@"gpui/1.0" forHTTPHeaderField:@"User-Agent"];
        for (int i = 0; i < request.nHeaders; i++) {
            NSString* name = NSFromStr(request.headers[i].name);
            NSString* value = NSFromStr(request.headers[i].value);
            if (name && value) {
                [req setValue:value forHTTPHeaderField:name];
            }
        }
        if (len(request.body) > 0) {
            [req setHTTPBody:[NSData
                                 dataWithBytes:request.body.s
                                        length:(NSUInteger)len(request.body)]];
        }

        __block NSData* body = nil;
        __block NSHTTPURLResponse* rsp = nil;
        dispatch_semaphore_t done = dispatch_semaphore_create(0);
        GpuiNoRedirectDelegate* delegate =
            noRedirect ? [[GpuiNoRedirectDelegate alloc] init] : nil;
        NSURLSession* session =
            noRedirect
                ? [NSURLSession
                      sessionWithConfiguration:[NSURLSessionConfiguration
                                                   defaultSessionConfiguration]
                                      delegate:delegate
                                 delegateQueue:nil]
                : [NSURLSession sharedSession];
        NSURLSessionDataTask* task = [session
            dataTaskWithRequest:req
              completionHandler:^(NSData* d, NSURLResponse* r, NSError* e) {
                if (!e && [r isKindOfClass:[NSHTTPURLResponse class]]) {
                    body = d;
                    rsp = (NSHTTPURLResponse*)r;
                }
                dispatch_semaphore_signal(done);
              }];
        [task resume];
        // The timeout above is the request's; this is the backstop for a
        // completion handler that never runs at all.
        dispatch_time_t deadline =
            dispatch_time(DISPATCH_TIME_NOW,
                          (int64_t)(kHttpTimeoutMs + 5000) * NSEC_PER_MSEC);
        if (dispatch_semaphore_wait(done, deadline) != 0) {
            [task cancel];
            if (noRedirect) [session invalidateAndCancel];
            return false;
        }
        if (noRedirect) [session finishTasksAndInvalidate];
        if (!rsp) {
            return false;
        }
        out->status = (int)[rsp statusCode];
        if (noRedirect && out->status >= 300 && out->status < 400) {
            NSString* location = [rsp valueForHTTPHeaderField:@"Location"];
            NSURL* target = location ? [NSURL URLWithString:location
                                              relativeToURL:[rsp URL]]
                                     : nil;
            out->redirectUrl = StrFromNS([[target absoluteURL] absoluteString]);
        }
        Str ct = StrFromNS([rsp valueForHTTPHeaderField:@"Content-Type"]);
        TrimMediaType(&ct);
        out->contentType = ct;

        NSUInteger n = body ? [body length] : 0;
        if (n > (NSUInteger)kHttpMaxBody) {
            return false; // refused, not truncated
        }
        if (n > 0) {
            uint8_t* dst = VecAppendBlanks(out->body, (int)n);
            if (!dst) {
                return false;
            }
            [body getBytes:dst length:n];
        }
        return true;
    }
}

bool HttpGet(Str url, HttpRsp* out) {
    HttpReq req;
    req.url = url;
    return HttpSend(req, out);
}

bool HttpGetNoRedirect(Str url, HttpRsp* out) {
    HttpReq req;
    req.url = url;
    req.noRedirect = true;
    return HttpSend(req, out);
}

} // namespace gpui
