// A small PNG reader for the PC edition's car textures (CarMesh): 8-bit grey, grey + alpha, RGB and
// RGBA pictures, not interlaced, as Blender writes them. The pixels come out RGBA, rows top to bottom.
// zlib (already linked for the packed files) does the unpacking.
#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <zlib.h>

namespace png {

inline uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

inline int paeth(int a, int b, int c) {
    int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    return pa <= pb && pa <= pc ? a : (pb <= pc ? b : c);
}

// decode a whole PNG file in memory; false (and nothing in `out`) if it isn't one this reader handles
inline bool decode(const void* data, size_t size, std::vector<uint8_t>& out, int& w, int& h) {
    const uint8_t* p = (const uint8_t*)data;
    static const uint8_t SIG[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (size < 33 || memcmp(p, SIG, 8)) return false;
    size_t o = 8;
    int depth = 0, type = -1, interlace = 0;
    std::vector<uint8_t> z;
    w = h = 0;
    while (o + 12 <= size) {
        uint32_t len = be32(p + o);
        const uint8_t* tag = p + o + 4;
        const uint8_t* body = p + o + 8;
        if (o + 12 + (size_t)len > size) return false;
        if (!memcmp(tag, "IHDR", 4) && len >= 13) {
            w = (int)be32(body); h = (int)be32(body + 4);
            depth = body[8]; type = body[9]; interlace = body[12];
        } else if (!memcmp(tag, "IDAT", 4)) {
            z.insert(z.end(), body, body + len);
        } else if (!memcmp(tag, "IEND", 4)) break;
        o += 12 + (size_t)len;
    }
    int ch = type == 0 ? 1 : type == 2 ? 3 : type == 4 ? 2 : type == 6 ? 4 : 0;
    if (w <= 0 || h <= 0 || w > 16384 || h > 16384 || depth != 8 || !ch || interlace || z.empty()) return false;
    size_t stride = (size_t)w * ch;
    std::vector<uint8_t> raw((stride + 1) * h);
    uLongf got = (uLongf)raw.size();
    if (uncompress(raw.data(), &got, z.data(), (uLong)z.size()) != Z_OK || got != raw.size()) return false;
    // undo the row filters (in place: each row is rebuilt from the one above, already rebuilt)
    std::vector<uint8_t> prev(stride, 0);
    for (int y = 0; y < h; y++) {
        uint8_t* row = &raw[(stride + 1) * y + 1];
        int f = row[-1];
        for (size_t x = 0; x < stride; x++) {
            int a = x >= (size_t)ch ? row[x - ch] : 0, b = prev[x], c = x >= (size_t)ch ? prev[x - ch] : 0;
            int v = row[x];
            switch (f) {
                case 0: break;
                case 1: v += a; break;
                case 2: v += b; break;
                case 3: v += (a + b) >> 1; break;
                case 4: v += paeth(a, b, c); break;
                default: return false;
            }
            row[x] = (uint8_t)v;
        }
        memcpy(prev.data(), row, stride);
    }
    out.resize((size_t)w * h * 4);
    for (int y = 0; y < h; y++) {
        const uint8_t* row = &raw[(stride + 1) * y + 1];
        uint8_t* d = &out[(size_t)y * w * 4];
        for (int x = 0; x < w; x++, d += 4, row += ch) {
            if (ch == 1) { d[0] = d[1] = d[2] = row[0]; d[3] = 255; }
            else if (ch == 2) { d[0] = d[1] = d[2] = row[0]; d[3] = row[1]; }
            else if (ch == 3) { d[0] = row[0]; d[1] = row[1]; d[2] = row[2]; d[3] = 255; }
            else { d[0] = row[0]; d[1] = row[1]; d[2] = row[2]; d[3] = row[3]; }
        }
    }
    return true;
}

// half the size (2 x 2 average); for the AI cars' textures, which never come close enough to need all of it
inline void halve(std::vector<uint8_t>& px, int& w, int& h) {
    int nw = w / 2, nh = h / 2;
    if (nw < 1 || nh < 1) return;
    std::vector<uint8_t> o((size_t)nw * nh * 4);
    for (int y = 0; y < nh; y++)
        for (int x = 0; x < nw; x++)
            for (int c = 0; c < 4; c++) {
                const uint8_t* a = &px[((size_t)(2 * y) * w + 2 * x) * 4 + c];
                const uint8_t* b = a + (size_t)w * 4;
                o[((size_t)y * nw + x) * 4 + c] = (uint8_t)((a[0] + a[4] + b[0] + b[4] + 2) >> 2);
            }
    px.swap(o); w = nw; h = nh;
}

}
