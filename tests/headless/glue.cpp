// Offscreen test build (Makefile.headless): what tests/pc_main.cpp expects of its platform (the
// software renderer's pc:: names), on top of the real PC layer (src/platform_desktop.cpp) drawing
// with OpenGL into an offscreen picture. saveShot reads that picture back.
#include <SDL.h>
#include <SDL_opengl.h>
#include <cstdio>
#include <vector>
#include <cstdlib>
extern "C" unsigned glGetError(void);
#include "platform.h"

namespace pc {
float fakeWait = 0;
Pad pad;
long trisDrawn = 0, drawCalls = 0, vertsSent = 0;
// the game's mixer, as the PC layer gave it to the (stand-in) sound device
static void mixThroughDevice(int16_t* out, int frames) {
    if (sdlshim::audioCb()) sdlshim::audioCb()(sdlshim::audioUser(), (Uint8*)out, frames * 4);
    else for (int i = 0; i < frames * 2; i++) out[i] = 0;
}
audio::MixFn mixFn = mixThroughDevice;
void saveShot(const char* path) {
    int w = sdlshim::win().w, h = sdlshim::win().h;
    std::vector<unsigned char> px((size_t)w * h * 3);
    glFinish();
    if (getenv("EMR_GLDEBUG")) {

        GLint db = 0, rb = 0, vp[4] = {0, 0, 0, 0}; glGetIntegerv(0x0C01, &db); glGetIntegerv(0x0C02, &rb); glGetIntegerv(0x0BA2, vp);
        fprintf(stderr, "gl error %x, draw buffer %x, read buffer %x, viewport %d %d %d %d\n", glGetError(), db, rb, vp[0], vp[1], vp[2], vp[3]);
    }
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(0x0405);                 // the back buffer: the game drew there (no real swap here)
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px.data());
    FILE* f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = h - 1; y >= 0; y--) fwrite(&px[(size_t)y * w * 3], 1, (size_t)w * 3, f);   // (OpenGL's rows start at the bottom)
    fclose(f);
}
}
