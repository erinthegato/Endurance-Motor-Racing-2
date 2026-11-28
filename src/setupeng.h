// The setup engineer: a second engineer (not the race engineer on the radio), in the garage in practice and
// qualifying. She reads the player's telemetry lap by lap and suggests ONE setup change at a time.
//
// API (stable; the walking garage's engineer NPC calls the same functions):
//   setupeng::reset()                      a new session / a new car: forget the laps
//   setupeng::sample(race, dt)             every physics step of practice / qualifying (Race::update calls it)
//   setupeng::Report r = setupeng::analyse(setup, tyre)
//                                          what the laps say: r.laps clean laps, r.obs an observation (understeer
//                                          into corners / oversteer out of them), r.sug[0..r.n-1] suggestions, best first
//   setupeng::apply(sug, setup)            make the change (one step); false if it can't (at the end of its range)
//   setupeng::Ui ui; ui.ask(setup, tyre)   the conversation: speaks greet, the observation and the top suggestion,
//                                          with subtitles; ui.update(dt) each frame; ui.pending() while the player
//                                          should answer: X -> ui.accept(setup), O -> ui.dismiss()
//   setupeng::text(sug) / clip(sug)        the subtitle and the voice clip of a suggestion
// Voice: DATA/sfx/setup_<name>.raw (sound::voice), a different voice from the race engineer, not radio-filtered.
#pragma once
#include "race.h"

namespace setupeng {

enum Sug {
    SG_NEED_LAPS = 0, SG_BALANCED,
    SG_GEAR_LONGER, SG_GEAR_SHORTER,
    SG_WING_MORE, SG_WING_LESS,
    SG_SUSP_SOFTER, SG_SUSP_STIFFER,
    SG_BIAS_REAR, SG_BIAS_FWD,
    SG_TYRE_HARDER, SG_TYRE_SOFTER,
    SG_COUNT
};
enum Obs { OB_NONE = 0, OB_UNDER_ENTRY, OB_OVER_EXIT };

// one clean lap's telemetry, as fractions of the lap's time (or as given)
struct Lap {
    float time = 0;
    float limiter = 0;        // on the rev limiter in top gear
    float flatOut = 0;        // flat out above 75 % of top speed (straights)
    int topGearUsed = 0;      // the highest gear reached
    int topGear = 7;
    float underEntry = 0, underMid = 0, underExit = 0;   // past the front grip, by phase
    float overEntry = 0, overExit = 0;                    // the rear stepping out, by phase
    float fastSlide = 0;      // either end sliding above 160 km/h
    float lockF = 0, lockR = 0;   // wheels locked under braking (front / rear), as fractions of the braking time
    float braking = 0;
    float kerbHop = 0;        // light over the kerbs and bumps (lost load at speed)
    float tyreT = 0;          // average tyre temperature, deg C
};

struct Report {
    int laps = 0;             // clean laps the report is based on
    Obs obs = OB_NONE;
    int n = 0;
    Sug sug[6];
    float score[6];
    Lap avg;
};

void reset();
void sample(const Race& r, float dt);
Report analyse(const CarSetup& su, int tyre, bool sealed = false);   // sealed: parc ferme (no wing, springs or gearing)
bool apply(Sug s, CarSetup& su);
const char* text(Sug s);
const char* clip(Sug s);
const char* obsText(Obs o);
const char* obsClip(Obs o);
int cleanLaps();

// the conversation in the garage
struct Ui {
    bool open = false;        // talking (a subtitle shows)
    Sug sug = SG_BALANCED;    // the suggestion on the table
    bool offer = false;       // waiting for X apply / O ignore
    char sub[96] = "";        // the subtitle now
    const char* queue[4] = {nullptr, nullptr, nullptr, nullptr};
    const char* subs[4] = {nullptr, nullptr, nullptr, nullptr};
    int qn = 0, qi = 0;
    float gap = 0, holdT = 0;
    bool closing = false;
    float lineT = 0;          // seconds into the line being said
    void ask(const CarSetup& su, int tyre, bool sealed = false);
    void update(float dt);
    bool pending() const { return offer; }
    bool accept(CarSetup& su);    // X: apply the suggestion; false if nothing applied
    void dismiss();               // O: leave it as it is
private:
    void say(const char* clipName, const char* subtitle);
};

}
