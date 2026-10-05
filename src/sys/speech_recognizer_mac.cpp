/* SFSpeechRecognizer, recognizing on the device only — system/macos.rs.

   The framework reports on its own queues; its blocks only copy what they
   were handed and post it to the main thread (ExecPost), where the
   recognition turns results into phrases. A session that was dropped hears
   nothing more: the events it has in flight find the flag set. */

#include "sys/executor.h"
#include "sys/speech_recognizer.h"

#import <AVFoundation/AVFoundation.h>
#import <Foundation/Foundation.h>
#import <Speech/Speech.h>

namespace gpui {
struct SysSpeechSession;
static void SessionRelease(SysSpeechSession* s);
} // namespace gpui

// A counted reference to a session that a framework block can own: the
// session stays alive until the framework lets go of the block, however
// late that is.
@interface GpuiSpeechSessionRef : NSObject {
  @public
    gpui::SysSpeechSession* session;
}
@end

@implementation GpuiSpeechSessionRef
- (void)dealloc {
    if (session) {
        gpui::SessionRelease(session);
    }
}
@end

namespace gpui {

// Without this Info.plist key, asking for speech recognition access
// terminates the process.
static NSString* const kUsageDescriptionKey =
    @"NSSpeechRecognitionUsageDescription";

// The error SFSpeechRecognizer reports when the audio held no speech.
static NSString* const kNoSpeechDomain = @"kAFAssistantErrorDomain";
static const NSInteger kNoSpeechCode = 1110;

struct SysSpeechRecognizer {
    // nil when the locale has no recognizer or the application cannot ask
    // for access.
    SFSpeechRecognizer* recognizer = nil;
};

static bool HasUsageDescription() {
    return [[NSBundle mainBundle]
               objectForInfoDictionaryKey:kUsageDescriptionKey] != nil;
}

static bool IsSpeechDenied(SFSpeechRecognizerAuthorizationStatus status) {
    return status == SFSpeechRecognizerAuthorizationStatusDenied ||
           status == SFSpeechRecognizerAuthorizationStatusRestricted;
}

SysSpeechRecognizer* SysSpeechRecognizerNew(Str locale) {
    SysSpeechRecognizer* r = new SysSpeechRecognizer();
    if (!HasUsageDescription()) {
        logf(
            "speech recognition is unavailable: the application's Info.plist "
            "has no `NSSpeechRecognitionUsageDescription`, and asking for "
            "access without it terminates the process\n");
        return r;
    }
    if (len(locale) > 0) {
        NSString* identifier =
            [[NSString alloc] initWithBytes:locale.s
                                     length:(NSUInteger)len(locale)
                                   encoding:NSUTF8StringEncoding];
        NSLocale* nsLocale =
            identifier ? [[NSLocale alloc] initWithLocaleIdentifier:identifier]
                       : nil;
        r->recognizer =
            nsLocale ? [[SFSpeechRecognizer alloc] initWithLocale:nsLocale]
                     : nil;
    } else {
        r->recognizer = [[SFSpeechRecognizer alloc] init];
    }
    if (!r->recognizer) {
        logf(
            "speech recognition is unavailable: no recognizer for the "
            "locale\n");
    }
    return r;
}

void SysSpeechRecognizerFree(SysSpeechRecognizer* r) {
    delete r;
}

// The recognizer, if it can recognize on the device.
static SFSpeechRecognizer* OnDevice(const SysSpeechRecognizer* r) {
    SFSpeechRecognizer* recognizer = r ? r->recognizer : nil;
    if (!recognizer || !recognizer.isAvailable) {
        return nil;
    }
    if (@available(macOS 10.15, *)) {
        return recognizer.supportsOnDeviceRecognition ? recognizer : nil;
    }
    return nil;
}

bool SysSpeechRecognizerAvailable(const SysSpeechRecognizer* r) {
    return OnDevice(r) != nil &&
           !IsSpeechDenied([SFSpeechRecognizer authorizationStatus]);
}

// ─── the recognition ──────────────────────────────────────────────────────

struct SysSpeechSession {
    // Held by the caller and by each event on its way to the main thread.
    volatile int refs = 1;
    SysSpeechEvents events = {};
    // Main thread only.
    bool dropped = false;

    SFSpeechRecognizer* recognizer = nil;
    SFSpeechAudioBufferRecognitionRequest* request = nil;
    AVAudioFormat* format = nil;
    // nil until access is granted.
    SFSpeechRecognitionTask* task = nil;
    // Audio pushed before the task started.
    Vec<int16_t> pending;
    bool finishing = false;
    bool done = false;
    // The text of the last result that ended an utterance, until the next
    // result shows whether the recognizer carries it on. Heap, owned.
    Str utterance = {};
    bool hasUtterance = false;
    // The current hypothesis, as the framework reported it. Heap, owned.
    Str hypothesis = {};
    // The last character committed as a phrase, to join the next one.
    uint32_t lastCommitted = 0;
    bool hasLastCommitted = false;

    ~SysSpeechSession() {
        StrFree(utterance);
        StrFree(hypothesis);
    }
};

static void SessionAddRef(SysSpeechSession* s) {
    __atomic_add_fetch(&s->refs, 1, __ATOMIC_RELAXED);
}

static GpuiSpeechSessionRef* SessionRef(SysSpeechSession* s) {
    GpuiSpeechSessionRef* ref = [[GpuiSpeechSessionRef alloc] init];
    ref->session = s;
    SessionAddRef(s);
    return ref;
}

void SessionRelease(SysSpeechSession* s) {
    if (__atomic_sub_fetch(&s->refs, 1, __ATOMIC_ACQ_REL) == 0) {
        delete s;
    }
}

// What the framework reports, sent from its queues to the main thread.
enum class SpeechEventKind : uint8_t {
    Authorized,
    Result,
    Error
};

struct SpeechEventMessage {
    SysSpeechSession* session = nullptr;
    SpeechEventKind kind = SpeechEventKind::Authorized;
    bool authorized = false;
    // Heap, owned: a Result's text, an Error's message.
    Str text = {};
    bool isFinal = false;
    // The result ends an utterance, see OnResult.
    bool endsUtterance = false;
    // Heap, owned.
    Str domain = {};
    long code = 0;
};

static Str HeapStr(NSString* s) {
    const char* utf8 = s ? [s UTF8String] : nullptr;
    return utf8 ? StrDup(Str(utf8)) : Str{};
}

static void OnEvent(SpeechEventMessage* m);

static void Post(SpeechEventMessage* m) {
    SessionAddRef(m->session);
    ExecPost(MkFunc0(&OnEvent, m));
}

static uint32_t FirstChar(Str s, int* bytes) {
    uint8_t b = (uint8_t)s.s[0];
    int n = b < 0x80 ? 1 : (b >> 5) == 0x6 ? 2 : (b >> 4) == 0xE ? 3 : 4;
    if (n > len(s)) {
        n = 1;
    }
    uint32_t c = n == 1   ? b
                 : n == 2 ? (b & 0x1F)
                 : n == 3 ? (b & 0x0F)
                          : (b & 0x07);
    for (int i = 1; i < n; i++) {
        c = (c << 6) | ((uint8_t)s.s[i] & 0x3F);
    }
    *bytes = n;
    return c;
}

static uint32_t LastChar(Str s) {
    int at = len(s) - 1;
    while (at > 0 && ((uint8_t)s.s[at] & 0xC0) == 0x80) {
        at--;
    }
    int n = 0;
    return FirstChar(Str(s.s + at, len(s) - at), &n);
}

// joined: `text` with the separator it needs after the committed phrases.
static Str JoinedTemp(const SysSpeechSession* s, Str text) {
    if (s->hasLastCommitted && len(text) > 0) {
        int n = 0;
        if (SpeechNeedsSpace(s->lastCommitted, FirstChar(text, &n))) {
            return fmt(" %s", text);
        }
    }
    return text;
}

static void CommitPhrase(SysSpeechSession* s, Str text) {
    if (len(text) == 0) {
        return;
    }
    Str joined = JoinedTemp(s, text);
    s->lastCommitted = LastChar(joined);
    s->hasLastCommitted = true;
    s->events.phrase(s->events.user, joined);
}

static void AppendAudio(SysSpeechSession* s, const int16_t* samples, int count) {
    if (count <= 0) {
        return;
    }
    AVAudioPCMBuffer* buffer =
        [[AVAudioPCMBuffer alloc] initWithPCMFormat:s->format
                                      frameCapacity:(AVAudioFrameCount)count];
    if (!buffer || !buffer.int16ChannelData) {
        return;
    }
    // The format is 16-bit mono, so channel 0 holds `count` writable samples.
    memcpy(buffer.int16ChannelData[0], samples,
           (size_t)count * sizeof(int16_t));
    buffer.frameLength = (AVAudioFrameCount)count;
    [s->request appendAudioPCMBuffer:buffer];
}

// Start recognizing, once access is granted.
static void BeginRecognition(SysSpeechSession* s) {
    // The block only copies and posts, so it may run on any queue.
    GpuiSpeechSessionRef* ref = SessionRef(s);
    s->task = [s->recognizer
        recognitionTaskWithRequest:s->request
                     resultHandler:^(SFSpeechRecognitionResult* result,
                                     NSError* error) {
                       SysSpeechSession* session = ref->session;
                       if (result) {
                           SpeechEventMessage* m = new SpeechEventMessage();
                           m->session = session;
                           m->kind = SpeechEventKind::Result;
                           m->text = HeapStr(result.bestTranscription
                                                 .formattedString);
                           m->isFinal = result.isFinal;
                           if (@available(macOS 11.3, *)) {
                               m->endsUtterance =
                                   result.speechRecognitionMetadata != nil;
                           }
                           Post(m);
                       }
                       if (error) {
                           SpeechEventMessage* m = new SpeechEventMessage();
                           m->session = session;
                           m->kind = SpeechEventKind::Error;
                           m->domain = HeapStr(error.domain);
                           m->code = (long)error.code;
                           m->text = HeapStr(error.localizedDescription);
                           Post(m);
                       }
                     }];
    if (len(s->pending) > 0) {
        AppendAudio(s, s->pending.els, len(s->pending));
        s->pending.len = 0;
    }
    if (s->finishing) {
        [s->request endAudio];
    }
    s->events.ready(s->events.user);
}

// on_result: results carry the whole text of the request so far, except that
// some macOS versions start over after a pause: the result that ends an
// utterance has metadata, and the next one may no longer include its text.
// Commit that utterance as a phrase only once it is dropped.
static void OnResult(SysSpeechSession* s, Str text, bool isFinal,
                     bool endsUtterance) {
    if (s->hasUtterance) {
        Str utterance = s->utterance;
        s->utterance = {};
        s->hasUtterance = false;
        if (SpeechStartsOver(utterance, text)) {
            CommitPhrase(s, utterance);
        }
        StrFree(utterance);
        if (s->dropped) {
            return;
        }
    }
    if (isFinal) {
        StrFree(s->hypothesis);
        s->hypothesis = {};
        CommitPhrase(s, text);
        s->done = true;
        if (!s->dropped) {
            s->events.finish(s->events.user);
        }
        return;
    }
    if (endsUtterance) {
        s->utterance = len(text) > 0 ? StrDup(text) : Str{};
        s->hasUtterance = true;
    }
    Str shown = JoinedTemp(s, text);
    StrFree(s->hypothesis);
    s->hypothesis = len(text) > 0 ? StrDup(text) : Str{};
    s->events.hypothesis(s->events.user, shown);
}

// on_event. Main thread.
static void OnEvent(SpeechEventMessage* m) {
    SysSpeechSession* s = m->session;
    if (!s->dropped && !s->done) {
        switch (m->kind) {
            case SpeechEventKind::Authorized:
                if (m->authorized) {
                    BeginRecognition(s);
                } else {
                    s->done = true;
                    s->events.error(s->events.user,
                                    SysSpeechError::PermissionDenied, Str{});
                }
                break;
            case SpeechEventKind::Result:
                OnResult(s, m->text, m->isFinal, m->endsUtterance);
                break;
            case SpeechEventKind::Error: {
                bool noSpeech =
                    s->finishing && m->code == kNoSpeechCode &&
                    StrEq(m->domain, Str([kNoSpeechDomain UTF8String]));
                s->done = true;
                if (noSpeech) {
                    // Stopping without (more) speech is a normal end.
                    Str hypothesis = s->hypothesis;
                    s->hypothesis = {};
                    CommitPhrase(s, hypothesis);
                    StrFree(hypothesis);
                    if (!s->dropped) {
                        s->events.finish(s->events.user);
                    }
                } else {
                    s->events.error(
                        s->events.user, SysSpeechError::Recognizer,
                        fmt("%s (%s %d)", m->text, m->domain, (int)m->code));
                }
                break;
            }
        }
    }
    StrFree(m->text);
    StrFree(m->domain);
    delete m;
    SessionRelease(s);
}

SysSpeechSession* SysSpeechSessionStart(SysSpeechRecognizer* r,
                                        const SysSpeechEvents& events,
                                        SysSpeechError* error, char* message,
                                        int messageCap) {
    if (message && messageCap > 0) {
        message[0] = 0;
    }
    SFSpeechRecognizer* recognizer = OnDevice(r);
    if (!recognizer) {
        *error = SysSpeechError::Unsupported;
        return nullptr;
    }
    SFSpeechRecognizerAuthorizationStatus authorization =
        [SFSpeechRecognizer authorizationStatus];
    if (IsSpeechDenied(authorization)) {
        *error = SysSpeechError::PermissionDenied;
        return nullptr;
    }

    // 16-bit mono is also the request's native format, so appended audio
    // needs no conversion.
    AVAudioFormat* format =
        [[AVAudioFormat alloc] initWithCommonFormat:AVAudioPCMFormatInt16
                                         sampleRate:16000
                                           channels:1
                                        interleaved:NO];
    if (!format) {
        *error = SysSpeechError::Recognizer;
        const char* text = "cannot create the audio format";
        if (message && messageCap > (int)strlen(text)) {
            memcpy(message, text, strlen(text) + 1);
        }
        return nullptr;
    }
    SFSpeechAudioBufferRecognitionRequest* request =
        [[SFSpeechAudioBufferRecognitionRequest alloc] init];
    request.shouldReportPartialResults = YES;
    // Never send audio to Apple's servers.
    if (@available(macOS 10.15, *)) {
        request.requiresOnDeviceRecognition = YES;
    }
    // Punctuation needs macOS 13.
    if (@available(macOS 13.0, *)) {
        request.addsPunctuation = YES;
    }

    SysSpeechSession* s = new SysSpeechSession();
    s->events = events;
    s->recognizer = recognizer;
    s->request = request;
    s->format = format;
    if (authorization == SFSpeechRecognizerAuthorizationStatusAuthorized) {
        // `ready` is reported from the session's own first event rather
        // than from inside this call, which the caller has not returned
        // from yet.
        SpeechEventMessage* m = new SpeechEventMessage();
        m->session = s;
        m->kind = SpeechEventKind::Authorized;
        m->authorized = true;
        Post(m);
    } else {
        // Ask for access; the answer arrives as an event. Only reached with
        // the usage description present (see SysSpeechRecognizerNew).
        GpuiSpeechSessionRef* ref = SessionRef(s);
        [SFSpeechRecognizer requestAuthorization:^(
                                SFSpeechRecognizerAuthorizationStatus status) {
          SpeechEventMessage* m = new SpeechEventMessage();
          m->session = ref->session;
          m->kind = SpeechEventKind::Authorized;
          m->authorized =
              status == SFSpeechRecognizerAuthorizationStatusAuthorized;
          Post(m);
        }];
    }
    return s;
}

void SysSpeechSessionPushAudio(SysSpeechSession* s, const int16_t* samples,
                               int count) {
    if (!s || s->done || s->finishing || count <= 0) {
        return;
    }
    if (s->task) {
        AppendAudio(s, samples, count);
    } else {
        int16_t* dst = VecInsertSpace(s->pending, len(s->pending), count);
        if (dst) {
            memcpy(dst, samples, (size_t)count * sizeof(int16_t));
        }
    }
}

void SysSpeechSessionFinish(SysSpeechSession* s) {
    if (!s || s->finishing) {
        return;
    }
    s->finishing = true;
    if (s->task) {
        [s->request endAudio];
    }
}

void SysSpeechSessionDrop(SysSpeechSession* s) {
    if (!s) {
        return;
    }
    s->dropped = true;
    if (s->task) {
        // It reports nothing we still read.
        [s->task cancel];
    }
    s->task = nil;
    s->request = nil;
    s->recognizer = nil;
    SessionRelease(s);
}

} // namespace gpui
