# Endurance Motor Racing 2.0 (PS Vita)

The 2027 FIA World Endurance Championship for the PS Vita by CODE 4 Studios: Hypercars and LMGT3s racing
together on the nine circuits of the 2027 season. Built on the Formula Legends engine (itself grown from
Endurance Motor Racing): its physics, controls, circuits, trackside, pit crews, menus, career, replays
and trophies, with Formula Legends' F1 rules taken out (no DRS, no standing-start-only racing) and the
endurance racing of the original EMR put back. Not for distribution.

## What's in it
* **Two classes, one race.** 20 Hypercars (10 makes: LMH and LMDh) and 20 LMGT3s (10 makes), fictional
  names in the style of the real 2027 entries. Pick your class at headquarters; every race runs both
  classes together, with blue flags, class positions, class results and class points.
* **Cars built in code to the 2027 technical regulations** (`tools/gen_wec.py`, shapes in
  `tools/wec_bodies.py`; the regulations' numbers are listed at the top of `gen_wec.py`):
  * **LMH**: <= 5000 mm long, wheelbase 3150 mm, overhangs <= 1100 / 1000 mm, <= 1150 mm high
  * **LMDh**: <= 5100 mm long, wheelbase 3148 mm, overhangs <= 1100 / 850 mm, <= 1060 mm high, the fin's top
    edge at 1050-1060 mm, the compulsory cut-outs over the front wheels, the rear tyre fairings
  * both: <= 2000 mm wide, 29/71-18 front and 34/71-18 rear tyres, headlights >= 400 mm up, ~1030 kg + BoP
  * **LMGT3**: production-based coupes (front-, mid- and rear-engined), 30/68-18 and 31/71-18 tyres,
    flared arches, glass house, swan-neck wings
  * every car is checked against its rule set when it is generated (length, width, wheelbase,
    overhangs, height); each make has its own nose, pods, lights and wing mount; closed cockpits with
    the interiors and dash displays
* **The 2027 calendar, from open data** (`tools/build_tracks.py`): Qatar 1812 km (Lusail), 6 Hours of
  Imola, 6 Hours of Silverstone, 6 Hours of Spa-Francorchamps, 24 Hours of Le Mans (Circuit de la
  Sarthe), 6 Hours of Sao Paulo (Interlagos), Lone Star Le Mans (COTA), 6 Hours of Fuji, 8 Hours of
  Bahrain. Formula Legends' seven circuits (bacinger/f1-circuits outlines, EU-DEM / SRTM heights) plus
  Le Mans and Fuji from OpenStreetMap (the loops assembled for Endurance Motor Racing 3D; Le Mans' Daytona
  chicane added from OSM ways 1358131505 + 34405892), all scaled to the official lap lengths. Le Mans runs
  through the night on headlights; Qatar and Bahrain under floodlights.
* **WEC rules**: rolling starts behind the safety car (standing and the classic Le Mans start as
  options), full-course yellows and slow zones at 80 km/h, the safety car, a 60 km/h pit lane,
  refuelling at every stop (a tank ~ a 45-55 minute stint), tyre changes after the fuel, three drivers per
  car with fatigue and the 4 h stint / 14 h total limits, driver changes, Michelin softs / mediums /
  hards / inters / wets, BoP-level power.
* **Physics** (Formula Legends' model, per class, `Race::phys`): the body on its springs, load-sensitive
  tyres, tyre temperatures, brake temperatures, slipstream and dirty air, mechanical failures. The
  Hypercars: ~500 kW, the hybrid's boost (the LMH's front motor only above 190 km/h), no ABS, a light
  traction control. The LMGT3s: heavier, less downforce, ABS and a firm traction control, steel brakes
  that fade sooner, more forgiving over kerbs. Turbo and naturally aspirated engines pull differently.
  AI on Normal against the 2025 poles (after Formula Legends' own per-circuit spread):

  | (2025 pole) | Qatar | Imola | Le Mans | Fuji | Bahrain |
  |---|---|---|---|---|---|
  | Hypercar | 1:45.9 (1:38.4) | 1:32.6 (1:28.9) | 3:36.0 (3:23.2) | 1:29.8 (~1:28.9) | 1:48.3 (1:46.8) |
  | LMGT3 | 2:01.5 (~1:54) | 1:45.7 (1:42.9) | 4:09.9 (3:52.8) | 1:43.2 (1:40.0) | 2:02.1 (2:01.7) |

* **Modes**: quick race, time trial, championship (the 9-round season in your class: WEC points per class,
  double at Le Mans, a point for class pole, classified at 70 %), career (your own two-car team in a
  class), race weekends (practice, qualifying, race), replays, photo mode, trophies.
* **Race lengths**: laps, the real distance, timed races (10 min to 6 h standing for the event's real
  length), or **Full length (real time)**: the real 6 / 8 / 24 hours (Qatar's 1812 km in up to 10 h),
  in the championship too (save and resume).
* **The garage computer** (practice and qualifying): three tabs, L / R to switch
  * SETUP: wing, springs, gearing, brakes, engine map, fuel, tyres (inters and wets as well)
  * WEATHER: the forecast strip (now and the next hour: an icon, the time, the chance of rain), the
    track's condition and the tyres it calls for
  * TIMING: the circuit with every car on it and the session's timing, class by class or overall
    (Square), each car tagged with its class colour
  While the garage clock runs at x4 / x16 the music fades in fully, as plain mono.
* **Team manager** (from Endurance Motor Racing; race settings > Role: Team manager): your team's drivers
  race both of your cars while you run the team from the engineers' stand on the pit wall at your box.
  Three screens on the desk, each with two tabs:
  * live timing for the whole field, each car in its class colour (tabs: TIMING / MAP, a live track map
    with your cars marked)
  * one screen per car (tabs: ORDERS / DRIVERS): fuel, tyres, driver, stint and damage, and the orders --
    pace (save / normal / push), box this lap, the next stop's tyres, fuel and driver, the team's strategy
    or yours, and DRIVE: get in while the car is stopped in its box (hand back to a team-mate at a later stop)
  * controls: d-pad left / right the screen, up / down the order, X / Square change it (X also boxes and
    DRIVEs), L / R the screen's tab, Triangle the speed of time (up to x16), Circle the camera: the pit
    wall, TV, or the **dashcam** (manager mode only: on the dash of the car you picked, with broadcast-style
    graphics). On TV and the dashcam left / right picks the car. While you manage, no button reaches the
    car's own controls, whatever it is bound to.
* **Positions**: your class position on the HUD and the dash, with your overall place next to it.
* **Sound**: the endurance engines (V6 / V8 / V12 hybrids, flat-6, I6, V8 GTs), made with ElevenLabs
  sound effects (`tools/engine_sounds.py`): loud, the Hypercars with a high hybrid whine throughout, the
  LMGT3s with a muffled rumble under them and a high straight-cut gear whine on every shift, one engineer (the 2016
  voice and its radio beeps), the music by class.

## PS Vita
`cmake -S vita -B build-vita && cmake --build build-vita` with VitaSDK + vitaGL (GitHub Actions builds
the VPK on every push). Two files:
* `EnduranceMotorRacing2.vpk`: the game. Install with VitaShell.
* `EnduranceMotorRacing2-music.zip`: the music. Install **once**: copy it to the Vita, open it in
  VitaShell, select all, copy, paste at the root of `uma0:` (it unpacks to
  `uma0:data/EnduranceMotorRacing2/DATA/music/`). No `uma0:` drive on your Vita: paste it in `ux0:`.

The game's folder (saves, settings, ghosts, the music) is `uma0:data/EnduranceMotorRacing2/` when the
Vita has a `uma0:` drive, else `ux0:data/EnduranceMotorRacing2/`. Saves, settings and a music pack left
in the old `ux0:` folder are still found, and the next save goes to `uma0:`. (The game itself installs to
`ux0:app/` like every VPK.)

Memory: a full 40-car grid at Le Mans peaks at 19.8 MB on the PC test (`FL_MEM=1`; Formula Legends'
20-car races were ~16 MB there, ~29 MB on the Vita). The AI cars carry 256 px liveries
(`car_<id>.emm`), your car and the showroom the 512 px ones (`car_<id>_hd.emm`); straight, level
stretches of road are meshed coarser; Le Mans' terrain cells are 1.6x larger; models and circuits are
stored packed (`tools/pack_files.py`).

## PC edition (Windows), for streaming to the PS Vita
The same game for Windows (and Linux), made to be streamed from the PC to **Moonlight on the PS Vita**
(Sunshine on the PC), with a higher-detail set of models and textures in `PC/`.
* **Get it**: GitHub Actions builds `EnduranceMotorRacing2-Windows.zip` on every push (the run's
  Artifacts; releases carry it too). Unzip anywhere and run `EnduranceMotorRacing2.exe`; keep `DATA`,
  `HD` and `PC` next to it. Saves and settings: `%APPDATA%\CODE4\EnduranceMotorRacing2\`.
* **Build it**: `cmake -S . -B build && cmake --build build` with SDL2, zlib and OpenGL (Windows: MSYS2
  MinGW64 with `mingw-w64-x86_64-{gcc,cmake,SDL2,zlib}`). `src/platform_desktop.cpp` is the PC layer.
* **Streaming to the Vita**: add the exe to Sunshine as an application. The game is laid out at the
  Vita's own screen shape (480:272 = 960 x 544), so it fills the Vita's screen. It runs at a steady 60
  frames a second (capped at 60 on faster monitors, as Moonlight streams at 60: `--fps N` changes the
  cap, `--fps 0` removes it), with 4x anti-aliasing and filtered textures so the picture stays clean
  through the video stream.
* **Controls**: the Vita arrives through Moonlight as an Xbox pad: X / O / Square / Triangle as on
  the Vita, the left stick steers, the right stick is analogue throttle (up) and brake (down); in
  Moonlight's settings you can put L2 / R2 (analogue brake / throttle) on the Vita's rear touch pad.
  Keyboard: arrows, Enter, Backspace, Esc, W / S throttle / brake, Q / E = L / R. Alt + Enter: window.
  Command line: `--windowed [WxH]`, `--msaa N` (0, 2, 4, 8), `--novsync`, `--fps N`.
* **What's higher on the PC** (the Vita's set is unchanged):
  * models (`python3 tools/build_pc_set.py` -> `PC/`, `tools/pc_refine.py`): every model's curved
    surfaces split onto their curves, flat faces kept: Hypercars ~30,700 triangles up close (Vita
    2,970), LMGT3s ~19,800 (2,300), road wheels 3,908 (560), the cockpits and steering wheels with round
    tubes and rims, the pit crew 11,136 a pose (696), the driver 11,096, trackside objects up to 15x
    (tyre stacks, trees, the TV tower, the crane, the fuel rig, the footbridge) with three levels of detail;
    liveries painted at 2048 px; ground textures 2048 px (Vita 512) with finer grit and grass
  * 60 % further draw distance, the close-up models kept ~80 % further out, a finer landscape (20 m
    cells), every point of the road kept
  * sound: the nearest 8 opponents' engines (Vita 3), Doppler as cars pass, the treble fading with
    distance, smoother engine pitching (cubic), equal-power stereo
* The PC test build reads `PC/` with `EMR_PCSET=1`.

### Real lighting (PC)
The PC edition lights the world itself instead of using the light baked into the Vita's vertex
colours. The sun moves with the race clock (rises in the east, high at midday, low and orange at
dusk), the sky and the ground light each surface by which way it faces, and car paint gets a
clear-coat shine. The PC models carry their normals in an `EMN1` block (written by the tools with
`--pc`). Start the game with the environment variable `EMR_NOLIGHT=1` to compare with the old look.

The sun also casts shadows (a 4096 px shadow map around the camera): cars on the track and on each
other, the stands, buildings, trees and walls, and the car's own bodywork over the cockpit.
`EMR_NOSHADOW=1` turns them off; `EMR_SHADOWMAP=2048` uses a smaller map on a slower graphics card.
The ground gets fine grain close up and soft patches further away (so the textures don't visibly
repeat), and the road a sheen that turns into a mirror in the rain.

To check the PC renderer without a Windows machine: `make -f Makefile.headless` builds `emr_gl`,
which draws with OpenGL offscreen (EGL + Mesa) and saves screenshots, e.g.
`mkdir -p /tmp/gl && EMR_WX=1 EMR_CLOCK=18.3 ./emr_gl race lemans 0 1 11 /tmp/gl 25`.

## PC test build
`make -f Makefile.pc`, then from this folder e.g. `./emr_test race lemans 0 3 39 shots 20 60` (car 0, 39
opponents, screenshots at 20 s and 60 s), `./emr_test physlap spa 1 3` (an LMGT3's lap times),
`./emr_test garage shots` (the garage computer's tabs), `./emr_test menus shots`, `./emr_test intro shots`.
`EMR_WX` (0 changeable, 1 dry, 2 wet, 3 storm), `EMR_MIN` (timed race, minutes), `EMR_CLOCK` (start time).

## Rebuilding the data
* Cars: `python3 tools/gen_wec.py [preview dir]` -> `HD/models/car_*.emm`, `wheel_hyper/gt3`,
  `steering_*`, `cockpit_*`, `livery_career*.lvy`; then `python3 tools/pack_files.py HD/models/*`.
* Circuits: `python3 tools/build_tracks.py` -> `DATA/tracks/*.emt`; then `tools/pack_files.py DATA/tracks/*`.

Data credits: bacinger/f1-circuits (MIT), OpenStreetMap contributors (ODbL; Le Mans, Fuji), EU-DEM
(Copernicus), SRTM (NASA) via OpenTopoData. Regulations: FIA / ACO 2027 Le Mans Hypercar, LMDh and LMGT3
Technical Regulations. Engine, crew, props, pit crews and engine sounds from Endurance Motor Racing,
EMR 3D and Formula Legends. Font: DejaVu Sans. Team and car names are fictional.
Music: 'Take On The World' by All Good Folks on #Uppbeat https://uppbeat.io/c/all-good-folks  License code: TOLCYIUTLLD5VYGM
Music: 'Salsa' by Hey Pluto! on #Uppbeat https://uppbeat.io/c/hey-pluto  License code: CCZCUH212QC10SVI
Other music: Uppbeat (uppbeat.io), Epidemic Sound (epidemicsound.com) and originals made for the game.
