/* Project Window::accessibility through AT-SPI without adding GLib or
   libdbus. Modern Linux toolkits speak these D-Bus interfaces directly too;
   this is the deliberately small application-side subset needed to expose a
   live hierarchy, component geometry, actions, values, selection and text. */

#include "gpui/accessibility_linux.h"

#include <errno.h>
#include <locale.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

// The amalgam lifts X11 includes ahead of Linux sources. Keep its macros from
// rewriting enum members here, then restore them for the X11 backend.
#pragma push_macro("Status")
#pragma push_macro("True")
#undef Status
#undef True

namespace gpui {

static constexpr const char* kRootPath = "/org/a11y/atspi/accessible/root";
static constexpr const char* kNullPath = "/org/a11y/atspi/null";
static constexpr const char* kCachePath = "/org/a11y/atspi/cache";
static constexpr const char* kCache = "org.a11y.atspi.Cache";
#define GPUI_ATSPI_CACHE_ITEM "((so)(so)(so)iiassusau)"
static constexpr const char* kAccessible = "org.a11y.atspi.Accessible";
static constexpr const char* kApplication = "org.a11y.atspi.Application";
static constexpr const char* kComponent = "org.a11y.atspi.Component";
static constexpr const char* kAction = "org.a11y.atspi.Action";
static constexpr const char* kValue = "org.a11y.atspi.Value";
static constexpr const char* kSelection = "org.a11y.atspi.Selection";
static constexpr const char* kText = "org.a11y.atspi.Text";
static constexpr const char* kEditableText = "org.a11y.atspi.EditableText";

struct DbusWriter {
    Vec<uint8_t> bytes;
};

static void PutBytes(DbusWriter* w, const void* data, int n) {
    if (n > 0) {
        memcpy(VecAppendBlanks(w->bytes, n), data, (size_t)n);
    }
}

static void PutAlign(DbusWriter* w, int alignment) {
    int n = (alignment - (len(w->bytes) % alignment)) % alignment;
    if (n) {
        memset(VecAppendBlanks(w->bytes, n), 0, (size_t)n);
    }
}

static void PutByte(DbusWriter* w, uint8_t value) {
    PutBytes(w, &value, 1);
}

static void PutU32(DbusWriter* w, uint32_t value) {
    PutAlign(w, 4);
    PutBytes(w, &value, 4);
}

static void PutI32(DbusWriter* w, int32_t value) {
    PutU32(w, (uint32_t)value);
}

static void PutI16(DbusWriter* w, int16_t value) {
    PutAlign(w, 2);
    PutBytes(w, &value, 2);
}

static void PutDouble(DbusWriter* w, double value) {
    PutAlign(w, 8);
    PutBytes(w, &value, 8);
}

static void PutBool(DbusWriter* w, bool value) {
    PutU32(w, value ? 1u : 0u);
}

// A rectangle as AT-SPI's (iiii): x, y, width, height. Answers the reply's
// signature.
static const char* PutExtents(DbusWriter* w, Bounds b) {
    PutAlign(w, 8);
    PutI32(w, (int)b.x);
    PutI32(w, (int)b.y);
    PutI32(w, (int)b.w);
    PutI32(w, (int)b.h);
    return "(iiii)";
}

static void PutString(DbusWriter* w, Str value) {
    PutAlign(w, 4);
    PutU32(w, (uint32_t)std::max(0, len(value)));
    PutBytes(w, value.s, std::max(0, len(value)));
    PutByte(w, 0);
}

static void PutString(DbusWriter* w, const char* value) {
    PutString(w, value ? Str(value, (int)strlen(value)) : Str{});
}

static void PutSignature(DbusWriter* w, const char* signature) {
    int n = signature ? (int)strlen(signature) : 0;
    PutByte(w, (uint8_t)n);
    PutBytes(w, signature, n);
    PutByte(w, 0);
}

static void PutVariantString(DbusWriter* w, const char* signature, Str value) {
    PutSignature(w, signature);
    PutString(w, value);
}

static void PutVariantU32(DbusWriter* w, uint32_t value) {
    PutSignature(w, "u");
    PutU32(w, value);
}

static void PutVariantI32(DbusWriter* w, int32_t value) {
    PutSignature(w, "i");
    PutI32(w, value);
}

static void PutVariantDouble(DbusWriter* w, double value) {
    PutSignature(w, "d");
    PutDouble(w, value);
}

static void PutHeaderString(DbusWriter* fields, uint8_t code,
                            const char* signature, Str value) {
    PutAlign(fields, 8);
    PutByte(fields, code);
    PutSignature(fields, signature);
    if (signature[0] == 'g') {
        TempStr copy = StrDupTemp(Str(value.s, std::min(len(value), 255)));
        PutSignature(fields, copy.s);
    } else {
        PutString(fields, value);
    }
}

static void PutHeaderU32(DbusWriter* fields, uint8_t code, uint32_t value) {
    PutAlign(fields, 8);
    PutByte(fields, code);
    PutVariantU32(fields, value);
}

struct LinuxTextSnapshot {
    Window* window;
    const InputState* input;
    uint32_t path;
    AccessibilityRole role;
    Str text;
    Selection selection;
    int caret;
    int excerptStart;
    bool excerpt;
    uint64_t revision;
};

struct LinuxNodeSnapshot {
    uint32_t path, id;
    uint32_t wireId;
    int index;
    int previousIndex;
    uint32_t role;
    uint64_t state;
    uint64_t name, description;
    float value;
    bool hasValue;
    Bounds bounds = {};
    int parentIndex = -1;
    int siblingIndex = 0;
    int nextIndex = -1;
    int selectionParent = -1;
    bool selectionContainer = false;
    bool selectionChanged = false;
    uint8_t interfaces = 0;
};

struct LinuxNodeHistory {
    Window* window;
    Vec<LinuxNodeSnapshot> frames[2];
    Vec<uint32_t> wireIds;
    Vec<int> treeOrder;
    Vec<int> childCounts;
    Vec<int> lastRetainedChild;
    uint64_t nextWireId = 1;
    int frame = 0;
    uint32_t focusedNode = 0;
    int focusedHandle = 0;
};

struct AccessibilityLinuxState {
    App* app = nullptr;
    int fd = -1;
    uint32_t serial = 1;
    uint32_t helloSerial = 0;
    uint32_t embedSerial = 0;
    Str busName = {};
    Str desktopBusName = {};
    Str desktopPath = {};
    Vec<uint8_t> rx;
    Vec<LinuxTextSnapshot> text;
    Vec<LinuxNodeHistory*> nodes;
    uint64_t textRevision = 0;
    int32_t applicationId = 0;
    bool ready = false;
};

static AccessibilityLinuxState gA11y;

static LinuxNodeHistory* LinuxHistory(Window* win) {
    for (auto* history : gA11y.nodes) {
        if (history->window == win) return history;
    }
    return nullptr;
}

static bool SendAll(const uint8_t* data, int n) {
    while (n > 0 && gA11y.fd >= 0) {
        ssize_t sent = send(gA11y.fd, data, (size_t)n, MSG_NOSIGNAL);
        if (sent < 0 && errno == EINTR) {
            continue;
        }
        if (sent <= 0) {
            return false;
        }
        data += sent;
        n -= (int)sent;
    }
    return n == 0;
}

static uint32_t SendMessage(uint8_t type, uint8_t flags, DbusWriter* fields,
                            DbusWriter* body) {
    if (gA11y.fd < 0) {
        return 0;
    }
    DbusWriter message;
    PutByte(&message, 'l');
    PutByte(&message, type);
    PutByte(&message, flags);
    PutByte(&message, 1);
    PutU32(&message, body ? (uint32_t)len(body->bytes) : 0);
    uint32_t serial = gA11y.serial++;
    PutU32(&message, serial);
    PutU32(&message, (uint32_t)len(fields->bytes));
    PutBytes(&message, fields->bytes.els, len(fields->bytes));
    PutAlign(&message, 8);
    if (body) {
        PutBytes(&message, body->bytes.els, len(body->bytes));
    }
    bool ok = SendAll(message.bytes.els, len(message.bytes));
    VecReset(message.bytes);
    return ok ? serial : 0;
}

static uint32_t SendCall(const char* destination, const char* path,
                         const char* interfaceName, const char* member,
                         const char* signature, DbusWriter* body) {
    DbusWriter fields;
    PutHeaderString(&fields, 1, "o", Str(path));
    PutHeaderString(&fields, 2, "s", Str(interfaceName));
    PutHeaderString(&fields, 3, "s", Str(member));
    PutHeaderString(&fields, 6, "s", Str(destination));
    if (signature && *signature) {
        PutHeaderString(&fields, 8, "g", Str(signature));
    }
    uint32_t serial = SendMessage(1, 0, &fields, body);
    VecReset(fields.bytes);
    return serial;
}

static void SendReply(uint32_t replySerial, Str destination,
                      const char* signature, DbusWriter* body) {
    DbusWriter fields;
    PutHeaderU32(&fields, 5, replySerial);
    PutHeaderString(&fields, 6, "s", destination);
    if (signature && *signature) {
        PutHeaderString(&fields, 8, "g", Str(signature));
    }
    SendMessage(2, 0, &fields, body);
    VecReset(fields.bytes);
}

static void SendError(uint32_t replySerial, Str destination, const char* name,
                      const char* message) {
    DbusWriter fields;
    DbusWriter body;
    PutHeaderString(&fields, 4, "s", Str(name));
    PutHeaderU32(&fields, 5, replySerial);
    PutHeaderString(&fields, 6, "s", destination);
    PutHeaderString(&fields, 8, "g", Str("s"));
    PutString(&body, message);
    SendMessage(3, 0, &fields, &body);
    VecReset(fields.bytes);
    VecReset(body.bytes);
}

static int AlignAt(int at, int alignment) {
    return at + (alignment - (at % alignment)) % alignment;
}

static uint32_t ReadU32(const uint8_t* data, int size, int* at) {
    *at = AlignAt(*at, 4);
    if (*at < 0 || *at + 4 > size) {
        *at = size;
        return 0;
    }
    uint32_t value = 0;
    memcpy(&value, data + *at, 4);
    *at += 4;
    return value;
}

static int32_t ReadI32(const uint8_t* data, int size, int* at) {
    return (int32_t)ReadU32(data, size, at);
}

static double ReadDouble(const uint8_t* data, int size, int* at) {
    *at = AlignAt(*at, 8);
    if (*at < 0 || *at + 8 > size) {
        *at = size;
        return 0;
    }
    double value = 0;
    memcpy(&value, data + *at, 8);
    *at += 8;
    return value;
}

static Str ReadString(const uint8_t* data, int size, int* at) {
    uint32_t n = ReadU32(data, size, at);
    if (n > (uint32_t)(size - *at) || *at + (int)n + 1 > size) {
        *at = size;
        return {};
    }
    Str value((const char*)data + *at, (int)n);
    *at += (int)n + 1;
    return value;
}

static Str ReadSignature(const uint8_t* data, int size, int* at) {
    if (*at < 0 || *at >= size) return {};
    uint8_t n = data[(*at)++];
    if (*at + n + 1 > size) {
        *at = size;
        return {};
    }
    Str value((const char*)data + *at, n);
    *at += n + 1;
    return value;
}

static void ArrayBegin(DbusWriter* w, int elementAlign, int* lengthAt,
                       int* contentsAt) {
    PutAlign(w, 4);
    *lengthAt = w->bytes.len;
    PutU32(w, 0);
    PutAlign(w, elementAlign);
    *contentsAt = w->bytes.len;
}

static void ArrayEnd(DbusWriter* w, int lengthAt, int contentsAt) {
    uint32_t n = (uint32_t)(len(w->bytes) - contentsAt);
    memcpy(w->bytes.els + lengthAt, &n, 4);
}

static bool StrIs(Str value, const char* literal) {
    return StrEq(value, literal);
}

static bool ParseUnixAddress(Str address, sockaddr_un* out, socklen_t* outLen) {
    if (!address.s || !out || !outLen) {
        return false;
    }
    int copyLen = std::min(len(address), 511);
    TempStr copy = StrDupTemp(Str(address.s, copyLen));
    int key = StrFind(copy, "unix:path=");
    bool abstract = false;
    if (key < 0) {
        key = StrFind(copy, "unix:abstract=");
        abstract = true;
    }
    if (key < 0) {
        return false;
    }
    key += abstract ? 14 : 10;
    int end = key;
    while (end < copyLen && copy.s[end] != ',' && copy.s[end] != ';') {
        end++;
    }
    memset(out, 0, sizeof(*out));
    out->sun_family = AF_UNIX;
    int at = abstract ? 1 : 0;
    // Decoded in place in the copy, then as much as fits.
    int decoded = PercentDecode(Str(copy.s + key, end - key), copy.s + key);
    int room = (int)sizeof(out->sun_path) - 1 - at;
    int take = std::min(decoded, room);
    memcpy(out->sun_path + at, copy.s + key, (size_t)take);
    at += take;
    *outLen =
        (socklen_t)(offsetof(sockaddr_un, sun_path) + at + (abstract ? 0 : 1));
    return at > (abstract ? 1 : 0);
}

static bool Authenticate() {
    TempStr uid = fmt("%u", (unsigned)getuid());
    TempStr auth = AllocStrTemp(159);
    memset(auth.s, 0, (size_t)len(auth));
    int at = 0;
    auth.s[at++] = 0;
    memcpy(auth.s + at, "AUTH EXTERNAL ", 14);
    at += 14;
    for (const char* p = uid.s; *p && at + 4 < len(auth); p++) {
        HexEncode(p, 1, auth.s + at);
        at += 2;
    }
    auth.s[at++] = '\r';
    auth.s[at++] = '\n';
    if (!SendAll((const uint8_t*)auth.s, at)) {
        return false;
    }
    TempStr reply = AllocStrTemp(255);
    reply.s[0] = 0;
    int n = 0;
    while (n < len(reply)) {
        ssize_t got = recv(gA11y.fd, reply.s + n, (size_t)(len(reply) - n), 0);
        if (got <= 0) {
            return false;
        }
        n += (int)got;
        reply.s[n] = 0;
        Str received = Str(reply.s, n);
        if (StrContains(received, StrL("\r\n"))) {
            break;
        }
    }
    if (!StrStartsWith(Str(reply.s, n), "OK ")) {
        return false;
    }
    return SendAll((const uint8_t*)"BEGIN\r\n", 7);
}

// The root, an element node, or part `part` of a document node (a page,
// AccessibilityDocumentSource::partCount), which is a child of that node.
struct LinuxAccessible {
    Window* win = nullptr;
    int windowIndex = -1;
    int nodeIndex = -1;
    int part = -1;
    bool root = false;
};

static uint32_t LinuxWireId(Window* win, uint32_t nodeId) {
    const auto* history = LinuxHistory(win);
    if (!history || !nodeId) return nodeId;
    // Collection assigns IDs in tree order, so the common lookup is direct.
    int index = (int)(nodeId - 1);
    if (nodeId > (uint32_t)len(win->accessibility) || win->accessibility[index]
                                                              .id != nodeId)
        index = WindowAccessibilityNodeIndex(win, nodeId);
    return index >= 0 && index < len(history->wireIds) ? history->wireIds[index]
                                                       : 0;
}

static int LinuxNodeIndexForWire(Window* win, uint32_t wireId) {
    if (!wireId) return -1;
    const auto* history = LinuxHistory(win);
    if (!history) return WindowAccessibilityNodeIndex(win, wireId);
    int count = std::min(len(history->wireIds), len(win->accessibility));
    for (int i = 0; i < count; i++) {
        if (history->wireIds[i] == wireId) return i;
    }
    return -1;
}

static LinuxAccessible AccessibleForPath(Str path) {
    LinuxAccessible result;
    if (StrIs(path, kRootPath)) {
        result.root = true;
        return result;
    }
    TempStr value = StrDupTemp(Str(path.s, std::min(len(path), 127)));
    int windowIndex = -1;
    unsigned nodeId = 0;
    int part = -1;
    int fields = sscanf(value.s, "/org/a11y/atspi/accessible/w%d/n%u/p%d",
                        &windowIndex, &nodeId, &part);
    if (fields < 2 || (fields == 3 && part < 0) || !gA11y.app ||
        windowIndex < 0 || windowIndex >= len(gA11y.app->windows)) {
        return result;
    }
    Window* win = gA11y.app->windows[windowIndex];
    if (!win || !win->plat) {
        return result;
    }
    int index = LinuxNodeIndexForWire(win, nodeId);
    if (index < 0 ||
        part >= WindowAccessibilityPartCount(win->accessibility[index]))
        return result;
    result.win = win;
    result.windowIndex = windowIndex;
    result.nodeIndex = index;
    result.part = part;
    return result;
}

static TempStr WirePathForTemp(int windowIndex, uint32_t wireId) {
    return fmt("/org/a11y/atspi/accessible/w%d/n%u", windowIndex, wireId);
}

static TempStr PathForTemp(int windowIndex, uint32_t nodeId) {
    Window* win = gA11y.app->windows[windowIndex];
    return WirePathForTemp(windowIndex, LinuxWireId(win, nodeId));
}

static TempStr PartPathForTemp(int windowIndex, uint32_t nodeId, int part) {
    Window* win = gA11y.app->windows[windowIndex];
    return fmt("/org/a11y/atspi/accessible/w%d/n%u/p%d", windowIndex,
               LinuxWireId(win, nodeId), part);
}

// The object's node when it is an element, null for the root and parts.
static const AccessibilityNode* ElementNode(const LinuxAccessible& object) {
    if (object.root || !object.win || object.nodeIndex < 0 || object.part >= 0)
        return nullptr;
    return &object.win->accessibility[object.nodeIndex];
}

static TempStr ElementPathForTemp(Window* win, int wi, int index) {
    const auto* history = LinuxHistory(win);
    uint32_t wireId = history && index < len(history->wireIds)
                          ? history->wireIds[index]
                          : win->accessibility[index].id;
    return WirePathForTemp(wi, wireId);
}

// The document node that owns a part.
static const AccessibilityNode& PartOwner(const LinuxAccessible& object) {
    return object.win->accessibility[object.nodeIndex];
}

static void PutObjectRef(DbusWriter* body, Str busName, Str path) {
    PutAlign(body, 8);
    PutString(body, busName);
    PutString(body, path);
}

static void PutObjectRef(DbusWriter* body, Str path) {
    PutObjectRef(body, gA11y.busName, path);
}

static void PutNullObjectRef(DbusWriter* body) {
    PutObjectRef(body, StrL(""), Str(kNullPath));
}

static int ChildCount(const LinuxAccessible& object) {
    if (object.root) {
        int n = 0;
        if (gA11y.app) {
            for (int wi = 0; wi < len(gA11y.app->windows); wi++) {
                Window* win = gA11y.app->windows[wi];
                if (!win || !win->plat) continue;
                for (int i = 0; i < len(win->accessibility); i++) {
                    n += win->accessibility[i].parent < 0 ? 1 : 0;
                }
            }
        }
        return n;
    }
    if (!object.win || object.nodeIndex < 0 || object.part >= 0) {
        return 0;
    }
    int n = 0;
    for (int i = 0; i < len(object.win->accessibility); i++) {
        n += object.win->accessibility[i].parent == object.nodeIndex ? 1 : 0;
    }
    return n;
}

// Element children, then a document's parts.
static int AllChildCount(const LinuxAccessible& object) {
    int n = ChildCount(object);
    if (const AccessibilityNode* node = ElementNode(object)) {
        n += WindowAccessibilityPartCount(*node);
    }
    return n;
}

static bool ChildAt(const LinuxAccessible& object, int wanted, int* windowIndex,
                    int* nodeIndex) {
    if (wanted < 0) {
        return false;
    }
    if (object.root) {
        if (!gA11y.app) return false;
        for (int wi = 0; wi < len(gA11y.app->windows); wi++) {
            Window* win = gA11y.app->windows[wi];
            if (!win || !win->plat) continue;
            for (int i = 0; i < len(win->accessibility); i++) {
                if (win->accessibility[i].parent < 0 && wanted-- == 0) {
                    *windowIndex = wi;
                    *nodeIndex = i;
                    return true;
                }
            }
        }
        return false;
    }
    for (int i = 0; ElementNode(object) && i < len(object.win->accessibility);
         i++) {
        if (object.win->accessibility[i].parent == object.nodeIndex &&
            wanted-- == 0) {
            *windowIndex = object.windowIndex;
            *nodeIndex = i;
            return true;
        }
    }
    return false;
}

// The path of child `wanted`, counting a document's parts after its element
// children; empty when there is none.
static Str ChildPathTemp(const LinuxAccessible& object, int wanted) {
    int wi = -1;
    int ni = -1;
    if (ChildAt(object, wanted, &wi, &ni)) {
        return PathForTemp(wi, gA11y.app->windows[wi]->accessibility[ni].id);
    }
    const AccessibilityNode* node = ElementNode(object);
    int part = wanted - ChildCount(object);
    if (!node || part < 0 || part >= WindowAccessibilityPartCount(*node)) {
        return {};
    }
    return PartPathForTemp(object.windowIndex, node->id, part);
}

static bool IsSelectionRole(AccessibilityRole role) {
    switch (role) {
        case AccessibilityRole::ComboBox:
        case AccessibilityRole::EditableComboBox:
        case AccessibilityRole::Grid:
        case AccessibilityRole::ListBox:
        case AccessibilityRole::ListGrid:
        case AccessibilityRole::Menu:
        case AccessibilityRole::MenuBar:
        case AccessibilityRole::MenuListPopup:
        case AccessibilityRole::RadioGroup:
        case AccessibilityRole::TabList:
        case AccessibilityRole::Toolbar:
        case AccessibilityRole::Tree:
        case AccessibilityRole::TreeGrid:
            return true;
        default:
            return false;
    }
}

// ATSPI_ROLE_PAGE, a document part's role, as for Evince's pages. UIA has no
// page control type, so Windows uses Custom.
static constexpr uint32_t kRolePage = 84;

static uint32_t AtspiRole(AccessibilityRole role) {
    switch (role) {
        case AccessibilityRole::Alert:
            return 2;
        case AccessibilityRole::Canvas:
            return 6;
        case AccessibilityRole::CheckBox:
        case AccessibilityRole::Switch:
            return 7;
        case AccessibilityRole::ColumnHeader:
            return 10;
        case AccessibilityRole::ComboBox:
        case AccessibilityRole::EditableComboBox:
            return 11;
        case AccessibilityRole::DateInput:
        case AccessibilityRole::DateTimeInput:
            return 12;
        case AccessibilityRole::Dialog:
        case AccessibilityRole::AlertDialog:
            return 16;
        case AccessibilityRole::Document:
            return 94; // ATSPI_ROLE_DOCUMENT_TEXT
        case AccessibilityRole::Image:
            return 27;
        case AccessibilityRole::Label:
        case AccessibilityRole::TextRun:
            return 29;
        case AccessibilityRole::List:
            return 31;
        case AccessibilityRole::ListItem:
        case AccessibilityRole::ListBoxOption:
            return 32;
        case AccessibilityRole::Menu:
            return 33;
        case AccessibilityRole::MenuBar:
            return 34;
        case AccessibilityRole::MenuItem:
            return 35;
        case AccessibilityRole::Tab:
            return 37;
        case AccessibilityRole::TabList:
            return 38;
        case AccessibilityRole::Pane:
        case AccessibilityRole::Group:
        case AccessibilityRole::RadioGroup:
        case AccessibilityRole::RowGroup:
            return 39;
        case AccessibilityRole::PasswordInput:
            return 40;
        case AccessibilityRole::ProgressIndicator:
            return 42;
        case AccessibilityRole::Button:
        case AccessibilityRole::DefaultButton:
            return 43;
        case AccessibilityRole::RadioButton:
            return 44;
        case AccessibilityRole::RowHeader:
            return 47;
        case AccessibilityRole::ScrollBar:
            return 48;
        case AccessibilityRole::ScrollView:
            return 49;
        case AccessibilityRole::Slider:
            return 51;
        case AccessibilityRole::SpinButton:
            return 52;
        case AccessibilityRole::Splitter:
            return 53;
        case AccessibilityRole::Status:
            return 54;
        case AccessibilityRole::Table:
        case AccessibilityRole::Grid:
            return 55;
        case AccessibilityRole::Cell:
        case AccessibilityRole::GridCell:
            return 56;
        case AccessibilityRole::Terminal:
            return 60;
        case AccessibilityRole::MultilineTextInput:
            return 61;
        case AccessibilityRole::Toolbar:
            return 63;
        case AccessibilityRole::Tooltip:
            return 64;
        case AccessibilityRole::Tree:
            return 65;
        case AccessibilityRole::TreeGrid:
            return 66;
        case AccessibilityRole::Window:
            return 69;
        case AccessibilityRole::Header:
            return 71;
        case AccessibilityRole::Footer:
            return 72;
        case AccessibilityRole::Paragraph:
            return 73;
        case AccessibilityRole::Application:
            return 75;
        case AccessibilityRole::TextInput:
        case AccessibilityRole::SearchInput:
        case AccessibilityRole::EmailInput:
        case AccessibilityRole::NumberInput:
        case AccessibilityRole::PhoneNumberInput:
        case AccessibilityRole::UrlInput:
            return 79;
        case AccessibilityRole::Heading:
            return 83;
        case AccessibilityRole::Section:
        case AccessibilityRole::Region:
            return 85;
        case AccessibilityRole::Link:
            return 88;
        case AccessibilityRole::Row:
            return 90;
        case AccessibilityRole::TreeItem:
            return 91;
        case AccessibilityRole::Comment:
            return 97;
        case AccessibilityRole::ListBox:
            return 98;
        case AccessibilityRole::TitleBar:
            return 104;
        case AccessibilityRole::Blockquote:
            return 105;
        case AccessibilityRole::Audio:
            return 106;
        case AccessibilityRole::Video:
            return 107;
        case AccessibilityRole::Definition:
            return 108;
        case AccessibilityRole::Article:
            return 109;
        default:
            return 67;
    }
}

static const char* AtspiRoleName(uint32_t role) {
    switch (role) {
        case 7:
            return "check box";
        case 10:
            return "column header";
        case 11:
            return "combo box";
        case 16:
            return "dialog";
        case 27:
            return "image";
        case 29:
            return "label";
        case 31:
            return "list";
        case 32:
            return "list item";
        case 35:
            return "menu item";
        case 37:
            return "page tab";
        case 38:
            return "page tab list";
        case 40:
            return "password text";
        case 42:
            return "progress bar";
        case 43:
            return "push button";
        case 44:
            return "radio button";
        case 51:
            return "slider";
        case 52:
            return "spin button";
        case 55:
            return "table";
        case 56:
            return "table cell";
        case 61:
            return "text";
        case 65:
            return "tree";
        case 69:
            return "window";
        case 73:
            return "paragraph";
        case 75:
            return "application";
        case 79:
            return "entry";
        case 83:
            return "heading";
        case kRolePage:
            return "page";
        case 88:
            return "link";
        case 90:
            return "table row";
        case 91:
            return "tree item";
        case 94:
            return "document text";
        case 98:
            return "list box";
        default:
            return "unknown";
    }
}

static int ActionCount(const AccessibilityNode& node) {
    int n = 0;
    n += (node.actions & AccessibilityActionDefault) ? 1 : 0;
    n += (node.actions & AccessibilityActionIncrement) ? 1 : 0;
    n += (node.actions & AccessibilityActionDecrement) ? 1 : 0;
    return n;
}

static AccessibilityAction ActionAt(const AccessibilityNode& node, int index,
                                    const char** name) {
    struct Entry {
        uint8_t bit;
        AccessibilityAction action;
        const char* name;
    } entries[] = {
        {AccessibilityActionDefault, AccessibilityAction::Default, "click"},
        {AccessibilityActionIncrement, AccessibilityAction::Increment,
         "increment"},
        {AccessibilityActionDecrement, AccessibilityAction::Decrement,
         "decrement"},
    };
    for (const Entry& entry : entries) {
        if (node.actions & entry.bit) {
            if (index-- == 0) {
                *name = entry.name;
                return entry.action;
            }
        }
    }
    *name = "";
    return AccessibilityAction::Default;
}

struct Incoming {
    uint8_t type = 0;
    uint32_t serial = 0;
    uint32_t replySerial = 0;
    Str path = {};
    Str interfaceName = {};
    Str member = {};
    Str sender = {};
    Str signature = {};
    const uint8_t* body = nullptr;
    int bodyLen = 0;
};

static void ParseHeaderField(Incoming* in, uint8_t code, char signature,
                             const uint8_t* data, int size, int* at) {
    if (signature == 'u') {
        uint32_t value = ReadU32(data, size, at);
        if (code == 5) in->replySerial = value;
        return;
    }
    Str value = signature == 'g' ? ReadSignature(data, size, at)
                                 : ReadString(data, size, at);
    if (code == 1)
        in->path = value;
    else if (code == 2)
        in->interfaceName = value;
    else if (code == 3)
        in->member = value;
    else if (code == 7)
        in->sender = value;
    else if (code == 8)
        in->signature = value;
}

static bool ParseIncoming(const uint8_t* data, int size, Incoming* in,
                          int* messageLen) {
    if (size < 16 || data[0] != 'l' || data[3] != 1) {
        return false;
    }
    int at = 4;
    uint32_t bodyLen = ReadU32(data, size, &at);
    in->serial = ReadU32(data, size, &at);
    uint32_t fieldsLen = ReadU32(data, size, &at);
    int bodyAt = AlignAt(16 + (int)fieldsLen, 8);
    uint64_t total = (uint64_t)bodyAt + bodyLen;
    if (total > (uint64_t)size) {
        return false;
    }
    in->type = data[1];
    int fieldAt = 16;
    int fieldEnd = 16 + (int)fieldsLen;
    while (fieldAt < fieldEnd) {
        fieldAt = AlignAt(fieldAt, 8);
        if (fieldAt + 4 > fieldEnd) break;
        uint8_t code = data[fieldAt++];
        uint8_t sigLen = data[fieldAt++];
        if (sigLen != 1 || fieldAt + sigLen + 1 > fieldEnd) break;
        char signature = (char)data[fieldAt];
        fieldAt += sigLen + 1;
        ParseHeaderField(in, code, signature, data, fieldEnd, &fieldAt);
    }
    in->body = data + bodyAt;
    in->bodyLen = (int)bodyLen;
    *messageLen = (int)total;
    return true;
}

enum LinuxOptionalInterface : uint8_t {
    kLinuxAction = 1 << 0,
    kLinuxValue = 1 << 1,
    kLinuxSelection = 1 << 2,
    kLinuxText = 1 << 3,
    kLinuxEditable = 1 << 4,
};

static uint8_t LinuxNodeInterfaces(const AccessibilityNode& node) {
    uint8_t interfaces = 0;
    if (ActionCount(node)) interfaces |= kLinuxAction;
    if (node.info.hasNumericValue) interfaces |= kLinuxValue;
    if (IsSelectionRole(node.info.role)) interfaces |= kLinuxSelection;
    if (WindowAccessibilityHasText(node)) interfaces |= kLinuxText;
    if (node.input && (node.actions & AccessibilityActionSetValue))
        interfaces |= kLinuxEditable;
    return interfaces;
}

static void PutInterfaces(DbusWriter* body, const LinuxAccessible& object,
                          uint8_t interfaces) {
    int lengthAt = 0;
    int contentsAt = 0;
    ArrayBegin(body, 4, &lengthAt, &contentsAt);
    PutString(body, kAccessible);
    if (object.root) {
        PutString(body, kApplication);
    } else if (object.part >= 0) {
        PutString(body, kComponent);
        PutString(body, kText);
    } else {
        PutString(body, kComponent);
        if (interfaces & kLinuxAction) PutString(body, kAction);
        if (interfaces & kLinuxValue) PutString(body, kValue);
        if (interfaces & kLinuxSelection) PutString(body, kSelection);
        if (interfaces & kLinuxText) PutString(body, kText);
        if (interfaces & kLinuxEditable) PutString(body, kEditableText);
    }
    ArrayEnd(body, lengthAt, contentsAt);
}

enum AtspiStateBit {
    kAtspiChecked = 4,
    kAtspiCollapsed = 5,
    kAtspiEditable = 7,
    kAtspiEnabled = 8,
    kAtspiExpandable = 9,
    kAtspiExpanded = 10,
    kAtspiFocusable = 11,
    kAtspiFocused = 12,
    kAtspiHorizontal = 14,
    kAtspiMultiLine = 17,
    kAtspiSelectable = 22,
    kAtspiSelected = 23,
    kAtspiSensitive = 24,
    kAtspiShowing = 25,
    kAtspiSingleLine = 26,
    kAtspiVertical = 29,
    kAtspiVisible = 30,
    kAtspiIndeterminate = 32,
    kAtspiSelectableText = 38,
    kAtspiDefault = 39,
    kAtspiCheckable = 41,
    kAtspiReadOnly = 43,
};

static constexpr uint64_t AtspiBit(AtspiStateBit bit) {
    return uint64_t{1} << bit;
}

static uint64_t LinuxNodeStates(Window* win, const AccessibilityNode& node) {
    uint64_t states = AtspiBit(kAtspiVisible) | AtspiBit(kAtspiShowing);
    if (!node.info.disabled)
        states |= AtspiBit(kAtspiEnabled) | AtspiBit(kAtspiSensitive);
    if (node.actions & AccessibilityActionFocus)
        states |= AtspiBit(kAtspiFocusable);
    if (node.focusId && node.focusId == win->focusId &&
        AccessibilityLinuxWindowFocused(win))
        states |= AtspiBit(kAtspiFocused);
    if (node.info.toggled != AccessibilityToggled::Unset)
        states |= AtspiBit(kAtspiCheckable);
    if (node.info.toggled == AccessibilityToggled::True)
        states |= AtspiBit(kAtspiChecked);
    if (node.info.toggled == AccessibilityToggled::Mixed)
        states |= AtspiBit(kAtspiIndeterminate);
    if (node.info.hasExpanded) {
        states |=
            AtspiBit(kAtspiExpandable) |
            AtspiBit(node.info.expanded ? kAtspiExpanded : kAtspiCollapsed);
    }
    if (node.info.hasSelected) {
        states |= AtspiBit(kAtspiSelectable);
        if (node.info.selected) states |= AtspiBit(kAtspiSelected);
    }
    if (node.input) {
        states |= AtspiBit(kAtspiSelectableText) |
                  AtspiBit(InputIsMultiLine(node.input) ? kAtspiMultiLine
                                                        : kAtspiSingleLine) |
                  AtspiBit(InputIsEditable(node.input) ? kAtspiEditable
                                                       : kAtspiReadOnly);
    }
    if (node.info.orientation == AccessibilityOrientation::Horizontal)
        states |= AtspiBit(kAtspiHorizontal);
    if (node.info.orientation == AccessibilityOrientation::Vertical)
        states |= AtspiBit(kAtspiVertical);
    if (node.info.role == AccessibilityRole::DefaultButton)
        states |= AtspiBit(kAtspiDefault);
    return states;
}

// A part's bounds in window coordinates; false when the source has none.
static bool PartBounds(const LinuxAccessible& object, Bounds* out) {
    return WindowAccessibilityPartBounds(PartOwner(object), object.part, out);
}

// Whether a part is on its document's visible area, as UIA's IsOffscreen.
static bool PartShowing(const LinuxAccessible& object) {
    Bounds b;
    if (!PartBounds(object, &b)) return false;
    const Bounds& doc = PartOwner(object).bounds;
    return b.x < doc.Right() && b.Right() > doc.x && b.y < doc.Bottom() &&
           b.Bottom() > doc.y;
}

static void PutStates(DbusWriter* body, const LinuxAccessible& object) {
    uint64_t state = AtspiBit(kAtspiVisible);
    if (object.root) {
        state |= AtspiBit(kAtspiEnabled) | AtspiBit(kAtspiShowing);
    } else if (object.part >= 0) {
        state |= AtspiBit(kAtspiEnabled) | AtspiBit(kAtspiSensitive) |
                 AtspiBit(kAtspiReadOnly);
        if (PartShowing(object)) state |= AtspiBit(kAtspiShowing);
    } else if (const auto* node = ElementNode(object)) {
        state = LinuxNodeStates(object.win, *node);
    }
    int lengthAt = 0;
    int contentsAt = 0;
    ArrayBegin(body, 4, &lengthAt, &contentsAt);
    PutU32(body, (uint32_t)state);
    PutU32(body, (uint32_t)(state >> 32));
    ArrayEnd(body, lengthAt, contentsAt);
}

static void PutEmptyStringDict(DbusWriter* body) {
    int lengthAt = 0;
    int contentsAt = 0;
    ArrayBegin(body, 8, &lengthAt, &contentsAt);
    ArrayEnd(body, lengthAt, contentsAt);
}

static void PutEmptyRelations(DbusWriter* body) {
    int lengthAt = 0;
    int contentsAt = 0;
    ArrayBegin(body, 8, &lengthAt, &contentsAt);
    ArrayEnd(body, lengthAt, contentsAt);
}

static void PutParent(DbusWriter* body, const LinuxAccessible& object) {
    if (object.root) {
        if (gA11y.desktopPath.s) {
            PutObjectRef(body, gA11y.desktopBusName, gA11y.desktopPath);
        } else {
            PutNullObjectRef(body);
        }
        return;
    }
    const AccessibilityNode& node = object.win->accessibility[object.nodeIndex];
    if (object.part >= 0) {
        PutObjectRef(body, PathForTemp(object.windowIndex, node.id));
    } else if (node.parent < 0) {
        PutObjectRef(body, Str(kRootPath));
    } else {
        PutObjectRef(body, ElementPathForTemp(object.win, object.windowIndex,
                                              node.parent));
    }
}

static int IndexInParent(const LinuxAccessible& object) {
    if (object.root || !object.win || object.nodeIndex < 0) return -1;
    if (object.part >= 0) {
        // after the document's element children
        LinuxAccessible owner = object;
        owner.part = -1;
        return ChildCount(owner) + object.part;
    }
    int parent = object.win->accessibility[object.nodeIndex].parent;
    int index = 0;
    if (parent < 0) {
        for (int wi = 0; gA11y.app && wi < len(gA11y.app->windows); wi++) {
            Window* win = gA11y.app->windows[wi];
            if (!win || !win->plat) continue;
            for (int i = 0; i < len(win->accessibility); i++) {
                if (win->accessibility[i].parent < 0) {
                    if (win == object.win && i == object.nodeIndex)
                        return index;
                    index++;
                }
            }
        }
        return -1;
    }
    for (int i = 0; i < len(object.win->accessibility); i++) {
        if (object.win->accessibility[i].parent == parent) {
            if (i == object.nodeIndex) return index;
            index++;
        }
    }
    return -1;
}

static Str ObjectName(const LinuxAccessible& object) {
    if (object.root) return Str("gpui application");
    if (object.part >= 0) {
        return WindowAccessibilityPartName(PartOwner(object), object.part);
    }
    return object.win->accessibility[object.nodeIndex].info.PlatformName();
}

static Str LinuxNodeDescription(const AccessibilityInfo& info) {
    return info.description.s ? info.description : info.placeholder;
}

// The Text interface's text: a part's own text or the node's.
static Str ObjectText(const LinuxAccessible& object) {
    if (object.part >= 0) {
        return WindowAccessibilityPartText(PartOwner(object), object.part);
    }
    const AccessibilityNode* node = ElementNode(object);
    return node ? WindowAccessibilityTextValue(*node) : Str{};
}

static int Utf8CharacterForByte(Str text, int byte);
static int SelectedCount(const LinuxAccessible& object);

static bool PutPropertyVariant(DbusWriter* body, const LinuxAccessible& object,
                               Str interfaceName, Str property) {
    const AccessibilityNode* node = ElementNode(object);
    if (StrIs(interfaceName, kAccessible)) {
        if (StrIs(property, "version")) {
            PutVariantU32(body, 1);
        } else if (StrIs(property, "Name")) {
            PutVariantString(body, "s", ObjectName(object));
        } else if (StrIs(property, "Description")) {
            // AccessKit's description; a node without one keeps offering its
            // placeholder here, as it did before it could have one.
            Str description = node ? LinuxNodeDescription(node->info) : Str{};
            PutVariantString(body, "s", description);
        } else if (StrIs(property, "HelpText")) {
            PutVariantString(body, "s", node ? node->info.placeholder : Str{});
        } else if (StrIs(property, "AccessibleId")) {
            PutVariantString(body, "s", node ? node->info.authorId : Str{});
        } else if (StrIs(property, "Locale")) {
            const char* locale = setlocale(LC_MESSAGES, nullptr);
            PutVariantString(body, "s", Str(locale ? locale : ""));
        } else if (StrIs(property, "ChildCount")) {
            PutVariantI32(body, AllChildCount(object));
        } else if (StrIs(property, "Parent")) {
            PutSignature(body, "(so)");
            PutParent(body, object);
        } else {
            return false;
        }
    } else if (StrIs(interfaceName, kApplication)) {
        if (StrIs(property, "Id"))
            PutVariantI32(body, gA11y.applicationId);
        else if (StrIs(property, "ToolkitName"))
            PutVariantString(body, "s", Str("gpui-cpp"));
        else if (StrIs(property, "Version") ||
                 StrIs(property, "ToolkitVersion"))
            PutVariantString(body, "s", Str("1"));
        else if (StrIs(property, "AtspiVersion"))
            PutVariantString(body, "s", Str("2.1"));
        else if (StrIs(property, "InterfaceVersion"))
            PutVariantU32(body, 1);
        else
            return false;
    } else if (StrIs(interfaceName, kComponent)) {
        if (!StrIs(property, "version")) return false;
        PutVariantU32(body, 1);
    } else if (StrIs(interfaceName, kAction) && node) {
        if (StrIs(property, "version"))
            PutVariantU32(body, 1);
        else if (StrIs(property, "NActions"))
            PutVariantI32(body, ActionCount(*node));
        else
            return false;
    } else if (StrIs(interfaceName, kValue) && node) {
        if (StrIs(property, "version")) {
            PutVariantU32(body, 1);
            return true;
        }
        double value = node->info.hasNumericValue ? node->info.numericValue : 0;
        if (StrIs(property, "MinimumValue"))
            value = node->info.minNumericValue;
        else if (StrIs(property, "MaximumValue"))
            value = node->info.maxNumericValue;
        else if (StrIs(property, "MinimumIncrement"))
            value = node->info.numericValueStep;
        else if (StrIs(property, "Text")) {
            PutVariantString(body, "s", node->info.value);
            return true;
        } else if (!StrIs(property, "CurrentValue")) {
            return false;
        }
        PutVariantDouble(body, value);
    } else if (StrIs(interfaceName, kSelection) && node &&
               IsSelectionRole(node->info.role)) {
        if (StrIs(property, "version")) {
            PutVariantU32(body, 1);
        } else if (StrIs(property, "NSelectedChildren")) {
            PutVariantI32(body, SelectedCount(object));
        } else {
            return false;
        }
    } else if (StrIs(interfaceName, kText) && (node || object.part >= 0)) {
        Str text = ObjectText(object);
        if (!text.s) return false;
        if (StrIs(property, "version"))
            PutVariantU32(body, 1);
        else if (StrIs(property, "CharacterCount"))
            PutVariantI32(body, Utf8CharCount(text));
        else if (StrIs(property, "CaretOffset"))
            PutVariantI32(body,
                          node ? Utf8CharacterForByte(
                                     text, WindowAccessibilityTextCaret(*node))
                               : 0);
        else
            return false;
    } else if (StrIs(interfaceName, kEditableText) && node && node->input) {
        if (!StrIs(property, "version")) return false;
        PutVariantU32(body, 1);
    } else {
        return false;
    }
    return true;
}

static void ReplyProperty(const Incoming& in, const LinuxAccessible& object,
                          Str interfaceName, Str property) {
    DbusWriter body;
    if (!PutPropertyVariant(&body, object, interfaceName, property)) {
        VecReset(body.bytes);
        SendError(in.serial, in.sender,
                  "org.freedesktop.DBus.Error.InvalidArgs",
                  "unknown AT-SPI property");
        return;
    }
    SendReply(in.serial, in.sender, "v", &body);
    VecReset(body.bytes);
}

static void PutPropertyEntry(DbusWriter* body, const LinuxAccessible& object,
                             Str interfaceName, const char* property) {
    PutAlign(body, 8);
    PutString(body, property);
    bool ok = PutPropertyVariant(body, object, interfaceName, Str(property));
    (void)ok;
}

static bool PutAllProperties(DbusWriter* body, const LinuxAccessible& object,
                             Str interfaceName) {
    const AccessibilityNode* node = ElementNode(object);
    int lengthAt = 0;
    int contentsAt = 0;
    ArrayBegin(body, 8, &lengthAt, &contentsAt);
#define GPUI_ATSPI_PROPERTY(name) \
    PutPropertyEntry(body, object, interfaceName, name)
    if (StrIs(interfaceName, kAccessible)) {
        GPUI_ATSPI_PROPERTY("version");
        GPUI_ATSPI_PROPERTY("Name");
        GPUI_ATSPI_PROPERTY("Description");
        GPUI_ATSPI_PROPERTY("Parent");
        GPUI_ATSPI_PROPERTY("ChildCount");
        GPUI_ATSPI_PROPERTY("Locale");
        GPUI_ATSPI_PROPERTY("AccessibleId");
        GPUI_ATSPI_PROPERTY("HelpText");
    } else if (StrIs(interfaceName, kApplication) && object.root) {
        GPUI_ATSPI_PROPERTY("ToolkitName");
        GPUI_ATSPI_PROPERTY("Version");
        GPUI_ATSPI_PROPERTY("ToolkitVersion");
        GPUI_ATSPI_PROPERTY("AtspiVersion");
        GPUI_ATSPI_PROPERTY("InterfaceVersion");
        GPUI_ATSPI_PROPERTY("Id");
    } else if (StrIs(interfaceName, kComponent) && object.nodeIndex >= 0) {
        GPUI_ATSPI_PROPERTY("version");
    } else if (StrIs(interfaceName, kAction) && node && ActionCount(*node)) {
        GPUI_ATSPI_PROPERTY("version");
        GPUI_ATSPI_PROPERTY("NActions");
    } else if (StrIs(interfaceName, kValue) && node &&
               node->info.hasNumericValue) {
        GPUI_ATSPI_PROPERTY("version");
        GPUI_ATSPI_PROPERTY("MinimumValue");
        GPUI_ATSPI_PROPERTY("MaximumValue");
        GPUI_ATSPI_PROPERTY("MinimumIncrement");
        GPUI_ATSPI_PROPERTY("CurrentValue");
        GPUI_ATSPI_PROPERTY("Text");
    } else if (StrIs(interfaceName, kSelection) && node &&
               IsSelectionRole(node->info.role)) {
        GPUI_ATSPI_PROPERTY("version");
        GPUI_ATSPI_PROPERTY("NSelectedChildren");
    } else if (StrIs(interfaceName, kText) && object.nodeIndex >= 0 &&
               ObjectText(object).s) {
        GPUI_ATSPI_PROPERTY("version");
        GPUI_ATSPI_PROPERTY("CharacterCount");
        GPUI_ATSPI_PROPERTY("CaretOffset");
    } else if (StrIs(interfaceName, kEditableText) && node && node->input &&
               (node->actions & AccessibilityActionSetValue)) {
        GPUI_ATSPI_PROPERTY("version");
    } else {
        VecReset(body->bytes);
        return false;
    }
#undef GPUI_ATSPI_PROPERTY
    ArrayEnd(body, lengthAt, contentsAt);
    return true;
}

static bool HandleProperties(const Incoming& in,
                             const LinuxAccessible& object) {
    if (!StrIs(in.interfaceName, "org.freedesktop.DBus.Properties")) {
        return false;
    }
    int at = 0;
    if (StrIs(in.member, "Get")) {
        Str interfaceName = ReadString(in.body, in.bodyLen, &at);
        Str property = ReadString(in.body, in.bodyLen, &at);
        ReplyProperty(in, object, interfaceName, property);
    } else if (StrIs(in.member, "Set")) {
        Str interfaceName = ReadString(in.body, in.bodyLen, &at);
        Str property = ReadString(in.body, in.bodyLen, &at);
        Str valueSignature = ReadSignature(in.body, in.bodyLen, &at);
        if (object.root && StrIs(interfaceName, kApplication) &&
            StrIs(property, "Id") && StrIs(valueSignature, "i")) {
            gA11y.applicationId = ReadI32(in.body, in.bodyLen, &at);
            SendReply(in.serial, in.sender, "", nullptr);
        } else {
            SendError(in.serial, in.sender,
                      "org.freedesktop.DBus.Error.PropertyReadOnly",
                      "AT-SPI property is not writable");
        }
    } else if (StrIs(in.member, "GetAll")) {
        Str interfaceName = ReadString(in.body, in.bodyLen, &at);
        DbusWriter body;
        if (PutAllProperties(&body, object, interfaceName)) {
            SendReply(in.serial, in.sender, "a{sv}", &body);
        } else {
            SendError(in.serial, in.sender,
                      "org.freedesktop.DBus.Error.InvalidArgs",
                      "unknown AT-SPI property interface");
        }
        VecReset(body.bytes);
    } else {
        SendError(in.serial, in.sender,
                  "org.freedesktop.DBus.Error.UnknownMethod",
                  "unknown properties method");
    }
    return true;
}

// A method a handler answered: its reply goes back to the caller and the body
// is freed. True, which is what the handler returns.
static bool SendHandledReply(const Incoming& in, const char* signature,
                             DbusWriter* body) {
    SendReply(in.serial, in.sender, signature, body);
    VecReset(body->bytes);
    return true;
}

static void PutCacheItem(DbusWriter* body, const LinuxAccessible& object,
                         int index, int children, uint8_t interfaces) {
    Str path = object.root
                   ? Str(kRootPath)
                   : (Str)ElementPathForTemp(object.win, object.windowIndex,
                                             object.nodeIndex);
    const auto* node = ElementNode(object);
    PutObjectRef(body, path);
    PutObjectRef(body, Str(kRootPath));
    PutParent(body, object);
    PutI32(body, index);
    // Keep synthetic document pages on demand. A negative count tells
    // clients to query the document's children through Accessible.
    PutI32(body, node && node->document.partCount ? -1 : children);
    PutInterfaces(body, object, interfaces);
    PutString(body, ObjectName(object));
    PutU32(body, node ? AtspiRole(node->info.role) : 75);
    PutString(body, node ? LinuxNodeDescription(node->info) : Str{});
    PutStates(body, object);
}

static bool HandleLinuxCache(const Incoming& in) {
    DbusWriter body;
    if (StrIs(in.interfaceName, kCache) && StrIs(in.member, "GetItems")) {
        int lengthAt = 0;
        int contentsAt = 0;
        ArrayBegin(&body, 8, &lengthAt, &contentsAt);
        int rootChildren = 0;
        if (gA11y.app) {
            for (auto* win : gA11y.app->windows) {
                if (!win || !win->plat || !LinuxHistory(win)) continue;
                for (const auto& node : win->accessibility)
                    rootChildren += node.parent < 0;
            }
        }
        LinuxAccessible root;
        root.root = true;
        PutCacheItem(&body, root, -1, rootChildren, 0);
        Vec<int> childCounts;
        Vec<int> siblingIndices;
        int rootIndex = 0;
        for (int wi = 0; gA11y.app && wi < len(gA11y.app->windows); wi++) {
            Window* win = gA11y.app->windows[wi];
            if (!win || !win->plat || !LinuxHistory(win)) continue;
            int count = len(win->accessibility);
            if (!VecResize(childCounts, count) ||
                !VecResize(siblingIndices, count)) {
                SendError(in.serial, in.sender,
                          "org.freedesktop.DBus.Error.NoMemory",
                          "could not gather the accessibility tree");
                return true;
            }
            if (count) memset(childCounts.els, 0, count * sizeof(int));
            for (int i = 0; i < count; i++) {
                int parent = win->accessibility[i].parent;
                siblingIndices[i] =
                    parent < 0 ? rootIndex++ : childCounts[parent]++;
            }
            for (int i = 0; i < count; i++) {
                LinuxAccessible object{win, wi, i};
                PutCacheItem(&body, object, siblingIndices[i], childCounts[i],
                             LinuxNodeInterfaces(win->accessibility[i]));
            }
        }
        ArrayEnd(&body, lengthAt, contentsAt);
        return SendHandledReply(in, "a" GPUI_ATSPI_CACHE_ITEM, &body);
    }
    if (StrIs(in.interfaceName, "org.freedesktop.DBus.Introspectable") &&
        StrIs(in.member, "Introspect")) {
        PutString(
            &body,
            "<node><interface name='org.a11y.atspi.Cache'>"
            "<method name='GetItems'><arg type='a" GPUI_ATSPI_CACHE_ITEM
            "' direction='out'/></method>"
            "<signal name='AddAccessible'><arg type='" GPUI_ATSPI_CACHE_ITEM
            "'/></signal>"
            "<signal name='RemoveAccessible'><arg type='(so)'/></signal>"
            "</interface></node>");
        return SendHandledReply(in, "s", &body);
    }
    return false;
}

static bool HandleAccessible(const Incoming& in,
                             const LinuxAccessible& object) {
    if (!StrIs(in.interfaceName, kAccessible)) return false;
    DbusWriter body;
    const AccessibilityNode* node = object.win && object.nodeIndex >= 0
                                        ? &object.win
                                               ->accessibility[object.nodeIndex]
                                        : nullptr;
    const char* signature = "";
    uint32_t role = object.root        ? 75
                    : object.part >= 0 ? kRolePage
                                       : AtspiRole(node->info.role);
    if (StrIs(in.member, "GetChildAtIndex")) {
        int at = 0;
        int wanted = (int)ReadU32(in.body, in.bodyLen, &at);
        Str path = ChildPathTemp(object, wanted);
        if (path.s) {
            PutObjectRef(&body, path);
        } else {
            PutNullObjectRef(&body);
        }
        signature = "(so)";
    } else if (StrIs(in.member, "GetChildren")) {
        int lengthAt = 0;
        int contentsAt = 0;
        ArrayBegin(&body, 8, &lengthAt, &contentsAt);
        int n = AllChildCount(object);
        for (int i = 0; i < n; i++) {
            Str path = ChildPathTemp(object, i);
            if (path.s) PutObjectRef(&body, path);
        }
        ArrayEnd(&body, lengthAt, contentsAt);
        signature = "a(so)";
    } else if (StrIs(in.member, "GetIndexInParent")) {
        PutI32(&body, IndexInParent(object));
        signature = "i";
    } else if (StrIs(in.member, "GetRole")) {
        PutU32(&body, role);
        signature = "u";
    } else if (StrIs(in.member, "GetRoleName") ||
               StrIs(in.member, "GetLocalizedRoleName")) {
        PutString(&body, AtspiRoleName(role));
        signature = "s";
    } else if (StrIs(in.member, "GetState")) {
        PutStates(&body, object);
        signature = "au";
    } else if (StrIs(in.member, "GetInterfaces")) {
        PutInterfaces(&body, object, node ? LinuxNodeInterfaces(*node) : 0);
        signature = "as";
    } else if (StrIs(in.member, "GetAttributes")) {
        PutEmptyStringDict(&body);
        signature = "a{ss}";
    } else if (StrIs(in.member, "GetAttributesAsArray")) {
        int lengthAt = 0;
        int contentsAt = 0;
        ArrayBegin(&body, 4, &lengthAt, &contentsAt);
        ArrayEnd(&body, lengthAt, contentsAt);
        signature = "as";
    } else if (StrIs(in.member, "GetRelationSet")) {
        PutEmptyRelations(&body);
        signature = "a(ua(so))";
    } else if (StrIs(in.member, "GetApplication")) {
        PutObjectRef(&body, Str(kRootPath));
        signature = "(so)";
    } else {
        VecReset(body.bytes);
        return false;
    }
    return SendHandledReply(in, signature, &body);
}

static bool HandleApplication(const Incoming& in,
                              const LinuxAccessible& object) {
    if (!StrIs(in.interfaceName, kApplication) || !object.root) return false;
    DbusWriter body;
    if (StrIs(in.member, "GetLocale")) {
        int at = 0;
        (void)ReadU32(in.body, in.bodyLen, &at);
        const char* locale = setlocale(LC_MESSAGES, nullptr);
        PutString(&body, locale ? locale : "");
        SendReply(in.serial, in.sender, "s", &body);
    } else if (StrIs(in.member, "GetApplicationBusAddress")) {
        // The application serves requests on its registered bus name. Only
        // applications listening on a separate peer socket advertise one.
        PutString(&body, "");
        SendReply(in.serial, in.sender, "s", &body);
    } else {
        VecReset(body.bytes);
        return false;
    }
    VecReset(body.bytes);
    return true;
}

static bool NodeIsInSubtree(const Window* win, int index, int root) {
    if (root < 0) return true;
    for (int at = index; at >= 0 && at < len(win->accessibility);
         at = win->accessibility[at].parent) {
        if (at == root) return true;
    }
    return false;
}

static int NodeAtPoint(Window* win, int root, int x, int y) {
    int found = -1;
    for (int i = 0; win && i < len(win->accessibility); i++) {
        const Bounds& b = win->accessibility[i].bounds;
        if (NodeIsInSubtree(win, i, root) && x >= b.x && x <= b.Right() &&
            y >= b.y && y <= b.Bottom()) {
            found = i;
        }
    }
    return found;
}

static Point ComponentCoordOrigin(const LinuxAccessible& object,
                                  uint32_t coordType) {
    Point origin = {};
    if (!object.win) return origin;
    if (coordType == 0) {
        return AccessibilityLinuxWindowOrigin(object.win);
    }
    if (coordType == 2 && object.part >= 0) {
        origin.x = PartOwner(object).bounds.x;
        origin.y = PartOwner(object).bounds.y;
    } else if (coordType == 2 && object.nodeIndex >= 0) {
        int parent = object.win->accessibility[object.nodeIndex].parent;
        if (parent >= 0 && parent < len(object.win->accessibility)) {
            origin.x = object.win->accessibility[parent].bounds.x;
            origin.y = object.win->accessibility[parent].bounds.y;
        }
    }
    return origin;
}

static Bounds BoundsInComponentCoordinates(const LinuxAccessible& object,
                                           Bounds bounds, uint32_t coordType) {
    Point origin = ComponentCoordOrigin(object, coordType);
    if (coordType == 2) {
        bounds.x -= origin.x;
        bounds.y -= origin.y;
    } else {
        bounds.x += origin.x;
        bounds.y += origin.y;
    }
    return bounds;
}

static Bounds ComponentBounds(const LinuxAccessible& object,
                              uint32_t coordType) {
    if (!object.win || object.nodeIndex < 0) return {};
    Bounds bounds = object.win->accessibility[object.nodeIndex].bounds;
    if (object.part >= 0 && !PartBounds(object, &bounds)) return {};
    return BoundsInComponentCoordinates(object, bounds, coordType);
}

// The accessible at a window point under node `found`: a document's part
// when no element child is there, like Windows' ElementProviderFromPoint.
static void PutHitRef(DbusWriter* body, const LinuxAccessible& object,
                      int found, Point point) {
    if (found < 0) {
        PutNullObjectRef(body);
        return;
    }
    const AccessibilityNode& node = object.win->accessibility[found];
    int parts = WindowAccessibilityPartCount(node);
    for (int part = 0; part < parts; part++) {
        Bounds b;
        if (WindowAccessibilityPartBounds(node, part, &b) && point.x >= b.x &&
            point.x <= b.Right() && point.y >= b.y && point.y <= b.Bottom()) {
            PutObjectRef(body,
                         PartPathForTemp(object.windowIndex, node.id, part));
            return;
        }
    }
    PutObjectRef(body, PathForTemp(object.windowIndex, node.id));
}

static Point ComponentPointInWindow(const LinuxAccessible& object, int x, int y,
                                    uint32_t coordType) {
    Point point = {(float)x, (float)y};
    Point origin = ComponentCoordOrigin(object, coordType);
    if (coordType == 2) {
        point.x += origin.x;
        point.y += origin.y;
    } else {
        point.x -= origin.x;
        point.y -= origin.y;
    }
    return point;
}

static bool HandleComponent(const Incoming& in, const LinuxAccessible& object) {
    if (!StrIs(in.interfaceName, kComponent)) return false;
    DbusWriter body;
    int at = 0;
    const char* signature = "";
    if (StrIs(in.member, "Contains")) {
        int x = ReadI32(in.body, in.bodyLen, &at);
        int y = ReadI32(in.body, in.bodyLen, &at);
        uint32_t coordType = ReadU32(in.body, in.bodyLen, &at);
        Bounds bounds = ComponentBounds(object, coordType);
        PutBool(&body, x >= bounds.x && x <= bounds.Right() && y >= bounds.y &&
                           y <= bounds.Bottom());
        signature = "b";
    } else if (StrIs(in.member, "GetAccessibleAtPoint")) {
        int x = ReadI32(in.body, in.bodyLen, &at);
        int y = ReadI32(in.body, in.bodyLen, &at);
        uint32_t coordType = ReadU32(in.body, in.bodyLen, &at);
        Point point = ComponentPointInWindow(object, x, y, coordType);
        int found = object.part >= 0 ? -1
                                     : NodeAtPoint(object.win, object.nodeIndex,
                                                   (int)point.x, (int)point.y);
        PutHitRef(&body, object, found, point);
        signature = "(so)";
    } else if (StrIs(in.member, "GetExtents")) {
        uint32_t coordType = ReadU32(in.body, in.bodyLen, &at);
        Bounds bounds = ComponentBounds(object, coordType);
        signature = PutExtents(&body, bounds);
    } else if (StrIs(in.member, "GetPosition")) {
        uint32_t coordType = ReadU32(in.body, in.bodyLen, &at);
        Bounds bounds = ComponentBounds(object, coordType);
        PutAlign(&body, 8);
        PutI32(&body, (int)bounds.x);
        PutI32(&body, (int)bounds.y);
        signature = "(ii)";
    } else if (StrIs(in.member, "GetSize")) {
        Bounds bounds = ComponentBounds(object, 1);
        PutAlign(&body, 8);
        PutI32(&body, (int)bounds.w);
        PutI32(&body, (int)bounds.h);
        signature = "(ii)";
    } else if (StrIs(in.member, "GetLayer")) {
        PutU32(&body, 2); // widget layer
        signature = "u";
    } else if (StrIs(in.member, "GetMDIZOrder")) {
        PutI16(&body, 0);
        signature = "n";
    } else if (StrIs(in.member, "GetAlpha")) {
        PutDouble(&body, 1);
        signature = "d";
    } else if (StrIs(in.member, "GrabFocus")) {
        bool focused =
            ElementNode(object) &&
            WindowAccessibilityPerform(
                object.win, object.win->accessibility[object.nodeIndex].id,
                AccessibilityAction::Focus);
        PutBool(&body, focused);
        signature = "b";
    } else {
        VecReset(body.bytes);
        return false;
    }
    return SendHandledReply(in, signature, &body);
}

static bool HandleAction(const Incoming& in, const LinuxAccessible& object) {
    if (!StrIs(in.interfaceName, kAction) || !object.win ||
        object.nodeIndex < 0) {
        return false;
    }
    const AccessibilityNode& node = object.win->accessibility[object.nodeIndex];
    DbusWriter body;
    const char* signature = "";
    int at = 0;
    int index = in.bodyLen ? (int)ReadU32(in.body, in.bodyLen, &at) : 0;
    if (StrIs(in.member, "GetNActions")) {
        PutI32(&body, ActionCount(node));
        signature = "i";
    } else if (StrIs(in.member, "GetActions")) {
        int lengthAt = 0;
        int contentsAt = 0;
        ArrayBegin(&body, 8, &lengthAt, &contentsAt);
        int count = ActionCount(node);
        for (int i = 0; i < count; i++) {
            const char* name = "";
            ActionAt(node, i, &name);
            PutAlign(&body, 8);
            PutString(&body, name);
            PutString(&body, "");
            PutString(&body, "");
        }
        ArrayEnd(&body, lengthAt, contentsAt);
        signature = "a(sss)";
    } else if (StrIs(in.member, "GetName") ||
               StrIs(in.member, "GetLocalizedName")) {
        const char* name = "";
        ActionAt(node, index, &name);
        PutString(&body, name);
        signature = "s";
    } else if (StrIs(in.member, "GetDescription") ||
               StrIs(in.member, "GetKeyBinding")) {
        PutString(&body, "");
        signature = "s";
    } else if (StrIs(in.member, "DoAction")) {
        const char* name = "";
        AccessibilityAction action = ActionAt(node, index, &name);
        PutBool(&body, *name && WindowAccessibilityPerform(object.win, node.id,
                                                           action));
        signature = "b";
    } else {
        VecReset(body.bytes);
        return false;
    }
    return SendHandledReply(in, signature, &body);
}

static bool HandleValue(const Incoming& in, const LinuxAccessible& object) {
    if (!StrIs(in.interfaceName, kValue) || !object.win ||
        object.nodeIndex < 0) {
        return false;
    }
    const AccessibilityNode& node = object.win->accessibility[object.nodeIndex];
    DbusWriter body;
    const char* signature = "";
    if (StrIs(in.member, "SetCurrentValue")) {
        int at = 0;
        double value = ReadDouble(in.body, in.bodyLen, &at);
        PutBool(&body, WindowAccessibilitySetNumericValue(object.win, node.id,
                                                          (float)value));
        signature = "b";
    } else if (StrIs(in.member, "GetCurrentValue")) {
        PutDouble(&body, node.info.numericValue);
        signature = "d";
    } else if (StrIs(in.member, "GetMinimumValue")) {
        PutDouble(&body, node.info.minNumericValue);
        signature = "d";
    } else if (StrIs(in.member, "GetMaximumValue")) {
        PutDouble(&body, node.info.maxNumericValue);
        signature = "d";
    } else if (StrIs(in.member, "GetMinimumIncrement")) {
        PutDouble(&body, node.info.numericValueStep);
        signature = "d";
    } else if (StrIs(in.member, "GetText")) {
        PutString(&body, node.info.value);
        signature = "s";
    } else {
        return false;
    }
    return SendHandledReply(in, signature, &body);
}

static int Utf8ByteForCharacter(Str text, int character) {
    character = std::max(0, character);
    int n = 0;
    for (int i = 0; i < len(text); i++) {
        if (((uint8_t)text.s[i] & 0xc0) != 0x80) {
            if (n++ == character) return i;
        }
    }
    return len(text);
}

static int Utf8CharacterForByte(Str text, int byte) {
    byte = std::max(0, std::min(byte, len(text)));
    return byte > 0 ? Utf8CharCount(Str(text.s, byte)) : 0;
}

static uint32_t Utf8CodepointForCharacter(Str text, int character) {
    int at = Utf8ByteForCharacter(text, character);
    if (at < 0 || at >= len(text)) return 0;
    uint32_t cp = 0;
    Utf8At(text, at, &cp);
    return cp;
}

static bool TextSpace(uint32_t cp) {
    return cp <= 0x20 || cp == 0x85 || cp == 0xa0 || cp == 0x2028 ||
           cp == 0x2029;
}

static bool SentenceEnd(uint32_t cp) {
    return cp == '.' || cp == '!' || cp == '?' || cp == '\n' || cp == 0x2028 ||
           cp == 0x2029;
}

static void TextGranularRange(Str text, int offset, uint32_t granularity,
                              int* lo, int* hi) {
    if (granularity == 3) {
        Selection line =
            AccessibilityTextLineAt(text, Utf8ByteForCharacter(text, offset));
        *lo = Utf8CharacterForByte(text, line.start);
        *hi = Utf8CharacterForByte(text, line.end);
        return;
    }
    int count = Utf8CharCount(text);
    if (count <= 0) {
        *lo = 0;
        *hi = 0;
        return;
    }
    int probe = std::max(0, std::min(offset, count - 1));
    if (granularity == 0) {
        *lo = probe;
        *hi = probe + 1;
        return;
    }
    if (granularity == 1) {
        while (probe > 0 && TextSpace(Utf8CodepointForCharacter(text, probe))) {
            probe--;
        }
        *lo = probe;
        while (*lo > 0 &&
               !TextSpace(Utf8CodepointForCharacter(text, *lo - 1))) {
            (*lo)--;
        }
        *hi = probe;
        while (*hi < count &&
               !TextSpace(Utf8CodepointForCharacter(text, *hi))) {
            (*hi)++;
        }
        while (*hi < count && TextSpace(Utf8CodepointForCharacter(text, *hi))) {
            (*hi)++;
        }
        return;
    }
    if (granularity == 4) {
        *lo = probe;
        while (*lo > 1) {
            if (Utf8CodepointForCharacter(text, *lo - 1) == '\n' &&
                Utf8CodepointForCharacter(text, *lo - 2) == '\n') {
                break;
            }
            (*lo)--;
        }
        *hi = probe;
        while (*hi + 1 < count) {
            if (Utf8CodepointForCharacter(text, *hi) == '\n' &&
                Utf8CodepointForCharacter(text, *hi + 1) == '\n') {
                *hi += 2;
                break;
            }
            (*hi)++;
        }
        if (*hi + 1 >= count) *hi = count;
        return;
    }
    *lo = probe;
    while (*lo > 0 && !SentenceEnd(Utf8CodepointForCharacter(text, *lo - 1))) {
        (*lo)--;
    }
    while (*lo < count && TextSpace(Utf8CodepointForCharacter(text, *lo))) {
        (*lo)++;
    }
    *hi = probe;
    while (*hi < count && !SentenceEnd(Utf8CodepointForCharacter(text, *hi))) {
        (*hi)++;
    }
    if (*hi < count) (*hi)++;
    while (*hi < count && TextSpace(Utf8CodepointForCharacter(text, *hi))) {
        (*hi)++;
    }
}

static Selection TextSelection(Str text, int lo, int hi) {
    Selection result = {};
    result.start = Utf8ByteForCharacter(text, lo);
    result.end = Utf8ByteForCharacter(text, hi);
    return result;
}

static Str TextSlice(Str text, int lo, int hi) {
    lo = std::max(0, std::min(lo, len(text)));
    hi = std::max(lo, std::min(hi, len(text)));
    return text.s ? Str(text.s + lo, hi - lo) : Str{};
}

static Bounds TextBounds(const LinuxAccessible& object,
                         const AccessibilityNode& node, Selection bytes,
                         uint32_t coordType) {
    if (object.part >= 0) return ComponentBounds(object, coordType);
    if (!node.input || node.textExcerpt.s) return {};
    if (!InputIsSingleLine(node.input)) {
        return ComponentBounds(object, coordType);
    }
    Bounds bounds;
    return WindowAccessibilityTextRangeBounds(object.win, node, bytes, &bounds)
               ? BoundsInComponentCoordinates(object, bounds, coordType)
               : Bounds{};
}

static bool HandleText(const Incoming& in, const LinuxAccessible& object) {
    bool textInterface = StrIs(in.interfaceName, kText);
    bool editableInterface = StrIs(in.interfaceName, kEditableText);
    if ((!textInterface && !editableInterface) || !object.win ||
        object.nodeIndex < 0) {
        return false;
    }
    // a part has its own text but no input, selection or caret
    static const AccessibilityNode kPartNode{};
    const AccessibilityNode& node =
        object.part >= 0 ? kPartNode
                         : object.win->accessibility[object.nodeIndex];
    Str text = ObjectText(object);
    if ((textInterface && !text.s) || (editableInterface && !node.input)) {
        return false;
    }
    int at = 0;
    DbusWriter body;
    const char* signature = "";
    if (textInterface && StrIs(in.member, "GetCharacterCount")) {
        PutI32(&body, Utf8CharCount(text));
        signature = "i";
    } else if (textInterface && StrIs(in.member, "GetText")) {
        int lo = (int)ReadU32(in.body, in.bodyLen, &at);
        int hi = (int)ReadU32(in.body, in.bodyLen, &at);
        if (hi < 0) hi = Utf8CharCount(text);
        int byteLo = Utf8ByteForCharacter(text, lo);
        int byteHi = Utf8ByteForCharacter(text, hi);
        PutString(&body, TextSlice(text, byteLo, byteHi));
        signature = "s";
    } else if (textInterface && StrIs(in.member, "GetStringAtOffset")) {
        int offset = ReadI32(in.body, in.bodyLen, &at);
        uint32_t granularity = ReadU32(in.body, in.bodyLen, &at);
        int lo = 0;
        int hi = 0;
        TextGranularRange(text, offset, granularity, &lo, &hi);
        int byteLo = Utf8ByteForCharacter(text, lo);
        int byteHi = Utf8ByteForCharacter(text, hi);
        PutString(&body, TextSlice(text, byteLo, byteHi));
        PutI32(&body, lo);
        PutI32(&body, hi);
        signature = "sii";
    } else if (textInterface && StrIs(in.member, "GetCharacterAtOffset")) {
        int offset = ReadI32(in.body, in.bodyLen, &at);
        PutI32(&body, (int32_t)Utf8CodepointForCharacter(text, offset));
        signature = "i";
    } else if (textInterface && StrIs(in.member, "GetNSelections")) {
        Selection range;
        bool selected = WindowAccessibilityTextSelection(node, &range);
        PutI32(&body, selected && !range.IsEmpty() ? 1 : 0);
        signature = "i";
    } else if (textInterface && StrIs(in.member, "GetSelection")) {
        int selection = ReadI32(in.body, in.bodyLen, &at);
        PutAlign(&body, 8);
        Selection range;
        bool selected = WindowAccessibilityTextSelection(node, &range);
        int lo = Utf8CharacterForByte(text, range.start);
        int hi = Utf8CharacterForByte(text, range.end);
        bool exists = selection == 0 && selected && lo != hi;
        PutI32(&body, exists ? std::min(lo, hi) : -1);
        PutI32(&body, exists ? std::max(lo, hi) : -1);
        signature = "(ii)";
    } else if (textInterface && (StrIs(in.member, "SetSelection") ||
                                 StrIs(in.member, "AddSelection"))) {
        if (StrIs(in.member, "SetSelection")) {
            (void)ReadU32(in.body, in.bodyLen, &at);
        }
        int lo = (int)ReadU32(in.body, in.bodyLen, &at);
        int hi = (int)ReadU32(in.body, in.bodyLen, &at);
        PutBool(&body, WindowAccessibilitySetTextSelection(
                           object.win, node,
                           {Utf8ByteForCharacter(text, lo),
                            Utf8ByteForCharacter(text, hi)}));
        signature = "b";
    } else if (textInterface && StrIs(in.member, "RemoveSelection")) {
        if (node.input) {
            InputUnselect(node.input, object.win->app, object.win);
            AppInvalidate(object.win);
        }
        PutBool(&body, node.input != nullptr);
        signature = "b";
    } else if (textInterface && StrIs(in.member, "GetCaretOffset")) {
        PutI32(&body,
               Utf8CharacterForByte(text, WindowAccessibilityTextCaret(node)));
        signature = "i";
    } else if (textInterface && StrIs(in.member, "GetOffsetAtPoint")) {
        int x = ReadI32(in.body, in.bodyLen, &at);
        int y = ReadI32(in.body, in.bodyLen, &at);
        uint32_t coordType = ReadU32(in.body, in.bodyLen, &at);
        Point point = ComponentPointInWindow(object, x, y, coordType);
        int offset = node.input && !node.textExcerpt.s
                         ? InputIndexForPosition(node.input, &object.win->paint,
                                                 point.x, point.y)
                         : -1;
        PutI32(&body, offset >= 0 ? Utf8CharacterForByte(text, offset) : -1);
        signature = "i";
    } else if (textInterface && StrIs(in.member, "GetCharacterExtents")) {
        int offset = ReadI32(in.body, in.bodyLen, &at);
        uint32_t coordType = ReadU32(in.body, in.bodyLen, &at);
        int count = Utf8CharCount(text);
        Bounds bounds =
            offset < 0 || offset >= count
                ? Bounds{}
                : TextBounds(object, node,
                             TextSelection(text, offset, offset + 1),
                             coordType);
        signature = PutExtents(&body, bounds);
    } else if (textInterface && StrIs(in.member, "GetRangeExtents")) {
        int lo = ReadI32(in.body, in.bodyLen, &at);
        int hi = ReadI32(in.body, in.bodyLen, &at);
        uint32_t coordType = ReadU32(in.body, in.bodyLen, &at);
        Bounds bounds =
            TextBounds(object, node, TextSelection(text, lo, hi), coordType);
        signature = PutExtents(&body, bounds);
    } else if (textInterface && (StrIs(in.member, "GetDefaultAttributes") ||
                                 StrIs(in.member, "GetDefaultAttributeSet"))) {
        PutEmptyStringDict(&body);
        signature = "a{ss}";
    } else if (textInterface && StrIs(in.member, "GetAttributeValue")) {
        (void)ReadI32(in.body, in.bodyLen, &at);
        (void)ReadString(in.body, in.bodyLen, &at);
        PutString(&body, "");
        signature = "s";
    } else if (textInterface && (StrIs(in.member, "GetAttributes") ||
                                 StrIs(in.member, "GetAttributeRun"))) {
        (void)ReadI32(in.body, in.bodyLen, &at);
        if (StrIs(in.member, "GetAttributeRun"))
            (void)ReadU32(in.body, in.bodyLen, &at);
        PutEmptyStringDict(&body);
        PutI32(&body, 0);
        PutI32(&body, Utf8CharCount(text));
        signature = "a{ss}ii";
    } else if (textInterface && StrIs(in.member, "SetCaretOffset")) {
        int offset = (int)ReadU32(in.body, in.bodyLen, &at);
        int byte = Utf8ByteForCharacter(text, offset);
        PutBool(&body, WindowAccessibilitySetTextSelection(object.win, node,
                                                           {byte, byte}));
        signature = "b";
    } else if (editableInterface && StrIs(in.member, "SetTextContents")) {
        Str value = ReadString(in.body, in.bodyLen, &at);
        bool changed = WindowAccessibilityPerform(
            object.win, node.id, AccessibilityAction::SetValue, value);
        PutBool(&body, changed);
        signature = "b";
    } else if (editableInterface && StrIs(in.member, "InsertText")) {
        int position = ReadI32(in.body, in.bodyLen, &at);
        Str value = ReadString(in.body, in.bodyLen, &at);
        int length = ReadI32(in.body, in.bodyLen, &at);
        int available = len(value);
        value.len = std::max(0, std::min(len(value), length));
        while (len(value) > 0 && len(value) < available &&
               ((uint8_t)value.s[len(value)] & 0xc0) == 0x80) {
            value.len--;
        }
        Selection range = TextSelection(text, position, position);
        bool changed = InputReplaceTextInRange(node.input, object.win->app,
                                               object.win, &range, value);
        PutBool(&body, changed);
        signature = "b";
    } else if (editableInterface &&
               (StrIs(in.member, "DeleteText") || StrIs(in.member, "CutText") ||
                StrIs(in.member, "CopyText"))) {
        int lo = ReadI32(in.body, in.bodyLen, &at);
        int hi = ReadI32(in.body, in.bodyLen, &at);
        Selection range = TextSelection(text, lo, hi);
        int byteLo = std::min(range.start, range.end);
        int byteHi = std::max(range.start, range.end);
        bool copy = StrIs(in.member, "CopyText") || StrIs(in.member, "CutText");
        if (copy) ClipboardSetText(object.win, TextSlice(text, byteLo, byteHi));
        if (StrIs(in.member, "CopyText")) {
            signature = "";
        } else {
            bool changed = InputReplaceTextInRange(node.input, object.win->app,
                                                   object.win, &range, Str{});
            PutBool(&body, changed);
            signature = "b";
        }
    } else if (editableInterface && StrIs(in.member, "PasteText")) {
        int position = ReadI32(in.body, in.bodyLen, &at);
        Selection range = TextSelection(text, position, position);
        Str value = ClipboardGetText(GetTempArena(), object.win);
        bool changed = InputReplaceTextInRange(node.input, object.win->app,
                                               object.win, &range, value);
        PutBool(&body, changed);
        signature = "b";
    } else {
        VecReset(body.bytes);
        return false;
    }
    return SendHandledReply(in, signature, &body);
}

static bool SelectionDescendant(const Window* win, int index, int ancestor) {
    int at = win->accessibility[index].parent;
    while (at >= 0 && at < len(win->accessibility)) {
        if (at == ancestor) return true;
        if (IsSelectionRole(win->accessibility[at].info.role)) return false;
        at = win->accessibility[at].parent;
    }
    return false;
}

static bool IsSelectedDescendant(const LinuxAccessible& object, int index) {
    const auto& info = object.win->accessibility[index].info;
    return info.hasSelected && info.selected &&
           SelectionDescendant(object.win, index, object.nodeIndex);
}

static int SelectedAt(const LinuxAccessible& object, int wanted) {
    for (int i = 0; i < len(object.win->accessibility); i++) {
        if (IsSelectedDescendant(object, i) && wanted-- == 0) return i;
    }
    return -1;
}

static int SelectedCount(const LinuxAccessible& object) {
    int count = 0;
    for (int i = 0; i < len(object.win->accessibility); i++) {
        if (IsSelectedDescendant(object, i)) count++;
    }
    return count;
}

static bool LinuxSelectChild(const LinuxAccessible& object, int child) {
    int wi = -1;
    int ni = -1;
    if (!ChildAt(object, child, &wi, &ni) || wi != object.windowIndex) {
        return false;
    }
    const auto& node = object.win->accessibility[ni];
    return node.info.hasSelected &&
           (node.info.selected ||
            (!node.info.disabled &&
             WindowAccessibilityPerform(object.win, node.id,
                                        AccessibilityAction::Default)));
}

static bool HandleSelection(const Incoming& in, const LinuxAccessible& object) {
    if (!StrIs(in.interfaceName, kSelection)) return false;
    const auto* target = ElementNode(object);
    if (!target || !IsSelectionRole(target->info.role)) {
        return false;
    }
    DbusWriter body;
    const char* signature = "";
    int at = 0;
    if (StrIs(in.member, "GetNSelectedChildren")) {
        PutI32(&body, SelectedCount(object));
        signature = "i";
    } else if (StrIs(in.member, "GetSelectedChild")) {
        int selected =
            SelectedAt(object, (int)ReadU32(in.body, in.bodyLen, &at));
        if (selected >= 0) {
            PutObjectRef(&body,
                         PathForTemp(object.windowIndex,
                                     object.win->accessibility[selected].id));
        } else {
            PutNullObjectRef(&body);
        }
        signature = "(so)";
    } else if (StrIs(in.member, "SelectChild")) {
        int child = (int)ReadU32(in.body, in.bodyLen, &at);
        PutBool(&body, LinuxSelectChild(object, child));
        signature = "b";
    } else if (StrIs(in.member, "IsChildSelected")) {
        int wi = -1;
        int ni = -1;
        int child = (int)ReadU32(in.body, in.bodyLen, &at);
        bool selected = ChildAt(object, child, &wi, &ni) &&
                        object.win->accessibility[ni].info.hasSelected &&
                        object.win->accessibility[ni].info.selected;
        PutBool(&body, selected);
        signature = "b";
    } else if (StrIs(in.member, "SelectAll") ||
               StrIs(in.member, "ClearSelection") ||
               StrIs(in.member, "DeselectChild") ||
               StrIs(in.member, "DeselectSelectedChild")) {
        PutBool(&body, false);
        signature = "b";
    } else {
        return false;
    }
    return SendHandledReply(in, signature, &body);
}

static void SendEmbed() {
    if (!gA11y.busName.s || gA11y.embedSerial) return;
    DbusWriter body;
    PutObjectRef(&body, Str(kRootPath));
    gA11y.embedSerial =
        SendCall("org.a11y.atspi.Registry", kRootPath, "org.a11y.atspi.Socket",
                 "Embed", "(so)", &body);
    VecReset(body.bytes);
}

static void HandleReply(const Incoming& in) {
    if (in.replySerial == gA11y.helloSerial && !gA11y.busName.s) {
        int at = 0;
        Str name = ReadString(in.body, in.bodyLen, &at);
        if (name.s) {
            gA11y.busName = StrDup(name);
            SendEmbed();
        }
    } else if (in.replySerial == gA11y.embedSerial && !gA11y.ready &&
               StrIs(in.signature, "(so)")) {
        int at = 0;
        Str desktopBusName = ReadString(in.body, in.bodyLen, &at);
        Str desktopPath = ReadString(in.body, in.bodyLen, &at);
        if (!len(desktopBusName) || !len(desktopPath)) return;
        gA11y.desktopBusName = StrDup(desktopBusName);
        gA11y.desktopPath = StrDup(desktopPath);
        gA11y.ready = true;
        if (gA11y.app) {
            for (auto* win : gA11y.app->windows) {
                // Rebuild the bridge's snapshots even if the last rendered
                // tree was identical before the previous connection closed.
                win->accessibilityHash = 0;
                AppInvalidate(win);
            }
        }
    }
}

static void HandleMethod(const Incoming& in) {
    if (StrIs(in.path, kCachePath)) {
        if (!HandleLinuxCache(in))
            SendError(in.serial, in.sender,
                      "org.freedesktop.DBus.Error.UnknownMethod",
                      "unsupported AT-SPI cache method");
        return;
    }
    LinuxAccessible object = AccessibleForPath(in.path);
    if (!object.root && (!object.win || object.nodeIndex < 0)) {
        SendError(in.serial, in.sender,
                  "org.freedesktop.DBus.Error.UnknownObject",
                  "accessible object is no longer available");
        return;
    }
    if (HandleProperties(in, object) || HandleAccessible(in, object) ||
        HandleApplication(in, object) || HandleComponent(in, object) ||
        HandleText(in, object)) {
        return;
    }
    // a part is read-only text: no actions, value or selection of children
    if (object.part < 0 &&
        (HandleAction(in, object) || HandleValue(in, object) ||
         HandleSelection(in, object))) {
        return;
    }
    if (StrIs(in.interfaceName, "org.freedesktop.DBus.Introspectable") &&
        StrIs(in.member, "Introspect")) {
        static const char xml[] =
            "<node><interface name='org.freedesktop.DBus.Properties'/>"
            "<interface name='org.a11y.atspi.Accessible'/>"
            "<interface name='org.a11y.atspi.Application'/>"
            "<interface name='org.a11y.atspi.Component'/>"
            "<interface name='org.a11y.atspi.Action'/>"
            "<interface name='org.a11y.atspi.Value'/>"
            "<interface name='org.a11y.atspi.Selection'/>"
            "<interface name='org.a11y.atspi.Text'/>"
            "<interface name='org.a11y.atspi.EditableText'/></node>";
        DbusWriter body;
        PutString(&body, xml);
        SendReply(in.serial, in.sender, "s", &body);
        VecReset(body.bytes);
        return;
    }
    SendError(in.serial, in.sender, "org.freedesktop.DBus.Error.UnknownMethod",
              "unsupported AT-SPI method");
}

static void ProcessMessages() {
    for (;;) {
        Incoming in;
        int messageLen = 0;
        if (!ParseIncoming(gA11y.rx.els, len(gA11y.rx), &in, &messageLen)) {
            break;
        }
        if (in.type == 1)
            HandleMethod(in);
        else if (in.type == 2)
            HandleReply(in);
        memmove(gA11y.rx.els, gA11y.rx.els + messageLen,
                (size_t)(len(gA11y.rx) - messageLen));
        gA11y.rx.len -= messageLen;
    }
}

void AccessibilityLinuxInit(App* app, Str busAddress) {
    if (gA11y.fd >= 0 || !app || !busAddress.s || len(busAddress) <= 0) {
        return;
    }
    sockaddr_un address = {};
    socklen_t addressLen = 0;
    if (!ParseUnixAddress(busAddress, &address, &addressLen)) {
        return;
    }
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0 || connect(fd, (sockaddr*)&address, addressLen) != 0) {
        if (fd >= 0) close(fd);
        return;
    }
    gA11y.fd = fd;
    gA11y.app = app;
    if (!Authenticate()) {
        AccessibilityLinuxShutdown();
        return;
    }
    gA11y
        .helloSerial = SendCall("org.freedesktop.DBus", "/org/freedesktop/DBus",
                                "org.freedesktop.DBus", "Hello", "", nullptr);
}

void AccessibilityLinuxShutdown() {
    if (gA11y.fd >= 0) close(gA11y.fd);
    gA11y.fd = -1;
    gA11y.app = nullptr;
    gA11y.serial = 1;
    gA11y.helloSerial = 0;
    gA11y.embedSerial = 0;
    gA11y.applicationId = 0;
    gA11y.ready = false;
    StrFree(gA11y.busName);
    gA11y.busName = {};
    StrFree(gA11y.desktopBusName);
    gA11y.desktopBusName = {};
    StrFree(gA11y.desktopPath);
    gA11y.desktopPath = {};
    VecReset(gA11y.rx);
    for (const auto& snapshot : gA11y.text) StrFree(snapshot.text);
    VecReset(gA11y.text);
    for (auto* history : gA11y.nodes) delete history;
    VecReset(gA11y.nodes);
}

static void RemoveLinuxRootChildren(Window* win,
                                    const LinuxNodeHistory& history);

void AccessibilityLinuxForgetWindow(Window* win) {
    for (int i = len(gA11y.text) - 1; i >= 0; i--) {
        if (gA11y.text[i].window != win) continue;
        StrFree(gA11y.text[i].text);
        VecRemoveAtFast(gA11y.text, i);
    }
    for (int i = 0; i < len(gA11y.nodes); i++) {
        if (gA11y.nodes[i]->window != win) continue;
        RemoveLinuxRootChildren(win, *gA11y.nodes[i]);
        delete gA11y.nodes[i];
        VecRemoveAtFast(gA11y.nodes, i);
        break;
    }
}

int AccessibilityLinuxFd() {
    return gA11y.fd;
}

bool AccessibilityLinuxReady() {
    return gA11y.ready;
}

void AccessibilityLinuxPump() {
    if (gA11y.fd < 0) return;
    uint8_t block[8192];
    for (;;) {
        ssize_t n = recv(gA11y.fd, block, sizeof(block), MSG_DONTWAIT);
        if (n > 0) {
            memcpy(VecAppendBlanks(gA11y.rx, (int)n), block, (size_t)n);
            continue;
        }
        if (n == 0 ||
            (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
            AccessibilityLinuxShutdown();
        }
        break;
    }
    ProcessMessages();
}

struct AtspiEventValue {
    char type = 's';
    Str text = {};
    uint32_t role = 0;
    double number = 0;
    Bounds bounds = {};

    AtspiEventValue() = default;
    AtspiEventValue(Str value) : text(value) {}
    explicit AtspiEventValue(uint32_t value) : type('u'), role(value) {}
    explicit AtspiEventValue(double value) : type('d'), number(value) {}

    static AtspiEventValue Object(uint32_t wireId) {
        AtspiEventValue value(wireId);
        value.type = '(';
        return value;
    }

    static AtspiEventValue Extents(Bounds bounds) {
        AtspiEventValue value;
        value.type = 'r';
        value.bounds = bounds;
        return value;
    }
};

static void SendWireEvent(Window* win, uint32_t wireId, const char* member,
                          const char* detail, int detail1,
                          AtspiEventValue payload = {}, int detail2 = 0) {
    if (!gA11y.ready || !win || !gA11y.app) return;
    int wi = -1;
    for (int i = 0; i < len(gA11y.app->windows); i++) {
        if (gA11y.app->windows[i] == win) {
            wi = i;
            break;
        }
    }
    if (wi < 0) return;
    Str path = wireId ? (Str)WirePathForTemp(wi, wireId) : Str(kRootPath);
    DbusWriter fields;
    DbusWriter body;
    PutHeaderString(&fields, 1, "o", path);
    PutHeaderString(&fields, 2, "s", Str("org.a11y.atspi.Event.Object"));
    PutHeaderString(&fields, 3, "s", Str(member));
    PutHeaderString(&fields, 8, "g", Str("siiva{sv}"));
    PutString(&body, detail);
    PutI32(&body, detail1);
    PutI32(&body, detail2);
    if (payload.type == 'u')
        PutVariantU32(&body, payload.role);
    else if (payload.type == 'd')
        PutVariantDouble(&body, payload.number);
    else if (payload.type == '(') {
        PutSignature(&body, "(so)");
        PutObjectRef(&body, payload.role
                                ? (Str)WirePathForTemp(wi, payload.role)
                                : Str(kRootPath));
    } else if (payload.type == 'r') {
        PutSignature(&body, "(iiii)");
        PutExtents(&body, payload.bounds);
    } else
        PutVariantString(&body, "s", payload.text);
    PutEmptyStringDict(&body);
    SendMessage(4, 0, &fields, &body);
    VecReset(fields.bytes);
    VecReset(body.bytes);
}

static void SendEvent(Window* win, uint32_t nodeId, const char* member,
                      const char* detail, int detail1,
                      AtspiEventValue payload = {}, int detail2 = 0) {
    if (!win) return;
    uint32_t wireId = LinuxWireId(win, nodeId);
    if (wireId || !nodeId)
        SendWireEvent(win, wireId, member, detail, detail1, payload, detail2);
}

static void SendCacheSignal(const char* member, const char* signature,
                            DbusWriter* body) {
    DbusWriter fields;
    PutHeaderString(&fields, 1, "o", Str(kCachePath));
    PutHeaderString(&fields, 2, "s", Str(kCache));
    PutHeaderString(&fields, 3, "s", Str(member));
    PutHeaderString(&fields, 8, "g", Str(signature));
    SendMessage(4, 0, &fields, body);
}

static void RemoveCacheItem(int windowIndex, uint32_t wireId) {
    DbusWriter body;
    PutObjectRef(&body, WirePathForTemp(windowIndex, wireId));
    SendCacheSignal("RemoveAccessible", "(so)", &body);
}

static void RemoveLinuxRootChildren(Window* win,
                                    const LinuxNodeHistory& history) {
    for (const auto& node : history.frames[history.frame]) {
        if (node.parentIndex < 0 && node.wireId)
            SendWireEvent(win, 0, "ChildrenChanged", "remove", -1,
                          AtspiEventValue::Object(node.wireId));
    }
    int wi = 0;
    while (wi < len(gA11y.app->windows) && gA11y.app->windows[wi] != win) wi++;
    if (wi == len(gA11y.app->windows)) return;
    for (const auto& node : history.frames[history.frame]) {
        if (node.wireId) RemoveCacheItem(wi, node.wireId);
    }
}

static void SendTextEdit(Window* win, uint32_t id, Str before, Str after) {
    int prefix = 0;
    int common = std::min(len(before), len(after));
    while (prefix < common && before.s[prefix] == after.s[prefix]) prefix++;
    // Two different UTF-8 scalars can share their leading byte(s).
    while (!Utf8IsCharBoundary(before, prefix) ||
           !Utf8IsCharBoundary(after, prefix))
        prefix--;
    int oldEnd = len(before);
    int newEnd = len(after);
    while (oldEnd > prefix && newEnd > prefix) {
        int oldStart = Utf8Prev(before, oldEnd);
        int newStart = Utf8Prev(after, newEnd);
        if (oldStart < prefix || newStart < prefix ||
            !StrEq(Str(before.s + oldStart, oldEnd - oldStart),
                   Str(after.s + newStart, newEnd - newStart)))
            break;
        oldEnd = oldStart;
        newEnd = newStart;
    }
    Str removed(before.s + prefix, oldEnd - prefix);
    Str added(after.s + prefix, newEnd - prefix);
    if ((len(removed) && memchr(removed.s, 0, len(removed))) ||
        (len(added) && memchr(added.s, 0, len(added)))) {
        // D-Bus strings cannot contain a NUL.
        SendEvent(win, id, "VisibleDataChanged", "", 0);
        return;
    }
    int offset = Utf8CharacterForByte(before, prefix);
    if (len(removed))
        SendEvent(win, id, "TextChanged", "delete", offset, removed,
                  Utf8CharCount(removed));
    if (len(added))
        SendEvent(win, id, "TextChanged", "insert", offset, added,
                  Utf8CharCount(added));
}

static void TrackLinuxText(Window* win, const AccessibilityNode& node,
                           int previouslyFocused) {
    // A lazy document advertises revisions; observing its changes must not
    // eagerly call the application's text provider or duplicate its text.
    if (node.document.text ||
        (!node.input && node.info.role != AccessibilityRole::Document))
        return;
    Str text = WindowAccessibilityTextValue(node);
    if (!text.s) return;
    uint32_t path = node.elementPath ? node.elementPath : node.id;
    LinuxTextSnapshot* previous = nullptr;
    for (auto& snapshot : gA11y.text) {
        if (snapshot.window == win && snapshot.path == path &&
            snapshot.input == node.input && snapshot.role == node.info.role) {
            previous = &snapshot;
            break;
        }
    }

    Selection selection = {-1, -1};
    Selection bytes;
    if (WindowAccessibilityTextSelection(node, &bytes) &&
        bytes.start != bytes.end) {
        selection.start =
            Utf8CharacterForByte(text, std::min(bytes.start, bytes.end));
        selection
            .end = Utf8CharacterForByte(text, std::max(bytes.start, bytes.end));
    }
    int caret = Utf8CharacterForByte(text, WindowAccessibilityTextCaret(node));
    bool excerpt = node.textExcerpt.s != nullptr;
    LinuxTextSnapshot current{
        win,     node.input,        path,  node.info.role,
        {},      selection,         caret, node.textExcerptStart,
        excerpt, gA11y.textRevision};
    if (!previous) {
        current.text = StrDup(text);
        if (current.text.s && !VecAppend(gA11y.text, current))
            StrFree(current.text);
        return;
    }
    bool changed = !StrEq(previous->text, text);
    if (previous->excerpt != excerpt ||
        previous->excerptStart != node.textExcerptStart) {
        // Moving the excerpt's origin changes the coordinate system.
        SendEvent(win, node.id, "VisibleDataChanged", "", 0);
    } else if (changed) {
        SendTextEdit(win, node.id, previous->text, text);
    }
    if (node.focusId && node.focusId == previouslyFocused &&
        node.focusId == win->focusId && AccessibilityLinuxWindowFocused(win)) {
        if (previous->selection.start != selection.start ||
            previous->selection.end != selection.end)
            SendEvent(win, node.id, "TextSelectionChanged", "", 0);
        if (previous->caret != caret)
            SendEvent(win, node.id, "TextCaretMoved", "", caret);
    }
    if (changed) {
        current.text = StrDup(text);
        if (!current.text.s) return;
        StrFree(previous->text);
    } else {
        current.text = previous->text;
    }
    *previous = current;
}

static int CompareLinuxNodePath(const void* a, const void* b) {
    uint32_t x = static_cast<const LinuxNodeSnapshot*>(a)->path;
    uint32_t y = static_cast<const LinuxNodeSnapshot*>(b)->path;
    return (x > y) - (x < y);
}

static int CompareLinuxNode(const void* a, const void* b) {
    if (int path = CompareLinuxNodePath(a, b)) return path;
    uint32_t x = static_cast<const LinuxNodeSnapshot*>(a)->id;
    uint32_t y = static_cast<const LinuxNodeSnapshot*>(b)->id;
    return (x > y) - (x < y);
}

static bool UniqueLinuxNodePath(const Vec<LinuxNodeSnapshot>& frame,
                                const LinuxNodeSnapshot* node) {
    return node->path &&
           (node == frame.begin() || node[-1].path != node->path) &&
           (node + 1 == frame.end() || node[1].path != node->path);
}

static uint64_t LinuxPropertyHash(Str text) {
    return Fnv1a64(kFnv1aBasis64, text.s, (size_t)len(text));
}

static void SendLinuxNodeChanges(Window* win, const LinuxNodeSnapshot& before,
                                 const LinuxNodeSnapshot& after) {
    const AccessibilityInfo& info = win->accessibility[after.index].info;
    const struct {
        AtspiStateBit bit;
        const char* name;
    } states[] = {
        {kAtspiChecked, "checked"},
        {kAtspiCollapsed, "collapsed"},
        {kAtspiEditable, "editable"},
        {kAtspiEnabled, "enabled"},
        {kAtspiExpandable, "expandable"},
        {kAtspiExpanded, "expanded"},
        {kAtspiFocusable, "focusable"},
        {kAtspiHorizontal, "horizontal"},
        {kAtspiMultiLine, "multi-line"},
        {kAtspiSelectable, "selectable"},
        {kAtspiSelected, "selected"},
        {kAtspiSensitive, "sensitive"},
        {kAtspiSingleLine, "single-line"},
        {kAtspiVertical, "vertical"},
        {kAtspiIndeterminate, "indeterminate"},
        {kAtspiSelectableText, "selectable-text"},
        {kAtspiDefault, "is-default"},
        {kAtspiCheckable, "checkable"},
        {kAtspiReadOnly, "read-only"},
    };
    // Focus has its own dispatch path. Report the other GetState transitions.
    uint64_t changed = (before.state ^ after.state) & ~AtspiBit(kAtspiFocused);
    if (changed) {
        for (const auto& state : states) {
            if (changed & AtspiBit(state.bit))
                SendEvent(win, after.id, "StateChanged", state.name,
                          (after.state & AtspiBit(state.bit)) != 0,
                          AtspiEventValue(uint32_t{0}));
        }
    }
    if (before.name != after.name)
        SendEvent(win, after.id, "PropertyChange", "accessible-name", 0,
                  info.PlatformName());
    if (before.description != after.description)
        SendEvent(win, after.id, "PropertyChange", "accessible-description", 0,
                  LinuxNodeDescription(info));
    if (before.role != after.role)
        SendEvent(win, after.id, "PropertyChange", "accessible-role", 0,
                  AtspiEventValue(after.role));
    if (after.hasValue && (!before.hasValue || before.value != after.value))
        SendEvent(win, after.id, "PropertyChange", "accessible-value", 0,
                  AtspiEventValue((double)after.value));
    Bounds oldBounds = before.bounds;
    Bounds bounds = after.bounds;
    if (bounds.x != oldBounds.x || bounds.y != oldBounds.y ||
        bounds.w != oldBounds.w || bounds.h != oldBounds.h) {
        // AccessKit uses CoordType::Window for the event rectangle.
        SendWireEvent(win, after.wireId, "BoundsChanged", "", 0,
                      AtspiEventValue::Extents(bounds));
    }
}

static uint32_t LinuxParentWireId(const Vec<LinuxNodeSnapshot>& frame,
                                  const LinuxNodeSnapshot& node) {
    return node.parentIndex < 0 ? 0 : frame[node.parentIndex].wireId;
}

static int LinuxRootChildOffset(int windowIndex) {
    int count = 0;
    for (int wi = 0; wi < windowIndex; wi++) {
        Window* window = gA11y.app->windows[wi];
        if (!window || !window->plat) continue;
        for (const auto& node : window->accessibility) count += node.parent < 0;
    }
    return count;
}

static void SendLinuxChildrenChanges(Window* win, LinuxNodeHistory* history,
                                     const Vec<LinuxNodeSnapshot>& previous,
                                     Vec<LinuxNodeSnapshot>& next,
                                     int windowIndex) {
    auto& childCounts = history->childCounts;
    auto& lastRetainedChild = history->lastRetainedChild;
    int rootSlot = len(next);
    childCounts[rootSlot] = LinuxRootChildOffset(windowIndex);
    for (int index : history->treeOrder) {
        auto& child = next[index];
        int parent = win->accessibility[child.index].parent;
        child.parentIndex = parent < 0 ? -1 : history->treeOrder[parent];
        if (child.parentIndex >= 0) {
            const auto& parentNode = next[child.parentIndex];
            child.selectionParent = parentNode.selectionContainer
                                        ? child.parentIndex
                                        : parentNode.selectionParent;
        }
        int slot = child.parentIndex < 0 ? rootSlot : child.parentIndex;
        child.siblingIndex = childCounts[slot]++;
    }

    // Once a subtree disappears its former parent is the only live object
    // whose child list needs a removal. Report all removals before additions.
    for (const auto& old : previous) {
        if (old.parentIndex >= 0 && previous[old.parentIndex].nextIndex < 0)
            continue;
        uint32_t parent = LinuxParentWireId(previous, old);
        if (old.nextIndex < 0 ||
            LinuxParentWireId(next, next[old.nextIndex]) != parent)
            SendWireEvent(win, parent, "ChildrenChanged", "remove", -1,
                          AtspiEventValue::Object(old.wireId));
    }

    for (int index : history->treeOrder) {
        const auto& child = next[index];
        if (child.parentIndex >= 0 && next[child.parentIndex].previousIndex < 0)
            continue;
        uint32_t parent = LinuxParentWireId(next, child);
        const auto* old =
            child.previousIndex >= 0 ? &previous[child.previousIndex] : nullptr;
        if (!old || LinuxParentWireId(previous, *old) != parent) {
            SendWireEvent(win, parent, "ChildrenChanged", "add",
                          child.siblingIndex,
                          AtspiEventValue::Object(child.wireId));
        } else {
            // An insertion can shift child indices without changing the
            // relative order. Only actual reorders invalidate the list.
            int slot = child.parentIndex < 0 ? rootSlot : child.parentIndex;
            int& last = lastRetainedChild[slot];
            if (last == -2) continue;
            last = old->siblingIndex < last ? -2 : old->siblingIndex;
        }
    }
    for (int slot = 0; slot <= rootSlot; slot++) {
        if (lastRetainedChild[slot] == -2)
            SendWireEvent(win, slot == rootSlot ? 0 : next[slot].wireId,
                          "ChildrenChanged", "invalidate", 0);
    }
    for (const auto& child : next) {
        if (child.previousIndex < 0) continue;
        uint32_t parent = LinuxParentWireId(next, child);
        if (LinuxParentWireId(previous, previous[child.previousIndex]) !=
            parent)
            SendWireEvent(win, child.wireId, "PropertyChange",
                          "accessible-parent", 0,
                          AtspiEventValue::Object(parent));
    }
}

static void SendLinuxCacheChanges(Window* win, int wi,
                                  const LinuxNodeHistory& history,
                                  const Vec<LinuxNodeSnapshot>& previous,
                                  const Vec<LinuxNodeSnapshot>& next) {
    for (const auto& node : previous) {
        if (node.nextIndex < 0 && node.wireId) RemoveCacheItem(wi, node.wireId);
    }
    for (int index : history.treeOrder) {
        const auto& node = next[index];
        if (!node.wireId) continue;
        int slot = node.parentIndex < 0 ? len(next) : node.parentIndex;
        if (node.previousIndex >= 0 && history.lastRetainedChild[slot] != -2) {
            const auto& old = previous[node.previousIndex];
            if (old.interfaces == node.interfaces &&
                LinuxParentWireId(previous, old) ==
                    LinuxParentWireId(next, node))
                continue;
        }
        DbusWriter body;
        LinuxAccessible object{win, wi, node.index};
        PutCacheItem(&body, object, node.siblingIndex,
                     history.childCounts[index], node.interfaces);
        SendCacheSignal("AddAccessible", GPUI_ATSPI_CACHE_ITEM, &body);
    }
}

static uint32_t LinuxSelectionOwner(const Vec<LinuxNodeSnapshot>& frame,
                                    const LinuxNodeSnapshot& item) {
    if (!(item.state & AtspiBit(kAtspiSelected)) || item.selectionParent < 0)
        return 0;
    return frame[item.selectionParent].wireId;
}

static void SendLinuxSelectionChanges(Window* win,
                                      const Vec<LinuxNodeSnapshot>& previous,
                                      Vec<LinuxNodeSnapshot>& next) {
    for (const auto& old : previous) {
        uint32_t owner = LinuxSelectionOwner(previous, old);
        if (!owner) continue;
        int container = previous[old.selectionParent].nextIndex;
        if (container < 0 || !next[container].selectionContainer) continue;
        if (old.nextIndex < 0 ||
            LinuxSelectionOwner(next, next[old.nextIndex]) != owner)
            next[container].selectionChanged = true;
    }
    for (const auto& item : next) {
        uint32_t owner = LinuxSelectionOwner(next, item);
        if (!owner) continue;
        if (item.previousIndex < 0 ||
            LinuxSelectionOwner(previous, previous[item.previousIndex]) !=
                owner)
            next[item.selectionParent].selectionChanged = true;
    }
    for (const auto& item : next) {
        if (item.selectionChanged)
            SendWireEvent(win, item.wireId, "SelectionChanged", "", 0,
                          AtspiEventValue(uint32_t{0}));
    }
}

static void TrackLinuxNodes(Window* win) {
    int windowIndex = 0;
    if (!gA11y.app) return;
    while (windowIndex < len(gA11y.app->windows) &&
           gA11y.app->windows[windowIndex] != win)
        windowIndex++;
    if (windowIndex == len(gA11y.app->windows)) return;
    LinuxNodeHistory* history = LinuxHistory(win);
    if (!history) {
        history = new LinuxNodeHistory;
        history->window = win;
        if (!VecAppend(gA11y.nodes, history)) {
            delete history;
            return;
        }
    }

    auto& previous = history->frames[history->frame];
    auto& next = history->frames[1 - history->frame];
    int count = len(win->accessibility);
    if (!VecResize(next, count) || !VecResize(history->wireIds, count) ||
        !VecResize(history->treeOrder, count) ||
        !VecResize(history->childCounts, count + 1) ||
        !VecResize(history->lastRetainedChild, count + 1)) {
        SendWireEvent(win, 0, "ChildrenChanged", "invalidate", 0);
        return;
    }
    for (int i = 0; i <= count; i++) {
        history->childCounts[i] = 0;
        history->lastRetainedChild[i] = -1;
    }
    for (auto& old : previous) old.nextIndex = -1;
    for (int i = 0; i < len(next); i++) {
        const auto& node = win->accessibility[i];
        const auto& info = node.info;
        next[i] = {node.elementPath,
                   node.id,
                   0,
                   i,
                   -1,
                   AtspiRole(info.role),
                   LinuxNodeStates(win, node),
                   LinuxPropertyHash(info.PlatformName()),
                   LinuxPropertyHash(LinuxNodeDescription(info)),
                   info.numericValue,
                   (bool)info.hasNumericValue,
                   node.bounds};
        next[i].selectionContainer = IsSelectionRole(info.role);
        next[i].interfaces = LinuxNodeInterfaces(node);
    }
    if (len(next) > 1)
        qsort(next.els, (size_t)len(next), sizeof(next[0]), CompareLinuxNode);
    for (auto& current : next) {
        const LinuxNodeSnapshot* old = nullptr;
        if (len(previous)) {
            old = static_cast<const LinuxNodeSnapshot*>(
                bsearch(&current, previous.els, (size_t)len(previous),
                        sizeof(current), CompareLinuxNode));
            // Unnamed elements share their ancestor's path. Only use a path
            // by itself when it identifies exactly one node in both frames.
            if (!old && UniqueLinuxNodePath(next, &current)) {
                old = static_cast<const LinuxNodeSnapshot*>(
                    bsearch(&current, previous.els, (size_t)len(previous),
                            sizeof(current), CompareLinuxNodePath));
                if (old && !UniqueLinuxNodePath(previous, old)) old = nullptr;
            }
        }
        if (old) current.previousIndex = (int)(old - previous.els);
    }
    // Publish the full mapping before emitting any events. The frame-local
    // IDs still identify nodes for the shared action and document APIs.
    for (auto& current : next) {
        int sortedIndex = (int)(&current - next.els);
        if (current.previousIndex >= 0) {
            current.wireId = previous[current.previousIndex].wireId;
            previous[current.previousIndex].nextIndex = sortedIndex;
        } else if (history->nextWireId <= UINT32_MAX) {
            current.wireId = (uint32_t)history->nextWireId++;
        }
        history->wireIds[current.index] = current.wireId;
        history->treeOrder[current.index] = sortedIndex;
    }
    history->frame = 1 - history->frame;
    SendLinuxChildrenChanges(win, history, previous, next, windowIndex);
    SendLinuxCacheChanges(win, windowIndex, *history, previous, next);
    for (const auto& current : next) {
        if (current.previousIndex >= 0)
            SendLinuxNodeChanges(win, previous[current.previousIndex], current);
    }
    SendLinuxSelectionChanges(win, previous, next);
}

void AccessibilityLinuxTreeChanged(Window* win) {
    if (!win || !gA11y.ready) return;
    TrackLinuxNodes(win);
    const auto* history = LinuxHistory(win);
    int previouslyFocused = history ? history->focusedHandle : 0;
    ++gA11y.textRevision;
    for (int i = 0; i < len(win->accessibility); i++) {
        const auto& node = win->accessibility[i];
        TrackLinuxText(win, node, previouslyFocused);
        // A revision invalidates the source without specifying an edit span.
        if (node.documentTextChanged) {
            SendEvent(win, node.id, "VisibleDataChanged", "", 0);
            if (node.document.partCount)
                SendEvent(win, node.id, "ChildrenChanged", "invalidate", 0);
        }
        if (node.documentSelectionChanged) {
            SendEvent(win, node.id, "TextSelectionChanged", "", 0);
        }
        if (node.liveChanged) {
            SendEvent(win, node.id, "Announcement", "", (int)node.info.live,
                      node.info.label);
        }
    }
    for (int i = len(gA11y.text) - 1; i >= 0; i--) {
        if (gA11y.text[i].window != win ||
            gA11y.text[i].revision == gA11y.textRevision)
            continue;
        StrFree(gA11y.text[i].text);
        VecRemoveAtFast(gA11y.text, i);
    }
    AccessibilityLinuxFocusChanged(win);
}

void AccessibilityLinuxFocusChanged(Window* win) {
    if (!win || !gA11y.ready) return;
    auto* history = LinuxHistory(win);
    if (!history) return; // wait until the first tree has been published

    const AccessibilityNode* focused = nullptr;
    if (win->focusId && AccessibilityLinuxWindowFocused(win)) {
        for (const auto& node : win->accessibility) {
            if (node.focusId == win->focusId) {
                focused = &node;
                break;
            }
        }
    }
    uint32_t next = focused ? LinuxWireId(win, focused->id) : 0;
    int handle = focused ? focused->focusId : 0;
    uint32_t prior = history->focusedNode;
    int priorHandle = history->focusedHandle;
    if (prior == next && priorHandle == handle) return;
    history->focusedNode = next;
    history->focusedHandle = handle;

    const AtspiEventValue zero(uint32_t{0});
    if (next) {
        SendWireEvent(win, next, "StateChanged", "focused", 1, zero);
        // A logical focus gain includes the current selection and caret.
        // Lazy document providers are queried only on request.
        if (priorHandle != handle && !focused->document.text) {
            Str text = WindowAccessibilityTextValue(*focused);
            Selection selection;
            if (text.s &&
                WindowAccessibilityTextSelection(*focused, &selection)) {
                if (selection.start != selection.end)
                    SendWireEvent(win, next, "TextSelectionChanged", "", 0);
                SendWireEvent(
                    win, next, "TextCaretMoved", "",
                    Utf8CharacterForByte(
                        text, WindowAccessibilityTextCaret(*focused)));
            }
        }
    }
    if (prior && prior != next) {
        SendWireEvent(win, prior, "StateChanged", "focused", 0, zero);
    }
}

// The smoke test's client end of a socketpair: one method call through the
// real parser and handlers, its reply parsed into `reply`, whose body points
// into `rx`.
struct SmokeClient {
    int fd = -1;
    int serverFd = -1;
    Vec<uint8_t> rx;
};

static bool SmokeCall(SmokeClient* c, Str path, const char* interfaceName,
                      const char* member, const char* signature,
                      DbusWriter* body, Incoming* reply) {
    gA11y.fd = c->fd;
    SendCall("", StrDupTemp(path).s, interfaceName, member, signature, body);
    gA11y.fd = c->serverFd;
    if (body) VecReset(body->bytes);
    AccessibilityLinuxPump();
    VecReset(c->rx);
    uint8_t block[4096];
    int messageLen = 0;
    for (;;) {
        ssize_t n = recv(c->fd, block, sizeof(block), MSG_DONTWAIT);
        if (n <= 0) break;
        memcpy(VecAppendBlanks(c->rx, (int)n), block, (size_t)n);
    }
    *reply = {};
    return ParseIncoming(c->rx.els, len(c->rx), reply, &messageLen) &&
           reply->type == 2;
}

static Str SmokeStringProperty(SmokeClient* c, Str path, const char* property) {
    DbusWriter body;
    PutString(&body, kAccessible);
    PutString(&body, property);
    Incoming reply;
    if (!SmokeCall(c, path, "org.freedesktop.DBus.Properties", "Get", "ss",
                   &body, &reply)) {
        return {};
    }
    int at = 0;
    ReadSignature(reply.body, reply.bodyLen, &at);
    return StrDupTemp(ReadString(reply.body, reply.bodyLen, &at));
}

// A method answering one (so) reference; its path.
static Str SmokeRefCall(SmokeClient* c, Str path, const char* interfaceName,
                        const char* member, const char* signature,
                        DbusWriter* body) {
    Incoming reply;
    if (!SmokeCall(c, path, interfaceName, member, signature, body, &reply)) {
        return {};
    }
    int at = 0;
    ReadString(reply.body, reply.bodyLen, &at);
    return StrDupTemp(ReadString(reply.body, reply.bodyLen, &at));
}

static int SmokeIntCall(SmokeClient* c, Str path, const char* interfaceName,
                        const char* member) {
    Incoming reply;
    if (!SmokeCall(c, path, interfaceName, member, "", nullptr, &reply)) {
        return -1;
    }
    int at = 0;
    return ReadI32(reply.body, reply.bodyLen, &at);
}

static bool SmokeCheckPart(SmokeClient* c, int windowIndex,
                           const AccessibilityNode& node, int elements,
                           int part) {
    Str docPath = StrDupTemp(PathForTemp(windowIndex, node.id));
    DbusWriter body;
    PutU32(&body, (uint32_t)(elements + part));
    Str path =
        SmokeRefCall(c, docPath, kAccessible, "GetChildAtIndex", "i", &body);
    if (!StrEq(path, PartPathForTemp(windowIndex, node.id, part))) return false;

    bool ok = SmokeIntCall(c, path, kAccessible, "GetRole") == (int)kRolePage;
    Incoming application;
    ok = ok && SmokeCall(c, path, kAccessible, "GetApplication", "", nullptr,
                         &application);
    if (ok) {
        int at = 0;
        ok = StrIs(application.signature, "(so)") &&
             StrEq(ReadString(application.body, application.bodyLen, &at),
                   gA11y.busName) &&
             StrIs(ReadString(application.body, application.bodyLen, &at),
                   kRootPath);
    }
    ok = ok && SmokeIntCall(c, path, kAccessible, "GetIndexInParent") ==
                   elements + part;
    ok = ok && StrEq(SmokeStringProperty(c, path, "Name"),
                     WindowAccessibilityPartName(node, part));

    // the parent reference is the document
    DbusWriter get;
    PutString(&get, kAccessible);
    PutString(&get, "Parent");
    Incoming reply;
    ok = ok && SmokeCall(c, path, "org.freedesktop.DBus.Properties", "Get",
                         "ss", &get, &reply);
    if (ok) {
        int at = 0;
        ReadSignature(reply.body, reply.bodyLen, &at);
        at = AlignAt(at, 8);
        ReadString(reply.body, reply.bodyLen, &at);
        ok = StrEq(ReadString(reply.body, reply.bodyLen, &at), docPath);
    }

    // the Text interface reads the part's text
    PutI32(&body, 0);
    PutI32(&body, -1);
    ok = ok && SmokeCall(c, path, kText, "GetText", "ii", &body, &reply);
    if (ok) {
        int at = 0;
        Str text = ReadString(reply.body, reply.bodyLen, &at);
        ok = StrEq(text, WindowAccessibilityPartText(node, part));
    }

    // Component extents in window coordinates are the part's bounds
    Bounds b;
    PutU32(&body, 1);
    ok = ok && WindowAccessibilityPartBounds(node, part, &b) &&
         SmokeCall(c, path, kComponent, "GetExtents", "u", &body, &reply);
    if (ok) {
        int at = 0;
        int x = ReadI32(reply.body, reply.bodyLen, &at);
        int y = ReadI32(reply.body, reply.bodyLen, &at);
        int w = ReadI32(reply.body, reply.bodyLen, &at);
        int h = ReadI32(reply.body, reply.bodyLen, &at);
        ok = x == (int)b.x && y == (int)b.y && w == (int)b.w && h == (int)b.h;
    }

    // hit testing the document at the part's center finds the part
    PutI32(&body, (int)b.CenterX());
    PutI32(&body, (int)b.CenterY());
    PutU32(&body, 1);
    ok = ok && StrEq(SmokeRefCall(c, docPath, kComponent,
                                  "GetAccessibleAtPoint", "iiu", &body),
                     path);
    return ok;
}

// Linked only by the repository test runner (absent from
// accessibility_linux.h), like AccessibilityWinPartsSmokeTest: drives the
// AT-SPI handlers over a socketpair, without a bus, and checks node
// `nodeId`'s document parts against the source.
bool AccessibilityLinuxPartsSmokeTest(Window* win, uint32_t nodeId) {
    const AccessibilityNode* node = WindowAccessibilityNode(win, nodeId);
    int fds[2] = {-1, -1};
    if (!node || !win->app || socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
        return false;
    }
    // stand in for a connected adapter; the window only needs to look open
    AccessibilityLinuxState saved = gA11y;
    gA11y = AccessibilityLinuxState();
    gA11y.app = win->app;
    gA11y.busName = StrL(":1.0");
    PlatWindow* plat = win->plat;
    if (!plat) win->plat = (PlatWindow*)win;
    int windowIndex = len(win->app->windows);
    VecAppend(win->app->windows, win);

    SmokeClient c;
    c.fd = fds[0];
    c.serverFd = fds[1];
    LinuxAccessible doc = AccessibleForPath(PathForTemp(windowIndex, nodeId));
    int elements = ChildCount(doc);
    int parts = WindowAccessibilityPartCount(*node);
    Str docPath = StrDupTemp(PathForTemp(windowIndex, nodeId));

    DbusWriter get;
    PutString(&get, kAccessible);
    PutString(&get, "ChildCount");
    Incoming reply;
    bool ok =
        parts > 0 && SmokeCall(&c, docPath, "org.freedesktop.DBus.Properties",
                               "Get", "ss", &get, &reply);
    if (ok) {
        int at = 0;
        ReadSignature(reply.body, reply.bodyLen, &at);
        ok = ReadI32(reply.body, reply.bodyLen, &at) == elements + parts;
    }
    for (int part = 0; ok && part < parts; part++) {
        ok = SmokeCheckPart(&c, windowIndex, *node, elements, part);
    }
    // GetChildren lists the parts after the element children
    ok = ok && SmokeCall(&c, docPath, kAccessible, "GetChildren", "", nullptr,
                         &reply);
    if (ok) {
        int at = 0;
        uint32_t bytes = ReadU32(reply.body, reply.bodyLen, &at);
        int end = AlignAt(at, 8) + (int)bytes;
        int n = 0;
        Str last;
        while (ok && AlignAt(at, 8) < end) {
            at = AlignAt(at, 8);
            ReadString(reply.body, reply.bodyLen, &at);
            last = ReadString(reply.body, reply.bodyLen, &at);
            n++;
        }
        ok = n == elements + parts &&
             StrEq(last, PartPathForTemp(windowIndex, nodeId, parts - 1));
    }
    // a part past the end is no object
    ok = ok &&
         !SmokeCall(&c, PartPathForTemp(windowIndex, nodeId, parts),
                    kAccessible, "GetRole", "", nullptr, &reply) &&
         reply.type == 3;

    VecReset(c.rx);
    close(fds[0]);
    close(fds[1]);
    VecRemoveLast(win->app->windows);
    win->plat = plat;
    VecReset(gA11y.rx);
    gA11y = saved;
    return ok;
}

struct LinuxSmokeEvent {
    const char* member;
    const char* detail;
    int offset;
    int count;
    AtspiEventValue payload;
    uint32_t nodeId = 0;
};

static bool LinuxSmokeEvents(int fd, const LinuxSmokeEvent* expected, int count,
                             bool checkChildren = false, int cacheAdds = -1,
                             int cacheRemoves = -1) {
    uint8_t buffer[16384];
    int size = (int)recv(fd, buffer, sizeof(buffer), MSG_DONTWAIT);
    int seen = 0;
    int adds = 0;
    int removes = 0;
    for (int offset = 0; offset < size;) {
        int bytes = 0;
        Incoming in;
        if (!ParseIncoming(buffer + offset, size - offset, &in, &bytes) ||
            in.type != 4)
            return false;
        offset += bytes;
        if (StrIs(in.path, kCachePath)) {
            bool add = StrIs(in.member, "AddAccessible") &&
                       StrIs(in.signature, GPUI_ATSPI_CACHE_ITEM);
            bool remove = StrIs(in.member, "RemoveAccessible") &&
                          StrIs(in.signature, "(so)");
            if (!StrIs(in.interfaceName, kCache) || !(add || remove))
                return false;
            adds += add;
            removes += remove;
            continue;
        }
        if (!StrIs(in.signature, "siiva{sv}")) return false;
        if (!checkChildren && StrIs(in.member, "ChildrenChanged")) continue;
        if (seen >= count) return false;
        const auto& event = expected[seen++];
        int at = 0;
        Str detail = ReadString(in.body, in.bodyLen, &at);
        int pos = ReadI32(in.body, in.bodyLen, &at);
        int lenText = ReadI32(in.body, in.bodyLen, &at);
        Str payloadKind = ReadSignature(in.body, in.bodyLen, &at);
        char wireKind = event.payload.type == 'r' ? '(' : event.payload.type;
        if (!StrIs(in.member, event.member) || !StrIs(detail, event.detail) ||
            pos != event.offset || lenText != event.count ||
            !len(payloadKind) || payloadKind.s[0] != wireKind)
            return false;
        if (event.nodeId && !StrEq(in.path, WirePathForTemp(0, event.nodeId)))
            return false;
        if (event.payload.type == 'u') {
            if (ReadU32(in.body, in.bodyLen, &at) != event.payload.role)
                return false;
        } else if (event.payload.type == 'd') {
            if (ReadDouble(in.body, in.bodyLen, &at) != event.payload.number)
                return false;
        } else if (event.payload.type == 'r') {
            if (!StrIs(payloadKind, "(iiii)")) return false;
            at = AlignAt(at, 8);
            Bounds bounds = event.payload.bounds;
            if (ReadI32(in.body, in.bodyLen, &at) != (int)bounds.x ||
                ReadI32(in.body, in.bodyLen, &at) != (int)bounds.y ||
                ReadI32(in.body, in.bodyLen, &at) != (int)bounds.w ||
                ReadI32(in.body, in.bodyLen, &at) != (int)bounds.h)
                return false;
        } else if (event.payload.type == '(') {
            if (!StrIs(payloadKind, "(so)")) return false;
            at = AlignAt(at, 8);
            Str bus = ReadString(in.body, in.bodyLen, &at);
            Str path = ReadString(in.body, in.bodyLen, &at);
            Str wanted = event.payload.role
                             ? (Str)WirePathForTemp(0, event.payload.role)
                             : Str(kRootPath);
            if (!StrEq(bus, gA11y.busName) || !StrEq(path, wanted))
                return false;
        } else if (!StrEq(ReadString(in.body, in.bodyLen, &at), event.payload
                                                                    .text)) {
            return false;
        }
    }
    return seen == count && (cacheAdds < 0 || adds == cacheAdds) &&
           (cacheRemoves < 0 || removes == cacheRemoves);
}

static bool LinuxSmokeChildChanges(Window* win, int fd) {
    gA11y.busName = StrL(":1.9");
    auto setTree = [win](const uint32_t* paths, const int* parents, int count) {
        VecClear(win->accessibility);
        for (int i = 0; i < count; i++) {
            AccessibilityNode node;
            node.id = (uint32_t)i + 1;
            node.elementPath = paths[i];
            node.parent = parents[i];
            node.info.role = AccessibilityRole::Group;
            VecAppend(win->accessibility, node);
        }
        AccessibilityLinuxTreeChanged(win);
    };
    const uint32_t original[] = {100, 200, 300};
    const int siblings[] = {-1, 0, 0};
    setTree(original, siblings, dimofi(original));
    const LinuxSmokeEvent opened[] = {
        {"ChildrenChanged", "add", 0, 0, AtspiEventValue::Object(1)},
    };
    bool ok = LinuxSmokeEvents(fd, opened, dimofi(opened), true, 3, 0);

    const uint32_t inserted[] = {100, 150, 200, 300};
    const int insertedParents[] = {-1, 0, 0, 0};
    setTree(inserted, insertedParents, dimofi(inserted));
    const LinuxSmokeEvent added[] = {
        {"ChildrenChanged", "add", 0, 0, AtspiEventValue::Object(4), 1},
    };
    ok = LinuxSmokeEvents(fd, added, dimofi(added), true, 1, 0) && ok;
    ok = LinuxWireId(win, 3) == 2 && LinuxWireId(win, 4) == 3 && ok;

    setTree(original, siblings, dimofi(original));
    const LinuxSmokeEvent removed[] = {
        {"ChildrenChanged", "remove", -1, 0, AtspiEventValue::Object(4), 1},
    };
    ok = LinuxSmokeEvents(fd, removed, dimofi(removed), true, 0, 1) && ok;
    ok = LinuxNodeIndexForWire(win, 4) < 0 && ok;

    const uint32_t reordered[] = {100, 300, 200};
    setTree(reordered, siblings, dimofi(reordered));
    const LinuxSmokeEvent orderChanged[] = {
        {"ChildrenChanged", "invalidate", 0, 0, {}, 1},
    };
    ok = LinuxSmokeEvents(fd, orderChanged, dimofi(orderChanged), true, 2, 0) &&
         ok;

    const int reparented[] = {-1, 0, 1};
    setTree(original, reparented, dimofi(original));
    const LinuxSmokeEvent moved[] = {
        {"ChildrenChanged", "remove", -1, 0, AtspiEventValue::Object(3), 1},
        {"ChildrenChanged", "add", 0, 0, AtspiEventValue::Object(3), 2},
        {"PropertyChange", "accessible-parent", 0, 0,
         AtspiEventValue::Object(2), 3},
    };
    ok = LinuxSmokeEvents(fd, moved, dimofi(moved), true, 1, 0) && ok;
    AccessibilityLinuxTreeChanged(win);
    ok = LinuxSmokeEvents(fd, nullptr, 0, true) && ok;

    win->accessibility[2].bounds = {-8, 14, 25, 31};
    AccessibilityLinuxTreeChanged(win);
    const LinuxSmokeEvent boundsChanged[] = {
        {"BoundsChanged", "", 0, 0, AtspiEventValue::Extents({-8, 14, 25, 31}),
         3},
    };
    ok = LinuxSmokeEvents(fd, boundsChanged, dimofi(boundsChanged), true) && ok;
    AccessibilityLinuxTreeChanged(win);
    ok = LinuxSmokeEvents(fd, nullptr, 0, true) && ok;

    AccessibilityLinuxForgetWindow(win);
    const LinuxSmokeEvent closed[] = {
        {"ChildrenChanged", "remove", -1, 0, AtspiEventValue::Object(1)},
    };
    return LinuxSmokeEvents(fd, closed, dimofi(closed), true, 0, 3) && ok;
}

static bool LinuxSmokeSelectionChanges(Window* win, int fd) {
    VecClear(win->accessibility);
    auto append = [win](AccessibilityRole role, int parent, bool selected) {
        AccessibilityNode node;
        node.id = (uint32_t)len(win->accessibility) + 1;
        node.elementPath = node.id;
        node.parent = parent;
        node.info.role = role;
        node.info.hasSelected = role == AccessibilityRole::ListBoxOption ||
                                role == AccessibilityRole::GridCell;
        node.info.selected = selected;
        VecAppend(win->accessibility, node);
    };
    append(AccessibilityRole::ListBox, -1, false);
    append(AccessibilityRole::Group, 0, false);
    append(AccessibilityRole::ListBoxOption, 1, true);
    append(AccessibilityRole::ListBoxOption, 1, false);
    append(AccessibilityRole::Grid, 0, false);
    append(AccessibilityRole::GridCell, 4, false);
    AccessibilityLinuxTreeChanged(win);
    const AtspiEventValue zero(uint32_t{0});
    const LinuxSmokeEvent initial[] = {
        {"SelectionChanged", "", 0, 0, zero, 1},
    };
    bool ok = LinuxSmokeEvents(fd, initial, dimofi(initial));
    int clicks = 0;
    auto& cell = win->accessibility[5];
    cell.actions = AccessibilityActionDefault;
    cell.onClick = MkFunc0(+[](int* count) { ++*count; }, &clicks);
    const LinuxAccessible list{win, 0, 0};
    const LinuxAccessible grid{win, 0, 4};
    ok = !LinuxSelectChild(list, 0) && !LinuxSelectChild(grid, 2) &&
         LinuxSelectChild(grid, 0) && clicks == 1 && ok;

    win->accessibility[2].info.selected = false;
    win->accessibility[3].info.selected = true;
    cell.info.selected = true;
    ok = LinuxSelectChild(grid, 0) && clicks == 1 && SelectedAt(grid, 0) == 5 &&
         SelectedAt(list, 0) == 3 && SelectedAt(list, 1) == -1 && ok;
    DbusWriter property;
    int at = 0;
    ok = PutPropertyVariant(&property, grid, Str(kSelection),
                            StrL("NSelectedChildren")) &&
         StrIs(ReadSignature(property.bytes.els, len(property.bytes), &at),
               "i") &&
         ReadI32(property.bytes.els, len(property.bytes), &at) == 1 && ok;
    const LinuxAccessible group{win, 0, 1};
    ok = !PutPropertyVariant(&property, group, Str(kSelection),
                             StrL("NSelectedChildren")) &&
         ok;
    AccessibilityLinuxTreeChanged(win);
    const LinuxSmokeEvent switched[] = {
        {"StateChanged", "selected", 0, 0, zero, 3},
        {"StateChanged", "selected", 1, 0, zero, 4},
        {"StateChanged", "selected", 1, 0, zero, 6},
        {"SelectionChanged", "", 0, 0, zero, 1},
        {"SelectionChanged", "", 0, 0, zero, 5},
    };
    ok = LinuxSmokeEvents(fd, switched, dimofi(switched)) && ok;

    VecRemoveAt(win->accessibility, 3);
    win->accessibility[4].parent = 3;
    AccessibilityLinuxTreeChanged(win);
    ok = LinuxSmokeEvents(fd, initial, dimofi(initial)) && ok;
    AccessibilityLinuxTreeChanged(win);
    ok = LinuxSmokeEvents(fd, nullptr, 0) && ok;
    AccessibilityLinuxForgetWindow(win);
    return LinuxSmokeEvents(fd, nullptr, 0) && ok;
}

bool AccessibilityLinuxEventSmokeTest() {
    int fds[2] = {-1, -1};
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) return false;
    AccessibilityLinuxState saved = gA11y;
    gA11y = AccessibilityLinuxState();
    App app;
    Window win;
    win.app = &app;
    VecAppend(app.windows, &win);
    gA11y.app = &app;
    gA11y.fd = fds[1];
    gA11y.ready = true;
    InputState input;
    AccessibilityNode node;
    node.id = 1;
    node.elementPath = 55;
    node.focusId = win.focusId = 7;
    node.info.role = AccessibilityRole::TextInput;
    node.info.value = StrL("");
    node.input = &input;
    VecAppend(win.accessibility, node);

    InputSetValue(&input, StrL("a\xc3\xa9!"));
    input.selectedRange = {3, 3};
    AccessibilityLinuxTreeChanged(&win);
    const AtspiEventValue zero(uint32_t{0});
    const LinuxSmokeEvent initialFocus[] = {
        {"StateChanged", "focused", 1, 0, zero, 1},
        {"TextCaretMoved", "", 2, 0, {}, 1},
    };
    bool ok = LinuxSmokeEvents(fds[0], initialFocus, dimofi(initialFocus));

    InputSetValue(&input, StrL("a\xc3\xaa\xf0\x9f\x92\xbb!"));
    input.selectedRange = {1, 7};
    win.accessibility[0].id = 2; // the semantic path survives renumbering
    AccessibilityLinuxTreeChanged(&win);
    const LinuxSmokeEvent replacement[] = {
        {"TextChanged", "delete", 1, 1, StrL("\xc3\xa9"), 1},
        {"TextChanged", "insert", 1, 2, StrL("\xc3\xaa\xf0\x9f\x92\xbb"), 1},
        {"TextSelectionChanged", "", 0, 0, {}},
        {"TextCaretMoved", "", 3, 0, {}},
    };
    ok = LinuxSmokeEvents(fds[0], replacement, dimofi(replacement)) && ok;
    ok = LinuxWireId(&win, 2) == 1 && LinuxNodeIndexForWire(&win, 1) == 0 && ok;

    input.selectedRange = {1, 1};
    AccessibilityLinuxTreeChanged(&win);
    const LinuxSmokeEvent caret[] = {
        {"TextSelectionChanged", "", 0, 0, {}},
        {"TextCaretMoved", "", 1, 0, {}},
    };
    ok = LinuxSmokeEvents(fds[0], caret, dimofi(caret)) && ok;
    AccessibilityLinuxTreeChanged(&win);
    ok = LinuxSmokeEvents(fds[0], nullptr, 0) && ok;

    AccessibilityNode button;
    button.id = 5;
    button.elementPath = 89;
    button.focusId = 8;
    button.info.role = AccessibilityRole::Button;
    VecAppend(win.accessibility, button);
    AccessibilityLinuxTreeChanged(&win);
    ok = LinuxSmokeEvents(fds[0], nullptr, 0) && ok;
    WindowSetFocusId(&win, 8);
    const LinuxSmokeEvent transferred[] = {
        {"StateChanged", "focused", 1, 0, zero, 2},
        {"StateChanged", "focused", 0, 0, zero, 1},
    };
    ok = LinuxSmokeEvents(fds[0], transferred, dimofi(transferred)) && ok;
    const LinuxSmokeEvent buttonBlur[] = {
        {"StateChanged", "focused", 0, 0, zero, 2},
    };
    WindowSetFocusId(&win, 0);
    ok = LinuxSmokeEvents(fds[0], buttonBlur, dimofi(buttonBlur)) && ok;
    input.selectedRange = {1, 7};
    WindowSetFocusId(&win, 7);
    const LinuxSmokeEvent editableFocus[] = {
        {"StateChanged", "focused", 1, 0, zero, 1},
        {"TextSelectionChanged", "", 0, 0, {}, 1},
        {"TextCaretMoved", "", 3, 0, {}, 1},
    };
    ok = LinuxSmokeEvents(fds[0], editableFocus, dimofi(editableFocus)) && ok;
    const LinuxSmokeEvent editableBlur[] = {
        {"StateChanged", "focused", 0, 0, zero, 1},
    };
    win.active = false;
    AccessibilityLinuxFocusChanged(&win);
    ok = LinuxSmokeEvents(fds[0], editableBlur, dimofi(editableBlur)) &&
         win.focusId == 7 &&
         !(LinuxNodeStates(&win, win.accessibility[0]) &
           AtspiBit(kAtspiFocused)) &&
         ok;
    win.active = true;
    AccessibilityLinuxFocusChanged(&win);
    ok = LinuxSmokeEvents(fds[0], editableFocus, dimofi(editableFocus)) && ok;
    AccessibilityLinuxFocusChanged(&win);
    ok = LinuxSmokeEvents(fds[0], nullptr, 0) && ok;

    input.masked = true;
    InputSetValue(&input, StrL("secret"));
    AccessibilityLinuxTreeChanged(&win);
    ok = LinuxSmokeEvents(fds[0], nullptr, 0) && !len(gA11y.text) && ok;
    WindowSetFocusId(&win, 0);
    ok = LinuxSmokeEvents(fds[0], editableBlur, dimofi(editableBlur)) && ok;
    VecRemoveLast(win.accessibility);

    auto& control = win.accessibility[0];
    control = {};
    control.id = 3;
    control.elementPath = 60;
    control.info.role = AccessibilityRole::CheckBox;
    control.info.label = StrL("Send alerts");
    control.info.placeholder = StrL("Alerts from this device");
    control.info.toggled = AccessibilityToggled::False;
    AccessibilityLinuxTreeChanged(&win);
    ok = LinuxSmokeEvents(fds[0], nullptr, 0) && ok;
    ok = LinuxWireId(&win, 3) == 3 && LinuxNodeIndexForWire(&win, 1) == -1 &&
         LinuxNodeIndexForWire(&win, 2) == -1 && ok;

    control.id = 4;
    control.info.label = StrL("Send notifications");
    control.info.description = StrL("Alerts on all devices");
    control.info.toggled = AccessibilityToggled::True;
    control.info.hasNumericValue = true;
    control.info.numericValue = 2.5f;
    AccessibilityLinuxTreeChanged(&win);
    const LinuxSmokeEvent checked[] = {
        {"StateChanged", "checked", 1, 0, zero},
        {"PropertyChange", "accessible-name", 0, 0, StrL("Send notifications")},
        {"PropertyChange", "accessible-description", 0, 0,
         StrL("Alerts on all devices")},
        {"PropertyChange", "accessible-value", 0, 0, AtspiEventValue(2.5)},
    };
    ok = LinuxSmokeEvents(fds[0], checked, dimofi(checked)) && ok;

    control.info.disabled = true;
    control.info.toggled = AccessibilityToggled::Mixed;
    control.info.description = {};
    control.info.role = AccessibilityRole::Slider;
    AccessibilityLinuxTreeChanged(&win);
    const LinuxSmokeEvent mixed[] = {
        {"StateChanged", "checked", 0, 0, zero},
        {"StateChanged", "enabled", 0, 0, zero},
        {"StateChanged", "sensitive", 0, 0, zero},
        {"StateChanged", "indeterminate", 1, 0, zero},
        {"PropertyChange", "accessible-description", 0, 0,
         StrL("Alerts from this device")},
        {"PropertyChange", "accessible-role", 0, 0,
         AtspiEventValue(AtspiRole(AccessibilityRole::Slider))},
    };
    ok = LinuxSmokeEvents(fds[0], mixed, dimofi(mixed)) && ok;
    AccessibilityLinuxTreeChanged(&win);
    ok = LinuxSmokeEvents(fds[0], nullptr, 0) && ok;
    AccessibilityLinuxForgetWindow(&win);
    ok = LinuxSmokeEvents(fds[0], nullptr, 0) && ok;
    ok = LinuxSmokeChildChanges(&win, fds[0]) && ok;
    ok = LinuxSmokeSelectionChanges(&win, fds[0]) && ok;
    close(fds[0]);
    close(fds[1]);
    gA11y = saved;
    return ok;
}

} // namespace gpui

#undef GPUI_ATSPI_CACHE_ITEM
#pragma pop_macro("True")
#pragma pop_macro("Status")
