// The setup engineer: telemetry lap by lap, and one setup change at a time (setupeng.h)
#include "setupeng.h"
#include "sound.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace setupeng {

namespace {
Lap cur;                       // the lap being driven
std::vector<Lap> laps;         // the last clean laps (newest last)
int lastLap = -1;
bool clean = false;
float offT = 0, tyreSum = 0;
const void* lastRace = nullptr;
int lastSession = -1;
const int KEEP = 5;

struct Line { const char* clip; const char* text; };
const Line SUG[SG_COUNT] = {
    {"setup_need_laps", "I need a few clean laps of data before I can tell you anything."},
    {"setup_balanced", "The car looks well balanced. I wouldn't touch it."},
    {"setup_gear_longer", "You're on the limiter before the braking zone. Let's go a step longer on the gearing."},
    {"setup_gear_shorter", "You're not getting into top gear. Shorter gearing will help the acceleration."},
    {"setup_wing_more", "The rear's moving around in the fast corners. I'd add a click of wing."},
    {"setup_wing_less", "The car's planted, but we're losing time on the straights. Let's take a click of wing off."},
    {"setup_susp_softer", "It's skipping over the kerbs and the bumps. Soften the suspension a step."},
    {"setup_susp_stiffer", "There's a lot of roll and the response is lazy. Go a step stiffer."},
    {"setup_bias_rear", "You're locking the fronts under braking. Move the brake bias rearward."},
    {"setup_bias_fwd", "The rear's stepping out under braking. Bring the brake bias forward."},
    {"setup_tyre_harder", "The tyres are running too hot. A harder compound should last better."},
    {"setup_tyre_softer", "The tyres aren't coming up to temperature. A softer compound should help."},
};
const Line OBS[3] = {{nullptr, nullptr}, {"setup_under_entry", "Understeer on corner entry."}, {"setup_over_exit", "Oversteer on the exit."}};

void finishLap() {
    if (!clean || cur.time < 20) return;
    Lap l = cur;
    float T = l.time;
    l.limiter /= T; l.flatOut /= T;
    l.underEntry /= T; l.underMid /= T; l.underExit /= T;
    l.overEntry /= T; l.overExit /= T; l.fastSlide /= T; l.kerbHop /= T;
    if (l.braking > 0.5f) { l.lockF /= l.braking; l.lockR /= l.braking; } else { l.lockF = l.lockR = 0; }
    l.braking /= T;
    l.tyreT = tyreSum / T;
    laps.push_back(l);
    if ((int)laps.size() > KEEP) laps.erase(laps.begin());
}
}

void reset() { laps.clear(); cur = Lap(); lastLap = -1; clean = false; offT = 0; tyreSum = 0; }
int cleanLaps() { return (int)laps.size(); }

void sample(const Race& r, float dt) {
    if (dt <= 0) return;
    // a new race / session: start again
    if (lastRace != (const void*)&r || lastSession != (int)r.session) {
        if (lastSession >= 0 && lastRace == (const void*)&r && lastSession == SES_PRACTICE && r.session == SES_QUALI) {
            // practice to qualifying, same weekend: keep what we learnt, start the lap afresh
            cur = Lap(); lastLap = -1; clean = false;
        } else reset();
        lastRace = &r; lastSession = r.session;
    }
    const Car& c = r.player();
    if (c.lap != lastLap) {
        if (lastLap >= 0) finishLap();
        cur = Lap(); tyreSum = 0; offT = 0;
        clean = !c.outLap;
        lastLap = c.lap;
    }
    if (c.pit != PIT_NONE || c.spinning || c.v < 1) { if (c.pit != PIT_NONE || c.spinning) clean = false; return; }
    if (c.offTrack) { offT += dt; if (offT > 1.0f) clean = false; }
    const Race::PlayerTele& t = r.tele;
    float v = c.v;
    cur.time += dt;
    tyreSum += c.tyreTemp() * dt;
    cur.topGear = t.topGear;
    cur.topGearUsed = std::max(cur.topGearUsed, c.gear);
    if (c.gear >= t.topGear && t.rpmFrac > 0.985f && c.throttle > 0.8f) cur.limiter += dt;
    if (c.throttle > 0.9f && t.vTop > 0 && v > 0.75f * t.vTop) cur.flatOut += dt;
    bool braking = c.brake > 0.15f;
    bool exitPh = !braking && c.throttle > 0.5f;
    bool entry = braking || (c.throttle < 0.3f);
    // understeer: the steering asks more than the fronts can give
    if (t.ex > 0.04f && v > 12) {
        float w = dt * std::min(1.0f, t.ex * 4);
        if (entry) cur.underEntry += w; else if (exitPh) cur.underExit += w; else cur.underMid += w;
    }
    // oversteer: the rear turns the car further into the corner on its own, or the car is sideways
    bool over = (t.sgn != 0 && t.rot * t.sgn > 0.06f) || fabsf(c.drift) > 0.06f;
    if (over && v > 12) { if (exitPh) cur.overExit += dt; else if (braking) cur.overEntry += dt; }
    if (v > 44 && (t.ex > 0.06f || over)) cur.fastSlide += dt;
    if (c.brake > 0.3f && v > 15) {
        cur.braking += dt;
        if (t.lock > 0.2f) { if (t.frontLock) cur.lockF += dt; else cur.lockR += dt; }
        if (t.rearLock > 0) cur.lockR += dt;
    }
    if (v > 30 && t.kerb && t.load < 0.75f) cur.kerbHop += dt;
    else if (v > 40 && !t.off && t.load < 0.6f) cur.kerbHop += dt * 0.5f;
}

Report analyse(const CarSetup& su, int tyre, bool sealed) {
    Report rp;
    rp.laps = (int)laps.size();
    if (rp.laps < 2) { rp.n = 1; rp.sug[0] = SG_NEED_LAPS; rp.score[0] = 1; return rp; }
    // the average of the last (up to) three clean laps
    int n = std::min(3, rp.laps);
    Lap a; int topUsed = 0;
    for (int i = rp.laps - n; i < rp.laps; i++) {
        const Lap& l = laps[i];
        a.time += l.time / n; a.limiter += l.limiter / n; a.flatOut += l.flatOut / n;
        a.underEntry += l.underEntry / n; a.underMid += l.underMid / n; a.underExit += l.underExit / n;
        a.overEntry += l.overEntry / n; a.overExit += l.overExit / n; a.fastSlide += l.fastSlide / n;
        a.lockF += l.lockF / n; a.lockR += l.lockR / n; a.braking += l.braking / n; a.kerbHop += l.kerbHop / n;
        a.tyreT += l.tyreT / n; a.topGear = l.topGear; topUsed = std::max(topUsed, l.topGearUsed);
    }
    a.topGearUsed = topUsed;
    rp.avg = a;
    struct C { Sug s; float k; } cand[SG_COUNT]; int nc = 0;
    auto add = [&](Sug s, float k) { if (k >= 1.0f) cand[nc++] = {s, k}; };
    float bias = su.bias();
    bool dry = tyre <= TY_HARD;
    if (!sealed) {
        // gearing: on the limiter before the braking zones -> longer; never into top gear -> shorter
        if (su.gearing < 2) add(SG_GEAR_LONGER, a.limiter / 0.012f);
        if (su.gearing > 0 && a.topGearUsed < a.topGear) add(SG_GEAR_SHORTER, 1.3f);
        // wing: sliding at speed -> more; planted, with long straights -> less
        if (su.wing < 5) add(SG_WING_MORE, a.fastSlide / 0.02f);
        if (su.wing > 1 && a.fastSlide < 0.012f) add(SG_WING_LESS, (a.flatOut / 0.22f) * (0.75f + 0.2f * (su.wing - 3)));
        // springs: light over the kerbs and bumps -> softer; a soft car pushing in the long corners -> stiffer
        if (su.susp > 0) add(SG_SUSP_SOFTER, a.kerbHop / 0.012f);
        if (su.susp < 2 && a.kerbHop < 0.004f) add(SG_SUSP_STIFFER, (a.underMid / 0.05f) * (su.susp == 0 ? 1.0f : 0.7f));
    }
    // brakes: locking the fronts -> bias rearward; the rear stepping out under braking -> forward
    if (bias > 0.55f) add(SG_BIAS_REAR, a.lockF / 0.06f);
    if (bias < 0.72f) add(SG_BIAS_FWD, std::max(a.lockR / 0.04f, a.overEntry / 0.025f));
    // tyres (dry compounds only; the weather decides the rest)
    if (dry && tyre < TY_HARD) add(SG_TYRE_HARDER, (a.tyreT - 100.0f) / 5.0f);
    if (dry && tyre > TY_SOFT) add(SG_TYRE_SOFTER, (75.0f - a.tyreT) / 5.0f);
    std::sort(cand, cand + nc, [](const C& x, const C& y) { return x.k > y.k; });
    for (int i = 0; i < nc && rp.n < 6; i++) { rp.sug[rp.n] = cand[i].s; rp.score[rp.n] = cand[i].k; rp.n++; }
    if (rp.n == 0) { rp.n = 1; rp.sug[0] = SG_BALANCED; rp.score[0] = 1; }
    // what she noticed (only said with a change to make: a balanced car gets no complaint)
    if (rp.sug[0] != SG_BALANCED) {
        if (a.underEntry > 0.08f && a.underEntry > a.overExit) rp.obs = OB_UNDER_ENTRY;
        else if (a.overExit > 0.02f) rp.obs = OB_OVER_EXIT;
    }
    return rp;
}

bool apply(Sug s, CarSetup& su) {
    switch (s) {
        case SG_GEAR_LONGER: if (su.gearing >= 2) return false; su.gearing++; return true;
        case SG_GEAR_SHORTER: if (su.gearing <= 0) return false; su.gearing--; return true;
        case SG_WING_MORE: if (su.wing >= 5) return false; su.wing++; return true;
        case SG_WING_LESS: if (su.wing <= 1) return false; su.wing--; return true;
        case SG_SUSP_SOFTER: if (su.susp <= 0) return false; su.susp--; return true;
        case SG_SUSP_STIFFER: if (su.susp >= 2) return false; su.susp++; return true;
        case SG_BIAS_REAR:     // a step (5 %) off the front, or onto the rear if the front is at its floor
            if (su.brakeF - 5 >= 30) { su.brakeF -= 5; return true; }
            if (su.brakeR + 5 <= 100) { su.brakeR += 5; return true; }
            return false;
        case SG_BIAS_FWD:
            if (su.brakeR - 5 >= 20) { su.brakeR -= 5; return true; }
            if (su.brakeF + 5 <= 100) { su.brakeF += 5; return true; }
            return false;
        case SG_TYRE_HARDER: if (su.tyre >= TY_HARD) return false; su.tyre++; return true;
        case SG_TYRE_SOFTER: if (su.tyre <= TY_SOFT || su.tyre > TY_HARD) return false; su.tyre--; return true;
        default: return false;
    }
}

const char* text(Sug s) { return s >= 0 && s < SG_COUNT ? SUG[s].text : ""; }
const char* clip(Sug s) { return s >= 0 && s < SG_COUNT ? SUG[s].clip : nullptr; }
const char* obsText(Obs o) { return OBS[o].text; }
const char* obsClip(Obs o) { return OBS[o].clip; }

// ---- the conversation
void Ui::say(const char* clipName, const char* subtitle) {
    if (qn < 4) { queue[qn] = clipName; subs[qn] = subtitle; qn++; }
}

void Ui::ask(const CarSetup& su, int tyre, bool sealed) {
    Report rp = analyse(su, tyre, sealed);
    qn = qi = 0; gap = 0; holdT = 0; open = true; offer = false; closing = false; sub[0] = 0;
    say("setup_greet", "Right, let's have a look at the data.");
    sug = rp.sug[0];
    if (sug == SG_NEED_LAPS) { say(clip(sug), text(sug)); return; }
    if (rp.obs != OB_NONE) say(obsClip(rp.obs), obsText(rp.obs));
    say(clip(sug), text(sug));
    offer = sug != SG_BALANCED;
}

void Ui::update(float dt) {
    if (!open) return;
    if (holdT > 0) holdT -= dt;
    lineT += dt;
    // (the clip still playing -- but never longer than the line could take, should the sound stall)
    bool talking = sound::voiceBusy() && lineT < 1.5f + 0.08f * (float)strlen(sub);
    if (talking || holdT > 0) return;
    if (gap > 0) { gap -= dt; return; }
    if (qi < qn) {
        snprintf(sub, sizeof sub, "%s", subs[qi] ? subs[qi] : "");
        bool spoken = queue[qi] && sound::voice(queue[qi]);
        // no clip (or no sound): hold the subtitle about as long as it would take to say
        holdT = spoken ? 0.0f : 0.8f + 0.06f * (float)strlen(sub);
        lineT = 0;
        qi++; gap = 0.25f;
        return;
    }
    // all said: the subtitle stays while the offer is open; otherwise it shows 2 s more, then goes
    if (offer) return;
    if (!closing) { closing = true; holdT = 2.0f; return; }
    open = false; closing = false; sub[0] = 0;
}

bool Ui::accept(CarSetup& su) {
    if (!offer) return false;
    offer = false;
    bool ok = apply(sug, su);
    qn = qi = 0; open = true; gap = 0; holdT = 0; closing = false;
    if (ok) say("setup_applied", "Done. The changes are on the car.");
    return ok;
}

void Ui::dismiss() {
    if (!offer) return;
    offer = false;
    qn = qi = 0; open = true; gap = 0; holdT = 0; closing = false;
    say("setup_anything", "Anything else, just give me a shout.");
}

}
