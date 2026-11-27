#include "data.h"
#include "endian.h"
#include <cstdlib>
#include <cstring>

static u32 C(float r, float g, float b) { return RGBA((int)(r * 255), (int)(g * 255), (int)(b * 255)); }

// The 2027 FIA WEC field: 10 Hypercar makes (LMH and LMDh) and 10 LMGT3 makes, two cars each, fictional
// names in the style of the real ones, in each class's pecking order; then the career cars (your own team).
// Mass includes the driver; power is the BoP-limited output at the wheels (Hypercar ~500 kW, LMGT3 ~400 kW).
CarDef CARS[] = {
    // id              make        model          team                     numbers     class        era        type       engine                            sound                    idle  max    kg    kW   gain     aero   grip    len    colour                   wheels
    {"ardenza499",   "Ardenza",  "499X",        "Ardenza Corse",         {50, 51},   CLASS_HYPER, ERA_HYPER, TYPE_LMH,  "3.0 V6 twin-turbo hybrid (4WD)",  "engine_hyper_v6",       3200, 9000,  1112, 507,  0.000f, 1.000f, 1.012f, 4.98f, C(0.78f, 0.04f, 0.05f), false},
    {"kaizen10",     "Kaizen",   "TR-010",      "Kaizen Racing Works",   {7, 8},     CLASS_HYPER, ERA_HYPER, TYPE_LMH,  "3.5 V6 twin-turbo hybrid (4WD)",  "engine_hyper_v6",       3000, 8500,  1120, 505,  0.010f, 1.002f, 1.011f, 4.99f, C(0.06f, 0.06f, 0.07f), false},
    {"regalvr8",     "Regal",    "VR-8",        "Regal Motorsport",      {12, 38},   CLASS_HYPER, ERA_HYPER, TYPE_LMDH, "5.5 V8 naturally aspirated hybrid", "engine_hyper_v8_na",  2800, 8600,  1108, 506,  0.020f, 0.995f, 1.008f, 5.08f, C(1.00f, 0.78f, 0.05f), false},
    {"isarmh8",      "Isar",     "MH-8",        "Isar Werks Team",       {15, 20},   CLASS_HYPER, ERA_HYPER, TYPE_LMDH, "4.0 V8 twin-turbo hybrid",        "engine_hyper_v8_turbo", 2900, 8500,  1110, 506,  0.008f, 0.993f, 1.007f, 5.08f, C(0.95f, 0.95f, 0.97f), false},
    {"lionne9x9",    "Lionne",   "9X9",         "Lionne Sport",          {93, 94},   CLASS_HYPER, ERA_HYPER, TYPE_LMH,  "2.6 V6 twin-turbo hybrid (4WD)",  "engine_hyper_v6",       3300, 9500,  1104, 508, -0.010f, 0.990f, 1.006f, 4.99f, C(0.13f, 0.13f, 0.15f), false},
    {"montagne27",   "Montagne", "MA-27",       "Montagne Endurance",    {35, 36},   CLASS_HYPER, ERA_HYPER, TYPE_LMDH, "3.4 V6 twin-turbo hybrid",        "engine_hyper_v6",       3200, 9000,  1112, 506, -0.010f, 1.000f, 1.005f, 5.08f, C(0.04f, 0.22f, 0.70f), false},
    {"ashcombevalkyr","Ashcombe","Valkyr",      "Ashcombe Thunder",      {107, 109}, CLASS_HYPER, ERA_HYPER, TYPE_LMH,  "6.5 V12 naturally aspirated",     "engine_v12",            3500, 10800, 1108, 504,  0.004f, 1.006f, 1.003f, 4.99f, C(0.00f, 0.28f, 0.20f), false},
    {"origingx01",   "Origin",   "GX-01",       "Origin Racing",         {17, 19},   CLASS_HYPER, ERA_HYPER, TYPE_LMDH, "3.2 V8 twin-turbo hybrid",        "engine_hyper_v8_turbo", 3000, 9000,  1114, 505,  0.000f, 1.000f, 1.002f, 5.08f, C(0.05f, 0.09f, 0.22f), false},
    {"wokingw1",     "Woking",   "W-LMDh",      "Woking Hypercar Team",  {58, 59},   CLASS_HYPER, ERA_HYPER, TYPE_LMDH, "2.9 V6 twin-turbo hybrid",        "engine_hyper_v6",       3200, 9200,  1112, 504,  0.000f, 0.998f, 1.000f, 5.08f, C(1.00f, 0.48f, 0.04f), false},
    {"dearbornlmdh", "Dearborn", "Stallion LMDh","Dearborn Performance", {60, 61},   CLASS_HYPER, ERA_HYPER, TYPE_LMDH, "5.0 V8 naturally aspirated hybrid", "engine_hyper_v8_na",  2800, 8500,  1116, 504,  0.004f, 1.002f, 0.998f, 5.08f, C(0.10f, 0.20f, 0.55f), false},

    {"ardenza296",   "Ardenza",  "296 GT",      "Velo Corse",            {21, 54},   CLASS_GT3,   ERA_GT3,   TYPE_GT3,  "3.0 V6 twin-turbo",               "engine_gt_v6_turbo",    2500, 8000,  1350, 405,  0.008f, 1.000f, 1.007f, 4.60f, C(0.82f, 0.05f, 0.05f), false},
    {"zuffen911",    "Zuffen",   "9-R GT",      "Grello Motorsport",     {91, 92},   CLASS_GT3,   ERA_GT3,   TYPE_GT3,  "4.2 flat-6 naturally aspirated",  "engine_flat6",          2800, 9400,  1352, 398,  0.030f, 1.010f, 1.005f, 4.62f, C(0.72f, 0.85f, 0.08f), false},
    {"isarm4",       "Isar",     "M4 GT",       "Werks Team W",          {31, 46},   CLASS_GT3,   ERA_GT3,   TYPE_GT3,  "3.0 I6 twin-turbo",               "engine_i6_turbo",       2400, 7500,  1344, 404,  0.023f, 0.990f, 1.004f, 4.85f, C(0.06f, 0.10f, 0.30f), false},
    {"kentuckyz6",   "Kentucky", "Z6.R",        "Bluegrass Racing",      {33, 81},   CLASS_GT3,   ERA_GT3,   TYPE_GT3,  "5.5 V8 flat-plane",               "engine_v8_flatplane",   2600, 8800,  1360, 405,  0.030f, 1.000f, 1.004f, 4.72f, C(1.00f, 0.80f, 0.04f), false},
    {"ashcombevanguard","Ashcombe","Vanguard GT","Ashcombe Heartland",   {23, 27},   CLASS_GT3,   ERA_GT3,   TYPE_GT3,  "4.0 V8 twin-turbo",               "engine_gt_v8_turbo",    2300, 7500,  1358, 400,  0.006f, 1.000f, 1.003f, 4.70f, C(0.00f, 0.30f, 0.22f), false},
    {"woking720",    "Woking",   "W720 GT",     "Papaya Union",          {95, 69},   CLASS_GT3,   ERA_GT3,   TYPE_GT3,  "4.0 V8 twin-turbo flat-plane",    "engine_gt_v8_turbo",    2600, 8000,  1351, 403,  0.020f, 1.005f, 1.002f, 4.60f, C(1.00f, 0.48f, 0.04f), false},
    {"sterneamg",    "Sterne",   "AMG-R GT",    "Ferro Lince",           {61, 79},   CLASS_GT3,   ERA_GT3,   TYPE_GT3,  "6.2 V8 naturally aspirated",      "engine_v8_na",          2200, 7500,  1371, 402,  0.040f, 0.990f, 1.000f, 4.75f, C(0.72f, 0.74f, 0.78f), false},
    {"kotobukigr",   "Kotobuki", "GR-G GT",     "Akita Sport",           {78, 87},   CLASS_GT3,   ERA_GT3,   TYPE_GT3,  "4.0 V8 twin-turbo",               "engine_gt_v8_turbo",    2300, 7800,  1354, 404,  0.000f, 0.995f, 0.999f, 4.70f, C(0.95f, 0.95f, 0.95f), false},
    {"dearbornstallion","Dearborn","Stallion GT","Rail City Racing",     {77, 88},   CLASS_GT3,   ERA_GT3,   TYPE_GT3,  "5.4 V8 cross-plane",              "engine_v8_na",          2000, 7800,  1349, 410, -0.020f, 0.985f, 0.998f, 4.85f, C(0.92f, 0.92f, 0.94f), false},
    {"torotgt",      "Toro",     "T-GT3",       "Iron Toro",             {63, 85},   CLASS_GT3,   ERA_GT3,   TYPE_GT3,  "4.0 V8 twin-turbo hybrid-ready",  "engine_v8_flatplane",   2600, 9000,  1355, 402,  0.000f, 1.000f, 0.996f, 4.70f, C(0.40f, 0.80f, 0.10f), false},

    // career mode: your own team's car, one per class (name, number, livery and upgrades set by the career)
    {"careerhyper",  "Your",     "Hypercar",    "Your Team",             {0, 0},     CLASS_HYPER, ERA_HYPER, TYPE_LMDH, "3.0 V6 twin-turbo hybrid",        "engine_hyper_v6",       3200, 9000,  1118, 500,  0.000f, 1.000f, 0.995f, 5.08f, C(0.90f, 0.45f, 0.05f), false, 1.0f, true},
    {"careergt3",    "Your",     "LMGT3",       "Your Team",             {0, 0},     CLASS_GT3,   ERA_GT3,   TYPE_GT3,  "4.0 V8 twin-turbo",               "engine_gt_v8_turbo",    2400, 7800,  1365, 398,  0.000f, 1.000f, 0.995f, 4.70f, C(0.90f, 0.45f, 0.05f), false, 1.0f, true},
};
const int CAR_COUNT = sizeof(CARS) / sizeof(CARS[0]);
const int CAR_PICK = CAR_COUNT - ERA_COUNT;
int careerCarIndex(int era) { return CAR_PICK + (era < 0 ? 0 : era % ERA_COUNT); }
int eraFirstCar(int era) { return era * TEAMS_PER_ERA; }

// The 2027 FIA WEC circuits (tools/build_tracks.py): real layouts and heights, scaled to the official lap.
// Formula Legends' circuits (bacinger/f1-circuits) plus Le Mans and Fuji from OpenStreetMap.
//  id             name                                   location                     start  hours  laps  street floodlit
const TrackDef TRACKS[] = {
    {"lusail",      "Lusail International Circuit",       "Lusail, Qatar",               13.0f, 10.0f, 334, false, true},
    {"imola",       "Imola",                              "Emilia-Romagna, Italy",       13.0f, 6.0f,  207, false, false},
    {"silverstone", "Silverstone",                        "Northamptonshire, UK",        12.0f, 6.0f,  195, false, false},
    {"spa",         "Spa-Francorchamps",                  "Ardennes, Belgium",           13.5f, 6.0f,  146, false, false},
    {"lemans",      "Circuit de la Sarthe",               "Le Mans, France",             16.0f, 24.0f, 387, false, false},
    {"interlagos",  "Interlagos",                         "Sao Paulo, Brazil",           11.5f, 6.0f,  236, false, false},
    {"cota",        "Circuit of the Americas",            "Austin, Texas, USA",          12.0f, 6.0f,  182, false, false},
    {"fuji",        "Fuji Speedway",                      "Oyama, Shizuoka, Japan",      11.0f, 6.0f,  226, false, false},
    {"bahrain",     "Bahrain International Circuit",      "Sakhir, Bahrain",             14.0f, 8.0f,  250, false, true},
};
const int TRACK_COUNT = sizeof(TRACKS) / sizeof(TRACKS[0]);

const char* ERA_NAME[ERA_COUNT] = {"Hypercar", "LMGT3"};
const char* ERA_SHORT[ERA_COUNT] = {"HYPERCAR", "LMGT3"};
const char* ERA_ID[ERA_COUNT] = {"hyper", "gt3"};
const char* TYPE_NAME[3] = {"LMH", "LMDh", "LMGT3"};

static int T(const char* id) { for (int i = 0; i < TRACK_COUNT; i++) if (!strcmp(TRACKS[i].id, id)) return i; return 0; }
// the 2027 FIA WEC calendar: Qatar 1812 km (27 Mar), Imola 6 h (11 Apr), Silverstone 6 h (25 Apr),
// Spa 6 h (15 May), Le Mans 24 h (13 Jun), Sao Paulo 6 h (11 Jul), Lone Star Le Mans (12 Sep),
// Fuji 6 h (26 Sep), Bahrain 8 h (6 Nov). Both classes race every round.
const int ERA_CAL[ERA_COUNT][ERA_ROUNDS] = {
    {T("lusail"), T("imola"), T("silverstone"), T("spa"), T("lemans"), T("interlagos"), T("cota"), T("fuji"), T("bahrain")},
    {T("lusail"), T("imola"), T("silverstone"), T("spa"), T("lemans"), T("interlagos"), T("cota"), T("fuji"), T("bahrain")},
};
// start times (local): Qatar and Bahrain run into the night under floodlights, Le Mans through it on headlights
const float ERA_HOUR[ERA_COUNT][ERA_ROUNDS] = {
    {13, 13, 12, 13.5f, 16, 11.5f, 12, 11, 14},
    {13, 13, 12, 13.5f, 16, 11.5f, 12, 11, 14},
};
const char* ROUND_NAME[ERA_ROUNDS] = {"Qatar 1812 km", "6 Hours of Imola", "6 Hours of Silverstone", "6 Hours of Spa-Francorchamps",
                                      "24 Hours of Le Mans", "6 Hours of Sao Paulo", "Lone Star Le Mans", "6 Hours of Fuji", "8 Hours of Bahrain"};
// WEC points (the same in both classes); Le Mans pays double
float roundPointsK(int round) { return round == 4 ? 2.0f : 1.0f; }
bool trackInEra(int track, int era) { for (int k = 0; k < ERA_ROUNDS; k++) if (ERA_CAL[era][k] == track) return true; return false; }
int eraTrackCount(int) { return ERA_ROUNDS; }
int eraTrack(int era, int k) { return ERA_CAL[era][((k % ERA_ROUNDS) + ERA_ROUNDS) % ERA_ROUNDS]; }

// ---------------------------------------------------------------- loaders
bool Model::load(const char* path) {
    int size = 0;
    blob = sys::loadFile(path, &size);
    if (!blob) return false;
#if EMR_SWAP
    if (!endian::fixEmm(blob, size, true)) { sys::log("bad model %s\n", path); unload(); return false; }
#endif
    const u8* p = (const u8*)blob;
    if (memcmp(p, "EMM1", 4) != 0) { unload(); return false; }
    p += 4;
    axleF = axleR = wheelR = 0; wheelY = 0.86f; ownWheels = false; flags = 0;
    uint32_t n; memcpy(&n, p, 4); p += 4;
    memcpy(scale, p, 12); p += 12;
    memcpy(offset, p, 12); p += 12;
    lodCount = (int)(n > 4 ? 4 : n);
    sharedTex = 0;
    for (int i = 0; i < lodCount; i++) {
        uint32_t vc, ts, gl; memcpy(&vc, p, 4); memcpy(&ts, p + 4, 4); memcpy(&gl, p + 8, 4);
        if (i == 0) memcpy(&cabX, p + 12, 4);
        if (i == 1) memcpy(&panelY, p + 12, 4);
        if (i == 2) memcpy(&panelX, p + 12, 4);
        lod[i].glass = gl <= vc ? (int)gl : 0;
        p += 16;
        lod[i].tex.w = lod[i].tex.h = (int)ts;
        if (ts) { lod[i].tex.palette = (const u32*)p; p += 1024; lod[i].tex.pixels = p; p += ts * ts; }
        else if (i > 0 && lod[0].tex.pixels) { lod[i].tex = lod[0].tex; sharedTex |= (uint8_t)(1u << i); }   // shares level 0's
        else { lod[i].tex.palette = nullptr; lod[i].tex.pixels = nullptr; }   // no texture: borrows another model's
        lod[i].verts = (const ModelVertex*)p; lod[i].count = (int)vc; p += vc * sizeof(ModelVertex);
    }
    // extras (Vita models): 'EMX1', f32 axleF, axleR, wheelY, wheelR, u32 flags (bit 0: own wheels)
    if (p + 24 <= (const u8*)blob + size && !memcmp(p, "EMX1", 4)) {
        memcpy(&axleF, p + 4, 4); memcpy(&axleR, p + 8, 4); memcpy(&wheelY, p + 12, 4); memcpy(&wheelR, p + 16, 4);
        uint32_t fl; memcpy(&fl, p + 20, 4); ownWheels = fl & 1; flags = fl;
    }
    gfx::flushCache(blob, size);
    return true;
}
void Model::unload() { if (blob) sys::freeFile(blob); blob = nullptr; lodCount = 0; }

bool TexFile::load(const char* path) {
    int size = 0;
    blob = sys::loadFile(path, &size);
    if (!blob) return false;
    const u8* p = (const u8*)blob;
    if (memcmp(p, "TEX1", 4) != 0) { unload(); return false; }
#if EMR_SWAP
    if (!endian::fixTex(blob, size, true)) { unload(); return false; }
#endif
    uint32_t s; memcpy(&s, p + 4, 4);
    tex.w = tex.h = (int)s; tex.palette = (const u32*)(p + 16); tex.pixels = p + 16 + 1024;
    gfx::flushCache(blob, size);
    return true;
}
void TexFile::unload() { if (blob) sys::freeFile(blob); blob = nullptr; }

bool Font::load(const char* path) {
    int size = 0;
    blob = sys::loadFile(path, &size);
    if (!blob) return false;
    const u8* p = (const u8*)blob;
    bool v2 = memcmp(p, "FNT2", 4) == 0;
    if (!v2 && memcmp(p, "FNT1", 4) != 0) return false;
#if EMR_SWAP
    if (!endian::fixFont(blob, size, true)) return false;
#endif
    uint32_t w, h, lh; memcpy(&w, p + 4, 4); memcpy(&h, p + 8, 4); memcpy(&lh, p + 12, 4);
    texScale = v2 ? (int)(lh >> 16) : 1;
    if (texScale < 1) texScale = 1;
    lineH = (int)(lh & 0xFFFF);
    tex.w = (int)w; tex.h = (int)h; tex.palette = (const u32*)(p + 16); tex.pixels = p + 16 + 1024;
    memcpy(g, p + 16 + 1024 + w * h, sizeof g);
    gfx::flushCache(blob, size);
    return true;
}

int Font::width(const char* s, float scale) const {
    float x = 0;
    for (; *s; s++) { int c = (unsigned char)*s; if (c < 32 || c > 126) c = '?'; x += g[c - 32].adv * scale; }
    return (int)(x + 0.5f);
}

void Font::draw(float x, float y, const char* s, u32 color, float scale) const {
    if (!blob) return;
    gfx::Sprite2D buf[96];
    int n = 0;
    for (; *s; s++) {
        int c = (unsigned char)*s; if (c < 32 || c > 126) c = '?';
        const Glyph& q = g[c - 32];
        if (c != ' ') {
            buf[n++] = gfx::Sprite2D{x + q.xoff * scale, y, q.w * scale, q.h * scale, u(q.x), v(q.y),
                                     u(q.x + q.w), v(q.y + q.h), color};
            if (n == 96) { gfx::bindTexture(&tex); gfx::drawSprites2D(buf, n); n = 0; }
        }
        x += q.adv * scale;
    }
    gfx::bindTexture(&tex);
    gfx::drawSprites2D(buf, n);   // the whole string in one draw call
}
