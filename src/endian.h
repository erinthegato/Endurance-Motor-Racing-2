// Byte order. The data files (made by the tools/ scripts on a PC) store their numbers little-endian,
// like the PSP, the Vita and the PC. The PS3's Cell CPU is big-endian, so there each file is turned
// round in memory right after loading (fix* below), before anything reads it. Everywhere else
// EMR_SWAP is 0 and the loaders skip all of this.
//
// Each fix* walks one file format, turns every 2- and 4-byte number round (8-bit pixels stay as they
// are) and returns false if the file isn't laid out as expected (it walked past the end, or didn't
// reach it). `fromLE`: the numbers are little-endian now (true on the PS3). The PC test
// (emr_test swapcheck) turns every data file round and back, and checks each walk ends exactly at the
// end of the file.
#pragma once
#include <cstdint>
#include <cstring>

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define EMR_SWAP 1
#else
#define EMR_SWAP 0
#endif

namespace endian {

inline uint16_t bswap16(uint16_t v) { return (uint16_t)((v >> 8) | (v << 8)); }
inline uint32_t bswap32(uint32_t v) { return (v >> 24) | ((v >> 8) & 0xFF00u) | ((v << 8) & 0xFF0000u) | (v << 24); }

// a little-endian number in a file, read on any machine
inline uint32_t le32(const void* p) { const uint8_t* b = (const uint8_t*)p; return b[0] | (b[1] << 8) | (b[2] << 16) | ((uint32_t)b[3] << 24); }
inline uint16_t le16(const void* p) { const uint8_t* b = (const uint8_t*)p; return (uint16_t)(b[0] | (b[1] << 8)); }
// on this machine: a number just read from a file as it is stored (little-endian) -> its value
inline uint32_t fromFile32(uint32_t v) { return EMR_SWAP ? bswap32(v) : v; }
inline int16_t fromFile16(int16_t v) { return EMR_SWAP ? (int16_t)bswap16((uint16_t)v) : v; }

// turn n 4-byte / 2-byte numbers round in place
inline void swap32s(void* p, size_t n) { uint8_t* b = (uint8_t*)p; for (size_t i = 0; i < n; i++, b += 4) { uint8_t t = b[0]; b[0] = b[3]; b[3] = t; t = b[1]; b[1] = b[2]; b[2] = t; } }
inline void swap16s(void* p, size_t n) { uint8_t* b = (uint8_t*)p; for (size_t i = 0; i < n; i++, b += 2) { uint8_t t = b[0]; b[0] = b[1]; b[1] = t; } }

// a reader that walks a file, turning numbers round as it goes; `ok` turns false on running past the end
struct Walk {
    uint8_t* p; size_t left; bool fromLE; bool ok = true;
    Walk(void* d, size_t n, bool le) : p((uint8_t*)d), left(n), fromLE(le) {}
    bool need(size_t n) { if (!ok || n > left) { ok = false; return false; } return true; }
    // the value of the next 4-byte number (as the file holds it now), then turn it round
    uint32_t u32() {
        if (!need(4)) return 0;
        uint32_t v = fromLE ? le32(p) : (((uint32_t)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]);
        swap32s(p, 1); p += 4; left -= 4; return v;
    }
    void words(size_t n) { if (!need(n * 4)) return; swap32s(p, n); p += n * 4; left -= n * 4; }
    void halves(size_t n) { if (!need(n * 2)) return; swap16s(p, n); p += n * 2; left -= n * 2; }
    void bytes(size_t n) { if (!need(n)) return; p += n; left -= n; }
    bool magic(const char* m) { return left >= 4 && !memcmp(p, m, 4); }
    bool done() const { return ok && left == 0; }
};

// 'EMM1' models (tools/gen_cars.py write_emm)
inline bool fixEmm(void* d, size_t n, bool fromLE) {
    Walk w(d, n, fromLE);
    if (!w.magic("EMM1")) return false;
    w.bytes(4);
    uint32_t lods = w.u32();
    if (lods > 16) return false;
    w.words(6);                                   // scale, offset
    for (uint32_t i = 0; i < lods && w.ok; i++) {
        uint32_t vc = w.u32(), ts = w.u32();
        w.words(2);                               // glass count, cabX / panelY / panelX
        if (ts > 4096 || vc > (1u << 24)) return false;
        if (ts) { w.words(256); w.bytes((size_t)ts * ts); }
        for (uint32_t k = 0; k < vc && w.ok; k++) { w.halves(2); w.words(1); w.halves(4); }   // ModelVertex
    }
    if (w.magic("EMX1")) { w.bytes(4); w.words(5); }
    return w.done();
}

// 'TEX1' textures: size, two spare words, palette, pixels
inline bool fixTex(void* d, size_t n, bool fromLE) {
    Walk w(d, n, fromLE);
    if (!w.magic("TEX1")) return false;
    w.bytes(4);
    uint32_t s = w.u32(); w.words(2);
    if (s > 4096) return false;
    w.words(256); w.bytes((size_t)s * s);
    return w.done();
}

// 'FNT1' / 'FNT2' fonts: width, height, line height (+ scale), palette, pixels, 95 glyphs of six int16
inline bool fixFont(void* d, size_t n, bool fromLE) {
    Walk w(d, n, fromLE);
    if (!w.magic("FNT1") && !w.magic("FNT2")) return false;
    w.bytes(4);
    uint32_t fw = w.u32(), fh = w.u32(); w.words(1);
    if (fw > 4096 || fh > 4096) return false;
    w.words(256); w.bytes((size_t)fw * fh);
    w.halves(95 * 6);
    return w.done();
}

// 'EMT1' circuits: every number after the magic is 4 bytes (counts, floats, the height grid)
inline bool fixEmt(void* d, size_t n, bool) {
    if (n < 4 || memcmp(d, "EMT1", 4) || (n & 3)) return false;
    swap32s((uint8_t*)d + 4, (n - 4) / 4);
    return true;
}

// 'LVY1' career liveries: a 288-byte header of 4-byte numbers (counts, sizes, colour roles), then pixels
inline bool fixLvy(void* d, size_t n, bool) {
    if (n < 288 || memcmp(d, "LVY1", 4)) return false;
    swap32s((uint8_t*)d + 4, (288 - 4) / 4);
    return true;
}

// sounds (DATA/audio, DATA/sfx): plain 16-bit samples, or 'ADP1' (4-bit ADPCM) / 'ULW1' (u-law) with
// a rate and a sample count in front of 8-bit data
inline bool fixRaw(void* d, size_t n, bool) {
    if (n >= 12 && (!memcmp(d, "ADP1", 4) || !memcmp(d, "ULW1", 4))) { swap32s((uint8_t*)d + 4, 2); return true; }
    if (n & 1) return false;
    swap16s(d, n / 2);
    return true;
}

}  // namespace endian
