#pragma once

#include "ModSettings.h"

// Sync's Toolkit menus. Made by Sync.

// Entry shown in the pause menu.
class ToolkitMenu : public ModMenu {
public:
    const char* GetName() override { return "SYNC'S TOOLKIT"; }
    const char* GetDescription() override { return "VERSION 3.2 BY SYNC. GOD MODE, ENEMIES, FLY, SWING SPEED AND TELEPORT."; }
    MenuType GetType() override { return MenuType::Pause; }
    void Create() override;
};

class HealthMenu : public BaseMenu {
public:
    const char* GetName() override { return "HEALTH"; }
    const char* GetDescription() override { return "HEAL, ADD HEALTH, REGEN, MAX HEALTH AND GUARD."; }
    void Create() override;
};

class EnemyMenu : public BaseMenu {
public:
    const char* GetName() override { return "ENEMY"; }
    const char* GetDescription() override { return "HOW MUCH DAMAGE THEY DO AND HEALTH THEY HAVE. 0.25X TO 10X."; }
    void Create() override;
};

class SwingMenu : public BaseMenu {
public:
    const char* GetName() override { return "SWING SPEED"; }
    const char* GetDescription() override { return "SWING AND TRAVEL FASTER. 1X TO 5X."; }
    void Create() override;
};

class FlyMenu : public BaseMenu {
public:
    const char* GetName() override { return "FLY"; }
    const char* GetDescription() override { return "SPEED, BOOST AND CONTROLS."; }
    void Create() override;
};

class TeleportMenu : public BaseMenu {
public:
    const char* GetName() override { return "TELEPORT"; }
    const char* GetDescription() override { return "JUMPS, SAVED SPOTS AND MEET UP."; }
    void Create() override;
};

class HotkeysMenu : public BaseMenu {
public:
    const char* GetName() override { return "HOTKEYS"; }
    const char* GetDescription() override { return "EVERY KEY AND BUTTON."; }
    void Create() override;
};

class DiagnosticsMenu : public BaseMenu {
public:
    const char* GetName() override { return "DIAGNOSTICS"; }
    const char* GetDescription() override { return "WHAT WORKS, AND ANY CRASHES."; }
    void Create() override;
};

class SettingsMenu : public BaseMenu {
public:
    const char* GetName() override { return "SETTINGS"; }
    const char* GetDescription() override { return "SAVE, RESET AND PANIC."; }
    void Create() override;
};
