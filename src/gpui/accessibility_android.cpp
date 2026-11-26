#include "gpui/gpui.h"

namespace gpui {

jint AndroidAccessibilityLiveRegion(AccessibilityLive priority) {
    switch (priority) {
        case AccessibilityLive::Assertive:
            return 2; // View.ACCESSIBILITY_LIVE_REGION_ASSERTIVE
        case AccessibilityLive::Polite:
            return 1; // View.ACCESSIBILITY_LIVE_REGION_POLITE
        case AccessibilityLive::Off:
            return 0; // View.ACCESSIBILITY_LIVE_REGION_NONE
    }
    return 0;
}

static jstring AndroidAnnouncementText(JNIEnv* env, Str utf8) {
    Vec<jchar> utf16;
    if (!VecReserve(utf16, len(utf8))) return nullptr;
    for (int i = 0; i < len(utf8);) {
        uint32_t point = 0;
        i += Utf8At(utf8, i, &point);
        if (point > 0x10ffff || (point >= 0xd800 && point <= 0xdfff))
            point = 0xfffd;
        if (point <= 0xffff) {
            VecAppend(utf16, (jchar)point);
        } else {
            point -= 0x10000;
            VecAppend(utf16, (jchar)(0xd800 + (point >> 10)));
            VecAppend(utf16, (jchar)(0xdc00 + (point & 0x3ff)));
        }
    }
    return env->NewString(utf16.els, len(utf16));
}

static jmethodID AndroidAnnouncementMethod(JNIEnv* env, jobject object,
                                           const char* name,
                                           const char* signature) {
    jclass type = env->GetObjectClass(object);
    return type ? env->GetMethodID(type, name, signature) : nullptr;
}

static jobject AndroidAnnouncementProperty(JNIEnv* env, jobject object,
                                           const char* name,
                                           const char* signature) {
    jmethodID method = AndroidAnnouncementMethod(env, object, name, signature);
    return method ? env->CallObjectMethod(object, method) : nullptr;
}

static bool AndroidSendLiveRegionEvent(JNIEnv* env, jobject view,
                                       jint virtualId, Str text) {
    jobject parent = AndroidAnnouncementProperty(env, view, "getParent",
                                                 "()Landroid/view/ViewParent;");
    if (!parent || env->ExceptionCheck()) return false;
    jmethodID send =
        AndroidAnnouncementMethod(env, parent, "requestSendAccessibilityEvent",
                                  "(Landroid/view/View;Landroid/view/"
                                  "accessibility/AccessibilityEvent;)Z");
    if (!send) return false;

    jobject context = AndroidAnnouncementProperty(
        env, view, "getContext", "()Landroid/content/Context;");
    if (!context || env->ExceptionCheck()) return false;
    jobject package = AndroidAnnouncementProperty(
        env, context, "getPackageName", "()Ljava/lang/String;");
    if (!package || env->ExceptionCheck()) return false;

    jclass type =
        env->FindClass("android/view/accessibility/AccessibilityEvent");
    if (!type) return false;
    jmethodID obtain = env->GetStaticMethodID(
        type, "obtain", "(I)Landroid/view/accessibility/AccessibilityEvent;");
    if (!obtain) return false;
    jmethodID setSource =
        env->GetMethodID(type, "setSource", "(Landroid/view/View;I)V");
    if (!setSource) return false;
    jmethodID setChanges =
        env->GetMethodID(type, "setContentChangeTypes", "(I)V");
    if (!setChanges) return false;
    jmethodID setPackage =
        env->GetMethodID(type, "setPackageName", "(Ljava/lang/CharSequence;)V");
    if (!setPackage) return false;
    jmethodID getText = env->GetMethodID(type, "getText", "()Ljava/util/List;");
    if (!getText) return false;

    jstring message = AndroidAnnouncementText(env, text);
    if (!message) return false;
    constexpr jint kTypeWindowContentChanged = 0x800;
    constexpr jint kContentChangeTypeText = 0x2;
    jobject event =
        env->CallStaticObjectMethod(type, obtain, kTypeWindowContentChanged);
    if (!event || env->ExceptionCheck()) return false;
    env->CallVoidMethod(event, setSource, view, virtualId);
    if (env->ExceptionCheck()) return false;
    env->CallVoidMethod(event, setChanges, kContentChangeTypeText);
    if (env->ExceptionCheck()) return false;
    env->CallVoidMethod(event, setPackage, package);
    if (env->ExceptionCheck()) return false;

    jobject items = env->CallObjectMethod(event, getText);
    if (!items || env->ExceptionCheck()) return false;
    jmethodID add =
        AndroidAnnouncementMethod(env, items, "add", "(Ljava/lang/Object;)Z");
    if (!add) return false;
    env->CallBooleanMethod(items, add, message);
    if (env->ExceptionCheck()) return false;
    jboolean accepted = env->CallBooleanMethod(parent, send, view, event);
    return !env->ExceptionCheck() && accepted == JNI_TRUE;
}

bool AndroidPostAccessibilityAnnouncement(
    JNIEnv* env, jobject hostView, jint virtualId,
    const AccessibilityAnnouncement& event) {
    if (!env || !hostView || virtualId == -1 || virtualId == INT32_MAX ||
        event.priority == AccessibilityLive::Off || !len(event.text) ||
        env->ExceptionCheck() || env->PushLocalFrame(16) != JNI_OK)
        return false;
    bool accepted =
        AndroidSendLiveRegionEvent(env, hostView, virtualId, event.text);
    env->PopLocalFrame(nullptr);
    return accepted;
}

} // namespace gpui
