#include "../src/setupeng.h"
#include "../src/trophies.h"
#include <algorithm>
// PC test program: runs the real game code with the software renderer.
//
//   fl_test menus <outdir>
//   fl_test race <track id> <car index> <laps> <opponents> <outdir> [shot times...]
//
// "race" lets the AI drive your car too, takes screenshots (chase, far, cockpit) at the given
// race times, writes 8 s of the engine mix to race_audio.wav and prints the lap times.
#include "../src/game.h"
#include "../src/endian.h"
#include <dirent.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>

namespace pc { extern float fakeWait; }
#ifdef EMR_HEADLESS
namespace perfstat { extern long calls, verts, arena, arenaMiss; }
#else
namespace perfstat { static long calls = 0, verts = 0, arena = 0, arenaMiss = 0; }
#endif
namespace pc { extern Pad pad; extern audio::MixFn mixFn; extern long trisDrawn, drawCalls, vertsSent; void saveShot(const char*); }

extern long statVerts[6];
static void shot(Game& g, const std::string& path, float dt = 1.0f / 60) {
    Pad p{};
    for (long& s : statVerts) s = 0;
    g.frame(dt, p);
    pc::saveShot(path.c_str());
    fprintf(stderr, "    vertices: track %ld  terrain %ld  props %ld  cars %ld  wheels %ld\n", statVerts[0], statVerts[1], statVerts[2], statVerts[3], statVerts[4]);
#ifdef EMR_HEADLESS
    fprintf(stderr, "  shot %s  (%ld draw calls, %ld vertices, arena %.1f MB%s, heap %.1f MB, update %.2f ms, draw %.2f ms)\n", path.c_str(), perfstat::calls, perfstat::verts, perfstat::arena / 1048576.0, perfstat::arenaMiss ? " FULL" : "", sys::memUsedKB() / 1024.0, g.lastUpdMs, g.lastDrawMs);
#else
    fprintf(stderr, "  shot %s  (%ld triangles, %ld vertices sent, %ld draw calls, heap %.1f MB)\n", path.c_str(), pc::trisDrawn, pc::vertsSent, pc::drawCalls, sys::memUsedKB() / 1024.0);
#endif
}

static void press(Game& g, bool Pad::*b) {
    Pad p{}; p.*b = true; g.frame(1.0f / 60, p);
    Pad q{}; g.frame(1.0f / 60, q);
}

static void writeWav(const char* path, const std::vector<int16_t>& s) {
    FILE* f = fopen(path, "wb");
    int n = (int)s.size() * 2;
    int hdr[] = {0x46464952, 36 + n, 0x45564157, 0x20746d66, 16, 0x00020001, 44100, 44100 * 4, 0x00100004, 0x61746164, n};
    fwrite(hdr, 4, 11, f);
    fwrite(s.data(), 2, s.size(), f);
    fclose(f);
}

// swapcheck <dir>...: every data file turned round to big-endian (as on the PS3) and back again. Each
// walk must end exactly at the end of the file, and the round trip must give back the same bytes.
static void listFiles(const std::string& dir, std::vector<std::string>& out) {
    DIR* d = opendir(dir.c_str());
    if (!d) return;
    while (dirent* e = readdir(d)) {
        std::string n = e->d_name;
        if (n == "." || n == "..") continue;
        std::string p = dir + "/" + n;
        if (DIR* sub = opendir(p.c_str())) { closedir(sub); listFiles(p, out); } else out.push_back(p);
    }
    closedir(d);
}
static int swapCheck(int argc, char** argv) {
    int checked = 0, bad = 0;
    for (int a = 2; a < argc; a++) {
        std::vector<std::string> files; listFiles(argv[a], files);
        std::sort(files.begin(), files.end());
        for (const std::string& path : files) {
            auto ends = [&](const char* x) { size_t k = strlen(x); return path.size() >= k && !path.compare(path.size() - k, k, x); };
            bool (*fix)(void*, size_t, bool) = nullptr;
            if (ends(".emm")) fix = endian::fixEmm;
            else if (ends(".tex")) fix = endian::fixTex;
            else if (ends("font.bin")) fix = endian::fixFont;
            else if (ends(".emt")) fix = endian::fixEmt;
            else if (ends(".lvy")) fix = endian::fixLvy;
            else if (ends(".raw")) fix = endian::fixRaw;
            if (!fix) continue;
            int size = 0;
            void* p = sys::loadFile(("./" + path).c_str(), &size);     // ("./": no HD/ swap)
            if (!p) { printf("swapcheck: can't read %s\n", path.c_str()); bad++; continue; }
            std::vector<uint8_t> orig((uint8_t*)p, (uint8_t*)p + size);
            bool there = fix(p, size, true);
            bool changed = memcmp(orig.data(), p, size) != 0;
            bool back = fix(p, size, false);
            bool same = memcmp(orig.data(), p, size) == 0;
            if (!there || !back || !same || !changed) { printf("swapcheck: %s FAILED (to BE %d, back %d, same %d, changed %d)\n", path.c_str(), there, back, same, changed); bad++; }
            checked++;
            sys::freeFile(p);
        }
    }
    printf("swapcheck: %d files, %d bad\n", checked, bad);
    return bad ? 1 : 0;
}

int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "usage: see source\n"); return 1; }
    sys::init();
    Game g;
    if (!g.init()) { fprintf(stderr, "init failed (run from the folder that holds DATA/)\n"); return 1; }
    g.autoAllowed = false;          // fixed time steps here: the automatic quality would react to them
    std::string mode = argv[1];
    g.asyncLoad = mode == "loading";   // (the tests load a session at once; "loading" tries the slideshow)
    if (mode == "swapcheck") return swapCheck(argc, argv);
    if (mode != "intro") { sound::musicFile(nullptr); g.screen = SCR_TITLE; }
    g.walkMenus = mode == "walk";   // (the old menus everywhere else: the scripts below press through the lists)
    if (mode == "intro") {
        // play the intro with the real audio clock and grab frames at the sync points
        std::string out = argv[2];
        std::vector<int16_t> wav;
        // a frame 0.6 s into each shot, and every cut against the beat grid in DATA/music/intro.txt
        float marks[] = {2.6f, 5.0f, 7.0f, 8.9f, 9.6f};       // the logo strobe
        const int NM = sizeof(marks) / sizeof(marks[0]);
        int mi = 0, lastShot = -1; float cutAt = -1;
        for (int f = 0; f < 60 * 80 && g.screen == SCR_INTRO; f++) {
            Pad q{}; g.frame(1.0f / 60, q);
            if (pc::mixFn) { std::vector<int16_t> b(735 * 2); pc::mixFn(b.data(), 735); if (f % 2 == 0) wav.insert(wav.end(), b.begin(), b.end()); }
            if (mi < NM && g.introPos >= marks[mi]) { char b[48]; snprintf(b, sizeof b, "/intro_%03d.ppm", (int)(marks[mi] * 10)); pc::saveShot((out + b).c_str()); mi++; }
            if (g.introShot != lastShot) {
                lastShot = g.introShot; cutAt = g.introPos;
                float beat = (g.introPos - g.introBeat0) / g.introPeriod;
                fprintf(stderr, "cut: shot %d at %.3f s of music = beat %.2f (bar %.2f)\n", g.introShot, g.introPos, beat, beat / 4);
            }
            if (cutAt >= 0 && g.introPos >= cutAt + 0.6f) { char b[48]; snprintf(b, sizeof b, "/intro_shot%02d.ppm", lastShot); pc::saveShot((out + b).c_str()); cutAt = -1; }
        }
        fprintf(stderr, "intro ended: screen %d at %.1f s of music\n", (int)g.screen, g.introPos);
        return 0;
    }
    if (mode == "attract") {
        // attract <outdir>: idle on the title until the attract movie starts, a frame from each shot,
        // then past the end of the song to see it loop, and a button press back to the title
        std::string out = argv[2];
        sound::musicFile(nullptr); g.screen = SCR_TITLE; g.idle = 0;
        int f = 0;
        for (; f < 60 * 40 && g.screen == SCR_TITLE; f++) { Pad q{}; g.frame(1.0f / 60, q); }
        fprintf(stderr, "attract started after %.1f s idle (screen %d)\n", f / 60.0f, (int)g.screen);
        int lastShot = -2, shotFrames = 0, n = 0, loops = 0; float lastPos = 0;
        for (f = 0; f < 60 * 225 && g.screen == SCR_INTRO; f++) {
            Pad q{}; g.frame(1.0f / 60, q);
            if (pc::mixFn) { std::vector<int16_t> b(735 * 2); pc::mixFn(b.data(), 735); }
            if (g.introPos < lastPos - 5) { loops++; fprintf(stderr, "looped at frame %d (music %.1f -> %.1f)\n", f, lastPos, g.introPos); }
            lastPos = g.introPos;
            if (g.introShot != lastShot) { lastShot = g.introShot; shotFrames = 0; }
            if (getenv("FL_VIDEO") && f < 60 * atoi(getenv("FL_VIDEO")) && f % 2 == 0) {
                char b[64]; snprintf(b, sizeof b, "/v_%05d.ppm", f / 2); pc::saveShot((out + b).c_str());
            }
            if (++shotFrames == 50 && loops == 0) {
                char b[64]; snprintf(b, sizeof b, "/attract_%03d_%05.1f.ppm", n++, g.introPos); pc::saveShot((out + b).c_str());
            }
        }
        fprintf(stderr, "after %.1f s: screen %d, loops %d, shot %d, music %.1f\n", f / 60.0f, (int)g.screen, loops, g.introShot, g.introPos);
        press(g, &Pad::cross);
        for (int i = 0; i < 30; i++) { Pad q{}; g.frame(1.0f / 60, q); }
        fprintf(stderr, "after a button: screen %d (title %d)\n", (int)g.screen, (int)SCR_TITLE);
        return 0;
    }
    if (mode == "hq") {
        // hq <outdir>: headquarters (each era), pick 2016, the main menu, the showroom (two teams, turning)
        std::string out = argv[2];
        auto run = [&](float secs) { for (int i = 0; i < (int)(secs * 30); i++) { Pad q{}; g.frame(1.0f / 30, q); } };
        auto shot = [&](const char* name) { pc::saveShot((out + "/" + name + ".ppm").c_str()); };
        auto tap = [&](bool Pad::*b) { Pad q{}; q.*b = true; g.frame(1.0f / 30, q); Pad z{}; g.frame(1.0f / 30, z); };
        sound::musicFile(nullptr); g.screen = SCR_HQ; g.hqChosen = false; g.hqSel = 0; g.t = 0;
        run(1.5f); shot("hq_0");
        for (int e = 1; e < ERA_COUNT; e++) { tap(&Pad::right); run(1.5f); char n[16]; snprintf(n, sizeof n, "hq_%d", e); shot(n); }
        tap(&Pad::left); run(1.0f); tap(&Pad::cross); run(1.5f);
        fprintf(stderr, "picked era %d, screen %d (title %d), car %d (era %d), track %s\n", g.era, (int)g.screen, (int)SCR_TITLE, g.set.car, CARS[g.set.car].era, TRACKS[g.set.track].name);
        shot("menu");
        // to the showroom (SHOWROOM is the 5th item)
        for (int i = 0; i < 4; i++) tap(&Pad::down);
        tap(&Pad::cross); run(2.5f); shot("show_0");
        tap(&Pad::right); run(2.0f); shot("show_1");
        { Pad q{}; q.rstickX = 1; for (int i = 0; i < 40; i++) g.frame(1.0f / 30, q); } run(0.8f); shot("show_2");
        tap(&Pad::r); run(1.5f); shot("show_3");
        tap(&Pad::circle); run(1.0f);
        tap(&Pad::circle); run(1.5f);
        fprintf(stderr, "after O, O: screen %d (hq %d)\n", (int)g.screen, (int)SCR_HQ);
        return 0;
    }
    if (mode == "duck") {
        // duck: an engine at steady revs, then a radio call; the engine's level before, during and after
        sound::musicFile(nullptr);
        sound::setVoice(0, &CARS[eraFirstCar(ERA_HYPER)], 12000, 0.8f, 0, false, 1);
        std::vector<int16_t> b(735 * 2);
        auto level = [&](int frames60) {
            double e = 0; int n = 0;
            for (int f = 0; f < frames60; f++) { pc::mixFn(b.data(), 735); for (int16_t v : b) { e += (double)v * v; n++; } }
            return sqrt(e / n);
        };
        double before = level(60);
        sound::radio("16_box");
        double start = level(3);
        double during = level(40);
        while (sound::radioBusy()) level(1);
        double just = level(6);
        double after = level(90);
        printf("engine level: before %.0f, first 50 ms of the call %.0f, during the call %.0f (engine+voice), just after %.0f, 1.5 s after %.0f\n", before, start, during, just, after);
        sound::radio("16_box"); level(5);
        sound::setVoice(0, nullptr, 0, 0, 0, false);
        double voiceOnly = level(30);
        printf("voice alone %.0f\n", voiceOnly);
        return 0;
    }
    if (mode == "tour") {
        // tour <track id> <outdir> [n]: n frames round the lap (no cars), each from 14 m up and 30 m back
        // looking along the road, plus a high oblique one every 4th, to find gaps, overlaps and clipping
        std::string tid = argv[2], out = argv[3]; int n = argc > 4 ? atoi(argv[4]) : 24;
        float s0 = argc > 5 ? atof(argv[5]) : 0, s1 = argc > 6 ? atof(argv[6]) : -1;   // (a stretch only)
        int tr = 0; for (int i = 0; i < TRACK_COUNT; i++) if (tid == TRACKS[i].id) tr = i;
        g.set.track = tr; g.set.car = eraFirstCar(ERA_HYPER); g.set.weather = WX_DRY; g.ttMode = true;
        g.startSession(SES_PRACTICE);
        Race& r = g.race; for (auto& c : r.cars) c.pos = V3(0, 0, -500);
        const Track& T = r.track;
        for (int k = 0; k < n; k++) {
            for (int high = 0; high < 2; high++) {
                if (high && k % 4) continue;
                float s = s1 > s0 ? s0 + (s1 - s0) * k / n : T.length * k / n;
                V3 p, t, l; float hw; T.frame(s, p, t, l, hw);
                V3 p2, t2, l2; float hw2; T.frame(T.wrap(s + 60), p2, t2, l2, hw2);
                Camera c;
                if (!high) {
                    float back = getenv("FL_TOUR_LOW") ? 12 : 30, up = getenv("FL_TOUR_LOW") ? 3.0f : 14;
                    c.eye = p - t * back + V3(0, 0, up); c.fwd = norm(p2 - c.eye);
                }
                else { c.eye = p - t * 120 + l * 120 + V3(0, 0, 110); c.fwd = norm(p - c.eye); }
                c.up = V3(0, 0, 1); c.fov = 60;
                gfx::beginFrame(g.R.skyColour(r));
                g.R.drawScene(r, c, false);
                gfx::endFrame();
                char b[96]; snprintf(b, sizeof b, "/%s_%02d%s_%05.0fm.ppm", tid.c_str(), k, high ? "air" : "", s);
                pc::saveShot((out + b).c_str());
            }
        }
        return 0;
    }
    if (mode == "physlap") {
        // physlap <track id> <era 0-3> <laps>: the AI drives the era's first car alone (time trial
        // rules, dry), no drawing; lap times and how often the car went light / left the ground
        std::string tid = argv[2]; int era = atoi(argv[3]), laps = atoi(argv[4]);
        int tr = 0; for (int i = 0; i < TRACK_COUNT; i++) if (tid == TRACKS[i].id) tr = i;
        g.ttMode = true; g.set.track = tr; g.set.car = eraFirstCar(era); g.aiDrivesPlayer = true; g.set.weather = WX_DRY;
        if (const char* ch = getenv("EMR_CH")) sscanf(ch, "%d %d %d", &g.race.playerChassis.ride, &g.race.playerChassis.diff, &g.race.playerChassis.press);
        g.startSession(SES_PRACTICE);
        g.race.toGarage(); g.race.flyingLap(g.set.setup);
        Race& r = g.race;
        float simT = 0, light = 0, minLoad = 9, maxAir = 0, maxLift = 0; int lastLap = 0; float best = 1e9f;
        double tSum = 0, pSum = 0; int nS = 0;
        while (r.player().lap < laps && simT < 1500) {
            r.update(1.0f / 60, 0, 0, 0, 0, true); simT += 1.0f / 60;
            r.sfxEvents.clear();
            Car& me = r.player();
            if (me.v > 20) { if (me.body.load < 0.5f) light += 1.0f / 60; minLoad = fminf(minLoad, me.body.load); maxAir = fmaxf(maxAir, me.body.air); maxLift = fmaxf(maxLift, me.body.hOff); }
            if (getenv("FL_TRACE") && me.lap == 2) {
                float a = 0, b = 0; sscanf(getenv("FL_TRACE"), "%f %f", &a, &b);
                if (me.s >= a && me.s <= b) printf("trace %.1f %.3f %.4f %.4f\n", me.s, me.body.load, me.body.hOff, me.body.pitch);
            }
            if (getenv("FL_DBG") && me.v > 20 && me.body.load < 0.6f && me.lap == 2) {
                int i = r.track.index(me.s);
                fprintf(stderr, "s %6.0f d %5.1f hw %4.1f kerb %d v %3.0f load %.2f hOff %+.3f vz %+.2f grade %+.3f\n", me.s, me.d, r.track.pts[i].hw, (int)r.track.pts[i].kerb, me.v * 3.6f, me.body.load, me.body.hOff, me.body.vz, r.track.gradeAt(me.s));
            }
            if (me.lap >= 2) { tSum += me.tyreTemp(); pSum += 0.25f * (r.tyrePressure(me, 0) + r.tyrePressure(me, 1) + r.tyrePressure(me, 2) + r.tyrePressure(me, 3)); nS++; }
            if (me.lap != lastLap) { lastLap = me.lap; if (me.lap >= 2) best = fminf(best, me.lastLap); }
            if (getenv("FL_PARTS")) { static int mx[5], fr = 0; int n[5] = {}; for (auto& q : r.parts) n[q.kind]++; for (int k = 0; k < 5; k++) mx[k] = n[k] > mx[k] ? n[k] : mx[k];
                if (++fr % 3600 == 0) fprintf(stderr, "particles now/max: smoke %d/%d spray %d/%d shard %d/%d spark %d/%d flame %d/%d\n", n[0], mx[0], n[1], mx[1], n[2], mx[2], n[3], mx[3], n[4], mx[4]); }
        }
        printf("%-12s era %d  best %.3f  light %.2f s  min load %.2f  max air %.2f s  max rise %.2f m\n", tid.c_str(), era, best, light, minLoad, maxAir, maxLift);
        {   const Car& me = g.race.player();
            printf("   on the timed laps: tyres %.1f C, %.3f bar on average\n", nS ? tSum / nS : 0, nS ? pSum / nS : 0);
            printf("   sausage kerbs hit %d  bottomed %d times  tyres %.0f %.0f %.0f %.0f C  hot %.2f %.2f %.2f %.2f bar  wear %.3f %.3f %.3f %.3f  air %.0f track %.0f C\n",
                   g.race.sausageHits, g.race.bottomEvents, me.tyreT[0], me.tyreT[1], me.tyreT[2], me.tyreT[3],
                   g.race.tyrePressure(me, 0), g.race.tyrePressure(me, 1), g.race.tyrePressure(me, 2), g.race.tyrePressure(me, 3),
                   me.tyreWear[0], me.tyreWear[1], me.tyreWear[2], me.tyreWear[3], g.race.airNow, g.race.trackNow); }
        return 0;
    }
    if (mode == "barriers") {   // barrier lines of a circuit (x y per point, left then right), with / without the run-off cut
        std::string tid = argv[2]; FILE* f = fopen(argv[3], "w");
        for (int cut = 0; cut < 2; cut++) {
            Track::noRunoffCut = cut == 0;
            Track t; for (int i = 0; i < TRACK_COUNT; i++) if (tid == TRACKS[i].id) t.load(&TRACKS[i]);
            for (auto& q : t.pts) {
                V3 l = q.p + q.n * (q.hw + q.outL), r = q.p - q.n * (q.hw + q.outR);
                fprintf(f, "%d %.2f %.2f %.2f %.2f %.2f %.2f %.2f\n", cut, q.p.x, q.p.y, q.hw, l.x, l.y, r.x, r.y);
            }
        }
        fclose(f); return 0;
    }
    if (mode == "overlaps") {   // fl_test overlaps all -- CI: every circuit -- barriers folded or on another road, terrain through the road,
                                // trackside objects on a road. Prints one line per circuit; exit 1 if anything is wrong
        int fails = 0;
        if (getenv("FL_NOCUT")) Track::noRunoffCut = true;      // (to see the test catch something)
        for (int k = 0; k < TRACK_COUNT; k++) {
            Track t; t.load(&TRACKS[k]);
            int folds = 0, wallOnRoad = 0, ground = 0, propsOnRoad = 0;
            // barrier lines
            for (int side = 0; side < 2; side++) {
                float sg = side ? -1.0f : 1.0f;
                for (int i = 0; i < t.n; i++) {
                    const TrackPoint& a = t.pts[i]; const TrackPoint& b = t.pts[(i + 1) % t.n];
                    V3 pa = a.p + a.n * (sg * (a.hw + (side ? a.outR : a.outL))), pb = b.p + b.n * (sg * (b.hw + (side ? b.outR : b.outL)));
                    if ((pb.x - pa.x) * a.t.x + (pb.y - pa.y) * a.t.y < 0) folds++;
                    for (int j = 0; j < t.n; j++) {
                        int dij = abs(i - j); dij = dij > t.n / 2 ? t.n - dij : dij;
                        if (dij * t.step < 30 || fabsf(t.pts[j].p.z - a.p.z) > 4) continue;   // (bridges pass over)
                        float dx = pa.x - t.pts[j].p.x, dy = pa.y - t.pts[j].p.y, r = t.pts[j].hw - 0.3f;
                        if (dx * dx + dy * dy < r * r) { wallOnRoad++; break; }
                    }
                }
            }
            // terrain mesh against the road and verges
            for (int i = 0; i < t.n; i++) {
                const TrackPoint& q = t.pts[i];
                for (float f = -1; f <= 1.001f; f += 0.25f) { V3 p = q.p + q.n * (f * (q.hw + 2)); if (t.meshZ(p.x, p.y) > p.z) ground++; }
            }
            // objects (not the gantries and bridges, which span the road)
            for (const PropInst& pr : t.props) {
                if (pr.model == PROP_GANTRY || pr.model == PROP_BRIDGE) continue;
                for (int j = 0; j < t.n; j++) {
                    float dx = pr.p.x - t.pts[j].p.x, dy = pr.p.y - t.pts[j].p.y, r = t.pts[j].hw + 0.3f;
                    if (fabsf(pr.p.z - t.pts[j].p.z) < 6 && dx * dx + dy * dy < r * r) { propsOnRoad++; break; }
                }
            }
            bool ok = !folds && !wallOnRoad && !ground && !propsOnRoad;
            if (!ok) fails++;
            printf("%-12s %s  barrier folds %d, barrier on a road %d, terrain through the road %d, objects on a road %d\n",
                   TRACKS[k].id, ok ? "ok  " : "FAIL", folds, wallOnRoad, ground, propsOnRoad);
        }
        return fails ? 1 : 0;
    }
    if (mode == "terrain") {    // where the terrain mesh comes up through the road or the verges: fl_test terrain <track|all>
        std::string tid = argv[2];
        Track::keepGround = true;
        for (int k = 0; k < TRACK_COUNT; k++) {
            if (tid != "all" && tid != TRACKS[k].id) continue;
            Track t; t.load(&TRACKS[k]);
            auto meshZ = [&](float x, float y) {       // the two triangles of a cell, as the strips draw them
                float fx = (x - t.gx0) / t.gsub, fy = (y - t.gy0) / t.gsub;
                fx = clampf(fx, 0, t.gtnx - 1.001f); fy = clampf(fy, 0, t.gtny - 1.001f);
                int ix = (int)fx, iy = (int)fy; float u = fx - ix, v = fy - iy;
                float a = t.groundTz[iy * t.gtnx + ix], b = t.groundTz[iy * t.gtnx + ix + 1];
                float c = t.groundTz[(iy + 1) * t.gtnx + ix], d = t.groundTz[(iy + 1) * t.gtnx + ix + 1];
                // quad (x,y) (x,y+1) (x+1,y+1) (x+1,y): split along (x,y+1)-(x+1,y)
                return u + v <= 1 ? a + (b - a) * u + (c - a) * v : d + (c - d) * (1 - u) + (b - d) * (1 - v);
            };
            if (getenv("FL_TERRAIN_DUMP")) {
                // the mesh heights, the elevation model under them, and the centre line, for a picture
                FILE* f = fopen(getenv("FL_TERRAIN_DUMP"), "w");
                fprintf(f, "%d %d %f %f %f\n", t.gtnx, t.gtny, t.gx0, t.gy0, t.gsub);
                for (int y = 0; y < t.gtny; y++) for (int x = 0; x < t.gtnx; x++)
                    fprintf(f, "%.2f %.2f\n", t.groundTz[y * t.gtnx + x], t.terrainZ(t.gx0 + x * t.gsub, t.gy0 + y * t.gsub));
                for (int i = 0; i < t.n; i++) fprintf(f, "P %.1f %.1f %.2f\n", t.pts[i].p.x, t.pts[i].p.y, t.pts[i].p.z);
                fclose(f);
            }
            int bad = 0; float worst = 0; float worstS = 0;
            for (int i = 0; i < t.n; i++) {
                const TrackPoint& q = t.pts[i];
                for (float f = -1; f <= 1.001f; f += 0.25f) {
                    float d = f * (q.hw + 2);
                    V3 p = q.p + q.n * d;
                    float over = meshZ(p.x, p.y) - p.z;
                    if (over > 0.0f) { bad++; if (over > worst) { worst = over; worstS = i * t.step; } }
                }
            }
            fprintf(stderr, "%-9s terrain above the road at %d of %d samples, worst %.2f m at %.0f m\n", TRACKS[k].id, bad, t.n * 9, worst, worstS);
        }
        return 0;
    }
    if (mode == "verge") {      // the player's car parked on the steepest verges of a circuit: fl_test verge <track#> <out>
        std::string out = argv[3];
        Track::noGroundCap = getenv("EMR_NOCAP") != nullptr;
        g.set.track = atoi(argv[2]); g.set.car = 1; g.set.raceMin = 30; g.aiDrivesPlayer = true;
        g.startRace();
        for (int i = 0; i < 120; i++) { Pad q{}; g.step(1.0f / 30, q); }
        Track& t = g.race.track;
        std::vector<std::pair<float, int>> steep;
        for (int i = 0; i < t.n; i += 4) for (int side = 0; side < 2; side++) {
            float sg = side ? -1.0f : 1.0f, w = side ? t.pts[i].outR : t.pts[i].outL;
            if (w < 6) continue;
            steep.push_back({-fabsf(t.groundAt(i * t.step, sg * (t.pts[i].hw + w - 1))), i * 2 + side});
        }
        std::sort(steep.begin(), steep.end());
        if (getenv("EMR_AT")) { steep.clear(); steep.push_back({0, (int)(atof(getenv("EMR_AT")) / t.step) * 2}); }
        for (int k = 0; k < 3 && k < (int)steep.size(); k++) {
            int i = steep[k].second / 2, side = steep[k].second % 2;
            float sg = side ? -1.0f : 1.0f, w = side ? t.pts[i].outR : t.pts[i].outL;
            Car& me = g.race.player();
            me.s = i * t.step; me.d = getenv("EMR_AT") ? 0 : sg * (t.pts[i].hw + w * 0.6f); me.v = 0; me.yaw = 0.35f * sg; me.lift = 0;
            g.race.pose(me);
            g.cam = CAM_CHASE; g.R.camInit = false;
            float sl = 0; float z = t.groundAt(me.s, me.d, &sl);
            fprintf(stderr, "verge at %.0f m (%s): car %.2f m above the road, slope %.2f\n", me.s, side ? "right" : "left", z, sl);
            char bf[64]; snprintf(bf, sizeof bf, "/verge_%d.ppm", k);
            g.race.pose(me); shot(g, out + bf, 0.0001f);
        }
        return 0;
    }
    if (mode == "autoq") {      // automatic quality: slow frames lower the detail, fast ones bring it back
        g.autoAllowed = true; cfg.autoQuality = true; cfg.setPreset(0); cfg.apply();
        g.set.track = 1; g.set.car = 1; g.set.raceMin = 30; g.aiDrivesPlayer = true;
        g.startRace();
        auto run = [&](float secs, float dt, float wait) {
            pc::fakeWait = wait;
            for (float t = 0; t < secs; t += dt) { Pad q{}; g.step(dt, q); g.autoQualityStep(dt); }
            fprintf(stderr, "  %.0f s at %.0f fps (waiting %.1f ms a frame): level %d  distance %.2f scenery %d effects %d\n",
                    secs, 1 / dt, wait * 1000, g.autoLevel, perf.distance, (int)perf.scenery, (int)perf.effects);
        };
        run(6, 1.0f / 45, 0); run(6, 1.0f / 45, 0); run(6, 1.0f / 58, 0);
        run(14, 1.0f / 60, 0.010f); run(14, 1.0f / 60, 0.010f); run(30, 1.0f / 60, 0.010f);
        fprintf(stderr, "messages: %s\n", g.race.msgs.empty() ? "" : g.race.msgs[0].text);
        return 0;
    }
    if (mode == "bench") {      // the benchmark scenes (what bench.txt starts on the PSP), perf.log written in the current folder
        g.bench = true; remove("perf.log");
        float simT = 0;
        while (!g.benchDone && simT < 400) { Pad q{}; g.frame(1.0f / 30, q); simT += 1.0f / 30; }
        fprintf(stderr, "bench done %d after %.0f s\n", (int)g.benchDone, simT);
        return 0;
    }
    if (mode == "touchskip") {   // the songs can't be skipped: main-menu corner taps, L / R change nothing; headquarters plays only its own theme
        int bad = 0;
        auto settle = [&](int n) { for (int i = 0; i < n; i++) { Pad q{}; g.frame(1.0f / 30, q); } };
        auto tap = [&](float x, float y) { Pad q{}; q.touches = 1; q.tx[0] = x; q.ty[0] = y; g.frame(1.0f / 30, q); settle(3); };
        g.screen = SCR_HQ; g.t = 1;
        for (int e = 0; e < ERA_COUNT; e++) {
            g.hqSel = e; settle(10);
            printf("headquarters, %s looked at: playing '%s'\n", ERA_SHORT[e], sound::musicForced());
            if (strncmp(sound::musicForced(), "hq_", 3)) bad++;
        }
        // no skipping the era themes at headquarters: corner taps change nothing
        {
            std::string before = sound::musicForced(); float t0 = sound::trackTime();
            Pad q{}; q.touches = 1; q.tx[0] = 470; q.ty[0] = 10; g.frame(1.0f / 30, q); settle(3);
            q.tx[0] = 10; g.frame(1.0f / 30, q); settle(3);
            settle(3);
            bool same = before == sound::musicForced() && sound::trackTime() >= t0;
            printf("headquarters, after taps and skips: '%s' %s\n", sound::musicForced(), same ? "(still playing)" : "(CHANGED)");
            if (!same) bad++;
        }
        g.chooseEra(ERA_COUNT - 1); g.screen = SCR_TITLE; g.sel = 0; g.t = 1; settle(10);
        printf("main menu: playing '%s' (playlist), now playing '%s'\n", sound::musicForced(), sound::nowPlaying());
        if (sound::musicForced()[0]) bad++;
        std::string a = sound::nowPlaying();
        tap(470, 10); std::string b = sound::nowPlaying();
        tap(10, 10);  std::string c = sound::nowPlaying();
        tap(240, 150); std::string d = sound::nowPlaying();
        { Pad q{}; q.l = true; g.frame(1.0f / 30, q); settle(3); q = Pad{}; q.r = true; g.frame(1.0f / 30, q); settle(3); }
        std::string e2 = sound::nowPlaying();
        printf("top-right: '%s' -> '%s'\ntop-left:  -> '%s'\nmiddle:    -> '%s'\nL / R:     -> '%s'\n", a.c_str(), b.c_str(), c.c_str(), d.c_str(), e2.c_str());
        if (b != a || c != a || d != a || e2 != a) bad++;
        printf("%s\n", bad ? "NO-SKIP FAILED (a song changed)" : "no skipping: ok");
        return bad ? 1 : 0;
    }
    if (mode == "eralock") {   // each era from headquarters: its circuits, its teams, its own career and championship
        int bad = 0;
        auto settle = [&](int n = 10) { for (int i = 0; i < n; i++) { Pad q{}; g.frame(1.0f / 30, q); } };
        for (int e = 0; e < ERA_COUNT; e++) { remove(eraSave("career", e)); remove(eraSave("champ", e)); }
        for (int e = 0; e < ERA_COUNT; e++) {
            g.chooseEra(e); g.screen = SCR_TITLE; g.t = 1; settle();
            // quick race: the circuit list, left / right / L / R do nothing to the era
            g.sel = 0; press(g, &Pad::cross); settle();
            press(g, &Pad::right); press(g, &Pad::l); press(g, &Pad::r); press(g, &Pad::left);
            if (g.screen != SCR_TRACK || g.era != e) { printf("era %d: circuit screen era %d\n", e, g.era); bad++; }
            for (int k = 0; k < ERA_ROUNDS; k++) { g.sel = k; settle(1); if (!trackInEra(eraTrack(g.era, k), e)) bad++; }
            press(g, &Pad::cross); settle();
            press(g, &Pad::l); press(g, &Pad::r);
            if (g.screen != SCR_CAR || CARS[g.sel].era != e) { printf("era %d: team screen car era %d\n", e, CARS[g.sel].era); bad++; }
            press(g, &Pad::circle); press(g, &Pad::circle); settle();
            // a new career here: it is this era's, in its own file
            g.screen = SCR_TITLE; g.sel = 2; press(g, &Pad::cross); settle();
            if (g.screen != SCR_CAREER_NEW) { printf("era %d: no new-career screen (%d)\n", e, g.screen); bad++; }
            g.sel = 4; press(g, &Pad::cross); settle();
            if (g.career.cls != e) { printf("era %d: career era %d\n", e, g.career.cls); bad++; }
            FILE* f = fopen(eraSave("career", e), "rb"); if (f) fclose(f); else { printf("era %d: no %s\n", e, eraSave("career", e)); bad++; }
            g.screen = SCR_TITLE; g.careerMode = false;
            printf("era %s: circuits, teams and career ok so far (%d problems)\n", ERA_SHORT[e], bad);
        }
        // and each era's career comes back as its own
        for (int e = 0; e < ERA_COUNT; e++) {
            g.chooseEra(e); g.screen = SCR_TITLE; g.sel = 2; press(g, &Pad::cross); settle();
            if (g.screen != SCR_CAREER || g.career.cls != e) { printf("era %d: reloaded career era %d screen %d\n", e, g.career.cls, g.screen); bad++; }
            g.screen = SCR_TITLE; g.careerMode = false;
        }
        for (int e = 0; e < ERA_COUNT; e++) remove(eraSave("career", e));
        printf("%s\n", bad ? "ERA LOCK FAILED" : "era lock ok");
        return bad ? 1 : 0;
    }
    if (mode == "menuidle") {     // menuidle <seconds>: the heap while standing on the title
        sound::musicFile(nullptr);
        g.era = argc > 3 ? atoi(argv[3]) : ERA_GT3; g.set.car = eraFirstCar(g.era) + 5; g.hqChosen = true; g.screen = SCR_TITLE;
        int secs = argc > 2 ? atoi(argv[2]) : 20;
        for (int i = 0; i < 30 * secs; i++) { Pad q{}; g.frame(1.0f / 30, q); g.idle = 0;
            if (i % 30 == 0) printf("menuidle %2d s: heap %.1f MB, cars held %d, drawn %d, screen %d, draw %.1f ms, calls %ld\n", i / 30, sys::memUsedKB() / 1024.0, g.R.carsLoadedCount(), g.R.carsDrawn, (int)g.screen, g.lastDrawMs, perfstat::calls); }
        return 0;
    }
    if (mode == "attractperf") {   // attractperf [seconds]: the attract movie's cost per frame (cars, calls, vertices, sim steps, loads)
        sound::musicFile(nullptr);
        g.era = ERA_GT3; g.set.car = eraFirstCar(ERA_GT3) + 5; g.hqChosen = true; g.screen = SCR_TITLE;
        if (getenv("EMR_INTRO_LITE")) g.introLite = true;
        int script = argc > 3 ? atoi(argv[3]) : 1;       // 1 the attract movie, 0 the boot intro
        g.idle = 0;
        if (script == 0) g.introBegin(0);
        else for (int i = 0; i < 60 * 40 && g.screen == SCR_TITLE; i++) { Pad q{}; g.frame(1.0f / 60, q); }   // idle until it starts
        int secs = argc > 2 ? atoi(argv[2]) : 60;
        int slow = 0; double loadMs = 0, loadMax = 0;
        double up = 0, dr = 0, upMax = 0, drMax = 0, calls = 0, verts = 0, cars = 0, held = 0; int n = 0, loads = 0, lastBlock = g.introBlock, heldMax = 0;
        for (int f = 0; f < 60 * secs && g.screen == SCR_INTRO; f++) {
            Pad q{}; g.frame(1.0f / 60, q);
            if (pc::mixFn) { std::vector<int16_t> b(735 * 2); pc::mixFn(b.data(), 735); }
            if (g.introBlock != lastBlock) { loads++; lastBlock = g.introBlock; loadMs += g.lastUpdMs; loadMax = std::max(loadMax, g.lastUpdMs); continue; }
            if (g.lastUpdMs > 16.7) slow++;
            if (g.lastDrawMs > 400) printf("attractperf: long frame %d: draw %.0f ms, update %.0f ms, shot %d, music %.2f s\n", f, g.lastDrawMs, g.lastUpdMs, g.introShot, g.introPos);
            if (f < 10) continue;
            up += g.lastUpdMs; dr += g.lastDrawMs; upMax = std::max(upMax, g.lastUpdMs); drMax = std::max(drMax, g.lastDrawMs);
            calls += perfstat::calls; verts += perfstat::verts; cars += g.R.carsDrawn; held += g.R.carsLoadedCount(); heldMax = std::max(heldMax, g.R.carsLoadedCount()); n++;
        }
        printf("attractperf: music at %.1f s, shot %d\n", g.introPos, g.introShot);
        printf("attractperf: %d frames: update %.2f ms (max %.2f), draw %.2f ms (max %.2f), calls %.0f, verts %.0f, cars drawn %.1f, cars loaded %.1f (max %d), circuit loads %d (%.0f ms, longest %.0f ms), updates over 16.7 ms %d, field %d cars, heap %.1f MB\n",
               n, up / n, upMax, dr / n, drMax, calls / n, verts / n, cars / n, held / n, heldMax, loads, loadMs, loadMax, slow, (int)g.race.cars.size(), sys::memUsedKB() / 1024.0);
        return 0;
    }
    if (mode == "garagesight") {   // garagesight: our garage on every circuit and class: the monitors' sight lines clear
        int bad = 0;
        for (int tr = 0; tr < TRACK_COUNT; tr++) for (int era = 0; era < ERA_COUNT; era++) {
            g.set.track = tr; g.set.car = eraFirstCar(era) + (tr % TEAMS_PER_ERA); g.set.weekend = true; g.set.sessionMin = 30; g.walkMenus = true;
            g.startRace();
            for (int i = 0; i < 10; i++) { Pad q{}; g.frame(1.0f / 30, q); }
            if (g.screen != SCR_WALK) { printf("garagesight: %s era %d not on foot (screen %d)\n", TRACKS[tr].id, era, (int)g.screen); bad++; continue; }
            int b = g.walkSightBlocked();
            if (b) { printf("garagesight: FAIL %s era %d: %d blocked\n", TRACKS[tr].id, era, b); bad++; }
            g.leaveRace(); g.screen = SCR_TITLE;
        }
        printf("garagesight: %s\n", bad ? "FAIL" : "ok");
        return bad ? 1 : 0;
    }
    if (mode == "practiceruns") {
        // practiceruns <track id> [outdir]: practice then qualifying (30 min each), the player in the garage.
        // The AI must come and go: runs of an out lap, 2-5 flying laps (some long runs in practice), an
        // in lap, minutes in the garage; qualifying few out early, a rush late. Screenshots of the timing.
        std::string tid = argc > 2 ? argv[2] : "spa";
        std::string out = argc > 3 ? argv[3] : "";
        for (int i = 0; i < TRACK_COUNT; i++) if (tid == TRACKS[i].id) g.set.track = i;
        g.set.car = 0; g.set.opponents = 19; g.set.weekend = true; g.set.sessionMin = 30; g.walkMenus = true;
        int bad = 0;
        for (int ses = 0; ses < 2; ses++) {
            if (ses == 0) g.startRace(); else { g.leaveRace(); g.startSession(SES_QUALI); }
            for (int i = 0; i < 10; i++) { Pad q{}; g.frame(1.0f / 30, q); }
            Race& r = g.race;
            int n = (int)r.cars.size();
            std::vector<int> runs(n, 0), wasStop(n, 1); std::vector<int> lapsAtOut(n, 0); std::vector<std::vector<int>> runLaps(n);
            int samples = 0, minOut = 99, maxOut = 0, outEarly = 0, outLate = 0, nEarly = 0, nLate = 0; double outSum = 0;
            bool shotTaken = false;
            float L = r.sessionLength;
            for (float t = 0; t < L - 5; t += 1.0f / 30) {
                r.update(1.0f / 30, 0, 0, 0, 0, false); r.sfxEvents.clear();
                for (int k = 0; k < n; k++) {
                    const Car& c = r.cars[k]; if (c.player) continue;
                    int st = c.pit == PIT_STOP ? 1 : 0;
                    if (wasStop[k] && !st) { runs[k]++; lapsAtOut[k] = c.lap; }
                    if (!wasStop[k] && st) runLaps[k].push_back(c.lap - lapsAtOut[k]);
                    wasStop[k] = st;
                }
                if (fmodf(t, 10.0f) < 1.0f / 30) {
                    int outN = 0; for (const Car& c : r.cars) if (!c.player && c.pit != PIT_STOP) outN++;
                    if (t > 300) { samples++; outSum += outN; minOut = std::min(minOut, outN); maxOut = std::max(maxOut, outN); }
                    if (t < L * 0.3f) { outEarly += outN; nEarly++; } else if (t > L * 0.7f) { outLate += outN; nLate++; }
                }
                if (!shotTaken && !out.empty() && t > L * (ses == 0 ? 0.45f : 0.8f)) {
                    shotTaken = true;
                    // the timing on the session monitor (at its desk)
                    for (auto& s2 : g.walker.stations) if (s2.id == WST_SESSION) { g.walker.x = s2.x; g.walker.y = s2.y; }
                    g.screen = SCR_WALK;
                    for (int i = 0; i < 3; i++) { Pad q{}; g.frame(1.0f / 30, q); }
                    press(g, &Pad::cross);
                    for (int i = 0; i < 25; i++) { Pad q{}; g.frame(1.0f / 30, q); }
                    press(g, &Pad::right);                         // the TIMING tab
                    for (int i = 0; i < 5; i++) { Pad q{}; g.frame(1.0f / 30, q); }
                    shot(g, out + (ses == 0 ? "/g_timing_practice.ppm" : "/g_timing_quali.ppm"));
                    press(g, &Pad::circle);
                    for (int i = 0; i < 10; i++) { Pad q{}; g.frame(1.0f / 30, q); }
                }
            }
            int totalRuns = 0, noRun = 0, lapsN = 0; double lapsSum = 0; int longRuns = 0;
            for (int k = 0; k < n; k++) { if (r.cars[k].player) continue; totalRuns += runs[k]; if (!runs[k]) noRun++;
                for (int l : runLaps[k]) { lapsSum += l; lapsN++; if (l >= 7) longRuns++; } }
            float avgOut = samples ? (float)(outSum / samples) : 0, early = nEarly ? (float)outEarly / nEarly : 0, late = nLate ? (float)outLate / nLate : 0;
            printf("practiceruns %s %s: %d AI cars, runs %d (%d cars never out), laps per run %.1f (long runs %d), cars out after 5 min: avg %.1f min %d max %d; first 30%% %.1f, last 30%% %.1f\n",
                   tid.c_str(), ses ? "quali" : "practice", n - 1, totalRuns, noRun, lapsN ? lapsSum / lapsN : 0, longRuns, avgOut, minOut, maxOut, early, late);
            int ai = n - 1;
            if (ses == 0 && (totalRuns < ai * 1.4f || avgOut > ai * 0.75f || minOut >= ai || noRun > 0)) { bad++; printf("practiceruns: FAIL practice\n"); }
            if (ses == 1 && !(late > early * 1.3f)) { bad++; printf("practiceruns: FAIL qualifying has no late rush\n"); }
            if (lapsN && (lapsSum / lapsN < 3 || lapsSum / lapsN > 9)) { bad++; printf("practiceruns: FAIL run length\n"); }
        }
        printf("practiceruns: %s\n", bad ? "FAIL" : "ok");
        return bad ? 1 : 0;
    }
    if (mode == "doors") {     // doors x: which cars have doors that open (Blender ECM with DOR1 hinges)
        int with = 0, bad = 0;
        for (int i = 0; i < CAR_PICK; i++) {
            g.R.hdAll = true; g.R.loadCar(i);
            bool mesh = g.R.carMesh[i].loaded(); int dc = g.R.carMesh[i].doorCount;
            char p[160]; snprintf(p, sizeof p, "DATA/cars/%s/%s.ecm", CARS[i].id, CARS[i].id);
            int sz = 0; void* b = sys::loadFile(p, &sz); bool dor = false;
            if (b) { for (int k = 0; k + 4 <= sz; k++) if (!memcmp((char*)b + k, "DOR1", 4)) { dor = true; break; } sys::freeFile(b); }
            printf("doors %-18s %-8s %s, %d door%s%s\n", CARS[i].id, CARS[i].era == ERA_GT3 ? "LMGT3" : "Hypercar", mesh ? "Blender model" : "plain model", dc, dc == 1 ? "" : "s",
                   dc ? "" : " (get-in: the short step)");
            if (dc) with++;
            if (dor && dc == 0) { bad++; printf("doors: FAIL %s has DOR1 but no doors loaded\n", CARS[i].id); }
            for (const CarMesh::Door& d : g.R.carMesh[i].doors) if (&d - g.R.carMesh[i].doors < dc) {
                float al = sqrtf(d.axis[0] * d.axis[0] + d.axis[1] * d.axis[1] + d.axis[2] * d.axis[2]);
                if (fabsf(al - 1) > 0.02f || d.maxAngle <= 0.1f) { bad++; printf("doors: FAIL %s hinge axis |%.2f| angle %.2f\n", CARS[i].id, al, d.maxAngle); }
            }
            g.R.freeCars();
        }
        printf("doors: %d of %d cars open their doors; %s\n", with, CAR_PICK, bad ? "FAIL" : "ok");
        return bad ? 1 : 0;
    }
    if (mode == "memcycle") {
        // memcycle [rounds]: menu -> attract -> menu -> race -> menu, many times. The heap must come
        // back to where it was (nothing that only grows), and the menu garage holds and draws one car
        int rounds = argc > 2 ? atoi(argv[2]) : 6; int bad = 0;
        sound::musicFile(nullptr);
        auto frames = [&](int n) { for (int i = 0; i < n; i++) { Pad q{}; g.frame(1.0f / 30, q); if (pc::mixFn) { std::vector<int16_t> b(1470 * 2); pc::mixFn(b.data(), 1470); } } };
        g.era = ERA_GT3; g.set.car = eraFirstCar(ERA_GT3) + 5; g.hqChosen = true;
        g.screen = SCR_TITLE; g.idle = 0; g.t = 0;
        frames(30);
        int base = 0, peak = 0;
        for (int r = 0; r < rounds; r++) {
            // the menu: long enough that the old title would have cycled through several cars
            g.screen = SCR_TITLE; g.idle = 0;
            for (int i = 0; i < 30 * 25; i++) { Pad q{}; q.up = (i % 200) == 0; g.frame(1.0f / 30, q); g.idle = 0; }
            int menuKB = sys::memUsedKB(), held = g.R.carsLoadedCount(), drawn = g.R.carsDrawn;
            if (held > 1 || drawn > 1) { bad++; printf("memcycle: FAIL the menu holds %d cars, draws %d\n", held, drawn); }
            // the attract movie (straight in), a minute of it, then a button
            g.introBegin(1);
            frames(30 * 60);
            int attractKB = sys::memUsedKB();
            g.introEnd(); frames(30);
            // a race: started, a few seconds, left
            g.set.track = r % TRACK_COUNT; g.set.opponents = 19; g.set.weekend = false; g.aiDrivesPlayer = true;
            g.startRace();
            for (int i = 0; i < 30 * 5; i++) { Pad q{}; g.screen = SCR_RACE; g.frame(1.0f / 30, q); }
            int raceKB = sys::memUsedKB();
            g.leaveRace(); g.screen = SCR_TITLE; g.aiDrivesPlayer = false;
            frames(60);
            int backKB = sys::memUsedKB();
            peak = std::max(peak, std::max(attractKB, raceKB));
            if (r == 1) base = backKB;          // (round 0 fills the one-time caches: fonts, sounds, the garage)
            printf("memcycle round %d: menu %.1f MB (%d car held), attract %.1f MB, race %.1f MB, back on the menu %.1f MB\n",
                   r, menuKB / 1024.0, held, attractKB / 1024.0, raceKB / 1024.0, backKB / 1024.0);
            if (r > 1 && backKB > base + 3 * 1024) { bad++; printf("memcycle: FAIL the heap grew %.1f MB since round 1\n", (backKB - base) / 1024.0); }
        }
        printf("memcycle: peak %.1f MB; %s\n", peak / 1024.0, bad ? "FAIL" : "ok");
        return bad ? 1 : 0;
    }
    if (mode == "flyspawn") {
        // flyspawn [tries]: FLYING LAP from the garage on every track and era, many times, with the field
        // out in practice. The car must stay on the road (no launch), take no damage, and the AI must
        // hand it over after the 10 s countdown (the player's pad input is ignored until then)
        int tries = argc > 2 ? atoi(argv[2]) : 6; int bad = 0, total = 0, moved = 0;
        float worstRise = 0, worstVz = 0;
        const char* only = getenv("FS_TRACK");
        for (int tr = 0; tr < TRACK_COUNT; tr++) for (int era = 0; era < ERA_COUNT; era++) {
            if (only && strcmp(only, TRACKS[tr].id)) continue;
            g.set.track = tr; g.set.car = eraFirstCar(era); g.set.weather = WX_DRY; g.set.opponents = 19;
            g.aiDrivesPlayer = false;
            g.startSession(SES_PRACTICE);
            Race& r = g.race;
            for (int k = 0; k < tries; k++) {
                // let the field run a while (different traffic each try)
                float run = 20 + 37.0f * k;
                for (float t = 0; t < run; t += 1.0f / 30) { r.update(1.0f / 30, 0, 0, 0, 0, true); r.sfxEvents.clear(); }
                r.toGarage();
                r.update(1.0f / 60, 0, 0, 0, 0, false);
                r.flyingLap(g.set.setup);
                total++;
                float rise = 0, vz = 0, dmg = 0; bool handed = false; float handAt = -1, t0 = r.time;
                float maxSteerJump = 0, prevYaw = r.player().yawRate;
                for (int f = 0; f < 60 * 14; f++) {
                    // the pad: full right lock and full throttle all along: ignored until the handover
                    r.update(1.0f / 60, 1.0f, 1.0f, 0, 0, false); r.sfxEvents.clear();
                    const Car& me = r.player();
                    if (getenv("FS_TRACE") && f < 120 && f % 4 == 0) printf("  f %3d s %7.1f d %5.2f v %5.1f hOff %+.3f vz %+.2f grade %+.3f air %.2f load %.2f\n", f, me.s, me.d, me.v, me.body.hOff, me.body.vz, r.track.gradeAt(me.s), me.body.air, me.body.load);
                    if (r.time - t0 < 3.0f) { rise = fmaxf(rise, me.body.hOff); vz = fmaxf(vz, fabsf(me.body.vz - me.v * r.track.gradeAt(me.s))); dmg = fmaxf(dmg, me.damage()); }
                    if (r.handing() && me.human) { printf("flyspawn: FAIL pad drove during the countdown\n"); bad++; }
                    if (!handed && !r.handing()) { handed = true; handAt = r.time - t0; prevYaw = me.yawRate; }
                    if (handed && r.time - t0 < handAt + 0.1f) maxSteerJump = fmaxf(maxSteerJump, fabsf(me.yawRate - prevYaw));
                    prevYaw = me.yawRate;
                }
                worstRise = fmaxf(worstRise, rise); worstVz = fmaxf(worstVz, vz);
                bool ok = rise < 0.35f && vz < 4.0f && dmg < 0.01f && handed && handAt > 9.9f && handAt < 10.2f;
                if (!ok) { bad++; printf("flyspawn: FAIL %s era %d try %d: rise %.2f m vz %.1f m/s damage %.2f handover %s at %.2f s\n", TRACKS[tr].id, era, k, rise, vz, dmg, handed ? "yes" : "no", handAt); }
                r.toGarage(); r.update(1.0f / 60, 0, 0, 0, 0, false);
            }
            moved += r.spawnHits;
        }
        printf("flyspawn: %d spawns, %d failed; worst rise %.3f m, worst vertical speed off the road %.2f m/s; spawn spot moved back for traffic %d times\n", total, bad, worstRise, worstVz, moved);
        printf("flyspawn: %s\n", bad ? "FAIL" : "ok");
        return bad ? 1 : 0;
    }
    if (mode == "careereng") {     // a new career, its first round's practice: does the setup engineer get her laps?
        std::string out = argc > 2 ? argv[2] : "/tmp";
        remove(eraSave("career", g.era));
        auto settle = [&](int n = 50) { for (int i = 0; i < n; i++) { Pad q{}; g.frame(1.0f / 30, q); } };
        settle(30);
        g.sel = 2; press(g, &Pad::cross); settle(40);
        g.sel = 4; press(g, &Pad::cross); settle(20);
        g.sel = 5; press(g, &Pad::cross); settle(20);
        g.aiDrivesPlayer = true;
        g.sel = 0; press(g, &Pad::cross);
        for (int i = 0; i < 400 && g.screen == SCR_LOADING; i++) { Pad q{}; g.frame(1.0f / 30, q); }
        fprintf(stderr, "careereng: screen %d session %d career %d car %s walk %d\n", (int)g.screen, (int)g.race.session, (int)g.race.set.career, g.race.player().def->model, (int)g.walkMenus);
        int last = -1;
        for (int k = 0; k < 60 * 60 * 25 && setupeng::cleanLaps() < 3; k++) {
            g.race.update(1.0f / 60, 0, 0, 0, 0, true);
            if (setupeng::cleanLaps() != last || k % (60 * 120) == 0) { last = setupeng::cleanLaps(); fprintf(stderr, "  t %.0f lap %d pit %d outLap %d clean %d\n", g.race.time, g.race.player().lap, (int)g.race.player().pit, (int)g.race.player().outLap, last); }
        }
        setupeng::Report rp = setupeng::analyse(g.set.setup, g.race.player().tyre);
        // and in person: back in the garage on foot, at her place, X
        g.aiDrivesPlayer = false; g.walkMenus = true;
        g.race.toGarage(); g.walkPitBegin(true); g.walkDoor = 0; g.race.player().doorHold = -1;
        for (auto& s2 : g.walker.stations) if (s2.id == WST_SETUPENG) { g.walker.place(s2.x, s2.y, 0); g.walker.camYaw = 0; }
        settle(5);
        fprintf(stderr, "careereng: screen %d near %s\n", (int)g.screen, g.walker.nearSt >= 0 ? g.walker.stations[g.walker.nearSt].label : "-");
        press(g, &Pad::cross); settle(40);
        fprintf(stderr, "careereng: focus %d open %d sub \"%s\" pending %d\n", g.walkFocus, (int)g.engUi.open, g.engUi.sub, (int)g.engUi.pending());
        shot(g, out + "/careereng_talk.ppm");
        press(g, &Pad::circle);
        for (int i = 0; i < 30 * 20 && (g.engUi.open || g.walkFocus >= 0); i++) { Pad q{}; if (i % 60 == 59) q.circle = true; g.frame(1.0f / 30, q); }
        // the PIT PLANS desk: walking up to it (nothing in the way), then the engineer decides the plans
        for (auto& s2 : g.walker.stations) if (s2.id == WST_PLANS && s2.labelZ < 3) { g.walker.place(s2.x - 0.6f, s2.y - 3.0f, 0); g.walker.camYaw = 1.5708f; }
        settle(10); shot(g, out + "/g_pitplans_approach.ppm");
        for (auto& s2 : g.walker.stations) if (s2.id == WST_PLANS && s2.labelZ < 3) { g.walker.place(s2.x, s2.y, 0); g.walker.camYaw = 1.5708f; }
        settle(5); press(g, &Pad::cross); settle(30);
        for (int i = 0; i < 6; i++) press(g, &Pad::down);
        press(g, &Pad::cross); settle(20);
        fprintf(stderr, "careereng: plans focus %d, plan 1: tyre %d tyres %d fuel %.2f; plan 3 swap %d; engineer says \"%s\"\n", g.walkFocus,
                g.pitPlans[0].tyre, (int)g.pitPlans[0].tyres, g.pitPlans[0].fuel, (int)g.pitPlans[2].swapDriver, g.engUi.sub);
        shot(g, out + "/g_pitplans_strategy.ppm");
        bool plansOk = g.pitPlanSet[0] && g.pitPlanSet[3] && g.pitPlans[3].tyre == TY_INTER && !g.pitPlans[1].tyres && g.pitPlans[2].swapDriver;
        if (!plansOk) fprintf(stdout, "careereng: FAIL the engineer's plans\n");
        press(g, &Pad::circle); settle(20);
        // out of the garage: the 10 s handover countdown
        for (auto& s2 : g.walker.stations) if (s2.id == WST_CAR) { g.walker.place(s2.x, s2.y, 0); }
        settle(5); press(g, &Pad::cross);
        for (int i = 0; i < 30 * 6 && !(g.screen == SCR_RACE && g.race.handing() && g.race.handover < 7.5f); i++) { Pad q{}; g.frame(1.0f / 30, q); }
        fprintf(stderr, "careereng: screen %d handing %d countdown %.1f\n", (int)g.screen, (int)g.race.handing(), g.race.handover);
        shot(g, out + "/g_countdown.ppm");
        if (!g.race.handing()) { fprintf(stdout, "careereng: FAIL no handover countdown\n"); plansOk = false; }
        if (!plansOk) return 1;
        fprintf(stdout, "careereng: %s (%d clean laps, first: %s)\n", rp.laps >= 2 ? "ok" : "FAIL", rp.laps, setupeng::text(rp.sug[0]));
        return rp.laps >= 2 ? 0 : 1;
    }
    if (mode == "career") {     // a new team: setup, livery, the team screens, one race, the report
        std::string out = argv[2];
        remove(eraSave("career", g.era));
        auto settle = [&](int n = 50) { for (int i = 0; i < n; i++) { Pad q{}; g.frame(1.0f / 30, q); } };
        settle(30);
        g.sel = 2; press(g, &Pad::cross); settle(40); shot(g, out + "/c_new.ppm");
        press(g, &Pad::right);                                   // team name
        g.sel = 3; press(g, &Pad::left);                         // race length 10 min
        g.sel = 4; press(g, &Pad::cross); settle(60); shot(g, out + "/c_livery0.ppm");
        press(g, &Pad::right); press(g, &Pad::right);           // style
        g.sel = 1; press(g, &Pad::right); press(g, &Pad::right); press(g, &Pad::right); press(g, &Pad::right);   // main colour
        g.sel = 2; press(g, &Pad::right); press(g, &Pad::right);
        settle(30); shot(g, out + "/c_livery1.ppm"); settle(60); shot(g, out + "/c_livery2.ppm");
        g.sel = 5; press(g, &Pad::cross); settle(50); shot(g, out + "/c_team.ppm");
        g.sel = 1; press(g, &Pad::cross); settle(5); shot(g, out + "/c_sponsors.ppm");
        g.sel = 1; press(g, &Pad::cross); settle(5); press(g, &Pad::circle);
        g.sel = 2; press(g, &Pad::cross); settle(5); shot(g, out + "/c_drivers.ppm"); press(g, &Pad::circle);
        g.sel = 3; press(g, &Pad::cross); g.sel = 0; press(g, &Pad::cross); g.sel = 1; press(g, &Pad::cross); settle(5); shot(g, out + "/c_upgrades.ppm"); press(g, &Pad::circle);
        fprintf(stderr, "career: %s #%d money %d skip #%d (%s) car idx %d\n", TEAM_NAMES[g.career.name], g.career.number, g.career.money, g.career.skipNum, CARS[g.career.skipCar].team, g.career.carIndex());
        // one race, driven by the AI
        g.aiDrivesPlayer = true;
        g.sel = 0; press(g, &Pad::cross);
        fprintf(stderr, "race started: screen %d, player car %s %s #%d\n", (int)g.screen, g.race.player().def->make, g.race.player().def->model, g.race.player().number);
        bool skipSeen = false; for (auto& c : g.race.cars) if (c.number == g.career.skipNum && (int)(c.def - CARS) == g.career.skipCar) skipSeen = true;
        fprintf(stderr, "replaced entry still on the grid: %s; field %d cars\n", skipSeen ? "YES (bug)" : "no", (int)g.race.cars.size());
        g.cam = CAM_CHASE; float simT = 0; bool shotTaken = false;
        // a career round is a full weekend: practice and qualifying first, then the race
        for (;;) {
            while (g.screen == SCR_RACE && g.race.session != SES_RACE && simT < 3000) { Pad q{}; g.step(1.0f / 30, q); simT += 1.0f / 30; }
            if (g.race.session == SES_RACE || g.screen != SCR_RESULTS) break;
            fprintf(stderr, "session %d done at %.0f s (rubber %.2f, P1 best %.3f), on to the next\n", (int)g.race.session, simT, g.race.rubber, g.race.cars[0].bestLap);
            press(g, &Pad::cross);
        }
        fprintf(stderr, "race session: screen %d, grid from qualifying %d cars\n", (int)g.screen, (int)g.qualiGrid.size());
        simT = 0;
        while (g.screen == SCR_RACE && simT < 3000) {
            Pad q{}; g.step(1.0f / 30, q); simT += 1.0f / 30;
            if (!shotTaken && simT > 70) {
                for (int k = 0; k < 2; k++) { g.cam = k ? CAM_COCKPIT : CAM_CHASE; g.R.camInit = false; for (int f = 0; f < 20; f++) { Pad z{}; g.frame(1.0f / 30, z); } shot(g, out + (k ? "/c_race_cockpit.ppm" : "/c_race.ppm"), 1.0f / 60); }
                g.cam = CAM_CHASE; shotTaken = true;
            }
        }
        fprintf(stderr, "rubber at the end %.2f\n", g.race.rubber);
        fprintf(stderr, "after race: screen %d  money %d  last pos %d  prize %d sponsor %d bonus %d running %d salaries %d repairs %d\n", (int)g.screen,
                g.career.money, g.career.lastPos, g.career.inPrize, g.career.inSponsor, g.career.inBonus, g.career.outRunning, g.career.outSalaries, g.career.outRepairs);
        press(g, &Pad::cross); settle(40); press(g, &Pad::cross); settle(40);
        fprintf(stderr, "back at screen %d, round %d\n", (int)g.screen, g.career.champ.round);
        shot(g, out + "/c_report.ppm");
        Career c2; bool okLoad = c2.load(eraSave("career", c2.cls = g.career.cls)); fprintf(stderr, "saved career loads: %d round %d\n", (int)okLoad, c2.champ.round);
        return 0;
    }
    if (mode == "tyrewear") {
        // tyrewear <track> <minutes>: the AI races our car; each tyre's wear, temperature and hot pressure at
        // the end, then a save and a load (save version 10) to check they come back
        std::string tid = argv[2];
        for (int i = 0; i < TRACK_COUNT; i++) if (tid == TRACKS[i].id) g.set.track = i;
        g.set.car = 1; g.set.opponents = 7; g.set.raceMin = atoi(argv[3]); g.set.weather = WX_DRY; g.aiDrivesPlayer = true; g.mgrMode = false;
        g.startRace();
        g.race.playerChassis.ride = 0; g.race.playerChassis.diff = 2; g.race.playerChassis.press = 1;   // (after any remembered setup)
        g.race.player().ch = g.race.playerChassis;
        for (int i = 0; i < 30 * 60 * g.set.raceMin && !g.race.player().finished; i++) { Pad z{}; g.frame(1.0f / 30, z); }
        const Car& me = g.race.player();
        printf("tyrewear: %s after %.0f s: wear FL %.3f FR %.3f RL %.3f RR %.3f (mean %.3f)  temps %.0f %.0f %.0f %.0f  hot %.2f %.2f %.2f %.2f bar\n",
               tid.c_str(), g.race.time, me.tyreWear[0], me.tyreWear[1], me.tyreWear[2], me.tyreWear[3], me.wear, me.tyreT[0], me.tyreT[1], me.tyreT[2], me.tyreT[3],
               g.race.tyrePressure(me, 0), g.race.tyrePressure(me, 1), g.race.tyrePressure(me, 2), g.race.tyrePressure(me, 3));
        float w1 = me.tyreWear[1]; Chassis c0 = me.ch;
        bool saved = g.race.save("tyrewear_test.sav");
        Race r2; bool loaded = r2.load("tyrewear_test.sav");
        const Car& b2 = r2.player();
        bool ok = saved && loaded && fabsf(b2.tyreWear[1] - w1) < 1e-6f && b2.ch.ride == c0.ride && b2.ch.diff == c0.diff && b2.ch.press == c0.press
                  && r2.playerChassis.diff == 2 && me.wear > 0;
        printf("tyrewear: save %d load %d -- %s\n", (int)saved, (int)loaded, ok ? "ok" : "MISMATCH");
        if (!ok) printf("  w %.4f/%.4f  ch %d%d%d/%d%d%d  pc %d  idx %d/%d\n", w1, b2.tyreWear[1], c0.ride, c0.diff, c0.press, b2.ch.ride, b2.ch.diff, b2.ch.press, r2.playerChassis.diff, g.race.playerIdx, r2.playerIdx);
        remove("tyrewear_test.sav");
        return ok ? 0 : 1;
    }
    if (mode == "temps") {
        // temps: each circuit's air and track temperature through the day (Race::airTemp / trackTemp)
        for (int t = 0; t < TRACK_COUNT; t++) {
            g.set.track = t; g.set.car = 1; g.set.weather = WX_DRY; g.ttMode = true;
            g.startSession(SES_PRACTICE);
            Race& r = g.race; r.greenTime = r.time; r.timeScale = 1;
            printf("%-12s", TRACKS[t].id);
            for (int h = 2; h < 24; h += 4) { r.startHour = (float)h; printf("  %02d:00 %2.0f/%2.0f", h, r.airTemp(), r.trackTemp()); }
            int sl = 0, first = -1; for (int i = 0; i < r.track.n; i++) if (r.track.pts[i].sausageL || r.track.pts[i].sausageR) { sl++; if (first < 0) first = i; }
            printf("   (air/track C)  sausage kerbs %.0f m (first at %.0f m)\n", sl * r.track.step, first * r.track.step);
        }
        return 0;
    }
    if (mode == "radiolog") {   // radiolog <track> <minutes> <event hours> [weather]: every engineer call in a timed race
        RaceSettings rs; rs.track = atoi(argv[2]); rs.car = 1; rs.raceMin = atoi(argv[3]); rs.eventHours = (float)atof(argv[4]);
        rs.opponents = 39; rs.weather = argc > 5 ? atoi(argv[5]) : WX_SIMULATED; rs.difficulty = 1;
        g.set = rs; g.aiDrivesPlayer = true; g.startSession(SES_RACE); g.screen = SCR_RACE;
        float simT = 0; int n = 0;
        while (!g.race.over && simT < rs.raceMin * 60 + 900) {
            g.race.update(1.0f / 60, 0, 0, 0, 0, true); simT += 1.0f / 60;
            if (g.race.player().pit == PIT_STOP && g.race.playerAtBox()) g.race.releasePlayer();
            int r = g.race.radioPending;
            if (r >= 0) { int s2 = (int)((g.race.time - g.race.greenTime) * g.race.timeScale); n++;
                fprintf(stderr, "  %2d:%02d race  engineer: %-16s %s\n", s2 / 3600, (s2 / 60) % 60, RADIO_FILE[r], g.race.msgs.empty() ? "" : g.race.msgs[0].text); }
            g.race.radioPending = -1;
        }
        fprintf(stderr, "%d calls, class P%d\n", n, g.race.classPosition(g.race.player()));
        return 0;
    }
    if (mode == "badradio") {   // badradio <track>: the engineer's bad-day lines (a poor finish, a retirement)
        int tr = atoi(argv[2]);
        RaceSettings rs; rs.track = tr; rs.car = 1; rs.laps = 1; rs.opponents = 19; rs.startPos = 2; rs.weather = WX_DRY; rs.difficulty = 2;
        g.set = rs; g.aiDrivesPlayer = true; g.startSession(SES_RACE); g.screen = SCR_RACE;
        Car& me = g.race.player(); me.skill *= 0.85f;                    // from the back, slowly: a bad day
        int heard = -1; float simT = 0;
        while (!g.race.over && simT < 1500) {
            g.race.update(1.0f / 60, 0, 0, 0, 0, true); simT += 1.0f / 60;
            if (g.race.radioPending == RADIO_FINISH || radioSwears(g.race.radioPending)) heard = g.race.radioPending;
            if (g.race.radioPending >= RADIO_RET_SISTER) fprintf(stderr, "  %5.0f s  engineer: %s\n", g.race.time, RADIO_FILE[g.race.radioPending]);
            g.race.radioPending = -1;
        }
        const Car& m2 = g.race.player();
        fprintf(stderr, "finish: class P%d of %d, bad %d, radio %s\n", g.race.classPosition(m2), g.race.classCount(m2.def->cls),
                (int)g.race.badResult(m2), heard >= 0 ? RADIO_FILE[heard] : "none");
        { int miss = 0; char nm[48];
          for (int r = 0; r < RADIO_COUNT; r++) { snprintf(nm, sizeof nm, "16_%s", RADIO_FILE[r]); if (!sound::radio(nm)) { miss++; fprintf(stderr, "  missing %s\n", nm); }
              if (radioSwears(r)) { snprintf(nm, sizeof nm, "16_%s_swear", RADIO_FILE[r]); if (!sound::radio(nm)) { miss++; fprintf(stderr, "  missing %s\n", nm); } } }
          fprintf(stderr, "clips: %d lines, %d missing\n", RADIO_COUNT, miss); }
        // a retirement in a fresh race: the failure call, then (once it has finished) the engineer's word
        g.aiDrivesPlayer = false; cfg.swearing = true;
        rs.laps = 3; g.set = rs; g.startSession(SES_RACE); g.screen = SCR_RACE;
        Pad q{}; for (int i = 0; i < 60 * 40; i++) g.step(1.0f / 60, q);
        g.race.failure(g.race.player(), MF_WHEEL);
        bool first = false, gap = false, second = false; float t0 = g.race.time;
        static int16_t abuf[735 * 2];
        for (int i = 0; i < 60 * 10 && !g.race.over; i++) {
            g.step(1.0f / 60, q);
            if (pc::mixFn) pc::mixFn(abuf, 735);                      // the audio clock (1/60 s)
            bool busy = sound::radioBusy();
            if (busy && !first) first = true;
            if (first && !busy) gap = true;
            if (gap && busy && !second) { second = true; fprintf(stderr, "retired call at +%.1f s\n", g.race.time - t0); }
        }
        fprintf(stderr, "retirement: failure call %d, then the last word %d, results at +%.1f s\n", (int)first, (int)second, g.race.time - t0);
        Config keep = cfg; cfg.swearing = true; cfg.save(); Config c2; c2.load();
        fprintf(stderr, "setting saved and loaded: swearing %d\n", (int)c2.swearing);
        cfg = keep; cfg.save();
        return 0;
    }
    if (mode == "timetrial") {   // timetrial <track> <car> <laps> <outdir>: AI laps against its own ghost, saved and reloaded
        int tr = atoi(argv[2]), car = atoi(argv[3]), laps = atoi(argv[4]); std::string out = argv[5];
        g.ttMode = true; g.set.track = tr; g.set.car = car; g.aiDrivesPlayer = true;
        if (getenv("EMR_NOHYBRID")) Race::hybridOff = true;
        char gp[64]; g.ghostPath(gp, sizeof gp); remove(gp);
        g.startSession(SES_PRACTICE);
        g.race.toGarage(); g.race.flyingLap(g.set.setup); g.screen = SCR_RACE;
        fprintf(stderr, "time trial: %s, %s, %d cars, session %d, dry %.2f\n", TRACKS[tr].name, CARS[car].id, (int)g.race.cars.size(), (int)g.race.session, g.race.rain);
        float simT = 0; int lastLap = 0; bool shot1 = false, shot2 = false;
        while (g.race.player().lap < laps && simT < 3000) {
            Pad q{}; g.step(1.0f / 30, q); simT += 1.0f / 30;
            Car& me = g.race.player();
            if (me.lap != lastLap) {
                lastLap = me.lap;
                fprintf(stderr, "lap %d done: last %.3f best %.3f record %.3f ghost pts %d delta-on %d\n", me.lap, me.lastLap, me.bestLap, g.race.ghostLap, (int)g.race.ghost.size(), (int)g.race.ttDeltaOn);
                if (me.lap == 2) me.skill *= 0.95f;          // lap 3 slower: the ghost pulls away
            }
            if (me.lap == 2 && !shot1 && simT - 0 > 0 && g.race.time - me.lapStart > 8) {
                shot1 = true;
                fprintf(stderr, "  delta at 8 s: %+.3f\n", g.race.ttDelta);
                g.cam = CAM_CHASE; g.R.camInit = false; shot(g, out + "/tt_chase.ppm", 0);
            }
            if (me.lap == 2 && !shot2 && g.race.time - me.lapStart > 14) {
                shot2 = true; fprintf(stderr, "  delta at 14 s: %+.3f\n", g.race.ttDelta);
                g.cam = CAM_COCKPIT; g.R.camInit = false; shot(g, out + "/tt_cockpit.ppm", 0); g.cam = CAM_CHASE;
            }
        }
        FILE* f = fopen(gp, "rb"); fprintf(stderr, "ghost file %s: %s\n", gp, f ? "saved" : "MISSING"); if (f) fclose(f);
        Race r2; r2.track = g.race.track; bool ok = r2.loadGhost(gp);
        fprintf(stderr, "reload: %d  record %.3f  points %d\n", (int)ok, r2.ghostLap, (int)r2.ghost.size());
        g.aiDrivesPlayer = false; g.screen = SCR_PAUSE; g.sel = 6; press(g, &Pad::cross); g.sel = 6; g.idle = 0;
        g.frame(1.0f / 30, *(new Pad{})); shot(g, out + "/tt_title.ppm");
        remove(gp);
        return 0;
    }
    if (mode == "crew") {      // the crew on the title screen, 12 frames 1/15 s apart (do the poses blend?)
        std::string out = argv[2];
        for (int i = 0; i < 80; i++) { Pad q{}; g.frame(1.0f / 30, q); }
        for (int k = 0; k < 12; k++) { char fn[64]; snprintf(fn, sizeof fn, "/crew_%02d.ppm", k); shot(g, out + fn); Pad q{}; g.frame(1.0f / 30, q); g.frame(1.0f / 30, q); }
        return 0;
    }
    if (mode == "music") {      // music x: which menu songs play with a 1998 car and with a 2025 car (era songs: playlist.txt 3rd column)
        for (int era : {0, 2, 3}) {
            g.set.car = eraFirstCar(era);
            for (int i = 0; i < 5; i++) { Pad q{}; g.frame(1.0f / 30, q); }
            int salsa = 0, orig = 0, h16 = 0, n = 40;
            for (int i = 0; i < n; i++) {
                sound::testNextSong(); sound::update();
                if (strstr(sound::nowPlaying(), "Salsa") || strstr(sound::nowPlaying(), "DISTRXCT")) salsa++;   // (2025 songs)
                if (strstr(sound::nowPlaying(), "Formula Legends - ") || strstr(sound::nowPlaying(), "Ruzer")) orig++;      // the 1998 songs
                if (strstr(sound::nowPlaying(), "Exekat - Big Rush") || strstr(sound::nowPlaying(), "Velveteen")) h16++;   // (2016 songs)
            }
            printf("era %d: 2025 songs (Salsa, DISTRXCT) %d, 2016 songs (Big Rush, Velveteen) %d, 1998 songs %d, of %d songs (now: %s)\n", era, salsa, h16, orig, n, sound::nowPlaying());
        }
        return 0;
    }
    if (mode == "walk") {
        // walk <outdir> [track]: the main menu garage, headquarters and a race pit stop on foot
        std::string out = argv[2];
        auto settle = [&](int n) { for (int i = 0; i < n; i++) { Pad q{}; if (i < n - 2) g.step(1.0f / 30, q); else g.frame(1.0f / 30, q); } };
        // walk (or run) the person towards a point of the local frame for up to `secs`, a shot every `every` s
        auto walkTo = [&](float tx, float ty, float secs, bool runIt, const char* name, float every) {
            if (runIt != g.walker.run) press(g, &Pad::l);
            int k = 0; float nextShot = every;
            for (float tt = 0; tt < secs; tt += 1.0f / 30) {
                float dx = tx - g.walker.x, dy = ty - g.walker.y, d = sqrtf(dx * dx + dy * dy);
                Pad q{};
                if (d > 0.5f) {
                    dx /= d; dy /= d;
                    // first person: the head turns towards where we're going (as a player would look)
                    float want = atan2f(dy, dx), dd = want - g.walker.camYaw;
                    while (dd > 3.14159f) dd -= 6.28318f; while (dd < -3.14159f) dd += 6.28318f;
                    g.walker.camYaw += fmaxf(-0.12f, fminf(0.12f, dd)); g.walker.camPitch = -0.08f;
                    float fx = cosf(g.walker.camYaw), fy = sinf(g.walker.camYaw);
                    q.stickY = -(dx * fx + dy * fy); q.stickX = dx * sinf(g.walker.camYaw) - dy * cosf(g.walker.camYaw);
                    float m = fmaxf(fabsf(q.stickX), fabsf(q.stickY)); q.stickX /= m; q.stickY /= m;
                }
                g.step(1.0f / 30, q);      // (no drawing on the way: the shots draw)
                if (name && tt >= nextShot) { char b[96]; snprintf(b, sizeof b, "/%s_%02d.ppm", name, k++); shot(g, out + b, 0); nextShot += every; }
                if (d <= 0.5f) break;
            }
        };
        // turn the view to look at a point (a monitor, the car) before X
        auto face = [&](float fx2, float fy2) { g.walker.camYaw = atan2f(fy2 - g.walker.y, fx2 - g.walker.x); g.walker.camPitch = -0.05f; };
        // 1. the main menu garage
        settle(40); shot(g, out + "/walk_hub_start.ppm");
        fprintf(stderr, "hub: at %.2f %.2f, %d stations, screen %d\n", g.walker.x, g.walker.y, (int)g.walker.stations.size(), (int)g.screen);
        auto stn = [&](int id) { for (auto& s2 : g.walker.stations) if (s2.id == id) return s2; return g.walker.stations[0]; };
        { WalkStation q = stn(0); walkTo(q.x, q.y, 12, false, "walk_hub_go", 1.5f); face(q.x - 1, q.y); }
        settle(4); shot(g, out + "/walk_hub_quickrace.ppm");
        fprintf(stderr, "hub: at %.2f %.2f near %s\n", g.walker.x, g.walker.y, g.walker.nearSt >= 0 ? g.walker.stations[g.walker.nearSt].label : "-");
        { Pad q{}; q.rstickX = 1; for (int i = 0; i < 25; i++) g.frame(1.0f / 30, q); } settle(2); shot(g, out + "/walk_hub_turned.ppm");
        press(g, &Pad::cross); settle(30);
        fprintf(stderr, "hub: X opened screen %d (SCR_TRACK %d)\n", (int)g.screen, (int)SCR_TRACK);
        press(g, &Pad::circle); settle(30);
        fprintf(stderr, "hub: O back to screen %d (title %d)\n", (int)g.screen, (int)SCR_TITLE);
        { WalkStation q = stn(7); walkTo(q.x, q.y, 12, true, nullptr, 9); face(q.x + 1, q.y); } settle(4); shot(g, out + "/walk_hub_trophies.ppm");
        // 2. headquarters (O on foot does nothing: the door's station)
        press(g, &Pad::circle); settle(3);
        fprintf(stderr, "hub: O on foot -> screen %d (still the title)\n", (int)g.screen);
        { WalkStation q = stn(9); walkTo(q.x, q.y, 12, true, nullptr, 9); face(q.x, q.y - 1); } settle(3); shot(g, out + "/walk_hub_hqdoor.ppm"); press(g, &Pad::cross); settle(30);
        fprintf(stderr, "hq: screen %d (SCR_HQ %d)\n", (int)g.screen, (int)SCR_HQ);
        shot(g, out + "/walk_hq_start.ppm");
        walkTo(0.6f, 0.15f, 8, false, nullptr, 9); face(0.6f, 0.95f); settle(6); shot(g, out + "/walk_hq_car.ppm");
        fprintf(stderr, "hq: near %s\n", g.walker.nearSt >= 0 ? g.walker.stations[g.walker.nearSt].label : "-");
        press(g, &Pad::cross); settle(10);
        fprintf(stderr, "hq: X -> screen %d era %d\n", (int)g.screen, g.era);
        // 3. practice: out of the garage on foot to the engineer's place, a pit plan made there
        const char* tid = argc > 3 ? argv[3] : "imola";
        for (int i = 0; i < TRACK_COUNT; i++) if (!strcmp(TRACKS[i].id, tid)) g.set.track = i;
        g.set.car = eraFirstCar(ERA_HYPER); g.set.laps = 3; g.set.opponents = 7; g.set.weather = WX_DRY;
        g.set.weekend = true; g.set.sessionMin = 10;
        g.startRace();
        settle(40);
        fprintf(stderr, "practice: screen %d (on foot %d)\n", (int)g.screen, (int)SCR_WALK);
        if (g.screen != SCR_WALK) return 1;
        {   int blocked = g.walkSightBlocked();
            fprintf(stderr, "garage sight lines: %d blocked (%s)\n", blocked, blocked ? "FAIL" : "ok");
            if (blocked) return 1; }
        shot(g, out + "/walk_garage_start.ppm");
        auto st = [&](int id) { for (auto& s2 : g.walker.stations) if (s2.id == id) return s2; return g.walker.stations[0]; };
        WalkStation su = st(WST_SETUP), pl = st(WST_PLANS), car = st(WST_CAR), se = st(WST_SETUPENG);
        walkTo(su.x, su.y, 15, false, "walk_garage_go", 1.5f); face(su.x, su.y + 1); settle(3); shot(g, out + "/walk_garage_setupdesk.ppm");
        press(g, &Pad::cross); settle(25); shot(g, out + "/walk_garage_setup_focus.ppm");
        fprintf(stderr, "setup desk: focus %d (WST_SETUP %d)\n", g.walkFocus, (int)WST_SETUP);
        press(g, &Pad::right); press(g, &Pad::circle); settle(20);
        fprintf(stderr, "setup desk: wing now %d, O -> focus %d\n", g.set.setup.wing, g.walkFocus);
        WalkStation ss = st(WST_SESSION);
        walkTo(ss.x, ss.y, 10, false, nullptr, 9); face(ss.x, ss.y + 1); press(g, &Pad::cross); press(g, &Pad::right); settle(25); shot(g, out + "/walk_garage_session.ppm");
        press(g, &Pad::circle); settle(10);
        walkTo(pl.x, pl.y, 10, false, nullptr, 9); face(pl.x, pl.y + 1); press(g, &Pad::cross); settle(25);
        fprintf(stderr, "plans desk: focus %d (WST_PLANS %d)\n", g.walkFocus, (int)WST_PLANS);
        // plan 2: softs, half fuel, driver stays in
        press(g, &Pad::right); press(g, &Pad::down); press(g, &Pad::left); press(g, &Pad::left);
        press(g, &Pad::down); for (int i = 0; i < 5; i++) press(g, &Pad::left);
        settle(3); shot(g, out + "/walk_pit_plans.ppm");
        fprintf(stderr, "plan 2: tyres %d (%s) fuel %.1f swap %d\n", g.pitPlans[1].tyres, TYRE_NAME[g.pitPlans[1].tyre], g.pitPlans[1].fuel, g.pitPlans[1].swapDriver);
        press(g, &Pad::circle); settle(10);
        fprintf(stderr, "plans: O -> focus %d screen %d\n", g.walkFocus, (int)g.screen);
        walkTo(se.x, se.y, 10, false, nullptr, 9); face(se.x + 1, se.y); settle(3); shot(g, out + "/walk_garage_engineer.ppm");
        press(g, &Pad::cross); settle(20); shot(g, out + "/walk_garage_engineer_talk.ppm");
        fprintf(stderr, "setup engineer: focus %d open %d sub \"%s\" pending %d\n", g.walkFocus, g.engUi.open, g.engUi.sub, g.engUi.pending());
        for (int i = 0; i < 400 && g.walkFocus >= 0; i++) { if (g.engUi.pending()) press(g, &Pad::circle); else { Pad z{}; g.step(1.0f / 30, z); } }
        fprintf(stderr, "setup engineer: done, focus %d\n", g.walkFocus);
        // out to the pit lane and back, then in the car
        { const WalkStation& w2 = st(WST_PLANS); (void)w2; }
        walkTo(g.walker.x, g.walker.minY + 3.0f, 12, true, "walk_pit_go", 1.5f); settle(3); shot(g, out + "/walk_pit_lane.ppm");
        walkTo(car.x, car.y, 15, true, nullptr, 9);
        { float cxp = g.race.pitRel(g.race.player().s), cyp = g.race.player().d; face(cxp, cyp); }
        settle(3); shot(g, out + "/walk_garage_getin.ppm");
        press(g, &Pad::cross);
        fprintf(stderr, "get in: door sequence %d\n", g.walkDoor);
        for (int i = 0; i < 60 && g.walkDoor; i++) {
            Pad z{}; g.step(1.0f / 30, z);
            if (i == 14) { shot(g, out + "/walk_door_open.ppm"); fprintf(stderr, "  door at %.2f s: %.2f (screen %d)\n", g.walkDoorT, g.race.player().door, (int)g.screen); }
            if (i == 28) { shot(g, out + "/walk_door_inside.ppm"); fprintf(stderr, "  door at %.2f s: %.2f\n", g.walkDoorT, g.race.player().door); }
            if (i == 46) { shot(g, out + "/walk_door_closing.ppm"); fprintf(stderr, "  door at %.2f s: %.2f\n", g.walkDoorT, g.race.player().door); }
        }
        settle(5);
        fprintf(stderr, "get in: screen %d (race view %d), car pit %d, door %.2f hold %.1f\n", (int)g.screen, (int)SCR_RACE, g.race.player().pit, g.race.player().door, g.race.player().doorHold);
        // 4. the race: touch the top-right corner (plan 2); the AI drives in, the stop runs the plan
        g.set.weekend = false;
        g.aiDrivesPlayer = true;
        g.startRace();
        if (g.screen != SCR_RACE) { fprintf(stderr, "race did not start\n"); return 1; }
        for (int i = 0; i < 300; i++) { Pad q{}; g.step(1.0f / 60, q); }
        g.aiDrivesPlayer = false;
        { Pad q{}; q.touches = 1; q.tx[0] = SCREEN_W - 10; q.ty[0] = 10; g.step(1.0f / 60, q); Pad z{}; g.step(1.0f / 60, z); }
        fprintf(stderr, "race: plan called %d, wantPit %d, radio queue %d, screen %d\n", g.pitPlanSel, g.race.player().wantPit, (int)g.radioQ.size(), (int)g.screen);
        for (auto& m : g.race.msgs) fprintf(stderr, "   msg: %s\n", m.text);
        shot(g, out + "/walk_race_called.ppm");
        g.aiDrivesPlayer = true;
        bool applied = false;
        for (int step = 0; step < 60 * 600 && !applied; step++) {
            Pad q{};
            if (!g.aiDrivesPlayer && g.race.player().pit == PIT_IN) q.trigR = 0.4f;     // (creeping the last metre in)
            g.step(1.0f / 60, q);
            const Car& me = g.race.player();
            if (!g.aiDrivesPlayer && (step % 20) == 0 && me.pit == PIT_IN) fprintf(stderr, "  creeping: to box %.2f m, v %.2f d %.2f (box d %.2f)\n", g.race.pitRel(g.race.boxS(me)) - g.race.pitRel(me.s), me.v, me.d, g.race.boxD);
            // (the AI brings the car down the lane; ours from just short of the box, so the stop is the player's)
            if (g.aiDrivesPlayer && me.pit == PIT_IN && g.race.pitRel(g.race.boxS(me)) - g.race.pitRel(me.s) < 1.2f) g.aiDrivesPlayer = false;
            if (me.pit == PIT_STOP && me.pitPhase >= 0) applied = true;
            if (g.screen == SCR_TEAM) break;
        }
        const Car& me = g.race.player();
        fprintf(stderr, "stop: screen %d (team %d), pit %d phase %d, plan tyres %d %s fuel %.2f, sel %d\n", (int)g.screen, (int)SCR_TEAM, me.pit, me.pitPhase, me.plan.tyres, TYRE_NAME[me.plan.tyre], me.plan.fuel, g.pitPlanSel);
        for (int i = 0; i < 60; i++) { Pad q{}; g.frame(1.0f / 60, q); }
        shot(g, out + "/walk_race_stop.ppm");
        return 0;
    }
    if (mode == "menus") {
        std::string out = argv[2];
        auto settle = [&](int n = 80) { for (int i = 0; i < n; i++) { Pad q{}; g.frame(1.0f / 30, q); } };
        settle(); shot(g, out + "/menu_title.ppm");
        press(g, &Pad::cross); settle(8); shot(g, out + "/menu_walk.ppm"); settle(); shot(g, out + "/menu_track.ppm");
        press(g, &Pad::down); settle(5); shot(g, out + "/menu_track2.ppm");
        press(g, &Pad::cross); settle(); shot(g, out + "/menu_car.ppm");
        for (int i = 0; i < 9; i++) press(g, &Pad::down);
        settle(20); shot(g, out + "/menu_car2.ppm");
        press(g, &Pad::cross); settle(); shot(g, out + "/menu_settings.ppm");
        g.sel = 14; press(g, &Pad::cross); settle(40); press(g, &Pad::down); press(g, &Pad::down); press(g, &Pad::down); press(g, &Pad::left); settle(5); shot(g, out + "/menu_setup.ppm");
        { int keep = g.set.car; g.set.car = 1; g.menuCamInit = false; settle(30); shot(g, out + "/menu_setup_hy.ppm"); press(g, &Pad::circle); settle(30); shot(g, out + "/menu_settings_hy.ppm"); press(g, &Pad::down); g.set.car = keep; g.sel = 14; press(g, &Pad::cross); g.menuCamInit = false; settle(30); }
        press(g, &Pad::circle); settle(30);
        press(g, &Pad::circle); press(g, &Pad::circle); press(g, &Pad::circle);      // back to the title
        g.sel = 4; press(g, &Pad::cross); settle(); shot(g, out + "/menu_options.ppm");
        g.sel = OC_SOUND; press(g, &Pad::cross); settle(5); shot(g, out + "/menu_opt_sound.ppm"); press(g, &Pad::circle);
        g.sel = OC_DISPLAY; press(g, &Pad::cross); settle(5); shot(g, out + "/menu_opt_display.ppm"); press(g, &Pad::circle);
        g.sel = OC_CAMERA; press(g, &Pad::cross); settle(5); shot(g, out + "/menu_opt_camera.ppm"); press(g, &Pad::circle);
        g.sel = OC_REALISM; press(g, &Pad::cross); settle(5); shot(g, out + "/menu_realism.ppm");
        g.sel = 10; press(g, &Pad::cross); settle(5); shot(g, out + "/menu_mech.ppm"); press(g, &Pad::circle); press(g, &Pad::circle); settle(5);
        g.sel = OC_CONTROLS; press(g, &Pad::cross); settle(5);
        g.sel = 6; press(g, &Pad::cross); press(g, &Pad::up); press(g, &Pad::up); g.sel = 2; press(g, &Pad::square); settle(4); shot(g, out + "/menu_controls.ppm");
        fprintf(stderr, "pit request is now on: %s\n", BUTTON_NAME[cfg.bind[ACT_PIT]]);
        { int ids[24]; g.sel = g.controlRows(ids) - 2; } press(g, &Pad::cross);       // reset buttons
        press(g, &Pad::circle); g.sel = OC_BACK; press(g, &Pad::cross); settle(5);
        g.sel = 5; press(g, &Pad::cross); settle(); shot(g, out + "/menu_help.ppm");
        press(g, &Pad::circle);
        g.sel = 7; press(g, &Pad::cross); settle(); shot(g, out + "/menu_trophies.ppm");
        press(g, &Pad::circle);
        { unsigned keep = trophies.won; trophies.won &= ~(1u << TR_PODIUM);
          trophies.award(TR_PODIUM); for (int i = 0; i < 30; i++) g.frame(1.0f / 60, Pad{}); shot(g, out + "/menu_trophy_popup.ppm");
          trophies.won = keep; trophies.queued = 0; trophies.save(); }
        // 6 s of menu audio (the soundtrack)
        std::vector<int16_t> wav;
        for (int f = 0; f < 360; f++) {
            Pad q{}; g.step(1.0f / 60, q); sound::musicEnable(true); sound::update();
            if (pc::mixFn) { std::vector<int16_t> b(735 * 2); pc::mixFn(b.data(), 735); wav.insert(wav.end(), b.begin(), b.end()); }
        }
        writeWav((out + "/menu_audio.wav").c_str(), wav);
        fprintf(stderr, "now playing: %s\n", sound::nowPlaying());
        return 0;
    }
    if (mode == "garage") {
        std::string out = argv[2];
        auto settle = [&](int n) { for (int i = 0; i < n; i++) { Pad q{}; g.frame(1.0f / 30, q); } };
        g.set.track = 4; g.set.car = getenv("EMR_CAR") ? atoi(getenv("EMR_CAR")) : 9; g.set.weekend = true; g.set.sessionMin = 10; g.sessionClamp = false; g.set.opponents = 11;
        g.startRace();
        settle(40); shot(g, out + "/g_1_garage.ppm");
        g.setupTab = 1; settle(2); shot(g, out + "/g_1w_weather.ppm");
        g.setupTab = 2; settle(400); shot(g, out + "/g_1t_timing.ppm"); g.timingOverall = true; shot(g, out + "/g_1t_timing_overall.ppm"); g.timingOverall = false; g.setupTab = 0;
        int inG = 0; for (auto& c : g.race.cars) if (c.pit == PIT_STOP) inG++;
        fprintf(stderr, "practice start: screen %d, player at the computer %d, cars in garages %d/%d\n", (int)g.screen, g.race.inGarage(), inG, (int)g.race.cars.size());
        g.setupSel = Game::SR_WING; press(g, &Pad::right); press(g, &Pad::right);          // wing 3 -> 5
        g.setupSel = Game::SR_DRIVE; press(g, &Pad::cross);                                // drive out
        settle(60); shot(g, out + "/g_2_pushed.ppm");
        settle(55); shot(g, out + "/g_2b_pushing.ppm"); settle(8); shot(g, out + "/g_2c_pushing.ppm");
        settle(87);
        const Car& me = g.race.player();
        fprintf(stderr, "drive out: screen %d pit %d (0 = on track) v %.1f wing %d\n", (int)g.screen, me.pit, me.v, me.su.wing);
        settle(120); shot(g, out + "/g_3_out.ppm");
        int onTrack = 0; for (auto& c : g.race.cars) if (c.pit == PIT_NONE) onTrack++;
        fprintf(stderr, "cars out on track after %.0f s: %d\n", g.race.time, onTrack);
        // the AI brings our car back in (pit request -> pit lane -> garage)
        g.aiDrivesPlayer = true; g.race.player().wantPit = true;
        bool sawIn = false; int overlaps = 0;
        for (int f = 0; f < 30 * 400 && !sawIn; f++) {
            Pad q{}; g.frame(1.0f / 30, q);
            if (g.race.player().pit == PIT_STOP && g.race.player().pitPhase == -4) { sawIn = true; pc::saveShot((out + "/g_4_in.ppm").c_str()); }
            for (size_t a = 0; a < g.race.cars.size(); a++) for (size_t b = a + 1; b < g.race.cars.size(); b++) {
                const Car &A = g.race.cars[a], &B = g.race.cars[b];
                if (A.pit || B.pit) continue;
                if (fabsf(g.race.lapGap(A.s, B.s)) < 3.0f && fabsf(A.d - B.d) < 1.2f) overlaps++;
            }
        }
        fprintf(stderr, "pit request -> garage: %s at %.0f s; frames with cars inside each other: %d\n", sawIn ? "yes" : "NO", g.race.time, overlaps);
        g.aiDrivesPlayer = false;
        g.race.toGarage(); g.screen = SCR_GARAGE;
        g.setupSel = Game::SR_TIME; press(g, &Pad::right); press(g, &Pad::right);        // time x16
        float t0 = g.race.time; settle(30);
        fprintf(stderr, "time x%d: %.1f s of session in 1 s\n", (int)g.race.timeAccel, g.race.time - t0);
        g.setupSel = Game::SR_FLYING; press(g, &Pad::cross); settle(5);                      // flying lap
        fprintf(stderr, "flying lap: pit %d v %.0f km/h outLap %d\n", g.race.player().pit, g.race.player().v * 3.6f, (int)g.race.player().outLap);
        shot(g, out + "/g_5_flying.ppm");
        g.race.toGarage(); g.screen = SCR_GARAGE; settle(3);
        g.setupSel = Game::SR_SKIP; press(g, &Pad::cross); settle(5);                     // skip to qualifying
        fprintf(stderr, "skip: screen %d (7 = results)\n", (int)g.screen);
        press(g, &Pad::cross); settle(40);
        fprintf(stderr, "quali: screen %d at the computer %d\n", (int)g.screen, g.race.inGarage());
        g.setupSel = Game::SR_WING; press(g, &Pad::left); g.setupSel = Game::SR_BRAKEF; press(g, &Pad::right);
        settle(3); shot(g, out + "/g_6_quali.ppm");
        fprintf(stderr, "quali setup: wing %d (sealed at 5), brake front %d\n", g.set.setup.wing, g.set.setup.brakeF);
        return 0;
    }
    if (mode == "reel") {
        // highlights: how often the TV camera's view of the followed car is blocked by scenery
        std::string out = argv[2];
        int blocked = 0, frames = 0, shots = 0;
        for (int run = 0; run < 3; run++) {
            g.startReel();
            for (int f = 0; f < 30 * 60 && g.screen == SCR_REEL; f++) {
                Pad q{}; g.step(1.0f / 30, q);
                Camera c = g.R.tvCamera(g.race, g.reelFocus, 1.0f / 30);
                const Car& car = g.race.cars[g.reelFocus];
                if (g.R.sightBlocked(g.race.track, c.eye, car.pos + V3(0, 0, 0.8f))) blocked++;
                frames++;
                if (f % 450 == 200 && shots < 8) { g.frame(0, q); char b[64]; snprintf(b, sizeof b, "/reel_%d.ppm", shots++); pc::saveShot((out + b).c_str()); }
            }
            fprintf(stderr, "reel %d: %s\n", run, TRACKS[g.race.set.track].name);
        }
        fprintf(stderr, "view blocked in %d of %d frames (%.1f%%)\n", blocked, frames, 100.0 * blocked / frames);
        return 0;
    }
    if (mode == "podium") {
        g.set.track = 8; g.set.car = 1; g.set.laps = 1; g.set.opponents = 11; g.aiDrivesPlayer = true;
        g.startRace();
        for (int i = 0; i < 30 * 400 && g.screen == SCR_RACE; i++) { Pad q{}; g.frame(1.0f / 30, q); }
        press(g, &Pad::cross);
        for (int i = 0; i < 60; i++) { Pad q{}; g.frame(1.0f / 30, q); }
        shot(g, std::string(argv[2]) + "/podium_wait.ppm");          // 2 s: trophies on their stands
        for (int i = 0; i < 60; i++) { Pad q{}; g.frame(1.0f / 30, q); }
        shot(g, std::string(argv[2]) + "/podium.ppm");               // 4 s: champagne
        for (int i = 0; i < 4; i++) { Pad q{}; g.frame(1.0f / 30, q); }
        shot(g, std::string(argv[2]) + "/podium_b.ppm");
        return 0;
    }
    if (mode == "launch") {
        // launch [era]: three standing starts for the player: good (revs held in the band, clutch
        // 0.2 s after lights out), bad (button flat out, late clutch), jump (clutch before the lights)
        int era = argc > 2 ? atoi(argv[2]) : 0;
        const char* NAME[3] = {"good", "bad", "jump"};
        for (int sc = 0; sc < 3; sc++) {
            RaceSettings rs; rs.track = 2; rs.car = eraFirstCar(era); rs.opponents = 5; rs.weather = WX_DRY; rs.startType = START_STANDING;
            g.race.start(rs, SES_RACE);
            Race& r = g.race;
            float mid = (r.launch.lo + r.launch.hi) * 0.5f, outT = -1, v3 = -1, d3 = 0, s0 = r.player().total;
            bool jumpDone = false;
            for (int i = 0; i < 60 * 20 && v3 < 0; i++) {
                float th = sc == 0 ? (mid - 0.05f) / 0.95f : 1.0f;
                bool press = false;
                if (r.countdown <= 0 && outT < 0) outT = r.time;
                if (outT >= 0 && (sc != 0 || r.launch.state != Race::LS_WAIT)) th = 1.0f;   // (good: holds the revs until the clutch)
                if (sc == 0 && outT >= 0 && r.time - outT >= 0.2f) press = true;
                if (sc == 1 && outT >= 0 && r.time - outT >= 0.6f) press = true;
                if (sc == 2 && r.countdown > 0 && r.countdown < 2.0f && !jumpDone) { press = true; jumpDone = true; }
                if (press && r.launch.state != Race::LS_OFF) r.drsPress = true;
                r.update(1.0f / 60, 0, th, 0, 0, false);
                r.sfxEvents.clear();
                if (outT >= 0 && r.time - outT >= 3.0f) { v3 = r.player().v; d3 = r.player().total - s0; }
            }
            const char* last = r.msgs.empty() ? "" : r.msgs.back().text;
            printf("era %d %-4s: quality %.2f reaction %.2f revs %.2f (band %.2f-%.2f) penalty %.0f s  3 s after green: %.1f km/h, %.1f m  [%s]\n",
                   era, NAME[sc], r.launch.quality, r.launch.reaction, r.launch.revAtDrop, r.launch.lo, r.launch.hi, r.player().penalty, v3 * 3.6f, d3, last);
            for (auto& m : r.msgs) printf("      msg: %s\n", m.text);
        }
        return 0;
    }
    if (mode == "drsers") {
        // drsers <era 2|3> <map 0-2>: Monza practice. The AI drives; on the straights the pad takes
        // over (steer 0, flat out) so DRS must wait for the button. Prints the ERS time per state
        // and section, and when the flap opened against when the button was pressed.
        int era = argc > 2 ? atoi(argv[2]) : 2, map = argc > 3 ? atoi(argv[3]) : 1;
        for (int i = 0; i < TRACK_COUNT; i++) if (!strcmp(TRACKS[i].id, "imola")) g.set.track = i;
        g.ttMode = true; g.set.car = eraFirstCar(era); g.set.weather = WX_DRY;
        g.startSession(SES_PRACTICE);
        g.race.toGarage(); g.race.flyingLap(g.set.setup);
        Race& r = g.race; r.player().su.map = map;
        float st[3][3] = {}; float simT = 0; int presses = 0, opens = 0, early = 0; float pressAt = -1;
        while (r.player().lap < 3 && simT < 400) {
            Car& me = r.player();
            int z = r.drsZoneAt(me.s), zn = r.drsZoneAt(me.s + 40);
            bool pad = (z >= 0 || zn >= 0) && me.lap >= 1;
            if (pad && pressAt < 0 && r.track.wrap(me.s - r.drsZones[z].from) > 150) { r.drsPress = true; pressAt = simT; presses++; }
            if (z < 0) pressAt = -1;
            bool wasOpen = me.drs;
            r.update(1.0f / 60, 0, pad ? 1 : 0, 0, 0, !pad); simT += 1.0f / 60;
            r.sfxEvents.clear();
            if (me.drs && !wasOpen) { opens++; if (pressAt < 0) early++; printf("DRS open at s %.0f lap %d pad %d zone %d (press %s)\n", me.s, me.lap, (int)pad, z, pressAt < 0 ? "none" : "yes"); }
            st[me.ersState][r.ersSecAt(me.s)] += 1.0f / 60;
        }
        printf("era %d map %d: presses %d opens %d without press %d  best lap %.3f\n", era, map, presses, opens, early, r.player().bestLap);
        const char* SN[3] = {"idle", "deploy", "harvest"};
        for (int a = 0; a < 3; a++) printf("  %-8s corner %5.1f s  exit %5.1f s  straight %5.1f s\n", SN[a], st[a][0], st[a][1], st[a][2]);
        return 0;
    }
    if (mode == "handling") {
        // scripted driver: accelerate, then steer <amount> for 1.5 s, then let go
        RaceSettings rs; rs.track = 2; rs.car = argc > 5 ? eraFirstCar(atoi(argv[5])) : 1; rs.opponents = 5; rs.weather = WX_DRY;
        float amount = (float)atof(argv[2]); float brake = argc > 3 ? (float)atof(argv[3]) : 0;
        g.race.start(rs, SES_PRACTICE);
        Race& r = g.race; float t0 = -1;
        for (int i = 0; i < 60 * 40; i++) {
            Car& c = r.player();
            float st = 0, th = 1, bk = 0;
            if (t0 < 0 && c.v > (argc > 4 ? atof(argv[4]) : 55)) t0 = r.time;
            if (t0 >= 0) { float tt = r.time - t0; if (tt < 0.8f) { st = amount; th = brake > 0 ? 0 : 0.6f; bk = brake; } else { th = 0.5f; if (getenv("FL_COUNTER") && tt < 1.6f) st = -(float)atof(getenv("FL_COUNTER")) * (amount > 0 ? 1 : -1); } if (tt > 4) break;
                if (i % 12 == 0) printf("t %.1f  steer %.1f  v %5.1f  nose %6.1f deg  slide %6.1f deg  across %5.1f m  slip %.2f\n", tt, st, c.v, c.yaw * 57.3f, c.drift * 57.3f, c.d, c.slip); }
            r.update(1.0f / 60, st, th, bk, 0, false);
        }
        return 0;
    }
    if (mode == "fx") {
        // fx <car> <outdir>: sparks from under the car, an engine on fire, exhaust flames and dust,
        // forced on for a moment and pictured from the chase camera
        g.set.car = atoi(argv[2]); std::string out = argv[3];
        for (int i = 0; i < TRACK_COUNT; i++) if (!strcmp(TRACKS[i].id, "imola")) g.set.track = i;
        g.set.laps = 1; g.set.opponents = 0; g.set.weather = WX_DRY; g.aiDrivesPlayer = true;
        g.startRace();
        for (int f = 0; f < 30 * 14; f++) { Pad q{}; g.step(1.0f / 30, q); }
        Race& r = g.race; Car& me = r.player();
        for (int f = 0; f < 10; f++) {
            r.sparks(me.pos - me.fwd * 1.8f + V3(0, 0, 0.05f), me.fwd * (me.v * 0.55f) + V3(0, 0, 1.5f), 6, 4.0f);
            Pad q{}; g.step(1.0f / 30, q);
        }
        g.R.camInit = false; shot(g, out + "/fx_sparks.ppm", 0);
        me.dmg[DM_ENGINE] = 0.95f;
        for (int f = 0; f < 20; f++) { Pad q{}; g.step(1.0f / 30, q); }
        g.R.camInit = false; shot(g, out + "/fx_fire.ppm", 0);
        // a big crash: the car catches fire
        me.dmg[DM_ENGINE] = 0.0f;
        for (int f = 0; f < 60; f++) { Pad q{}; g.step(1.0f / 30, q); }     // (the engine fire's smoke clears)
        me.dmg[DM_LEFT] = 0.6f; r.crashFire(me, 0.6f);
        for (int f = 0; f < 12; f++) { Pad q{}; g.step(1.0f / 30, q); }
        g.R.camInit = false; shot(g, out + "/fx_crash.ppm", 0);
        int n[5] = {}; for (auto& q : r.parts) n[q.kind]++;
        printf("smoke %d spray %d shard %d spark %d flame %d\n", n[0], n[1], n[2], n[3], n[4]);
        return 0;
    }
    if (mode == "driver") {
        // driver <car> <outdir>: a few seconds into a race, close views of the player's car (driver in
        // the cockpit) from the side, behind and above, front three-quarter
        g.set.car = atoi(argv[2]); std::string out = argv[3];
        for (int i = 0; i < TRACK_COUNT; i++) if (!strcmp(TRACKS[i].id, "imola")) g.set.track = i;
        g.set.laps = 1; g.set.opponents = 3; g.set.weather = WX_DRY; g.aiDrivesPlayer = true;
        g.startRace();
        for (int f = 0; f < 30 * 9; f++) { Pad q{}; g.step(1.0f / 30, q); }
        const Car& c = g.race.player();
        V3 views[4] = {V3(-0.6f, 2.2f, 1.3f), V3(-3.2f, 0.0f, 1.9f), V3(2.4f, 1.6f, 1.1f), V3(0.4f, -1.3f, 2.4f)};
        for (int k = 0; k < 4; k++) {
            Camera cam;
            V3 v = views[k];
            cam.eye = c.pos + c.fwd * v.x + c.left * v.y + c.up * v.z;
            V3 look = c.pos + c.up * 0.6f - cam.eye;
            cam.fwd = look * (1.0f / len(look)); cam.up = V3(0, 0, 1); cam.fov = 50;
            gfx::beginFrame(RGBA(120, 160, 210));
            g.R.drawScene(g.race, cam, false);
            gfx::endFrame();
            char b[64]; snprintf(b, sizeof b, "/driver_%d.ppm", k);
            pc::saveShot((out + b).c_str());
        }
        return 0;
    }
    if (mode == "loading") {
        // loading <outdir> [track]: a weekend's practice loaded behind the slideshow; the steps' times, and
        // pictures of the slideshow mid-load (held on screen by moving its start back)
        std::string out = argv[2];
        g.set.track = 0;
        if (argc > 3) for (int i = 0; i < TRACK_COUNT; i++) if (!strcmp(TRACKS[i].id, argv[3])) g.set.track = i;
        g.set.car = 3; g.set.weekend = true; g.set.opponents = 38;
        g.startSession(SES_PRACTICE);
        Pad none{};
        int shots = 0, frames = 0;
        double t0 = sys::timeSec();
        while (g.screen == SCR_LOADING && frames < 600) {
            int stepBefore = g.ld.step;
            double a = sys::timeSec();
            g.ld.t0 = sys::timeSec() - 2.0;              // (keep the slideshow showing for the pictures)
            if (frames % 3 == 0) g.ld.slideT = 1.5f;     // (a picture fully in)
            g.frame(1.0f / 30, none);
            fprintf(stderr, "loading: frame %d step %d -> %d  %.0f ms  progress %.2f\n", frames, stepBefore, g.ld.step, (sys::timeSec() - a) * 1000, g.ld.prog);
            if (shots < 6 && (frames == 1 || frames == 2 || stepBefore == 2)) {
                char b[64]; snprintf(b, sizeof b, "/loading_%d.ppm", shots++);
                pc::saveShot((out + b).c_str());
                g.ld.slide++;
            }
            frames++;
        }
        fprintf(stderr, "loading: done in %d frames, %.2f s, screen %d (walk %d garage %d)\n", frames, sys::timeSec() - t0, g.screen, SCR_WALK, SCR_GARAGE);
        return g.screen == SCR_WALK || g.screen == SCR_GARAGE ? 0 : 1;
    }
    if (mode == "art") {
        // art <outdir> <car id> <yaw> ...: cars on magenta, to be cut out for the LiveArea pictures
        std::string out = argv[2];
        for (int k = 3; k + 1 < argc; k += 2) {
            int i = 0; for (int c = 0; c < CAR_COUNT; c++) if (!strcmp(CARS[c].id, argv[k])) i = c;
            g.R.hdCar = i; g.R.hdAll = true;
            gfx::beginFrame(RGBA(255, 0, 255));
            { float sz = getenv("ART_SZ") ? atof(getenv("ART_SZ")) : 300; g.R.drawCarShowroom(i, (float)atof(argv[k + 1]), 240 - sz / 2, 136 - sz / 2 - sz * 0.1f, sz, sz); }
            gfx::endFrame();
            pc::saveShot((out + "/art_" + argv[k] + ".ppm").c_str());
        }
        return 0;
    }
    if (mode == "cars") {
        // every car on the turntable from two sides
        std::string out = argv[2];
        for (int i = 0; i < CAR_COUNT; i++) for (int v = 0; v < 2; v++) {
            gfx::beginFrame(RGBA(70, 80, 95));
            g.R.drawCarShowroom(i, v ? 2.6f : -0.5f, 0, -104, 480, 480);
            gfx::endFrame();
            char b[64]; snprintf(b, sizeof b, "/car_%02d_%d.ppm", i, v);
            pc::saveShot((out + b).c_str());
        }
        return 0;
    }
    if (mode == "vitacars") {
        // vitacars <outdir> [car index ...]: each car drawn the race way (what the garage, grid and
        // race draw) close up like the walk garage's camera, then side on at the Vita's level-of-detail
        // distances (10 m: level 0, 40 m: level 1, 100 m: level 2), the view narrowed so the car fills
        // the picture. Run with EMR_VITASET=1 EMR_NOGLASS=1 for the Vita's files and glass.
        std::string out = argv[2];
        std::vector<int> cars;
        for (int k = 3; k < argc; k++) cars.push_back(atoi(argv[k]));
        if (cars.empty()) for (int i = 0; i < CAR_COUNT; i++) if (!CARS[i].career) cars.push_back(i);
        const float ASPECT = (float)SCREEN_W / SCREEN_H;
        struct View { const char* name; V3 eye; float fovy; };
        const View views[4] = {
            {"close", V3(2.6f, 2.4f, 1.15f), 62},
            {"d10", V3(0.0f, 10.0f, 1.0f), 2 * atanf(1.7f / 10) * 57.2958f},
            {"d40", V3(0.0f, 40.0f, 1.0f), 2 * atanf(1.7f / 40) * 57.2958f},
            {"d100", V3(0.0f, 100.0f, 1.0f), 2 * atanf(1.7f / 100) * 57.2958f},
        };
        for (int idx : cars) {
            g.R.loadCar(idx);
            for (int v = 0; v < 4; v++) {
                gfx::beginFrame(RGBA(70, 80, 95));
                const View& vw = views[v];
                V3 at(0, 0, 0.6f), fwd = norm(at - vw.eye);
                gfx::setProjection(M4::perspective(vw.fovy, ASPECT, 0.3f, 400));
                gfx::setView(M4::lookDir(V3(0, 0, 0), fwd, V3(0, 0, 1)));   // (camera-relative, as the game draws)
                gfx::setFog(false, 0, 1, 0);
                gfx::setDepthTest(true); gfx::setDepthWrite(true); gfx::setBlend(false);
                gfx::Lighting L;
                L.sun = norm(V3(-0.55f, -0.75f, 0.9f));
                L.sunCol[0] = 0.95f; L.sunCol[1] = 0.93f; L.sunCol[2] = 0.90f;
                L.sky[0] = 0.50f; L.sky[1] = 0.51f; L.sky[2] = 0.55f;
                L.ground[0] = L.ground[1] = L.ground[2] = 0.18f;
                L.zenith[0] = 0.92f; L.zenith[1] = 0.93f; L.zenith[2] = 0.96f;
                L.shadows = false;
                gfx::setEye(V3(0, 0, 0)); gfx::setLighting(L); gfx::setLit(true);
                g.R.camRight = norm(cross(fwd, V3(0, 0, 1))); g.R.camUp = cross(g.R.camRight, fwd);
                Car c; c.def = &CARS[idx]; c.number = CARS[idx].numbers[0];
                c.fwd = V3(1, 0, 0); c.left = V3(0, 1, 0); c.up = V3(0, 0, 1); c.pos = V3(0, 0, 0);
                float tv = tanf(vw.fovy * 3.14159f / 360.0f);
                g.R.drawCarAt(c, vw.eye, fwd, tv * ASPECT, tv);
                gfx::setLit(false);
                gfx::endFrame();
                char b[96]; snprintf(b, sizeof b, "/vc_%02d_%s.ppm", idx, vw.name);
                pc::saveShot((out + b).c_str());
#ifdef EMR_HEADLESS
                long calls = perfstat::calls, verts = perfstat::verts;
#else
                long calls = pc::drawCalls, verts = pc::vertsSent;
#endif
                fprintf(stderr, "vitacars %02d %-5s %s: %ld draw calls, %ld vertices\n", idx, vw.name, CARS[idx].id, calls, verts);
            }
        }
        return 0;
    }
    if (mode == "screens") {
        // walks through the new screens: championship, manage mode, team screen + save, podium,
        // championship standings, load menu, highlights reel
        std::string out = argv[2];
        auto run = [&](float secs, const Pad& p) { int n = (int)(secs * 30); for (int i = 0; i < n; i++) { Pad q = p; g.frame(1.0f / 30, q); Pad z{}; (void)z; } };
        Pad none{};
        g.t = 2.5f; shot(g, out + "/s_title.ppm");
        remove(eraSave("champ", g.era));
        press(g, &Pad::down); press(g, &Pad::cross);                     // championship
        fprintf(stderr, "screen %d\n", (int)g.screen);
        if (g.screen == SCR_CHAMP) { press(g, &Pad::down); press(g, &Pad::cross); }   // new season
        press(g, &Pad::cross);                                            // car
        g.set.raceMin = getenv("EMR_MIN") ? atoi(getenv("EMR_MIN")) : 5; g.set.opponents = 11;
        shot(g, out + "/s_settings.ppm");
        g.sel = 13; press(g, &Pad::cross);                                // start season
        shot(g, out + "/s_champ0.ppm");
        g.sel = 0; press(g, &Pad::cross);                                 // round 1
        fprintf(stderr, "round start screen %d track %s timeScale %.1f\n", (int)g.screen, TRACKS[g.race.set.track].id, g.race.timeScale);
        // manage mode: the team drives our car, x16, and we call it in
        g.race.player().plan.aiDrives = true;
        g.race.timeAccel = 16;
        float simT = 0;
        while (g.screen == SCR_RACE && simT < 200) { g.frame(1.0f / 30, none); simT += 1.0f / 30; if (simT > 6 && simT < 6.04f) { pc::saveShot((out + "/s_manage.ppm").c_str()); g.wallView = false; } if (simT > 7 && simT < 7.04f) { pc::saveShot((out + "/s_manage_tv.ppm").c_str()); g.wallView = true; } if (simT > 30 && simT < 30.04f) { pc::saveShot((out + "/s_wall2.ppm").c_str()); g.race.player().wantPit = true; } }
        fprintf(stderr, "after manage: screen %d t %.0f\n", (int)g.screen, g.race.time);
        if (g.screen == SCR_TEAM) {
            shot(g, out + "/s_team.ppm");
            g.sel = 4; press(g, &Pad::cross);                             // save to slot 1
            shot(g, out + "/s_team_saved.ppm");
            // manual pit stop: we work the prompts (perfect timing on the first ones, a fumble on one)
            g.race.player().plan.aiDrives = false; g.race.player().plan.nextDriver = 0; g.race.player().plan.swapDriver = g.race.player().driver != 0;
            g.sel = 5; press(g, &Pad::cross);
            int prompts = 0; bool shotTaken = false;
            for (int f = 0; f < 3000 && g.race.player().pit == PIT_STOP; f++) {
                Pad q{};
                const PitQte& qt = g.race.qte;
                if (qt.waiting && qt.pos > (qt.p0 + qt.p1) * 0.5f) {
                    if (!shotTaken && prompts == 2) { pc::saveShot((out + "/s_qte.ppm").c_str()); shotTaken = true; }
                    bool fumble = prompts == 3;
                    int b = fumble ? (qt.button + 1) % 4 : qt.button;
                    if (b == QB_CROSS) q.cross = true;
                    if (b == QB_CIRCLE) q.circle = true;
                    if (b == QB_SQUARE) q.square = true;
                    if (b == QB_TRIANGLE) q.triangle = true;
                    prompts++;
                }
                g.frame(1.0f / 30, q);
                if (f % 2 == 1 && !qt.waiting && qt.flash > 0.6f && prompts == 4) pc::saveShot((out + "/s_qte_result.ppm").c_str());
            }
            fprintf(stderr, "manual stop: %d prompts, perfect %d good %d fumbled %d, last message: %s\n", prompts, g.race.qte.perfect, g.race.qte.good, g.race.qte.miss, g.race.msgs.empty() ? "" : g.race.msgs[0].text);
            g.race.player().plan.aiDrives = true;
        }
        simT = 0;
        while (g.screen == SCR_RACE && simT < 2000) { g.frame(1.0f / 30, none); simT += 1.0f / 30; if (g.screen == SCR_TEAM) { g.sel = 5; press(g, &Pad::cross); } }
        fprintf(stderr, "race done: screen %d time %.0f\n", (int)g.screen, g.race.time);
        shot(g, out + "/s_results.ppm");
        press(g, &Pad::cross);
        run(4, none); shot(g, out + "/s_podium.ppm");
        press(g, &Pad::cross);
        shot(g, out + "/s_champ1.ppm");
        press(g, &Pad::circle);                                           // title
        g.sel = 2; press(g, &Pad::cross); shot(g, out + "/s_load.ppm");
        g.sel = 0; press(g, &Pad::cross);
        fprintf(stderr, "loaded: screen %d atBox %d\n", (int)g.screen, g.race.playerAtBox());
        g.frame(1.0f / 30, none);
        shot(g, out + "/s_loaded.ppm");
        g.sel = 7; press(g, &Pad::cross);                                 // retire to menu
        g.idle = 30; g.frame(1.0f / 30, none);
        run(10, none); shot(g, out + "/s_reel.ppm");
        fprintf(stderr, "reel screen %d\n", (int)g.screen);
        return 0;
    }
    if (mode == "setupeng") {
        // setupeng <track> [wing gearing susp]: practice laps (the AI driving our car) with a deliberately bad setup,
        // then the setup engineer's report; checks she asks for less wing / longer gearing on a fast track
        std::string tid = argc > 2 ? argv[2] : "lemans";
        for (int i = 0; i < TRACK_COUNT; i++) if (tid == TRACKS[i].id) g.set.track = i;
        g.set.car = 1; g.set.opponents = 0; g.set.weather = WX_DRY; g.set.weekend = true; g.set.sessionMin = 60;
        g.aiDrivesPlayer = true;
        g.startRace();
        if (g.screen != SCR_RACE) { fprintf(stderr, "session did not start\n"); return 1; }
        CarSetup bad = g.race.player().su;
        bad.wing = argc > 3 ? atoi(argv[3]) : 5; bad.gearing = argc > 4 ? atoi(argv[4]) : 0; if (argc > 5) bad.susp = atoi(argv[5]);
        g.race.player().su = bad;
        setupeng::Report rp;
        int lastShown = -1;
        for (int k = 0; k < 60 * 60 * 30 && setupeng::cleanLaps() < 3; k++) {
            g.race.update(1.0f / 60, 0, 0, 0, 0, true);
            if (setupeng::cleanLaps() != lastShown) { lastShown = setupeng::cleanLaps(); fprintf(stdout, "t %.0f s: lap %d, %d clean laps\n", g.race.time, g.race.player().lap, lastShown); }
        }
        rp = setupeng::analyse(bad, g.race.player().tyre);
        const setupeng::Lap& a = rp.avg;
        fprintf(stdout, "laps %d  lap %.1f s  limiter %.3f  flat-out %.3f  top gear %d/%d  under e/m/x %.3f %.3f %.3f  over e/x %.3f %.3f  fast slide %.3f  lock F/R %.3f %.3f  kerb %.3f  tyres %.0f C\n",
                rp.laps, a.time, a.limiter, a.flatOut, a.topGearUsed, a.topGear, a.underEntry, a.underMid, a.underExit, a.overEntry, a.overExit, a.fastSlide, a.lockF, a.lockR, a.kerbHop, a.tyreT);
        bool wingLess = false, gearLonger = false;
        for (int i = 0; i < rp.n; i++) {
            fprintf(stdout, "  %d. %-55s (score %.2f)\n", i + 1, setupeng::text(rp.sug[i]), rp.score[i]);
            if (rp.sug[i] == setupeng::SG_WING_LESS) wingLess = true;
            if (rp.sug[i] == setupeng::SG_GEAR_LONGER) gearLonger = true;
        }
        if (rp.obs) fprintf(stdout, "  observation: %s\n", setupeng::obsText(rp.obs));
        bool okk = wingLess || gearLonger;
        fprintf(stdout, "setupeng: %s (less wing %d, longer gearing %d)\n", okk ? "ok" : "FAIL", (int)wingLess, (int)gearLonger);
        // the garage: ask her in person (screenshot of the subtitle and the X apply / O ignore offer)
        if (argc > 6) {
            std::string out = argv[6];
            g.set.setup = bad;
            g.race.toGarage(); g.screen = SCR_GARAGE; g.setupPage = true; g.setupTab = 0; g.setupSel = 12;
            Pad x{}; x.cross = true; g.frame(1.0f / 30, x);
            for (int k = 0; k < 150; k++) { Pad z{}; g.frame(1.0f / 30, z); }
            shot(g, out + "/setupeng_offer.ppm");
            fprintf(stdout, "offer pending %d: %s\n", (int)g.engUi.pending(), g.engUi.sub);
            int w0 = g.set.setup.wing;
            Pad x2{}; x2.cross = true; g.frame(1.0f / 30, x2);
            for (int k = 0; k < 60; k++) { Pad z{}; g.frame(1.0f / 30, z); }
            shot(g, out + "/setupeng_applied.ppm");
            fprintf(stdout, "after X: wing %d -> %d, pending %d: %s\n", w0, g.set.setup.wing, (int)g.engUi.pending(), g.engUi.sub);
        }
        return okk ? 0 : 1;
    }
    if (mode == "weekend") {
        // practice -> qualifying -> race with 1-minute sessions; checks music and the grid
        std::string tid = argv[2];
        for (int i = 0; i < TRACK_COUNT; i++) if (tid == TRACKS[i].id) g.set.track = i;
        g.set.car = atoi(argv[3]); g.set.laps = atoi(argv[4]); g.set.opponents = atoi(argv[5]);
        std::string out = argv[6];
        g.set.weekend = true; g.set.sessionMin = getenv("EMR_SESS") ? atoi(getenv("EMR_SESS")) : 1; g.sessionClamp = false;
        g.aiDrivesPlayer = true;
        g.startRace();
        const char* names[3] = {"practice", "qualifying", "race"};
        for (int ses = 0; ses < 3; ses++) {
            std::vector<int16_t> wav;
            float simT = 0, musicSum = 0; int musicN = 0;
            if (ses == 2) {
                fprintf(stdout, "race grid:");
                std::vector<const Car*> grid(g.race.cars.size());
                for (auto& c : g.race.cars) fprintf(stdout, " #%d", c.number);
                fprintf(stdout, "\n");
            }
            while (g.screen == SCR_RACE && simT < 1200) {
                Pad q{};
                g.frame(1.0f / 30, q);
                if (pc::mixFn && simT > 3 && simT < 13) {
                    std::vector<int16_t> b(1470 * 2); pc::mixFn(b.data(), 1470); wav.insert(wav.end(), b.begin(), b.end());
                    musicSum += sound::musicLevel(); musicN++;
                }
                if (simT > 20 && simT < 20.04f) { pc::saveShot((out + "/weekend_" + names[ses] + ".ppm").c_str()); }
                simT += 1.0f / 30;
            }
            writeWav((out + "/weekend_" + names[ses] + ".wav").c_str(), wav);
            fprintf(stdout, "%s: %.0f s, music level %.2f, results:\n", names[ses], g.race.time, musicN ? musicSum / musicN : 0);
            std::vector<const Car*> order(g.race.cars.size());
            for (auto& c : g.race.cars) order[c.position - 1] = &c;
            for (auto* c : order) { char b[16]; formatTime(b, c->bestLap); fprintf(stdout, "  P%-2d #%-3d %-9s %s %s%s\n", c->position, c->number, c->def->make, b, c->def->cls == CLASS_HYPER ? "HY" : "GT", c->player ? " <- player" : ""); }
            if (ses == 1) { fprintf(stdout, "qualifying order:"); for (int n : g.race.qualifyingOrder()) fprintf(stdout, " #%d", n); fprintf(stdout, "\n"); }
            Pad x{}; x.cross = true; g.frame(1.0f / 30, x);
            Pad z{}; g.frame(1.0f / 30, z);
        }
        return 0;
    }
    if (mode == "crewreel") {
        // crewreel <outdir> [frames]: four crew members on the grid, one per motion (idle, wheel gun,
        // push, celebrate), 20 frames a second, for a preview of the animations
        std::string out = argv[2];
        int frames = argc > 3 ? atoi(argv[3]) : 60;
        for (int i = 0; i < TRACK_COUNT; i++) if (!strcmp(TRACKS[i].id, "imola")) g.set.track = i;
        g.set.car = 1; g.set.laps = 1; g.set.opponents = 0; g.set.weather = 0;
        g.aiDrivesPlayer = true;
        g.startRace();
        if (g.screen != SCR_RACE) { fprintf(stderr, "race did not start\n"); return 1; }
        const Track& T = g.race.track;
        V3 p, t, l; float hw; T.frame(T.wrap(-60), p, t, l, hw);
        Camera cam; cam.up = V3(0, 0, 1); cam.fov = 50;
        V3 mid = p + l * 2.0f;
        cam.eye = mid - t * 0.4f - l * 6.2f + V3(0, 0, 1.5f);
        cam.fwd = norm(mid + V3(0, 0, 0.75f) - cam.eye);
        for (int f = 0; f < frames; f++) {
            float time = f / 20.0f;
            gfx::beginFrame(RGBA(150, 180, 220));
            g.R.camRight = norm(cross(cam.fwd, cam.up)); g.R.camUp = cross(g.R.camRight, cam.fwd);
            g.R.drawScene(g.race, cam, true);
            for (int k = 0; k < 4; k++) {
                V3 at = mid + t * ((k - 1.5f) * 1.45f);
                at.z = p.z;
                // turned to show each motion best: idle three quarters on, wheel gun and push side on,
                // the celebration facing the camera
                static const float TURN[4] = {0.6f, 1.35f, 1.3f, 0.15f};
                float yaw = atan2f(-l.y, -l.x) + TURN[k];
                M4 place = M4::translate(at - cam.eye) * M4::rotZ(yaw);
                // each in a different team's colours
                static const char* TEAM[4] = {"m25_marchetti", "m25_papaya", "m25_sterne", "m25_enstone"};
                u32 tm = 0, ts = 0;
                for (int i = 0; i < CAR_COUNT; i++) if (!strcmp(CARS[i].id, TEAM[k])) g.R.teamColours(&CARS[i], tm, ts);
                g.R.drawPerson(place, k, time, true, tm, ts);
            }
            gfx::endFrame();
            char b[128]; snprintf(b, sizeof b, "%s/reel_%03d.ppm", out.c_str(), f);
            pc::saveShot(b);
        }
        return 0;
    }
    if (mode == "screel") {
        // screel <outdir> <era>: the safety car comes out; pictures of it from beside and behind
        std::string out = argv[2];
        int era = argc > 3 ? atoi(argv[3]) : 0;
        for (int i = 0; i < TRACK_COUNT; i++) if (!strcmp(TRACKS[i].id, "imola")) g.set.track = i;
        g.set.car = eraFirstCar(era); g.set.laps = 3; g.set.opponents = 7; g.set.weather = WX_DRY;
        g.aiDrivesPlayer = true;
        g.startRace();
        for (int i = 0; i < 60 * 25; i++) { Pad q{}; g.frame(1.0f / 60, q); }
        g.race.scCooldown = 0; g.race.testSafetyCar();
        int shot = 0;
        for (int i = 0; i < 60 * 40 && shot < 3; i++) {
            Pad q{}; g.frame(1.0f / 60, q);
            if (!g.race.sc.state || (i % 240) != 239) continue;
            const auto& sc = g.race.sc;
            Camera cam; cam.up = V3(0, 0, 1); cam.fov = 50;
            static const float SIDE[3] = {5.5f, -5.0f, 0.0f}, BACK[3] = {3.0f, -2.5f, -8.0f};
            cam.eye = sc.pos + sc.left * SIDE[shot] + sc.fwd * BACK[shot] + V3(0, 0, 1.4f);
            cam.fwd = norm(sc.pos + V3(0, 0, 0.6f) - cam.eye);
            gfx::beginFrame(RGBA(150, 180, 220));
            g.R.camRight = norm(cross(cam.fwd, cam.up)); g.R.camUp = cross(g.R.camRight, cam.fwd);
            g.R.drawScene(g.race, cam, false);
            gfx::endFrame();
            char b[128]; snprintf(b, sizeof b, "%s/sc_%d.ppm", out.c_str(), shot++);
            pc::saveShot(b);
        }
        fprintf(stderr, "safety car shots: %d\n", shot);
        return 0;
    }
    if (mode == "pitreel") {
        // pitreel <outdir> [era 0-3]: a car is called in on lap 1; from a fixed camera beside its box,
        // 20 frames a second from its approach (the crew waiting) until just after its release
        std::string out = argv[2];
        int era = argc > 3 ? atoi(argv[3]) : 1;
        const char* tid = getenv("EMR_TRACK") ? getenv("EMR_TRACK") : "imola";
        for (int i = 0; i < TRACK_COUNT; i++) if (!strcmp(TRACKS[i].id, tid)) g.set.track = i;
        g.set.car = eraFirstCar(era); g.set.laps = 3; g.set.opponents = 5; g.set.weather = WX_DRY;
        g.aiDrivesPlayer = true;
        g.startRace();
        if (g.screen != SCR_RACE) { fprintf(stderr, "race did not start\n"); return 1; }
        int who = -1;
        for (int i = 0; i < (int)g.race.cars.size(); i++) if (!g.race.cars[i].player) { who = i; break; }
        g.race.cars[who].wantPit = true;
        int f = 0; bool started = false, stopped = false; float leftAt = -1;
        for (int step = 0; step < 60 * 400 && f < 400; step++) {
            Pad q{}; g.frame(1.0f / 60, q);
            const Car& c = g.race.cars[who];
            float toBox = g.race.pitRel(g.race.boxS(c)) - g.race.pitRel(c.s);
            if (!started && c.pit == PIT_IN && toBox < 165) { started = true; fprintf(stderr, "car %d coming in at %.1f s\n", who, g.race.time); }
            if (c.pit == PIT_STOP && !stopped) {
                V3 bw = g.race.track.world(g.race.boxS(c), g.race.boxD);
                fprintf(stderr, "stop: car s %.1f d %.2f  box s %.1f d %.2f  car-box %.2f m\n", c.s, c.d, g.race.boxS(c), g.race.boxD, len(c.pos - bw));
            }
            if (c.pit == PIT_STOP) stopped = true;
            if (stopped && c.pit == PIT_OUT && leftAt < 0) leftAt = g.race.time;
            // the way out: how far the car stays from the pit wall while the wall is beside it
            if (stopped && (c.pit == PIT_OUT || (c.pit == PIT_NONE && leftAt > 0 && g.race.time - leftAt < 40))) {
                static float worst = 1e9f;
                float rel = g.race.pitRel(c.s), wallTo = g.race.pitRel(g.race.pitWallTo);
                float wallD = g.race.track.pts[0].hw + 2.3f;
                if (rel < wallTo + 1) { float m = c.d - (wallD + 0.25f) - 0.9f; if (m < worst) worst = m; }
                if (getenv("EMR_EXITLOG") && (step % 30) == 0) fprintf(stderr, "exit rel %.0f d %.2f wallTo %.0f wall %.2f\n", rel, c.d, wallTo, wallD + 0.25f);
                if (c.pit == PIT_NONE && worst < 1e8f) { fprintf(stderr, "closest to the pit wall on the way out: %.2f m %s\n", worst, worst < 0 ? "(HITS IT)" : "(clear)"); worst = 1e9f; break; }
            }
            if (leftAt > 0 && g.race.time - leftAt > 3.6f) { if (!getenv("EMR_EXIT")) break; if (g.race.time - leftAt > 40) break; continue; }
            if (!started || (step % 3)) continue;
            float bs = g.race.boxS(c);
            V3 p, t, l; float hw; g.race.track.frame(bs, p, t, l, hw);
            V3 box = g.race.track.world(bs, g.race.boxD);
            Camera cam; cam.up = V3(0, 0, 1); cam.fov = 58;
            cam.eye = box - l * 4.4f + t * 4.6f + V3(0, 0, 1.6f);
            cam.fwd = norm(box + t * 0.4f + V3(0, 0, 0.5f) - cam.eye);
            if (getenv("EMR_STAND")) {    // from the fast lane, looking at the pit wall stands
                cam.eye = box - l * 6.0f - t * 7.0f + V3(0, 0, 1.9f);
                cam.fwd = norm(box - l * 10.5f + t * 2.0f + V3(0, 0, 1.2f) - cam.eye);
            }
            if (getenv("EMR_TOP")) {      // from above the box, the car driving up the picture
                cam.eye = box + V3(0, 0, 15.0f) - t * 0.01f; cam.up = t;
                cam.fwd = norm(box - cam.eye);
            }
            gfx::beginFrame(RGBA(150, 180, 220));
            g.R.camRight = norm(cross(cam.fwd, cam.up)); g.R.camUp = cross(g.R.camRight, cam.fwd);
            g.R.drawScene(g.race, cam, false);
            gfx::endFrame();
            char b[128]; snprintf(b, sizeof b, "%s/pit_%03d.ppm", out.c_str(), f++);
            pc::saveShot(b);
        }
        if (!stopped) { fprintf(stderr, "the car never stopped\n"); return 1; }
        fprintf(stderr, "%d frames\n", f);
        return 0;
    }
    if (mode == "props") {
        // fl_test props <track id> <outdir>: a picture of every Unity AI building on the circuit
        std::string tid = argv[2], out = argv[3];
        for (int i = 0; i < TRACK_COUNT; i++) if (tid == TRACKS[i].id) g.set.track = i;
        g.set.car = 1; g.set.laps = 1; g.set.opponents = 3; g.set.weather = 1;
        g.aiDrivesPlayer = true;
        g.startRace();
        if (g.screen != SCR_RACE) { fprintf(stderr, "race did not start\n"); return 1; }
        if (getenv("EMR_FCY")) { g.race.scCooldown = 0; g.race.callFcy(60); for (int i = 0; i < 400; i++) { Pad q{}; g.frame(1.0f / 60, q); } fprintf(stderr, "sc %d fcy %.1f\n", g.race.sc.state, g.race.fcy); }   // VSC / SC out
        if (getenv("EMR_T")) g.race.time = (float)atof(getenv("EMR_T"));                                                   // (flags' phase)
        static const char* NM[PROP_COUNT] = {"grandstand", "pine", "round", "tyres", "billboard", "marshal", "control", "bridge", "tvtower", "hospitality",
                                             "gantry", "bleacher", "tent", "flagpanel", "crane", "tecpro"};
        int k = 0, count[PROP_COUNT] = {};
        for (const PropInst& p : g.race.track.props) count[p.model]++;
        for (int m = PROP_GANTRY; m < PROP_COUNT; m++) fprintf(stderr, "%-10s x%d\n", NM[m], count[m]);
        int shown[PROP_COUNT] = {};
        for (const PropInst& p : g.race.track.props) {
            if (getenv("EMR_ALL")) { if (shown[p.model]++ >= 2) continue; }    // EMR_ALL: two of every object, scenery too
            else if (p.model < PROP_CONTROL && p.model != PROP_GRANDSTAND && p.model != PROP_BILLBOARD) continue;
            else if ((p.model >= PROP_GANTRY || p.model == PROP_GRANDSTAND || p.model == PROP_BILLBOARD) && shown[p.model]++ >= 2) continue;    // the Blender trackside objects: two pictures each
            // stand on the track near it, looking at it
            int bi = 0; float bd = 1e18f;
            for (int i = 0; i < g.race.track.n; i++) { V3 d = g.race.track.pts[i].p - p.p; float dd = dot(d, d); if (dd < bd) { bd = dd; bi = i; } }
            const TrackPoint& q = g.race.track.pts[(bi + g.race.track.n - (p.model == PROP_BRIDGE ? 12 : 6)) % g.race.track.n];
            Camera cam; cam.eye = q.p + V3(0, 0, getenv("EMR_CAMZ") ? (float)atof(getenv("EMR_CAMZ")) : 3.0f); cam.up = V3(0, 0, 1); cam.fov = 60;
            if (getenv("EMR_NEAR")) {   // EMR_NEAR=<m>: stand that far from the object, between it and the track
                V3 d = q.p - p.p; d.z = 0; d = norm(d);
                cam.eye = p.p + d * (float)atof(getenv("EMR_NEAR")) + V3(0, 0, 2.5f);
            }
            cam.fwd = norm(p.p + V3(0, 0, p.model == PROP_BRIDGE ? 4.0f : 5.0f) - cam.eye);
            gfx::beginFrame(RGBA(150, 180, 220));
            g.R.camRight = norm(cross(cam.fwd, cam.up)); g.R.camUp = cross(g.R.camRight, cam.fwd);
            g.R.drawScene(g.race, cam, false);
            gfx::endFrame();
            char b[128]; snprintf(b, sizeof b, "%s/prop_%s_%02d.ppm", out.c_str(), NM[p.model], k++);
            pc::saveShot(b);
            fprintf(stderr, "%s at %.0f %.0f (%.0f m from the track)\n", NM[p.model], p.p.x, p.p.y, sqrtf(bd));
        }
        return 0;
    }
    if (mode == "padsteer") {   // padsteer <setting 0-2>: on a straight at speed, hold right 1 s, let go 1 s: the wheel angle
        int ps = atoi(argv[2]);
        g.set.track = 0; g.set.car = 3; g.set.laps = 3; g.set.opponents = 0;
        cfg.padSteer = ps;
        g.aiDrivesPlayer = true; g.startRace();
        for (float simT = 0; simT < 12; simT += 1.0f / 60) { Pad z{}; g.step(1.0f / 60, z); }
        g.aiDrivesPlayer = false;
        float simT = 0;
        for (int f = 0; f < 120; f++, simT += 1.0f / 60) {
            Pad pp{}; pp.right = f < 60; pp.r = true;
            g.step(1.0f / 60, pp);
            if (f % 6 == 5) printf("%.2f %.3f\n", simT + 1.0f / 60, g.steer);
        }
        return 0;
    }
    if (mode == "steer") {      // steer <track> <help 0-2>: AI drives 30 s, then full throttle + full stick lock for 3 s
        std::string tid = argv[2]; int help = atoi(argv[3]);
        for (int i = 0; i < TRACK_COUNT; i++) if (tid == TRACKS[i].id) g.set.track = i;
        g.set.car = 3; g.set.laps = 3; g.set.opponents = 0;
        cfg.steerHelp = help;
        g.aiDrivesPlayer = true;
        g.startRace();
        for (float simT = 0; simT < 30; simT += 1.0f / 60) { Pad z{}; g.step(1.0f / 60, z); }
        g.aiDrivesPlayer = false;
        const Car& me = g.race.player();
        float v0 = me.v * 3.6f; bool spun = false; float maxSlip = 0, maxDrift = 0;
        for (float simT = 0; simT < 3; simT += 1.0f / 60) {
            Pad pp{}; if (getenv("FL_DPAD")) pp.right = true; else pp.stickX = 1.0f; pp.r = true;
            g.step(1.0f / 60, pp);
            spun |= me.spinning; maxSlip = fmaxf(maxSlip, me.slip); maxDrift = fmaxf(maxDrift, fabsf(me.drift));
        }
        printf("help %d: from %.0f km/h  spun %s  max slide %.2f rad  now %.0f km/h\n", help, v0, spun ? "YES" : "no", maxDrift, me.v * 3.6f);
        return 0;
    }
    if (mode == "difflap") {
        // difflap <track> <car>: 5 AI opponents, 4 laps, at every AI level; the AI's best lap per level
        // and the spread. Each level up to Normal must be faster than the one below it.
        std::string tid = argv[2]; int car = atoi(argv[3]);
        static const int ORD[DIFF_COUNT] = {DIFF_LUDICROUS, DIFF_VERYEASY, DIFF_EASY, DIFF_NORMAL, DIFF_HARD};
        float prev = 0; bool ok = true;
        for (int k = 0; k < DIFF_COUNT; k++) {
            int d = ORD[k];
            for (int i = 0; i < TRACK_COUNT; i++) if (tid == TRACKS[i].id) g.set.track = i;
            g.set.car = car; g.set.laps = 4; g.set.opponents = 5; g.set.weather = WX_DRY; g.set.raceMin = 0;
            g.set.weekend = false; g.set.difficulty = d; g.aiDrivesPlayer = true;
            g.startRace();
            for (float t = 0; t < 900; t += 1.0f / 60) {
                Pad z{}; g.step(1.0f / 60, z); g.race.sfxEvents.clear();
                bool done = true;
                for (auto& c : g.race.cars) if (!c.player && c.lap < 4) done = false;
                if (done) break;
            }
            float best = 1e9f, worst = 0;
            for (auto& c : g.race.cars) if (!c.player && c.bestLap > 0) { best = fminf(best, c.bestLap); worst = fmaxf(worst, c.bestLap); }
            printf("difflap %-17s best %7.2f s  slowest %7.2f s\n", diffName(d), best, worst);
            if (k > 0 && d != DIFF_HARD && !(best < prev)) ok = false;   // (Normal vs Hard is within the cars' spread)
            prev = best;
        }
        printf("difflap %s\n", ok ? "OK: each level faster than the one below" : "FAIL: order");
        if (getenv("DIFF_SHOT")) {   // the race-settings tablet at the easiest level (text width check)
            g.set.difficulty = DIFF_LUDICROUS; g.screen = SCR_SETTINGS; g.sel = 3;
            for (int i = 0; i < 40; i++) { Pad q{}; g.frame(1.0f / 30, q); }
            shot(g, getenv("DIFF_SHOT"));
        }
        return ok ? 0 : 1;
    }
    if (mode == "nospin") {
        // nospin <track> <car> <help 0-2>: AI drives 30 s, then 25 s of abuse -- full lock flung side to
        // side, the throttle stabbed, the brakes slammed mid-corner, held lock into the barriers.
        // Strong help (2) must never spin.
        std::string tid = argv[2]; int car = atoi(argv[3]), help = atoi(argv[4]);
        for (int i = 0; i < TRACK_COUNT; i++) if (tid == TRACKS[i].id) g.set.track = i;
        g.set.car = car; g.set.laps = 5; g.set.opponents = 0; g.set.weather = WX_DRY;
        cfg.steerHelp = help;
        g.aiDrivesPlayer = true;
        g.startRace();
        for (float simT = 0; simT < 30; simT += 1.0f / 60) { Pad z{}; g.step(1.0f / 60, z); }
        g.aiDrivesPlayer = false;
        const Car& me = g.race.player();
        int spins = 0, hits = 0; bool was = false; float maxDrift = 0, prevV = me.v;
        unsigned rng = 12345;
        float side = 1, t = 0;
        for (float simT = 0; simT < 25; simT += 1.0f / 60, t += 1.0f / 60) {
            rng = rng * 1103515245u + 12345u;
            if (t > 0.35f + (rng >> 24) / 255.0f * 0.5f) { side = -side; t = 0; }     // fling the wheel the other way
            Pad pp{};
            float phase = fmodf(simT, 6.0f);
            pp.stickX = phase < 4.5f ? side : 1.0f;                                  // (and 1.5 s held into a barrier)
            bool stab = fmodf(simT, 1.3f) < 0.25f;
            if (stab || phase > 4.5f) pp.r = true;                                   // throttle stabs
            if (fmodf(simT, 2.7f) < 0.3f) { pp.r = false; pp.l = true; }             // brakes slammed
            g.step(1.0f / 60, pp);
            if (me.spinning && !was) spins++;
            was = me.spinning;
            if (prevV - me.v > 3.0f) hits++;
            prevV = me.v;
            maxDrift = fmaxf(maxDrift, fabsf(me.drift));
        }
        printf("nospin %s %s help %d: %d spins, biggest slide %.2f rad, %d hard hits\n", tid.c_str(), CARS[car].id, help, spins, maxDrift, hits);
        return help >= 2 && spins > 0 ? 1 : 0;
    }
    if (mode == "teamswap") {
        // teamswap <track> <car> <outdir>: we drive in, and at our stop hand the car to a team-mate --
        // the team screen's Driver row -- and go to the pit wall as the manager
        std::string tid = argv[2], out = argv[4];
        for (int i = 0; i < TRACK_COUNT; i++) if (tid == TRACKS[i].id) g.set.track = i;
        g.set.car = atoi(argv[3]); g.set.opponents = 7; g.set.raceMin = 8; g.set.weather = WX_DRY;
        g.mgrMode = false; g.aiDrivesPlayer = true;
        g.startRace();
        for (float simT = 0; simT < 25; simT += 1.0f / 30) { Pad z{}; g.frame(1.0f / 30, z); }
        g.race.player().wantPit = true;
        int bad = 0;
        // the AI brings it in; once it's in the pit lane we're the one in the car again, so the team screen opens
        for (int i = 0; i < 30 * 400 && g.screen != SCR_TEAM; i++) {
            if (g.race.player().pit != PIT_NONE) g.aiDrivesPlayer = false;
            Pad z{}; g.frame(1.0f / 30, z);
        }
        if (g.screen != SCR_TEAM) { printf("teamswap: the team screen never opened\n"); return 1; }
        if (g.sel != 5) { printf("teamswap: the team screen opened on row %d, not RELEASE\n", g.sel); bad++; }
        int drvBefore = g.race.player().driver;
        g.sel = 3; press(g, &Pad::right);
        const PitPlan& pl = g.race.player().plan;
        printf("teamswap: next driver %d (was %d), team drives %d\n", pl.nextDriver, drvBefore, (int)pl.aiDrives);
        if (pl.nextDriver == 0 || !pl.aiDrives) bad++;
        shot(g, out + "/team_swap.ppm", 0);
        g.sel = 5; press(g, &Pad::cross);
        for (int i = 0; i < 30 * 60 && g.race.player().pit != PIT_NONE; i++) { Pad z{}; g.frame(1.0f / 30, z); }
        for (int i = 0; i < 60; i++) { Pad z{}; g.frame(1.0f / 30, z); }
        printf("teamswap: after the stop -- driver %d, managing %d, camera %d (0 pit wall), screen %d\n", g.race.player().driver, (int)g.race.managing(), g.mgrCam, (int)g.screen);
        if (g.race.player().driver == drvBefore || g.race.player().driver == 0 || !g.race.managing() || g.mgrCam != 0) bad++;
        shot(g, out + "/team_wall.ppm", 0);
        printf("teamswap: %s\n", bad ? "FAILED" : "ok");
        return bad ? 1 : 0;
    }
    if (mode == "careerlen") {
        // careerlen <outdir>: a career at the real length; then the hub's NEXT row changes a running career's length
        std::string out = argv[2];
        int bad = 0;
        g.career.begin(0, ERA_HYPER, 1, -RACE_FULL);
        RaceSettings s = g.career.champ.roundSettings();
        printf("careerlen: full -> round 1 %s: raceMin %d laps %d\n", TRACKS[s.track].id, s.raceMin, s.laps);
        if (s.raceMin < RACE_FULL) bad++;
        g.screen = SCR_CAREER; g.careerPage = 0; g.sel = 0;
        printf("careerlen: before %d screen %d page %d sel %d\n", g.career.raceMin, (int)g.screen, g.careerPage, g.sel);
        press(g, &Pad::left);                                             // full -> 3 h a 6 h
        printf("careerlen: after %d screen %d page %d sel %d\n", g.career.raceMin, (int)g.screen, g.careerPage, g.sel);
        s = g.career.champ.roundSettings();
        printf("careerlen: hub left -> career %d, round 1: raceMin %d laps %d\n", g.career.raceMin, s.raceMin, s.laps);
        if (g.career.raceMin != -180 || s.raceMin <= 0 || s.raceMin >= RACE_FULL) bad++;
        shot(g, out + "/career_hub.ppm", 0);
        press(g, &Pad::right);
        if (g.career.raceMin != -RACE_FULL) bad++;
        g.career.begin(0, ERA_HYPER, 1, 10);                              // laps still work
        s = g.career.champ.roundSettings();
        printf("careerlen: 10 laps -> raceMin %d laps %d\n", s.raceMin, s.laps);
        if (s.laps != 10 || s.raceMin != 0) bad++;
        printf("careerlen: %s\n", bad ? "FAILED" : "ok");
        return bad ? 1 : 0;
    }
    if (mode == "replay") {     // replay <track> <outdir>: 40 s of racing, then the replay (TV + cockpit), then photo mode
        std::string tid = argv[2], out = argv[3];
        for (int i = 0; i < TRACK_COUNT; i++) if (tid == TRACKS[i].id) g.set.track = i;
        g.set.car = 3; g.set.laps = 2; g.set.opponents = 11;
        g.aiDrivesPlayer = true;
        g.startRace();
        for (float simT = 0; simT < 44 && g.screen == SCR_RACE; simT += 1.0f / 30) { Pad z{}; g.step(1.0f / 30, z); }
        fprintf(stderr, "recorded %d frames of %d cars\n", g.repCount, g.repCars);
        Pad z{};
        g.screen = SCR_PAUSE; g.sel = 7; press(g, &Pad::cross);
        if (g.screen != SCR_REPLAY) { fprintf(stderr, "replay did not start\n"); return 1; }
        for (int f = 0; f < 150; f++) g.frame(1.0f / 30, z);        // 5 s in
        shot(g, out + "/replay_tv.ppm", 1.0f / 30);
        press(g, &Pad::triangle); press(g, &Pad::triangle); press(g, &Pad::triangle);   // cockpit
        for (int f = 0; f < 30; f++) g.frame(1.0f / 30, z);
        shot(g, out + "/replay_cockpit.ppm", 1.0f / 30);
        press(g, &Pad::right);                                     // another car
        press(g, &Pad::triangle);                                  // TV again
        for (int f = 0; f < 30; f++) g.frame(1.0f / 30, z);
        shot(g, out + "/replay_tv2.ppm", 1.0f / 30);
        float before = g.race.cars[g.repLivePlayer].s;
        press(g, &Pad::square);
        if (g.screen != SCR_PHOTO) { fprintf(stderr, "photo mode did not start\n"); return 1; }
        Pad turn{}; turn.stickX = 1; turn.stickY = -0.6f;
        for (int f = 0; f < 25; f++) g.frame(1.0f / 30, turn);
        press(g, &Pad::cross);                                     // hide the help
        shot(g, out + "/photo.ppm", 1.0f / 30);
        press(g, &Pad::circle); press(g, &Pad::circle);            // photo -> replay -> pause
        fprintf(stderr, "back on screen %d; player restored: %s\n", (int)g.screen, g.race.playerIdx == g.repLivePlayer && g.repLive.empty() ? "yes" : "no");
        (void)before;
        return 0;
    }
    if (mode == "sfx") {
        // sfx <out.wav> <name>...: plays each effect (sound::play / radio) through the mixer, one after another
        std::vector<int16_t> wav;
        sound::init();
        for (int i = 3; i < argc; i++) {
            if (!strncmp(argv[i], "radio_", 6)) sound::radio(argv[i] + 6); else sound::play(argv[i], 1.0f, 0);
            for (int f = 0; f < 44100 * 3 / 512; f++) { std::vector<int16_t> b(1024); pc::mixFn(b.data(), 512); wav.insert(wav.end(), b.begin(), b.end()); }
        }
        writeWav(argv[2], wav);
        return 0;
    }
    if (mode == "reliab") {
        // reliab <era> <laps> [runs]: whole races with the AI in every car; how often things break and who retires
        int era = atoi(argv[2]), laps = atoi(argv[3]), runs = argc > 4 ? atoi(argv[4]) : 1;
        int ret[MF_COUNT + 1] = {}, fail[MF_COUNT] = {}, cars = 0, flats = 0; float sumDmg = 0;
        for (int r = 0; r < runs; r++) {
            g.set.track = 0; g.set.car = eraFirstCar(era); g.set.laps = laps; g.set.opponents = 19; g.set.weather = WX_DRY;
            g.set.weekend = false; g.set.startType = START_STANDING;
            g.aiDrivesPlayer = true; g.startRace();
            if (g.screen != SCR_RACE) { fprintf(stderr, "race did not start\n"); return 1; }
            float simT = 0; std::string lastMsg;
            while (g.screen == SCR_RACE && !g.race.over && simT < 3 * 3600) {
                Pad p{}; g.screen = SCR_RACE; g.step(1.0f / 30, p); simT += 1.0f / 30;
                if (!g.race.msgs.empty() && lastMsg != g.race.msgs[0].text) { lastMsg = g.race.msgs[0].text; if (strstr(lastMsg.c_str(), "out:") || strstr(lastMsg.c_str(), "slowing") || strstr(lastMsg.c_str(), "RETIRED")) fprintf(stdout, "  [%5.0f s lap %d] %s\n", g.race.time - g.race.greenTime, g.race.player().lap, lastMsg.c_str()); }
            }
            for (auto& c : g.race.cars) {
                cars++; sumDmg += c.damage();
                if (c.rel.retired) ret[c.rel.cause < 0 ? MF_COUNT : c.rel.cause]++;
                for (int k = 0; k < MF_COUNT; k++) if (c.rel.fails & (1u << k)) fail[k]++;
                if (c.rel.flat >= 0) flats++;
            }
            fprintf(stdout, "run %d: %.0f s, consumeScale %.2f, leader laps %d\n", r, g.race.time - g.race.greenTime, g.race.consumeScale, g.race.cars[0].lap);
            g.race.end();
        }
        int nret = 0; for (int k = 0; k <= MF_COUNT; k++) nret += ret[k];
        printf("era %d: %d cars, %d retired (%.0f %%), mean damage %.2f, flats at the end %d\n", era, cars, nret, 100.0f * nret / cars, sumDmg / cars, flats);
        for (int k = 0; k < MF_COUNT; k++) printf("  %-28s failed %2d  retired %2d\n", MECH_NAME[k], fail[k], ret[k]);
        return 0;
    }
    if (mode == "manager") {
        // team manager <track> <car> <outdir>: the race from the pit wall -- orders for both cars, the
        // three cameras, a stop on the manager's plan, then DRIVE when our car stands in its box
        std::string tid = argv[2], out = argv[4];
        for (int i = 0; i < TRACK_COUNT; i++) if (tid == TRACKS[i].id) g.set.track = i;
        g.set.car = atoi(argv[3]); g.set.opponents = 19; g.set.raceMin = 8; g.set.weather = WX_DRY;
        g.mgrMode = true; g.aiDrivesPlayer = false;
        g.startRace();
        if (g.screen != SCR_RACE || !g.race.managing()) { printf("manager: did not start managing\n"); return 1; }
        auto run = [&](float sec) { for (float t = 0; t < sec && g.screen == SCR_RACE; t += 1.0f / 30) { Pad z{}; g.frame(1.0f / 30, z); } };
        int bad = 0;
        int mate = g.race.teamCar(1);
        printf("manager: our cars #%d (idx %d) and #%d (idx %d), driver in ours: %d, AI drives: %d\n", g.race.player().number, g.race.playerIdx,
               mate >= 0 ? g.race.cars[mate].number : 0, mate, g.race.player().driver, (int)g.race.player().plan.aiDrives);
        if (mate < 0) bad++;
        run(45);
        const char* camn[3] = {"wall", "tv", "dash"};
        for (int cm = 0; cm < 3; cm++) { g.mgrCam = cm; g.R.tvInit = false; run(0.5f); shot(g, out + "/mgr_" + camn[cm] + ".ppm", 0); }
        g.mgrCam = 0;
        // the left screen (d-pad left from our first car) and its MAP tab (R), then back to TIMING (L)
        press(g, &Pad::left); press(g, &Pad::r); run(0.3f); shot(g, out + "/mgr_wall_map.ppm", 0);
        if (g.mgrFocus != 0 || g.mgrTab[0] != 1) { printf("manager: left + R did not show the map (focus %d tab %d)\n", g.mgrFocus, g.mgrTab[0]); bad++; }
        press(g, &Pad::l);
        // our first car's DRIVERS tab
        press(g, &Pad::right); press(g, &Pad::r); run(0.3f); shot(g, out + "/mgr_wall_drivers.ppm", 0);
        if (g.mgrFocus != 1 || g.mgrTab[1] != 1) { printf("manager: no DRIVERS tab (focus %d)\n", g.mgrFocus); bad++; }
        press(g, &Pad::l);
        // no button may reach the car's own controls while managing, whatever they're bound to: every
        // action bound in turn to each button, then that button pressed
        {
            Car& me = g.race.player();
            int cam0 = g.cam, map0 = me.su.map; bool pit0 = me.wantPit;
            int keepRow = g.mgrRow; g.mgrRow = 5;            // (the STRATEGY row: X toggles it, undone below)
            bool Pad::*BTN[] = {&Pad::select, &Pad::up, &Pad::down};
            int saved[ACT_COUNT]; for (int a = 0; a < ACT_COUNT; a++) saved[a] = cfg.bind[a];
            for (int a = 0; a < ACT_COUNT; a++) for (int bt = 0; bt < 3; bt++) {
                for (int z = 0; z < ACT_COUNT; z++) cfg.bind[z] = BTN_NONE;
                cfg.bind[a] = bt == 0 ? BTN_SELECT : (bt == 1 ? BTN_UP : BTN_DOWN);
                press(g, BTN[bt]);
            }
            for (int a = 0; a < ACT_COUNT; a++) saved[a] = saved[a], cfg.bind[a] = saved[a];
            g.mgrRow = keepRow;
            bool same = g.cam == cam0 && me.su.map == map0 && me.wantPit == pit0 && !g.race.drsPress && g.race.managing();
            printf("manager: buttons with every binding while managing: %s\n", same ? "no effect on the car" : "CHANGED THE CAR");
            if (!same) bad++;
        }
        // the team-mate (d-pad right to its screen): push, then box this lap for wets and a full tank
        press(g, &Pad::right);
        if (g.mgrSel != 1 || g.mgrFocus != 2) { printf("manager: right did not focus the second car\n"); bad++; }
        g.mgrRow = 0; press(g, &Pad::cross);
        int k = g.race.teamCar(1);
        printf("manager: #%d pace map %d\n", g.race.cars[k].number, g.race.cars[k].su.map);
        if (g.race.cars[k].su.map != MAP_ATTACK) bad++;
        g.mgrRow = 2; for (int i = 0; i < 6; i++) press(g, &Pad::cross); press(g, &Pad::square);   // TEAM -> S M H I W NO CHANGE, back to W
        g.mgrRow = 3; for (int i = 0; i < 4; i++) press(g, &Pad::cross);     // TEAM -> 25 .. 100
        g.mgrRow = 1; press(g, &Pad::cross);
        const MgrOrders& o = g.race.cars[k].mgr;
        printf("manager: orders tyre %d fuel %d auto %d, box %d\n", o.tyre, o.fuel, (int)o.autoStrat, (int)g.race.cars[k].wantPit);
        shot(g, out + "/mgr_wall_orders.ppm", 0);
        int stops0 = g.race.cars[k].stops;
        for (int i = 0; i < 400 && g.race.cars[k].stops == stops0; i++) run(1);
        for (int i = 0; i < 60 && g.race.cars[k].pit != PIT_NONE; i++) run(1);
        printf("manager: #%d stopped (%d stops) and left on %s, orders now tyre %d\n", g.race.cars[k].number, g.race.cars[k].stops, TYRE_NAME[g.race.cars[k].tyre], g.race.cars[k].mgr.tyre);
        if (g.race.cars[k].tyre != TY_WET || g.race.cars[k].mgr.tyre != -1) bad++;
        // our car: box, then DRIVE in the box
        press(g, &Pad::left);
        g.mgrRow = 1; press(g, &Pad::cross);
        bool took = false;
        for (int i = 0; i < 400 * 30 && !took && g.screen == SCR_RACE; i++) {
            Pad z{}; g.frame(1.0f / 30, z);
            if (g.race.canTakeOver(g.race.playerIdx)) { g.mgrRow = 6; press(g, &Pad::cross); took = true; }
        }
        printf("manager: DRIVE %s -- managing now %d, our driver %d\n", took ? "pressed in the box" : "never possible", (int)g.race.managing(), g.race.player().driver);
        if (!took || g.race.managing() || g.race.player().driver != 0) bad++;
        run(20);
        g.cam = 0; shot(g, out + "/mgr_after_drive.ppm", 0);
        printf("manager: %s\n", bad ? "FAILED" : "ok");
        return bad ? 1 : 0;
    }
    if (mode == "cam") {
        // cam <track id> <outdir>: the race camera follows the (AI-driven) player car at varying frame rates.
        // Prints the chase camera's distance from the car (it must hold steady at any speed / frame time),
        // the look-to-apex angle in the corners, then shots: chase at speed and the cockpit in a corner.
        std::string tid = argv[2], out = argv[3];
        for (int i = 0; i < TRACK_COUNT; i++) if (tid == TRACKS[i].id) g.set.track = i;
        g.set.car = 0; g.set.laps = 2; g.set.opponents = 5;
        g.aiDrivesPlayer = true;
        g.startRace();
        if (g.screen != SCR_RACE) { fprintf(stderr, "race did not start\n"); return 1; }
        float simT = 0; const float dt = 1.0f / 30;
        float dmin = 1e9f, dmax = 0, vmax = 0, amax = 0; int bad = 0, nshot = 0; float lastShot = -100;
        const float dts[3] = {1.0f / 30, 1.0f / 60, 1.0f / 144};
        int k = 0;
        while (g.screen == SCR_RACE && simT < 150) {
            Pad p{};
            g.screen = SCR_RACE; g.step(dt, p); simT += dt;
            if (simT < 25) continue;                       // (the start)
            const Car& c = g.race.player();
            // the camera stepped in sub-frames of a varying length over this 1/30 s
            float fdt = dts[(k++ / 90) % 3]; int sub = (int)(dt / fdt + 0.5f);
            Camera cam;
            for (int j = 0; j < sub; j++) cam = g.R.camera(g.race, CAM_CHASE, fdt, false);
            V3 d = cam.eye - c.pos; d.z = 0;
            float dist = sqrtf(dot(d, d));
            if (c.v > 20) { dmin = fminf(dmin, dist); dmax = fmaxf(dmax, dist); }
            vmax = fmaxf(vmax, c.v); amax = fmaxf(amax, fabsf(g.R.apexYaw));
            // the car must stay in front of the chase camera, near the middle of the picture
            V3 rel = c.pos - cam.eye; float fz = dot(rel, cam.fwd);
            if (fz < 2) bad++;
            // shots: at speed, and in a corner (the apex angle well over 5 degrees)
            const TrackPoint& tp = g.race.track.pts[(int)(g.race.track.wrap(c.s) / g.race.track.step) % g.race.track.n];
            bool corner = fabsf(g.R.apexYaw) > 0.09f && c.v > 25, fast = c.v > 75 && fabsf(tp.curv) > 1.0f / 900;
            if ((corner || fast) && simT - lastShot > 8 && nshot < 6) {
                char b[96];
                g.cam = CAM_CHASE; snprintf(b, sizeof b, "/cam_chase_%s_%d.ppm", tid.c_str(), nshot); shot(g, out + b, dt);
                g.cam = CAM_COCKPIT; for (int j = 0; j < 20; j++) { Pad z{}; g.frame(1.0f / 60, z); }
                snprintf(b, sizeof b, "/cam_cockpit_%s_%d.ppm", tid.c_str(), nshot); shot(g, out + b, dt);
                printf("shot %d at %.0f s: %.0f km/h, apex %.1f deg, curvature 1/%.0f m\n", nshot, simT, c.v * 3.6f, g.R.apexYaw * 57.3f, tp.curv != 0 ? 1 / tp.curv : 0);
                g.cam = CAM_CHASE; g.R.camInit = false; nshot++; lastShot = simT;
            }
        }
        printf("chase distance above 72 km/h: %.2f .. %.2f m (top speed %.0f km/h), largest apex angle %.1f deg, frames with the car behind the camera %d\n",
               dmin, dmax, vmax * 3.6f, amax * 57.3f, bad);
        return (dmax - dmin > 0.6f || bad) ? 1 : 0;
    }
    if (mode == "perf") {       // perf <track> <seconds> [opponents]: the full grid, the AI driving us, frames drawn at 1/60 s:
                                // CPU ms per frame for the update and the draw, draw calls, vertices, heap
        std::string tid = argv[2];
        for (int i = 0; i < TRACK_COUNT; i++) if (tid == TRACKS[i].id) g.set.track = i;
        float secs = argc > 3 ? (float)atof(argv[3]) : 20;
        g.set.opponents = argc > 4 ? atoi(argv[4]) : 39;
        g.set.laps = 3; g.set.car = 0; g.aiDrivesPlayer = true;
        double tl = sys::timeSec();
        g.startRace();
        fprintf(stderr, "perf: load %.2f s, heap %.1f MB, cars %d\n", sys::timeSec() - tl, sys::memUsedKB() / 1024.0, (int)g.race.cars.size());
        if (g.screen != SCR_RACE) { fprintf(stderr, "race did not start\n"); return 1; }
        if (getenv("EMR_CLOCK")) g.race.startHour = (float)atof(getenv("EMR_CLOCK"));
        for (int i = 0; i < 30 * 12; i++) { Pad p{}; g.screen = SCR_RACE; g.step(1.0f / 30, p); }   // the start and the first corners
        const char* cn[3] = {"chase", "far", "cockpit"};
        for (int cm = 0; cm < 3; cm++) {
            g.cam = cm; g.R.camInit = false;
            double up = 0, dr = 0, upMax = 0, drMax = 0, calls = 0, verts = 0, arenaMax = 0; int n = 0, misses = 0;
            int frames = (int)(secs * 60 / 3);
            for (int f = 0; f < frames; f++) {
                Pad p{}; g.screen = SCR_RACE; g.frame(1.0f / 60, p);
                if (f < 10) continue;
                up += g.lastUpdMs; dr += g.lastDrawMs; upMax = std::max(upMax, g.lastUpdMs); drMax = std::max(drMax, g.lastDrawMs);
                calls += perfstat::calls; verts += perfstat::verts; n++;
                arenaMax = std::max(arenaMax, (double)perfstat::arena); if (perfstat::arenaMiss) misses++;
            }
            fprintf(stderr, "perf %-8s %-7s update %6.2f ms (max %6.2f)  draw %6.2f ms (max %6.2f)  calls %6.0f  verts %8.0f  arena %.1f MB (full %d)  heap %.1f MB\n",
                    tid.c_str(), cn[cm], up / n, upMax, dr / n, drMax, calls / n, verts / n, arenaMax / 1048576.0, misses, sys::memUsedKB() / 1024.0);
        }
        // the update alone (what the Vita's CPU pays for every car, every frame)
        double t0 = sys::timeSec(); int un = 600;
        for (int i = 0; i < un; i++) { Pad p{}; g.screen = SCR_RACE; g.step(1.0f / 60, p); }
        fprintf(stderr, "perf %-8s sim     %6.3f ms per 1/60 s step (%d cars)\n", tid.c_str(), (sys::timeSec() - t0) * 1000 / un, (int)g.race.cars.size());
        return 0;
    }
    if (mode == "race") {
        std::string tid = argv[2];
        for (int i = 0; i < TRACK_COUNT; i++) if (tid == TRACKS[i].id) g.set.track = i;
        g.set.car = atoi(argv[3]);
        g.set.laps = atoi(argv[4]);
        g.set.opponents = atoi(argv[5]);
        std::string out = argv[6];
        std::vector<float> shots;
        for (int i = 7; i < argc; i++) shots.push_back((float)atof(argv[i]));
        if (getenv("EMR_DIFF")) g.set.difficulty = atoi(getenv("EMR_DIFF"));
        if (getenv("EMR_MIN")) g.set.raceMin = atoi(getenv("EMR_MIN"));       // timed race (real minutes)
        if (getenv("EMR_HOURS")) g.set.eventHours = (float)atof(getenv("EMR_HOURS"));
        if (getenv("EMR_WX")) g.set.weather = atoi(getenv("EMR_WX"));
        if (getenv("EMR_BLUEOFF") && *getenv("EMR_BLUEOFF")) Race::blueOff = true;
        if (getenv("EMR_NOHYBRID")) Race::hybridOff = true;
        if (getenv("EMR_START")) g.set.startType = atoi(getenv("EMR_START"));
        g.aiDrivesPlayer = true;
        g.startRace();
        if (g.screen != SCR_RACE) { fprintf(stderr, "race did not start\n"); return 1; }
        if (getenv("EMR_CLOCK")) g.race.startHour = (float)atof(getenv("EMR_CLOCK"));   // time of day at the start (22 = night)
        size_t si = 0;
        std::vector<int16_t> wav;
        float simT = -4;
        double updTime = 0; long updN = 0;
        const float dt = 1.0f / 30;
        while (g.screen == SCR_RACE && simT < 3600) {
            Pad p{};
            bool want = si < shots.size() && simT >= shots[si];
            if (want) {
                const char* names[3] = {"chase", "far", "cockpit"};
                for (int cm = 0; cm < 3; cm++) {
                    g.cam = cm;
                    g.R.camInit = false;
                    // EMR_WARM=n: n frames drawn before the shot (wiper, drops on the glass)
                    if (getenv("EMR_WARM") && cm == 2) for (int f = 0; f < atoi(getenv("EMR_WARM")); f++) { Pad z{}; g.frame(1.0f / 30, z); }
                    char b[64]; snprintf(b, sizeof b, "/race_%s_t%03d_%s.ppm", tid.c_str(), (int)shots[si], names[cm]);
                    shot(g, out + b, 0);
                }
                g.cam = CAM_CHASE;
                si++;
            }
            // update without drawing (fast)
            g.screen = SCR_RACE;
            { double t0 = sys::timeSec(); g.step(dt, p); updTime += sys::timeSec() - t0; updN++; }
            if (simT > 5 && simT < 13 && pc::mixFn) {
                int frames = (int)(44100 * dt);
                std::vector<int16_t> b(frames * 2);
                pc::mixFn(b.data(), frames);
                wav.insert(wav.end(), b.begin(), b.end());
            }
            simT += dt;
            // race control messages as they happen
            static std::string lastMsg;
            if (!g.race.msgs.empty() && lastMsg != g.race.msgs[0].text) {
                lastMsg = g.race.msgs[0].text;
                fprintf(stdout, "  [%6.1f s  clock %05.2f h  rain %.2f wet %.2f] %s\n", g.race.time, g.race.clockHour(), g.race.rain, g.race.wet, lastMsg.c_str());
            }
            if (getenv("EMR_SPOT")) {
                const Car& c = g.race.cars[1];
                float s0 = atof(getenv("EMR_SPOT"));
                if (c.s > s0 && c.s < s0 + 200 && c.lap < 2)
                    fprintf(stdout, "    s %.0f d %.2f hw %.1f v %.1f yaw %.2f slip %.2f curv %.4f line %.2f thr %.2f brk %.2f\n", c.s, c.d, g.race.track.pts[g.race.track.index(c.s)].hw, c.v, c.yaw, c.slip, g.race.track.curvAt(c.s), g.race.line[g.race.track.index(c.s)], c.throttle, c.brake);
            }
            if (getenv("EMR_TRACE")) {
                static int lastPit[64], lastLap[64];
                for (size_t i = 0; i < g.race.cars.size(); i++) {
                    const Car& c = g.race.cars[i];
                    if (c.number != atoi(getenv("EMR_TRACE"))) continue;
                    if (getenv("EMR_T0") && g.race.time > atof(getenv("EMR_T0")) && fmodf(g.race.time, 2.0f) < 1.0f / 30)
                        fprintf(stdout, "      t %.0f rel %.1f box %.1f v %.1f d %.2f yaw %.2f thr %.2f brk %.2f phase %d stuck %.1f\n", g.race.time, g.race.pitRel(c.s), g.race.pitRel(g.race.boxS(c)), c.v, c.d, c.yaw, c.throttle, c.brake, c.pitPhase, c.stuck);
                    if (c.pit != lastPit[i] || c.lap != lastLap[i])
                        fprintf(stdout, "    #%d t %.0f lap %d pit %d want %d tyre %s wear %.2f fuel %.2f wet %.2f plan %s/%d\n", c.number, g.race.time, c.lap, c.pit, c.wantPit, TYRE_NAME[c.tyre], c.wear, c.fuel, g.race.wet, TYRE_NAME[c.plan.tyre], c.plan.tyres);
                    lastPit[i] = c.pit; lastLap[i] = c.lap;
                }
            }
            if (getenv("EMR_YELLOW")) {   // 1: FCY at 30 s; 2: a slow zone 500 m ahead of the player at 30 s
                static bool called = false; static float maxIn = 0; static int zoneShots = 0; static float inT = 0;
                int mode = atoi(getenv("EMR_YELLOW"));
                if (!called && simT > 100 && !g.race.formation) { called = true; g.race.scCooldown = 0; if (mode == 1) g.race.callFcy(40); else g.race.addSlowZone(g.race.player().s + 500, 60); }
                bool any = false;
                for (auto& c : g.race.cars) if (c.pit == PIT_NONE && g.race.slowAt(c.s) && !c.player) { any = true; if (inT > 4) maxIn = fmaxf(maxIn, c.v * 3.6f); }
                inT = any ? inT + 1.0f / 30 : 0;
                if (called && zoneShots < 2 && g.race.slowAt(g.race.player().s) && inT > 6) {
                    for (int cm = 0; cm < 3; cm += 2) { g.cam = cm; g.R.camInit = false; char b[64]; snprintf(b, sizeof b, "/yellow%d_%s.ppm", mode, cm ? "cockpit" : "chase"); shot(g, out + b, 0); }
                    g.cam = CAM_CHASE; zoneShots = 2;
                }
                static int lastLog = -1;
                if ((int)simT % 10 == 0 && (int)simT != lastLog) { lastLog = (int)simT; fprintf(stderr, "t %3d fcy %.0f warn %.1f zones %d  AI max in zone %.0f km/h  player %.0f km/h pen %.0f\n", (int)simT, g.race.fcy, g.race.fcyWarn, (int)g.race.zones.size(), maxIn, g.race.player().v * 3.6f, g.race.player().penalty); }
            }
            if (getenv("EMR_BOARDSHOT") && simT > 3) {   // screenshots on the approach to the first few distance boards
                static int taken = 0; static float last = -100;
                const Car& me = g.race.player();
                for (const BrakeBoard& bd : g.race.track.boards) {
                    if (bd.metres != 300 || taken >= 3 || simT - last < 10) continue;
                    V3 rel = bd.p - me.pos; float ahead = dot(rel, me.fwd);
                    if (ahead > 28 && ahead < 40 && fabsf(dot(rel, me.left)) < 25) {
                        for (int cm = 0; cm < 3; cm += 2) {
                            g.cam = cm; g.R.camInit = false;
                            char b[64]; snprintf(b, sizeof b, "/board_%d_%s.ppm", taken, cm ? "cockpit" : "chase");
                            shot(g, out + b, 0);
                        }
                        g.cam = CAM_CHASE; taken++; last = simT;
                    }
                }
            }
            if (getenv("EMR_BLUESHOT")) {    // screenshots the first time the player's car is shown blue flags
                static int taken = 0; static float last = -99;
                const Car& me = g.race.player();
                if (taken < 3 && me.blue > 0 && me.blueGap < 70 && simT - last > 20 && me.pit == PIT_NONE) {
                    for (int cm = 0; cm < 2; cm++) {
                        g.cam = cm == 0 ? CAM_CHASE : CAM_COCKPIT; g.R.camInit = false;
                        char bf[64]; snprintf(bf, sizeof bf, "/blue_%d_%s.ppm", taken, cm ? "cockpit" : "chase");
                        shot(g, out + bf, 0);
                    }
                    g.cam = CAM_CHASE; taken++; last = simT;
                }
            }
            if (getenv("EMR_TRAFFIC")) {      // seconds a faster-class car spends stuck right behind a GT
                static double stuck = 0, frames = 0;
                for (auto& c : g.race.cars) {
                    if (c.def->cls != CLASS_HYPER || c.pit != PIT_NONE) continue;
                    for (auto& o : g.race.cars)
                        if (o.def->cls == CLASS_GT3 && o.pit == PIT_NONE) { float gp = g.race.lapGap(c.s, o.s); if (gp > 0 && gp < 12 && fabsf(o.d - c.d) < 2.5f) { stuck += dt; break; } }
                }
                frames += dt;
                if (g.race.over || simT + dt >= 3600) {}
                g.trafficStuck = stuck;
            }
            if (g.screen == SCR_RESULTS) break;
        }
        if (getenv("EMR_TRAFFIC")) {
            float dmg = 0; for (auto& c : g.race.cars) dmg += c.damage();
            fprintf(stdout, "traffic: hypercars stuck behind GTs %.1f car-seconds, total damage %.2f\n", g.trafficStuck, dmg);
        }
        writeWav((out + "/race_audio.wav").c_str(), wav);
        fprintf(stderr, "update cost: %.3f ms per 1/30 s frame (PC)\n", updTime * 1000 / (updN ? updN : 1));
        // results
        const Race& r = g.race;
        fprintf(stdout, "track %s  %.0f m  race time %.1f s\n", tid.c_str(), r.track.length, r.time);
        fprintf(stdout, "track rubber at the end %.2f (wet %.2f)\n", r.rubber, r.wet);
        std::vector<const Car*> order(r.cars.size());
        for (auto& c : r.cars) order[c.position - 1] = &c;
        for (auto* c : order) {
            char best[16]; formatTime(best, c->bestLap);
            fprintf(stdout, "P%-2d #%-3d %-10s %-12s laps %d  best %s  %s  stops %d tyre %s wear %.2f fuel %.2f dmg %.2f%s%s pen %.0f%s\n", c->position, c->number, c->def->make, c->def->model, c->lap, best,
                    c->def->cls == CLASS_HYPER ? "HY" : "GT", c->stops, TYRE_NAME[c->tyre], c->wear, c->fuel, c->damage(), c->noseOff ? " NOSE-OFF" : "", c->wingOff ? " WING-OFF" : "", c->penalty, c->player ? "  <- player" : "");
        }
        if (getenv("EMR_DRIVERS"))
            for (auto* c : order) {
                fprintf(stdout, "    #%-3d", c->number);
                for (int k = 0; k < DRIVERS_PER_CAR; k++) {
                    char nm[24]; Race::driverName(*c, k, nm, sizeof nm, true);
                    fprintf(stdout, "  %-18s %4.1f h tired %3d%%%s", nm, c->drv[k].hours, (int)(c->drv[k].fatigue * 100), k == c->driver ? "*" : " ");
                }
                fprintf(stdout, "\n");
            }
        return 0;
    }
    return 1;
}
