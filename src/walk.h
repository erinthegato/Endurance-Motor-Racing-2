// On foot: the player as a person walking (or running) round a small area -- the pit lane, garages and
// pit wall at the track, the garage of the main menu, the headquarters office. Movement on the d-pad and
// the left stick, L toggles running, the right stick turns the camera (third person, kept out of walls),
// X uses the station in reach, O backs out.
//
// The walker lives in its own flat local frame (x, y on the ground, z up, right-handed): the menu garage
// and the office use their own coordinates; the pits use (metres from the pit entry, metres to the left
// of the centre line), which the game turns into the world with Track::world.
#pragma once
#include "render.h"
#include <vector>

// the walkable areas
enum { WALK_HUB = 0, WALK_HQ, WALK_PIT };
// station ids the game gives meaning to (the main menu's stations use the title item ids 0..9)
enum { WST_LIST = 100, WST_ENGINEER = 200, WST_CAR = 201 };

// an obstacle: a box on the ground (h: its height -- low ones up to STEP_H are stepped onto, tall ones
// over CAM_H also stop the camera), a turned box (a car), a post
struct WalkBox { float x0, y0, x1, y1, h; };
struct WalkObb { float cx, cy, ang, hx, hy; };
struct WalkPost { float x, y, r; };
// somewhere X does something: id (the game's meaning), reach radius, its label and where the label floats
struct WalkStation { float x, y, r; int id; const char* label; float labelZ; };

struct Walker {
    static constexpr float RADIUS = 0.30f, STEP_H = 0.55f, CAM_H = 1.8f;
    static constexpr float WALK = 1.4f, RUN = 5.5f;       // m/s
    int scene = -1;                         // which area is laid out (the game's WALK_* ids)
    // the person
    float x = 0, y = 0, z = 0, yaw = 0, vx = 0, vy = 0;
    bool run = false; float stillT = 0;     // L toggles running; standing still a moment drops back to walking
    float phase = 0, t = 0, bob = 0;        // stride phase (radians), clock, the body's bounce
    // the camera: angles round the person, distance (wanted / after the walls)
    float camYaw = 0, camPitch = 0.30f, camDist = 3.4f, camNow = 3.4f, camIdle = 0;
    bool camInit = false;
    // the area
    float minX = -5, maxX = 5, minY = -5, maxY = 5;
    bool boundsAreWalls = true;              // indoors: the edges stop the camera too
    float ceiling = 99;                      // the camera stays under it (indoors)
    std::vector<WalkBox> boxes, roofs;       // roofs: areas with a ceiling (h) for the camera
    std::vector<WalkObb> obbs;               // turned boxes (cars), refreshed by the game when they move
    std::vector<WalkPost> posts;
    std::vector<WalkStation> stations;
    int near = -1;                           // index of the station in reach (-1 none)
    bool moving = false;                     // the pad asked to move this frame

    void clear();
    void place(float px, float py, float facing);
    // one frame: the pad moves the person and turns the camera; returns true if X was pressed at a station
    bool update(float dt, const Pad& p, const Pad& prev);
    // the camera in the local frame: eye and the point it looks at
    void camera(V3& eye, V3& target) const;
    // the person drawn at `place` (camera-relative, feet on z 0, facing +X), suit / trim colours
    void draw(Renderer& R, const M4& place, u32 suit, u32 trim) const;
    // a floating marker over a station: an orange diamond turning, larger when in reach
    static void marker(Renderer& R, const M4& place, float t, bool active);
private:
    void collide();
    float floorAt(float px, float py) const;
    float camRay(float tx, float ty, float ex, float ey) const;   // 0..1 along target -> eye before a wall
};

// the 2D bits shown while walking: the prompt for the station in reach and a line of controls
void walkHud(Renderer& R, const char* prompt, const char* help, const char* info = nullptr);
