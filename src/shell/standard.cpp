#include "shell/standard.h"

#include <string.h>

namespace gpui::shell {

static uint32_t RotateRight(uint32_t value, int count) {
    return (value >> count) | (value << (32 - count));
}

static uint32_t ReadBig32(const uint8_t* value) {
    return ((uint32_t)value[0] << 24) | ((uint32_t)value[1] << 16) |
           ((uint32_t)value[2] << 8) | value[3];
}

static void WriteBig32(uint8_t* out, uint32_t value) {
    out[0] = (uint8_t)(value >> 24);
    out[1] = (uint8_t)(value >> 16);
    out[2] = (uint8_t)(value >> 8);
    out[3] = (uint8_t)value;
}

static void Sha256Block(uint32_t state[8], const uint8_t block[64]) {
    static const uint32_t constants[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
        0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
        0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
        0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
        0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
        0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
        0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
        0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
        0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
    };
    uint32_t words[64];
    for (int i = 0; i < 16; i++) words[i] = ReadBig32(block + (size_t)i * 4);
    for (int i = 16; i < 64; i++) {
        uint32_t a = words[i - 15];
        uint32_t b = words[i - 2];
        uint32_t s0 = RotateRight(a, 7) ^ RotateRight(a, 18) ^ (a >> 3);
        uint32_t s1 = RotateRight(b, 17) ^ RotateRight(b, 19) ^ (b >> 10);
        words[i] = words[i - 16] + s0 + words[i - 7] + s1;
    }
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
    for (int i = 0; i < 64; i++) {
        uint32_t s1 =
            RotateRight(e, 6) ^ RotateRight(e, 11) ^ RotateRight(e, 25);
        uint32_t choice = (e & f) ^ (~e & g);
        uint32_t t1 = h + s1 + choice + constants[i] + words[i];
        uint32_t s0 =
            RotateRight(a, 2) ^ RotateRight(a, 13) ^ RotateRight(a, 22);
        uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = s0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

void Sha256(Str data, uint8_t digest[32]) {
    uint32_t state[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                         0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    int offset = 0;
    while (offset + 64 <= data.len) {
        Sha256Block(state, (const uint8_t*)data.s + offset);
        offset += 64;
    }
    uint8_t tail[128] = {};
    int remaining = data.len - offset;
    if (remaining > 0) memcpy(tail, data.s + offset, (size_t)remaining);
    tail[remaining] = 0x80;
    int blocks = remaining < 56 ? 1 : 2;
    uint64_t bits = (uint64_t)(uint32_t)data.len * 8;
    for (int i = 0; i < 8; i++) {
        tail[blocks * 64 - 1 - i] = (uint8_t)(bits >> (i * 8));
    }
    Sha256Block(state, tail);
    if (blocks == 2) Sha256Block(state, tail + 64);
    for (int i = 0; i < 8; i++) WriteBig32(digest + (size_t)i * 4, state[i]);
}

static void StandardError(Str* error, Str message) {
    if (!error) return;
    StrFree(*error);
    *error = StrDup(message);
}

static uint32_t Adler32(Str data) {
    uint32_t a = 1, b = 0;
    for (int i = 0; i < data.len; i++) {
        a = (a + (uint8_t)data.s[i]) % 65521;
        b = (b + a) % 65521;
    }
    return (b << 16) | a;
}

static uint32_t Crc32(Str data) {
    uint32_t crc = 0xffffffffu;
    for (int i = 0; i < data.len; i++) {
        crc ^= (uint8_t)data.s[i];
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc >> 1) ^ (0xedb88320u & (uint32_t)-(int)(crc & 1));
        }
    }
    return ~crc;
}

static void AppendLittle16(StrBuilder* out, uint16_t value) {
    out->AppendChar((char)value);
    out->AppendChar((char)(value >> 8));
}

static void AppendLittle32(StrBuilder* out, uint32_t value) {
    out->AppendChar((char)value);
    out->AppendChar((char)(value >> 8));
    out->AppendChar((char)(value >> 16));
    out->AppendChar((char)(value >> 24));
}

static void AppendBig32(StrBuilder* out, uint32_t value) {
    out->AppendChar((char)(value >> 24));
    out->AppendChar((char)(value >> 16));
    out->AppendChar((char)(value >> 8));
    out->AppendChar((char)value);
}

static bool DeflateStored(Str input, StrBuilder* out) {
    int offset = 0;
    do {
        int count = input.len - offset;
        if (count > 65535) count = 65535;
        bool final = offset + count == input.len;
        out->AppendChar(final ? 1 : 0);
        AppendLittle16(out, (uint16_t)count);
        AppendLittle16(out, (uint16_t)~count);
        out->Append(Str(input.s ? input.s + offset : "", count));
        offset += count;
    } while (offset < input.len);
    return true;
}

bool ZlibDeflate(Str input, bool gzip, Str* output, Str* error) {
    if (output) {
        StrFree(*output);
        *output = {};
    }
    if (error) {
        StrFree(*error);
        *error = {};
    }
    if (input.len < 0 || input.len > kStandardDataLimit) {
        StandardError(error,
                      StrL("compression input exceeds the 64 MiB limit"));
        return false;
    }
    StrBuilder encoded;
    if (gzip) {
        const uint8_t header[10] = {0x1f, 0x8b, 8, 0, 0, 0, 0, 0, 0, 255};
        encoded.Append(Str((const char*)header, 10));
    } else {
        encoded.AppendChar(0x78);
        encoded.AppendChar(0x01);
    }
    DeflateStored(input, &encoded);
    if (gzip) {
        AppendLittle32(&encoded, Crc32(input));
        AppendLittle32(&encoded, (uint32_t)input.len);
    } else {
        AppendBig32(&encoded, Adler32(input));
    }
    if (output) *output = encoded.TakeStr();
    return true;
}

struct BitReader {
    const uint8_t* bytes = nullptr;
    int length = 0;
    int offset = 0;
    uint32_t bits = 0;
    int count = 0;

    bool Read(int wanted, uint32_t* value) {
        while (count < wanted) {
            if (offset >= length) return false;
            bits |= (uint32_t)bytes[offset++] << count;
            count += 8;
        }
        *value = wanted == 0 ? 0 : bits & ((1u << wanted) - 1u);
        bits >>= wanted;
        count -= wanted;
        return true;
    }

    void Align() {
        bits = 0;
        count = 0;
    }
};

struct Huffman {
    uint16_t counts[16] = {};
    uint16_t symbols[320] = {};
};

static bool BuildHuffman(Huffman* table, const uint8_t* lengths, int count) {
    uint16_t offsets[16] = {};
    for (int i = 0; i < count; i++) {
        if (lengths[i] > 15) return false;
        table->counts[lengths[i]]++;
    }
    if (table->counts[0] == count) return false;
    int left = 1;
    for (int bits = 1; bits <= 15; bits++) {
        left = (left << 1) - table->counts[bits];
        if (left < 0) return false;
    }
    offsets[1] = 0;
    for (int bits = 1; bits < 15; bits++) {
        offsets[bits + 1] = offsets[bits] + table->counts[bits];
    }
    for (int symbol = 0; symbol < count; symbol++) {
        int length = lengths[symbol];
        if (length) table->symbols[offsets[length]++] = (uint16_t)symbol;
    }
    return true;
}

static int DecodeSymbol(BitReader* reader, const Huffman& table) {
    uint32_t code = 0;
    int first = 0, index = 0;
    for (int length = 1; length <= 15; length++) {
        uint32_t bit = 0;
        if (!reader->Read(1, &bit)) return -1;
        code |= bit;
        int count = table.counts[length];
        if ((int)code < first + count) {
            return table.symbols[index + ((int)code - first)];
        }
        index += count;
        first = (first + count) << 1;
        code <<= 1;
    }
    return -1;
}

static bool FixedTables(Huffman* literals, Huffman* distances) {
    uint8_t litLengths[288];
    uint8_t distLengths[32];
    for (int i = 0; i <= 143; i++) litLengths[i] = 8;
    for (int i = 144; i <= 255; i++) litLengths[i] = 9;
    for (int i = 256; i <= 279; i++) litLengths[i] = 7;
    for (int i = 280; i < 288; i++) litLengths[i] = 8;
    memset(distLengths, 5, sizeof(distLengths));
    return BuildHuffman(literals, litLengths, 288) &&
           BuildHuffman(distances, distLengths, 32);
}

static bool DynamicTables(BitReader* reader, Huffman* literals,
                          Huffman* distances) {
    static const uint8_t order[19] = {16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                      11, 4,  12, 3, 13, 2, 14, 1, 15};
    uint32_t hlit = 0, hdist = 0, hclen = 0;
    if (!reader->Read(5, &hlit) || !reader->Read(5, &hdist) ||
        !reader->Read(4, &hclen))
        return false;
    int literalCount = (int)hlit + 257;
    int distanceCount = (int)hdist + 1;
    int codeCount = (int)hclen + 4;
    uint8_t codeLengths[19] = {};
    for (int i = 0; i < codeCount; i++) {
        uint32_t length = 0;
        if (!reader->Read(3, &length)) return false;
        codeLengths[order[i]] = (uint8_t)length;
    }
    Huffman codes;
    if (!BuildHuffman(&codes, codeLengths, 19)) return false;
    uint8_t lengths[320] = {};
    int total = literalCount + distanceCount;
    int at = 0;
    while (at < total) {
        int symbol = DecodeSymbol(reader, codes);
        if (symbol >= 0 && symbol <= 15) {
            lengths[at++] = (uint8_t)symbol;
            continue;
        }
        uint32_t extra = 0;
        int repeat = 0;
        uint8_t value = 0;
        if (symbol == 16 && at > 0) {
            if (!reader->Read(2, &extra)) return false;
            repeat = (int)extra + 3;
            value = lengths[at - 1];
        } else if (symbol == 17) {
            if (!reader->Read(3, &extra)) return false;
            repeat = (int)extra + 3;
        } else if (symbol == 18) {
            if (!reader->Read(7, &extra)) return false;
            repeat = (int)extra + 11;
        } else {
            return false;
        }
        if (repeat > total - at) return false;
        while (repeat-- > 0) lengths[at++] = value;
    }
    if (lengths[256] == 0) return false;
    return BuildHuffman(literals, lengths, literalCount) &&
           BuildHuffman(distances, lengths + literalCount, distanceCount);
}

static bool AppendOutput(Vec<uint8_t>* output, uint8_t value) {
    return output->len < kStandardDataLimit && VecAppend(*output, value);
}

static bool InflateCodes(BitReader* reader, const Huffman& literals,
                         const Huffman& distances, Vec<uint8_t>* output) {
    static const uint16_t lengthBase[29] = {
        3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
        31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
    static const uint8_t lengthExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1,
                                            1, 1, 2, 2, 2, 2, 3, 3, 3, 3,
                                            4, 4, 4, 4, 5, 5, 5, 5, 0};
    static const uint16_t distanceBase[30] = {
        1,    2,    3,    4,    5,    7,    9,    13,    17,    25,
        33,   49,   65,   97,   129,  193,  257,  385,   513,   769,
        1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
    static const uint8_t distanceExtra[30] = {
        0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
        6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
    for (;;) {
        int symbol = DecodeSymbol(reader, literals);
        if (symbol < 0) return false;
        if (symbol < 256) {
            if (!AppendOutput(output, (uint8_t)symbol)) return false;
            continue;
        }
        if (symbol == 256) return true;
        if (symbol < 257 || symbol > 285) return false;
        int lengthIndex = symbol - 257;
        uint32_t extra = 0;
        if (!reader->Read(lengthExtra[lengthIndex], &extra)) return false;
        int length = lengthBase[lengthIndex] + (int)extra;
        int distanceSymbol = DecodeSymbol(reader, distances);
        if (distanceSymbol < 0 || distanceSymbol >= 30) return false;
        if (!reader->Read(distanceExtra[distanceSymbol], &extra)) return false;
        int distance = distanceBase[distanceSymbol] + (int)extra;
        if (distance <= 0 || distance > output->len ||
            length > kStandardDataLimit - output->len)
            return false;
        for (int i = 0; i < length; i++) {
            uint8_t byte = (*output)[output->len - distance];
            if (!VecAppend(*output, byte)) return false;
        }
    }
}

static bool InflateRaw(const uint8_t* bytes, int length, Vec<uint8_t>* output,
                       int* consumed) {
    BitReader reader = {bytes, length};
    bool final = false;
    while (!final) {
        uint32_t finalBit = 0, type = 0;
        if (!reader.Read(1, &finalBit) || !reader.Read(2, &type)) return false;
        final = finalBit != 0;
        if (type == 0) {
            reader.Align();
            if (reader.offset + 4 > reader.length) return false;
            int count = reader.bytes[reader.offset] |
                        (reader.bytes[reader.offset + 1] << 8);
            int inverted = reader.bytes[reader.offset + 2] |
                           (reader.bytes[reader.offset + 3] << 8);
            reader.offset += 4;
            if (((count ^ inverted) & 0xffff) != 0xffff ||
                count > reader.length - reader.offset ||
                count > kStandardDataLimit - output->len)
                return false;
            for (int i = 0; i < count; i++) {
                if (!VecAppend(*output, reader.bytes[reader.offset + i]))
                    return false;
            }
            reader.offset += count;
        } else if (type == 1 || type == 2) {
            Huffman literals, distances;
            bool ok = type == 1 ? FixedTables(&literals, &distances)
                                : DynamicTables(&reader, &literals, &distances);
            if (!ok || !InflateCodes(&reader, literals, distances, output))
                return false;
        } else {
            return false;
        }
    }
    if (consumed) *consumed = reader.offset;
    return true;
}

static uint32_t ReadLittle32(const uint8_t* value) {
    return value[0] | ((uint32_t)value[1] << 8) | ((uint32_t)value[2] << 16) |
           ((uint32_t)value[3] << 24);
}

bool ZlibInflate(Str input, bool gzip, Str* output, Str* error) {
    if (output) {
        StrFree(*output);
        *output = {};
    }
    if (error) {
        StrFree(*error);
        *error = {};
    }
    if (!input.s || input.len < (gzip ? 18 : 6) ||
        input.len > kStandardDataLimit) {
        StandardError(
            error,
            StrL("compressed input is invalid or exceeds the 64 MiB limit"));
        return false;
    }
    const uint8_t* bytes = (const uint8_t*)input.s;
    int start = 0, trailer = 4;
    if (gzip) {
        if (bytes[0] != 0x1f || bytes[1] != 0x8b || bytes[2] != 8 ||
            (bytes[3] & 0xe0) != 0) {
            StandardError(error, StrL("invalid gzip header"));
            return false;
        }
        uint8_t flags = bytes[3];
        start = 10;
        if (flags & 4) {
            if (start + 2 > input.len - 8) {
                StandardError(error, StrL("invalid gzip extra field"));
                return false;
            }
            int extra = bytes[start] | (bytes[start + 1] << 8);
            start += 2 + extra;
        }
        if (flags & 8) {
            while (start < input.len - 8 && bytes[start] != 0) start++;
        }
        if (flags & 16) {
            while (start < input.len - 8 && bytes[start] != 0) start++;
        }
        if (flags & 2) start += 2;
        trailer = 8;
    } else {
        int header = (bytes[0] << 8) | bytes[1];
        if ((bytes[0] & 15) != 8 || (bytes[0] >> 4) > 7 || header % 31 != 0 ||
            (bytes[1] & 32) != 0) {
            StandardError(error, StrL("invalid zlib header"));
            return false;
        }
        start = 2;
    }
    if (start < 0 || start > input.len - trailer) {
        StandardError(error, StrL("compressed header exceeds its input"));
        return false;
    }
    Vec<uint8_t> decoded;
    int consumed = 0;
    int compressedLength = input.len - start - trailer;
    bool ok =
        InflateRaw(bytes + start, compressedLength, &decoded, &consumed) &&
        consumed == compressedLength;
    if (!ok) {
        StandardError(error, StrL("invalid or oversized DEFLATE stream"));
        return false;
    }
    Str decodedText((const char*)decoded.els, len(decoded));
    const uint8_t* check = bytes + input.len - trailer;
    if (gzip) {
        ok = ReadLittle32(check) == Crc32(decodedText) &&
             ReadLittle32(check + 4) == (uint32_t)len(decoded);
    } else {
        ok = ReadBig32(check) == Adler32(decodedText);
    }
    if (!ok) {
        StandardError(error, StrL("compressed stream checksum does not match"));
        return false;
    }
    if (output) {
        char* result = (char*)Alloc(nullptr, len(decoded) + 1);
        if (!result) {
            StandardError(error, StrL("allocating decompressed data failed"));
            return false;
        }
        if (len(decoded)) memcpy(result, decoded.els, (size_t)len(decoded));
        result[len(decoded)] = 0;
        *output = Str(result, len(decoded));
    }
    return true;
}

} // namespace gpui::shell
