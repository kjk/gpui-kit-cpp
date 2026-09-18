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

namespace gpui {

static constexpr const char* kRootPath = "/org/a11y/atspi/accessible/root";
static constexpr const char* kNullPath = "/org/a11y/atspi/null";
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
    int n = (alignment - (w->bytes.len % alignment)) % alignment;
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

struct AccessibilityLinuxState {
    App* app = nullptr;
    int fd = -1;
    uint32_t serial = 1;
    uint32_t helloSerial = 0;
    uint32_t embedSerial = 0;
    Str busName = {};
    Str busAddress = {};
    Vec<uint8_t> rx;
    int32_t applicationId = 0;
    bool ready = false;
};

static AccessibilityLinuxState gA11y;

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
    PutU32(&message, body ? (uint32_t)body->bytes.len : 0);
    uint32_t serial = gA11y.serial++;
    PutU32(&message, serial);
    PutU32(&message, (uint32_t)fields->bytes.len);
    PutBytes(&message, fields->bytes.els, fields->bytes.len);
    PutAlign(&message, 8);
    if (body) {
        PutBytes(&message, body->bytes.els, body->bytes.len);
    }
    bool ok = SendAll(message.bytes.els, message.bytes.len);
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
    uint32_t n = (uint32_t)(w->bytes.len - contentsAt);
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
    for (int p = key; p < end && at < (int)sizeof(out->sun_path) - 1; p++) {
        if (copy.s[p] == '%' && p + 2 < end) {
            auto hex = [](char ch) -> int {
                if (ch >= '0' && ch <= '9') return ch - '0';
                if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
                if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
                return -1;
            };
            int hi = hex(copy.s[p + 1]);
            int lo = hex(copy.s[p + 2]);
            if (hi >= 0 && lo >= 0) {
                out->sun_path[at++] = (char)((hi << 4) | lo);
                p += 2;
                continue;
            }
        }
        out->sun_path[at++] = copy.s[p];
    }
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
        static const char hex[] = "0123456789abcdef";
        auth.s[at++] = hex[((uint8_t)*p) >> 4];
        auth.s[at++] = hex[((uint8_t)*p) & 15];
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

struct LinuxAccessible {
    Window* win = nullptr;
    int windowIndex = -1;
    int nodeIndex = -1;
    bool root = false;
};

static LinuxAccessible AccessibleForPath(Str path) {
    LinuxAccessible result;
    if (StrIs(path, kRootPath)) {
        result.root = true;
        return result;
    }
    TempStr value = StrDupTemp(Str(path.s, std::min(len(path), 127)));
    int windowIndex = -1;
    unsigned nodeId = 0;
    if (sscanf(value.s, "/org/a11y/atspi/accessible/w%d/n%u", &windowIndex,
               &nodeId) != 2 ||
        !gA11y.app || windowIndex < 0 ||
        windowIndex >= gA11y.app->windows.len) {
        return result;
    }
    Window* win = gA11y.app->windows[windowIndex];
    if (!win || !win->plat) {
        return result;
    }
    for (int i = 0; i < win->accessibility.len; i++) {
        if (win->accessibility[i].id == nodeId) {
            result.win = win;
            result.windowIndex = windowIndex;
            result.nodeIndex = i;
            break;
        }
    }
    return result;
}

static TempStr PathForTemp(int windowIndex, uint32_t nodeId) {
    return fmt("/org/a11y/atspi/accessible/w%d/n%u", windowIndex, nodeId);
}

static void PutObjectRef(DbusWriter* body, Str path) {
    PutAlign(body, 8);
    PutString(body, gA11y.busName);
    PutString(body, path);
}

static void PutNullObjectRef(DbusWriter* body) {
    PutAlign(body, 8);
    PutString(body, "");
    PutString(body, kNullPath);
}

static int ChildCount(const LinuxAccessible& object) {
    if (object.root) {
        int n = 0;
        if (gA11y.app) {
            for (int wi = 0; wi < gA11y.app->windows.len; wi++) {
                Window* win = gA11y.app->windows[wi];
                if (!win || !win->plat) continue;
                for (int i = 0; i < win->accessibility.len; i++) {
                    n += win->accessibility[i].parent < 0 ? 1 : 0;
                }
            }
        }
        return n;
    }
    if (!object.win || object.nodeIndex < 0) {
        return 0;
    }
    int n = 0;
    for (int i = 0; i < object.win->accessibility.len; i++) {
        n += object.win->accessibility[i].parent == object.nodeIndex ? 1 : 0;
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
        for (int wi = 0; wi < gA11y.app->windows.len; wi++) {
            Window* win = gA11y.app->windows[wi];
            if (!win || !win->plat) continue;
            for (int i = 0; i < win->accessibility.len; i++) {
                if (win->accessibility[i].parent < 0 && wanted-- == 0) {
                    *windowIndex = wi;
                    *nodeIndex = i;
                    return true;
                }
            }
        }
        return false;
    }
    for (int i = 0; object.win && i < object.win->accessibility.len; i++) {
        if (object.win->accessibility[i].parent == object.nodeIndex &&
            wanted-- == 0) {
            *windowIndex = object.windowIndex;
            *nodeIndex = i;
            return true;
        }
    }
    return false;
}

static bool IsTextRole(AccessibilityRole role) {
    switch (role) {
        case AccessibilityRole::TextInput:
        case AccessibilityRole::SearchInput:
        case AccessibilityRole::MultilineTextInput:
        case AccessibilityRole::DateInput:
        case AccessibilityRole::DateTimeInput:
        case AccessibilityRole::WeekInput:
        case AccessibilityRole::MonthInput:
        case AccessibilityRole::TimeInput:
        case AccessibilityRole::EmailInput:
        case AccessibilityRole::NumberInput:
        case AccessibilityRole::PasswordInput:
        case AccessibilityRole::PhoneNumberInput:
        case AccessibilityRole::UrlInput:
            return true;
        default:
            return false;
    }
}

static bool IsSelectionRole(AccessibilityRole role) {
    return role == AccessibilityRole::ListBox ||
           role == AccessibilityRole::RadioGroup ||
           role == AccessibilityRole::TabList ||
           role == AccessibilityRole::Tree;
}

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
        case 88:
            return "link";
        case 90:
            return "table row";
        case 91:
            return "tree item";
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

static void PutInterfaces(DbusWriter* body, const LinuxAccessible& object) {
    int lengthAt = 0;
    int contentsAt = 0;
    ArrayBegin(body, 4, &lengthAt, &contentsAt);
    PutString(body, kAccessible);
    if (object.root) {
        PutString(body, kApplication);
    } else if (object.win && object.nodeIndex >= 0) {
        PutString(body, kComponent);
        const AccessibilityNode& node = object.win
                                            ->accessibility[object.nodeIndex];
        if (ActionCount(node)) PutString(body, kAction);
        if (node.info.hasNumericValue) PutString(body, kValue);
        if (IsSelectionRole(node.info.role)) PutString(body, kSelection);
        if (IsTextRole(node.info.role) && node.input) {
            PutString(body, kText);
            if (node.actions & AccessibilityActionSetValue) {
                PutString(body, kEditableText);
            }
        }
    }
    ArrayEnd(body, lengthAt, contentsAt);
}

static void SetStateBit(uint32_t state[2], int bit) {
    state[bit / 32] |= 1u << (bit % 32);
}

static void PutStates(DbusWriter* body, const LinuxAccessible& object) {
    uint32_t state[2] = {};
    SetStateBit(state, 25); // showing
    SetStateBit(state, 34); // visible
    if (object.root) {
        SetStateBit(state, 8); // enabled
    } else if (object.win && object.nodeIndex >= 0) {
        const AccessibilityNode& node = object.win
                                            ->accessibility[object.nodeIndex];
        if (!node.info.disabled) {
            SetStateBit(state, 8);  // enabled
            SetStateBit(state, 24); // sensitive
        }
        if (node.actions & AccessibilityActionFocus) SetStateBit(state, 11);
        if (node.focusId && node.focusId == object.win->focusId) {
            SetStateBit(state, 12);
        }
        if (node.info.toggled != AccessibilityToggled::Unset) {
            SetStateBit(state, 41); // checkable
            if (node.info.toggled == AccessibilityToggled::True) {
                SetStateBit(state, 4);
            }
        }
        if (node.info.hasExpanded) {
            SetStateBit(state, 9);
            SetStateBit(state, node.info.expanded ? 10 : 5);
        }
        if (node.info.hasSelected) {
            SetStateBit(state, 22);
            if (node.info.selected) SetStateBit(state, 23);
        }
        if (node.input) {
            SetStateBit(state, 38); // selectable text
            SetStateBit(state, InputIsMultiLine(node.input) ? 17 : 26);
            if (InputIsEditable(node.input))
                SetStateBit(state, 7);
            else
                SetStateBit(state, 43);
        }
        if (node.info.orientation == AccessibilityOrientation::Horizontal) {
            SetStateBit(state, 14);
        } else if (node.info
                       .orientation == AccessibilityOrientation::Vertical) {
            SetStateBit(state, 29);
        }
        if (node.info.role == AccessibilityRole::DefaultButton) {
            SetStateBit(state, 39);
        }
    }
    int lengthAt = 0;
    int contentsAt = 0;
    ArrayBegin(body, 4, &lengthAt, &contentsAt);
    PutU32(body, state[0]);
    PutU32(body, state[1]);
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
        PutNullObjectRef(body);
        return;
    }
    const AccessibilityNode& node = object.win->accessibility[object.nodeIndex];
    if (node.parent < 0) {
        PutObjectRef(body, Str(kRootPath));
    } else {
        PutObjectRef(body,
                     PathForTemp(object.windowIndex,
                                 object.win->accessibility[node.parent].id));
    }
}

static int IndexInParent(const LinuxAccessible& object) {
    if (object.root || !object.win || object.nodeIndex < 0) return -1;
    int parent = object.win->accessibility[object.nodeIndex].parent;
    int index = 0;
    if (parent < 0) {
        for (int wi = 0; gA11y.app && wi < gA11y.app->windows.len; wi++) {
            Window* win = gA11y.app->windows[wi];
            if (!win || !win->plat) continue;
            for (int i = 0; i < win->accessibility.len; i++) {
                if (win->accessibility[i].parent < 0) {
                    if (win == object.win && i == object.nodeIndex)
                        return index;
                    index++;
                }
            }
        }
        return -1;
    }
    for (int i = 0; i < object.win->accessibility.len; i++) {
        if (object.win->accessibility[i].parent == parent) {
            if (i == object.nodeIndex) return index;
            index++;
        }
    }
    return -1;
}

static Str ObjectName(const LinuxAccessible& object) {
    if (object.root) return Str("gpui application");
    return object.win->accessibility[object.nodeIndex].info.label;
}

static int Utf8Characters(Str text);
static int Utf8CharacterForByte(Str text, int byte);
static int SelectedAt(const LinuxAccessible& object, int wanted);

static bool PutPropertyVariant(DbusWriter* body, const LinuxAccessible& object,
                               Str interfaceName, Str property) {
    const AccessibilityNode* node = object.win && object.nodeIndex >= 0
                                        ? &object.win
                                               ->accessibility[object.nodeIndex]
                                        : nullptr;
    if (StrIs(interfaceName, kAccessible)) {
        if (StrIs(property, "version")) {
            PutVariantU32(body, 1);
        } else if (StrIs(property, "Name")) {
            PutVariantString(body, "s", ObjectName(object));
        } else if (StrIs(property, "Description") ||
                   StrIs(property, "HelpText")) {
            PutVariantString(body, "s", node ? node->info.placeholder : Str{});
        } else if (StrIs(property, "AccessibleId")) {
            PutVariantString(body, "s", node ? node->info.authorId : Str{});
        } else if (StrIs(property, "Locale")) {
            const char* locale = setlocale(LC_MESSAGES, nullptr);
            PutVariantString(body, "s", Str(locale ? locale : ""));
        } else if (StrIs(property, "ChildCount")) {
            PutVariantI32(body, ChildCount(object));
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
    } else if (StrIs(interfaceName, kSelection) && node) {
        if (StrIs(property, "version")) {
            PutVariantU32(body, 1);
        } else if (StrIs(property, "NSelectedChildren")) {
            int n = 0;
            while (SelectedAt(object, n) >= 0) n++;
            PutVariantI32(body, n);
        } else {
            return false;
        }
    } else if (StrIs(interfaceName, kText) && node && node->input) {
        Str text = node->info.role == AccessibilityRole::PasswordInput
                       ? Str{}
                       : InputValue(node->input);
        if (StrIs(property, "version"))
            PutVariantU32(body, 1);
        else if (StrIs(property, "CharacterCount"))
            PutVariantI32(body, Utf8Characters(text));
        else if (StrIs(property, "CaretOffset"))
            PutVariantI32(body,
                          Utf8CharacterForByte(text, InputCursor(node->input)));
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
    } else if (StrIs(interfaceName, kAction) && object.nodeIndex >= 0 &&
               ActionCount(object.win->accessibility[object.nodeIndex])) {
        GPUI_ATSPI_PROPERTY("version");
        GPUI_ATSPI_PROPERTY("NActions");
    } else if (StrIs(interfaceName, kValue) && object.nodeIndex >= 0 &&
               object.win->accessibility[object.nodeIndex]
                   .info.hasNumericValue) {
        GPUI_ATSPI_PROPERTY("version");
        GPUI_ATSPI_PROPERTY("MinimumValue");
        GPUI_ATSPI_PROPERTY("MaximumValue");
        GPUI_ATSPI_PROPERTY("MinimumIncrement");
        GPUI_ATSPI_PROPERTY("CurrentValue");
        GPUI_ATSPI_PROPERTY("Text");
    } else if (StrIs(interfaceName, kSelection) && object.nodeIndex >= 0 &&
               IsSelectionRole(object.win->accessibility[object.nodeIndex]
                                   .info.role)) {
        GPUI_ATSPI_PROPERTY("version");
        GPUI_ATSPI_PROPERTY("NSelectedChildren");
    } else if (StrIs(interfaceName, kText) && object.nodeIndex >= 0 &&
               object.win->accessibility[object.nodeIndex].input) {
        GPUI_ATSPI_PROPERTY("version");
        GPUI_ATSPI_PROPERTY("CharacterCount");
        GPUI_ATSPI_PROPERTY("CaretOffset");
    } else if (StrIs(interfaceName, kEditableText) && object.nodeIndex >= 0 &&
               object.win->accessibility[object.nodeIndex].input &&
               (object.win->accessibility[object.nodeIndex].actions &
                AccessibilityActionSetValue)) {
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

static bool HandleAccessible(const Incoming& in,
                             const LinuxAccessible& object) {
    if (!StrIs(in.interfaceName, kAccessible)) return false;
    DbusWriter body;
    const AccessibilityNode* node = object.win && object.nodeIndex >= 0
                                        ? &object.win
                                               ->accessibility[object.nodeIndex]
                                        : nullptr;
    const char* signature = "";
    if (StrIs(in.member, "GetChildAtIndex")) {
        int at = 0;
        int wanted = (int)ReadU32(in.body, in.bodyLen, &at);
        int wi = -1;
        int ni = -1;
        if (ChildAt(object, wanted, &wi, &ni)) {
            PutObjectRef(
                &body,
                PathForTemp(wi, gA11y.app->windows[wi]->accessibility[ni].id));
        } else {
            PutNullObjectRef(&body);
        }
        signature = "(so)";
    } else if (StrIs(in.member, "GetChildren")) {
        int lengthAt = 0;
        int contentsAt = 0;
        ArrayBegin(&body, 8, &lengthAt, &contentsAt);
        for (int i = 0; i < ChildCount(object); i++) {
            int wi = -1;
            int ni = -1;
            if (ChildAt(object, i, &wi, &ni)) {
                PutObjectRef(&body, PathForTemp(wi, gA11y.app->windows[wi]
                                                        ->accessibility[ni]
                                                        .id));
            }
        }
        ArrayEnd(&body, lengthAt, contentsAt);
        signature = "a(so)";
    } else if (StrIs(in.member, "GetIndexInParent")) {
        PutI32(&body, IndexInParent(object));
        signature = "i";
    } else if (StrIs(in.member, "GetRole")) {
        PutU32(&body, object.root ? 75 : AtspiRole(node->info.role));
        signature = "u";
    } else if (StrIs(in.member, "GetRoleName") ||
               StrIs(in.member, "GetLocalizedRoleName")) {
        PutString(&body,
                  AtspiRoleName(object.root ? 75 : AtspiRole(node->info.role)));
        signature = "s";
    } else if (StrIs(in.member, "GetState")) {
        PutStates(&body, object);
        signature = "au";
    } else if (StrIs(in.member, "GetInterfaces")) {
        PutInterfaces(&body, object);
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
    } else {
        VecReset(body.bytes);
        return false;
    }
    SendReply(in.serial, in.sender, signature, &body);
    VecReset(body.bytes);
    return true;
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
        PutString(&body, gA11y.busAddress);
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
    for (int at = index; at >= 0 && at < win->accessibility.len;
         at = win->accessibility[at].parent) {
        if (at == root) return true;
    }
    return false;
}

static int NodeAtPoint(Window* win, int root, int x, int y) {
    int found = -1;
    for (int i = 0; win && i < win->accessibility.len; i++) {
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
    if (coordType == 2 && object.nodeIndex >= 0) {
        int parent = object.win->accessibility[object.nodeIndex].parent;
        if (parent >= 0 && parent < object.win->accessibility.len) {
            origin.x = object.win->accessibility[parent].bounds.x;
            origin.y = object.win->accessibility[parent].bounds.y;
        }
    }
    return origin;
}

static Bounds ComponentBounds(const LinuxAccessible& object,
                              uint32_t coordType) {
    Bounds bounds = {};
    if (!object.win || object.nodeIndex < 0) return bounds;
    bounds = object.win->accessibility[object.nodeIndex].bounds;
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
        int found = NodeAtPoint(object.win, object.nodeIndex, (int)point.x,
                                (int)point.y);
        if (found >= 0) {
            PutObjectRef(&body,
                         PathForTemp(object.windowIndex,
                                     object.win->accessibility[found].id));
        } else {
            PutNullObjectRef(&body);
        }
        signature = "(so)";
    } else if (StrIs(in.member, "GetExtents")) {
        uint32_t coordType = ReadU32(in.body, in.bodyLen, &at);
        Bounds bounds = ComponentBounds(object, coordType);
        PutAlign(&body, 8);
        PutI32(&body, (int)bounds.x);
        PutI32(&body, (int)bounds.y);
        PutI32(&body, (int)bounds.w);
        PutI32(&body, (int)bounds.h);
        signature = "(iiii)";
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
            !object.root && object.win &&
            WindowAccessibilityPerform(
                object.win, object.win->accessibility[object.nodeIndex].id,
                AccessibilityAction::Focus);
        PutBool(&body, focused);
        signature = "b";
    } else {
        VecReset(body.bytes);
        return false;
    }
    SendReply(in.serial, in.sender, signature, &body);
    VecReset(body.bytes);
    return true;
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
    SendReply(in.serial, in.sender, signature, &body);
    VecReset(body.bytes);
    return true;
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
    SendReply(in.serial, in.sender, signature, &body);
    VecReset(body.bytes);
    return true;
}

static int Utf8Characters(Str text) {
    int n = 0;
    for (int i = 0; i < len(text); i++) {
        if (((uint8_t)text.s[i] & 0xc0) != 0x80) n++;
    }
    return n;
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
    int n = 0;
    for (int i = 0; i < byte; i++) {
        if (((uint8_t)text.s[i] & 0xc0) != 0x80) n++;
    }
    return n;
}

static uint32_t Utf8CodepointForCharacter(Str text, int character) {
    int at = Utf8ByteForCharacter(text, character);
    if (at < 0 || at >= len(text)) return 0;
    const uint8_t* s = (const uint8_t*)text.s + at;
    int left = len(text) - at;
    if (s[0] < 0x80) return s[0];
    if ((s[0] & 0xe0) == 0xc0 && left >= 2)
        return ((uint32_t)(s[0] & 0x1f) << 6) | (s[1] & 0x3f);
    if ((s[0] & 0xf0) == 0xe0 && left >= 3)
        return ((uint32_t)(s[0] & 0x0f) << 12) |
               ((uint32_t)(s[1] & 0x3f) << 6) | (s[2] & 0x3f);
    if ((s[0] & 0xf8) == 0xf0 && left >= 4)
        return ((uint32_t)(s[0] & 7) << 18) | ((uint32_t)(s[1] & 0x3f) << 12) |
               ((uint32_t)(s[2] & 0x3f) << 6) | (s[3] & 0x3f);
    return 0xfffd;
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
    int count = Utf8Characters(text);
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
    if (granularity == 3) {
        *lo = probe;
        while (*lo > 0 && Utf8CodepointForCharacter(text, *lo - 1) != '\n') {
            (*lo)--;
        }
        *hi = probe;
        while (*hi < count && Utf8CodepointForCharacter(text, *hi) != '\n') {
            (*hi)++;
        }
        if (*hi < count) (*hi)++;
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

static bool HandleText(const Incoming& in, const LinuxAccessible& object) {
    bool textInterface = StrIs(in.interfaceName, kText);
    bool editableInterface = StrIs(in.interfaceName, kEditableText);
    if ((!textInterface && !editableInterface) || !object.win ||
        object.nodeIndex < 0) {
        return false;
    }
    const AccessibilityNode& node = object.win->accessibility[object.nodeIndex];
    if (!node.input) return false;
    Str text = node.info.role == AccessibilityRole::PasswordInput
                   ? Str{}
                   : InputValue(node.input);
    int at = 0;
    DbusWriter body;
    const char* signature = "";
    if (textInterface && StrIs(in.member, "GetCharacterCount")) {
        PutI32(&body, Utf8Characters(text));
        signature = "i";
    } else if (textInterface && StrIs(in.member, "GetText")) {
        int lo = (int)ReadU32(in.body, in.bodyLen, &at);
        int hi = (int)ReadU32(in.body, in.bodyLen, &at);
        if (hi < 0) hi = Utf8Characters(text);
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
        PutI32(&body, node.input->selectedRange.start != node.input
                                                             ->selectedRange.end
                          ? 1
                          : 0);
        signature = "i";
    } else if (textInterface && StrIs(in.member, "GetSelection")) {
        int selection = ReadI32(in.body, in.bodyLen, &at);
        PutAlign(&body, 8);
        int lo = Utf8CharacterForByte(text, node.input->selectedRange.start);
        int hi = Utf8CharacterForByte(text, node.input->selectedRange.end);
        bool exists = selection == 0 && lo != hi;
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
        InputSetSelectedRange(node.input, object.win->app, object.win,
                              Utf8ByteForCharacter(text, lo),
                              Utf8ByteForCharacter(text, hi));
        AppInvalidate(object.win);
        PutBool(&body, true);
        signature = "b";
    } else if (textInterface && StrIs(in.member, "RemoveSelection")) {
        InputUnselect(node.input, object.win->app, object.win);
        AppInvalidate(object.win);
        PutBool(&body, true);
        signature = "b";
    } else if (textInterface && StrIs(in.member, "GetCaretOffset")) {
        PutI32(&body, Utf8CharacterForByte(text, InputCursor(node.input)));
        signature = "i";
    } else if (textInterface && StrIs(in.member, "GetOffsetAtPoint")) {
        int x = ReadI32(in.body, in.bodyLen, &at);
        int y = ReadI32(in.body, in.bodyLen, &at);
        uint32_t coordType = ReadU32(in.body, in.bodyLen, &at);
        Point point = ComponentPointInWindow(object, x, y, coordType);
        int offset = InputIndexForPosition(node.input, &object.win->paint,
                                           point.x, point.y);
        PutI32(&body, Utf8CharacterForByte(text, offset));
        signature = "i";
    } else if (textInterface && StrIs(in.member, "GetCharacterExtents")) {
        (void)ReadI32(in.body, in.bodyLen, &at);
        uint32_t coordType = ReadU32(in.body, in.bodyLen, &at);
        Bounds bounds = ComponentBounds(object, coordType);
        PutAlign(&body, 8);
        PutI32(&body, (int)bounds.x);
        PutI32(&body, (int)bounds.y);
        PutI32(&body, (int)bounds.w);
        PutI32(&body, (int)bounds.h);
        signature = "(iiii)";
    } else if (textInterface && StrIs(in.member, "GetRangeExtents")) {
        (void)ReadI32(in.body, in.bodyLen, &at);
        (void)ReadI32(in.body, in.bodyLen, &at);
        uint32_t coordType = ReadU32(in.body, in.bodyLen, &at);
        Bounds bounds = ComponentBounds(object, coordType);
        PutAlign(&body, 8);
        PutI32(&body, (int)bounds.x);
        PutI32(&body, (int)bounds.y);
        PutI32(&body, (int)bounds.w);
        PutI32(&body, (int)bounds.h);
        signature = "(iiii)";
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
        PutI32(&body, Utf8Characters(text));
        signature = "a{ss}ii";
    } else if (textInterface && StrIs(in.member, "SetCaretOffset")) {
        int offset = (int)ReadU32(in.body, in.bodyLen, &at);
        InputMoveTo(node.input, object.win->app, object.win,
                    Utf8ByteForCharacter(text, offset));
        AppInvalidate(object.win);
        PutBool(&body, true);
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
    SendReply(in.serial, in.sender, signature, &body);
    VecReset(body.bytes);
    return true;
}

static bool SelectionDescendant(const Window* win, int index, int ancestor) {
    int at = win->accessibility[index].parent;
    while (at >= 0 && at < win->accessibility.len) {
        if (at == ancestor) return true;
        if (IsSelectionRole(win->accessibility[at].info.role)) return false;
        at = win->accessibility[at].parent;
    }
    return false;
}

static int SelectedAt(const LinuxAccessible& object, int wanted) {
    for (int i = 0; object.win && i < object.win->accessibility.len; i++) {
        const AccessibilityNode& node = object.win->accessibility[i];
        if (node.info.hasSelected && node.info.selected &&
            SelectionDescendant(object.win, i, object.nodeIndex) &&
            wanted-- == 0) {
            return i;
        }
    }
    return -1;
}

static bool HandleSelection(const Incoming& in, const LinuxAccessible& object) {
    if (!StrIs(in.interfaceName, kSelection) || !object.win ||
        object.nodeIndex < 0) {
        return false;
    }
    DbusWriter body;
    const char* signature = "";
    int at = 0;
    if (StrIs(in.member, "GetNSelectedChildren")) {
        int n = 0;
        while (SelectedAt(object, n) >= 0) n++;
        PutI32(&body, n);
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
        int wi = -1;
        int ni = -1;
        int child = (int)ReadU32(in.body, in.bodyLen, &at);
        bool ok = ChildAt(object, child, &wi, &ni) && wi == object.windowIndex;
        if (ok) {
            ok = WindowAccessibilityPerform(object.win,
                                            object.win->accessibility[ni].id,
                                            AccessibilityAction::Default);
        }
        PutBool(&body, ok);
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
    SendReply(in.serial, in.sender, signature, &body);
    VecReset(body.bytes);
    return true;
}

static void SendEmbed() {
    if (!gA11y.busName.s || gA11y.embedSerial) return;
    DbusWriter body;
    PutObjectRef(&body, Str(kRootPath));
    gA11y.embedSerial =
        SendCall("org.a11y.atspi.Registry", "/org/a11y/atspi/registry",
                 "org.a11y.atspi.Socket", "Embed", "(so)", &body);
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
    } else if (in.replySerial == gA11y.embedSerial) {
        gA11y.ready = true;
    }
}

static void HandleMethod(const Incoming& in) {
    LinuxAccessible object = AccessibleForPath(in.path);
    if (!object.root && (!object.win || object.nodeIndex < 0)) {
        SendError(in.serial, in.sender,
                  "org.freedesktop.DBus.Error.UnknownObject",
                  "accessible object is no longer available");
        return;
    }
    if (HandleProperties(in, object) || HandleAccessible(in, object) ||
        HandleApplication(in, object) || HandleComponent(in, object) ||
        HandleAction(in, object) || HandleValue(in, object) ||
        HandleText(in, object) || HandleSelection(in, object)) {
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
        if (!ParseIncoming(gA11y.rx.els, gA11y.rx.len, &in, &messageLen)) {
            break;
        }
        if (in.type == 1)
            HandleMethod(in);
        else if (in.type == 2)
            HandleReply(in);
        memmove(gA11y.rx.els, gA11y.rx.els + messageLen,
                (size_t)(gA11y.rx.len - messageLen));
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
    gA11y.busAddress = StrDup(busAddress);
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
    StrFree(gA11y.busAddress);
    gA11y.busAddress = {};
    VecReset(gA11y.rx);
}

int AccessibilityLinuxFd() {
    return gA11y.fd;
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

static void SendEvent(Window* win, uint32_t nodeId, const char* member,
                      const char* detail, int detail1) {
    if (!gA11y.ready || !win || !gA11y.app) return;
    int wi = -1;
    for (int i = 0; i < gA11y.app->windows.len; i++) {
        if (gA11y.app->windows[i] == win) {
            wi = i;
            break;
        }
    }
    if (wi < 0) return;
    Str path = nodeId ? (Str)PathForTemp(wi, nodeId) : Str(kRootPath);
    DbusWriter fields;
    DbusWriter body;
    PutHeaderString(&fields, 1, "o", path);
    PutHeaderString(&fields, 2, "s", Str("org.a11y.atspi.Event.Object"));
    PutHeaderString(&fields, 3, "s", Str(member));
    PutHeaderString(&fields, 8, "g", Str("siiva{sv}"));
    PutString(&body, detail);
    PutI32(&body, detail1);
    PutI32(&body, 0);
    PutVariantString(&body, "s", Str{});
    PutEmptyStringDict(&body);
    SendMessage(4, 0, &fields, &body);
    VecReset(fields.bytes);
    VecReset(body.bytes);
}

void AccessibilityLinuxTreeChanged(Window* win) {
    SendEvent(win, 0, "ChildrenChanged", "invalidate", 0);
}

void AccessibilityLinuxFocusChanged(Window* win, int focusId) {
    if (!win || !focusId) return;
    for (int i = 0; i < win->accessibility.len; i++) {
        if (win->accessibility[i].focusId == focusId) {
            SendEvent(win, win->accessibility[i].id, "StateChanged", "focused",
                      1);
            break;
        }
    }
}

} // namespace gpui
