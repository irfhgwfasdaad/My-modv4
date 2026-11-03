#include "toolkit.h"

#include "crash.h"
#include "ComponentRegistry.h"
#include "logging.h"
#include "utils.h"

#include <Windows.h>
#include <setupapi.h>

#pragma comment(lib, "setupapi.lib")

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

// Sync's Toolkit, made by Sync.
//
// Safety rules used in this file:
//  * every read or write of game memory goes through Guard(), which catches
//    access violations instead of letting them crash the game
//  * every game function pointer is null checked before use
//  * the worker thread never lets a C++ exception escape
//  * nothing risky switches on at launch (fly always starts off)
//  * the toolkit installs no hooks and patches no game code

namespace tk {

Settings g;

const float kMultValues[MULT_COUNT] = { 0.25f, 0.5f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f };
const char* kMultNames[MULT_COUNT]  = { "0.25X", "0.5X", "1X NORMAL", "2X", "3X", "4X", "5X", "6X", "7X", "8X", "9X", "10X" };

const float kJumpValues[JUMP_COUNT] = { 5.0f, 10.0f, 25.0f, 50.0f, 100.0f, 250.0f, 500.0f };
const char* kJumpNames[JUMP_COUNT]  = { "5 UNITS", "10 UNITS", "25 UNITS", "50 UNITS", "100 UNITS", "250 UNITS", "500 UNITS" };

const char* kSteerNames[STEER_COUNT] = { "FOLLOW CAMERA", "STRAFE" };
const char* kPadFlyNames[PAD_FLY_COUNT] = { "OFF", "TAP D PAD UP", "HOLD D PAD UP", "CLICK BOTH STICKS" };
const char* kPadUpDownNames[PAD_UPDOWN_COUNT] = { "TRIGGERS (R2 UP, L2 DOWN)", "BUMPERS (R1 UP, L1 DOWN)", "BUTTONS (X UP, CIRCLE DOWN)" };

// Presets: pick from a list instead of dragging a slider one number at a time.
const float kHealValues[HEAL_COUNT] = { 25, 50, 75, 100, 150, 200, 250, 300, 400, 500, 750, 1000, 1500, 2000, 5000 };
const char* kHealNames[HEAL_COUNT]  = { "25", "50", "75", "100", "150", "200", "250", "300", "400", "500", "750", "1000", "1500", "2000", "5000" };

const float kMaxHpValues[MAXHP_COUNT] = { 100, 250, 500, 750, 1000, 1500, 2000, 3000, 5000, 10000 };
const char* kMaxHpNames[MAXHP_COUNT]  = { "100", "250", "500", "750", "1000", "1500", "2000", "3000", "5000", "10000" };

const float kRegenValues[REGEN_COUNT] = { 1, 2, 5, 10, 15, 25, 50, 100, 250 };
const char* kRegenNames[REGEN_COUNT]  = { "1 PER SECOND", "2 PER SECOND", "5 PER SECOND", "10 PER SECOND", "15 PER SECOND", "25 PER SECOND", "50 PER SECOND", "100 PER SECOND", "250 PER SECOND" };

const float kSwingValues[SWING_COUNT] = { 1.0f, 1.25f, 1.5f, 2.0f, 2.5f, 3.0f, 4.0f, 5.0f };
const char* kSwingNames[SWING_COUNT]  = { "1X NORMAL", "1.25X", "1.5X", "2X", "2.5X", "3X", "4X", "5X" };
const char* kDirNames[DIR_COUNT]     = { "NORMAL", "TURN 90", "TURN 180", "TURN 270" };

static char g_slot_text[SLOT_COUNT][24];
const char* kSlotNames[SLOT_COUNT] = { g_slot_text[0], g_slot_text[1], g_slot_text[2], g_slot_text[3], g_slot_text[4] };

static const float kGuardFractions[3] = { 0.10f, 0.25f, 0.50f };

static std::atomic<bool>  g_fly_resync { true };
static std::atomic<float> g_fly_grace { 0.0f };         // seconds of invulnerability left
static std::atomic<bool>  g_skip_hero_delta { false };  // re-baseline hero health next tick
static std::atomic<long>  g_faults { 0 };               // guarded access violations
static std::atomic<long>  g_fly_ticks { 0 };
static std::atomic<long>  g_fly_mismatch { 0 };
static std::atomic<bool>  g_pad_connected { false };
static std::atomic<const char*> g_pad_source { "NOT FOUND" };

// swing speed
static std::atomic<bool>  g_swing_resync { true };
static std::atomic<long>  g_swing_boosts { 0 };
static std::atomic<float> g_swing_speed { 0.0f };       // speed the game itself is moving us at (units per second)

// enemy tools (see the ENEMIES section below)
enum EnemyState {
    ES_UNTRIED = 0, ES_READY, ES_NO_NATIVE, ES_BAD_CODE, ES_BAD_TABLE, ES_NO_MATCH, ES_FAULTS
};
static std::atomic<int>   g_enemy_state { ES_UNTRIED };
static std::atomic<int>   g_enemy_tracked { 0 };      // enemies currently scaled
static std::atomic<int>   g_enemy_actors { 0 };       // live actors seen by the last scan
static std::atomic<float> g_enemy_scan_ms { 0.0f };

static float g_hero_last = 0.0f;
static float g_hero_last_max = 0.0f;
static bool  g_hero_have_last = false;

static Vector3 g_undo_pos;
static bool    g_have_undo = false;

struct Slot {
    bool    used = false;
    Vector3 pos;
};
static Slot g_slots[SLOT_COUNT];

// ---------------------------------------------------------------- logging / last action

static std::mutex g_log_mutex;
static char g_action[96] = "NONE";

void Log(const char* fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    buf[sizeof(buf) - 1] = '\0';

    try {
        std::lock_guard<std::mutex> lock(g_log_mutex);
        std::ofstream out(DataDir() + "log.txt", std::ios::app);
        out << '[' << (GetTickCount64() / 1000) << "s] " << buf << '\n';
    } catch (...) {}
}

static void SetAction(const char* fmt, ...) {
    char buf[96];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    buf[sizeof(buf) - 1] = '\0';
    memcpy(g_action, buf, sizeof(buf));
    Log("action: %s", buf);
}

const char* LastAction() { return g_action; }

// ---------------------------------------------------------------- paths / files

std::string DataDir() {
    static std::string dir;
    if (dir.empty()) {
        std::string root = utils::get_executable_directory();
        dir = root + "\\SyncsToolkit\\";
        CreateDirectoryA(dir.c_str(), nullptr);

        // Carry the old SM2Toolkit settings over once (never overwrites).
        std::string old_dir = root + "\\SM2Toolkit\\";
        CopyFileA((old_dir + "settings.ini").c_str(), (dir + "settings.ini").c_str(), TRUE);
    }
    return dir;
}

static std::string Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// ---------------------------------------------------------------- guarded game access
// Guard() runs a callable and swallows access violations. It must not contain
// objects with destructors (MSVC __try rule), so it only holds plain values.

template <class F>
static bool Guard(F&& f) {
    crash::guard_depth++;
    bool ok = true;
    __try {
        f();
    } __except (GetExceptionCode() != 0xE06D7363 ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        ok = false;
    }
    crash::guard_depth--;
    if (!ok) g_faults++;
    return ok;
}

static bool IsSane(const Vector3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z)
        && std::fabs(v.x) < 900000.0f && std::fabs(v.y) < 900000.0f && std::fabs(v.z) < 900000.0f;
}

static Actor* SafeGetHero() {
    Actor* result = nullptr;
    Guard([&]() { result = GetHeroActor(); });
    return result;
}

static Health* SafeHealthPtr(Actor* a) {
    Health* result = nullptr;
    Guard([&]() { result = GetHealthOf(a); });
    return result;
}

static bool SafeGetHealth(Health* h, float* cur, float* mx) {
    bool ok = false;
    Guard([&]() {
        if (!h) return;
        *cur = h->GetHealth();
        *mx  = h->GetMaxHealth();
        ok = true;
    });
    return ok;
}

static bool SafeSetHealth(Health* h, float v) {
    bool ok = false;
    Guard([&]() {
        if (!h) return;
        h->SetHealth(v);
        ok = true;
    });
    return ok;
}

static bool SafeSetMax(Health* h, float v) {
    bool ok = false;
    Guard([&]() {
        if (!h) return;
        h->SetMaxHealth(v);
        ok = true;
    });
    return ok;
}

static bool SafeGetPos(Actor* a, Vector3* out) {
    bool ok = false;
    Guard([&]() {
        if (!a) return;
        Transform* t = a->GetTransform();
        if (!t) return;
        *out = t->GetPosition();
        ok = true;
    });
    return ok;
}

static bool SafeSetPos(Actor* a, const Vector3& p) {
    bool ok = false;
    Guard([&]() {
        if (!a || !IsSane(p)) return;
        Transform* t = a->GetTransform();
        if (!t) return;
        Vector3 copy = p;
        t->SetPosition(copy);
        ok = true;
    });
    return ok;
}

static bool SafeGetRot(Actor* a, float* out) {
    bool ok = false;
    Guard([&]() {
        if (!a) return;
        Transform* t = a->GetTransform();
        if (!t) return;
        float r = t->GetRotation();
        if (!std::isfinite(r)) return;
        *out = r;
        ok = true;
    });
    return ok;
}

static bool SafeGetBasis(Actor* a, float raw[4]) {
    bool ok = false;
    Guard([&]() {
        if (!a) return;
        Transform* t = a->GetTransform();
        if (!t) return;
        // The transform starts with three rows of four floats (right, up, forward).
        const float* f = reinterpret_cast<const float*>(t);
        raw[0] = f[8];   // forward x
        raw[1] = f[10];  // forward z
        raw[2] = f[0];   // right x
        raw[3] = f[2];   // right z
        ok = true;
    });
    return ok;
}

// ---------------------------------------------------------------- game helpers

static bool ComponentsReady() {
    return Native::ComponentRegistry::Instance
        && Native::ComponentRegistry::GetComponentInfo
        && Native::Actor::GetComponent1
        && Native::Actor::GetComponent2;
}

Actor* GetHeroActor() {
    if (!Native::Actor::GetActor) return nullptr;
    HeroSystem* system = GetHeroSystem();
    if (!system) return nullptr;
    return system->GetHero();
}

Health* GetHealthOf(Actor* actor) {
    if (!actor || !ComponentsReady()) return nullptr;
    return actor->GetComponent<Health>();
}

static bool GameFocused() {
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();
}

static float DamageTakenMult() {
    return kMultValues[std::clamp(g.damage_idx, 0, MULT_COUNT - 1)];
}

static float EnemyHealthMult() {
    return kMultValues[std::clamp(g.enemy_health_idx, 0, MULT_COUNT - 1)];
}

static float SwingMult() {
    return kSwingValues[std::clamp(g.swing_idx, 0, SWING_COUNT - 1)];
}

static float CustomMaxHealth() {
    return kMaxHpValues[std::clamp(g.max_idx, 0, MAXHP_COUNT - 1)];
}

static float RegenPerSecond() {
    return kRegenValues[std::clamp(g.regen_idx, 0, REGEN_COUNT - 1)];
}

float HealAmount() {
    return kHealValues[std::clamp(g.heal_idx, 0, HEAL_COUNT - 1)];
}

// ---------------------------------------------------------------- heading

struct Heading {
    float fx = 1.0f, fz = 0.0f;    // forward on the ground plane
    float rx = 0.0f, rz = -1.0f;   // right on the ground plane
};

static bool NormalizeXZ(float& x, float& z) {
    if (!std::isfinite(x) || !std::isfinite(z)) return false;
    float len = std::sqrt(x * x + z * z);
    if (len < 0.1f) return false;
    x /= len;
    z /= len;
    return true;
}

static void Rot90(float& x, float& z, int times) {
    for (int i = 0; i < ((times % 4) + 4) % 4; i++) {
        float nx = -z;
        float nz = x;
        x = nx;
        z = nz;
    }
}

// Reads which way the hero faces. Falls back to a fixed direction when the
// transform cannot be read, so callers always get something usable.
static Heading GetHeading(Actor* hero) {
    Heading h;
    float raw[4] = {};
    if (SafeGetBasis(hero, raw)) {
        float fx = raw[0], fz = raw[1], rx = raw[2], rz = raw[3];
        if (NormalizeXZ(fx, fz)) {
            bool right_ok = NormalizeXZ(rx, rz) && std::fabs(fx * rx + fz * rz) < 0.3f;
            if (!right_ok) { rx = fz; rz = -fx; }
            h.fx = fx; h.fz = fz; h.rx = rx; h.rz = rz;
        }
    }
    Rot90(h.fx, h.fz, g.fly_dir);
    Rot90(h.rx, h.rz, g.fly_dir);
    return h;
}

// ---------------------------------------------------------------- settings

struct SettingRef {
    const char* name;
    int*        i;
    float*      f;
};

// fly_enabled is deliberately not saved: the game never starts in fly mode.
static std::vector<SettingRef> SettingTable() {
    return {
        { "fly_speed",           nullptr, &g.fly_speed },
        { "fly_accel",           nullptr, &g.fly_accel },
        { "fly_boost",           nullptr, &g.fly_boost },
        { "fly_steer",           &g.fly_steer, nullptr },
        { "fly_dir",             &g.fly_dir, nullptr },
        { "fly_god",             &g.fly_god, nullptr },
        { "pad_fly2",            &g.pad_fly, nullptr },           // new name: the old default (tap D-pad up) clashed with menus
        { "pad_updown",          &g.pad_updown, nullptr },
        { "swing_idx",           &g.swing_idx, nullptr },
        { "swing_all",           &g.swing_all, nullptr },
        { "god_mode",            &g.god_mode, nullptr },
        { "slow_regen",          &g.slow_regen, nullptr },
        { "regen_idx",           &g.regen_idx, nullptr },
        { "custom_max_enabled",  &g.custom_max_enabled, nullptr },
        { "max_idx",             &g.max_idx, nullptr },
        { "heal_idx",            &g.heal_idx, nullptr },
        { "guard_enabled",       &g.guard_enabled, nullptr },
        { "guard_idx",           &g.guard_idx, nullptr },
        { "damage_idx",          &g.damage_idx, nullptr },        // enemy damage (kept under the old name so saves still load)
        { "enemy_health_idx",    &g.enemy_health_idx, nullptr },
        { "jump_idx",            &g.jump_idx, nullptr },
        { "slot_idx",            &g.slot_idx, nullptr },
        { "hotkeys",             &g.hotkeys, nullptr },
    };
}

static void ClampSettings() {
    g.fly_speed          = std::clamp(g.fly_speed, 1.0f, 1000.0f);
    g.fly_accel          = std::clamp(g.fly_accel, 0.5f, 60.0f);
    g.fly_boost          = std::clamp(g.fly_boost, 1.0f, 20.0f);
    g.fly_steer          = std::clamp(g.fly_steer, 0, STEER_COUNT - 1);
    g.fly_dir            = std::clamp(g.fly_dir, 0, DIR_COUNT - 1);
    g.regen_idx          = std::clamp(g.regen_idx, 0, REGEN_COUNT - 1);
    g.max_idx            = std::clamp(g.max_idx, 0, MAXHP_COUNT - 1);
    g.heal_idx           = std::clamp(g.heal_idx, 0, HEAL_COUNT - 1);
    g.swing_idx          = std::clamp(g.swing_idx, 0, SWING_COUNT - 1);
    g.pad_updown         = std::clamp(g.pad_updown, 0, PAD_UPDOWN_COUNT - 1);
    g.guard_idx          = std::clamp(g.guard_idx, 0, 2);
    g.damage_idx         = std::clamp(g.damage_idx, 0, MULT_COUNT - 1);
    g.enemy_health_idx   = std::clamp(g.enemy_health_idx, 0, MULT_COUNT - 1);
    g.pad_fly            = std::clamp(g.pad_fly, 0, PAD_FLY_COUNT - 1);
    g.jump_idx           = std::clamp(g.jump_idx, 0, JUMP_COUNT - 1);
    g.slot_idx           = std::clamp(g.slot_idx, 0, SLOT_COUNT - 1);
    // NOTE: fly_enabled is deliberately NOT touched here. This runs on every save,
    // and zeroing it here is what used to switch fly off by itself.
}

void LoadSettings() {
    std::ifstream in(DataDir() + "settings.ini");
    std::string line;
    auto table = SettingTable();
    while (std::getline(in, line)) {
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = Trim(line.substr(0, eq));
        std::string val = Trim(line.substr(eq + 1));
        for (auto& s : table) {
            if (key != s.name) continue;
            try {
                if (s.i) *s.i = std::stoi(val);
                if (s.f) *s.f = std::stof(val);
            } catch (...) {}
        }
    }
    ClampSettings();
    g.fly_enabled = 0;      // the game never starts in fly mode (only when loading, never when saving)
}

void SaveSettings() {
    ClampSettings();
    std::ofstream out(DataDir() + "settings.ini", std::ios::trunc);
    for (auto& s : SettingTable()) {
        if (s.i) out << s.name << '=' << *s.i << '\n';
        if (s.f) out << s.name << '=' << *s.f << '\n';
    }
    SetAction("SETTINGS SAVED");
}

void ResetSettings() {
    // Menu items hold pointers into g, so assign in place instead of replacing it.
    Settings fresh;
    g = fresh;
    g_fly_grace.store(0.0f);
    g_skip_hero_delta = true;
    SetAction("SETTINGS RESET");
}

void Panic() {
    g.fly_enabled        = 0;
    g.god_mode           = 0;
    g.slow_regen         = 0;
    g.custom_max_enabled = 0;
    g.guard_enabled      = 0;
    g.swing_idx          = 0;
    g.damage_idx         = MULT_DEFAULT_IDX;
    g.enemy_health_idx   = MULT_DEFAULT_IDX;   // the worker puts scaled enemies back to normal
    g_fly_grace.store(0.0f);
    g_skip_hero_delta = true;
    SetAction("PANIC: EVERYTHING OFF");
}

void ResetEnemySettings() {
    g.damage_idx       = MULT_DEFAULT_IDX;
    g.enemy_health_idx = MULT_DEFAULT_IDX;
    g_skip_hero_delta = true;
    SetAction("ENEMIES BACK TO 1X");
}

// ---------------------------------------------------------------- saved spots

static void RefreshSlotText() {
    for (int i = 0; i < SLOT_COUNT; i++) {
        snprintf(g_slot_text[i], sizeof(g_slot_text[i]), "SLOT %d %s", i + 1, g_slots[i].used ? "SAVED" : "EMPTY");
    }
}

static void WriteSlots() {
    try {
        std::ofstream out(DataDir() + "spots.txt", std::ios::trunc);
        for (int i = 0; i < SLOT_COUNT; i++) {
            if (!g_slots[i].used) continue;
            out << i << ' ' << g_slots[i].pos.x << ' ' << g_slots[i].pos.y << ' ' << g_slots[i].pos.z << '\n';
        }
    } catch (...) {}
}

static void LoadSlots() {
    try {
        std::ifstream in(DataDir() + "spots.txt");
        int i; float x, y, z;
        while (in >> i >> x >> y >> z) {
            if (i < 0 || i >= SLOT_COUNT) continue;
            Vector3 p(x, y, z);
            if (!IsSane(p)) continue;
            g_slots[i].used = true;
            g_slots[i].pos = p;
        }
    } catch (...) {}
    RefreshSlotText();
}

// ---------------------------------------------------------------- teleport

static void GiveLandingGrace() {
    if (g.fly_god) {
        float cur = g_fly_grace.load();
        if (cur < 4.0f) g_fly_grace.store(4.0f);
    }
}

static bool TeleportTo(Actor* hero, const Vector3& from, const Vector3& to) {
    if (!hero || !IsSane(to)) return false;
    GiveLandingGrace();
    if (!SafeSetPos(hero, to)) return false;
    g_undo_pos = from;
    g_have_undo = true;
    g_fly_resync = true;
    g_swing_resync = true;
    return true;
}

void LogPosition(const char* label) {
    crash::context = "menu: log position";
    Actor* hero = SafeGetHero();
    Vector3 p;
    float rot = 0.0f;
    if (!hero || !SafeGetPos(hero, &p) || !IsSane(p)) {
        SetAction("LOG FAILED: NO HERO");
        return;
    }
    SafeGetRot(hero, &rot);
    double t = GetTickCount64() / 1000.0;
    try {
        std::string path = DataDir() + "positions.csv";
        bool fresh = GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES;
        std::ofstream out(path, std::ios::app);
        if (fresh) out << "uptime_s,label,x,y,z,rotation\n";
        out << t << ',' << label << ',' << p.x << ',' << p.y << ',' << p.z << ',' << rot << '\n';
        SetAction("LOGGED %.0f %.0f %.0f", p.x, p.y, p.z);
    } catch (...) {}
}

void Jump(JumpDir dir) {
    crash::context = "menu: jump";
    Actor* hero = SafeGetHero();
    Vector3 p;
    if (!hero || !SafeGetPos(hero, &p) || !IsSane(p)) {
        SetAction("JUMP FAILED: NO HERO");
        return;
    }
    Heading h = GetHeading(hero);
    float d = kJumpValues[std::clamp(g.jump_idx, 0, JUMP_COUNT - 1)];
    Vector3 n = p;
    switch (dir) {
        case JUMP_FORWARD: n.x += h.fx * d; n.z += h.fz * d; break;
        case JUMP_BACK:    n.x -= h.fx * d; n.z -= h.fz * d; break;
        case JUMP_UP:      n.y += d; break;
        case JUMP_DOWN:    n.y -= d; break;
    }
    if (TeleportTo(hero, p, n)) SetAction("JUMPED %.0f UNITS", d);
    else SetAction("JUMP FAILED");
}

void UndoJump() {
    crash::context = "menu: undo jump";
    if (!g_have_undo) {
        SetAction("NOTHING TO UNDO");
        return;
    }
    Actor* hero = SafeGetHero();
    Vector3 now;
    if (!hero || !SafeGetPos(hero, &now)) {
        SetAction("UNDO FAILED: NO HERO");
        return;
    }
    Vector3 target = g_undo_pos;
    if (TeleportTo(hero, now, target)) SetAction("JUMP UNDONE");
    else SetAction("UNDO FAILED");
}

void SaveSlot(int index) {
    crash::context = "menu: save slot";
    index = std::clamp(index, 0, SLOT_COUNT - 1);
    Actor* hero = SafeGetHero();
    Vector3 p;
    if (!hero || !SafeGetPos(hero, &p) || !IsSane(p)) {
        SetAction("SAVE FAILED: NO HERO");
        return;
    }
    g_slots[index].used = true;
    g_slots[index].pos = p;
    RefreshSlotText();
    WriteSlots();
    SetAction("SAVED SLOT %d", index + 1);
}

void GoToSlot(int index) {
    crash::context = "menu: go to slot";
    index = std::clamp(index, 0, SLOT_COUNT - 1);
    if (!g_slots[index].used) {
        SetAction("SLOT %d IS EMPTY", index + 1);
        return;
    }
    Actor* hero = SafeGetHero();
    Vector3 here;
    if (!hero || !SafeGetPos(hero, &here)) {
        SetAction("GO FAILED: NO HERO");
        return;
    }
    if (TeleportTo(hero, here, g_slots[index].pos)) SetAction("WENT TO SLOT %d", index + 1);
    else SetAction("GO FAILED");
}

static bool SetClipboardText(const char* s) {
    if (!OpenClipboard(nullptr)) return false;
    EmptyClipboard();
    size_t n = strlen(s) + 1;
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, n);
    if (!mem) { CloseClipboard(); return false; }
    void* dst = GlobalLock(mem);
    if (!dst) { GlobalFree(mem); CloseClipboard(); return false; }
    memcpy(dst, s, n);
    GlobalUnlock(mem);
    if (!SetClipboardData(CF_TEXT, mem)) {
        GlobalFree(mem);
        CloseClipboard();
        return false;
    }
    CloseClipboard();
    return true;
}

static bool GetClipboardText(char* out, size_t cap) {
    if (!OpenClipboard(nullptr)) return false;
    bool ok = false;
    HANDLE data = GetClipboardData(CF_TEXT);
    if (data) {
        const char* src = static_cast<const char*>(GlobalLock(data));
        if (src) {
            snprintf(out, cap, "%s", src);
            GlobalUnlock(data);
            ok = true;
        }
    }
    CloseClipboard();
    return ok;
}

void CopyPosition() {
    crash::context = "menu: copy position";
    Actor* hero = SafeGetHero();
    Vector3 p;
    if (!hero || !SafeGetPos(hero, &p) || !IsSane(p)) {
        SetAction("COPY FAILED: NO HERO");
        return;
    }
    char text[96];
    snprintf(text, sizeof(text), "SYNC %.2f %.2f %.2f", p.x, p.y, p.z);
    if (SetClipboardText(text)) SetAction("COPIED %.0f %.0f %.0f", p.x, p.y, p.z);
    else SetAction("COPY FAILED: CLIPBOARD BUSY");
}

void GoToCopiedPosition() {
    crash::context = "menu: go to copied position";
    char text[160] = {};
    if (!GetClipboardText(text, sizeof(text))) {
        SetAction("PASTE FAILED: CLIPBOARD EMPTY");
        return;
    }
    for (char* c = text; *c; ++c) if (*c == ',' || *c == ';') *c = ' ';

    float x = 0, y = 0, z = 0;
    int got = sscanf(text, " SYNC %f %f %f", &x, &y, &z);
    if (got != 3) got = sscanf(text, " %f %f %f", &x, &y, &z);
    Vector3 target(x, y + 1.0f, z);
    if (got != 3 || !IsSane(target)) {
        SetAction("PASTE FAILED: NOT A POSITION");
        return;
    }
    Actor* hero = SafeGetHero();
    Vector3 here;
    if (!hero || !SafeGetPos(hero, &here)) {
        SetAction("GO FAILED: NO HERO");
        return;
    }
    if (TeleportTo(hero, here, target)) SetAction("WENT TO %.0f %.0f %.0f", x, y, z);
    else SetAction("GO FAILED");
}

// ---------------------------------------------------------------- health

void HealHero() {
    crash::context = "menu: heal";
    Actor* hero = SafeGetHero();
    Health* h = SafeHealthPtr(hero);
    float cur = 0.0f, mx = 0.0f;
    if (h && SafeGetHealth(h, &cur, &mx) && SafeSetHealth(h, mx)) {
        g_skip_hero_delta = true;
        SetAction("HEALED TO %.0f", mx);
    } else {
        SetAction("HEAL FAILED: NO HEALTH FOUND");
    }
}

void AddHeroHealth(float amount) {
    crash::context = "menu: add health";
    Actor* hero = SafeGetHero();
    Health* h = SafeHealthPtr(hero);
    float cur = 0.0f, mx = 0.0f;
    if (h && SafeGetHealth(h, &cur, &mx) && std::isfinite(cur) && std::isfinite(mx)
        && SafeSetHealth(h, std::clamp(cur + amount, 0.0f, mx))) {
        g_skip_hero_delta = true;
        SetAction("HEALTH CHANGED BY %.0f", amount);
    } else {
        SetAction("HEALTH CHANGE FAILED");
    }
}

void ToggleFly() {
    g.fly_enabled = !g.fly_enabled;
    SetAction("FLY %s", g.fly_enabled ? "ON" : "OFF");
}

void CycleSwingSpeed() {
    g.swing_idx = (g.swing_idx + 1) % SWING_COUNT;
    g_swing_resync = true;
    SetAction("SWING SPEED %s", kSwingNames[std::clamp(g.swing_idx, 0, SWING_COUNT - 1)]);
}

// About 30 times a second: god mode, landing grace, custom max health,
// damage multiplier, guard and regen.
static void ProcessHero(Actor* hero, float step) {
    Health* h = SafeHealthPtr(hero);
    if (!h) { g_hero_have_last = false; return; }

    float cur = 0.0f, mx = 0.0f;
    if (!SafeGetHealth(h, &cur, &mx)) return;
    if (!std::isfinite(cur) || !std::isfinite(mx) || mx <= 0.0f) return;

    if (g.custom_max_enabled && std::fabs(mx - CustomMaxHealth()) > 0.5f) {
        if (SafeSetMax(h, CustomMaxHealth())) mx = CustomMaxHealth();
        g_hero_have_last = false;
    }
    if (g_skip_hero_delta.exchange(false)) g_hero_have_last = false;

    bool god = g.god_mode || g_fly_grace.load() > 0.0f;
    if (god) {
        if (cur < mx) SafeSetHealth(h, mx);
        g_hero_last = mx;
        g_hero_last_max = mx;
        g_hero_have_last = true;
        return;
    }

    // Enemy damage multiplier: any drop in health since the last tick is scaled.
    // Below 1X a hit that would have been lethal is softened too, when the scaled
    // hit would have left some health. (Above 1X a hit that already killed stays a kill.)
    float dm = DamageTakenMult();
    if (g_hero_have_last && std::fabs(dm - 1.0f) > 0.0001f && (cur > 0.0f || dm < 1.0f)
        && cur < g_hero_last - 0.001f && std::fabs(mx - g_hero_last_max) < 0.5f) {
        float drop = g_hero_last - cur;
        float nc = std::clamp(g_hero_last - drop * dm, 0.0f, mx);
        if (nc > 0.0f && nc != cur && SafeSetHealth(h, nc)) cur = nc;
        else if (nc <= 0.0f && cur > 0.0f && nc != cur && SafeSetHealth(h, nc)) cur = nc;
    }

    // Low health guard: never sit below the chosen fraction of max health.
    if (g.guard_enabled && cur > 0.0f) {
        float floor_hp = mx * kGuardFractions[std::clamp(g.guard_idx, 0, 2)];
        if (cur < floor_hp && SafeSetHealth(h, floor_hp)) cur = floor_hp;
    }

    if (g.slow_regen && cur > 0.0f && cur < mx) {
        float nc = std::min(mx, cur + RegenPerSecond() * step);
        if (SafeSetHealth(h, nc)) cur = nc;
    }

    g_hero_last = cur;
    g_hero_last_max = mx;
    g_hero_have_last = true;
}

// ---------------------------------------------------------------- enemies
// Enemy health multiplier.
//
// The game gives us no list of enemies, so the toolkit reads the game's own
// actor table. The table address, entry size and generation offset are read out
// of the machine code of the game's GetActor function (already found by the
// scanner), and then CHECKED against the hero before anything is trusted:
// the hero's slot must hold the hero's generation number and GetActor must
// return the hero for it. If any check fails the enemy health tool switches
// itself off and DIAGNOSTICS says why. It never writes to a guess.
//
// Each actor found is then confirmed (the game's GetActor returns it, it has a
// Health component that points back at it, sane numbers, close to the hero)
// before its max health and current health are scaled. The fraction of health
// left is kept, so a half dead enemy stays half dead.
// Enemy damage needs none of this: it scales the health you lose (ProcessHero).

constexpr float kEnemyRange = 250.0f;    // only actors this close to the hero are touched
constexpr uint32_t kMaxActorSlots = 0x100000;   // handles use 20 bits for the slot

struct ActorTable {
    const uintptr_t* table_ptr = nullptr;   // global that holds the address of the table
    const uint32_t*  count_ptr = nullptr;   // global that holds the slot count
    uint32_t stride = 0;                    // bytes per slot
    uint32_t gen_off = 0;                   // where the 16 bit generation sits in a slot
    bool     check_handle = false;          // actors store their own handle (checked on the hero)
};
static ActorTable g_tbl;

struct EnemyRec {
    uint32_t raw;        // actor handle
    float    base_max;   // max health before we scaled it
    float    applied;    // multiplier currently applied to it
    int      missed;     // scans in a row where this actor was not found
    bool     seen;
};
static std::vector<EnemyRec> g_enemy_recs;      // worker thread only
static std::vector<uint32_t> g_scan_buf;        // worker thread only
static float g_enemy_timer = 0.0f;
static float g_enemy_interval = 0.5f;
static float g_enemy_retry = 0.0f;
static int   g_enemy_fault_ticks = 0;
static float g_enemy_last_want = 1.0f;
static int   g_enemy_log_budget = 60;           // detailed log lines left (so log.txt stays small)
static std::atomic<long> g_enemy_scaled_total { 0 };
static std::atomic<long> g_enemy_rebased { 0 };
static std::atomic<long> g_enemy_refused { 0 };

// An enemy that is missing from this many scans in a row is forgotten.
// Records are kept a long time on purpose: forgetting an enemy that is still
// scaled makes the next scan treat its scaled health as normal and scale it AGAIN.
constexpr int kEnemyMissLimit = 120;

// Reads the table layout out of GetActor and proves it with the hero.
static int LearnActorTable(Actor* hero) {
    if (!Native::Actor::GetActor || !hero) return ES_NO_NATIVE;
    HeroSystem* hs = GetHeroSystem();
    if (!hs) return ES_NO_NATIVE;

    ActorTable t;
    bool layout_ok = false;
    Guard([&]() {
        const uint8_t* c = reinterpret_cast<const uint8_t*>(Native::Actor::GetActor);
        // These offsets follow the byte pattern the scanner used to find GetActor.
        if (memcmp(c + 24, "\x4B\x8D\x0C", 3) != 0) return;         // lea rcx,[r8+r8*k]
        if (memcmp(c + 28, "\x48\xC1\xE1\x06", 4) != 0) return;     // shl rcx,6
        if (memcmp(c + 32, "\x48\x03\x0D", 3) != 0) return;         // add rcx,[rip+table]
        if (memcmp(c + 39, "\x44\x3B\x05", 3) != 0) return;         // cmp r8d,[rip+count]
        if (memcmp(c + 48, "\x0F\xB7\x41", 3) != 0) return;         // movzx eax,word [rcx+gen]
        uint8_t sib = c[27];
        if ((sib & 0x3F) != 0) return;                              // index and base must both be r8
        uint32_t mult = 1u + (1u << (sib >> 6));
        int32_t d1 = 0, d2 = 0;
        memcpy(&d1, c + 35, 4);
        memcpy(&d2, c + 42, 4);
        t.table_ptr = reinterpret_cast<const uintptr_t*>(c + 39 + d1);
        t.count_ptr = reinterpret_cast<const uint32_t*>(c + 46 + d2);
        t.stride    = mult * 64u;
        t.gen_off   = c[51];
        layout_ok = true;
    });
    if (!layout_ok || t.gen_off > t.stride - 2) return ES_BAD_CODE;

    uint32_t raw = 0;
    Guard([&]() { raw = hs->GetHeroHandle().Raw(); });
    uint32_t gen = (raw >> 20) & 0x7FF;
    uint32_t idx = raw & 0xFFFFF;
    if (gen == 0 || idx == 0) return ES_NO_MATCH;

    bool good = false, handle_check = false;
    Guard([&]() {
        uintptr_t tbl = *t.table_ptr;
        uint32_t  cnt = *t.count_ptr;
        if (!tbl || cnt == 0 || cnt > kMaxActorSlots || idx >= cnt) return;

        // The hero's slot must carry the hero's generation.
        uint16_t w = *reinterpret_cast<const uint16_t*>(tbl + (uintptr_t)idx * t.stride + t.gen_off);
        if (w != gen) return;

        // A wrong entry size would make most of the first slots read as nonsense.
        uint32_t look = cnt < 512 ? cnt : 512, bad = 0;
        for (uint32_t i = 0; i < look; i++) {
            uint16_t v = *reinterpret_cast<const uint16_t*>(tbl + (uintptr_t)i * t.stride + t.gen_off);
            if (v > 0x7FF) bad++;
        }
        if (bad * 10 > look) return;

        // And the game's own lookup must agree.
        ActorHandle hd(gen, idx);
        if (Native::Actor::GetActor(&hd) != hero) return;

        good = true;
        handle_check = (hero->GetHandle().Raw() == raw);
    });
    if (!good) return ES_NO_MATCH;

    t.check_handle = handle_check;
    g_tbl = t;
    Log("enemy tools: actor table found, entry size %u, generation offset %u, handle check %s",
        t.stride, t.gen_off, handle_check ? "on" : "off");
    return ES_READY;
}

static bool EnsureActorTable(Actor* hero, float dt) {
    int st = g_enemy_state.load();
    if (st == ES_READY) return true;
    if (st == ES_NO_NATIVE || st == ES_BAD_CODE || st == ES_FAULTS) return false;   // not worth retrying
    g_enemy_retry -= dt;
    if (g_enemy_retry > 0.0f) return false;
    g_enemy_retry = 4.0f;
    int now = LearnActorTable(hero);
    if (now != st) {
        g_enemy_state = now;
        Log("enemy tools state: %d", now);
    }
    return now == ES_READY;
}

// Lists the handles of every live slot in the actor table.
static bool ScanLiveHandles(std::vector<uint32_t>& out) {
    out.clear();
    return Guard([&]() {
        uintptr_t tbl = *g_tbl.table_ptr;
        uint32_t  cnt = *g_tbl.count_ptr;
        if (!tbl || cnt == 0 || cnt > kMaxActorSlots) return;
        for (uint32_t i = 0; i < cnt; i++) {
            uint16_t w = *reinterpret_cast<const uint16_t*>(tbl + (uintptr_t)i * g_tbl.stride + g_tbl.gen_off);
            if (w == 0 || w > 0x7FF) continue;
            out.push_back((uint32_t(w) << 20) | i);
            if (out.size() >= 40000) break;
        }
    });
}

// Puts an enemy's health at base * target, keeping how hurt it is.
// Writes in an order that never leaves current health above max health.
// Returns false when the game did not keep the value we wrote.
static bool RescaleTo(Health* h, EnemyRec* rec, float cur, float mx, float target) {
    float frac = std::clamp(cur / mx, 0.0f, 1.0f);
    float nm = rec->base_max * target;
    float nc = std::max(frac * nm, 0.01f);
    if (nm < mx) { h->SetHealth(nc); h->SetMaxHealth(nm); }
    else         { h->SetMaxHealth(nm); h->SetHealth(nc); }

    float back = h->GetMaxHealth();
    bool stuck = std::isfinite(back) && std::fabs(back - nm) <= std::max(0.5f, nm * 0.001f);
    if (g_enemy_log_budget > 0) {
        g_enemy_log_budget--;
        Log("enemy %08X: max %.0f -> %.0f (base %.0f, x%.2f), health %.0f -> %.0f, %s",
            rec->raw, mx, nm, rec->base_max, target, cur, nc, stuck ? "kept" : "GAME CHANGED IT BACK");
    }
    rec->applied = target;
    if (stuck) g_enemy_scaled_total++; else g_enemy_refused++;
    return stuck;
}

// Checks one actor and scales its health when it is an enemy sized target.
// Returns false only when a memory fault was caught.
static bool TouchEnemy(uint32_t raw, Actor* hero, const Vector3& hp, float want, bool active) {
    return Guard([&]() {
        EnemyRec* rec = nullptr;
        for (auto& r : g_enemy_recs) {
            if (r.raw == raw) { rec = &r; break; }
        }
        if (!rec && !active) return;

        ActorHandle hd(raw >> 20, raw & 0xFFFFF);
        Actor* a = Native::Actor::GetActor(&hd);
        if (!a) return;
        if (g_tbl.check_handle && a->GetHandle().Raw() != raw) return;
        if (rec) { rec->seen = true; rec->missed = 0; }   // it still exists, keep the record

        if (a == hero) {
            // We scaled this actor while it was not the hero (for example the other
            // Spider-Man) and now it is. Put its health back, never leave it scaled.
            if (rec && std::fabs(rec->applied - 1.0f) > 0.0001f) {
                Health* hh = a->GetComponent<Health>();
                if (hh && hh->GetActor() == a) {
                    float c = hh->GetHealth(), m = hh->GetMaxHealth();
                    if (std::isfinite(c) && std::isfinite(m) && m >= 1.0f) RescaleTo(hh, rec, c, m, 1.0f);
                }
                rec->applied = 1.0f;
            }
            return;
        }

        Transform* tf = a->GetTransform();
        if (!tf) return;
        Vector3 p = tf->GetPosition();
        if (!IsSane(p)) return;

        // New enemies are only picked up near the hero. Ones we already scaled are
        // kept in step with the setting wherever they are.
        if (!rec) {
            float dx = p.x - hp.x, dy = p.y - hp.y, dz = p.z - hp.z;
            if (dx * dx + dy * dy + dz * dz > kEnemyRange * kEnemyRange) return;
        }

        Health* h = a->GetComponent<Health>();
        if (!h || h->GetActor() != a) return;
        float cur = h->GetHealth(), mx = h->GetMaxHealth();
        if (!std::isfinite(cur) || !std::isfinite(mx)) return;
        if (mx < 1.0f || mx > 10000000.0f || cur < 0.0f || cur > mx * 1.5f + 1.0f) return;

        if (!rec) {
            if (cur <= 0.0f) return;                        // already dead
            if (g_enemy_recs.size() >= 512) return;
            g_enemy_recs.push_back(EnemyRec { raw, mx, 1.0f, 0, true });
            rec = &g_enemy_recs.back();
        }

        // If max health is not what we left it at, the game changed it
        // (new phase, respawn, reset). Take the new value as the baseline.
        float expect = rec->base_max * rec->applied;
        if (std::fabs(mx - expect) > std::max(0.5f, expect * 0.001f)) {
            rec->base_max = mx;
            rec->applied  = 1.0f;
            g_enemy_rebased++;
        }

        if (cur > 0.0f && std::fabs(rec->applied - want) > 0.0001f) {
            RescaleTo(h, rec, cur, mx, want);
        }
    });
}

static void ProcessEnemies(Actor* hero, const Vector3& hero_pos, float dt) {
    float want = EnemyHealthMult();
    bool active = std::fabs(want - 1.0f) > 0.0001f;
    if (!active && g_enemy_recs.empty()) {
        g_enemy_tracked = 0;
        g_enemy_timer = 0.0f;
        g_enemy_last_want = want;
        return;
    }
    if (!EnsureActorTable(hero, dt)) return;

    // The menu value changed: do not wait for the next scheduled scan.
    if (std::fabs(want - g_enemy_last_want) > 0.0001f) {
        g_enemy_last_want = want;
        g_enemy_timer = 0.0f;
        g_enemy_interval = 0.5f;
        g_enemy_log_budget = std::max(g_enemy_log_budget, 30);
        Log("enemy health setting is now x%.2f", want);
    }

    g_enemy_timer -= dt;
    if (g_enemy_timer > 0.0f) return;
    g_enemy_timer = g_enemy_interval;

    crash::context = "worker: enemy scan";
    auto t0 = std::chrono::steady_clock::now();
    if (!ScanLiveHandles(g_scan_buf)) {
        if (++g_enemy_fault_ticks >= 10) {
            g_enemy_state = ES_FAULTS;
            g_enemy_recs.clear();
            g_enemy_tracked = 0;
            Log("enemy tools switched off: the actor table could not be read");
        }
        return;
    }
    g_enemy_fault_ticks = 0;
    g_enemy_actors = (int)g_scan_buf.size();

    crash::context = "worker: enemy health";
    for (auto& r : g_enemy_recs) r.seen = false;
    int faults = 0;
    bool complete = true;
    for (uint32_t raw : g_scan_buf) {
        if (!TouchEnemy(raw, hero, hero_pos, want, active)) {
            if (++faults > 25) { complete = false; break; }
        }
    }

    // Age the records, but only after a scan that looked at everything.
    // A scan that stopped early would make live enemies look gone.
    if (complete && !g_scan_buf.empty()) {
        for (auto& r : g_enemy_recs) {
            if (r.seen) r.missed = 0; else r.missed++;
        }
    }
    g_enemy_recs.erase(std::remove_if(g_enemy_recs.begin(), g_enemy_recs.end(),
        [&](const EnemyRec& r) {
            return r.missed >= kEnemyMissLimit || (!active && std::fabs(r.applied - 1.0f) < 0.0001f);
        }),
        g_enemy_recs.end());
    g_enemy_tracked = (int)g_enemy_recs.size();

    // Keep the scan under about a tenth of a core on big worlds.
    float ms = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();
    g_enemy_scan_ms = ms;
    g_enemy_interval = std::clamp(ms * 0.01f, 0.5f, 5.0f);
}

static const char* EnemyStateText(int st) {
    switch (st) {
        case ES_UNTRIED:   return "NOT USED YET";
        case ES_READY:     return "READY";
        case ES_NO_NATIVE: return "OFF, GAME LOOKUP MISSING";
        case ES_BAD_CODE:  return "OFF, GAME CODE LOOKS DIFFERENT";
        case ES_BAD_TABLE: return "ACTOR TABLE NOT READABLE, RETRYING";
        case ES_NO_MATCH:  return "COULD NOT CONFIRM TABLE, RETRYING";
        case ES_FAULTS:    return "OFF, TOO MANY READ FAULTS";
        default:           return "UNKNOWN";
    }
}

// ---------------------------------------------------------------- controller (XInput, loaded on demand)

struct XState {
    DWORD packet;
    WORD  buttons;
    BYTE  lt, rt;
    SHORT lx, ly, rx, ry;
};
typedef DWORD (WINAPI *XInputGetStateFn)(DWORD, XState*);

struct PadState {
    bool  connected = false;
    WORD  buttons = 0;
    BYTE  lt = 0, rt = 0;
    SHORT lx = 0, ly = 0;
    const char* source = "NOT FOUND";
};

// Button bits follow the XInput layout; PlayStation pads are translated to it.
constexpr WORD PAD_DPAD_UP     = 0x0001;
constexpr WORD PAD_LEFT_THUMB  = 0x0040;
constexpr WORD PAD_RIGHT_THUMB = 0x0080;
constexpr WORD PAD_LB          = 0x0100;
constexpr WORD PAD_RB          = 0x0200;
constexpr WORD PAD_A           = 0x1000;   // cross
constexpr WORD PAD_B           = 0x2000;   // circle

// ---- PlayStation controllers (DualSense, DualShock 4), read straight from the HID device.
// The game reads these pads natively, so XInput never sees them unless Steam Input or
// DS4Windows is turned on. A separate thread reads the pad and shares the latest state.
// The pad is opened read only and shared, so the game keeps working normally.

enum HidKind { HK_DUALSENSE, HK_DS4 };

struct HidShared {
    std::mutex  m;
    bool        connected = false;
    ULONGLONG   stamp = 0;
    PadState    st;
};
static HidShared g_hid;

static bool ParseSonyReport(HidKind kind, const uint8_t* b, DWORD n, PadState& out, const char* name) {
    if (n < 10) return false;
    int lx, ly, l2, r2, b0, b1;
    if (kind == HK_DUALSENSE) {
        if (b[0] == 0x01 && n >= 64)      { lx = b[1]; ly = b[2]; l2 = b[5]; r2 = b[6]; b0 = b[8];  b1 = b[9]; }    // USB
        else if (b[0] == 0x31 && n >= 11) { lx = b[2]; ly = b[3]; l2 = b[6]; r2 = b[7]; b0 = b[9];  b1 = b[10]; }   // Bluetooth, full
        else if (b[0] == 0x01)            { lx = b[1]; ly = b[2]; b0 = b[5]; b1 = b[6]; l2 = b[8]; r2 = b[9]; }     // Bluetooth, simple
        else return false;
    } else {
        if (b[0] == 0x01 && n >= 64)      { lx = b[1]; ly = b[2]; b0 = b[5]; b1 = b[6]; l2 = b[8]; r2 = b[9]; }     // USB
        else if (b[0] == 0x11 && n >= 12) { lx = b[3]; ly = b[4]; b0 = b[7]; b1 = b[8]; l2 = b[10]; r2 = b[11]; }   // Bluetooth, full
        else if (b[0] == 0x01)            { lx = b[1]; ly = b[2]; b0 = b[5]; b1 = b[6]; l2 = b[8]; r2 = b[9]; }     // Bluetooth, simple
        else return false;
    }

    WORD btn = 0;
    int hat = b0 & 0x0F;
    if (hat == 7 || hat == 0 || hat == 1) btn |= PAD_DPAD_UP;
    if (b0 & 0x20) btn |= PAD_A;          // cross
    if (b0 & 0x40) btn |= PAD_B;          // circle
    if (b1 & 0x01) btn |= PAD_LB;         // L1
    if (b1 & 0x02) btn |= PAD_RB;         // R1
    if (b1 & 0x40) btn |= PAD_LEFT_THUMB; // L3
    if (b1 & 0x80) btn |= PAD_RIGHT_THUMB;// R3

    out.connected = true;
    out.buttons = btn;
    out.lt = (BYTE)l2;
    out.rt = (BYTE)r2;
    out.lx = (SHORT)std::clamp((lx - 128) * 256, -32767, 32767);
    out.ly = (SHORT)std::clamp((128 - ly) * 256, -32767, 32767);   // stick up is a small number in HID
    out.source = name;
    return true;
}

static bool SonyKindFromPath(const std::string& lower, HidKind& kind, const char*& name) {
    if (lower.find("vid_054c") == std::string::npos) return false;
    if (lower.find("pid_0ce6") != std::string::npos || lower.find("pid_0df2") != std::string::npos) {
        kind = HK_DUALSENSE; name = "DUALSENSE"; return true;
    }
    if (lower.find("pid_05c4") != std::string::npos || lower.find("pid_09cc") != std::string::npos
        || lower.find("pid_0ba0") != std::string::npos) {
        kind = HK_DS4; name = "DUALSHOCK 4"; return true;
    }
    return false;
}

static HANDLE OpenSonyPad(HidKind& kind, const char*& name) {
    // The HID device interface class.
    static const GUID kHidGuid = { 0x4D1E55B2, 0xF16F, 0x11CF, { 0x88, 0xCB, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30 } };
    HDEVINFO set = SetupDiGetClassDevsA(&kHidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;

    HANDLE found = INVALID_HANDLE_VALUE;
    SP_DEVICE_INTERFACE_DATA ifd;
    ifd.cbSize = sizeof(ifd);
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(set, nullptr, &kHidGuid, i, &ifd); i++) {
        DWORD need = 0;
        SetupDiGetDeviceInterfaceDetailA(set, &ifd, nullptr, 0, &need, nullptr);
        if (need < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A) || need > 4096) continue;

        std::vector<char> mem(need, 0);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_A*>(mem.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);
        if (!SetupDiGetDeviceInterfaceDetailA(set, &ifd, detail, need, nullptr, nullptr)) continue;

        std::string path = detail->DevicePath;
        std::string lower = path;
        for (auto& c : lower) c = (char)tolower((unsigned char)c);
        HidKind k; const char* nm = nullptr;
        if (!SonyKindFromPath(lower, k, nm)) continue;

        HANDLE h = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                               OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (h == INVALID_HANDLE_VALUE) continue;
        kind = k;
        name = nm;
        found = h;
        break;
    }
    SetupDiDestroyDeviceInfoList(set);
    return found;
}

static void HidThread() {
    for (;;) {
        try {
            HidKind kind = HK_DUALSENSE;
            const char* name = "PLAYSTATION PAD";
            HANDLE h = OpenSonyPad(kind, name);
            if (h == INVALID_HANDLE_VALUE) {
                Sleep(2000);
                continue;
            }
            Log("controller: opened %s through HID", name);

            HANDLE ev = CreateEventA(nullptr, TRUE, FALSE, nullptr);
            uint8_t buf[1024];
            int quiet = 0;
            bool first = true;
            while (ev) {
                OVERLAPPED ov = {};
                ov.hEvent = ev;
                ResetEvent(ev);
                DWORD got = 0;
                if (!ReadFile(h, buf, sizeof(buf), nullptr, &ov) && GetLastError() != ERROR_IO_PENDING) break;
                DWORD w = WaitForSingleObject(ev, 1000);
                if (w == WAIT_TIMEOUT) {
                    CancelIoEx(h, &ov);
                    GetOverlappedResult(h, &ov, &got, TRUE);
                    if (++quiet >= 5) break;     // nothing for five seconds: reopen
                    continue;
                }
                if (!GetOverlappedResult(h, &ov, &got, FALSE)) break;
                quiet = 0;

                PadState ps;
                if (ParseSonyReport(kind, buf, got, ps, name)) {
                    if (first) { first = false; Log("controller: first report, id %02X, %lu bytes", buf[0], (unsigned long)got); }
                    std::lock_guard<std::mutex> lock(g_hid.m);
                    g_hid.st = ps;
                    g_hid.connected = true;
                    g_hid.stamp = GetTickCount64();
                }
            }
            if (ev) CloseHandle(ev);
            CloseHandle(h);
            {
                std::lock_guard<std::mutex> lock(g_hid.m);
                g_hid.connected = false;
            }
            Log("controller: %s closed", name);
            Sleep(1000);
        } catch (...) {
            Sleep(2000);
        }
    }
}

// ---- XInput (Xbox pads, and anything Steam Input or DS4Windows makes look like one)

static PadState PollPad(float dt) {
    static bool tried = false;
    static XInputGetStateFn fn = nullptr;
    static int index = -1;
    static float rescan = 0.0f;

    PadState ps;
    if (!tried) {
        tried = true;
        HMODULE m = LoadLibraryA("xinput1_4.dll");
        if (!m) m = LoadLibraryA("xinput1_3.dll");
        if (!m) m = LoadLibraryA("xinput9_1_0.dll");
        if (m) fn = reinterpret_cast<XInputGetStateFn>(GetProcAddress(m, "XInputGetState"));
    }

    XState st = {};
    auto fill = [&]() {
        ps.connected = true;
        ps.buttons = st.buttons;
        ps.lt = st.lt;
        ps.rt = st.rt;
        ps.lx = st.lx;
        ps.ly = st.ly;
        ps.source = "XINPUT";
    };

    if (fn) {
        if (index >= 0) {
            if (fn((DWORD)index, &st) == ERROR_SUCCESS) { fill(); return ps; }
            index = -1;
        }

        // Looking for a pad is slow when none is plugged in, so only once a second.
        rescan -= dt;
        if (rescan <= 0.0f) {
            rescan = 1.0f;
            for (int i = 0; i < 4; i++) {
                if (fn((DWORD)i, &st) == ERROR_SUCCESS) {
                    index = i;
                    fill();
                    return ps;
                }
            }
        }
    }

    // No XInput pad: use a PlayStation pad if the HID thread has one.
    {
        std::lock_guard<std::mutex> lock(g_hid.m);
        if (g_hid.connected && GetTickCount64() - g_hid.stamp < 1500) ps = g_hid.st;
    }
    return ps;
}

// ---------------------------------------------------------------- diagnostics

static void AddLine(char (*out)[96], int& n, int max_lines, const char* fmt, ...) {
    if (n >= max_lines) return;
    va_list args;
    va_start(args, fmt);
    vsnprintf(out[n], 96, fmt, args);
    va_end(args);
    out[n][95] = '\0';
    n++;
}

static const char* OkMissing(bool ok) { return ok ? "OK" : "MISSING"; }

int StatusLines(char (*out)[96], int max_lines) {
    int n = 0;

    Actor* hero = SafeGetHero();
    float cur = 0.0f, mx = 0.0f;
    Vector3 p;
    Health* h = hero ? SafeHealthPtr(hero) : nullptr;
    bool have_hp = h && SafeGetHealth(h, &cur, &mx);
    bool have_pos = hero && SafeGetPos(hero, &p) && IsSane(p);

    AddLine(out, n, max_lines, "HERO: %s", hero ? "FOUND" : "NOT FOUND, LOAD INTO THE GAME");
    if (have_hp) AddLine(out, n, max_lines, "HEALTH: %.0f OF %.0f", cur, mx);
    else         AddLine(out, n, max_lines, "HEALTH: NOT AVAILABLE");
    if (have_pos) AddLine(out, n, max_lines, "POSITION: %.0f %.0f %.0f", p.x, p.y, p.z);
    else          AddLine(out, n, max_lines, "POSITION: NOT AVAILABLE");

    AddLine(out, n, max_lines, "HERO SYSTEM: %s   ACTOR LOOKUP: %s",
            OkMissing(Native::HeroSystem::Instance != nullptr), OkMissing(Native::Actor::GetActor != nullptr));
    AddLine(out, n, max_lines, "HEALTH COMPONENT: %s", OkMissing(ComponentsReady()));

    AddLine(out, n, max_lines, "FLY: %s   CONTROLLER: %s", g.fly_enabled ? "ON" : "OFF",
            g_pad_connected.load() ? g_pad_source.load() : "NOT FOUND");

    long ticks = g_fly_ticks.load(), miss = g_fly_mismatch.load();
    if (ticks < 20)            AddLine(out, n, max_lines, "FLY CHECK: NOT USED YET");
    else if (miss * 2 > ticks) AddLine(out, n, max_lines, "FLY CHECK: GAME KEEPS MOVING YOU BACK");
    else                       AddLine(out, n, max_lines, "FLY CHECK: OK");

    AddLine(out, n, max_lines, "SWING SPEED: %s   GAME SPEED %.0f   BOOSTS %ld",
            kSwingNames[std::clamp(g.swing_idx, 0, SWING_COUNT - 1)], g_swing_speed.load(), g_swing_boosts.load());

    {
        int st = g_enemy_state.load();
        float dm = DamageTakenMult(), hm = EnemyHealthMult();
        AddLine(out, n, max_lines, "ENEMY DAMAGE %.2gX   ENEMY HEALTH %.2gX", dm, hm);
        if (st == ES_UNTRIED && std::fabs(hm - 1.0f) < 0.0001f)
            AddLine(out, n, max_lines, "ENEMY HEALTH TOOL: NOT USED YET (1X)");
        else
            AddLine(out, n, max_lines, "ENEMY HEALTH TOOL: %s", EnemyStateText(st));
        if (st == ES_READY)
            AddLine(out, n, max_lines, "ENEMIES SCALED: %d   ACTORS SEEN: %d   SCAN %.1f MS",
                    g_enemy_tracked.load(), g_enemy_actors.load(), g_enemy_scan_ms.load());
        if (st == ES_READY)
            AddLine(out, n, max_lines, "WRITES KEPT: %ld   REFUSED BY GAME: %ld   GAME RESET: %ld",
                    g_enemy_scaled_total.load(), g_enemy_refused.load(), g_enemy_rebased.load());
    }
    AddLine(out, n, max_lines, "CAUGHT FAULTS: %ld", g_faults.load());
    AddLine(out, n, max_lines, "CRASHES THIS SESSION: %d", crash::Count());
    if (crash::ThisSession()[0]) AddLine(out, n, max_lines, "NOW: %s", crash::ThisSession());
    if (crash::Previous()[0])    AddLine(out, n, max_lines, "LAST: %s", crash::Previous());
    else                         AddLine(out, n, max_lines, "LAST CRASH: NONE RECORDED");
    AddLine(out, n, max_lines, "LAST ACTION: %s", g_action);
    return n;
}

void WriteReport() {
    crash::context = "menu: write report";
    static char lines[24][96];
    int n = StatusLines(lines, 24);
    try {
        std::ofstream out(DataDir() + "diagnostics.txt", std::ios::trunc);
        out << NAME << " v" << VERSION << "\n\n";
        for (int i = 0; i < n; i++) out << lines[i] << '\n';
        out << "\nFiles to send if something is wrong:\n";
        out << "  SyncsToolkit\\diagnostics.txt\n";
        out << "  SyncsToolkit\\crash_report.txt and crash_report_old.txt\n";
        out << "  SyncsToolkit\\log.txt and log_prev.txt\n";
        SetAction("SAVED DIAGNOSTICS.TXT");
    } catch (...) {
        SetAction("COULD NOT WRITE DIAGNOSTICS");
    }
}

void ClearCrashLog() {
    crash::Clear();
    SetAction("CRASH LOG CLEARED");
}

// ---------------------------------------------------------------- worker thread

static bool Down(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

struct KeyEdge {
    bool prev[256] = {};
    bool Pressed(int vk) {
        bool d = Down(vk);
        bool p = d && !prev[vk & 0xFF];
        prev[vk & 0xFF] = d;
        return p;
    }
    // Remember what is held right now without reporting it as a press. Used when
    // the game window regains focus so a key that was already down does not fire.
    void Prime() {
        for (int vk = 1; vk < 256; vk++) prev[vk] = Down(vk);
    }
};

static void HandleHotkeys(KeyEdge& keys) {
    if (keys.Pressed(VK_END)) Panic();           // always works
    if (!g.hotkeys) return;

    if (keys.Pressed('R'))      ToggleFly();
    if (keys.Pressed(VK_F5))    { g.god_mode = !g.god_mode; SetAction("GOD MODE %s", g.god_mode ? "ON" : "OFF"); }
    if (keys.Pressed(VK_F6))    CycleSwingSpeed();
    if (keys.Pressed(VK_F7))    LogPosition("manual");
    if (keys.Pressed(VK_HOME))  Jump(JUMP_FORWARD);
    if (keys.Pressed(VK_F10))   UndoJump();
    if (keys.Pressed(VK_INSERT)) g.jump_idx = std::min(g.jump_idx + 1, JUMP_COUNT - 1);
    if (keys.Pressed(VK_DELETE)) g.jump_idx = std::max(g.jump_idx - 1, 0);
    if (keys.Pressed(VK_PRIOR))  g.fly_speed = std::min(g.fly_speed * 1.25f, 1000.0f);
    if (keys.Pressed(VK_NEXT))   g.fly_speed = std::max(g.fly_speed / 1.25f, 1.0f);
}

static void Worker() {
    using clock = std::chrono::steady_clock;
    auto last = clock::now();

    KeyEdge keys;
    Vector3 fly_pos, fly_vel;
    bool was_flying = false;
    float t30 = 0.0f;
    long faults_logged = 0;
    float dpad_hold = 0.0f;
    bool dpad_fired = false;
    bool dpad_prev = false;
    bool combo_prev = false;
    bool had_focus = false;

    // swing speed
    Vector3 sw_last;
    bool    sw_have = false;
    float   sw_acc_d = 0.0f, sw_acc_t = 0.0f, sw_speed = 0.0f;

    while (true) {
        Sleep(8);
        try {
            crash::context = "worker: timing";
            auto now = clock::now();
            float dt = std::chrono::duration<float>(now - last).count();
            last = now;
            dt = std::min(dt, 0.1f);

            long faults = g_faults.load();
            if (faults != faults_logged) {
                faults_logged = faults;
                Log("caught %ld access fault(s) so far (a crash was prevented)", faults);
            }

            float grace = g_fly_grace.load();
            if (grace > 0.0f) g_fly_grace.store(std::max(0.0f, grace - dt));

            if (!GameFocused()) {
                fly_vel = Vector3();
                had_focus = false;
                continue;
            }

            crash::context = "worker: input";
            PadState pad = PollPad(dt);
            g_pad_connected = pad.connected;
            g_pad_source = pad.source;
            bool dpad_now = pad.connected && (pad.buttons & PAD_DPAD_UP) != 0;
            bool combo_now = pad.connected && (pad.buttons & (PAD_LEFT_THUMB | PAD_RIGHT_THUMB)) == (PAD_LEFT_THUMB | PAD_RIGHT_THUMB);

            if (!had_focus) {
                // Just came back to the game: a key or button that was already down is not a new press.
                had_focus = true;
                keys.Prime();
                dpad_prev = dpad_now;
                combo_prev = combo_now;
                dpad_hold = 0.0f;
                dpad_fired = dpad_now;
                sw_have = false;
            }

            HandleHotkeys(keys);

            // Controller fly button.
            //   TAP:  one tap on D-pad up switches fly on or off.
            //   HOLD: hold D-pad up for half a second (safer while scrolling menus with the D-pad).
            //   BOTH STICKS: click the left and right stick together. Menus never use that,
            //                so it cannot flip fly while you scroll.
            if (g.pad_fly == 1) {
                if (dpad_now && !dpad_prev) ToggleFly();
            } else if (g.pad_fly == 3) {
                if (combo_now && !combo_prev) ToggleFly();
            } else if (g.pad_fly == 2) {
                if (dpad_now) {
                    dpad_hold += dt;
                    if (!dpad_fired && dpad_hold >= 0.5f) {
                        dpad_fired = true;
                        ToggleFly();
                    }
                } else {
                    dpad_hold = 0.0f;
                    dpad_fired = false;
                }
            }
            if (g.pad_fly != 2) { dpad_hold = 0.0f; dpad_fired = dpad_now; }
            dpad_prev = dpad_now;
            combo_prev = combo_now;

            crash::context = "worker: find hero";
            Actor* hero = SafeGetHero();
            Vector3 cur_pos;
            if (!hero || !SafeGetPos(hero, &cur_pos) || !IsSane(cur_pos)) {
                fly_vel = Vector3();
                was_flying = false;
                g_hero_have_last = false;
                g_swing_resync = true;
                // Enemy records are kept on purpose: the hero can vanish for a moment
                // (loading, swapping heroes) while scaled enemies are still around.
                // Records for actors that are really gone age out by themselves.
                continue;
            }

            // ---- fly mode
            if (g.fly_enabled) {
                crash::context = "worker: fly";
                if (!was_flying || g_fly_resync.exchange(false)) {
                    fly_pos = cur_pos;
                    fly_vel = Vector3();
                } else {
                    // Did the game accept the position we wrote last tick?
                    Vector3 d = cur_pos - fly_pos;
                    float dist = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
                    g_fly_ticks++;
                    if (dist > 2.0f) g_fly_mismatch++;
                    // The game moved the hero by itself (loading, cutscene, respawn):
                    // follow it instead of yanking the hero back.
                    if (dist > 150.0f) {
                        fly_pos = cur_pos;
                        fly_vel = Vector3();
                    }
                }
                was_flying = true;

                // Input from keyboard and controller.
                bool kw = Down('W'), ka = Down('A'), ks = Down('S'), kd = Down('D');
                float key_fwd  = (kw ? 1.0f : 0.0f) - (ks ? 1.0f : 0.0f);
                float key_side = (kd ? 1.0f : 0.0f) - (ka ? 1.0f : 0.0f);
                float key_up   = (Down('Y') ? 1.0f : 0.0f) - (Down(VK_LCONTROL) ? 1.0f : 0.0f);
                bool  boost    = Down(VK_LSHIFT);

                float stick_x = 0.0f, stick_y = 0.0f;
                if (pad.connected) {
                    stick_x = pad.lx / 32767.0f;
                    stick_y = pad.ly / 32767.0f;
                    float mag = std::sqrt(stick_x * stick_x + stick_y * stick_y);
                    if (mag < 0.24f) { stick_x = 0.0f; stick_y = 0.0f; }
                    if (g.pad_updown == 1) {
                        key_up += ((pad.buttons & PAD_RB) ? 1.0f : 0.0f) - ((pad.buttons & PAD_LB) ? 1.0f : 0.0f);
                    } else if (g.pad_updown == 2) {
                        key_up += ((pad.buttons & PAD_A) ? 1.0f : 0.0f) - ((pad.buttons & PAD_B) ? 1.0f : 0.0f);
                    } else {
                        key_up += ((pad.rt > 30 ? pad.rt / 255.0f : 0.0f) - (pad.lt > 30 ? pad.lt / 255.0f : 0.0f));
                    }
                    if (pad.buttons & PAD_LEFT_THUMB) boost = true;
                }

                Heading hd = GetHeading(hero);
                float mx = 0.0f, mz = 0.0f;
                if (g.fly_steer == 0) {
                    // FOLLOW CAMERA: the game turns the hero toward whatever way you push
                    // (relative to the camera), so flying where the hero faces is flying
                    // where the camera points, with W, S, A, D or the stick.
                    float mag = std::sqrt(stick_x * stick_x + stick_y * stick_y);
                    float strength = (kw || ka || ks || kd) ? 1.0f : std::min(mag, 1.0f);
                    mx = hd.fx * strength;
                    mz = hd.fz * strength;
                } else {
                    float fwd  = std::clamp(key_fwd + stick_y, -1.0f, 1.0f);
                    float side = std::clamp(key_side + stick_x, -1.0f, 1.0f);
                    mx = hd.fx * fwd + hd.rx * side;
                    mz = hd.fz * fwd + hd.rz * side;
                }

                Vector3 dir(mx, std::clamp(key_up, -1.0f, 1.0f), mz);
                float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
                if (len > 1.0f) dir = dir * (1.0f / len);

                float speed = g.fly_speed * (boost ? g.fly_boost : 1.0f);
                Vector3 target = dir * speed;

                float blend = 1.0f - std::exp(-std::max(g.fly_accel, 0.1f) * dt);
                fly_vel = fly_vel + (target - fly_vel) * blend;

                Vector3 next = fly_pos + fly_vel * dt;
                if (!IsSane(next)) {
                    g.fly_enabled = 0;
                    Log("fly mode stopped: position left the safe range");
                } else {
                    fly_pos = next;
                    SafeSetPos(hero, fly_pos);
                }
            } else {
                if (was_flying) GiveLandingGrace();
                was_flying = false;
                fly_vel = Vector3();
            }

            // ---- swing speed
            // The game gives no speed setting we can reach, so this adds a share of the
            // distance the game just moved the hero. At 2X, every metre the game moves
            // you becomes two. It only acts when you are already moving fast (swinging,
            // gliding, falling fast), unless BOOST RUNNING TOO is on.
            {
                float sm = SwingMult();
                if (!g.fly_enabled && sm > 1.0001f) {
                    crash::context = "worker: swing";
                    if (!sw_have || g_swing_resync.exchange(false)) {
                        sw_last = cur_pos;
                        sw_have = true;
                        sw_acc_d = sw_acc_t = 0.0f;
                    } else {
                        Vector3 d = cur_pos - sw_last;      // what the game moved us since our last write
                        float dist = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
                        if (!std::isfinite(dist) || dist > 40.0f) {
                            sw_last = cur_pos;              // teleport, loading or respawn: start over
                            sw_acc_d = sw_acc_t = 0.0f;
                        } else {
                            sw_acc_d += dist;
                            sw_acc_t += dt;
                            if (sw_acc_t >= 0.12f) {
                                sw_speed = sw_acc_d / sw_acc_t;
                                g_swing_speed = sw_speed;
                                sw_acc_d = sw_acc_t = 0.0f;
                            }
                            float need = g.swing_all ? 4.0f : 14.0f;
                            if (dist > 0.001f && sw_speed >= need) {
                                float extra = std::min(dist * (sm - 1.0f), 6.0f);
                                Vector3 np = cur_pos + d * (extra / dist);
                                if (IsSane(np) && SafeSetPos(hero, np)) { sw_last = np; g_swing_boosts++; }
                                else sw_last = cur_pos;
                            } else {
                                sw_last = cur_pos;
                            }
                        }
                    }
                } else {
                    sw_have = false;
                }
            }

            // ---- health tools at about 30 Hz
            t30 += dt;
            if (t30 >= 0.033f) {
                crash::context = "worker: health";
                float step = t30;
                t30 = 0.0f;
                ProcessHero(hero, step);
            }

            // ---- enemy health scaling (throttled inside)
            ProcessEnemies(hero, cur_pos, dt);
        } catch (...) {
            g_faults++;
        }
    }
}

// Keep the log of the previous session: a crash is usually noticed after a relaunch.
static void RotateLog() {
    std::string dir = DataDir();
    MoveFileExA((dir + "log.txt").c_str(), (dir + "log_prev.txt").c_str(), MOVEFILE_REPLACE_EXISTING);
}

static void LogNativeStatus() {
    Log("%s v%s by %s", NAME, VERSION, AUTHOR);
    Log("natives: hero system %s, actor lookup %s, health component %s",
        OkMissing(Native::HeroSystem::Instance != nullptr),
        OkMissing(Native::Actor::GetActor != nullptr),
        OkMissing(ComponentsReady()));
}

void Start() {
    RotateLog();
    crash::Install(DataDir());
    LoadSettings();
    LoadSlots();
    LogNativeStatus();
    std::thread(Worker).detach();
    try { std::thread(HidThread).detach(); } catch (...) {}
}

} // namespace tk
