/* WASAPI capture of the default input device, shared mode.

   Everything COM happens on the capture thread, in its own multithreaded
   apartment: the thread opens the device, reports how that went, and then
   waits on the device's event until it is told to stop. cpal does the same
   on a thread of its own; the mix format is taken as the device runs it and
   mixed down to mono here. */

#include "sys/audio_input.h"

#include <audioclient.h>
#include <mmdeviceapi.h>
#include <mmreg.h>

namespace gpui {

struct AudioInputStream {
    AudioInputCallbacks callbacks = {};
    HANDLE thread = nullptr;
    // Signalled once the thread has opened the device, or failed to.
    HANDLE ready = nullptr;
    HANDLE stop = nullptr;
    uint32_t sampleRate = 0;
    AudioInputError error = AudioInputError::None;
    char message[160] = {};
};

static void StreamFail(AudioInputStream* s, AudioInputError error, HRESULT hr,
                       const char* what) {
    s->error = error;
    Str text = fmt("%s (0x%08x)", Str(what), (uint32_t)hr);
    int n = len(text) < (int)sizeof(s->message) - 1
                ? len(text)
                : (int)sizeof(s->message) - 1;
    memcpy(s->message, text.s, (size_t)n);
    s->message[n] = 0;
}

static AudioInputError ErrorOf(HRESULT hr) {
    // ERROR_NOT_FOUND as an HRESULT: there is no default capture endpoint.
    if (hr == (HRESULT)0x80070490 || hr == AUDCLNT_E_DEVICE_INVALIDATED) {
        return AudioInputError::NoInputDevice;
    }
    if (hr == E_ACCESSDENIED) {
        return AudioInputError::PermissionDenied;
    }
    return AudioInputError::Failed;
}

// What one sample of the mix format is.
enum class SampleKind : uint8_t {
    F32,
    I16,
    I32,
    Unknown
};

static SampleKind KindOf(const WAVEFORMATEX* wf) {
    WORD tag = wf->wFormatTag;
    if (tag == WAVE_FORMAT_EXTENSIBLE && wf->cbSize >= 22) {
        // The sub-format GUID's first field is the plain format tag.
        tag = (WORD)((const WAVEFORMATEXTENSIBLE*)wf)->SubFormat.Data1;
    }
    if (tag == WAVE_FORMAT_IEEE_FLOAT && wf->wBitsPerSample == 32) {
        return SampleKind::F32;
    }
    if (tag == WAVE_FORMAT_PCM && wf->wBitsPerSample == 16) {
        return SampleKind::I16;
    }
    if (tag == WAVE_FORMAT_PCM && wf->wBitsPerSample == 32) {
        return SampleKind::I32;
    }
    return SampleKind::Unknown;
}

// One packet's frames, mixed down to mono, into `out`.
static void MixDown(const BYTE* data, UINT32 frames, int channels,
                    SampleKind kind, bool silent, float* out) {
    for (UINT32 f = 0; f < frames; f++) {
        float sum = 0;
        if (!silent && data) {
            for (int c = 0; c < channels; c++) {
                size_t ix = (size_t)f * (size_t)channels + (size_t)c;
                if (kind == SampleKind::F32) {
                    sum += ((const float*)data)[ix];
                } else if (kind == SampleKind::I16) {
                    sum += (float)((const int16_t*)data)[ix] / 32768.f;
                } else {
                    sum += (float)((const int32_t*)data)[ix] / 2147483648.f;
                }
            }
        }
        out[f] = sum / (float)channels;
    }
}

static DWORD WINAPI CaptureThread(LPVOID param) {
    AudioInputStream* s = (AudioInputStream*)param;
    HRESULT coInit = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* device = nullptr;
    IAudioClient* client = nullptr;
    IAudioCaptureClient* capture = nullptr;
    WAVEFORMATEX* format = nullptr;
    HANDLE dataEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    SampleKind kind = SampleKind::Unknown;
    int channels = 1;
    bool started = false;

    HRESULT hr =
        CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                         __uuidof(IMMDeviceEnumerator), (void**)&enumerator);
    const char* what = "cannot enumerate audio devices";
    if (SUCCEEDED(hr)) {
        what = "no default audio input device";
        hr = enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &device);
    }
    if (SUCCEEDED(hr)) {
        what = "cannot open the audio input device";
        hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                              (void**)&client);
    }
    if (SUCCEEDED(hr)) {
        hr = client->GetMixFormat(&format);
    }
    if (SUCCEEDED(hr)) {
        kind = KindOf(format);
        channels = format->nChannels > 0 ? format->nChannels : 1;
        if (kind == SampleKind::Unknown) {
            what = "unsupported sample format";
            hr = E_FAIL;
        }
    }
    if (SUCCEEDED(hr)) {
        what = "cannot start the audio input device";
        // 200 ms of buffer, in 100 ns units: room for a busy main thread.
        hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 2000000, 0,
                                format, nullptr);
    }
    if (SUCCEEDED(hr)) {
        hr = client->SetEventHandle(dataEvent);
    }
    if (SUCCEEDED(hr)) {
        hr = client
                 ->GetService(__uuidof(IAudioCaptureClient), (void**)&capture);
    }
    if (SUCCEEDED(hr)) {
        hr = client->Start();
        started = SUCCEEDED(hr);
    }
    if (FAILED(hr)) {
        StreamFail(s, ErrorOf(hr), hr, what);
    } else {
        s->sampleRate = format->nSamplesPerSec;
    }
    SetEvent(s->ready);

    Vec<float> mono;
    HANDLE waits[2] = {s->stop, dataEvent};
    while (started) {
        DWORD woke = WaitForMultipleObjects(2, waits, FALSE, 2000);
        if (woke == WAIT_OBJECT_0) {
            break;
        }
        if (woke != WAIT_OBJECT_0 + 1 && woke != WAIT_TIMEOUT) {
            break;
        }
        UINT32 packet = 0;
        hr = capture->GetNextPacketSize(&packet);
        while (SUCCEEDED(hr) && packet > 0) {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            hr = capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
            if (FAILED(hr)) {
                break;
            }
            if (frames > 0) {
                mono.len = 0;
                float* out = VecInsertSpace(mono, 0, (int)frames);
                if (out) {
                    MixDown(data, frames, channels, kind,
                            (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0, out);
                    s->callbacks.samples(s->callbacks.user, out, (int)frames);
                }
            }
            capture->ReleaseBuffer(frames);
            hr = capture->GetNextPacketSize(&packet);
        }
        if (FAILED(hr)) {
            if (s->callbacks.error) {
                s->callbacks
                    .error(s->callbacks.user,
                           hr == AUDCLNT_E_DEVICE_INVALIDATED
                               ? StrL("the audio input device went away")
                               : fmt("the audio input device failed (0x%08x)",
                                     (uint32_t)hr));
            }
            break;
        }
    }

    if (started) {
        client->Stop();
    }
    if (capture) capture->Release();
    if (format) CoTaskMemFree(format);
    if (client) client->Release();
    if (device) device->Release();
    if (enumerator) enumerator->Release();
    if (dataEvent) CloseHandle(dataEvent);
    if (SUCCEEDED(coInit)) {
        CoUninitialize();
    }
    return 0;
}

static void StreamFree(AudioInputStream* s) {
    if (s->thread) CloseHandle(s->thread);
    if (s->ready) CloseHandle(s->ready);
    if (s->stop) CloseHandle(s->stop);
    delete s;
}

bool SysAudioInputAvailable() {
    return true;
}

AudioInputStream* SysAudioInputStart(const AudioInputCallbacks& callbacks,
                                     uint32_t* sampleRate,
                                     AudioInputError* error, char* message,
                                     int messageCap) {
    AudioInputStream* s = new AudioInputStream();
    s->callbacks = callbacks;
    s->ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    s->stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    s->thread = s->ready && s->stop
                    ? CreateThread(nullptr, 0, &CaptureThread, s, 0, nullptr)
                    : nullptr;
    if (!s->thread) {
        StreamFail(s, AudioInputError::Failed, E_FAIL,
                   "cannot start the capture thread");
    } else {
        WaitForSingleObject(s->ready, INFINITE);
    }
    if (s->error != AudioInputError::None) {
        if (s->thread) {
            WaitForSingleObject(s->thread, INFINITE);
        }
        if (error) {
            *error = s->error;
        }
        if (message && messageCap > 0) {
            int n = (int)strlen(s->message);
            n = n < messageCap - 1 ? n : messageCap - 1;
            memcpy(message, s->message, (size_t)n);
            message[n] = 0;
        }
        StreamFree(s);
        return nullptr;
    }
    if (sampleRate) {
        *sampleRate = s->sampleRate;
    }
    return s;
}

void SysAudioInputStop(AudioInputStream* s) {
    if (!s) {
        return;
    }
    SetEvent(s->stop);
    WaitForSingleObject(s->thread, INFINITE);
    StreamFree(s);
}

} // namespace gpui
