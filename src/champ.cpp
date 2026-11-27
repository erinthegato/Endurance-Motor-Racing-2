#include "champ.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

// Endurance Motor Racing 2.0: the 9-round 2027 FIA WEC season (ERA_CAL in data.cpp). hours: how much
// longer than a 6-hour round each race is when you play timed races (Le Mans twice as long, the 8 Hours
// and the 1812 km a third longer); mult: points multiplier (Le Mans pays double)
const ChampRound CHAMP_ROUNDS[] = {
    {"lusail", 1.33f, 1}, {"imola", 1.0f, 1}, {"silverstone", 1.0f, 1}, {"spa", 1.0f, 1}, {"lemans", 2.0f, 2},
    {"interlagos", 1.0f, 1}, {"cota", 1.0f, 1}, {"fuji", 1.0f, 1}, {"bahrain", 1.33f, 1},
};
const int CHAMP_ROUND_COUNT = sizeof(CHAMP_ROUNDS) / sizeof(CHAMP_ROUNDS[0]);

// WEC points, in each class: the top ten 25-18-15-12-10-8-6-4-2-1, +1 for pole position in class
static const float PTS[10] = {25, 18, 15, 12, 10, 8, 6, 4, 2, 1};

int Champ::era() const { return base.car >= 0 && base.car < CAR_COUNT ? CARS[base.car].era : 0; }
int Champ::trackIndex(int rd) const { return eraTrack(era(), rd); }

void Champ::begin(const RaceSettings& s) {
    active = true; round = 0; base = s; count = 0;
    memset(lastPts, 0, sizeof lastPts);
}

RaceSettings Champ::roundSettings() const {
    RaceSettings s = base;
    int rd = round < CHAMP_ROUND_COUNT ? round : CHAMP_ROUND_COUNT - 1;
    s.track = trackIndex(rd);
    s.eventHours = 0;                    // the event's own length (6 h, 8 h, 24 h ...)
    s.startType = START_EVENT;           // rolling start
    // timed races: the chosen length, longer for the long rounds (Le Mans twice as long)
    // (RACE_FULL: the round's full real length, in real time -- 24 h at Le Mans; Race::start works it out)
    if (s.raceMin > 0 && s.raceMin < RACE_FULL) s.raceMin = (int)(s.raceMin * CHAMP_ROUNDS[rd].hours + 0.5f);
    else if (s.laps < 1) s.laps = 5;
    return s;
}

ChampEntry* Champ::find(int number) {
    for (int i = 0; i < count; i++) if (e[i].number == number) return &e[i];
    return nullptr;
}

void Champ::award(const Race& r, const std::vector<int>& qualiOrder) {
    int rd = round < CHAMP_ROUND_COUNT ? round : CHAMP_ROUND_COUNT - 1;
    float mult = CHAMP_ROUNDS[rd].mult;
    memset(lastPts, 0, sizeof lastPts);
    std::vector<const Car*> v;
    for (auto& c : r.cars) v.push_back(&c);
    std::sort(v.begin(), v.end(), [](const Car* a, const Car* b) { return a->position < b->position; });
    for (int cls = 0; cls < 2; cls++) {
        int k = 0, winnerLaps = -1;
        for (const Car* cp : v) {
            const Car& c = *cp;
            if ((int)c.def->cls != cls) continue;
            if (winnerLaps < 0) winnerLaps = c.lap;
            ChampEntry* en = find(c.number);
            if (!en && count < 48) { en = &e[count++]; en->number = c.number; en->car = (int)(c.def - CARS); }
            if (!en) { k++; continue; }
            float p = 0;
            // classified: 70 % of the class winner's distance (and still running at the end)
            if (c.lap >= 0.7f * winnerLaps && !c.rel.retired && k < 10) p = PTS[k] * mult;
            if (k == 0) en->wins++;
            en->best = std::min(en->best, k + 1);
            en->pts += p;
            lastPts[en - e] += (int)(p + 0.5f);
            k++;
        }
        // pole position in class: one point (qualiOrder: car numbers, Hypercars first, each class by best lap)
        for (int num : qualiOrder) {
            bool inClass = false;
            for (const Car& c : r.cars) if (c.number == num && (int)c.def->cls == cls) inClass = true;
            if (!inClass) continue;
            ChampEntry* en = find(num);
            if (en) { en->poles++; en->pts += 1; lastPts[en - e] += 1; }
            break;
        }
    }
    round++;
}

int Champ::standings(CarClass cls, int* out, int max) const {
    int n = 0;
    for (int i = 0; i < count && n < max; i++) if (CARS[e[i].car].cls == cls) out[n++] = i;
    std::sort(out, out + n, [this](int a, int b) {
        if (e[a].pts != e[b].pts) return e[a].pts > e[b].pts;
        if (e[a].wins != e[b].wins) return e[a].wins > e[b].wins;
        return e[a].best < e[b].best;
    });
    return n;
}

struct ChampFile { char magic[4]; int version; };

bool Champ::save(const char* path) const {
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    ChampFile h = {{'E', 'M', 'C', '2'}, 1};
    fwrite(&h, sizeof h, 1, f);
    fwrite(this, sizeof(Champ), 1, f);
    fclose(f);
    return true;
}

bool Champ::load(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    ChampFile h; Champ c;
    bool ok = fread(&h, sizeof h, 1, f) == 1 && !memcmp(h.magic, "EMC2", 4) && h.version == 1 && fread((void*)&c, sizeof(Champ), 1, f) == 1;
    fclose(f);
    if (!ok || c.count < 0 || c.count > 48) return false;
    *this = c;
    return true;
}
