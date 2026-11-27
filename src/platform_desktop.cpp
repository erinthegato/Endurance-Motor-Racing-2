// Desktop platform (Windows / Linux): SDL2 window, OpenGL 2.1 compatibility profile, game controller,
// SDL audio, files. The same rules as platform_vita.cpp, so the whole game runs unchanged:
//  * the game still lays out its screens at 480 x 272. The picture is scaled up to fill the window
//    (black bars keep the 480:272 shape); 2D is drawn through an orthographic 480 x 272 projection,
//    3D renders at the window's full resolution with 4x MSAA;
//  * the game's 8-bit palette textures become 32-bit RGBA textures (mipmaps + anisotropic filtering)
//    the first time they are bound; static vertex arrays live in GPU buffers after their first draw;
//  * relative paths are mapped: "DATA/x" is read from "PC/x" first (the PC edition's own, more detailed
//    models), then "HD/x" (the Vita's), then "DATA/x", next to the exe;
//    anything else (saves, settings, logs, ghosts) lives in the user's app-data folder
//    (Windows: %APPDATA%\CODE4\EnduranceMotorRacing2\).
// Command line: --windowed [WxH]   a window instead of borderless full screen
//               --msaa N           multisampling (0, 2, 4, 8; default 4)
//               --novsync          don't wait for the screen's refresh
//               --fps N            frame cap (default 60 on screens faster than 60 Hz: Moonlight streams at 60)
#include <SDL.h>
#include <SDL_opengl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <cmath>
#include <string>
#include <vector>
#include <unordered_map>
#include <map>
#include <cstddef>
#include <zlib.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <malloc.h>
#else
#include <malloc.h>
#endif
#include "platform.h"
#include "game.h"

// ---------------------------------------------------------------- OpenGL past 1.1
// Windows' opengl32.dll only exports OpenGL 1.1; buffers (1.5) and mipmap generation (3.0, or the
// EXT version) are looked up from the driver once the context exists.
#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#endif
#ifndef GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif
#ifndef GL_GENERATE_MIPMAP
#define GL_GENERATE_MIPMAP 0x8191
#endif
#ifndef GL_MULTISAMPLE
#define GL_MULTISAMPLE 0x809D
#endif
#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER 0x8892
#endif
#ifndef GL_STATIC_DRAW
#define GL_STATIC_DRAW 0x88E4
#endif
#ifndef APIENTRY
#define APIENTRY
#endif
typedef ptrdiff_t fl_GLsizeiptr;
typedef void (APIENTRY* FL_GENBUFFERS)(GLsizei, GLuint*);
typedef void (APIENTRY* FL_BINDBUFFER)(GLenum, GLuint);
typedef void (APIENTRY* FL_BUFFERDATA)(GLenum, fl_GLsizeiptr, const void*, GLenum);
typedef void (APIENTRY* FL_DELETEBUFFERS)(GLsizei, const GLuint*);
typedef void (APIENTRY* FL_GENERATEMIPMAP)(GLenum);
static FL_GENBUFFERS fl_glGenBuffers = nullptr;
static FL_BINDBUFFER fl_glBindBuffer = nullptr;
static FL_BUFFERDATA fl_glBufferData = nullptr;
static FL_DELETEBUFFERS fl_glDeleteBuffers = nullptr;
static FL_GENERATEMIPMAP fl_glGenerateMipmap = nullptr;
static bool haveBuffers = false;

// the graphics code below is shared with the Vita layer and calls these names
#undef glGenBuffers
#undef glBindBuffer
#undef glBufferData
#undef glDeleteBuffers
#undef glGenerateMipmap
#define glGenBuffers(n, ids) fl_glGenBuffers(n, ids)
#define glBindBuffer(t, id) do { if (haveBuffers) fl_glBindBuffer(t, id); } while (0)
#define glBufferData(t, n, d, u) fl_glBufferData(t, (fl_GLsizeiptr)(n), d, u)
#define glDeleteBuffers(n, ids) do { if (haveBuffers) fl_glDeleteBuffers(n, ids); } while (0)
#define glGenerateMipmap(t) do { if (fl_glGenerateMipmap) fl_glGenerateMipmap(t); } while (0)

template <class F> static F glProc(const char* a, const char* b) {
    void* p = SDL_GL_GetProcAddress(a);
    if (!p && b) p = SDL_GL_GetProcAddress(b);
    return (F)p;
}
static void loadGlProcs() {
    fl_glGenBuffers = glProc<FL_GENBUFFERS>("glGenBuffers", "glGenBuffersARB");
    fl_glBindBuffer = glProc<FL_BINDBUFFER>("glBindBuffer", "glBindBufferARB");
    fl_glBufferData = glProc<FL_BUFFERDATA>("glBufferData", "glBufferDataARB");
    fl_glDeleteBuffers = glProc<FL_DELETEBUFFERS>("glDeleteBuffers", "glDeleteBuffersARB");
    fl_glGenerateMipmap = glProc<FL_GENERATEMIPMAP>("glGenerateMipmap", "glGenerateMipmapEXT");
    haveBuffers = fl_glGenBuffers && fl_glBindBuffer && fl_glBufferData && fl_glDeleteBuffers;
}

// GLSL (OpenGL 2.0): the per-pixel lighting. Loaded by hand like the buffers above; any missing piece
// and the game draws the old way (light baked into the colours).
typedef unsigned int (APIENTRY* SH_CREATESHADER)(GLenum);
typedef void (APIENTRY* SH_SHADERSOURCE)(GLuint, GLsizei, const char* const*, const GLint*);
typedef void (APIENTRY* SH_COMPILESHADER)(GLuint);
typedef void (APIENTRY* SH_GETSHADERIV)(GLuint, GLenum, GLint*);
typedef void (APIENTRY* SH_GETINFOLOG)(GLuint, GLsizei, GLsizei*, char*);
typedef unsigned int (APIENTRY* SH_CREATEPROGRAM)(void);
typedef void (APIENTRY* SH_ATTACHSHADER)(GLuint, GLuint);
typedef void (APIENTRY* SH_LINKPROGRAM)(GLuint);
typedef void (APIENTRY* SH_USEPROGRAM)(GLuint);
typedef GLint (APIENTRY* SH_GETUNIFORMLOCATION)(GLuint, const char*);
typedef void (APIENTRY* SH_UNIFORM1F)(GLint, GLfloat);
typedef void (APIENTRY* SH_UNIFORM1I)(GLint, GLint);
typedef void (APIENTRY* SH_UNIFORM3F)(GLint, GLfloat, GLfloat, GLfloat);
static SH_CREATESHADER sh_CreateShader; static SH_SHADERSOURCE sh_ShaderSource; static SH_COMPILESHADER sh_CompileShader;
static SH_GETSHADERIV sh_GetShaderiv, sh_GetProgramiv; static SH_GETINFOLOG sh_GetShaderInfoLog, sh_GetProgramInfoLog;
static SH_CREATEPROGRAM sh_CreateProgram; static SH_ATTACHSHADER sh_AttachShader; static SH_LINKPROGRAM sh_LinkProgram;
static SH_USEPROGRAM sh_UseProgram; static SH_GETUNIFORMLOCATION sh_GetUniformLocation;
static SH_UNIFORM1F sh_Uniform1f; static SH_UNIFORM1I sh_Uniform1i; static SH_UNIFORM3F sh_Uniform3f;
static bool loadShaderProcs() {
    sh_CreateShader = glProc<SH_CREATESHADER>("glCreateShader", nullptr);
    sh_ShaderSource = glProc<SH_SHADERSOURCE>("glShaderSource", nullptr);
    sh_CompileShader = glProc<SH_COMPILESHADER>("glCompileShader", nullptr);
    sh_GetShaderiv = glProc<SH_GETSHADERIV>("glGetShaderiv", nullptr);
    sh_GetProgramiv = glProc<SH_GETSHADERIV>("glGetProgramiv", nullptr);
    sh_GetShaderInfoLog = glProc<SH_GETINFOLOG>("glGetShaderInfoLog", nullptr);
    sh_GetProgramInfoLog = glProc<SH_GETINFOLOG>("glGetProgramInfoLog", nullptr);
    sh_CreateProgram = glProc<SH_CREATEPROGRAM>("glCreateProgram", nullptr);
    sh_AttachShader = glProc<SH_ATTACHSHADER>("glAttachShader", nullptr);
    sh_LinkProgram = glProc<SH_LINKPROGRAM>("glLinkProgram", nullptr);
    sh_UseProgram = glProc<SH_USEPROGRAM>("glUseProgram", nullptr);
    sh_GetUniformLocation = glProc<SH_GETUNIFORMLOCATION>("glGetUniformLocation", nullptr);
    sh_Uniform1f = glProc<SH_UNIFORM1F>("glUniform1f", nullptr);
    sh_Uniform1i = glProc<SH_UNIFORM1I>("glUniform1i", nullptr);
    sh_Uniform3f = glProc<SH_UNIFORM3F>("glUniform3f", nullptr);
    return sh_CreateShader && sh_ShaderSource && sh_CompileShader && sh_GetShaderiv && sh_GetProgramiv && sh_GetShaderInfoLog &&
           sh_GetProgramInfoLog && sh_CreateProgram && sh_AttachShader && sh_LinkProgram && sh_UseProgram &&
           sh_GetUniformLocation && sh_Uniform1f && sh_Uniform1i && sh_Uniform3f;
}

// ---------------------------------------------------------------- window
static SDL_Window* window = nullptr;
static SDL_GLContext glctx = nullptr;
static int winW = 1280, winH = 720;                 // drawable size in pixels
static int vpX = 0, vpY = 0, vpW = 1280, vpH = 720;  // the 480:272 picture inside it
static bool vsyncWanted = true;                       // --novsync turns it off for good
// frame-rate cap: Moonlight streams at 60, so on a faster screen (120 / 144 Hz) the game still makes
// 60 frames a second -- the extra ones would never reach the Vita, and the GPU stays cooler.
// --fps N sets it (0 = no cap); by default 60 when the screen refreshes faster than ~65 Hz.
static int fpsCap = -1;

// the biggest 480:272 rectangle that fits the window, centred (black bars on the other sides)
static void layoutViewport() {
    SDL_GL_GetDrawableSize(window, &winW, &winH);
    if (winW < 1) winW = 1;
    if (winH < 1) winH = 1;
    if (winW * SCREEN_H > winH * SCREEN_W) { vpH = winH; vpW = winH * SCREEN_W / SCREEN_H; }
    else { vpW = winW; vpH = winW * SCREEN_H / SCREEN_W; }
    vpX = (winW - vpW) / 2; vpY = (winH - vpH) / 2;
}

// ---------------------------------------------------------------- paths
// The game opens files by relative path. The linker sends every fopen / remove here
// (-Wl,--wrap=fopen,--wrap=remove). Windows paths are opened as UTF-16 so any user name works.
extern "C" FILE* __real_fopen(const char* path, const char* mode);
extern "C" int __real_remove(const char* path);

static std::string baseDir, saveDir;   // both end with a slash

// layer 0: PC/ (the PC edition's own, more detailed models), 1: HD/ (the Vita's), 2: DATA/
static void mapPath(const char* in, std::string& out, int layer) {
    bool absolute = in[0] == '/' || in[0] == '\\' || (in[0] && in[1] == ':');
    if (absolute) { out = in; return; }
    if (!strncmp(in, "DATA/", 5)) {
        static const char* L[3] = {"PC/", "HD/", "DATA/"};
        out = baseDir + L[layer < 0 ? 2 : (layer > 2 ? 2 : layer)] + (in + 5);
        return;
    }
    out = saveDir + in;
}

#ifdef _WIN32
static std::wstring widen(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? n : 1, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    w.resize(wcslen(w.c_str()));
    return w;
}
static FILE* openReal(const std::string& p, const char* mode) { return _wfopen(widen(p).c_str(), widen(mode).c_str()); }
static int removeReal(const std::string& p) { return _wremove(widen(p).c_str()); }
#else
static FILE* openReal(const std::string& p, const char* mode) { return __real_fopen(p.c_str(), mode); }
static int removeReal(const std::string& p) { return __real_remove(p.c_str()); }
#endif

extern "C" FILE* __wrap_fopen(const char* path, const char* mode) {
    if (!path || !mode) return nullptr;
    std::string p;
    if (mode[0] == 'r' && !strncmp(path, "DATA/", 5)) {
        for (int layer = 0; layer < 2; layer++) {
            mapPath(path, p, layer);
            if (FILE* f = openReal(p, mode)) return f;
        }
    }
    mapPath(path, p, 2);
    return openReal(p, mode);
}
extern "C" int __wrap_remove(const char* path) {
    if (!path) return -1;
    std::string p; mapPath(path, p, 2);
    return removeReal(p);
}

namespace gfx { static void applySync(); }

// ---------------------------------------------------------------- graphics
namespace {
float anisotropy = 1;
u8* arena = nullptr;
const int ARENA_SIZE = 16 << 20;
int arenaUsed = 0;

// RGBA copies of the game's palette textures
struct GpuTex { const u8* pix; const u32* pal; int w, h; GLuint id; };
std::vector<GpuTex> texCache;
int lastHit = -1;
const Texture* curTex = nullptr;
bool texOn = false;

// what the game asked for (2D drawing changes some of it for a moment)
M4 projM = M4::identity(), viewM = M4::identity();
bool fogWanted = false, depthWanted = true, in2D = false;
bool progOn = false;          // the lighting program is in use (see useLit)

inline bool overlaps(const void* a, int an, const void* b, int bn) {
    const u8 *x = (const u8*)a, *y = (const u8*)b;
    return x < y + bn && y < x + an;
}

GLuint upload(const Texture* t) {
    for (int i = (lastHit >= 0 ? lastHit : 0), k = 0; k < (int)texCache.size(); k++, i = (i + 1) % (int)texCache.size()) {
        const GpuTex& g = texCache[i];
        if (g.pix == t->pixels && g.pal == t->palette && g.w == t->w && g.h == t->h) { lastHit = i; return g.id; }
    }
    // unswizzle (16 x 8 blocks) and look up the palette
    int w = t->w, h = t->h, bw = w / 16 > 0 ? w / 16 : 1;
    std::vector<u32> rgba((size_t)w * h);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int off = ((y >> 3) * bw + (x >> 4)) * 128 + (y & 7) * 16 + (x & 15);
            rgba[(size_t)y * w + x] = t->palette[t->pixels[off]];
        }
    // PC: always 32-bit colour; mipmaps on every power-of-two texture of any size that isn't see-through,
    // and anisotropic filtering so the road and kerbs stay sharp at a glancing angle.
    bool opaque = true;
    for (size_t i = 0; i < rgba.size() && opaque; i++) opaque = (rgba[i] >> 24) == 0xFF;
    // Big opaque textures (road, grass, car paint...) get mipmaps so they stay
    // smooth in the distance instead of shimmering. HUD/font/cut-out ones don't.
    bool pow2 = !(w & (w - 1)) && !(h & (h - 1));
    bool mip = opaque && pow2 && w >= 16 && h >= 16;
    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mip ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    if (mip && anisotropy > 1) glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, anisotropy);
    if (mip && !fl_glGenerateMipmap) glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE);   // GL 1.4 way
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    if (mip) glGenerateMipmap(GL_TEXTURE_2D);
    texCache.push_back(GpuTex{t->pixels, t->palette, w, h, id});
    lastHit = (int)texCache.size() - 1;
    return id;
}

// forget GPU copies made from memory in [p, p + n)
void forget(const void* p, int n) {
    for (int i = (int)texCache.size() - 1; i >= 0; i--) {
        GpuTex& g = texCache[i];
        if (overlaps(p, n, g.pix, g.w * g.h) || overlaps(p, n, g.pal, 1024)) {
            glDeleteTextures(1, &g.id);
            texCache[i] = texCache.back(); texCache.pop_back();
        }
    }
    lastHit = -1;
    curTex = nullptr;
}

// GPU copies of static vertex arrays (models, circuit chunks, pit lane): made on first draw, kept
// until the game rewrites that memory (gfx::flushCache) or frees it. Per-frame arrays (frame memory)
// are sent the slow way every time.
struct Vbo { const u8* ptr; int bytes; GLuint id; };
std::unordered_map<uint64_t, Vbo> vbos, litVbos;   // (lit: the models' copies with normals)
struct NormRange { const ModelVertex* v; int count; const int8_t* n; };
std::map<uintptr_t, NormRange> normRanges;
inline bool inArena(const void* p) { return arena && (const u8*)p >= arena && (const u8*)p < arena + ARENA_SIZE; }
inline uint64_t vkey(const void* p, int count, bool model) { return ((uint64_t)(uintptr_t)p << 1) ^ ((uint64_t)count << 40) ^ (model ? 1 : 0); }

void forgetVbos(const void* p, int n) {
    for (auto it = vbos.begin(); it != vbos.end();) {
        if (overlaps(p, n, it->second.ptr, it->second.bytes)) { glDeleteBuffers(1, &it->second.id); it = vbos.erase(it); }
        else ++it;
    }
    for (auto it = litVbos.begin(); it != litVbos.end();) {
        if (overlaps(p, n, it->second.ptr, it->second.bytes)) { glDeleteBuffers(1, &it->second.id); it = litVbos.erase(it); }
        else ++it;
    }
    for (auto it = normRanges.begin(); it != normRanges.end();) {
        if (overlaps(p, n, it->second.v, it->second.count * (int)sizeof(ModelVertex))) it = normRanges.erase(it);
        else ++it;
    }
}

GLuint vboFor(const void* src, int count, bool model) {
    if (!haveBuffers || inArena(src)) return 0;     // (no buffer support: plain arrays every draw)
    uint64_t k = vkey(src, count, model);
    auto it = vbos.find(k);
    if (it != vbos.end()) return it->second.id;
    std::vector<WorldVertex> tmp;
    const WorldVertex* data = (const WorldVertex*)src;
    if (model) {
        tmp.resize(count);
        const ModelVertex* v = (const ModelVertex*)src;
        const float kk = 1.0f / 32768.0f;
        for (int i = 0; i < count; i++) tmp[i] = WorldVertex{v[i].u * kk, v[i].v * kk, v[i].color, v[i].x * kk, v[i].y * kk, v[i].z * kk};
        data = tmp.data();
    }
    GLuint id = 0;
    glGenBuffers(1, &id);
    glBindBuffer(GL_ARRAY_BUFFER, id);
    glBufferData(GL_ARRAY_BUFFER, count * sizeof(WorldVertex), data, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    vbos[k] = Vbo{(const u8*)src, count * (int)(model ? sizeof(ModelVertex) : sizeof(WorldVertex)), id};
    return id;
}

void applyTex() {
    if (curTex && curTex->pixels && curTex->palette) {
        glEnable(GL_TEXTURE_2D);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glBindTexture(GL_TEXTURE_2D, upload(curTex));
        texOn = true;
    } else {
        glDisable(GL_TEXTURE_2D);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        texOn = false;
    }
}

void enter3D() {
    if (!in2D) return;
    in2D = false;
    glMatrixMode(GL_PROJECTION); glLoadMatrixf(projM.m);
    if (fogWanted) glEnable(GL_FOG);
    if (depthWanted) glEnable(GL_DEPTH_TEST);
}
void enter2D() {
    if (in2D) return;
    in2D = true;
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glOrtho(0, SCREEN_W, SCREEN_H, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    if (progOn) { sh_UseProgram(0); progOn = false; }
    glDisable(GL_FOG);
    glDisable(GL_DEPTH_TEST);
}

void drawVbo(GLuint id, int count, GLenum mode) {
    glBindBuffer(GL_ARRAY_BUFFER, id);
    glVertexPointer(3, GL_FLOAT, sizeof(WorldVertex), (void*)offsetof(WorldVertex, x));
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(WorldVertex), (void*)offsetof(WorldVertex, color));
    if (texOn) glTexCoordPointer(2, GL_FLOAT, sizeof(WorldVertex), (void*)offsetof(WorldVertex, u));
    glDrawArrays(mode, 0, count);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void drawArrays(const WorldVertex* v, int count, GLenum mode) {
    glVertexPointer(3, GL_FLOAT, sizeof(WorldVertex), &v->x);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(WorldVertex), &v->color);
    if (texOn) glTexCoordPointer(2, GL_FLOAT, sizeof(WorldVertex), &v->u);
    glDrawArrays(mode, 0, count);
}

void setModel(const M4& model) {
    M4 mv = viewM * model;
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(mv.m);
}

// ---------------------------------------------------------------- real lighting
// One small GLSL program lights the 3D world per pixel: light from the sky above and the ground
// below (by which way the surface faces), the sun on top, a highlight on shiny things, the fog.
// The PC set's models bring their normals ('EMN1'); the circuit's strips have none and keep their
// baked colours, scaled by how much sun and sky there is.
const char* LIT_VS = R"(#version 120
varying vec4 vCol; varying vec3 vN; varying vec3 vPos; varying vec2 vUV;
void main() {
    vec4 e = gl_ModelViewMatrix * gl_Vertex;
    vPos = e.xyz;
    vN = gl_NormalMatrix * gl_Normal;
    vCol = gl_Color;
    vUV = gl_MultiTexCoord0.xy;
    gl_Position = gl_ProjectionMatrix * e;
}
)";
const char* LIT_FS = R"(#version 120
varying vec4 vCol; varying vec3 vN; varying vec3 vPos; varying vec2 vUV;
uniform sampler2D uTex;
uniform float uTexOn, uHasN, uFog, uSpec, uGloss;
uniform vec3 uSun, uUp, uSunCol, uSky, uGround, uWorldK;
void main() {
    vec4 c = vCol;
    if (uTexOn > 0.5) c *= texture2D(uTex, vUV);
    if (c.a <= 8.0 / 255.0) discard;                 // (the old alpha test)
    vec3 V = normalize(-vPos);
    vec3 col;
    if (uHasN > 0.5) {
        vec3 N = normalize(vN);
        // the models are drawn from both sides: turn the normal to the side we're looking at
        vec3 F = normalize(cross(dFdx(vPos), dFdy(vPos)));
        if (dot(F, V) < 0.0) F = -F;
        if (dot(N, F) < 0.0) N = -N;
        vec3 amb = mix(uGround, uSky, dot(N, uUp) * 0.5 + 0.5);
        float d = max(dot(N, uSun), 0.0);
        vec3 H = normalize(uSun + V);
        float s = d > 0.0 ? pow(max(dot(N, H), 0.0), uGloss) * uSpec * (uGloss + 8.0) / 40.0 : 0.0;
        float fr = pow(1.0 - max(dot(N, V), 0.0), 5.0) * uSpec;
        col = c.rgb * (amb + uSunCol * d) + uSunCol * s + uSky * fr * 0.35;
    } else {
        col = c.rgb * uWorldK;
    }
    if (uFog > 0.5) col = mix(gl_Fog.color.rgb, col, clamp((gl_Fog.end - abs(vPos.z)) * gl_Fog.scale, 0.0, 1.0));
    gl_FragColor = vec4(col, c.a);
}
)";
GLuint litProg = 0;
GLint uTex = -1, uTexOn = -1, uHasN = -1, uFog = -1, uSpec = -1, uGloss = -1, uSun = -1, uUp = -1, uSunCol = -1, uSky = -1, uGround = -1, uWorldK = -1;
gfx::Lighting light;
bool litWanted = false, blendOn = false, lightDirty = true;
float matSpec = 0.15f, matGloss = 24;

GLuint compileShader(GLenum type, const char* src) {
    GLuint s = sh_CreateShader(type);
    sh_ShaderSource(s, 1, &src, nullptr);
    sh_CompileShader(s);
    GLint ok = 0; sh_GetShaderiv(s, 0x8B81 /*GL_COMPILE_STATUS*/, &ok);
    if (!ok) { char log[1024] = {0}; sh_GetShaderInfoLog(s, sizeof log, nullptr, log); sys::log("lighting shader: %s\n", log); return 0; }
    return s;
}
void buildLighting() {
    if (getenv("EMR_NOLIGHT") || !loadShaderProcs()) { sys::log("lighting: off (baked)\n"); return; }
    GLuint vs = compileShader(0x8B31 /*GL_VERTEX_SHADER*/, LIT_VS), fs = compileShader(0x8B30 /*GL_FRAGMENT_SHADER*/, LIT_FS);
    if (!vs || !fs) return;
    GLuint p = sh_CreateProgram();
    sh_AttachShader(p, vs); sh_AttachShader(p, fs);
    sh_LinkProgram(p);
    GLint ok = 0; sh_GetProgramiv(p, 0x8B82 /*GL_LINK_STATUS*/, &ok);
    if (!ok) { char log[1024] = {0}; sh_GetProgramInfoLog(p, sizeof log, nullptr, log); sys::log("lighting link: %s\n", log); return; }
    litProg = p;
    uTex = sh_GetUniformLocation(p, "uTex"); uTexOn = sh_GetUniformLocation(p, "uTexOn"); uHasN = sh_GetUniformLocation(p, "uHasN");
    uFog = sh_GetUniformLocation(p, "uFog"); uSpec = sh_GetUniformLocation(p, "uSpec"); uGloss = sh_GetUniformLocation(p, "uGloss");
    uSun = sh_GetUniformLocation(p, "uSun"); uUp = sh_GetUniformLocation(p, "uUp"); uSunCol = sh_GetUniformLocation(p, "uSunCol");
    uSky = sh_GetUniformLocation(p, "uSky"); uGround = sh_GetUniformLocation(p, "uGround"); uWorldK = sh_GetUniformLocation(p, "uWorldK");
    sh_UseProgram(p); sh_Uniform1i(uTex, 0); sh_UseProgram(0);
    sys::log("lighting: per pixel\n");
}
inline V3 toEye(const V3& v) {        // a world direction into the camera's space (the view's rotation)
    const float* m = viewM.m;
    return norm(V3(m[0] * v.x + m[4] * v.y + m[8] * v.z, m[1] * v.x + m[5] * v.y + m[9] * v.z, m[2] * v.x + m[6] * v.y + m[10] * v.z));
}
// switch the program on for a lit draw (or off): true when this draw is lit
bool useLit(bool hasNormals) {
    bool want = litProg && litWanted && !in2D && !blendOn;
    if (!want) { if (progOn) { sh_UseProgram(0); progOn = false; } return false; }
    if (!progOn) { sh_UseProgram(litProg); progOn = true; lightDirty = true; }
    if (lightDirty) {
        V3 s = toEye(light.sun), u = toEye(V3(0, 0, 1));
        sh_Uniform3f(uSun, s.x, s.y, s.z); sh_Uniform3f(uUp, u.x, u.y, u.z);
        sh_Uniform3f(uSunCol, light.sunCol[0], light.sunCol[1], light.sunCol[2]);
        sh_Uniform3f(uSky, light.sky[0], light.sky[1], light.sky[2]);
        sh_Uniform3f(uGround, light.ground[0], light.ground[1], light.ground[2]);
        sh_Uniform3f(uWorldK, light.worldK[0], light.worldK[1], light.worldK[2]);
        sh_Uniform1f(uSpec, matSpec); sh_Uniform1f(uGloss, matGloss);
        lightDirty = false;
    }
    sh_Uniform1f(uTexOn, texOn ? 1.0f : 0.0f);
    sh_Uniform1f(uHasN, hasNormals ? 1.0f : 0.0f);
    sh_Uniform1f(uFog, fogWanted ? 1.0f : 0.0f);
    return true;
}

// the normals the models brought ('EMN1'): looked up by the vertex memory they belong to (the
// renderer draws parts of a model too: body, nose, wing, glass)
const int8_t* normalsFor(const ModelVertex* v, int count) {
    auto it = normRanges.upper_bound((uintptr_t)v);
    if (it == normRanges.begin()) return nullptr;
    --it;
    const NormRange& r = it->second;
    if (v < r.v || v + count > r.v + r.count) return nullptr;
    return r.n + (v - r.v) * 4;
}
// the lit copy of a model: the baked sun taken back out of the colour, and the normal
struct LitVertex { float u, v; u32 color; float x, y, z; int8_t nx, ny, nz, pad; };
GLuint litVboFor(const ModelVertex* v, int count, const int8_t* n) {
    if (!haveBuffers) return 0;
    uint64_t k = vkey(v, count, true);
    auto it = litVbos.find(k);
    if (it != litVbos.end()) return it->second.id;
    std::vector<LitVertex> tmp(count);
    const float kk = 1.0f / 32768.0f;
    for (int i = 0; i < count; i++) {
        const u8* nb = (const u8*)n + i * 4;
        float baked = nb[3] * (1.5f / 255.0f), inv = baked > 0.05f ? 1.0f / baked : 1.0f;
        u32 c = v[i].color;
        u32 r = (u32)fminf((c & 255) * inv, 255), g = (u32)fminf(((c >> 8) & 255) * inv, 255), b = (u32)fminf(((c >> 16) & 255) * inv, 255);
        tmp[i] = LitVertex{v[i].u * kk, v[i].v * kk, (c & 0xFF000000u) | (b << 16) | (g << 8) | r, v[i].x * kk, v[i].y * kk, v[i].z * kk,
                           (int8_t)nb[0], (int8_t)nb[1], (int8_t)nb[2], 0};
    }
    GLuint id = 0;
    glGenBuffers(1, &id);
    glBindBuffer(GL_ARRAY_BUFFER, id);
    glBufferData(GL_ARRAY_BUFFER, count * sizeof(LitVertex), tmp.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    litVbos[k] = Vbo{(const u8*)v, count * (int)sizeof(ModelVertex), id};
    return id;
}
void drawLitVbo(GLuint id, int count) {
    glBindBuffer(GL_ARRAY_BUFFER, id);
    glEnableClientState(GL_NORMAL_ARRAY);
    glVertexPointer(3, GL_FLOAT, sizeof(LitVertex), (void*)offsetof(LitVertex, x));
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(LitVertex), (void*)offsetof(LitVertex, color));
    glNormalPointer(GL_BYTE, sizeof(LitVertex), (void*)offsetof(LitVertex, nx));
    if (texOn) glTexCoordPointer(2, GL_FLOAT, sizeof(LitVertex), (void*)offsetof(LitVertex, u));
    glDrawArrays(GL_TRIANGLES, 0, count);
    glDisableClientState(GL_NORMAL_ARRAY);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}
}

namespace gfx {
static bool syncOn = true; static int syncEvery = 1;
static float waitSec = 0, gpuWaitSec = 0;
float lastWaitSec() { return waitSec; }
float lastGpuWaitSec() { return gpuWaitSec; }
void setSync(bool vsync, int every) {
    syncOn = vsync || every > 1; syncEvery = every < 1 ? 1 : every;
    applySync();
}
void beginFrame(u32 clear) {
    arenaUsed = 0;
    // the window's edges outside the game's 480:272 picture stay black
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, winW, winH);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glViewport(vpX, vpY, vpW, vpH);
    glEnable(GL_SCISSOR_TEST); glScissor(vpX, vpY, vpW, vpH);
    glClearColor((clear & 255) / 255.0f, ((clear >> 8) & 255) / 255.0f, ((clear >> 16) & 255) / 255.0f, 1);
    glClearDepth(1.0);
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    in2D = true; enter3D();
    curTex = nullptr; applyTex();
    gfx::setLit(false);
}
void endFrame() {
    double t0 = sys::timeSec();
    glDisable(GL_SCISSOR_TEST);
    SDL_GL_SwapWindow(window);
    // the cap (only while syncing: the benchmark turns vsync off to see the real headroom)
    static double nextAt = 0;
    if (fpsCap > 0 && syncOn) {
        double now = sys::timeSec(), period = 1.0 / fpsCap;
        if (nextAt < now - period) nextAt = now;           // (fell behind: start again from now)
        while (now < nextAt) {
            double left = nextAt - now;
            if (left > 0.002) SDL_Delay((Uint32)((left - 0.001) * 1000)); // sleep most of it, spin the last ms
            now = sys::timeSec();
        }
        nextAt += period;
    }
    waitSec = (float)(sys::timeSec() - t0);
    gpuWaitSec = 0;
}
void clearDepth() { glDepthMask(GL_TRUE); glClear(GL_DEPTH_BUFFER_BIT); }
bool beginInset(float x, float y, float w, float h, u32 c) {
    float k = vpW / (float)SCREEN_W;
    int sx = vpX + (int)(x * k), sy = vpY + (int)((SCREEN_H - y - h) * k), sw = (int)(w * k), sh = (int)(h * k);
    glViewport(sx, sy, sw, sh);
    glEnable(GL_SCISSOR_TEST); glScissor(sx, sy, sw, sh);
    glClearColor((c & 255) / 255.0f, ((c >> 8) & 255) / 255.0f, ((c >> 16) & 255) / 255.0f, 1);
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    return true;
}
void endInset() { glViewport(vpX, vpY, vpW, vpH); glScissor(vpX, vpY, vpW, vpH); }
void setProjection(const M4& p) { projM = p; if (!in2D) { glMatrixMode(GL_PROJECTION); glLoadMatrixf(p.m); } }
void setView(const M4& v) { viewM = v; lightDirty = true; }
void setFog(bool on, float n, float f, u32 c) {
    fogWanted = on;
    if (on) {
        GLfloat col[4] = {(c & 255) / 255.0f, ((c >> 8) & 255) / 255.0f, ((c >> 16) & 255) / 255.0f, 1};
        glFogi(GL_FOG_MODE, GL_LINEAR);
        glFogf(GL_FOG_START, n); glFogf(GL_FOG_END, f);
        glFogfv(GL_FOG_COLOR, col);
    }
    if (!in2D) { if (on) glEnable(GL_FOG); else glDisable(GL_FOG); }
}
void bindTexture(const Texture* t) {
    const Texture* want = (t && t->pixels && t->palette) ? t : nullptr;
    if (want == curTex && (want != nullptr) == texOn) return;
    curTex = want; applyTex();
}
void setBlend(bool on) {
    blendOn = on;
    if (on) { glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); }
    else glDisable(GL_BLEND);
}
void setDepthTest(bool on) { depthWanted = on; if (!in2D) { if (on) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST); } }
void setDepthWrite(bool on) { glDepthMask(on ? GL_TRUE : GL_FALSE); }

void drawModel(const ModelVertex* v, int count, const M4& model) {
    if (count <= 0) return;
    enter3D();
    if (const int8_t* n = normalsFor(v, count))
        if (useLit(true))
            if (GLuint id = litVboFor(v, count, n)) { setModel(model); drawLitVbo(id, count); static int c = 0; if (getenv("EMR_LITLOG") && c++ % 200 == 0) sys::log("lit draw %d (%d verts)\n", c, count); return; }
    useLit(false);
    if (GLuint id = vboFor(v, count, true)) { setModel(model); drawVbo(id, count, GL_TRIANGLES); return; }
    // 16-bit model vertices -> floats (positions and texture coordinates are value / 32768)
    WorldVertex* w = (WorldVertex*)frameAlloc(count * (int)sizeof(WorldVertex));
    if (!w) return;
    const float k = 1.0f / 32768.0f;
    for (int i = 0; i < count; i++) {
        const ModelVertex& q = v[i];
        w[i] = WorldVertex{q.u * k, q.v * k, q.color, q.x * k, q.y * k, q.z * k};
    }
    setModel(model);
    drawArrays(w, count, GL_TRIANGLES);
}
void drawWorld(const WorldVertex* v, int count, Prim prim, const M4& model) {
    if (count <= 0) return;
    static const GLenum P[4] = {GL_TRIANGLES, GL_TRIANGLE_STRIP, GL_LINES, GL_LINE_STRIP};
    enter3D();
    useLit(false);             // (the circuit: lit without normals, when lighting is on)
    setModel(model);
    if (GLuint id = vboFor(v, count, false)) { drawVbo(id, count, P[prim]); return; }
    drawArrays(v, count, P[prim]);
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
    drawArrays(w, n * 6, GL_TRIANGLES);
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
// The game calls this after writing pixels or palettes: the RGBA copy made from them is stale.
void frameTrim(void* p, int keepBytes) {
    u8* b = (u8*)p;
    if (!b || b < arena || b > arena + arenaUsed) return;
    int end = (int)(b - arena) + ((keepBytes + 15) & ~15);
    if (end < arenaUsed) arenaUsed = end;   // (only shrinks; the block was the last one handed out)
}
void flushCache(const void* p, int bytes) { if (p && bytes > 0 && !inArena(p)) { forget(p, bytes); forgetVbos(p, bytes); } }
bool gpuClips() { return true; }
// Textures live on the GPU once bound.
bool moveToFastMemory(Texture&) { return false; }
int fastMark() { return 0; }
int vramUsedKB() { return 0; }
void fastRelease(int) {}
}


namespace gfx {
// vsync: one refresh per frame (60 fps on a 60 Hz screen; Sunshine captures at the display's rate),
// or every second refresh for the game's 30 fps setting. Adaptive vsync (-1) is tried first so a
// frame that just misses its refresh doesn't drop straight to half rate.
static void applySync() {
    if (!window) return;
    if (!vsyncWanted || !syncOn) { SDL_GL_SetSwapInterval(0); return; }
    if (syncEvery > 1) { SDL_GL_SetSwapInterval(syncEvery); return; }
    if (SDL_GL_SetSwapInterval(-1) != 0) SDL_GL_SetSwapInterval(1);
}
bool highDetail() { return true; }
void setLighting(const Lighting& l) { light = l; light.sun = norm(l.sun); lightDirty = true; }
void setLit(bool on) { litWanted = on; if (!on && progOn) { sh_UseProgram(0); progOn = false; } }
void setMaterial(float spec, float gloss) {
    if (spec == matSpec && gloss == matGloss) return;
    matSpec = spec; matGloss = gloss;
    if (progOn) { sh_Uniform1f(uSpec, spec); sh_Uniform1f(uGloss, gloss); } else lightDirty = true;
}
void attachNormals(const ModelVertex* v, const int8_t* n, int count) {
    if (v && n && count > 0) normRanges[(uintptr_t)v] = NormRange{v, count, n};
}
}

// ---------------------------------------------------------------- input
// Moonlight (on the PS Vita, or anything else) reaches the PC as an Xbox controller, so the game
// controller API sees it: A = cross, B = circle, X = square, Y = triangle, Back = select.
// Every connected controller is read and combined, plus a keyboard fallback.
namespace input {
static std::vector<SDL_GameController*> pads;

static void openPad(int index) {
    if (!SDL_IsGameController(index)) return;
    SDL_GameController* c = SDL_GameControllerOpen(index);
    if (!c) return;
    for (SDL_GameController* p : pads) if (p == c) return;
    pads.push_back(c);
    sys::log("controller: %s\n", SDL_GameControllerName(c) ? SDL_GameControllerName(c) : "?");
}
static void closePad(SDL_JoystickID id) {
    for (size_t i = 0; i < pads.size(); i++)
        if (SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pads[i])) == id) {
            SDL_GameControllerClose(pads[i]); pads.erase(pads.begin() + i); return;
        }
}
static float axis(SDL_GameController* c, SDL_GameControllerAxis a) {
    int v = SDL_GameControllerGetAxis(c, a);
    return v < 0 ? v / 32768.0f : v / 32767.0f;
}
static float dead(float v, float z) { return fabsf(v) < z ? 0 : v; }
static float bigger(float a, float b) { return fabsf(b) > fabsf(a) ? b : a; }

bool hasTouch() { return false; }

void poll(Pad& p) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
            case SDL_QUIT: sys::quit(); break;
            case SDL_CONTROLLERDEVICEADDED: openPad(e.cdevice.which); break;
            case SDL_CONTROLLERDEVICEREMOVED: closePad(e.cdevice.which); break;
            case SDL_WINDOWEVENT:
                if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED || e.window.event == SDL_WINDOWEVENT_RESIZED) layoutViewport();
                break;
            case SDL_KEYDOWN:
                // Alt + Enter: full screen <-> window
                if (e.key.keysym.sym == SDLK_RETURN && (e.key.keysym.mod & KMOD_ALT) && !e.key.repeat) {
                    bool full = SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN;
                    SDL_SetWindowFullscreen(window, full ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
                    SDL_ShowCursor(full ? SDL_ENABLE : SDL_DISABLE);
                    layoutViewport();
                }
                break;
        }
    }
    p = Pad();
    for (SDL_GameController* c : pads) {
        auto btn = [&](SDL_GameControllerButton b) { return SDL_GameControllerGetButton(c, b) != 0; };
        p.cross |= btn(SDL_CONTROLLER_BUTTON_A); p.circle |= btn(SDL_CONTROLLER_BUTTON_B);
        p.square |= btn(SDL_CONTROLLER_BUTTON_X); p.triangle |= btn(SDL_CONTROLLER_BUTTON_Y);
        p.select |= btn(SDL_CONTROLLER_BUTTON_BACK); p.start |= btn(SDL_CONTROLLER_BUTTON_START);
        p.up |= btn(SDL_CONTROLLER_BUTTON_DPAD_UP); p.down |= btn(SDL_CONTROLLER_BUTTON_DPAD_DOWN);
        p.left |= btn(SDL_CONTROLLER_BUTTON_DPAD_LEFT); p.right |= btn(SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
        p.l |= btn(SDL_CONTROLLER_BUTTON_LEFTSHOULDER); p.r |= btn(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
        p.stickX = bigger(p.stickX, dead(axis(c, SDL_CONTROLLER_AXIS_LEFTX), 0.12f));
        p.stickY = bigger(p.stickY, dead(axis(c, SDL_CONTROLLER_AXIS_LEFTY), 0.12f));
        p.rstickX = bigger(p.rstickX, dead(axis(c, SDL_CONTROLLER_AXIS_RIGHTX), 0.10f));
        p.rstickY = bigger(p.rstickY, dead(axis(c, SDL_CONTROLLER_AXIS_RIGHTY), 0.10f));
        p.trigL = fmaxf(p.trigL, dead(axis(c, SDL_CONTROLLER_AXIS_TRIGGERLEFT), 0.05f));
        p.trigR = fmaxf(p.trigR, dead(axis(c, SDL_CONTROLLER_AXIS_TRIGGERRIGHT), 0.05f));
        // the Vita's rear touch pad skips music in the menus (left half back, right half on):
        // here the stick clicks do it (Moonlight on the Vita can put them on its rear touch pad)
        if (btn(SDL_CONTROLLER_BUTTON_RIGHTSTICK)) { p.backTouch = true; p.backX = 400; }
        else if (btn(SDL_CONTROLLER_BUTTON_LEFTSTICK)) { p.backTouch = true; p.backX = 80; }
    }
    // keyboard: arrows = d-pad, Enter / Space = X, Backspace = O, Esc = START, Tab = SELECT,
    // W / S = throttle / brake, Q / E = L / R
    const Uint8* k = SDL_GetKeyboardState(nullptr);
    bool alt = k[SDL_SCANCODE_LALT] || k[SDL_SCANCODE_RALT];
    p.up |= k[SDL_SCANCODE_UP] != 0; p.down |= k[SDL_SCANCODE_DOWN] != 0;
    p.left |= k[SDL_SCANCODE_LEFT] != 0; p.right |= k[SDL_SCANCODE_RIGHT] != 0;
    p.cross |= (k[SDL_SCANCODE_RETURN] && !alt) || k[SDL_SCANCODE_SPACE] || k[SDL_SCANCODE_KP_ENTER];
    p.circle |= k[SDL_SCANCODE_BACKSPACE] != 0;
    p.start |= k[SDL_SCANCODE_ESCAPE] != 0; p.select |= k[SDL_SCANCODE_TAB] != 0;
    p.l |= k[SDL_SCANCODE_Q] != 0; p.r |= k[SDL_SCANCODE_E] != 0;
    if (k[SDL_SCANCODE_W]) p.trigR = 1;
    if (k[SDL_SCANCODE_S]) p.trigL = 1;
    p.hasTilt = false; p.touches = 0;
}
}

// ---------------------------------------------------------------- audio
namespace audio {
static MixFn mixFn = nullptr;
static SDL_AudioDeviceID dev = 0;
static void SDLCALL callback(void*, Uint8* stream, int len) {
    MixFn f = mixFn;
    if (f) f((int16_t*)stream, len / 4); else memset(stream, 0, len);
}
void start(MixFn fn) {
    if (dev) { SDL_LockAudioDevice(dev); mixFn = fn; SDL_UnlockAudioDevice(dev); return; }
    mixFn = fn;
    SDL_AudioSpec want{}, got{};
    want.freq = 44100; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 512;   // ~12 ms: streaming adds its own delay want.callback = callback;
    dev = SDL_OpenAudioDevice(nullptr, 0, &want, &got, 0);    // no changes allowed: SDL converts if it must
    if (!dev) { sys::log("audio failed: %s\n", SDL_GetError()); return; }
    SDL_PauseAudioDevice(dev, 0);
}
void stop() {
    if (!dev) return;
    SDL_CloseAudioDevice(dev);
    dev = 0;
}
}

// ---------------------------------------------------------------- system
namespace sys {
static bool runningFlag = true;
static Uint64 t0 = 0;
static int msaaWanted = 4;
static bool windowed = false;
static int wantW = 1600, wantH = 906;

static void parseArgs(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--windowed")) {
            windowed = true;
            int w, h;
            if (i + 1 < argc && sscanf(argv[i + 1], "%dx%d", &w, &h) == 2 && w >= 320 && h >= 180) { wantW = w; wantH = h; i++; }
        } else if (!strcmp(argv[i], "--msaa") && i + 1 < argc) msaaWanted = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--novsync")) vsyncWanted = false;
        else if (!strcmp(argv[i], "--fps") && i + 1 < argc) fpsCap = atoi(argv[++i]);
    }
}

static bool makeWindow(int msaa) {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, msaa > 0 ? 1 : 0);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, msaa > 0 ? msaa : 0);
    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI | (windowed ? SDL_WINDOW_RESIZABLE : SDL_WINDOW_FULLSCREEN_DESKTOP);
    window = SDL_CreateWindow("Endurance Motor Racing 2.0", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, wantW, wantH, flags);
    if (!window) return false;
    glctx = SDL_GL_CreateContext(window);
    if (!glctx) { SDL_DestroyWindow(window); window = nullptr; return false; }
    return true;
}

bool init() {
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");   // Moonlight's pad works even if focus moves
    SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Endurance Motor Racing 2.0", SDL_GetError(), nullptr);
        return false;
    }
    t0 = SDL_GetPerformanceCounter();
    if (char* b = SDL_GetBasePath()) { baseDir = b; SDL_free(b); }
    if (char* s = SDL_GetPrefPath("CODE4", "EnduranceMotorRacing2")) { saveDir = s; SDL_free(s); }
    else saveDir = baseDir;

    // 4x MSAA if the driver has it, then 2x, then none
    int tries[3] = {msaaWanted, msaaWanted > 2 ? 2 : 0, 0};
    bool ok = false;
    for (int i = 0; i < 3 && !ok; i++) { ok = makeWindow(tries[i]); if (ok) msaaWanted = tries[i]; }
    if (!ok) {
        std::string m = std::string("Couldn't start OpenGL 2.1:\n") + SDL_GetError() + "\n\nPlease update your graphics driver.";
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Endurance Motor Racing 2.0", m.c_str(), nullptr);
        return false;
    }
    if (!windowed) SDL_ShowCursor(SDL_DISABLE);
    loadGlProcs();
    log("OpenGL %s on %s, MSAA %dx, buffers %s\n", (const char*)glGetString(GL_VERSION), (const char*)glGetString(GL_RENDERER),
        msaaWanted, haveBuffers ? "yes" : "no");
    if (strstr((const char*)glGetString(GL_EXTENSIONS), "GL_EXT_texture_filter_anisotropic")) {
        float maxA = 1; glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxA);
        anisotropy = maxA < 16 ? maxA : 16;
    }
    layoutViewport();
    gfx::applySync();
    if (fpsCap < 0) {
        SDL_DisplayMode dm;
        int hz = SDL_GetCurrentDisplayMode(SDL_GetWindowDisplayIndex(window), &dm) == 0 ? dm.refresh_rate : 60;
        fpsCap = hz > 65 ? 60 : 0;
        log("screen %d Hz, frame cap %d\n", hz, fpsCap);
    }
    arena = (u8*)malloc(ARENA_SIZE);

    if (msaaWanted > 0) glEnable(GL_MULTISAMPLE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_CULL_FACE);          // the AI models are not consistently wound
    glShadeModel(GL_SMOOTH);
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, 8.0f / 255.0f);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
    glHint(GL_FOG_HINT, GL_NICEST);
    buildLighting();

    for (int i = 0; i < SDL_NumJoysticks(); i++) input::openPad(i);
    return true;
}
void shutdown() {
    for (SDL_GameController* c : input::pads) SDL_GameControllerClose(c);
    input::pads.clear();
    if (glctx) SDL_GL_DeleteContext(glctx);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
}
bool running() { return runningFlag; }
void quit() { runningFlag = false; }
int memUsedKB() {
#if defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 33))
    struct mallinfo2 mi = mallinfo2(); return (int)(mi.uordblks / 1024);
#else
    return 0;
#endif
}
double timeSec() { return (double)(SDL_GetPerformanceCounter() - t0) / (double)SDL_GetPerformanceFrequency(); }
static size_t blockSize(void* p) {
#ifdef _WIN32
    return _msize(p);
#else
    return malloc_usable_size(p);
#endif
}
void* loadFile(const char* path, int* size) {
    FILE* f = fopen(path, "rb");    // wrapped: DATA/ -> HD/ then DATA/ next to the exe
    if (!f) return nullptr;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    void* p = malloc(n > 0 ? n : 1);   // malloc is 16-byte aligned on 64-bit Windows and Linux
    if (!p) { fclose(f); return nullptr; }
    if (fread(p, 1, n, f) != (size_t)n) { fclose(f); free(p); return nullptr; }
    fclose(f);
    // 'FLZ1' u32 size, then a zlib stream (tools/pack_files.py)
    if (n >= 8 && !memcmp(p, "FLZ1", 4)) {
        uint32_t raw; memcpy(&raw, (char*)p + 4, 4);
        void* q = malloc(raw > 0 ? raw : 1);
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
    gfx::flushCache(p, (int)blockSize(p));
    free(p);
}
void log(const char* fmt, ...) {
    va_list a;
    va_start(a, fmt);
    FILE* f = fopen("log.txt", "a");
    if (f) { vfprintf(f, fmt, a); fclose(f); }
    va_end(a);
#ifndef _WIN32
    va_start(a, fmt); vfprintf(stderr, fmt, a); va_end(a);
#endif
}
}

// ---------------------------------------------------------------- main
// (EMR_HEADLESS: the offscreen test build, Makefile.headless, where tests/pc_main.cpp drives the game)
#ifndef EMR_HEADLESS
static Game game;
int main(int argc, char** argv) {
    sys::parseArgs(argc, argv);
    if (!sys::init()) return 1;
    remove("log.txt");
    if (!game.init()) {
        sys::log("DATA folder missing or incomplete\n");
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Endurance Motor Racing 2.0",
            "The DATA folder is missing or incomplete.\nKeep DATA and HD next to EnduranceMotorRacing2.exe.", window);
    }
    double last = sys::timeSec();
    while (sys::running()) {
        double now = sys::timeSec();
        float dt = (float)(now - last);
        if (dt > 0.25f) dt = 0.25f;     // a dragged window or a long load mustn't jump the race ahead
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
#endif
