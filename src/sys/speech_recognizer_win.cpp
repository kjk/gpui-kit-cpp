/* The Windows recognizer: continuous dictation with
   Windows.Media.SpeechRecognition — system/winrt.rs.

   The WinRT recognizer captures from the default microphone itself and has
   no way to consume audio from elsewhere, so the pushed PCM is ignored.

   Rust reaches WinRT through the `windows` crate's projections. This file
   speaks the same interfaces through the SDK's own ABI headers, which are
   plain COM declarations: no C++/WinRT, no WRL, nothing of the standard
   library. combase.dll is loaded by name rather than linked, so the
   executable imports nothing a machine without WinRT lacks; on one, the
   recognizer is simply not there.

   The speech objects are agile, so they are called from the main thread, an
   STA, without further apartment setup. Their completions and events arrive
   on WinRT threads, which only copy what they were handed and post it to
   the main thread (ExecPost), where the session reports to its events. Rust
   awaits those in an async task; here the task is the little state machine
   in OnMessage. */

#include "sys/executor.h"
#include "sys/speech_recognizer.h"

#include <windows.foundation.collections.h>
#include <windows.foundation.h>
#include <windows.globalization.h>
#include <windows.media.speechrecognition.h>

namespace gpui {

using namespace ABI::Windows::Foundation;
using namespace ABI::Windows::Globalization;
using namespace ABI::Windows::Media::SpeechRecognition;

// SPERR_SPEECH_PRIVACY_POLICY_NOT_ACCEPTED: "Online speech recognition" is
// turned off in the privacy settings, which dictation requires.
static const HRESULT kPrivacyPolicyNotAccepted = (HRESULT)0x80045509;
// MF_E_NO_CAPTURE_DEVICES_AVAILABLE.
static const HRESULT kNoCaptureDevices = (HRESULT)0xC00DABE0;

// SpeechRecognitionResultStatus, by value: the later members are behind
// contract-version guards in the header.
enum : int {
    kStatusSuccess = 0,
    kStatusTopicLanguageNotSupported = 1,
    kStatusAudioQualityFailure = 4,
    kStatusUserCanceled = 5,
    kStatusTimeoutExceeded = 7,
    kStatusNetworkFailure = 9,
    kStatusMicrophoneUnavailable = 10,
};
// SpeechRecognitionConfidence::Rejected.
static const int kConfidenceRejected = 3;

// ─── combase, by name ─────────────────────────────────────────────────────

struct WinRt {
    bool tried = false;
    bool ok = false;
    HRESULT(WINAPI* getActivationFactory)(HSTRING, REFIID, void**) = nullptr;
    HRESULT(WINAPI* createString)(PCNZWCH, UINT32, HSTRING*) = nullptr;
    HRESULT(WINAPI* deleteString)(HSTRING) = nullptr;
    PCWSTR(WINAPI* getStringRawBuffer)(HSTRING, UINT32*) = nullptr;
};

static WinRt* WinRtGet() {
    static WinRt rt;
    if (rt.tried) {
        return rt.ok ? &rt : nullptr;
    }
    rt.tried = true;
    HMODULE mod = LoadLibraryW(L"combase.dll");
    if (!mod) {
        return nullptr;
    }
    rt.getActivationFactory =
        (decltype(rt.getActivationFactory))(void*)GetProcAddress(
            mod, "RoGetActivationFactory");
    rt.createString = (decltype(rt.createString))(void*)GetProcAddress(
        mod, "WindowsCreateString");
    rt.deleteString = (decltype(rt.deleteString))(void*)GetProcAddress(
        mod, "WindowsDeleteString");
    rt.getStringRawBuffer =
        (decltype(rt.getStringRawBuffer))(void*)GetProcAddress(
            mod, "WindowsGetStringRawBuffer");
    rt.ok = rt.getActivationFactory && rt.createString && rt.deleteString &&
            rt.getStringRawBuffer;
    return rt.ok ? &rt : nullptr;
}

// An HSTRING for the length of a call.
struct HStr {
    HSTRING h = nullptr;

    explicit HStr(const WCHAR* w) {
        if (WinRt* rt = WinRtGet()) {
            rt->createString(w, (UINT32)wcslen(w), &h);
        }
    }
    explicit HStr(Str s) : HStr(ToCWstrTemp(s)) {}
    ~HStr() {
        if (h) {
            WinRtGet()->deleteString(h);
        }
    }
    HStr(const HStr&) = delete;
    HStr& operator=(const HStr&) = delete;
};

// The text of `h` as heap UTF-8, and `h` deleted. Empty for an empty string.
static Str TakeHString(HSTRING h) {
    WinRt* rt = WinRtGet();
    if (!rt || !h) {
        return {};
    }
    UINT32 n = 0;
    PCWSTR w = rt->getStringRawBuffer(h, &n);
    Str out = {};
    if (w && n > 0) {
        int bytes = WideCharToMultiByte(CP_UTF8, 0, w, (int)n, nullptr, 0,
                                        nullptr, nullptr);
        char* buf = bytes > 0 ? (char*)malloc((size_t)bytes + 1) : nullptr;
        if (buf) {
            WideCharToMultiByte(CP_UTF8, 0, w, (int)n, buf, bytes, nullptr,
                                nullptr);
            buf[bytes] = 0;
            out = StrDup(Str(buf, bytes));
            free(buf);
        }
    }
    rt->deleteString(h);
    return out;
}

template <typename T>
static HRESULT ActivationFactory(const WCHAR* runtimeClass, T** out) {
    *out = nullptr;
    WinRt* rt = WinRtGet();
    if (!rt) {
        return E_NOTIMPL;
    }
    HStr name(runtimeClass);
    if (!name.h) {
        return E_OUTOFMEMORY;
    }
    return rt->getActivationFactory(name.h, __uuidof(T), (void**)out);
}

static bool AsciiEqIgnoreCase(Str a, Str b) {
    if (len(a) != len(b)) {
        return false;
    }
    for (int i = 0; i < len(a); i++) {
        char x = a.s[i], y = b.s[i];
        x = x >= 'A' && x <= 'Z' ? (char)(x - 'A' + 'a') : x;
        y = y >= 'A' && y <= 'Z' ? (char)(y - 'A' + 'a') : y;
        if (x != y) {
            return false;
        }
    }
    return true;
}

// The language's BCP 47 tag, as a heap string.
static Str LanguageTag(ILanguage* language) {
    HSTRING tag = nullptr;
    if (!language || FAILED(language->get_LanguageTag(&tag))) {
        return {};
    }
    return TakeHString(tag);
}

// supported_language: the language to dictate `locale` (or, without one, the
// system's speech language) in, if a speech pack supports it.
//
// A bare language such as `en` falls back to the first supported region.
static ILanguage* SupportedLanguage(Str locale) {
    ISpeechRecognizerStatics* statics = nullptr;
    if (FAILED(ActivationFactory(
            RuntimeClass_Windows_Media_SpeechRecognition_SpeechRecognizer,
            &statics))) {
        return nullptr;
    }
    ILanguage* requested = nullptr;
    if (len(locale) > 0) {
        ILanguageStatics* languages = nullptr;
        ILanguageFactory* factory = nullptr;
        ActivationFactory(RuntimeClass_Windows_Globalization_Language,
                          &languages);
        ActivationFactory(RuntimeClass_Windows_Globalization_Language,
                          &factory);
        HStr tag(locale);
        boolean wellFormed = 0;
        if (languages && factory && tag.h &&
            SUCCEEDED(languages->IsWellFormed(tag.h, &wellFormed)) &&
            wellFormed) {
            factory->CreateLanguage(tag.h, &requested);
        }
        if (languages) languages->Release();
        if (factory) factory->Release();
    } else {
        statics->get_SystemSpeechLanguage(&requested);
    }
    Str want = LanguageTag(requested);
    if (requested) {
        requested->Release();
    }
    ILanguage* found = nullptr;
    ILanguage* fallback = nullptr;
    Collections::IVectorView<Language*>* supported = nullptr;
    if (len(want) > 0 &&
        SUCCEEDED(statics->get_SupportedTopicLanguages(&supported)) &&
        supported) {
        unsigned count = 0;
        supported->get_Size(&count);
        for (unsigned i = 0; i < count && !found; i++) {
            ILanguage* language = nullptr;
            if (FAILED(supported->GetAt(i, &language)) || !language) {
                continue;
            }
            Str tag = LanguageTag(language);
            // `requested-`, then a region.
            bool region = len(tag) > len(want) + 1 && tag.s[len(want)] == '-' &&
                          AsciiEqIgnoreCase(Str(tag.s, len(want)), want);
            if (AsciiEqIgnoreCase(tag, want)) {
                found = language;
            } else if (!fallback && region) {
                fallback = language;
            } else {
                language->Release();
            }
            StrFree(tag);
        }
        supported->Release();
    }
    StrFree(want);
    statics->Release();
    if (found) {
        if (fallback) fallback->Release();
        return found;
    }
    return fallback;
}

struct SysSpeechRecognizer {
    // The dictation language, or null when the requested one is malformed
    // or has no speech pack installed.
    ILanguage* language = nullptr;
    Str separator = {};
};

SysSpeechRecognizer* SysSpeechRecognizerNew(Str locale) {
    if (!WinRtGet()) {
        return nullptr;
    }
    SysSpeechRecognizer* r = new SysSpeechRecognizer();
    r->language = SupportedLanguage(locale);
    r->separator = StrL(" ");
    if (r->language) {
        Str tag = LanguageTag(r->language);
        r->separator = SpeechPhraseSeparator(tag);
        StrFree(tag);
    } else {
        logf("speech: no dictation language\n");
    }
    return r;
}

void SysSpeechRecognizerFree(SysSpeechRecognizer* r) {
    if (!r) {
        return;
    }
    if (r->language) {
        r->language->Release();
    }
    delete r;
}

bool SysSpeechRecognizerAvailable(const SysSpeechRecognizer* r) {
    return r && r->language != nullptr;
}

// ─── the session ──────────────────────────────────────────────────────────

typedef IAsyncOperation<SpeechRecognitionCompilationResult*> CompileOperation;

struct SysSpeechSession {
    // Held by the caller, by each handler and by each message on its way to
    // the main thread.
    volatile LONG refs = 1;
    SysSpeechEvents events = {};
    // Main thread only: nothing is reported once this is set.
    bool dropped = false;
    bool done = false;
    ISpeechRecognizer* recognizer = nullptr;
    ISpeechRecognizer2* recognizer2 = nullptr;
    ISpeechContinuousRecognitionSession* continuous = nullptr;
    EventRegistrationToken hypothesisToken = {};
    EventRegistrationToken resultToken = {};
    EventRegistrationToken completedToken = {};
    bool hasHypothesisToken = false;
    bool hasResultToken = false;
    bool hasCompletedToken = false;
    CompileOperation* compiling = nullptr;
    IAsyncAction* starting = nullptr;
    bool started = false;
    // Finish arrived while connecting; it stops the session once it runs.
    bool finishRequested = false;
    bool separatorDue = false;
    Str separator = {};
};

static void SessionAddRef(SysSpeechSession* s) {
    InterlockedIncrement(&s->refs);
}

static void SessionRelease(SysSpeechSession* s) {
    if (InterlockedDecrement(&s->refs) == 0) {
        delete s;
    }
}

// What the WinRT threads tell the main thread.
enum class SpeechMessageKind : uint8_t {
    Compiled,
    Started,
    Hypothesis,
    Result,
    Completed
};

struct SpeechMessage {
    SysSpeechSession* session = nullptr;
    SpeechMessageKind kind = SpeechMessageKind::Compiled;
    // Heap, owned: the text of a Hypothesis or a Result.
    Str text = {};
    int status = 0;
};

static void OnMessage(SpeechMessage* m);

static void Post(SysSpeechSession* s, SpeechMessageKind kind, Str text = {},
                 int status = 0) {
    SpeechMessage* m = new SpeechMessage();
    m->session = s;
    m->kind = kind;
    m->text = text;
    m->status = status;
    SessionAddRef(s);
    ExecPost(MkFunc0(&OnMessage, m));
}

// The IUnknown half of a handler that belongs to a session. A handler is
// agile: WinRT calls it on whichever thread the event was raised on.
#define GPUI_SPEECH_HANDLER(Name, Interface, SenderT, ArgsT)                   \
    struct Name final : Interface {                                            \
        volatile LONG refs = 1;                                                \
        SysSpeechSession* session = nullptr;                                   \
        explicit Name(SysSpeechSession* s) : session(s) { SessionAddRef(s); }  \
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,                  \
                                                 void** out) override {        \
            if (riid == __uuidof(IUnknown) ||                                  \
                riid == __uuidof(IAgileObject) ||                              \
                riid == __uuidof(Interface)) {                                 \
                *out = static_cast<Interface*>(this);                          \
                AddRef();                                                      \
                return S_OK;                                                   \
            }                                                                  \
            *out = nullptr;                                                    \
            return E_NOINTERFACE;                                              \
        }                                                                      \
        ULONG STDMETHODCALLTYPE AddRef() override {                            \
            return (ULONG)InterlockedIncrement(&refs);                         \
        }                                                                      \
        ULONG STDMETHODCALLTYPE Release() override {                           \
            LONG left = InterlockedDecrement(&refs);                           \
            if (left == 0) {                                                   \
                SessionRelease(session);                                       \
                delete this;                                                   \
            }                                                                  \
            return (ULONG)left;                                                \
        }                                                                      \
        HRESULT STDMETHODCALLTYPE Invoke(SenderT sender, ArgsT args) override; \
    }

typedef ITypedEventHandler<SpeechRecognizer*,
                           SpeechRecognitionHypothesisGeneratedEventArgs*>
    HypothesisHandlerInterface;
typedef ITypedEventHandler<SpeechContinuousRecognitionSession*,
                           SpeechContinuousRecognitionResultGeneratedEventArgs*>
    ResultHandlerInterface;
typedef ITypedEventHandler<SpeechContinuousRecognitionSession*,
                           SpeechContinuousRecognitionCompletedEventArgs*>
    CompletedHandlerInterface;
typedef IAsyncOperationCompletedHandler<SpeechRecognitionCompilationResult*>
    CompiledHandlerInterface;

GPUI_SPEECH_HANDLER(HypothesisHandler, HypothesisHandlerInterface,
                    ISpeechRecognizer*,
                    ISpeechRecognitionHypothesisGeneratedEventArgs*);
GPUI_SPEECH_HANDLER(ResultHandler, ResultHandlerInterface,
                    ISpeechContinuousRecognitionSession*,
                    ISpeechContinuousRecognitionResultGeneratedEventArgs*);
GPUI_SPEECH_HANDLER(CompletedHandler, CompletedHandlerInterface,
                    ISpeechContinuousRecognitionSession*,
                    ISpeechContinuousRecognitionCompletedEventArgs*);
GPUI_SPEECH_HANDLER(CompiledHandler, CompiledHandlerInterface,
                    CompileOperation*, AsyncStatus);
GPUI_SPEECH_HANDLER(StartedHandler, IAsyncActionCompletedHandler, IAsyncAction*,
                    AsyncStatus);

HRESULT HypothesisHandler::Invoke(
    ISpeechRecognizer*, ISpeechRecognitionHypothesisGeneratedEventArgs* args) {
    ISpeechRecognitionHypothesis* hypothesis = nullptr;
    if (args && SUCCEEDED(args->get_Hypothesis(&hypothesis)) && hypothesis) {
        HSTRING text = nullptr;
        if (SUCCEEDED(hypothesis->get_Text(&text))) {
            Post(session, SpeechMessageKind::Hypothesis, TakeHString(text));
        }
        hypothesis->Release();
    }
    return S_OK;
}

// phrase_text: the text of a recognized phrase, unless it was rejected or
// empty.
HRESULT ResultHandler::Invoke(
    ISpeechContinuousRecognitionSession*,
    ISpeechContinuousRecognitionResultGeneratedEventArgs* args) {
    ISpeechRecognitionResult* result = nullptr;
    if (!args || FAILED(args->get_Result(&result)) || !result) {
        return S_OK;
    }
    SpeechRecognitionResultStatus status = {};
    SpeechRecognitionConfidence confidence = {};
    HSTRING text = nullptr;
    if (SUCCEEDED(result->get_Status(&status)) &&
        (int)status == kStatusSuccess &&
        SUCCEEDED(result->get_Confidence(&confidence)) &&
        (int)confidence != kConfidenceRejected &&
        SUCCEEDED(result->get_Text(&text))) {
        Str phrase = TakeHString(text);
        if (len(phrase) > 0) {
            Post(session, SpeechMessageKind::Result, phrase);
        }
    }
    result->Release();
    return S_OK;
}

HRESULT CompletedHandler::Invoke(
    ISpeechContinuousRecognitionSession*,
    ISpeechContinuousRecognitionCompletedEventArgs* args) {
    SpeechRecognitionResultStatus status = {};
    if (args && SUCCEEDED(args->get_Status(&status))) {
        Post(session, SpeechMessageKind::Completed, {}, (int)status);
    }
    return S_OK;
}

HRESULT CompiledHandler::Invoke(CompileOperation*, AsyncStatus) {
    Post(session, SpeechMessageKind::Compiled);
    return S_OK;
}

HRESULT StartedHandler::Invoke(IAsyncAction*, AsyncStatus) {
    Post(session, SpeechMessageKind::Started);
    return S_OK;
}

// Closes the recognizer once its cancellation has landed.
struct CloseHandler final : IAsyncActionCompletedHandler {
    volatile LONG refs = 1;
    IClosable* closable = nullptr;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IAgileObject) ||
            riid == __uuidof(IAsyncActionCompletedHandler)) {
            *out = static_cast<IAsyncActionCompletedHandler*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return (ULONG)InterlockedIncrement(&refs);
    }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG left = InterlockedDecrement(&refs);
        if (left == 0) {
            if (closable) {
                closable->Release();
            }
            delete this;
        }
        return (ULONG)left;
    }
    HRESULT STDMETHODCALLTYPE Invoke(IAsyncAction*, AsyncStatus) override {
        if (closable) {
            closable->Close();
        }
        return S_OK;
    }
};

// speech_error
static void ErrorOfHresult(HRESULT hr, SysSpeechError* kind, char* message,
                           int cap) {
    Str text = {};
    if (hr == E_ACCESSDENIED) {
        *kind = SysSpeechError::PermissionDenied;
    } else if (hr == kNoCaptureDevices) {
        *kind = SysSpeechError::NoInputDevice;
    } else if (hr == kPrivacyPolicyNotAccepted) {
        *kind = SysSpeechError::Recognizer;
        text = StrL(
            "Online speech recognition is turned off; turn it on in "
            "Settings > Privacy & security > Speech");
    } else {
        *kind = SysSpeechError::Recognizer;
        WCHAR wide[256] = {};
        DWORD n = FormatMessageW(
            FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
            (DWORD)hr, 0, wide, 255, nullptr);
        while (n > 0 && (wide[n - 1] == L'\r' || wide[n - 1] == L'\n' ||
                         wide[n - 1] == L' ')) {
            wide[--n] = 0;
        }
        char utf8[512] = {};
        if (n > 0) {
            WideCharToMultiByte(CP_UTF8, 0, wide, (int)n, utf8,
                                (int)sizeof(utf8) - 1, nullptr, nullptr);
        }
        text = utf8[0] ? fmt("%s (0x%08x)", Str(utf8), (uint32_t)hr)
                       : fmt("HRESULT 0x%08x", (uint32_t)hr);
    }
    if (message && cap > 0) {
        int n = len(text) < cap - 1 ? len(text) : cap - 1;
        if (n > 0) {
            memcpy(message, text.s, (size_t)n);
        }
        message[n] = 0;
    }
}

// status_error
static void ErrorOfStatus(int status, SysSpeechError* kind, Str* message) {
    *message = {};
    switch (status) {
        case kStatusTopicLanguageNotSupported:
            *kind = SysSpeechError::Unsupported;
            return;
        case kStatusMicrophoneUnavailable:
            *kind = SysSpeechError::NoInputDevice;
            return;
        case kStatusNetworkFailure:
            *kind = SysSpeechError::Recognizer;
            *message = StrL("could not reach the online speech service");
            return;
        case kStatusAudioQualityFailure:
            *kind = SysSpeechError::Recognizer;
            *message = StrL("the audio was too poor to recognize");
            return;
        default:
            *kind = SysSpeechError::Recognizer;
            *message = fmt("recognition ended with status %d", status);
            return;
    }
}

static void FailHresult(SysSpeechSession* s, HRESULT hr) {
    SysSpeechError kind = SysSpeechError::Recognizer;
    char message[600] = {};
    ErrorOfHresult(hr, &kind, message, (int)sizeof(message));
    s->done = true;
    s->events.error(s->events.user, kind, Str(message));
}

static void FailStatus(SysSpeechSession* s, int status) {
    SysSpeechError kind = SysSpeechError::Recognizer;
    Str message;
    ErrorOfStatus(status, &kind, &message);
    s->done = true;
    s->events.error(s->events.user, kind, message);
}

// Stopping flushes the last phrase, then completes the session.
//
// The session may have ended by itself, e.g. after the silence timeout, with
// its Completed still queued behind this; stopping it then fails, and that
// queued event ends the session instead.
static void StopDictation(SysSpeechSession* s) {
    IAsyncAction* stop = nullptr;
    HRESULT hr = s->continuous->StopAsync(&stop);
    if (stop) {
        stop->Release();
    }
    if (FAILED(hr)) {
        logf("speech: stopping dictation failed: 0x%08x\n", (uint32_t)hr);
    }
}

// joined: `text` preceded by the separator once a phrase has been committed.
static Str JoinedTemp(const SysSpeechSession* s, Str text) {
    return s->separatorDue ? fmt("%s%s", s->separator, text) : text;
}

// dictate, one message at a time. Main thread.
static void OnMessage(SpeechMessage* m) {
    SysSpeechSession* s = m->session;
    if (!s->dropped && !s->done) {
        switch (m->kind) {
            case SpeechMessageKind::Compiled: {
                ISpeechRecognitionCompilationResult* result = nullptr;
                HRESULT hr =
                    s->compiling ? s->compiling->GetResults(&result) : E_FAIL;
                SpeechRecognitionResultStatus status = {};
                if (SUCCEEDED(hr) && result) {
                    hr = result->get_Status(&status);
                }
                if (result) {
                    result->Release();
                }
                if (FAILED(hr)) {
                    FailHresult(s, hr);
                    break;
                }
                if ((int)status != kStatusSuccess) {
                    FailStatus(s, (int)status);
                    break;
                }
                hr = s->continuous->StartAsync(&s->starting);
                if (SUCCEEDED(hr) && s->starting) {
                    StartedHandler* handler = new StartedHandler(s);
                    hr = s->starting->put_Completed(handler);
                    handler->Release();
                }
                if (FAILED(hr)) {
                    FailHresult(s, hr);
                }
                break;
            }
            case SpeechMessageKind::Started: {
                HRESULT hr = s->starting ? s->starting->GetResults() : E_FAIL;
                if (FAILED(hr)) {
                    FailHresult(s, hr);
                    break;
                }
                s->started = true;
                s->events.ready(s->events.user);
                // Finishing while connecting stops the session as soon as it
                // runs.
                if (!s->dropped && s->finishRequested) {
                    StopDictation(s);
                }
                break;
            }
            case SpeechMessageKind::Hypothesis:
                s->events.hypothesis(s->events.user, JoinedTemp(s, m->text));
                break;
            case SpeechMessageKind::Result: {
                Str text = JoinedTemp(s, m->text);
                s->separatorDue = true;
                s->events.phrase(s->events.user, text);
                break;
            }
            case SpeechMessageKind::Completed:
                // Stopped, cancelled, or ended by the silence timeout.
                if (m->status == kStatusSuccess ||
                    m->status == kStatusUserCanceled ||
                    m->status == kStatusTimeoutExceeded) {
                    s->done = true;
                    s->events.finish(s->events.user);
                } else {
                    FailStatus(s, m->status);
                }
                break;
        }
    }
    StrFree(m->text);
    delete m;
    SessionRelease(s);
}

// What Drop does to the COM half, also the unwinding of a failed start.
static void SessionTearDown(SysSpeechSession* s) {
    if (s->hasHypothesisToken && s->recognizer2) {
        s->recognizer2->remove_HypothesisGenerated(s->hypothesisToken);
    }
    if (s->hasResultToken && s->continuous) {
        s->continuous->remove_ResultGenerated(s->resultToken);
    }
    if (s->hasCompletedToken && s->continuous) {
        s->continuous->remove_Completed(s->completedToken);
    }
    s->hasHypothesisToken = s->hasResultToken = s->hasCompletedToken = false;

    // Close the recognizer once the cancellation lands, or right away when
    // there is nothing to cancel.
    IClosable* closable = nullptr;
    if (s->recognizer) {
        s->recognizer->QueryInterface(__uuidof(IClosable), (void**)&closable);
    }
    bool closing = false;
    IAsyncAction* cancel = nullptr;
    if (closable && s->continuous &&
        SUCCEEDED(s->continuous->CancelAsync(&cancel)) && cancel) {
        CloseHandler* handler = new CloseHandler();
        handler->closable = closable;
        closable->AddRef();
        closing = SUCCEEDED(cancel->put_Completed(handler));
        handler->Release();
    }
    if (cancel) {
        cancel->Release();
    }
    if (closable) {
        if (!closing) {
            closable->Close();
        }
        closable->Release();
    }
    if (s->compiling) s->compiling->Release();
    if (s->starting) s->starting->Release();
    if (s->continuous) s->continuous->Release();
    if (s->recognizer2) s->recognizer2->Release();
    if (s->recognizer) s->recognizer->Release();
    s->compiling = nullptr;
    s->starting = nullptr;
    s->continuous = nullptr;
    s->recognizer2 = nullptr;
    s->recognizer = nullptr;
}

SysSpeechSession* SysSpeechSessionStart(SysSpeechRecognizer* r,
                                        const SysSpeechEvents& events,
                                        SysSpeechError* error, char* message,
                                        int messageCap) {
    if (message && messageCap > 0) {
        message[0] = 0;
    }
    if (!r || !r->language) {
        *error = SysSpeechError::Unsupported;
        return nullptr;
    }
    SysSpeechSession* s = new SysSpeechSession();
    s->events = events;
    s->separator = r->separator;

    ISpeechRecognizerFactory* factory = nullptr;
    HRESULT hr = ActivationFactory(
        RuntimeClass_Windows_Media_SpeechRecognition_SpeechRecognizer,
        &factory);
    if (SUCCEEDED(hr)) {
        hr = factory->Create(r->language, &s->recognizer);
    }
    if (factory) {
        factory->Release();
    }
    if (SUCCEEDED(hr)) {
        hr = s->recognizer->QueryInterface(__uuidof(ISpeechRecognizer2),
                                           (void**)&s->recognizer2);
    }
    if (SUCCEEDED(hr)) {
        hr = s->recognizer2->get_ContinuousRecognitionSession(&s->continuous);
    }

    // The dictation topic constraint.
    ISpeechRecognitionTopicConstraintFactory* topics = nullptr;
    ISpeechRecognitionTopicConstraint* topic = nullptr;
    ISpeechRecognitionConstraint* constraint = nullptr;
    Collections::IVector<ISpeechRecognitionConstraint*>* constraints = nullptr;
    if (SUCCEEDED(hr)) {
        hr = ActivationFactory(
            RuntimeClass_Windows_Media_SpeechRecognition_SpeechRecognitionTopicConstraint,
            &topics);
    }
    if (SUCCEEDED(hr)) {
        HStr hint(L"dictation");
        hr = topics
                 ->Create(SpeechRecognitionScenario_Dictation, hint.h, &topic);
    }
    if (SUCCEEDED(hr)) {
        hr = topic->QueryInterface(__uuidof(ISpeechRecognitionConstraint),
                                   (void**)&constraint);
    }
    if (SUCCEEDED(hr)) {
        hr = s->recognizer->get_Constraints(&constraints);
    }
    if (SUCCEEDED(hr)) {
        hr = constraints->Append(constraint);
    }
    if (constraints) constraints->Release();
    if (constraint) constraint->Release();
    if (topic) topic->Release();
    if (topics) topics->Release();

    if (SUCCEEDED(hr)) {
        HypothesisHandler* handler = new HypothesisHandler(s);
        hr = s->recognizer2
                 ->add_HypothesisGenerated(handler, &s->hypothesisToken);
        s->hasHypothesisToken = SUCCEEDED(hr);
        handler->Release();
    }
    if (SUCCEEDED(hr)) {
        ResultHandler* handler = new ResultHandler(s);
        hr = s->continuous->add_ResultGenerated(handler, &s->resultToken);
        s->hasResultToken = SUCCEEDED(hr);
        handler->Release();
    }
    if (SUCCEEDED(hr)) {
        CompletedHandler* handler = new CompletedHandler(s);
        hr = s->continuous->add_Completed(handler, &s->completedToken);
        s->hasCompletedToken = SUCCEEDED(hr);
        handler->Release();
    }
    // Compile the dictation constraint; the rest follows in OnMessage.
    if (SUCCEEDED(hr)) {
        hr = s->recognizer->CompileConstraintsAsync(&s->compiling);
    }
    if (SUCCEEDED(hr)) {
        CompiledHandler* handler = new CompiledHandler(s);
        hr = s->compiling->put_Completed(handler);
        handler->Release();
    }
    if (FAILED(hr)) {
        ErrorOfHresult(hr, error, message, messageCap);
        s->dropped = true;
        SessionTearDown(s);
        SessionRelease(s);
        return nullptr;
    }
    return s;
}

// Ignored: the WinRT recognizer captures from the microphone itself.
void SysSpeechSessionPushAudio(SysSpeechSession*, const int16_t*, int) {}

void SysSpeechSessionFinish(SysSpeechSession* s) {
    if (!s || s->dropped || s->done || s->finishRequested) {
        return;
    }
    s->finishRequested = true;
    if (s->started) {
        StopDictation(s);
    }
}

void SysSpeechSessionDrop(SysSpeechSession* s) {
    if (!s) {
        return;
    }
    s->dropped = true;
    SessionTearDown(s);
    SessionRelease(s);
}

} // namespace gpui
