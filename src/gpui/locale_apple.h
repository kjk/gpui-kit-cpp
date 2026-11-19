#ifndef GPUI_LOCALE_APPLE_H_
#define GPUI_LOCALE_APPLE_H_

// Expand beside the Apple SDK includes in the platform source. Including
// CoreFoundation in this header would leak Objective-C macros into the
// portable part of the amalgam.
#define GPUI_DEFINE_APPLE_LOCALE()                                      \
    TempStr PlatPreferredLocaleTemp() {                                 \
        CFArrayRef names = CFLocaleCopyPreferredLanguages();            \
        if (!names) return {};                                          \
        char locale[256] = {};                                          \
        if (CFArrayGetCount(names) > 0) {                               \
            auto first = (CFStringRef)CFArrayGetValueAtIndex(names, 0); \
            if (!CFStringGetCString(first, locale, sizeof(locale),      \
                                    kCFStringEncodingUTF8)) {           \
                locale[0] = 0;                                          \
            }                                                           \
        }                                                               \
        CFRelease(names);                                               \
        return StrDupTemp(Str(locale));                                 \
    }

#endif // GPUI_LOCALE_APPLE_H_
