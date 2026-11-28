#pragma once
// Menu navigation and on-foot movement in one place: the d-pad and the left stick do the same thing
// everywhere. nav::Repeat turns them into menu steps (a step on the press, then repeats while held);
// nav::move gives the combined movement vector for walking.
#include "platform.h"
#include <cmath>

namespace nav {

const float STICK_ON = 0.55f;      // the stick counts as pushed past this ...
const float STICK_OFF = 0.35f;     // ... and as released below this (hysteresis: no flicker at the edge)
const float REPEAT_DELAY = 0.40f;  // held: the first repeat after this long
const float REPEAT_RATE = 0.11f;   // then one step every this long

// the movement vector (x right, y down, as on the stick), length 0..1: the d-pad when it's pressed
// (it wins, diagonals normalised), otherwise the left stick (its own dead zone applied by the platform)
inline void move(const Pad& p, float& x, float& y) {
    int dx = (p.right ? 1 : 0) - (p.left ? 1 : 0), dy = (p.down ? 1 : 0) - (p.up ? 1 : 0);
    if (dx || dy) {
        float n = (dx && dy) ? 0.70710678f : 1.0f;
        x = dx * n; y = dy * n;
        return;
    }
    x = p.stickX; y = p.stickY;
    float l = sqrtf(x * x + y * y);
    if (l > 1) { x /= l; y /= l; }
}

// menu steps from the d-pad or the left stick: -1 / 0 / +1 on each axis this frame
struct Repeat {
    int dirX = 0, dirY = 0;          // the direction held now on each axis
    int stickX = 0, stickY = 0;      // the stick's own (hysteresis) state
    float tX = 0, tY = 0;            // how long that direction has been held

    static int stickDir(float v, int cur) {
        if (cur != 0 && v * cur > STICK_OFF) return cur;          // still held the same way
        if (v > STICK_ON) return 1;
        if (v < -STICK_ON) return -1;
        return 0;
    }
    static int axis(int d, int& held, float& t, float dt) {
        if (d == 0) { held = 0; t = 0; return 0; }
        if (d != held) { held = d; t = 0; return d; }             // a new press: one step now
        t += dt;
        if (t >= REPEAT_DELAY) { t -= REPEAT_RATE; return d; }    // held: repeat
        return 0;
    }
    void update(const Pad& p, float dt, int& dx, int& dy) {
        int sx = stickDir(p.stickX, stickX), sy = stickDir(p.stickY, stickY);
        // the stick: only its stronger axis, so a slightly diagonal push doesn't step both ways
        if (sx && sy) { if (fabsf(p.stickX) >= fabsf(p.stickY)) sy = 0; else sx = 0; }
        stickX = sx; stickY = sy;
        int px = (p.right ? 1 : 0) - (p.left ? 1 : 0), py = (p.down ? 1 : 0) - (p.up ? 1 : 0);
        dx = axis(px ? px : sx, dirX, tX, dt);
        dy = axis(py ? py : sy, dirY, tY, dt);
    }
    void reset() { dirX = dirY = stickX = stickY = 0; tX = tY = 0; }
};

}
