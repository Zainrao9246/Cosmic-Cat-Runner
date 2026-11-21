// ============================================================================
//  COSMO CAT RUN  —  v1.4
//
//  A 2.5D endless runner: a cartoon cat with a rocket backpack sprints down
//  a candy-coloured space highway. The cat really RUNS (animated gallop legs
//  + footstep dust) and only fires the rocket while jumping; it dances in the
//  menu and when you beat your best score. Dodge the walls, rocket-jump over
//  the hurdles, slide under the gates, weave through the SUBWAY TRAINS, dodge
//  the UFO's energy balls, run through the glowing PORTALS that take you to
//  the next world — and grab FIVE power-ups: magnet, shield, x2 coins &
//  score, spring shoes and a jetpack. Six level zones loop forever, each
//  with its own backdrop, track colours and a chiptune loop that speeds up
//  every level. Touch friendly: swipe to steer, tap to jump.
//
//  "2.5D" = the world is real 3D (raylib camera + primitives), but the
//  gameplay is 2D (three lanes, left/right + jump + slide). The art lives in
//  the assets/ folder (AI-generated, then chroma-keyed into cutouts); if the
//  folder is missing the game falls back to code-drawn shapes, so it runs
//  anywhere no matter what. All sound effects AND the music are synthesised
//  in code.
//
//  Controls:
//    A / D  or  LEFT / RIGHT    change lane
//    SPACE / W / UP             rocket jump
//    S / DOWN                   slide (fits under overhead gates)
//    SWIPE left/right/up/down   steer on touch screens (mouse-drag works too)
//    TAP                        jump (touch screens)
//    P                          pause
//    ENTER / R (after crash)    retry
//    ESC                        quit
//
//  Hidden test flags (used by the sandbox / CI smoke tests):
//    --auto                     auto-start a run and exit after ~2.5 s
//    (screenshots menu.png / gameplay.png are written to the current folder,
//     because raylib's TakeScreenshot() always uses the base file name)
// ============================================================================
#include "raylib.h"
#include "rlgl.h"
#include "raymath.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <vector>

// ----------------------------------------------------------------- constants
static constexpr int   SCREEN_W = 1280;
static constexpr int   SCREEN_H = 720;
static constexpr float LANE_X[3]     = { -2.2f, 0.0f, 2.2f };
static constexpr float SPEED_START   = 12.0f;
static constexpr float SPEED_MAX     = 38.0f;
static constexpr float SPEED_RAMP    = 0.30f;    // speed gained per second
static constexpr float GRAVITY       = -28.0f;
static constexpr float JUMP_V        = 10.5f;
static constexpr float SLIDE_T       = 0.70f;    // slide duration in seconds
static constexpr float SPAWN_Z       = -140.0f;  // z where new patterns appear
static constexpr float DESPAWN_Z     = 14.0f;    // z where things get recycled
static constexpr float PLAYER_HALF_W = 0.38f;
static constexpr float PLAYER_HALF_D = 0.35f;
static constexpr float HURDLE_H      = 0.85f;
static const char* BEST_FILE = "cosmocat_best.txt";

// -------------------------------------------------------------------- palette
// Cartoon-bright "candy space" colours.
static const Color SKY        = { 26, 18, 54, 255 };
static const Color FLOOR_A    = { 255, 105, 155, 255 };   // bubblegum
static const Color FLOOR_B    = { 128, 70, 200, 255 };    // grape
static const Color RAIL       = { 70, 225, 255, 255 };    // cyan
static const Color DASH       = { 255, 245, 230, 255 };
static const Color CAT_FUR    = { 255, 154, 60, 255 };    // marmalade
static const Color CAT_DARK   = { 205, 110, 35, 255 };
static const Color ROCKET_SIL = { 205, 210, 220, 255 };
static const Color ROCKET_RED = { 240, 80, 90, 255 };
static const Color FLAME_OUT  = { 255, 150, 40, 255 };
static const Color FLAME_IN   = { 255, 230, 90, 255 };
static const Color WALL_RED   = { 245, 65, 95, 255 };
static const Color WALL_CAP   = { 255, 240, 245, 255 };
static const Color HURDLE_COL = { 95, 225, 130, 255 };    // mint
static const Color COIN_GOLD  = { 255, 200, 60, 255 };
static const Color SKYBLUE_TX = { 190, 215, 255, 255 };
static const Color HEART_RED  = { 255, 80, 100, 255 };
static const Color COMBO_ORNG = { 255, 170, 60, 255 };

// ---------------------------------------------------------------- game state
enum GameState { MENU, PLAYING, GAMEOVER };

// kind: 0 = wall (change lane), 1 = hurdle (jump), 2 = overhead gate (slide),
// 3 = parked subway train (change lane! too long and tall to jump),
// 4 = a moving train coming right at you
struct Obstacle
{
    int lane; float z; float top; float bottom;
    float halfW; float halfD; bool jumpable; int kind;
};
struct Coin     { float x, y, z; bool alive; };
struct Streak   { Vector3 pos; float len; float life, maxLife; };  // light trail at high speed
struct Particle { Vector3 pos; Vector3 vel; Color col; float life, maxLife, size; };
struct Planet   { Vector3 base; float radius; Color col; float speed; bool ring; };
struct Powerup  { int kind; float x, y, z; bool alive; };
struct Projectile { float x, y, z; bool alive; };   // UFO energy ball

// ---------------------------------------------------------------- level zones
// The run is split into 6 themed levels of 500 m each (then it loops): each
// one swaps the backdrop, road colours, planet tints, announces itself with
// a banner — and the music plays a little faster.
enum { Z_CANDY = 0, Z_NEBULA, Z_ICE, Z_LAVA, Z_NEON, Z_GALAXY, ZONE_COUNT };
struct Zone
{
    const char* name;
    Color floorA, floorB, rail, dash, sky, banner, planetTint;
};
static const Zone ZONES[ZONE_COUNT] =
{
    { "CANDY SPACE",   { 255, 105, 155, 255 }, { 128,  70, 200, 255 },
                       {  70, 225, 255, 255 }, { 255, 245, 230, 255 },
                       {  26,  18,  54, 255 }, { 255, 140, 190, 255 }, { 255, 255, 255, 255 } },
    { "PURPLE NEBULA", { 150,  80, 220, 255 }, {  88,  44, 158, 255 },
                       { 255, 100, 230, 255 }, { 235, 215, 255, 255 },
                       {  22,  10,  44, 255 }, { 200, 140, 255, 255 }, { 190, 130, 255, 255 } },
    { "ICE COMET",     { 205, 232, 255, 255 }, { 140, 180, 240, 255 },
                       { 110, 220, 255, 255 }, { 255, 255, 255, 255 },
                       {  18,  32,  64, 255 }, { 170, 235, 255, 255 }, { 185, 230, 255, 255 } },
    { "LAVA PLANET",   { 235,  95,  45, 255 }, { 110,  25,  18, 255 },
                       { 255, 185,  60, 255 }, { 255, 235, 170, 255 },
                       {  36,   8,  12, 255 }, { 255, 150,  70, 255 }, { 255, 150,  90, 255 } },
    { "NEON CITY",     {  54,  58, 120, 255 }, {  30,  34,  78, 255 },
                       { 255,  60, 220, 255 }, {   0, 255, 230, 255 },
                       {   8,  10,  34, 255 }, {   0, 255, 230, 255 }, { 180, 120, 255, 255 } },
    { "FINAL GALAXY",  {  58,  66, 126, 255 }, {  28,  34,  76, 255 },
                       { 255, 215, 100, 255 }, { 255, 250, 220, 255 },
                       {   8,   8,  26, 255 }, { 255, 215, 100, 255 }, { 255, 225, 150, 255 } },
};
static constexpr float ZONE_LEN = 500.0f;    // metres per level zone

struct Game
{
    GameState state = MENU;
    bool paused = false;

    float speed = SPEED_START;
    float scroll = 0.0f;              // total distance travelled
    float nextSpawn = 40.0f;          // spawn next pattern after this distance
    float scoreF = 0.0f;              // score in metres

    int lane = 1;
    float playerX = 0.0f;
    float targetX = 0.0f;
    float playerY = 0.0f;
    float vy = 0.0f;
    bool airborne = false;
    float tilt = 0.0f;

    int coins = 0;
    int best = 0;
    float crashTimer = 0.0f;
    float shake = 0.0f;
    float time = 0.0f;

    int zone = Z_CANDY;               // current level zone
    int prevZone = Z_CANDY;           // zone shown before the last switch
    float bannerTimer = 0.0f;         // "LEVEL n" banner countdown
    float zoneFade = 0.0f;            // backdrop crossfade countdown

    float magnetT = 0.0f;             // power-up timers / state
    float x2T = 0.0f;
    bool shield = false;

    int hearts = 3;                   // a hit costs one heart; 0 left = over
    float invincT = 0.0f;             // blinking invincibility after a heart loss
    float slideT = 0.0f;              // slide/roll countdown (S / DOWN)
    bool sliding = false;

    int combo = 0;                    // coins grabbed inside the combo window
    float comboTimer = 0.0f;          // window; on expiry big combos pay out
    float comboPop = 0.0f;            // "COMBO xN  +bonus" popup countdown
    int popCombo = 0, popBonus = 0;   // values the popup shows

    float runPhase = 0.0f;            // gallop cycle (drives legs + footstep dust)
    float runPhasePrev = 0.0f;
    bool newBest = false;             // beat the best score -> victory dance!
    float sparkleT = 0.0f;            // dance sparkle timer

    float shoesT = 0.0f;              // spring shoes: super-high jumps
    float jetT = 0.0f;                // jetpack: fly above everything

    int ufoState = 0;                 // saucer: 0 none, 1 swoop in, 2 aim, 3 leave
    float ufoTimer = 8.0f;            // countdown to the next saucer attack
    float ufoPhaseT = 0.0f;
    float ufoX = 0.0f, ufoZ = -60.0f; // saucer position
    int ufoLane = 1;                  // lane the saucer is attacking
    std::vector<Projectile> projectiles;  // energy balls it fires at the cat

    std::vector<Obstacle> obstacles;
    std::vector<Coin> coinsList;
    std::vector<Particle> particles;
    std::vector<Streak> streaks;
    std::vector<Powerup> powerups;
    float streakTimer = 0.0f;
    std::vector<Vector3> starPos;
    std::vector<Color> starCol;
};
static Game g;

static int CurrentZone() { return ((int)(g.scroll / ZONE_LEN)) % ZONE_COUNT; }

static const Planet PLANETS[5] =
{
    { { -26.0f, 14.0f, -80.0f },  7.0f, { 120, 235, 180, 255 }, 0.22f, false },
    { {  30.0f, 20.0f, -40.0f },  9.0f, { 255, 140, 190, 255 }, 0.30f, true  },
    { { -38.0f, 10.0f, -160.0f }, 6.0f, { 255, 225,  90, 255 }, 0.26f, false },
    { {  24.0f, 12.0f, -200.0f }, 8.0f, { 110, 190, 255, 255 }, 0.20f, false },
    { {  40.0f, 24.0f, -120.0f }, 5.0f, { 200, 160, 255, 255 }, 0.35f, true  },
};

// ------------------------------------------------------------- art assets
// All art is optional: every asset has a code-drawn fallback, so the game
// still runs even if the assets/ folder is missing.
static Texture2D texCat, texCoin, texWall, texHurdle, texBg;
static Texture2D texBgNebula, texBgIce, texBgLava, texBgNeon,
                 texBgGalaxy;                                 // zone backdrops
static Texture2D texPlanetA, texPlanetB, texFloor, texFlame;
static Texture2D texPower[5];              // magnet/shield/x2/spring-shoes/jetpack
static Model floorModel, wallModel, hurdleModel, planetModelA, planetModelB;

static Texture2D LoadAssetTex(const char* file)
{
    static char path[512];
    const char* base = GetApplicationDirectory();
    snprintf(path, sizeof(path), "%sassets/%s", base ? base : "", file);
    if (FileExists(path)) return LoadTexture(path);
    Texture2D empty = {};
    return empty;
}

static void LoadAssets()
{
    texCat     = LoadAssetTex("cat.png");
    texCoin    = LoadAssetTex("coin.png");
    texWall    = LoadAssetTex("wall.png");
    texHurdle  = LoadAssetTex("hurdle.png");
    texBg      = LoadAssetTex("bg.jpg");
    texBgNebula = LoadAssetTex("bg_nebula.jpg");
    texBgIce   = LoadAssetTex("bg_ice.jpg");
    texBgLava  = LoadAssetTex("bg_lava.jpg");
    texBgNeon  = LoadAssetTex("bg_neon.jpg");
    texBgGalaxy = LoadAssetTex("bg_galaxy.jpg");
    texPower[0] = LoadAssetTex("icon_magnet.png");
    texPower[1] = LoadAssetTex("icon_shield.png");
    texPower[2] = LoadAssetTex("icon_x2.png");
    texPower[3] = LoadAssetTex("icon_springs.png");
    texPower[4] = LoadAssetTex("icon_jet.png");
    texPlanetA = LoadAssetTex("planet_a.png");
    texPlanetB = LoadAssetTex("planet_b.png");
    texFloor   = LoadAssetTex("floor_tile.png");
    texFlame   = LoadAssetTex("flame.png");

    if (IsTextureValid(texFloor))
    {
        floorModel = LoadModelFromMesh(GenMeshCube(6.8f, 0.5f, 4.0f));
        SetMaterialTexture(&floorModel.materials[0], MATERIAL_MAP_DIFFUSE, texFloor);
    }
    if (IsTextureValid(texWall))
    {
        wallModel = LoadModelFromMesh(GenMeshCube(1.9f, 3.0f, 0.6f));
        SetMaterialTexture(&wallModel.materials[0], MATERIAL_MAP_DIFFUSE, texWall);
    }
    if (IsTextureValid(texHurdle))
    {
        hurdleModel = LoadModelFromMesh(GenMeshCube(1.9f, 0.85f, 0.16f));
        SetMaterialTexture(&hurdleModel.materials[0], MATERIAL_MAP_DIFFUSE, texHurdle);
    }
    if (IsTextureValid(texPlanetA))
    {
        planetModelA = LoadModelFromMesh(GenMeshSphere(1.0f, 24, 24));
        SetMaterialTexture(&planetModelA.materials[0], MATERIAL_MAP_DIFFUSE, texPlanetA);
    }
    if (IsTextureValid(texPlanetB))
    {
        planetModelB = LoadModelFromMesh(GenMeshSphere(1.0f, 24, 24));
        SetMaterialTexture(&planetModelB.materials[0], MATERIAL_MAP_DIFFUSE, texPlanetB);
    }
}

static void UnloadAssets()
{
    if (IsTextureValid(texCat)) UnloadTexture(texCat);
    if (IsTextureValid(texCoin)) UnloadTexture(texCoin);
    if (IsTextureValid(texWall)) UnloadTexture(texWall);
    if (IsTextureValid(texHurdle)) UnloadTexture(texHurdle);
    if (IsTextureValid(texBg)) UnloadTexture(texBg);
    if (IsTextureValid(texBgNebula)) UnloadTexture(texBgNebula);
    if (IsTextureValid(texBgIce)) UnloadTexture(texBgIce);
    if (IsTextureValid(texBgLava)) UnloadTexture(texBgLava);
    if (IsTextureValid(texBgNeon)) UnloadTexture(texBgNeon);
    if (IsTextureValid(texBgGalaxy)) UnloadTexture(texBgGalaxy);
    for (int i = 0; i < 5; i++)
        if (IsTextureValid(texPower[i])) UnloadTexture(texPower[i]);
    if (IsTextureValid(texPlanetA)) UnloadTexture(texPlanetA);
    if (IsTextureValid(texPlanetB)) UnloadTexture(texPlanetB);
    if (IsTextureValid(texFloor)) UnloadTexture(texFloor);
    if (IsTextureValid(texFlame)) UnloadTexture(texFlame);
    if (IsTextureValid(texFloor)) UnloadModel(floorModel);
    if (IsTextureValid(texWall)) UnloadModel(wallModel);
    if (IsTextureValid(texHurdle)) UnloadModel(hurdleModel);
    if (IsTextureValid(texPlanetA)) UnloadModel(planetModelA);
    if (IsTextureValid(texPlanetB)) UnloadModel(planetModelB);
}

// -------------------------------------------------------------- sound (sfx)
// Every sound effect is synthesised in code — no audio files needed. Each is
// a short frequency sweep (or a noise burst) with an exponential fade, built
// into a Wave and loaded as a raylib Sound. If the machine has no audio
// device (e.g. a headless CI runner), every Sfx() call is simply skipped.
static bool audioOk = false;
static Sound sndCoin, sndJump, sndSlide, sndHit, sndPower, sndCombo,
             sndLaser, sndBoing, sndJet, sndMusic;

static Sound MakeTone(float f0, float f1, float dur, float vol, bool noise = false)
{
    const int SR = 22050;
    int n = (int)(SR * dur);
    short* data = (short*)MemAlloc(n * sizeof(short));
    for (int i = 0; i < n; i++)
    {
        float t = (float)i / SR;
        float f = f0 + (f1 - f0) * (t / dur);
        float s = noise ? (GetRandomValue(-100, 100) / 100.0f)
                        : sinf(2.0f * PI * f * t);
        float env = expf(-3.5f * t / dur) * vol;
        data[i] = (short)(s * env * 32000.0f);
    }
    Wave w = {};
    w.frameCount = n; w.sampleRate = SR; w.sampleSize = 16; w.channels = 1;
    w.data = data;
    Sound snd = LoadSoundFromWave(w);
    UnloadWave(w);                    // also frees the sample buffer
    return snd;
}

static Sound MakeMusic();                     // defined below LoadSounds
static void LoadSounds()
{
    sndCoin  = MakeTone(1500.0f, 1900.0f, 0.10f, 0.50f);          // bright ding
    sndJump  = MakeTone(280.0f,  620.0f,  0.20f, 0.50f);          // rising whoosh
    sndSlide = MakeTone(500.0f,  140.0f,  0.22f, 0.50f);          // falling slide
    sndHit   = MakeTone(0.0f,    0.0f,    0.30f, 0.70f, true);    // crunchy noise
    sndPower = MakeTone(600.0f,  1200.0f, 0.22f, 0.50f);          // sparkle up
    sndCombo = MakeTone(800.0f,  1600.0f, 0.30f, 0.55f);          // mini fanfare
    sndLaser = MakeTone(900.0f,  150.0f,  0.25f, 0.50f);          // UFO zap
    sndBoing = MakeTone(200.0f,  550.0f,  0.18f, 0.50f);          // spring shoes
    sndJet   = MakeTone(140.0f,  420.0f,  0.50f, 0.45f);          // jetpack roar
    sndMusic = MakeMusic();                                       // chiptune loop
}

// A little chiptune loop, sequenced in code: square-wave bass line, an
// 8th-note lead arpeggio and noise hats over 4 bars (C-minor pentatonic).
// SetSoundPitch() plays it faster with every level zone.
static Sound MakeMusic()
{
    const int SR = 22050;
    const float BPM = 140.0f, BEAT = 60.0f / BPM;
    const int BARS = 4;
    int n = (int)(BARS * 4 * BEAT * SR);
    float* buf = (float*)MemAlloc(n * sizeof(float));
    for (int i = 0; i < n; i++) buf[i] = 0.0f;

    // wave: 0 = sine, 1 = square, 2 = noise
    auto addNote = [&](float startBeat, float durBeat, float freq,
                       float vol, int wave)
    {
        int s0 = (int)(startBeat * BEAT * SR);
        int ns = (int)(durBeat * BEAT * SR);
        for (int i = 0; i < ns && s0 + i < n; i++)
        {
            float t = (float)i / SR;
            float s;
            if (wave == 0) s = sinf(2.0f * PI * freq * t);
            else if (wave == 1)
                s = (sinf(2.0f * PI * freq * t) > 0.0f) ? 0.6f : -0.6f;
            else s = (float)(GetRandomValue(-100, 100)) / 100.0f;
            float env = fminf(1.0f, 8.0f * t / (durBeat * BEAT))      // attack
                      * expf(-3.0f * t / (durBeat * BEAT));           // decay
            buf[s0 + i] += s * env * vol;
        }
    };

    float bass[4] = { 65.4f, 77.8f, 87.3f, 98.0f };   // C2 Eb2 F2 G2, one/bar
    float scale[5] = { 523.3f, 622.3f, 698.5f, 784.0f, 932.3f };
    int arp[8] = { 0, 2, 4, 2, 1, 3, 4, 2 };
    for (int bar = 0; bar < 4; bar++)
    {
        float b0 = (float)bar * 4.0f;
        addNote(b0, 3.6f, bass[bar], 0.22f, 1);                  // square bass
        for (int e = 0; e < 8; e++)
            addNote(b0 + e * 0.5f, 0.22f, 0.0f, 0.045f, 2);      // 8th-note hats
        for (int e = 0; e < 8; e++)
            addNote(b0 + e * 0.5f, 0.45f, scale[arp[e]], 0.10f, 1);  // lead arp
    }

    short* data = (short*)MemAlloc(n * sizeof(short));
    for (int i = 0; i < n; i++)
    {
        float v = buf[i];
        if (v > 0.9f) v = 0.9f;                    // soft clip the mix
        if (v < -0.9f) v = -0.9f;
        data[i] = (short)(v * 24000.0f);           // music sits under the SFX
    }
    MemFree(buf);
    Wave w = {};
    w.frameCount = n; w.sampleRate = SR; w.sampleSize = 16; w.channels = 1;
    w.data = data;
    Sound snd = LoadSoundFromWave(w);
    UnloadWave(w);
    return snd;
}

static void UnloadSounds()
{
    if (!audioOk) return;
    UnloadSound(sndCoin); UnloadSound(sndJump); UnloadSound(sndSlide);
    UnloadSound(sndHit);  UnloadSound(sndPower); UnloadSound(sndCombo);
    UnloadSound(sndLaser); UnloadSound(sndBoing); UnloadSound(sndJet);
    UnloadSound(sndMusic);
}

static void Sfx(const Sound& s) { if (audioOk) PlaySound(s); }

// ------------------------------------------------------------- best score io
static int LoadBest()
{
    char* txt = LoadFileText(BEST_FILE);
    if (txt == nullptr) return 0;
    int v = atoi(txt);
    MemFree(txt);
    return v;
}

static void SaveBest(int v)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", v);          // raylib's SaveFileText takes char* (non-const)
    SaveFileText((char*)BEST_FILE, buf);
}

// ------------------------------------------------------------------ effects
static Color PowerColor(int kind)
{
    if (kind == 0) return Color{ 255, 90, 90, 255 };    // magnet
    if (kind == 1) return Color{ 110, 220, 255, 255 };  // shield
    if (kind == 2) return COIN_GOLD;                    // x2 score & coins
    if (kind == 3) return HURDLE_COL;                   // spring shoes
    return FLAME_OUT;                                   // jetpack
}

static void Burst(Vector3 at, Color col, int n, float spread, float up)
{
    for (int i = 0; i < n; i++)
    {
        float a = GetRandomValue(0, 359) * DEG2RAD;
        float s = GetRandomValue(20, 60) / 10.0f * spread;
        Particle p;
        p.pos = at;
        p.vel = { cosf(a) * s, up + GetRandomValue(0, 40) / 10.0f, sinf(a) * s };
        p.col = col;
        p.maxLife = p.life = GetRandomValue(30, 70) / 100.0f;
        p.size = GetRandomValue(8, 18) / 100.0f;
        g.particles.push_back(p);
    }
}

// ------------------------------------------------------------------ spawning
static void AddWall(int lane, float z)
{
    g.obstacles.push_back({ lane, z, 3.0f, 0.0f, 0.95f, 0.30f, false, 0 });  // too tall to jump
}

static void AddHurdle(int lane, float z)
{
    g.obstacles.push_back({ lane, z, HURDLE_H, 0.0f, 0.95f, 0.15f, true, 1 }); // jump over it
}

static void AddGate(int lane, float z)
{
    // overhead slab with a gap underneath: only a slide fits below it
    g.obstacles.push_back({ lane, z, 2.4f, 1.15f, 0.95f, 0.15f, false, 2 });
}

static void AddTrain(int lane, float z, bool moving)
{
    // a whole subway car: 13 m long and too tall to jump — change lane!
    g.obstacles.push_back({ lane, z, 2.8f, 0.0f, 0.95f, moving ? 0.8f : 6.5f,
                            false, moving ? 4 : 3 });
}

static void AddCoinRow(int lane, float z, int n, float dz = 1.8f, float y = 0.6f)
{
    for (int i = 0; i < n; i++)
        g.coinsList.push_back({ LANE_X[lane], y, z + i * dz, true });
}

static void AddCoinArc(int lane, float z)
{
    for (int i = -3; i <= 3; i++)          // coin arc that rewards a jump
    {
        float t = (float)i / 3.0f;
        float y = 0.7f + (1.0f - t * t) * 1.4f;
        g.coinsList.push_back({ LANE_X[lane], y, z + t * 3.4f, true });
    }
}

// Every pattern is solvable by construction: never three walls at once.
static void SpawnPattern()
{
    float speedT = Clamp((g.speed - SPEED_START) / (SPEED_MAX - SPEED_START), 0.0f, 1.0f);
    int type = GetRandomValue(0, 9);
    if (speedT < 0.25f && (type == 1 || type == 3 || type == 5 || type == 6 ||
                           type == 7 || type == 8 || type == 9)) type = 0;  // easy start

    if (type == 0)                                    // one wall + coins elsewhere
    {
        int wallLane = GetRandomValue(0, 2);
        int coinLane = (wallLane + GetRandomValue(1, 2)) % 3;
        AddWall(wallLane, SPAWN_Z);
        AddCoinRow(coinLane, SPAWN_Z - 2.0f, 4);
    }
    else if (type == 1)                               // two walls, one free lane
    {
        int freeLane = GetRandomValue(0, 2);
        for (int l = 0; l < 3; l++) if (l != freeLane) AddWall(l, SPAWN_Z);
        AddCoinRow(freeLane, SPAWN_Z - 2.0f, 4);
    }
    else if (type == 2)                               // hurdle + arc of coins
    {
        int h1 = GetRandomValue(0, 2);
        AddHurdle(h1, SPAWN_Z);
        AddCoinArc(h1, SPAWN_Z);
        if (speedT > 0.5f)
        {
            int h2 = (h1 + GetRandomValue(1, 2)) % 3;
            AddHurdle(h2, SPAWN_Z);
        }
    }
    else if (type == 3)                               // hurdles everywhere: JUMP!
    {
        for (int l = 0; l < 3; l++) AddHurdle(l, SPAWN_Z);
        AddCoinArc(1, SPAWN_Z);
    }
    else if (type == 4)                               // coin zigzag, no danger
    {
        int start = GetRandomValue(0, 2);
        for (int k = 0; k < 3; k++)
            AddCoinRow((start + k) % 3, SPAWN_Z + k * 3.0f, 3, 1.6f);
    }
    else if (type == 6)                               // double jump-arcs + bonus lane
    {
        int h1 = GetRandomValue(0, 2);
        int h2 = (h1 + GetRandomValue(1, 2)) % 3;
        AddHurdle(h1, SPAWN_Z);
        AddHurdle(h2, SPAWN_Z);
        AddCoinArc(h1, SPAWN_Z);
        AddCoinRow(3 - h1 - h2, SPAWN_Z - 2.0f, 4);
    }
    else if (type == 7)                               // overhead gates: slide!
    {
        int g1 = GetRandomValue(0, 2);
        AddGate(g1, SPAWN_Z);
        AddCoinRow((g1 + 1) % 3, SPAWN_Z - 2.0f, 4);
        if (speedT > 0.45f) AddGate((g1 + 2) % 3, SPAWN_Z);
    }
    else if (type == 8)                               // train yard: change lane!
    {
        int freeLane = GetRandomValue(0, 2);
        for (int l = 0; l < 3; l++)
            if (l != freeLane) AddTrain(l, SPAWN_Z - 6.0f, false);
        AddCoinRow(freeLane, SPAWN_Z - 2.0f, 5);
    }
    else if (type == 9)                               // a train coming AT you!
    {
        int tLane = GetRandomValue(0, 2);
        AddTrain(tLane, SPAWN_Z - 30.0f, true);       // spawns far, but races in
        int safe = (tLane + GetRandomValue(1, 2)) % 3;
        AddCoinRow(safe, SPAWN_Z - 2.0f, 4);
        AddHurdle(3 - tLane - safe, SPAWN_Z);
    }
    else                                              // wall + hurdle + coins
    {
        int wallLane = GetRandomValue(0, 2);
        int hurdleLane = (wallLane + GetRandomValue(1, 2)) % 3;
        int coinLane = 3 - wallLane - hurdleLane;
        AddWall(wallLane, SPAWN_Z);
        AddHurdle(hurdleLane, SPAWN_Z);
        AddCoinRow(coinLane, SPAWN_Z - 2.0f, 4);
    }

    // every so often a power-up floats in a lane without a wall
    if (GetRandomValue(0, 99) < 24)
    {
        bool walled[3] = { false, false, false };
        for (const Obstacle& o : g.obstacles)
            if (!o.jumpable && o.z == SPAWN_Z) walled[o.lane] = true;
        int safe[3], n = 0;
        for (int l = 0; l < 3; l++) if (!walled[l]) safe[n++] = l;
        if (n > 0)
            g.powerups.push_back({ GetRandomValue(0, 4),          // 5 power-up types
                                   LANE_X[safe[GetRandomValue(0, n - 1)]],
                                   0.95f, SPAWN_Z - 4.0f, true });
    }

    float gap = (1.3f - 0.5f * speedT) * g.speed;     // time-based spacing: the
    g.nextSpawn = g.scroll + gap;                     // pace stays fair at any speed
}

// ------------------------------------------------------------- run lifecycle
static void ResetRun()
{
    g.state = PLAYING;
    g.paused = false;
    g.speed = SPEED_START;
    g.scroll = 0.0f;
    g.nextSpawn = 40.0f;
    g.scoreF = 0.0f;
    g.lane = 1;
    g.playerX = 0.0f;
    g.targetX = 0.0f;
    g.playerY = 0.0f;
    g.vy = 0.0f;
    g.airborne = false;
    g.tilt = 0.0f;
    g.coins = 0;
    g.crashTimer = 0.0f;
    g.shake = 0.0f;
    g.magnetT = 0.0f;
    g.x2T = 0.0f;
    g.shield = false;
    g.hearts = 3;
    g.invincT = 0.0f;
    g.slideT = 0.0f;
    g.sliding = false;
    g.combo = 0;
    g.comboTimer = 0.0f;
    g.comboPop = 0.0f;
    g.runPhase = 0.0f;
    g.runPhasePrev = 0.0f;
    g.newBest = false;
    g.shoesT = 0.0f;
    g.jetT = 0.0f;
    g.ufoState = 0;
    g.ufoTimer = 8.0f;
    g.projectiles.clear();
    g.zone = Z_CANDY;
    g.prevZone = Z_CANDY;
    g.bannerTimer = 2.0f;                         // show "LEVEL 1" at the start
    g.zoneFade = 0.0f;
    g.obstacles.clear();
    g.coinsList.clear();
    g.particles.clear();
    g.streaks.clear();
    g.powerups.clear();
    g.streakTimer = 0.0f;
    SetRandomSeed((unsigned int)time(nullptr));   // fresh obstacle pattern every run
}

// Shared hit logic: a shield smashes (and removes) the thing that hit you,
// otherwise a spare heart is lost with blinking invincibility; with no hearts
// left it's game over. obstacleIndex = NO_REMOVE for things that are not
// obstacles (UFO energy balls) and must not be removed.
static constexpr size_t NO_REMOVE = (size_t)-1;
static void Crash();                          // defined just below
static void TakeHit(Vector3 at, size_t obstacleIndex)
{
    if (g.shield)
    {
        g.shield = false;
        Burst(at, RAIL, 16, 1.6f, 3.2f);
        if (obstacleIndex != NO_REMOVE)
        {
            g.obstacles[obstacleIndex] = g.obstacles.back();
            g.obstacles.pop_back();
        }
        g.shake = 0.35f;
        Sfx(sndPower);
    }
    else if (g.hearts > 1)
    {
        g.hearts--;                               // lose a heart, keep running
        g.invincT = 2.0f;
        Burst({ g.playerX, 0.9f, 0.0f }, WALL_RED, 14, 1.5f, 3.5f);
        Burst({ g.playerX, 0.9f, 0.0f }, HEART_RED, 8, 1.2f, 3.0f);
        if (obstacleIndex != NO_REMOVE)
        {
            g.obstacles[obstacleIndex] = g.obstacles.back();
            g.obstacles.pop_back();
        }
        g.shake = 0.5f;
        Sfx(sndHit);
    }
    else
        Crash();
}

static void Crash()
{
    g.state = GAMEOVER;
    g.crashTimer = 0.0f;
    g.shake = 0.6f;
    Sfx(sndHit);
    Vector3 at = { g.playerX, g.playerY + 0.8f, 0.0f };
    Burst(at, WALL_RED, 16, 1.6f, 4.5f);
    Burst(at, FLAME_IN, 10, 1.2f, 3.5f);
    Burst(at, ROCKET_SIL, 8, 1.4f, 4.0f);
    if ((int)g.scoreF > g.best)
    {
        g.best = (int)g.scoreF;
        SaveBest(g.best);
        g.newBest = true;                     // victory dance on the game-over screen
    }
}

// ------------------------------------------------------------------- update
static void ScrollWorld(float dz)
{
    g.scroll += dz;
    for (Obstacle& o : g.obstacles) o.z += dz;
    for (Coin& c : g.coinsList) c.z += dz;
    for (Powerup& p : g.powerups) p.z += dz;
    for (Particle& p : g.particles) p.pos.z += dz;
}

static void Update(float dt)
{
    g.time += dt;

    // chiptune loop: restarts when it finishes, and plays a little faster
    // (higher pitch) with every level zone
    if (audioOk)
    {
        if (!IsSoundPlaying(sndMusic)) PlaySound(sndMusic);
        SetSoundPitch(sndMusic, 1.0f + 0.05f * (float)g.zone);
    }

    // particles animate in every state
    for (Particle& p : g.particles)
    {
        p.pos = Vector3Add(p.pos, Vector3Scale(p.vel, dt));
        p.vel.y -= 18.0f * dt;
        p.life -= dt;
    }
    for (size_t i = g.particles.size(); i > 0; i--)
        if (g.particles[i - 1].life <= 0.0f)
        {
            g.particles[i - 1] = g.particles.back();
            g.particles.pop_back();
        }

    // speed streaks: light trails that fly past when the highway gets fast
    if (g.state == PLAYING && !g.paused && g.speed > 20.0f)
    {
        g.streakTimer -= dt;
        if (g.streakTimer <= 0.0f)
        {
            float speedT = Clamp((g.speed - SPEED_START) / (SPEED_MAX - SPEED_START), 0.0f, 1.0f);
            g.streakTimer = 0.18f - 0.10f * speedT;
            Streak st;
            st.pos = { GetRandomValue(-30, 30) / 10.0f, GetRandomValue(3, 26) / 10.0f, -40.0f };
            st.len = GetRandomValue(15, 30) / 10.0f;
            st.maxLife = st.life = 0.5f;
            g.streaks.push_back(st);
        }
    }
    for (Streak& st : g.streaks)
    {
        st.pos.z += (g.speed * 2.2f + 30.0f) * dt;    // faster than the world = whoosh
        st.life -= dt;
    }
    for (size_t i = g.streaks.size(); i > 0; i--)
        if (g.streaks[i - 1].life <= 0.0f || g.streaks[i - 1].pos.z > DESPAWN_Z)
        {
            g.streaks[i - 1] = g.streaks.back();
            g.streaks.pop_back();
        }

    if (g.state == MENU)
    {
        g.speed = 6.0f;
        ScrollWorld(g.speed * dt);
        // celebration dance: hopping, swaying, sparkling
        g.playerY = fabsf(sinf(g.time * 4.0f)) * 0.30f;
        g.tilt = sinf(g.time * 4.0f) * 0.9f;
        g.sparkleT -= dt;
        if (g.sparkleT <= 0.0f)
        {
            g.sparkleT = 0.22f;
            Burst({ g.playerX + sinf(g.time * 3.0f) * 0.8f, g.playerY + 1.1f, 0.0f },
                  (GetRandomValue(0, 1) == 0) ? COIN_GOLD : HEART_RED, 3, 0.6f, 1.6f);
        }
        if (IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER)) ResetRun();
        return;
    }

    if (g.state == GAMEOVER)
    {
        g.crashTimer += dt;
        if (g.shake > 0.0f) g.shake -= dt;
        if (g.newBest)                        // victory dance on a new best!
        {
            g.playerY = fabsf(sinf(g.time * 7.0f)) * 0.30f;
            g.tilt = sinf(g.time * 5.0f) * 0.9f;
            g.sparkleT -= dt;
            if (g.sparkleT <= 0.0f)
            {
                g.sparkleT = 0.22f;
                Burst({ g.playerX + sinf(g.time * 3.0f) * 0.8f, g.playerY + 1.1f, 0.0f },
                      (GetRandomValue(0, 1) == 0) ? COIN_GOLD : HEART_RED, 3, 0.6f, 1.6f);
            }
        }
        if (g.crashTimer > 0.6f &&
            (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_R) || IsKeyPressed(KEY_SPACE)))
            ResetRun();
        return;
    }

    // ---- PLAYING ----
    if (g.paused)
    {
        if (IsKeyPressed(KEY_P) || IsKeyPressed(KEY_ENTER)) g.paused = false;
        return;
    }
    if (IsKeyPressed(KEY_P)) { g.paused = true; return; }

    g.speed = Clamp(g.speed + SPEED_RAMP * dt, SPEED_START, SPEED_MAX);
    float dz = g.speed * dt;
    ScrollWorld(dz);
    g.scoreF += dz * (g.x2T > 0.0f ? 2.0f : 1.0f);    // x2 power-up doubles score

    // level zones: every 500 m the whole world changes theme
    int z = CurrentZone();
    if (z != g.zone)
    {
        g.prevZone = g.zone;
        g.zone = z;
        g.bannerTimer = 3.0f;
        g.zoneFade = 0.7f;
        g.speed = fminf(fmaxf(g.speed, 15.0f + 5.0f * z), SPEED_MAX);  // each level faster
    }
    if (g.bannerTimer > 0.0f) g.bannerTimer -= dt;
    if (g.zoneFade > 0.0f) g.zoneFade -= dt;
    if (g.magnetT > 0.0f) g.magnetT -= dt;
    if (g.x2T > 0.0f) g.x2T -= dt;
    if (g.invincT > 0.0f) g.invincT -= dt;
    if (g.comboPop > 0.0f) g.comboPop -= dt;
    if (g.shoesT > 0.0f) g.shoesT -= dt;
    if (g.jetT > 0.0f) g.jetT -= dt;
    if (g.slideT > 0.0f)
    {
        g.slideT -= dt;
        if (g.slideT <= 0.0f) g.sliding = false;
    }

    // UFO attacks: from level 2 on, a saucer swoops in, locks onto YOUR lane
    // for a moment (red reticle + beam!), fires an energy ball, then leaves.
    // Dodge the ball by changing lane or jumping over it.
    if (g.zone >= Z_NEBULA)
    {
        if (g.ufoState == 0)
        {
            g.ufoTimer -= dt;
            if (g.ufoTimer <= 0.0f)
            {
                g.ufoState = 1; g.ufoPhaseT = 0.0f;
                g.ufoZ = -60.0f;
                g.ufoLane = g.lane;
                g.ufoX = LANE_X[g.ufoLane];
            }
        }
        else if (g.ufoState == 1)                     // swoop in
        {
            g.ufoPhaseT += dt;
            g.ufoZ += (-22.0f - g.ufoZ) * Clamp(dt * 2.5f, 0.0f, 1.0f);
            g.ufoX += (LANE_X[g.ufoLane] - g.ufoX) * Clamp(dt * 3.0f, 0.0f, 1.0f);
            if (g.ufoPhaseT > 1.4f) { g.ufoState = 2; g.ufoPhaseT = 0.0f; }
        }
        else if (g.ufoState == 2)                     // lock on... fire!
        {
            g.ufoPhaseT += dt;
            g.ufoLane = g.lane;                       // it tracks your lane
            g.ufoX += (LANE_X[g.ufoLane] - g.ufoX) * Clamp(dt * 3.0f, 0.0f, 1.0f);
            if (g.ufoPhaseT > 1.1f)
            {
                g.projectiles.push_back({ LANE_X[g.ufoLane], 2.4f, g.ufoZ, true });
                Sfx(sndLaser);
                g.ufoState = 3; g.ufoPhaseT = 0.0f;
            }
        }
        else                                          // fly away
        {
            g.ufoPhaseT += dt;
            g.ufoZ -= 20.0f * dt;
            if (g.ufoPhaseT > 1.2f)
            {
                g.ufoState = 0;
                g.ufoTimer = (float)GetRandomValue(14, 26);
            }
        }
    }

    while (g.scroll >= g.nextSpawn) SpawnPattern();

    // input — keys, plus touch swipes/taps for phones & tablets (raylib maps
    // mouse drags to gestures too, so it works on desktop as well)
    if ((IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_A) ||
         IsGestureDetected(GESTURE_SWIPE_LEFT)) && g.lane > 0)
    { g.lane--; g.targetX = LANE_X[g.lane]; }
    if ((IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D) ||
         IsGestureDetected(GESTURE_SWIPE_RIGHT)) && g.lane < 2)
    { g.lane++; g.targetX = LANE_X[g.lane]; }
    bool jumpKey = IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_UP) ||
                   IsKeyPressed(KEY_W) || IsGestureDetected(GESTURE_SWIPE_UP) ||
                   IsGestureDetected(GESTURE_TAP);
    if (jumpKey && !g.airborne && g.jetT <= 0.0f)
    {
        g.vy = JUMP_V * (g.shoesT > 0.0f ? 1.4f : 1.0f);   // spring shoes: higher!
        g.airborne = true; g.slideT = 0.0f; g.sliding = false;
        if (g.shoesT > 0.0f)
        {
            Burst({ g.playerX, 0.15f, 0.0f }, HURDLE_COL, 10, 0.8f, 2.5f);
            Sfx(sndBoing);
        }
        else Sfx(sndJump);
    }
    if ((IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S) ||
         IsGestureDetected(GESTURE_SWIPE_DOWN)) && !g.airborne && !g.sliding &&
        g.jetT <= 0.0f)
    { g.slideT = SLIDE_T; g.sliding = true; Sfx(sndSlide); }

    // smooth lane motion + body tilt
    g.playerX += (g.targetX - g.playerX) * Clamp(dt * 10.0f, 0.0f, 1.0f);
    g.tilt = (g.targetX - g.playerX) * 0.5f;

    // jump physics / jetpack flight
    if (g.jetT > 0.0f)
    {
        // jetpack: hover above every obstacle, then drop when it runs out
        g.playerY += (2.7f - g.playerY) * Clamp(dt * 5.0f, 0.0f, 1.0f);
        g.vy = 0.0f; g.airborne = false;
        if (GetRandomValue(0, 2) == 0)
            Burst({ g.playerX, g.playerY + 0.15f, 0.4f }, FLAME_OUT, 1, 0.25f, -2.0f);
    }
    else if (g.airborne)
    {
        g.vy += GRAVITY * dt;
        g.playerY += g.vy * dt;
        if (g.playerY <= 0.0f)
        {
            g.playerY = 0.0f; g.vy = 0.0f; g.airborne = false;
            Burst({ g.playerX, 0.12f, 0.0f }, WALL_CAP, 7, 0.9f, 1.8f);   // landing dust puff
        }
    }
    else if (g.playerY > 0.0f)
    {
        g.airborne = true; g.vy = 0.0f;       // jetpack just ran out: fall down
    }

    // gallop cycle: steps get quicker as the highway speeds up; every half
    // cycle the paws touch down -> little footstep dust puff
    g.runPhase += (6.0f + g.speed * 0.35f) * dt;
    if (!g.airborne && !g.sliding && g.jetT <= 0.0f &&
        (int)(g.runPhase / PI) != (int)(g.runPhasePrev / PI))
        Burst({ g.playerX, 0.10f, 0.25f }, WALL_CAP, 4, 0.5f, 1.2f);
    g.runPhasePrev = g.runPhase;

    // moving trains creep toward you faster than the world scrolls
    for (Obstacle& o : g.obstacles)
        if (o.kind == 4) o.z += g.speed * 0.45f * dt;

    // recycle things that passed the camera
    for (size_t i = g.obstacles.size(); i > 0; i--)
        if (g.obstacles[i - 1].z > DESPAWN_Z)
        { g.obstacles[i - 1] = g.obstacles.back(); g.obstacles.pop_back(); }
    for (size_t i = g.coinsList.size(); i > 0; i--)
        if (!g.coinsList[i - 1].alive || g.coinsList[i - 1].z > DESPAWN_Z)
        { g.coinsList[i - 1] = g.coinsList.back(); g.coinsList.pop_back(); }
    for (size_t i = g.powerups.size(); i > 0; i--)
        if (!g.powerups[i - 1].alive || g.powerups[i - 1].z > DESPAWN_Z)
        { g.powerups[i - 1] = g.powerups.back(); g.powerups.pop_back(); }

    // UFO energy balls: they fly at the cat faster than the world scrolls
    for (Projectile& p : g.projectiles)
    {
        p.z += (g.speed * 1.3f + 6.0f) * dt;
        if (p.y > 1.0f) p.y -= 3.0f * dt;
        if (p.z > DESPAWN_Z) p.alive = false;
    }
    for (size_t i = g.projectiles.size(); i > 0; i--)
        if (!g.projectiles[i - 1].alive)
        { g.projectiles[i - 1] = g.projectiles.back(); g.projectiles.pop_back(); }

    // collisions (player AABB: x = playerX, z = 0, feet at playerY, head at
    // playerY + height — the height shrinks to 0.8 while sliding under gates).
    // Handled by TakeHit(): shield smashes, else a heart is lost, else crash.
    // While the jetpack is on, the cat flies above every obstacle: no hits.
    if (g.invincT <= 0.0f && g.jetT <= 0.0f)
    {
        float curH = g.sliding ? 0.8f : 1.7f;
        for (size_t i = 0; i < g.obstacles.size(); i++)
        {
            const Obstacle& o = g.obstacles[i];
            if (fabsf(LANE_X[o.lane] - g.playerX) < o.halfW + PLAYER_HALF_W &&
                fabsf(o.z) < o.halfD + PLAYER_HALF_D &&
                g.playerY < o.top - 0.06f &&
                g.playerY + curH > o.bottom + 0.06f)
            {
                TakeHit({ LANE_X[o.lane], o.top * 0.5f, o.z }, i);
                break;
            }
        }
    }

    // energy balls: dodge by changing lane or jumping over them
    for (size_t i = 0; i < g.projectiles.size(); i++)
    {
        Projectile& pr = g.projectiles[i];
        if (!pr.alive || g.jetT > 0.0f || g.invincT > 0.0f) continue;
        float curH = g.sliding ? 0.8f : 1.7f;
        if (fabsf(pr.x - g.playerX) < 0.75f && fabsf(pr.z) < 0.55f &&
            g.playerY < pr.y + 0.45f && g.playerY + curH > pr.y - 0.45f)
        {
            pr.alive = false;
            TakeHit({ pr.x, pr.y, pr.z }, NO_REMOVE);
            break;
        }
    }

    // coin pickup (the magnet power-up drags nearby coins toward the cat)
    for (Coin& c : g.coinsList)
    {
        if (!c.alive) continue;
        if (g.magnetT > 0.0f && c.z > -12.0f && c.z < 3.0f)
        {
            float pull = Clamp(dt * 9.0f, 0.0f, 1.0f);
            c.x += (g.playerX - c.x) * pull;
            c.y += (g.playerY + 0.8f - c.y) * pull;
        }
        if (fabsf(c.x - g.playerX) < 0.85f && fabsf(c.z) < 0.85f &&
            fabsf(c.y - (g.playerY + 0.8f)) < 1.1f)
        {
            c.alive = false;
            g.coins += (g.x2T > 0.0f) ? 2 : 1;    // x2 doubles coins too!
            g.combo++;                                // feeds the combo meter
            g.comboTimer = 2.0f;
            Sfx(sndCoin);
            Burst({ c.x, c.y, c.z }, COIN_GOLD, 6, 0.7f, 2.0f);
        }
    }

    // combo payout: keep grabbing coins within 2 s of each other and a big
    // chain pays a bonus when the window finally closes
    if (g.comboTimer > 0.0f)
        g.comboTimer -= dt;
    else if (g.combo > 0)
    {
        if (g.combo >= 10)
        {
            g.popCombo = g.combo;
            g.popBonus = g.combo * 2;
            g.scoreF += (float)g.popBonus;
            g.comboPop = 1.6f;
            Sfx(sndCombo);
        }
        g.combo = 0;
    }

    // power-up pickup
    for (Powerup& p : g.powerups)
    {
        if (!p.alive) continue;
        if (fabsf(p.x - g.playerX) < 0.95f && fabsf(p.z) < 0.95f &&
            fabsf(p.y - (g.playerY + 0.8f)) < 1.3f)
        {
            p.alive = false;
            Burst({ p.x, p.y, p.z }, PowerColor(p.kind), 12, 1.0f, 2.6f);
            Sfx(sndPower);
            if (p.kind == 0) g.magnetT = 8.0f;
            else if (p.kind == 1) g.shield = true;
            else if (p.kind == 2) g.x2T = 10.0f;
            else if (p.kind == 3) { g.shoesT = 8.0f; Sfx(sndBoing); }
            else { g.jetT = 5.0f; Sfx(sndJet); }
        }
    }
}

// ------------------------------------------------------------------ visuals
static void InitStars()
{
    unsigned int s = 987654321u;                       // deterministic starfield
    auto next = [&s]() -> float
    {
        s = s * 1664525u + 1013904223u;
        return (float)((s >> 8) & 0xFFFF) / 65535.0f;
    };
    for (int i = 0; i < 240; i++)
    {
        g.starPos.push_back({ (next() * 2.0f - 1.0f) * 90.0f, 8.0f + next() * 62.0f,
                              -300.0f + next() * 260.0f });
        Color c = (i % 3 == 0) ? Color{ 255, 235, 170, 255 }
                : (i % 3 == 1) ? Color{ 185, 215, 255, 255 }
                               : Color{ 255, 255, 255, 255 };
        g.starCol.push_back(c);
    }
}

static void DrawCat()
{
    float t = g.time;
    float boost = (g.jetT > 0.0f || (g.airborne && g.vy > 0.5f)) ? 1.9f : 1.0f;
    float flick = 0.85f + 0.15f * sinf(t * 30.0f);

    float bounce = g.airborne ? 0.0f : fabsf(sinf(t * 11.0f)) * 0.05f;
    rlPushMatrix();
    rlTranslatef(g.playerX, g.playerY + bounce, 0.0f);
    rlRotatef(g.tilt * 12.0f, 0.0f, 0.0f, 1.0f);       // lean when changing lane

    // rocket backpack (faces the camera)
    DrawCylinder({ 0.0f, 0.62f, 0.46f }, 0.17f, 0.17f, 0.55f, 12, ROCKET_SIL);
    DrawSphere({ 0.0f, 0.97f, 0.46f }, 0.17f, ROCKET_RED);
    float flameH = (0.30f * flick + (g.airborne ? 0.12f : 0.0f)) * boost;
    DrawCylinder({ 0.0f, 0.38f, 0.46f }, 0.12f, 0.0f, flameH, 10, FLAME_OUT);
    DrawCylinder({ 0.0f, 0.36f, 0.46f }, 0.07f, 0.0f, flameH * 0.55f, 8, FLAME_IN);

    // tail (wags)
    float wag = sinf(t * 6.0f) * 0.06f;
    DrawSphere({ 0.12f + wag, 0.50f, 0.36f }, 0.10f, CAT_FUR);
    DrawSphere({ 0.20f + wag, 0.70f, 0.44f }, 0.09f, CAT_FUR);
    DrawSphere({ 0.24f + wag, 0.90f, 0.50f }, 0.08f, CAT_DARK);

    // running legs (gallop cycle, faster with speed) — only while grounded
    if (g.state == PLAYING && !g.airborne && !g.sliding && g.jetT <= 0.0f)
        for (int i = 0; i < 4; i++)
        {
            float ph = g.runPhase + ((i % 2) * PI) + ((i / 2) * 0.6f);
            DrawSphere({ (i % 2 == 0) ? -0.15f : 0.15f,
                         0.24f + sinf(ph) * 0.12f,
                         -0.05f + cosf(ph) * 0.14f }, 0.085f,
                       (i < 2) ? CAT_FUR : CAT_DARK);
        }

    // body + head
    DrawSphere({ 0.0f, 0.60f, 0.0f }, 0.42f, CAT_FUR);
    DrawSphere({ 0.0f, 1.14f, -0.06f }, 0.30f, CAT_FUR);

    // ears
    DrawCylinder({ -0.15f, 1.36f, -0.06f }, 0.0f, 0.11f, 0.26f, 8, CAT_FUR);
    DrawCylinder({  0.15f, 1.36f, -0.06f }, 0.0f, 0.11f, 0.26f, 8, CAT_FUR);

    rlPopMatrix();
}

static void DrawWorld(Camera3D cam)
{
    float t = g.time;

    // twinkling stars (only needed as fallback when there is no bg image)
    if (!IsTextureValid(texBg))
        for (size_t i = 0; i < g.starPos.size(); i++)
        {
            float s = 0.14f + 0.05f * sinf(t * 2.0f + (float)i);
            DrawCube(g.starPos[i], s, s, s, g.starCol[i]);
        }

    // parallax planets (textured candy spheres, tinted per zone + planet)
    const Zone& zn = ZONES[g.zone];
    for (int i = 0; i < 5; i++)
    {
        const Planet& p = PLANETS[i];
        float z = -150.0f + fmodf(p.base.z + 150.0f + g.scroll * p.speed, 320.0f);
        Vector3 pos = { p.base.x, p.base.y, z };
        Color pc = (g.zone == Z_CANDY) ? p.col : zn.planetTint;
        if (IsTextureValid(texPlanetA) && IsTextureValid(texPlanetB))
            DrawModel((i % 2 == 0) ? planetModelA : planetModelB, pos, p.radius, pc);
        else
            DrawSphere(pos, p.radius, pc);
        if (p.ring)
            DrawCircle3D(pos, p.radius * 1.7f, { 1.0f, 0.25f, 0.0f }, 65.0f,
                         ColorAlpha(zn.planetTint, 0.6f));
    }

    // highway slabs (world-anchored so they scroll and repeat seamlessly);
    // with art: the white candy tile texture is tinted pink / grape per slab
    int kmin = (int)floorf((-150.0f - g.scroll) / 4.0f);
    int kmax = (int)((DESPAWN_Z + 4.0f - g.scroll) / 4.0f);
    for (int k = kmin; k <= kmax; k++)
    {
        float z = 4.0f * k + g.scroll;
        Color tint = (k & 1) ? zn.floorB : zn.floorA;
        if (IsTextureValid(texFloor))
            DrawModel(floorModel, { 0.0f, -0.25f, z }, 1.0f, tint);
        else
            DrawCube({ 0.0f, -0.25f, z }, 6.8f, 0.5f, 4.0f, tint);
    }

    // lane dashes
    int dmin = (int)floorf((-150.0f - g.scroll) / 3.0f);
    int dmax = (int)((DESPAWN_Z + 4.0f - g.scroll) / 3.0f);
    for (int k = dmin; k <= dmax; k++)
    {
        float z = 3.0f * k + g.scroll;
        DrawCube({ -1.1f, 0.01f, z }, 0.09f, 0.02f, 1.5f, zn.dash);
        DrawCube({  1.1f, 0.01f, z }, 0.09f, 0.02f, 1.5f, zn.dash);
    }

    // glowing side rails (zone colour)
    DrawCube({ -3.55f, 0.20f, -70.0f }, 0.24f, 0.40f, 240.0f, zn.rail);
    DrawCube({  3.55f, 0.20f, -70.0f }, 0.24f, 0.40f, 240.0f, zn.rail);

    // zone portal: a glowing ring on the road exactly at the next 500 m
    // boundary — run through it and the next world begins (banner + music)
    {
        float nextB = (floorf(g.scroll / ZONE_LEN) + 1.0f) * ZONE_LEN;
        float pz = nextB - g.scroll;
        if (pz < 130.0f)
        {
            const Zone& zn2 = ZONES[(CurrentZone() + 1) % ZONE_COUNT];
            float pulse = 1.0f + 0.06f * sinf(t * 6.0f);
            DrawCircle3D({ 0.0f, 1.7f, -pz }, 1.9f * pulse, { 0, 0, 1 }, 0.0f,
                         ColorAlpha(zn2.rail, 210));
            DrawCircle3D({ 0.0f, 1.7f, -pz }, 1.45f * pulse, { 0, 0, 1 }, 0.0f,
                         ColorAlpha(zn2.banner, 160));
            for (int i = 0; i < 10; i++)              // orbiting sparkles
            {
                float a = t * 2.5f + (float)i * 0.628f;
                float rr = 1.25f + 0.45f * sinf(t * 3.0f + (float)i);
                DrawSphere({ cosf(a) * rr, 1.7f + sinf(a) * rr, -pz }, 0.09f,
                           zn2.banner);
            }
            DrawCube({ 0.0f, 0.02f, -pz }, 6.6f, 0.04f, 1.4f,
                     ColorAlpha(zn2.rail, 130));      // glow strip on the road
        }
    }

    // obstacles
    for (const Obstacle& o : g.obstacles)
    {
        float lx = LANE_X[o.lane];
        if (o.kind == 2)                              // overhead gate: slide!
        {
            DrawCube({ lx, (o.top + o.bottom) * 0.5f, o.z }, 1.9f,
                     o.top - o.bottom, 0.16f, WALL_RED);
            DrawCube({ lx, o.bottom - 0.05f, o.z }, 1.9f, 0.10f, 0.18f, WALL_CAP);
            DrawCube({ lx, o.top + 0.05f, o.z }, 1.9f, 0.10f, 0.18f, WALL_CAP);
        }
        else if (o.jumpable)
        {
            if (IsTextureValid(texHurdle))
                DrawModel(hurdleModel, { lx, HURDLE_H * 0.5f, o.z }, 1.0f, WHITE);
            else
                DrawCube({ lx, HURDLE_H - 0.10f, o.z }, 1.9f, 0.18f, 0.16f, HURDLE_COL);
            DrawCube({ lx - 0.88f, 0.40f, o.z }, 0.14f, 0.80f, 0.14f, HURDLE_COL);
            DrawCube({ lx + 0.88f, 0.40f, o.z }, 0.14f, 0.80f, 0.14f, HURDLE_COL);
        }
        else if (o.kind == 3 || o.kind == 4)          // subway trains: change lane!
        {
            float len = o.halfD * 2.0f;
            Color body = (o.kind == 4) ? FLAME_OUT : zn.rail;  // moving = hazard orange
            DrawCube({ lx, 0.25f, o.z }, 1.6f, 0.5f, len - 0.2f, CAT_DARK);  // wheels/skirt
            DrawCube({ lx, 1.5f, o.z }, 1.8f, 2.2f, len, body);
            DrawCube({ lx, 2.75f, o.z }, 1.9f, 0.35f, len, WALL_CAP);        // roof
            for (float wz = -len / 2.0f + 1.0f; wz <= len / 2.0f - 1.0f; wz += 1.6f)
            {
                DrawCube({ lx - 0.92f, 1.9f, o.z + wz }, 0.06f, 0.55f, 0.8f, SKYBLUE_TX);
                DrawCube({ lx + 0.92f, 1.9f, o.z + wz }, 0.06f, 0.55f, 0.8f, SKYBLUE_TX);
            }
            float fz = o.z + o.halfD;                 // the end facing the cat
            DrawCube({ lx, 1.6f, fz + 0.05f }, 1.6f, 0.9f, 0.10f, SKYBLUE_TX);  // windshield
            DrawSphere({ lx - 0.6f, 0.7f, fz + 0.10f }, 0.12f, FLAME_IN);       // headlights
            DrawSphere({ lx + 0.6f, 0.7f, fz + 0.10f }, 0.12f, FLAME_IN);
            if (o.kind == 4 && GetRandomValue(0, 2) == 0)   // moving train: smoke!
                Burst({ lx, 2.9f, fz }, Color{ 235, 235, 240, 200 }, 1, 0.2f, 0.8f);
        }
        else                                          // red walls: change lane
        {
            if (IsTextureValid(texWall))
                DrawModel(wallModel, { lx, o.top * 0.5f, o.z }, 1.0f, WHITE);
            else
                DrawCube({ lx, o.top * 0.5f, o.z }, o.halfW * 2.0f, o.top, o.halfD * 2.0f, WALL_RED);
            DrawCube({ lx, o.top + 0.07f, o.z }, o.halfW * 2.0f + 0.14f, 0.14f,
                     o.halfD * 2.0f + 0.14f, WALL_CAP);
        }
    }

    // speed streaks (only exist once the highway gets fast; zone colour)
    for (const Streak& st : g.streaks)
        DrawCube(st.pos, 0.08f, 0.08f, st.len + 2.5f * (st.life / st.maxLife),
                 ColorAlpha(ZONES[g.zone].rail, 0.85f * st.life / st.maxLife));

    // spinning coins: with art, a billboard whose width oscillates = fake 3D spin
    float spin = t * 3.0f;
    for (const Coin& c : g.coinsList)
    {
        if (!c.alive) continue;
        if (IsTextureValid(texCoin))
        {
            float wx = 0.62f * fabsf(cosf(spin + c.z * 0.5f));
            if (wx < 0.06f) wx = 0.06f;
            Vector2 size = { wx, 0.62f };
            DrawBillboardPro(cam, texCoin,
                             { 0, 0, (float)texCoin.width, (float)texCoin.height },
                             { c.x, c.y, c.z }, { 0, 1, 0 }, size,
                             { size.x * 0.5f, size.y * 0.5f }, 0.0f, WHITE);
        }
        else
        {
            float a = spin + c.z * 0.5f;
            Vector3 d = { cosf(a) * 0.05f, 0.0f, sinf(a) * 0.05f };   // thin axis = flat disc
            DrawCylinderEx({ c.x - d.x, c.y, c.z - d.z }, { c.x + d.x, c.y, c.z + d.z },
                           0.28f, 0.28f, 22, COIN_GOLD);
        }
    }

    // power-ups: bobbing, spinning icon billboards over a glow disc
    for (const Powerup& p : g.powerups)
    {
        if (!p.alive) continue;
        Color glow = PowerColor(p.kind);
        float bob = sinf(t * 3.0f + p.z * 0.35f) * 0.10f;
        DrawCylinder({ p.x, 0.02f, p.z }, 0.40f, 0.40f, 0.02f, 16,
                     ColorAlpha(glow, 120));
        if (IsTextureValid(texPower[p.kind]))
        {
            float wx = 0.58f * fabsf(cosf(spin * 1.3f + p.z * 0.4f));
            if (wx < 0.10f) wx = 0.10f;
            Vector2 size = { wx, 0.58f };
            DrawBillboardPro(cam, texPower[p.kind],
                             { 0, 0, (float)texPower[p.kind].width,
                                    (float)texPower[p.kind].height },
                             { p.x, p.y + bob, p.z }, { 0, 1, 0 }, size,
                             { size.x * 0.5f, size.y * 0.5f }, 0.0f, WHITE);
        }
        else
            DrawSphere({ p.x, p.y + bob, p.z }, 0.30f, glow);
    }

    // debris particles
    for (const Particle& p : g.particles)
        DrawSphere(p.pos, p.size * (p.life / p.maxLife) + 0.02f, p.col);

    // UFO saucer (attacks from level 2 on) with its aim beam + ground reticle
    if (g.ufoState > 0)
    {
        Vector3 up = { g.ufoX, 3.4f + sinf(t * 5.0f) * 0.15f, g.ufoZ };
        DrawCylinder(up, 1.0f, 1.15f, 0.16f, 18, ROCKET_SIL);         // saucer disc
        DrawSphere({ up.x, up.y + 0.22f, up.z }, 0.34f, SKYBLUE_TX);  // glass dome
        for (int i = 0; i < 6; i++)                    // chasing lights
        {
            float a = t * 3.0f + (float)i * (2.0f * PI / 6.0f);
            DrawSphere({ up.x + cosf(a) * 0.85f, up.y - 0.10f,
                         up.z + sinf(a) * 0.85f }, 0.07f,
                       (i % 2 == 0) ? FLAME_IN : HEART_RED);
        }
        if (g.ufoState == 2)                           // charging the shot
        {
            DrawCylinder({ up.x, 1.7f, up.z }, 0.25f, 1.05f, 3.4f, 12,
                         ColorAlpha(HEART_RED, 60));   // beam down to the road
            float pr = 0.6f + 0.25f * sinf(t * 12.0f);
            DrawCircle3D({ LANE_X[g.ufoLane], 0.06f, 0.0f }, pr, { 1, 0, 0 }, 90.0f,
                         ColorAlpha(HEART_RED, 220));  // reticle on your lane
        }
    }
    for (const Projectile& p : g.projectiles)
    {
        DrawSphere({ p.x, p.y, p.z }, 0.30f, FLAME_OUT);
        DrawSphere({ p.x, p.y, p.z }, 0.18f, FLAME_IN);
    }

    // rocket boost flame (extra puff while rising or flying the jetpack;
    // the cat art already has one)
    if (IsTextureValid(texCat) && ((g.airborne && g.vy > 0.5f) || g.jetT > 0.0f) &&
        IsTextureValid(texFlame))
    {
        float flick = 0.85f + 0.15f * sinf(t * 30.0f);
        Vector2 fs = { 0.42f * flick, 0.85f * flick };
        DrawBillboardPro(cam, texFlame,
                         { 0, 0, (float)texFlame.width, (float)texFlame.height },
                         { g.playerX, g.playerY + fs.y * 0.25f, 0.0f }, { 0, 1, 0 }, fs,
                         { fs.x * 0.5f, fs.y * 0.5f }, 0.0f, WHITE);
    }

    // cat shadow (filled disc that shrinks while jumping) + cat
    float shR = 0.48f - g.playerY * 0.06f;
    DrawCylinder({ g.playerX, 0.015f, 0.0f }, shR, shR, 0.02f, 18, Color{ 20, 10, 40, 120 });

    // spring-shoes glow under the cat while the power is active
    if (g.shoesT > 0.0f)
        DrawCylinder({ g.playerX, 0.03f, 0.0f }, 0.52f, 0.52f, 0.02f, 16,
                     ColorAlpha(HURDLE_COL,
                                90 + (int)(60.0f * fabsf(sinf(t * 8.0f)))));

    // shield bubble, gently pulsing
    if (g.shield)
    {
        float pulse = 0.95f + 0.05f * sinf(t * 6.0f);
        DrawSphere({ g.playerX, g.playerY + 0.85f, 0.0f }, pulse,
                   Color{ 110, 220, 255, 55 });
        DrawSphereWires({ g.playerX, g.playerY + 0.85f, 0.0f }, pulse, 8, 8,
                        Color{ 150, 235, 255, 120 });
    }

    if (IsTextureValid(texCat))
    {
        bool dancing = (g.state == MENU) || (g.state == GAMEOVER && g.newBest);
        float h = g.sliding ? 0.95f : 1.62f;          // squash while sliding
        float w = h * (float)texCat.width / (float)texCat.height;
        float lift = (g.state == PLAYING && !g.airborne && !g.sliding &&
                      g.jetT <= 0.0f) ? 0.14f : 0.0f;
        float bounce;
        if (dancing)       bounce = fabsf(sinf(t * 7.0f)) * 0.10f;   // dance bounce
        else if (g.airborne || g.jetT > 0.0f) bounce = 0.0f;
        else               bounce = fabsf(sinf(g.runPhase)) * 0.07f; // run-cycle bob
        Color catTint = WHITE;
        if (g.invincT > 0.0f && fmodf(t, 0.3f) < 0.15f)
            catTint = ColorAlpha(WHITE, 0.25f);       // blink when invincible
        DrawBillboardPro(cam, texCat,
                         { 0, 0, (float)texCat.width, (float)texCat.height },
                         { g.playerX, g.playerY + h * 0.5f + bounce + lift, 0.02f },
                         { 0, 1, 0 }, { w, h }, { w * 0.5f, h * 0.5f },
                         g.tilt * 10.0f, catTint);

        // (the cat art already has running legs baked in; the run-cycle
        // bounce + footstep dust above sell the gallop, and code-drawn legs
        // only appear in the fallback DrawCat below)
    }
    else
        DrawCat();                                    // primitive fallback
}

// ------------------------------------------------------------------ HUD
static void TextShadowed(const char* txt, int x, int y, int size, Color col)
{
    DrawText(txt, x + 2, y + 2, size, Color{ 10, 6, 25, 180 });
    DrawText(txt, x, y, size, col);
}

static bool DrawButton(Rectangle r, const char* label)
{
    Vector2 m = GetMousePosition();
    bool hover = CheckCollisionPointRec(m, r);
    Color base = hover ? Color{ 255, 170, 60, 255 } : Color{ 240, 80, 90, 255 };
    DrawRectangleRounded(r, 0.45f, 10, base);
    DrawRectangleRoundedLinesEx(r, 0.45f, 10, 2.0f, WALL_CAP);
    int fs = 26;
    int tw = MeasureText(label, fs);
    DrawText(label, (int)(r.x + r.width / 2.0f - tw / 2.0f),
             (int)(r.y + r.height / 2.0f - fs / 2.0f), fs, WHITE);
    return hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

static void DrawPowerIcon(int kind, int x, int y, int px)
{
    if (IsTextureValid(texPower[kind]))
        DrawTextureEx(texPower[kind], { (float)x, (float)y }, 0.0f,
                      (float)px / texPower[kind].width, WHITE);
    else
    {
        Color c = PowerColor(kind);
        DrawCircle(x + px / 2, y + px / 2, px * 0.5f, c);
    }
}

static void DrawHeart(float cx, float cy, float s, bool full)
{
    Color c = full ? HEART_RED : Color{ 70, 55, 95, 200 };
    DrawCircle((int)(cx - s * 0.45f), (int)(cy - s * 0.25f), (int)(s * 0.55f), c);
    DrawCircle((int)(cx + s * 0.45f), (int)(cy - s * 0.25f), (int)(s * 0.55f), c);
    DrawTriangle({ cx - s * 0.75f, cy - s * 0.05f },
                 { cx + s * 0.75f, cy - s * 0.05f },
                 { cx, cy + s * 1.05f }, c);
}

static void DrawPlayHud()
{
    int sw = GetScreenWidth(), sh = GetScreenHeight();

    Rectangle p = { 14, 14, 250, 120 };
    DrawRectangleRounded(p, 0.18f, 10, ColorAlpha(BLACK, 0.55f));
    TextShadowed("SCORE", 36, 28, 20, SKYBLUE_TX);
    TextShadowed(TextFormat("%d", (int)g.scoreF), 36, 50, 38, RAYWHITE);
    DrawCircle(48, 102, 11, COIN_GOLD);
    DrawCircle(48, 102, 6, Color{ 255, 235, 150, 255 });
    TextShadowed(TextFormat("x %d", g.coins), 66, 90, 26, COIN_GOLD);
    if (g.comboTimer > 0.0f && g.combo >= 3)
        TextShadowed(TextFormat("COMBO x%d", g.combo), 66, 122, 20, COMBO_ORNG);

    // hearts, top-centre (dark heart = already lost)
    for (int i = 0; i < 3; i++)
        DrawHeart(sw / 2 - 62 + i * 44, 26, 15.0f, i < g.hearts);

    Rectangle sb = { sw - 234.0f, 20.0f, 220.0f, 22.0f };
    DrawRectangleRounded(sb, 0.5f, 8, ColorAlpha(BLACK, 0.55f));
    float t = Clamp((g.speed - SPEED_START) / (SPEED_MAX - SPEED_START), 0.0f, 1.0f);
    if (t > 0.01f)
        DrawRectangleRounded({ sb.x + 3, sb.y + 3, (sb.width - 6) * t, sb.height - 6 },
                             0.5f, 8, Color{ 255, 170, 60, 255 });
    TextShadowed("SPEED", sw - 234, 50, 18, SKYBLUE_TX);

    // active power-ups, bottom-left: icon + countdown bar
    int y = sh - 66;
    if (g.shield)
    {
        DrawPowerIcon(1, 18, y, 44);
        TextShadowed("SHIELD", 70, y + 12, 22, Color{ 150, 235, 255, 255 });
        y -= 52;
    }
    if (g.magnetT > 0.0f)
    {
        DrawPowerIcon(0, 18, y, 44);
        DrawRectangleRounded({ 70, (float)y + 10, 110, 24 }, 0.5f, 8,
                             ColorAlpha(BLACK, 0.55f));
        DrawRectangleRounded({ 73, (float)y + 13, 104 * (g.magnetT / 8.0f), 18 },
                             0.5f, 8, Color{ 255, 90, 90, 255 });
        y -= 52;
    }
    if (g.x2T > 0.0f)
    {
        DrawPowerIcon(2, 18, y, 44);
        DrawRectangleRounded({ 70, (float)y + 10, 110, 24 }, 0.5f, 8,
                             ColorAlpha(BLACK, 0.55f));
        DrawRectangleRounded({ 73, (float)y + 13, 104 * (g.x2T / 10.0f), 18 },
                             0.5f, 8, COIN_GOLD);
        TextShadowed("x2 SCORE", 190, y + 12, 22, COIN_GOLD);
        y -= 52;
    }
    if (g.shoesT > 0.0f)
    {
        DrawPowerIcon(3, 18, y, 44);
        DrawRectangleRounded({ 70, (float)y + 10, 110, 24 }, 0.5f, 8,
                             ColorAlpha(BLACK, 0.55f));
        DrawRectangleRounded({ 73, (float)y + 13, 104 * (g.shoesT / 8.0f), 18 },
                             0.5f, 8, HURDLE_COL);
        TextShadowed("JUMP SHOES", 190, y + 12, 22, HURDLE_COL);
        y -= 52;
    }
    if (g.jetT > 0.0f)
    {
        DrawPowerIcon(4, 18, y, 44);
        DrawRectangleRounded({ 70, (float)y + 10, 110, 24 }, 0.5f, 8,
                             ColorAlpha(BLACK, 0.55f));
        DrawRectangleRounded({ 73, (float)y + 13, 104 * (g.jetT / 5.0f), 18 },
                             0.5f, 8, FLAME_OUT);
        TextShadowed("JETPACK", 190, y + 12, 22, FLAME_OUT);
    }

    // combo payout popup, centre screen
    if (g.comboPop > 0.0f)
    {
        const char* s = TextFormat("COMBO x%d   +%d", g.popCombo, g.popBonus);
        int fs = 44;
        TextShadowed(s, sw / 2 - MeasureText(s, fs) / 2, (int)(sh * 0.42f), fs,
                     ColorAlpha(COIN_GOLD, g.comboPop / 1.6f));
    }
}

static void DrawPauseOverlay()
{
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    DrawRectangle(0, 0, sw, sh, ColorAlpha(BLACK, 0.5f));
    TextShadowed("PAUSED", sw / 2 - MeasureText("PAUSED", 64) / 2, sh / 2 - 60, 64, RAYWHITE);
    const char* hint = "press  P  to continue";
    TextShadowed(hint, sw / 2 - MeasureText(hint, 26) / 2, sh / 2 + 20, 26, SKYBLUE_TX);
}

static void DrawMenuOverlay()
{
    int sw = GetScreenWidth(), sh = GetScreenHeight();

    const char* t1 = "COSMO CAT RUN";
    TextShadowed(t1, sw / 2 - MeasureText(t1, 72) / 2, (int)(sh * 0.10f), 72,
                 Color{ 255, 140, 190, 255 });
    const char* t2 = "a candy-space endless dash";
    TextShadowed(t2, sw / 2 - MeasureText(t2, 28) / 2, (int)(sh * 0.10f) + 88, 28, SKYBLUE_TX);

    Rectangle p = { sw / 2.0f - 270, sh * 0.30f, 540, 320 };
    DrawRectangleRounded(p, 0.18f, 10, ColorAlpha(BLACK, 0.55f));
    int x = (int)p.x + 40;
    int y = (int)p.y + 24;
    TextShadowed("A / D  or  LEFT / RIGHT ... change lane", x, y, 24, RAYWHITE);
    y += 38;
    TextShadowed("SPACE / W / UP ........ rocket jump", x, y, 24, RAYWHITE);
    y += 38;
    TextShadowed("S / DOWN .............. slide under gates", x, y, 24, RAYWHITE);
    y += 38;
    TextShadowed("P ..................... pause", x, y, 24, RAYWHITE);
    y += 38;
    TextShadowed("MOBILE: swipe ........ steer, tap = jump", x, y, 24, RAYWHITE);
    y += 38;
    TextShadowed("5 power-ups - trains! - UFO attacks - portals", x, y, 24,
                 Color{ 150, 235, 255, 255 });

    Rectangle btn = { sw / 2.0f - 130, sh * 0.70f, 260, 64 };
    bool click = DrawButton(btn, "START  >>");
    if (click || IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER)) ResetRun();

    if (g.best > 0)
    {
        const char* bs = TextFormat("BEST  %d", g.best);
        TextShadowed(bs, sw / 2 - MeasureText(bs, 26) / 2, (int)(sh * 0.70f) + 80, 26, COIN_GOLD);
    }
}

static void DrawGameOverOverlay()
{
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    Rectangle p = { sw / 2.0f - 300, sh * 0.22f, 600, 352 };
    DrawRectangleRounded(p, 0.12f, 10, ColorAlpha(BLACK, 0.65f));

    const char* big = "CRASHED!";
    TextShadowed(big, sw / 2 - MeasureText(big, 60) / 2, (int)p.y + 30, 60,
                 Color{ 255, 95, 115, 255 });

    const char* line = TextFormat("SCORE %d      COINS %d      BEST %d",
                                  (int)g.scoreF, g.coins, g.best);
    TextShadowed(line, sw / 2 - MeasureText(line, 28) / 2, (int)p.y + 130, 28, RAYWHITE);

    const char* zl = TextFormat("you reached: %s", ZONES[g.zone].name);
    TextShadowed(zl, sw / 2 - MeasureText(zl, 22) / 2, (int)p.y + 168, 22, SKYBLUE_TX);

    if ((int)g.scoreF >= g.best && g.best > 0)
    {
        const char* nb = "NEW BEST!";
        TextShadowed(nb, sw / 2 - MeasureText(nb, 24) / 2, (int)p.y + 198, 24, COIN_GOLD);
    }

    Rectangle btn = { sw / 2.0f - 150, p.y + 246, 300, 62 };
    bool click = DrawButton(btn, "RETRY  (ENTER)");
    if (click || (g.crashTimer > 0.6f && (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_R) ||
                IsKeyPressed(KEY_SPACE))))
        ResetRun();
}

static void DrawBackdrop(Texture2D tex, float alpha, int sw, int sh)
{
    if (!IsTextureValid(tex)) return;
    float scale = fmaxf((float)sw / tex.width, (float)sh / tex.height);
    float dw = tex.width * scale, dh = tex.height * scale;
    float dy = (sh - dh) * 0.6f;
    float off = fmodf(g.time * 10.0f, dw);        // gentle endless drift
    Rectangle src = { 0, 0, (float)tex.width, (float)tex.height };
    Color tint = ColorAlpha(WHITE, alpha);
    DrawTexturePro(tex, src, { -off, dy, dw, dh }, { 0, 0 }, 0, tint);
    DrawTexturePro(tex, src, { dw - off, dy, dw, dh }, { 0, 0 }, 0, tint);
}

static void DrawZoneBanner()
{
    if (g.bannerTimer <= 0.0f) return;
    float bt = g.bannerTimer;
    float alpha = (bt > 2.6f) ? (3.0f - bt) / 0.4f
                : (bt < 0.8f) ? bt / 0.8f : 1.0f;
    if (alpha <= 0.0f) return;
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    const Zone& zn = ZONES[g.zone];
    const char* lv = TextFormat("LEVEL %d", g.zone + 1);
    int fs1 = 30, fs2 = 58;
    int y = (int)(sh * 0.20f);
    TextShadowed(lv, sw / 2 - MeasureText(lv, fs1) / 2, y, fs1,
                 ColorAlpha(WHITE, alpha));
    TextShadowed(zn.name, sw / 2 - MeasureText(zn.name, fs2) / 2, y + 40, fs2,
                 ColorAlpha(zn.banner, alpha));
}

static void DrawFrame()
{
    int sw = GetScreenWidth(), sh = GetScreenHeight();

    // camera with crash shake
    float shX = 0.0f, shY = 0.0f;
    if (g.shake > 0.0f)
    {
        shX = GetRandomValue(-30, 30) / 100.0f * g.shake;
        shY = GetRandomValue(-30, 30) / 100.0f * g.shake;
    }
    Camera3D cam = {};
    cam.position = { g.playerX * 0.45f + shX, 4.3f + shY, 7.6f };
    cam.target = { g.playerX * 0.55f, 1.5f, -8.0f };
    cam.up = { 0.0f, 1.0f, 0.0f };
    cam.fovy = 52.0f;
    cam.projection = CAMERA_PERSPECTIVE;

    ClearBackground(ZONES[g.zone].sky);

    // zone backdrop (cover-fit) that slowly drifts sideways so even the far
    // background keeps moving; on a zone change the new backdrop crossfades in
    static Texture2D zoneBgs[ZONE_COUNT] = { texBg, texBgNebula, texBgIce,
                                             texBgLava, texBgNeon, texBgGalaxy };
    if (g.zoneFade <= 0.0f || g.prevZone == g.zone)
        DrawBackdrop(zoneBgs[g.zone], 1.0f, sw, sh);
    else
    {
        DrawBackdrop(zoneBgs[g.prevZone], 1.0f, sw, sh);
        DrawBackdrop(zoneBgs[g.zone], 1.0f - g.zoneFade / 0.7f, sw, sh);
    }

    BeginMode3D(cam);
    DrawWorld(cam);
    EndMode3D();

    // red flash right after a crash
    if (g.state == GAMEOVER && g.crashTimer < 0.35f)
        DrawRectangle(0, 0, sw, sh, ColorAlpha(Color{ 255, 60, 80, 255 },
                                               (0.35f - g.crashTimer) * 1.2f));

    if (g.state == MENU) DrawMenuOverlay();
    else if (g.state == PLAYING)
    {
        DrawPlayHud();
        if (g.paused) DrawPauseOverlay();
        DrawZoneBanner();
    }
    else DrawGameOverOverlay();
}

// --------------------------------------------------------------------- main
int main(int argc, char* argv[])
{
    bool autoTest = false;
    for (int i = 1; i < argc; i++)
        if (strcmp(argv[i], "--auto") == 0) autoTest = true;

    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(SCREEN_W, SCREEN_H, "Cosmo Cat Run");
    SetTargetFPS(60);

    g.best = LoadBest();
    InitStars();
    LoadAssets();
    InitAudioDevice();                  // synthesised SFX; without an audio
    audioOk = IsAudioDeviceReady();     // device (CI) the Sfx() calls no-op
    if (audioOk) LoadSounds();

    int frame = 0;
    bool autoFreeze = false;            // freeze the sim for staged screenshots
    while (!WindowShouldClose())
    {
        float dt = GetFrameTime();
        if (dt > 0.05f) dt = 0.05f;
        if (!autoFreeze) Update(dt);

        BeginDrawing();
        DrawFrame();
        EndDrawing();

        if (autoTest)
        {
            if (frame == 20) TakeScreenshot("menu.png");
            if (frame == 25)
            {
                ResetRun();
                SpawnPattern();                       // pull one pattern close so
                for (Obstacle& o : g.obstacles) o.z += 100.0f;   // the screenshot has
                for (Coin& c : g.coinsList) c.z += 100.0f;       // some action in it
                SpawnPattern();
                g.hearts = 2;                         // show the hearts HUD
            }
            if (frame == 60) g.speed = 28.0f;                        // show speed streaks
            if (frame == 100) { g.vy = JUMP_V; g.airborne = true; }  // mid-air shot
            if (frame == 116) { g.combo = 12; g.comboTimer = 1.6f; } // combo meter
            // demo gates are spawned fresh right before each shot: during the
            // first frames dt is clamped high, so pre-placed props would drift
            if (frame == 117)
            {
                g.obstacles.clear();
                AddGate(1, -9.0f);                    // gate ahead of the cat
                AddCoinRow(1, -10.5f, 4);             // with coins under it
            }
            if (frame == 118) TakeScreenshot("gameplay.png");
            if (frame == 120)                         // slide pose under a gate
            {
                g.obstacles.clear();
                g.vy = 0.0f; g.playerY = 0.0f; g.airborne = false;
                g.sliding = true; g.slideT = 1.0f;
                AddGate(1, -7.0f);
                AddCoinRow(1, -8.5f, 4);
            }
            if (frame == 122) TakeScreenshot("slide.png");
            // one screenshot per level zone (scroll is moved with the zone so
            // the automatic zone detection does not fight the manual pick;
            // shots are taken ~0.4 s after the banner starts so it is visible)
            if (frame == 126)
            {
                g.obstacles.clear(); g.coinsList.clear(); g.powerups.clear();
                g.scroll = 1 * ZONE_LEN + 10; g.scoreF = g.scroll;
                g.nextSpawn = g.scroll + 25;
                g.prevZone = 0; g.zone = 1; g.bannerTimer = 3.0f; g.zoneFade = 0.0f;
                g.sliding = false; g.slideT = 0.0f;   // stand back up for the shots
                g.powerups.push_back({ 0, -1.6f, 0.95f, -10.0f, true });   // show all
                g.powerups.push_back({ 1,  1.6f, 0.95f, -13.0f, true });   // five
                g.powerups.push_back({ 2,  0.0f, 1.15f, -16.0f, true });   // power-ups
                g.powerups.push_back({ 3, -1.6f, 0.95f, -19.0f, true });
                g.powerups.push_back({ 4,  0.0f, 1.15f, -22.0f, true });
                g.shield = true; g.magnetT = 5.0f; g.x2T = 7.0f;           // + HUD bars
                g.shoesT = 6.0f; g.jetT = 4.0f;
            }
            if (frame == 150) TakeScreenshot("zone_nebula.png");
            if (frame == 152)
            {
                g.obstacles.clear(); g.coinsList.clear(); g.powerups.clear();
                g.scroll = 2 * ZONE_LEN + 10; g.scoreF = g.scroll;
                g.nextSpawn = g.scroll + 25;
                g.prevZone = 1; g.zone = 2; g.bannerTimer = 3.0f; g.zoneFade = 0.0f;
                g.shield = false; g.magnetT = 0.0f; g.x2T = 0.0f;
                g.shoesT = 0.0f; g.jetT = 0.0f;
            }
            if (frame == 176) TakeScreenshot("zone_ice.png");
            if (frame == 178)
            {
                g.obstacles.clear(); g.coinsList.clear(); g.powerups.clear();
                g.scroll = 3 * ZONE_LEN + 10; g.scoreF = g.scroll;
                g.nextSpawn = g.scroll + 25;
                g.prevZone = 2; g.zone = 3; g.bannerTimer = 3.0f; g.zoneFade = 0.0f;
            }
            if (frame == 202) TakeScreenshot("zone_lava.png");
            if (frame == 204)
            {
                g.obstacles.clear(); g.coinsList.clear(); g.powerups.clear();
                g.scroll = 4 * ZONE_LEN + 10; g.scoreF = g.scroll;
                g.nextSpawn = g.scroll + 25;
                g.prevZone = 3; g.zone = 4; g.bannerTimer = 3.0f; g.zoneFade = 0.0f;
            }
            if (frame == 228) TakeScreenshot("zone_neon.png");
            if (frame == 230)
            {
                g.obstacles.clear(); g.coinsList.clear(); g.powerups.clear();
                g.scroll = 5 * ZONE_LEN + 10; g.scoreF = g.scroll;
                g.nextSpawn = g.scroll + 25;
                g.prevZone = 4; g.zone = 5; g.bannerTimer = 3.0f; g.zoneFade = 0.0f;
            }
            if (frame == 254) TakeScreenshot("zone_galaxy.png");
            // trains ahead: two parked cars + one racing in from the far lane
            if (frame == 262)
            {
                g.obstacles.clear(); g.coinsList.clear(); g.powerups.clear();
                AddTrain(0, -16.0f, false);
                AddTrain(2, -30.0f, false);
                AddTrain(1, -70.0f, true);
                AddCoinRow(1, -14.0f, 3);
                g.lane = 1; g.targetX = 0.0f; g.playerX = 0.0f;
            }
            if (frame == 286) TakeScreenshot("trains.png");
            // UFO attack: saucer locked on, beam + reticle + ball in flight
            // (sim frozen so the whole scene shows at once)
            if (frame == 288)
            {
                g.obstacles.clear(); g.coinsList.clear();
                g.ufoState = 2; g.ufoPhaseT = 0.45f;
                g.ufoZ = -22.0f; g.ufoX = 0.0f; g.ufoLane = 1;
                g.projectiles.push_back({ 0.0f, 1.6f, -12.0f, true });
                g.hearts = 3; g.invincT = 0.0f;
                autoFreeze = true;
            }
            if (frame == 310)
            {
                TakeScreenshot("ufo.png");
                autoFreeze = false;
            }
            // portal: 18 m before the level-5 boundary, Neon City colours
            if (frame == 312)
            {
                g.obstacles.clear(); g.coinsList.clear(); g.powerups.clear();
                g.projectiles.clear(); g.ufoState = 0;
                g.scroll = 4 * ZONE_LEN - 18.0f; g.scoreF = g.scroll;
                g.nextSpawn = g.scroll + 25;
                g.prevZone = 3; g.zone = 3; g.bannerTimer = 0.0f; g.zoneFade = 0.0f;
                g.sliding = false; g.slideT = 0.0f;
                g.lane = 1; g.targetX = 0.0f; g.playerX = 0.0f;
            }
            if (frame == 336) TakeScreenshot("portal.png");
            if (frame >= 340)
            {
                printf("FPS: %d\n", GetFPS());     // performance proof for the log
                break;
            }
        }
        frame++;
    }

    UnloadAssets();
    UnloadSounds();
    CloseWindow();
    return 0;
}
