#ifndef GPUI_LOCALE_APPLE_H_
#define GPUI_LOCALE_APPLE_H_

// Expand beside the Apple SDK includes in the platform source. Including
// CoreFoundation in this header would leak Objective-C macros into the
// portable part of the amalgam.
#define GPUI_DEFINE_APPLE_LOCALE()                                     \
    TempStr PlatPreferredLocalesTemp() {                               \
        CFArrayRef names = CFLocaleCopyPreferredLanguages();           \
        if (!names) return {};                                         \
        StrBuilder out(GetTempArena());                                \
        CFIndex n = CFArrayGetCount(names);                            \
        for (CFIndex i = 0; i < n; i++) {                              \
            auto name = (CFStringRef)CFArrayGetValueAtIndex(names, i); \
            char locale[256] = {};                                     \
            if (CFStringGetCString(name, locale, sizeof(locale),       \
                                   kCFStringEncodingUTF8)) {           \
                PlatLocaleListAdd(&out, Str(locale));                  \
            }                                                          \
        }                                                              \
        CFRelease(names);                                              \
        return out.TakeStr();                                          \
    }

#endif // GPUI_LOCALE_APPLE_H_
