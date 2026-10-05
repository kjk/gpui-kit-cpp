/* ALSA capture of the `default` PCM.

   libasound is a soft dependency, found by cmd/build.ts with pkg-config the
   way libcurl is: GPUI_HAVE_ALSA says it was there. Without it this build
   has no audio input, and speech works with an application's own AudioInput
   only.

   The plug layer behind `default` converts whatever the card runs at, so
   this asks for 16-bit mono at 48 kHz and takes the rate it is given. A
   thread of its own reads 10 ms at a time. */

#include "sys/audio_input.h"

#if GPUI_HAVE_ALSA

#include <alsa/asoundlib.h>
#include <pthread.h>

namespace gpui {

struct AudioInputStream {
    AudioInputCallbacks callbacks = {};
    snd_pcm_t* pcm = nullptr;
    pthread_t thread = {};
    bool threadStarted = false;
    volatile int stop = 0;
    unsigned rate = 48000;
    unsigned channels = 1;
};

static void* CaptureThread(void* param) {
    AudioInputStream* s = (AudioInputStream*)param;
    // 10 ms a read.
    const int kFrames = (int)(s->rate / 100);
    int16_t* pcm =
        (int16_t*)malloc((size_t)kFrames * s->channels * sizeof(int16_t));
    float* mono = (float*)malloc((size_t)kFrames * sizeof(float));
    while (pcm && mono && !__atomic_load_n(&s->stop, __ATOMIC_RELAXED)) {
        snd_pcm_sframes_t got =
            snd_pcm_readi(s->pcm, pcm, (snd_pcm_uframes_t)kFrames);
        if (got < 0) {
            // An overrun or a suspend: recover and carry on.
            if (snd_pcm_recover(s->pcm, (int)got, 1) < 0) {
                if (s->callbacks.error &&
                    !__atomic_load_n(&s->stop, __ATOMIC_RELAXED)) {
                    s->callbacks
                        .error(s->callbacks.user, Str(snd_strerror((int)got)));
                }
                break;
            }
            continue;
        }
        for (snd_pcm_sframes_t f = 0; f < got; f++) {
            float sum = 0;
            for (unsigned c = 0; c < s->channels; c++) {
                sum += (float)pcm[f * s->channels + c] / 32768.f;
            }
            mono[f] = sum / (float)s->channels;
        }
        if (got > 0) {
            s->callbacks.samples(s->callbacks.user, mono, (int)got);
        }
    }
    free(pcm);
    free(mono);
    return nullptr;
}

bool SysAudioInputAvailable() {
    return true;
}

static void SetMessage(char* message, int cap, const char* text) {
    if (!message || cap <= 0) {
        return;
    }
    int n = (int)strlen(text);
    n = n < cap - 1 ? n : cap - 1;
    memcpy(message, text, (size_t)n);
    message[n] = 0;
}

AudioInputStream* SysAudioInputStart(const AudioInputCallbacks& callbacks,
                                     uint32_t* sampleRate,
                                     AudioInputError* error, char* message,
                                     int messageCap) {
    snd_pcm_t* pcm = nullptr;
    int rc = snd_pcm_open(&pcm, "default", SND_PCM_STREAM_CAPTURE, 0);
    if (rc < 0) {
        // No card, no `default`, or no sound server to route it to.
        *error = rc == -EACCES ? AudioInputError::PermissionDenied
                               : AudioInputError::NoInputDevice;
        SetMessage(message, messageCap, snd_strerror(rc));
        return nullptr;
    }
    // Mono first; a device that only records stereo is mixed down here.
    unsigned channels = 1;
    snd_pcm_hw_params_t* params = nullptr;
    snd_pcm_hw_params_alloca(&params);
    unsigned rate = 48000;
    for (;;) {
        rc = snd_pcm_hw_params_any(pcm, params);
        if (rc >= 0) {
            rc = snd_pcm_hw_params_set_access(pcm, params,
                                              SND_PCM_ACCESS_RW_INTERLEAVED);
        }
        if (rc >= 0) {
            rc = snd_pcm_hw_params_set_format(pcm, params,
                                              SND_PCM_FORMAT_S16_LE);
        }
        if (rc >= 0) {
            rc = snd_pcm_hw_params_set_channels(pcm, params, channels);
        }
        rate = 48000;
        if (rc >= 0) {
            rc = snd_pcm_hw_params_set_rate_near(pcm, params, &rate, nullptr);
        }
        if (rc >= 0) {
            rc = snd_pcm_hw_params(pcm, params);
        }
        if (rc >= 0 || channels == 2) {
            break;
        }
        channels = 2;
    }
    if (rc >= 0) {
        rc = snd_pcm_prepare(pcm);
    }
    if (rc < 0 || rate == 0) {
        *error = AudioInputError::Failed;
        SetMessage(message, messageCap, snd_strerror(rc));
        snd_pcm_close(pcm);
        return nullptr;
    }
    AudioInputStream* s = new AudioInputStream();
    s->callbacks = callbacks;
    s->pcm = pcm;
    s->rate = rate;
    s->channels = channels;
    if (pthread_create(&s->thread, nullptr, &CaptureThread, s) != 0) {
        *error = AudioInputError::Failed;
        SetMessage(message, messageCap, "cannot start the capture thread");
        snd_pcm_close(pcm);
        delete s;
        return nullptr;
    }
    s->threadStarted = true;
    if (sampleRate) {
        *sampleRate = rate;
    }
    return s;
}

void SysAudioInputStop(AudioInputStream* s) {
    if (!s) {
        return;
    }
    __atomic_store_n(&s->stop, 1, __ATOMIC_RELAXED);
    if (s->threadStarted) {
        // Wakes a read that is waiting on a silent device.
        snd_pcm_drop(s->pcm);
        pthread_join(s->thread, nullptr);
    }
    snd_pcm_close(s->pcm);
    delete s;
}

} // namespace gpui

#else // !GPUI_HAVE_ALSA

namespace gpui {

bool SysAudioInputAvailable() {
    return false;
}

AudioInputStream* SysAudioInputStart(const AudioInputCallbacks&, uint32_t*,
                                     AudioInputError* error, char* message,
                                     int messageCap) {
    if (error) {
        *error = AudioInputError::Unsupported;
    }
    if (message && messageCap > 0) {
        message[0] = 0;
    }
    return nullptr;
}

void SysAudioInputStop(AudioInputStream*) {}

} // namespace gpui

#endif // GPUI_HAVE_ALSA
