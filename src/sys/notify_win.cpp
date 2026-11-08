/* The Windows notification center, reached through the notification area.

   One hidden window owns one icon, and a balloon on that icon is what
   Windows 10 and 11 show as a toast and file in the Action Center. See
   sys/notify.h for why this rather than the WinRT toast API. */

#include "sys/notify.h"

#include <shellapi.h>

// Vista and later; spelled out so this does not depend on which NTDDI the
// amalgam was compiled against.
#ifndef NIN_BALLOONSHOW
#define NIN_BALLOONSHOW (WM_USER + 2)
#define NIN_BALLOONHIDE (WM_USER + 3)
#define NIN_BALLOONTIMEOUT (WM_USER + 4)
#define NIN_BALLOONUSERCLICK (WM_USER + 5)
#endif

namespace gpui {

// WM_APP and not WM_USER: the window is ours, but WM_USER is the shell's to
// reuse in a dialog procedure and this is the safer half of the range.
static const UINT kNotifyMsg = WM_APP + 71;
static const UINT kIconId = 1;

// Long enough for every tag the notification component makes, which is a
// fixed prefix and a decimal id.
static const int kTagCap = 128;

struct WinNotify {
    HWND hwnd = nullptr;
    bool iconAdded = false;
    // The tag of the balloon on screen, empty when none is. Only one is: a
    // second post replaces the first, which is what a tag asks for anyway.
    char tag[kTagCap] = {};
    // How many balloons were replaced while on screen and have not yet
    // reported their disappearance. The shell sends NIN_BALLOONHIDE for the
    // replaced one after the new tag is already stored, and without this
    // that late message would clear the new balloon's tag.
    int replacedHides = 0;
    char appName[128] = {};
    SysNotifyResponseFn onResponse = {};
};

static WinNotify gNotify;

static void FillIconData(NOTIFYICONDATAW* nid) {
    ZeroStruct(nid);
    nid->cbSize = sizeof(*nid);
    nid->hWnd = gNotify.hwnd;
    nid->uID = kIconId;
}

static LRESULT CALLBACK NotifyWndProc(HWND hwnd, UINT msg, WPARAM wp,
                                      LPARAM lp) {
    if (msg != kNotifyMsg) {
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    // The event is the low word of lParam under NOTIFYICON_VERSION_4 and the
    // whole of it before that, and NIN_* all fit in a word either way.
    UINT ev = LOWORD(lp);
    if (ev == NIN_BALLOONUSERCLICK) {
        // Copied out first: the handler is free to post another notification,
        // which writes `gNotify.tag`.
        TempStr tag = StrDupTemp(Str(gNotify.tag));
        gNotify.tag[0] = 0;
        if (gNotify.onResponse.IsValid() && tag) {
            gNotify.onResponse.Call(tag);
        }
    } else if (ev == NIN_BALLOONHIDE && gNotify.replacedHides > 0) {
        // The balloon a later post replaced going away, not the one on
        // screen now.
        gNotify.replacedHides--;
    } else if (ev == NIN_BALLOONTIMEOUT || ev == NIN_BALLOONHIDE) {
        // Timed out into the Action Center, or taken off the screen. Either
        // way there is nothing left for a dismiss to retract.
        gNotify.tag[0] = 0;
        gNotify.replacedHides = 0;
    }
    return 0;
}

// The window and its icon, made on the first post. Nothing is in the
// notification area until an application asks for a system notification.
static bool EnsureIcon() {
    if (gNotify.iconAdded) {
        return true;
    }
    if (!gNotify.hwnd) {
        HINSTANCE inst = GetModuleHandleW(nullptr);
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = NotifyWndProc;
        wc.hInstance = inst;
        wc.lpszClassName = L"GpuiNotifySink";
        // A second registration of the same class fails with
        // ERROR_CLASS_ALREADY_EXISTS, which CreateWindowExW is happy with.
        RegisterClassExW(&wc);
        // A real window rather than a message-only one: the shell talks to
        // the notification area's owner, and HWND_MESSAGE windows are not
        // everywhere accepted for that. It is never shown.
        gNotify
            .hwnd = CreateWindowExW(0, L"GpuiNotifySink", L"", WS_POPUP, 0, 0,
                                    0, 0, nullptr, nullptr, inst, nullptr);
        if (!gNotify.hwnd) {
            return false;
        }
    }
    NOTIFYICONDATAW nid;
    FillIconData(&nid);
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = kNotifyMsg;
    // The executable's own icon, and the generic application one for a build
    // that has none.
    nid.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1));
    if (!nid.hIcon) {
        nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    }
    Str name = Str(gNotify.appName[0] ? gNotify.appName : "");
    if (len(name) > 0) {
        wcsncpy_s(nid.szTip, ToCWstrTemp(name), _TRUNCATE);
    }
    if (!Shell_NotifyIconW(NIM_ADD, &nid)) {
        return false;
    }
    // Version 4 for the modern callback packing; a failure here leaves the
    // original packing, which the window procedure also reads.
    nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid);
    gNotify.iconAdded = true;
    return true;
}

bool SysNotifyAvailable() {
    return true;
}

void SysNotifySetAppIdentity(Str, Str appName) {
    // The identity is the notification area icon's name here. Windows wants
    // an AppUserModelID for the WinRT toast API, which this does not use.
    StrCopyZ(gNotify.appName, (int)sizeof(gNotify.appName),
             appName.s ? appName.s : "");
    if (gNotify.iconAdded) {
        NOTIFYICONDATAW nid;
        FillIconData(&nid);
        nid.uFlags = NIF_TIP;
        wcsncpy_s(nid.szTip, ToCWstrTemp(appName), _TRUNCATE);
        Shell_NotifyIconW(NIM_MODIFY, &nid);
    }
}

bool SysNotifyShow(Str tag, Str title, Str body) {
    if (!EnsureIcon()) {
        return false;
    }
    NOTIFYICONDATAW nid;
    FillIconData(&nid);
    nid.uFlags = NIF_INFO;
    nid.dwInfoFlags = NIIF_NONE | NIIF_RESPECT_QUIET_TIME;
    // A balloon with an empty body is not shown at all, so a title-only
    // notification puts its text where the shell will draw it.
    if (len(body) > 0) {
        wcsncpy_s(nid.szInfoTitle, ToCWstrTemp(title), _TRUNCATE);
        wcsncpy_s(nid.szInfo, ToCWstrTemp(body), _TRUNCATE);
    } else {
        wcsncpy_s(nid.szInfo, ToCWstrTemp(title), _TRUNCATE);
    }
    if (!Shell_NotifyIconW(NIM_MODIFY, &nid)) {
        return false;
    }
    if (gNotify.tag[0]) {
        // Replacing a balloon on screen; its hide notice is still to come.
        gNotify.replacedHides++;
    }
    StrCopyCapped(gNotify.tag, kTagCap, tag);
    return true;
}

void SysNotifyDismiss(Str tag) {
    if (!gNotify.iconAdded || !gNotify.tag[0]) {
        return;
    }
    if (!StrEq(Str(gNotify.tag), tag)) {
        // Somebody else's balloon is on screen; the tag this asks about has
        // already gone, or is one the Action Center has kept, which cannot be
        // retracted from here.
        return;
    }
    NOTIFYICONDATAW nid;
    FillIconData(&nid);
    // An empty NIF_INFO takes the balloon off the screen.
    nid.uFlags = NIF_INFO;
    Shell_NotifyIconW(NIM_MODIFY, &nid);
    gNotify.tag[0] = 0;
    gNotify.replacedHides = 0;
}

void SysNotifyOnResponse(SysNotifyResponseFn fn) {
    gNotify.onResponse = fn;
}

void SysNotifyShutdown() {
    if (gNotify.iconAdded) {
        NOTIFYICONDATAW nid;
        FillIconData(&nid);
        Shell_NotifyIconW(NIM_DELETE, &nid);
        gNotify.iconAdded = false;
    }
    if (gNotify.hwnd) {
        DestroyWindow(gNotify.hwnd);
        gNotify.hwnd = nullptr;
    }
    gNotify.onResponse = {};
    gNotify.tag[0] = 0;
    gNotify.replacedHides = 0;
}

} // namespace gpui
