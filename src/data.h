// Cars and circuits (copied from the Unity game's CarDatabase / TrackDatabase) and the loaders
// for the model packs (.emm) and texture files made by the tools/ scripts.
#pragma once
#include "platform.h"
#include <vector>

enum CarClass { CLASS_HYPER = 0, CLASS_GT3 = 1 };
// Endurance Motor Racing 2.0: the two classes of the 2027 FIA WEC. Formula Legends' "eras" became the
// classes: the class you pick at headquarters sets your car, championship and career; every race runs
// both classes together. (A car's era == its class.)
enum Era { ERA_HYPER = 0, ERA_GT3 = 1, ERA_COUNT = 2 };
// the three sets of technical regulations: Le Mans Hypercar (LMH), LMDh (both race as Hypercar), LMGT3
enum CarType { TYPE_LMH = 0, TYPE_LMDH = 1, TYPE_GT3 = 2 };
extern const char* ERA_NAME[ERA_COUNT];     // "Hypercar", "LMGT3"
extern const char* ERA_SHORT[ERA_COUNT];    // "HYPERCAR", "LMGT3"
extern const char* ERA_ID[ERA_COUNT];       // "hyper", "gt3" (files)
extern const char* TYPE_NAME[3];            // "LMH", "LMDh", "LMGT3"

struct CarDef {
    const char* id;
    const char* make;
    const char* model;
    const char* team;
    int numbers[2];
    CarClass cls;
    int era;                 // Era (= the class)
    int type;                // CarType: which technical regulations it is built to
    const char* engine;      // label shown in the menu
    const char* sound;       // engine recording (DATA/audio/<sound>.raw)
    float idleRpm, maxRpm;
    // performance: mass with the driver, engine power, top speed gain, drag factor, grip factor
    float massKg, powerKW, topGain, aero, grip;
    float lengthM;
    u32 colour;              // main paint colour (menus, map dots)
    bool hasWheels;          // the AI model already has wheels
    float cornerMul = 1;     // AI corner speed factor (career car upgrades)
    bool career = false;     // the player's own team car in career mode (not in the normal car lists)
};

struct TrackDef {
    const char* id;
    const char* name;
    const char* location;
    float startHour;         // local time of the race start
    float hours;             // the real race's length in hours (6, 8, 24; Qatar 1812 km ~10)
    int gpLaps;              // laps the winner covers in the real race (the "full distance" option)
    bool street;             // street circuit: walls close to the road, buildings behind
    bool floodlit;           // night race under floodlights
};
// Each era's championship calendar (indices into TRACKS) and which eras race at a circuit
static const int ERA_ROUNDS = 9;            // the 2027 FIA WEC calendar (the same for both classes)
extern const int ERA_CAL[ERA_COUNT][ERA_ROUNDS];
extern const float ERA_HOUR[ERA_COUNT][ERA_ROUNDS];   // start time of each round (night races)
bool trackInEra(int track, int era);
int eraTrackCount(int era);
int eraTrack(int era, int k);                          // k-th circuit of an era's calendar
extern const char* ROUND_NAME[ERA_ROUNDS];             // "24 Hours of Le Mans", ...
float roundPointsK(int round);                         // points multiplier (Le Mans: double)

extern CarDef CARS[];          // not const: the career team car takes the player's name, livery and upgrades
extern const int CAR_COUNT;     // every car, the career cars included (they are last)
extern const int CAR_PICK;      // the 40 team cars (20 Hypercars, 20 LMGT3s)
int careerCarIndex(int era);    // CARS index of the career car of that era
int eraFirstCar(int era);       // CARS index of a class's first make
static const int TEAMS_PER_ERA = 10;   // makes in each class
extern const TrackDef TRACKS[];
extern const int TRACK_COUNT;

struct ModelLod {
    int count = 0;
    int glass = 0;           // vertices at the end of the list that are window glass
    const ModelVertex* verts = nullptr;
    const int8_t* normals = nullptr;   // PC models ('EMN1'): per vertex int8 nx, ny, nz + uint8 shade, for real lighting
    Texture tex;
};

struct Model {
    int lodCount = 0;
    ModelLod lod[4];
    float scale[3] = {1, 1, 1};
    float offset[3] = {0, 0, 0};
    float cabX = 0;          // cars: where the cabin sits along the car (the interior is placed there)
    float panelY = 1, panelX = 0;   // cars: where the race number goes on the side
    // optional 'EMX1' block after the levels of detail (the Vita's Unity AI cars): where this body's
    // wheel arches are. axleF == 0: use the class defaults. ownWheels: the model has its wheels on.
    float axleF = 0, axleR = 0, wheelY = 0.86f, wheelR = 0;
    bool ownWheels = false;
    int mirrorCount = 0;     // (PC cars, 'EMQ1') the side mirrors' glass: x, y, z, half width, half height
    float mirror[2][5] = {};
    uint32_t flags = 0;    // EMX1 flags: bit 0 own wheels, bit 1 Unity AI interior (new instrument layout)
    // levels of detail stored without a texture of their own use level 0's (the GPU's mipmaps already
    // make it smaller in the distance): bit i set = lod[i] borrows lod[0]'s texture
    uint8_t sharedTex = 0;
    void reshare() { for (int i = 1; i < lodCount; i++) if (sharedTex & (1u << i)) lod[i].tex = lod[0].tex; }
    void* blob = nullptr;
    bool load(const char* path);
    void unload();
    // model matrix that turns the stored -1..1 positions into metres placed by `place`
    M4 matrix(const M4& place) const {
        return place * M4::translate(V3(offset[0], offset[1], offset[2])) * M4::scale(scale[0], scale[1], scale[2]);
    }
};

// (the PC edition) a car made in Blender (tools/blender/emr_export_game.py): 'ECM1' in
// DATA/cars/<id>/<id>.ecm (the PC/ folder), with its PNG textures next to it. Up to four levels of
// detail; each is the body and its four wheels (pivoted at the hub), each a mesh in video memory drawn
// in a few groups (texture + shine). Metres, game axes (+X forward, +Y left, +Z up), the ground at z = 0.
struct CarMesh {
    enum { MAT_GLASS = 1, MAT_INTERIOR = 2, MAT_HELMET = 4, MAT_TYRE = 8, MAT_LIVERY = 16, MAT_BOARD = 32 };
    enum { TEX_PLAIN = 0, TEX_LIVERY = 1, TEX_TYRE = 2, TEX_BOARD = 3 };
    enum { PART_BODY = 0, PART_FL, PART_FR, PART_RL, PART_RR, PART_COUNT };
    struct Mat { int tex = -1; float spec = 0.1f, gloss = 12; uint32_t flags = 0; };
    struct Tex { int id = 0; int role = TEX_PLAIN; int arg = 0; int kb = 0; };
    struct Draw { int mat = 0, first = 0, count = 0; };
    struct Part { int mesh = 0; int tris = 0; int drawFirst = 0, drawCount = 0; };
    struct Lod { Part part[PART_COUNT]; int tris = 0; };
    int lodCount = 0;
    Lod lod[4];
    std::vector<Mat> mats;
    std::vector<Tex> texs;
    std::vector<Draw> draws;
    float size[3] = {0, 0, 0};       // length, width, height
    float head[3] = {0, 0, 1};       // the driver's helmet (its centre)
    float hub[4][3] = {};            // wheel hubs: FL, FR, RL, RR
    float wheelR[4] = {};            // tyre radius
    int textureKB = 0;
    // hd: full-size textures (your car, the showroom); else halved once when 2048 px or more
    bool load(const char* carId, bool hd);
    void unload();
    bool loaded() const { return lodCount > 0; }
    // the texture to draw a material with: the livery / number board of this car number, the tyres
    // of this compound (TY_*), or the material's own
    int texFor(const Mat& m, int number, int compound) const;
};

struct TexFile {
    Texture tex;
    void* blob = nullptr;
    bool load(const char* path);
    void unload();
};

struct Font {
    Texture tex;
    int lineH = 16;
    int texScale = 1;     // 'FNT2' (the Vita's font): the atlas is drawn this many times larger than the layout
    float u(int px) const { return (float)(px * texScale) / tex.w; }
    float v(int px) const { return (float)(px * texScale) / tex.h; }
    struct Glyph { int16_t x, y, w, h, adv, xoff; } g[95];
    void* blob = nullptr;
    bool load(const char* path);
    int width(const char* s, float scale = 1) const;
    void draw(float x, float y, const char* s, u32 color, float scale = 1) const;
    void drawRight(float x, float y, const char* s, u32 color, float scale = 1) const { draw(x - width(s, scale), y, s, color, scale); }
    void drawCentre(float x, float y, const char* s, u32 color, float scale = 1) const { draw(x - width(s, scale) * 0.5f, y, s, color, scale); }
};
