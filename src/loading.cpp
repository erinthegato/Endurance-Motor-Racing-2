// The loading slideshow: a session (the circuit, the field's cars, the pits) is built over several frames
// behind a full-screen slideshow -- the cars turning on the turntable as they come in, the circuit's map,
// a tip -- with a progress bar that follows the real steps. Shown only once loading has taken 0.3 s
// (a quick load goes straight in). Cheap: one car or one map on screen at a time.
#include "game.h"
#include <cstdio>
#include <cmath>
#include <algorithm>

static const u32 L_WHITE = RGBA(255, 255, 255), L_DIM = RGBA(175, 185, 200), L_ACC = RGBA(255, 150, 40);
static const float L_SHOW = 0.3f;     // loading takes longer than this: the slideshow comes up
static const float L_SLIDE = 4.0f;    // each picture's time on screen

static const char* const TIPS[] = {
    "Touch a corner of the screen in the race to call one of your four pit plans. Touch it again to call it off.",
    "Set your pit plans at the engineer's desk in the garage during practice.",
    "The setup engineer needs two clean laps before she can tell you what the car wants.",
    "Hypercars are faster everywhere: in a GT3, keep to your line and let them by.",
    "Track rubber builds through the weekend: the circuit is quickest by the race.",
    "Tyres work best around 1.80 bar hot. Cold pressures that are too low overheat on hot days.",
    "Press L to run when you're on foot. The right stick looks around.",
    "Under parc ferme the chassis settings are sealed: only brake balance, the map, tyres and fuel change.",
    "Rain is on the forecast screen at the session desk: 20 minutes ahead, an hour in all.",
};
static const int TIP_N = (int)(sizeof TIPS / sizeof TIPS[0]);

void Game::loadStep() {
    // as many steps as fit in ~25 ms each frame (the big one, the circuit, is a frame of its own)
    double f0 = sys::timeSec();
    while (ld.active) {
        switch (ld.step) {
        case 0:                                      // the settings; the last session goes
            ld.ses = sessionPrep(ld.ses, ld.s, ld.grid);
            ld.prog = 0.05f; ld.step = 1;
            return;                                  // (a frame of the slideshow before the circuit)
        case 1: {                                    // the circuit and the field
            R.freeCars(); menuCarShown = -1;         // (the menu's car goes first: never two scenes' cars at once)
            raceLoaded = race.start(ld.s, ld.ses, ld.grid.empty() ? nullptr : &ld.grid);
            if (!raceLoaded) { ld.active = false; screen = SCR_TRACK; return; }
            afterLoadBegin();
            ld.cars.clear();
            ld.cars.push_back((int)(race.player().def - CARS));          // yours first: the slideshow shows it
            for (const Car& c : race.cars) {
                int i = (int)(c.def - CARS);
                if (std::find(ld.cars.begin(), ld.cars.end(), i) == ld.cars.end()) ld.cars.push_back(i);
            }
            ld.carNext = 0; ld.prog = 0.45f; ld.step = 2;
            return;
        }
        case 2:                                      // the cars, a few a frame
            if (ld.carNext < (int)ld.cars.size()) {
                R.loadCar(ld.cars[ld.carNext++]);
                ld.prog = 0.45f + 0.45f * ld.carNext / (float)ld.cars.size();
            }
            if (ld.carNext >= (int)ld.cars.size()) ld.step = 3;
            break;
        case 3:                                      // the pits, the engine sounds
            afterLoadEnd();
            ld.prog = 0.97f; ld.step = 4;
            break;
        default:                                     // in: the garage or the grid
            ld.active = false;
            sessionFinish(ld.ses);
            return;
        }
        if (sys::timeSec() - f0 > 0.025) return;
    }
}

void Game::drawLoading() {
    gfx::beginFrame(RGBA(6, 8, 14));
    float el = (float)(sys::timeSec() - ld.t0);
    float a = fminf(1, fmaxf(0, (el - L_SHOW) / 0.25f));
    if (a <= 0) { finishFrame(); return; }           // (quick: nothing shown)
    ld.slideT += lastDt;
    if (ld.slideT > L_SLIDE) { ld.slideT -= L_SLIDE; ld.slide++; }
    ld.shown += (ld.prog - ld.shown) * fminf(1, lastDt * 8);
    // the picture: a car on the turntable (the ones loaded so far) or the circuit's map, in turn
    bool carSlide = (ld.slide & 1) == 0;
    int car = -1;
    if (carSlide) {
        int n = ld.step >= 2 ? ld.carNext : 0;
        if (n > 0) car = ld.cars[(ld.slide / 2) % n];
        else if (set.car >= 0 && set.car < CAR_COUNT && R.carLoaded[set.car]) car = set.car;   // (the menus' car, still in)
    }
    if (car >= 0) {
        R.drawCarShowroom(car, 2.55f + ld.slideT * 0.10f, 105, 6, 270, 270);     // (three-quarter front, turning slowly)
    } else if (raceLoaded) {
        gfx::setDepthTest(false); gfx::setBlend(true);
        R.drawMap(race.track, nullptr, SCREEN_W / 2 - 80, 46, 160, RGBA(220, 226, 236));
    }
    gfx::setDepthTest(false); gfx::setBlend(true); gfx::bindTexture(nullptr);
    // each picture fades in and out (a dip between them)
    float f = fminf(1, fminf(ld.slideT / 0.6f, (L_SLIDE - ld.slideT) / 0.6f));
    float dark = 1 - f * a;
    if (dark > 0.01f) gfx::drawFill2D(0, 0, SCREEN_W, SCREEN_H, RGBA(6, 8, 14, (int)(dark * 255)));
    // the circuit, the session, a tip, the progress
    auto fade = [&](u32 c) { return (c & 0x00FFFFFFu) | ((u32)(a * 255) << 24); };
    gfx::drawFill2D(0, 0, SCREEN_W, 40, RGBA(0, 0, 0, (int)(150 * a)));
    gfx::drawFill2D(0, 40, SCREEN_W, 2, fade(L_ACC));
    int tr = set.track >= 0 && set.track < TRACK_COUNT ? set.track : 0;
    R.font.draw(12, 6, TRACKS[tr].name, fade(L_WHITE), 1.1f);
    const char* sess = ttMode ? "TIME TRIAL" : (ld.ses == SES_PRACTICE ? "PRACTICE" : (ld.ses == SES_QUALI ? "QUALIFYING" : "RACE"));
    R.font.drawRight(SCREEN_W - 12, 6, sess, fade(L_ACC), 0.9f);
    R.font.drawRight(SCREEN_W - 12, 24, TRACKS[tr].location, fade(L_DIM), 0.6f);
    if (car >= 0) {
        char b[64]; snprintf(b, sizeof b, "%s %s", CARS[car].make, CARS[car].model);
        R.font.draw(12, 24, b, fade(L_DIM), 0.6f);
    }
    gfx::drawFill2D(0, SCREEN_H - 52, SCREEN_W, 52, RGBA(0, 0, 0, (int)(170 * a)));
    const char* tip = TIPS[(unsigned)(ld.slide / 2 + (unsigned)(long long)(ld.t0 * 7)) % (unsigned)TIP_N];   // (unsigned: a big clock must not make the index negative)
    R.font.drawCentre(SCREEN_W / 2, SCREEN_H - 46, tip, fade(L_WHITE), R.font.width(tip, 0.62f) > SCREEN_W - 24 ? 0.5f : 0.62f);
    float bw = SCREEN_W - 80, bx = 40, by = SCREEN_H - 22;
    gfx::drawFill2D(bx, by, bw, 5, RGBA(40, 44, 54, (int)(255 * a)));
    gfx::drawFill2D(bx, by, bw * fminf(1, ld.shown), 5, fade(L_ACC));
    R.font.drawRight(bx + bw, by - 13, "LOADING", fade(L_DIM), 0.5f);
    gfx::setBlend(false); gfx::setDepthTest(true);
    finishFrame();
}
