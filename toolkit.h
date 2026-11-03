#pragma once

#include <string>
#include <vector>

#include "Actor.h"
#include "HeroSystem.h"
#include "Health.h"

// Sync's Toolkit: health, enemy, fly, swing, teleport and meet up tools. Made by Sync.

namespace tk {

constexpr const char* NAME    = "SYNC'S TOOLKIT";
constexpr const char* AUTHOR  = "SYNC";
constexpr const char* VERSION = "3.2.0";

constexpr int MULT_COUNT       = 12;
constexpr int MULT_DEFAULT_IDX = 2;   // 1X
extern const float kMultValues[MULT_COUNT];
extern const char* kMultNames[MULT_COUNT];

constexpr int JUMP_COUNT = 7;
extern const float kJumpValues[JUMP_COUNT];
extern const char* kJumpNames[JUMP_COUNT];

// Controller button that turns fly on and off.
constexpr int PAD_FLY_COUNT = 4;
extern const char* kPadFlyNames[PAD_FLY_COUNT];   // 0 off, 1 tap, 2 hold, 3 click both sticks

// Controller buttons for flying up and down.
constexpr int PAD_UPDOWN_COUNT = 3;
extern const char* kPadUpDownNames[PAD_UPDOWN_COUNT];   // 0 triggers, 1 bumpers, 2 face buttons

constexpr int STEER_COUNT = 2;
extern const char* kSteerNames[STEER_COUNT];

constexpr int DIR_COUNT = 4;
extern const char* kDirNames[DIR_COUNT];

constexpr int SLOT_COUNT = 5;
extern const char* kSlotNames[SLOT_COUNT];   // text changes when a slot is saved

// Easy to pick presets, so nobody has to drag a slider one number at a time.
constexpr int HEAL_COUNT = 15;
extern const float kHealValues[HEAL_COUNT];
extern const char* kHealNames[HEAL_COUNT];

constexpr int MAXHP_COUNT = 10;
extern const float kMaxHpValues[MAXHP_COUNT];
extern const char* kMaxHpNames[MAXHP_COUNT];

constexpr int REGEN_COUNT = 9;
extern const float kRegenValues[REGEN_COUNT];
extern const char* kRegenNames[REGEN_COUNT];

constexpr int SWING_COUNT = 8;
extern const float kSwingValues[SWING_COUNT];
extern const char* kSwingNames[SWING_COUNT];

// Every value the menus bind to. Ints are used for toggles because the
// ModSettings Toggle() takes an int pointer.
struct Settings {
    // fly
    int   fly_enabled = 0;
    float fly_speed   = 25.0f;   // units per second
    float fly_accel   = 8.0f;    // higher is snappier, lower is floatier
    float fly_boost   = 4.0f;    // multiplier while Left Shift (or left stick click) is held
    int   fly_steer   = 0;       // 0 = follow the camera (W A S D go where the camera points), 1 = strafe
    int   fly_dir     = 0;       // turns the forward direction by 0, 90, 180 or 270 degrees
    int   fly_god     = 1;       // stay invulnerable while flying and for a few seconds after
    int   pad_fly     = 3;       // controller fly button: 0 off, 1 tap D-pad up, 2 hold D-pad up, 3 click both sticks
    int   pad_updown  = 0;       // controller up and down: 0 triggers, 1 bumpers, 2 face buttons

    // swing
    int   swing_idx   = 0;       // index into kSwingValues (0 = 1X, off)
    int   swing_all   = 0;       // also speed up running, not only swinging and other fast moves

    // health
    int   god_mode           = 0;
    int   slow_regen         = 0;
    int   regen_idx          = 1;                 // index into kRegenValues
    int   custom_max_enabled = 0;
    int   max_idx            = 4;                 // index into kMaxHpValues (1000)
    int   heal_idx           = 3;                 // index into kHealValues (100)
    int   guard_enabled      = 0;
    int   guard_idx          = 1;                 // 0 = 10%, 1 = 25%, 2 = 50%

    // enemy
    int   damage_idx         = MULT_DEFAULT_IDX;  // damage enemies do to you (0.25X to 10X)
    int   enemy_health_idx   = MULT_DEFAULT_IDX;  // health enemies have (0.25X to 10X)

    // teleport
    int   jump_idx = 3;
    int   slot_idx = 0;

    // misc
    int   hotkeys = 1;
};

extern Settings g;

enum JumpDir { JUMP_FORWARD, JUMP_BACK, JUMP_UP, JUMP_DOWN };

// lifecycle
std::string DataDir();
void LoadSettings();
void SaveSettings();
void ResetSettings();
void Start();
void Panic();
void ResetEnemySettings();
void Log(const char* fmt, ...);

// helpers
Actor*  GetHeroActor();
Health* GetHealthOf(Actor* actor);

// health
void HealHero();
void AddHeroHealth(float amount);   // negative takes health
float HealAmount();                  // the amount chosen in the menu

// fly
void ToggleFly();

// swing
void CycleSwingSpeed();

// teleport
void Jump(JumpDir dir);
void UndoJump();
void SaveSlot(int index);
void GoToSlot(int index);
void CopyPosition();
void GoToCopiedPosition();
void LogPosition(const char* label);

// diagnostics
int         StatusLines(char (*out)[96], int max_lines);
void        WriteReport();
void        ClearCrashLog();
const char* LastAction();

} // namespace tk
