#include "menu.h"

#include "toolkit.h"
#include "logging.h"

#include <cstdio>

// Labels are upper case to match the game's UI style.
// Menu items keep raw pointers into tk::g and the statics below, so those must outlive the menus.
//
// Every menu has at least one item you can select. A menu made only of
// headers has nothing to focus, and that is what made the old About menu crash.

static char g_status[24][96];
static char g_add_label[40];
static char g_take_label[40];

// ------------------------------------------------------------------ top level

void ToolkitMenu::Create() {
    Header("QUICK SWITCHES");
    Toggle("GOD MODE", "Keeps your health full. Hotkey F5.", &tk::g.god_mode);
    Toggle("FLY MODE", "Hotkey R. Controller: click both sticks (change it in the FLY menu).", &tk::g.fly_enabled);
    Option("SWING SPEED", "Hotkey F6 steps through the speeds. 1X is normal.", &tk::g.swing_idx, tk::kSwingNames, tk::SWING_COUNT);
    Button("HEAL NOW", "Fill your health bar.", []() { tk::HealHero(); });

    Header("MENUS");
    GotoMenu<HealthMenu>();
    GotoMenu<EnemyMenu>();
    GotoMenu<SwingMenu>();
    GotoMenu<FlyMenu>();
    GotoMenu<TeleportMenu>();

    Header("HELP");
    GotoMenu<HotkeysMenu>();
    GotoMenu<DiagnosticsMenu>();
    GotoMenu<SettingsMenu>();
}

// ------------------------------------------------------------------ health

void HealthMenu::Create() {
    Header("HEAL");
    Button("HEAL NOW", "Fill your health bar.", []() { tk::HealHero(); });

    // Pick an amount from a list that goes up in easy steps (25, 50, 75, 100 and so on),
    // then use the two buttons. The buttons show the amount so you can see what they do.
    Option("HEALTH AMOUNT", "Pick how much. Then press ADD or TAKE below.", &tk::g.heal_idx, tk::kHealNames, tk::HEAL_COUNT,
           [this]() { RequestRefresh(); });
    snprintf(g_add_label, sizeof(g_add_label), "ADD %.0f HEALTH", tk::HealAmount());
    snprintf(g_take_label, sizeof(g_take_label), "TAKE %.0f HEALTH", tk::HealAmount());
    Button(g_add_label, "Heals that much. Never goes over your max health.", []() { tk::AddHeroHealth(tk::HealAmount()); });
    Button(g_take_label, "Removes that much. Handy for testing.", []() { tk::AddHeroHealth(-tk::HealAmount()); });

    Header("REGEN");
    Toggle("SLOW REGEN", "Slowly heal over time.", &tk::g.slow_regen);
    Option("REGEN SPEED", "How fast SLOW REGEN heals.", &tk::g.regen_idx, tk::kRegenNames, tk::REGEN_COUNT);

    Header("MAX HEALTH");
    Toggle("CUSTOM MAX HEALTH", "Keep your max health at the amount below.", &tk::g.custom_max_enabled);
    Option("MAX HEALTH", "Pick the max health to keep. Turn CUSTOM MAX HEALTH on for it to apply.", &tk::g.max_idx, tk::kMaxHpNames, tk::MAXHP_COUNT);

    Header("PROTECTION");
    Toggle("LOW HEALTH GUARD", "Never let health drop below the level chosen here.", &tk::g.guard_enabled);
    static const char* kGuardNames[3] = { "10 PERCENT OF MAX", "25 PERCENT OF MAX", "50 PERCENT OF MAX" };
    Option("GUARD LEVEL", nullptr, &tk::g.guard_idx, kGuardNames, 3);
}

// ------------------------------------------------------------------ enemy

void EnemyMenu::Create() {
    Header("DAMAGE");
    Option("DAMAGE THEY DO", "Scales every hit you take. 1X is normal, 0.25X is gentle, 10X hurts.", &tk::g.damage_idx, tk::kMultNames, tk::MULT_COUNT);

    Header("HEALTH");
    Option("HEALTH THEY HAVE", "Scales the health of enemies near you and keeps how hurt they are. Change it as often as you like. See DIAGNOSTICS if it does nothing.", &tk::g.enemy_health_idx, tk::kMultNames, tk::MULT_COUNT);

    Header("RESET");
    Button("BACK TO NORMAL", "Both multipliers back to 1X.", [this]() {
        tk::ResetEnemySettings();
        RequestRefresh();
    });
}

// ------------------------------------------------------------------ swing

void SwingMenu::Create() {
    Header("SPEED");
    Option("SWING SPEED", "How much faster you move while swinging, gliding or falling fast. Hotkey F6. 1X is normal.", &tk::g.swing_idx, tk::kSwingNames, tk::SWING_COUNT);
    Toggle("BOOST RUNNING TOO", "Also speed up running and walking, not only fast moves.", &tk::g.swing_all);
}

// ------------------------------------------------------------------ fly

void FlyMenu::Create() {
    Header("FLY");
    Toggle("FLY MODE", "Hotkey R.", &tk::g.fly_enabled);

    Header("CONTROLLER");
    Option("CONTROLLER FLY BUTTON", "CLICK BOTH STICKS never clashes with menus. D PAD UP can flip fly while you scroll a menu, so HOLD is safer than TAP.", &tk::g.pad_fly, tk::kPadFlyNames, tk::PAD_FLY_COUNT);
    Option("CONTROLLER UP AND DOWN", "Which buttons fly you up and down. Left stick flies, left stick click boosts.", &tk::g.pad_updown, tk::kPadUpDownNames, tk::PAD_UPDOWN_COUNT);

    Header("SPEED");
    SliderOpts speed;
    speed.min = 1.0f; speed.max = 500.0f; speed.display_min = 1; speed.display_max = 500;
    Slider("FLY SPEED", "Page Up and Page Down also change this.", &tk::g.fly_speed, speed);
    SliderOpts boost;
    boost.min = 1.0f; boost.max = 10.0f; boost.display_min = 1; boost.display_max = 10;
    Slider("BOOST", "Speed multiplier while Left Shift or the left stick click is held.", &tk::g.fly_boost, boost);
    SliderOpts accel;
    accel.min = 1.0f; accel.max = 30.0f; accel.display_min = 1; accel.display_max = 30;
    Slider("SMOOTHNESS", "Lower is floatier. Higher is snappier.", &tk::g.fly_accel, accel);

    Header("CONTROLS");
    Option("STEERING", "FOLLOW CAMERA flies where the camera points. Keys: W A S D, Y up, Left Ctrl down.", &tk::g.fly_steer, tk::kSteerNames, tk::STEER_COUNT);
    Option("FORWARD DIRECTION", "If forward flies sideways, change this.", &tk::g.fly_dir, tk::kDirNames, tk::DIR_COUNT);
    Toggle("SAFE LANDING", "No damage while flying and for a few seconds after.", &tk::g.fly_god);
}

// ------------------------------------------------------------------ teleport

void TeleportMenu::Create() {
    Header("JUMPS");
    Option("JUMP DISTANCE", "Insert and Delete also change this.", &tk::g.jump_idx, tk::kJumpNames, tk::JUMP_COUNT);
    Button("JUMP FORWARD", "Hotkey Home.", []() { tk::Jump(tk::JUMP_FORWARD); });
    Button("JUMP BACK", nullptr, []() { tk::Jump(tk::JUMP_BACK); });
    Button("JUMP UP", nullptr, []() { tk::Jump(tk::JUMP_UP); });
    Button("JUMP DOWN", nullptr, []() { tk::Jump(tk::JUMP_DOWN); });
    Button("UNDO LAST JUMP", "Hotkey F10. Press again to go back and forth.", []() { tk::UndoJump(); });

    Header("SAVED SPOTS");
    Option("SLOT", "Pick one of five spots.", &tk::g.slot_idx, tk::kSlotNames, tk::SLOT_COUNT);
    Button("SAVE HERE", "Remember where you are in this slot.", [this]() {
        tk::SaveSlot(tk::g.slot_idx);
        RequestRefresh();
    });
    Button("GO TO SLOT", "Teleport to the chosen slot.", []() { tk::GoToSlot(tk::g.slot_idx); });

    Header("MEET UP");
    Button("COPY MY POSITION", "Copies your position so you can send it to a friend.", []() { tk::CopyPosition(); });
    Button("GO TO COPIED POSITION", "Teleports to a position copied from a friend.", []() { tk::GoToCopiedPosition(); });

    Header("LOGGER");
    Button("LOG POSITION TO FILE", "Hotkey F7. Writes to positions.csv.", []() { tk::LogPosition("manual"); });
}

// ------------------------------------------------------------------ hotkeys
// Each line is a button that does nothing, so the menu always has something to select.

void HotkeysMenu::Create() {
    auto none = []() {};
    Header("KEYBOARD");
    Button("R", "Fly on or off.", none);
    Button("F5", "God mode on or off.", none);
    Button("F6", "Swing speed: next speed.", none);
    Button("F7", "Log your position to a file.", none);
    Button("HOME", "Jump forward.", none);
    Button("F10", "Undo the last jump.", none);
    Button("INSERT AND DELETE", "Jump distance up and down.", none);
    Button("PAGE UP AND PAGE DOWN", "Fly speed up and down.", none);
    Button("END", "Panic. Turns everything off.", none);

    Header("FLYING");
    Button("W A S D", "Fly.", none);
    Button("Y AND LEFT CTRL", "Fly up and down.", none);
    Button("LEFT SHIFT", "Boost.", none);

    Header("CONTROLLER");
    Button("CLICK BOTH STICKS", "Fly on or off (other buttons in the FLY menu).", none);
    Button("LEFT STICK", "Fly.", none);
    Button("TRIGGERS", "Fly up and down (bumpers or buttons in the FLY menu).", none);
    Button("LEFT STICK CLICK", "Boost.", none);
    Button("PLAYSTATION PADS", "DualSense and DualShock 4 work directly.", none);
}

// ------------------------------------------------------------------ diagnostics

void DiagnosticsMenu::Create() {
    Header("STATUS");
    int n = tk::StatusLines(g_status, 24);
    for (int i = 0; i < n; i++) Header(g_status[i]);

    Header("TOOLS");
    Button("REFRESH", "Update the lines above.", [this]() { RequestRefresh(); });
    Button("SAVE REPORT TO FILE", "Writes diagnostics.txt in the SyncsToolkit folder.", []() { tk::WriteReport(); });
    Button("CLEAR CRASH LOG", "Forget the last crash.", [this]() {
        tk::ClearCrashLog();
        RequestRefresh();
    });
}

// ------------------------------------------------------------------ settings

void SettingsMenu::Create() {
    Header("SETTINGS");
    Toggle("KEYBOARD HOTKEYS", "Turn off if a key clashes with another mod. End still works.", &tk::g.hotkeys);
    Button("SAVE NOW", "Write settings.ini right away.", []() { tk::SaveSettings(); });
    Button("PANIC", "Fly, god mode, regen, guard, custom health, swing speed and enemy multipliers off. Hotkey End.", []() { tk::Panic(); });
    Button("RESET ALL SETTINGS", "Back to the defaults.", [this]() {
        tk::ResetSettings();
        RequestRefresh();
    });
}
