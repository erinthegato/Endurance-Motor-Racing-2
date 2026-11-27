// Offscreen test build only (tests/headless, Makefile.headless): just enough of SDL2 for
// src/platform_desktop.cpp, with an offscreen OpenGL context from EGL (Mesa's llvmpipe) instead of a
// window. No input, no sound device: the test program drives the game and takes screenshots of the
// real PC renderer (shaders, lighting) with glReadPixels.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <unistd.h>
#include <sys/stat.h>

typedef uint8_t Uint8; typedef uint16_t Uint16; typedef uint32_t Uint32; typedef uint64_t Uint64; typedef int16_t Sint16; typedef int32_t Sint32;
#define SDLCALL
#define SDL_INIT_VIDEO 0x20u
#define SDL_INIT_AUDIO 0x10u
#define SDL_INIT_GAMECONTROLLER 0x2000u
#define SDL_INIT_EVENTS 0x4000u
#define SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS "SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS"
#define SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS "SDL_VIDEO_MINIMIZE_ON_FOCUS_LOSS"
#define SDL_MESSAGEBOX_ERROR 0x10
#define SDL_WINDOW_FULLSCREEN 0x1u
#define SDL_WINDOW_OPENGL 0x2u
#define SDL_WINDOW_RESIZABLE 0x20u
#define SDL_WINDOW_ALLOW_HIGHDPI 0x2000u
#define SDL_WINDOW_FULLSCREEN_DESKTOP (SDL_WINDOW_FULLSCREEN | 0x1000u)
#define SDL_WINDOWPOS_CENTERED 0x2FFF0000
#define SDL_ENABLE 1
#define SDL_DISABLE 0
#define AUDIO_S16SYS 0x8010
enum { SDL_QUIT = 0x100, SDL_WINDOWEVENT = 0x200, SDL_KEYDOWN = 0x300, SDL_CONTROLLERDEVICEADDED = 0x653, SDL_CONTROLLERDEVICEREMOVED = 0x654 };
enum { SDL_WINDOWEVENT_RESIZED = 5, SDL_WINDOWEVENT_SIZE_CHANGED = 6 };
enum { SDLK_RETURN = '\r' };
enum { KMOD_ALT = 0x0300 };
enum SDL_GLattr { SDL_GL_DOUBLEBUFFER = 5, SDL_GL_DEPTH_SIZE = 6, SDL_GL_MULTISAMPLEBUFFERS = 13, SDL_GL_MULTISAMPLESAMPLES = 14,
                  SDL_GL_CONTEXT_MAJOR_VERSION = 17, SDL_GL_CONTEXT_MINOR_VERSION = 18 };
enum SDL_Scancode { SDL_SCANCODE_E = 8, SDL_SCANCODE_H = 11, SDL_SCANCODE_Q = 20, SDL_SCANCODE_S = 22, SDL_SCANCODE_W = 26, SDL_SCANCODE_RETURN = 40,
                    SDL_SCANCODE_ESCAPE = 41, SDL_SCANCODE_BACKSPACE = 42, SDL_SCANCODE_TAB = 43, SDL_SCANCODE_SPACE = 44,
                    SDL_SCANCODE_RIGHT = 79, SDL_SCANCODE_LEFT = 80, SDL_SCANCODE_DOWN = 81, SDL_SCANCODE_UP = 82,
                    SDL_SCANCODE_KP_ENTER = 88, SDL_SCANCODE_LALT = 226, SDL_SCANCODE_RALT = 230 };
enum SDL_GameControllerButton { SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_Y,
    SDL_CONTROLLER_BUTTON_BACK, SDL_CONTROLLER_BUTTON_GUIDE, SDL_CONTROLLER_BUTTON_START, SDL_CONTROLLER_BUTTON_LEFTSTICK,
    SDL_CONTROLLER_BUTTON_RIGHTSTICK, SDL_CONTROLLER_BUTTON_LEFTSHOULDER, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
    SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_DOWN, SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT };
enum SDL_GameControllerAxis { SDL_CONTROLLER_AXIS_LEFTX, SDL_CONTROLLER_AXIS_LEFTY, SDL_CONTROLLER_AXIS_RIGHTX, SDL_CONTROLLER_AXIS_RIGHTY,
    SDL_CONTROLLER_AXIS_TRIGGERLEFT, SDL_CONTROLLER_AXIS_TRIGGERRIGHT };
typedef int32_t SDL_JoystickID;
typedef Uint32 SDL_AudioDeviceID;
struct SDL_Window { int w, h; };
struct SDL_GameController;
struct SDL_Joystick;
typedef void* SDL_GLContext;
typedef void (SDLCALL* SDL_AudioCallback)(void*, Uint8*, int);
struct SDL_AudioSpec { int freq; Uint16 format; Uint8 channels; Uint8 silence; Uint16 samples; Uint16 padding; Uint32 size; SDL_AudioCallback callback; void* userdata; };
struct SDL_DisplayMode { Uint32 format; int w, h, refresh_rate; void* driverdata; };
struct SDL_Keysym { int scancode; int sym; Uint16 mod; Uint32 unused; };
struct SDL_KeyboardEvent { Uint32 type, timestamp, windowID; Uint8 state, repeat, padding2, padding3; SDL_Keysym keysym; };
struct SDL_WindowEvent { Uint32 type, timestamp, windowID; Uint8 event, p1, p2, p3; Sint32 data1, data2; };
struct SDL_ControllerDeviceEvent { Uint32 type, timestamp; Sint32 which; };
union SDL_Event { Uint32 type; SDL_KeyboardEvent key; SDL_WindowEvent window; SDL_ControllerDeviceEvent cdevice; Uint8 pad[56]; };

// ---- EGL (Mesa), declared here: the sandbox has the library but not its headers
extern "C" {
typedef void* EGLDisplay; typedef void* EGLConfig; typedef void* EGLSurface; typedef void* EGLContext; typedef int32_t EGLint;
typedef unsigned EGLBoolean; typedef unsigned EGLenum;
EGLDisplay eglGetDisplay(void*); EGLBoolean eglInitialize(EGLDisplay, EGLint*, EGLint*);
EGLBoolean eglChooseConfig(EGLDisplay, const EGLint*, EGLConfig*, EGLint, EGLint*);
EGLSurface eglCreatePbufferSurface(EGLDisplay, EGLConfig, const EGLint*); EGLBoolean eglBindAPI(EGLenum);
EGLContext eglCreateContext(EGLDisplay, EGLConfig, EGLContext, const EGLint*);
EGLBoolean eglMakeCurrent(EGLDisplay, EGLSurface, EGLSurface, EGLContext); void* eglGetProcAddress(const char*); EGLint eglGetError(void);
}

namespace sdlshim {
inline EGLDisplay& dpy() { static EGLDisplay d = nullptr; return d; }
inline SDL_Window& win() { static SDL_Window w{1280, 720}; return w; }
inline int& samples() { static int s = 0; return s; }
inline SDL_AudioCallback& audioCb() { static SDL_AudioCallback c = nullptr; return c; }
inline void*& audioUser() { static void* u = nullptr; return u; }
inline std::string& err() { static std::string e; return e; }
}

inline int SDL_Init(Uint32) { setenv("EGL_PLATFORM", "surfaceless", 0); return 0; }
inline void SDL_Quit() {}
inline int SDL_SetHint(const char*, const char*) { return 1; }
inline const char* SDL_GetError() { return sdlshim::err().c_str(); }
inline int SDL_ShowSimpleMessageBox(Uint32, const char* t, const char* m, SDL_Window*) { fprintf(stderr, "[%s] %s\n", t, m); return 0; }
inline void SDL_free(void* p) { free(p); }
inline char* SDL_GetBasePath() { return strdup("./"); }
inline char* SDL_GetPrefPath(const char*, const char*) { mkdir("/tmp/emr_headless_save", 0755); return strdup("/tmp/emr_headless_save/"); }
inline Uint64 SDL_GetPerformanceCounter() { timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (Uint64)t.tv_sec * 1000000000ull + t.tv_nsec; }
inline Uint64 SDL_GetPerformanceFrequency() { return 1000000000ull; }
inline void SDL_Delay(Uint32 ms) { usleep(ms * 1000); }
// (multisampling is left out here: Mesa's software renderer reads a multisampled picture back wrongly)
inline int SDL_GL_SetAttribute(SDL_GLattr, int) { return 0; }
inline SDL_Window* SDL_CreateWindow(const char*, int, int, int, int, Uint32) {
    // EMR_W / EMR_H: the picture's size (default 1280 x 720)
    SDL_Window& w = sdlshim::win();
    if (const char* s = getenv("EMR_W")) w.w = atoi(s);
    if (const char* s = getenv("EMR_H")) w.h = atoi(s);
    return &w;
}
inline void SDL_DestroyWindow(SDL_Window*) {}
inline SDL_GLContext SDL_GL_CreateContext(SDL_Window* w) {
    EGLDisplay d = eglGetDisplay(nullptr); EGLint ma, mi;
    if (!eglInitialize(d, &ma, &mi)) { sdlshim::err() = "eglInitialize"; return nullptr; }
    int s = sdlshim::samples();
    EGLint at[] = {0x3033, 0x0001, 0x3040, 0x0008, 0x3024, 8, 0x3023, 8, 0x3022, 8, 0x3021, 8, 0x3025, 24,
                   0x3032, s > 0 ? 1 : 0, 0x3031, s, 0x3038};
    EGLConfig c; EGLint n = 0;
    if (!eglChooseConfig(d, at, &c, 1, &n) || !n) { sdlshim::err() = "no EGL config"; return nullptr; }
    EGLint pa[] = {0x3057, w->w, 0x3056, w->h, 0x3038};
    EGLSurface sf = eglCreatePbufferSurface(d, c, pa);
    eglBindAPI(0x30A2);
    EGLContext x = eglCreateContext(d, c, nullptr, nullptr);
    if (!x || !eglMakeCurrent(d, sf, sf, x)) { sdlshim::err() = "eglMakeCurrent"; return nullptr; }
    sdlshim::dpy() = d;
    return x;
}
inline void SDL_GL_DeleteContext(SDL_GLContext) {}
inline void* SDL_GL_GetProcAddress(const char* n) { return eglGetProcAddress(n); }
inline int SDL_GL_SetSwapInterval(int) { return 0; }
inline void SDL_GL_SwapWindow(SDL_Window*) {}
inline void SDL_GL_GetDrawableSize(SDL_Window* w, int* x, int* y) { *x = w->w; *y = w->h; }
inline Uint32 SDL_GetWindowFlags(SDL_Window*) { return 0; }
inline int SDL_SetWindowFullscreen(SDL_Window*, Uint32) { return 0; }
inline int SDL_ShowCursor(int) { return 0; }
inline int SDL_GetWindowDisplayIndex(SDL_Window*) { return 0; }
inline int SDL_GetCurrentDisplayMode(int, SDL_DisplayMode* m) { m->w = 1280; m->h = 720; m->refresh_rate = 60; return 0; }
inline int SDL_PollEvent(SDL_Event*) { return 0; }
inline const Uint8* SDL_GetKeyboardState(int*) { static Uint8 k[512] = {}; return k; }
inline int SDL_NumJoysticks() { return 0; }
inline bool SDL_IsGameController(int) { return false; }
inline SDL_GameController* SDL_GameControllerOpen(int) { return nullptr; }
inline void SDL_GameControllerClose(SDL_GameController*) {}
inline const char* SDL_GameControllerName(SDL_GameController*) { return "none"; }
inline SDL_Joystick* SDL_GameControllerGetJoystick(SDL_GameController*) { return nullptr; }
inline SDL_JoystickID SDL_JoystickInstanceID(SDL_Joystick*) { return -1; }
inline Uint8 SDL_GameControllerGetButton(SDL_GameController*, SDL_GameControllerButton) { return 0; }
inline Sint16 SDL_GameControllerGetAxis(SDL_GameController*, SDL_GameControllerAxis) { return 0; }
inline SDL_AudioDeviceID SDL_OpenAudioDevice(const char*, int, const SDL_AudioSpec* want, SDL_AudioSpec* got, int) {
    if (got) *got = *want;
    sdlshim::audioCb() = want->callback; sdlshim::audioUser() = want->userdata;
    return 1;
}
inline void SDL_PauseAudioDevice(SDL_AudioDeviceID, int) {}
inline void SDL_CloseAudioDevice(SDL_AudioDeviceID) { sdlshim::audioCb() = nullptr; }
inline void SDL_LockAudioDevice(SDL_AudioDeviceID) {}
inline void SDL_UnlockAudioDevice(SDL_AudioDeviceID) {}
