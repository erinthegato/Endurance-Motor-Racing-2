// Player settings (settings menu): sound, performance / vsync, button bindings. Saved to settings.cfg.
#pragma once
#include "platform.h"
#include "realism.h"

// L and R are not bindable: they brake (L) and accelerate (R) while driving (the rear touch pad skips songs in the menus)
enum Button { BTN_NONE = -1, BTN_CROSS = 0, BTN_CIRCLE, BTN_SQUARE, BTN_TRIANGLE, BTN_SELECT, BTN_UP, BTN_DOWN, BTN_COUNT };
enum Action { ACT_ACCEL = 0, ACT_BRAKE, ACT_GEAR_UP, ACT_GEAR_DOWN, ACT_CAMERA, ACT_LOOK_BACK, ACT_PIT, ACT_MAP, ACT_DRS, ACT_HUD, ACT_COUNT };
extern const char* BUTTON_NAME[BTN_COUNT];
extern const char* ACTION_NAME[ACT_COUNT];

struct Config {
    // sound, 0..10
    int master = 8, music = 7, engine = 8, effects = 8, radio = 9;
    // performance
    int preset = 0;          // 0 quality, 1 balanced, 2 performance, 3 custom
    bool vsync = true;
    int fps = 0;             // 0 = 60, 1 = 30 frames a second
    int distance = 0;        // 0 far, 1 medium, 2 near
    bool scenery = true;     // all trackside trees / signs, or only the near ones
    bool effects3d = true;   // full smoke, spray and rain, or reduced
    bool showFps = false;
    bool autoQuality = true; // lower the detail by itself when the frame rate drops (see Game::autoQualityStep)
    int attract = 10;
    bool spins = true;       // steering far past the grip makes the rear step out and the car spin
    // PS Vita: steer by turning the console (0 off, 1 gentle, 2 normal, 3 strong); reversed if it turns the wrong way
    int tilt = 0; bool tiltInvert = false;
    bool mirror = true;      // PS Vita: rear-view mirror at the top of the screen
    // cockpit: hide the steering wheel (and its screen); hidden, the cockpit always shows the virtual HUD
    bool hideWheel = false;
    // what the cockpit view shows: 0 the car's own instruments only, 1 instruments + the timing boxes and
    // map, 2 instruments + every HUD element (speed and gear too), 3 virtual HUD (the car's screens off)
    int cockpitShow = 1;
    // the HUD: 0 light (position, lap, speed and gear), 1 full, 2 telemetry (full + the car's live data).
    // In a race the HUD button (ACT_HUD; the Vita: a tap on the screen, the PC: H) steps through them
    int hudMode = 1;
    // steering help: 0 off, 1 light (default; -1 = not set yet), 2 strong (no slides or spins)
    int steerHelp = -1;
    // button (d-pad) steering: 0 quick, 1 smooth, 2 extra smooth (the wheel winds on and back more gently)
    int padSteer = 1;        // (Smooth for new players)
    int help() const;   // PS Vita: holding the rear touch pad looks behind (off: fingers resting on it would)
    bool swearing = false;   // the engineer swears on a bad day (Settings > Sound)
    // race cameras: turn the view toward the coming corner's apex; the cockpit's high-speed rumble;
    // the right stick: 0 looks round (all cameras), 1 analogue throttle (up) and brake (down)
    bool lookApex = true, rumble = true; int rstick = 0;
    int pitCam = 0;          // pit stops and the garage: 0 cinematic camera, 1 your own camera        // seconds idle on the main menu before the highlights start (5..30)
    // BTN_NONE = the action has no button
    Realism real;            // car realism and mechanical problems (settings > Car realism)
    int bind[ACT_COUNT] = {BTN_CROSS, BTN_SQUARE, BTN_NONE, BTN_NONE, BTN_TRIANGLE, BTN_CIRCLE, BTN_SELECT, BTN_UP, BTN_DOWN, BTN_NONE};

    void load();
    void save() const;
    void apply(int autoLevel = 0) const;   // push the values into sound / renderer / display; autoLevel 1..4 = detail lowered automatically
    void setPreset(int p);
    void resetBinds();
    void rebind(int action, int button);   // a button already in use swaps with the old one
};
extern Config cfg;

bool held(const Pad& p, int button);
inline bool held(const Pad& p, Action a) { return held(p, cfg.bind[a]); }
void clearBind(int action);
const char* buttonName(int b);     // "None" for an unbound action
int firstHeld(const Pad& p);         // -1 if none of the bindable buttons is down
