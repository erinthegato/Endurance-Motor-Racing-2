// PS3 platform: PSL1GHT. The RSX graphics chip driven directly (GCM commands, two small Cg programs in
// ps3/shaders), the controller, sound, files. Same rules as the Vita layer, so the game runs unchanged:
//  * the game lays out its screens at 480 x 272; 2D is drawn through an orthographic 480 x 272
//    projection onto the TV picture (1280 x 720, or what the TV offers), 3D renders at the full size;
//  * the Vita's fixed-function OpenGL is done by ps3/shaders: texture x vertex colour, linear fog
//    (worked out per vertex), alpha test and blending in the RSX's own fixed stages;
//  * per-frame vertex memory (frameAlloc) is memory the RSX reads directly (mapped main memory, two
//    halves used on alternate frames), so the game's per-frame vertices are drawn without a copy;
//    static vertex arrays (models, circuit, pit lane) are copied into video memory on their first
//    draw, as floats, and kept until the game rewrites or frees that memory (gfx::flushCache);
//  * the 8-bit swizzled palette textures are turned into 32-bit RSX textures the first time they are
//    bound (big opaque ones get mipmaps and 4x anisotropic filtering), and cached;
//  * the Cell is big-endian: the data files are turned round as they load (src/endian.h);
//  * relative paths are mapped: "DATA/..." is read from the game's folder, the PS3's own set first
//    (USRDIR/PS3/: the denser car models, from ps3/data), then HD/ (the Vita's), then DATA/; anything else (saves,
//    settings, logs, ghosts) lives in USRDIR/save/.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <cmath>
#include <malloc.h>
#include <unistd.h>
#include <vector>
#include <unordered_map>
#include <string>
#include <zlib.h>

#include <ppu-types.h>
#include <sys/process.h>
#include <sys/thread.h>
#include <lv2/systime.h>
#include <sys/stat.h>
#include <rsx/rsx.h>
#include <sysutil/video.h>
#include <sysutil/sysutil.h>
#include <io/pad.h>
#include <audio/audio.h>

#include "platform.h"
#include "game.h"
#include "data.h"
#include "endian.h"

#include "emr_vp_vpo.h"
#include "emr_fp_tex_fpo.h"
#include "emr_fp_col_fpo.h"

SYS_PROCESS_PARAM(1001, 0x100000);

// ---------------------------------------------------------------- paths
static std::string gameDir = "/dev_hdd0/game/EMRW00027/USRDIR";      // (from argv[0] in main)
static std::string saveDir() { return gameDir + "/save"; }

extern "C" FILE* __real_fopen(const char* path, const char* mode);
extern "C" int __real_remove(const char* path);

static std::string mapPath(const char* in, const char* sub) {
    if (in[0] == '/') return in;                                    // already a full path
    if (!strncmp(in, "./", 2)) in += 2;
    if (!strncmp(in, "DATA/", 5)) return gameDir + "/" + sub + "/" + (in + 5);
    return saveDir() + "/" + in;
}

extern "C" FILE* __wrap_fopen(const char* path, const char* mode) {
    bool reading = mode && mode[0] == 'r';
    if (reading && (!strncmp(path, "DATA/", 5) || !strncmp(path, "./DATA/", 7))) {
        for (const char* sub : {"PS3", "HD", "DATA"})
            if (FILE* f = __real_fopen(mapPath(path, sub).c_str(), mode)) return f;
        return nullptr;
    }
    return __real_fopen(mapPath(path, "DATA").c_str(), mode);
}
extern "C" int __wrap_remove(const char* path) { return __real_remove(mapPath(path, "DATA").c_str()); }

// ---------------------------------------------------------------- graphics
namespace {
gcmContextData* ctx = nullptr;
u32 dispW = 1280, dispH = 720;
const int FB_COUNT = 2;
u32* colorBuf[FB_COUNT]; u32 colorOff[FB_COUNT]; u32 colorPitch = 0;
u32* depthBuf = nullptr; u32 depthOff = 0, depthPitch = 0;
u32 curFb = 0; bool firstFlip = true;
const u8 FENCE_LABEL = 254;
u32 frameNo = 0;                 // frames handed to the RSX so far
// the fence label: the RSX writes frame N + 1 there when it has finished frame N
inline bool frameDone(u32 n) { return (s32)(*(volatile u32*)gcmGetLabelAddress(FENCE_LABEL) - (n + 1)) >= 0; }

// mapped main memory: the command buffer, then the two per-frame halves (frameAlloc)
const u32 CB_SIZE = 2 << 20;
const u32 ARENA_HALF = 16 << 20;
const u32 HOST_SIZE = CB_SIZE + 2 * ARENA_HALF + (1 << 20);
u8* hostMem = nullptr;
u8* arena[2] = {nullptr, nullptr};
int arenaUsed = 0;
inline u8* curArena() { return arena[frameNo & 1]; }
inline bool inArena(const void* p) {
    const u8* b = (const u8*)p;
    return hostMem && b >= arena[0] && b < arena[0] + 2 * ARENA_HALF;
}

// programs
rsxVertexProgram* vpo = (rsxVertexProgram*)emr_vp_vpo;
rsxFragmentProgram* fpTex = (rsxFragmentProgram*)emr_fp_tex_fpo;
rsxFragmentProgram* fpCol = (rsxFragmentProgram*)emr_fp_col_fpo;
void* vpUcode = nullptr;
u32 fpTexOff = 0, fpColOff = 0;
rsxProgramConst *cMvp, *cMvz, *cFogP, *cFogC;
int texUnit = 0;
int curFp = -1;                  // 0 untextured, 1 textured (loaded)

// video memory that the RSX may still be reading: freed two frames later
struct LateFree { void* p; u32 frame; };
std::vector<LateFree> lateFree;
int vramKB = 0;
void freeLater(void* p) { if (p) lateFree.push_back(LateFree{p, frameNo}); }

// what the game asked for
M4 projM = M4::identity(), viewM = M4::identity(), orthoM = M4::identity();
bool fogWanted = false; float fogNear = 0, fogFar = 1; u32 fogCol = 0;
bool depthWanted = true, depthWriteOn = true, in2D = false, insetOn = false;
float vpX = 0, vpY = 0, vpW = 0, vpH = 0;       // the 3D viewport in screen pixels (full screen or the inset)

// RSX textures made from the game's palette textures
struct GpuTex { const u8* pix; const u32* pal; int w, h; void* mem; gcmTexture t; int levels; };
std::vector<GpuTex> texCache;
int lastHit = -1;
const Texture* curTex = nullptr;
const GpuTex* curGpuTex = nullptr;

inline bool overlaps(const void* a, int an, const void* b, int bn) {
    const u8 *x = (const u8*)a, *y = (const u8*)b;
    return x < y + bn && y < x + an;
}

// RSX swizzled layout: the bits of x and y interleaved (x first), the longer side's extra bits on top
inline u32 swz(u32 x, u32 y, u32 w, u32 h) {
    u32 r = 0, bit = 0;
    for (u32 i = 0; (1u << i) < w || (1u << i) < h; i++) {
        if ((1u << i) < w) r |= ((x >> i) & 1) << bit++;
        if ((1u << i) < h) r |= ((y >> i) & 1) << bit++;
    }
    return r;
}
// a 0xAABBGGRR colour as the RSX's A8R8G8B8 texel (bytes A R G B in memory)
inline u32 texel(u32 c) { return (c & 0xFF000000u) | ((c & 0xFF) << 16) | (c & 0xFF00) | ((c >> 16) & 0xFF); }

const GpuTex* upload(const Texture* t) {
    int n = (int)texCache.size();
    for (int i = (lastHit >= 0 ? lastHit : 0), k = 0; k < n; k++, i = (i + 1) % n) {
        const GpuTex& g = texCache[i];
        if (g.pix == t->pixels && g.pal == t->palette && g.w == t->w && g.h == t->h) { lastHit = i; return &texCache[i]; }
    }
    // unswizzle the PSP layout (16 x 8 blocks) and look up the palette
    int w = t->w, h = t->h, bw = w / 16 > 0 ? w / 16 : 1;
    std::vector<u32> rgba((size_t)w * h);
    bool opaque = true;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int off = ((y >> 3) * bw + (x >> 4)) * 128 + (y & 7) * 16 + (x & 15);
            u32 c = t->palette[t->pixels[off]];
            rgba[(size_t)y * w + x] = c;
            if ((c >> 24) != 0xFF) opaque = false;
        }
    bool pow2 = !(w & (w - 1)) && !(h & (h - 1));
    bool mip = opaque && pow2 && w >= 64 && h >= 64;
    int levels = 1;
    if (mip) { int s = w > h ? w : h; while (s > 1) { s >>= 1; levels++; } }
    GpuTex g{};
    g.pix = t->pixels; g.pal = t->palette; g.w = w; g.h = h; g.levels = levels;
    size_t bytes = 0;
    u32 pitch = 0;
    if (pow2) { for (int l = 0; l < levels; l++) bytes += (size_t)(w >> l > 0 ? w >> l : 1) * (h >> l > 0 ? h >> l : 1) * 4; }
    else { pitch = ((u32)w * 4 + 63) & ~63u; bytes = (size_t)pitch * h; }
    g.mem = rsxMemalign(128, (u32)bytes);
    if (!g.mem) return nullptr;
    vramKB += (int)(bytes / 1024);
    u32* dst = (u32*)g.mem;
    if (pow2) {
        // level 0 and the mipmaps (each a box filter of the one before), swizzled, one after another
        std::vector<u32> cur = rgba, next;
        int lw = w, lh = h;
        for (int l = 0; l < levels; l++) {
            for (int y = 0; y < lh; y++) for (int x = 0; x < lw; x++) dst[swz(x, y, lw, lh)] = texel(cur[(size_t)y * lw + x]);
            dst += (size_t)lw * lh;
            if (l + 1 < levels) {
                int nw = lw > 1 ? lw / 2 : 1, nh = lh > 1 ? lh / 2 : 1;
                next.assign((size_t)nw * nh, 0);
                for (int y = 0; y < nh; y++) for (int x = 0; x < nw; x++) {
                    u32 s[4] = {0, 0, 0, 0};
                    for (int k = 0; k < 4; k++) {
                        int sx = x * 2 + (k & 1), sy = y * 2 + (k >> 1);
                        if (sx >= lw) sx = lw - 1; if (sy >= lh) sy = lh - 1;
                        u32 c = cur[(size_t)sy * lw + sx];
                        for (int ch = 0; ch < 4; ch++) s[ch] += (c >> (ch * 8)) & 255;
                    }
                    next[(size_t)y * nw + x] = (s[0] / 4) | ((s[1] / 4) << 8) | ((s[2] / 4) << 16) | ((s[3] / 4) << 24);
                }
                cur.swap(next); lw = nw; lh = nh;
            }
        }
    } else {
        for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) ((u32*)((u8*)g.mem + (size_t)y * pitch))[x] = texel(rgba[(size_t)y * w + x]);
    }
    u32 off = 0; rsxAddressToOffset(g.mem, &off);
    g.t.format = GCM_TEXTURE_FORMAT_A8R8G8B8 | (pow2 ? GCM_TEXTURE_FORMAT_SWZ : GCM_TEXTURE_FORMAT_LIN);
    g.t.mipmap = (u8)levels;
    g.t.dimension = GCM_TEXTURE_DIMS_2D;
    g.t.cubemap = GCM_FALSE;
    g.t.remap = ((GCM_TEXTURE_REMAP_TYPE_REMAP << GCM_TEXTURE_REMAP_TYPE_B_SHIFT) |
                 (GCM_TEXTURE_REMAP_TYPE_REMAP << GCM_TEXTURE_REMAP_TYPE_G_SHIFT) |
                 (GCM_TEXTURE_REMAP_TYPE_REMAP << GCM_TEXTURE_REMAP_TYPE_R_SHIFT) |
                 (GCM_TEXTURE_REMAP_TYPE_REMAP << GCM_TEXTURE_REMAP_TYPE_A_SHIFT) |
                 (GCM_TEXTURE_REMAP_COLOR_B << GCM_TEXTURE_REMAP_COLOR_B_SHIFT) |
                 (GCM_TEXTURE_REMAP_COLOR_G << GCM_TEXTURE_REMAP_COLOR_G_SHIFT) |
                 (GCM_TEXTURE_REMAP_COLOR_R << GCM_TEXTURE_REMAP_COLOR_R_SHIFT) |
                 (GCM_TEXTURE_REMAP_COLOR_A << GCM_TEXTURE_REMAP_COLOR_A_SHIFT));
    g.t.width = (u16)w; g.t.height = (u16)h; g.t.depth = 1;
    g.t.location = GCM_LOCATION_RSX;
    g.t.pitch = pow2 ? 0 : pitch;
    g.t.offset = off;
    texCache.push_back(g);
    lastHit = (int)texCache.size() - 1;
    rsxInvalidateTextureCache(ctx, GCM_INVALIDATE_TEXTURE);
    return &texCache[lastHit];
}

// forget RSX copies made from memory in [p, p + n)
void forget(const void* p, int n) {
    for (int i = (int)texCache.size() - 1; i >= 0; i--) {
        GpuTex& g = texCache[i];
        if (overlaps(p, n, g.pix, g.w * g.h) || overlaps(p, n, g.pal, 1024)) {
            freeLater(g.mem);
            texCache[i] = texCache.back(); texCache.pop_back();
        }
    }
    lastHit = -1;
    curTex = nullptr; curGpuTex = nullptr;
}

// static vertex arrays in video memory (as WorldVertex floats)
struct Vbo { const u8* ptr; int bytes; void* mem; u32 off; };
std::unordered_map<uint64_t, Vbo> vbos;
inline uint64_t vkey(const void* p, int count, bool model) { return ((uint64_t)(uintptr_t)p << 1) ^ ((uint64_t)count << 40) ^ (model ? 1 : 0); }

void forgetVbos(const void* p, int n) {
    for (auto it = vbos.begin(); it != vbos.end();) {
        if (overlaps(p, n, it->second.ptr, it->second.bytes)) { freeLater(it->second.mem); it = vbos.erase(it); }
        else ++it;
    }
}

inline void toWorld(WorldVertex* w, const ModelVertex* v, int count) {
    const float k = 1.0f / 32768.0f;
    for (int i = 0; i < count; i++) {
        const ModelVertex& q = v[i];
        w[i] = WorldVertex{q.u * k, q.v * k, q.color, q.x * k, q.y * k, q.z * k};
    }
}

// where the RSX can read `count` vertices from: (offset, location). Static arrays are copied to video
// memory once; frame memory is read where it is; ModelVertex arrays in frame memory are converted.
bool vertexSource(const void* src, int count, bool model, u32& off, u32& loc) {
    if (inArena(src)) {
        const WorldVertex* w = (const WorldVertex*)src;
        if (model) {
            WorldVertex* c = (WorldVertex*)gfx::frameAlloc(count * (int)sizeof(WorldVertex));
            if (!c) return false;
            toWorld(c, (const ModelVertex*)src, count);
            w = c;
        }
        rsxAddressToOffset((void*)w, &off); loc = GCM_LOCATION_CELL;
        return true;
    }
    uint64_t k = vkey(src, count, model);
    auto it = vbos.find(k);
    if (it == vbos.end()) {
        u32 bytes = (u32)count * sizeof(WorldVertex);
        void* mem = rsxMemalign(128, bytes);
        if (!mem) return false;
        vramKB += (int)(bytes / 1024);
        if (model) toWorld((WorldVertex*)mem, (const ModelVertex*)src, count);
        else memcpy(mem, src, bytes);
        Vbo v{(const u8*)src, count * (int)(model ? sizeof(ModelVertex) : sizeof(WorldVertex)), mem, 0};
        rsxAddressToOffset(mem, &v.off);
        it = vbos.emplace(k, v).first;
    }
    off = it->second.off; loc = GCM_LOCATION_RSX;
    return true;
}

void setViewport(float x, float y, float w, float h) {
    // (x, y) the top left in screen pixels
    f32 scale[4] = {w * 0.5f, h * -0.5f, 0.5f, 0.0f};
    f32 offset[4] = {x + w * 0.5f, y + h * 0.5f, 0.5f, 0.0f};
    rsxSetViewport(ctx, (u16)x, (u16)y, (u16)w, (u16)h, 0.0f, 1.0f, scale, offset);
}
void fullScissor() { rsxSetScissor(ctx, 0, 0, (u16)dispW, (u16)dispH); }

void useFp(bool textured) {
    int want = textured ? 1 : 0;
    if (want == curFp) return;
    curFp = want;
    if (textured) rsxLoadFragmentProgramLocation(ctx, fpTex, fpTexOff, GCM_LOCATION_RSX);
    else rsxLoadFragmentProgramLocation(ctx, fpCol, fpColOff, GCM_LOCATION_RSX);
}

void bindTex(const GpuTex* g) {
    rsxLoadTexture(ctx, texUnit, &g->t);
    rsxTextureControl(ctx, texUnit, GCM_TRUE, 0 << 8, (u16)((g->levels - 1) << 8), g->levels > 1 ? GCM_TEXTURE_MAX_ANISO_4 : GCM_TEXTURE_MAX_ANISO_1);
    rsxTextureFilter(ctx, texUnit, 0, g->levels > 1 ? GCM_TEXTURE_LINEAR_MIPMAP_LINEAR : GCM_TEXTURE_LINEAR, GCM_TEXTURE_LINEAR, GCM_TEXTURE_CONVOLUTION_QUINCUNX);
    rsxTextureWrapMode(ctx, texUnit, GCM_TEXTURE_REPEAT, GCM_TEXTURE_REPEAT, GCM_TEXTURE_CLAMP_TO_EDGE, 0, GCM_TEXTURE_ZFUNC_LESS, 0);
}

// row-major 4x4 for Cg's mul(matrix, vector) from the game's column-major M4
inline void rows(const M4& m, float* out) { for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) out[r * 4 + c] = m.m[c * 4 + r]; }

void setMatrices(const M4& model) {
    float mvp[16], mvz[4], fp[4] = {0, 0, 0, 0}, fc[4];
    if (in2D) {
        rows(orthoM * model, mvp);
        mvz[0] = mvz[1] = mvz[2] = mvz[3] = 0;
    } else {
        M4 mv = viewM * model;
        rows(projM * mv, mvp);
        mvz[0] = mv.m[2]; mvz[1] = mv.m[6]; mvz[2] = mv.m[10]; mvz[3] = mv.m[14];
        if (fogWanted && fogFar > fogNear) { fp[0] = fogFar; fp[1] = 1.0f / (fogFar - fogNear); fp[2] = 1; }
    }
    fc[0] = (fogCol & 255) / 255.0f; fc[1] = ((fogCol >> 8) & 255) / 255.0f; fc[2] = ((fogCol >> 16) & 255) / 255.0f; fc[3] = 1;
    rsxSetVertexProgramParameter(ctx, vpo, cMvp, mvp);
    rsxSetVertexProgramParameter(ctx, vpo, cMvz, mvz);
    rsxSetVertexProgramParameter(ctx, vpo, cFogP, fp);
    rsxSetVertexProgramParameter(ctx, vpo, cFogC, fc);
}

void applyTex() {
    if (curTex && curTex->pixels && curTex->palette) {
        curGpuTex = upload(curTex);
        if (curGpuTex) { useFp(true); bindTex(curGpuTex); return; }
    }
    curGpuTex = nullptr;
    useFp(false);
}

void enter3D() {
    if (!in2D) return;
    in2D = false;
    setViewport(vpX, vpY, vpW, vpH);
    rsxSetDepthTestEnable(ctx, depthWanted ? GCM_TRUE : GCM_FALSE);
}
void enter2D() {
    if (in2D) return;
    in2D = true;
    setViewport(0, 0, (float)dispW, (float)dispH);
    rsxSetDepthTestEnable(ctx, GCM_FALSE);
}

void drawFrom(u32 off, u32 loc, int count, u32 type) {
    const u32 S = sizeof(WorldVertex);
    rsxBindVertexArrayAttrib(ctx, GCM_VERTEX_ATTRIB_POS, 0, off + offsetof(WorldVertex, x), S, 3, GCM_VERTEX_DATA_TYPE_F32, loc);
    rsxBindVertexArrayAttrib(ctx, GCM_VERTEX_ATTRIB_COLOR0, 0, off + offsetof(WorldVertex, color), S, 4, GCM_VERTEX_DATA_TYPE_U8, loc);
    rsxBindVertexArrayAttrib(ctx, GCM_VERTEX_ATTRIB_TEX0, 0, off + offsetof(WorldVertex, u), S, 2, GCM_VERTEX_DATA_TYPE_F32, loc);
    rsxDrawVertexArray(ctx, type, 0, (u32)count);
}

// a rectangle of the screen (pixels) filled with a colour and the far depth, whatever the scissor
// (the inset's clear): drawn as two triangles through the 2D projection, depth test "always"
void clearRect(float x, float y, float w, float h, u32 color, bool colour) {
    bool was2D = in2D;
    enter2D();
    float k = (float)SCREEN_W / dispW, l = (float)SCREEN_H / dispH;
    float x0 = x * k, y0 = y * l, x1 = (x + w) * k, y1 = (y + h) * l;
    WorldVertex* q = (WorldVertex*)gfx::frameAlloc(6 * sizeof(WorldVertex));
    if (!q) return;
    WorldVertex a{0, 0, color, x0, y0, 1}, b{0, 0, color, x1, y0, 1}, c{0, 0, color, x1, y1, 1}, d{0, 0, color, x0, y1, 1};
    q[0] = a; q[1] = b; q[2] = c; q[3] = a; q[4] = c; q[5] = d;
    useFp(false);
    rsxSetDepthTestEnable(ctx, GCM_TRUE);
    rsxSetDepthFunc(ctx, GCM_ALWAYS);
    rsxSetDepthWriteEnable(ctx, GCM_TRUE);
    rsxSetBlendEnable(ctx, GCM_FALSE);
    rsxSetAlphaTestEnable(ctx, GCM_FALSE);            // (a depth-only clear draws with alpha 0)
    if (!colour) rsxSetColorMask(ctx, 0);
    setMatrices(M4::identity());
    u32 off, loc; rsxAddressToOffset(q, &off); loc = GCM_LOCATION_CELL;
    drawFrom(off, loc, 6, GCM_TYPE_TRIANGLES);
    if (!colour) rsxSetColorMask(ctx, GCM_COLOR_MASK_R | GCM_COLOR_MASK_G | GCM_COLOR_MASK_B | GCM_COLOR_MASK_A);
    rsxSetAlphaTestEnable(ctx, GCM_TRUE);
    rsxSetDepthFunc(ctx, GCM_LEQUAL);
    rsxSetDepthWriteEnable(ctx, depthWriteOn ? GCM_TRUE : GCM_FALSE);
    rsxSetDepthTestEnable(ctx, GCM_FALSE);
    if (!was2D) enter3D();
    curTex = nullptr; curGpuTex = nullptr;
}

void setRenderTarget(u32 i) {
    gcmSurface sf;
    memset(&sf, 0, sizeof sf);
    sf.colorFormat = GCM_SURFACE_X8R8G8B8;
    sf.colorTarget = GCM_SURFACE_TARGET_0;
    sf.colorLocation[0] = GCM_LOCATION_RSX; sf.colorOffset[0] = colorOff[i]; sf.colorPitch[0] = colorPitch;
    for (int k = 1; k < 4; k++) { sf.colorLocation[k] = GCM_LOCATION_RSX; sf.colorOffset[k] = 0; sf.colorPitch[k] = 64; }
    sf.depthFormat = GCM_SURFACE_ZETA_Z24S8;
    sf.depthLocation = GCM_LOCATION_RSX; sf.depthOffset = depthOff; sf.depthPitch = depthPitch;
    sf.type = GCM_SURFACE_TYPE_LINEAR;
    sf.antiAlias = GCM_SURFACE_CENTER_1;
    sf.width = (u16)dispW; sf.height = (u16)dispH; sf.x = 0; sf.y = 0;
    rsxSetSurface(ctx, &sf);
}

void defaultState() {
    rsxSetColorMask(ctx, GCM_COLOR_MASK_R | GCM_COLOR_MASK_G | GCM_COLOR_MASK_B | GCM_COLOR_MASK_A);
    rsxSetColorMaskMrt(ctx, 0);
    rsxSetShadeModel(ctx, GCM_SHADE_MODEL_SMOOTH);
    rsxSetCullFaceEnable(ctx, GCM_FALSE);           // the models are not consistently wound
    rsxSetDepthFunc(ctx, GCM_LEQUAL);
    rsxSetDepthTestEnable(ctx, GCM_TRUE);
    rsxSetDepthWriteEnable(ctx, GCM_TRUE);
    rsxSetAlphaFunc(ctx, GCM_GREATER, 8);
    rsxSetAlphaTestEnable(ctx, GCM_TRUE);
    rsxSetBlendFunc(ctx, GCM_SRC_ALPHA, GCM_ONE_MINUS_SRC_ALPHA, GCM_SRC_ALPHA, GCM_ONE_MINUS_SRC_ALPHA);
    rsxSetBlendEquation(ctx, GCM_FUNC_ADD, GCM_FUNC_ADD);
    rsxSetBlendEnable(ctx, GCM_FALSE);
    rsxSetZMinMaxControl(ctx, GCM_FALSE, GCM_TRUE, GCM_FALSE);
    for (int i = 0; i < 8; i++) rsxSetViewportClip(ctx, i, dispW, dispH);
    rsxSetUserClipPlaneControl(ctx, GCM_USER_CLIP_PLANE_DISABLE, GCM_USER_CLIP_PLANE_DISABLE, GCM_USER_CLIP_PLANE_DISABLE,
                               GCM_USER_CLIP_PLANE_DISABLE, GCM_USER_CLIP_PLANE_DISABLE, GCM_USER_CLIP_PLANE_DISABLE);
    rsxLoadVertexProgram(ctx, vpo, vpUcode);
    curFp = -1;
    useFp(false);
}

bool initVideo() {
    static const u32 RES[] = {VIDEO_RESOLUTION_720, VIDEO_RESOLUTION_1080, VIDEO_RESOLUTION_576, VIDEO_RESOLUTION_480};
    videoResolution res; s32 resId = -1;
    for (u32 id : RES) {
        if (videoGetResolutionAvailability(VIDEO_PRIMARY, id, VIDEO_ASPECT_AUTO, 0) != 1) continue;
        if (videoGetResolution(id, &res) == 0) { resId = (s32)id; break; }
    }
    if (resId < 0) return false;
    videoConfiguration cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.resolution = (u8)resId;
    cfg.format = VIDEO_BUFFER_FORMAT_XRGB;
    cfg.aspect = VIDEO_ASPECT_AUTO;
    cfg.pitch = (u32)res.width * 4;
    if (videoConfigure(VIDEO_PRIMARY, &cfg, nullptr, 0) != 0) return false;
    dispW = res.width; dispH = res.height;
    return true;
}
}

namespace gfx {
static int syncEvery = 1;
static float waitSec = 0;
static double lastFlip = 0;
float lastWaitSec() { return waitSec; }
float lastGpuWaitSec() { return 0; }
// vsync off (the benchmark): flip at once, so the frame rate shows the real headroom above 60
void setSync(bool vsync, int every) {
    syncEvery = every < 1 ? 1 : every;
    gcmSetFlipMode(vsync || syncEvery > 1 ? GCM_FLIP_VSYNC : GCM_FLIP_HSYNC);
}

void beginFrame(u32 clear) {
    // this frame's half of the frame memory was last used two frames ago: wait until the RSX is done with it
    if (frameNo >= 2) while (!frameDone(frameNo - 2)) usleep(20);
    arenaUsed = 0;
    for (size_t i = 0; i < lateFree.size();) {
        if (frameDone(lateFree[i].frame)) { rsxFree(lateFree[i].p); lateFree[i] = lateFree.back(); lateFree.pop_back(); }
        else i++;
    }
    setRenderTarget(curFb);
    defaultState();
    insetOn = false;
    vpX = 0; vpY = 0; vpW = (float)dispW; vpH = (float)dispH;
    fullScissor();
    setViewport(0, 0, (float)dispW, (float)dispH);
    rsxSetClearColor(ctx, 0xFF000000u | ((clear & 255) << 16) | (clear & 0xFF00) | ((clear >> 16) & 255));
    rsxSetClearDepthStencil(ctx, 0xFFFFFF00u);
    rsxClearSurface(ctx, GCM_CLEAR_R | GCM_CLEAR_G | GCM_CLEAR_B | GCM_CLEAR_A | GCM_CLEAR_S | GCM_CLEAR_Z);
    depthWanted = true; depthWriteOn = true;
    in2D = true; enter3D();
    curTex = nullptr; curGpuTex = nullptr;
}

void endFrame() {
    rsxSetWriteBackendLabel(ctx, FENCE_LABEL, frameNo + 1);
    double t0 = sys::timeSec();
    if (!firstFlip) { while (gcmGetFlipStatus() != 0) usleep(200); }
    // 30 fps: at least two screen refreshes between pictures
    if (syncEvery > 1) { double want = lastFlip + syncEvery / 60.0 - 0.002; while (sys::timeSec() < want) usleep(500); }
    gcmResetFlipStatus();
    gcmSetFlip(ctx, (u8)curFb);
    rsxFlushBuffer(ctx);
    gcmSetWaitFlip(ctx);
    lastFlip = sys::timeSec();
    waitSec = (float)(lastFlip - t0);
    firstFlip = false;
    curFb ^= 1;
    frameNo++;
}

void clearDepth() {
    if (insetOn) { clearRect(vpX, vpY, vpW, vpH, 0, false); return; }
    rsxSetDepthWriteEnable(ctx, GCM_TRUE);
    rsxClearSurface(ctx, GCM_CLEAR_Z | GCM_CLEAR_S);
    rsxSetDepthWriteEnable(ctx, depthWriteOn ? GCM_TRUE : GCM_FALSE);
}
bool beginInset(float x, float y, float w, float h, u32 c) {
    float kx = (float)dispW / SCREEN_W, ky = (float)dispH / SCREEN_H;
    vpX = floorf(x * kx); vpY = floorf(y * ky); vpW = floorf(w * kx); vpH = floorf(h * ky);
    insetOn = true;
    rsxSetScissor(ctx, (u16)vpX, (u16)vpY, (u16)vpW, (u16)vpH);
    clearRect(vpX, vpY, vpW, vpH, c | 0xFF000000u, true);
    setViewport(vpX, vpY, vpW, vpH);
    in2D = false;
    rsxSetDepthTestEnable(ctx, depthWanted ? GCM_TRUE : GCM_FALSE);
    return true;
}
void endInset() {
    insetOn = false;
    vpX = 0; vpY = 0; vpW = (float)dispW; vpH = (float)dispH;
    fullScissor();
    if (!in2D) setViewport(vpX, vpY, vpW, vpH);
}
void setProjection(const M4& p) { projM = p; }
void setView(const M4& v) { viewM = v; }
void setFog(bool on, float n, float f, u32 c) { fogWanted = on; fogNear = n; fogFar = f; fogCol = c; }
void bindTexture(const Texture* t) {
    const Texture* want = (t && t->pixels && t->palette) ? t : nullptr;
    if (want == curTex && (want != nullptr) == (curGpuTex != nullptr)) return;
    curTex = want; applyTex();
}
void setBlend(bool on) { rsxSetBlendEnable(ctx, on ? GCM_TRUE : GCM_FALSE); }
void setDepthTest(bool on) { depthWanted = on; if (!in2D) rsxSetDepthTestEnable(ctx, on ? GCM_TRUE : GCM_FALSE); }
void setDepthWrite(bool on) { depthWriteOn = on; rsxSetDepthWriteEnable(ctx, on ? GCM_TRUE : GCM_FALSE); }

void drawModel(const ModelVertex* v, int count, const M4& model) {
    if (count <= 0) return;
    enter3D();
    u32 off, loc;
    if (!vertexSource(v, count, true, off, loc)) return;
    setMatrices(model);
    drawFrom(off, loc, count, GCM_TYPE_TRIANGLES);
}
void drawWorld(const WorldVertex* v, int count, Prim prim, const M4& model) {
    if (count <= 0) return;
    static const u32 P[4] = {GCM_TYPE_TRIANGLES, GCM_TYPE_TRIANGLE_STRIP, GCM_TYPE_LINES, GCM_TYPE_LINE_STRIP};
    enter3D();
    u32 off, loc;
    if (!vertexSource(v, count, false, off, loc)) return;
    setMatrices(model);
    drawFrom(off, loc, count, P[prim]);
}
void drawSprites2D(const Sprite2D* s, int n) {
    if (n <= 0) return;
    enter2D();
    WorldVertex* w = (WorldVertex*)frameAlloc(n * 6 * (int)sizeof(WorldVertex));
    if (!w) return;
    for (int i = 0; i < n; i++) {
        const Sprite2D& r = s[i];
        float x0 = r.x, y0 = r.y, x1 = r.x + r.w, y1 = r.y + r.h;
        WorldVertex a{r.u0, r.v0, r.color, x0, y0, 0}, b{r.u1, r.v0, r.color, x1, y0, 0};
        WorldVertex c{r.u1, r.v1, r.color, x1, y1, 0}, d{r.u0, r.v1, r.color, x0, y1, 0};
        WorldVertex* q = w + i * 6;
        q[0] = a; q[1] = b; q[2] = c; q[3] = a; q[4] = c; q[5] = d;
    }
    setMatrices(M4::identity());
    u32 off; rsxAddressToOffset(w, &off);
    drawFrom(off, GCM_LOCATION_CELL, n * 6, GCM_TYPE_TRIANGLES);
}
void drawRect2D(float x, float y, float w, float h, float u0, float v0, float u1, float v1, u32 color) {
    Sprite2D s{x, y, w, h, u0, v0, u1, v1, color};
    drawSprites2D(&s, 1);
}
void drawFill2D(float x, float y, float w, float h, u32 color) {
    const Texture* t = curTex;
    bindTexture(nullptr);
    drawRect2D(x, y, w, h, 0, 0, 0, 0, color);
    bindTexture(t);
}
void* frameAlloc(int bytes) {
    bytes = (bytes + 127) & ~127;                 // (vertex arrays start on 128 bytes)
    if (!hostMem || arenaUsed + bytes > (int)ARENA_HALF) return nullptr;
    void* p = curArena() + arenaUsed; arenaUsed += bytes; return p;
}
void frameTrim(void* p, int keepBytes) {
    u8* b = (u8*)p;
    u8* a = curArena();
    if (!b || b < a || b > a + arenaUsed) return;
    int end = (int)(b - a) + ((keepBytes + 127) & ~127);
    if (end < arenaUsed) arenaUsed = end;
}
void flushCache(const void* p, int bytes) { if (p && bytes > 0 && !inArena(p)) { forget(p, bytes); forgetVbos(p, bytes); } }
bool gpuClips() { return true; }
bool highDetail() { return true; }
bool moveToFastMemory(Texture&) { return false; }     // textures already live in video memory once bound
int fastMark() { return 0; }
int vramUsedKB() { return vramKB; }
void fastRelease(int) {}
}

// ---------------------------------------------------------------- input
namespace sys { static bool runningFlag = true; }
extern "C" {
static void onSysutil(u64 status, u64, void*) { if (status == SYSUTIL_EXIT_GAME) sys::runningFlag = false; }
}
namespace input {
bool hasTouch() { return false; }
void poll(Pad& p) {
    sysUtilCheckCallback();
    static padData last;
    static bool haveLast = false;
    padInfo info;
    ioPadGetInfo(&info);
    int port = -1;
    for (int i = 0; i < 7; i++) if (info.status[i]) { port = i; break; }
    if (port >= 0) {
        padData d;
        if (ioPadGetData(port, &d) == 0 && d.len > 0) { last = d; haveLast = true; }
    } else haveLast = false;
    p = Pad{};
    if (!haveLast) return;
    const padData& d = last;
    p.stickX = ((int)d.ANA_L_H - 128) / 127.0f; p.stickY = ((int)d.ANA_L_V - 128) / 127.0f;
    if (fabsf(p.stickX) < 0.12f) p.stickX = 0;
    if (fabsf(p.stickY) < 0.12f) p.stickY = 0;
    p.rstickX = ((int)d.ANA_R_H - 128) / 127.0f; p.rstickY = ((int)d.ANA_R_V - 128) / 127.0f;
    if (fabsf(p.rstickX) < 0.10f) p.rstickX = 0;
    if (fabsf(p.rstickY) < 0.10f) p.rstickY = 0;
    p.up = d.BTN_UP; p.down = d.BTN_DOWN; p.left = d.BTN_LEFT; p.right = d.BTN_RIGHT;
    p.cross = d.BTN_CROSS; p.circle = d.BTN_CIRCLE; p.square = d.BTN_SQUARE; p.triangle = d.BTN_TRIANGLE;
    p.l = d.BTN_L1 || d.BTN_L2; p.r = d.BTN_R1 || d.BTN_R2;
    p.start = d.BTN_START; p.select = d.BTN_SELECT;
}
}

// ---------------------------------------------------------------- audio
// The game mixes 44100 Hz 16-bit stereo; the PS3 plays 48000 Hz floats in blocks of 256 frames.
namespace audio {
static MixFn mixFn = nullptr;
static volatile bool audioRun = false;
static sys_ppu_thread_t audioThread;
static u32 portNum = 0;
static audioPortConfig portCfg;

static void audioMain(void*) {
    const int IN = 512;
    static int16_t in[(IN + 1) * 2];
    double pos = 0;                 // read position in `in` (frames)
    int have = 0;                   // frames in `in`
    int16_t prevL = 0, prevR = 0;
    u32 block = 1;
    const double step = 44100.0 / 48000.0;
    while (audioRun) {
        u64 reading = *(volatile u64*)(u64)portCfg.readIndex;
        if (block == reading) { usleep(1000); continue; }
        float* out = (float*)(u64)portCfg.audioDataStart + block * 2 * AUDIO_BLOCK_SAMPLES;
        for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
            while ((int)pos + 1 >= have) {
                // keep the last frame for the interpolation, then mix the next stretch
                if (have > 0) { prevL = in[(have - 1) * 2]; prevR = in[(have - 1) * 2 + 1]; }
                pos -= have > 0 ? have - 1 : 0;
                in[0] = prevL; in[1] = prevR;
                MixFn f = mixFn;
                if (f) f(in + 2, IN); else memset(in + 2, 0, IN * 4);
                have = IN + 1;
            }
            int a = (int)pos; float t = (float)(pos - a);
            out[i * 2] = (in[a * 2] * (1 - t) + in[a * 2 + 2] * t) * (1.0f / 32768.0f);
            out[i * 2 + 1] = (in[a * 2 + 1] * (1 - t) + in[a * 2 + 3] * t) * (1.0f / 32768.0f);
            pos += step;
        }
        block = (block + 1) % (u32)portCfg.numBlocks;
    }
    sysThreadExit(0);
}

void start(MixFn fn) {
    mixFn = fn;
    if (audioRun) return;
    if (audioInit() != 0) { sys::log("audioInit failed\n"); return; }
    audioPortParam params;
    params.numChannels = AUDIO_PORT_2CH;
    params.numBlocks = AUDIO_BLOCK_8;
    params.attrib = 0;
    params.level = 1;
    if (audioPortOpen(&params, &portNum) != 0) { sys::log("audioPortOpen failed\n"); return; }
    audioGetPortConfig(portNum, &portCfg);
    audioPortStart(portNum);
    audioRun = true;
    sysThreadCreate(&audioThread, audioMain, nullptr, 900, 0x10000, THREAD_JOINABLE, (char*)"emr_audio");
}
void stop() {
    if (!audioRun) return;
    audioRun = false;
    u64 r; sysThreadJoin(audioThread, &r);
    audioPortStop(portNum);
    audioPortClose(portNum);
    audioQuit();
}
}

// ---------------------------------------------------------------- system
namespace sys {
bool init() {
    // the RSX: mapped main memory for its commands and the per-frame vertices, two screens and a depth buffer
    hostMem = (u8*)memalign(1024 * 1024, HOST_SIZE);
    if (!hostMem || rsxInit(&ctx, CB_SIZE, HOST_SIZE, hostMem) != 0) return false;
    arena[0] = hostMem + CB_SIZE + (1 << 20); arena[1] = arena[0] + ARENA_HALF;
    if (!initVideo()) return false;
    gcmSetFlipMode(GCM_FLIP_VSYNC);
    colorPitch = dispW * 4; depthPitch = dispW * 4;
    for (int i = 0; i < FB_COUNT; i++) {
        colorBuf[i] = (u32*)rsxMemalign(64, dispH * colorPitch);
        rsxAddressToOffset(colorBuf[i], &colorOff[i]);
        gcmSetDisplayBuffer((u8)i, colorOff[i], colorPitch, dispW, dispH);
    }
    depthBuf = (u32*)rsxMemalign(64, dispH * depthPitch);
    rsxAddressToOffset(depthBuf, &depthOff);
    vramKB = (int)((FB_COUNT * dispH * colorPitch + dispH * depthPitch) / 1024);
    *(volatile u32*)gcmGetLabelAddress(FENCE_LABEL) = 0;

    // the programs: the vertex program's code stays in main memory; the fragment programs go to video memory
    u32 vpSize = 0; rsxVertexProgramGetUCode(vpo, &vpUcode, &vpSize);
    cMvp = rsxVertexProgramGetConst(vpo, "mvp");
    cMvz = rsxVertexProgramGetConst(vpo, "mvz");
    cFogP = rsxVertexProgramGetConst(vpo, "fogp");
    cFogC = rsxVertexProgramGetConst(vpo, "fogc");
    for (int k = 0; k < 2; k++) {
        rsxFragmentProgram* fp = k ? fpTex : fpCol;
        void* code = nullptr; u32 size = 0;
        rsxFragmentProgramGetUCode(fp, &code, &size);
        void* mem = rsxMemalign(64, size);
        memcpy(mem, code, size);
        rsxAddressToOffset(mem, k ? &fpTexOff : &fpColOff);
    }
    rsxProgramAttrib* tu = rsxFragmentProgramGetAttrib(fpTex, "tex");
    texUnit = tu ? tu->index : 0;
    // the 2D projection: x 0..480 to the right, y 0..272 down, z as it is (the inset clear draws at z 1)
    orthoM = M4::identity();
    orthoM.m[0] = 2.0f / SCREEN_W; orthoM.m[12] = -1;
    orthoM.m[5] = -2.0f / SCREEN_H; orthoM.m[13] = 1;
    orthoM.m[10] = 1;

    ioPadInit(7);
    sysUtilRegisterCallback(0, onSysutil, nullptr);
    mkdir(saveDir().c_str(), 0755);
    log("Endurance Motor Racing 2.0 (PS3): %ux%u, game folder %s\n", dispW, dispH, gameDir.c_str());
    return true;
}
void shutdown() {
    gcmSetWaitFlip(ctx);
    rsxFinish(ctx, 1);
}
bool running() { return runningFlag; }
void quit() { runningFlag = false; }
int memUsedKB() { struct mallinfo mi = mallinfo(); return (int)(mi.uordblks / 1024); }
double timeSec() { return sysGetSystemTime() / 1000000.0; }
void* loadFile(const char* path, int* size) {
    FILE* f = fopen(path, "rb");    // wrapped: DATA/ -> PS3/, HD/, DATA/
    if (!f) return nullptr;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    void* p = memalign(64, (n + 63) & ~63);
    if (!p) { fclose(f); return nullptr; }
    if (fread(p, 1, n, f) != (size_t)n) { fclose(f); free(p); return nullptr; }
    fclose(f);
    // 'FLZ1' u32 size (little-endian), then a zlib stream (tools/pack_files.py)
    if (n >= 8 && !memcmp(p, "FLZ1", 4)) {
        uint32_t raw = endian::le32((char*)p + 4);
        void* q = memalign(64, (raw + 63) & ~63);
        uLongf got = raw;
        if (!q || uncompress((Bytef*)q, &got, (const Bytef*)p + 8, (uLong)(n - 8)) != Z_OK || got != raw) { free(p); free(q); return nullptr; }
        free(p); p = q; n = (long)raw;
    }
    if (size) *size = (int)n;
    gfx::flushCache(p, (int)n);   // memory reused from a freed file: drop old copies made from it
    return p;
}
void freeFile(void* p) {
    if (!p) return;
    gfx::flushCache(p, (int)malloc_usable_size(p));
    free(p);
}
void log(const char* fmt, ...) {
    char buf[1024];
    va_list a; va_start(a, fmt); vsnprintf(buf, sizeof buf, fmt, a); va_end(a);
    fputs(buf, stdout);              // (RPCS3 shows this in its TTY log)
    FILE* f = fopen("log.txt", "a");
    if (f) { fputs(buf, f); fclose(f); }
}
}

// ---------------------------------------------------------------- main
static Game game;
int main(int argc, char** argv) {
    // the game's folder: where this EBOOT.BIN is (/dev_hdd0/game/EMRW00027/USRDIR)
    if (argc > 0 && argv[0]) {
        std::string self = argv[0];
        size_t k = self.rfind('/');
        if (k != std::string::npos && self.find("/USRDIR") != std::string::npos) gameDir = self.substr(0, k);
    }
    if (!sys::init()) return 1;
    remove("log.txt");
    if (!game.init()) sys::log("DATA folder missing or incomplete\n");
    double last = sys::timeSec();
    while (sys::running()) {
        double now = sys::timeSec();
        float dt = (float)(now - last);
        last = now;
        Pad p;
        input::poll(p);
        game.frame(dt, p);
    }
    game.shutdown();
    audio::stop();
    sys::shutdown();
    return 0;
}
