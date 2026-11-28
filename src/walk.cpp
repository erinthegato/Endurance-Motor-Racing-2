// On foot: the walker (movement, collisions, the third-person camera, the person drawn in team kit) and
// the three places walked round -- the main menu garage, the headquarters office and the pits at the
// track. See walk.h.
#include "game.h"
#include "clip.h"
#include <cstdio>
#include <cstring>
#include <cmath>

static const u32 W_WHITE = RGBA(255, 255, 255), W_DIM = RGBA(175, 185, 200), W_ACC = RGBA(255, 150, 40), W_GOLD = RGBA(255, 215, 70);
static const float PI_ = 3.14159265f;
static float wrapA(float a) { while (a > PI_) a -= 2 * PI_; while (a < -PI_) a += 2 * PI_; return a; }
static float clampW(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }

// ------------------------------------------------------------------ the walker
void Walker::clear() {
    boxes.clear(); roofs.clear(); obbs.clear(); posts.clear(); stations.clear();
    near = -1; ceiling = 99; boundsAreWalls = true;
}

void Walker::place(float px, float py, float facing) {
    x = px; y = py; z = floorAt(px, py); yaw = facing; vx = vy = 0; run = false; stillT = 0;
    camYaw = facing; camPitch = 0.30f; camNow = camDist; camIdle = 0; camInit = true; phase = 0;
}

float Walker::floorAt(float px, float py) const {
    float f = 0;
    for (const WalkBox& b : boxes)
        if (b.h <= STEP_H && px >= b.x0 && px <= b.x1 && py >= b.y0 && py <= b.y1 && b.h > f) f = b.h;
    return f;
}

// push the person (a circle) out of whatever it stands in
void Walker::collide() {
    auto pushBox = [&](float lx, float ly, float hx, float hy, float& ox, float& oy) {
        // lx, ly: the person relative to a box centred on 0 with half sizes hx, hy; returns the push
        float cx = clampW(lx, -hx, hx), cy = clampW(ly, -hy, hy);
        float dx = lx - cx, dy = ly - cy, d2 = dx * dx + dy * dy;
        ox = oy = 0;
        if (d2 >= RADIUS * RADIUS) return false;
        if (d2 > 1e-8f) { float d = sqrtf(d2), k = (RADIUS - d) / d; ox = dx * k; oy = dy * k; return true; }
        // the centre inside: out by the nearest side
        float px = hx - fabsf(lx) + RADIUS, py = hy - fabsf(ly) + RADIUS;
        if (px < py) ox = lx >= 0 ? px : -px; else oy = ly >= 0 ? py : -py;
        return true;
    };
    for (int it = 0; it < 3; it++) {
        float ox, oy;
        for (const WalkBox& b : boxes) {
            if (b.h <= STEP_H) continue;                       // stepped onto, not walked into
            float hx = (b.x1 - b.x0) * 0.5f, hy = (b.y1 - b.y0) * 0.5f;
            if (pushBox(x - (b.x0 + hx), y - (b.y0 + hy), hx, hy, ox, oy)) { x += ox; y += oy; }
        }
        for (const WalkObb& o : obbs) {
            float c = cosf(o.ang), s = sinf(o.ang);
            float lx = (x - o.cx) * c + (y - o.cy) * s, ly = -(x - o.cx) * s + (y - o.cy) * c;
            if (pushBox(lx, ly, o.hx, o.hy, ox, oy)) { x += ox * c - oy * s; y += ox * s + oy * c; }
        }
        for (const WalkPost& p : posts) {
            float dx = x - p.x, dy = y - p.y, d2 = dx * dx + dy * dy, r = p.r + RADIUS;
            if (d2 < r * r) {
                float d = sqrtf(d2);
                if (d < 1e-4f) { dx = 1; dy = 0; d = 1; }
                x = p.x + dx / d * r; y = p.y + dy / d * r;
            }
        }
        x = clampW(x, minX + RADIUS, maxX - RADIUS);
        y = clampW(y, minY + RADIUS, maxY - RADIUS);
    }
}

// how far (0..1) from the target to the eye before a wall: tall boxes, and indoors the room's walls
float Walker::camRay(float tx, float ty, float ex, float ey) const {
    float best = 1, dx = ex - tx, dy = ey - ty;
    const float M = 0.18f;
    for (const WalkBox& b : boxes) {
        if (b.h < CAM_H) continue;
        float x0 = b.x0 - M, x1 = b.x1 + M, y0 = b.y0 - M, y1 = b.y1 + M;
        if (tx > x0 && tx < x1 && ty > y0 && ty < y1) continue;      // (the person is inside it: ignore)
        float t0 = 0, t1 = 1;
        auto slab = [&](float o, float d, float lo, float hi) {
            if (fabsf(d) < 1e-6f) return o >= lo && o <= hi;
            float a = (lo - o) / d, c = (hi - o) / d;
            if (a > c) { float s = a; a = c; c = s; }
            if (a > t0) t0 = a;
            if (c < t1) t1 = c;
            return t0 <= t1;
        };
        if (slab(tx, dx, x0, x1) && slab(ty, dy, y0, y1) && t0 < best) best = t0;
    }
    if (boundsAreWalls) {
        auto edge = [&](float o, float d, float lo, float hi) {
            if (d > 1e-6f && o + d > hi) best = fminf(best, (hi - o) / d);
            if (d < -1e-6f && o + d < lo) best = fminf(best, (lo - o) / d);
        };
        edge(tx, dx, minX + M, maxX - M);
        edge(ty, dy, minY + M, maxY - M);
    }
    return best < 0 ? 0 : best;
}

bool Walker::update(float dt, const Pad& p, const Pad& prev) {
    t += dt;
    if (!camInit) { camYaw = yaw; camInit = true; }
    // the pad: the left stick (analogue: a light push walks slowly), else the d-pad
    float sx = 0, sy = 0;
    float sm = sqrtf(p.stickX * p.stickX + p.stickY * p.stickY);
    if (sm > 0.22f) {
        float k = clampW((sm - 0.22f) / (0.88f - 0.22f), 0, 1) / sm;
        sx = p.stickX * k; sy = p.stickY * k;
    } else {
        sx = (float)((p.right ? 1 : 0) - (p.left ? 1 : 0));
        sy = (float)((p.down ? 1 : 0) - (p.up ? 1 : 0));
        float m = sqrtf(sx * sx + sy * sy);
        if (m > 1) { sx /= m; sy /= m; }
    }
    moving = sx * sx + sy * sy > 0.0025f;
    if (p.l && !prev.l) run = !run;                       // L: run / walk
    if (moving) stillT = 0; else if ((stillT += dt) > 0.8f) run = false;   // (stopping drops back to a walk)
    // the right stick turns the camera round the person and up / down
    float rx = fabsf(p.rstickX) > 0.15f ? p.rstickX : 0, ry = fabsf(p.rstickY) > 0.15f ? p.rstickY : 0;
    camYaw = wrapA(camYaw - rx * 2.6f * dt);
    camPitch = clampW(camPitch + ry * 1.5f * dt, -0.10f, 1.05f);
    if (rx != 0 || ry != 0) camIdle = 0; else camIdle += dt;
    // where the pad points, seen from the camera
    float fx = cosf(camYaw), fy = sinf(camYaw), rgx = sinf(camYaw), rgy = -cosf(camYaw);
    float wx = fx * -sy + rgx * sx, wy = fy * -sy + rgy * sx;
    float top = run ? RUN : WALK;
    float acc = moving ? (run ? 7.5f : 5.0f) : 9.0f;
    float dvx = wx * top - vx, dvy = wy * top - vy, dl = sqrtf(dvx * dvx + dvy * dvy), mx = acc * dt;
    if (dl > mx) { dvx *= mx / dl; dvy *= mx / dl; }
    vx += dvx; vy += dvy;
    float ox = x, oy = y;
    x += vx * dt; y += vy * dt;
    collide();
    // walking into something: lose the speed into it (so the stride doesn't run on the spot)
    if (dt > 0) {
        float ax = (x - ox) / dt, ay = (y - oy) / dt;
        if (ax * ax + ay * ay < vx * vx + vy * vy) { vx = ax; vy = ay; }
    }
    float sp = sqrtf(vx * vx + vy * vy);
    if (sp > 0.15f) yaw = wrapA(yaw + clampW(wrapA(atan2f(vy, vx) - yaw), -10 * dt, 10 * dt));
    // the stride: a walk ~0.9 cycles a second, a run ~1.4 (longer steps)
    float cycle = sp <= WALK ? 1.55f : 1.55f + (sp - WALK) * 0.57f;
    if (sp > 0.05f) phase += sp / cycle * 2 * PI_ * dt;
    else { float rest = roundf(phase / PI_) * PI_; phase += (rest - phase) * fminf(1, dt * 8); }
    float w = clampW(sp / WALK, 0, 1), r = clampW((sp - WALK) / (RUN - WALK), 0, 1);
    bob = (0.022f * w + 0.045f * r) * (0.5f - fabsf(sinf(phase)));
    z += (floorAt(x, y) - z) * fminf(1, dt * 12);
    // the camera swings in behind when walking on without turning it (not when coming towards it)
    if (camIdle > 1.2f && sp > 0.6f) {
        float d = wrapA(yaw - camYaw);
        if (fabsf(d) < 2.2f) camYaw = wrapA(camYaw + d * fminf(1, dt * 0.9f * fminf(sp / WALK, 2.0f)));
    }
    // the camera's distance after the walls: pulled in at once, let out gently
    {
        float cp = cosf(camPitch);
        float ex = x - cosf(camYaw) * cp * camDist, ey = y - sinf(camYaw) * cp * camDist;
        float f = camRay(x, y, ex, ey);
        float allowed = fmaxf(0.5f, camDist * f - 0.15f);
        camNow = allowed < camNow ? allowed : camNow + (allowed - camNow) * fminf(1, dt * 2.5f);
    }
    // the station in reach: the nearest
    near = -1; float bd = 1e9f;
    for (int i = 0; i < (int)stations.size(); i++) {
        const WalkStation& s = stations[i];
        float d2 = (s.x - x) * (s.x - x) + (s.y - y) * (s.y - y);
        if (d2 < s.r * s.r && d2 < bd) { bd = d2; near = i; }
    }
    return near >= 0 && p.cross && !prev.cross;
}

void Walker::camera(V3& eye, V3& target) const {
    target = V3(x, y, z + 1.45f + bob * 0.3f);
    float cp = cosf(camPitch), spch = sinf(camPitch);
    eye = target - V3(cosf(camYaw) * cp, sinf(camYaw) * cp, -spch) * camNow;
    // under the ceiling (indoors, the garages and the pit wall's canopy), above the floor
    float top = ceiling;
    for (const WalkBox& r : roofs) if (eye.x >= r.x0 && eye.x <= r.x1 && eye.y >= r.y0 && eye.y <= r.y1 && r.h < top) top = r.h;
    eye.z = clampW(eye.z, z + 0.35f, top - 0.22f);
    target.z = fminf(target.z, top - 0.15f);
}

// the person: boxes for boots, legs, hips, body, arms and gloves, the helmet with its visor; the legs
// and arms swing with the stride (more, and the knees and elbows bend more, running)
void Walker::draw(Renderer& R, const M4& place, u32 suit, u32 trim) const {
    float sp = sqrtf(vx * vx + vy * vy);
    float w = clampW(sp / WALK, 0, 1), r = clampW((sp - WALK) / (RUN - WALK), 0, 1);
    float legA = 0.40f * w + 0.42f * r;
    float s = sinf(phase), c = cosf(phase);
    float lean = 0.04f * w + 0.20f * r;
    float breathe = sp < 0.1f ? 0.004f * sinf(t * 2.1f) : 0;
    u32 boot = RGBA(26, 26, 30), glove = RGBA(34, 34, 38), visor = RGBA(16, 20, 30);
    const float HIP = 0.92f + bob, TH = 0.44f, SH = 0.44f;
    M4 base = place * M4::translate(V3(0, 0, z * 0));    // (the floor height is already in `place`)
    // legs: thigh from the hip, shin from the knee, the boot at the foot
    for (int side = -1; side <= 1; side += 2) {
        float ph = side < 0 ? 1.0f : -1.0f;               // left leg forward when sin > 0
        float thigh = ph * legA * s;
        float swingC = ph * c;                             // > 0 while this leg swings forward
        float knee = (0.10f + 0.30f * w + 1.10f * r) * fmaxf(0.0f, swingC) + 0.05f * w;
        M4 hip = base * M4::translate(V3(0, side * 0.105f, HIP)) * M4::rotY(-thigh);
        R.box3D(hip, V3(-0.075f, -0.07f, -TH), V3(0.075f, 0.07f, 0.02f), suit);
        R.box3D(hip, V3(0.074f, -0.02f, -TH * 0.8f), V3(0.078f, 0.02f, -0.05f), trim);      // the stripe down the leg
        M4 kn = hip * M4::translate(V3(0, 0, -TH)) * M4::rotY(knee);
        R.box3D(kn, V3(-0.065f, -0.062f, -SH), V3(0.065f, 0.062f, 0.0f), suit);
        M4 ft = kn * M4::translate(V3(0, 0, -SH)) * M4::rotY(-knee * 0.5f + thigh * 0.2f);
        R.box3D(ft, V3(-0.09f, -0.06f, -0.075f), V3(0.17f, 0.06f, 0.01f), boot);
    }
    // body: hips, belt, chest leaning into the run
    M4 body = base * M4::translate(V3(0, 0, HIP)) * M4::rotY(lean);
    R.box3D(body, V3(-0.11f, -0.18f, -0.04f), V3(0.11f, 0.18f, 0.10f), suit);
    R.box3D(body, V3(-0.115f, -0.185f, 0.10f), V3(0.115f, 0.185f, 0.15f), trim);              // belt
    R.box3D(body, V3(-0.12f, -0.21f, 0.15f + breathe), V3(0.13f, 0.21f, 0.56f + breathe), suit);
    R.box3D(body, V3(0.13f, -0.06f, 0.20f), V3(0.135f, 0.06f, 0.50f), trim);                  // the chest panel
    // arms: opposite to the legs, elbows bent (a lot when running)
    for (int side = -1; side <= 1; side += 2) {
        float ph = side < 0 ? -1.0f : 1.0f;
        float arm = ph * legA * 0.85f * s;
        float elbow = 0.20f + 0.25f * w + 1.15f * r;
        M4 sh = body * M4::translate(V3(0, side * 0.255f, 0.52f + breathe)) * M4::rotY(-arm) * M4::rotX(side * 0.08f);
        R.box3D(sh, V3(-0.055f, -0.05f, -0.30f), V3(0.055f, 0.05f, 0.04f), suit);
        R.box3D(sh, V3(-0.056f, -0.051f, -0.06f), V3(0.056f, 0.051f, 0.04f), trim);          // shoulder flash
        M4 el = sh * M4::translate(V3(0, 0, -0.30f)) * M4::rotY(-elbow);
        R.box3D(el, V3(-0.048f, -0.045f, -0.25f), V3(0.048f, 0.045f, 0.0f), suit);
        R.box3D(el, V3(-0.05f, -0.047f, -0.34f), V3(0.05f, 0.047f, -0.25f), glove);
    }
    // neck and helmet
    M4 head = body * M4::translate(V3(0.01f, 0, 0.60f + breathe));
    R.box3D(head, V3(-0.05f, -0.05f, -0.04f), V3(0.05f, 0.05f, 0.04f), suit);
    R.box3D(head, V3(-0.15f, -0.13f, 0.02f), V3(0.14f, 0.13f, 0.30f), trim);
    R.box3D(head, V3(-0.151f, -0.03f, 0.18f), V3(0.141f, 0.03f, 0.305f), suit);               // the stripe over the top
    R.box3D(head, V3(0.10f, -0.105f, 0.11f), V3(0.147f, 0.105f, 0.19f), visor);
}

void Walker::marker(Renderer& R, const M4& place, float t, bool active) {
    float k = active ? 0.11f : 0.08f;
    float hover = sinf(t * 2.2f) * 0.06f;
    M4 m = place * M4::translate(V3(0, 0, hover)) * M4::rotZ(t * 1.6f) * M4::rotX(0.6155f) * M4::rotY(0.7854f);
    R.box3D(m, V3(-k, -k, -k), V3(k, k, k), active ? W_GOLD : W_ACC);
}

void walkHud(Renderer& R, const char* prompt, const char* help, const char* info) {
    gfx::setDepthTest(false); gfx::setBlend(true); gfx::setFog(false, 0, 1, 0);
    if (info && *info) {
        float w = R.font.width(info, 0.72f) + 14;
        gfx::drawFill2D(6, 6, w, 16, RGBA(0, 0, 0, 150));
        R.font.draw(13, 7, info, W_WHITE, 0.72f);
    }
    if (prompt && *prompt) {
        char b[96]; snprintf(b, sizeof b, "X   %s", prompt);
        float w = R.font.width(b, 0.95f) + 28;
        gfx::drawFill2D(SCREEN_W / 2 - w / 2, SCREEN_H - 58, w, 22, RGBA(0, 0, 0, 170));
        gfx::drawFill2D(SCREEN_W / 2 - w / 2, SCREEN_H - 58, w, 2, W_ACC);
        R.font.drawCentre(SCREEN_W / 2, SCREEN_H - 55, b, W_ACC, 0.95f);
    }
    if (help && *help) {
        gfx::drawFill2D(0, SCREEN_H - 18, SCREEN_W, 18, RGBA(0, 0, 0, 140));
        R.font.drawCentre(SCREEN_W / 2, SCREEN_H - 15, help, W_DIM, 0.6f);
    }
    gfx::setBlend(false); gfx::setDepthTest(true);
}

// a label floating over a station, turned to the camera
static void label3D(Renderer& R, const V3& at, const V3& eye, const V3& fwd, const char* s, bool active) {
    M4 face = M4::translate(at - eye) * M4::basis(R.camRight, R.camUp, fwd * -1.0f, V3(0, 0, 0));
    float d = len(at - eye);
    float h = (active ? 0.20f : 0.14f) * fmaxf(1.0f, d / 6.0f);     // (far ones a little bigger, to stay readable)
    R.text3D(face, s, h, active ? W_GOLD : W_WHITE, true);
}

// ------------------------------------------------------------------ the person in the game's places
void Game::walkFigure(const V3& at, const V3& fwdW, const V3& eye) {
    V3 f = norm(V3(fwdW.x, fwdW.y, 0));
    V3 l = norm(cross(V3(0, 0, 1), f));
    const CarDef* d = &CARS[set.car >= 0 && set.car < CAR_COUNT ? set.car : 0];
    if (race.cars.size() && raceLoaded && walker.scene == WALK_PIT) d = race.player().def;
    u32 m, s; R.teamColours(d, m, s);
    if (!m) m = d->colour;
    if (!s) s = RGBA(240, 240, 240);
    M4 place = M4::translate(at - eye) * M4::basis(f, l, V3(0, 0, 1), V3(0, 0, 0));
    gfx::bindTexture(nullptr);
    walker.draw(R, place, m | 0xFF000000u, s | 0xFF000000u);
}

// ------------------------------------------------------------------ the main menu: the garage on foot
// the monitors on stands in the menu garage (menu.cpp): screen centres and the way they face
int menuStands(V3* pos, V3* facing, int max);

namespace {
struct HubSpot { float x, y; int id; const char* label; };
// each main menu item at a place in the garage (title item ids, see TITLE_ORDER)
const HubSpot HUB[] = {
    {1.45f, 0.45f, 0, "QUICK RACE"},       // by the car's cockpit
    {-1.45f, -0.45f, 6, "TIME TRIAL"},     // the car's other side
    {-0.95f, 2.95f, 8, "SHOWROOM"},        // behind the car
    {-3.6f, 4.15f, 1, "CHAMPIONSHIP"},     // the wall screen
    {3.7f, 3.75f, 2, "CAREER"},            // the workbench
    {5.1f, 3.4f, 4, "SETTINGS"},           // the tool chest
    {-5.75f, -0.6f, 3, "LOAD RACE"},       // the shelves
    {5.7f, -1.0f, 7, "TROPHIES"},          // the trophy cabinet
    {0.0f, -4.35f, 9, "HEADQUARTERS"},     // out of the door
    {-6.2f, -4.2f, 5, "CREDITS"},          // the corner by the door
};
int hubStands = 0;
}   // namespace

void Game::walkHubLayout() {
    Walker& w = walker;
    w.clear(); w.scene = WALK_HUB;
    // the garage (its front open: the door out to the pit lane, where the camera can stand back)
    w.minX = -7; w.maxX = 7; w.minY = -9.0f; w.maxY = 5; w.camDist = 3.3f;
    w.roofs.push_back({-7, -5, 7, 5, 4.6f});
    w.boxes.push_back({-7.5f, -5.15f, -4.2f, -4.95f, 4.6f});  // the front wall either side of the door
    w.boxes.push_back({4.2f, -5.15f, 7.5f, -4.95f, 4.6f});
    w.boxes.push_back({5.6f, 2.4f, 6.7f, 4.4f, 1.5f});        // tool chest
    w.boxes.push_back({2.2f, 4.3f, 5.2f, 4.95f, 0.92f});      // workbench
    w.boxes.push_back({-6.9f, -1.0f, -6.3f, 1.4f, 2.1f});     // shelves
    w.boxes.push_back({6.25f, -2.0f, 6.95f, 0.0f, 2.0f});     // the trophy cabinet (drawn here)
    for (const V3& p : {V3(-5.9f, 3.9f, 0), V3(-5.9f, 2.5f, 0), V3(6.0f, -3.9f, 0)}) w.posts.push_back({p.x, p.y, 0.42f});   // tyres
    w.posts.push_back({-5.0f, -2.8f, 0.5f});                  // fuel rig
    for (const V3& p : {V3(-5.5f, 0.9f, 0), V3(1.5f, 3.9f, 0), V3(-3.9f, -1.6f, 0)}) w.posts.push_back({p.x, p.y, 0.28f});   // the crew
    // the car in its box (as the menu garage turns it)
    float a = 0.32f;
    w.obbs.push_back({0, 0, atan2f(-cosf(a), sinf(a)), 2.45f, 1.0f});
    // the monitors on stands, and the main menu monitor as a station (the old list)
    V3 pos[8], fac[8];
    hubStands = menuStands(pos, fac, 8);
    for (int i = 0; i < hubStands; i++) w.posts.push_back({pos[i].x - fac[i].x * 0.05f, pos[i].y - fac[i].y * 0.05f, 0.27f});
    for (const HubSpot& h : HUB) w.stations.push_back({h.x, h.y, 1.15f, h.id, h.label, 2.3f});
    if (hubStands > 0) {
        V3 f = norm(V3(fac[0].x, fac[0].y, 0));
        w.stations.push_back({pos[0].x + f.x * 0.95f, pos[0].y + f.y * 0.95f, 1.0f, WST_LIST, "MAIN MENU LIST", pos[0].z + 0.85f});
    }
    w.place(0.6f, -3.0f, PI_ * 0.5f);
}

int Game::updateWalkHub(float dt, const Pad& p) {
    if (walker.scene != WALK_HUB) walkHubLayout();
    else if (hubStands == 0) {      // (the garage is built on the first draw: its monitors come in then)
        V3 pos[8], fac[8];
        if (menuStands(pos, fac, 8) > 0) { float px = walker.x, py = walker.y, pyw = walker.yaw; walkHubLayout(); walker.place(px, py, pyw); }
    }
    bool use = walker.update(dt, p, prev);
    if (walker.moving || fabsf(p.rstickX) > 0.15f || fabsf(p.rstickY) > 0.15f) idle = 0;
    if (walker.near >= 0) { int id = walker.stations[walker.near].id; if (id >= 0 && id < TITLE_N) sel = id; }
    if (use) {
        int id = walker.stations[walker.near].id;
        if (id == WST_LIST) { walkList = true; return -1; }
        return id;
    }
    return -1;
}

void Game::walkMenuCamera(V3& eye, V3& fwd, float& fov) const {
    V3 target;
    walker.camera(eye, target);
    fwd = norm(target - eye);
    fov = walker.scene == WALK_HQ ? 56.0f : 58.0f;
}

void Game::drawWalkHub(const V3& eye, const V3& fwd) {
    gfx::setDepthTest(true); gfx::setDepthWrite(true); gfx::setBlend(false); gfx::bindTexture(nullptr);
    // the trophy cabinet: dark wood, glass shelves, gold cups
    M4 rel = M4::translate(V3(0, 0, 0) - eye);
    R.box3D(rel, V3(6.25f, -2.0f, 0), V3(6.95f, 0.0f, 2.0f), RGBA(46, 32, 24));
    for (int s = 0; s < 3; s++) {
        float zz = 0.45f + s * 0.5f;
        R.box3D(rel, V3(6.2f, -1.9f, zz), V3(6.6f, -0.1f, zz + 0.03f), RGBA(150, 150, 160));
        for (int k = 0; k < 4; k++) {
            float yy = -1.7f + k * 0.45f, hh = 0.14f + ((k + s) % 3) * 0.05f;
            R.box3D(rel, V3(6.36f, yy - 0.03f, zz + 0.03f), V3(6.44f, yy + 0.03f, zz + 0.03f + hh), RGBA(230, 185, 60));
            R.box3D(rel, V3(6.33f, yy - 0.06f, zz + 0.03f + hh), V3(6.47f, yy + 0.06f, zz + 0.09f + hh), RGBA(250, 205, 80));
        }
    }
    walkFigure(V3(walker.x, walker.y, walker.z), V3(cosf(walker.yaw), sinf(walker.yaw), 0), eye);
    for (int i = 0; i < (int)walker.stations.size(); i++) {
        const WalkStation& s = walker.stations[i];
        bool on = i == walker.near;
        V3 at(s.x, s.y, s.labelZ);
        float dd = len(V3(s.x - walker.x, s.y - walker.y, 0));
        Walker::marker(R, M4::translate(at - eye), walker.t + i, on);
        if (!on && dd > 6.5f) continue;          // (far stations: just the marker)
        gfx::setBlend(true); gfx::setDepthWrite(false);
        label3D(R, at + V3(0, 0, 0.22f), eye, fwd, s.label, on);
        gfx::setBlend(false); gfx::setDepthWrite(true);
    }
    char info[48]; snprintf(info, sizeof info, "MAIN MENU  %s", ERA_SHORT[era]);
    walkHud(R, walker.near >= 0 ? walker.stations[walker.near].label : nullptr,
            walker.run ? "Move: d-pad / left stick   L: walk   Right stick: camera   X: open"
                       : "Move: d-pad / left stick   L: run   Right stick: camera   X: open", info);
}

// ------------------------------------------------------------------ headquarters on foot
void Game::walkHQLayout() {
    Walker& w = walker;
    w.clear(); w.scene = WALK_HQ;
    w.minX = -5; w.maxX = 5; w.minY = -4; w.maxY = 4; w.ceiling = 3.2f; w.camDist = 2.7f;
    w.boxes.push_back({-1.98f, 0.5f, 1.98f, 1.4f, 0.76f});    // the desk (low: the camera looks over it)
    w.boxes.push_back({4.45f, -2.6f, 5.0f, 1.2f, 2.4f});      // the trophy cabinet
    w.boxes.push_back({-0.3f, 2.5f, 0.3f, 3.1f, 1.05f});      // the principal's chair
    static const float CX[ERA_COUNT] = {-0.6f, 0.6f};         // (menu.cpp HQ_CAR_X: the model cars on the desk)
    for (int e = 0; e < ERA_COUNT; e++) w.stations.push_back({CX[e], 0.15f, 0.7f, e, ERA_SHORT[e], 1.25f});
    if (hqChosen) w.stations.push_back({0.0f, -3.35f, 0.9f, WST_LIST, "MAIN MENU", 2.0f});
    w.place(0.0f, -2.3f, PI_ * 0.5f);
}

void Game::updateWalkHQ(float dt, const Pad& p) {
    if (walker.scene != WALK_HQ) walkHQLayout();
    bool use = walker.update(dt, p, prev);
    if (walker.moving || fabsf(p.rstickX) > 0.15f || fabsf(p.rstickY) > 0.15f) idle = 0;
    if (walker.near >= 0 && walker.stations[walker.near].id < ERA_COUNT) hqSel = walker.stations[walker.near].id;
    if (use && t > 0.3f) {
        int id = walker.stations[walker.near].id;
        if (id < ERA_COUNT) chooseEra(id);
        screen = SCR_TITLE; sel = 0; t = 0; walkList = false; walker.scene = -1;   // (the garage: in at the door)
        return;
    }
    if (pressed(p, &Pad::circle) && hqChosen) { screen = SCR_TITLE; sel = 0; t = 0; walkList = false; walker.scene = -1; return; }
    if (idle > cfg.attract && !aiDrivesPlayer) { idle = 0; introBegin(1); }
}

void Game::drawWalkHQ(const V3& eye, const V3& fwd) {
    gfx::setDepthTest(true); gfx::setDepthWrite(true); gfx::setBlend(false); gfx::bindTexture(nullptr);
    walkFigure(V3(walker.x, walker.y, walker.z), V3(cosf(walker.yaw), sinf(walker.yaw), 0), eye);
    for (int i = 0; i < (int)walker.stations.size(); i++) {
        const WalkStation& s = walker.stations[i];
        bool on = i == walker.near;
        V3 at = s.id < ERA_COUNT ? V3(s.x, 0.95f, s.labelZ) : V3(s.x, s.y, s.labelZ);
        Walker::marker(R, M4::translate(at - eye) * M4::scale(0.6f, 0.6f, 0.6f), walker.t + i, on);
        gfx::setBlend(true); gfx::setDepthWrite(false);
        label3D(R, at + V3(0, 0, 0.22f), eye, fwd, s.label, on);
        gfx::setBlend(false); gfx::setDepthWrite(true);
    }
}

// ------------------------------------------------------------------ the pits on foot
// the local frame: x metres from the pit entry along the lap, y metres to the left of the centre line
V3 Game::walkPitWorld(float x, float y, float z) const { return race.track.world(race.pitIn + x, y, z); }

void Game::walkPitLayout() {
    Walker& w = walker;
    w.clear(); w.scene = WALK_PIT;
    w.boundsAreWalls = false; w.camDist = 3.6f;
    float hw = race.track.pts[0].hw;
    float wallD = hw + 2.3f, garage = race.boxD + 3.6f;
    int slots = TEAMS_PER_ERA + 1;
    float first = PIT_BOX0 - 7.5f, last = PIT_BOX0 + (slots - 1) * 16.0f + 7.5f;
    w.minX = first - 14; w.maxX = last + 14;
    w.minY = wallD + 0.25f; w.maxY = garage + 11.9f;
    for (int team = 0; team < slots; team++) {
        float a = PIT_BOX0 + team * 16.0f - 7.5f, b = a + 15.0f;
        // the garage: its walls (the side wall and the pillar to the next one), its ceiling
        w.boxes.push_back({b, garage, b + 1.0f, garage + 12.0f, 4.6f});
        if (team == 0) w.boxes.push_back({a - 0.3f, garage, a, garage + 12.0f, 4.6f});
        w.roofs.push_back({a, garage, b + 1.0f, garage + 12.0f, 4.6f});
        // the team's pit wall stand: the desk with its screens, the raised floor behind it, posts, canopy
        float sa = a + 3.5f, sb = b - 3.5f, d0 = wallD + 0.25f, d1 = wallD + 2.5f;
        w.boxes.push_back({sa, d0, sb, d0 + 0.75f, 2.05f});
        w.boxes.push_back({sa, d0 + 0.75f, sb, d1, 0.5f});
        w.posts.push_back({sa + 0.06f, d1 - 0.05f, 0.08f});
        w.posts.push_back({sb + 0.06f, d1 - 0.05f, 0.08f});
        w.roofs.push_back({sa - 0.3f, d0 - 0.4f, sb + 0.3f, d1 + 0.3f, 3.0f});
    }
    // the stations: our engineer's place on the stand, our car
    const Car& me = race.player();
    int slot = pitSlot(me.def);
    float a = PIT_BOX0 + slot * 16.0f - 7.5f, b = a + 15.0f;
    float d0 = wallD + 0.25f, d1 = wallD + 2.5f;
    w.stations.push_back({(a + b) * 0.5f, (d0 + 0.75f + d1) * 0.5f, 2.1f, WST_ENGINEER, "PIT PLANS", 3.7f});
    w.stations.push_back({race.pitRel(me.s), me.d, 2.6f, WST_CAR, "GET IN", 2.2f});
}

void Game::walkPitBegin(bool garage) {
    (void)garage;      // (only from the practice / qualifying garage: race stops run the called plan)
    Car& me = race.player();
    walkGarage = true;
    pitPlansLoad(race.era);
    walkPitLayout();
    float cx = race.pitRel(me.s), cy = me.d;
    walker.place(cx - 1.0f, cy - 1.7f, -PI_ * 0.5f);         // beside the car in the garage, facing out
    walker.camYaw = -PI_ * 0.5f + 0.5f;
    race.timeAccel = 1;
    screen = SCR_WALK; t = 0;
}

void Game::updateWalkPit(float dt, const Pad& p, bool onFoot) {
    // the session carries on round us (our car waits in the garage)
    {
        int accel = (int)race.timeAccel;
        float STEP = accel >= 4 ? 1.0f / 30 : 1.0f / 60;
        accum += dt * accel;
        int guard = 0;
        while (accum >= STEP && guard++ < 40) { race.update(STEP, 0, 0, 0, 0, false); accum -= STEP; }
    }
    raceAudio();
    race.sfxEvents.clear();
    if (race.over) { finishRace(); return; }
    if (walker.scene != WALK_PIT) walkPitLayout();
    if (!onFoot) return;          // (a menu has the focus: the person stands still)
    // the cars nearby are in the way (turned boxes along the lane)
    walker.obbs.clear();
    for (const Car& c : race.cars) {
        float rx = race.pitRel(c.s);
        if (rx < walker.minX - 5 || rx > walker.maxX + 5 || c.d < walker.minY - 3) continue;
        V3 t0 = walkPitWorld(rx, c.d, 0), t1 = walkPitWorld(rx + 1, c.d, 0), l1 = walkPitWorld(rx, c.d + 1, 0);
        float a = atan2f(dot(c.fwd, norm(l1 - t0)), dot(c.fwd, norm(t1 - t0)));
        walker.obbs.push_back({rx, c.d, a, c.def->lengthM * 0.5f, 1.0f});
    }
    bool use = walker.update(dt, p, prev);
    if (pressed(p, &Pad::start)) { screen = SCR_PAUSE; sel = 0; return; }
    int id = walker.near >= 0 ? walker.stations[walker.near].id : -1;
    if (use && id == WST_ENGINEER) { screen = SCR_PLANS; planRow = 0; return; }        // the pit plans
    if ((use && id == WST_CAR) || pressed(p, &Pad::circle)) { screen = SCR_GARAGE; setupSel = 8; R.camInit = false; }
}

// ------------------------------------------------------------------ the pit plans
void Game::pitPlansLoad(int e) {
    if (pitPlansEra == e) return;
    pitPlansEra = e;
    // the defaults: 1 mediums and a full tank, 2 softs, 3 the driver change (hards), 4 intermediates
    static const int TY[PLANS] = {TY_MEDIUM, TY_SOFT, TY_HARD, TY_INTER};
    for (int i = 0; i < PLANS; i++) {
        PitPlan d; d.tyres = true; d.tyre = TY[i]; d.fuel = 1; d.repair = true;
        d.swapDriver = i == 2; d.nextDriver = i == 2 ? 1 : 0; d.aiDrives = d.nextDriver != 0;
        pitPlans[i] = d; pitPlanSet[i] = true;
    }
    FILE* f = fopen(eraSave("pitplans", e), "rb");
    if (!f) return;
    int head[2] = {0, 0};
    if (fread(head, 4, 2, f) == 2 && head[0] == 0x4E4C5050 && head[1] == PLANS) {   // "PPLN"
        for (int i = 0; i < PLANS; i++) {
            int v[6]; float fu;
            if (fread(v, 4, 6, f) != 6 || fread(&fu, 4, 1, f) != 1) break;
            pitPlanSet[i] = v[0] != 0;
            PitPlan& q = pitPlans[i];
            q.tyres = v[1] != 0; q.tyre = v[2] >= 0 && v[2] < TY_COUNT ? v[2] : TY_MEDIUM; q.repair = v[3] != 0;
            q.swapDriver = v[4] != 0; q.nextDriver = v[5] >= 0 && v[5] < DRIVERS_PER_CAR ? v[5] : 0;
            q.fuel = fu < 0 ? 0 : (fu > 1 ? 1 : fu); q.aiDrives = q.swapDriver && q.nextDriver != 0;
        }
    }
    fclose(f);
}

void Game::pitPlansSave() const {
    if (pitPlansEra < 0) return;
    FILE* f = fopen(eraSave("pitplans", pitPlansEra), "wb");
    if (!f) return;
    int head[2] = {0x4E4C5050, PLANS};
    fwrite(head, 4, 2, f);
    for (int i = 0; i < PLANS; i++) {
        const PitPlan& q = pitPlans[i];
        int v[6] = {pitPlanSet[i] ? 1 : 0, q.tyres ? 1 : 0, q.tyre, q.repair ? 1 : 0, q.swapDriver ? 1 : 0, q.nextDriver};
        fwrite(v, 4, 6, f); fwrite(&q.fuel, 4, 1, f);
    }
    fclose(f);
}

// the corners of the screen (in the 480 x 272 layout): about 15 % of the width by 20 % of the height
int Game::touchCorner(float x, float y) {
    const float CW = SCREEN_W * 0.15f, CH = SCREEN_H * 0.20f;
    bool l = x < CW, r = x >= SCREEN_W - CW, t = y < CH, b = y >= SCREEN_H - CH;
    if (t && l) return 0;
    if (t && r) return 1;
    if (b && l) return 2;
    if (b && r) return 3;
    return -1;
}

static const char* fuelClip(float f) {
    return f >= 0.9f ? "fuel_full" : f >= 0.65f ? "fuel_three_quarter" : f >= 0.4f ? "fuel_half" : f >= 0.2f ? "fuel_quarter" : f > 0 ? "fuel_splash" : "fuel_none";
}
static const char* fuelWords(float f) {
    return f >= 0.9f ? "fuel full" : f >= 0.65f ? "three-quarter fuel" : f >= 0.4f ? "half a tank" : f >= 0.2f ? "a quarter tank" : f > 0 ? "a splash of fuel" : "no fuel";
}
static const char* TYRE_CLIP[TY_COUNT] = {"tyres_soft", "tyres_medium", "tyres_hard", "tyres_inter", "tyres_wet"};
static const char* TYRE_WORDS[TY_COUNT] = {"four new softs", "four new mediums", "four new hards", "four new inters", "four new wets"};

bool Game::pitPlanTouch(const Pad& p, bool manage) {
    if (!(p.touches > 0 && prev.touches == 0)) return false;
    int c = touchCorner(p.tx[0], p.ty[0]);
    if (c < 0) return false;
    if (!raceLoaded || race.session != SES_RACE || manage || race.qte.on || race.set.timeTrial) return true;   // (a corner: never the HUD)
    Car& me = race.player();
    if (me.finished || (me.pit == PIT_STOP && me.pitPhase >= 0)) return true;
    pitPlansLoad(race.era);
    radioQ.clear();
    char b[64];
    if (c == pitPlanSel) {                      // the chosen plan's corner again: called off
        pitPlanSel = -1;
        if (me.pit == PIT_NONE) me.wantPit = false;
        radioQ.push_back("16_cancel");
        race.message("Copy, stop cancelled", RGBA(255, 220, 60));
        return true;
    }
    if (!pitPlanSet[c]) {
        snprintf(b, sizeof b, "Plan %d is empty - set it up in practice", c + 1);
        radioQ.push_back("16_plan_empty");
        race.message(b, RGBA(255, 160, 60));
        return true;
    }
    pitPlanSel = c;
    if (me.pit == PIT_NONE) me.wantPit = true;
    const PitPlan& q = pitPlans[c];
    snprintf(b, sizeof b, "16_plan_%d", c + 1); radioQ.push_back(b);
    radioQ.push_back(q.tyres ? std::string("16_") + TYRE_CLIP[q.tyre] : std::string("16_tyres_none"));
    bool fuel = race.ep().refuel;
    if (fuel) radioQ.push_back(std::string("16_") + fuelClip(q.fuel));
    bool fix = q.repair && me.damage() > 0.01f;
    if (fix) radioQ.push_back("16_repair");
    radioQ.push_back(q.swapDriver ? "16_swap" : "16_no_swap");
    radioQ.push_back("16_box_plan");
    // the same as words on the screen (two lines: the messages hold 64 characters)
    char l1[64], l2[64];
    snprintf(l1, sizeof l1, "Plan %d: %s%s%s", c + 1, q.tyres ? TYRE_WORDS[q.tyre] : "tyres stay on", fuel ? ", " : "", fuel ? fuelWords(q.fuel) : "");
    snprintf(l2, sizeof l2, "%s%s - box this lap", fix ? "Fix the damage, " : "", q.swapDriver ? "driver change" : "you're staying in");
    race.message(l2, RGBA(255, 220, 60));
    race.message(l1, RGBA(255, 220, 60));
    return true;
}

void Game::pitPlanApply() {
    Car& me = race.player();
    PitPlan& pl = me.plan;
    const PitPlan& q = pitPlans[pitPlanSel < 0 ? 0 : pitPlanSel];
    pl.tyres = q.tyres; pl.tyre = q.tyre; pl.repair = q.repair;
    if (race.ep().refuel) pl.fuel = fminf(1.0f, fmaxf(q.fuel, fmaxf(me.fuel, 0.1f)));     // (never drained)
    // the driver: the plan's team-mate (or the next one who may still drive), else whoever is in
    int k = me.driver;
    if (q.swapDriver) {
        k = q.nextDriver;
        for (int tries = 0; tries < DRIVERS_PER_CAR && (k == me.driver || me.drv[k].hours >= MAX_DRIVE_H - 0.05f); tries++) k = (k + 1) % DRIVERS_PER_CAR;
    }
    pl.nextDriver = k; pl.swapDriver = k != me.driver; pl.aiDrives = k != 0;
    pitPlanSel = -1;
    race.releasePlayer();
    if (!pl.aiDrives) race.timeAccel = 1;
    else { mgrCam = 0; mgrSel = 0; mgrFocus = 1; mgrRow = 0; snprintf(note, sizeof note, "On the pit wall: you're managing the team"); noteT = 3.0f; }
}

void Game::radioTick() {
    if (radioQ.empty() || sound::radioBusy()) return;
    std::string n = radioQ.front();
    radioQ.erase(radioQ.begin());
    sound::radio(n.c_str());          // (a clip not made yet: skipped, the words are on the screen)
}

// the plan editor at the engineer's place: which plan (1-4, the touch corner it's on), its tyres, fuel,
// repairs and the driver for the next stint
static const char* CORNER_NAME[4] = {"top left", "top right", "bottom left", "bottom right"};
void Game::updatePlans(int dx, int dy, bool ok, bool back) {
    const int ROWS = 7;          // plan, tyres, fuel, repair, driver, in use, done
    planRow = (planRow + dy + ROWS) % ROWS;
    PitPlan& q = pitPlans[planEdit];
    bool changed = false;
    if (dx) {
        switch (planRow) {
            case 0: planEdit = (planEdit + dx + PLANS) % PLANS; break;
            case 1: { int v = q.tyres ? q.tyre + 1 : 0; v = (v + dx + TY_COUNT + 1) % (TY_COUNT + 1); q.tyres = v > 0; if (v) q.tyre = v - 1; changed = true; break; }
            case 2: q.fuel = fminf(1, fmaxf(0, roundf(q.fuel * 10 + dx) / 10)); changed = true; break;
            case 3: q.repair = !q.repair; changed = true; break;
            case 4: { int v = q.swapDriver ? q.nextDriver : 0;      // 0 stay in, 1-2 a team-mate (k 1..2)
                      v = (v + dx + DRIVERS_PER_CAR) % DRIVERS_PER_CAR;
                      q.swapDriver = v != 0; q.nextDriver = v; q.aiDrives = v != 0; changed = true; break; }
            case 5: pitPlanSet[planEdit] = !pitPlanSet[planEdit]; changed = true; break;
            default: break;
        }
        if (planRow >= 1 && planRow <= 4) pitPlanSet[planEdit] = true;     // (editing a plan puts it in use)
    }
    if (ok && planRow == 5) { pitPlanSet[planEdit] = !pitPlanSet[planEdit]; changed = true; }
    if (changed) pitPlansSave();
    if (back || (ok && planRow == 6)) { screen = SCR_WALK; }
}

void Game::drawPlans() {
    gfx::setDepthTest(false); gfx::setBlend(true); gfx::setFog(false, 0, 1, 0);
    float x0 = SCREEN_W / 2 - 150, y0 = 38, w = 300, h = 196;
    gfx::drawFill2D(x0, y0, w, h, RGBA(8, 12, 20, 225));
    gfx::drawFill2D(x0, y0, w, 2, W_ACC);
    R.font.drawCentre(SCREEN_W / 2, y0 + 6, "PIT PLANS", W_WHITE, 1.1f);
    // the four plans as the corners they're called with
    for (int i = 0; i < PLANS; i++) {
        float bx = x0 + 18 + i * 68, by = y0 + 28;
        u32 c = i == planEdit ? W_ACC : (pitPlanSet[i] ? RGBA(60, 70, 90) : RGBA(30, 34, 44));
        gfx::drawFill2D(bx, by, 60, 20, c);
        char b[8]; snprintf(b, sizeof b, "%d", i + 1);
        R.font.drawCentre(bx + 30, by + 3, b, i == planEdit ? RGBA(20, 20, 20) : W_WHITE, 0.9f);
    }
    const PitPlan& q = pitPlans[planEdit];
    char v[7][64];
    snprintf(v[0], 64, "Plan %d  (touch the %s corner)", planEdit + 1, CORNER_NAME[planEdit]);
    snprintf(v[1], 64, "Tyres  < %s >", q.tyres ? TYRE_NAME[q.tyre] : "stay on");
    if (raceLoaded && !race.ep().refuel) snprintf(v[2], 64, "Fuel  (no refuelling)");
    else snprintf(v[2], 64, "Fuel  < fill to %.0f%% >", q.fuel * 100);
    snprintf(v[3], 64, "Repairs  < %s >", q.repair ? "yes" : "no");
    if (q.swapDriver && raceLoaded) { char dn[24]; Race::driverName(race.player(), q.nextDriver, dn, sizeof dn); snprintf(v[4], 64, "Driver  < change to %s >", dn); }
    else snprintf(v[4], 64, "Driver  < stay in >");
    snprintf(v[5], 64, "In use  < %s >", pitPlanSet[planEdit] ? "yes" : "no (empty)");
    snprintf(v[6], 64, "Done");
    for (int r = 0; r < 7; r++) {
        float y = y0 + 58 + r * 18;
        if (r == planRow) gfx::drawFill2D(x0 + 8, y - 2, w - 16, 17, RGBA(255, 150, 40, 220));
        R.font.drawCentre(SCREEN_W / 2, y, v[r], r == planRow ? RGBA(20, 20, 20) : (r >= 1 && r <= 4 && !pitPlanSet[planEdit] ? W_DIM : W_WHITE), 0.78f);
    }
    R.font.drawCentre(SCREEN_W / 2, y0 + h - 14, "In the race: touch a corner to call that plan, again to call it off", W_DIM, 0.56f);
    gfx::drawFill2D(0, SCREEN_H - 18, SCREEN_W, 18, RGBA(0, 0, 0, 140));
    R.font.drawCentre(SCREEN_W / 2, SCREEN_H - 15, "UP/DOWN  row     LEFT/RIGHT  change     X  select     O  back to the pits", W_DIM, 0.6f);
    gfx::setBlend(false); gfx::setDepthTest(true);
}

Camera Game::walkPitCamera() const {
    V3 e, tg; walker.camera(e, tg);
    Camera c;
    c.eye = walkPitWorld(e.x, e.y, e.z);
    c.fwd = norm(walkPitWorld(tg.x, tg.y, tg.z) - c.eye);
    c.up = V3(0, 0, 1); c.fov = 60;
    return c;
}

void Game::drawWalkPit() {
    gfx::beginFrame(R.skyColour(race));
    Camera c = walkPitCamera();
    R.mirrorsLive = false;
    R.drawScene(race, c, false);
    M4 proj = M4::perspective(c.fov, (float)SCREEN_W / SCREEN_H, 0.8f, 1600.0f), view = M4::lookDir(V3(0, 0, 0), c.fwd, c.up);
    gfx::setProjection(proj); gfx::setView(view); clip::setMatrices(proj, view);
    gfx::setFog(false, 0, 1, 0);
    gfx::setDepthTest(true); gfx::setDepthWrite(true); gfx::setBlend(false);
    R.camRight = norm(cross(c.fwd, V3(0, 0, 1))); R.camUp = cross(R.camRight, c.fwd);
    // the person, the markers over the stations
    V3 at = walkPitWorld(walker.x, walker.y, walker.z);
    V3 ahead = walkPitWorld(walker.x + cosf(walker.yaw) * 0.5f, walker.y + sinf(walker.yaw) * 0.5f, walker.z);
    walkFigure(at, ahead - at, c.eye);
    char lab[48] = "";
    for (int i = 0; i < (int)walker.stations.size(); i++) {
        const WalkStation& s = walker.stations[i];
        bool on = i == walker.near;
        V3 m = walkPitWorld(s.x, s.y, s.labelZ);
        float dist = sqrtf((s.x - walker.x) * (s.x - walker.x) + (s.y - walker.y) * (s.y - walker.y));
        float ms = clampW(len(m - c.eye) / 7.0f, 1.0f, 4.0f);   // (seen from far off: bigger)
        Walker::marker(R, M4::translate(m - c.eye) * M4::scale(ms, ms, ms), walker.t + i, on);
        if (on) snprintf(lab, sizeof lab, "%s", s.label);
        else snprintf(lab, sizeof lab, "%s  %.0f m", s.label, dist);
        gfx::setBlend(true); gfx::setDepthTest(false);       // (seen through the pit building: it's where to go)
        label3D(R, m + V3(0, 0, 0.55f), c.eye, c.fwd, lab, on);
        gfx::setBlend(false); gfx::setDepthTest(true);
    }
    R.drawNight(race, 0);
    gfx::setProjection(proj); gfx::setView(view);
    gfx::setFog(false, 0, 1, 0);
    R.drawBeam(race, c);
    R.drawEffects(race, c);
    R.drawWeather(race, false);
    // the words (or the plan editor over the scene)
    if (screen == SCR_PLANS) drawPlans();
    else {
        char info[64]; snprintf(info, sizeof info, "PIT LANE  #%d", race.player().number);
        const char* prompt = nullptr;
        if (walker.near >= 0) prompt = walker.stations[walker.near].id == WST_ENGINEER ? "PIT PLANS" : "GET IN";
        walkHud(R, prompt, walker.run ? "Move: d-pad / left stick   L: walk   Right stick: camera   O: back to the garage"
                                      : "Move: d-pad / left stick   L: run   Right stick: camera   O: back to the garage", info);
    }
    if (cfg.showFps) drawFps();
    finishFrame();
}
