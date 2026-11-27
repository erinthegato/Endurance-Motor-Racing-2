// PS Vita platform: sceGxm (the Vita's own graphics API, see "graphics" below), buttons, sound, files.
// Same rules as the PSP layer, so the whole game runs unchanged:
//  * the game still lays out its screens at 480 x 272; 2D is drawn through an orthographic
//    480 x 272 projection onto the 960 x 544 screen, 3D renders at the full 960 x 544 with 2x MSAA;
//  * static vertex arrays (models, circuit, pit lane) live in GPU memory after their first draw;
//  * the PSP's 8-bit swizzled palette textures are turned into 16/32-bit textures the first time they
//    are bound, and cached. gfx::flushCache (which the game calls after loading or changing any
//    pixels, because the PSP needs it) tells this layer to forget the old copy;
//  * relative paths are mapped: "DATA/..." is read from the app (app0:), anything else (saves,
//    settings, logs, ghosts, the music pack) lives in uma0:data/EnduranceMotorRacing2/ (ux0:data/... on a
//    Vita without a uma0 drive; files left on ux0 are still read). A model in "HD/" overrides the
//    PSP one in "DATA/" (higher detail for the Vita).
#include <vitasdk.h>
#include <vitashark.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <malloc.h>
#include <vector>
#include <unordered_map>
#include <string>
#include <cstddef>
#include <initializer_list>
#include "platform.h"
#include <zlib.h>
#include "game.h"
#include "data.h"

extern "C" {
// The game's own heap (models, circuits, sounds): a full 20-car race peaks at ~15 MB on the PC test
// (FL_MEM=1) and ~29 MB on the Vita, so 48 MB still leaves ~19 MB spare. The graphics memory pools are
// separate blocks (see startGpu). (It was 64 MB; before that 200 MB.)
int _newlib_heap_size_user = 48 * 1024 * 1024;
unsigned int sceUserMainThreadStackSize = 2 * 1024 * 1024;
}

// vita-elf-create appends ~3.5 KB of module data after the code segment and fails ("Cannot allocate
// ... bytes for SCE data at end of segment 0; segment 1 overlaps") when the code happens to end just
// short of the data segment. This read-only block moves the end of the code segment; if that error
// ever comes back after a code change, change VITA_PAD (any multiple of 1 KB).
#ifndef VITA_PAD
#define VITA_PAD 6144
#endif
extern "C" __attribute__((used, aligned(16))) const unsigned char emr_segment_pad[VITA_PAD] = {1};

// the game's own folder: uma0 when the Vita has that drive (a memory card in the SD2Vita / USB slot),
// else ux0. Anything not found there is looked for in the old ux0 folder, so saves, settings and a music
// pack from before still load (the next save is written to uma0).
#define SAVE_UX0 "ux0:data/EnduranceMotorRacing2"
#define SAVE_UMA0 "uma0:data/EnduranceMotorRacing2"
static const char* saveDir() {
    static int uma = -1;
    if (uma < 0) {
        SceUID d = sceIoDopen("uma0:");
        uma = d >= 0 ? 1 : 0;
        if (d >= 0) sceIoDclose(d);
    }
    return uma ? SAVE_UMA0 : SAVE_UX0;
}
#define SAVE_DIR saveDir()
static const int VITA_W = 960, VITA_H = 544;

// ---------------------------------------------------------------- paths
// The game opens files by relative path (next to the EBOOT on the PSP). The linker sends every
// fopen / remove here (-Wl,--wrap=fopen,--wrap=remove).
extern "C" FILE* __real_fopen(const char* path, const char* mode);
extern "C" int __real_remove(const char* path);

static void mapPath(const char* in, char* out, int n, bool hdFirst) {
    if (strchr(in, ':')) { snprintf(out, n, "%s", in); return; }   // already a device path
    if (!strncmp(in, "DATA/", 5)) {
        if (hdFirst) snprintf(out, n, "app0:HD/%s", in + 5);
        else snprintf(out, n, "app0:%s", in);
        return;
    }
    snprintf(out, n, "%s/%s", SAVE_DIR, in);
}

extern "C" FILE* __wrap_fopen(const char* path, const char* mode) {
    char p[512];
    bool reading = mode && mode[0] == 'r';
    if (reading && !strncmp(path, "DATA/", 5)) {
        mapPath(path, p, sizeof p, true);
        if (FILE* f = __real_fopen(p, mode)) return f;
        mapPath(path, p, sizeof p, false);
        if (FILE* f = __real_fopen(p, mode)) return f;
        // not in the app: the data packs installed beside the saves (the music pack puts the
        // race music in uma0:data/EnduranceMotorRacing2/DATA/music/, so the VPK stays small)
        snprintf(p, sizeof p, "%s/%s", SAVE_DIR, path);
        if (FILE* f = __real_fopen(p, mode)) return f;
        snprintf(p, sizeof p, SAVE_UX0 "/%s", path);           // (a music pack installed on ux0 before)
        return __real_fopen(p, mode);
    }
    mapPath(path, p, sizeof p, false);
    FILE* f = __real_fopen(p, mode);
    if (!f && reading && strcmp(SAVE_DIR, SAVE_UX0) && !strchr(path, ':')) {
        snprintf(p, sizeof p, SAVE_UX0 "/%s", path);           // saves and settings from before the move
        f = __real_fopen(p, mode);
    }
    return f;
}
extern "C" int __wrap_remove(const char* path) {
    char p[512]; mapPath(path, p, sizeof p, false);
    return __real_remove(p);
}

// ---------------------------------------------------------------- graphics (sceGxm)
// The Vita's own graphics API, no OpenGL layer in between. One shader pair (compiled when the game
// starts by the system's compiler, libshacccg.suprx, through vitaShaRK) does everything the old fixed
// function path did: texture x vertex colour, alpha test (> 8/255), linear fog. Three vertex layouts
// feed it: the game's float vertices, its 16-bit model vertices (read as they are, no conversion) and
// (later) the Blender cars' meshes.
//  * GPU memory: one pool in video memory (CDRAM), one in uncached main memory when that fills up.
//    Memory the GPU may still be reading is only given back once the frame that used it has finished
//    (each frame's end writes its number to a notification the GPU sets when it's done).
//  * per-frame vertices (gfx::frameAlloc) come from two arenas in GPU-visible memory, used in turn.
//  * insets (the rear-view mirror) are cut out with the stencil buffer: pixel exact, like glScissor.
namespace {
const int ARENA_SIZE = 16 << 20;
u8* arenas[2] = {nullptr, nullptr};
u8* arena = nullptr;
int arenaUsed = 0;
inline bool inArena(const void* p) {
    for (u8* a : arenas) if (a && (const u8*)p >= a && (const u8*)p < a + ARENA_SIZE) return true;
    return false;
}
inline bool overlaps(const void* a, int an, const void* b, int bn) {
    const u8 *x = (const u8*)a, *y = (const u8*)b;
    return x < y + bn && y < x + an;
}

// ---- memory the GPU can see
const unsigned CDRAM_ALIGN = 256 * 1024, RAM_ALIGN = 4 * 1024;
void* gpuBlock(SceKernelMemBlockType type, unsigned size, unsigned* sizeOut = nullptr) {
    unsigned al = (type == SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW) ? CDRAM_ALIGN : RAM_ALIGN;
    size = (size + al - 1) & ~(al - 1);
    SceUID uid = sceKernelAllocMemBlock("emr_gpu", type, size, nullptr);
    if (uid < 0) return nullptr;
    void* p = nullptr;
    sceKernelGetMemBlockBase(uid, &p);
    if (sceGxmMapMemory(p, size, (SceGxmMemoryAttribFlags)(SCE_GXM_MEMORY_ATTRIB_READ | SCE_GXM_MEMORY_ATTRIB_WRITE)) < 0) {
        sceKernelFreeMemBlock(uid);
        return nullptr;
    }
    if (sizeOut) *sizeOut = size;
    return p;
}
void* usseBlock(unsigned size, bool vertex, unsigned* offset) {
    size = (size + RAM_ALIGN - 1) & ~(RAM_ALIGN - 1);
    SceUID uid = sceKernelAllocMemBlock("emr_usse", SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_NC_RW, size, nullptr);
    if (uid < 0) return nullptr;
    void* p = nullptr;
    sceKernelGetMemBlockBase(uid, &p);
    int r = vertex ? sceGxmMapVertexUsseMemory(p, size, offset) : sceGxmMapFragmentUsseMemory(p, size, offset);
    if (r < 0) { sceKernelFreeMemBlock(uid); return nullptr; }
    return p;
}

// first-fit allocator over one big block
struct Pool {
    u8* base = nullptr;
    unsigned size = 0, usedBytes = 0;
    struct Range { unsigned off, len; };
    std::vector<Range> freeList;                         // sorted by offset, never touching
    std::unordered_map<uintptr_t, Range> used;           // returned address -> what it took
    void init(void* b, unsigned s) { base = (u8*)b; size = s; usedBytes = 0; freeList.assign(1, Range{0, s}); used.clear(); }
    bool owns(const void* p) const { return base && (const u8*)p >= base && (const u8*)p < base + size; }
    void* alloc(unsigned n, unsigned align) {
        if (!base || !n) return nullptr;
        for (size_t i = 0; i < freeList.size(); i++) {
            unsigned o = freeList[i].off, s = freeList[i].len;
            uintptr_t a = ((uintptr_t)base + o + align - 1) & ~(uintptr_t)(align - 1);
            unsigned ao = (unsigned)(a - (uintptr_t)base), end = ao + n;
            if (end > o + s) continue;
            if (end == o + s) freeList.erase(freeList.begin() + i);
            else freeList[i] = Range{end, o + s - end};
            used[a] = Range{o, end - o};
            usedBytes += end - o;
            return (void*)a;
        }
        return nullptr;
    }
    void release(void* p) {
        auto it = used.find((uintptr_t)p);
        if (it == used.end()) return;
        Range r = it->second;
        used.erase(it);
        usedBytes -= r.len;
        size_t i = 0;
        while (i < freeList.size() && freeList[i].off < r.off) i++;
        freeList.insert(freeList.begin() + i, r);
        if (i + 1 < freeList.size() && freeList[i].off + freeList[i].len == freeList[i + 1].off) {
            freeList[i].len += freeList[i + 1].len; freeList.erase(freeList.begin() + i + 1);
        }
        if (i > 0 && freeList[i - 1].off + freeList[i - 1].len == freeList[i].off) {
            freeList[i - 1].len += freeList[i].len; freeList.erase(freeList.begin() + i);
        }
    }
};
Pool vram, ram;

// frames: each one's end sets a notification once the GPU has finished it
volatile unsigned int* notifyAddr = nullptr;
unsigned frameNo = 1;                                    // the frame being built
inline unsigned framesDone() { return notifyAddr ? *notifyAddr : 0; }
struct Pending { void* p; unsigned frame; };
std::vector<Pending> pending;
void collect() {
    unsigned done = framesDone();
    for (size_t i = 0; i < pending.size();) {
        if ((int)(done - pending[i].frame) >= 0) {
            if (vram.owns(pending[i].p)) vram.release(pending[i].p); else ram.release(pending[i].p);
            pending[i] = pending.back(); pending.pop_back();
        } else i++;
    }
}
void* gpuAlloc(unsigned n, unsigned align = 16) {
    void* p = vram.alloc(n, align);
    if (!p) p = ram.alloc(n, align);
    if (!p && !pending.empty()) {                       // what finished frames gave back
        collect();
        p = vram.alloc(n, align);
        if (!p) p = ram.alloc(n, align);
    }
    return p;
}
void gpuFree(void* p) { if (p) pending.push_back(Pending{p, frameNo}); }

// ---- the GPU's objects
const int BUFFERS = 3;                                   // triple buffered display
SceGxmContext* ctx = nullptr;
SceGxmShaderPatcher* patcher = nullptr;
SceGxmRenderTarget* target = nullptr;
SceGxmColorSurface surface[BUFFERS];
void* surfaceMem[BUFFERS];
SceGxmSyncObject* syncObj[BUFFERS];
int backBuf = 0, frontBuf = BUFFERS - 1;
SceGxmDepthStencilSurface depthSurface;
SceGxmNotification frameNotify;
bool gpuReady = false, inScene = false;

const SceGxmProgram* vProg = nullptr;
const SceGxmProgram* fProg = nullptr;
const SceGxmProgramParameter *uMvp = nullptr, *uMv = nullptr, *uPrm = nullptr, *uFog = nullptr, *uFogND = nullptr;
SceGxmVertexProgram* vpWorld = nullptr;                  // WorldVertex: float uv, colour, float xyz
SceGxmVertexProgram* vpModel = nullptr;                  // ModelVertex: u16 uv, colour, s16 xyz
SceGxmFragmentProgram* fpOpaque = nullptr;
SceGxmFragmentProgram* fpBlend = nullptr;
SceGxmFragmentProgram* fpNoColour = nullptr;             // depth / stencil only (clears)
// The same three without the alpha test's discard: a shader that can discard switches off the GPU's
// hidden-surface removal (the PowerVR chip then shades every layer of overdraw), so opaque draws --
// a texture with no see-through texels, no blending -- and blended draws that don't write depth use these
SceGxmFragmentProgram* fpOpaqueND = nullptr;
SceGxmFragmentProgram* fpBlendND = nullptr;
SceGxmFragmentProgram* fpNoColourND = nullptr;
bool curOpaque = true;                                   // the bound texture has no see-through texels
uint32_t* linearIdx = nullptr;                           // 0, 1, 2 ... for the plain draws
const unsigned LINEAR_IDX = 1u << 20;
SceGxmTexture whiteTex;                                  // "no texture": vertex colour only

// The one shader pair. Vertex: positions and texture coordinates are scaled by uPrm.xy (1/32768 for
// the 16-bit models), fog factor (GL_LINEAR on the distance along the view) goes in vTex.z.
const char* VERT_SRC = R"(
void main(
    float3 aPos,
    float4 aCol,
    float2 aUv,
    uniform float4x4 uMvp,
    uniform float4x4 uMv,
    uniform float4 uPrm,
    float4 out vPos : POSITION,
    float4 out vCol : COLOR0,
    float3 out vTex : TEXCOORD0)
{
    float4 p = float4(aPos * uPrm.x, 1.0f);
    vPos = mul(p, uMvp);
    float d = abs(mul(p, uMv).z);
    vCol = aCol;
    vTex = float3(aUv * uPrm.y, saturate((uPrm.z - d) * uPrm.w));
}
)";
const char* FRAG_SRC = R"(
float4 main(
    float4 vCol : COLOR0,
    float3 vTex : TEXCOORD0,
    uniform sampler2D uTex,
    uniform float4 uFog) : COLOR
{
    float4 c = tex2D(uTex, vTex.xy) * vCol;
    if (c.a <= 0.0314f) discard;
    c.rgb = lerp(uFog.rgb, c.rgb, vTex.z);
    return c;
}
)";

// The Blender cars (CarMesh): lit per pixel like the PC's models, in a cut-down form -- light from the
// sky above and the ground below by which way the surface faces, the sun, a highlight, and the sky
// reflected in shiny paint and glass (Fresnel: more at a glancing look), squeezed into the screen's
// range the PC's way (ACES film curve). No shadows. Positions and normals go to the camera-centred
// world (uModel: the camera sits at 0), where the sun and the sky are given.
const char* LIT_VERT_SRC = R"(
void main(
    float3 aPos,
    float3 aNrm,
    float4 aCol,
    float2 aUv,
    uniform float4x4 uMvp,
    uniform float4x4 uMv,
    uniform float4x4 uModel,
    uniform float4 uPrm,
    float4 out vPos : POSITION,
    float4 out vCol : COLOR0,
    float3 out vTex : TEXCOORD0,
    float3 out vN : TEXCOORD1,
    float3 out vP : TEXCOORD2)
{
    float4 p = float4(aPos, 1.0f);
    vPos = mul(p, uMvp);
    float d = abs(mul(p, uMv).z);
    vCol = aCol;
    vTex = float3(aUv, saturate((uPrm.z - d) * uPrm.w));
    vN = mul(float4(aNrm, 0.0f), uModel).xyz;
    vP = mul(p, uModel).xyz;
}
)";
const char* LIT_FRAG_SRC = R"(
float3 lin(float3 c) { return c * c; }
float3 film(float3 x) {
    x = (x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f);
    return sqrt(saturate(x));
}
float4 main(
    float4 vCol : COLOR0,
    float3 vTex : TEXCOORD0,
    float3 vN : TEXCOORD1,
    float3 vP : TEXCOORD2,
    uniform sampler2D uTex,
    uniform float4 uFog,
    uniform float4 uSun,
    uniform float4 uSunCol,
    uniform float4 uSky,
    uniform float4 uGround,
    uniform float4 uZenith,
    uniform float4 uMat) : COLOR
{
    float4 c = tex2D(uTex, vTex.xy) * vCol;
    if (c.a <= 0.0314f) discard;
    float3 N = normalize(vN);
    float3 V = normalize(-vP);
    if (dot(N, V) < 0.0f) N = -N;
    float3 amb = lerp(uGround.rgb, uSky.rgb, N.z * 0.5f + 0.5f);
    float d = max(dot(N, uSun.xyz), 0.0f);
    float3 H = normalize(uSun.xyz + V);
    float s = d > 0.0f ? pow(max(dot(N, H), 0.0001f), uMat.y) * uMat.x * (uMat.y + 8.0f) / 40.0f : 0.0f;
    float rough = 1.0f - saturate(uMat.y / 300.0f);
    float F0 = lerp(0.02f, 0.06f, uMat.x);
    float Fr = (F0 + (1.0f - F0) * pow(1.0f - max(dot(N, V), 0.0f), 5.0f)) * saturate(uMat.x * uMat.x * 0.9f);
    float3 R = reflect(-V, N);
    float w = lerp(0.012f, 0.45f, rough);
    float3 sky = lerp(lin(uFog.rgb), lin(uZenith.rgb), smoothstep(0.0f, 0.55f, R.z)) * 1.5f;
    float3 env = lerp(uGround.rgb * uGround.rgb * 0.35f, sky, smoothstep(-w, w * 0.4f, R.z));
    env += lin(uSunCol.rgb) * pow(max(dot(R, uSun.xyz), 0.0001f), lerp(3000.0f, 40.0f, rough)) * lerp(60.0f, 1.5f, rough) * (d > 0.0f ? 1.0f : 0.3f);
    float3 diff = lin(c.rgb) * (lin(amb) * 2.2f + lin(uSunCol.rgb) * d * 1.6f) * 0.62f;
    float so = saturate(max(vCol.r, max(vCol.g, vCol.b)) * 1.4f - 0.15f);
    float3 col = film(diff * (1.0f - Fr) + (env * Fr + lin(uSunCol.rgb) * s * 0.8f) * so);
    return float4(lerp(uFog.rgb, col, vTex.z), c.a);
}
)";
const SceGxmProgram* vLitProg = nullptr;
const SceGxmProgram* fLitProg = nullptr;
const SceGxmProgramParameter *lMvp = nullptr, *lMv = nullptr, *lModel = nullptr, *lPrm = nullptr;
// the lit fragment program's uniforms: [0] with the discard, [1] without (each program has its own layout):
// fog, sun, sun colour, sky, ground, zenith, material
const SceGxmProgramParameter* lFrag[2][7] = {};
SceGxmVertexProgram* vpMesh = nullptr;                   // MeshVertex: float xyz, s8 normal, colour, float uv
SceGxmFragmentProgram* fpLit = nullptr;
SceGxmFragmentProgram* fpLitBlend = nullptr;
SceGxmFragmentProgram* fpLitND = nullptr;                // (opaque meshes: no discard, see fpOpaqueND)
float litSun[4] = {0.45f, 0.35f, 0.82f, 0}, litSunCol[4] = {0.6f, 0.58f, 0.52f, 0}, litSky[4] = {0.66f, 0.72f, 0.82f, 0};
float litGround[4] = {0.40f, 0.40f, 0.38f, 0}, litZenith[4] = {0.47f, 0.67f, 0.90f, 0}, litMat[4] = {0.08f, 12, 0, 0};
bool meshReady = false;

// the same shader without its alpha test (see fpOpaqueND)
std::string noDiscard(const char* src) {
    std::string s = src;
    size_t a = s.find("if (c.a <= 0.0314f) discard;");
    if (a != std::string::npos) s.erase(a, strlen("if (c.a <= 0.0314f) discard;"));
    return s;
}
const SceGxmProgram* compile(const char* src, bool vertex) {
    uint32_t size = (uint32_t)strlen(src);
    SceGxmProgram* p = shark_compile_shader(src, &size, vertex ? SHARK_VERTEX_SHADER : SHARK_FRAGMENT_SHADER);
    if (!p) {
        if (const SceShaccCgCompileOutput* o = shark_get_internal_compile_output())
            for (int i = 0; i < o->diagnosticCount; i++) sys::log("shader: %s\n", o->diagnostics[i].message);
        shark_clear_output();
        return nullptr;
    }
    void* copy = memalign(16, size);
    memcpy(copy, p, size);
    shark_clear_output();
    return (const SceGxmProgram*)copy;
}

// ---- what the game asked for (2D drawing changes some of it for a moment)
M4 projM = M4::identity(), viewM = M4::identity();
bool fogWanted = false, depthWanted = true, depthWriteWanted = true, blendOn = false, in2D = false;
float fogEnd = 1e20f, fogInv = 1;
float fogCol[4] = {0.72f, 0.78f, 0.86f, 1};             // (also the haze the lit cars reflect: a sky colour until set)
int insetRef = 0;                                       // stencil value of the current inset (0: none)

// RGBA copies of the game's palette textures
struct GpuTex { const u8* pix; const u32* pal; int w, h; void* mem; SceGxmTexture tex; bool opaque = true; };
std::vector<GpuTex*> texCache;                          // (pointers: a bound texture never moves)
int lastHit = -1;
const Texture* curTex = nullptr;
const SceGxmTexture* curGxmTex = nullptr;

inline bool isPow2(int v) { return v > 0 && !(v & (v - 1)); }

bool buildTexture(std::vector<u32>& rgba, int w, int h, bool opaque, bool wantMip, void*& memOut, SceGxmTexture& texOut);
int upload(const Texture* t) {
    for (int i = (lastHit >= 0 ? lastHit : 0), k = 0; k < (int)texCache.size(); k++, i = (i + 1) % (int)texCache.size()) {
        const GpuTex& g = *texCache[i];
        if (g.pix == t->pixels && g.pal == t->palette && g.w == t->w && g.h == t->h) { lastHit = i; return i; }
    }
    // unswizzle (16 x 8 blocks) and look up the palette
    int w = t->w, h = t->h, bw = w / 16 > 0 ? w / 16 : 1;
    std::vector<u32> rgba((size_t)w * h);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int off = ((y >> 3) * bw + (x >> 4)) * 128 + (y & 7) * 16 + (x & 15);
            rgba[(size_t)y * w + x] = t->palette[t->pixels[off]];
        }
    // fully opaque textures go to the GPU as 16-bit colour (half the memory and bandwidth); anything
    // with see-through parts stays 32-bit. Big opaque power-of-two ones get mipmaps (no shimmering in
    // the distance); HUD, font and cut-out ones don't.
    bool opaque = true;
    for (size_t i = 0; i < rgba.size() && opaque; i++) opaque = (rgba[i] >> 24) == 0xFF;
    GpuTex* gp = new GpuTex{t->pixels, t->palette, w, h, nullptr, {}, opaque};
    if (!buildTexture(rgba, w, h, opaque, opaque && w >= 64 && h >= 64, gp->mem, gp->tex)) { delete gp; return -1; }
    texCache.push_back(gp);
    lastHit = (int)texCache.size() - 1;
    return lastHit;
}

// RGBA pixels (A B G R words) -> a GPU texture: 16-bit colour when opaque, else 32-bit; mipmaps (2 x 2
// box filter) when asked for and the size is a power of two. Rows are padded to 8 texels, one level
// after the other.
bool buildTexture(std::vector<u32>& rgba, int w, int h, bool opaque, bool wantMip, void*& memOut, SceGxmTexture& texOut) {
    bool pow2 = isPow2(w) && isPow2(h);
    bool mip = wantMip && pow2;
    int bpp = opaque ? 2 : 4;
    // the levels: each row padded to 8 texels, one level after the other
    int levels = 1;
    unsigned bytes = (unsigned)(((w + 7) & ~7) * h * bpp);
    if (mip) {
        for (int lw = w, lh = h; lw > 1 && lh > 1;) {
            lw /= 2; lh /= 2;
            bytes += (unsigned)(((lw + 7) & ~7) * lh * bpp);
            levels++;
        }
    }
    void* mem = gpuAlloc(bytes, 16);
    if (!mem) { sys::log("gpu: out of memory for a %dx%d texture\n", w, h); return false; }
    u8* dst = (u8*)mem;
    std::vector<u32> cur = rgba, next;
    int lw = w, lh = h;
    for (int L = 0; L < levels; L++) {
        int stride = (lw + 7) & ~7;
        for (int y = 0; y < lh; y++) {
            const u32* src = &cur[(size_t)y * lw];
            if (opaque) {
                u16* row = (u16*)dst + (size_t)y * stride;
                for (int x = 0; x < lw; x++) {
                    u32 c = src[x];                       // A B G R (bytes R, G, B, A)
                    u32 r = c & 0xFF, g = (c >> 8) & 0xFF, b = (c >> 16) & 0xFF;
                    row[x] = (u16)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
                }
            } else {
                memcpy((u32*)dst + (size_t)y * stride, src, (size_t)lw * 4);
            }
        }
        dst += (size_t)stride * lh * bpp;
        if (L + 1 < levels) {                             // 2 x 2 box filter for the next level
            int nw = lw / 2, nh = lh / 2;
            next.resize((size_t)nw * nh);
            for (int y = 0; y < nh; y++)
                for (int x = 0; x < nw; x++) {
                    u32 a = cur[(size_t)(2 * y) * lw + 2 * x], b = cur[(size_t)(2 * y) * lw + 2 * x + 1];
                    u32 c = cur[(size_t)(2 * y + 1) * lw + 2 * x], d = cur[(size_t)(2 * y + 1) * lw + 2 * x + 1];
                    u32 o = 0;
                    for (int s = 0; s < 32; s += 8)
                        o |= ((((a >> s) & 255) + ((b >> s) & 255) + ((c >> s) & 255) + ((d >> s) & 255) + 2) / 4) << s;
                    next[(size_t)y * nw + x] = o;
                }
            cur.swap(next);
            lw = nw; lh = nh;
        }
    }
    memOut = mem;
    sceGxmTextureInitLinear(&texOut, mem, opaque ? SCE_GXM_TEXTURE_FORMAT_U5U6U5_RGB : SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR, w, h, levels);
    sceGxmTextureSetMinFilter(&texOut, SCE_GXM_TEXTURE_FILTER_LINEAR);
    sceGxmTextureSetMagFilter(&texOut, SCE_GXM_TEXTURE_FILTER_LINEAR);
    sceGxmTextureSetMipFilter(&texOut, mip ? SCE_GXM_TEXTURE_MIP_FILTER_ENABLED : SCE_GXM_TEXTURE_MIP_FILTER_DISABLED);
    SceGxmTextureAddrMode am = pow2 ? SCE_GXM_TEXTURE_ADDR_REPEAT : SCE_GXM_TEXTURE_ADDR_CLAMP;
    sceGxmTextureSetUAddrMode(&texOut, am);
    sceGxmTextureSetVAddrMode(&texOut, am);
    return true;
}

// forget GPU copies made from memory in [p, p + n)
void forget(const void* p, int n) {
    for (int i = (int)texCache.size() - 1; i >= 0; i--) {
        GpuTex* g = texCache[i];
        if (overlaps(p, n, g->pix, g->w * g->h) || overlaps(p, n, g->pal, 1024)) {
            if (curGxmTex == &g->tex) { curTex = nullptr; curGxmTex = &whiteTex; curOpaque = true; }   // (it was the bound one)
            gpuFree(g->mem);
            delete g;
            texCache[i] = texCache.back(); texCache.pop_back();
            lastHit = -1;
        }
    }
}

// GPU copies of static vertex arrays (models, circuit chunks, pit lane): made on first draw, kept until
// the game rewrites that memory (gfx::flushCache) or frees it. The models stay 16-bit.
struct Vbo { const u8* ptr; int bytes; void* mem; };
std::unordered_map<uint64_t, Vbo> vbos;
inline uint64_t vkey(const void* p, int count, bool model) { return ((uint64_t)(uintptr_t)p << 1) ^ ((uint64_t)count << 40) ^ (model ? 1 : 0); }

void forgetVbos(const void* p, int n) {
    for (auto it = vbos.begin(); it != vbos.end();) {
        if (overlaps(p, n, it->second.ptr, it->second.bytes)) { gpuFree(it->second.mem); it = vbos.erase(it); }
        else ++it;
    }
}

// where the GPU can read these vertices: the arena as it is, anything else copied once
const void* gpuVerts(const void* src, int count, bool model) {
    if (inArena(src)) return src;
    int bytes = count * (int)(model ? sizeof(ModelVertex) : sizeof(WorldVertex));
    uint64_t k = vkey(src, count, model);
    auto it = vbos.find(k);
    if (it != vbos.end()) return it->second.mem;
    if (void* mem = gpuAlloc((unsigned)bytes, 16)) {
        memcpy(mem, src, bytes);
        vbos[k] = Vbo{(const u8*)src, bytes, mem};
        return mem;
    }
    void* tmp = gfx::frameAlloc(bytes);                 // (out of GPU memory: this frame only)
    if (tmp) memcpy(tmp, src, bytes);
    return tmp;
}

// ---- state
void setDepthState(SceGxmDepthFunc f, bool write) {
    SceGxmDepthWriteMode w = write ? SCE_GXM_DEPTH_WRITE_ENABLED : SCE_GXM_DEPTH_WRITE_DISABLED;
    sceGxmSetFrontDepthFunc(ctx, f); sceGxmSetBackDepthFunc(ctx, f);
    sceGxmSetFrontDepthWriteEnable(ctx, w); sceGxmSetBackDepthWriteEnable(ctx, w);
}
// GL rules: no depth test (or 2D) -> no depth writes either
void applyDepth() {
    bool test = !in2D && depthWanted;
    setDepthState(test ? SCE_GXM_DEPTH_FUNC_LESS_EQUAL : SCE_GXM_DEPTH_FUNC_ALWAYS, test && depthWriteWanted);
}
void setStencil(SceGxmStencilFunc f, SceGxmStencilOp pass, unsigned ref) {
    sceGxmSetFrontStencilFunc(ctx, f, SCE_GXM_STENCIL_OP_KEEP, SCE_GXM_STENCIL_OP_KEEP, pass, 0xFF, 0xFF);
    sceGxmSetBackStencilFunc(ctx, f, SCE_GXM_STENCIL_OP_KEEP, SCE_GXM_STENCIL_OP_KEEP, pass, 0xFF, 0xFF);
    sceGxmSetFrontStencilRef(ctx, ref); sceGxmSetBackStencilRef(ctx, ref);
}
void setViewportPx(float x, float y, float w, float h) {
    sceGxmSetViewport(ctx, x + w * 0.5f, w * 0.5f, y + h * 0.5f, -h * 0.5f, 0.5f, 0.5f);
}

M4 ortho2D() {
    M4 r = M4::identity();
    r.m[0] = 2.0f / SCREEN_W; r.m[5] = -2.0f / SCREEN_H; r.m[10] = -1;
    r.m[12] = -1; r.m[13] = 1;
    return r;
}

// one draw: vertices the GPU can read, the matrices, then the state above
void draw(const void* verts, bool model, SceGxmPrimitiveType prim, const uint32_t* idx, unsigned n,
          const M4& mvp, const M4& mv, SceGxmFragmentProgram* fp = nullptr, bool fog = true) {
    if (!inScene || !verts || !n) return;
    sceGxmSetVertexProgram(ctx, model ? vpModel : vpWorld);
    if (!fp) {
        bool depthWrites = !in2D && depthWanted && depthWriteWanted;
        if (blendOn) fp = depthWrites ? fpBlend : fpBlendND;
        else fp = (curOpaque || curGxmTex == &whiteTex) ? fpOpaqueND : fpOpaque;
    }
    sceGxmSetFragmentProgram(ctx, fp);
    sceGxmSetFragmentTexture(ctx, 0, curGxmTex ? curGxmTex : &whiteTex);
    void* vb = nullptr;
    sceGxmReserveVertexDefaultUniformBuffer(ctx, &vb);
    sceGxmSetUniformDataF(vb, uMvp, 0, 16, mvp.m);
    sceGxmSetUniformDataF(vb, uMv, 0, 16, mv.m);
    const float k = model ? 1.0f / 32768.0f : 1.0f;
    bool fogNow = fog && fogWanted && !in2D;
    float prm[4] = {k, k, fogNow ? fogEnd : 1e20f, fogNow ? fogInv : 1.0f};
    sceGxmSetUniformDataF(vb, uPrm, 0, 4, prm);
    void* fb = nullptr;
    sceGxmReserveFragmentDefaultUniformBuffer(ctx, &fb);
    bool nd = fp == fpOpaqueND || fp == fpBlendND || fp == fpNoColourND;
    sceGxmSetUniformDataF(fb, nd ? uFogND : uFog, 0, 4, fogCol);
    sceGxmSetVertexStream(ctx, 0, verts);
    sceGxmDraw(ctx, prim, SCE_GXM_INDEX_FORMAT_U32, idx ? idx : linearIdx, n);
}

// split very long plain draws (more vertices than the linear index list): triangles only
void drawPlain(const void* verts, bool model, SceGxmPrimitiveType prim, unsigned count, const M4& mvp, const M4& mv) {
    unsigned stride = model ? sizeof(ModelVertex) : sizeof(WorldVertex);
    if (count <= LINEAR_IDX || prim != SCE_GXM_PRIMITIVE_TRIANGLES) {
        draw(verts, model, prim, nullptr, count < LINEAR_IDX ? count : LINEAR_IDX, mvp, mv);
        return;
    }
    const unsigned chunk = LINEAR_IDX - LINEAR_IDX % 3;
    for (unsigned first = 0; first < count; first += chunk) {
        unsigned n = count - first < chunk ? count - first : chunk;
        draw((const u8*)verts + (size_t)first * stride, model, prim, nullptr, n, mvp, mv);
    }
}

// a rectangle over the whole viewport at the far plane: colour (or none), depth 1, stencil `ref`
void clearQuad(u32 colour, bool writeColour, bool markStencil) {
    WorldVertex* q = (WorldVertex*)gfx::frameAlloc(6 * (int)sizeof(WorldVertex));
    if (!q) return;
    WorldVertex a{0, 0, colour | 0xFF000000u, -1, -1, 1}, b{0, 0, colour | 0xFF000000u, 1, -1, 1};
    WorldVertex c{0, 0, colour | 0xFF000000u, 1, 1, 1}, d{0, 0, colour | 0xFF000000u, -1, 1, 1};
    q[0] = a; q[1] = b; q[2] = c; q[3] = a; q[4] = c; q[5] = d;
    const SceGxmTexture* keep = curGxmTex;
    curGxmTex = &whiteTex;
    setDepthState(SCE_GXM_DEPTH_FUNC_ALWAYS, true);
    if (markStencil) setStencil(SCE_GXM_STENCIL_FUNC_ALWAYS, SCE_GXM_STENCIL_OP_REPLACE, (unsigned)insetRef);
    M4 id = M4::identity();
    draw(q, false, SCE_GXM_PRIMITIVE_TRIANGLES, nullptr, 6, id, id, writeColour ? fpOpaqueND : fpNoColourND, false);
    if (markStencil) setStencil(SCE_GXM_STENCIL_FUNC_EQUAL, SCE_GXM_STENCIL_OP_KEEP, (unsigned)insetRef);
    curGxmTex = keep;
    applyDepth();
}

// ---- start up
struct DisplayData { void* addr; };
volatile int vblanks = 1;                               // screen refreshes per frame (0: no wait)
void displayCallback(const void* data) {
    const DisplayData* d = (const DisplayData*)data;
    SceDisplayFrameBuf fb;
    memset(&fb, 0, sizeof fb);
    fb.size = sizeof fb; fb.base = d->addr; fb.pitch = VITA_W;
    fb.pixelformat = SCE_DISPLAY_PIXELFORMAT_A8B8G8R8; fb.width = VITA_W; fb.height = VITA_H;
    sceDisplaySetFrameBuf(&fb, SCE_DISPLAY_SETBUF_NEXTFRAME);
    if (vblanks > 0) sceDisplayWaitVblankStartMulti((unsigned)vblanks);
}
void* patcherAlloc(void*, unsigned int size) { return malloc(size); }
void patcherFree(void*, void* p) { free(p); }

#define GXM_CHECK(x) do { int r_ = (x); if (r_ < 0) { sys::log("gpu: %s failed %08x\n", #x, r_); return false; } } while (0)
#define GXM_NEED(p) do { if (!(p)) { sys::log("gpu: no memory for %s\n", #p); return false; } } while (0)

bool startGpu() {
    SceGxmInitializeParams ip;
    memset(&ip, 0, sizeof ip);
    ip.flags = 0;
    ip.displayQueueMaxPendingCount = BUFFERS - 1;
    ip.displayQueueCallback = displayCallback;
    ip.displayQueueCallbackDataSize = sizeof(DisplayData);
    ip.parameterBufferSize = SCE_GXM_DEFAULT_PARAMETER_BUFFER_SIZE;
    GXM_CHECK(sceGxmInitialize(&ip));

    // the context and its ring buffers
    const unsigned VERTEX_RING = 4 * 1024 * 1024, FRAGMENT_RING = 1024 * 1024;
    void* vdm = gpuBlock(SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_NC_RW, SCE_GXM_DEFAULT_VDM_RING_BUFFER_SIZE);
    void* vring = gpuBlock(SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_NC_RW, VERTEX_RING);
    void* fring = gpuBlock(SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_NC_RW, FRAGMENT_RING);
    unsigned fusseOff = 0;
    void* fusse = usseBlock(SCE_GXM_DEFAULT_FRAGMENT_USSE_RING_BUFFER_SIZE, false, &fusseOff);
    GXM_NEED(vdm); GXM_NEED(vring); GXM_NEED(fring); GXM_NEED(fusse);
    SceGxmContextParams cp;
    memset(&cp, 0, sizeof cp);
    cp.hostMem = malloc(SCE_GXM_MINIMUM_CONTEXT_HOST_MEM_SIZE);
    cp.hostMemSize = SCE_GXM_MINIMUM_CONTEXT_HOST_MEM_SIZE;
    cp.vdmRingBufferMem = vdm; cp.vdmRingBufferMemSize = SCE_GXM_DEFAULT_VDM_RING_BUFFER_SIZE;
    cp.vertexRingBufferMem = vring; cp.vertexRingBufferMemSize = VERTEX_RING;
    cp.fragmentRingBufferMem = fring; cp.fragmentRingBufferMemSize = FRAGMENT_RING;
    cp.fragmentUsseRingBufferMem = fusse; cp.fragmentUsseRingBufferMemSize = SCE_GXM_DEFAULT_FRAGMENT_USSE_RING_BUFFER_SIZE;
    cp.fragmentUsseRingBufferOffset = fusseOff;
    GXM_CHECK(sceGxmCreateContext(&cp, &ctx));

    // the screen: 960 x 544, 2x multisampling, three colour buffers, depth + stencil
    SceGxmRenderTargetParams rp;
    memset(&rp, 0, sizeof rp);
    rp.flags = 0; rp.width = VITA_W; rp.height = VITA_H; rp.scenesPerFrame = 1;
    rp.multisampleMode = SCE_GXM_MULTISAMPLE_2X; rp.multisampleLocations = 0; rp.driverMemBlock = -1;
    GXM_CHECK(sceGxmCreateRenderTarget(&rp, &target));
    for (int i = 0; i < BUFFERS; i++) {
        surfaceMem[i] = gpuBlock(SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, 4 * VITA_W * VITA_H);
        GXM_NEED(surfaceMem[i]);
        memset(surfaceMem[i], 0, 4 * VITA_W * VITA_H);
        GXM_CHECK(sceGxmColorSurfaceInit(&surface[i], SCE_GXM_COLOR_FORMAT_A8B8G8R8, SCE_GXM_COLOR_SURFACE_LINEAR,
                                         SCE_GXM_COLOR_SURFACE_SCALE_MSAA_DOWNSCALE, SCE_GXM_OUTPUT_REGISTER_SIZE_32BIT,
                                         VITA_W, VITA_H, VITA_W, surfaceMem[i]));
        GXM_CHECK(sceGxmSyncObjectCreate(&syncObj[i]));
    }
    const unsigned dsW = (VITA_W + SCE_GXM_TILE_SIZEX - 1) & ~(SCE_GXM_TILE_SIZEX - 1);
    const unsigned dsH = (VITA_H + SCE_GXM_TILE_SIZEY - 1) & ~(SCE_GXM_TILE_SIZEY - 1);
    const unsigned samples = dsW * dsH * 2;              // 2x MSAA: two samples per pixel, same stride
    void* depthMem = gpuBlock(SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, 4 * samples);
    void* stencilMem = gpuBlock(SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, samples);
    GXM_NEED(depthMem); GXM_NEED(stencilMem);
    GXM_CHECK(sceGxmDepthStencilSurfaceInit(&depthSurface, SCE_GXM_DEPTH_STENCIL_FORMAT_DF32M_S8, SCE_GXM_DEPTH_STENCIL_SURFACE_LINEAR,
                                            dsW, depthMem, stencilMem));
    sceGxmDepthStencilSurfaceSetBackgroundDepth(&depthSurface, 1.0f);
    sceGxmDepthStencilSurfaceSetBackgroundStencil(&depthSurface, 0);

    // the shader patcher and the shaders
    const unsigned PATCH_BUF = 64 * 1024, PATCH_USSE = 64 * 1024;
    void* pbuf = gpuBlock(SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_NC_RW, PATCH_BUF);
    unsigned vOff = 0, fOff = 0;
    void* vusse = usseBlock(PATCH_USSE, true, &vOff);
    void* fusse2 = usseBlock(PATCH_USSE, false, &fOff);
    GXM_NEED(pbuf); GXM_NEED(vusse); GXM_NEED(fusse2);
    SceGxmShaderPatcherParams pp;
    memset(&pp, 0, sizeof pp);
    pp.hostAllocCallback = patcherAlloc; pp.hostFreeCallback = patcherFree;
    pp.bufferMem = pbuf; pp.bufferMemSize = PATCH_BUF;
    pp.vertexUsseMem = vusse; pp.vertexUsseMemSize = PATCH_USSE; pp.vertexUsseOffset = vOff;
    pp.fragmentUsseMem = fusse2; pp.fragmentUsseMemSize = PATCH_USSE; pp.fragmentUsseOffset = fOff;
    GXM_CHECK(sceGxmShaderPatcherCreate(&pp, &patcher));

    vProg = compile(VERT_SRC, true);
    fProg = compile(FRAG_SRC, false);
    const SceGxmProgram* fProgND = compile(noDiscard(FRAG_SRC).c_str(), false);
    GXM_NEED(vProg); GXM_NEED(fProg); GXM_NEED(fProgND);
    SceGxmShaderPatcherId vId, fId, fIdND;
    GXM_CHECK(sceGxmShaderPatcherRegisterProgram(patcher, vProg, &vId));
    GXM_CHECK(sceGxmShaderPatcherRegisterProgram(patcher, fProg, &fId));
    GXM_CHECK(sceGxmShaderPatcherRegisterProgram(patcher, fProgND, &fIdND));
    const SceGxmProgramParameter* aPos = sceGxmProgramFindParameterByName(vProg, "aPos");
    const SceGxmProgramParameter* aCol = sceGxmProgramFindParameterByName(vProg, "aCol");
    const SceGxmProgramParameter* aUv = sceGxmProgramFindParameterByName(vProg, "aUv");
    uMvp = sceGxmProgramFindParameterByName(vProg, "uMvp");
    uMv = sceGxmProgramFindParameterByName(vProg, "uMv");
    uPrm = sceGxmProgramFindParameterByName(vProg, "uPrm");
    uFog = sceGxmProgramFindParameterByName(fProg, "uFog");
    uFogND = sceGxmProgramFindParameterByName(fProgND, "uFog");
    GXM_NEED(aPos); GXM_NEED(aCol); GXM_NEED(aUv); GXM_NEED(uMvp); GXM_NEED(uMv); GXM_NEED(uPrm); GXM_NEED(uFog);
    auto reg = [](const SceGxmProgramParameter* p) { return (uint16_t)sceGxmProgramParameterGetResourceIndex(p); };
    {   // WorldVertex { float u, v; u32 color; float x, y, z; } = 24 bytes
        SceGxmVertexAttribute at[3];
        at[0].streamIndex = 0; at[0].offset = 12; at[0].format = SCE_GXM_ATTRIBUTE_FORMAT_F32; at[0].componentCount = 3; at[0].regIndex = reg(aPos);
        at[1].streamIndex = 0; at[1].offset = 8; at[1].format = SCE_GXM_ATTRIBUTE_FORMAT_U8N; at[1].componentCount = 4; at[1].regIndex = reg(aCol);
        at[2].streamIndex = 0; at[2].offset = 0; at[2].format = SCE_GXM_ATTRIBUTE_FORMAT_F32; at[2].componentCount = 2; at[2].regIndex = reg(aUv);
        SceGxmVertexStream st; st.stride = sizeof(WorldVertex); st.indexSource = SCE_GXM_INDEX_SOURCE_INDEX_32BIT;
        GXM_CHECK(sceGxmShaderPatcherCreateVertexProgram(patcher, vId, at, 3, &st, 1, &vpWorld));
    }
    {   // ModelVertex { u16 u, v; u32 color; int16 x, y, z, pad; } = 16 bytes, read as integers (x 1/32768 in the shader)
        SceGxmVertexAttribute at[3];
        at[0].streamIndex = 0; at[0].offset = 8; at[0].format = SCE_GXM_ATTRIBUTE_FORMAT_S16; at[0].componentCount = 3; at[0].regIndex = reg(aPos);
        at[1].streamIndex = 0; at[1].offset = 4; at[1].format = SCE_GXM_ATTRIBUTE_FORMAT_U8N; at[1].componentCount = 4; at[1].regIndex = reg(aCol);
        at[2].streamIndex = 0; at[2].offset = 0; at[2].format = SCE_GXM_ATTRIBUTE_FORMAT_U16; at[2].componentCount = 2; at[2].regIndex = reg(aUv);
        SceGxmVertexStream st; st.stride = sizeof(ModelVertex); st.indexSource = SCE_GXM_INDEX_SOURCE_INDEX_32BIT;
        GXM_CHECK(sceGxmShaderPatcherCreateVertexProgram(patcher, vId, at, 3, &st, 1, &vpModel));
    }
    SceGxmBlendInfo alpha;
    memset(&alpha, 0, sizeof alpha);
    alpha.colorMask = SCE_GXM_COLOR_MASK_ALL;
    alpha.colorFunc = SCE_GXM_BLEND_FUNC_ADD; alpha.alphaFunc = SCE_GXM_BLEND_FUNC_ADD;
    alpha.colorSrc = SCE_GXM_BLEND_FACTOR_SRC_ALPHA; alpha.colorDst = SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    alpha.alphaSrc = SCE_GXM_BLEND_FACTOR_SRC_ALPHA; alpha.alphaDst = SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    SceGxmBlendInfo none;
    memset(&none, 0, sizeof none);
    none.colorMask = SCE_GXM_COLOR_MASK_NONE;
    none.colorFunc = SCE_GXM_BLEND_FUNC_NONE; none.alphaFunc = SCE_GXM_BLEND_FUNC_NONE;
    none.colorSrc = SCE_GXM_BLEND_FACTOR_ONE; none.colorDst = SCE_GXM_BLEND_FACTOR_ZERO;
    none.alphaSrc = SCE_GXM_BLEND_FACTOR_ONE; none.alphaDst = SCE_GXM_BLEND_FACTOR_ZERO;
    const SceGxmOutputRegisterFormat OUT = SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4;
    GXM_CHECK(sceGxmShaderPatcherCreateFragmentProgram(patcher, fId, OUT, SCE_GXM_MULTISAMPLE_2X, nullptr, vProg, &fpOpaque));
    GXM_CHECK(sceGxmShaderPatcherCreateFragmentProgram(patcher, fId, OUT, SCE_GXM_MULTISAMPLE_2X, &alpha, vProg, &fpBlend));
    GXM_CHECK(sceGxmShaderPatcherCreateFragmentProgram(patcher, fId, OUT, SCE_GXM_MULTISAMPLE_2X, &none, vProg, &fpNoColour));
    GXM_CHECK(sceGxmShaderPatcherCreateFragmentProgram(patcher, fIdND, OUT, SCE_GXM_MULTISAMPLE_2X, nullptr, vProg, &fpOpaqueND));
    GXM_CHECK(sceGxmShaderPatcherCreateFragmentProgram(patcher, fIdND, OUT, SCE_GXM_MULTISAMPLE_2X, &alpha, vProg, &fpBlendND));
    GXM_CHECK(sceGxmShaderPatcherCreateFragmentProgram(patcher, fIdND, OUT, SCE_GXM_MULTISAMPLE_2X, &none, vProg, &fpNoColourND));
    // the lit pair for the Blender cars: without it the game keeps its own car models
    vLitProg = compile(LIT_VERT_SRC, true);
    fLitProg = compile(LIT_FRAG_SRC, false);
    const SceGxmProgram* fLitND = compile(noDiscard(LIT_FRAG_SRC).c_str(), false);
    if (vLitProg && fLitProg && fLitND) {
        SceGxmShaderPatcherId lv, lf, lfND;
        const SceGxmProgramParameter *bPos = nullptr, *bNrm = nullptr, *bCol = nullptr, *bUv = nullptr;
        bool ok = sceGxmShaderPatcherRegisterProgram(patcher, vLitProg, &lv) >= 0 && sceGxmShaderPatcherRegisterProgram(patcher, fLitProg, &lf) >= 0
               && sceGxmShaderPatcherRegisterProgram(patcher, fLitND, &lfND) >= 0;
        if (ok) {
            bPos = sceGxmProgramFindParameterByName(vLitProg, "aPos"); bNrm = sceGxmProgramFindParameterByName(vLitProg, "aNrm");
            bCol = sceGxmProgramFindParameterByName(vLitProg, "aCol"); bUv = sceGxmProgramFindParameterByName(vLitProg, "aUv");
            lMvp = sceGxmProgramFindParameterByName(vLitProg, "uMvp"); lMv = sceGxmProgramFindParameterByName(vLitProg, "uMv");
            lModel = sceGxmProgramFindParameterByName(vLitProg, "uModel"); lPrm = sceGxmProgramFindParameterByName(vLitProg, "uPrm");
            static const char* FN[7] = {"uFog", "uSun", "uSunCol", "uSky", "uGround", "uZenith", "uMat"};
            ok = bPos && bNrm && bCol && bUv && lMvp && lMv && lModel && lPrm;
            for (int v = 0; v < 2; v++)
                for (int k = 0; k < 7; k++) {
                    lFrag[v][k] = sceGxmProgramFindParameterByName(v ? fLitND : fLitProg, FN[k]);
                    ok = ok && lFrag[v][k];
                }
        }
        if (ok) {   // MeshVertex { float x, y, z; int8 nx, ny, nz, pad; u32 color; float u, v; } = 28 bytes
            SceGxmVertexAttribute at[4];
            at[0].streamIndex = 0; at[0].offset = 0; at[0].format = SCE_GXM_ATTRIBUTE_FORMAT_F32; at[0].componentCount = 3; at[0].regIndex = reg(bPos);
            at[1].streamIndex = 0; at[1].offset = 12; at[1].format = SCE_GXM_ATTRIBUTE_FORMAT_S8N; at[1].componentCount = 3; at[1].regIndex = reg(bNrm);
            at[2].streamIndex = 0; at[2].offset = 16; at[2].format = SCE_GXM_ATTRIBUTE_FORMAT_U8N; at[2].componentCount = 4; at[2].regIndex = reg(bCol);
            at[3].streamIndex = 0; at[3].offset = 20; at[3].format = SCE_GXM_ATTRIBUTE_FORMAT_F32; at[3].componentCount = 2; at[3].regIndex = reg(bUv);
            SceGxmVertexStream st; st.stride = sizeof(MeshVertex); st.indexSource = SCE_GXM_INDEX_SOURCE_INDEX_32BIT;
            ok = sceGxmShaderPatcherCreateVertexProgram(patcher, lv, at, 4, &st, 1, &vpMesh) >= 0
              && sceGxmShaderPatcherCreateFragmentProgram(patcher, lf, OUT, SCE_GXM_MULTISAMPLE_2X, nullptr, vLitProg, &fpLit) >= 0
              && sceGxmShaderPatcherCreateFragmentProgram(patcher, lf, OUT, SCE_GXM_MULTISAMPLE_2X, &alpha, vLitProg, &fpLitBlend) >= 0
              && sceGxmShaderPatcherCreateFragmentProgram(patcher, lfND, OUT, SCE_GXM_MULTISAMPLE_2X, nullptr, vLitProg, &fpLitND) >= 0;
        }
        meshReady = ok;
    }
    if (!meshReady) sys::log("gpu: no lit shader - the game's own car models are used\n");

    // memory pools for textures and vertex buffers: video memory, keeping 8 MB of it free for plugins (a
    // screen recorder), then uncached main memory, keeping 24 MB free for everything else
    SceKernelFreeMemorySizeInfo fm;
    memset(&fm, 0, sizeof fm);
    fm.size = sizeof fm;
    sceKernelGetFreeMemorySize(&fm);
    const unsigned KEEP_CDRAM = 8u << 20, KEEP_RAM = 24u << 20;
    unsigned got = 0;
    if ((unsigned)fm.size_cdram > KEEP_CDRAM + CDRAM_ALIGN) {
        void* v = gpuBlock(SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, ((unsigned)fm.size_cdram - KEEP_CDRAM) & ~(CDRAM_ALIGN - 1), &got);
        if (v) vram.init(v, got);
    }
    // the two frame arenas first (they must exist), then the rest
    for (u8*& a : arenas) { a = (u8*)gpuBlock(SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_NC_RW, ARENA_SIZE); GXM_NEED(a); }
    arena = arenas[0];
    memset(&fm, 0, sizeof fm);
    fm.size = sizeof fm;
    sceKernelGetFreeMemorySize(&fm);
    if ((unsigned)fm.size_user > KEEP_RAM + RAM_ALIGN) {
        unsigned want = (unsigned)fm.size_user - KEEP_RAM;
        if (want > (128u << 20)) want = 128u << 20;
        void* r = gpuBlock(SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_NC_RW, want & ~(RAM_ALIGN - 1), &got);
        if (r) ram.init(r, got);
    }
    sys::log("gpu: video memory pool %u KB, main memory pool %u KB\n", vram.size / 1024, ram.size / 1024);

    linearIdx = (uint32_t*)gpuAlloc(LINEAR_IDX * 4, 16);
    GXM_NEED(linearIdx);
    for (unsigned i = 0; i < LINEAR_IDX; i++) linearIdx[i] = i;
    u32* white = (u32*)gpuAlloc(8 * 4, 16);             // 1 x 1 (rows are 8 texels)
    GXM_NEED(white);
    for (int i = 0; i < 8; i++) white[i] = 0xFFFFFFFFu;
    sceGxmTextureInitLinear(&whiteTex, white, SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR, 1, 1, 1);
    curGxmTex = &whiteTex;

    notifyAddr = sceGxmGetNotificationRegion();
    *notifyAddr = 0;
    frameNotify.address = notifyAddr;
    frameNotify.value = 0;
    gpuReady = true;
    return true;
}
}

namespace gfx {
static bool syncOn = true; static int syncEvery = 1;
static float waitSec = 0, gpuWaitSec = 0;
float lastWaitSec() { return waitSec; }
float lastGpuWaitSec() { return gpuWaitSec; }
void setSync(bool vsync, int every) {
    syncOn = vsync || every > 1; syncEvery = every < 1 ? 1 : every;
    vblanks = syncOn ? syncEvery : 0;
}
void beginFrame(u32 clear) {
    if (!gpuReady) return;
    // this frame's arena was last used two frames ago: wait until the GPU has finished with it
    arena = arenas[frameNo & 1];
    SceUInt64 t0 = sceKernelGetProcessTimeWide();
    while (frameNo > 2 && (int)(framesDone() - (frameNo - 2)) < 0) sceKernelDelayThread(100);
    gpuWaitSec = (sceKernelGetProcessTimeWide() - t0) / 1000000.0f;
    arenaUsed = 0;
    collect();
    sceGxmBeginScene(ctx, 0, target, nullptr, nullptr, syncObj[backBuf], &surface[backBuf], &depthSurface);
    inScene = true;
    sceGxmSetCullMode(ctx, SCE_GXM_CULL_NONE);           // the AI models are not consistently wound
    sceGxmSetRegionClip(ctx, SCE_GXM_REGION_CLIP_OUTSIDE, 0, 0, VITA_W - 1, VITA_H - 1);
    setViewportPx(0, 0, VITA_W, VITA_H);
    insetRef = 0;
    setStencil(SCE_GXM_STENCIL_FUNC_ALWAYS, SCE_GXM_STENCIL_OP_KEEP, 0);
    in2D = false;
    clearQuad(clear, true, false);
    curTex = nullptr; curGxmTex = &whiteTex; curOpaque = true;
}
void endFrame() {
    if (!inScene) return;
    inScene = false;
    frameNotify.value = frameNo;
    sceGxmEndScene(ctx, nullptr, &frameNotify);
    sceGxmPadHeartbeat(&surface[backBuf], syncObj[backBuf]);
    SceUInt64 t0 = sceKernelGetProcessTimeWide();
    DisplayData d{surfaceMem[backBuf]};
    sceGxmDisplayQueueAddEntry(syncObj[frontBuf], syncObj[backBuf], &d);   // waits while the queue is full
    waitSec = (sceKernelGetProcessTimeWide() - t0) / 1000000.0f;
    frontBuf = backBuf;
    backBuf = (backBuf + 1) % BUFFERS;
    frameNo++;
}
void clearDepth() { if (inScene) clearQuad(0, false, false); }
bool beginInset(float x, float y, float w, float h, u32 c) {
    if (!inScene || insetRef >= 255) return false;
    setViewportPx(x * 2, y * 2, w * 2, h * 2);
    {   // (tiles outside the inset never see its geometry; the stencil cuts it exactly)
        int x0 = (int)(x * 2), y0 = (int)(y * 2), x1 = (int)((x + w) * 2) - 1, y1 = (int)((y + h) * 2) - 1;
        sceGxmSetRegionClip(ctx, SCE_GXM_REGION_CLIP_OUTSIDE, x0 < 0 ? 0 : x0, y0 < 0 ? 0 : y0, x1 > VITA_W - 1 ? VITA_W - 1 : x1, y1 > VITA_H - 1 ? VITA_H - 1 : y1);
    }
    insetRef++;
    clearQuad(c, true, true);                            // colour, depth and this inset's stencil mark
    return true;
}
void endInset() {
    if (!inScene) return;
    setStencil(SCE_GXM_STENCIL_FUNC_ALWAYS, SCE_GXM_STENCIL_OP_KEEP, 0);
    setViewportPx(0, 0, VITA_W, VITA_H);
    sceGxmSetRegionClip(ctx, SCE_GXM_REGION_CLIP_OUTSIDE, 0, 0, VITA_W - 1, VITA_H - 1);
}
void setProjection(const M4& p) { projM = p; }
void setView(const M4& v) { viewM = v; }
void setFog(bool on, float n, float f, u32 c) {
    fogWanted = on;
    if (on) {
        fogEnd = f;
        fogInv = f > n ? 1.0f / (f - n) : 1e6f;
        fogCol[0] = (c & 255) / 255.0f; fogCol[1] = ((c >> 8) & 255) / 255.0f; fogCol[2] = ((c >> 16) & 255) / 255.0f; fogCol[3] = 1;
    }
}
void bindTexture(const Texture* t) {
    const Texture* want = (t && t->pixels && t->palette) ? t : nullptr;
    if (want == curTex && curGxmTex) return;
    curTex = want;
    curGxmTex = &whiteTex; curOpaque = true;
    if (want) { int i = upload(want); if (i >= 0) { curGxmTex = &texCache[i]->tex; curOpaque = texCache[i]->opaque; } else curTex = nullptr; }
}
void setBlend(bool on) { blendOn = on; }
void setDepthTest(bool on) { depthWanted = on; if (inScene) applyDepth(); }
void setDepthWrite(bool on) { depthWriteWanted = on; if (inScene) applyDepth(); }

static void enter3D() { if (in2D) { in2D = false; applyDepth(); } }
static void enter2D() { if (!in2D) { in2D = true; applyDepth(); } }

void drawModel(const ModelVertex* v, int count, const M4& model) {
    if (count <= 0 || !inScene) return;
    enter3D();
    M4 mv = viewM * model;
    drawPlain(gpuVerts(v, count, true), true, SCE_GXM_PRIMITIVE_TRIANGLES, (unsigned)count, projM * mv, mv);
}
void drawWorld(const WorldVertex* v, int count, Prim prim, const M4& model) {
    if (count <= 0 || !inScene) return;
    enter3D();
    M4 mv = viewM * model;
    M4 mvp = projM * mv;
    const void* g = gpuVerts(v, count, false);
    if (prim == PRIM_LINE_STRIP) {                      // (no line strips on the GPU: index pairs)
        if (count < 2) return;
        uint32_t* idx = (uint32_t*)frameAlloc((count - 1) * 2 * 4);
        if (!idx) return;
        for (int i = 0; i < count - 1; i++) { idx[2 * i] = (uint32_t)i; idx[2 * i + 1] = (uint32_t)(i + 1); }
        draw(g, false, SCE_GXM_PRIMITIVE_LINES, idx, (unsigned)(count - 1) * 2, mvp, mv);
        return;
    }
    static const SceGxmPrimitiveType P[3] = {SCE_GXM_PRIMITIVE_TRIANGLES, SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, SCE_GXM_PRIMITIVE_LINES};
    drawPlain(g, false, P[prim], (unsigned)count, mvp, mv);
}
void drawSprites2D(const Sprite2D* s, int n) {
    if (n <= 0 || !inScene) return;
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
    M4 o = ortho2D();
    draw(w, false, SCE_GXM_PRIMITIVE_TRIANGLES, nullptr, (unsigned)n * 6, o, M4::identity());
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
    bytes = (bytes + 15) & ~15;
    if (!arena || arenaUsed + bytes > ARENA_SIZE) return nullptr;
    void* p = arena + arenaUsed; arenaUsed += bytes; return p;
}
// give back the end of the last block handed out
void frameTrim(void* p, int keepBytes) {
    u8* b = (u8*)p;
    if (!b || b < arena || b > arena + arenaUsed) return;
    int end = (int)(b - arena) + ((keepBytes + 15) & ~15);
    if (end < arenaUsed) arenaUsed = end;
}
// The game calls this after writing pixels, palettes or vertices: the GPU copies made from them are stale.
void flushCache(const void* p, int bytes) { if (p && bytes > 0 && !inArena(p)) { forget(p, bytes); forgetVbos(p, bytes); } }
bool gpuClips() { return true; }
bool highDetail() { return true; }
void setLighting(const Lighting& l) {
    float n = sqrtf(l.sun.x * l.sun.x + l.sun.y * l.sun.y + l.sun.z * l.sun.z);
    if (n < 1e-6f) n = 1;
    litSun[0] = l.sun.x / n; litSun[1] = l.sun.y / n; litSun[2] = l.sun.z / n;
    for (int i = 0; i < 3; i++) { litSunCol[i] = l.sunCol[i]; litSky[i] = l.sky[i]; litGround[i] = l.ground[i]; litZenith[i] = l.zenith[i]; }
}
void setLit(bool) {}
void setEye(const V3&) {}
void setSurface(int) {}
bool pcGlass() { return false; }
void setGlass(bool) {}
bool beginMirrorView(int, int, int, u32) { return false; }
void endMirrorView() {}
void drawMirrorGlass(int, const M4&, const float*) {}
bool beginShadow(const V3&, const V3&, float) { return false; }
void endShadow() {}
void setMaterial(float spec, float gloss) { litMat[0] = spec; litMat[1] = gloss; }
void attachNormals(const ModelVertex*, const int8_t*, int) {}
// ---- the Blender cars (CarMesh): meshes and full-colour textures in GPU memory
struct GMesh { void* verts = nullptr; uint32_t* idx = nullptr; int vc = 0, ic = 0; };
struct GTexRGBA { void* mem = nullptr; SceGxmTexture tex; int kb = 0; bool opaque = true; };
static std::vector<GMesh> gMeshes;
static std::vector<GTexRGBA*> gTexRGBA;              // (pointers: stable addresses)
bool meshesSupported() { return meshReady && (vram.size || ram.size); }
int createMesh(const MeshVertex* v, int vc, const uint32_t* idx, int ic) {
    if (!meshReady || !v || !idx || vc <= 0 || ic <= 0) return 0;
    GMesh m; m.vc = vc; m.ic = ic;
    m.verts = gpuAlloc((unsigned)vc * sizeof(MeshVertex), 16);
    m.idx = (uint32_t*)gpuAlloc((unsigned)ic * 4, 16);
    if (!m.verts || !m.idx) { gpuFree(m.verts); gpuFree(m.idx); sys::log("gpu: out of memory for a mesh\n"); return 0; }
    memcpy(m.verts, v, (size_t)vc * sizeof(MeshVertex));
    memcpy(m.idx, idx, (size_t)ic * 4);
    for (size_t i = 0; i < gMeshes.size(); i++) if (!gMeshes[i].verts) { gMeshes[i] = m; return (int)i + 1; }
    gMeshes.push_back(m);
    return (int)gMeshes.size();
}
void freeMesh(int mesh) {
    if (mesh <= 0 || mesh > (int)gMeshes.size()) return;
    GMesh& m = gMeshes[mesh - 1];
    gpuFree(m.verts); gpuFree(m.idx);
    m = GMesh();
}
int createTextureRGBA(const u8* rgba, int w, int h) {
    if (!meshReady || !rgba || w <= 0 || h <= 0) return 0;
    std::vector<u32> px((size_t)w * h);
    memcpy(px.data(), rgba, px.size() * 4);             // (bytes R, G, B, A = the GPU's A B G R words)
    bool opaque = true;
    for (size_t i = 0; i < px.size() && opaque; i++) opaque = (px[i] >> 24) == 0xFF;
    GTexRGBA* t = new GTexRGBA();
    if (!buildTexture(px, w, h, opaque, true, t->mem, t->tex)) { delete t; return 0; }
    t->kb = (int)((double)w * h * (opaque ? 2 : 4) * 4.0 / 3.0 / 1024);
    t->opaque = opaque;
    for (size_t i = 0; i < gTexRGBA.size(); i++) if (!gTexRGBA[i]) { gTexRGBA[i] = t; return (int)i + 1; }
    gTexRGBA.push_back(t);
    return (int)gTexRGBA.size();
}
void freeTextureRGBA(int tex) {
    if (tex <= 0 || tex > (int)gTexRGBA.size() || !gTexRGBA[tex - 1]) return;
    gpuFree(gTexRGBA[tex - 1]->mem);
    delete gTexRGBA[tex - 1];
    gTexRGBA[tex - 1] = nullptr;
}
int textureKB(int tex) { return tex > 0 && tex <= (int)gTexRGBA.size() && gTexRGBA[tex - 1] ? gTexRGBA[tex - 1]->kb : 0; }
void drawMesh(int mesh, int tex, int first, int count, const M4& model) {
    if (!inScene || !meshReady || mesh <= 0 || mesh > (int)gMeshes.size() || count <= 0) return;
    const GMesh& m = gMeshes[mesh - 1];
    if (!m.verts || first < 0 || first + count > m.ic) return;
    enter3D();
    const GTexRGBA* gt = tex > 0 && tex <= (int)gTexRGBA.size() ? gTexRGBA[tex - 1] : nullptr;
    const SceGxmTexture* t = gt ? &gt->tex : &whiteTex;
    const int v = (!blendOn && (!gt || gt->opaque)) ? 1 : 0;      // (opaque: the program without the discard)
    M4 mv = viewM * model, mvp = projM * mv;
    sceGxmSetVertexProgram(ctx, vpMesh);
    sceGxmSetFragmentProgram(ctx, blendOn ? fpLitBlend : (v ? fpLitND : fpLit));
    sceGxmSetFragmentTexture(ctx, 0, t);
    void* vb = nullptr;
    sceGxmReserveVertexDefaultUniformBuffer(ctx, &vb);
    sceGxmSetUniformDataF(vb, lMvp, 0, 16, mvp.m);
    sceGxmSetUniformDataF(vb, lMv, 0, 16, mv.m);
    sceGxmSetUniformDataF(vb, lModel, 0, 16, model.m);
    float prm[4] = {1, 1, fogWanted ? fogEnd : 1e20f, fogWanted ? fogInv : 1.0f};
    sceGxmSetUniformDataF(vb, lPrm, 0, 4, prm);
    void* fb = nullptr;
    sceGxmReserveFragmentDefaultUniformBuffer(ctx, &fb);
    const float* vals[7] = {fogCol, litSun, litSunCol, litSky, litGround, litZenith, litMat};
    for (int k = 0; k < 7; k++) sceGxmSetUniformDataF(fb, lFrag[v][k], 0, 4, vals[k]);
    sceGxmSetVertexStream(ctx, 0, m.verts);
    sceGxmDraw(ctx, SCE_GXM_PRIMITIVE_TRIANGLES, SCE_GXM_INDEX_FORMAT_U32, m.idx + first, (unsigned)count);
}
// every texture already lives in GPU memory once bound
bool moveToFastMemory(Texture&) { return false; }
int fastMark() { return 0; }
int vramUsedKB() { return (int)(vram.usedBytes / 1024); }
void fastRelease(int) {}
}

// ---------------------------------------------------------------- input
namespace input {
static float tiltRoll() {
    SceMotionState m;
    if (sceMotionGetState(&m) < 0) return 0;
    // gravity in the console's frame: x across the screen, y up the screen. Held like a steering
    // wheel, turning it right moves gravity towards +x.
    return atan2f(m.acceleration.x, -m.acceleration.y);
}
bool hasTouch() { return true; }
void poll(Pad& p) {
    SceCtrlData d;
    memset(&d, 0, sizeof d);
    sceCtrlPeekBufferPositive(0, &d, 1);
    p.stickX = (d.lx - 128) / 127.0f; p.stickY = (d.ly - 128) / 127.0f;
    if (fabsf(p.stickX) < 0.12f) p.stickX = 0;
    if (fabsf(p.stickY) < 0.12f) p.stickY = 0;
    p.rstickX = (d.rx - 128) / 127.0f; p.rstickY = (d.ry - 128) / 127.0f;
    if (fabsf(p.rstickX) < 0.10f) p.rstickX = 0;
    if (fabsf(p.rstickY) < 0.10f) p.rstickY = 0;
    p.up = d.buttons & SCE_CTRL_UP; p.down = d.buttons & SCE_CTRL_DOWN;
    p.left = d.buttons & SCE_CTRL_LEFT; p.right = d.buttons & SCE_CTRL_RIGHT;
    p.cross = d.buttons & SCE_CTRL_CROSS; p.circle = d.buttons & SCE_CTRL_CIRCLE;
    p.square = d.buttons & SCE_CTRL_SQUARE; p.triangle = d.buttons & SCE_CTRL_TRIANGLE;
    p.l = d.buttons & SCE_CTRL_LTRIGGER; p.r = d.buttons & SCE_CTRL_RTRIGGER;
    p.start = d.buttons & SCE_CTRL_START; p.select = d.buttons & SCE_CTRL_SELECT;
    // front touch screen: 1920 x 1088 units -> the game's 480 x 272 layout
    SceTouchData t;
    p.touches = 0;
    if (sceTouchPeek(SCE_TOUCH_PORT_FRONT, &t, 1) > 0)
        for (int i = 0; i < (int)t.reportNum && p.touches < 2; i++) {
            p.tx[p.touches] = t.report[i].x / 4.0f; p.ty[p.touches] = t.report[i].y / 4.0f; p.touches++;
        }
    p.hasTilt = true; p.tilt = tiltRoll();
}
}

// ---------------------------------------------------------------- audio
namespace audio {
static MixFn mixFn = nullptr;
static int port = -1;
static volatile bool audioRun = false;
static SceUID audioThread = -1;
static const int FRAMES = 1024;
static int audioMain(SceSize, void*) {
    static int16_t buf[2][FRAMES * 2] __attribute__((aligned(64)));
    int k = 0;
    while (audioRun) {
        MixFn f = mixFn;
        if (f) f(buf[k], FRAMES); else memset(buf[k], 0, sizeof buf[k]);
        sceAudioOutOutput(port, buf[k]);
        k ^= 1;
    }
    return 0;
}
void start(MixFn fn) {
    mixFn = fn;
    if (audioRun) return;
    port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, FRAMES, 44100, SCE_AUDIO_OUT_MODE_STEREO);
    if (port < 0) { sys::log("audio port failed %08x\n", port); return; }
    int vol[2] = {SCE_AUDIO_VOLUME_0DB, SCE_AUDIO_VOLUME_0DB};
    sceAudioOutSetVolume(port, (SceAudioOutChannelFlag)(SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH), vol);
    audioRun = true;
    audioThread = sceKernelCreateThread("audio", audioMain, 0x10000100 - 20, 0x10000, 0, 0, nullptr);
    if (audioThread >= 0) sceKernelStartThread(audioThread, 0, nullptr);
}
void stop() {
    if (!audioRun) return;
    audioRun = false;
    sceKernelWaitThreadEnd(audioThread, nullptr, nullptr);
    sceKernelDeleteThread(audioThread);
    sceAudioOutReleasePort(port);
}
}

// ---------------------------------------------------------------- system
// A plain message on screen without the GPU (used when the graphics can't start): the game's own font,
// drawn by the CPU straight into a frame buffer. Waits for a button, then closes the app.
static void messageScreen(std::initializer_list<const char*> lines) {
    const int W = 960, H = 544, PITCH = 960;
    int size = (W * H * 4 + 0x3FFFF) & ~0x3FFFF;
    SceUID blk = sceKernelAllocMemBlock("emr_msg_fb", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, size, nullptr);
    if (blk < 0) { sceKernelExitProcess(0); return; }
    u32* fb = nullptr; sceKernelGetMemBlockBase(blk, (void**)&fb);
    for (int i = 0; i < PITCH * H; i++) fb[i] = 0xFF402010;      // dark blue (A B G R)
    Font f;
    bool haveFont = f.load("DATA/font.bin");
    auto texel = [&](int x, int y) -> u32 {
        const Texture& t = f.tex;
        if (x < 0 || y < 0 || x >= t.w || y >= t.h) return 0;
        int bw = t.w / 16, off = ((y >> 3) * bw + (x >> 4)) * 128 + (y & 7) * 16 + (x & 15);
        return t.palette[t.pixels[off]];
    };
    const int S = 2;                                   // screen pixels per font layout pixel
    int y = 60, k = 0;
    for (const char* line : lines) {
        u32 col = k == 0 ? 0xFF10A0FF : 0xFFFFFFFF;    // title in orange
        if (haveFont) {
            int x = 60;
            for (const char* c = line; *c; c++) {
                int ch = (unsigned char)*c; if (ch < 32 || ch > 126) ch = '?';
                const Font::Glyph& q = f.g[ch - 32];
                for (int oy = 0; oy < q.h * S; oy++)
                    for (int ox = 0; ox < q.w * S; ox++) {
                        int sx = x + q.xoff * S + ox, sy = y + oy;
                        if (sx < 0 || sx >= W || sy < 0 || sy >= H) continue;
                        u32 t = texel(q.x * f.texScale + ox * f.texScale / S, q.y * f.texScale + oy * f.texScale / S);
                        u32 a = t >> 24;
                        if (!a) continue;
                        u32 d = fb[sy * PITCH + sx], o = 0xFF000000;
                        for (int sh = 0; sh < 24; sh += 8)
                            o |= ((((col >> sh) & 255) * a + ((d >> sh) & 255) * (255 - a)) / 255) << sh;
                        fb[sy * PITCH + sx] = o;
                    }
                x += q.adv * S;
            }
        }
        y += (haveFont ? f.lineH : 16) * S + 4; k++;
    }
    SceDisplayFrameBuf d{};
    d.size = sizeof d; d.base = fb; d.pitch = PITCH; d.pixelformat = SCE_DISPLAY_PIXELFORMAT_A8B8G8R8; d.width = W; d.height = H;
    sceDisplaySetFrameBuf(&d, SCE_DISPLAY_SETBUF_NEXTFRAME);
    SceCtrlData pad{};
    for (int i = 0; i < 30; i++) sceDisplayWaitVblankStart();       // ignore a button still held from the LiveArea
    for (;;) {
        sceCtrlPeekBufferPositive(0, &pad, 1);
        if (pad.buttons & (SCE_CTRL_CROSS | SCE_CTRL_CIRCLE | SCE_CTRL_SQUARE | SCE_CTRL_TRIANGLE | SCE_CTRL_START | SCE_CTRL_SELECT)) break;
        sceDisplayWaitVblankStart();
    }
    sceKernelExitProcess(0);
}

namespace sys {
static bool runningFlag = true;
static bool fileExists(const char* p) { SceIoStat st; return sceIoGetstat(p, &st) >= 0; }
bool init() {
    scePowerSetArmClockFrequency(444);
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222);
    scePowerSetGpuXbarClockFrequency(166);
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_BACK, SCE_TOUCH_SAMPLING_STATE_STOP);     // the rear pad isn't used
    sceMotionStartSampling();
    sceIoMkdir(!strncmp(SAVE_DIR, "uma0", 4) ? "uma0:data" : "ux0:data", 0777);
    sceIoMkdir(SAVE_DIR, 0777);
    log("game folder: %s\n", SAVE_DIR);

    // the shaders are compiled when the game starts, by the system's shader compiler (libshacccg.suprx)
    if (shark_init(nullptr) < 0 && shark_init("ur0:data/external/libshacccg.suprx") < 0) {
        log("libshacccg.suprx not found in ur0:data - install it (VitaDB does it for you)\n");
        messageScreen({"Endurance Motor Racing 2.0 can't start yet", "",
                       "The PS Vita's shader compiler (libshacccg.suprx) is missing.", "",
                       "To install it:", "1. Open VitaDB (or download it from vitadb.rinnegatamante.it)",
                       "2. VitaDB installs libshacccg.suprx by itself on first start",
                       "3. Start this game again", "", "Press any button to close"});
    }
    if (!startGpu()) {
        messageScreen({"Endurance Motor Racing 2.0: the graphics could not start", "",
                       "Details are in log.txt in the game's folder", "(uma0:data/EnduranceMotorRacing2 or ux0:data/...).", "",
                       "Press any button to close"});
    }
    shark_end();                     // (the compiler's memory back: both shaders are made)
    return true;
}
void shutdown() {}
bool running() { return runningFlag; }
void quit() { runningFlag = false; }
int memUsedKB() { struct mallinfo mi = mallinfo(); return (int)(mi.uordblks / 1024); }
double timeSec() { return sceKernelGetProcessTimeWide() / 1000000.0; }
void* loadFile(const char* path, int* size) {
    FILE* f = fopen(path, "rb");    // wrapped: DATA/ -> app0:HD/ then app0:DATA/
    if (!f) return nullptr;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    void* p = memalign(64, (n + 63) & ~63);
    if (!p) { fclose(f); return nullptr; }
    if (fread(p, 1, n, f) != (size_t)n) { fclose(f); free(p); return nullptr; }
    fclose(f);
    // 'FLZ1' u32 size, then a zlib stream: packed on the memory card (tools/pack_files.py), unpacked here
    if (n >= 8 && !memcmp(p, "FLZ1", 4)) {
        uint32_t raw; memcpy(&raw, (char*)p + 4, 4);
        void* q = memalign(64, (raw + 63) & ~63);
        uLongf got = raw;
        if (!q || uncompress((Bytef*)q, &got, (const Bytef*)p + 8, (uLong)(n - 8)) != Z_OK || got != raw) { free(p); free(q); return nullptr; }
        free(p); p = q; n = (long)raw;
    }
    if (size) *size = (int)n;
    gfx::flushCache(p, (int)n);   // memory reused from a freed file: drop old texture copies
    return p;
}
void freeFile(void* p) {
    if (!p) return;
    gfx::flushCache(p, (int)malloc_usable_size(p));
    free(p);
}
void log(const char* fmt, ...) {
    va_list a; va_start(a, fmt);
    FILE* f = fopen("log.txt", "a");
    if (f) { vfprintf(f, fmt, a); fclose(f); }
    va_end(a);
}
}

// ---------------------------------------------------------------- main
static Game game;
int main(int, char**) {
    remove("log.txt");               // (before starting up, so the start-up's own notes stay in the new log)
    sys::init();
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
    sceKernelExitProcess(0);
    return 0;
}

// vita-elf-create needs a few KB free at the end of the code segment for its own tables; when the
// code happens to end right at the segment boundary it fails ("segment 1 overlaps"). This read-only
// filler moves the boundary. If that error ever comes back, change its size by a few KB.
extern "C" __attribute__((used, aligned(16))) const char fl_segment_pad[6144] = {1};
