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

// A request with a streamed body, a streamed answer, or both. The delegate
// methods run on the session's queue while HttpSend waits on `done`.
@interface GpuiStreamDelegate : NSObject <NSURLSessionDataDelegate> {
  @public
    const gpui::HttpReq* request;
    gpui::HttpRsp* out;
    NSHTTPURLResponse* response;
    dispatch_semaphore_t done;
    bool ok;
    bool refused;
}
@end

@implementation GpuiStreamDelegate
- (void)URLSession:(NSURLSession*)session
                          task:(NSURLSessionTask*)task
    willPerformHTTPRedirection:(NSHTTPURLResponse*)redirect
                    newRequest:(NSURLRequest*)newRequest
             completionHandler:(void (^)(NSURLRequest*))completionHandler {
    (void)session;
    (void)task;
    (void)redirect;
    // A streamed body cannot be sent a second time.
    bool follow = !request->noRedirect && !request->readBody;
    completionHandler(follow ? newRequest : nil);
}

- (void)URLSession:(NSURLSession*)session
                 task:(NSURLSessionTask*)task
    needNewBodyStream:(void (^)(NSInputStream*))completionHandler {
    (void)session;
    refused = true;
    [task cancel];
    completionHandler(nil);
}

- (void)URLSession:(NSURLSession*)session
              dataTask:(NSURLSessionDataTask*)task
    didReceiveResponse:(NSURLResponse*)rsp
     completionHandler:
         (void (^)(NSURLSessionResponseDisposition))completionHandler {
    (void)session;
    (void)task;
    if ([rsp isKindOfClass:[NSHTTPURLResponse class]]) {
        response = (NSHTTPURLResponse*)rsp;
    }
    completionHandler(NSURLSessionResponseAllow);
}

- (void)URLSession:(NSURLSession*)session
          dataTask:(NSURLSessionDataTask*)task
    didReceiveData:(NSData*)data {
    (void)session;
    if (refused) {
        return;
    }
    NSInteger status = response ? [response statusCode] : 0;
    bool stream = request->onBody && status >= 200 && status < 300;
    [data enumerateByteRangesUsingBlock:^(const void* bytes, NSRange range,
                                          BOOL* stop) {
      int n = (int)range.length;
      if (stream) {
          if (!request->onBody(request->onBodyCtx, (const uint8_t*)bytes, n)) {
              refused = true;
          }
      } else if ((int64_t)gpui::len(out->body) + n >
                 (int64_t)gpui::kHttpMaxBody) {
          refused = true;
      } else {
          uint8_t* dst = gpui::VecAppendBlanks(out->body, n);
          if (dst) {
              memcpy(dst, bytes, (size_t)n);
          } else {
              refused = true;
          }
      }
      if (refused) {
          *stop = YES;
      }
    }];
    if (refused) {
        [task cancel];
    }
}

- (void)URLSession:(NSURLSession*)session
                    task:(NSURLSessionTask*)task
    didCompleteWithError:(NSError*)error {
    (void)session;
    (void)task;
    ok = !error && !refused && response != nil;
    dispatch_semaphore_signal(done);
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

// Foundation keeps the headers as a dictionary, so the order is its own and
// a header the server sent twice arrives as one value.
static Str HeadersFromResponse(NSHTTPURLResponse* rsp) {
    StrBuilder out;
    NSDictionary* fields = [rsp allHeaderFields];
    for (id key in fields) {
        id value = [fields objectForKey:key];
        if (![key isKindOfClass:[NSString class]] ||
            ![value isKindOfClass:[NSString class]]) {
            continue;
        }
        const char* name = [(NSString*)key UTF8String];
        const char* text = [(NSString*)value UTF8String];
        if (!name || !text) {
            continue;
        }
        out.Append(Str(name));
        out.Append(StrL(": "));
        out.Append(Str(text));
        out.AppendChar('\n');
    }
    return out.TakeStr();
}

static NSString* NSFromStr(Str s) {
    if (len(s) <= 0) {
        return @"";
    }
    return [[NSString alloc] initWithBytes:s.s
                                    length:(NSUInteger)len(s)
                                  encoding:NSUTF8StringEncoding];
}

// The streamed request body goes through a bound pair of streams: the
// session reads one end while this thread fills the other.
static bool HttpSendStreamed(const HttpReq& request, HttpRsp* out) {
    @autoreleasepool {
        NSURL* u = [NSURL URLWithString:NSFromStr(request.url)];
        if (!u || request.bodyLen < 0) {
            return false;
        }
        // The timeout of a request is how long it may stay idle, which is
        // the limit a long transfer wants.
        NSMutableURLRequest* req = [NSMutableURLRequest
             requestWithURL:u
                cachePolicy:NSURLRequestUseProtocolCachePolicy
            timeoutInterval:(NSTimeInterval)kHttpTimeoutMs / 1000.0];
        [req setHTTPMethod:len(request.method) > 0 ? NSFromStr(request.method)
                                                   : @"GET"];
        [req setValue:@"gpui/1.0" forHTTPHeaderField:@"User-Agent"];
        for (int i = 0; i < request.nHeaders; i++) {
            NSString* name = NSFromStr(request.headers[i].name);
            NSString* value = NSFromStr(request.headers[i].value);
            if (name && value) {
                [req setValue:value forHTTPHeaderField:name];
            }
        }
        NSInputStream* bodyIn = nil;
        NSOutputStream* bodyOut = nil;
        if (request.readBody) {
            [NSStream getBoundStreamsWithBufferSize:64 * 1024
                                        inputStream:&bodyIn
                                       outputStream:&bodyOut];
            if (!bodyIn || !bodyOut) {
                return false;
            }
            [req setHTTPBodyStream:bodyIn];
            // Named so the body is not sent chunked, which not every server
            // takes for an upload.
            [req setValue:[NSString stringWithFormat:@"%lld", (long long)request
                                                                  .bodyLen]
                forHTTPHeaderField:@"Content-Length"];
        }

        GpuiStreamDelegate* delegate = [[GpuiStreamDelegate alloc] init];
        delegate->request = &request;
        delegate->out = out;
        delegate->response = nil;
        delegate->done = dispatch_semaphore_create(0);
        delegate->ok = false;
        delegate->refused = false;
        NSURLSession* session = [NSURLSession
            sessionWithConfiguration:[NSURLSessionConfiguration
                                         defaultSessionConfiguration]
                            delegate:delegate
                       delegateQueue:nil];
        NSURLSessionDataTask* task = [session dataTaskWithRequest:req];
        [task resume];

        bool finished = false;
        if (bodyOut) {
            [bodyOut open];
            uint8_t* buf = AllocArray<uint8_t>(64 * 1024);
            bool sent = buf != nullptr;
            for (int64_t left = request.bodyLen;
                 sent && !finished && left > 0;) {
                int cap = left < 64 * 1024 ? (int)left : 64 * 1024;
                int n = request.readBody(request.readBodyCtx, buf, cap);
                sent = n > 0 && n <= cap;
                for (int at = 0; sent && !finished && at < n;) {
                    // A write blocks until the session has read what is
                    // queued, which a task that already failed never does.
                    while (!finished && ![bodyOut hasSpaceAvailable]) {
                        dispatch_time_t tick = dispatch_time(
                            DISPATCH_TIME_NOW, 10 * NSEC_PER_MSEC);
                        finished =
                            dispatch_semaphore_wait(delegate->done, tick) == 0;
                    }
                    if (finished) {
                        break;
                    }
                    NSInteger wrote =
                        [bodyOut write:buf + at maxLength:(NSUInteger)(n - at)];
                    sent = wrote > 0;
                    at += (int)wrote;
                }
                left -= n;
            }
            Free(nullptr, buf);
            [bodyOut close];
            if (!sent && !finished) {
                [task cancel];
            }
        }
        if (!finished) {
            dispatch_semaphore_wait(delegate->done, DISPATCH_TIME_FOREVER);
        }
        [session finishTasksAndInvalidate];
        NSHTTPURLResponse* rsp = delegate->response;
        if (!delegate->ok || !rsp) {
            VecReset(out->body);
            return false;
        }
        out->status = (int)[rsp statusCode];
        if (request.noRedirect && out->status >= 300 && out->status < 400) {
            NSString* location = [rsp valueForHTTPHeaderField:@"Location"];
            NSURL* target = location ? [NSURL URLWithString:location
                                              relativeToURL:[rsp URL]]
                                     : nil;
            out->redirectUrl = StrFromNS([[target absoluteURL] absoluteString]);
        }
        Str ct = StrFromNS([rsp valueForHTTPHeaderField:@"Content-Type"]);
        TrimMediaType(&ct);
        out->contentType = ct;
        out->headers = HeadersFromResponse(rsp);
        return true;
    }
}

bool HttpSend(const HttpReq& request, HttpRsp* out) {
    Str url = request.url;
    bool noRedirect = request.noRedirect;
    if (!out || !HttpUrlIsRemote(url)) {
        return false;
    }
    if (request.readBody || request.onBody) {
        return HttpSendStreamed(request, out);
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
        __block NSString* failure = nil;
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
                } else if (e) {
                    failure = [e localizedDescription];
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
            const char* why = failure ? [failure UTF8String] : nullptr;
            if (why) {
                out->error = StrDup(Str(why));
            }
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
        out->headers = HeadersFromResponse(rsp);

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
