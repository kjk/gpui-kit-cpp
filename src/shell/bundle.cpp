// crates/shell/src/host/bundle.rs. Archives are ustar+gzip and zip. Deflate
// is the stored-block writer in standard.cpp, which every inflater accepts.
// Icon scaling is a box filter; Rust uses Lanczos3. See port-status.md.

#include "base.h"

#include "shell/bundle.h"

#include "shell/dependencies.h"
#include "shell/host.h"
#include "shell/plugin.h"
#include "shell/standard.h"
#include "sys/http.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

namespace gpui::shell {

extern const char kBundleRuntimeSh[];
extern const char kBundleLinuxLauncherSh[];
extern const char kBundleLinuxInstallSh[];
extern const char kBundleMacosLauncherSh[];

static const char* kWindowsTarget = "x86_64-pc-windows-msvc";
static const int kBundleReadLimit = 64 * 1024 * 1024;

static bool RawDeflate(Str input, Str* out, Str* error);

static bool BundleFail(Str* error, Str message) {
    if (error) {
        StrFree(*error);
        *error = message ? StrDup(message) : Str{};
    }
    return false;
}

static Str JoinDisk(Str left, Str right) {
    if (!left || len(left) == 0) return StrDup(right);
    if (!right || len(right) == 0) return StrDup(left);
    char last = left.s[len(left) - 1];
    if (last == '/' || last == '\\') return StrDup(fmt("%s%s", left, right));
    return StrDup(fmt("%s/%s", left, right));
}

static Str ParentPath(Str path) {
    int end = len(path);
    while (end > 0 && (path.s[end - 1] == '/' || path.s[end - 1] == '\\'))
        end--;
    int slash = end;
    while (slash > 0 && path.s[slash - 1] != '/' && path.s[slash - 1] != '\\')
        slash--;
    if (slash <= 0) return StrDup(StrL("."));
    while (slash > 1 && (path.s[slash - 1] == '/' || path.s[slash - 1] == '\\'))
        slash--;
    return StrDup(Str(path.s, slash));
}

static bool ReadFileBytes(Str path, Str* out, Str* error) {
    if (out) *out = {};
    if (!path || len(path) <= 0 || len(path) >= kMaxPath)
        return BundleFail(error,
                          StrL("cannot read: the path is empty or too long"));
    TempStr name = StrDupTemp(path);
    FILE* file = fopen(name.s, "rb");
    if (!file) return BundleFail(error, fmt("cannot read %s", path));
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return BundleFail(error, fmt("cannot read %s", path));
    }
    long size = ftell(file);
    if (size < 0 || size > kBundleReadLimit || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return BundleFail(error, fmt("cannot read %s", path));
    }
    char* buffer = (char*)Alloc(nullptr, (size_t)size + 1);
    if (!buffer) {
        fclose(file);
        return BundleFail(error, StrL("allocating a file failed"));
    }
    size_t got = size ? fread(buffer, 1, (size_t)size, file) : 0;
    fclose(file);
    if (got != (size_t)size) {
        free(buffer);
        return BundleFail(error, fmt("cannot read %s", path));
    }
    buffer[size] = 0;
    if (out) *out = Str(buffer, (int)size);
    return true;
}

static bool WriteFileBytes(Str path, Str bytes, Str* error) {
    if (!path || len(path) <= 0 || len(path) >= kMaxPath)
        return BundleFail(error,
                          StrL("cannot write: the path is empty or too long"));
    TempStr name = StrDupTemp(path);
    FILE* file = fopen(name.s, "wb");
    if (!file) return BundleFail(error, fmt("cannot write %s", path));
    bool ok = len(bytes) == 0 || fwrite(bytes.s, 1, (size_t)len(bytes), file) ==
                                     (size_t)len(bytes);
    if (fclose(file) != 0) ok = false;
    if (!ok) return BundleFail(error, fmt("cannot write %s", path));
    return true;
}

static bool ReplaceFile(Str destination, Str bytes, Str* error) {
    Str partial = StrDup(fmt("%s.partial", destination));
    bool ok = WriteFileBytes(partial, bytes, error);
    if (ok) {
        remove(destination.s);
        TempStr from = StrDupTemp(partial);
        TempStr to = StrDupTemp(destination);
        if (rename(from.s, to.s) != 0) {
            remove(partial.s);
            ok = BundleFail(error, fmt("cannot write %s", destination));
        }
    } else {
        remove(partial.s);
    }
    StrFree(partial);
    return ok;
}

bool BundlePlatformParse(Str value, BundlePlatform* out) {
    if (StrEq(value, StrL("macos"))) {
        if (out) *out = BundlePlatform::MacOS;
        return true;
    }
    if (StrEq(value, StrL("linux"))) {
        if (out) *out = BundlePlatform::Linux;
        return true;
    }
    if (StrEq(value, StrL("windows"))) {
        if (out) *out = BundlePlatform::Windows;
        return true;
    }
    return false;
}

BundlePlatform BundleHostPlatform() {
    Str name = Str(PlatShellPlatformName());
    if (StrEq(name, StrL("macos"))) return BundlePlatform::MacOS;
    if (StrEq(name, StrL("windows"))) return BundlePlatform::Windows;
    return BundlePlatform::Linux;
}

static const char* PlatformName(BundlePlatform platform) {
    if (platform == BundlePlatform::MacOS) return "macos";
    if (platform == BundlePlatform::Windows) return "windows";
    return "linux";
}

static bool IsAsciiAlnum(unsigned char c) {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z');
}

static char ToAsciiLower(char c) {
    if (c >= 'A' && c <= 'Z') return (char)(c - 'A' + 'a');
    return c;
}

static Str Slug(Str value) {
    StrBuilder out;
    bool word = false;
    for (int i = 0; i < len(value); i++) {
        unsigned char c = (unsigned char)value.s[i];
        if (c >= 0x80) {
            word = false;
            continue;
        }
        if (IsAsciiAlnum(c)) {
            if (word == false && len(out) > 0) out.AppendChar('-');
            out.AppendChar(ToAsciiLower((char)c));
            word = true;
        } else {
            word = false;
        }
    }
    return out.TakeStr();
}

static Str LastIdPart(Str id) {
    int start = 0;
    for (int i = 0; i < len(id); i++)
        if (id.s[i] == '.') start = i + 1;
    return Str(id.s + start, len(id) - start);
}

static Str CommandName(const PluginManifest& manifest) {
    Str fromName = Slug(manifest.name);
    if (len(fromName) > 0) return fromName;
    StrFree(fromName);
    Str fromId = Slug(LastIdPart(manifest.id));
    if (len(fromId) > 0) return fromId;
    StrFree(fromId);
    return StrDup(StrL("app"));
}

bool BundleFileName(Str name, Str* out) {
    if (out) *out = {};
    StrBuilder cleaned;
    for (int i = 0; i < len(name);) {
        unsigned char c = (unsigned char)name.s[i];
        if (c < 0x80) {
            bool banned = c < 0x20 || c == 0x7f || c == '/' || c == '\\' ||
                          c == ':' || c == '*' || c == '?' || c == '"' ||
                          c == '<' || c == '>' || c == '|';
            cleaned.AppendChar(banned ? '-' : (char)c);
            i++;
        } else {
            int n = 1;
            if ((c & 0xe0) == 0xc0)
                n = 2;
            else if ((c & 0xf0) == 0xe0)
                n = 3;
            else if ((c & 0xf8) == 0xf0)
                n = 4;
            if (i + n > len(name)) n = 1;
            cleaned.Append(Str(name.s + i, n));
            i += n;
        }
    }
    Str text = cleaned.TakeStr();
    int begin = 0;
    int end = len(text);
    while (begin < end && (text.s[begin] == ' ' || text.s[begin] == '\t' ||
                           text.s[begin] == '\n' || text.s[begin] == '\r'))
        begin++;
    while (end > begin && (text.s[end - 1] == ' ' || text.s[end - 1] == '\t' ||
                           text.s[end - 1] == '\n' || text.s[end - 1] == '\r' ||
                           text.s[end - 1] == '.'))
        end--;
    while (end > begin && (text.s[end - 1] == ' ' || text.s[end - 1] == '\t'))
        end--;
    bool keep = end > begin;
    if (keep && out) *out = StrDup(Str(text.s + begin, end - begin));
    StrFree(text);
    return keep;
}

bool BundleNamesFrom(const PluginManifest& manifest, BundlePlatform platform,
                     BundleNames* out) {
    if (!out) return false;
    *out = {};
    out->command = CommandName(manifest);
    if (!BundleFileName(manifest.name, &out->display)) {
        StrFree(out->display);
        out->display = StrDup(out->command);
    }
    out->appBundle = StrDup(fmt("%s.app", out->display));
    out->archiveStem = StrDup(fmt("%s-%s-%s", out->command, manifest.version,
                                  Str(PlatformName(platform))));
    return true;
}

static Str XmlEscape(Str value) {
    StrBuilder out;
    for (int i = 0; i < len(value); i++) {
        char c = value.s[i];
        if (c == '&')
            out.Append(StrL("&amp;"));
        else if (c == '<')
            out.Append(StrL("&lt;"));
        else if (c == '>')
            out.Append(StrL("&gt;"));
        else if (c == '"')
            out.Append(StrL("&quot;"));
        else
            out.AppendChar(c);
    }
    return out.TakeStr();
}

static Str BundleVersion(Str version) {
    int parts[3] = {};
    int count = 0;
    int i = 0;
    while (count < 3 && i < len(version)) {
        if (version.s[i] < '0' || version.s[i] > '9') return StrDup(version);
        int value = 0;
        while (i < len(version) && version.s[i] >= '0' && version.s[i] <= '9') {
            value = value * 10 + (version.s[i] - '0');
            i++;
        }
        parts[count++] = value;
        if (count == 3) break;
        if (i >= len(version) || version.s[i] != '.') return StrDup(version);
        i++;
    }
    if (count != 3) return StrDup(version);
    return StrDup(fmt("%d.%d.%d", parts[0], parts[1], parts[2]));
}

struct PlistEntry {
    const char* key;
    Str value;
};

Str BundleInfoPlist(const PluginManifest& manifest, const BundleNames& names,
                    bool hasIcon) {
    Str build = BundleVersion(manifest.version);
    PlistEntry entries[12];
    int n = 0;
    entries[n++] = {"CFBundleDevelopmentRegion", StrDup(StrL("en"))};
    entries[n++] = {"CFBundleDisplayName", StrDup(manifest.name)};
    entries[n++] = {"CFBundleExecutable", StrDup(names.display)};
    entries[n++] = {"CFBundleIdentifier", StrDup(manifest.id)};
    entries[n++] = {"CFBundleInfoDictionaryVersion", StrDup(StrL("6.0"))};
    entries[n++] = {"CFBundleName", StrDup(manifest.name)};
    entries[n++] = {"CFBundlePackageType", StrDup(StrL("APPL"))};
    entries[n++] = {"CFBundleShortVersionString", StrDup(manifest.version)};
    entries[n++] = {"CFBundleVersion", build};
    entries[n++] = {"LSMinimumSystemVersion", StrDup(StrL("11.0"))};
    if (hasIcon) entries[n++] = {"CFBundleIconFile", StrDup(names.display)};
    for (int i = 1; i < n; i++) {
        PlistEntry item = entries[i];
        int j = i;
        while (j > 0 && strcmp(entries[j - 1].key, item.key) > 0) {
            entries[j] = entries[j - 1];
            j--;
        }
        entries[j] = item;
    }
    StrBuilder plist;
    plist
        .Append(StrL("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                     "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
                     "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
                     "<plist version=\"1.0\">\n<dict>\n"));
    for (int i = 0; i < n; i++) {
        Str escaped = XmlEscape(entries[i].value);
        plist.Append(fmt("  <key>%s</key>\n  <string>%s</string>\n",
                         Str(entries[i].key), escaped));
        StrFree(escaped);
        StrFree(entries[i].value);
    }
    plist
        .Append(StrL("  <key>NSHighResolutionCapable</key>\n  "
                     "<true/>\n</dict>\n</plist>\n"));
    return plist.TakeStr();
}

static Str ShellQuote(Str value) {
    StrBuilder out;
    out.AppendChar('\'');
    for (int i = 0; i < len(value); i++) {
        if (value.s[i] == '\'')
            out.Append(StrL("'\\''"));
        else
            out.AppendChar(value.s[i]);
    }
    out.AppendChar('\'');
    return out.TakeStr();
}

static bool PlaceholderName(Str text, int at, int* nameLen) {
    int i = at;
    while (i < len(text)) {
        unsigned char c = (unsigned char)text.s[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'))
            break;
        i++;
    }
    *nameLen = i - at;
    return i < len(text) && text.s[i] == '@';
}

static Str ScriptValue(const BundleScriptValues& values, Str name) {
    if (StrEq(name, StrL("ID"))) return values.id;
    if (StrEq(name, StrL("NAME"))) return values.name;
    if (StrEq(name, StrL("COMMAND"))) return values.command;
    if (StrEq(name, StrL("APP_VERSION"))) return values.appVersion;
    if (StrEq(name, StrL("RUNTIME_VERSION"))) return values.runtimeVersion;
    if (StrEq(name, StrL("ICON_SIZE"))) return values.iconSize;
    if (StrEq(name, StrL("ARCHIVE"))) return values.archive;
    if (StrEq(name, StrL("ARCHIVE_SHA256"))) return values.archiveSha256;
    if (StrEq(name, StrL("DOWNLOAD_URL"))) return values.downloadUrl;
    if (StrEq(name, StrL("STEM"))) return values.stem;
    return {};
}

Str BundleShellScript(Str templ, const BundleScriptValues& values) {
    StrBuilder out;
    int i = 0;
    while (i < len(templ)) {
        if (templ.s[i] != '@') {
            out.AppendChar(templ.s[i]);
            i++;
            continue;
        }
        int nameLen = 0;
        if (!PlaceholderName(templ, i + 1, &nameLen)) {
            out.AppendChar('@');
            i++;
            continue;
        }
        Str name = Str(templ.s + i + 1, nameLen);
        if (StrEq(name, StrL("RUNTIME_FUNCTIONS"))) {
            Str nested = BundleShellScript(Str(kBundleRuntimeSh), values);
            out.Append(nested);
            StrFree(nested);
            i += nameLen + 2;
            continue;
        }
        Str raw = ScriptValue(values, name);
        if (!raw.s && len(raw) == 0 && !StrEq(name, StrL("ICON_SIZE")) &&
            !StrEq(name, StrL("ARCHIVE")) &&
            !StrEq(name, StrL("DOWNLOAD_URL")) &&
            !StrEq(name, StrL("ARCHIVE_SHA256"))) {
            // An unknown placeholder is left as written, @ included.
            bool known = false;
            const char* keys[] = {"ID",
                                  "NAME",
                                  "COMMAND",
                                  "APP_VERSION",
                                  "RUNTIME_VERSION",
                                  "ICON_SIZE",
                                  "ARCHIVE",
                                  "ARCHIVE_SHA256",
                                  "DOWNLOAD_URL",
                                  "STEM"};
            for (const char* key : keys)
                if (StrEq(name, Str(key))) known = true;
            if (!known) {
                out.AppendChar('@');
                i++;
                continue;
            }
        }
        Str quoted = ShellQuote(raw.s ? raw : Str{});
        out.Append(quoted);
        StrFree(quoted);
        i += nameLen + 2;
    }
    return out.TakeStr();
}

static uint32_t Crc32Bytes(const uint8_t* data, int n) {
    static uint32_t table[256];
    static int ready = 0;
    if (!ready) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++)
                c = (c & 1u) ? (0xedb88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        ready = 1;
    }
    uint32_t crc = 0xffffffffu;
    for (int i = 0; i < n; i++)
        crc = table[(crc ^ data[i]) & 0xffu] ^ (crc >> 8);
    return crc ^ 0xffffffffu;
}

static void AppendBe32(StrBuilder* out, uint32_t value) {
    out->AppendChar((char)(value >> 24));
    out->AppendChar((char)(value >> 16));
    out->AppendChar((char)(value >> 8));
    out->AppendChar((char)value);
}

static void AppendPngChunk(StrBuilder* file, const char* type,
                           const uint8_t* data, int n) {
    AppendBe32(file, (uint32_t)n);
    file->Append(Str(type, 4));
    if (n) file->Append(Str((const char*)data, n));
    StrBuilder crcIn;
    crcIn.Append(Str(type, 4));
    if (n) crcIn.Append(Str((const char*)data, n));
    Str crcBytes = crcIn.TakeStr();
    uint32_t crc = Crc32Bytes((const uint8_t*)crcBytes.s, len(crcBytes));
    StrFree(crcBytes);
    AppendBe32(file, crc);
}

static bool PngEncode(const uint8_t* rgba, int size, Str* out, Str* error) {
    int stride = size * 4;
    int rawLen = size * (1 + stride);
    char* raw = (char*)Alloc(nullptr, (size_t)rawLen + 1);
    if (!raw) return BundleFail(error, StrL("allocating an icon failed"));
    for (int y = 0; y < size; y++) {
        raw[y * (1 + stride)] = 0;
        memcpy(raw + y * (1 + stride) + 1, rgba + y * stride, (size_t)stride);
    }
    raw[rawLen] = 0;
    Str compressed;
    // IDAT is a raw deflate stream. ZlibDeflate wraps one; RawDeflate strips
    // it.
    bool ok = RawDeflate(Str(raw, rawLen), &compressed, error);
    free(raw);
    if (!ok) return false;
    uint8_t ihdr[13];
    ihdr[0] = (uint8_t)(size >> 24);
    ihdr[1] = (uint8_t)(size >> 16);
    ihdr[2] = (uint8_t)(size >> 8);
    ihdr[3] = (uint8_t)size;
    memcpy(ihdr + 4, ihdr, 4);
    ihdr[8] = 8;
    ihdr[9] = 6;
    ihdr[10] = 0;
    ihdr[11] = 0;
    ihdr[12] = 0;
    StrBuilder file;
    const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    file.Append(Str((const char*)sig, 8));
    AppendPngChunk(&file, "IHDR", ihdr, 13);
    AppendPngChunk(&file, "IDAT", (const uint8_t*)compressed.s,
                   len(compressed));
    AppendPngChunk(&file, "IEND", nullptr, 0);
    StrFree(compressed);
    if (out) *out = file.TakeStr();
    return true;
}

static void BoxScale(const uint8_t* src, int srcN, uint8_t* dst, int dstN) {
    if (srcN == dstN) {
        memcpy(dst, src, (size_t)srcN * srcN * 4);
        return;
    }
    for (int y = 0; y < dstN; y++) {
        int y0 = y * srcN / dstN;
        int y1 = (y + 1) * srcN / dstN;
        if (y1 <= y0) y1 = y0 + 1;
        for (int x = 0; x < dstN; x++) {
            int x0 = x * srcN / dstN;
            int x1 = (x + 1) * srcN / dstN;
            if (x1 <= x0) x1 = x0 + 1;
            uint32_t sum[4] = {};
            int count = 0;
            for (int yy = y0; yy < y1; yy++) {
                for (int xx = x0; xx < x1; xx++) {
                    const uint8_t* p = src + ((yy * srcN + xx) * 4);
                    for (int c = 0; c < 4; c++) sum[c] += p[c];
                    count++;
                }
            }
            uint8_t* d = dst + ((y * dstN + x) * 4);
            for (int c = 0; c < 4; c++)
                d[c] = (uint8_t)(sum[c] / (uint32_t)count);
        }
    }
}

bool BundleWriteIcns(const uint8_t* rgba, int width, Str* out, Str* error) {
    if (out) *out = {};
    if (!rgba || width < 16)
        return BundleFail(error, StrL("the icon must be a square PNG, ideally "
                                      "1024×1024"));
    struct Kind {
        int size;
        char type[4];
    };
    const Kind kinds[] = {
        {16, {'i', 'c', 'p', '4'}},   {32, {'i', 'c', 'p', '5'}},
        {64, {'i', 'c', 'p', '6'}},   {128, {'i', 'c', '0', '7'}},
        {256, {'i', 'c', '0', '8'}},  {512, {'i', 'c', '0', '9'}},
        {1024, {'i', 'c', '1', '0'}},
    };
    StrBuilder body;
    uint8_t* scaled = (uint8_t*)Alloc(nullptr, (size_t)width * width * 4);
    if (!scaled) return BundleFail(error, StrL("allocating an icon failed"));
    for (const Kind& kind : kinds) {
        if (kind.size > width) continue;
        BoxScale(rgba, width, scaled, kind.size);
        Str png;
        if (!PngEncode(scaled, kind.size, &png, error)) {
            free(scaled);
            return false;
        }
        body.Append(Str(kind.type, 4));
        AppendBe32(&body, (uint32_t)len(png) + 8);
        body.Append(png);
        StrFree(png);
    }
    free(scaled);
    StrBuilder file;
    file.Append(StrL("icns"));
    Str bodyBytes = body.TakeStr();
    AppendBe32(&file, (uint32_t)len(bodyBytes) + 8);
    file.Append(bodyBytes);
    StrFree(bodyBytes);
    if (out) *out = file.TakeStr();
    return true;
}

static uint32_t Be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static uint8_t Paeth(uint8_t a, uint8_t b, uint8_t c) {
    int p = (int)a + (int)b - (int)c;
    int pa = abs(p - (int)a);
    int pb = abs(p - (int)b);
    int pc = abs(p - (int)c);
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

static bool PngDecode(Str bytes, Str* rgba, int* width, int* height,
                      Str* error) {
    if (rgba) *rgba = {};
    const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (len(bytes) < 8 || memcmp(bytes.s, sig, 8) != 0)
        return BundleFail(error, StrL("the icon is not a PNG image"));
    int w = 0, h = 0, depth = 0, color = 0, interlace = 0;
    bool have = false;
    StrBuilder idat;
    int at = 8;
    while (at + 12 <= len(bytes)) {
        uint32_t length = Be32((const uint8_t*)bytes.s + at);
        if (at + 12 + (int)length > len(bytes))
            return BundleFail(error, StrL("the icon is not a PNG image"));
        const char* type = bytes.s + at + 4;
        const uint8_t* data = (const uint8_t*)bytes.s + at + 8;
        if (memcmp(type, "IHDR", 4) == 0) {
            if (length < 13)
                return BundleFail(error, StrL("the icon is not a PNG image"));
            w = (int)Be32(data);
            h = (int)Be32(data + 4);
            depth = data[8];
            color = data[9];
            interlace = data[12];
            have = true;
        } else if (memcmp(type, "IDAT", 4) == 0) {
            idat.Append(Str((const char*)data, (int)length));
        } else if (memcmp(type, "IEND", 4) == 0) {
            break;
        }
        at += 12 + (int)length;
    }
    if (!have || w <= 0 || h <= 0 || depth != 8 || interlace != 0 ||
        (color != 0 && color != 2 && color != 4 && color != 6))
        return BundleFail(error, StrL("the icon is not an 8-bit RGB PNG"));
    int channels = color == 0 ? 1 : color == 2 ? 3 : color == 4 ? 2 : 4;
    int stride = w * channels;
    int rawLen = h * (1 + stride);
    Str compressed = idat.TakeStr();
    Str inflated;
    if (!ZlibInflate(compressed, false, &inflated, error) ||
        len(inflated) < rawLen) {
        StrFree(compressed);
        StrFree(inflated);
        if (error && !*error)
            BundleFail(error, StrL("the icon is not a PNG image"));
        return false;
    }
    StrFree(compressed);
    uint8_t* raw = (uint8_t*)inflated.s;
    for (int y = 0; y < h; y++) {
        uint8_t* row = raw + y * (1 + stride);
        uint8_t filter = row[0];
        uint8_t* prev = y ? raw + (y - 1) * (1 + stride) + 1 : nullptr;
        uint8_t* pix = row + 1;
        for (int x = 0; x < stride; x++) {
            uint8_t left = x >= channels ? pix[x - channels] : 0;
            uint8_t up = prev ? prev[x] : 0;
            uint8_t ul = prev && x >= channels ? prev[x - channels] : 0;
            if (filter == 1)
                pix[x] = (uint8_t)(pix[x] + left);
            else if (filter == 2)
                pix[x] = (uint8_t)(pix[x] + up);
            else if (filter == 3)
                pix[x] = (uint8_t)(pix[x] + ((int)left + (int)up) / 2);
            else if (filter == 4)
                pix[x] = (uint8_t)(pix[x] + Paeth(left, up, ul));
            else if (filter != 0) {
                StrFree(inflated);
                return BundleFail(error, StrL("the icon is not a PNG image"));
            }
        }
    }
    char* pixels = (char*)Alloc(nullptr, (size_t)w * h * 4 + 1);
    if (!pixels) {
        StrFree(inflated);
        return BundleFail(error, StrL("allocating an icon failed"));
    }
    for (int y = 0; y < h; y++) {
        const uint8_t* pix = raw + y * (1 + stride) + 1;
        for (int x = 0; x < w; x++) {
            uint8_t* d = (uint8_t*)pixels + ((y * w + x) * 4);
            const uint8_t* s = pix + x * channels;
            if (channels == 4) {
                memcpy(d, s, 4);
            } else if (channels == 3) {
                d[0] = s[0];
                d[1] = s[1];
                d[2] = s[2];
                d[3] = 255;
            } else if (channels == 2) {
                d[0] = d[1] = d[2] = s[0];
                d[3] = s[1];
            } else {
                d[0] = d[1] = d[2] = s[0];
                d[3] = 255;
            }
        }
    }
    pixels[w * h * 4] = 0;
    StrFree(inflated);
    if (rgba)
        *rgba = Str(pixels, w * h * 4);
    else
        free(pixels);
    if (width) *width = w;
    if (height) *height = h;
    return true;
}

struct BundleItem {
    Str path;
    Str bytes;
    bool directory = false;
    bool executable = false;
};

struct BundleTree {
    Vec<BundleItem> items;

    void Free() {
        for (int i = 0; i < len(items); i++) {
            StrFree(items.els[i].path);
            StrFree(items.els[i].bytes);
        }
        VecReset(items);
    }

    bool Has(Str path) const {
        for (int i = 0; i < len(items); i++)
            if (StrEq(items.els[i].path, path)) return true;
        return false;
    }

    bool Directory(Str path) {
        if (len(path) == 0 || Has(path)) return true;
        int slash = -1;
        for (int i = 0; i < len(path); i++)
            if (path.s[i] == '/') slash = i;
        if (slash > 0 && !Directory(Str(path.s, slash))) return false;
        BundleItem item;
        item.path = StrDup(path);
        item.directory = true;
        item.executable = true;
        return VecAppend(items, item);
    }

    bool Add(Str path, Str bytes, bool executable, bool directory) {
        int slash = -1;
        for (int i = 0; i < len(path); i++)
            if (path.s[i] == '/') slash = i;
        if (slash > 0 && !Directory(Str(path.s, slash))) return false;
        BundleItem item;
        item.path = StrDup(path);
        item.bytes = bytes;
        item.directory = directory;
        item.executable = executable;
        return VecAppend(items, item);
    }
};

static bool AddDirectory(BundleTree* tree, Str directory, Str prefix,
                         Str exclude, bool isRoot, Vec<Str>* visited,
                         Str* error);

static bool Canonical(Str path, Str* out, Str* error) {
    if (out) *out = {};
    char buffer[kMaxPath];
    buffer[0] = 0;
    TempStr raw = StrDupTemp(path);
    if (!raw || !PlatCanonicalPath(raw.s, buffer, kMaxPath))
        return BundleFail(error, fmt("cannot read %s", path));
    if (out) *out = StrDup(Str(buffer));
    return true;
}

static int CmpEntry(const void* a, const void* b) {
    return strcmp(((const DirEntry*)a)->name, ((const DirEntry*)b)->name);
}

static bool LeftOut(Str name, bool isRoot) {
    if (len(name) == 0 || name.s[0] == '.') return true;
    if (StrEq(name, StrL("node_modules"))) return true;
    if (StrEq(name, StrL("gpui-kit.d.ts"))) return true;
    if (isRoot &&
        (StrEq(name, StrL("jsconfig.json")) || StrEq(name, StrL("AGENTS.md"))))
        return true;
    return false;
}

static bool AddDirectory(BundleTree* tree, Str directory, Str prefix,
                         Str exclude, bool isRoot, Vec<Str>* visited,
                         Str* error) {
    Str canonical;
    if (!Canonical(directory, &canonical, error)) return false;
    for (int i = 0; i < len(*visited); i++) {
        if (StrEq(visited->els[i], canonical)) {
            StrFree(canonical);
            return true;
        }
    }
    VecAppend(*visited, canonical);
    Vec<DirEntry> listed;
    if (!VecResize(listed, 4096)) {
        VecReset(listed);
        return BundleFail(error, fmt("cannot read %s", directory));
    }
    int n = PlatListDir(directory.s, listed.els, 4096);
    if (n < 0) n = 0;
    if (n >= 4096) {
        VecReset(listed);
        return BundleFail(error, fmt("cannot read %s", directory));
    }
    qsort(listed.els, (size_t)n, sizeof(DirEntry), CmpEntry);
    for (int i = 0; i < n; i++) {
        Str name = Str(listed.els[i].name);
        Str path = JoinDisk(directory, name);
        if (LeftOut(name, isRoot)) {
            StrFree(path);
            continue;
        }
        Str canon;
        bool excluded = false;
        if (Canonical(path, &canon, nullptr) && exclude &&
            StrEq(canon, exclude))
            excluded = true;
        StrFree(canon);
        if (excluded) {
            StrFree(path);
            continue;
        }
        bool dir = listed.els[i].isDir;
        bool file = listed.els[i].isFile;
        if (listed.els[i].isSymlink) {
            dir = PlatDirExists(path.s);
            file = PlatFileExists(path.s);
        }
        Str inner = StrDup(fmt("%s/%s", prefix, name));
        if (dir) {
            if (!tree->Directory(inner) ||
                !AddDirectory(tree, path, inner, exclude, false, visited,
                              error)) {
                StrFree(inner);
                StrFree(path);
                VecReset(listed);
                return false;
            }
        } else if (file) {
            Str bytes;
            if (!ReadFileBytes(path, &bytes, error) ||
                !tree->Add(inner, bytes, PlatFileIsExecutable(path.s), false)) {
                StrFree(bytes);
                StrFree(inner);
                StrFree(path);
                VecReset(listed);
                return false;
            }
        }
        StrFree(inner);
        StrFree(path);
    }
    VecReset(listed);
    return true;
}

static bool AddApplication(BundleTree* tree, Str root, Str prefix, Str exclude,
                           Str* error) {
    if (!tree->Directory(prefix))
        return BundleFail(error, StrL("the bundle is empty"));
    Vec<Str> visited;
    bool ok = AddDirectory(tree, root, prefix, exclude, true, &visited, error);
    for (int i = 0; i < len(visited); i++) StrFree(visited.els[i]);
    VecReset(visited);
    return ok;
}

bool BundleApplicationPaths(Str root, Str prefix, Str exclude, Str* paths,
                            Str* error) {
    if (paths) *paths = {};
    BundleTree tree;
    if (!AddApplication(&tree, root, prefix, exclude, error)) {
        tree.Free();
        return false;
    }
    StrBuilder out;
    for (int i = 0; i < len(tree.items); i++) {
        if (i) out.AppendChar('\n');
        out.Append(tree.items.els[i].path);
    }
    tree.Free();
    if (paths) *paths = out.TakeStr();
    return true;
}

static void PutOctal(char* field, int width, uint64_t value) {
    memset(field, '0', (size_t)width);
    field[width - 1] = 0;
    for (int i = width - 2; i >= 0; i--) {
        field[i] = (char)('0' + (value & 7u));
        value >>= 3;
    }
}

static bool WriteTar(const BundleTree& tree, Str* out, Str* error) {
    StrBuilder archive;
    uint64_t mtime = (uint64_t)time(nullptr);
    for (int i = 0; i < len(tree.items); i++) {
        const BundleItem& item = tree.items.els[i];
        StrBuilder name;
        name.Append(item.path);
        if (item.directory) name.AppendChar('/');
        Str shown = name.TakeStr();
        if (len(shown) > 100) {
            StrFree(shown);
            return BundleFail(error,
                              StrL("a bundled path is too long for a tar "
                                   "archive"));
        }
        char header[512];
        memset(header, 0, sizeof(header));
        memcpy(header, shown.s, (size_t)len(shown));
        StrFree(shown);
        uint32_t mode = item.executable ? 0755u : 0644u;
        PutOctal(header + 100, 8, mode);
        PutOctal(header + 108, 8, 0);
        PutOctal(header + 116, 8, 0);
        PutOctal(header + 124, 12,
                 item.directory ? 0 : (uint64_t)len(item.bytes));
        PutOctal(header + 136, 12, mtime);
        memset(header + 148, ' ', 8);
        header[156] = item.directory ? '5' : '0';
        memcpy(header + 257, "ustar", 5);
        header[263] = '0';
        header[264] = '0';
        unsigned sum = 0;
        for (int b = 0; b < 512; b++) sum += (unsigned char)header[b];
        snprintf(header + 148, 8, "%06o", sum);
        header[154] = 0;
        header[155] = ' ';
        archive.Append(Str(header, 512));
        if (!item.directory && len(item.bytes) > 0) archive.Append(item.bytes);
        int padded = item.directory ? 0 : (len(item.bytes) + 511) & ~511;
        int extra = padded - (item.directory ? 0 : len(item.bytes));
        for (int z = 0; z < extra; z++) archive.AppendChar(0);
    }
    for (int z = 0; z < 1024; z++) archive.AppendChar(0);
    Str raw = archive.TakeStr();
    bool ok = ZlibDeflate(raw, true, out, error);
    StrFree(raw);
    return ok;
}

static void AppendLe16(StrBuilder* out, uint16_t value) {
    out->AppendChar((char)value);
    out->AppendChar((char)(value >> 8));
}

static void AppendLe32(StrBuilder* out, uint32_t value) {
    out->AppendChar((char)value);
    out->AppendChar((char)(value >> 8));
    out->AppendChar((char)(value >> 16));
    out->AppendChar((char)(value >> 24));
}

static bool RawDeflate(Str input, Str* out, Str* error) {
    Str wrapped;
    if (!ZlibDeflate(input, false, &wrapped, error)) return false;
    if (len(wrapped) < 6) {
        StrFree(wrapped);
        return BundleFail(error, StrL("deflate produced an empty stream"));
    }
    int n = len(wrapped) - 6;
    char* raw = (char*)Alloc(nullptr, (size_t)n + 1);
    if (!raw) {
        StrFree(wrapped);
        return BundleFail(error, StrL("allocating a zip entry failed"));
    }
    memcpy(raw, wrapped.s + 2, (size_t)n);
    raw[n] = 0;
    StrFree(wrapped);
    *out = Str(raw, n);
    return true;
}

static void DosStamp(uint16_t* dosTime, uint16_t* dosDate) {
    time_t now = time(nullptr);
    // UTC. The archive tests do not read the stamp.
    struct tm* stamp = gmtime(&now);
    if (!stamp || stamp->tm_year + 1900 < 1980) {
        *dosTime = 0;
        *dosDate = (uint16_t)((1 << 5) | 1);
        return;
    }
    *dosTime = (uint16_t)((stamp->tm_hour << 11) | (stamp->tm_min << 5) |
                          (stamp->tm_sec / 2));
    *dosDate = (uint16_t)(((stamp->tm_year + 1900 - 1980) << 9) |
                          ((stamp->tm_mon + 1) << 5) | stamp->tm_mday);
}

static bool WriteZip(const BundleTree& tree, Str* out, Str* error) {
    StrBuilder file;
    StrBuilder central;
    uint16_t dosTime = 0, dosDate = 0;
    DosStamp(&dosTime, &dosDate);
    uint32_t offset = 0;
    for (int i = 0; i < len(tree.items); i++) {
        const BundleItem& item = tree.items.els[i];
        StrBuilder nameBuf;
        nameBuf.Append(item.path);
        if (item.directory) nameBuf.AppendChar('/');
        Str name = nameBuf.TakeStr();
        if (len(name) > 65535) {
            StrFree(name);
            return BundleFail(
                error, StrL("the bundle is too large for a zip archive"));
        }
        Str stored;
        uint16_t method = 0;
        if (!item.directory) {
            method = 8;
            if (!RawDeflate(item.bytes.s ? item.bytes : Str{}, &stored,
                            error)) {
                StrFree(name);
                return false;
            }
        }
        uint32_t crc =
            item.directory
                ? 0
                : Crc32Bytes((const uint8_t*)(item.bytes.s ? item.bytes.s : ""),
                             len(item.bytes));
        uint16_t flags = 0x0800;
        StrBuilder local;
        AppendLe32(&local, 0x04034b50u);
        AppendLe16(&local, 20);
        AppendLe16(&local, flags);
        AppendLe16(&local, method);
        AppendLe16(&local, dosTime);
        AppendLe16(&local, dosDate);
        AppendLe32(&local, crc);
        AppendLe32(&local, (uint32_t)len(stored));
        AppendLe32(&local, item.directory ? 0 : (uint32_t)len(item.bytes));
        AppendLe16(&local, (uint16_t)len(name));
        AppendLe16(&local, 0);
        local.Append(name);
        Str localBytes = local.TakeStr();
        file.Append(localBytes);
        if (len(stored)) file.Append(stored);
        uint32_t fileType = item.directory ? 040000u : 0100000u;
        uint32_t mode = item.executable ? 0755u : 0644u;
        uint32_t attributes =
            ((fileType | mode) << 16) | (item.directory ? 0x10u : 0);
        AppendLe32(&central, 0x02014b50u);
        AppendLe16(&central, (uint16_t)((3u << 8) | 20));
        AppendLe16(&central, 20);
        AppendLe16(&central, flags);
        AppendLe16(&central, method);
        AppendLe16(&central, dosTime);
        AppendLe16(&central, dosDate);
        AppendLe32(&central, crc);
        AppendLe32(&central, (uint32_t)len(stored));
        AppendLe32(&central, item.directory ? 0 : (uint32_t)len(item.bytes));
        AppendLe16(&central, (uint16_t)len(name));
        for (int z = 0; z < 8; z++) central.AppendChar(0);
        AppendLe32(&central, attributes);
        AppendLe32(&central, offset);
        central.Append(name);
        offset += (uint32_t)len(localBytes) + (uint32_t)len(stored);
        StrFree(localBytes);
        StrFree(stored);
        StrFree(name);
    }
    Str centralBytes = central.TakeStr();
    file.Append(centralBytes);
    AppendLe32(&file, 0x06054b50u);
    AppendLe16(&file, 0);
    AppendLe16(&file, 0);
    AppendLe16(&file, (uint16_t)len(tree.items));
    AppendLe16(&file, (uint16_t)len(tree.items));
    AppendLe32(&file, (uint32_t)len(centralBytes));
    AppendLe32(&file, offset);
    AppendLe16(&file, 0);
    StrFree(centralBytes);
    if (out) *out = file.TakeStr();
    return true;
}

static bool U16At(Str archive, int at, int* out, Str* error) {
    if (at < 0 || at + 2 > len(archive))
        return BundleFail(error, StrL("the zip archive is truncated"));
    *out =
        (unsigned char)archive.s[at] | ((unsigned char)archive.s[at + 1] << 8);
    return true;
}

static bool U32At(Str archive, int at, uint32_t* out, Str* error) {
    if (at < 0 || at + 4 > len(archive))
        return BundleFail(error, StrL("the zip archive is truncated"));
    *out = (unsigned char)archive.s[at] |
           ((unsigned char)archive.s[at + 1] << 8) |
           ((unsigned char)archive.s[at + 2] << 16) |
           ((unsigned char)archive.s[at + 3] << 24);
    return true;
}

bool BundleZipEntry(Str archive, Str name, Str* out, Str* error) {
    if (out) *out = {};
    int end = -1;
    int last = len(archive) - 22;
    int stop = last - 65557;
    if (stop < 0) stop = 0;
    for (int at = last; at >= stop; at--) {
        if ((unsigned char)archive.s[at] == 0x50 &&
            (unsigned char)archive.s[at + 1] == 0x4b &&
            (unsigned char)archive.s[at + 2] == 0x05 &&
            (unsigned char)archive.s[at + 3] == 0x06) {
            end = at;
            break;
        }
    }
    if (end < 0) return BundleFail(error, StrL("not a zip archive"));
    int count = 0;
    uint32_t at = 0;
    if (!U16At(archive, end + 10, &count, error) ||
        !U32At(archive, end + 16, &at, error))
        return false;
    for (int i = 0; i < count; i++) {
        uint32_t sig = 0;
        if (!U32At(archive, (int)at, &sig, error)) return false;
        if (sig != 0x02014b50u)
            return BundleFail(error,
                              StrL("the zip archive's directory is damaged"));
        int method = 0, compressed = 0, size = 0, nameLen = 0, extra = 0,
            comment = 0;
        uint32_t compressed32 = 0, size32 = 0, local = 0;
        if (!U16At(archive, (int)at + 10, &method, error) ||
            !U32At(archive, (int)at + 20, &compressed32, error) ||
            !U32At(archive, (int)at + 24, &size32, error) ||
            !U16At(archive, (int)at + 28, &nameLen, error) ||
            !U16At(archive, (int)at + 30, &extra, error) ||
            !U16At(archive, (int)at + 32, &comment, error) ||
            !U32At(archive, (int)at + 42, &local, error))
            return false;
        compressed = (int)compressed32;
        size = (int)size32;
        if ((int)at + 46 + nameLen > len(archive))
            return BundleFail(error, StrL("the zip archive is truncated"));
        Str entry = Str(archive.s + at + 46, nameLen);
        if (StrEq(entry, name)) {
            uint32_t localSig = 0;
            if (!U32At(archive, (int)local, &localSig, error)) return false;
            if (localSig != 0x04034b50u)
                return BundleFail(error,
                                  fmt("the zip archive's entry for %s is "
                                      "damaged",
                                      name));
            int localName = 0, localExtra = 0;
            if (!U16At(archive, (int)local + 26, &localName, error) ||
                !U16At(archive, (int)local + 28, &localExtra, error))
                return false;
            int data = (int)local + 30 + localName + localExtra;
            if (data + compressed > len(archive))
                return BundleFail(error, StrL("the zip archive is truncated"));
            Str stored = Str(archive.s + data, compressed);
            Str contents;
            if (method == 0) {
                contents = StrDup(stored);
            } else if (method == 8) {
                if (!ZlibInflateRaw(stored, &contents, error)) return false;
            } else {
                return BundleFail(error,
                                  fmt("%s is compressed with method %d, which "
                                      "is not supported",
                                      name, method));
            }
            if (len(contents) != size) {
                StrFree(contents);
                return BundleFail(
                    error, fmt("%s in the zip archive is damaged", name));
            }
            if (out)
                *out = contents;
            else
                StrFree(contents);
            return true;
        }
        at += (uint32_t)(46 + nameLen + extra + comment);
    }
    return BundleFail(error, fmt("the zip archive holds no %s", name));
}

static int PeHeader(Str bytes) {
    if (len(bytes) < 0x40 || bytes.s[0] != 'M' || bytes.s[1] != 'Z') return -1;
    uint32_t at = (unsigned char)bytes.s[0x3c] |
                  ((unsigned char)bytes.s[0x3d] << 8) |
                  ((unsigned char)bytes.s[0x3e] << 16) |
                  ((unsigned char)bytes.s[0x3f] << 24);
    if ((int)at < 0 || (int)at + 4 > len(bytes)) return -1;
    if (memcmp(bytes.s + at, "PE\0\0", 4) != 0) return -1;
    return (int)at;
}

bool BundleMarkAsGuiProgram(Str bytes, Str* error) {
    int pe = PeHeader(bytes);
    if (pe < 0) return BundleFail(error, StrL("not a PE executable"));
    int optional = pe + 24;
    if (optional + 70 > len(bytes))
        return BundleFail(error, StrL("the PE optional header is truncated"));
    uint16_t magic = (unsigned char)bytes.s[optional] |
                     ((unsigned char)bytes.s[optional + 1] << 8);
    if (magic != 0x10b && magic != 0x20b)
        return BundleFail(error,
                          StrL("the PE executable has no optional header"));
    bytes.s[optional + 68] = 2;
    bytes.s[optional + 69] = 0;
    return true;
}

static Str Sha256Hex(Str bytes) {
    uint8_t digest[32];
    Sha256(bytes, digest);
    StrBuilder out;
    for (int i = 0; i < 32; i++) out.Append(fmt("%02x", (int)digest[i]));
    return out.TakeStr();
}

bool BundleWindowsRuntimeFrom(Str cache, Str version, BundleDownloadFn download,
                              void* data, Str* executable, Str* error) {
    if (executable) *executable = {};
    if (cache && PlatFileExists(cache.s)) {
        Str bytes;
        if (!ReadFileBytes(cache, &bytes, error)) return false;
        if (PeHeader(bytes) < 0) {
            StrFree(bytes);
            return BundleFail(error,
                              fmt("%s is not a Windows executable", cache));
        }
        if (executable)
            *executable = bytes;
        else
            StrFree(bytes);
        return true;
    }
    Str url = StrDup(
        fmt("https://github.com/longbridge/gpui-kit/releases/download/v%s/"
            "gpui-shell-%s.zip",
            version, Str(kWindowsTarget)));
    Str archive;
    bool got = download && download(url, data, &archive, error);
    if (!got) {
        StrFree(url);
        // The downloader already owns the sentence. Re-wrapping it would free
        // that string and then copy it.
        if (error && *error) return false;
        return BundleFail(error,
                          StrL("cannot download gpui-shell for Windows"));
    }
    Str entryName =
        StrDup(fmt("gpui-shell-%s/gpui-shell.exe", Str(kWindowsTarget)));
    Str bytes;
    bool ok = BundleZipEntry(archive, entryName, &bytes, error);
    StrFree(archive);
    StrFree(entryName);
    if (!ok) {
        StrFree(url);
        return false;
    }
    if (PeHeader(bytes) < 0) {
        StrFree(bytes);
        StrFree(url);
        return BundleFail(error, fmt("%s holds no Windows executable", url));
    }
    StrFree(url);
    if (cache) {
        Str parent = ParentPath(cache);
        Str ignored;
        DependencyMakeDirectories(parent, &ignored);
        StrFree(ignored);
        StrFree(parent);
        if (!ReplaceFile(cache, bytes, error)) {
            StrFree(bytes);
            return false;
        }
    }
    if (executable)
        *executable = bytes;
    else
        StrFree(bytes);
    return true;
}

static bool WriteDirectory(const BundleTree& tree, Str destination,
                           Str* error) {
    for (int i = 0; i < len(tree.items); i++) {
        const BundleItem& item = tree.items.els[i];
        Str path = JoinDisk(destination, item.path);
        if (item.directory) {
            Str ignored;
            if (!DependencyMakeDirectories(path, &ignored)) {
                Str message =
                    ignored ? ignored : StrDup(fmt("cannot write %s", path));
                StrFree(path);
                return BundleFail(error, message);
            }
            StrFree(ignored);
        } else if (!WriteFileBytes(path, item.bytes, error)) {
            StrFree(path);
            return false;
        }
        if (item.executable && !PlatMakeExecutable(path.s)) {
            StrFree(path);
            return BundleFail(error,
                              fmt("cannot make %s executable", item.path));
        }
        StrFree(path);
    }
    return true;
}

static int IconSizeFor(int width) {
    const int sizes[] = {512, 256, 128, 64, 48, 32, 16};
    for (int size : sizes)
        if (size <= width) return size;
    return width;
}

static Str TrimTrailingSlash(Str value) {
    int n = len(value);
    while (n > 0 && value.s[n - 1] == '/') n--;
    return Str(value.s, n);
}

// The release zip, checked against the `.sha256` published beside it. The
// unit test injects the bytes; a live download goes through HttpGet, whose
// body cap is 16 MiB.
static bool DownloadRelease(Str url, void*, Str* bytes, Str* err) {
    HttpRsp response;
    if (!HttpGet(url, &response) || response.status < 200 ||
        response.status >= 300) {
        HttpRspFree(&response);
        return BundleFail(err, fmt("cannot download %s", url));
    }
    char* copy = (char*)Alloc(nullptr, (size_t)len(response.body) + 1);
    if (!copy) {
        HttpRspFree(&response);
        return BundleFail(err, StrL("allocating a download failed"));
    }
    if (len(response.body))
        memcpy(copy, response.body.els, (size_t)len(response.body));
    copy[len(response.body)] = 0;
    int n = len(response.body);
    HttpRspFree(&response);
    Str body = Str(copy, n);
    Str sumUrl = StrDup(fmt("%s.sha256", url));
    HttpRsp sum;
    bool summed =
        HttpGet(sumUrl, &sum) && sum.status >= 200 && sum.status < 300;
    StrFree(sumUrl);
    if (!summed) {
        HttpRspFree(&sum);
        StrFree(body);
        return BundleFail(err, fmt("%s.sha256 is empty", url));
    }
    int hex = 0;
    while (hex < len(sum.body) && sum.body.els[hex] != ' ' &&
           sum.body.els[hex] != '\t' && sum.body.els[hex] != '\n' &&
           sum.body.els[hex] != '\r')
        hex++;
    Str expect = Str((const char*)sum.body.els, hex);
    Str actual = Sha256Hex(body);
    bool match = len(expect) == len(actual);
    if (match) {
        for (int i = 0; i < len(expect); i++) {
            char a = expect.s[i];
            char b = actual.s[i];
            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
            if (a != b) match = false;
        }
    }
    StrFree(actual);
    HttpRspFree(&sum);
    if (!match) {
        StrFree(body);
        return BundleFail(err, fmt("%s does not match its checksum", url));
    }
    if (bytes)
        *bytes = body;
    else
        StrFree(body);
    return true;
}

bool ShellBundle(Str directory, const Invocation& invocation, Str* report,
                 Str* error) {
    if (report) *report = {};
    if (error) {
        StrFree(*error);
        *error = {};
    }
    Str manifestOwned = JoinDisk(directory, Str(kShellManifestFile));
    if (!PlatFileExists(manifestOwned.s)) {
        StrFree(manifestOwned);
        return BundleFail(
            error,
            fmt("%s has no %s. A bundle needs one: its `id` keeps the user's "
                "data when the application is moved or updated, and its `name` "
                "and `version` name the bundle.",
                directory, Str(kShellManifestFile)));
    }
    StrFree(manifestOwned);
    PluginManifest manifest;
    if (!PluginManifestRead(directory, &manifest, nullptr))
        return BundleFail(
            error, fmt("invalid %s/%s", directory, Str(kShellManifestFile)));
    BundlePlatform platform =
        invocation.platformSet ? invocation.platform : BundleHostPlatform();
    if (invocation.downloadUrl && platform != BundlePlatform::Linux)
        return BundleFail(error,
                          StrL("`--download-url` is for the Linux bundle, "
                               "whose install.sh downloads the archive"));
    Str rgba;
    int iconW = 0, iconH = 0;
    if (invocation.icon) {
        Str png;
        if (!ReadFileBytes(invocation.icon, &png, error)) return false;
        Str decodeError;
        if (!PngDecode(png, &rgba, &iconW, &iconH, &decodeError)) {
            StrFree(png);
            if (decodeError && StrContains(decodeError, StrL("8-bit"))) {
                bool failed = BundleFail(error, decodeError);
                StrFree(decodeError);
                return failed;
            }
            StrFree(decodeError);
            return BundleFail(error,
                              fmt("%s is not a PNG image", invocation.icon));
        }
        StrFree(png);
        if (iconW != iconH || iconW < 16) {
            StrFree(rgba);
            return BundleFail(error,
                              fmt("%s is %d×%d; the icon must be a square "
                                  "PNG, ideally 1024×1024",
                                  invocation.icon, iconW, iconH));
        }
    }
    Str out = invocation.out ? StrDup(invocation.out) : Str{};
    if (!out) {
        char cwd[kMaxPath];
        cwd[0] = 0;
        PlatGetCwd(cwd, kMaxPath);
        out = JoinDisk(Str(cwd), StrL("dist"));
    }
    Str ignored;
    if (!DependencyMakeDirectories(out, &ignored)) {
        StrFree(rgba);
        StrFree(out);
        return BundleFail(error,
                          ignored ? ignored : StrL("cannot create the output"));
    }
    StrFree(ignored);
    Str exclude;
    if (!Canonical(out, &exclude, error)) {
        StrFree(rgba);
        StrFree(out);
        return false;
    }
    BundleNames names;
    BundleNamesFrom(manifest, platform, &names);
    Str runtime = StrDup(fmt(
        "It uses gpui-shell %s or later when one is installed, and downloads "
        "v%s otherwise.",
        Str(kShellVersion), Str(kShellVersion)));
    BundleScriptValues values;
    values.id = manifest.id;
    values.name = manifest.name;
    values.command = names.command;
    values.appVersion = manifest.version;
    values.runtimeVersion = Str(kShellVersion);
    values.stem = names.archiveStem;
    values.downloadUrl = invocation.downloadUrl
                             ? TrimTrailingSlash(invocation.downloadUrl)
                             : Str{};
    BundleTree tree;
    Str built;
    if (platform == BundlePlatform::MacOS) {
        Str contents = StrDup(fmt("%s/Contents", names.appBundle));
        Str plist = BundleInfoPlist(manifest, names, rgba.s != nullptr);
        Str plistPath = StrDup(fmt("%s/Info.plist", contents));
        Str pkgPath = StrDup(fmt("%s/PkgInfo", contents));
        Str exePath = StrDup(fmt("%s/MacOS/%s", contents, names.display));
        Str launcher = BundleShellScript(Str(kBundleMacosLauncherSh), values);
        tree.Add(plistPath, plist, false, false);
        tree.Add(pkgPath, StrDup(StrL("APPL????")), false, false);
        tree.Add(exePath, launcher, true, false);
        StrFree(plistPath);
        StrFree(pkgPath);
        StrFree(exePath);
        if (rgba.s) {
            Str icns;
            if (!BundleWriteIcns((const uint8_t*)rgba.s, iconW, &icns, error)) {
                tree.Free();
                StrFree(contents);
                names.Free();
                StrFree(runtime);
                StrFree(rgba);
                StrFree(exclude);
                StrFree(out);
                return false;
            }
            Str iconPath =
                StrDup(fmt("%s/Resources/%s.icns", contents, names.display));
            tree.Add(iconPath, icns, false, false);
            StrFree(iconPath);
        }
        Str appPrefix = StrDup(fmt("%s/Resources/app", contents));
        bool added =
            AddApplication(&tree, directory, appPrefix, exclude, error);
        StrFree(appPrefix);
        StrFree(contents);
        if (!added) {
            tree.Free();
            names.Free();
            StrFree(runtime);
            StrFree(rgba);
            StrFree(exclude);
            StrFree(out);
            return false;
        }
        Str staging = JoinDisk(out, StrL(".bundle-partial"));
        DependencyRemoveTree(staging);
        if (!WriteDirectory(tree, staging, error)) {
            DependencyRemoveTree(staging);
            StrFree(staging);
            tree.Free();
            names.Free();
            StrFree(runtime);
            StrFree(rgba);
            StrFree(exclude);
            StrFree(out);
            return false;
        }
        Str destination = JoinDisk(out, names.appBundle);
        Str marker = JoinDisk(destination, StrL("Contents/Info.plist"));
        if (PlatFileExists(destination.s) || PlatDirExists(destination.s)) {
            if (!PlatFileExists(marker.s)) {
                StrFree(marker);
                DependencyRemoveTree(staging);
                StrFree(staging);
                tree.Free();
                names.Free();
                StrFree(runtime);
                StrFree(rgba);
                StrFree(exclude);
                Str message =
                    StrDup(fmt("%s exists and is not an application "
                               "bundle; move it away first",
                               destination));
                StrFree(destination);
                StrFree(out);
                return BundleFail(error, message);
            }
            DependencyRemoveTree(destination);
        }
        StrFree(marker);
        Str top = JoinDisk(staging, names.appBundle);
        bool renamed = DependencyRenameDirectory(top, destination);
        StrFree(top);
        DependencyRemoveTree(staging);
        StrFree(staging);
        if (!renamed) {
            tree.Free();
            names.Free();
            StrFree(runtime);
            StrFree(rgba);
            StrFree(exclude);
            StrFree(out);
            return BundleFail(error, fmt("cannot write %s", destination));
        }
        built = StrDup(fmt(
            "bundled %s\n%s The first launch downloads it when needed.\n"
            "The bundle is not signed. Sign and notarize it before giving it "
            "to anyone; see "
            "https://gpui-kit.com/shell/packaging#signing-on-macos",
            destination, runtime));
        StrFree(destination);
    } else if (platform == BundlePlatform::Linux) {
        if (rgba.s) {
            int size = IconSizeFor(iconW);
            uint8_t* scaled = (uint8_t*)Alloc(nullptr, (size_t)size * size * 4);
            if (!scaled) {
                tree.Free();
                names.Free();
                StrFree(runtime);
                StrFree(rgba);
                StrFree(exclude);
                StrFree(out);
                return BundleFail(error, StrL("allocating an icon failed"));
            }
            BoxScale((const uint8_t*)rgba.s, iconW, scaled, size);
            Str png;
            bool encoded = PngEncode(scaled, size, &png, error);
            free(scaled);
            if (!encoded) {
                tree.Free();
                names.Free();
                StrFree(runtime);
                StrFree(rgba);
                StrFree(exclude);
                StrFree(out);
                return false;
            }
            Str iconPath =
                StrDup(fmt("%s/%s.png", names.archiveStem, names.command));
            tree.Add(iconPath, png, false, false);
            StrFree(iconPath);
            values.iconSize = StrDup(fmt("%d", size));
        }
        Str launcher = BundleShellScript(Str(kBundleLinuxLauncherSh), values);
        Str launcherPath =
            StrDup(fmt("%s/%s", names.archiveStem, names.command));
        tree.Add(launcherPath, launcher, true, false);
        StrFree(launcherPath);
        Str appPrefix = StrDup(fmt("%s/app", names.archiveStem));
        bool added =
            AddApplication(&tree, directory, appPrefix, exclude, error);
        StrFree(appPrefix);
        if (!added) {
            tree.Free();
            names.Free();
            StrFree(runtime);
            StrFree(rgba);
            StrFree(values.iconSize);
            StrFree(exclude);
            StrFree(out);
            return false;
        }
        Str tar;
        if (!WriteTar(tree, &tar, error)) {
            tree.Free();
            names.Free();
            StrFree(runtime);
            StrFree(rgba);
            StrFree(values.iconSize);
            StrFree(exclude);
            StrFree(out);
            return false;
        }
        Str archive = JoinDisk(out, fmt("%s.tar.gz", names.archiveStem));
        bool wrote = ReplaceFile(archive, tar, error);
        StrFree(tar);
        if (!wrote) {
            StrFree(archive);
            tree.Free();
            names.Free();
            StrFree(runtime);
            StrFree(rgba);
            StrFree(values.iconSize);
            StrFree(exclude);
            StrFree(out);
            return false;
        }
        Str archiveBytes;
        if (!ReadFileBytes(archive, &archiveBytes, error)) {
            StrFree(archive);
            tree.Free();
            names.Free();
            StrFree(runtime);
            StrFree(rgba);
            StrFree(values.iconSize);
            StrFree(exclude);
            StrFree(out);
            return false;
        }
        values.archive = StrDup(fmt("%s.tar.gz", names.archiveStem));
        values.archiveSha256 = Sha256Hex(archiveBytes);
        StrFree(archiveBytes);
        Str installerText =
            BundleShellScript(Str(kBundleLinuxInstallSh), values);
        Str installer = JoinDisk(out, StrL("install.sh"));
        bool installOk = ReplaceFile(installer, installerText, error) &&
                         PlatMakeExecutable(installer.s);
        StrFree(installerText);
        if (!installOk) {
            StrFree(installer);
            StrFree(archive);
            StrFree(values.archive);
            StrFree(values.archiveSha256);
            tree.Free();
            names.Free();
            StrFree(runtime);
            StrFree(rgba);
            StrFree(values.iconSize);
            StrFree(exclude);
            StrFree(out);
            return false;
        }
        Str remote;
        if (invocation.downloadUrl) {
            Str trimmed = TrimTrailingSlash(invocation.downloadUrl);
            remote = StrDup(fmt(
                "\nPublish both at %s; `curl -fsSL %s/install.sh | sh` then "
                "installs it.",
                trimmed, trimmed));
        }
        built = StrDup(
            fmt("bundled %s\nbundled %s\n"
                "Unpack the archive and run the launcher in it, or run `sh "
                "install.sh` beside the archive to install it for the current "
                "user; `sh install.sh --uninstall` removes it.%s\n%s",
                archive, installer, remote ? remote : Str{}, runtime));
        StrFree(remote);
        StrFree(installer);
        StrFree(archive);
        StrFree(values.archive);
        StrFree(values.archiveSha256);
        StrFree(values.iconSize);
    } else {
        char exePath[kMaxPath];
        exePath[0] = 0;
        Str executable;
        if (StrEq(Str(PlatShellPlatformName()), StrL("windows"))) {
            if (!PlatGetExePath(exePath, kMaxPath) ||
                !ReadFileBytes(Str(exePath), &executable, error)) {
                tree.Free();
                names.Free();
                StrFree(runtime);
                StrFree(rgba);
                StrFree(exclude);
                StrFree(out);
                if (error && *error) return false;
                return BundleFail(error,
                                  StrL("cannot locate the running executable"));
            }
        } else {
            Str apps = ShellApplicationsDirectory();
            Str home = ParentPath(apps);
            StrFree(apps);
            Str cache =
                StrDup(fmt("%s/cache/runtime/v%s/%s/gpui-shell.exe", home,
                           Str(kShellVersion), Str(kWindowsTarget)));
            StrFree(home);
            bool got = BundleWindowsRuntimeFrom(cache, Str(kShellVersion),
                                                DownloadRelease, nullptr,
                                                &executable, error);
            StrFree(cache);
            if (!got) {
                tree.Free();
                names.Free();
                StrFree(runtime);
                StrFree(rgba);
                StrFree(exclude);
                StrFree(out);
                return false;
            }
        }
        if (!BundleMarkAsGuiProgram(executable, error)) {
            StrFree(executable);
            tree.Free();
            names.Free();
            StrFree(runtime);
            StrFree(rgba);
            StrFree(exclude);
            StrFree(out);
            return false;
        }
        Str top = StrDup(fmt("%s-x86_64", names.archiveStem));
        Str exeName = StrDup(fmt("%s/%s.exe", top, names.display));
        tree.Add(exeName, executable, true, false);
        StrFree(exeName);
        Str appPrefix = StrDup(fmt("%s/app", top));
        bool added =
            AddApplication(&tree, directory, appPrefix, exclude, error);
        StrFree(appPrefix);
        if (!added) {
            StrFree(top);
            tree.Free();
            names.Free();
            StrFree(runtime);
            StrFree(rgba);
            StrFree(exclude);
            StrFree(out);
            return false;
        }
        Str zip;
        if (!WriteZip(tree, &zip, error)) {
            StrFree(top);
            tree.Free();
            names.Free();
            StrFree(runtime);
            StrFree(rgba);
            StrFree(exclude);
            StrFree(out);
            return false;
        }
        Str archive = JoinDisk(out, fmt("%s.zip", top));
        bool wrote = ReplaceFile(archive, zip, error);
        StrFree(zip);
        if (!wrote) {
            StrFree(archive);
            StrFree(top);
            tree.Free();
            names.Free();
            StrFree(runtime);
            StrFree(rgba);
            StrFree(exclude);
            StrFree(out);
            return false;
        }
        built = StrDup(
            fmt("bundled %s\nUnzip it anywhere and open %s.exe; it needs no "
                "installation. It carries gpui-shell %s.",
                archive, names.display, Str(kShellVersion)));
        if (rgba.s) {
            Str extra =
                StrDup(fmt("%s\nThe icon is not embedded: the executable keeps "
                           "gpui-shell's.",
                           built));
            StrFree(built);
            built = extra;
        }
        StrFree(archive);
        StrFree(top);
    }
    tree.Free();
    names.Free();
    StrFree(runtime);
    StrFree(rgba);
    StrFree(exclude);
    StrFree(out);
    if (report)
        *report = built;
    else
        StrFree(built);
    return true;
}

bool BundleWriteSampleArchives(Str directory, Str* error) {
    BundleTree tree;
    tree.Add(StrL("top/run"), StrDup(StrL("#!/bin/sh\n")), true, false);
    StrBuilder body;
    for (int i = 0; i < 100; i++) body.Append(StrL("export {}"));
    tree.Add(StrL("top/app/main.js"), body.TakeStr(), false, false);
    Str tar;
    if (!WriteTar(tree, &tar, error)) {
        tree.Free();
        return false;
    }
    Str tarPath = JoinDisk(directory, StrL("bundle.tar.gz"));
    bool ok = ReplaceFile(tarPath, tar, error);
    StrFree(tar);
    StrFree(tarPath);
    if (!ok) {
        tree.Free();
        return false;
    }
    Str zip;
    if (!WriteZip(tree, &zip, error)) {
        tree.Free();
        return false;
    }
    Str zipPath = JoinDisk(directory, StrL("bundle.zip"));
    ok = ReplaceFile(zipPath, zip, error);
    StrFree(zip);
    StrFree(zipPath);
    tree.Free();
    return ok;
}

static uint32_t ParseOctal(const char* field, int width) {
    uint32_t value = 0;
    for (int i = 0; i < width; i++) {
        char c = field[i];
        if (c == 0 || c == ' ') break;
        if (c < '0' || c > '7') break;
        value = (value << 3) | (uint32_t)(c - '0');
    }
    return value;
}

bool BundleTarMode(Str tarGz, Str path, uint32_t* mode, Str* error) {
    Str gz;
    if (!ReadFileBytes(tarGz, &gz, error)) return false;
    Str tar;
    if (!ZlibInflate(gz, true, &tar, error)) {
        StrFree(gz);
        return false;
    }
    StrFree(gz);
    int at = 0;
    bool found = false;
    while (at + 512 <= len(tar)) {
        if (tar.s[at] == 0) break;
        int nameLen = 0;
        while (nameLen < 100 && tar.s[at + nameLen]) nameLen++;
        Str name = Str(tar.s + at, nameLen);
        uint32_t entryMode = ParseOctal(tar.s + at + 100, 8);
        uint64_t size = ParseOctal(tar.s + at + 124, 12);
        if (StrEq(name, path)) {
            if (mode) *mode = entryMode;
            found = true;
            break;
        }
        int blocks = (int)((size + 511) / 512);
        at += 512 + blocks * 512;
    }
    StrFree(tar);
    if (!found) return BundleFail(error, fmt("tar holds no %s", path));
    return true;
}

bool BundleZipEntryCount(Str zipPath, int* count, Str* error) {
    Str zip;
    if (!ReadFileBytes(zipPath, &zip, error)) return false;
    if (len(zip) < 22 || memcmp(zip.s + len(zip) - 22, "PK\x05\x06", 4) != 0) {
        StrFree(zip);
        return BundleFail(error, StrL("not a zip archive"));
    }
    int n = (unsigned char)zip.s[len(zip) - 22 + 10] |
            ((unsigned char)zip.s[len(zip) - 22 + 11] << 8);
    StrFree(zip);
    if (count) *count = n;
    return true;
}

bool BundleReleaseZip(Str executable, Str* zip, Str* error) {
    if (zip) *zip = {};
    BundleTree tree;
    Str readmeName =
        StrDup(fmt("gpui-shell-%s/README.md", Str(kWindowsTarget)));
    Str exeName =
        StrDup(fmt("gpui-shell-%s/gpui-shell.exe", Str(kWindowsTarget)));
    tree.Add(readmeName, StrDup(StrL("readme")), false, false);
    tree.Add(exeName, StrDup(executable), true, false);
    StrFree(readmeName);
    StrFree(exeName);
    bool ok = WriteZip(tree, zip, error);
    tree.Free();
    return ok;
}

} // namespace gpui::shell
