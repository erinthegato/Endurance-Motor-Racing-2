#include "setups.h"
#include <cstdio>
#include <cstring>

SetupStore setups;

bool SetupStore::get(int track, int cls, CarSetup& out, Chassis& outCh) const {
    if (track < 0 || track >= TRACK_COUNT || track >= MAX_TRACKS || cls < 0 || cls >= ERA_COUNT || !has[track][cls]) return false;
    out = su[track][cls]; outCh = ch[track][cls];
    return true;
}

void SetupStore::put(int track, int cls, const CarSetup& s, const Chassis& c) {
    if (track < 0 || track >= TRACK_COUNT || track >= MAX_TRACKS || cls < 0 || cls >= ERA_COUNT) return;
    has[track][cls] = true; su[track][cls] = s; ch[track][cls] = c;
    save();
}

void SetupStore::load() {
    FILE* f = fopen("setups.sav", "rb");
    if (!f) return;
    // EMSU1: wing springs gearing brakeF brakeR map tyre fuel; EMSU2 adds ride height, differential, pressure
    char magic[8] = {};
    if (fscanf(f, "%7s", magic) != 1 || (strcmp(magic, "EMSU1") && strcmp(magic, "EMSU2"))) { fclose(f); return; }
    bool v2 = !strcmp(magic, "EMSU2");
    char line[160];
    fgets(line, sizeof line, f);                                    // (the rest of the magic line)
    while (fgets(line, sizeof line, f)) {
        char id[32]; int cls, w, sp, g, bf, br, mp, ty, fu, rh = 1, df = 1, pr = 2;
        int got = sscanf(line, "%31s %d %d %d %d %d %d %d %d %d %d %d %d", id, &cls, &w, &sp, &g, &bf, &br, &mp, &ty, &fu, &rh, &df, &pr);
        if (got < 10 || (v2 && got < 13)) continue;
        int t = -1;
        for (int i = 0; i < TRACK_COUNT; i++) if (!strcmp(TRACKS[i].id, id)) t = i;
        if (t < 0 || t >= MAX_TRACKS || cls < 0 || cls >= ERA_COUNT) continue;
        auto in = [](int x, int lo, int hi) { return x < lo ? lo : (x > hi ? hi : x); };
        CarSetup& s = su[t][cls];
        s.wing = in(w, 1, 5); s.susp = in(sp, 0, 2); s.gearing = in(g, 0, 2); s.brakeF = in(bf, 30, 100); s.brakeR = in(br, 20, 100);
        s.map = in(mp, 0, MAP_COUNT - 1); s.tyre = in(ty, TY_SOFT, TY_WET); s.fuel = in(fu, 10, 100);
        Chassis& c = ch[t][cls];
        c.ride = in(rh, 0, 2); c.diff = in(df, 0, 2); c.press = in(pr, 0, 4);
        has[t][cls] = true;
    }
    fclose(f);
}

void SetupStore::save() const {
    FILE* f = fopen("setups.sav", "wb");
    if (!f) return;
    fprintf(f, "EMSU2\n");
    for (int t = 0; t < TRACK_COUNT && t < MAX_TRACKS; t++)
        for (int c = 0; c < 2; c++)
            if (has[t][c]) {
                const CarSetup& s = su[t][c]; const Chassis& k = ch[t][c];
                fprintf(f, "%s %d %d %d %d %d %d %d %d %d %d %d %d\n", TRACKS[t].id, c, s.wing, s.susp, s.gearing, s.brakeF, s.brakeR, s.map, s.tyre, s.fuel,
                        k.ride, k.diff, k.press);
            }
    fclose(f);
}
